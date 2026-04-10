

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionAbilitiesSet.h"
#include "GameFramework/Character.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "GAS_InteractionsComponent.generated.h"

UENUM(BlueprintType)
enum class EAGLS_InteractiveActorsSort : uint8
{
	NoSorting,
	BasedOnDistance,
	BasedOnAngle,
	DistanceAndAngle
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityLoadingComplete, TSubclassOf<UGameplayAbility>, LoadedAbility);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FContinueInteraction, AActor* , BestActor, const TArray<AActor*>& , AllActorsInRange);

UCLASS(Blueprintable, ClassGroup = (PlayerMechanics), meta = (BlueprintSpawnableComponent))
class IWALS_ABILITYSYSTEM_API UGAS_InteractionsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGAS_InteractionsComponent();

	UPROPERTY(BlueprintAssignable, Category = "Interactions Core")
	FOnAbilityLoadingComplete OnAbilityLoadingComplete;

	UPROPERTY(BlueprintAssignable, Category = "Interactions Core")
	FContinueInteraction ContinueIntractionByInput;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|References", DisplayName = "Character"))
	ACharacter* RefChar = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	UInteractionAbilitiesSet* AbilitiesSet = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	bool bLoadAllAbilitiesOnStart = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	EAGLS_InteractiveActorsSort MultiActorsSorting = EAGLS_InteractiveActorsSort::NoSorting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	bool ShowWidgetOnlyForFirstValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	TSubclassOf<UUserWidget> MainInteractionWidget = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	TEnumAsByte<ECollisionChannel> InteractiveActorsTraceChannel = ECollisionChannel::ECC_Vehicle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	FGameplayTagContainer DestroyWidgetWhenTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	float TraceRadiusScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	float TraceHeightScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	float TraceForwardOffset = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core|Config"))
	int PlayerControllerIndex = 0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Interactions Core", DisplayName = "ChoosedActor"))
	AActor* ActorChoosedForInteraction = nullptr;

	TArray<AActor*> ActorsToIgnoreByTrace;

public:

	UFUNCTION(BlueprintCallable, Category = "Interactions Core", meta = (DisplayName = "Add New Ability Class To Component", Keywords = "Interactions,Ability"))
	bool AddNewAbilityClassToComponent(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> NewClass, TEnumAsByte<EIWALS_AbilityInputBinds::Type>	Command);

	UFUNCTION(BlueprintPure, Category = "Interactions Core", meta = (DisplayName = "Current Has This Ability", Keywords = "Interactions,Ability"))
	bool CurrentHasThisAbility(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> ClassToCheck);

	UFUNCTION(BlueprintPure, Category = "Interactions Core", meta = (DisplayName = "Get Soft Ref Abilities From Set", Keywords = "Interactions,Ability"))
	TMap<FGameplayTag, TSoftClassPtr<UGameplayAbility>> GetSoftRefAbilitiesFromSet();

	UFUNCTION(BlueprintPure, Category = "Interactions Core", meta = (DisplayName = "Get Hard Ref Abilities From Set", Keywords = "Interactions,Ability"))
	TArray<FGameplayAbilityBindIWALS> GetHardRefAbilitiesFromSet();

	UFUNCTION(BlueprintPure, Category = "Interactions Core", meta = (DisplayName = "Try Find Ability Class By Tag", Keywords = "Interactions,Ability"))
	bool TryFindAbilityClassByTag(TSoftClassPtr<UGameplayAbility>& ReturnClass, FGameplayTag Key);




	UFUNCTION(BlueprintCallable, Category = "Interactions Core|Async Loading")
	void AsyncLoadWidgetClass(TSoftClassPtr<UUserWidget> WidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Interactions Core|Async Loading")
	void AsyncLoadAbilityClass(TSoftClassPtr<UGameplayAbility> AbilityClass, bool TriggerDeletage = false);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interactions Core|Async Loading")
	void OnWidgetClassLoaded();
	virtual void OnWidgetClassLoaded_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interactions Core|Async Loading")
	void OnAbilityClassLoaded(TSubclassOf<UGameplayAbility> AbilityClass);
	virtual void OnAbilityClassLoaded_Implementation(TSubclassOf<UGameplayAbility> AbilityClass);


	/*It allows you to get the Actor that will be selected as the best in terms of the sorting algorithm. This does not always mean that this actor will be chosen. 
Please NOTE that calling this function executes a sorting algorithm which is expensive when there are a large number of elements in the array.*/
	UFUNCTION(BlueprintPure, Category = "Interactions Core", meta = (DisplayName = "Get PRE Choosed Actor For Interaction"))
	AActor* GetChoosedActorForInteraction(bool& IsValid);


	UFUNCTION(BlueprintCallable, Category = "Interactions Core" ,meta = (DisplayName = "Try Spawn Correct Interaction Widget"))
	void TrySpawnCorrectInteractionWidget();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Interactions Core", meta = (ForceAsFunction, DisplayName = "CheckRequiredConditionsWhenOverlap"))
	bool CheckRequiredConditionsWhenOverlap(FHitResult InHitActor);
	virtual bool CheckRequiredConditionsWhenOverlap_Implementation(FHitResult InHitActor);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Interactions Core", meta = (ForceAsFunction, DisplayName = "CheckRequiredConditionsWhenAbility"))
	bool CheckRequiredConditionsWhenAbility(AActor* InHitActor);
	virtual bool CheckRequiredConditionsWhenAbility_Implementation(AActor* InHitActor);

	bool RemoveNotRequiredWidgets();
	bool RemoveNotRequiredWidgetSingleActor(AActor* InActor);

	void AddToViewportRequiredWidget(TSubclassOf<UUserWidget> WidgetClass, AActor* ForActor);

	// Main functions about Ability Activation
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interactions Core", meta = (ForceAsFunction, DisplayName = "Try Find And Activate Ability"))
	bool TryFindAndActivateAbility();
	virtual bool TryFindAndActivateAbility_Implementation();

	bool CheckRequiredAbilityOnOverlap(AActor* ForActor, UAbilitySystemComponent* AbilitySystemComponent);

	UFUNCTION(BlueprintCallable, Category = "Interactions Core", meta = (DisplayName = "Load Abilites Matched To Actors"))
	bool LoadAbilitesMatchedToActors(bool& CurrentlyLoaded, TArray<AActor*> InteractiveActors, UAbilitySystemComponent* AbilitySystemComponent, bool LoadOnlyForSingleActor, bool SortActorsBeforLoad);

	void SortActorsBasedOn(TArray<AActor*>& InActors, EAGLS_InteractiveActorsSort SortingMethod);

	UFUNCTION(BlueprintCallable, Category = "Interactions Core", meta = (DisplayName = "Run Ability For Actor"))
	bool RunAbilityForActor(AActor* InActor);

	UFUNCTION(BlueprintPure, Category = "Interactions Core")
	void GetOverlappedActors(TArray<AActor*>& Actors) const;

private:

	void WidgetClassLoadingComplete();
	void AbilityClassLoadingComplete(TSoftClassPtr<UGameplayAbility> SoftClass, bool TriggerDeletage = false);
	void AddLoadedAbilityToComponent(TSoftClassPtr<UGameplayAbility> InSoftClass, TArray<AActor*> A);

	UAbilitySystemComponent* ASC = nullptr;
	TArray<AActor*> CurrentOverlapedActors;

	bool bCurrentlyLoadingWidget = false;
	bool bCurrentylLoadingAbility = false;

};
