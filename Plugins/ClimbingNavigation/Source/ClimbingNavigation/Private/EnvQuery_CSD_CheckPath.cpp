


#include "EnvQuery_CSD_CheckPath.h"

UEnvQuery_CSD_CheckPath::UEnvQuery_CSD_CheckPath(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{

}

float UEnvQuery_CSD_CheckPath::SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors)
{
    if (!ContextWorld) return 0.0;  
    if (ContextLocations.Num() == 0 && ContextActors.Num() == 0) return 0.0;

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(ContextWorld);
    const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));

    if (!NavSys || !RecastNavMesh)
    {
        return 0.0;
    }

    FVector PathStart;
    FVector PathEnd = CurrentPoint;
    if (ContextActors.Num() > 0 && bUseContextAsActors)
    {
        AActor* CA = Cast<AActor>(ContextActors[0]);
        if (CA) PathStart = CA->GetActorLocation();
    }
    else if(ContextLocations.Num() > 0)
    {
        PathStart = ContextLocations[0];
    }

    const FNavLocation StartLocation(PathStart);
    const FNavLocation EndLocation(PathEnd);


    UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(NavSys, PathStart, PathEnd, nullptr, NavFilter);
    if (!NavPath) { return ApplyFilter(0.0); }

    float PathScore = 1.0;

    if (PathTestMode == EPathTestMode::WithDistance)
    {
        PathScore = NavPath->GetPathLength();
        if (bTryNormalize)
        {
            PathScore = FMath::GetMappedRangeValueClamped(FVector2D(RefNormalizationMin, RefNormalizationMax), FVector2D(0.0, 1.0), PathScore);
        }

        PathScore = ScaleByCurve(PathScore);
        PathScore = PathScore * ScoreFactor;
    }

    float CostValue = 1.0;

    if (bCheckCost)
    {
        if (NavPath->GetPathCost() <= 0.0f) { CostValue = 0.0; }
    }

    return ApplyFilter(PathScore * CostValue);
}
