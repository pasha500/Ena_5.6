
#pragma once

#include "PashaAnimNodesTool/Public/Pasha_LayerBlendingLogic.h"
#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "Pasha_LayerBlendingGraph.generated.h"
/**
 *
 */
UCLASS()
class PASHAANIMNODES_API UPasha_LayerBlendingGraph : public UAnimGraphNode_Base
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, Category = "Settings")
        FPasha_LayerBlendingLogic Node;

    //~ Begin UEdGraphNode Interface.
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual FText GetTooltipText() const override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    //~ End UEdGraphNode Interface.

    virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog) override;

    //~ Begin UAnimGraphNode_Base Interface
    virtual FString GetNodeCategory() const override;
    //~ End UAnimGraphNode_Base Interface

};


//class PASHAANIMNODES_API Pasha_LayerBlendingGraph



