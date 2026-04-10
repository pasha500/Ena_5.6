

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "IWALS_GameplayAbilitySet.h"
#include "InteractionAbilitiesSet.generated.h"

/**
 *
 */
UCLASS()
class IWALS_ABILITYSYSTEM_API UInteractionAbilitiesSet : public UDataAsset
{
	GENERATED_BODY()

public:

	/* Abilities that will be stored here and will not be loaded into memory at the start of the game. During the interaction, it is determined
	   what matching GameplayAbility is required, and then the class is loaded into memory from disk.*/
	UPROPERTY(EditAnywhere, Category = AbilitySetSoftRef)
	TMap<FGameplayTag, TSoftClassPtr<UGameplayAbility>> SoftRefAbilitiesSet;

	UPROPERTY(EditAnywhere, Category = AbilitySetAlwaysLoaded)
	TArray<FGameplayAbilityBindIWALS> AlwaysLoadedAbilitiesSet;

};
