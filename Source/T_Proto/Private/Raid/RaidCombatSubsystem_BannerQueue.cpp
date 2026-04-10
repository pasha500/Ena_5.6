#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRegionBannerWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

void URaidCombatSubsystem::EnqueueRegionBannerMessage(const FText& Title, const FText& Subtitle, float DurationSeconds, bool bHighPriority)
{
    if (!bEnableRegionBannerQueue)
    {
        return;
    }

    if (Title.IsEmpty() && Subtitle.IsEmpty())
    {
        return;
    }

    const float SafeDuration = FMath::Clamp(DurationSeconds, 1.0f, 12.0f);

    for (const FRaidQueuedRegionBanner& Existing : PendingRegionBanners)
    {
        if (Existing.Title.EqualTo(Title) && Existing.Subtitle.EqualTo(Subtitle))
        {
            return;
        }
    }

    FRaidQueuedRegionBanner NewMessage;
    NewMessage.Title = Title;
    NewMessage.Subtitle = Subtitle;
    NewMessage.DurationSeconds = SafeDuration;
    NewMessage.Priority = bHighPriority ? 1 : 0;
    if (const UWorld* World = GetWorld())
    {
        NewMessage.EnqueuedAtSeconds = World->GetTimeSeconds();
    }

    if (bHighPriority)
    {
        PendingRegionBanners.Insert(MoveTemp(NewMessage), 0);
    }
    else
    {
        PendingRegionBanners.Add(MoveTemp(NewMessage));
    }

    StartRegionBannerQueueTicker();
}

APlayerController* URaidCombatSubsystem::GetPrimaryLocalPlayerController() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (APlayerController* FirstPC = World->GetFirstPlayerController())
    {
        if (FirstPC->IsLocalController())
        {
            return FirstPC;
        }
    }

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (IsValid(PC) && PC->IsLocalController())
        {
            return PC;
        }
    }

    return nullptr;
}

bool URaidCombatSubsystem::IsAnyRegionBannerWidgetVisible() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    TArray<UUserWidget*> FoundWidgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, FoundWidgets, URaidRegionBannerWidget::StaticClass(), false);
    for (UUserWidget* Widget : FoundWidgets)
    {
        if (IsValid(Widget) && Widget->IsInViewport())
        {
            return true;
        }
    }
    return false;
}

bool URaidCombatSubsystem::TryPresentRegionBannerNow(const FRaidQueuedRegionBanner& BannerMessage)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    APlayerController* LocalPC = GetPrimaryLocalPlayerController();
    if (!IsValid(LocalPC))
    {
        return false;
    }

    UClass* WidgetClass = CachedRegionBannerWidgetClass.Get();
    if (!WidgetClass)
    {
        WidgetClass = RegionBannerWidgetClass.LoadSynchronous();
        if (!WidgetClass)
        {
            WidgetClass = LoadClass<URaidRegionBannerWidget>(nullptr, TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C"));
        }
        CachedRegionBannerWidgetClass = WidgetClass;
    }
    if (!WidgetClass)
    {
        return false;
    }

    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->GetWorld() != World)
    {
        if (ActiveRegionBannerWidget->IsInViewport())
        {
            ActiveRegionBannerWidget->RemoveFromParent();
        }
        ActiveRegionBannerWidget = nullptr;
    }

    if (!IsValid(ActiveRegionBannerWidget))
    {
        ActiveRegionBannerWidget = CreateWidget<URaidRegionBannerWidget>(LocalPC, WidgetClass);
    }
    if (!IsValid(ActiveRegionBannerWidget))
    {
        return false;
    }

    if (!ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->AddToViewport(25);
    }

    const float SafeDuration = FMath::Clamp(BannerMessage.DurationSeconds, 1.0f, 12.0f);
    ActiveRegionBannerWidget->ShowRegionTitle(BannerMessage.Title, BannerMessage.Subtitle, SafeDuration);

    bRegionBannerVisibleBySubsystem = true;
    RegionBannerBusyUntilTimeSeconds = World->GetTimeSeconds() + SafeDuration + FMath::Max(0.0f, RegionBannerCooldownPadding);

    World->GetTimerManager().ClearTimer(RegionBannerAutoHideTimerHandle);
    World->GetTimerManager().SetTimer(
        RegionBannerAutoHideTimerHandle,
        this,
        &URaidCombatSubsystem::HideRegionBannerWidgetNow,
        SafeDuration + 1.10f,
        false);

    return true;
}

void URaidCombatSubsystem::HideRegionBannerWidgetNow()
{
    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->RemoveFromParent();
    }

    bRegionBannerVisibleBySubsystem = false;
    if (const UWorld* World = GetWorld())
    {
        RegionBannerBusyUntilTimeSeconds = World->GetTimeSeconds();
    }

    StopRegionBannerQueueTickerIfIdle();
}

void URaidCombatSubsystem::TickRegionBannerQueue()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const double NowSeconds = World->GetTimeSeconds();

    if (bRegionBannerVisibleBySubsystem && NowSeconds < RegionBannerBusyUntilTimeSeconds)
    {
        return;
    }

    if (IsAnyRegionBannerWidgetVisible())
    {
        return;
    }

    bRegionBannerVisibleBySubsystem = false;

    if (PendingRegionBanners.Num() <= 0)
    {
        StopRegionBannerQueueTickerIfIdle();
        return;
    }

    const FRaidQueuedRegionBanner NextMessage = PendingRegionBanners[0];
    if (TryPresentRegionBannerNow(NextMessage))
    {
        PendingRegionBanners.RemoveAt(0);
    }
}

void URaidCombatSubsystem::StartRegionBannerQueueTicker()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FTimerManager& TimerManager = World->GetTimerManager();
    if (TimerManager.IsTimerActive(RegionBannerQueueTickHandle))
    {
        return;
    }

    TimerManager.SetTimer(
        RegionBannerQueueTickHandle,
        this,
        &URaidCombatSubsystem::TickRegionBannerQueue,
        FMath::Clamp(RegionBannerQueueTickInterval, 0.05f, 1.0f),
        true,
        0.01f);
}

void URaidCombatSubsystem::StopRegionBannerQueueTickerIfIdle()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (PendingRegionBanners.Num() > 0 || bRegionBannerVisibleBySubsystem)
    {
        return;
    }

    FTimerManager& TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(RegionBannerQueueTickHandle);
    TimerManager.ClearTimer(RegionBannerAutoHideTimerHandle);
}
