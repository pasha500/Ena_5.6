// Pasha

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IWALS_SniperScopeProvider.generated.h"

/**
 * Data contract for sniper/scope presentation.
 * Providers (weapon actor/component, character, etc.) populate this state.
 */
USTRUCT(BlueprintType)
struct FIWALSSniperScopeViewState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bIsSniperWeapon = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bIsAimingDownSights = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bUseScopedFOV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float ScopedFOV = 25.0f;

	// Components matching these tags remain visible during scoped ADS.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	TArray<FName> KeepVisibleComponentTags;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UIWALS_SniperScopeProvider : public UInterface
{
	GENERATED_BODY()
};

class IWALS_ABILITYSYSTEM_API IIWALS_SniperScopeProvider
{
	GENERATED_BODY()

public:
	// Return true when provider has an authoritative state for current frame.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "IWALS|Sniper Scope")
	bool IWALS_GetSniperScopeViewState(FIWALSSniperScopeViewState& OutViewState) const;
};
