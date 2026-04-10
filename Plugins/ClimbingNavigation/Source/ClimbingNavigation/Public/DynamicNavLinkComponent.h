

#pragma once

#include "CoreMinimal.h"
#include "NavLinkComponent.h"
#include "DynamicNavLinkComponent.generated.h"

UENUM(BlueprintType)
enum class NavLink_NavAreaMode : uint8
{
	Default,
	LowHeight,
	Null,
	Obstacle
};


UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), hidecategories = (Activation))
class CLIMBINGNAVIGATION_API UDynamicNavLinkComponent : public UNavLinkComponent
{
	GENERATED_BODY()


public:

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Rebuild Nav Links", Keywords = "Navigation"), Category = "Navigation")
	virtual void RebuildNavLinks();

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add New Nav Link", Keywords = "Navigation", AdvancedDisplay = 1), Category = "Navigation")
	virtual void AddNewNavLink(const FNavigationLink& Object, float LeftProjectHeight = 0.0, float RightProjectHeight = 1000.0, float SnapRadius = 30.0, NavLink_NavAreaMode NavAreaMode = NavLink_NavAreaMode::Default);

};
