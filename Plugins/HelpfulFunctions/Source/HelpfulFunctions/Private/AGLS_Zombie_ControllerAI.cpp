


#include "AGLS_Zombie_ControllerAI.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

void AAGLS_Zombie_ControllerAI::UpdateSightPerceptionValues_Implementation()
{
}

bool AAGLS_Zombie_ControllerAI::DoesPathUseNavLink(UNavigationPath* Path, float MaxDistanceToPoint2D, float MaxHeightDiff)
{
    if (!Path || Path->PathPoints.Num() < 2)
        return false;

    for (int i = 0; i < Path->PathPoints.Num() - 1; i++)
    {
        const FVector P1 = Path->PathPoints[i];
        const FVector P2 = Path->PathPoints[i + 1];

        if (FVector().DistXY(P1,P2) < MaxDistanceToPoint2D && abs(P1.Z - P2.Z) > MaxHeightDiff)
        {
            return true;
        }

    }
    return false;
}

float AAGLS_Zombie_ControllerAI::PathWeightByNavLinksNumber(UNavigationPath* Path, float MaxDistanceToPoint2D, float MaxHeightDiff, float Bias, bool UseAbsOnHeight)
{
    if (!Path || Path->PathPoints.Num() < 2)
        return 1.0;

    float WeightValue = 1.0;

    for (int i = 0; i < Path->PathPoints.Num() - 1; i++)
    {
        const FVector P1 = Path->PathPoints[i];
        const FVector P2 = Path->PathPoints[i + 1];

        float HeightValue = P2.Z - P1.Z;
        if (UseAbsOnHeight) HeightValue = abs(HeightValue);

        if (FVector().DistXY(P1, P2) < MaxDistanceToPoint2D && HeightValue > MaxHeightDiff)
        {
            if (WeightValue > 0.0)
            {
                WeightValue = WeightValue - Bias;
            }
            else
            {
                return WeightValue;
            }
        }

    }
    return WeightValue;
}

void AAGLS_Zombie_ControllerAI::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
    CustomUpdateControlRotation(DeltaTime, bUpdatePawn);
}

void AAGLS_Zombie_ControllerAI::CustomUpdateControlRotation_Implementation(float DeltaTime, bool bUpdatePawn)
{
    APawn* const MyPawn = GetPawn();
    if (MyPawn)
    {
        FRotator NewControlRotation = GetControlRotation();

        // Look toward focus
        const FVector FocalPoint = GetFocalPoint();
        if (FAISystem::IsValidLocation(FocalPoint))
        {
            NewControlRotation = (FocalPoint - MyPawn->GetPawnViewLocation()).Rotation();
        }
        else if (bSetControlRotationFromPawnOrientation)
        {
            NewControlRotation = MyPawn->GetActorRotation();
        }

        // Don't pitch view unless looking at another pawn
        if (NewControlRotation.Pitch != 0 && Cast<APawn>(GetFocusActor()) == nullptr)
        {
            NewControlRotation.Pitch = 0.f;
        }

        SetControlRotation(NewControlRotation);

        if (bUpdatePawn)
        {
            const FRotator CurrentPawnRotation = MyPawn->GetActorRotation();

            if (CurrentPawnRotation.Equals(NewControlRotation, 1e-3f) == false)
            {
                MyPawn->FaceRotation(NewControlRotation, DeltaTime);
            }
        }
    }
}
