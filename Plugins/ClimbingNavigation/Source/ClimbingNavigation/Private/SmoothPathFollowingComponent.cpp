


#include "SmoothPathFollowingComponent.h"
#include "NavigationSystemTypes.h"
#include "NavigationSystem.h"

bool USmoothPathFollowingComponent::CalculateTangentCircle(const FVector& P1, const FVector& P2, const FVector& P3, float MaxRadius, FVector& OutCenter, float& OutRadius)
{
    FVector V1 = (P1 - P2).GetSafeNormal(); // Odcinek P2 -> P1
    FVector V2 = (P3 - P2).GetSafeNormal(); // Odcinek P2 -> P3

    float Angle = FMath::Acos(FVector::DotProduct(V1, V2));

    if (FMath::IsNearlyZero(Angle, KINDA_SMALL_NUMBER) == true || Angle > MinAngleTolleranceInRadians)
    {
        return false;
    }
    FVector Bisector = (V1 + V2).GetSafeNormal();
    float distanceToCircle = MaxRadius / FMath::Sin(Angle / 2);
    OutCenter = P2 + Bisector * distanceToCircle;
    OutRadius = FMath::Min(distanceToCircle, MaxRadius);

    return true;
}

FVector ProjectPointToLine(FVector LineStart, FVector LineEnd, FVector Point)
{
    const FVector LineDirection = (LineEnd - LineStart).GetSafeNormal();
    const FVector PointToStart = Point - LineStart;
    const float ProjectionLength = FVector::DotProduct(PointToStart, LineDirection);
    FVector ProjectedPoint = LineStart + LineDirection * ProjectionLength;

    return ProjectedPoint;
}


void USmoothPathFollowingComponent::PauseCurrentMove(bool ResetVelocity)
{
	FAIRequestID RequestID = FAIRequestID::CurrentRequest;
	EPathFollowingVelocityMode VelocityMode = EPathFollowingVelocityMode::Keep;
	if (ResetVelocity) VelocityMode = EPathFollowingVelocityMode::Reset;

	PauseMove(RequestID, VelocityMode);
}

void USmoothPathFollowingComponent::ResumePausedMove()
{
	FAIRequestID RequestID = FAIRequestID::CurrentRequest;
	ResumeMove(RequestID);
}

void USmoothPathFollowingComponent::SetMoveSegment(int32 SegmentStartIndex)
{
	Super::SetMoveSegment(SegmentStartIndex);
}

//Main Function - Triggered when some called any of MoveTo function
FAIRequestID USmoothPathFollowingComponent::RequestMove(const FAIMoveRequest& RequestData, FNavPathSharedPtr InPath)
{
	FNavPathSharedPtr NewPath = CreateSmoothedPath(PathRoundingAccuracy, MaxAcceptableDistToNext, InPath, IsValid(RequestData.GetGoalActor()));
	return Super::RequestMove(RequestData, NewPath);
}

FNavPathSharedPtr USmoothPathFollowingComponent::CreateSmoothedPath(int Accuracy, float MaxDistTonext, FNavPathSharedPtr InPath, bool HaveGoalActor)
{
    if (!InPath)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid input path."));
        return nullptr;
    }

    TArray<FNavPathPoint> NavMainPoints = InPath->GetPathPoints();
    TArray<FNavPathPoint> SmoothedPoints;

    SmoothedPoints.Add(NavMainPoints[0]);

    for (int32 i = 1; i < NavMainPoints.Num() - 1; i++)
    {
        FVector PrevPoint = NavMainPoints[i - 1].Location;
        FVector CurrentPoint = NavMainPoints[i].Location;
        FVector NextPoint = NavMainPoints[i + 1].Location;

        bool bHeightCondition = true;

        if (MaximumHeightDifference > 0.0)
        {
            bHeightCondition = abs(PrevPoint.Z - CurrentPoint.Z) <= MaximumHeightDifference && abs(CurrentPoint.Z - NextPoint.Z) <= MaximumHeightDifference;
        }

        if (FVector::Dist(PrevPoint, CurrentPoint) <= MaxDistTonext && FVector::Dist(CurrentPoint, NextPoint) <= MaxDistTonext && bHeightCondition && bUseRounding)
        {
           
            TArray<FVector> ArcPoints = GenerateArcPoints(PrevPoint, CurrentPoint, NextPoint, Accuracy, HaveGoalActor);

            bool bAllPointsValid = true;
            for (const FVector& ArcPoint : ArcPoints)
            {
                if (!IsPointOnNavMesh(ArcPoint))
                {
                    bAllPointsValid = false;
                    break;
                }
            }
            if (bAllPointsValid)
            {
                for (const FVector& ArcPoint : ArcPoints)
                {
                    SmoothedPoints.Add(FNavPathPoint(ArcPoint));
                }
            }
            else
            {
                SmoothedPoints.Add(NavMainPoints[i]);
            }
        }
        else
        {
            SmoothedPoints.Add(NavMainPoints[i]);
        }
    }

    // Dodanie ostatniego punktu do wynikowej œcie¿ki
    SmoothedPoints.Add(NavMainPoints.Last());

    if (bDrawDebugPath == true && SmoothedPoints.Num() > 1)
    {
        for (int i = 0; i < SmoothedPoints.Num() - 1; i++)
        {
            DrawDebugLine(GetWorld(), SmoothedPoints[i], SmoothedPoints[i + 1], FColor::Emerald, false, DebugDisplayTime, 0, 3.2);
        }
    }

    return CreateAndSetPathFromPoints(SmoothedPoints, InPath);
}


