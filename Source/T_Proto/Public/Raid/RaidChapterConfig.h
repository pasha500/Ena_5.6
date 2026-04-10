#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h" 
#include "Math/RandomStream.h"
#include "Raid/LevelNodeRow.h"
#include "RaidChapterConfig.generated.h"

class ARaidRoomActor;
class URaidEnemyPresetRegistry;
class URaidLootRegistry;
class URoomPrefabRegistry;
class URaidWaveProfile;

UENUM(BlueprintType)
enum class ERaidConfigBulkAddTarget : uint8
{
    FloorVariations UMETA(DisplayName = "Floor Variations"),
    WallVariations UMETA(DisplayName = "Wall Variations"),
    ObstacleVariations UMETA(DisplayName = "Obstacle Variations"),
    DecorationVariations UMETA(DisplayName = "Decoration Variations"),
    DoorBlockerVariations UMETA(DisplayName = "DoorBlocker Variations"),
    FoliageClusterVariations UMETA(DisplayName = "Foliage Cluster Variations")
};

UENUM(BlueprintType)
enum class ERaidVariationOffsetChannel : uint8
{
    Default UMETA(DisplayName = "Default"),
    Floor UMETA(DisplayName = "Floor Variations"),
    Wall UMETA(DisplayName = "Wall Variations"),
    Doorway UMETA(DisplayName = "Doorway Variations"),
    DoorBlocker UMETA(DisplayName = "DoorBlocker Variations"),
    Obstacle UMETA(DisplayName = "Obstacle Variations"),
    Decoration UMETA(DisplayName = "Decoration Variations"),
    Foliage UMETA(DisplayName = "Foliage Clusters")
};

USTRUCT(BlueprintType)
struct T_PROTO_API FMeshVariation {
    GENERATED_BODY()

    // [최적화 적용] Hard Reference -> Soft Reference 교체 완료
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Asset")
    TSoftObjectPtr<UStaticMesh> Mesh;

    // [최적화 적용] TSubclassOf -> TSoftClassPtr 교체 완료
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Asset")
    TSoftClassPtr<AActor> BlueprintPrefab;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Spawn")
    float SpawnWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Transform")
    FTransform Offset = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random")
    bool bUseRandomScale = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random", meta = (EditCondition = "bUseRandomScale"))
    float RandomScaleMin = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random", meta = (EditCondition = "bUseRandomScale"))
    float RandomScaleMax = 1.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random")
    bool bUseRandomRotation = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random", meta = (EditCondition = "bUseRandomRotation"))
    float RandomRotationYawMin = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random", meta = (EditCondition = "bUseRandomRotation"))
    float RandomRotationYawMax = 360.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random")
    bool bUseRandomLocationJitter = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation|Random", meta = (EditCondition = "bUseRandomLocationJitter"))
    float JitterRadius = 50.0f;

    FTransform GetRandomizedTransform(const FTransform& BaseTransform, const FRandomStream& Stream) const
    {
        FTransform FinalTrans = Offset * BaseTransform;
        if (bUseRandomLocationJitter) {
            FVector Jitter(Stream.FRandRange(-JitterRadius, JitterRadius), Stream.FRandRange(-JitterRadius, JitterRadius), 0.0f);
            FinalTrans.AddToTranslation(Jitter);
        }
        if (bUseRandomRotation) {
            FRotator RandRot(0.0f, Stream.FRandRange(RandomRotationYawMin, RandomRotationYawMax), 0.0f);
            FinalTrans.ConcatenateRotation(RandRot.Quaternion());
        }
        if (bUseRandomScale) {
            float RandScale = Stream.FRandRange(RandomScaleMin, RandomScaleMax);
            FinalTrans.SetScale3D(FinalTrans.GetScale3D() * RandScale);
        }
        return FinalTrans;
    }
};

USTRUCT(BlueprintType)
struct T_PROTO_API FMeshVariationIndexGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Index")
    FString IndexName = TEXT("Index_00");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Index|Transform")
    FVector IndexLocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Index|Items")
    TArray<FMeshVariation> Variations;
};

