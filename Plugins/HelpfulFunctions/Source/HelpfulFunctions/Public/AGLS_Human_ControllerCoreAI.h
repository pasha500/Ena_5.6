

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AGLS_Zombie_ControllerAI.h"
//#include "ALS_StructuresAndEnumsCpp.h"
#include "GameFramework/Character.h"
//#include "AGLS_AI_HumanCharInterface.h"
#include "AGLS_AI_CharacterInterface.h"
#include "AGLS_HumanAI_CharacterBase.h"
#include "GameplayStateTreeModule/Public/Components/StateTreeAIComponent.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeInstanceData.h"
#include "StateTreePropertyBindings.h"
#include "Kismet/KismetMathLibrary.h"
#include "AGLS_Human_ControllerCoreAI.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API AAGLS_Human_ControllerCoreAI : public AAIController
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|References", DisplayName = "RefChar"))
	ACharacter* ControlledCharacter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Config"))
	float WhenSeeReactionScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Config"))
	float WhenLostSightReactionScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Config"))
	EControllerAI_ControlRotMode YawControlRotDesiredMode = EControllerAI_ControlRotMode::FromNotZeroVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Config"))
	bool bUseCustomControlRotationCode = true;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Perception"))
	float EnemyDetectionTime = 0.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Perception"))
	bool bDetectedEnemy = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Perception"))
	ACharacter* ChoosedEnemyActor = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Rotation"))
	FVector NonZeroVelocityDirection = FVector(1, 0, 0);

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Rotation"))
	FVector FocalPointBumpReaction = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Rotation"))
	FVector FocalPointOnDamageCauser = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Rotation"))
	float FocusOnDamageCauserInterpSpeed = 10.0;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Human Controller Core|Debug", EditCondition = "bUseCustomControlRotationCode"))
	bool bDrawDebugAboutAdvancedRotation = false;


	FVector CustomFocusPoint = FVector(0, 0, 0);



	UFUNCTION(BlueprintPure, Category = "Human Controller Core|Blackboard Keys")
	const AGLS_HumanAI_MainBehaviorMode BB_GetMainBehaviorMode();

	UFUNCTION(BlueprintPure, Category = "Human Controller Core|Blackboard Keys")
	const AGLS_HumanAI_PatrolingMode BB_GetPatrolingMode();

	UFUNCTION(BlueprintPure, Category = "Human Controller Core|Blackboard Keys")
	const AGLS_HumanAI_FightingMode BB_GetFightingMode();

	UFUNCTION(BlueprintCallable, Category = "Human Controller Core|Blackboard Keys")
	void BB_SetMainBehaviorMode(AGLS_HumanAI_MainBehaviorMode NewValue);

	UFUNCTION(BlueprintCallable, Category = "Human Controller Core|State Tree Parameters")
	bool ST_SetParameterAsObject(UStateTreeAIComponent* InComponent, const FName ParameterName, UObject* ObjectToSet);



	UFUNCTION(BlueprintPure, Category = "Human Controller Core|Navigation")
	bool DoesPathUseNavLink(UNavigationPath* Path, float MaxDistanceToPoint2D = 200.0, float MaxHeightDiff = 50.0);

	UFUNCTION(BlueprintPure, Category = "Human Controller Core|Navigation")
	float PathWeightByNavLinksNumber(UNavigationPath* Path, float MaxDistanceToPoint2D = 200.0, float MaxHeightDiff = 50.0, float Bias = 0.1, bool UseAbsOnHeight = true);


	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human Controller Core|Rotation", meta = (ForceAsFunction))
	void CustomUpdateControlRotation(float DeltaTime, bool bUpdatePawn);
	virtual void CustomUpdateControlRotation_Implementation(float DeltaTime, bool bUpdatePawn);


	UFUNCTION(BlueprintCallable, Category = "Human Controller Core|Rotation", meta = (ForceAsFunction, AdvancedDisplay = 6))
	void AdvancedControlRotationForHumanAI(
		float DeltaTime,
		bool bUpdatePawn,
		TArray<FName> ShootingTargetsBonesName,
		FName GunMuzzleSocketName = TEXT("BulletStart"),
		float ShotDirectionIncludeVelocityAlpha = 0,
		float BlendTraceStartWithWeaponMuzzle = 0,
		FName SmartObjectRotationKey = TEXT("DesiredSmartObjectRotation"),
		float SpeedWhenGoToSmartObject = 6.0,
		float SpeedWhenFocalPoint = 8.0,
		float SpeedWhenBumpReaction = 3.0,
		float SpeedWhenFocusOnEnemy = 8.0,
		float SpeedWhenFocusActor = 8.0,
		float SpeedWhenRecivedDamage = 10.0,
		FVector2D SpeedRangeWhenDefault = FVector2D(8.0, 2.0)
	);


	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Human Controller Core|Rotation", meta = (ForceAsFunction))
	FVector CustomDefaultControlRotDirection();
	virtual FVector CustomDefaultControlRotDirection_Implementation();

/*This function is designed to change the current direction of movement so that characters can avoid each other. The need 
for this solution stems from the fact that, by default, PathFollowing doesn't account for dynamic obstacles in the path 
such as other characters. This can be avoided using RuntimeNavMeshModify, but it seems to be a more expensive solution. 
This function forces the controlled Character to change its movement direction via AddMovementInput when a Character is 
in the path. Note that this function is not responsive and is designed to work only with HumanAI.*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human Controller Core|Movement", meta = (ForceAsFunction, AdvancedDisplay = 1))
	bool TryToAvoidOtherCharacter(UPARAM(ref) FVector& FollowDirection, float TraceRadiusScale = 1.0, float TraceLenghtOffset = 0, float PerGaitInputStrength = 1.0, bool DrawTrace = false);
	virtual bool TryToAvoidOtherCharacter_Implementation(UPARAM(ref) FVector& FollowDirection, float TraceRadiusScale = 1.0, float TraceLenghtOffset = 0, float PerGaitInputStrength = 1.0, bool DrawTrace = false);


	virtual void Tick(float DeltaTime) override;


	virtual void SetFocalPoint(FVector NewFocus, EAIFocusPriority::Type InPriority = EAIFocusPriority::Gameplay) override;
	virtual void ClearFocus(EAIFocusPriority::Type InPriority) override;

	UFUNCTION(BlueprintCallable, Category = "AI")
	FVector GetCustomFocalPoint() const;

private:

	bool ControlRotationAsSelfHeadSocket(bool& SkipUpdate, FRotator& OutDesiredRotation, FRotator InDesiredRotation);
	FRotator ControlRotationAsAimInEnemyHead(TArray<FName> InShootingTargetsBonesName, FName InGunMuzzleSocketName, FName ShootingStartSocketName, float ShotDirectionIncludeVelocityAlpha, float BlendTraceStartWithWeaponMuzzle);
	FRotator ControlRotationOnFocusActor(FRotator InDesiredRot);
	void DrawDebugRotation(FRotator CurrentRot, FString Description, FColor Color = FColor::Yellow, float ArrowLenght = 45.0);
	void MakeSmoothSpeedValueWhenAiming(float dt);

	template<typename T>
	FORCEINLINE T& IgnoreOut()
	{
		// Oddzielny bufor na w¹tek; bezpieczny dla Game Thread.
		static thread_local T Dummy{};   // wymaga domyœlnego konstruktora / trivialnego T
		return Dummy;
	}

	FName RandomEnemyBoneName = TEXT("head");
	float TimeToSetNewRandomBone = -1.0;
	float DefaultControlInterpSpeedScale = 0.0;

};
