#include "Raid/RaidLayoutManager.h"
#include "Raid/RaidRoomActor.h"

int32 ARaidLayoutManager::ResolveMeshTypeFromComponentTags(const TArray<FName>& ComponentTags)
{
    static const FString MeshTypePrefix(TEXT("MeshType_"));
    for (const FName& Tag : ComponentTags)
    {
        const FString TagString = Tag.ToString();
        if (!TagString.StartsWith(MeshTypePrefix))
        {
            continue;
        }

        const FString Suffix = TagString.RightChop(MeshTypePrefix.Len());
        if (!Suffix.IsEmpty())
        {
            return FCString::Atoi(*Suffix);
        }
    }

    return 2;
}

int32 ARaidLayoutManager::ApplyRoomOptimizationToRoom(ARaidRoomActor* Room, bool bReapplyExistingInstances)
{
    if (!IsValid(Room))
    {
        return 0;
    }

    const float SafeCullStart = FMath::Max(0.0f, RoomDetailCullStartDistance);
    const float SafeCullEnd = FMath::Max(SafeCullStart, RoomDetailCullEndDistance);

    Room->Modify();
    Room->bAutoOptimizeInstancedMeshes = bAutoOptimizeRoomInstancedMeshes;
    Room->DetailCullStartDistance = SafeCullStart;
    Room->DetailCullEndDistance = SafeCullEnd;

    if (!bReapplyExistingInstances)
    {
        return 0;
    }

    int32 UpdatedISMCCount = 0;
    Room->DynamicISMC_Pool.RemoveAll([](UHierarchicalInstancedStaticMeshComponent* Comp) { return !IsValid(Comp); });
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : Room->DynamicISMC_Pool)
    {
        if (!IsValid(ISMC))
        {
            continue;
        }

        const int32 MeshType = ResolveMeshTypeFromComponentTags(ISMC->ComponentTags);
        Room->ApplyISMCOptimization(ISMC, MeshType);
        UpdatedISMCCount++;
    }

    return UpdatedISMCCount;
}

void ARaidLayoutManager::ApplyRoomOptimizationToSpawnedRooms(const TCHAR* PresetName)
{
    if (SpawnedRooms.Num() <= 0)
    {
        return;
    }

    int32 UpdatedRoomCount = 0;
    int32 UpdatedISMCCount = 0;
    for (TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : SpawnedRooms)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!IsValid(Room))
        {
            continue;
        }

        UpdatedISMCCount += ApplyRoomOptimizationToRoom(Room, true);
        UpdatedRoomCount++;
    }

    if (UpdatedRoomCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Linked room LOD preset '%s' applied to %d rooms (%d ISMCs, AutoOptimize=%s, Cull=%.0f~%.0f)."),
            PresetName ? PresetName : TEXT("Unknown"),
            UpdatedRoomCount,
            UpdatedISMCCount,
            bAutoOptimizeRoomInstancedMeshes ? TEXT("On") : TEXT("Off"),
            RoomDetailCullStartDistance,
            RoomDetailCullEndDistance);
    }
}

void ARaidLayoutManager::HandleBackgroundPresetApplied(const TCHAR* PresetName)
{
    const float NearMax = FMath::Max(1000.0f, BackgroundISMCNearMaxDistance);
    const float MidMax = FMath::Max(NearMax + 500.0f, BackgroundISMCMidMaxDistance);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidLayout] Applied background LOD preset '%s' (Bands=%s Near=%.0f Mid=%.0f NearCull=%d~%d MidCull=%d~%d FarCull=%d~%d)"),
        PresetName ? PresetName : TEXT("Unknown"),
        bEnableBackgroundISMCDistanceBands ? TEXT("On") : TEXT("Off"),
        NearMax,
        MidMax,
        BackgroundISMCNearCullStart,
        BackgroundISMCNearCullEnd,
        BackgroundISMCMidCullStart,
        BackgroundISMCMidCullEnd,
        BackgroundISMCFarCullStart,
        BackgroundISMCFarCullEnd);

    if (bLinkRoomLODToBackgroundPresets && bAutoApplyRoomOptimizationOnPresetApply)
    {
        ApplyRoomOptimizationToSpawnedRooms(PresetName);
    }

    if (!bAutoRebuildBackgroundOnPresetApply)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const bool bHasSpawnedBackground =
        (SpawnedBackgroundActors.Num() > 0) || (BackgroundISMC_Pool.Num() > 0);
    if (!bHasSpawnedBackground)
    {
        return;
    }

    ScatterBackgroundScenery();
    UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Rebuilt background scatter after '%s' preset."), PresetName ? PresetName : TEXT("Unknown"));
}

