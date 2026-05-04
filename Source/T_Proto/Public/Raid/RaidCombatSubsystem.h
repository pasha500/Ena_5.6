#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "RaidCombatSubsystem.generated.h"

class AActor;
class APawn;
class APlayerController;
class ARaidRoomActor;
class UDataTable;
class URaidChapterConfig;
class URaidEnemyPresetRegistry;
class URaidRegionBannerWidget;
class URaidWaveProfile;
class UNavigationSystemV1;
struct FStreamableHandle;
struct FHitResult;
struct FLevelNodeRow;

USTRUCT(BlueprintType)
struct FRaidPOI
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Compass")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Compass")
    FName MarkerType = TEXT("Default");
};

USTRUCT(BlueprintType)
struct FRaidGuidanceSignal
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Guidance")
    bool bValid = false;

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Guidance")
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Guidance")
    float Urgency = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Guidance")
    bool bUseStrongCue = false;

    UPROPERTY(BlueprintReadWrite, Category = "Raid|Guidance")
    FName CueStyle = TEXT("Subtle");
};

USTRUCT(BlueprintType)
struct FRaidRoomConceptRule
{
    GENERATED_BODY()

    // -1이면 모든 룸에 적용 가능
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    int32 RoomId = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FString MatchRoomType = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FString MatchRoomRoleContains = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FString MatchTagContains = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FString MatchThemeContains = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FName EnemyPresetOverride = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept")
    FString BotProfileOverride = TEXT("");

    // 0 이하면 원본 Row.SpawnCount 사용
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept", meta = (ClampMin = "0", ClampMax = "64"))
    int32 SpawnCountOverride = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept", meta = (ClampMin = "0.10", ClampMax = "6.0"))
    float DifficultyMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Concept", meta = (ClampMin = "0.10", ClampMax = "6.0"))
    float CombatWeightMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct FRaidWaveSpawnEntry
{
    GENERATED_BODY()

    // Actor class priority: if valid, this class is spawned first.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave")
    TSoftClassPtr<APawn> EnemyClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave")
    FName EnemyPreset = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave", meta = (ClampMin = "1", ClampMax = "128"))
    int32 SpawnCount = 4;
};

USTRUCT(BlueprintType)
struct FRaidWaveDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave")
    FText WaveLabel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave")
    TArray<FRaidWaveSpawnEntry> Entries;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave|Distance", meta = (ClampMin = "1500.0", ClampMax = "180000.0"))
    float SpawnMinDistanceFromPlayer = 5500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave|Distance", meta = (ClampMin = "2000.0", ClampMax = "220000.0"))
    float SpawnMaxDistanceFromPlayer = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Wave|Distance", meta = (ClampMin = "200.0", ClampMax = "8000.0"))
    float SpawnScatterRadius = 1400.0f;
};

USTRUCT(BlueprintType)
struct FRaidWaveStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    bool bWaveSystemEnabled = false;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    bool bAllWavesCompleted = false;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    int32 CurrentWave = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    int32 TotalWaves = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    int32 AliveWaveEnemies = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    float SecondsUntilNextWave = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Raid|Wave")
    FText NextWaveLabel;
};

USTRUCT()
struct FRaidEnemySpawnPlanEntry
{
    GENERATED_BODY()

    UPROPERTY()
    TSubclassOf<APawn> EnemyClass;

    UPROPERTY()
    FName PresetId = NAME_None;

    UPROPERTY()
    FVector SpawnLocation = FVector::ZeroVector;

    UPROPERTY()
    FRotator SpawnRotation = FRotator::ZeroRotator;

    UPROPERTY()
    FString BotProfile;

    UPROPERTY()
    float CapsuleRadius = 42.0f;

    UPROPERTY()
    float CapsuleHalfHeight = 88.0f;

    UPROPERTY()
    bool bForceSpawnIfColliding = false;
};

