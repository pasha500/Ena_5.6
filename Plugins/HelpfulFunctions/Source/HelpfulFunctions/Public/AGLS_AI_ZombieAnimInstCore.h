

#pragma once

#include "CoreMinimal.h"
#include "AGLS_AI_CharacterInterface.h"
#include "ALS_HumanAI_InterfaceCpp.h"
#include "AGLS_AI_AnimInstanceBase.h"
#include "AGLS_AI_ZombieAnimInstCore.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API UAGLS_AI_ZombieAnimInstCore : public UAGLS_AI_AnimInstanceBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	bool UseCollideDatabasesForCharact = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	TArray<TEnumAsByte<EObjectTypeQuery>> CollisionObject;

	//Choosed by current Gait Value. First Index = Walking, Second Index = Running, Third Index = Sprinting
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	TArray<USoundBase*> FootStepSoundClasses;

	UPROPERTY(BlueprintReadWrite, Category = "References", meta = (AllowPrivateAccess = "True"))
	const UBlendProfile* BlendProfile_SlowHead = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "References", meta = (AllowPrivateAccess = "True"))
	const UBlendProfile* BlendProfile_FastFoots = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Other", meta = (AllowPrivateAccess = "True"))
	bool PrevCapsuleColliding = false;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Other", meta = (AllowPrivateAccess = "True"))
	FTransform TraversalInteraction = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Other", meta = (AllowPrivateAccess = "True"))
	float PrevDetectedEnemyTime = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Other", meta = (AllowPrivateAccess = "True"))
	float RandomJiggleAlpha = 0.0;



	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Bump Reaction", meta = (AllowPrivateAccess = "True"))
	bool PlayBumpReaction = false;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Bump Reaction", meta = (AllowPrivateAccess = "True"))
	FVector BumpReactionAnimData = FVector::ZeroVector;



	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Traversal", meta = (AllowPrivateAccess = "True"))
	bool StartedTraversalAction = false;



	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Foot Steps", meta = (AllowPrivateAccess = "True"))
	FVector PrevFootLocation_L = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Foot Steps", meta = (AllowPrivateAccess = "True"))
	FVector PrevFootLocation_R = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Foot Steps", meta = (AllowPrivateAccess = "True"))
	float FootPlantedTime_L = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Foot Steps", meta = (AllowPrivateAccess = "True"))
	float FootPlantedTime_R = 1.0;



	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Look At", meta = (AllowPrivateAccess = "True"))
	float LookAtEnemyAlpha = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Look At", meta = (AllowPrivateAccess = "True"))
	float LookingSweepTime = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Look At", meta = (AllowPrivateAccess = "True"))
	FVector LookAtEnemyLocation = FVector::ZeroVector;


	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Attacking", meta = (AllowPrivateAccess = "True"))
	bool StartedAttackSeq = false;

	UPROPERTY(BlueprintReadWrite, Category = "Graph Core|Attacking", meta = (AllowPrivateAccess = "True"))
	float NearEnemyAttackingAlpha = 0.0;


	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;


private:
	float StanceTransitionTimer = 0.0;

protected:

	IAGLS_AI_CharacterInterface* CharInterface = nullptr;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Update Per Frame On Tick", Keywords = "Update,Tick"))
	void UpdatePerFrameOnTick();
	virtual void UpdatePerFrameOnTick_Implementation();


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Foots Steps Player", Keywords = "Update,Sounds"))
	void FootsStepsPlayerC();
	virtual void FootsStepsPlayerC_Implementation();

	UFUNCTION(BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Single Foot Step Detection", Keywords = "Zombie,Blueprint Update"))
	void SingleFootStepDetectionC
	(
		USoundBase* SoundClass,
		UPARAM(ref) float& PlantedTime,
		UPARAM(ref) FVector& PrevFootLocation,
		FTransform RootTransform,
		FName FootBone = "foot_l",
		FName FootCurveSpeed = "FootSpeed_R",
		float DeltaTime = 0.01,
		float TraceOffsetUp = 8.0,
		float TraceOffsetDown = 10.0,
		float FootNotMoveTollerance = 5.0,
		float FootNotMoveOffset = 0.0,
		FLinearColor DebugColor = FLinearColor(1.0, 0.0, 0.0)
	);

	UFUNCTION(BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Update Values From Character", Keywords = "Interface,Update,Character", BlueprintThreadSafe))
	void UpdateValuesFromCharacterC();

	UFUNCTION(BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Set Corrected Is Collide Value", Keywords = "Collision,Motion,Matching"))
	void SetCorrectedIsCollideValueC(int DebugModeIndex = 0, float DebugTime = 0.1, float CapRadiusBias = 8.0, float CapHeightBias = -10.0);

	UFUNCTION(BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Set Aim At Enemy Properties", Keywords = "Zombie,Blueprint Update"))
	void SetAimAtEnemyPropertiesC();

	UFUNCTION(BlueprintCallable, Category = "Anim Blueprint Logic", meta = (DisplayName = "Set Alpha For Near Attacking", Keywords = "Zombie,Blueprint Update", AdvancedDisplay = 1))
	void SetAlphaForNearAttackingC(FVector2D DistanceRangeIn = FVector2D(140,200), FVector2D EnemyDetectRangeIn = FVector2D(0.4,0.8));

	//Blueprint Bindable


	UFUNCTION(BlueprintPure, Category = "Anim Blueprint Graph", meta = (DisplayName = "Addtive Slot Weight", Keywords = "Anim Graph,Slot,Weight", BlueprintThreadSafe))
	float GetAddtiveSlotWeight();

	UFUNCTION(BlueprintPure, Category = "Anim Blueprint Graph", meta = (DisplayName = "Base Layer Slot Weigth", Keywords = "Anim Graph,Slot,Weight", BlueprintThreadSafe))
	float GetBaseLayerSlotWeigth();

	UFUNCTION(BlueprintPure, Category = "Anim Blueprint Graph", meta = (DisplayName = "FootsIK Alpha", Keywords = "Anim Graph,IK,Weight", BlueprintThreadSafe))
	float GetFootsIK_Alpha();


};
