


#include "AbilityTask_MoveByInputWithRot.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"


UAbilityTask_MoveByInputWithRot* UAbilityTask_MoveByInputWithRot::MovePawnByInputWithRotation(UGameplayAbility* OwningAbility, FName TaskInstanceName, FVector InTargetLocation, 
    FRotator InTargetRotation, float InMaxDuration, float InDistanceTolerance, float InRotUpdateStartTime, float InRotationInterpSpeed, bool InUseLocationFixAtEnd, bool InApplyDeceleration)
{
    UAbilityTask_MoveByInputWithRot* Task = NewAbilityTask<UAbilityTask_MoveByInputWithRot>(OwningAbility, TaskInstanceName);

    Task->TargetLocation = InTargetLocation;
    Task->MaxDuration = InMaxDuration;
    Task->DistanceTolerance = InDistanceTolerance;
    Task->TargetRotation = InTargetRotation;
    Task->RotUpdateStartTime = InRotUpdateStartTime;
    Task->RotationInterpSpeed = InRotationInterpSpeed;
    Task->UseLocationFixAtEnd = InUseLocationFixAtEnd;
    Task->ApplyDeceleration = InApplyDeceleration;
    Task->ElapsedTime = 0.0f;

    return Task;
}

void UAbilityTask_MoveByInputWithRot::Activate()
{
    Super::Activate();
    bTickingTask = true;
}

void UAbilityTask_MoveByInputWithRot::TickTask(float DeltaTime)
{
    Super::TickTask(DeltaTime);

    APawn* Pawn = Cast<APawn>(GetAvatarActor());
    if (!IsValid(Pawn))
    {
        return;
    }

    float BrakingDistance = 0.0f;

    if (ApplyDeceleration == true)
    {
        ACharacter* Character = Cast<ACharacter>(Pawn);
        if (!IsValid(Character))
        {
            return;
        }

        UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
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


    ElapsedTime += DeltaTime;

    FVector CurrentLocation = Pawn->GetActorLocation();
    FRotator DirectionToTarget = UKismetMathLibrary::FindLookAtRotation(CurrentLocation, TargetLocation);
    FVector WorldDirection = DirectionToTarget.Vector();

    float DistanceToTarget = UKismetMathLibrary::Vector_Distance2D(CurrentLocation, TargetLocation);

    if (ApplyDeceleration == true)
    {
        if (DistanceToTarget > BrakingDistance)
        {
            Pawn->AddMovementInput(WorldDirection);
        }
    }
    else
    {
        Pawn->AddMovementInput(WorldDirection);
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
