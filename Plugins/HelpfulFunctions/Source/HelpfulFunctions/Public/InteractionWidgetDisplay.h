//2024 Jakub W

#pragma once

#include "CoreMinimal.h"
#include "Components/ArrowComponent.h"
#include "Blueprint/UserWidget.h"
#include "InteractionWidgetCondition.h"
#include "InteractionWidgetInterface.h"
#include "InteractionWidgetDisplay.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWidgetCreatedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWidgetDestroyedSignature);

UCLASS(ClassGroup = (PlayerMechanics), meta = (BlueprintSpawnableComponent))
class HELPFULFUNCTIONS_API UInteractionWidgetDisplay : public UArrowComponent
{
    GENERATED_BODY()

public:
    UInteractionWidgetDisplay();

    // Function to manually try generating the widget
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void TryGenerateWidget();

    // Function to destroy the widget
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void DestroyInteractionWidget();

    // BlueprintAssignable events
    UPROPERTY(BlueprintAssignable, Category = "Interaction Widget")
    FWidgetCreatedSignature OnWidgetCreated;

    UPROPERTY(BlueprintAssignable, Category = "Interaction Widget")
    FWidgetDestroyedSignature OnWidgetDestroyed;

private:
    UFUNCTION()
    void OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    void NotifyWidgetCreated();
    void NotifyWidgetDestroyed();

protected:
    virtual void BeginPlay() override;

    // Tick event to monitor overlapping actors
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // Checks if a player character is overlapping
    bool CheckForValidOverlap();

    // Additional conditions to check before showing the widget
    bool CheckAdditionalConditions();

    // Loads and creates the widget
    void LoadAndCreateWidget();

    // Current widget instance
    UPROPERTY()
    UUserWidget* CurrentWidget;

    // Soft reference to the widget class
    UPROPERTY(EditAnywhere, Category = "Interaction")
    TSubclassOf<UUserWidget> WidgetClass;

    // Collision component explicitly assigned for interaction
    //UPROPERTY(EditAnywhere, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
    UPrimitiveComponent* InteractionCollision;

    // Collision component explicitly assigned for interaction
    UPROPERTY(EditAnywhere, Category = "Interaction")
    FComponentReference OverlapComponent;

    UPROPERTY(EditAnywhere, Category = "Interaction")
    TSubclassOf<UInteractionWidgetCondition> AddtiveConditionObject;

    UPROPERTY(EditAnywhere, Category = "Interaction")
    float TickIntervalWhenDisplay = 0.1;

    UPROPERTY(EditAnywhere, Category = "Interaction")
    float TickIntervalWhenOnlyOverlap = 0.2;

    UPROPERTY(EditAnywhere, Category = "Interaction")
    FVector ConditionCheckOffset = FVector(0, 0, 0);

    // Whether the widget is currently displayed
    bool bWidgetDisplayed;

    ACharacter* OverlappedChar = nullptr;
	
};
