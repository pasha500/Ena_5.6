#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Raid/LevelNodeRow.h"
#include "Raid/RaidRoomType.h"
#include "Math/RandomStream.h"
#include "Raid/RaidChapterConfig.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "RaidRoomActor.generated.h"

class UBoxComponent;
class UTextRenderComponent;
class URaidChapterConfig;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class URaidRegionBannerWidget;
class UParticleSystem;
class UParticleSystemComponent;
class UWidgetComponent;
class UUserWidget;
class APawn;
class UWorld;

UCLASS()
class T_PROTO_API ARaidRoomActor : public AActor
{
    GENERATED_BODY()

public:
    ARaidRoomActor();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaTime) override;

public:
    void ClearAllMeshInstances();

    void SetNodeData(int32 InNodeId, const FLevelNodeRow& InNodeRow, const URaidChapterConfig* InConfig);
    void InternalSpawnLoot();
    void SetCombatStarted(bool bStarted) { bCombatStarted = bStarted; }
    void SetCombatCleared(bool bCleared);
    void OpenRoom();

    int32 GetNodeId() const { return NodeId; }
    const FLevelNodeRow& GetNodeRow() const { return NodeRow; }
    bool IsCleared() const { return bCombatCleared; }
    bool HasCombatStarted() const { return bCombatStarted; }
    const URaidChapterConfig* GetChapterConfig() const { return ChapterConfigRef; }
    void GenerateRoomLayout();
    FVector GetRoomExtent() const;

    UFUNCTION()
    void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    bool TryShowRegionBanner(APawn* OverlappingPawn);

    // 컴뱃 서브시스템이 접근할 수 있도록 public으로 개방!
    UPROPERTY(EditAnywhere, Category = "Room|Config") float TileSize = 400.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Config") int32 GridSize = 11;

    int32 NeighborNorth = -1, NeighborSouth = -1, NeighborEast = -1, NeighborWest = -1;
    bool bDoorNorth = false, bDoorSouth = false, bDoorEast = false, bDoorWest = false;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleInstanceOnly, Category = "Room|State")
    int32 NodeId = 0;

    UPROPERTY(VisibleInstanceOnly, Category = "Room|State")
    FLevelNodeRow NodeRow;

    UPROPERTY(VisibleInstanceOnly, Category = "Room|State")
    ERaidRoomType CurrentRoomType = ERaidRoomType::Unknown;

    UPROPERTY(Transient)
    FRandomStream RoomRandomStream;

    bool bCombatStarted = false;
    bool bCombatCleared = false;

    UPROPERTY(EditAnywhere, Category = "Room|Geometry") bool bEnableRoomShellGeometry = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry") bool bEnableRoomInteriorGeometry = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry") bool bEnableRoomOrganicClusters = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry") bool bEnableTraversalWhiteboxKit = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry") bool bSpawnWindAnimatedRoomTreesAsActors = true;
    // Editor-only terrain flatten pass for marked mesh variations.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain")
    bool bEnableEditorLandscapeFlattenForMarkedVariations = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0", ClampMax = "2048", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    int32 EditorLandscapeFlattenMaxOpsPerRoom = 96;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "2000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenMinHeightRange = 18.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenMinSlopeDeg = 6.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    bool bAvoidOverlappingEditorLandscapeFlatten = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "1200.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bAvoidOverlappingEditorLandscapeFlatten"))
    float EditorLandscapeFlattenOverlapPadding = 120.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    bool bLogEditorLandscapeFlatten = false;
    // If true, terrain flatten requests from multiple nearby spawned actors are merged into one flatten op.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    bool bUseGroupedEditorLandscapeFlatten = true;
    // Extra radius margin applied to grouped/object flatten area to reduce edge floating.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "2000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenExtraMargin = 120.0f;
    // Nearby flatten candidates within this 2D distance are merged into one group.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "4000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bUseGroupedEditorLandscapeFlatten"))
    float EditorLandscapeFlattenGroupMergeDistance = 260.0f;
    // Minimum grouped member count to ignore overlap-skip and apply one unified flatten.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "2", ClampMax = "64", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bUseGroupedEditorLandscapeFlatten"))
    int32 EditorLandscapeFlattenGroupMinMembersForOverride = 2;
    // Keep smooth transition strong enough to avoid staircase-like boundaries.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenMinFalloffRatio = 0.30f;
    // Guarantees flatten radius is at least actor footprint diagonal * scale.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "1.0", ClampMax = "2.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenFootprintCoverageScale = 1.08f;
    // Edge style mix ratios for flattened border appearance:
    // remaining ratio after cliff+erosion is treated as strong smooth.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "0.8", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenEdgeCliffRatio = 0.16f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "0.9", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenEdgeErosionRatio = 0.42f;
    // World-space patch size used to vary edge style zones.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "120.0", ClampMax = "6000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenEdgePatchSize = 950.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenEdgeErosionStrength = 0.74f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    float EditorLandscapeFlattenEdgeSmoothStrength = 0.60f;
    // Fallback: if room is crowded with flatten-marked actors, flatten one room-wide area once.
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations"))
    bool bEnableRoomWideFlattenFallback = true;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "1", ClampMax = "256", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bEnableRoomWideFlattenFallback"))
    int32 EditorLandscapeRoomWideFlattenMinActors = 12;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bEnableRoomWideFlattenFallback"))
    float EditorLandscapeRoomWideFlattenCoverageThreshold = 0.35f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "10000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bEnableRoomWideFlattenFallback"))
    float EditorLandscapeRoomWideFlattenMargin = 450.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Terrain", meta = (ClampMin = "0.0", ClampMax = "8000.0", EditCondition = "bEnableEditorLandscapeFlattenForMarkedVariations && bEnableRoomWideFlattenFallback"))
    float EditorLandscapeRoomWideFlattenFalloff = 900.0f;
    // Runtime visual consistency: keep semantic whitebox tinting/editor helper materials OFF by default.
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox") bool bUseSemanticWhiteboxColors = false;
    // If enabled, semantic whitebox overrides are also allowed during PIE/Game.
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox", AdvancedDisplay)
    bool bAllowSemanticWhiteboxColorsInGame = false;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0"))
    float ObstacleMinSpacing = 180.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0"))
    float BlueprintObstacleMinSpacing = 320.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0"))
    float DecorationMinSpacing = 220.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0"))
    float BlueprintDecorationMinSpacing = 380.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "1.0", ClampMax = "3.0"))
    float UrbanDecorationSpacingMultiplier = 1.35f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float StaticMeshObstacleWeightScale = 1.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float BlueprintObstacleWeightScale = 0.25f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0", ClampMax = "64"))
    int32 MaxBlueprintObstaclesPerRoom = 0;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "0.1", ClampMax = "5.0"))
    float ObstacleSpawnCountScale = 1.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle", meta = (ClampMin = "1", ClampMax = "64"))
    int32 ObstaclePlacementAttemptMultiplier = 12;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle")
    bool bLogObstacleSpawnSummary = false;
    UPROPERTY(EditAnywhere, Category = "Room|Geometry|Obstacle")
    bool bLogObstacleVariationBreakdown = false;

    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Colors") FLinearColor FloorTint = FLinearColor(0.1f, 0.1f, 0.15f);
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Colors") FLinearColor WallTint = FLinearColor(0.2f, 0.2f, 0.2f);
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Colors") FLinearColor ObstacleTint = FLinearColor(0.25f, 0.3f, 0.35f);
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Colors") FLinearColor DecorationTint = FLinearColor(0.4f, 0.3f, 0.1f);
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Colors") FLinearColor TraversalTint = FLinearColor(0.12f, 0.15f, 0.12f);
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Meshes")
    TSoftObjectPtr<UStaticMesh> TraversalMeshOverride;
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Meshes")
    bool bUseThemeMeshForTraversalKit = true;
    UPROPERTY(EditAnywhere, Category = "Room|Whitebox|Meshes")
    bool bPreserveThemeMeshScaleInTraversalKit = true;

    UPROPERTY(EditAnywhere, Category = "Room|Optimization") bool bAutoOptimizeInstancedMeshes = true;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization|Navigation")
    bool bEnableObstacleNavigationUpdates = false;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization", meta = (ClampMin = "0.01", ClampMax = "1.0")) float RoomTickInterval = 0.08f;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization", meta = (ClampMin = "1000.0", ClampMax = "200000.0")) float StatusTextFacingMaxDistance = 30000.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization") float DetailCullStartDistance = 8000.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization") float DetailCullEndDistance = 15000.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization") bool bAutoEnableNaniteInEditor = true;
    UPROPERTY(EditAnywhere, Category = "Room|Optimization") int32 NaniteTriangleThreshold = 1000;
    UPROPERTY(EditAnywhere, Category = "Room|Combat|ProximitySpawn")
    bool bEnableRoomProximityAutoStart = true;
    UPROPERTY(EditAnywhere, Category = "Room|Combat|ProximitySpawn", meta = (ClampMin = "10.0", ClampMax = "300.0", EditCondition = "bEnableRoomProximityAutoStart"))
    float RoomProximityAutoStartMinDistanceMeters = 80.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Combat|ProximitySpawn", meta = (ClampMin = "10.0", ClampMax = "400.0", EditCondition = "bEnableRoomProximityAutoStart"))
    float RoomProximityAutoStartMaxDistanceMeters = 100.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline") bool bEnableLootProximityOutline = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline", meta = (ClampMin = "50.0", ClampMax = "5000.0", EditCondition = "bEnableLootProximityOutline"))
    float LootOutlineDistance = 900.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline", meta = (ClampMin = "1", ClampMax = "255", EditCondition = "bEnableLootProximityOutline"))
    int32 LootOutlineStencilValue = 1;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline", meta = (EditCondition = "bEnableLootProximityOutline"))
    bool bAutoInstallLootOutlinePostProcess = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline", meta = (EditCondition = "bEnableLootProximityOutline && bAutoInstallLootOutlinePostProcess"))
    TSoftObjectPtr<UMaterialInterface> LootOutlinePostProcessMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Game/Materials/M_PP_Outlines_Instance.M_PP_Outlines_Instance")));
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Outline", meta = (ClampMin = "0.02", ClampMax = "1.0"))
    float LootOutlineUpdateInterval = 0.12f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|DataBinding")
    bool bApplyLootDataTableValuesAtSpawn = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|DataBinding", meta = (EditCondition = "bApplyLootDataTableValuesAtSpawn"))
    bool bApplyLootParamValuesAtSpawn = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|DataBinding", meta = (EditCondition = "bApplyLootDataTableValuesAtSpawn"))
    bool bApplyLootQuantityValuesAtSpawn = false;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|DataBinding", meta = (EditCondition = "bApplyLootDataTableValuesAtSpawn"))
    bool bApplyLootPickupRestrictionValuesAtSpawn = false;
    // Legacy compatibility only: kept to avoid BP load warnings from older assets.
    UPROPERTY()
    bool bApplyMedicalLootHealMultiplierAtSpawn = false;
    // Legacy compatibility only: heal source is now LootItemsDataTable medical row Param1.
    UPROPERTY()
    float MedicalLootHealMultiplierAtSpawn = 1.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator")
    bool bEnableLootProximityFx = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (ClampMin = "50.0", ClampMax = "8000.0", EditCondition = "bEnableLootProximityFx"))
    float LootProximityFxDistance = 1400.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (EditCondition = "bEnableLootProximityFx"))
    TSoftObjectPtr<UParticleSystem> LootProximityFxTemplate = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/TemplesOfCambodia/Demo/EpicContent/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust")));
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (EditCondition = "bEnableLootProximityFx"))
    FVector LootProximityFxOffset = FVector(0.0f, 0.0f, 35.0f);
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (ClampMin = "0.1", ClampMax = "4.0", EditCondition = "bEnableLootProximityFx"))
    float LootProximityFxScale = 0.8f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (EditCondition = "bEnableLootProximityFx"))
    bool bDisableOutlineWhenLootFxIsActive = false;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator")
    bool bLogLootDataBinding = false;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator")
    bool bEnableLootProximityDot = true;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (ClampMin = "50.0", ClampMax = "8000.0", EditCondition = "bEnableLootProximityDot"))
    float LootDotDistance = 1500.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (ClampMin = "50.0", ClampMax = "300.0", EditCondition = "bEnableLootProximityDot"))
    float LootDotHeightOffset = 80.0f;
    UPROPERTY(EditAnywhere, Category = "Room|Loot|Indicator", meta = (EditCondition = "bEnableLootProximityDot"))
    TSoftClassPtr<UUserWidget> LootDotWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_ItemDot.WBP_ItemDot_C")));

    friend class ARaidLayoutManager;

    void MaybeEnableNaniteForMesh(UStaticMesh* Mesh);
    FTransform ResolveVariationTransform(
        const FMeshVariation& Variation,
        const FTransform& BaseTransform,
        ERaidVariationOffsetChannel OffsetChannel = ERaidVariationOffsetChannel::Default);
    void ApplyISMCOptimization(UHierarchicalInstancedStaticMeshComponent* ISMC, int32 MeshType) const;
    void QueueNavigationUpdateForISMC(UHierarchicalInstancedStaticMeshComponent* ISMC);
    void FlushQueuedNavigationUpdates();
    FLinearColor ResolveSemanticTintForType(int32 MeshType) const;
    UMaterialInterface* GetSemanticMaterialForType(int32 MeshType);
    UMaterialInterface* GetTraversalMaterial();
    AActor* SpawnProceduralDoorBlocker(const FModularMeshKit& ThemeKit, const FVector& LocalLocation, float LocalYaw);
    void GenerateTraversalWhiteboxKit(float RoomRadius, const FModularMeshKit* ThemeKit);
    bool IsProximityAutoStartEligibleRoomType() const;
    float ResolveProximityAutoStartDistanceUU() const;
    void EnsureLootOutlinePostProcess(UWorld* World);
    void UpdateLootProximityOutline(const APawn* PlayerPawn);
    void SetLootActorOutline(AActor* LootActor, bool bEnable) const;
    void SetLootActorProximityFx(AActor* LootActor, bool bEnable);
    void SetLootActorDotWidget(AActor* LootActor, bool bEnable);
    void ClearLootProximityFxCache();
    void ClearLootDotWidgetCache();
    UParticleSystem* ResolveLootProximityFxTemplate();
    UClass* ResolveLootDotWidgetClass();
    bool ShouldSuppressLootProximityIndicators(const AActor* LootActor) const;
    void RunBlueprintTerrainStabilizationPass();

    // 누락되었던 AddMeshInstance 함수 선언 추가!
    AActor* AddMeshInstance(const FMeshVariation& Variation, const FTransform& BaseTransform, int32 MeshType, UMaterialInterface* MaterialOverride = nullptr);

    struct FPendingBlueprintTerrainPlacement
    {
        TWeakObjectPtr<AActor> Actor;
        int32 MeshType = 0;
        float BaseLocalZContribution = 0.0f;
        float EffectiveVariationDeltaLocalZ = 0.0f;
        bool bFlattenLandscapeUnderSpawn = false;
        bool bFlattenAppliedInSpawnPass = false;
        bool bGroundHitOnLandscape = false;
        float FlattenRadius = 0.0f;
        float FlattenSmoothFalloff = 0.0f;
        float FlattenHeightOffset = 0.0f;
        bool bFlattenRaiseHeights = true;
        bool bFlattenLowerHeights = true;
    };

    UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> DynamicISMC_Pool;
    UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PendingNavUpdateISMCs;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SpawnedDynamicActors;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SpawnedDoorActors;
    TArray<FPendingBlueprintTerrainPlacement> PendingBlueprintTerrainPlacements;
    TArray<FBox2D> SpawnedObstacleFootprints;
    TArray<FBox2D> AppliedTerrainFlattenFootprints;
    UPROPERTY(Transient) TMap<int32, TObjectPtr<UMaterialInterface>> SemanticMaterialCache;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TraversalMaterialCache;
    UPROPERTY(Transient) TArray<TWeakObjectPtr<AActor>> SpawnedLootActors;
    UPROPERTY(Transient) TObjectPtr<UParticleSystem> CachedLootProximityFxTemplate;
    UPROPERTY(Transient) FSoftObjectPath CachedLootProximityFxTemplatePath;
    UPROPERTY(Transient) bool bLootProximityFxTemplateResolveAttempted = false;
    UPROPERTY(Transient) bool bLootProximityFxTemplateResolveLogged = false;
    TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UParticleSystemComponent>> LootProximityFxComponents;
    UPROPERTY(Transient) TSubclassOf<UUserWidget> CachedLootDotWidgetClass;
    UPROPERTY(Transient) FSoftObjectPath CachedLootDotWidgetClassPath;
    UPROPERTY(Transient) bool bLootDotWidgetResolveAttempted = false;
    UPROPERTY(Transient) bool bLootDotWidgetResolveLogged = false;
    TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UWidgetComponent>> LootDotWidgetComponents;
    UPROPERTY(EditAnywhere, Category = "Room|UI") TSoftClassPtr<URaidRegionBannerWidget> RegionBannerWidgetClass;
    UPROPERTY(Transient) TSubclassOf<URaidRegionBannerWidget> CachedRegionBannerWidgetClass;
    UPROPERTY(Transient) TObjectPtr<URaidRegionBannerWidget> ActiveRegionBannerWidget;

    bool bEntryBannerShown = false;
    bool bPendingBannerRetry = false;
    bool bWasPlayerInsideBannerZone = false;
    int32 EditorLandscapeFlattenOpsApplied = 0;
    double NextBannerAttemptTimeSeconds = 0.0;
    double NextLootOutlineUpdateTimeSeconds = 0.0;
    mutable float CachedProximityAutoStartDistanceUU = -1.0f;
    UPROPERTY(VisibleInstanceOnly, Category = "Room|State")
    bool bNodeDataInitialized = false;
    bool bLootAlreadySpawned = false;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> StatusText;
    FTimerHandle RegionBannerHideTimerHandle;

    UPROPERTY(VisibleInstanceOnly, Category = "Room|State")
    TSoftObjectPtr<URaidChapterConfig> ChapterConfigAsset;
    const URaidChapterConfig* ChapterConfigRef = nullptr;
    const FModularMeshKit* CachedResolvedThemeKit = nullptr;
    FString CachedResolvedThemeKey;
};
