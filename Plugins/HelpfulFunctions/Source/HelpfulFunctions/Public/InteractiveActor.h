

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InteractiveActorsInterface.h"
#include "InteractionWidgetCondition.h"
#include "Components/ArrowComponent.h"
#include "GameFramework/Actor.h"
#include "InteractiveActor.generated.h"

/*
Base class that allows efficient management of interactions between the environment and the player. 

The basic way of interaction is to: 
- find actors with the ability to interact via Overlaps 
- select the most appropriate instance
- find the correct GameplayAbility class
- and finally activate it.

Some elements are loaded asynchronously, which improves performance, but mainly in the packed game. 
In the editor, the loading time of classes is much longer.
*/
UCLASS(HideCategories = (Navigation, HLOD, Input, NetWorking, Replication, Mobile, DataLayers))
class HELPFULFUNCTIONS_API AInteractiveActor : public AActor, public IInteractiveActorsInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AInteractiveActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/*a parameter specifying what 'Gameplay Ability' class has been assigned to the actor. 
This is a value that identifies and distinguishes the class for AbilitySystem. 
Note: One GameplayTag should be assigned to a given class.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	FGameplayTagContainer InteractionAbilityTag;

	/*Some Abilities for a given interaction may not be in memory yet. Therefore, the Ability class must be loaded asynchronously somewhere. 
If this value is set to 'true', the Ability Class will be loaded when activated by Overlap (this does not mean that the actor will 
be used for interaction, but that the player is in near distance). If the value is 'false', the Ability Class will be loaded when an 
attempt is made to interact with the actor (usually via the 'E' input).*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	bool LoadAbilityClassOnOverlap = false;

	/*It allows you to assign another class 'Widget Blueprint' that displays the ability to interact with the actor. It is not HardReference, 
which means that the class will need to be loaded before it can be spawned as instance.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	TSoftClassPtr<UUserWidget> OverrideWidgetClass;

	/*Defines how often the Widget's position will be updated, once it has been created. Another important element is checking if the 
Player is still overlaping the object collision. If this condition is not valid, it means that the widget instance should be destroyed*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	float WidgetTickInterval = 0.05;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	FName WidgetTextSlot01 = TEXT("Interactive Prop");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	FName WidgetTextSlot02 = TEXT("Info about this item");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	bool bDestroyWidgetWhenAbilityRun = true;

	/*Activating this causes the actor instance to try to calculate Velocity based on getting its position in the current frame, 
and the previous one. For this to work correctly, the TickEvent must be active*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	bool CalculateFakeVelocity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	int AddtiveInteractionID = -1;

	/*When trying to generate a Widget instance, we may want to check for an additional condition. For this purpose, 
we can use the addition of such a condition via the appropriate blueprint class (InteractionWidgetCondition). 
When this parameter is null then we ignore this segment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	TSubclassOf<UInteractionWidgetCondition> AddtiveConditionClass = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interaction Config"))
	bool bCanDisplayWidget = true;

	UFUNCTION(BlueprintCallable, meta = (Category = "Interactive Actor"))
	void UpdateInteractionState(bool StartedInteraction);

	UFUNCTION(BlueprintPure, meta = (Category = "Interactive Actor"))
	bool GetInteractionState() const;

	UFUNCTION(BlueprintCallable, meta = (Category = "Interactive Actor"))
	bool ForceDestroyWidget();

// Interface functions override:

	virtual void BPI_IA_Get_InteractionTag_Implementation(FGameplayTagContainer& ReturnTag) override;
	virtual void BPI_IA_Get_OverridedWidget_Implementation(TSoftClassPtr<UUserWidget>& ReturnSoftClass) override;
	virtual void BPI_IA_Get_WidgetParams_Implementation(ACharacter* PlayerChar, FName& Text01, FName& Text02, float& Float01, FLinearColor& Color01, FLinearColor& Color02, UObject*& Object01, UObject*& Object02) override;
	virtual void BPI_IA_Get_WidgetWorldPosition_Implementation(FVector& ReturnPosition) override;
	virtual void BPI_AI_Get_ActorStartedInteraction_Implementation(bool& Started) override;
	virtual void BPI_AI_Get_ObjectTracingOrigin_Implementation(FVector& PositionWS);
	virtual void BPI_IA_Get_CurrentVelocity_Implementation(FVector& ReturnVelocity) override;
	virtual void BPI_IA_Set_CreatedWidgetInstance_Implementation(UUserWidget* WidgetInstance) override;
	virtual void BPI_IA_Get_CreatedWidgetInstance_Implementation(UUserWidget*& WidgetInstance) const override;
	virtual void BPI_IA_Get_RequiredAbilityOnOverlap_Implementation(bool& Require) const override;
	virtual void BPI_IA_Set_CanDisplayWidget_Implementation(bool CanDisplay) override;

	virtual void BPI_IA_Get_CanDisplayWidget(bool& CanDisplay) const override;
	virtual void BPI_IA_Get_DestroyWhenAbilityRun(bool& Destroy) const override;
	virtual TSubclassOf<UInteractionWidgetCondition> BPI_IA_Get_AddtiveConditionClass() const override;

private:

	bool bGeneratedWidget = false;
	bool bStartedInteraction = false;
	FVector PrevPosition = FVector::ZeroVector;
	float DT = 0.003f;

	UUserWidget* CreatedWidget = nullptr;

	FTimerHandle WidgetUpdateTimer; // Timer do aktualizacji Widgetu

	void UpdateWhenWidgetIsValid();

};
