

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "UObject/ObjectMacros.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_MoveByInputComplex.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDurningMove3, float, Time);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLocationReached3, float, Time);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFailed3, float, Time);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStopped3, float, Time);

/*A task focused on moving the character capsule using AddMovementInput(). This function allows the capsule to automatically 
move toward a selected point, and since it’s triggered through input, the animation system won’t notice the difference between 
actual input held by the player and input triggered by an AbilityTask. However, it’s important to note that the direction is 
calculated without verifying whether reaching the target position is actually possible.

Main parameters:

1) MaxDuration – Defines the maximum amount of time allowed for the capsule to reach the target.

2) DistanceTolerance – Specifies how close the capsule must be to the target location for it to be considered correctly positioned.

3) RotUpdateStartTime – A normalized time value indicating when to start interpolating the capsule’s rotation to match the 
   specified TargetRotation.

4) ApplyDeceleration – If enabled, the system will account for braking time (using BrakingDecelerationWalking) when calculating how 
   long it takes to reach the target. This may improve accuracy.

5) StopWhenInputPressedTime – Stops the movement if the player physically presses a movement input. This requires providing the 
   MovementInput on the input pin.
*/
UCLASS()
class IWALS_ABILITYSYSTEM_API UAbilityTask_MoveByInputComplex : public UAbilityTask
{
	GENERATED_BODY()


    /*A task focused on moving the character capsule using AddMovementInput(). This function allows the capsule to automatically
    move toward a selected point, and since its triggered through input, the animation system wont notice the difference between
    actual input held by the player and input triggered by an AbilityTask. However, its important to note that the direction is
    calculated without verifying whether reaching the target position is actually possible.

    Main parameters:

    1) MaxDuration - Defines the maximum amount of time allowed for the capsule to reach the target.

    2) DistanceTolerance - Specifies how close the capsule must be to the target location for it to be considered correctly positioned.

    3) RotUpdateStartTime - A normalized time value indicating when to start interpolating the capsules rotation to match the
       specified TargetRotation.

    4) ApplyDeceleration - If enabled, the system will account for braking time (using BrakingDecelerationWalking) when calculating how
       long it takes to reach the target. This may improve accuracy.

    5) StopWhenInputPressedTime - Stops the movement if the player physically presses a movement input. This requires providing the
       MovementInput on the input pin.
    */
    UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", AutoCreateRefTerm = "MovementInput", BlueprintInternalUseOnly = "TRUE", AdvancedDisplay = 5))
    static UAbilityTask_MoveByInputComplex* MovePawnByInputWithStop(UGameplayAbility* OwningAbility, FName TaskInstanceName, FVector TargetLocation, FRotator TargetRotation, FName AxisNameForward = "MoveForward/Backwards", FName AxisNameRight = "MoveRight/Left",
        float MaxDuration = 1.0, float DistanceTolerance = 10.0, float RotUpdateStartTime = 0.5, float RotationInterpSpeed = 10.0, bool UseLocationFixAtEnd = false, 
        bool ApplyDeceleration = false, float StopWhenInputPressedTime = 0.05, bool EndTaskWhenInputPressed = false);

    virtual void Activate() override;

    virtual void TickTask(float DeltaTime) override;

    UPROPERTY(BlueprintAssignable)
    FOnDurningMove3 DurningMove;

    UPROPERTY(BlueprintAssignable)
    FOnTargetLocationReached3 TargetLocationReached;

    UPROPERTY(BlueprintAssignable)
    FOnFailed3 Failed;

    UPROPERTY(BlueprintAssignable)
    FOnStopped3 Stopped;

protected:
    UPROPERTY()
    FVector TargetLocation;

    UPROPERTY()
    float MaxDuration;

    UPROPERTY()
    float DistanceTolerance;

    UPROPERTY()
    FRotator TargetRotation;

    UPROPERTY()
    float RotUpdateStartTime;

    UPROPERTY()
    float RotationInterpSpeed;

    UPROPERTY()
    bool UseLocationFixAtEnd;

    UPROPERTY()
    bool ApplyDeceleration;

    UPROPERTY()
    float ElapsedTime;

    UPROPERTY()
    float StopWhenInputPressedTime;

    UPROPERTY()
    bool EndTaskWhenInputPressed;

    UPROPERTY()
    float InputPressTime;

    UPROPERTY()
    bool MovementStopped;

    UPROPERTY()
    FName InputAxisFB;

    UPROPERTY()
    FName InputAxisRL;

};
