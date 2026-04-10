


#include "GAS_InteractionsComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BlueprintGameplayTagLibrary.h"
#include "InteractiveActorsInterface.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InteractionWidgetInterface.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values for this component's properties
UGAS_InteractionsComponent::UGAS_InteractionsComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	SetComponentTickInterval(0.1);
	// ...
}


// Called when the game starts
void UGAS_InteractionsComponent::BeginPlay()
{
	Super::BeginPlay();

	RefChar = Cast<ACharacter>(GetOwner());
	if (!RefChar) SetComponentTickEnabled(false);

	ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(RefChar);

	if (AbilitiesSet && ASC)
	{
		for (FGameplayAbilityBindIWALS I : AbilitiesSet->AlwaysLoadedAbilitiesSet)
		{
			AddNewAbilityClassToComponent(ASC, I.GameplayAbilityClass, I.Command);
		}
	}


}


// Called every frame
void UGAS_InteractionsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

bool UGAS_InteractionsComponent::AddNewAbilityClassToComponent(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> NewClass, TEnumAsByte<EIWALS_AbilityInputBinds::Type>	Command)
{
	if (RefChar && AbilitySystemComponent)
	{
		if (NewClass && !CurrentHasThisAbility(AbilitySystemComponent, NewClass))
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(NewClass, 1, (int32)Command, RefChar));
			return true;
		}
	}
	return false;
}

bool UGAS_InteractionsComponent::CurrentHasThisAbility(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> ClassToCheck)
{
	TArray<FGameplayAbilitySpec> Abilitiess = AbilitySystemComponent->GetActivatableAbilities();
	for (const FGameplayAbilitySpec& Spec : Abilitiess)
	{
		if (Spec.Ability->GetClass() == ClassToCheck)
		{
			// Zdolnoœæ zosta³a ju¿ przydzielona
			return true;
		}
	}
	// Zdolnoœæ nie zosta³a jeszcze przydzielona
	return false;
}

TMap<FGameplayTag, TSoftClassPtr<UGameplayAbility>> UGAS_InteractionsComponent::GetSoftRefAbilitiesFromSet()
{
	if (AbilitiesSet)
	{
		return AbilitiesSet->SoftRefAbilitiesSet;
	}
	else
	{
		return TMap<FGameplayTag, TSoftClassPtr<UGameplayAbility>>();
	}
}

TArray<FGameplayAbilityBindIWALS> UGAS_InteractionsComponent::GetHardRefAbilitiesFromSet()
{
	if (AbilitiesSet)
	{
		return AbilitiesSet->AlwaysLoadedAbilitiesSet;
	}
	else
	{
		return TArray<FGameplayAbilityBindIWALS>();
	}
}

bool UGAS_InteractionsComponent::TryFindAbilityClassByTag(TSoftClassPtr<UGameplayAbility>& ReturnClass, FGameplayTag Key)
{
	if (!AbilitiesSet)
	{
		ReturnClass = nullptr;
		return false;
	}

	if (TSoftClassPtr<UGameplayAbility>* FoundClass = AbilitiesSet->SoftRefAbilitiesSet.Find(Key))
	{
		ReturnClass = *FoundClass;
		return FoundClass->IsValid();
	}

	ReturnClass = nullptr;
	return false;
}

// Asynchroniczne ³adowanie klasy widgetu
void UGAS_InteractionsComponent::AsyncLoadWidgetClass(TSoftClassPtr<UUserWidget> WidgetClass)
{
	if (bCurrentlyLoadingWidget) return;

	if (WidgetClass.IsValid()) // Jeœli asset jest ju¿ za³adowany, pomijamy async load
	{
		WidgetClassLoadingComplete();
		return;
	}

	const FSoftObjectPath WidgetPath = WidgetClass.ToSoftObjectPath();
	if (!WidgetPath.IsValid())
	{
		WidgetClassLoadingComplete();
		return;
	}

	bCurrentlyLoadingWidget = true;

	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	StreamableManager.RequestAsyncLoad(
		WidgetPath,
		FStreamableDelegate::CreateUObject(this, &UGAS_InteractionsComponent::WidgetClassLoadingComplete)
	);
}

