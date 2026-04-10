#include "Raid/RaidRoomActor.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"

void ARaidRoomActor::MaybeEnableNaniteForMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if (!bAutoEnableNaniteInEditor || !Mesh) return;
    const FString MeshPath = Mesh->GetPathName();
    if (!MeshPath.StartsWith(TEXT("/Game/"))) return;
    if (Mesh->GetNumTriangles(0) < NaniteTriangleThreshold) return;
    if (Mesh->NaniteSettings.bEnabled) return;
    Mesh->Modify(); Mesh->NaniteSettings.bEnabled = true; Mesh->MarkPackageDirty();
#endif
}

FTransform ARaidRoomActor::ResolveVariationTransform(
    const FMeshVariation& Variation,
    const FTransform& BaseTransform,
    ERaidVariationOffsetChannel OffsetChannel)
{
    static_cast<void>(OffsetChannel);
    return Variation.GetRandomizedTransform(BaseTransform, RoomRandomStream);
}

bool ARaidRoomActor::IsProximityAutoStartEligibleRoomType() const
{
    return
        NodeRow.RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase) ||
        NodeRow.RoomType.Equals(TEXT("Normal"), ESearchCase::IgnoreCase) ||
        NodeRow.RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase);
}

float ARaidRoomActor::ResolveProximityAutoStartDistanceUU() const
{
    if (CachedProximityAutoStartDistanceUU > KINDA_SMALL_NUMBER)
    {
        return CachedProximityAutoStartDistanceUU;
    }

    const float MinMeters = FMath::Clamp(RoomProximityAutoStartMinDistanceMeters, 10.0f, 300.0f);
    const float MaxMeters = FMath::Clamp(RoomProximityAutoStartMaxDistanceMeters, MinMeters, 400.0f);

    uint32 Hash = HashCombine(GetTypeHash(NodeId), GetTypeHash(NodeRow.Seed));
    Hash = HashCombine(Hash, GetTypeHash(NodeRow.RoomType));
    const float Alpha = static_cast<float>(Hash & 0xFFFFu) / 65535.0f;
    const float Meters = FMath::Lerp(MinMeters, MaxMeters, Alpha);
    CachedProximityAutoStartDistanceUU = FMath::Max(0.0f, Meters * 100.0f);
    return CachedProximityAutoStartDistanceUU;
}

void ARaidRoomActor::ApplyISMCOptimization(UHierarchicalInstancedStaticMeshComponent* ISMC, int32 MeshType) const
{
    if (!ISMC) return;
    ISMC->SetMobility(EComponentMobility::Static);
    // Keep gameplay collision but avoid forcing all trace channels to Block.
    // Forcing BlockAll made traversal traces hit every generated mesh and bypassed
    // map-authored climbing restrictions.
    const bool bNoCollisionMeshType = (MeshType == 7);
    if (bNoCollisionMeshType)
    {
        ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ISMC->SetCollisionProfileName(TEXT("NoCollision"));
        ISMC->CanCharacterStepUpOn = ECB_No;
    }
    else
    {
        ISMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        ISMC->SetCollisionObjectType(ECC_WorldStatic);
        // Preserve per-asset/per-map response setup and only guarantee solid pawn collision.
        ISMC->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        ISMC->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
        // Traversal / mantle / climb traces are commonly on Visibility/Camera.
        // Keep these blocked for room gameplay geometry to avoid non-climbable obstacles.
        ISMC->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        ISMC->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
        ISMC->CanCharacterStepUpOn = ECB_Yes;
    }

    if (bAutoOptimizeInstancedMeshes)
    {
        // Keep room gameplay geometry always visible to avoid invisible-collider reports.
        if (MeshType == 0 || MeshType == 1 || MeshType == 2 || MeshType == 3)
        {
            ISMC->SetCullDistances(0, 0);
        }
        else
        {
            ISMC->SetCullDistances(
                FMath::RoundToInt(FMath::Max(0.0f, DetailCullStartDistance)),
                FMath::RoundToInt(FMath::Max(0.0f, DetailCullEndDistance)));
        }
    }

    const bool bShouldCastShadow = (MeshType <= 2 || MeshType == 6 || MeshType == 8);
    // Keep nav relevance only for blocking obstacle meshes.
    // Wall/floor/deco ISMC nav updates are expensive and often generate empty-bounds warnings during procedural regeneration.
    const bool bShouldAffectNavigation = bEnableObstacleNavigationUpdates && (!bNoCollisionMeshType) && (MeshType == 2);
    ISMC->SetCastShadow(bShouldCastShadow);
    ISMC->bCastDynamicShadow = bShouldCastShadow;
    ISMC->bCastStaticShadow = bShouldCastShadow;
    ISMC->SetReceivesDecals(false);
    ISMC->SetCanEverAffectNavigation(bShouldAffectNavigation);
    // Keep visual instances and collision representation in sync.
    // Density scaling can hide render instances while collision remains, causing "invisible mesh" reports.
    ISMC->bEnableDensityScaling = false;
}