namespace RaidMeshUtils
{
    inline const FMeshVariation* PickRandomVariation(const TArray<FMeshVariation>& Variations, const FRandomStream& Stream)
    {
        if (Variations.Num() == 0) return nullptr;
        float TotalWeight = 0.0f;
        for (const auto& Var : Variations) TotalWeight += FMath::Max(0.0f, Var.SpawnWeight);
        if (TotalWeight <= 0.0f) return &Variations[Stream.RandRange(0, Variations.Num() - 1)];
        float RandVal = Stream.FRandRange(0.0f, TotalWeight);
        for (const auto& Var : Variations)
        {
            RandVal -= FMath::Max(0.0f, Var.SpawnWeight);
            if (RandVal <= 0.0f) return &Var;
        }
        return &Variations.Last();
    }
}

USTRUCT(BlueprintType)
struct T_PROTO_API FMeshCluster {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
    FString ClusterName = TEXT("New Cluster Set");

    // Cluster-level index offset (applies to all variations in this cluster).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Transform")
    FVector ClusterLocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Spawning")
    bool bUseAreaSpawning = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Spawning", meta = (EditCondition = "bUseAreaSpawning"))
    float SpawnCountMin = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Spawning", meta = (EditCondition = "bUseAreaSpawning"))
    float SpawnCountMax = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Spawning", meta = (EditCondition = "bUseAreaSpawning"))
    float SpawnRadius = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Spawning")
    float MinDistanceBetweenInstances = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides")
    bool bOverrideRandomization = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    bool bUseRandomScale = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    float RandomScaleMin = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    float RandomScaleMax = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    bool bUseRandomRotation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    bool bUseRandomLocationJitter = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Overrides", meta = (EditCondition = "bOverrideRandomization"))
    float JitterRadius = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster|Items")
    TArray<FMeshVariation> Variations;

    int32 CalculateSpawnCount(float TotalAreaRadius, const FRandomStream& Stream) const
    {
        if (!bUseAreaSpawning) return 1;
        float SafeSpawnRadius = FMath::Max(100.0f, SpawnRadius);
        float AreaRatio = (TotalAreaRadius * TotalAreaRadius) / (SafeSpawnRadius * SafeSpawnRadius);
        float RandomDensity = Stream.FRandRange(SpawnCountMin, SpawnCountMax);
        float TotalSpawnsF = RandomDensity * AreaRatio;
        int32 Count = FMath::FloorToInt(TotalSpawnsF);
        if (Stream.FRand() < (TotalSpawnsF - Count)) Count++;
        return Count;
    }

    FTransform GetClusterRandomizedTransform(const FMeshVariation& Var, const FTransform& BaseTransform, const FRandomStream& Stream) const
    {
        FTransform FinalTrans = Var.Offset * BaseTransform;
        FinalTrans.AddToTranslation(ClusterLocationOffset);
        bool bJitter = bOverrideRandomization ? bUseRandomLocationJitter : Var.bUseRandomLocationJitter;
        float JRad = bOverrideRandomization ? JitterRadius : Var.JitterRadius;
        if (bJitter) {
            FVector Jitter(Stream.FRandRange(-JRad, JRad), Stream.FRandRange(-JRad, JRad), 0.0f);
            FinalTrans.AddToTranslation(Jitter);
        }
        bool bRot = bOverrideRandomization ? bUseRandomRotation : Var.bUseRandomRotation;
        if (bRot) {
            FRotator RandRot(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);
            FinalTrans.ConcatenateRotation(RandRot.Quaternion());
        }
        bool bScale = bOverrideRandomization ? bUseRandomScale : Var.bUseRandomScale;
        float SMin = bOverrideRandomization ? RandomScaleMin : Var.RandomScaleMin;
        float SMax = bOverrideRandomization ? RandomScaleMax : Var.RandomScaleMax;
        if (bScale) {
            float RandScale = Stream.FRandRange(SMin, SMax);
            FinalTrans.SetScale3D(FinalTrans.GetScale3D() * RandScale);
        }
        return FinalTrans;
    }
};

USTRUCT(BlueprintType)
struct T_PROTO_API FModularMeshKit {
    GENERATED_BODY()

    // Legacy flat arrays (hidden). Kept only for backward-compatible load/migration.
    UPROPERTY()
    TArray<FMeshVariation> FloorVariations;
    UPROPERTY()
    TArray<FMeshVariation> WallVariations;
    UPROPERTY()
    TArray<FMeshVariation> CornerVariations;
    UPROPERTY()
    TArray<FMeshVariation> DoorwayVariations;

    UPROPERTY()
    TArray<FMeshVariation> DoorBlockerVariations;