// Asynchroniczne ³adowanie klasy zdolnoœci
void UGAS_InteractionsComponent::AsyncLoadAbilityClass(TSoftClassPtr<UGameplayAbility> AbilityClass, bool TriggerDeletage)
{
	if (bCurrentylLoadingAbility) return;

	if (AbilityClass.IsValid()) // Jeœli asset jest ju¿ za³adowany, pomijamy async load
	{
		AbilityClassLoadingComplete(AbilityClass);

		if (TriggerDeletage)
		{
			OnAbilityLoadingComplete.Broadcast(AbilityClass.Get());
		}

		return;
	}

	const FSoftObjectPath AbilityPath = AbilityClass.ToSoftObjectPath();
	if (!AbilityPath.IsValid())
	{
		AbilityClassLoadingComplete(AbilityClass, TriggerDeletage);
		return;
	}

	bCurrentylLoadingAbility = true;

	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	StreamableManager.RequestAsyncLoad(
		AbilityPath,
		FStreamableDelegate::CreateUObject(this, &UGAS_InteractionsComponent::AbilityClassLoadingComplete, AbilityClass, TriggerDeletage)
	);
}


// Funkcja wywo³ywana po za³adowaniu widgetu
void UGAS_InteractionsComponent::WidgetClassLoadingComplete()
{
	bCurrentlyLoadingWidget = false;
	OnWidgetClassLoaded();
}

// Funkcja wywo³ywana po za³adowaniu zdolnoœci
void UGAS_InteractionsComponent::AbilityClassLoadingComplete(TSoftClassPtr<UGameplayAbility> SoftClass, bool TriggerDeletage)
{
	bCurrentylLoadingAbility = false;
	if (SoftClass.Get())
	{
		OnAbilityClassLoaded(SoftClass.Get());
		if (TriggerDeletage)
		{
			OnAbilityLoadingComplete.Broadcast(SoftClass.Get());
		}
	}
	else
	{
		OnAbilityClassLoaded(nullptr);
	}
	
}

// Domyœlne implementacje eventów BlueprintNativeEvent
void UGAS_InteractionsComponent::OnWidgetClassLoaded_Implementation() {}
void UGAS_InteractionsComponent::OnAbilityClassLoaded_Implementation(TSubclassOf<UGameplayAbility> AbilityClass) {}




AActor* UGAS_InteractionsComponent::GetChoosedActorForInteraction(bool& IsValid)
{
	SortActorsBasedOn(CurrentOverlapedActors, MultiActorsSorting);
	if (CurrentOverlapedActors.Num() > 0)
	{
		IsValid = true;
		return CurrentOverlapedActors[0];
	}
	IsValid = false;
	return nullptr;
}