void ARaidLayoutManager::ApplyBackgroundLODQualityPreset()
{
    Modify();
    bEnableBackgroundISMCDistanceBands = true;

    BackgroundISMCNearMaxDistance = 26000.0f;
    BackgroundISMCMidMaxDistance = 78000.0f;

    BackgroundISMCNearCullStart = 0;
    BackgroundISMCNearCullEnd = 65000;
    bBackgroundISMCNearCastShadow = true;

    BackgroundISMCMidCullStart = 18000;
    BackgroundISMCMidCullEnd = 130000;
    bBackgroundISMCMidCastShadow = true;

    BackgroundISMCFarCullStart = 52000;
    BackgroundISMCFarCullEnd = 230000;
    bBackgroundISMCFarCastShadow = true;

    bReduceShadowsForInstancedBackgroundTrees = false;
    bReduceShadowsForNoCollisionBackground = false;

    if (bLinkRoomLODToBackgroundPresets)
    {
        bAutoOptimizeRoomInstancedMeshes = true;
        RoomDetailCullStartDistance = FMath::Max(0.0f, RoomDetailCullStart_Quality);
        RoomDetailCullEndDistance = FMath::Max(RoomDetailCullStartDistance, RoomDetailCullEnd_Quality);
    }

    HandleBackgroundPresetApplied(TEXT("Quality"));
}

void ARaidLayoutManager::ApplyBackgroundLODBalancedPreset()
{
    Modify();
    bEnableBackgroundISMCDistanceBands = true;

    BackgroundISMCNearMaxDistance = 18000.0f;
    BackgroundISMCMidMaxDistance = 45000.0f;

    BackgroundISMCNearCullStart = 0;
    BackgroundISMCNearCullEnd = 35000;
    bBackgroundISMCNearCastShadow = true;

    BackgroundISMCMidCullStart = 12000;
    BackgroundISMCMidCullEnd = 70000;
    bBackgroundISMCMidCastShadow = true;

    BackgroundISMCFarCullStart = 28000;
    BackgroundISMCFarCullEnd = 140000;
    bBackgroundISMCFarCastShadow = false;

    bReduceShadowsForInstancedBackgroundTrees = true;
    bReduceShadowsForNoCollisionBackground = true;

    if (bLinkRoomLODToBackgroundPresets)
    {
        bAutoOptimizeRoomInstancedMeshes = true;
        RoomDetailCullStartDistance = FMath::Max(0.0f, RoomDetailCullStart_Balanced);
        RoomDetailCullEndDistance = FMath::Max(RoomDetailCullStartDistance, RoomDetailCullEnd_Balanced);
    }

    HandleBackgroundPresetApplied(TEXT("Balanced"));
}

void ARaidLayoutManager::ApplyBackgroundLODPerformancePreset()
{
    Modify();
    bEnableBackgroundISMCDistanceBands = true;

    BackgroundISMCNearMaxDistance = 12000.0f;
    BackgroundISMCMidMaxDistance = 32000.0f;

    BackgroundISMCNearCullStart = 0;
    BackgroundISMCNearCullEnd = 24000;
    bBackgroundISMCNearCastShadow = true;

    BackgroundISMCMidCullStart = 9000;
    BackgroundISMCMidCullEnd = 48000;
    bBackgroundISMCMidCastShadow = false;

    BackgroundISMCFarCullStart = 18000;
    BackgroundISMCFarCullEnd = 90000;
    bBackgroundISMCFarCastShadow = false;

    bReduceShadowsForInstancedBackgroundTrees = true;
    bReduceShadowsForNoCollisionBackground = true;

    if (bLinkRoomLODToBackgroundPresets)
    {
        bAutoOptimizeRoomInstancedMeshes = true;
        RoomDetailCullStartDistance = FMath::Max(0.0f, RoomDetailCullStart_Performance);
        RoomDetailCullEndDistance = FMath::Max(RoomDetailCullStartDistance, RoomDetailCullEnd_Performance);
    }

    HandleBackgroundPresetApplied(TEXT("Performance"));
}
