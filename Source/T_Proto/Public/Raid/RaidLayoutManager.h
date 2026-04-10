#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Raid/RaidChapterConfig.h" 
#include "Raid/LevelNodeRow.h"
#include "RaidLayoutManager.generated.h"

class ARaidRoomActor;
class URaidChapterConfig;
class URaidWaveProfile;
class UHierarchicalInstancedStaticMeshComponent;
class USplineComponent;

UENUM(BlueprintType)
enum class ERaidBulkAddTarget : uint8
{
    BackgroundCluster UMETA(DisplayName = "Background Cluster"),
    ThemeFloorVariations UMETA(DisplayName = "Theme Floor Variations"),
    ThemeWallVariations UMETA(DisplayName = "Theme Wall Variations"),
    ThemeObstacleVariations UMETA(DisplayName = "Theme Obstacle Variations"),
    ThemeDecorationVariations UMETA(DisplayName = "Theme Decoration Variations"),
    ThemeDoorBlockerVariations UMETA(DisplayName = "Theme DoorBlocker Variations"),
    ThemeFoliageCluster UMETA(DisplayName = "Theme Foliage Cluster")
};

UCLASS(Blueprintable, BlueprintType, Placeable, ClassGroup = (Raid), meta = (DisplayName = "Raid Layout Manager"))
class T_PROTO_API ARaidLayoutManager : public AActor
{
    GENERATED_BODY()

public:
    ARaidLayoutManager();

protected:
    virtual void BeginPlay() override;

public:
    // =========================================================================
    // 🔥 초직관적 3-STEP 프로세스로 완벽 개편!
    // =========================================================================

    // --- STEP 1: 핵심 데이터 설정 ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Step 1. Config")
    TObjectPtr<URaidChapterConfig> ChapterConfig;

    // --- STEP 2: 배경 스펙 및 테마 셋업 ---
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    float BackgroundRadius = 100000.0f; // 기본 1km 반경

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Raid|Quick Actions")
    void AutoGenerateWhiteboxFromCSV();

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    TArray<FMeshCluster> BackgroundClusters;

    // Editor helper: add selected Content Browser assets in bulk to background/theme mesh slots.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|1 Target")
    ERaidBulkAddTarget BulkAddTarget = ERaidBulkAddTarget::BackgroundCluster;

    // Used when target is a theme array.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|1 Target")
    FString BulkAddThemeKey = TEXT("Jungle");

    // Used for non-foliage theme categories (Floor/Wall/Obstacle/Decoration/DoorBlocker).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|1 Target")
    FString BulkAddVariationIndexName = TEXT("Index_00");

    // Used when target is BackgroundCluster or ThemeFoliageCluster.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|1 Target")
    FString BulkAddClusterName = TEXT("Background_Trees");

    // Default values for newly added entries.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|2 Defaults", meta = (ClampMin = "0.01", ClampMax = "100.0"))
    float BulkAddDefaultSpawnWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|2 Defaults", meta = (ClampMin = "0.01", ClampMax = "10.0"))
    FVector BulkAddDefaultScale = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|2 Defaults")
    FVector BulkAddDefaultLocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|3 Options")
    bool bBulkAddSkipDuplicates = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|3 Options")
    bool bBulkAddCreateMissingTargets = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Bulk Add|3 Options")
    bool bBulkAddIncludeBlueprintActors = true;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Raid|Quick Actions")
    void AddSelectedAssetsToConfiguredTarget();

    // If enabled, background scatter uses this location offset for all variations
    // (instead of each variation's own Offset.Location).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Placement Offset")
    bool bUseGlobalBackgroundLocationOffset = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Placement Offset", meta = (EditCondition = "bUseGlobalBackgroundLocationOffset"))
    FVector GlobalBackgroundLocationOffset = FVector::ZeroVector;

    // true면 ThemeRegistry/Foliage 데이터를 읽어 BackgroundClusters를 자동으로 구성.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bAutoBuildBackgroundClustersFromThemes = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.2", ClampMax = "4.0"))
    float BackgroundAutoDensityScale = 1.0f;