void UGAS_InteractionsComponent::TrySpawnCorrectInteractionWidget()
{

	TArray<FHitResult> ActorsHit;

	ETraceTypeQuery TraceChannel = UEngineTypes::ConvertToTraceType(InteractiveActorsTraceChannel);

	UKismetSystemLibrary::CapsuleTraceMulti
	(
		RefChar, 
		RefChar->GetActorLocation() + (RefChar->GetActorForwardVector() * TraceForwardOffset),
		RefChar->GetActorLocation() + (RefChar->GetActorForwardVector() * TraceForwardOffset) + FVector(0, 0, -2),
		RefChar->GetCapsuleComponent()->GetScaledCapsuleRadius() * TraceRadiusScale, 
		RefChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * TraceHeightScale, 
		TraceChannel, 
		false, 
		ActorsToIgnoreByTrace, 
		EDrawDebugTrace::None, 
		ActorsHit, 
		true, 
		FColor::Blue, 
		FColor::Cyan, 
		GetComponentTickInterval() * 1.25
	);

	if (ActorsHit.Num() == 0)
	{
		RemoveNotRequiredWidgets();
	}

	if (ShowWidgetOnlyForFirstValid && CurrentOverlapedActors.Num() > 0)
	{
		if (CurrentOverlapedActors[0])
		{
			UUserWidget* CurrentW = nullptr;
			IInteractiveActorsInterface::Execute_BPI_IA_Get_CreatedWidgetInstance(CurrentOverlapedActors[0], CurrentW);
			if (CurrentW) return;
		}
	}

	int PreNumOfInteractiveActors = CurrentOverlapedActors.Num();
	TArray<AActor*> AllActors = {};

	for (FHitResult HitActor : ActorsHit)
	{
		AActor* IActor = HitActor.GetActor();
		AllActors.Add(IActor);

		if (!IActor->GetClass()->ImplementsInterface(UInteractiveActorsInterface::StaticClass())) continue; //Check Hitted Actor Implementing Interaction Actor Interface. When Not Skip this

		IInteractiveActorsInterface* IAI = Cast<IInteractiveActorsInterface>(IActor); //Get Interactive Actor Interface

		const bool MainConditions = CheckRequiredConditionsWhenOverlap(HitActor);
		bool Notlocked = true; 
		
		if(IAI) IAI->BPI_IA_Get_CanDisplayWidget(Notlocked);


		if (!MainConditions || !Notlocked)
		{
			RemoveNotRequiredWidgetSingleActor(IActor);
			CurrentOverlapedActors.Remove(IActor);
			continue;
		}
		
		// --------------------------------------------------------------------------------------------------------------------------------- IMPLEMENTACJA DODATKOWEGO WARUNKU opartego na tworzeniu instancji klasy
		TSubclassOf<UInteractionWidgetCondition> AddConditionClass = nullptr;
		if(IAI) AddConditionClass = IAI->BPI_IA_Get_AddtiveConditionClass();

		if (AddConditionClass)
		{
			// Tworzymy instancjê klasy przekazanej jako typ
			UInteractionWidgetCondition* ConditionInstance = NewObject<UInteractionWidgetCondition>(this, AddConditionClass);
			if (ConditionInstance)
			{
				if (!ConditionInstance->AddtiveConditionToCheck(RefChar, IActor, IActor->GetActorLocation(), IActor->GetActorLocation())) 
				{
					RemoveNotRequiredWidgetSingleActor(IActor);
					CurrentOverlapedActors.Remove(IActor);
					ConditionInstance->MarkAsGarbage();
					ConditionInstance = nullptr;
					continue;
				}
			}
		}

		// Sekcja zwi¹zania z utworzeniem nowej instancji Widgetu. Obecnie mamy do dyspozycji 2 mo¿liwoœci. Podstawowo klasa widgetu jest oparta od HardReference okreœlona jako MainInteractionWidget
		// Jednak czasami mo¿emy chcieæ zastosowaæ inn¹ klasê. W tym celu interaktywny aktor mo¿e posiadaæ klasê UUserWidget w postaci SoftReference, która powinna zostaæ ustorzona zamiast klasy
		// MainInteractionWidget. Jednak aby tak siê sta³o najpierw wymagane jest za³adowanie klasy do pamiêci je¿eli jeszcze to nie nast¹pi³o. Je¿eli ³adowanie by³o wymagane, to instancja Widgetu
		// zostanie utworzona dopiero podczas nastêpnego Tick-u.
		UUserWidget* ActorWidget = nullptr;
		IInteractiveActorsInterface::Execute_BPI_IA_Get_CreatedWidgetInstance(IActor, ActorWidget);
		if (!ActorWidget)
		{
			TSoftClassPtr<UUserWidget> SoftOverridedWiget;
			IInteractiveActorsInterface::Execute_BPI_IA_Get_OverridedWidget(IActor, SoftOverridedWiget);

			if (!SoftOverridedWiget.IsNull())
			{
				if (SoftOverridedWiget.IsValid())
				{
					AddToViewportRequiredWidget(SoftOverridedWiget.Get(), IActor);
					CurrentOverlapedActors.AddUnique(IActor);
				}
				else
				{
					AsyncLoadWidgetClass(SoftOverridedWiget);
				}
			}
			else
			{
				AddToViewportRequiredWidget(MainInteractionWidget, IActor);
				CurrentOverlapedActors.AddUnique(IActor);
			}
		}
		// --------------------------------------------------------------------------------- KONIEC sekcji poœwiêconej Widgetowi

		CheckRequiredAbilityOnOverlap(IActor, ASC);


	}

	if (CurrentOverlapedActors.Num() != ActorsHit.Num() && CurrentOverlapedActors.Num() > 0)
	{
		TArray<AActor*> InteractActors = CurrentOverlapedActors;
		for (AActor* AAA : InteractActors)
		{
			const int Finded = AllActors.Find(AAA);
			if (Finded == -1)
			{
				RemoveNotRequiredWidgetSingleActor(AAA);
				CurrentOverlapedActors.Remove(AAA);
			}
		}
	}

	if (CurrentOverlapedActors.Num() > 0)
	{
		FGameplayTagContainer PlayerTags = UBlueprintGameplayTagLibrary::GetOwnedGameplayTags(ASC);

		if (PlayerTags.HasAny(DestroyWidgetWhenTags))
		{
			for (AActor* COA : CurrentOverlapedActors)
			{
				RemoveNotRequiredWidgetSingleActor(COA);
			}
		}
	}


}