UCLASS()
class T_PROTO_API URaidCombatSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Raid|Room")
    void RegisterRoom(ARaidRoomActor* Room);

    // Backward-compatible entry point used by existing BPs/scripts.
    UFUNCTION(BlueprintCallable, Category = "Raid|Room")
    void RegisterRoomAsPOI(ARaidRoomActor* InRoom);

    UFUNCTION(BlueprintCallable, Category = "Raid|Room")
    void ResetSubsystem();

    UFUNCTION(BlueprintCallable, Category = "Raid|Combat")
    void StartCombatForRoom(ARaidRoomActor* Room);

    // Optional compatibility hooks for external systems.
    UFUNCTION(BlueprintCallable, Category = "Raid|Combat")
    void OnEnemySpawned(APawn* Enemy, int32 RoomId);

    UFUNCTION(BlueprintCallable, Category = "Raid|Combat")
    void OnEnemyKilled(APawn* Enemy);

    UFUNCTION(BlueprintPure, Category = "Raid|Compass")
    const TArray<FRaidPOI>& GetActivePOIs() const { return ActivePOIs; }

    UFUNCTION(BlueprintCallable, Category = "Raid|Compass")
    void AddPOI(const FVector& Loc, FName Type);

    UFUNCTION(BlueprintCallable, Category = "Raid|Compass")
    void ClearPOIs();

    UFUNCTION(BlueprintCallable, Category = "Raid|Compass")
    void UpdateCompassForNextRooms(ARaidRoomActor* ClearedRoom);

    UFUNCTION(BlueprintCallable, Category = "Raid|Guidance")
    FRaidGuidanceSignal GetGuidanceSignalForPlayer(APawn* PlayerPawn);

    UFUNCTION(BlueprintPure, Category = "Raid|Guidance")
    int32 GetCurrentObjectiveRoomId() const { return CurrentObjectiveRoomId; }

    UFUNCTION(BlueprintPure, Category = "Raid|Guidance")
    FVector GetCurrentObjectiveLocation() const { return CurrentObjectiveLocation; }

    UFUNCTION(BlueprintPure, Category = "Raid|Guidance")
    float GetRoomUtility(const ARaidRoomActor* Room, const FVector& PlayerLoc, bool bHasPendingBoss) const;

    UFUNCTION(BlueprintPure, Category = "Raid|Wave")
    FRaidWaveStatus GetWaveStatus() const;

    UFUNCTION(BlueprintCallable, Category = "Raid|Wave")
    void ForceStartNextWave();

    UFUNCTION(BlueprintCallable, Category = "Raid|Wave")
    void ConfigureDynamicWaves(bool bEnable, int32 InTotalWaves, float InFirstWaveDelaySeconds, float InWaveIntervalSeconds, int32 InMaxAliveWaveEnemies);

    UFUNCTION(BlueprintCallable, Category = "Raid|Wave")
    void SetWaveDefinitions(const TArray<FRaidWaveDefinition>& InWaveDefinitions);

    UFUNCTION(BlueprintCallable, Category = "Raid|Wave")
    void ApplyWaveProfile(const URaidWaveProfile* WaveProfile);

    UFUNCTION(BlueprintCallable, Category = "Raid|Combat|Prewarm")
    void RebuildUpcomingRoomSpawnPlans();

    UFUNCTION(BlueprintCallable, Category = "Raid|Combat|Concept")
    void SetRoomConceptRules(const TArray<FRaidRoomConceptRule>& InRules);

    UFUNCTION(BlueprintCallable, Category = "Raid|Loot")
    void ConfigureDropSoulPolicy(float InSpawnChance, int32 InMaxPerRoom, int32 InMaxActiveInWorld);

    UFUNCTION(BlueprintCallable, Category = "Raid|Performance")
    void PrimeRuntimeAssets(const URaidChapterConfig* ChapterConfig);

    UFUNCTION(BlueprintCallable, Category = "Raid|UI|Banner")
    void EnqueueRegionBannerMessage(const FText& Title, const FText& Subtitle, float DurationSeconds = 4.0f, bool bHighPriority = false);

