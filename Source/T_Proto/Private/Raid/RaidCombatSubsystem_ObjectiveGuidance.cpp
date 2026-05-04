#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

APawn* URaidCombatSubsystem::GetPrimaryPlayerPawn() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    const double NowSeconds = World->GetTimeSeconds();
    if (CachedPrimaryPlayerPawn.IsValid() && NextPrimaryPawnRefreshTimeSeconds > NowSeconds)
    {
        return CachedPrimaryPlayerPawn.Get();
    }

    APawn* ResolvedPawn = nullptr;
    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        APawn* ControlledPawn = PC->GetPawn();
        if (IsValid(ControlledPawn) && ControlledPawn->IsPlayerControlled())
        {
            ResolvedPawn = ControlledPawn;
        }
    }

    if (!IsValid(ResolvedPawn))
    {
        for (TActorIterator<APawn> It(World); It; ++It)
        {
            APawn* Pawn = *It;
            if (IsValid(Pawn) && Pawn->IsPlayerControlled())
            {
                ResolvedPawn = Pawn;
                break;
            }
        }
    }

    CachedPrimaryPlayerPawn = ResolvedPawn;
    NextPrimaryPawnRefreshTimeSeconds = NowSeconds + (IsValid(ResolvedPawn) ? 0.50 : 0.15);
    return ResolvedPawn;
}

ARaidRoomActor* URaidCombatSubsystem::FindStartRoom() const
{
    if (StartRoomId != -1)
    {
        if (const TObjectPtr<ARaidRoomActor>* Found = RoomById.Find(StartRoomId))
        {
            return Found->Get();
        }
    }

    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room) continue;
        if (Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            return Room;
        }
    }
    return nullptr;
}

bool URaidCombatSubsystem::IsPawnInsideRoomBounds2D(const APawn* Pawn, const ARaidRoomActor* Room) const
{
    if (!Pawn || !Room)
    {
        return false;
    }

    const FVector PawnLoc = Pawn->GetActorLocation();
    const FVector RoomLoc = Room->GetActorLocation();
    const FVector Extent = Room->GetRoomExtent();
    const float Padding = FMath::Max(0.0f, RoomInsideCheckPadding);

    return
        FMath::Abs(PawnLoc.X - RoomLoc.X) <= (Extent.X + Padding) &&
        FMath::Abs(PawnLoc.Y - RoomLoc.Y) <= (Extent.Y + Padding);
}

int32 URaidCombatSubsystem::ResolvePrimaryProgressionRoomId(const ARaidRoomActor* StartRoom) const
{
    if (const TObjectPtr<ARaidRoomActor>* RoomOnePtr = RoomById.Find(1))
    {
        ARaidRoomActor* RoomOne = RoomOnePtr->Get();
        if (RoomOne && !RoomOne->IsCleared() &&
            !RoomOne->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            return 1;
        }
    }

    if (!StartRoom)
    {
        return -1;
    }

    const FVector StartLoc = StartRoom->GetActorLocation();
    float BestDistSq = TNumericLimits<float>::Max();
    int32 BestId = -1;
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room || Room->IsCleared()) continue;
        if (Room == StartRoom) continue;
        if (Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) continue;

        const float DistSq = FVector::DistSquared2D(StartLoc, Room->GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestId = Pair.Key;
        }
    }
    return BestId;
}

void URaidCombatSubsystem::ForceObjectiveToRoom(ARaidRoomActor* Room, FName MarkerType)
{
    if (!Room)
    {
        return;
    }

    if (MarkerType != NAME_None)
    {
        AddPOI(Room->GetActorLocation(), MarkerType);
    }

    CurrentObjectiveRoomId = Room->GetNodeId();
    CurrentObjectiveLocation = Room->GetActorLocation();
    LastProgressTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    LastDistanceToObjective = TNumericLimits<float>::Max();
    WrongDirectionScore = 0.0f;
}

void URaidCombatSubsystem::AddNearbyOptionalPOIsFromStart(const ARaidRoomActor* StartRoom, int32 PrimaryRoomId)
{
    if (!StartRoom)
    {
        return;
    }

    const float MaxDistSq = FMath::Square(FMath::Max(1000.0f, StartOptionalPOIRadius));
    const FVector StartLoc = StartRoom->GetActorLocation();
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room || Room->IsCleared()) continue;
        if (Room->GetNodeId() == PrimaryRoomId) continue;
        if (Room == StartRoom) continue;
        if (Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) continue;

        const float DistSq = FVector::DistSquared2D(StartLoc, Room->GetActorLocation());
        if (DistSq <= MaxDistSq)
        {
            AddPOI(Room->GetActorLocation(), FName(*Room->GetNodeRow().RoomType));
        }
    }
}