bool UGAS_InteractionsComponent::CheckRequiredConditionsWhenOverlap_Implementation(FHitResult InHitActor)
{

	AActor* IActor = InHitActor.GetActor();

	if (!IActor->GetClass()->ImplementsInterface(UInteractiveActorsInterface::StaticClass())) return false;

	bool CheckBasicStates = false;
	FVector2D MaxPositionZ = FVector2D::ZeroVector;
	float MaxDistance = 100.0;
	bool CheckWallHit = false;
	bool UseOtherTrace = false;

	IInteractiveActorsInterface::Execute_BPI_IA_Get_CheckDurningOverlap(IActor, CheckBasicStates, MaxPositionZ, MaxDistance, CheckWallHit, UseOtherTrace);

	return true;
}

bool UGAS_InteractionsComponent::CheckRequiredConditionsWhenAbility_Implementation(AActor* InHitActor)
{
	return false;
}

bool UGAS_InteractionsComponent::RemoveNotRequiredWidgets()
{
	if (CurrentOverlapedActors.Num() > 0)
	{
		for (AActor* A : CurrentOverlapedActors)
		{
			const bool Removed = RemoveNotRequiredWidgetSingleActor(A);
		}

		CurrentOverlapedActors.Empty();
		return true;
	}
	return false;
}

bool UGAS_InteractionsComponent::RemoveNotRequiredWidgetSingleActor(AActor* InActor)
{
	// Sprawdzenie, czy aktor implementuje interfejs IInteractiveActorsInterface
	if (!InActor->GetClass()->ImplementsInterface(UInteractiveActorsInterface::StaticClass())) return false;

	UUserWidget* ActorWidget = nullptr;

	// Pobranie instancji widgetu aktora poprzez interfejs
	IInteractiveActorsInterface::Execute_BPI_IA_Get_CreatedWidgetInstance(InActor, ActorWidget);

	// Jeœli widget istnieje, usuñ go z ekranu i ustaw wskaŸnik na nullptr
	if (ActorWidget)
	{
		ActorWidget->RemoveFromParent();  // Usuniêcie widgetu z ekranu
		ActorWidget = nullptr;            // Wyzerowanie wskaŸnika
		IInteractiveActorsInterface::Execute_BPI_IA_Set_CreatedWidgetInstance(InActor, nullptr);
		return true;  // Powiadomienie, ¿e usuniêto widget
	}

	return false; // Jeœli widget nie istnia³, zwracamy false
}

