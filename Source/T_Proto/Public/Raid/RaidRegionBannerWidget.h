#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RaidRegionBannerWidget.generated.h"

UCLASS(BlueprintType, Blueprintable)
class T_PROTO_API URaidRegionBannerWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Implement in BP: fade-in -> hold -> fade-out title/subtitle.
    UFUNCTION(BlueprintImplementableEvent, Category = "Raid|Region Banner")
    void ShowRegionTitle(const FText& Title, const FText& Subtitle, float DurationSeconds);
};

