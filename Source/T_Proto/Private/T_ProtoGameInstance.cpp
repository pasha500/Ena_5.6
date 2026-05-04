#include "T_ProtoGameInstance.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void UT_ProtoGameInstance::Init()
{
	Super::Init();

	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(
		this, &UT_ProtoGameInstance::OnPreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UT_ProtoGameInstance::OnPostLoadMap);
}

void UT_ProtoGameInstance::Shutdown()
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	Super::Shutdown();
}

void UT_ProtoGameInstance::OnPreLoadMap(const FString& MapName)
{
	for (const FString& Excluded : LoadingScreenExcludedMapNames)
	{
		if (MapName.Contains(Excluded, ESearchCase::IgnoreCase))
		{
			return;
		}
	}

	if (LoadingScreenWidgetClass.IsNull())
	{
		return;
	}

	UClass* WidgetClass = LoadingScreenWidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		return;
	}

	// Use GameInstance as outer to survive during world travel.
	ActiveLoadingScreen = CreateWidget<UUserWidget>(this, WidgetClass);
	if (ActiveLoadingScreen)
	{
		ActiveLoadingScreen->AddToViewport(200);
		bLoadingScreenActive = true;
		LoadingScreenStartTime = FPlatformTime::Seconds();
	}
}

void UT_ProtoGameInstance::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (bLoadingScreenActive && ActiveLoadingScreen)
	{
		const double Elapsed = FPlatformTime::Seconds() - LoadingScreenStartTime;
		const float Remaining = static_cast<float>(LoadingScreenMinDisplaySeconds - Elapsed);

		if (Remaining <= 0.0f)
		{
			HideLoadingScreen();
		}
		else if (UWorld* World = LoadedWorld ? LoadedWorld : GetWorld())
		{
			World->GetTimerManager().SetTimer(
				MinDisplayTimerHandle,
				FTimerDelegate::CreateUObject(this, &UT_ProtoGameInstance::HideLoadingScreen),
				Remaining,
				false);
		}
		else
		{
			HideLoadingScreen();
		}
	}

	ApplyMainMenuInputMode(LoadedWorld);
}

void UT_ProtoGameInstance::HideLoadingScreen()
{
	if (ActiveLoadingScreen)
	{
		ActiveLoadingScreen->RemoveFromParent();
		ActiveLoadingScreen = nullptr;
	}
	bLoadingScreenActive = false;
}

bool UT_ProtoGameInstance::IsMainMenuMapName(const FString& MapName) const
{
	return MapName.Contains(TEXT("MainmenuLevel"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("MainMenu"), ESearchCase::IgnoreCase);
}

void UT_ProtoGameInstance::ApplyMainMenuInputMode(UWorld* LoadedWorld)
{
	if (!LoadedWorld)
	{
		return;
	}

	if (!IsMainMenuMapName(LoadedWorld->GetMapName()))
	{
		return;
	}

	TryApplyMainMenuInputMode(LoadedWorld, 20);
}

void UT_ProtoGameInstance::TryApplyMainMenuInputMode(UWorld* LoadedWorld, int32 RemainingRetries)
{
	if (!LoadedWorld)
	{
		return;
	}

	if (APlayerController* PC = LoadedWorld->GetFirstPlayerController())
	{
		PC->SetShowMouseCursor(true);
		PC->bEnableClickEvents = true;
		PC->bEnableMouseOverEvents = true;

		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		return;
	}

	if (RemainingRetries <= 0)
	{
		return;
	}

	FTimerDelegate RetryDelegate = FTimerDelegate::CreateUObject(
		this,
		&UT_ProtoGameInstance::TryApplyMainMenuInputMode,
		LoadedWorld,
		RemainingRetries - 1);

	LoadedWorld->GetTimerManager().SetTimerForNextTick(RetryDelegate);
}