void UGAS_InteractionsComponent::AddToViewportRequiredWidget(TSubclassOf<UUserWidget> WidgetClass, AActor* ForActor)
{
	if (!WidgetClass) return; // Upewnij siê, ¿e klasa widgetu nie jest pusta

	// Pobranie referencji do PlayerController dla indeksu gracza 0
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), PlayerControllerIndex);
	if (!PlayerController) return;

	// Utworzenie instancji widgetu
	UUserWidget* CreatedWidget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
	if (!CreatedWidget) return;

	// Dodanie widgetu do ekranu
	CreatedWidget->AddToViewport(-2);

	IInteractiveActorsInterface::Execute_BPI_IA_Set_CreatedWidgetInstance(ForActor, CreatedWidget); //Wa¿ne

	FVector WidgetPositionWS = ForActor->GetActorLocation();
	IInteractiveActorsInterface::Execute_BPI_IA_Get_WidgetWorldPosition(ForActor, WidgetPositionWS);

	FName Text01, Text02; FLinearColor Color01, Color02; float Param01; UObject* Obj01 = nullptr; UObject* Obj02 = nullptr;
	IInteractiveActorsInterface::Execute_BPI_IA_Get_WidgetParams(ForActor, RefChar, Text01, Text02, Param01, Color01, Color02, Obj01, Obj02);

	if (CreatedWidget->GetClass()->ImplementsInterface(UInteractionWidgetInterface::StaticClass()))
	{
		IInteractionWidgetInterface::Execute_BPI_UI_Set_WidgetWorldLocation(CreatedWidget, WidgetPositionWS);
		//DrawDebugSphere(GetWorld(), WidgetPositionWS, 15, 10, FColor::Red, false, 1.0, 0, 1);
		IInteractionWidgetInterface::Execute_BPI_UI_Set_WidgetTextsValues(CreatedWidget, Text01, Text02, Param01, Color01, Color02, Obj01, Obj02);
	}

}



bool UGAS_InteractionsComponent::TryFindAndActivateAbility_Implementation()
{
	return false;
}

bool UGAS_InteractionsComponent::CheckRequiredAbilityOnOverlap(AActor* ForActor, UAbilitySystemComponent* AbilitySystemComponent)
{
	if (!ForActor || !AbilitySystemComponent || !AbilitiesSet) return false;

	bool RequireOnOverlap = false;
	IInteractiveActorsInterface::Execute_BPI_IA_Get_RequiredAbilityOnOverlap(ForActor, RequireOnOverlap);
	if (!RequireOnOverlap) return false;

	FGameplayTagContainer Tags;
	IInteractiveActorsInterface::Execute_BPI_IA_Get_InteractionTag(ForActor, Tags);

	FGameplayTag FirstTag = Tags.First();
	if (!FirstTag.IsValid()) return false;

	TSoftClassPtr<UGameplayAbility> FindedClass = AbilitiesSet->SoftRefAbilitiesSet.FindRef(FirstTag);
	//GEngine->AddOnScreenDebugMessage(-1, 1, FColor::Green, FirstTag.GetTagName().ToString());

	if (!FindedClass.ToSoftObjectPath().IsValid()) return false;

	if (FindedClass.Get())
	{
		AddNewAbilityClassToComponent(AbilitySystemComponent, FindedClass.Get(), EIWALS_AbilityInputBinds::None0);
	}
	else
	{
		AsyncLoadAbilityClass(FindedClass);
	}

	return true;
}

