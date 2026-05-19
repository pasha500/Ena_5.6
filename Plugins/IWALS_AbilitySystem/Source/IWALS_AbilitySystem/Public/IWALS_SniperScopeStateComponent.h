#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IWALS_SniperScopeProvider.h"
#include "IWALS_SniperScopeStateComponent.generated.h"

/**
 * Authoritative sniper scope state provider.
 * Add this component to character/weapon BPs and drive values from existing gameplay logic.
 */
UCLASS(ClassGroup = (IWALS), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class IWALS_ABILITYSYSTEM_API UIWALS_SniperScopeStateComponent : public UActorComponent, public IIWALS_SniperScopeProvider
{
	GENERATED_BODY()

public:
	UIWALS_SniperScopeStateComponent();

	// Disable to temporarily opt out from the provider pipeline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bProvideState = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bIsSniperWeapon = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bIsAimingDownSights = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	bool bUseScopedFOV = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope", meta = (ClampMin = "1.0", ClampMax = "179.0", EditCondition = "bUseScopedFOV"))
	float ScopedFOV = 25.0f;

	// Mesh component tags to keep visible while scoped (reticle/lens/scope-only meshes).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IWALS|Sniper Scope")
	TArray<FName> KeepVisibleComponentTags;

	UFUNCTION(BlueprintCallable, Category = "IWALS|Sniper Scope")
	void SetSniperScopeState(bool bInIsSniperWeapon, bool bInIsAimingDownSights, bool bInUseScopedFOV, float InScopedFOV);

	virtual bool IWALS_GetSniperScopeViewState_Implementation(FIWALSSniperScopeViewState& OutViewState) const override;
};