void URaidCombatSubsystem::RefreshStartRoomProgressState(APawn* PlayerPawn)
{
    ARaidRoomActor* StartRoom = FindStartRoom();
    if (!StartRoom || !PlayerPawn)
    {
        return;
    }

    if (StartRoom->IsCleared())
    {
        bStartFlowInitialized = true;
        bStartPendingClearOnExit = false;
        return;
    }

    const bool bInsideStart = IsPawnInsideRoomBounds2D(PlayerPawn, StartRoom);
    if (!bStartFlowInitialized)
    {
        bStartFlowInitialized = true;
        bPlayerSpawnedInsideStartRoom = bInsideStart;
        bStartPendingClearOnExit = false;
    }

    // Start room is cleared on entry only.
    if (bInsideStart)
    {
        HandleRoomCleared(StartRoom->GetNodeId());
    }
}

void URaidCombatSubsystem::AddPOI(const FVector& Loc, FName Type)
{
    for (const FRaidPOI& Existing : ActivePOIs)
    {
        if (Existing.Location.Equals(Loc, 5.0f))
        {
            return;
        }
    }

    FRaidPOI NewPOI;
    NewPOI.Location = Loc;
    NewPOI.MarkerType = Type;
    ActivePOIs.Add(NewPOI);
}

void URaidCombatSubsystem::ClearPOIs()
{
    ActivePOIs.Empty();
    CurrentObjectiveRoomId = -1;
    CurrentObjectiveLocation = FVector::ZeroVector;
    LastDistanceToObjective = TNumericLimits<float>::Max();
}

