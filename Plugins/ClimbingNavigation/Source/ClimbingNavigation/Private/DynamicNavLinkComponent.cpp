


#include "DynamicNavLinkComponent.h"

#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_LowHeight.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Obstacle.h"



void UDynamicNavLinkComponent::RebuildNavLinks()
{

	for (FNavigationLink& Link : Links)
	{
		Link.InitializeAreaClass(true);
	}

	UpdateNavigationData();
	UpdateNavigationBounds();

	MarkRenderStateDirty();
}

void UDynamicNavLinkComponent::AddNewNavLink(const FNavigationLink& Object, float LeftProjectHeight, float RightProjectHeight, float SnapRadius, NavLink_NavAreaMode NavAreaMode)
{
	FNavigationLink NewLink;
	NewLink = Object;

	NewLink.LeftProjectHeight = LeftProjectHeight;
	NewLink.SnapRadius = SnapRadius;

	if(NavAreaMode == NavLink_NavAreaMode::Default) NewLink.SetAreaClass(UNavArea_Default::StaticClass());
	else if (NavAreaMode == NavLink_NavAreaMode::LowHeight) NewLink.SetAreaClass(UNavArea_LowHeight::StaticClass());
	else if (NavAreaMode == NavLink_NavAreaMode::Null) NewLink.SetAreaClass(UNavArea_Null::StaticClass());
	else if (NavAreaMode == NavLink_NavAreaMode::Obstacle) NewLink.SetAreaClass(UNavArea_Obstacle::StaticClass());

	NewLink.InitializeAreaClass(true);
	Links.Add(NewLink);

	//RebuildNavLinks();
}
