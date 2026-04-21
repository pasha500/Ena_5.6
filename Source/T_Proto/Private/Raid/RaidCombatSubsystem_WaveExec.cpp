#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidEnemyPresetRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

void URaidCombatSubsystem::SpawnWaveNow(int32 WaveNumber, APawn* PlayerPawn)
{
    if (!bEnableDynamicWaves || !IsValid(PlayerPawn))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || WaveNumber <= 0 || WaveNumber > FMath::Max(1, TotalDynamicWaves))
    {
        return;
    }

    if (PostSpawnHeavyTaskCooldown > 0.0f)
    {
        const double DeferUntil = World->GetTimeSeconds() + (double)FMath::Max(0.0f, PostSpawnHeavyTaskCooldown);
        RecentSpawnHeavyWorkDeferUntilSeconds = FMath::Max(RecentSpawnHeavyWorkDeferUntilSeconds, DeferUntil);
    }

    const URaidChapterConfig* Config = nullptr;
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
    {
        const ARaidRoomActor* Room = Pair.Value.Get();
        if (IsValid(Room) && Room->GetChapterConfig() && Room->GetChapterConfig()->EnemyPresetRegistry)
        {
            Config = Room->GetChapterConfig();
            break;
        }
    }
    if (!Config || !Config->EnemyPresetRegistry)
    {
        return;
    }

    FRaidWaveDefinition WaveDef;
    if (WaveDefinitions.IsValidIndex(WaveNumber - 1))
    {
        WaveDef = WaveDefinitions[WaveNumber - 1];
    }
    else if (WaveDefinitions.Num() > 0)
    {
        WaveDef = WaveDefinitions.Last();
    }
    else
    {
        FRaidWaveSpawnEntry DefaultEntry;
        DefaultEntry.EnemyPreset = TEXT("Default");
        DefaultEntry.SpawnCount = 5;
        WaveDef.Entries.Add(DefaultEntry);
    }

    if (WaveDef.Entries.Num() == 0)
    {
        FRaidWaveSpawnEntry FallbackEntry;
        FallbackEntry.EnemyPreset = TEXT("Default");
        FallbackEntry.SpawnCount = 5;
        WaveDef.Entries.Add(FallbackEntry);
    }

    const int32 WaveRoomId = MakeWaveRoomIdInternal(WaveNumber);
    int32 SpawnedCount = 0;
    int32 SpawnOrder = 0;

    for (const FRaidWaveSpawnEntry& WaveEntry : WaveDef.Entries)
    {
        TSubclassOf<APawn> EntryEnemyClass = nullptr;
        if (!WaveEntry.EnemyClass.IsNull())
        {
            UClass* LoadedClass = WaveEntry.EnemyClass.Get();
            if (!LoadedClass)
            {
                LoadedClass = WaveEntry.EnemyClass.LoadSynchronous();
            }

            if (LoadedClass && LoadedClass->IsChildOf(APawn::StaticClass()))
            {
                EntryEnemyClass = LoadedClass;
            }
        }

        const FName PresetId = WaveEntry.EnemyPreset.IsNone() ? FName(TEXT("Default")) : WaveEntry.EnemyPreset;
        const int32 TargetCount = FMath::Clamp(WaveEntry.SpawnCount, 1, 128);
        for (int32 i = 0; i < TargetCount; ++i)
        {
            TSubclassOf<APawn> EnemyClass = EntryEnemyClass;
            if (!EnemyClass)
            {
                EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(PresetId);
            }
            if (!EnemyClass && PresetId != TEXT("Default"))
            {
                EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(TEXT("Default"));
            }
            if (!EnemyClass)
            {
                continue;
            }

            float CapsuleRadius = 42.0f;
            float CapsuleHalfHeight = 88.0f;
            ResolvePawnCapsuleSizeInternal(EnemyClass, CapsuleRadius, CapsuleHalfHeight);

            FVector SpawnLoc = FVector::ZeroVector;
            if (!TryResolveWaveSpawnLocation(PlayerPawn, WaveDef, CapsuleRadius, CapsuleHalfHeight, SpawnLoc))
            {
                continue;
            }

            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
            APawn* SpawnedEnemy = World->SpawnActor<APawn>(EnemyClass, SpawnLoc, FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f), SpawnParams);
            if (!IsValid(SpawnedEnemy))
            {
                continue;
            }

            float EffectiveCapsuleRadius = CapsuleRadius;
            float EffectiveCapsuleHalfHeight = CapsuleHalfHeight;
            ResolvePawnInstanceCapsuleSizeInternal(SpawnedEnemy, EffectiveCapsuleRadius, EffectiveCapsuleHalfHeight);

            FVector EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
            if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
            {
                FNavLocation NavLoc;
                if (NavSys->ProjectPointToNavigation(EffectiveSpawnLoc, NavLoc, FVector(220.0f, 220.0f, 360.0f)))
                {
                    const bool bFarFromNav = FVector::DistSquared2D(NavLoc.Location, EffectiveSpawnLoc) > FMath::Square(200.0f);
                    if (bFarFromNav)
                    {
                        SpawnedEnemy->SetActorLocation(NavLoc.Location, false, nullptr, ETeleportType::TeleportPhysics);
                        EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
                    }
                }
            }

            if (IsCapsuleBlockedForPawnInternal(World, EffectiveSpawnLoc, EffectiveCapsuleRadius, EffectiveCapsuleHalfHeight, SpawnedEnemy))
            {
                SpawnedEnemy->Destroy();
                continue;
            }

            FString RepairLog;
            Config->EnemyPresetRegistry->TryRepairSpawnedPawn(SpawnedEnemy, RepairLog);

            EnforceNoZombieGrabInternal(SpawnedEnemy);
            ForceEnemyTraceCollisionInternal(SpawnedEnemy);
            SpawnEnemyControllerDeferred(SpawnedEnemy, WaveRoomId, TEXT("Wave"), SpawnOrder++);

            SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidRoom_%d"), WaveRoomId)));
            SpawnedEnemy->Tags.AddUnique(TEXT("Enemy"));
            SpawnedEnemy->Tags.AddUnique(TEXT("RaidEnemy"));
            SpawnedEnemy->Tags.AddUnique(TEXT("RaidWaveEnemy"));
            SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidWave_%d"), WaveNumber)));

            EnemyToRoomMap.Add(SpawnedEnemy, WaveRoomId);
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
            EnemySearchNextOrderTimeByPawn.Add(WeakSpawnedEnemy, ActivationTime + FMath::FRandRange(0.08, 0.40));
            SpawnedEnemy->OnDestroyed.AddDynamic(this, &URaidCombatSubsystem::OnEnemyDestroyed);

            ++SpawnedCount;
            ++AliveWaveEnemyCount;
        }
    }

    if (SpawnedCount > 0)
    {
        CurrentWaveNumber = FMath::Max(CurrentWaveNumber, WaveNumber);
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Dynamic wave %d spawned. Count=%d AliveWave=%d"), WaveNumber, SpawnedCount, AliveWaveEnemyCount);

        if (bShowWaveStartBanner)
        {
            FText BannerTitle = WaveDef.WaveLabel;
            if (BannerTitle.IsEmpty())
            {
                BannerTitle = FText::FromString(FString::Printf(TEXT("WAVE %d"), WaveNumber));
            }

            const FText BannerSubtitle = WaveStartBannerSubtitle.IsEmpty()
                ? FText::FromString(TEXT("?ㅼ닔???곷뱾???ㅺ??듬땲??"))
                : WaveStartBannerSubtitle;
            EnqueueRegionBannerMessage(BannerTitle, BannerSubtitle, WaveStartBannerDuration, false);
        }
    }
}
