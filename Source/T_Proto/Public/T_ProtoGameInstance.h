#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "T_ProtoGameInstance.generated.h"

class UUserWidget;

UCLASS()
class T_PROTO_API UT_ProtoGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

protected:
	// Assign this in Class Defaults (e.g. WBP_LoadingScreen)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Screen")
	TSoftClassPtr<UUserWidget> LoadingScreenWidgetClass;

	// Minimum loading screen visible time in seconds (0 = hide immediately)
	UPROPERTY(EditDefaultsOnly, Category = "Loading Screen", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float LoadingScreenMinDisplaySeconds = 1.0f;

	// If map name contains any token below, loading screen is skipped
	UPROPERTY(EditDefaultsOnly, Category = "Loading Screen")
	TArray<FString> LoadingScreenExcludedMapNames = { TEXT("MainmenuLevel"), TEXT("MainMenu") };

private:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveLoadingScreen = nullptr;

	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FTimerHandle MinDisplayTimerHandle;

	bool bLoadingScreenActive = false;
	double LoadingScreenStartTime = 0.0;

	void OnPreLoadMap(const FString& MapName);
	void OnPostLoadMap(UWorld* LoadedWorld);
	void HideLoadingScreen();

	// Main menu UX safety: ensure cursor + UI-only input after map load.
	void ApplyMainMenuInputMode(UWorld* LoadedWorld);
	void TryApplyMainMenuInputMode(UWorld* LoadedWorld, int32 RemainingRetries);
	bool IsMainMenuMapName(const FString& MapName) const;
};