bool UGAS_InteractionsComponent::LoadAbilitesMatchedToActors(bool& CurrentlyLoaded, TArray<AActor*> InteractiveActors, UAbilitySystemComponent* AbilitySystemComponent, bool LoadOnlyForSingleActor, bool SortActorsBeforLoad)
{
	if (InteractiveActors.Num() == 0 || !AbilitySystemComponent) return false;

	if (SortActorsBeforLoad) SortActorsBasedOn(InteractiveActors, MultiActorsSorting);

	if (LoadOnlyForSingleActor || InteractiveActors.Num() == 1)
	{
		AActor* CurrentActor = InteractiveActors[0];

		FGameplayTagContainer Tags;

		if (CurrentActor->GetClass()->ImplementsInterface(UInteractiveActorsInterface::StaticClass()))
		{
			IInteractiveActorsInterface::Execute_BPI_IA_Get_InteractionTag(CurrentActor, Tags);
		}

		if (CheckRequiredConditionsWhenAbility(CurrentActor) == false) return false; // <-------------------------------- Main Condition (Overrided In BLUEPRINT)

		FGameplayTag FirstTag = Tags.First();
		if (!FirstTag.IsValid()) return false;

		TSoftClassPtr<UGameplayAbility> FindedClass = AbilitiesSet->SoftRefAbilitiesSet.FindRef(FirstTag);
		if (!FindedClass.ToSoftObjectPath().IsValid())
		{
			if (AbilitiesSet->AlwaysLoadedAbilitiesSet.Num() > 0)
			{
				ContinueIntractionByInput.Broadcast(CurrentActor, InteractiveActors);
			}
			return false;
		}

		if (FindedClass.Get())
		{
			AddNewAbilityClassToComponent(AbilitySystemComponent, FindedClass.Get(), EIWALS_AbilityInputBinds::None0);
			CurrentlyLoaded = true;
			ContinueIntractionByInput.Broadcast(CurrentActor, InteractiveActors);
			return true;
		}
		else
		{
			const FSoftObjectPath AbilityPath = FindedClass.ToSoftObjectPath();
			if (!AbilityPath.IsValid())
			{
				return false;
			}

			FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
			StreamableManager.RequestAsyncLoad
			(
				AbilityPath,
				FStreamableDelegate::CreateUObject(this, &UGAS_InteractionsComponent::AddLoadedAbilityToComponent, FindedClass, InteractiveActors)
			);
			return false;

		}

	}
	else
	{
		for (int i = 0; i < InteractiveActors.Num(); i++)
		{
			AActor* CurrentActor = InteractiveActors[i];

			FGameplayTagContainer Tags;

			if (CurrentActor->GetClass()->ImplementsInterface(UInteractiveActorsInterface::StaticClass()))
			{
				IInteractiveActorsInterface::Execute_BPI_IA_Get_InteractionTag(CurrentActor, Tags);
			}

			FGameplayTag FirstTag = Tags.First();
			if (!FirstTag.IsValid()) continue;

			if (CheckRequiredConditionsWhenAbility(CurrentActor) == false) continue; // <-------------------------------- Main Condition (Overrided In BLUEPRINT)

			TSoftClassPtr<UGameplayAbility> FindedClass = AbilitiesSet->SoftRefAbilitiesSet.FindRef(FirstTag);
			if (!FindedClass.ToSoftObjectPath().IsValid())
			{
				if (AbilitiesSet->AlwaysLoadedAbilitiesSet.Num() > 0)
				{
					ContinueIntractionByInput.Broadcast(CurrentActor, InteractiveActors);
					CurrentlyLoaded = true;
					return false;
				}
				continue;
			}

			if (FindedClass.Get())
			{
				AddNewAbilityClassToComponent(AbilitySystemComponent, FindedClass.Get(), EIWALS_AbilityInputBinds::None0);
				CurrentlyLoaded = true;
				ContinueIntractionByInput.Broadcast(CurrentActor, InteractiveActors);
				return true;
			}
			else
			{
				const FSoftObjectPath AbilityPath = FindedClass.ToSoftObjectPath();
				if (!AbilityPath.IsValid())
				{
					continue;
				}

				FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
				StreamableManager.RequestAsyncLoad
				(
					AbilityPath,
					FStreamableDelegate::CreateUObject(this, &UGAS_InteractionsComponent::AddLoadedAbilityToComponent, FindedClass, InteractiveActors)
				);
				return false;

			}
		}
	}
	return false;
}

