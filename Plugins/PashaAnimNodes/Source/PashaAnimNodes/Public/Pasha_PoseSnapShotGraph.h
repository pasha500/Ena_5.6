#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "PashaAnimNodesTool/Public/Pasha_PoseSnapShotLogic.h"
#include "Pasha_PoseSnapShotGraph.generated.h"

// This will be displayed in the AnimGraph
UCLASS()
class PASHAANIMNODES_API UPasha_PoseSnapShotGraph : public UAnimGraphNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Settings")
    FPasha_PoseSnapShotLogic Node;

    // Get the tooltip for this node
    virtual FText GetTooltipText() const override;

    // Get the node title for this node
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

    // Get the menu category for this node
    //virtual FString GetMenuCategory() const override;
};