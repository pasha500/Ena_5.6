

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreePropertyRef.h"
#include "StateTreeExecutionContext.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "Curves/CurveFloat.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "STE_HumanAI_SightPerceptionCore.generated.h"

/**
 The Evaluator class code for StateTree updates the values ​​of variables related to SightPerception. The entire implementation is designed specifically 
 for HumanAI. The main result of this Evaluator is the correct completion of the SightPerceptionRef structure. 
 Additionally, if BackboardData != nullptr, then values ​​are set for certain keys, e.g.:
- (object)StateTreePerceptionEvaluatorRef,
- (bool)EnemyIsPhysicallySeen,
- (bool)IsCurrentlySeeEnemy,
- (float)SeeEnemyTime,
- (float)EnemySeeCheckerAlpha

Scheme for reading and writing data from an object of type FStateTreeBlueprintPropertyRef:

binds from ST __________________ working on copied values    _____________________  working on copied values   ______________________   [Next Tick]
>-----------> ReadST PropertyRef --------------------------> UpdateSightProperties --------------------------> WriteTo ST_PropertyRef ------------->
			 |_________________|                            |____________________|                            |_____________________|

PL:
Kod klasy Evaluator dla StateTree, realizujący aktualizajcę wartość zmiennych związanych z SightPerception. Cała implementacja jest przygotowana 
tylko pod działanie z HumanAI. Głównym rezultatem działania tego Evaluator jest prawidłowe wypełnienie struktury SightPerceptionRef. Dodatkowo 
jeżeli BackboardData != nullptr, to wtedy ustawiane są wartości dla niektórych kluczy np:
- (object)StateTreePercpetionEvaluatorRef,
- (bool)EnemyIsPhysicallySeen,
- (bool)IsCurrentlySeeEnemy,
- (float)SeeEnemyTime,
- (float)EnemySeeCheckerAlpha
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class HELPFULFUNCTIONS_API USTE_HumanAI_SightPerceptionCore : public UStateTreeEvaluatorBlueprintBase
{
	GENERATED_BODY()

public:


	UPROPERTY(EditAnywhere, Category = "Context")
	AActor* Actor;

	UPROPERTY(EditAnywhere, Category = "Context")
	AAIController* AIController;

	UPROPERTY(BlueprintReadWrite, Category = "References")
	ACharacter* Char = nullptr;


	/*A reference to a parameter declared in the StateTree. Its correct completion is crucial to the Evaluator's operation. 
	For the code to function correctly, it's necessary to assign an appropriate pointer to this variable.*/
	UPROPERTY(EditAnywhere, Category = "Binding", meta = (RefType = "/Script/HelpfulFunctions.AGLS_HumanAI_EnemyTags"))
	FStateTreeBlueprintPropertyRef SightPerceptionRef;

	UPROPERTY(EditAnywhere, Category = "Binding", meta = (RefType = "/Script/HelpfulFunctions.AGLS_HumanAI_MainBehaviorMode"))
	FStateTreeBlueprintPropertyRef MainBehaviorModeRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	AGLS_LOD_State CurrentLOD = AGLS_LOD_State::LOD0;

	/*An object that holds a reference to the Blackboard Component. By default, a Blackboard instance is created for the HumanAI Controller. 
	You should assign an appropriate pointer to this object here.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	UBlackboardComponent* BlackboardData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	AActor* TargetFocusActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	ACharacter* PrevEnemyChar;

	/*The basic value responsible for whether the Evaluator should be actively running. If set to true, then, along with the Tick, 
	information about SightPerception is processed to fill the SightPerceptionRef structure.*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	bool bRunSourceLogicOnTick = true;

	/*Indication of what type of Perception class data about registered actors should be retrieved from*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	TSubclassOf<UAISense> SenseToUse = UAISense_Sight::StaticClass();

	/*Data needed to configure the refresh rate of certain data. This determines how often data will be collected from the 
	PerceptionComponent. For higher LOD values, the refresh rate should be less frequent (a larger float value, e.g., for 
	LOD3, it might be 0.5s or 1.0s).*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	TMap<AGLS_LOD_State, float> RefresingPerceptionIntervalsMap;

	/*A map defining the speed at which the reaction time to a detected enemy should be incremented. The higher the value, 
	the faster perception data will indicate the need to transition to a new state, e.g., CurrentlySeeEnemy. This value 
	is multiplied by DeltaTime.*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	TMap<AGLS_HumanAI_SightStatus, float> DeltaTimesWhenSeePerState;

	/*Works similarly to DeltaTimesWhenSeePerState, but this data is retrieved when the AI ​​currently does not see any valid enemy.*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	TMap<AGLS_HumanAI_SightStatus, float> DeltaTimesWhenNotSeePerState;

	UPROPERTY(EditAnywhere, Category = "Perception Config")
	FName EnemyActorBlackboardKey = TEXT("CurrentEnemyChar");

	/*
	ENG:
	This curve is active when SightPerception determines the weight for registered enemies. If the perception component has 
	registered more than one character, a mechanism comes into play that determines the weight for each character. The higher 
	the final weight, the character assigned to that value becomes the active enemy. This curve is important for calculating 
	the weight of the distance between SightPerceptionOwner and the detected character.
	PL:
	Krzywa aktywna w momencie okreslania wagi dla zajerejestrowanych wrogow przez SightPerception. W przypadku kiedy komponent 
	percepcji zajerestrowal wiecej niz 1 charakter, do gry wkracza mechanizm okreslajacy dla kazdej postaci wage. Czym finalna 
	waga bedzie wyzsza, to wtedy aktywnym wrogiem staje sie charakter przypisany do tej wartosci. Krzywa ta jest istotna do 
	obliczania wagi odleglosci miedzy SightPerceptionOwner oraz wykrytym Charakterem.
	*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	UCurveFloat* PerceptionDistWeightCurve = nullptr;

	/*An important parameter that determines the perception status (enum AGLS_HumanAI_SightStatus). It affects the change from 
	'CurrentlySeeEnemy' to 'LostSight'. The higher the value, the faster the AI ​​will switch to LostSight after losing sight of 
	enemies.*/
	UPROPERTY(EditAnywhere, Category = "Perception Config", meta = (ClampMin = "0.2", ClampMax = "0.94"))
	float LostSightTimeTreshold = 0.65;

	/*Determines how long the LostSightLocation value will be updated after enemies are lost from sight.*/
	UPROPERTY(EditAnywhere, Category = "Perception Config", meta = (ClampMin = "0.2", ClampMax = "0.98"))
	float LostSightPointUpdateTimeTreshold = 0.8;

	/*The collision channel is needed to determine whether an enemy is hiding in the Foliage. For this condition to be met, 
	proper preparation of Foliage-related data is necessary. By default, for AGLS, this channel is named 'Lost_AI_Sight'*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	TEnumAsByte< ECollisionChannel> FoliageCollisionChannel = ECollisionChannel::ECC_EngineTraceChannel5;

	/*GameplayTags container, which are checked when determining the weight of Characters within the SightComponent's field of 
	view. If a given instance has any of these tags, a lower weight value is assigned to it - TotalWeight = TotalWeight * 0.5*/
	UPROPERTY(EditAnywhere, Category = "Perception Config")
	FGameplayTagContainer TagsThatEnemyShouldNotHave;

	UPROPERTY(EditAnywhere, Category = "Perception Config|Debuging", meta = (ClampMin = "0", ClampMax = "2"))
	int DrawDebugMode = 0;

	UPROPERTY(EditAnywhere, Category = "Perception Config|Debuging")
	int DrawingShapesDepth = 0;

	UPROPERTY(EditAnywhere, Category = "Perception Config|Debuging")
	float DrawingStringFontScale = 1.0;



	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	float CurrentElapsedTime = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	float CurrentTickInterval = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	float DesiredSightDetectionValue = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	float EnemyDetectionTime = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	float LastDetectionTime = 0.55;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	bool PotencialySeeSomething = false;

	UPROPERTY(BlueprintReadWrite, Category = "Task Runtime")
	AGLS_HumanAI_SightStatus DesiresSightState = AGLS_HumanAI_SightStatus::SeesNothing;


	/* DEFAULT CODE:
		CurrentElapsedTime = CurrentElapsedTime + dt;
	if (CurrentElapsedTime > CurrentTickInterval)
	{
		CurrentElapsedTime = 0.0;
		if (RefresingPerceptionIntervalsMap.Num() > 0)
		{
			if (float* d = RefresingPerceptionIntervalsMap.Find(CurrentLOD))
			{
				CurrentTickInterval = *d;
				return true; }
			else
			{ return true; }
		} }
	return false;
	*/
	UFUNCTION(BlueprintCallable, Category = "Perception Update|Optimalization", meta = (ForceAsFunction))
	bool CheckShouldRefresh(float dt);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Perception Update|Core", meta = (ForceAsFunction))
	float UpdateSightPerceptionValues();
	virtual float UpdateSightPerceptionValues_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Utilities", meta = (ForceAsFunction))
	bool GetCharacterIsDead(ACharacter* InCharacter);
	virtual bool GetCharacterIsDead_Implementation(ACharacter* InCharacter);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Utilities", meta = (ForceAsFunction))
	bool ModifySightForPlayerCompanion(ACharacter* InCharacter);
	virtual bool ModifySightForPlayerCompanion_Implementation(ACharacter* InCharacter);

	UFUNCTION(BlueprintCallable, Category = "Perception Update|Utilities", meta = (ForceAsFunction))
	void SetBB_EnemyActor(ACharacter* InEnemy);

	UFUNCTION(BlueprintCallable, Category = "Perception Update|Utilities", meta = (ForceAsFunction))
	bool SetNewEnemyButRememberPrev(ACharacter* NewEnemy);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Utilities", meta = (ForceAsFunction))
	void GetSightPerceptionTagsFromCharacter(bool& IsZombie, bool& EnemySpottedHim, bool& ShouldHideSelf, ACharacter* InEnemy);
	virtual void GetSightPerceptionTagsFromCharacter_Implementation(bool& IsZombie, bool& EnemySpottedHim, bool& ShouldHideSelf, ACharacter* InEnemy);

	UFUNCTION(BlueprintCallable, Category = "Perception Update|Core", meta = (ForceAsFunction))
	void UpdatePerFrameSightValues(float dt);

	/* Default CODE:
		if (!Char) { return 0.1; }
	float ByDistanceSpeedBias = 0.0;
	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (float DistTo = FVector::Distance(SIGHTPROPERTIES.EnemyCharacter->GetActorLocation(), Char->GetActorLocation()) < 300)
		{ ByDistanceSpeedBias = KML::MapRangeClamped(DistTo, 30, 300, 3, 0); }
	}
	float ByAnimCurveBias = 0.0;
	if (Char->GetMesh()->GetAnimInstance())
	{ ByAnimCurveBias = Char->GetMesh()->GetAnimInstance()->GetCurveValue("WhenSeeDetectionSpeedBias"); }
	if (DeltaTimesWhenSeePerState.Num() == 0) { return 0.6 + ByDistanceSpeedBias + ByAnimCurveBias; }
	float* MapValue = DeltaTimesWhenSeePerState.Find(DesiresSightState);
	return *MapValue + ByDistanceSpeedBias + ByAnimCurveBias;
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Utilities", meta = (ForceAsFunction, CompactNodeTitle = "WhenSeeInterpSpeed"))
	float GetWhenCurrentSeeInterpSpeed();
	virtual float GetWhenCurrentSeeInterpSpeed_Implementation();

	/* Default CODE:
		if (!Char) { return 0.1; }
	float ByDistanceSpeedBias = 0.0;
	float ByDeadSpeedBias = 0.0;
	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (float DistTo = FVector::Distance(SIGHTPROPERTIES.EnemyCharacter->GetActorLocation(), Char->GetActorLocation()) > 300)
		{ ByDistanceSpeedBias = KML::MapRangeClamped(DistTo, 300, 3000, 0, 0.5); }
		const bool EnemyIsDead = GetCharacterIsDead(SIGHTPROPERTIES.EnemyCharacter);
		if (EnemyIsDead) { ByDeadSpeedBias = 0.2; }
	}
	if (DeltaTimesWhenNotSeePerState.Num() == 0) { return 0.2 + ByDistanceSpeedBias; }
	float* MapValue = DeltaTimesWhenNotSeePerState.Find(DesiresSightState);
	return KML::FClamp(*MapValue + ByDistanceSpeedBias + ByDeadSpeedBias, 0.005, 2.0);
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Utilities", meta = (ForceAsFunction, CompactNodeTitle = "WhenNotSeeInterpSpeed"))
	float GetWhenCurrentNOTSeeInterpSpeed();
	virtual float GetWhenCurrentNOTSeeInterpSpeed_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Perception Update|Core", meta = (ForceAsFunction))
	void GetSightParametersFromData(float& EnemySpottedEnemyReductionSpeed, float& SeeReactionIncreaseSpeed, float& SeeReactionReduceSpeed);
	virtual void GetSightParametersFromData_Implementation(float& EnemySpottedEnemyReductionSpeed, float& SeeReactionIncreaseSpeed, float& SeeReactionReduceSpeed);


	UFUNCTION(BlueprintCallable, Category = "Perception Update|Debug")
	void DrawDebugAboutResultOnTick();

	UFUNCTION(BlueprintCallable, Category = "Perception Update|Debug")
	void DrawPerEnemyDebugInfo(ACharacter* InEnemy, float WeightDistance, float WeightPlayer, float WeightTags, float WeightSee, float Total);


	virtual void TreeStart(FStateTreeExecutionContext& Context) override;
	virtual void Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	void ResolveStateTreeProperties(FStateTreeExecutionContext& Context);
	void WriteToStateTreeProperties(FStateTreeExecutionContext& Context, AGLS_HumanAI_MainBehaviorMode& ModeState, FAGLS_HumanAI_EnemyTags& EnemyTags) const;


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Perception Update|Core")
	void ForceSetPropertiesIgnoringSightComponent(
		ACharacter* SourceActor, 
		AGLS_HumanAI_MainBehaviorMode SourceBehaviorMode, 
		AGLS_HumanAI_FightingMode SourceFightingMode, 
		FAGLS_HumanAI_EnemyTags SourceSightProperties, 
		float WaitTime, 
		float Chance
	);
	virtual void ForceSetPropertiesIgnoringSightComponent_Implementation(
		ACharacter* SourceActor,
		AGLS_HumanAI_MainBehaviorMode SourceBehaviorMode,
		AGLS_HumanAI_FightingMode SourceFightingMode,
		FAGLS_HumanAI_EnemyTags SourceSightProperties,
		float WaitTime,
		float Chance
	);

	virtual void ForceSetSightEnemyParams(FAGLS_HumanAI_EnemyTags InSightParams, AGLS_HumanAI_SightStatus RequiredSightStatus, bool bEnemySpottedHim);
	float WaitOnForceSetParams = -1.0;

private:

	bool SimpleCheckDoesEnemySee(const FRotator& CenterAngle, const FVector& StartPosition, const FVector& PointToCheck, float HalfAngle, float Radius, float Height, float DrawDebug);
	FVector TryGetHeadSocket(ACharacter* InCharacter);

	//Solved State Tree Parameters
	AGLS_HumanAI_MainBehaviorMode BehaviorModeCopy;
	FAGLS_HumanAI_EnemyTags SightPerceptionCopy;

	float TimerToForceSetParams = -1.0;
	FAGLS_HumanAI_EnemyTags CachedSightParams;
	AGLS_HumanAI_SightStatus CachedSightStatus = AGLS_HumanAI_SightStatus::SeesNothing;

	void DrawDebugWeightInfo(AActor* CurrentActor, float WeightValue, float OffsetUp = 60, float OffsetRight = 25, FString WeightDescription = "Weight", float LerpRangeMin = 0.1, float LerpRangeMax = 1.0, int ColorPattern = 0);
	FVector GetCorrectDebugLocation(int PositionIndex, float BiasZ = 150);
	void DrawFailedPerceivedActorInfo(ACharacter* InCharacter, FString t, int ColorIndex = 0);
};
