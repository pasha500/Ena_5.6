

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AGLS_Zombie_ControllerAI.generated.h"


UENUM(BlueprintType)
enum class EControllerAI_ControlRotMode : uint8
{
	FromControlRotation,
	FromNotZeroVelocity,
	FromCurrentAcceleration,
	FromFocusPoint,
	FromCustomDefinition
};


/**
 * 
 */
UCLASS(Abstract)
class HELPFULFUNCTIONS_API AAGLS_Zombie_ControllerAI : public AAIController
{
	GENERATED_BODY()
	
public:


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Config"))
	float WhenSeeReactionSpeed = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Config"))
	float WhenLostSightReactionSpeed = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Config"))
	EControllerAI_ControlRotMode YawControlRotDesiredMode = EControllerAI_ControlRotMode::FromNotZeroVelocity;


	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Perception"))
	float EnemyDetectionTime = 0.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Perception"))
	bool bDetectedEnemy = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Controller|Perception"))
	ACharacter* ChoosedEnemyActor = nullptr;


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie Controller|Perception", meta = (ForceAsFunction))
	void UpdateSightPerceptionValues();
	virtual void UpdateSightPerceptionValues_Implementation();

	UFUNCTION(BlueprintPure, Category = "Zombie Controller|Navigation")
	bool DoesPathUseNavLink(UNavigationPath* Path, float MaxDistanceToPoint2D = 200.0, float MaxHeightDiff = 50.0);

	UFUNCTION(BlueprintPure, Category = "Zombie Controller|Navigation")
	float PathWeightByNavLinksNumber(UNavigationPath* Path, float MaxDistanceToPoint2D = 200.0, float MaxHeightDiff = 50.0, float Bias = 0.1, bool UseAbsOnHeight = true);


	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie Controller|Rotation", meta = (ForceAsFunction))
	void CustomUpdateControlRotation(float DeltaTime, bool bUpdatePawn);
	virtual void CustomUpdateControlRotation_Implementation(float DeltaTime, bool bUpdatePawn);


};