TArray<FVector> USmoothPathFollowingComponent::GenerateArcPoints(const FVector& PrevPoint, const FVector& CurrentPoint, const FVector& NextPoint, int Accuracy, bool HaveGoalActor)
{
    TArray<FVector> ArcPoints;

    // Oblicz ca³kowit¹ d³ugoœæ dwóch odcinków (P1-P2, P2-P3)
    const float TotalLength = (PrevPoint - CurrentPoint).Size() + (CurrentPoint - NextPoint).Size();

    float TargetMaxRadius = TotalLength * RoundingRadiusScale;

    if (HaveGoalActor)
    {
        TargetMaxRadius = TargetMaxRadius * ScaleMaxRadiusWhenMoveToActor;
    }

    FVector CircleCenter = FVector(0, 0, 0);
    float CircleRadius = 0.0;

    const bool CircleValid = CalculateTangentCircle(PrevPoint, CurrentPoint, NextPoint, TargetMaxRadius, CircleCenter, CircleRadius);

    if (CircleValid)
    {
        const FVector ProjectedPointA = ProjectPointToLine(PrevPoint, CurrentPoint, CircleCenter);
        const FVector ProjectedPointB = ProjectPointToLine(CurrentPoint, NextPoint, CircleCenter);

        ArcPoints.Add(ProjectedPointA);
        
        int CurrentAccuracy = Accuracy;

        if (DynamicRoundingAccuracy)
        {
            const float d = (ProjectedPointA - ProjectedPointB).Length();
            if (d > MinDistanceBetweenTangents)
            {
                CurrentAccuracy = int(d / RoundingAccuracyDivider);
            }
            else
            {
                CurrentAccuracy = 0;
            }
        }

        if (CurrentAccuracy > 0 && (ProjectedPointA - ProjectedPointB).Length() > MinDistanceBetweenTangents)
        {
            const FVector DirectionA = (ProjectedPointA - CircleCenter).GetSafeNormal();
            const FVector DirectionB = (ProjectedPointB - CircleCenter).GetSafeNormal();

            if (CurrentAccuracy == 1)
            {
                FVector LerpedNormal = FMath::Lerp<FVector>(DirectionA, DirectionB, 0.5);
                LerpedNormal.Normalize();
                ArcPoints.Add(CircleCenter + (LerpedNormal * CircleRadius));
            }
            else
            {
                for (int i = 0; i < CurrentAccuracy; i++)
                {
                    float CurrentAlpha = FMath::GetMappedRangeValueClamped(FVector2D(0.0, float(CurrentAccuracy)), FVector2D(0.2, 0.8), (float)i);
                    FVector LerpedNormal = FMath::Lerp<FVector>(DirectionA, DirectionB, CurrentAlpha);
                    LerpedNormal.Normalize();
                    ArcPoints.Add(CircleCenter + (LerpedNormal * CircleRadius));
                }
            }
        }

        if (bDrawDebugPath)
        {
            FMatrix CircleMatrix = FTransform(FRotator(90, 0, 0), FVector(CircleCenter)).ToMatrixNoScale();
            DrawDebugCircle(GetWorld(), CircleMatrix, CircleRadius, 24, FColor::Orange, false, DebugDisplayTime, 0, 1.2);
        }


        ArcPoints.Add(ProjectedPointB);

    }
    else
    {
        ArcPoints.Add(CurrentPoint);
    }

    return ArcPoints;
}

//Simple switch function to enable/disable rounding path algoritm (can be triggered by blueprint)
void USmoothPathFollowingComponent::DisableRoundingPath(bool UseRoundingPath)
{
    bUseRounding = UseRoundingPath;
}

void USmoothPathFollowingComponent::UpdateMoveFocus()
{
    if (bUseAutoFocusSetDurningMove)
    {
        Super::UpdateMoveFocus();
    }
}

bool USmoothPathFollowingComponent::IsPointOnNavMesh(const FVector& Point)
{
    FNavLocation NavLocation;
    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

    if (NavSys)
    {
        return NavSys->ProjectPointToNavigation(Point, NavLocation, FVector(50, 50, 50));
    }

    return false;
}



FNavPathSharedPtr USmoothPathFollowingComponent::CreateAndSetPathFromPoints(const TArray<FNavPathPoint>& NavMainPoints, FNavPathSharedPtr InPath)
{
	if (NavMainPoints.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cannot create a path with less than 2 points."));
		return nullptr;
	}

	FNavPathSharedPtr PathShared = InPath;
	PathShared->GetPathPoints().Empty();
	for (const FNavPathPoint& Point : NavMainPoints)
	{
		PathShared->GetPathPoints().Add(Point);
	}
	return PathShared;
}



