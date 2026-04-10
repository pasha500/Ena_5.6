//2024 Jakub W

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractionWidgetInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UInteractionWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class HELPFULFUNCTIONS_API IInteractionWidgetInterface
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction Widget Interface")
	void BPI_UI_Set_WidgetWorldLocation(FVector InWorldLocation);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction Widget Interface")
	void BPI_UI_PlayInitAnimation();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interaction Widget Interface")
	void BPI_UI_Set_WidgetTextsValues(FName Text01, FName Text02, float Float01, FLinearColor Color01, FLinearColor Color02, UObject* Object01, UObject* Object02);

};
