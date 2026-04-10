


#include "InteractiveActorsInterface.h"

// Add default functionality here for any IInteractiveActorsInterface functions that are not pure virtual.

void IInteractiveActorsInterface::BPI_IA_Get_CanDisplayWidget(bool& CanDisplay) const
{
	CanDisplay = true;
}

void IInteractiveActorsInterface::BPI_IA_Get_DestroyWhenAbilityRun(bool& Destroy) const
{
}

TSubclassOf<UInteractionWidgetCondition> IInteractiveActorsInterface::BPI_IA_Get_AddtiveConditionClass() const
{
	return TSubclassOf<UInteractionWidgetCondition>();
}



