#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidEnemyPresetRegistry.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

bool URaidCombatSubsystem::BuildRoomSpawnPlan(ARaidRoomActor* Room, TArray<FRaidEnemySpawnPlanEntry>& OutPlan, bool bFromPrewarm, bool bLogResult)
{
    OutPlan.Reset();

    UWorld* World = GetWorld();
    if (!World || !Room)
    {
        return false;
    }

    const URaidChapterConfig* Config = Room->GetChapterConfig();
    if (!Config || !Config->EnemyPresetRegistry)
    {
        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] EnemyPresetRegistry missing while building spawn plan. Room=%d"), Room->GetNodeId());
        return false;
    }

    const FLevelNodeRow& Row = Room->GetNodeRow();
    const int32 RoomId = Room->GetNodeId();

    TArray<FName> PresetCandidates;
    BuildPresetCandidatesInternal(Row, PresetCandidates);

    FName EffectivePreset = NAME_None;
    for (const FName Candidate : PresetCandidates)
    {
        FRaidEnemyPreset FoundPreset;
        if (Config->EnemyPresetRegistry->ResolvePreset(Candidate, FoundPreset) && FoundPreset.IsValid())
        {
            EffectivePreset = Candidate;
            break;
        }
    }

    if (EffectivePreset.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] No valid enemy preset candidates for Room %d (requested '%s')."), RoomId, *Row.EnemyPreset);
        return false;
    }

    const bool bBossRoom = Row.RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase);
    const bool bCombatLike = IsCombatSpawnRoomTypeInternal(Row.RoomType);

    float SafeDifficulty = FMath::Clamp(Row.Difficulty > 0.01f ? Row.Difficulty : 1.0f, 0.6f, 3.0f);
    float SafeCombatWeight = FMath::Clamp(Row.CombatWeight > 0.01f ? Row.CombatWeight : 1.0f, 0.6f, 2.6f);
    int32 BaseSpawnCount = Row.SpawnCount > 0 ? Row.SpawnCount : 3;
    FString BotProfile = Row.BotProfile.TrimStartAndEnd();

    ApplyRoomConceptRule(Room, EffectivePreset, BotProfile, BaseSpawnCount, SafeDifficulty, SafeCombatWeight);

    int32 FinalSpawnCount = FMath::RoundToInt((float)BaseSpawnCount * (0.75f + SafeDifficulty * 0.45f) * (0.70f + SafeCombatWeight * 0.35f));
    if (bBossRoom) FinalSpawnCount = FMath::Max(2, FinalSpawnCount);
    else if (bCombatLike) FinalSpawnCount = FMath::Max(2, FinalSpawnCount);
    else FinalSpawnCount = FMath::Max(1, FinalSpawnCount);
    FinalSpawnCount = FMath::Clamp(FinalSpawnCount, 1, FMath::Max(1, MaxEnemiesPerRoom));

    FRandomStream Stream(Row.Seed ^ (RoomId * 7919));
    const FVector Center = Room->GetActorLocation();
    const float SpawnRadius = FMath::Max(420.0f, (Room->GridSize * Room->TileSize) / 2.0f - 200.0f);
    const float AngleStep = 360.0f / FMath::Max(1, FinalSpawnCount);
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);

    for (int32 i = 0; i < FinalSpawnCount; ++i)
    {
        FName SpawnPreset = EffectivePreset;
        if (bBossRoom && i > 0)
        {
            static const FName HelperCandidates[] =
            {
                TEXT("Raider"),
                TEXT("Scavenger"),
                TEXT("Sniper"),
                TEXT("Default")
            };

            for (const FName Candidate : HelperCandidates)
            {
                FRaidEnemyPreset FoundPreset;
                if (Config->EnemyPresetRegistry->ResolvePreset(Candidate, FoundPreset) && FoundPreset.IsValid())
                {
                    SpawnPreset = Candidate;
                    break;
                }
            }
        }

        TSubclassOf<APawn> EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(SpawnPreset);
        if (!EnemyClass)
        {
            for (const FName Candidate : PresetCandidates)
            {
                EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(Candidate);
                if (EnemyClass)
                {
                    SpawnPreset = Candidate;
                    break;
                }
            }
        }
        if (!EnemyClass)
        {
            continue;
        }

        float CapsuleRadius = 42.0f;
        float CapsuleHalfHeight = 88.0f;
        ResolvePawnCapsuleSizeInternal(EnemyClass, CapsuleRadius, CapsuleHalfHeight);

        float MinSpawnDistance = 300.0f;
        float MaxSpawnDistance = SpawnRadius;
        if (BotProfile.Equals(TEXT("Defensive"), ESearchCase::IgnoreCase))
        {
            MinSpawnDistance = FMath::Max(300.0f, SpawnRadius * 0.55f);
        }
        else if (BotProfile.Equals(TEXT("Tactical"), ESearchCase::IgnoreCase))
        {
            MinSpawnDistance = FMath::Max(300.0f, SpawnRadius * 0.42f);
            MaxSpawnDistance = FMath::Max(MinSpawnDistance + 50.0f, SpawnRadius * 0.88f);
        }
        else if (BotProfile.Equals(TEXT("Aggressive"), ESearchCase::IgnoreCase))
        {
            MaxSpawnDistance = FMath::Max(350.0f, SpawnRadius * 0.70f);
        }
        if (SpawnPreset == TEXT("Sniper"))
        {
            MinSpawnDistance = FMath::Max(MinSpawnDistance, SpawnRadius * 0.60f);
        }
        MaxSpawnDistance = FMath::Max(MinSpawnDistance + 50.0f, MaxSpawnDistance);

        const float BaseAngle = i * AngleStep;
        const float RandomAngle = BaseAngle + Stream.FRandRange(-25.0f, 25.0f);
        const float Distance = Stream.FRandRange(MinSpawnDistance, MaxSpawnDistance);

        FVector FinalSpawnLoc = FVector::ZeroVector;
        bool bFoundSafeSpawn = false;
        constexpr int32 MaxSpawnLocationAttempts = 10;
        for (int32 Attempt = 0; Attempt < MaxSpawnLocationAttempts && !bFoundSafeSpawn; ++Attempt)
        {
            const float CandidateAngle = RandomAngle + ((Attempt == 0) ? 0.0f : Stream.FRandRange(-95.0f, 95.0f));
            const float CandidateDistance = (Attempt == 0) ? Distance : Stream.FRandRange(MinSpawnDistance, MaxSpawnDistance);
            const float Radian = FMath::DegreesToRadians(CandidateAngle);
            const FVector Offset(FMath::Cos(Radian) * CandidateDistance, FMath::Sin(Radian) * CandidateDistance, 0.0f);
            const FVector XYCandidate = Center + Offset;

            if (TryResolveSafeAIPawnSpawnLocationInternal(World, Room, NavSys, XYCandidate, CapsuleRadius, CapsuleHalfHeight, FinalSpawnLoc))
            {
                bFoundSafeSpawn = true;
            }
        }

        if (!bFoundSafeSpawn)
        {
            FVector RecoveryLoc = FVector::ZeroVector;
            if (TryResolveNearbyFallbackSpawnLocationInternal(World, Room, NavSys, Center, CapsuleRadius, CapsuleHalfHeight, Stream, RecoveryLoc))
            {
                FinalSpawnLoc = RecoveryLoc;
                bFoundSafeSpawn = true;
            }
        }

        if (!bFoundSafeSpawn && bBossRoom)
        {
            FVector EmergencyLoc = Center;
            if (NavSys)
            {
                FNavLocation NavLoc;
                if (NavSys->ProjectPointToNavigation(Center + FVector(0.0f, 0.0f, 50.0f), NavLoc, FVector(600.0f, 600.0f, 1000.0f)))
                {
                    EmergencyLoc = NavLoc.Location;
                }
            }

            FHitResult EmergencyHit;
            if (TryResolveAIGroundHitInternal(World, Room, EmergencyLoc, EmergencyHit) && !IsWaterHitInternal(EmergencyHit))
            {
                EmergencyLoc = EmergencyHit.ImpactPoint;
            }
            FinalSpawnLoc = EmergencyLoc + FVector(0.0f, 0.0f, CapsuleHalfHeight + 10.0f);
            bFoundSafeSpawn = true;
        }

        if (!bFoundSafeSpawn)
        {
            continue;
        }

        FRaidEnemySpawnPlanEntry& Entry = OutPlan.AddDefaulted_GetRef();
        Entry.EnemyClass = EnemyClass;
        Entry.PresetId = SpawnPreset;
        Entry.SpawnLocation = FinalSpawnLoc;
        Entry.SpawnRotation = FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);
        Entry.BotProfile = BotProfile;
        Entry.CapsuleRadius = CapsuleRadius;
        Entry.CapsuleHalfHeight = CapsuleHalfHeight;
        Entry.bForceSpawnIfColliding = bBossRoom && i == 0;
    }

    if (bLogResult)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] %s spawn plan for Room %d built. Entries=%d"),
            bFromPrewarm ? TEXT("Prewarmed") : TEXT("Runtime"),
            RoomId,
            OutPlan.Num());
    }

    return OutPlan.Num() > 0;
}