    UPROPERTY()
    TArray<FMeshVariation> ObstacleVariations;
    UPROPERTY()
    TArray<FMeshVariation> DecorationVariations;

    // Preferred grouped/indexed arrays (index-level offsets).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Base|Indices")
    TArray<FMeshVariationIndexGroup> FloorVariationIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Base|Indices")
    TArray<FMeshVariationIndexGroup> WallVariationIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Base|Indices")
    TArray<FMeshVariationIndexGroup> DoorwayVariationIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Base|Indices")
    TArray<FMeshVariationIndexGroup> DoorBlockerVariationIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Props|Indices")
    TArray<FMeshVariationIndexGroup> ObstacleVariationIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Props|Indices")
    TArray<FMeshVariationIndexGroup> DecorationVariationIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Nature")
    bool bIsOrganicTheme = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Nature")
    TArray<FMeshCluster> FoliageClusters;

    // Deprecated: category-wide offsets are hidden; index-group offsets are preferred.
    UPROPERTY()
    bool bUseCategoryLocationOffsets = false;

    UPROPERTY()
    FVector FloorCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector WallCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DoorwayCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DoorBlockerCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector ObstacleCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DecorationCategoryLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector FoliageCategoryLocationOffset = FVector::ZeroVector;

    // Theme matching keywords (room NodeTags/RoomRole/RoomType/EnvType와 매칭)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match")
    TArray<FString> ThemeTags;

    // Combat concept defaults for rooms resolved to this theme.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match|Combat")
    FName EnemyPresetOverride = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match|Combat")
    FString BotProfileOverride = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match|Combat", meta = (ClampMin = "0", ClampMax = "64"))
    int32 SpawnCountOverride = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match|Combat", meta = (ClampMin = "0.10", ClampMax = "6.0"))
    float DifficultyMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modular|Theme Match|Combat", meta = (ClampMin = "0.10", ClampMax = "6.0"))
    float CombatWeightMultiplier = 1.0f;

    const TArray<FMeshVariation>& ResolveLegacyVariations(ERaidVariationOffsetChannel Channel) const
    {
        static const TArray<FMeshVariation> Empty;
        switch (Channel)
        {
        case ERaidVariationOffsetChannel::Floor:
            return FloorVariations;
        case ERaidVariationOffsetChannel::Wall:
            return WallVariations;
        case ERaidVariationOffsetChannel::Doorway:
            return DoorwayVariations;
        case ERaidVariationOffsetChannel::DoorBlocker:
            return DoorBlockerVariations;
        case ERaidVariationOffsetChannel::Obstacle:
            return ObstacleVariations;
        case ERaidVariationOffsetChannel::Decoration:
            return DecorationVariations;
        case ERaidVariationOffsetChannel::Default:
        case ERaidVariationOffsetChannel::Foliage:
        default:
            return Empty;
        }
    }

    const TArray<FMeshVariationIndexGroup>& ResolveVariationIndices(ERaidVariationOffsetChannel Channel) const
    {
        static const TArray<FMeshVariationIndexGroup> Empty;
        switch (Channel)
        {
        case ERaidVariationOffsetChannel::Floor:
            return FloorVariationIndices;
        case ERaidVariationOffsetChannel::Wall:
            return WallVariationIndices;
        case ERaidVariationOffsetChannel::Doorway:
            return DoorwayVariationIndices;
        case ERaidVariationOffsetChannel::DoorBlocker:
            return DoorBlockerVariationIndices;
        case ERaidVariationOffsetChannel::Obstacle:
            return ObstacleVariationIndices;
        case ERaidVariationOffsetChannel::Decoration:
            return DecorationVariationIndices;
        case ERaidVariationOffsetChannel::Default:
        case ERaidVariationOffsetChannel::Foliage:
        default:
            return Empty;
        }
    }