    // ClearAllRooms 시 Theme/Background 설정에 등록된 Blueprint 클래스와 일치하는
    // 레거시 액터를 강제 제거한다(과거 버전 잔존 스폰물 정리용).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bPurgeRegisteredBlueprintActorsOnClear = true;

    // Dense tree profile: make forest feel thicker while preserving FPS by
    // shifting expensive actor trees to cheap instanced trees and concentrating
    // distribution radius around playable space.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees")
    bool bUseDenseTreeFastMode = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "1.0", ClampMax = "8.0", EditCondition = "bUseDenseTreeFastMode"))
    float DenseTreeSpawnScale = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "0.2", ClampMax = "1.0", EditCondition = "bUseDenseTreeFastMode"))
    float DenseTreeScatterRadiusScale = 0.45f;

    // Additional spacing reduction for tree clusters (values < 1.0 make forests denser).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "0.2", ClampMax = "1.0", EditCondition = "bUseDenseTreeFastMode"))
    float DenseTreeMinDistanceScale = 0.45f;

    // Keep total load stable by shifting spawn budget from non-tree clusters to tree clusters.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (EditCondition = "bUseDenseTreeFastMode"))
    bool bPrioritizeTreeClusterDensity = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "0.2", ClampMax = "1.0", EditCondition = "bUseDenseTreeFastMode && bPrioritizeTreeClusterDensity"))
    float NonTreeSpawnScaleWhenTreePriority = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "0", ClampMax = "1000", EditCondition = "bUseDenseTreeFastMode"))
    int32 DenseTreeActorBudget = 24;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "1000.0", ClampMax = "300000.0", EditCondition = "bUseDenseTreeFastMode"))
    float DenseTreeActorRadius = 8000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (ClampMin = "1000.0", ClampMax = "300000.0", EditCondition = "bUseDenseTreeFastMode"))
    float DenseTreeActorShadowRadius = 4500.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Dense Trees", meta = (EditCondition = "bUseDenseTreeFastMode"))
    bool bDenseTreeDisableCollisionOnInstances = true;

    // Global hard budget for background spawn count. Prevents accidental overpopulation
    // from causing severe frame drops/hitches when clusters are dense.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bEnableBackgroundGlobalSpawnBudget = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "1000", ClampMax = "200000", EditCondition = "bEnableBackgroundGlobalSpawnBudget"))
    int32 BackgroundMaxInstancedCount = 36000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0", ClampMax = "5000", EditCondition = "bEnableBackgroundGlobalSpawnBudget"))
    int32 BackgroundMaxActorCount = 96;

    // Fast path for very large open-world scatter:
    // skips expensive per-candidate overlap/footprint checks and relies on distance-based spacing.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Performance")
    bool bUseFastBackgroundScatter = true;

    // Strict overlap gate (footprint + overlap queries). Turning this off is much faster for large counts.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Performance")
    bool bUseStrictBackgroundOverlapChecks = false;

    // If false, background water avoidance uses only physics-volume checks (fast path).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Performance")
    bool bUseDetailedWaterAvoidanceForBackgroundScatter = false;

    // Hard cap for total scatter generation time to prevent multi-minute freezes.
    // 0 means unlimited.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Performance", meta = (ClampMin = "0.0", ClampMax = "300.0"))
    float BackgroundScatterMaxSeconds = 10.0f;

    // Attempts per target count. Lower values are much faster and still stable with fast scatter enabled.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Performance", meta = (ClampMin = "2", ClampMax = "40"))
    int32 BackgroundScatterAttemptMultiplier = 4;

    // Tree meshes that rely on wind WPO can look synchronized when heavily instanced.
    // Enable this to spawn those tree clusters as individual actors for per-object wind phase.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bSpawnWindAnimatedTreesAsActors = true;

    // Wind actor mode can be extremely expensive when thousands of trees are spawned.
    // Trees beyond this budget automatically fall back to instanced rendering.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0", ClampMax = "5000"))
    int32 WindTreeActorMaxCount = 24;

    // Only trees within this radius from manager origin are allowed to spawn as actors.
    // Outside this radius, trees fall back to instanced rendering for performance.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "1000.0", ClampMax = "300000.0"))
    float WindTreeActorSpawnRadius = 8000.0f;

    // Forces unique wind phase data per spawned tree actor (custom primitive data + MID scalar parameters).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bForceUniqueWindPhasePerTree = true;

    // Force-select a safer VSM path for dense non-Nanite open-world meshes.
    // Keeps visual quality high while reducing VSM non-Nanite overflow stalls.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bApplyOpenWorldVsmGuardrail = true;

    // Tree clusters that fall back to instancing can dominate VSM cost.
    // Enable to disable shadow casting on those instanced fallback trees.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bReduceShadowsForInstancedBackgroundTrees = true;

    // No-collision background scatter (small bushes/ground clutter) usually does not need dynamic shadows.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bReduceShadowsForNoCollisionBackground = true;

    // Wind-tree actors outside this radius keep animation but stop casting shadows.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "1000.0", ClampMax = "300000.0"))
    float WindTreeActorShadowRadius = 4500.0f;

    // =========================================================================================
    // Background ISMC Distance Bands (Near/Mid/Far) - easy distance-based optimization controls
    // =========================================================================================
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)")
    bool bEnableBackgroundISMCDistanceBands = true;

    // Near band range: 0 ~ NearMaxDistance
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)", meta = (ClampMin = "1000.0", ClampMax = "300000.0", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    float BackgroundISMCNearMaxDistance = 18000.0f;

    // Mid band range: NearMaxDistance ~ MidMaxDistance
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)", meta = (ClampMin = "2000.0", ClampMax = "500000.0", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    float BackgroundISMCMidMaxDistance = 45000.0f;

    // Per-band ISMC cull and shadow controls
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Near", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCNearCullStart = 0;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Near", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCNearCullEnd = 26000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Near", meta = (EditCondition = "bEnableBackgroundISMCDistanceBands"))
    bool bBackgroundISMCNearCastShadow = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Mid", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCMidCullStart = 9000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Mid", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCMidCullEnd = 52000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Mid", meta = (EditCondition = "bEnableBackgroundISMCDistanceBands"))
    bool bBackgroundISMCMidCastShadow = false;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Far", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCFarCullStart = 28000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Far", meta = (ClampMin = "0", ClampMax = "1000000", EditCondition = "bEnableBackgroundISMCDistanceBands"))
    int32 BackgroundISMCFarCullEnd = 120000;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Far", meta = (EditCondition = "bEnableBackgroundISMCDistanceBands"))
    bool bBackgroundISMCFarCastShadow = false;

    // When true, pressing a preset button instantly rebuilds background scatter with the new settings.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Background LOD (ISMC)|Presets")
    bool bAutoRebuildBackgroundOnPresetApply = true;

    UFUNCTION(BlueprintCallable, Category = "Raid|Step 2. Background LOD (ISMC)|Presets")
    void ApplyBackgroundLODQualityPreset();

    UFUNCTION(BlueprintCallable, Category = "Raid|Step 2. Background LOD (ISMC)|Presets")
    void ApplyBackgroundLODBalancedPreset();

    UFUNCTION(BlueprintCallable, Category = "Raid|Step 2. Background LOD (ISMC)|Presets")
    void ApplyBackgroundLODPerformancePreset();

    // When true, Quality/Balanced/Performance buttons also update room-object LOD settings.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Presets")
    bool bLinkRoomLODToBackgroundPresets = true;

    // When true, linked room preset values are reapplied to already spawned rooms immediately.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Presets")
    bool bAutoApplyRoomOptimizationOnPresetApply = true;

    // Global room ISMC optimization values used for new room spawns.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)")
    bool bAutoOptimizeRoomInstancedMeshes = true;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)", meta = (ClampMin = "0.0", ClampMax = "200000.0"))
    float RoomDetailCullStartDistance = 8000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)", meta = (ClampMin = "0.0", ClampMax = "250000.0"))
    float RoomDetailCullEndDistance = 15000.0f;

    // Preset value templates (editable): pressing preset buttons copies these values into room settings.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Quality", meta = (ClampMin = "0.0", ClampMax = "200000.0"))
    float RoomDetailCullStart_Quality = 12000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Quality", meta = (ClampMin = "0.0", ClampMax = "250000.0"))
    float RoomDetailCullEnd_Quality = 26000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Balanced", meta = (ClampMin = "0.0", ClampMax = "200000.0"))
    float RoomDetailCullStart_Balanced = 8000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Balanced", meta = (ClampMin = "0.0", ClampMax = "250000.0"))
    float RoomDetailCullEnd_Balanced = 15000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Performance", meta = (ClampMin = "0.0", ClampMax = "200000.0"))
    float RoomDetailCullStart_Performance = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Room LOD (Spawned Rooms)|Preset Values|Performance", meta = (ClampMin = "0.0", ClampMax = "250000.0"))
    float RoomDetailCullEnd_Performance = 10000.0f;

    // 수역 근처 룸/배경 스폰 차단 반경(uu)
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "100.0"))
    float WaterAvoidanceRadius = 2200.0f;

    // 물과 맞닿는 해안선(shore)에서도 약간 더 떨어져 스폰되도록 추가 여유 거리(uu).
    // 0이면 기존 WaterAvoidanceRadius만 사용.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.0", ClampMax = "12000.0"))
    float ShorelineSpawnBufferDistance = 1200.0f;

    // true면 Water PhysicsVolume 판정을 직접 사용한다.
    // Ocean 볼륨이 맵 전역을 덮는 경우 false로 두고 표면 트레이스 기반 판정을 권장.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bUseWaterVolumeForSpawnAvoidance = false;

    // 해안선 버퍼 판정에 사용하는 주변 샘플 수(최소 4).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "4", ClampMax = "32"))
    int32 ShorelineProbeSampleCount = 8;

    // true면 물가 여유 거리 판정 시 WaterVolume 포함 판정을 함께 사용한다.
    // 거대한 WaterVolume이 맵 전체를 감싸는 경우 false를 권장.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bUseWaterVolumeInShorelineProbe = false;

    // 랜드스케이프 스플라인 기반 길(road/path) 근처 스폰 차단.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bAvoidLandscapeSplineRoads = true;

    // 길 회피 반경(uu). 룸은 footprint와 합쳐서, 배경은 포인트 기준으로 적용된다.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "50.0", ClampMax = "8000.0", EditCondition = "bAvoidLandscapeSplineRoads"))
    float LandscapeSplineRoadAvoidanceRadius = 850.0f;

    // true면 WaterBody의 spline 경계를 직접 사용해 스폰 가능 영역을 제한한다.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bUseWaterBodySplineBoundaryRules = true;

    // WaterBody spline 경계선 근처 추가 완충 거리(uu).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.0", ClampMax = "10000.0", EditCondition = "bUseWaterBodySplineBoundaryRules"))
    float WaterBodySplineEdgeBufferDistance = 600.0f;

    // Ocean spline 내부 스폰에서 추가로 더 밀어낼 거리(uu).
    // 최종 Ocean 버퍼 = WaterBodySplineEdgeBufferDistance + WaterBodyOceanExtraBufferDistance.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.0", ClampMax = "20000.0", EditCondition = "bUseWaterBodySplineBoundaryRules"))
    float WaterBodyOceanExtraBufferDistance = 1200.0f;

    // Lake spline 외부 스폰에서 추가로 더 밀어낼 거리(uu).
    // 최종 Lake 버퍼 = WaterBodySplineEdgeBufferDistance + WaterBodyLakeExtraBufferDistance.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.0", ClampMax = "20000.0", EditCondition = "bUseWaterBodySplineBoundaryRules"))
    float WaterBodyLakeExtraBufferDistance = 0.0f;

    // 이미 맵(랜드스케이프 포함)에 배치된 스태틱/스플라인 메쉬와 겹치는 스폰을 차단.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bAvoidPreplacedStaticMeshGeometry = true;

    // 기존 배치 오브젝트와의 추가 완충 거리(uu).
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "0.0", ClampMax = "5000.0", EditCondition = "bAvoidPreplacedStaticMeshGeometry"))
    float PreplacedStaticMeshBufferDistance = 180.0f;

    // true면 지면 탐색 시 Landscape/Terrain 히트를 우선 선택.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bPreferLandscapeGroundHit = true;

    // 배경 산포 지면 스냅 보정: 중심점 외 주변 샘플을 같이 검사해
    // 굴곡 지형에서의 공중 부양/매립 편차를 줄인다.
    // (트리/노콜리전 클러스터는 기본적으로 단일 샘플 유지)
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup|Ground Snap")
    bool bUseAdaptiveGroundMultiSampleForBackgroundScatter = true;

    // true면 CSV 좌표 분포의 중심을 LayoutManager 좌표 원점으로 재정렬해 한쪽 치우침을 방지.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup")
    bool bCenterRoomLayoutAroundManager = true;

    // 재정렬된 룸 레이아웃의 최대 반경(uu). 초과 시 전체를 자동 축소해 랜드스케이프 이탈 스폰을 줄임.
    UPROPERTY(EditAnywhere, Category = "Raid|Step 2. Environment Setup", meta = (ClampMin = "5000.0", ClampMax = "500000.0"))
    float RoomLayoutMaxRadius = 85000.0f;

    // --- STEP 3-0: Combat runtime setup ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Step 3. Combat Setup|Waves")
    bool bEnableDynamicWavesFromLayoutManager = true;

    // If set, this overrides ChapterConfig.Registry.WaveProfile.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Step 3. Combat Setup|Waves")
    TObjectPtr<URaidWaveProfile> WaveProfileOverride;

    // If false, BeginPlay will keep prebuilt rooms/background in the level
    // and only register them to combat systems instead of regenerating layout.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Step 3. Actions")
    bool bAutoSpawnLayoutOnBeginPlay = false;

    // --- STEP 3: 스폰 및 클리어 ---
    UFUNCTION(BlueprintCallable, Category = "Raid|Step 3. Actions")
    void OneClickCsvImportBuild(); // 🔥 원클릭 빌드 부활!

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Raid|Quick Actions")
    void SpawnRaidLayout();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Raid|Quick Actions")
    void ClearAllRooms();

    // =========================================================================

    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Raid|State")
    TMap<int32, TObjectPtr<ARaidRoomActor>> SpawnedRooms;

    UPROPERTY(VisibleInstanceOnly, Transient, Category = "Raid|State")
    TArray<TObjectPtr<AActor>> SpawnedRoadActors;

public:
    void AutoSetupPrototypeRaid();
    void AutoFinalizeImportedData();
    void RunFullContentAuditAndRepair();
    bool ApplyOpenWorldSpecFromCsvPath(const FString& CsvPath);

private:
    void HandleBackgroundPresetApplied(const TCHAR* PresetName);
    static int32 ResolveMeshTypeFromComponentTags(const TArray<FName>& ComponentTags);
    int32 ApplyRoomOptimizationToRoom(ARaidRoomActor* Room, bool bReapplyExistingInstances);
    void ApplyRoomOptimizationToSpawnedRooms(const TCHAR* PresetName);
    void ConnectRoomDoors();
    void ScatterBackgroundScenery();
    void ClearBackgroundScenery();
    void GenerateRoadSplineNetwork(const FString& DominantEnv);
    void ApplyProceduralLandscapeDeformation(const FString& DominantEnv);
    void EnsureBackgroundClustersInitialized();

    UPROPERTY(Transient)
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> BackgroundISMC_Pool;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AActor>> SpawnedBackgroundActors;

    FString LastOpenWorldSpecDirectory;
};
