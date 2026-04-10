


#include "MyEnvQueryTest_PointsNearEdges.h"
#include "ClimbingNavigationBPLibrary.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"


UMyEnvQueryTest_PointsNearEdges::UMyEnvQueryTest_PointsNearEdges(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    EdgesFindingRange = 100.0f;
    ConstScoreRange = 50.0f;
    EdgeScoreValue = 1.0f;
    ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
    Cost = EEnvTestCost::Medium;
}

void UMyEnvQueryTest_PointsNearEdges::RunTest(FEnvQueryInstance& QueryInstance) const
{
    UObject* QueryOwner = QueryInstance.Owner.Get();
    if (!QueryOwner)
    {
        return;
    }

    UWorld* World = QueryInstance.World;
    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));

    if (!NavSys || !RecastNavMesh)
    {
        return;
    }

    FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
    float MinThresholdValue = FloatValueMin.GetValue();

    FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
    float MaxThresholdValue = FloatValueMax.GetValue();


    TArray<FVector> QueryPoints;
    QueryInstance.GetAllAsLocations(QueryPoints);

    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());
        FVector NearestPointOnEdge = FVector(0, 0, 0);


        
        const bool FindedEdge = UClimbingNavigationBPLibrary::FindClosestEdgeOnNavMesh(World, ItemLocation, NearestPointOnEdge, EdgesFindingRange);

        if (FindedEdge)
        {
            float Distance = FVector::Dist2D(ItemLocation, NearestPointOnEdge);

            if (Distance <= ConstScoreRange)
            {
                It.SetScore(TestPurpose, FilterType, EdgeScoreValue, MinThresholdValue, MaxThresholdValue); // Skaluje wynik do zakresu [0, 1]
            }
            else
            {
                const float NormalizedScore = 1.0f - ((Distance  - ConstScoreRange )/ EdgesFindingRange);
                It.SetScore(TestPurpose, FilterType, NormalizedScore, MinThresholdValue, MaxThresholdValue);
            }
        }

    }
}


float UMyEnvQueryTest_PointsNearEdges::CalculateScore(const FVector& Point, const FVector& EdgePoint, float Tolerance) const
{
    float Distance = FVector::Dist(Point, EdgePoint);
    return Distance <= Tolerance ? 1.0f - (Distance / Tolerance) : 0.0f;
}


FText UMyEnvQueryTest_PointsNearEdges::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("Points Near Edges"));
}


FText UMyEnvQueryTest_PointsNearEdges::GetDescriptionDetails() const
{
    FString Description = FString::Printf(
        TEXT("Detects points within %.1f units of navigation mesh edges.\nScore modification: %.1f."),
        EdgesFindingRange,
        EdgeScoreValue
    );

    return FText::FromString(Description);
}