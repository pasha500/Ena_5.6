


#include "InteractiveStaticMeshActor.h"
#include "Blueprint/UserWidget.h"
#include "InteractionWidgetInterface.h"

// Called every frame
void AInteractiveStaticMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	DT = DeltaTime;
}

void AInteractiveStaticMeshActor::UpdateInteractionState(bool StartedInteraction)
{
	bStartedInteraction = StartedInteraction;
}

bool AInteractiveStaticMeshActor::GetInteractionState() const
{
	return bStartedInteraction;
}

bool AInteractiveStaticMeshActor::ForceDestroyWidget()
{
	if (CreatedWidget)
	{
		CreatedWidget->RemoveFromParent();  // Usuniêcie widgetu z ekranu
		CreatedWidget = nullptr;            // Wyzerowanie wskaŸnika
		IInteractiveActorsInterface::Execute_BPI_IA_Set_CreatedWidgetInstance(this, nullptr);
		return true;  // Powiadomienie, ¿e usuniêto widget
	}

	return false; 
}

void AInteractiveStaticMeshActor::BPI_IA_Get_InteractionTag_Implementation(FGameplayTagContainer& ReturnTag)
{
	ReturnTag = InteractionAbilityTag;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_OverridedWidget_Implementation(TSoftClassPtr<UUserWidget>& ReturnSoftClass)
{
	ReturnSoftClass = OverrideWidgetClass;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_WidgetParams_Implementation(ACharacter* PlayerChar, FName& Text01, FName& Text02, float& Float01, FLinearColor& Color01, FLinearColor& Color02, UObject*& Object01, UObject*& Object02)
{
	Float01 = 0.0;
	Text01 = WidgetTextSlot01;
	Text02 = WidgetTextSlot02;
	Object01 = nullptr;
	Object02 = nullptr;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_WidgetWorldPosition_Implementation(FVector& ReturnPosition)
{
	ReturnPosition = GetActorLocation() + FVector(0, 0, 12);
}

void AInteractiveStaticMeshActor::BPI_AI_Get_ActorStartedInteraction_Implementation(bool& Started)
{
	Started = GetInteractionState();
}

void AInteractiveStaticMeshActor::BPI_AI_Get_ObjectTracingOrigin_Implementation(FVector& PositionWS)
{
	PositionWS = GetActorLocation();
}

void AInteractiveStaticMeshActor::BPI_IA_Get_CurrentVelocity_Implementation(FVector& ReturnVelocity)
{
	if (CalculateFakeVelocity)
	{
		ReturnVelocity = (GetActorLocation() - PrevPosition) / DT;
	}
	else
	{
		ReturnVelocity = GetVelocity();
	}
}

void AInteractiveStaticMeshActor::BPI_IA_Set_CreatedWidgetInstance_Implementation(UUserWidget* WidgetInstance)
{
	CreatedWidget = WidgetInstance;

	if (CreatedWidget)
	{
		// Uruchom timer do aktualizacji widgetu
		GetWorldTimerManager().SetTimer(
			WidgetUpdateTimer,
			this,
			&AInteractiveStaticMeshActor::UpdateWhenWidgetIsValid,
			WidgetTickInterval,
			true // Powtarzaj co WidgetTickInterval sekund
		);
	}
}


void AInteractiveStaticMeshActor::BPI_IA_Get_CreatedWidgetInstance_Implementation(UUserWidget*& WidgetInstance) const
{
	WidgetInstance = CreatedWidget;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_RequiredAbilityOnOverlap_Implementation(bool& Require) const
{
	Require = LoadAbilityClassOnOverlap;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_CanDisplayWidget(bool& CanDisplay) const
{
	CanDisplay = bCanDisplayWidget; return;
}

void AInteractiveStaticMeshActor::BPI_IA_Set_CanDisplayWidget_Implementation(bool CanDisplay)
{
	bCanDisplayWidget = CanDisplay;  return;
}

void AInteractiveStaticMeshActor::BPI_IA_Get_DestroyWhenAbilityRun(bool& Destroy) const
{
	Destroy = bDestroyWidgetWhenAbilityRun; return;
}

TSubclassOf<UInteractionWidgetCondition> AInteractiveStaticMeshActor::BPI_IA_Get_AddtiveConditionClass() const
{
	return AddtiveConditionClass;
}


void AInteractiveStaticMeshActor::UpdateWhenWidgetIsValid()
{
	if (!CreatedWidget) // SprawdŸ, czy widget jest nadal wa¿ny
	{
		// Jeœli widget jest nieprawid³owy, zatrzymaj timer
		GetWorldTimerManager().ClearTimer(WidgetUpdateTimer);
		CreatedWidget = nullptr; // Resetowanie wskaŸnika dla bezpieczeñstwa
		return;
	}

	// Pobierz now¹ pozycjê widgetu za pomoc¹ funkcji interfejsu
	FVector WidgetWorldPosition;
	Execute_BPI_IA_Get_WidgetWorldPosition(this, WidgetWorldPosition);

	// SprawdŸ, czy widget implementuje interfejs IInteractionWidgetInterface
	if (CreatedWidget->GetClass()->ImplementsInterface(UInteractionWidgetInterface::StaticClass()))
	{
		IInteractionWidgetInterface::Execute_BPI_UI_Set_WidgetWorldLocation(CreatedWidget, WidgetWorldPosition);
	}

}