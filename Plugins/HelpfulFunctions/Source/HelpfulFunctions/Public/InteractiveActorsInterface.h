

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "InteractionWidgetCondition.h"
#include "InteractiveActorsInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UInteractiveActorsInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class HELPFULFUNCTIONS_API IInteractiveActorsInterface
{
	GENERATED_BODY()

public:

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get")
	void BPI_IA_Get_InteractionTag(FGameplayTagContainer& ReturnTag);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get")
	void BPI_IA_Get_CurrentVelocity(FVector& ReturnVelocity);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Widget")
	void BPI_IA_Get_OverridedWidget(TSoftClassPtr<UUserWidget>& ReturnSoftClass);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Widget")
	void BPI_IA_Get_WidgetParams(ACharacter* PlayerChar, FName& Text01, FName& Text02, float& Float01, FLinearColor& Color01, FLinearColor& Color02, UObject*& Object01, UObject*& Object02);

	/* Blueprint Interface - Interactive Actor */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get")
	void BPI_IA_Get_InteractionType(int& InteractionType);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get")
	void BPI_IA_Get_RequiredAbilityOnOverlap(bool& Require) const;

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Widget")
	void BPI_IA_Get_WidgetWorldPosition(FVector& ReturnPosition);

	/* Blueprint Interface - Interactive Actor (Most of important function! SHOULD be overrided in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Conditions", meta = (AdvancedDisplay = 2))
	void BPI_IA_Get_CheckDurningAbilityRun
	(
		bool& CheckBasicStates,
		bool& UseCorrectAngle,
		FVector2D& AngleArc,
		FVector2D& MaxPositionZ,
		float& MaxDistance,
		bool& CheckWallHit,
		bool& UseOtherTrace,
		AActor*& ToIgnores
	);

	/* Blueprint Interface - Interactive Actor (Most of important function! SHOULD be overrided in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Conditions", meta = (AdvancedDisplay = 2))
	void BPI_IA_Get_CheckDurningOverlap
	(
		bool& CheckBasicStates,
		FVector2D& MaxPositionZ,
		float& MaxDistance,
		bool& CheckWallHit,
		bool& UseOtherTrace
	);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Widget")
	void BPI_IA_Get_CreatedWidgetInstance(UUserWidget*& WidgetInstance) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get")
	void BPI_IA_Get_ConfigHoldingOption(bool& ActorCanBeHold, CALS_OverlayState& OverlayMatch);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Conditions")
	void BPI_IA_Get_ObjectCollisionOverlap(bool& IsOverlaping, UPARAM(ref) TArray<AActor*>& ToIgnore);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get|Conditions", meta = (DisplayName = "BPI IA Get ActorStartedInteraction"))
	void BPI_AI_Get_ActorStartedInteraction(bool& Started);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Get", meta = (DisplayName = "BPI IA Get ObjectTracingOrigin"))
	void BPI_AI_Get_ObjectTracingOrigin(FVector& PositionWS);

	/* Blueprint Interface - Interactive Actor (This function NOT require override in child class)*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set")
	void BPI_IA_Set_CreatedWidgetInstance(UUserWidget* WidgetInstance);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set", meta = (AdvancedDisplay = 2, DisplayName = "BPI IA Set HitByBullet"))
	void BPI_AI_Set_HitByBullet(FHitResult HitInfo, ACharacter* FromCharacter, AActor* FromActor);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set", meta = (DisplayName = "BPI IA Set StartInteraction"))
	void BPI_AI_Set_StartInteraction(bool Start);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set", meta = (DisplayName = "BPI IA Set StartInteractionTypeB"))
	void BPI_AI_Set_StartInteractionTypeB(bool Start);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set", meta = (DisplayName = "BPI IA Set StartPlayerCollisionBlock"))
	void BPI_AI_Set_StartPlayerCollisionBlock(bool BlockCollision, float TimeToBlock);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set", meta = (DisplayName = "BPI IA Set ActivateFunctionality"))
	void BPI_AI_Set_ActivateFunctionality(bool Activate, ACharacter* Target);


	virtual void BPI_IA_Get_CanDisplayWidget(bool& CanDisplay) const;
	virtual void BPI_IA_Get_DestroyWhenAbilityRun(bool& Destroy) const;
	virtual TSubclassOf<UInteractionWidgetCondition> BPI_IA_Get_AddtiveConditionClass() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interactive Actors Interface|Set")
	void BPI_IA_Set_CanDisplayWidget(bool CanDisplay);

};