private:
    struct FRaidQueuedRegionBanner
    {
        FText Title;
        FText Subtitle;
        float DurationSeconds = 4.0f;
        int32 Priority = 0;
        double EnqueuedAtSeconds = 0.0;
    };

    void LogTrackedEnemyState(APawn* EnemyPawn, int32 RoomId, const TCHAR* Reason);
    bool IsPawnDeadLike(const APawn* EnemyPawn) const;
    bool IsPlayerLikelyMakingGunfireNoise(const APawn* PlayerPawn) const;
    bool IsRoomCombatAlertActive(int32 RoomId, double NowSeconds) const;
    void RaiseRoomCombatAlert(int32 RoomId, double NowSeconds);
    double ResolveEnemySearchActivationTime(int32 RoomId, const FVector& EnemyLocation, double NowSeconds, const APawn* PlayerPawn) const;
    double ResolveControllerSpawnDelaySeconds(const APawn* SpawnedEnemy, int32 RoomId, int32 SpawnOrderIndex) const;
    void DisableDeadEnemyDamageSources(APawn* EnemyPawn, const TCHAR* Reason);
    void UpdateEnemySearchBehavior(APawn* EnemyPawn, int32 RoomId, double NowSeconds, APawn* PlayerPawn);
    bool RecoverEnemyIfOutOfWorld(APawn* EnemyPawn, int32 RoomId, bool& bOutNeedsCull);
    bool RecoverStuckEnemyIfBlocked(APawn* EnemyPawn, int32 RoomId, bool& bOutNeedsCull);
    double ComputeRecoveryHighLoadScaleInternal(int32 ActiveEnemyCount) const;
    double ComputeRecoveryCheckIntervalInternal(double BaseIntervalSeconds, int32 ActiveEnemyCount) const;
    void ApplyRoomConceptRule(
        const ARaidRoomActor* Room,
        FName& InOutPreset,
        FString& InOutBotProfile,
        int32& InOutSpawnCount,
        float& InOutDifficulty,
        float& InOutCombatWeight) const;
    bool BuildRoomSpawnPlan(ARaidRoomActor* Room, TArray<FRaidEnemySpawnPlanEntry>& OutPlan, bool bFromPrewarm, bool bLogResult);
    int32 ExecuteRoomSpawnPlan(ARaidRoomActor* Room, const TArray<FRaidEnemySpawnPlanEntry>& Plan, bool bFromPrewarm);
    void PrepareUpcomingRoomSpawnPlans(ARaidRoomActor* ClearedRoom);
    void TickWaveSpawning(double NowSeconds, APawn* PlayerPawn);
    void SpawnWaveNow(int32 WaveNumber, APawn* PlayerPawn);
    bool TryResolveWaveSpawnLocation(APawn* PlayerPawn, const FRaidWaveDefinition& WaveDef, float CapsuleRadius, float CapsuleHalfHeight, FVector& OutLocation) const;
    void SpawnEnemiesForRoom(ARaidRoomActor* Room);
    void SpawnEnemyControllerDeferred(APawn* SpawnedEnemy, int32 RoomId, const FString& SanitizedProfile, int32 SpawnOrderIndex);
    void SpawnOrRepairDropSoulAt(const FVector& WorldLocation, int32 SourceRoomId);
    void RepairDropSoulNiagaraBindings();
    bool EnsureDropSoulNiagaraBinding(AActor* DropSoulActor, bool bLogRepair) const;
    void SyncBandageHealFromLootDataTable();
    void SanitizeProceduralFoliageCollisionForTraces();
    void PatchBloodEffectTraceSettings();
    void ApplyPerformanceWarmupCVars() const;
    void PreloadWarmupAssets();
    void SpawnHiddenWarmupEnemy(const URaidEnemyPresetRegistry* Registry);
    void CleanupFloatingBloodDecals();
    void StartTraceCollisionEnforcer();
    void StopTraceCollisionEnforcer();
    void EnforceTraceCollisionOnAllAIPawns();
    void StartRegionBannerQueueTicker();
    void StopRegionBannerQueueTickerIfIdle();
    void TickRegionBannerQueue();
    void HideRegionBannerWidgetNow();
    bool IsAnyRegionBannerWidgetVisible() const;
    APlayerController* GetPrimaryLocalPlayerController() const;
    bool TryPresentRegionBannerNow(const FRaidQueuedRegionBanner& BannerMessage);
    int32 GetRoomTypePriorityInternal(const FString& RoomType) const;
    int32 MakeWaveRoomIdInternal(int32 WaveNumber) const;
    bool IsCombatSpawnRoomTypeInternal(const FString& RoomType) const;
    void BuildPresetCandidatesInternal(const FLevelNodeRow& Row, TArray<FName>& OutCandidates) const;
    bool IsWaterHitInternal(const FHitResult& Hit) const;
    bool IsCapsuleBlockedForPawnInternal(UWorld* World, const FVector& PawnActorLocation, float CapsuleRadius, float CapsuleHalfHeight, const AActor* ActorToIgnore = nullptr) const;
    void ResolvePawnCapsuleSizeInternal(TSubclassOf<APawn> EnemyClass, float& OutRadius, float& OutHalfHeight) const;
    void ResolvePawnInstanceCapsuleSizeInternal(const APawn* Pawn, float& OutRadius, float& OutHalfHeight) const;
    bool IsNearRoomObstacleInternal(UWorld* World, const ARaidRoomActor* Room, const FVector& Location, float Radius) const;
    bool TryResolveAIGroundHitInternal(UWorld* World, ARaidRoomActor* Room, const FVector& XYLocation, FHitResult& OutHit) const;
    bool TryResolveSafeAIPawnSpawnLocationInternal(UWorld* World, ARaidRoomActor* Room, UNavigationSystemV1* NavSys, const FVector& XYLocation, float CapsuleRadius, float CapsuleHalfHeight, FVector& OutActorLocation) const;
    bool TryResolveNearbyFallbackSpawnLocationInternal(UWorld* World, ARaidRoomActor* Room, UNavigationSystemV1* NavSys, const FVector& SeedLocation, float CapsuleRadius, float CapsuleHalfHeight, FRandomStream& Stream, FVector& OutActorLocation) const;
    void ForceEnemyTraceCollisionInternal(APawn* Enemy) const;
    void DisableEnemyRuntimeDeformerComponentsInternal(APawn* Enemy) const;
    void EnforceNoZombieGrabInternal(APawn* Enemy) const;
    void LogEnemyTraceCollisionSnapshotInternal(const APawn* Enemy) const;

    UFUNCTION()
    void OnEnemyDestroyed(AActor* DestroyedActor);

    void HandleRoomCleared(int32 RoomId);
    APawn* GetPrimaryPlayerPawn() const;
    ARaidRoomActor* FindStartRoom() const;
    bool IsPawnInsideRoomBounds2D(const APawn* Pawn, const ARaidRoomActor* Room) const;
    int32 ResolvePrimaryProgressionRoomId(const ARaidRoomActor* StartRoom) const;
    void RefreshStartRoomProgressState(APawn* PlayerPawn);
    void ForceObjectiveToRoom(ARaidRoomActor* Room, FName MarkerType = NAME_None);
    void AddNearbyOptionalPOIsFromStart(const ARaidRoomActor* StartRoom, int32 PrimaryRoomId);
    void ReevaluateObjectiveByPlayer(APawn* PlayerPawn);
    float EvaluateObjectiveUtility(const ARaidRoomActor* Room, const FVector& PlayerLoc, bool bHasPendingBoss) const;

