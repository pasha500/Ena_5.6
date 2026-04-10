#include "Raid/RaidChapterConfig.h"

#include "Algo/Sort.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#endif

namespace
{
    void AppendNormalizedTokens(const FString& Source, TArray<FString>& OutTokens)
    {
        TArray<FString> SplitByComma;
        Source.ParseIntoArray(SplitByComma, TEXT(","), true);
        if (SplitByComma.Num() == 0)
        {
            SplitByComma.Add(Source);
        }

        for (const FString& RawToken : SplitByComma)
        {
            TArray<FString> SplitBySpace;
            RawToken.ParseIntoArrayWS(SplitBySpace);
            if (SplitBySpace.Num() == 0)
            {
                SplitBySpace.Add(RawToken);
            }

            for (FString Token : SplitBySpace)
            {
                Token = Token.TrimStartAndEnd().ToLower();
                if (!Token.IsEmpty())
                {
                    OutTokens.AddUnique(Token);
                }
            }
        }
    }
}

void URaidChapterConfig::PostLoad()
{
    Super::PostLoad();

    bool bMigratedAnyTheme = false;
    for (TPair<FString, FModularMeshKit>& Pair : ThemeRegistry)
    {
        if (Pair.Value.MigrateLegacyVariationArraysToIndexGroups())
        {
            bMigratedAnyTheme = true;
        }
    }

#if WITH_EDITOR
    if (bMigratedAnyTheme)
    {
        MarkPackageDirty();
    }
#endif
}

bool URaidChapterConfig::ResolveThemeKitByKey(const FString& InRequestedThemeKey, FString& OutResolvedThemeKey, const FModularMeshKit*& OutThemeKit) const
{
    OutResolvedThemeKey.Reset();
    OutThemeKit = nullptr;

    if (ThemeRegistry.Num() <= 0)
    {
        return false;
    }

    const FString RequestedKey = InRequestedThemeKey.TrimStartAndEnd();
    if (RequestedKey.IsEmpty())
    {
        return false;
    }

    if (const FModularMeshKit* Exact = ThemeRegistry.Find(RequestedKey))
    {
        OutResolvedThemeKey = RequestedKey;
        OutThemeKit = Exact;
        return true;
    }

    for (const TPair<FString, FModularMeshKit>& Pair : ThemeRegistry)
    {
        if (Pair.Key.Equals(RequestedKey, ESearchCase::IgnoreCase))
        {
            OutResolvedThemeKey = Pair.Key;
            OutThemeKit = &Pair.Value;
            return true;
        }
    }

    return false;
}

bool URaidChapterConfig::ResolveThemeKitForNode(const FLevelNodeRow& NodeRow, FString& OutResolvedThemeKey, const FModularMeshKit*& OutThemeKit) const
{
    OutResolvedThemeKey.Reset();
    OutThemeKit = nullptr;

    if (ThemeRegistry.Num() <= 0)
    {
        return false;
    }

    // 1) Explicit theme key wins.
    if (ResolveThemeKitByKey(NodeRow.Theme, OutResolvedThemeKey, OutThemeKit))
    {
        return true;
    }

    // 2) Environment fallback.
    if (ResolveThemeKitByKey(NodeRow.EnvType, OutResolvedThemeKey, OutThemeKit))
    {
        return true;
    }

    // 3) Tag-based matching.
    TArray<FString> RoomTokens;
    AppendNormalizedTokens(NodeRow.NodeTags, RoomTokens);
    AppendNormalizedTokens(NodeRow.RoomRole, RoomTokens);
    AppendNormalizedTokens(NodeRow.RoomType, RoomTokens);
    AppendNormalizedTokens(NodeRow.EnvType, RoomTokens);
    AppendNormalizedTokens(NodeRow.Theme, RoomTokens);

    const FString RoomMetaLower = (
        NodeRow.NodeTags + TEXT(" ") +
        NodeRow.RoomRole + TEXT(" ") +
        NodeRow.RoomType + TEXT(" ") +
        NodeRow.EnvType + TEXT(" ") +
        NodeRow.Theme).ToLower();

    int32 BestScore = TNumericLimits<int32>::Min();
    FString BestThemeKey;
    const FModularMeshKit* BestThemeKit = nullptr;

    for (const TPair<FString, FModularMeshKit>& Pair : ThemeRegistry)
    {
        const FString ThemeKeyLower = Pair.Key.ToLower();
        int32 Score = 0;

        if (!ThemeKeyLower.IsEmpty() && RoomMetaLower.Contains(ThemeKeyLower))
        {
            Score += 2;
        }

        if (Pair.Key.Equals(NodeRow.RoomType, ESearchCase::IgnoreCase))
        {
            Score += 3;
        }

        for (const FString& ThemeTagRaw : Pair.Value.ThemeTags)
        {
            const FString ThemeTag = ThemeTagRaw.TrimStartAndEnd().ToLower();
            if (ThemeTag.IsEmpty())
            {
                continue;
            }

            if (RoomMetaLower.Contains(ThemeTag))
            {
                Score += 6;
                continue;
            }

            if (RoomTokens.Contains(ThemeTag))
            {
                Score += 5;
            }
        }

        if (Score > BestScore || (Score == BestScore && (BestThemeKey.IsEmpty() || Pair.Key < BestThemeKey)))
        {
            BestScore = Score;
            BestThemeKey = Pair.Key;
            BestThemeKit = &Pair.Value;
        }
    }

    if (BestThemeKit)
    {
        OutResolvedThemeKey = BestThemeKey;
        OutThemeKit = BestThemeKit;
        return true;
    }

    // 4) Last resort: first theme entry.
    if (ThemeRegistry.Num() > 0)
    {
        auto It = ThemeRegistry.CreateConstIterator();
        if (It)
        {
            OutResolvedThemeKey = It.Key();
            OutThemeKit = &It.Value();
            return true;
        }
    }

    return false;
}