void ARaidRoomActor::QueueNavigationUpdateForISMC(UHierarchicalInstancedStaticMeshComponent* ISMC)
{
    if (!bEnableObstacleNavigationUpdates)
    {
        return;
    }

    if (!IsValid(ISMC))
    {
        return;
    }

    if (!ISMC->GetStaticMesh() || ISMC->GetInstanceCount() <= 0)
    {
        return;
    }

    PendingNavUpdateISMCs.AddUnique(ISMC);
}

void ARaidRoomActor::FlushQueuedNavigationUpdates()
{
    if (!bEnableObstacleNavigationUpdates)
    {
        PendingNavUpdateISMCs.Reset();
        return;
    }

    if (PendingNavUpdateISMCs.Num() <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        PendingNavUpdateISMCs.Reset();
        return;
    }

    UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
    if (!NavSys)
    {
        PendingNavUpdateISMCs.Reset();
        return;
    }

    for (UHierarchicalInstancedStaticMeshComponent* ISMC : PendingNavUpdateISMCs)
    {
        if (!IsValid(ISMC))
        {
            continue;
        }

        if (!ISMC->GetStaticMesh())
        {
            continue;
        }

        if (ISMC->GetInstanceCount() <= 0)
        {
            continue;
        }

        bool bHasAnyValidInstance = false;
        const int32 InstanceCount = ISMC->GetInstanceCount();
        for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
        {
            if (ISMC->IsValidInstance(InstanceIndex))
            {
                bHasAnyValidInstance = true;
                break;
            }
        }

        if (!bHasAnyValidInstance)
        {
            continue;
        }

        if (!ISMC->IsRegistered())
        {
            continue;
        }

        const FBoxSphereBounds Bounds = ISMC->CalcBounds(ISMC->GetComponentTransform());
        if (!Bounds.GetBox().IsValid || Bounds.SphereRadius <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        if (!ISMC->CanEverAffectNavigation())
        {
            ISMC->SetCanEverAffectNavigation(true);
            if (!ISMC->CanEverAffectNavigation())
            {
                continue;
            }
        }

        // Use full component nav update for stability; per-instance update paths can ensure on sparse/invalid instance slots.
        FNavigationSystem::UpdateComponentData(*ISMC);
    }

    PendingNavUpdateISMCs.Reset();
}

void ARaidRoomActor::ClearAllMeshInstances()
{
    for (AActor* Spawned : SpawnedDynamicActors) { if (IsValid(Spawned)) Spawned->Destroy(); }
    SpawnedDynamicActors.Empty();
    for (AActor* DoorActor : SpawnedDoorActors) { if (IsValid(DoorActor)) DoorActor->Destroy(); }
    SpawnedDoorActors.Empty();
    SpawnedObstacleFootprints.Reset();
    PendingNavUpdateISMCs.Reset();

    if (UWorld* World = GetWorld())
    {
        const FName RoomGeneratedTag(TEXT("RaidRoomGenerated"));
        const FName RoomNodeTag(*FString::Printf(TEXT("RaidRoomNode_%d"), NodeId));
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor == this)
            {
                continue;
            }

            const bool bOwnedByThisRoom =
                Actor->GetOwner() == this ||
                Actor->GetAttachParentActor() == this ||
                Actor->ActorHasTag(RoomNodeTag);
            if (!bOwnedByThisRoom)
            {
                continue;
            }

            if (Actor->ActorHasTag(RoomGeneratedTag) || Actor->ActorHasTag(TEXT("RaidDoorBlocker")) || Actor->ActorHasTag(RoomNodeTag))
            {
                Actor->Destroy();
            }
        }
    }

    TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> OwnedISMCs(this);
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : OwnedISMCs)
    {
        if (!IsValid(ISMC))
        {
            continue;
        }

        const bool bRuntimeGenerated =
            ISMC->CreationMethod == EComponentCreationMethod::Instance ||
            DynamicISMC_Pool.Contains(ISMC) ||
            ISMC->ComponentTags.Contains(TEXT("RaidRuntimeISMC")) ||
            ISMC->ComponentTags.Contains(TEXT("RaidRoomRuntimeISMC"));
        if (!bRuntimeGenerated)
        {
            continue;
        }

        if (ISMC->IsRegistered())
        {
            ISMC->UnregisterComponent();
        }
        ISMC->DestroyComponent();
    }

    DynamicISMC_Pool.Reset();
    SemanticMaterialCache.Empty();
    TraversalMaterialCache = nullptr;
}

