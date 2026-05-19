#include "IWALS_SniperScopeStateComponent.h"

#include "Math/UnrealMathUtility.h"

UIWALS_SniperScopeStateComponent::UIWALS_SniperScopeStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIWALS_SniperScopeStateComponent::SetSniperScopeState(
	const bool bInIsSniperWeapon,
	const bool bInIsAimingDownSights,
	const bool bInUseScopedFOV,
	const float InScopedFOV)
{
	bIsSniperWeapon = bInIsSniperWeapon;
	bIsAimingDownSights = bInIsAimingDownSights;
	bUseScopedFOV = bInUseScopedFOV;
	ScopedFOV = FMath::Clamp(InScopedFOV, 1.0f, 179.0f);
}

bool UIWALS_SniperScopeStateComponent::IWALS_GetSniperScopeViewState_Implementation(
	FIWALSSniperScopeViewState& OutViewState) const
{
	if (!bProvideState)
	{
		return false;
	}

	OutViewState.bIsValid = true;
	OutViewState.bIsSniperWeapon = bIsSniperWeapon;
	OutViewState.bIsAimingDownSights = bIsAimingDownSights;
	OutViewState.bUseScopedFOV = bUseScopedFOV;
	OutViewState.ScopedFOV = FMath::Clamp(ScopedFOV, 1.0f, 179.0f);
	OutViewState.KeepVisibleComponentTags = KeepVisibleComponentTags;
	return true;
}