    void GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel Channel, TArray<FMeshVariation>& OutVariations) const
    {
        OutVariations.Reset();
        const TArray<FMeshVariationIndexGroup>& IndexGroups = ResolveVariationIndices(Channel);
        bool bUsedIndexGroups = false;
        for (const FMeshVariationIndexGroup& Group : IndexGroups)
        {
            if (Group.Variations.Num() <= 0)
            {
                continue;
            }

            bUsedIndexGroups = true;
            for (const FMeshVariation& SourceVariation : Group.Variations)
            {
                FMeshVariation EffectiveVariation = SourceVariation;
                FTransform EffectiveOffset = EffectiveVariation.Offset;
                EffectiveOffset.AddToTranslation(Group.IndexLocationOffset);
                EffectiveVariation.Offset = EffectiveOffset;
                OutVariations.Add(EffectiveVariation);
            }
        }

        if (!bUsedIndexGroups)
        {
            OutVariations.Append(ResolveLegacyVariations(Channel));
        }
    }

    void GetAllRawVariationsForChannel(ERaidVariationOffsetChannel Channel, TArray<FMeshVariation>& OutVariations) const
    {
        OutVariations.Reset();
        OutVariations.Append(ResolveLegacyVariations(Channel));

        const TArray<FMeshVariationIndexGroup>& IndexGroups = ResolveVariationIndices(Channel);
        for (const FMeshVariationIndexGroup& Group : IndexGroups)
        {
            OutVariations.Append(Group.Variations);
        }
    }

    bool HasAnyVariationsForChannel(ERaidVariationOffsetChannel Channel) const
    {
        TArray<FMeshVariation> EffectiveVariations;
        GetEffectiveVariationsForChannel(Channel, EffectiveVariations);
        return EffectiveVariations.Num() > 0;
    }

    bool MigrateLegacyVariationArraysToIndexGroups()
    {
        bool bChanged = false;

        auto MigrateCategory = [&](TArray<FMeshVariation>& LegacyVariations, TArray<FMeshVariationIndexGroup>& IndexGroups, const FString& DefaultGroupName)
            {
                if (LegacyVariations.Num() <= 0)
                {
                    return;
                }

                auto IsVariationEmpty = [](const FMeshVariation& Variation) -> bool
                    {
                        return Variation.Mesh.IsNull() && Variation.BlueprintPrefab.IsNull();
                    };

                FMeshVariationIndexGroup* TargetGroup = nullptr;
                if (IndexGroups.Num() <= 0)
                {
                    FMeshVariationIndexGroup NewGroup;
                    NewGroup.IndexName = DefaultGroupName;
                    const int32 NewIndex = IndexGroups.Add(NewGroup);
                    TargetGroup = IndexGroups.IsValidIndex(NewIndex) ? &IndexGroups[NewIndex] : nullptr;
                    bChanged = true;
                }
                else
                {
                    TargetGroup = &IndexGroups[0];
                    if (TargetGroup && TargetGroup->IndexName.IsEmpty())
                    {
                        TargetGroup->IndexName = DefaultGroupName;
                        bChanged = true;
                    }
                }

                if (!TargetGroup)
                {
                    return;
                }

                TSet<FSoftObjectPath> ExistingMeshPaths;
                TSet<FSoftObjectPath> ExistingClassPaths;
                for (const FMeshVariationIndexGroup& ExistingGroup : IndexGroups)
                {
                    for (const FMeshVariation& ExistingVariation : ExistingGroup.Variations)
                    {
                        const FSoftObjectPath MeshPath = ExistingVariation.Mesh.ToSoftObjectPath();
                        if (!MeshPath.IsNull())
                        {
                            ExistingMeshPaths.Add(MeshPath);
                        }
                        const FSoftObjectPath ClassPath = ExistingVariation.BlueprintPrefab.ToSoftObjectPath();
                        if (!ClassPath.IsNull())
                        {
                            ExistingClassPaths.Add(ClassPath);
                        }
                    }
                }

                int32 AddedCount = 0;
                for (const FMeshVariation& LegacyVariation : LegacyVariations)
                {
                    if (IsVariationEmpty(LegacyVariation))
                    {
                        continue;
                    }

                    const FSoftObjectPath MeshPath = LegacyVariation.Mesh.ToSoftObjectPath();
                    const FSoftObjectPath ClassPath = LegacyVariation.BlueprintPrefab.ToSoftObjectPath();
                    if ((!MeshPath.IsNull() && ExistingMeshPaths.Contains(MeshPath)) ||
                        (!ClassPath.IsNull() && ExistingClassPaths.Contains(ClassPath)))
                    {
                        continue;
                    }

                    TargetGroup->Variations.Add(LegacyVariation);
                    if (!MeshPath.IsNull())
                    {
                        ExistingMeshPaths.Add(MeshPath);
                    }
                    if (!ClassPath.IsNull())
                    {
                        ExistingClassPaths.Add(ClassPath);
                    }
                    ++AddedCount;
                }

                if (AddedCount > 0)
                {
                    bChanged = true;
                }

                LegacyVariations.Reset();
                bChanged = true;
            };

        MigrateCategory(FloorVariations, FloorVariationIndices, TEXT("Floor_00"));
        MigrateCategory(WallVariations, WallVariationIndices, TEXT("Wall_00"));
        MigrateCategory(DoorwayVariations, DoorwayVariationIndices, TEXT("Doorway_00"));
        MigrateCategory(DoorBlockerVariations, DoorBlockerVariationIndices, TEXT("DoorBlocker_00"));
        MigrateCategory(ObstacleVariations, ObstacleVariationIndices, TEXT("Obstacle_00"));
        MigrateCategory(DecorationVariations, DecorationVariationIndices, TEXT("Decoration_00"));

        return bChanged;
    }

    FVector ResolveCategoryLocationOffset(ERaidVariationOffsetChannel Channel) const
    {
        if (!bUseCategoryLocationOffsets)
        {
            return FVector::ZeroVector;
        }

        switch (Channel)
        {
        case ERaidVariationOffsetChannel::Floor:
            return FloorCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Wall:
            return WallCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Doorway:
            return DoorwayCategoryLocationOffset;
        case ERaidVariationOffsetChannel::DoorBlocker:
            return DoorBlockerCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Obstacle:
            return ObstacleCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Decoration:
            return DecorationCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Foliage:
            return FoliageCategoryLocationOffset;
        case ERaidVariationOffsetChannel::Default:
        default:
            return FVector::ZeroVector;
        }
    }
};

