

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PairedAttackSequenceData.h"
#include "AGLS_ZombieAttacksComponentCore.generated.h"


USTRUCT(BlueprintType)
struct FPairedAttackSeqInteractionAnim : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paired Attack")
	bool ActivatedAttack = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paired Attack")
	UAnimSequence* MainAnimation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paired Attack")
	UAnimSequence* AddtiveAnimation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paired Attack")
	float AnimationTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paired Attack")
	float AddativeAnimStrength = 1.0;

};





DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAttackStarted, float, ActionDuration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAttackEnded, float, ActionElapsedTime);

/*
Implements the attack system for the Character. If the component has been declarated in AGLS_ZombieCharacter 
then in this case we have several functions at our disposal:
- bool GetIsStartedAttack()
- float GetAttackElapsedTime()
- DELEGATE OnAttackStarted()
- DELEGATE OnAttackEnded() */
UCLASS(Blueprintable, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELPFULFUNCTIONS_API UAGLS_ZombieAttacksComponentCore : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UAGLS_ZombieAttacksComponentCore();

	UPROPERTY(BlueprintAssignable, Category = "Attack Action")
	FOnAttackStarted OnAttackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Attack Action")
	FOnAttackEnded OnAttackEnded;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "References"))
	ACharacter* RefChar = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "References"))
	AController* RefController = nullptr;

	//Main Variable
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core"))
	bool bAttackStarted = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core"))
	float AttackTotalDuration = -1.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core"))
	float AttackElapsedTime = 0.0;

	//Paired Attacks Sequences (Dynamics)
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core|Paired Attack"))
	FTransform SequenceOrigin = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core|Paired Attack"))
	FHitResult PairedAttackHitData;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core|Paired Attack"))
	TSubclassOf<APairedAttackSequenceData> ChoosedPairedAttackClass;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Attacks Core|Paired Attack"))
	FVector PairedAttackAditionalData = FVector(0, 0, 0);

	// When true, paired/grab attack flow is hard-disabled at component level.
	// Normal zombie locomotion remains unaffected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zombie Attacks Core|Paired Attack")
	bool bDisablePairedAttackSequences = true;


	// ||||||||||||||||||||||||||||||| Debugging Variables ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||

	//Showing Peformed Traces by this component (On Per Frame or For Duration)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debugging"))
	bool bDrawDebugTraces = false;

	//When it is enable during game component drawing Debug Shapes like: Spheres, Lines, Poins or Arrows
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debugging"))
	bool bDrawDebugShapes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debugging", EditCondition = "bDrawDebugTraces"))
	float TracesViewDurationMultiply = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Debugging", EditCondition = "bDrawDebugShapes"))
	float ShapesViewDurationMultiply = 1.0;


	UFUNCTION(BlueprintCallable, Category = "Zombie Attacks Core")
	void CallOnAttackStarted();

	UFUNCTION(BlueprintCallable, Category = "Zombie Attacks Core")
	void CallOnAttackEnded();

	UFUNCTION(BlueprintCallable, Category = "Zombie Attacks Core")
	bool CorrectlySetAttackTimer(bool StartAttacking, float InDuration);

};
