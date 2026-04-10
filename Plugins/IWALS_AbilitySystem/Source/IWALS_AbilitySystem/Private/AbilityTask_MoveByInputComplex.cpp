


#include "AbilityTask_MoveByInputComplex.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"


UAbilityTask_MoveByInputComplex* UAbilityTask_MoveByInputComplex::MovePawnByInputWithStop(UGameplayAbility* OwningAbility, FName TaskInstanceName, FVector InTargetLocation,
    FRotator InTargetRotation, FName AxisNameForward, FName AxisNameRight, float InMaxDuration, float InDistanceTolerance, float InRotUpdateStartTime, float InRotationInterpSpeed, 
    bool InUseLocationFixAtEnd, bool InApplyDeceleration, float StopWhenInputPressedTime, bool EndTaskWhenInputPressed)
{
    UAbilityTask_MoveByInputComplex* Task = NewAbilityTask<UAbilityTask_MoveByInputComplex>(OwningAbility, TaskInstanceName);

    Task->TargetLocation = InTargetLocation;
    Task->MaxDuration = InMaxDuration;
    Task->DistanceTolerance = InDistanceTolerance;
    Task->TargetRotation = InTargetRotation;
    Task->RotUpdateStartTime = InRotUpdateStartTime;
    Task->RotationInterpSpeed = InRotationInterpSpeed;
    Task->UseLocationFixAtEnd = InUseLocationFixAtEnd;
    Task->ApplyDeceleration = InApplyDeceleration;
    Task->ElapsedTime = 0.0f;
    Task->StopWhenInputPressedTime = StopWhenInputPressedTime;
    Task->EndTaskWhenInputPressed = EndTaskWhenInputPressed;
    Task->InputPressTime = 0.0f;
    Task->MovementStopped = false;
    Task->InputAxisFB = AxisNameForward;
    Task->InputAxisRL = AxisNameRight;

    return Task;
}

void UAbilityTask_MoveByInputComplex::Activate()
{
    Super::Activate();
    bTickingTask = true;
}

void UAbilityTask_MoveByInputComplex::TickTask(float DeltaTime)
{
    Super::TickTask(DeltaTime);


    APawn* Pawn = Cast<APawn>(GetAvatarActor());
    if (!IsValid(Pawn))
    {
        return;
    }

    float BrakingDistance = 0.0f;

    //Character References
    ACharacter* Character = Cast<ACharacter>(Pawn);
    UCharacterMovementComponent* MovementComponent = nullptr;

    if (IsValid(Character))
    {
        MovementComponent = Character->GetCharacterMovement();
    }
    else
    {
        return;
    }

    // Calculate Deceleration When REQUIRED
    if (ApplyDeceleration == true)
    {

        if (!IsValid(MovementComponent))
        {
            return;
        }

        float CurrentSpeed = MovementComponent->Velocity.Size();
        float BrakingDeceleration = MovementComponent->BrakingDecelerationWalking;

        if (BrakingDeceleration > 0)
        {
            BrakingDistance = (CurrentSpeed * CurrentSpeed) / (2 * BrakingDeceleration);
        }
    }

    //Stop By Movement Input (Calculate Pressing INPUT TIME)
    if (StopWhenInputPressedTime >= 0.0)
    {
        FVector InputVector;
        InputVector.X = Pawn->GetInputAxisValue(InputAxisFB);
        InputVector.Y = Pawn->GetInputAxisValue(InputAxisRL);


        float MovementInput = InputVector.Length();

        if (MovementInput > 0.5)
        {
            InputPressTime += DeltaTime;
        }
        else if (InputPressTime > 0.0)
        {
            InputPressTime -= DeltaTime;
        }
    }


    ElapsedTime += DeltaTime; // MAIN TIMER 

    FVector CurrentLocation = Pawn->GetActorLocation();
    FRotator DirectionToTarget = UKismetMathLibrary::FindLookAtRotation(CurrentLocation, TargetLocation);
    FVector WorldDirection = DirectionToTarget.Vector();

    float DistanceToTarget = UKismetMathLibrary::Vector_Distance2D(CurrentLocation, TargetLocation);

    //Stop By Movement Input (Stopping Condition)
    if (InputPressTime < StopWhenInputPressedTime)
    {
        if (ApplyDeceleration == true)
        {
            if (DistanceToTarget > BrakingDistance)
            {
                Pawn->AddMovementInput(WorldDirection); // <-- Move By Input
            }
        }
        else
        {
            Pawn->AddMovementInput(WorldDirection); // <-- Move By Input
        }
    }
    else if (StopWhenInputPressedTime)
    {
        Stopped.Broadcast(ElapsedTime);
        bTickingTask = false;
        EndTask();
    }
    else if(!MovementStopped)
    {
        MovementStopped = true;
        Stopped.Broadcast(ElapsedTime);
    }

    if (ElapsedTime >= RotUpdateStartTime)
    {
        Pawn->SetActorRotation(UKismetMathLibrary::Conv_RotatorToQuaternion(UKismetMathLibrary::RInterpTo(Pawn->GetActorRotation(), TargetRotation, DeltaTime, RotationInterpSpeed)));
    }

    DurningMove.Broadcast(ElapsedTime);



    if (DistanceToTarget <= DistanceTolerance)
    {
        if (UseLocationFixAtEnd == true)
        {
            Pawn->SetActorLocation(TargetLocation, true);
        }

        TargetLocationReached.Broadcast(ElapsedTime);
        bTickingTask = false;
        EndTask();
    }
    else if (ElapsedTime >= MaxDuration)
    {
        Failed.Broadcast(ElapsedTime);
        bTickingTask = false;
        EndTask();
    }
}