void URaidCombatSubsystem::UpdateCompassForNextRooms(ARaidRoomActor* ClearedRoom)
{
    if (RoomById.Num() == 0)
    {
        ClearPOIs();
        return;
    }

    ClearPOIs();
    APawn* PlayerPawn = GetPrimaryPlayerPawn();
    if (PlayerPawn)
    {
        RefreshStartRoomProgressState(PlayerPawn);
    }

    ARaidRoomActor* StartRoom = FindStartRoom();
    if (StartRoom)
    {
        StartRoomId = StartRoom->GetNodeId();
    }

    bool bHasPendingBoss = false;
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room) continue;
        if (Room->GetNodeRow().RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase) && !Room->IsCleared())
        {
            bHasPendingBoss = true;
            break;
        }
    }

    const int32 PrimaryRoomId = ResolvePrimaryProgressionRoomId(StartRoom);
    ARaidRoomActor* PrimaryRoom = nullptr;
    if (PrimaryRoomId != -1)
    {
        if (TObjectPtr<ARaidRoomActor>* PrimaryPtr = RoomById.Find(PrimaryRoomId))
        {
            PrimaryRoom = PrimaryPtr->Get();
        }
    }

    // Start room flow:
    // - Spawned outside Start: objective must be Start until entered.
    // - Spawned inside Start: objective starts from primary room (ID 1 if valid).
    if (StartRoom && !StartRoom->IsCleared())
    {
        if (!bPlayerSpawnedInsideStartRoom)
        {
            ForceObjectiveToRoom(StartRoom, TEXT("Start"));
            return;
        }

        if (PrimaryRoom && !PrimaryRoom->IsCleared())
        {
            AddPOI(PrimaryRoom->GetActorLocation(), FName(*PrimaryRoom->GetNodeRow().RoomType));
            AddNearbyOptionalPOIsFromStart(StartRoom, PrimaryRoomId);
            ForceObjectiveToRoom(PrimaryRoom);
            return;
        }

        ForceObjectiveToRoom(StartRoom, TEXT("Start"));
        return;
    }

    // Even if optional rooms were cleared first, keep pulling objective back to primary room
    // until that primary progression room is cleared.
    if (PrimaryRoom && !PrimaryRoom->IsCleared())
    {
        AddPOI(PrimaryRoom->GetActorLocation(), FName(*PrimaryRoom->GetNodeRow().RoomType));
        AddNearbyOptionalPOIsFromStart(StartRoom, PrimaryRoomId);
        ForceObjectiveToRoom(PrimaryRoom);
        return;
    }

    if (ClearedRoom)
    {
        const TArray<int32> ConnectedIds = ClearedRoom->GetNodeRow().GetConnectionIds();
        for (int32 NextId : ConnectedIds)
        {
            if (TObjectPtr<ARaidRoomActor>* NextPtr = RoomById.Find(NextId))
            {
                if (ARaidRoomActor* NextRoom = NextPtr->Get())
                {
                    if (NextRoom->IsCleared()) continue;
                    FName RoomType = FName(*NextRoom->GetNodeRow().RoomType);
                    AddPOI(NextRoom->GetActorLocation(), RoomType);
                }
            }
        }

        int32 Neighbors[4] = { ClearedRoom->NeighborNorth, ClearedRoom->NeighborSouth, ClearedRoom->NeighborEast, ClearedRoom->NeighborWest };
        for (int32 NextId : Neighbors)
        {
            if (NextId == -1) continue;
            if (TObjectPtr<ARaidRoomActor>* NextPtr = RoomById.Find(NextId))
            {
                if (ARaidRoomActor* NextRoom = NextPtr->Get())
                {
                    if (NextRoom->IsCleared()) continue;
                    bool bAlreadyExists = false;
                    for (const FRaidPOI& ExistingPOI : ActivePOIs)
                    {
                        if (ExistingPOI.Location == NextRoom->GetActorLocation())
                        {
                            bAlreadyExists = true;
                            break;
                        }
                    }

                    if (!bAlreadyExists)
                    {
                        FName RoomType = FName(*NextRoom->GetNodeRow().RoomType);
                        AddPOI(NextRoom->GetActorLocation(), RoomType);
                    }
                }
            }
        }
    }
    else
    {
        // 초기 생성 직후(아직 클리어된 방이 없는 상태)에도 목표 마커를 세팅한다.
        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
        {
            ARaidRoomActor* Room = Pair.Value.Get();
            if (!Room || Room->IsCleared()) continue;

            const FString RoomType = Room->GetNodeRow().RoomType;
            if (RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) continue;
            if (bHasPendingBoss && RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) continue;

            AddPOI(Room->GetActorLocation(), FName(*RoomType));
        }
    }

    int32 BestRoomId = -1;
    int32 BestScore = TNumericLimits<int32>::Min();
    FVector BestLocation = FVector::ZeroVector;

    for (const FRaidPOI& POI : ActivePOIs)
    {
        int32 CandidateId = -1;
        FString CandidateType = POI.MarkerType.ToString();

        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
        {
            if (!Pair.Value) continue;
            if (Pair.Value->GetActorLocation().Equals(POI.Location, 1.0f))
            {
                CandidateId = Pair.Key;
                CandidateType = Pair.Value->GetNodeRow().RoomType;
                break;
            }
        }

        int32 Score = GetRoomTypePriorityInternal(CandidateType);
        if (bHasPendingBoss && CandidateType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase))
        {
            Score -= 200;
        }
        if (Score > BestScore)
        {
            BestScore = Score;
            BestRoomId = CandidateId;
            BestLocation = POI.Location;
        }
    }

    CurrentObjectiveRoomId = BestRoomId;
    CurrentObjectiveLocation = BestLocation;
    if (CurrentObjectiveRoomId == -1 || CurrentObjectiveLocation.IsNearlyZero())
    {
        // Fallback: 연결 데이터가 비정상이거나 비어도 가장 우선순위 높은 방 하나를 목표로 잡는다.
        int32 FallbackScore = TNumericLimits<int32>::Min();
        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
        {
            ARaidRoomActor* Room = Pair.Value.Get();
            if (!Room) continue;
            if (ClearedRoom && Pair.Key == ClearedRoom->GetNodeId()) continue;

            const FString RoomType = Room->GetNodeRow().RoomType;
            int32 Score = GetRoomTypePriorityInternal(RoomType);
            if (bHasPendingBoss && RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase))
            {
                Score -= 200;
            }
            if (Score > FallbackScore)
            {
                FallbackScore = Score;
                CurrentObjectiveRoomId = Pair.Key;
                CurrentObjectiveLocation = Room->GetActorLocation();
            }
        }

        if (CurrentObjectiveRoomId != -1)
        {
            AddPOI(CurrentObjectiveLocation, TEXT("Fallback"));
            if (bEnableCombatPerfLogs)
            {
                UE_LOG(LogTemp, Display, TEXT("[RaidCombat] Compass fallback objective selected: Room %d"), CurrentObjectiveRoomId);
            }
        }
    }

    LastProgressTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    LastDistanceToObjective = TNumericLimits<float>::Max();
    WrongDirectionScore = 0.0f;
}

