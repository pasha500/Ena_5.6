


#include "AGLS_CharacterMovementComponent.h"

float UAGLS_CharacterMovementComponent::GetMinAnalogSpeed() const
{
	return Super::GetMinAnalogSpeed();
}

float UAGLS_CharacterMovementComponent::GetMaxSpeed() const
{
	return Super::GetMaxSpeed();
}

void UAGLS_CharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}


void UAGLS_CharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{

	if (GetOwner())
	{
		Acceleration.Z = 0.f; //Always Set Non Acceleration for Z Axis

		if (bApplyRootMotionForCustomMove) // Apply When Using RootMotion
		{
			RestorePreAdditiveRootMotionVelocity();
			// Ensure velocity is horizontal.
			MaintainHorizontalGroundVelocity();
		}

		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && UpdateCustomMovementPhysic)
		{
			if (GetOwner() && deltaTime > KINDA_SMALL_NUMBER)
			{
				if (PrevPosition.Length() > KINDA_SMALL_NUMBER) //We need make sure the prev position is non zero
				{
					Velocity = (GetOwner()->GetActorLocation() - PrevPosition) / deltaTime; //Calculate real Velocity Vector

					//Simplified Acceleration Calculation
					const float MaxAccel = GetMaxAcceleration();
					if (Acceleration.SizeSquared() > UE_SMALL_NUMBER)
					{
						Acceleration = Acceleration.GetSafeNormal() * MaxAccel;
					}
					else
					{
						Acceleration = MaxAccel * (Velocity.SizeSquared() < UE_SMALL_NUMBER ? UpdatedComponent->GetForwardVector() : Velocity.GetSafeNormal());
					}

					PrevVelocity = Velocity; // This value is required to calculate real Acceleration Value (Not Used)
					if (Velocity.Length() > KINDA_SMALL_NUMBER) //AnalogInputModifier is value informing that character is changing position in time (Velocity.Length > 0)
					{
						AnalogInputModifier = 1.f;
						MaxCustomMovementSpeed = Velocity.Length(); //Set MaxCustomSpeed As Current Velocity Lenght
					}
					else
					{
						AnalogInputModifier = 0.0; //No Velocity so Imput = 0.0
					}
					
				}
				PrevPosition = GetOwner()->GetActorLocation(); // This value is required to calculate real Velocity
			}
		}
		else if(bApplyRootMotionForCustomMove) //Apply root motion (EXPERIMENTAL)
		{
			ApplyRootMotionToVelocity(deltaTime);
			if (IsMovingOnGround())
			{
				MaintainHorizontalGroundVelocity();
			}
		}
		Super::PhysCustom(deltaTime, Iterations);
	}
}
