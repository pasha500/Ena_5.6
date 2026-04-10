//2024 Jakub W


#include "InteractionWidgetDisplay.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Components/PrimitiveComponent.h"



UInteractionWidgetDisplay::UInteractionWidgetDisplay()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    ArrowSize = 0.8f;
    ArrowColor = FColor::Cyan;
    ArrowLength = 40.0f;

    bWidgetDisplayed = false;
    CurrentWidget = nullptr;
    InteractionCollision = nullptr;
}

void UInteractionWidgetDisplay::OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    TryGenerateWidget();
}

void UInteractionWidgetDisplay::OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    DestroyInteractionWidget();
}

void UInteractionWidgetDisplay::NotifyWidgetCreated()
{
    OnWidgetCreated.Broadcast();
}

void UInteractionWidgetDisplay::NotifyWidgetDestroyed()
{
    OnWidgetDestroyed.Broadcast();
}

void UInteractionWidgetDisplay::BeginPlay()
{
    Super::BeginPlay();

    InteractionCollision = Cast<UPrimitiveComponent>(OverlapComponent.GetComponent(GetOwner()));

    if (InteractionCollision)
    {
        InteractionCollision->OnComponentBeginOverlap.AddDynamic(this, &UInteractionWidgetDisplay::OnBeginOverlap);
        InteractionCollision->OnComponentEndOverlap.AddDynamic(this, &UInteractionWidgetDisplay::OnEndOverlap);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("InteractionCollision is not assigned in %s"), *GetOwner()->GetName());
    }
}

void UInteractionWidgetDisplay::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bWidgetDisplayed)
    {
        if (CheckAdditionalConditions())
        {
            PrimaryComponentTick.TickInterval = TickIntervalWhenDisplay;
            LoadAndCreateWidget();
        }     
    }
    else
    {
        if (CurrentWidget)
        {
            IInteractionWidgetInterface::Execute_BPI_UI_Set_WidgetWorldLocation(CurrentWidget, GetComponentLocation());
        }

        if (!CheckAdditionalConditions())
        {
            DestroyInteractionWidget();
            bWidgetDisplayed = false;
            PrimaryComponentTick.TickInterval = TickIntervalWhenOnlyOverlap;
            SetComponentTickEnabled(true);
        }
        else if (!CheckForValidOverlap())
        {
            DestroyInteractionWidget();
            bWidgetDisplayed = false;
        }

    }
}

bool UInteractionWidgetDisplay::CheckForValidOverlap()
{
    if (!InteractionCollision) return false;

    TArray<AActor*> OverlappingActors;
    InteractionCollision->GetOverlappingActors(OverlappingActors);

    for (AActor* Actor : OverlappingActors)
    {
        ACharacter* Character = Cast<ACharacter>(Actor);
        if (Character && Character->IsPlayerControlled())
        {
            OverlappedChar = Character;
            return true;
        }
    }
    OverlappedChar = nullptr;
    return false;
}

bool UInteractionWidgetDisplay::CheckAdditionalConditions()
{
    if (!AddtiveConditionObject) { return true; }
    
    // Tworzymy instancjê klasy przekazanej jako typ
    UInteractionWidgetCondition* ConditionInstance = NewObject<UInteractionWidgetCondition>(this, AddtiveConditionObject);

    if (!ConditionInstance) { return false; }

    return ConditionInstance->AddtiveConditionToCheck
    (
        OverlappedChar, 
        GetOwner(), 
        GetComponentLocation(), 
        GetOwner()->GetActorLocation() + (GetOwner()->GetActorForwardVector() * ConditionCheckOffset)
    );

}

void UInteractionWidgetDisplay::TryGenerateWidget()
{
    if (bWidgetDisplayed || !WidgetClass)
    {
        return;
    }

    if (CheckForValidOverlap())
    {
        SetComponentTickEnabled(true);
        PrimaryComponentTick.TickInterval = TickIntervalWhenOnlyOverlap;
    }
}

void UInteractionWidgetDisplay::LoadAndCreateWidget()
{
    if (!WidgetClass) return;

    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    if (!PlayerController) return;

    CurrentWidget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
    if (CurrentWidget)
    {
        CurrentWidget->AddToViewport();
        bWidgetDisplayed = true;
        NotifyWidgetCreated();
        IInteractionWidgetInterface::Execute_BPI_UI_Set_WidgetWorldLocation(CurrentWidget, GetComponentLocation());
        IInteractionWidgetInterface::Execute_BPI_UI_PlayInitAnimation(CurrentWidget);
    }
}

void UInteractionWidgetDisplay::DestroyInteractionWidget()
{
    if (CurrentWidget)
    {
        CurrentWidget->RemoveFromParent();
        CurrentWidget = nullptr;
        bWidgetDisplayed = false;
        NotifyWidgetDestroyed();
    }

    SetComponentTickEnabled(false);
}