void UGAS_InteractionsComponent::AddLoadedAbilityToComponent(TSoftClassPtr<UGameplayAbility> InSoftClass, TArray<AActor*> A)
{
	if (ASC && InSoftClass.Get() && A.Num() > 0)
	{
		AddNewAbilityClassToComponent(ASC, InSoftClass.Get(), EIWALS_AbilityInputBinds::None0);
		ContinueIntractionByInput.Broadcast(A[0], A);
	}
}

bool UGAS_InteractionsComponent::RunAbilityForActor(AActor* InActor)
{
	if (!InActor) return false;

	IInteractiveActorsInterface* IAI = Cast<IInteractiveActorsInterface>(InActor); //Get Interactive Actor Interface

	FGameplayTagContainer ActorTags;
	IInteractiveActorsInterface::Execute_BPI_IA_Get_InteractionTag(InActor, ActorTags);

	if (IAI)
	{
		bool DestWidget = true; IAI->BPI_IA_Get_DestroyWhenAbilityRun(DestWidget);
		if (DestWidget)
		{
			RemoveNotRequiredWidgetSingleActor(InActor);
		}
	}
	//if(CheckRequiredConditionsWhenAbility(InActor) == false) return false;

	const bool Activated = ASC->TryActivateAbilitiesByTag(ActorTags, true);
	return Activated;
}

void UGAS_InteractionsComponent::GetOverlappedActors(TArray<AActor*>& Actors) const
{
	Actors = CurrentOverlapedActors;
	return;
}


void UGAS_InteractionsComponent::SortActorsBasedOn(TArray<AActor*>& InActors, EAGLS_InteractiveActorsSort SortingMethod)
{
	if (SortingMethod == EAGLS_InteractiveActorsSort::NoSorting || InActors.Num() <= 1)
	{
		return; // Brak sortowania lub 1 aktor => zwróæ tablicê bez zmian
	}

	if (!RefChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("SortActorsBasedOn: RefChar is NULL!"));
		return;
	}

	FVector CharacterLocation = RefChar->GetActorLocation();
	FRotator CharacterRotation = RefChar->GetControlRotation();

	// Funkcja lambda do sortowania aktorów
	auto SortingLambda = [&](AActor& A, AActor& B) -> bool
		{
			float ScoreA = 0.f;
			float ScoreB = 0.f;

			FVector LocationA = A.GetActorLocation();
			FVector LocationB = B.GetActorLocation();

			if (SortingMethod == EAGLS_InteractiveActorsSort::BasedOnDistance || SortingMethod == EAGLS_InteractiveActorsSort::DistanceAndAngle)
			{
				// Oblicz dystans do aktora
				float DistanceA = FVector::DistSquared(CharacterLocation, LocationA);
				float DistanceB = FVector::DistSquared(CharacterLocation, LocationB);

				// Mniejszy dystans = wy¿szy priorytet
				ScoreA += (1.f / (1.f + DistanceA));
				ScoreB += (1.f / (1.f + DistanceB));
			}

			if (SortingMethod == EAGLS_InteractiveActorsSort::BasedOnAngle || SortingMethod == EAGLS_InteractiveActorsSort::DistanceAndAngle)
			{
				// Oblicz k¹t pomiêdzy kierunkiem postaci a aktorem
				FVector LookAtDirectionA = (LocationA - CharacterLocation).GetSafeNormal();
				FVector LookAtDirectionB = (LocationB - CharacterLocation).GetSafeNormal();
				FVector CharacterForward = CharacterRotation.Vector();

				float DotProductA = FVector::DotProduct(CharacterForward, LookAtDirectionA);
				float DotProductB = FVector::DotProduct(CharacterForward, LookAtDirectionB);

				// Wy¿szy iloczyn skalarny = aktor bardziej w linii wzroku postaci
				ScoreA += (DotProductA + 1.f) / 2.f; // Normalizacja do zakresu [0,1]
				ScoreB += (DotProductB + 1.f) / 2.f;
			}

			return ScoreA > ScoreB; // Sortowanie malej¹ce (wiêksza wartoœæ = wy¿szy priorytet)
		};

	InActors.Sort(SortingLambda);

	return;
}
