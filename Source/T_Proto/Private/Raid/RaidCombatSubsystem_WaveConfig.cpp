#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidWaveProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

FRaidWaveStatus URaidCombatSubsystem::GetWaveStatus() const
{
    FRaidWaveStatus Status;
    Status.bWaveSystemEnabled = bEnableDynamicWaves;
    Status.CurrentWave = CurrentWaveNumber;
    Status.TotalWaves = FMath::Max(0, TotalDynamicWaves);
    Status.AliveWaveEnemies = FMath::Max(0, AliveWaveEnemyCount);
    Status.bAllWavesCompleted = Status.TotalWaves > 0 && CurrentWaveNumber >= Status.TotalWaves;

    if (Status.bWaveSystemEnabled && !Status.bAllWavesCompleted && bWaveSchedulerInitialized)
    {
        if (const UWorld* World = GetWorld())
        {
            Status.SecondsUntilNextWave = FMath::Max(0.0f, (float)(NextWaveStartTimeSeconds - World->GetTimeSeconds()));
        }
    }

    if (!Status.bAllWavesCompleted && Status.TotalWaves > 0)
    {
        const int32 NextWaveNumber = FMath::Clamp(CurrentWaveNumber + 1, 1, Status.TotalWaves);
        if (WaveDefinitions.IsValidIndex(NextWaveNumber - 1))
        {
            Status.NextWaveLabel = WaveDefinitions[NextWaveNumber - 1].WaveLabel;
        }
        if (Status.NextWaveLabel.IsEmpty())
        {
            Status.NextWaveLabel = FText::FromString(FString::Printf(TEXT("Wave %d"), NextWaveNumber));
        }
    }

    return Status;
}

void URaidCombatSubsystem::ForceStartNextWave()
{
    if (!bEnableDynamicWaves || TotalDynamicWaves <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    bWaveSchedulerInitialized = true;
    NextWaveStartTimeSeconds = World->GetTimeSeconds();
    StartTraceCollisionEnforcer();
    TickWaveSpawning(World->GetTimeSeconds(), GetPrimaryPlayerPawn());
}

void URaidCombatSubsystem::RebuildUpcomingRoomSpawnPlans()
{
    PrewarmedSpawnPlansByRoomId.Empty();
    PrepareUpcomingRoomSpawnPlans(nullptr);
}

void URaidCombatSubsystem::ConfigureDynamicWaves(bool bEnable, int32 InTotalWaves, float InFirstWaveDelaySeconds, float InWaveIntervalSeconds, int32 InMaxAliveWaveEnemies)
{
    bEnableDynamicWaves = bEnable;
    TotalDynamicWaves = FMath::Clamp(InTotalWaves, 1, 100);
    FirstWaveDelaySeconds = FMath::Clamp(InFirstWaveDelaySeconds, 0.0f, 600.0f);
    WaveIntervalSeconds = FMath::Clamp(InWaveIntervalSeconds, 5.0f, 600.0f);
    MaxAliveWaveEnemies = FMath::Clamp(InMaxAliveWaveEnemies, 1, 512);

    bWaveSchedulerInitialized = false;
    CurrentWaveNumber = 0;
    AliveWaveEnemyCount = 0;
    if (UWorld* World = GetWorld())
    {
        NextWaveStartTimeSeconds = World->GetTimeSeconds() + FirstWaveDelaySeconds;
    }

    StartTraceCollisionEnforcer();
}

void URaidCombatSubsystem::SetWaveDefinitions(const TArray<FRaidWaveDefinition>& InWaveDefinitions)
{
    WaveDefinitions = InWaveDefinitions;

    // Preload wave-referenced pawn classes up front to avoid first-spawn hitch at wave start.
    int32 PreloadedWaveClassCount = 0;
    for (FRaidWaveDefinition& WaveDef : WaveDefinitions)
    {
        for (FRaidWaveSpawnEntry& Entry : WaveDef.Entries)
        {
            if (Entry.EnemyClass.IsNull())
            {
                continue;
            }

            UClass* LoadedClass = Entry.EnemyClass.Get();
            if (!LoadedClass)
            {
                LoadedClass = Entry.EnemyClass.LoadSynchronous();
            }

            if (LoadedClass && LoadedClass->IsChildOf(APawn::StaticClass()))
            {
                ++PreloadedWaveClassCount;
            }
        }
    }

    if (bEnableCombatPerfLogs && PreloadedWaveClassCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Preloaded wave enemy classes: %d"), PreloadedWaveClassCount);
    }
}

void URaidCombatSubsystem::ApplyWaveProfile(const URaidWaveProfile* WaveProfile)
{
    if (!IsValid(WaveProfile))
    {
        ConfigureDynamicWaves(false, 1, 0.0f, 60.0f, 1);
        WaveDefinitions.Reset();
        return;
    }

    ConfigureDynamicWaves(
        WaveProfile->bEnableDynamicWaves,
        FMath::Clamp(WaveProfile->TotalDynamicWaves, 1, 100),
        FMath::Clamp(WaveProfile->FirstWaveDelaySeconds, 0.0f, 600.0f),
        FMath::Clamp(WaveProfile->WaveIntervalSeconds, 5.0f, 600.0f),
        FMath::Clamp(WaveProfile->MaxAliveWaveEnemies, 1, 512));

    TArray<FRaidWaveDefinition> ConvertedDefs;
    ConvertedDefs.Reserve(WaveProfile->Stages.Num());

    for (const FRaidWaveStage& Stage : WaveProfile->Stages)
    {
        FRaidWaveDefinition NewDef;
        NewDef.WaveLabel = Stage.WaveLabel;
        NewDef.SpawnMinDistanceFromPlayer = FMath::Clamp(Stage.SpawnMinDistanceFromPlayer, 1500.0f, 180000.0f);
        NewDef.SpawnMaxDistanceFromPlayer = FMath::Clamp(Stage.SpawnMaxDistanceFromPlayer, 2000.0f, 220000.0f);
        NewDef.SpawnScatterRadius = FMath::Clamp(Stage.SpawnScatterRadius, 200.0f, 8000.0f);

        if (NewDef.SpawnMaxDistanceFromPlayer < NewDef.SpawnMinDistanceFromPlayer)
        {
            Swap(NewDef.SpawnMaxDistanceFromPlayer, NewDef.SpawnMinDistanceFromPlayer);
        }

        for (const FRaidWaveEnemyDescriptor& EnemyDesc : Stage.Enemies)
        {
            FRaidWaveSpawnEntry Entry;
            Entry.EnemyClass = EnemyDesc.EnemyClass;
            Entry.EnemyPreset = EnemyDesc.EnemyPreset.IsNone() ? FName(TEXT("Default")) : EnemyDesc.EnemyPreset;
            Entry.SpawnCount = FMath::Clamp(EnemyDesc.SpawnCount, 1, 128);
            NewDef.Entries.Add(Entry);
        }

        ConvertedDefs.Add(MoveTemp(NewDef));
    }

    if (WaveProfile->bLoopLastStageWhenShort && ConvertedDefs.Num() > 0 && TotalDynamicWaves > ConvertedDefs.Num())
    {
        const FRaidWaveDefinition LastDef = ConvertedDefs.Last();
        while (ConvertedDefs.Num() < TotalDynamicWaves)
        {
            ConvertedDefs.Add(LastDef);
        }
    }

    SetWaveDefinitions(ConvertedDefs);
}
