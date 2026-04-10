#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidEnemyPresetRegistry.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"

int32 URaidCombatSubsystem::ExecuteRoomSpawnPlan(ARaidRoomActor* Room, const TArray<FRaidEnemySpawnPlanEntry>& Plan, bool bFromPrewarm)
{
    UWorld* World = GetWorld();
    if (!World || !Room || Plan.Num() == 0)
    {
        return 0;
    }

    const URaidChapterConfig* Config = Room->GetChapterConfig();
    if (!Config || !Config->EnemyPresetRegistry)
    {
        return 0;
    }

    const int32 RoomId = Room->GetNodeId();
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
    FRandomStream RecoveryStream(Room->GetNodeRow().Seed ^ (RoomId * 97));

    int32 SpawnedCount = 0;
    for (int32 EntryIdx = 0; EntryIdx < Plan.Num(); ++EntryIdx)
    {
        const FRaidEnemySpawnPlanEntry& Entry = Plan[EntryIdx];
        if (!Entry.EnemyClass)
        {
            continue;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        APawn* SpawnedEnemy = World->SpawnActor<APawn>(Entry.EnemyClass, Entry.SpawnLocation, Entry.SpawnRotation, SpawnParams);
        if (!SpawnedEnemy && Entry.bForceSpawnIfColliding)
        {
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            SpawnedEnemy = World->SpawnActor<APawn>(Entry.EnemyClass, Entry.SpawnLocation, Entry.SpawnRotation, SpawnParams);
        }
        if (!SpawnedEnemy)
        {
            continue;
        }

        FVector EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
        const bool bBlockedNow =
            IsCapsuleBlockedForPawnInternal(World, EffectiveSpawnLoc, Entry.CapsuleRadius, Entry.CapsuleHalfHeight, SpawnedEnemy) ||
            IsNearRoomObstacleInternal(World, Room, EffectiveSpawnLoc, Entry.CapsuleRadius + 70.0f);
        if (bBlockedNow)
        {
            FVector RecoveryLoc = FVector::ZeroVector;
            if (TryResolveNearbyFallbackSpawnLocationInternal(World, Room, NavSys, EffectiveSpawnLoc, Entry.CapsuleRadius, Entry.CapsuleHalfHeight, RecoveryStream, RecoveryLoc))
            {
                SpawnedEnemy->SetActorLocation(RecoveryLoc, false, nullptr, ETeleportType::TeleportPhysics);
                EffectiveSpawnLoc = RecoveryLoc;
            }
            else if (!Entry.bForceSpawnIfColliding)
            {
                SpawnedEnemy->Destroy();
                continue;
            }
        }

        FString RepairLog;
        if (Config->EnemyPresetRegistry->TryRepairSpawnedPawn(SpawnedEnemy, RepairLog))
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Auto-repaired spawned enemy '%s': %s"),
                *GetNameSafe(SpawnedEnemy->GetClass()), *RepairLog);
        }

        // Final post-repair validation to prevent enemies from being born interpenetrating room meshes.
        // Repair logic can alter collision/capsule settings after the pre-spawn checks.
        float EffectiveCapsuleRadius = Entry.CapsuleRadius;
        float EffectiveCapsuleHalfHeight = Entry.CapsuleHalfHeight;
        ResolvePawnInstanceCapsuleSizeInternal(SpawnedEnemy, EffectiveCapsuleRadius, EffectiveCapsuleHalfHeight);

        EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
        bool bNavProjectionFailed = false;
        if (NavSys)
        {
            FNavLocation NavLoc;
            if (NavSys->ProjectPointToNavigation(EffectiveSpawnLoc, NavLoc, FVector(260.0f, 260.0f, 420.0f)))
            {
                const bool bFarFromNav = FVector::DistSquared2D(NavLoc.Location, EffectiveSpawnLoc) > FMath::Square(220.0f);
                if (bFarFromNav)
                {
                    SpawnedEnemy->SetActorLocation(NavLoc.Location, false, nullptr, ETeleportType::TeleportPhysics);
                    EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
                }
            }
            else
            {
                bNavProjectionFailed = true;
            }
        }

        bool bFinalBlocked =
            bNavProjectionFailed ||
            IsCapsuleBlockedForPawnInternal(World, EffectiveSpawnLoc, EffectiveCapsuleRadius, EffectiveCapsuleHalfHeight, SpawnedEnemy) ||
            IsNearRoomObstacleInternal(World, Room, EffectiveSpawnLoc, EffectiveCapsuleRadius + 70.0f);

        if (bFinalBlocked)
        {
            bool bRecovered = false;
            FVector RetrySeed = EffectiveSpawnLoc;
            for (int32 Retry = 0; Retry < 4; ++Retry)
            {
                FVector RecoveryLoc = FVector::ZeroVector;
                if (!TryResolveNearbyFallbackSpawnLocationInternal(
                    World,
                    Room,
                    NavSys,
                    RetrySeed,
                    EffectiveCapsuleRadius,
                    EffectiveCapsuleHalfHeight,
                    RecoveryStream,
                    RecoveryLoc))
                {
                    continue;
                }

                SpawnedEnemy->SetActorLocation(RecoveryLoc, false, nullptr, ETeleportType::TeleportPhysics);
                RetrySeed = RecoveryLoc;
                EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
                bFinalBlocked =
                    IsCapsuleBlockedForPawnInternal(World, EffectiveSpawnLoc, EffectiveCapsuleRadius, EffectiveCapsuleHalfHeight, SpawnedEnemy) ||
                    IsNearRoomObstacleInternal(World, Room, EffectiveSpawnLoc, EffectiveCapsuleRadius + 70.0f);
                if (!bFinalBlocked)
                {
                    bRecovered = true;
                    break;
                }
            }

            if (bFinalBlocked && !Entry.bForceSpawnIfColliding)
            {
                SpawnedEnemy->Destroy();
                continue;
            }

            if (bFinalBlocked && Entry.bForceSpawnIfColliding)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidCombat] Forced spawn kept while still near blockers. Room=%d Enemy='%s' Loc=%s"),
                    RoomId,
                    *GetNameSafe(SpawnedEnemy->GetClass()),
                    *EffectiveSpawnLoc.ToCompactString());
            }
            else if (bRecovered)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidCombat] Repositioned blocked spawn. Room=%d Enemy='%s' Loc=%s"),
                    RoomId,
                    *GetNameSafe(SpawnedEnemy->GetClass()),
                    *EffectiveSpawnLoc.ToCompactString());
            }
        }

        EnforceNoZombieGrabInternal(SpawnedEnemy);
        ForceEnemyTraceCollisionInternal(SpawnedEnemy);

        FString SanitizedProfile = Entry.BotProfile;
        SanitizedProfile.ReplaceInline(TEXT(" "), TEXT(""));
        SpawnEnemyControllerDeferred(SpawnedEnemy, RoomId, SanitizedProfile, EntryIdx);

        if (bEnableCombatPerfLogs)
        {
            LogEnemyTraceCollisionSnapshotInternal(SpawnedEnemy);
        }

        // Keep only one delayed refresh to reduce spawn-time timer bursts.
        TWeakObjectPtr<APawn> EnemyWeak = SpawnedEnemy;
        for (const float DelaySeconds : { 0.25f })
        {
            FTimerHandle RetryHandle;
            World->GetTimerManager().SetTimer(
                RetryHandle,
                FTimerDelegate::CreateWeakLambda(this, [EnemyWeak, this]()
                    {
                        if (APawn* EnemyStrong = EnemyWeak.Get())
                        {
                            ForceEnemyTraceCollisionInternal(EnemyStrong);
                        }
                    }),
                DelaySeconds,
                false);
        }

        SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidRoom_%d"), RoomId)));
        SpawnedEnemy->Tags.AddUnique(TEXT("Enemy"));
        SpawnedEnemy->Tags.AddUnique(TEXT("RaidEnemy"));
        if (!SanitizedProfile.IsEmpty())
        {
            SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("BotProfile_%s"), *SanitizedProfile)));
        }
        if (bFromPrewarm)
        {
            SpawnedEnemy->Tags.AddUnique(TEXT("RaidPrewarmedSpawn"));
        }

        EnemyToRoomMap.Add(SpawnedEnemy, RoomId);
        const TWeakObjectPtr<APawn> WeakSpawnedEnemy(SpawnedEnemy);
        const FVector SpawnedLoc = SpawnedEnemy->GetActorLocation();
        const double SpawnNow = World->GetTimeSeconds();
        const double RecoveryGraceUntil = SpawnNow + FMath::Max(0.0f, (double)EnemyRecoverySpawnGraceSeconds);
        const double ActivationTime = SpawnNow + FMath::Max(0.0f, EnemySearchStartDelay);
        EnemyLastKnownValidLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
        EnemyStuckLastProgressLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
        EnemyStuckLastProgressTimeByPawn.Add(WeakSpawnedEnemy, SpawnNow);
        EnemyUndergroundNextCheckByPawn.Add(WeakSpawnedEnemy, RecoveryGraceUntil);
        EnemyStuckNextCheckByPawn.Add(WeakSpawnedEnemy, RecoveryGraceUntil);
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakSpawnedEnemy);
        EnemyTrackedLastObservedLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
        EnemyTrackedNextPeriodicLogByPawn.Remove(WeakSpawnedEnemy);
        EnemySearchActivationTimeByPawn.Add(WeakSpawnedEnemy, ActivationTime);
        EnemySearchNextOrderTimeByPawn.Add(WeakSpawnedEnemy, ActivationTime + FMath::FRandRange(0.08, 0.55));
        SpawnedEnemy->OnDestroyed.AddDynamic(this, &URaidCombatSubsystem::OnEnemyDestroyed);
        LogTrackedEnemyState(SpawnedEnemy, RoomId, bFromPrewarm ? TEXT("SpawnedFromPrewarm") : TEXT("Spawned"));

        ++SpawnedCount;
    }

    return SpawnedCount;
}
