

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AGLS_CharacterMovementComponent.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API UAGLS_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()


public:

	virtual float GetMinAnalogSpeed() const override;
	virtual float GetMaxSpeed() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	//Specifies whether physical variables such as Velocity and Acceleration are to be calculated during the 'CustomMovement' state.Therefore, in this mode, 
	// PoseSearch will be able to generate trajectories based on the change in the position of the capsule
	UPROPERTY(BlueprintReadWrite, Category = "Character Movement: Custom Movement", EditAnywhere)
	bool UpdateCustomMovementPhysic = true;

	//(NOT FINISHED) It allows to set the capsule in motion in Custom Movement mode while Root Motion animation is playing
	UPROPERTY(BlueprintReadWrite, Category = "Character Movement: Custom Movement", EditAnywhere)
	bool bApplyRootMotionForCustomMove = false;

protected:

	FVector PrevPosition = FVector(0, 0, 0);
	FVector PrevVelocity = FVector(0, 0, 0);
	
};