private:
    UPROPERTY()
    TMap<int32, TObjectPtr<ARaidRoomActor>> RoomById;

    UPROPERTY()
    TMap<int32, int32> AliveByRoomId;

    UPROPERTY()
    TMap<AActor*, int32> EnemyToRoomMap;

    UPROPERTY()
    TArray<FRaidPOI> ActivePOIs;

    UPROPERTY()
    bool bInternalClearing = false;

    UPROPERTY()
    int32 CurrentObjectiveRoomId = -1;

    UPROPERTY()
    FVector CurrentObjectiveLocation = FVector::ZeroVector;

    UPROPERTY()
    float LastProgressTimeSeconds = 0.0f;

    UPROPERTY()
    float LastDistanceToObjective = TNumericLimits<float>::Max();

    UPROPERTY()
    float WrongDirectionScore = 0.0f;

    UPROPERTY()
    bool bFoliageTraceCollisionSanitized = false;

    UPROPERTY()
    bool bBloodTraceSettingsPatched = false;

    UPROPERTY()
    bool bRuntimeAssetsPrimed = false;

    UPROPERTY()
    bool bRuntimeAssetWarmupRequested = false;

    UPROPERTY()
    bool bEnemyPresetClassesPrimed = false;

    UPROPERTY()
    bool bEnemyPresetWarmupRequested = false;

    UPROPERTY()
    double NextFoliageSanitizeRetryTimeSeconds = 0.0;

    UPROPERTY()
    double NextBloodDecalCleanupTimeSeconds = 0.0;

    UPROPERTY()
    int32 FoliageSanitizeRetryCount = 0;

    UPROPERTY()
    double NextFallbackEnemySweepTimeSeconds = 0.0;

    UPROPERTY()
    int32 StartRoomId = -1;

    UPROPERTY()
    bool bStartFlowInitialized = false;

    UPROPERTY()
    bool bPlayerSpawnedInsideStartRoom = false;

    UPROPERTY()
    bool bStartPendingClearOnExit = false;

    UPROPERTY()
    double RecentSpawnHeavyWorkDeferUntilSeconds = 0.0;

    TSharedPtr<FStreamableHandle> RuntimeWarmupHandle;
    TSharedPtr<FStreamableHandle> EnemyPresetWarmupHandle;

    FTimerHandle TraceCollisionEnforcerHandle;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float GentleNudgeDelay = 10.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "2.0", ClampMax = "180.0"))
    float StrongNudgeDelay = 22.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "0.0", ClampMax = "300.0"))
    float ObjectiveSwitchHysteresis = 45.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ObjectiveProximityWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ObjectiveValueWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Guidance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ObjectiveSafetyWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Compass", meta = (ClampMin = "1000.0", ClampMax = "100000.0"))
    float StartOptionalPOIRadius = 18000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Compass", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
    float RoomInsideCheckPadding = 150.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxEnemiesPerRoom = 6;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.1", ClampMax = "30.0"))
    float FoliageSanitizeRetryInterval = 0.80f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1.0", ClampMax = "120.0"))
    float FoliageSanitizeMonitorInterval = 60.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "120"))
    int32 MaxFoliageSanitizeRetryCount = 10;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1000.0", ClampMax = "200000.0"))
    float FoliageSanitizeMaxDistanceFromPlayer = 32000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "8", ClampMax = "2048"))
    int32 MaxTraceBlockerActorsScannedPerSweep = 160;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "512"))
    int32 MaxTraceBlockerComponentsPatchedPerSweep = 32;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat")
    bool bEnableCombatPerfLogs = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Recovery", meta = (ClampMin = "8", ClampMax = "512"))
    int32 RecoveryHighLoadEnemyThreshold = 20;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Recovery", meta = (ClampMin = "1.0", ClampMax = "4.0"))
    float RecoveryHighLoadIntervalScale = 1.65f;

    UPROPERTY(EditAnywhere, Category = "Raid|UI|Banner")
    bool bEnableRegionBannerQueue = true;

    UPROPERTY(EditAnywhere, Category = "Raid|UI|Banner", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float RegionBannerQueueTickInterval = 0.12f;

    UPROPERTY(EditAnywhere, Category = "Raid|UI|Banner", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float RegionBannerCooldownPadding = 0.20f;

    UPROPERTY(EditAnywhere, Category = "Raid|UI|Banner")
    TSoftClassPtr<URaidRegionBannerWidget> RegionBannerWidgetClass = TSoftClassPtr<URaidRegionBannerWidget>(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C")));

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bEnableRuntimeAssetWarmup = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bEnableAsyncRuntimeAssetWarmup = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bEnableAsyncEnemyPresetPreload = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bForceNiagaraPSOPrecache = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bSpawnHiddenEnemyWarmup = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    bool bDisableEnemyRuntimeMeshDeformer = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance", meta = (ClampMin = "0.05", ClampMax = "5.0"))
    float HiddenEnemyWarmupLifeSeconds = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance", meta = (ClampMin = "0.0", ClampMax = "0.25"))
    float EnemyControllerSpawnDelayStep = 0.08f;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance", meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float PostSpawnHeavyTaskCooldown = 1.20f;

    UPROPERTY(EditAnywhere, Category = "Raid|Performance")
    TArray<FSoftObjectPath> AdditionalWarmupAssets;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.1", ClampMax = "5.0"))
    float TraceCollisionEnforcerTickInterval = 1.00f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float EnemyTraceCollisionRefreshInterval = 3.50f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.5", ClampMax = "30.0"))
    float FallbackEnemyTraceSweepInterval = 20.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat")
    bool bEnableUndergroundEnemyRecovery = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.10", ClampMax = "10.0"))
    float UndergroundEnemyCheckInterval = 1.10f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "120.0", ClampMax = "4000.0"))
    float UndergroundEnemyZTolerance = 420.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "20"))
    int32 MaxUndergroundEnemyRecoveryFailures = 4;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat")
    bool bEnableStuckEnemyRecovery = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.10", ClampMax = "10.0"))
    float StuckEnemyCheckInterval = 1.20f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.5", ClampMax = "30.0"))
    float StuckEnemyDetectionSeconds = 7.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "20.0", ClampMax = "2000.0"))
    float StuckEnemyMinProgressDistance2D = 110.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.0", ClampMax = "600.0"))
    float StuckEnemyMinVelocity2D = 24.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.0", ClampMax = "3000.0"))
    float StuckEnemyIgnoreWhenNearPlayerDistance = 260.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.0", ClampMax = "20.0"))
    float EnemyRecoverySpawnGraceSeconds = 3.5f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "20"))
    int32 MaxStuckEnemyRecoveryFailures = 4;

    UPROPERTY(EditAnywhere, Category = "Raid|Debug")
    bool bEnableRoomEnemyTracking = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Debug", meta = (ClampMin = "0"))
    int32 TrackedRoomId = 1;

    UPROPERTY(EditAnywhere, Category = "Raid|Debug", meta = (ClampMin = "0.10", ClampMax = "10.0"))
    float TrackedEnemyPeriodicLogInterval = 1.25f;

    UPROPERTY(EditAnywhere, Category = "Raid|Debug", meta = (ClampMin = "20.0", ClampMax = "2000.0"))
    float TrackedEnemySuddenZDropThreshold = 180.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "0.05", ClampMax = "5.0"))
    float BloodDecalCleanupInterval = 6.00f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat")
    bool bEnableBloodDecalCleanup = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1000.0", ClampMax = "100000.0"))
    float BloodDecalCleanupMaxDistanceFromPlayer = 18000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "512"))
    int32 MaxBloodDecalsEvaluatedPerSweep = 16;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "1", ClampMax = "128"))
    int32 MaxBloodDecalsDestroyedPerSweep = 8;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat", meta = (ClampMin = "16", ClampMax = "4096"))
    int32 MaxBloodDecalComponentsScannedPerSweep = 128;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot")
    bool bEnableDropSoulOnEnemyDeath = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot")
    bool bLogDropSoulLifecycle = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot")
    TSoftClassPtr<AActor> DropSoulActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Drop_VFX/BP_DropSoul.BP_DropSoul_C")));

    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0.0", ClampMax = "200.0"))
    float DropSoulSpawnZOffset = 12.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float DropSoulDuplicateCheckRadius = 120.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropSoulSpawnChance = 1.0f;

    // 0 이하면 룸별 제한 해제
    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0", ClampMax = "128"))
    int32 DropSoulMaxPerRoom = 10;

    // 0 이하면 월드 전체 활성 개수 제한 해제
    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0", ClampMax = "2048"))
    int32 DropSoulMaxActiveInWorld = 0;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot", meta = (ClampMin = "0.05", ClampMax = "10.0"))
    float DropSoulRepairInterval = 4.00f;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot|Medical")
    bool bSyncBandageHealFromLootDataTable = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot")
    TSoftObjectPtr<UDataTable> LootItemsDataTableOverride = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable")));

    // Legacy compatibility only: replaced by bSyncBandageHealFromLootDataTable.
    UPROPERTY()
    bool bApplyMedicalLootHealingMultiplier = false;
    // Legacy compatibility only: replaced by medical row Param1 in LootItemsDataTable.
    UPROPERTY()
    float MedicalLootHealingMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot|Medical")
    FName MedicalLootRowName = TEXT("Item04");

    UPROPERTY(EditAnywhere, Category = "Raid|Loot|Medical")
    bool bFallbackScanMedicalLootRow = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Loot|Medical")
    TSoftClassPtr<UObject> BandageUseAbilityClass = TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Blueprints/Abilities/ALS_Ability_UseBandage.ALS_Ability_UseBandage_C")));

    UPROPERTY(EditAnywhere, Category = "Raid|Loot|Medical")
    FName BandageHealthRegenPropertyName = TEXT("BandageHealthRegen");

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Concept")
    bool bEnableRoomConceptRules = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Concept")
    TArray<FRaidRoomConceptRule> RoomConceptRules;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Prewarm")
    bool bEnableUpcomingRoomSpawnPrewarm = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Prewarm", meta = (ClampMin = "1", ClampMax = "16"))
    int32 MaxPrewarmedRoomPlans = 3;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Prewarm")
    bool bPrewarmConnectedRoomsOnly = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Prewarm", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxPrewarmSpawnPlansPerRefresh = 4;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave")
    bool bEnableDynamicWaves = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave", meta = (ClampMin = "1", ClampMax = "100"))
    int32 TotalDynamicWaves = 10;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave", meta = (ClampMin = "0.0", ClampMax = "600.0"))
    float FirstWaveDelaySeconds = 45.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave", meta = (ClampMin = "5.0", ClampMax = "600.0"))
    float WaveIntervalSeconds = 70.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave", meta = (ClampMin = "1", ClampMax = "512"))
    int32 MaxAliveWaveEnemies = 64;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|Behavior", meta = (ClampMin = "200.0", ClampMax = "10000.0"))
    float WaveDirectChaseDistance = 1800.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|Behavior", meta = (ClampMin = "0.10", ClampMax = "0.95"))
    float WaveConvergeDistanceScale = 0.58f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|Behavior", meta = (ClampMin = "100.0", ClampMax = "6000.0"))
    float WaveConvergeLateralJitter = 700.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|Behavior", meta = (ClampMin = "200.0", ClampMax = "5000.0"))
    float WaveConvergeMinRingDistance = 450.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|Behavior", meta = (ClampMin = "300.0", ClampMax = "8000.0"))
    float WaveConvergeMaxRingDistance = 2600.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave")
    TArray<FRaidWaveDefinition> WaveDefinitions;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|UI")
    bool bShowWaveStartBanner = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|UI")
    FText WaveStartBannerSubtitle = FText::FromString(TEXT("다수의 적들이 다가옵니다."));

    UPROPERTY(EditAnywhere, Category = "Raid|Wave|UI", meta = (ClampMin = "1.0", ClampMax = "12.0"))
    float WaveStartBannerDuration = 4.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.0", ClampMax = "120.0"))
    float EnemySearchStartDelay = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float EnemySearchImmediateStartDelay = 0.18f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float EnemySearchPatrolInterval = 2.2f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.1", ClampMax = "6.0"))
    float EnemyGunshotRepathInterval = 0.55f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "150.0", ClampMax = "5000.0"))
    float EnemySearchAcceptanceRadius = 120.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "300.0", ClampMax = "20000.0"))
    float EnemyGunshotHearingDistance = 9500.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "300.0", ClampMax = "20000.0"))
    float EnemyAutoChaseDistance = 3200.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.1", ClampMax = "2.0"))
    float EnemySearchPatrolRadiusScale = 0.60f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search")
    bool bEnableRoomEnemySearchBehavior = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search")
    bool bDisableCustomSearchForStateTreeControllers = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search")
    bool bEnableStateTreeReactiveSearchAssist = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.1", ClampMax = "6.0", EditCondition = "bEnableStateTreeReactiveSearchAssist"))
    float StateTreeReactiveSearchRepathInterval = 0.75f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search")
    bool bBridgePlayerGunfireToAISenseHearing = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.05", ClampMax = "3.0", EditCondition = "bBridgePlayerGunfireToAISenseHearing"))
    float PlayerGunfireNoiseReportInterval = 0.45f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.05", ClampMax = "5.0", EditCondition = "bBridgePlayerGunfireToAISenseHearing"))
    float PlayerGunfireNoiseLoudness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float RoomCombatAlertHoldSeconds = 12.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float EnemyControllerSpawnDelayScaleWhenAlerted = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Search", meta = (ClampMin = "300.0", ClampMax = "20000.0"))
    float EnemyControllerSpawnDelayNearPlayerDistance = 2400.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Zombie")
    bool bDisableZombieGrab = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Zombie", meta = (EditCondition = "bDisableZombieGrab"))
    bool bDisableZombieGrabCollisionHelpers = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Combat|Zombie", meta = (EditCondition = "bDisableZombieGrab"))
    bool bLogZombieGrabDisable = false;

    UPROPERTY()
    bool bBandageHealSyncedFromLootDataTable = false;

    // Legacy compatibility only.
    UPROPERTY()
    bool bMedicalLootValuesPatched = false;

    UPROPERTY()
    double NextDropSoulRepairTimeSeconds = 0.0;

    UPROPERTY()
    double NextPlayerGunfireNoiseReportTimeSeconds = 0.0;

    UPROPERTY()
    TMap<int32, int32> DropSoulSpawnCountByRoom;

    TMap<int32, TArray<FRaidEnemySpawnPlanEntry>> PrewarmedSpawnPlansByRoomId;

    UPROPERTY()
    int32 CurrentWaveNumber = 0;

    UPROPERTY()
    int32 AliveWaveEnemyCount = 0;

    UPROPERTY()
    double NextWaveStartTimeSeconds = 0.0;

    UPROPERTY()
    bool bWaveSchedulerInitialized = false;

    UPROPERTY(Transient)
    TSubclassOf<URaidRegionBannerWidget> CachedRegionBannerWidgetClass;

    UPROPERTY(Transient)
    TObjectPtr<URaidRegionBannerWidget> ActiveRegionBannerWidget;

    TArray<FRaidQueuedRegionBanner> PendingRegionBanners;
    FTimerHandle RegionBannerQueueTickHandle;
    FTimerHandle RegionBannerAutoHideTimerHandle;
    double RegionBannerBusyUntilTimeSeconds = 0.0;
    bool bRegionBannerVisibleBySubsystem = false;

    mutable TWeakObjectPtr<APawn> CachedPrimaryPlayerPawn;
    mutable double NextPrimaryPawnRefreshTimeSeconds = 0.0;

    TMap<TWeakObjectPtr<APawn>, double> EnemyTraceCollisionNextRefreshByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemyUndergroundNextCheckByPawn;
    TMap<TWeakObjectPtr<APawn>, int32> EnemyUndergroundRecoveryFailuresByPawn;
    TMap<TWeakObjectPtr<APawn>, FVector> EnemyLastKnownValidLocationByPawn;
    TMap<TWeakObjectPtr<APawn>, FVector> EnemyStuckLastProgressLocationByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemyStuckLastProgressTimeByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemyStuckNextCheckByPawn;
    TMap<TWeakObjectPtr<APawn>, int32> EnemyStuckRecoveryFailuresByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemyTrackedNextPeriodicLogByPawn;
    TMap<TWeakObjectPtr<APawn>, FVector> EnemyTrackedLastObservedLocationByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemySearchActivationTimeByPawn;
    TMap<TWeakObjectPtr<APawn>, double> EnemySearchNextOrderTimeByPawn;
    TMap<int32, double> RoomCombatAlertUntilByRoomId;
    TSet<TWeakObjectPtr<AActor>> ProcessedEnemyDeathActors;
    TSet<FString> ProcessedEnemyDeathKeys;
};
