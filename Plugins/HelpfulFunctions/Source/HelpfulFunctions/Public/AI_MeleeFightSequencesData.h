

#pragma once

#include "CoreMinimal.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "Engine/DataAsset.h"
#include "AI_MeleeFightSequencesData.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class HELPFULFUNCTIONS_API UAI_MeleeFightSequencesData : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Sequences")
	TMap<FName, FAGLS_FinishersDataForAI> AnimSequences;

};
