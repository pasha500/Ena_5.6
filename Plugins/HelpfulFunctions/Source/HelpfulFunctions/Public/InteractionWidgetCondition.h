

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractionWidgetCondition.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class HELPFULFUNCTIONS_API UInteractionWidgetCondition : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction Widget", meta = (ForceAsFunction, DisplayName = "Addtive Condition To Check", Keywords = "Widget,Player,Condition"))
	bool AddtiveConditionToCheck(ACharacter* OverlappedActor,AActor* InteractiveActor, FVector WidgetLocation, FVector ActorLocation);
	virtual bool AddtiveConditionToCheck_Implementation(ACharacter* OverlappedActor, AActor* InteractiveActor, FVector WidgetLocation, FVector ActorLocation);
	
};
