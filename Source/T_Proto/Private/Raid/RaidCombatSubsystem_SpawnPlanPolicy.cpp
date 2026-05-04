#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "GameFramework/Pawn.h"

void URaidCombatSubsystem::ApplyRoomConceptRule(
    const ARaidRoomActor* Room,
    FName& InOutPreset,
    FString& InOutBotProfile,
    int32& InOutSpawnCount,
    float& InOutDifficulty,
    float& InOutCombatWeight) const
{
    if (!bEnableRoomConceptRules || !IsValid(Room) || RoomConceptRules.Num() == 0)
    {
        return;
    }

    const FLevelNodeRow& Row = Room->GetNodeRow();
    const FString RoomType = Row.RoomType;
    const FString Role = Row.RoomRole;
    const FString Tags = Row.NodeTags;
    const FString ThemeMeta = (Row.Theme + TEXT(" ") + Row.EnvType);

    for (const FRaidRoomConceptRule& Rule : RoomConceptRules)
    {
        if (Rule.RoomId >= 0 && Rule.RoomId != Room->GetNodeId())
        {
            continue;
        }
        if (!Rule.MatchRoomType.IsEmpty() && !RoomType.Equals(Rule.MatchRoomType, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (!Rule.MatchRoomRoleContains.IsEmpty() && !Role.Contains(Rule.MatchRoomRoleContains, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (!Rule.MatchTagContains.IsEmpty() && !Tags.Contains(Rule.MatchTagContains, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (!Rule.MatchThemeContains.IsEmpty() && !ThemeMeta.Contains(Rule.MatchThemeContains, ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (!Rule.EnemyPresetOverride.IsNone())
        {
            InOutPreset = Rule.EnemyPresetOverride;
        }
        if (!Rule.BotProfileOverride.IsEmpty())
        {
            InOutBotProfile = Rule.BotProfileOverride;
        }
        if (Rule.SpawnCountOverride > 0)
        {
            InOutSpawnCount = Rule.SpawnCountOverride;
        }
        InOutDifficulty *= FMath::Clamp(Rule.DifficultyMultiplier, 0.10f, 6.0f);
        InOutCombatWeight *= FMath::Clamp(Rule.CombatWeightMultiplier, 0.10f, 6.0f);
        return;
    }
}

void URaidCombatSubsystem::PrepareUpcomingRoomSpawnPlans(ARaidRoomActor* ClearedRoom)
{
    if (!bEnableUpcomingRoomSpawnPrewarm || RoomById.Num() == 0)
    {
        return;
    }

    TArray<ARaidRoomActor*> Candidates;
    Candidates.Reserve(RoomById.Num());

    auto AddCandidateRoom = [&](ARaidRoomActor* CandidateRoom)
        {
            if (!IsValid(CandidateRoom) || CandidateRoom->IsCleared() || CandidateRoom->HasCombatStarted())
            {
                return;
            }
            if (!IsCombatSpawnRoomTypeInternal(CandidateRoom->GetNodeRow().RoomType))
            {
                return;
            }
            if (PrewarmedSpawnPlansByRoomId.Contains(CandidateRoom->GetNodeId()))
            {
                return;
            }
            Candidates.AddUnique(CandidateRoom);
        };

    if (bPrewarmConnectedRoomsOnly && IsValid(ClearedRoom))
    {
        for (const int32 ConnectedId : ClearedRoom->GetNodeRow().GetConnectionIds())
        {
            if (TObjectPtr<ARaidRoomActor>* RoomPtr = RoomById.Find(ConnectedId))
            {
                AddCandidateRoom(RoomPtr->Get());
            }
        }
    }

    if (Candidates.Num() == 0)
    {
        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
        {
            AddCandidateRoom(Pair.Value.Get());
        }
    }

    APawn* PlayerPawn = GetPrimaryPlayerPawn();
    if (IsValid(PlayerPawn))
    {
        const FVector PlayerLoc = PlayerPawn->GetActorLocation();
        Candidates.Sort([PlayerLoc](const ARaidRoomActor& A, const ARaidRoomActor& B)
            {
                return FVector::DistSquared2D(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared2D(B.GetActorLocation(), PlayerLoc);
            });
    }

    const int32 GlobalCap = FMath::Max(1, MaxPrewarmedRoomPlans);
    const int32 RefreshCap = FMath::Max(1, MaxPrewarmSpawnPlansPerRefresh);
    int32 BuiltCount = 0;

    for (ARaidRoomActor* Candidate : Candidates)
    {
        if (!IsValid(Candidate))
        {
            continue;
        }
        if (PrewarmedSpawnPlansByRoomId.Num() >= GlobalCap || BuiltCount >= RefreshCap)
        {
            break;
        }

        TArray<FRaidEnemySpawnPlanEntry> Plan;
        if (BuildRoomSpawnPlan(Candidate, Plan, true, false) && Plan.Num() > 0)
        {
            PrewarmedSpawnPlansByRoomId.Add(Candidate->GetNodeId(), MoveTemp(Plan));
            ++BuiltCount;
        }
    }

    if (bEnableCombatPerfLogs && BuiltCount > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("[RaidCombat] Prewarmed room spawn plans updated. Built=%d Cached=%d"), BuiltCount, PrewarmedSpawnPlansByRoomId.Num());
    }
}
