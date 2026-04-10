

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AGLS_EditorCommands.generated.h"

/**
 * 
 */
UCLASS()
class GRAPHDEBBUGER_API UAGLS_EditorCommands : public UObject
{
	GENERATED_BODY()

public:

	static void SetInstancesVisibility(bool bHide, FName TagName, FString ActorName);

private:

	static FAutoConsoleCommand ToggleVisibilityCommand;
	static FAutoConsoleCommand AGLS_ClimbNavVisiblity;

};