FRaidGuidanceSignal URaidCombatSubsystem::GetGuidanceSignalForPlayer(APawn* PlayerPawn)
{
    FRaidGuidanceSignal Signal;
    if (!PlayerPawn)
    {
        return Signal;
    }

    ReevaluateObjectiveByPlayer(PlayerPawn);
    if (CurrentObjectiveLocation.IsNearlyZero())
    {
        return Signal;
    }

    UWorld* World = GetWorld();
    if (!World) return Signal;

    const float Now = World->GetTimeSeconds();
    const FVector PlayerLoc = PlayerPawn->GetActorLocation();
    const float CurrentDist = FVector::Dist2D(PlayerLoc, CurrentObjectiveLocation);

    if (LastProgressTimeSeconds <= 0.0f)
    {
        LastProgressTimeSeconds = Now;
        LastDistanceToObjective = CurrentDist;
    }

    const float DistDelta = LastDistanceToObjective - CurrentDist;
    if (DistDelta > 120.0f)
    {
        LastProgressTimeSeconds = Now;
        WrongDirectionScore = FMath::Max(0.0f, WrongDirectionScore - 0.3f);
    }
    else if (DistDelta < -60.0f)
    {
        WrongDirectionScore = FMath::Min(1.5f, WrongDirectionScore + 0.2f);
    }

    LastDistanceToObjective = CurrentDist;

    const float StuckSeconds = FMath::Max(0.0f, Now - LastProgressTimeSeconds);
    const float GentleAlpha = FMath::Clamp(StuckSeconds / FMath::Max(1.0f, GentleNudgeDelay), 0.0f, 1.0f);
    const float StrongAlpha = FMath::Clamp((StuckSeconds - GentleNudgeDelay) / FMath::Max(1.0f, StrongNudgeDelay - GentleNudgeDelay), 0.0f, 1.0f);
    const float WrongAlpha = FMath::Clamp(WrongDirectionScore, 0.0f, 1.0f);

    Signal.bValid = true;
    Signal.TargetLocation = CurrentObjectiveLocation;
    Signal.Urgency = FMath::Clamp(FMath::Max3(GentleAlpha * 0.75f, StrongAlpha, WrongAlpha), 0.0f, 1.0f);
    Signal.bUseStrongCue = (Signal.Urgency >= 0.75f);

    if (Signal.Urgency < 0.35f) Signal.CueStyle = TEXT("Subtle");
    else if (Signal.Urgency < 0.75f) Signal.CueStyle = TEXT("Pulse");
    else Signal.CueStyle = TEXT("StrongPulse");

    return Signal;
}

