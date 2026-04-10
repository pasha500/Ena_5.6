

#pragma once

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "SmoothPathFollowingComponent.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API USmoothPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:
	/*
	Specifies the scale of the maximum circle that can be attempted to be placed between two segments (circle tangent to straight lines). You should not set a value greater than 0.5 or less than 0.05.

	TotalLength = (PrevPoint - CurrentPoint).Size() + (CurrentPoint - NextPoint).Size();
	TargetMaxRadius = TotalLength * |RoundingRadiusScale|;
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = 0.05, ClampMax = 0.98))
	float RoundingRadiusScale = 0.2;

	/* The number of iterations determining the accuracy of adding additional points from the tangent circle. Can be set to 0. The value is not valid if 'DynamicRoundingAccuracy' is set to true */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = 0, ClampMax = 10))
	int PathRoundingAccuracy = 1;

	/* It allows you to dynamically determine how many additional points from the tangent circle should be added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Path Following Component"))
	bool DynamicRoundingAccuracy = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", EditCondition = "DynamicRoundingAccuracy"))
	float RoundingAccuracyDivider = 60.0;

	/* If the distance between the previous point and the current point is greater than this value, or the distance between the current point and the next point is greater, rounding will not be applied.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Path Following Component"))
	float MaxAcceptableDistToNext = 800.0;

	/* Specifies for which value of the angle between lines (PrevPoint, CurrentPoint), (CurrentPoint, NextPoint) rounding should not be applied. 
	   The PI value means that the angle condition does not affect whether rounding should be performed*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = 0.01, ClampMax = 3.14))
	float MinAngleTolleranceInRadians = 3.14;

	// Min Distance Between Tangents For Circle
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = 1.0, ClampMax = 100.0))
	float MinDistanceBetweenTangents = 10.0;

	//When AI is current performing moving to actor then scale rounding radius by this value. If equal 1.0 then this value is do nothing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = 0.1, ClampMax = 1.0))
	float ScaleMaxRadiusWhenMoveToActor = 0.4;

	//If this value is <= 0.0 then this condition is ignored
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Path Following Component", ClampMin = -1.0, ClampMax = 200))
	float MaximumHeightDifference = -1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Path Following Component"))
	bool bUseAutoFocusSetDurningMove = true;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debug"))
	bool bDrawDebugPath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debug", ClampMin = 0.0, ClampMax = 10.0))
	float DebugDisplayTime = 1.0;

	bool bUseRounding = true;

	TArray<FNavPathPoint> PathPoints;

	UFUNCTION(BlueprintCallable, Category = "Path Following Component", meta = (DisplayName = "Pause Move", Keywords = "Navigation,Path,Following,AI"))
	virtual void PauseCurrentMove(bool ResetVelocity = false);
	
	UFUNCTION(BlueprintCallable, Category = "Path Following Component", meta = (DisplayName = "Resume Move", Keywords = "Navigation,Path,Following,AI"))
	virtual void ResumePausedMove();

	virtual void SetMoveSegment(int32 SegmentStartIndex) override;
	virtual FAIRequestID RequestMove(const FAIMoveRequest& RequestData, FNavPathSharedPtr InPath) override;

	virtual FNavPathSharedPtr CreateSmoothedPath(int Accuracy, float MaxDistTonext, FNavPathSharedPtr InPath, bool HaveGoalActor);
	FNavPathSharedPtr CreateAndSetPathFromPoints(const TArray<FNavPathPoint>& NavMainPoints, FNavPathSharedPtr InPath);

	UFUNCTION(BlueprintPure, Category = "Path Following Component", meta = (DisplayName = "Calculate Tangent Circle", Keywords = "Navigation,Path,Following,AI"))
	bool CalculateTangentCircle(const FVector& P1, const FVector& P2, const FVector& P3, float MaxRadius, FVector& OutCenter, float& OutRadius);

	TArray<FVector> GenerateArcPoints(const FVector& PrevPoint, const FVector& CurrentPoint, const FVector& NextPoint, int Accuracy, bool HaveGoalActor);

	UFUNCTION(BlueprintCallable, Category = "Path Following Component", meta = (DisplayName = "Disable Rounding Path", Keywords = "Navigation,Path,Following,AI"))
	virtual void DisableRoundingPath(bool UseRoundingPath);


	virtual void UpdateMoveFocus() override;

private:

	bool IsPointOnNavMesh(const FVector& Point);

};