// 메시 타입별로 명확한 화이트박스 색상(직관성 100%)을 지정!
FLinearColor ARaidRoomActor::ResolveSemanticTintForType(int32 MeshType) const
{
    FLinearColor Tint = FLinearColor::White;
    if (MeshType == 0) Tint = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f); // 바닥 (어두운 회색)
    else if (MeshType == 1) Tint = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f); // 벽 (조금 밝은 회색)
    else if (MeshType == 2) Tint = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); // 건물/매스 (회색)
    else if (MeshType == 3) Tint = FLinearColor(0.8f, 0.7f, 0.3f, 1.0f); // 소품 (노란빛)
    else if (MeshType == 6) Tint = FLinearColor(0.15f, 0.4f, 0.15f, 1.0f); // 나무 (녹색)
    else if (MeshType == 7) Tint = FLinearColor(0.3f, 0.6f, 0.2f, 1.0f); // 풀/덤불 (밝은 녹색)
    else if (MeshType == 8) Tint = FLinearColor(0.4f, 0.25f, 0.15f, 1.0f); // 돌/바위 (갈색)

    // 환경(테마)별 약간의 톤 변화 (색깔이 너무 왜곡되지 않게 살짝만 적용)
    const bool bUrban = NodeRow.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase);
    const bool bVillage = NodeRow.EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
    if (bUrban) Tint *= FLinearColor(0.95f, 0.97f, 1.05f, 1.0f);
    else if (bVillage) Tint *= FLinearColor(1.05f, 0.98f, 0.90f, 1.0f);

    Tint.R = FMath::Clamp(Tint.R, 0.05f, 0.95f); Tint.G = FMath::Clamp(Tint.G, 0.05f, 0.95f); Tint.B = FMath::Clamp(Tint.B, 0.05f, 0.95f); Tint.A = 1.0f;
    return Tint;
}

UMaterialInterface* ARaidRoomActor::GetSemanticMaterialForType(int32 MeshType)
{
    if (!bUseSemanticWhiteboxColors) return nullptr;
    if (TObjectPtr<UMaterialInterface>* Found = SemanticMaterialCache.Find(MeshType)) return Found->Get();
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!BaseMaterial) return nullptr;
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    if (!MID) return nullptr;
    const FLinearColor Tint = ResolveSemanticTintForType(MeshType);
    MID->SetVectorParameterValue(TEXT("Color"), Tint); MID->SetVectorParameterValue(TEXT("BaseColor"), Tint); MID->SetVectorParameterValue(TEXT("Tint"), Tint);
    SemanticMaterialCache.Add(MeshType, MID);
    return MID;
}

UMaterialInterface* ARaidRoomActor::GetTraversalMaterial()
{
    if (!bUseSemanticWhiteboxColors) return nullptr;
    if (TraversalMaterialCache) return TraversalMaterialCache;
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!BaseMaterial) return nullptr;
    TraversalMaterialCache = UMaterialInstanceDynamic::Create(BaseMaterial, this);
    if (!TraversalMaterialCache) return nullptr;
    TraversalMaterialCache->SetVectorParameterValue(TEXT("Color"), TraversalTint); TraversalMaterialCache->SetVectorParameterValue(TEXT("BaseColor"), TraversalTint); TraversalMaterialCache->SetVectorParameterValue(TEXT("Tint"), TraversalTint);
    return TraversalMaterialCache;
}
