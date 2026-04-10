

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "SimpleMovementParamsData.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API USimpleMovementParamsData : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement Settings")
	TMap<FName, FCALSMovementSettingsStrafe> MovementSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement Settings")
	TMap<CALS_GroundedMoveMode, FName> MatchToMovementState;

};
