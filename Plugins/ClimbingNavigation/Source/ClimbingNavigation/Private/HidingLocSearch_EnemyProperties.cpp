


#include "HidingLocSearch_EnemyProperties.h"

#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"


void UHidingLocSearch_EnemyProperties::GetPredictedEnemyTransform_Implementation(FVector& ReturnLocation, FRotator& ReturnRotation, ACharacter* CurrentEnemy, ACharacter* Searcher, int IterationIndex)
{
	if (CurrentEnemy)
	{
		ReturnLocation = CurrentEnemy->GetActorLocation() - FVector(0, 0, CurrentEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		ReturnRotation = CurrentEnemy->GetActorRotation();
	}
}

void UHidingLocSearch_EnemyProperties::GetCurrentEnemySightParams_Implementation(float& HalfAngle, float& SightRadius, float& SightHeight, ACharacter* CurrentEnemy, ACharacter* Searcher)
{
	HalfAngle = -1.0;
	SightRadius = 0.0;
	SightHeight = 0.0;
}
