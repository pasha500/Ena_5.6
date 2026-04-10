#include "Raid/RaidCombatSubsystem.h"

double URaidCombatSubsystem::ComputeRecoveryHighLoadScaleInternal(int32 ActiveEnemyCount) const
{
    return
        (ActiveEnemyCount >= FMath::Max(8, RecoveryHighLoadEnemyThreshold))
            ? FMath::Max(1.0, (double)RecoveryHighLoadIntervalScale)
            : 1.0;
}

double URaidCombatSubsystem::ComputeRecoveryCheckIntervalInternal(double BaseIntervalSeconds, int32 ActiveEnemyCount) const
{
    const double HighLoadScale = ComputeRecoveryHighLoadScaleInternal(ActiveEnemyCount);
    return FMath::Max(0.10, BaseIntervalSeconds * HighLoadScale);
}