void URaidCombatSubsystem::ReevaluateObjectiveByPlayer(APawn* PlayerPawn)
{
    if (!PlayerPawn) return;

    RefreshStartRoomProgressState(PlayerPawn);

    ARaidRoomActor* StartRoom = FindStartRoom();
    const int32 PrimaryRoomId = ResolvePrimaryProgressionRoomId(StartRoom);
    ARaidRoomActor* PrimaryRoom = nullptr;
    if (PrimaryRoomId != -1)
    {
        if (TObjectPtr<ARaidRoomActor>* PrimaryPtr = RoomById.Find(PrimaryRoomId))
        {
            PrimaryRoom = PrimaryPtr->Get();
        }
    }

    if (StartRoom && !StartRoom->IsCleared())
    {
        if (!bPlayerSpawnedInsideStartRoom)
        {
            const bool bHasStartPOI = ActivePOIs.ContainsByPredicate(
                [StartRoom](const FRaidPOI& POI)
                {
                    return POI.Location.Equals(StartRoom->GetActorLocation(), 5.0f);
                });
            if (CurrentObjectiveRoomId != StartRoom->GetNodeId() || !bHasStartPOI)
            {
                UpdateCompassForNextRooms(nullptr);
            }
            CurrentObjectiveRoomId = StartRoom->GetNodeId();
            CurrentObjectiveLocation = StartRoom->GetActorLocation();
            return;
        }

        if (PrimaryRoom && !PrimaryRoom->IsCleared())
        {
            const bool bHasPrimaryPOI = ActivePOIs.ContainsByPredicate(
                [PrimaryRoom](const FRaidPOI& POI)
                {
                    return POI.Location.Equals(PrimaryRoom->GetActorLocation(), 5.0f);
                });
            if (CurrentObjectiveRoomId != PrimaryRoom->GetNodeId() || !bHasPrimaryPOI)
            {
                UpdateCompassForNextRooms(nullptr);
            }
            CurrentObjectiveRoomId = PrimaryRoom->GetNodeId();
            CurrentObjectiveLocation = PrimaryRoom->GetActorLocation();
            return;
        }
    }

    if (PrimaryRoom && !PrimaryRoom->IsCleared())
    {
        const bool bHasPrimaryPOI = ActivePOIs.ContainsByPredicate(
            [PrimaryRoom](const FRaidPOI& POI)
            {
                return POI.Location.Equals(PrimaryRoom->GetActorLocation(), 5.0f);
            });
        if (CurrentObjectiveRoomId != PrimaryRoom->GetNodeId() || !bHasPrimaryPOI)
        {
            UpdateCompassForNextRooms(nullptr);
        }
        CurrentObjectiveRoomId = PrimaryRoom->GetNodeId();
        CurrentObjectiveLocation = PrimaryRoom->GetActorLocation();
        return;
    }

    bool bHasPendingBoss = false;
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room) continue;
        if (Room->GetNodeRow().RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase) && !Room->IsCleared())
        {
            bHasPendingBoss = true;
            break;
        }
    }

    const FVector PlayerLoc = PlayerPawn->GetActorLocation();
    int32 BestRoomId = -1;
    FVector BestLocation = FVector::ZeroVector;
    float BestUtility = -TNumericLimits<float>::Max();

    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room || Room->IsCleared()) continue;

        const FString RoomType = Room->GetNodeRow().RoomType;
        if (RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) continue;
        if (bHasPendingBoss && RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) continue;

        const float Utility = EvaluateObjectiveUtility(Room, PlayerLoc, bHasPendingBoss);
        if (Utility > BestUtility)
        {
            BestUtility = Utility;
            BestRoomId = Pair.Key;
            BestLocation = Room->GetActorLocation();
        }
    }

    if (BestRoomId != -1)
    {
        bool bShouldSwitch = (CurrentObjectiveRoomId != BestRoomId);
        if (CurrentObjectiveRoomId != -1 && bShouldSwitch && ObjectiveSwitchHysteresis > 0.0f)
        {
            if (TObjectPtr<ARaidRoomActor>* CurrentRoomPtr = RoomById.Find(CurrentObjectiveRoomId))
            {
                if (ARaidRoomActor* CurrentRoom = CurrentRoomPtr->Get())
                {
                    const float CurrentUtility = EvaluateObjectiveUtility(CurrentRoom, PlayerLoc, bHasPendingBoss);
                    if ((BestUtility - CurrentUtility) < ObjectiveSwitchHysteresis)
                    {
                        bShouldSwitch = false;
                    }
                }
            }
        }

        if (bShouldSwitch)
        {
            LastProgressTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
            LastDistanceToObjective = TNumericLimits<float>::Max();
            WrongDirectionScore = 0.0f;
            CurrentObjectiveRoomId = BestRoomId;
            CurrentObjectiveLocation = BestLocation;
        }
    }
}

float URaidCombatSubsystem::EvaluateObjectiveUtility(const ARaidRoomActor* Room, const FVector& PlayerLoc, bool bHasPendingBoss) const
{
    if (!Room) return -TNumericLimits<float>::Max();

    const FLevelNodeRow& Row = Room->GetNodeRow();
    const FString RoomType = Row.RoomType;

    // 1) Proximity utility
    const float DistUU = FVector::Dist2D(PlayerLoc, Room->GetActorLocation());
    const float DistNorm = FMath::Clamp(DistUU / 120000.0f, 0.0f, 1.0f);
    const float ProximityUtility = 1.0f - DistNorm;

    // 2) Value utility (boss/loot/combat)
    float ValueUtility = 0.25f;
    if (RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase)) ValueUtility = 1.0f;
    else if (RoomType.Equals(TEXT("Loot"), ESearchCase::IgnoreCase)) ValueUtility = 0.85f;
    else if (RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase) || RoomType.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) ValueUtility = 0.65f;
    else if (RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) ValueUtility = bHasPendingBoss ? 0.0f : 0.55f;

    // 3) Safety utility (inverse of risk)
    const float RawRisk = Row.Difficulty * 0.45f + Row.CombatWeight * 0.35f + (float)FMath::Max(0, Row.SpawnCount) * 0.07f;
    const float RiskNorm = FMath::Clamp(RawRisk / 3.0f, 0.0f, 1.0f);
    const float SafetyUtility = 1.0f - RiskNorm;

    const float Utility =
        (ProximityUtility * ObjectiveProximityWeight * 1000.0f) +
        (ValueUtility * ObjectiveValueWeight * 900.0f) +
        (SafetyUtility * ObjectiveSafetyWeight * 700.0f);

    return Utility;
}

float URaidCombatSubsystem::GetRoomUtility(const ARaidRoomActor* Room, const FVector& PlayerLoc, bool bHasPendingBoss) const
{
    return EvaluateObjectiveUtility(Room, PlayerLoc, bHasPendingBoss);
}
