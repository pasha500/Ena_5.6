

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

/**
 * 
 */
class GRAPHDEBBUGER_API FCustomFunctionsVisualizer : public FComponentVisualizer
{
public:
    virtual void DrawVisualization(
        const UActorComponent* Component,
        const FSceneView* View,
        FPrimitiveDrawInterface* PDI) override;
};