void URaidChapterConfig::AddSelectedAssetsToConfiguredTarget()
{
#if !WITH_EDITOR
    UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] Bulk add is editor-only."));
#else
    TArray<FAssetData> SelectedAssets;
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
    if (SelectedAssets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: select assets in Content Browser first."));
        return;
    }

    auto ResolveThemeKit =
        [&](const FString& RequestedTheme, bool bCreateMissing, FString& OutResolvedThemeKey) -> FModularMeshKit*
        {
            const FString TrimmedTheme = RequestedTheme.TrimStartAndEnd();
            if (!TrimmedTheme.IsEmpty())
            {
                if (FModularMeshKit* Exact = ThemeRegistry.Find(TrimmedTheme))
                {
                    OutResolvedThemeKey = TrimmedTheme;
                    return Exact;
                }

                for (TPair<FString, FModularMeshKit>& Pair : ThemeRegistry)
                {
                    if (Pair.Key.Equals(TrimmedTheme, ESearchCase::IgnoreCase))
                    {
                        OutResolvedThemeKey = Pair.Key;
                        return &Pair.Value;
                    }
                }
            }

            if (!bCreateMissing)
            {
                return nullptr;
            }

            OutResolvedThemeKey = TrimmedTheme.IsEmpty() ? TEXT("Jungle") : TrimmedTheme;
            return &ThemeRegistry.Add(OutResolvedThemeKey, FModularMeshKit());
        };

    auto FindOrAddClusterByName =
        [&](TArray<FMeshCluster>& Clusters, const FString& RequestedName, bool bCreateMissing) -> FMeshCluster*
        {
            for (FMeshCluster& Cluster : Clusters)
            {
                if (Cluster.ClusterName.Equals(RequestedName, ESearchCase::IgnoreCase))
                {
                    return &Cluster;
                }
            }

            if (!bCreateMissing)
            {
                return nullptr;
            }

            FMeshCluster NewCluster;
            NewCluster.ClusterName = RequestedName.IsEmpty() ? TEXT("Foliage_New") : RequestedName;
            NewCluster.SpawnRadius = 1200.0f;
            NewCluster.SpawnCountMin = 1.0f;
            NewCluster.SpawnCountMax = 1.0f;
            NewCluster.MinDistanceBetweenInstances = 180.0f;
            const int32 NewIndex = Clusters.Add(NewCluster);
            return Clusters.IsValidIndex(NewIndex) ? &Clusters[NewIndex] : nullptr;
        };

    auto FindOrAddIndexGroupByName =
        [&](TArray<FMeshVariationIndexGroup>& Groups, const FString& RequestedName, bool bCreateMissing) -> FMeshVariationIndexGroup*
        {
            const FString TrimmedName = RequestedName.TrimStartAndEnd();
            for (FMeshVariationIndexGroup& Group : Groups)
            {
                if (Group.IndexName.Equals(TrimmedName, ESearchCase::IgnoreCase))
                {
                    return &Group;
                }
            }

            if (!bCreateMissing)
            {
                return nullptr;
            }

            FMeshVariationIndexGroup NewGroup;
            NewGroup.IndexName = TrimmedName.IsEmpty() ? TEXT("Index_00") : TrimmedName;
            const int32 NewIndex = Groups.Add(NewGroup);
            return Groups.IsValidIndex(NewIndex) ? &Groups[NewIndex] : nullptr;
        };

    FString ResolvedThemeKey;
    FModularMeshKit* TargetTheme = ResolveThemeKit(BulkAddThemeKey, bBulkAddCreateMissingThemeOrCluster, ResolvedThemeKey);
    if (!TargetTheme)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: theme '%s' not found."), *BulkAddThemeKey);
        return;
    }

    TArray<FMeshVariation>* TargetVariations = nullptr;
    FString ResolvedVariationIndexName;
    FString TargetLabel;
    switch (BulkAddTarget)
    {
    case ERaidConfigBulkAddTarget::FloorVariations:
    {
        FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
            TargetTheme->FloorVariationIndices,
            BulkAddVariationIndexName,
            bBulkAddCreateMissingThemeOrCluster);
        if (!TargetGroup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: floor index '%s' not found in theme '%s'."),
                *BulkAddVariationIndexName, *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetGroup->Variations;
        ResolvedVariationIndexName = TargetGroup->IndexName;
        TargetLabel = FString::Printf(TEXT("Theme[%s].FloorIndices[%s]"), *ResolvedThemeKey, *ResolvedVariationIndexName);
        break;
    }
    case ERaidConfigBulkAddTarget::WallVariations:
    {
        FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
            TargetTheme->WallVariationIndices,
            BulkAddVariationIndexName,
            bBulkAddCreateMissingThemeOrCluster);
        if (!TargetGroup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: wall index '%s' not found in theme '%s'."),
                *BulkAddVariationIndexName, *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetGroup->Variations;
        ResolvedVariationIndexName = TargetGroup->IndexName;
        TargetLabel = FString::Printf(TEXT("Theme[%s].WallIndices[%s]"), *ResolvedThemeKey, *ResolvedVariationIndexName);
        break;
    }
    case ERaidConfigBulkAddTarget::ObstacleVariations:
    {
        FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
            TargetTheme->ObstacleVariationIndices,
            BulkAddVariationIndexName,
            bBulkAddCreateMissingThemeOrCluster);
        if (!TargetGroup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: obstacle index '%s' not found in theme '%s'."),
                *BulkAddVariationIndexName, *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetGroup->Variations;
        ResolvedVariationIndexName = TargetGroup->IndexName;
        TargetLabel = FString::Printf(TEXT("Theme[%s].ObstacleIndices[%s]"), *ResolvedThemeKey, *ResolvedVariationIndexName);
        break;
    }
    case ERaidConfigBulkAddTarget::DecorationVariations:
    {
        FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
            TargetTheme->DecorationVariationIndices,
            BulkAddVariationIndexName,
            bBulkAddCreateMissingThemeOrCluster);
        if (!TargetGroup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: decoration index '%s' not found in theme '%s'."),
                *BulkAddVariationIndexName, *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetGroup->Variations;
        ResolvedVariationIndexName = TargetGroup->IndexName;
        TargetLabel = FString::Printf(TEXT("Theme[%s].DecorationIndices[%s]"), *ResolvedThemeKey, *ResolvedVariationIndexName);
        break;
    }
    case ERaidConfigBulkAddTarget::DoorBlockerVariations:
    {
        FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
            TargetTheme->DoorBlockerVariationIndices,
            BulkAddVariationIndexName,
            bBulkAddCreateMissingThemeOrCluster);
        if (!TargetGroup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: door blocker index '%s' not found in theme '%s'."),
                *BulkAddVariationIndexName, *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetGroup->Variations;
        ResolvedVariationIndexName = TargetGroup->IndexName;
        TargetLabel = FString::Printf(TEXT("Theme[%s].DoorBlockerIndices[%s]"), *ResolvedThemeKey, *ResolvedVariationIndexName);
        break;
    }
    case ERaidConfigBulkAddTarget::FoliageClusterVariations:
    {
        const FString RequestedCluster = BulkAddFoliageClusterName.TrimStartAndEnd();
        FMeshCluster* TargetCluster = FindOrAddClusterByName(TargetTheme->FoliageClusters, RequestedCluster, bBulkAddCreateMissingThemeOrCluster);
        if (!TargetCluster)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: foliage cluster '%s' not found in theme '%s'."),
                *RequestedCluster,
                *ResolvedThemeKey);
            return;
        }
        TargetVariations = &TargetCluster->Variations;
        TargetLabel = FString::Printf(TEXT("Theme[%s].FoliageCluster[%s]"), *ResolvedThemeKey, *TargetCluster->ClusterName);
        break;
    }
    default:
        break;
    }

    if (!TargetVariations)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidConfig] BulkAdd failed: target array resolve failed."));
        return;
    }

    const FVector SafeDefaultScale(
        FMath::Clamp(BulkAddDefaultScale.X, 0.01f, 10.0f),
        FMath::Clamp(BulkAddDefaultScale.Y, 0.01f, 10.0f),
        FMath::Clamp(BulkAddDefaultScale.Z, 0.01f, 10.0f));
    const float SafeDefaultWeight = FMath::Max(0.01f, BulkAddDefaultSpawnWeight);

    auto BuildVariationFromAsset =
        [&](const FAssetData& AssetData, FMeshVariation& OutVariation) -> bool
        {
            UObject* AssetObject = AssetData.GetAsset();
            if (!IsValid(AssetObject))
            {
                return false;
            }

            OutVariation = FMeshVariation();
            OutVariation.SpawnWeight = SafeDefaultWeight;
            OutVariation.Offset = FTransform::Identity;
            OutVariation.Offset.SetLocation(BulkAddDefaultLocationOffset);
            OutVariation.Offset.SetScale3D(SafeDefaultScale);
            OutVariation.bUseRandomScale = false;
            OutVariation.bUseRandomRotation = false;
            OutVariation.bUseRandomLocationJitter = false;

            if (UStaticMesh* StaticMeshAsset = Cast<UStaticMesh>(AssetObject))
            {
                OutVariation.Mesh = TSoftObjectPtr<UStaticMesh>(StaticMeshAsset);
                return true;
            }

            if (bBulkAddIncludeBlueprintActors)
            {
                if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(AssetObject))
                {
                    UClass* GeneratedClass = BlueprintAsset->GeneratedClass;
                    if (GeneratedClass && GeneratedClass->IsChildOf(AActor::StaticClass()))
                    {
                        OutVariation.BlueprintPrefab = TSoftClassPtr<AActor>(GeneratedClass);
                        return true;
                    }
                }
                else if (UClass* ClassAsset = Cast<UClass>(AssetObject))
                {
                    if (ClassAsset->IsChildOf(AActor::StaticClass()))
                    {
                        OutVariation.BlueprintPrefab = TSoftClassPtr<AActor>(ClassAsset);
                        return true;
                    }
                }
            }

            return false;
        };

    auto IsDuplicateVariation = [&](const FMeshVariation& Candidate) -> bool
        {
            if (!bBulkAddSkipDuplicates)
            {
                return false;
            }

            const FSoftObjectPath CandidateMeshPath = Candidate.Mesh.ToSoftObjectPath();
            const FSoftObjectPath CandidateClassPath = Candidate.BlueprintPrefab.ToSoftObjectPath();

            for (const FMeshVariation& Existing : *TargetVariations)
            {
                if (!CandidateMeshPath.IsNull() && CandidateMeshPath == Existing.Mesh.ToSoftObjectPath())
                {
                    return true;
                }
                if (!CandidateClassPath.IsNull() && CandidateClassPath == Existing.BlueprintPrefab.ToSoftObjectPath())
                {
                    return true;
                }
            }

            return false;
        };

    int32 AddedCount = 0;
    int32 DuplicateCount = 0;
    int32 UnsupportedCount = 0;

    Modify();
    for (const FAssetData& AssetData : SelectedAssets)
    {
        FMeshVariation NewVariation;
        if (!BuildVariationFromAsset(AssetData, NewVariation))
        {
            ++UnsupportedCount;
            continue;
        }

        if (IsDuplicateVariation(NewVariation))
        {
            ++DuplicateCount;
            continue;
        }

        TargetVariations->Add(NewVariation);
        ++AddedCount;
    }

    if (AddedCount > 0)
    {
        MarkPackageDirty();
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidConfig] BulkAdd done -> %s | Selected=%d Added=%d Duplicates=%d Unsupported=%d"),
        *TargetLabel,
        SelectedAssets.Num(),
        AddedCount,
        DuplicateCount,
        UnsupportedCount);
#endif
}
