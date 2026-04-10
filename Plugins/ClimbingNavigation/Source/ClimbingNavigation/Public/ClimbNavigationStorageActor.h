

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClimbingNavigationBPLibrary.h"
#include "ClimbNavigationStorageActor.generated.h"

UCLASS()
class CLIMBINGNAVIGATION_API AClimbNavigationStorageActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AClimbNavigationStorageActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Climb Nav Data"))
	TArray<FClimbNav_SingleClimbPoint> NavigationCompleteMap;

	TArray<FClimbNav_SingleClimbPoint> LastBuildedPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Nav Generation Config"))
	bool CheckCollisionWhenPathFinding = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Nav Generation Config", ClampMin = 0, ClampMax = 1, UIMin = 0, UIMax = 1))
	float PreferLowerAltitudesWeight = 0.05;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Nav Generation Config", ClampMin = 0, ClampMax = 1, UIMin = 0, UIMax = 1))
	float SameAnglePreferenceWeight = 0.0;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	TArray<FClimbNav_SingleClimbPoint> FindPathBetweenTwoIndex(int StartPointIndex, int EndPointIndex);

	TArray<FClimbNav_SingleClimbPoint> ReconstructPath(const TMap<int, int>& PreviousPoints, int EndPointIndex);

	int GetPointWithSmallestDistance(const TMap<int, float>& Distances, const TArray<int>& PointsToVisit);

	UFUNCTION(BlueprintCallable, Category = "Climb Nav Core", meta = (DisplayName = "Find Path Between Two Climb Points", Keywords = "Climbing Navigation"))
	void FindPathBetweenClimbPoints(bool& Succesful, float& TotalLenght, TArray<FClimbNav_SingleClimbPoint>& PathPoints, FVector StartLocation, FVector EndLocation);

	UFUNCTION(BlueprintPure, Category = "Climb Nav Core", meta = (DisplayName = "Find Nearest Nav Ledge", Keywords = "Climbing Navigation"))
	bool FindNearestNavLedge(int& ReturnIndex, float& ReturnDistance, FClimbNav_SingleClimbPoint& ReturnLedgeParams, FVector InLocation);

	UFUNCTION(BlueprintPure, Category = "Climb Nav Core", meta = (DisplayName = "Get Last Builded Path", Keywords = "Climbing Navigation"))
	TArray<FClimbNav_SingleClimbPoint> GetLastBuildedPath();

};
