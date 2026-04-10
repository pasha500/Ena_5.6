

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "MyEnvQueryTest_PointsNearEdges.generated.h"

/* It involves assessing the distance of current points from the nearest possible edge of the Navigation Mesh.*/
UCLASS()
class CLIMBINGNAVIGATION_API UMyEnvQueryTest_PointsNearEdges : public UEnvQueryTest
{
	GENERATED_BODY()

public:
    UMyEnvQueryTest_PointsNearEdges(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

private:

    // Funkcja pomocnicza do obliczania score dla punktu
    float CalculateScore(const FVector& Point, const FVector& EdgePoint, float DistanceTolerance) const;

    // Parametry konfiguracyjne testu
    UPROPERTY(EditDefaultsOnly, Category = "Edge Test", meta = (ToolTip = "Searching for Nav edges distance per point"))
    float EdgesFindingRange;

    UPROPERTY(EditDefaultsOnly, Category = "Edge Test", meta = (ToolTip = "Distance between grid point and nearest nav edge that been set a const score value (EdgeScoreValue)"))
    float ConstScoreRange;

    UPROPERTY(EditDefaultsOnly, Category = "Edge Test", meta = (ToolTip = "Score value added or multiplied for points near edges."))
    float EdgeScoreValue;

	
};
