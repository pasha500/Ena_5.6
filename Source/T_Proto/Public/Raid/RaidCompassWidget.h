#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RaidCompassWidget.generated.h"

UCLASS()
class T_PROTO_API URaidCompassWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Raid|Compass")
    float GetCompassRatio(FVector TargetLocation, float CompassFOV = 120.0f) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Raid|Guidance")
    float GetGuidanceOpacity(float Urgency) const;
};