UCLASS(BlueprintType)
class T_PROTO_API URaidChapterConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    virtual void PostLoad() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout Config")
    TObjectPtr<UDataTable> LevelDataTable;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout Config")
    TSubclassOf<ARaidRoomActor> RoomClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout Config|Themes")
    TMap<FString, FModularMeshKit> ThemeRegistry;

    // Legacy fields kept for backward-compatible asset load only (hidden in details).
    UPROPERTY()
    bool bUseGlobalVariationLocationOffset = false;

    UPROPERTY()
    FVector GlobalVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    bool bUseCategoryVariationLocationOffsets = false;

    UPROPERTY()
    FVector FloorVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector WallVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DoorwayVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DoorBlockerVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector ObstacleVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector DecorationVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY()
    FVector FoliageVariationLocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Registry")
    TObjectPtr<URoomPrefabRegistry> PrefabRegistry;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Registry")
    TObjectPtr<URaidEnemyPresetRegistry> EnemyPresetRegistry;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Registry")
    TObjectPtr<URaidLootRegistry> LootRegistry;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Registry")
    TObjectPtr<URaidWaveProfile> WaveProfile;

    bool ResolveThemeKitByKey(const FString& InRequestedThemeKey, FString& OutResolvedThemeKey, const FModularMeshKit*& OutThemeKit) const;
    bool ResolveThemeKitForNode(const FLevelNodeRow& NodeRow, FString& OutResolvedThemeKey, const FModularMeshKit*& OutThemeKit) const;

    // ==========================
    // Editor Bulk Add (Config)
    // ==========================
    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|1 Target")
    ERaidConfigBulkAddTarget BulkAddTarget = ERaidConfigBulkAddTarget::ObstacleVariations;

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|1 Target")
    FString BulkAddThemeKey = TEXT("Jungle");

    // Used for non-foliage category targets (Floor/Wall/Obstacle/Decoration/DoorBlocker).
    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|1 Target")
    FString BulkAddVariationIndexName = TEXT("Index_00");

    // Only used for FoliageClusterVariations target.
    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|1 Target")
    FString BulkAddFoliageClusterName = TEXT("Foliage_Trees");

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|2 Defaults", meta = (ClampMin = "0.01", ClampMax = "100.0"))
    float BulkAddDefaultSpawnWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|2 Defaults", meta = (ClampMin = "0.01", ClampMax = "10.0"))
    FVector BulkAddDefaultScale = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|2 Defaults")
    FVector BulkAddDefaultLocationOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|3 Options")
    bool bBulkAddSkipDuplicates = true;

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|3 Options")
    bool bBulkAddCreateMissingThemeOrCluster = true;

    UPROPERTY(EditAnywhere, Category = "Editor|Bulk Add|3 Options")
    bool bBulkAddIncludeBlueprintActors = true;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Editor|Bulk Add")
    void AddSelectedAssetsToConfiguredTarget();
};

