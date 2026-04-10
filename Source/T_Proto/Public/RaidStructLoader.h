#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "RaidStructLoader.generated.h"

UCLASS()
class T_PROTO_API URaidStructLoader : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Raid|Debug")
	static void ForceLoadLevelNodeRow();
};
