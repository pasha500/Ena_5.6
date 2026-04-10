#include "Raid/RaidCombatSubsystem.h"
#include "GameFramework/Pawn.h"

void URaidCombatSubsystem::TickWaveSpawning(double NowSeconds, APawn* PlayerPawn)
{
    if (!bEnableDynamicWaves || TotalDynamicWaves <= 0 || !IsValid(PlayerPawn))
    {
        return;
    }

    if (!bWaveSchedulerInitialized)
    {
        bWaveSchedulerInitialized = true;
        NextWaveStartTimeSeconds = NowSeconds + FMath::Max(0.0f, FirstWaveDelaySeconds);
    }

    if (CurrentWaveNumber >= TotalDynamicWaves)
    {
        return;
    }

    if (AliveWaveEnemyCount >= FMath::Max(1, MaxAliveWaveEnemies))
    {
        NextWaveStartTimeSeconds = FMath::Max(NextWaveStartTimeSeconds, NowSeconds + 2.0);
        return;
    }

    if (NowSeconds < NextWaveStartTimeSeconds)
    {
        return;
    }

    const int32 PreviousWave = CurrentWaveNumber;
    const int32 NextWave = CurrentWaveNumber + 1;
    SpawnWaveNow(NextWave, PlayerPawn);

    if (CurrentWaveNumber == PreviousWave)
    {
        NextWaveStartTimeSeconds = NowSeconds + 4.0;
    }
    else
    {
        NextWaveStartTimeSeconds = NowSeconds + FMath::Max(5.0f, WaveIntervalSeconds);
    }
}
