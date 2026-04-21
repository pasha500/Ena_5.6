#include "Raid/RaidRoomActor.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

void ARaidRoomActor::GenerateRoomLayout()
{
    if (!bNodeDataInitialized) return;

    // Keep production/runtime visuals aligned with world assets.
    // Semantic whitebox tinting is an editor helper and should stay opt-in in PIE/Game.
    if (UWorld* World = GetWorld())
    {
        if (World->IsGameWorld() && bUseSemanticWhiteboxColors && !bAllowSemanticWhiteboxColorsInGame)
        {
            bUseSemanticWhiteboxColors = false;
            SemanticMaterialCache.Empty();
            TraversalMaterialCache = nullptr;
        }
    }

    ClearAllMeshInstances();
    const FModularMeshKit* ThemeKit = nullptr;
    FString ResolvedThemeKey;
    if (ChapterConfigRef)
    {
        ChapterConfigRef->ResolveThemeKitForNode(NodeRow, ResolvedThemeKey, ThemeKit);
    }
    FString TypeStr = UEnum::GetDisplayValueAsText(CurrentRoomType).ToString();
    const FString ThemeLabel = ResolvedThemeKey.IsEmpty() ? NodeRow.EnvType : ResolvedThemeKey;
    StatusText->SetText(FText::FromString(FString::Printf(TEXT("< Room %d : %s >\n[%s] Zone %d"), NodeId, *TypeStr, *ThemeLabel, NodeRow.ZoneId)));
    float RoomRadius = (GridSize * TileSize) / 2.0f;
    Trigger->SetBoxExtent(FVector(RoomRadius, RoomRadius, 5000.0f));

    auto HasUsableTheme = [](const FModularMeshKit* InThemeKit) -> bool
    {
        if (!InThemeKit)
        {
            return false;
        }

        return
            InThemeKit->HasAnyVariationsForChannel(ERaidVariationOffsetChannel::Floor) ||
            InThemeKit->HasAnyVariationsForChannel(ERaidVariationOffsetChannel::Wall) ||
            InThemeKit->HasAnyVariationsForChannel(ERaidVariationOffsetChannel::Obstacle) ||
            InThemeKit->HasAnyVariationsForChannel(ERaidVariationOffsetChannel::Decoration) ||
            InThemeKit->FoliageClusters.Num() > 0;
    };

    if (!HasUsableTheme(ThemeKit))
    {
        if (HasUsableTheme(CachedResolvedThemeKit))
        {
            ThemeKit = CachedResolvedThemeKit;
        }

        if (!HasUsableTheme(ThemeKit) && ChapterConfigRef)
        {
            FString FallbackResolvedThemeKey;
            const FModularMeshKit* FallbackResolvedThemeKit = nullptr;
            if (ChapterConfigRef->ResolveThemeKitByKey(NodeRow.EnvType, FallbackResolvedThemeKey, FallbackResolvedThemeKit) &&
                HasUsableTheme(FallbackResolvedThemeKit))
            {
                ThemeKit = FallbackResolvedThemeKit;
                if (ResolvedThemeKey.IsEmpty())
                {
                    ResolvedThemeKey = FallbackResolvedThemeKey;
                }
            }
            else
            {
                for (const TPair<FString, FModularMeshKit>& ThemePair : ChapterConfigRef->ThemeRegistry)
                {
                    if (HasUsableTheme(&ThemePair.Value))
                    {
                        ThemeKit = &ThemePair.Value;
                        if (ResolvedThemeKey.IsEmpty())
                        {
                            ResolvedThemeKey = ThemePair.Key;
                        }
                        break;
                    }
                }
            }
        }
    }

    if (!HasUsableTheme(ThemeKit))
    {
        if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Room %d has no usable theme kit at runtime. Skipping prototype BasicShapes fallback."),
                NodeId);
            return;
        }

        static FModularMeshKit FallbackKit;
        static bool bFallbackInit = false;
        if (!bFallbackInit)
        {
            bFallbackInit = true;
            FallbackKit.bIsOrganicTheme = false;

            FMeshVariation FloorV;
            FloorV.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
            FloorV.Offset.SetScale3D(FVector(4.4f, 4.4f, 0.12f));
            FallbackKit.FloorVariations.Add(FloorV);

            FMeshVariation WallV;
            WallV.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
            WallV.Offset.SetScale3D(FVector(4.4f, 0.20f, 2.6f));
            FallbackKit.WallVariations.Add(WallV);
        }
        ThemeKit = &FallbackKit;
    }

    TArray<FMeshVariation> FloorVariations;
    TArray<FMeshVariation> WallVariations;
    TArray<FMeshVariation> ObstacleVariations;
    TArray<FMeshVariation> DecorationVariations;
    ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Floor, FloorVariations);
    ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Wall, WallVariations);
    ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, ObstacleVariations);
    ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Decoration, DecorationVariations);

    if (const UWorld* RuntimeWorld = GetWorld(); RuntimeWorld && RuntimeWorld->IsGameWorld())
    {
        FString FirstObstacleAsset = TEXT("<none>");
        FVector FirstObstacleOffset = FVector::ZeroVector;
        if (ObstacleVariations.Num() > 0)
        {
            const FMeshVariation& FirstObstacle = ObstacleVariations[0];
            FirstObstacleAsset = !FirstObstacle.Mesh.IsNull()
                ? FirstObstacle.Mesh.ToString()
                : FirstObstacle.BlueprintPrefab.ToString();
            FirstObstacleOffset = FirstObstacle.Offset.GetLocation();
        }

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Runtime theme kit Node=%d Theme=%s Env=%s ThemeField=%s RoomType=%s Floor=%d Wall=%d Obstacle=%d Decoration=%d FirstObstacle=%s FirstObstacleOffset=(%.1f,%.1f,%.1f)"),
            NodeId,
            *ResolvedThemeKey,
            *NodeRow.EnvType,
            *NodeRow.Theme,
            *NodeRow.RoomType,
            FloorVariations.Num(),
            WallVariations.Num(),
            ObstacleVariations.Num(),
            DecorationVariations.Num(),
            *FirstObstacleAsset,
            FirstObstacleOffset.X,
            FirstObstacleOffset.Y,
            FirstObstacleOffset.Z);
    }

    float Offset = RoomRadius - (TileSize / 2.0f); int32 Center = GridSize / 2; TArray<uint8> ActiveMask; ActiveMask.Init(1, GridSize * GridSize);
    auto ToIndex = [this](int32 X, int32 Y) { return X * GridSize + Y; }; auto IsInside = [this](int32 X, int32 Y) { return X >= 0 && X < GridSize && Y >= 0 && Y < GridSize; }; auto IsActiveTile = [&ActiveMask, &ToIndex](int32 X, int32 Y) { return ActiveMask[ToIndex(X, Y)] != 0; };
    const bool bUrban = NodeRow.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase);

    for (int32 X = 0; X < GridSize; ++X)
    {
        for (int32 Y = 0; Y < GridSize; ++Y)
        {
            if (!IsActiveTile(X, Y)) continue;
            FVector Location = FVector(X * TileSize - Offset, Y * TileSize - Offset, 0.0f);
            if (bEnableRoomShellGeometry) { if (const FMeshVariation* RandomFloor = RaidMeshUtils::PickRandomVariation(FloorVariations, RoomRandomStream)) { AddMeshInstance(*RandomFloor, FTransform(FRotator::ZeroRotator, Location), 0); } }

            bool bExposeNorth = !IsInside(X + 1, Y) || !IsActiveTile(X + 1, Y); bool bExposeSouth = !IsInside(X - 1, Y) || !IsActiveTile(X - 1, Y); bool bExposeEast = !IsInside(X, Y + 1) || !IsActiveTile(X, Y + 1); bool bExposeWest = !IsInside(X, Y - 1) || !IsActiveTile(X, Y - 1);
            bool bNorthBoundary = (X == GridSize - 1); bool bSouthBoundary = (X == 0); bool bEastBoundary = (Y == GridSize - 1); bool bWestBoundary = (Y == 0);

            auto SpawnEdge = [&](bool bExpose, float Yaw, bool bDoorOnEdge, bool bSkipForNeighbor) {
                if (!bExpose || bSkipForNeighbor) return;
                float WallSurvivalChance = bUrban ? 0.20f : 0.0f; bool bSpawnWall = RoomRandomStream.FRand() < WallSurvivalChance;
                if (bDoorOnEdge) { const bool bLockDoorForCombatFlow = !bCombatCleared && CurrentRoomType != ERaidRoomType::Start && CurrentRoomType != ERaidRoomType::Exit; if (bLockDoorForCombatFlow) { if (AActor* DoorBlocker = SpawnProceduralDoorBlocker(*ThemeKit, Location, Yaw)) { SpawnedDoorActors.Add(DoorBlocker); } } }
                else if (bSpawnWall) { if (const FMeshVariation* RandomWall = RaidMeshUtils::PickRandomVariation(WallVariations, RoomRandomStream)) { AddMeshInstance(*RandomWall, FTransform(FRotator(0, Yaw, 0), Location), 1); } }
                };

            if (bEnableRoomShellGeometry) { SpawnEdge(bExposeNorth, 180.0f, bNorthBoundary && bDoorNorth && Y == Center, false); SpawnEdge(bExposeSouth, 0.0f, bSouthBoundary && bDoorSouth && Y == Center, false); SpawnEdge(bExposeEast, -90.0f, bEastBoundary && bDoorEast && X == Center, false); SpawnEdge(bExposeWest, 90.0f, bWestBoundary && bDoorWest && X == Center, false); }

            int32 ExposedCount = (bExposeNorth ? 1 : 0) + (bExposeSouth ? 1 : 0) + (bExposeEast ? 1 : 0) + (bExposeWest ? 1 : 0);
            // Traversal 키트가 활성화된 경우 내부 장애물은 해당 경로에서만 생성해 중복/중첩을 막는다.
            if (bEnableRoomInteriorGeometry && ExposedCount == 0 && !bEnableTraversalWhiteboxKit)
            {
                if (CurrentRoomType == ERaidRoomType::Combat || CurrentRoomType == ERaidRoomType::Boss || CurrentRoomType == ERaidRoomType::Loot) {
                    FVector RandomOffset(RoomRandomStream.FRandRange(-160.0f, 160.0f), RoomRandomStream.FRandRange(-160.0f, 160.0f), 0.0f);
                    if (ObstacleVariations.Num() > 0 && RoomRandomStream.FRand() < NodeRow.ObstacleDensity) { if (const FMeshVariation* RandomObs = RaidMeshUtils::PickRandomVariation(ObstacleVariations, RoomRandomStream)) { AddMeshInstance(*RandomObs, FTransform(FRotator(0, RoomRandomStream.RandRange(0, 3) * 90.0f, 0), Location + RandomOffset), 2); } }
                }
            }
        }
    }

    if (bEnableRoomShellGeometry || bEnableRoomInteriorGeometry) GenerateTraversalWhiteboxKit(RoomRadius, ThemeKit);

    TArray<FVector> BuildingWorldLocs;
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : DynamicISMC_Pool)
    {
        if (IsValid(ISMC) && (ISMC->ComponentTags.Contains(TEXT("MeshType_1")) || ISMC->ComponentTags.Contains(TEXT("MeshType_2"))))
        {
            for (int32 i = 0; i < ISMC->GetInstanceCount(); ++i)
            {
                if (!ISMC->IsValidInstance(i))
                {
                    continue;
                }

                FTransform Trans;
                if (ISMC->GetInstanceTransform(i, Trans, true))
                {
                    BuildingWorldLocs.Add(Trans.GetLocation());
                }
            }
        }
    }

    if (bEnableRoomOrganicClusters && ThemeKit->bIsOrganicTheme && ThemeKit->FoliageClusters.Num() > 0)
    {
        TArray<FVector> LocalSpawnedLocations;
        for (const FMeshCluster& Cluster : ThemeKit->FoliageClusters)
        {
            if (Cluster.Variations.Num() == 0) continue;
            int32 TargetSpawnCount = Cluster.CalculateSpawnCount(RoomRadius, RoomRandomStream);
            int32 Spawned = 0;

            // [분기 처리] 6번 나무, 7번 풀, 8번 바위 분리!
            int32 FoliageMeshType = 6;
            if (Cluster.ClusterName.Contains(TEXT("NoCol")) || Cluster.ClusterName.Contains(TEXT("Bush"))) FoliageMeshType = 7;
            else if (Cluster.ClusterName.Contains(TEXT("Rock"))) FoliageMeshType = 8;

            float BuildingClearanceSq = FMath::Square(800.0f); // 나무는 건물에서 8m 배척
            if (FoliageMeshType == 7) BuildingClearanceSq = FMath::Square(350.0f); // 풀은 3.5m 배척
            else if (FoliageMeshType == 8) BuildingClearanceSq = FMath::Square(400.0f); // 바위는 4m 배척

            float MinDistSq = FMath::Max(FMath::Square(Cluster.MinDistanceBetweenInstances), BuildingClearanceSq * 0.5f);

            for (int32 i = 0; i < TargetSpawnCount * 10 && Spawned < TargetSpawnCount; ++i)
            {
                FVector FoliageLoc = FVector(RoomRandomStream.FRandRange(-RoomRadius + 150.f, RoomRadius - 150.f), RoomRandomStream.FRandRange(-RoomRadius + 150.f, RoomRadius - 150.f), 0.0f);

                bool bOverlaps = false;
                for (const FVector& Loc : LocalSpawnedLocations) { if (FVector::DistSquaredXY(Loc, FoliageLoc) < MinDistSq) { bOverlaps = true; break; } }
                if (bOverlaps) continue;

                bool bHitsBuilding = false;
                FVector WorldFoliageLoc = GetActorTransform().TransformPosition(FoliageLoc);
                for (const FVector& BldgLoc : BuildingWorldLocs) {
                    if (FVector::DistSquaredXY(BldgLoc, WorldFoliageLoc) < BuildingClearanceSq) { bHitsBuilding = true; break; }
                }
                if (bHitsBuilding) continue;

                if (const FMeshVariation* RandomFoliage = RaidMeshUtils::PickRandomVariation(Cluster.Variations, RoomRandomStream))
                {
                    // Avoid double-applying variation offset in AddMeshInstance():
                    // cluster randomization uses a no-offset proxy variation, then real variation offset is applied once in AddMeshInstance.
                    FMeshVariation ClusterProxyVariation = *RandomFoliage;
                    ClusterProxyVariation.Offset = FTransform::Identity;
                    FTransform ClusterBaseTrans = Cluster.GetClusterRandomizedTransform(
                        ClusterProxyVariation,
                        FTransform(FRotator::ZeroRotator, FoliageLoc),
                        RoomRandomStream);
                    AddMeshInstance(*RandomFoliage, ClusterBaseTrans, FoliageMeshType);
                    LocalSpawnedLocations.Add(FoliageLoc); Spawned++;
                }
            }
        }
    }

    RunBlueprintTerrainStabilizationPass();
    FlushQueuedNavigationUpdates();
}
