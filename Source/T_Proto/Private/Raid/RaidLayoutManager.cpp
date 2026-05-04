#include "Raid/RaidLayoutManager.h"
#include "Raid/RaidRoomActor.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/LevelNodeRow.h"
#include "Raid/RoomPrefabRegistry.h"
#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidWaveProfile.h"
#include "Raid/RaidEditorPipelineLibrary.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplineControlPoint.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PhysicsVolume.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#endif

namespace
{
    struct FRoadSplineWidthSample
    {
        FVector2D CenterXY = FVector2D::ZeroVector;
        float Radius = 0.0f;
    };

    struct FWaterBodySplineRule
    {
        TArray<FVector2D> PolygonXY;
        bool bAllowOnlyInside = false; // Ocean: true, Lake: false
    };

    void SetCVarIntIfExists(const TCHAR* Name, int32 Value)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            CVar->Set(Value, ECVF_SetByCode);
        }
    }

    void SetCVarFloatIfExists(const TCHAR* Name, float Value)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            CVar->Set(Value, ECVF_SetByCode);
        }
    }

    void ApplyRaidVsmGuardrailCVars()
    {
        // Dense open-world non-Nanite vegetation can overflow VSM marking jobs.
        // Keep high quality, but reduce the expensive non-Nanite coarse-page path.
        SetCVarIntIfExists(TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages"), 0);
        SetCVarIntIfExists(TEXT("r.Shadow.Virtual.MarkPixelPagesMipModeLocal"), 1);
        SetCVarIntIfExists(TEXT("r.Shadow.Virtual.NonNanite.UseRadiusThreshold"), 1);
        SetCVarFloatIfExists(TEXT("r.Shadow.Virtual.CoarsePagePixelThresholdDynamic"), 24.0f);
        SetCVarFloatIfExists(TEXT("r.Shadow.Virtual.CoarsePagePixelThresholdStatic"), 2.0f);
        SetCVarFloatIfExists(TEXT("r.Shadow.RadiusThreshold"), 0.02f);
    }

    EObjectFlags ResolveRaidSpawnObjectFlags(const UWorld* World)
    {
        return (World && World->IsGameWorld())
            ? (RF_Transient | RF_DuplicateTransient | RF_TextExportTransient)
            : RF_NoFlags;
    }

    double BytesToGiB(const uint64 Bytes)
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0 * 1024.0);
    }

    bool IsPrePieAutoBakeMemoryHealthy(FPlatformMemoryStats* OutStats = nullptr)
    {
        const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
        if (OutStats)
        {
            *OutStats = Stats;
        }

        // Pre-PIE auto-bake can spike memory heavily while generating room geometry and
        // landscape flatten blobs. Keep a safer headroom to avoid editor OOM crashes.
        constexpr uint64 MinAvailablePhysicalBytes = 4096ull * 1024ull * 1024ull; // 4.0 GiB
        constexpr uint64 MinAvailableVirtualBytes = 2048ull * 1024ull * 1024ull;  // 2.0 GiB
        return Stats.AvailablePhysical >= MinAvailablePhysicalBytes &&
               Stats.AvailableVirtual >= MinAvailableVirtualBytes;
    }

    int32 CountRoomTaggedGeometry(ARaidRoomActor* Room, int32 MeshType)
    {
        if (!IsValid(Room))
        {
            return 0;
        }

        const FName MeshTypeTag(*FString::Printf(TEXT("MeshType_%d"), MeshType));
        int32 Count = 0;

        TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> RoomISMComponents(Room);
        for (UHierarchicalInstancedStaticMeshComponent* ISMC : RoomISMComponents)
        {
            if (!IsValid(ISMC) || !ISMC->ComponentTags.Contains(MeshTypeTag))
            {
                continue;
            }

            Count += FMath::Max(0, ISMC->GetInstanceCount());
        }

        TArray<AActor*> AttachedActors;
        Room->GetAttachedActors(AttachedActors, true, true);
        for (AActor* AttachedActor : AttachedActors)
        {
            if (!IsValid(AttachedActor))
            {
                continue;
            }

            bool bTagged = AttachedActor->ActorHasTag(MeshTypeTag);
            if (!bTagged)
            {
                TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(AttachedActor);
                for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
                {
                    if (IsValid(PrimitiveComponent) && PrimitiveComponent->ComponentTags.Contains(MeshTypeTag))
                    {
                        bTagged = true;
                        break;
                    }
                }
            }

            if (bTagged)
            {
                ++Count;
            }
        }

        return Count;
    }

    int32 CountRuntimeUsableVariationsForChannel(const FModularMeshKit* ThemeKit, ERaidVariationOffsetChannel Channel)
    {
        if (!ThemeKit)
        {
            return 0;
        }

        TArray<FMeshVariation> Variations;
        ThemeKit->GetEffectiveVariationsForChannel(Channel, Variations);
        if (Variations.Num() <= 0)
        {
            ThemeKit->GetAllRawVariationsForChannel(Channel, Variations);
        }

        static TMap<FSoftObjectPath, bool> RuntimeLoadabilityCache;
        int32 UsableCount = 0;
        for (const FMeshVariation& Variation : Variations)
        {
            const FSoftObjectPath MeshPath = Variation.Mesh.ToSoftObjectPath();
            const FSoftObjectPath BlueprintPath = Variation.BlueprintPrefab.ToSoftObjectPath();
            const FSoftObjectPath AssetPath = !BlueprintPath.IsNull() ? BlueprintPath : MeshPath;
            if (AssetPath.IsNull())
            {
                continue;
            }

            bool bLoadable = false;
            if (const bool* Cached = RuntimeLoadabilityCache.Find(AssetPath))
            {
                bLoadable = *Cached;
            }
            else
            {
                if (!BlueprintPath.IsNull())
                {
                    bLoadable = Variation.BlueprintPrefab.LoadSynchronous() != nullptr;
                }
                else
                {
                    bLoadable = Variation.Mesh.LoadSynchronous() != nullptr;
                }
                RuntimeLoadabilityCache.Add(AssetPath, bLoadable);
            }

            if (bLoadable)
            {
                ++UsableCount;
            }
        }

        return UsableCount;
    }

    bool BuildWorldFootprintFromLocalBounds(const FBox& LocalBounds, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!LocalBounds.IsValid)
        {
            return false;
        }

        const FVector Min = LocalBounds.Min;
        const FVector Max = LocalBounds.Max;
        const FVector LocalCorners[8] =
        {
            FVector(Min.X, Min.Y, Min.Z),
            FVector(Min.X, Min.Y, Max.Z),
            FVector(Min.X, Max.Y, Min.Z),
            FVector(Min.X, Max.Y, Max.Z),
            FVector(Max.X, Min.Y, Min.Z),
            FVector(Max.X, Min.Y, Max.Z),
            FVector(Max.X, Max.Y, Min.Z),
            FVector(Max.X, Max.Y, Max.Z)
        };

        FBox2D Footprint(EForceInit::ForceInit);
        for (const FVector& Corner : LocalCorners)
        {
            const FVector WorldCorner = WorldTransform.TransformPosition(Corner);
            Footprint += FVector2D(WorldCorner.X, WorldCorner.Y);
        }

        if (!Footprint.bIsValid)
        {
            return false;
        }

        OutFootprint = Footprint;
        return true;
    }

    bool TryBuildFootprintFromStaticMesh(const UStaticMesh* Mesh, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!IsValid(Mesh))
        {
            return false;
        }

        return BuildWorldFootprintFromLocalBounds(Mesh->GetBoundingBox(), WorldTransform, OutFootprint);
    }

    bool TryBuildFootprintFromActor(const AActor* Actor, FBox2D& OutFootprint)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        const FBox ActorBounds = Actor->GetComponentsBoundingBox(false);
        if (!ActorBounds.IsValid)
        {
            return false;
        }

        OutFootprint = FBox2D(
            FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y),
            FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y));
        return OutFootprint.bIsValid;
    }

    bool IsFootprintOverlappingAny(const TArray<FBox2D>& ExistingFootprints, const FBox2D& CandidateFootprint, float Padding)
    {
        if (!CandidateFootprint.bIsValid)
        {
            return false;
        }

        FBox2D ExpandedCandidate = CandidateFootprint;
        if (Padding > 0.0f)
        {
            ExpandedCandidate.Min -= FVector2D(Padding, Padding);
            ExpandedCandidate.Max += FVector2D(Padding, Padding);
        }

        for (const FBox2D& Existing : ExistingFootprints)
        {
            if (Existing.bIsValid && Existing.Intersect(ExpandedCandidate))
            {
                return true;
            }
        }

        return false;
    }

    bool IsTreeLikeName(const FString& InName)
    {
        const FString Lower = InName.ToLower();
        static const TCHAR* Keywords[] = {
            TEXT("tree"), TEXT("sapling"), TEXT("pine"), TEXT("oak"), TEXT("beech"),
            TEXT("birch"), TEXT("fir"), TEXT("spruce"), TEXT("palm"), TEXT("cypress"),
            TEXT("willow"), TEXT("trunk")
        };

        for (const TCHAR* Keyword : Keywords)
        {
            if (Lower.Contains(Keyword))
            {
                return true;
            }
        }
        return false;
    }

    bool IsTreeLikeVariation(const FMeshVariation& Variation)
    {
        if (Variation.Mesh.IsNull())
        {
            return false;
        }

        const FString PathString = Variation.Mesh.ToSoftObjectPath().ToString();
        return IsTreeLikeName(PathString);
    }

    bool ClusterContainsTreeLikeVariation(const FMeshCluster& Cluster)
    {
        for (const FMeshVariation& Variation : Cluster.Variations)
        {
            if (IsTreeLikeVariation(Variation))
            {
                return true;
            }
        }
        return false;
    }

    bool IsLikelyWindScalarParamName(const FString& LowerParamName)
    {
        return
            LowerParamName.Contains(TEXT("wind")) ||
            LowerParamName.Contains(TEXT("phase")) ||
            LowerParamName.Contains(TEXT("gust")) ||
            LowerParamName.Contains(TEXT("sway")) ||
            LowerParamName.Contains(TEXT("bend")) ||
            LowerParamName.Contains(TEXT("offset"));
    }

    void GatherLikelyWindScalarParams(UMaterialInterface* Material, TArray<FName>& OutParamNames)
    {
        if (!Material)
        {
            return;
        }

        TArray<FMaterialParameterInfo> ScalarInfos;
        TArray<FGuid> ScalarIds;
        Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
        for (const FMaterialParameterInfo& Info : ScalarInfos)
        {
            const FString LowerName = Info.Name.ToString().ToLower();
            if (IsLikelyWindScalarParamName(LowerName))
            {
                OutParamNames.AddUnique(Info.Name);
            }
        }
    }

    void ApplyWindPhaseDesync(UStaticMeshComponent* MeshComp, FRandomStream& Stream, bool bCreateMIDPerTree)
    {
        if (!IsValid(MeshComp))
        {
            return;
        }

        // Cheap path for materials using custom primitive data.
        MeshComp->SetCustomPrimitiveDataFloat(0, Stream.FRandRange(0.0f, 1.0f));
        MeshComp->SetCustomPrimitiveDataFloat(1, Stream.FRandRange(0.0f, 6.283185f));

        if (!bCreateMIDPerTree)
        {
            return;
        }

        // Fallback path for materials exposing scalar wind-phase parameters.
        static const FName FallbackParamNames[] = {
            TEXT("WindPhaseOffset"),
            TEXT("WindPhase"),
            TEXT("WindOffset"),
            TEXT("WindTimeOffset"),
            TEXT("WindVariation"),
            TEXT("PerInstanceRandom"),
            TEXT("TreeWindOffset"),
            TEXT("GustPhase")
        };

        const float RandomPhase = Stream.FRandRange(-1.0f, 1.0f);
        const int32 MaterialCount = MeshComp->GetNumMaterials();
        for (int32 MatIndex = 0; MatIndex < MaterialCount; ++MatIndex)
        {
            UMaterialInterface* BaseMat = MeshComp->GetMaterial(MatIndex);
            if (!BaseMat)
            {
                continue;
            }

            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BaseMat);
            if (!MID)
            {
                MID = UMaterialInstanceDynamic::Create(BaseMat, MeshComp);
                if (!MID)
                {
                    continue;
                }
                MeshComp->SetMaterial(MatIndex, MID);
            }

            TArray<FName> ParamNamesToSet;
            GatherLikelyWindScalarParams(BaseMat, ParamNamesToSet);
            if (ParamNamesToSet.Num() == 0)
            {
                for (const FName ParamName : FallbackParamNames)
                {
                    ParamNamesToSet.Add(ParamName);
                }
            }

            for (const FName ParamName : ParamNamesToSet)
            {
                MID->SetScalarParameterValue(ParamName, RandomPhase);
            }
        }
    }

    bool IsWaterActorOrComponent(const AActor* HitActor, const UPrimitiveComponent* HitComp)
    {
        if (HitActor)
        {
            if (const APhysicsVolume* PhysicsVolume = Cast<APhysicsVolume>(HitActor))
            {
                if (PhysicsVolume->bWaterVolume) return true;
            }
            if (HitActor->ActorHasTag(TEXT("Water"))) return true;

            const FString ActorClass = HitActor->GetClass()->GetName();
            if (ActorClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase) ||
                ActorClass.Contains(TEXT("Lake"), ESearchCase::IgnoreCase) ||
                ActorClass.Contains(TEXT("River"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        if (HitComp)
        {
            if (HitComp->ComponentHasTag(TEXT("Water"))) return true;

            const FString CompClass = HitComp->GetClass()->GetName();
            if (CompClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    bool IsInsideWaterPhysicsVolume(UWorld* World, const FVector& Location, float SphereRadius)
    {
        if (!World) return false;

        for (FConstPhysicsVolumeIterator It = World->GetNonDefaultPhysicsVolumeIterator(); It; ++It)
        {
            const TWeakObjectPtr<APhysicsVolume>& WeakVolume = *It;
            const APhysicsVolume* PhysicsVolume = WeakVolume.Get();
            if (!PhysicsVolume || !PhysicsVolume->bWaterVolume) continue;
            if (PhysicsVolume->EncompassesPoint(Location, SphereRadius)) return true;
        }
        return false;
    }

    bool IsHitWaterLocation(const FHitResult& Hit)
    {
        return IsWaterActorOrComponent(Hit.GetActor(), Hit.GetComponent());
    }

    bool IsLandscapeLikeHitForLayout(const FHitResult& Hit)
    {
        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        if (!HitActor && !HitComp) return false;

        const FString ActorClass = HitActor ? HitActor->GetClass()->GetName() : FString();
        const FString CompClass = HitComp ? HitComp->GetClass()->GetName() : FString();
        return
            ActorClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            ActorClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase);
    }

    bool IsObstacleTaggedHit(const AActor* HitActor, const UPrimitiveComponent* HitComp)
    {
        const bool bObstacleComp =
            HitComp &&
            (HitComp->ComponentTags.Contains(TEXT("MeshType_2")) || HitComp->ComponentTags.Contains(TEXT("ObstacleSplineBlocker")));
        const bool bObstacleActor =
            HitActor &&
            (HitActor->ActorHasTag(TEXT("MeshType_2")) || HitActor->ActorHasTag(TEXT("ObstacleBlueprint")));
        return bObstacleComp || bObstacleActor;
    }

    bool IsPointNearObstacleTaggedGeometry(UWorld* World, const FVector& Location, float Radius)
    {
        if (!World || Radius <= 1.0f)
        {
            return false;
        }

        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidRoadSplineObstacleCheck), false);
        QueryParams.bTraceComplex = false;

        TArray<FOverlapResult> Overlaps;
        if (!World->OverlapMultiByObjectType(
            Overlaps,
            Location,
            FQuat::Identity,
            ObjQuery,
            FCollisionShape::MakeSphere(Radius),
            QueryParams))
        {
            return false;
        }

        for (const FOverlapResult& Overlap : Overlaps)
        {
            const AActor* HitActor = Overlap.GetActor();
            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
            if (!IsValid(HitComp))
            {
                continue;
            }

            if (IsObstacleTaggedHit(HitActor, HitComp))
            {
                return true;
            }
        }

        return false;
    }

    bool TryResolveSingleGroundHitAtPoint(
        UWorld* World,
        const FVector& XYLocation,
        bool bPreferLandscape,
        bool bIgnoreRaidRooms,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit)
    {
        if (!World)
        {
            return false;
        }

        TArray<FHitResult> Hits;
        const FVector TraceStart(XYLocation.X, XYLocation.Y, 120000.0f);
        const FVector TraceEnd(XYLocation.X, XYLocation.Y, -120000.0f);
        if (!World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
        {
            return false;
        }

        const FHitResult* BestLandscape = nullptr;
        const FHitResult* BestGeneral = nullptr;

        for (const FHitResult& Hit : Hits)
        {
            if (!Hit.bBlockingHit)
            {
                continue;
            }

            if (IsHitWaterLocation(Hit))
            {
                continue;
            }

            if (bIgnoreRaidRooms && Hit.GetActor() && Hit.GetActor()->IsA(ARaidRoomActor::StaticClass()))
            {
                continue;
            }

            if (!BestGeneral || Hit.Distance < BestGeneral->Distance)
            {
                BestGeneral = &Hit;
            }
            if (IsLandscapeLikeHitForLayout(Hit))
            {
                if (!BestLandscape || Hit.Distance < BestLandscape->Distance)
                {
                    BestLandscape = &Hit;
                }
            }
        }

        const FHitResult* SelectedHit = (bPreferLandscape && BestLandscape) ? BestLandscape : BestGeneral;
        if (!SelectedHit)
        {
            return false;
        }

        OutHit = *SelectedHit;
        return true;
    }

    bool TryResolveGroundHit(
        UWorld* World,
        const FVector& XYLocation,
        bool bPreferLandscape,
        bool bIgnoreRaidRooms,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit,
        float SupportSampleRadius = 0.0f,
        int32 SupportRadialSampleCount = 0,
        bool bRequireCenterHit = false)
    {
        if (!World)
        {
            return false;
        }

        FHitResult CenterHit;
        const bool bHasCenterHit = TryResolveSingleGroundHitAtPoint(
            World,
            XYLocation,
            bPreferLandscape,
            bIgnoreRaidRooms,
            QueryParams,
            CenterHit);

        if (!bHasCenterHit && bRequireCenterHit)
        {
            return false;
        }

        if ((!bHasCenterHit && (SupportSampleRadius <= 1.0f || SupportRadialSampleCount <= 0)) ||
            (SupportSampleRadius <= 1.0f || SupportRadialSampleCount <= 0))
        {
            if (!bHasCenterHit)
            {
                return false;
            }
            OutHit = CenterHit;
            return true;
        }

        const int32 SafeRadialSampleCount = FMath::Clamp(SupportRadialSampleCount, 1, 12);
        TArray<FHitResult> SupportHits;
        SupportHits.Reserve(SafeRadialSampleCount + 1);
        if (bHasCenterHit)
        {
            SupportHits.Add(CenterHit);
        }

        FHitResult FallbackHit;
        bool bHasFallbackHit = false;
        if (bHasCenterHit)
        {
            FallbackHit = CenterHit;
            bHasFallbackHit = true;
        }

        for (int32 SampleIndex = 0; SampleIndex < SafeRadialSampleCount; ++SampleIndex)
        {
            const float Angle = (2.0f * PI * static_cast<float>(SampleIndex)) / static_cast<float>(SafeRadialSampleCount);
            const FVector SampleXY = XYLocation + FVector(
                FMath::Cos(Angle) * SupportSampleRadius,
                FMath::Sin(Angle) * SupportSampleRadius,
                0.0f);

            FHitResult SampleHit;
            if (!TryResolveSingleGroundHitAtPoint(
                World,
                SampleXY,
                bPreferLandscape,
                bIgnoreRaidRooms,
                QueryParams,
                SampleHit))
            {
                continue;
            }

            SupportHits.Add(SampleHit);
            if (!bHasFallbackHit)
            {
                FallbackHit = SampleHit;
                bHasFallbackHit = true;
            }
        }

        if (!bHasCenterHit)
        {
            if (!bHasFallbackHit)
            {
                return false;
            }
            OutHit = FallbackHit;
            return true;
        }

        OutHit = CenterHit;

        if (SupportHits.Num() <= 1)
        {
            return true;
        }

        TArray<float> SupportZ;
        SupportZ.Reserve(SupportHits.Num());
        FVector SupportNormal = FVector::ZeroVector;
        int32 NormalCount = 0;
        for (const FHitResult& SupportHit : SupportHits)
        {
            SupportZ.Add(SupportHit.ImpactPoint.Z);
            if (!SupportHit.ImpactNormal.IsNearlyZero())
            {
                SupportNormal += SupportHit.ImpactNormal;
                ++NormalCount;
            }
        }

        SupportZ.Sort();
        const float MedianSupportZ = SupportZ[SupportZ.Num() / 2];
        OutHit.ImpactPoint.Z = MedianSupportZ;
        OutHit.Location.Z = MedianSupportZ;

        if (NormalCount > 0)
        {
            OutHit.ImpactNormal = SupportNormal.GetSafeNormal();
        }

        return true;
    }

    bool MeasureGroundSupportStats(
        UWorld* World,
        const FVector& XYLocation,
        bool bPreferLandscape,
        bool bIgnoreRaidRooms,
        const FCollisionQueryParams& QueryParams,
        float SampleRadius,
        int32 RadialSampleCount,
        float& OutHitRatio,
        float& OutHeightRange)
    {
        OutHitRatio = 0.0f;
        OutHeightRange = 0.0f;

        if (!World)
        {
            return false;
        }

        const int32 SafeRadialSamples = FMath::Clamp(RadialSampleCount, 1, 16);
        const int32 TotalSamples = SafeRadialSamples + 1; // center + ring

        TArray<float> Heights;
        Heights.Reserve(TotalSamples);

        auto TryCollectHeightAt = [&](const FVector& Point) -> void
        {
            FHitResult Hit;
            if (TryResolveSingleGroundHitAtPoint(
                World,
                Point,
                bPreferLandscape,
                bIgnoreRaidRooms,
                QueryParams,
                Hit))
            {
                Heights.Add(Hit.ImpactPoint.Z);
            }
        };

        TryCollectHeightAt(XYLocation);

        for (int32 SampleIndex = 0; SampleIndex < SafeRadialSamples; ++SampleIndex)
        {
            const float Angle = (2.0f * PI * (float)SampleIndex) / (float)SafeRadialSamples;
            const FVector RingPoint = XYLocation + FVector(FMath::Cos(Angle) * SampleRadius, FMath::Sin(Angle) * SampleRadius, 0.0f);
            TryCollectHeightAt(RingPoint);
        }

        if (Heights.Num() <= 0)
        {
            return false;
        }

        OutHitRatio = (float)Heights.Num() / (float)FMath::Max(1, TotalSamples);
        float MinHeight = TNumericLimits<float>::Max();
        float MaxHeight = -TNumericLimits<float>::Max();
        for (const float HeightZ : Heights)
        {
            MinHeight = FMath::Min(MinHeight, HeightZ);
            MaxHeight = FMath::Max(MaxHeight, HeightZ);
        }

        if (MinHeight < TNumericLimits<float>::Max() && MaxHeight > -TNumericLimits<float>::Max())
        {
            OutHeightRange = FMath::Max(0.0f, MaxHeight - MinHeight);
        }
        return true;
    }

    bool IsLocationNearWater(
        UWorld* World,
        const FVector& Location,
        float AvoidanceRadius,
        const FCollisionQueryParams& QueryParams,
        bool bUseWaterVolumeProbe)
    {
        if (!World || AvoidanceRadius <= 1.0f)
        {
            return false;
        }

        if (bUseWaterVolumeProbe && IsInsideWaterPhysicsVolume(World, Location, AvoidanceRadius))
        {
            return true;
        }

        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

        TArray<FOverlapResult> Overlaps;
        if (World->OverlapMultiByObjectType(
            Overlaps,
            Location,
            FQuat::Identity,
            ObjQuery,
            FCollisionShape::MakeSphere(AvoidanceRadius),
            QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                const AActor* Actor = Overlap.GetActor();
                const UPrimitiveComponent* Comp = Overlap.Component.Get();
                if (IsWaterActorOrComponent(Actor, Comp))
                {
                    return true;
                }
            }
        }

        constexpr int32 RingSamples = 6;
        for (int32 Index = 0; Index <= RingSamples; ++Index)
        {
            const float Dist = (Index == 0) ? 0.0f : AvoidanceRadius;
            const float Angle = (Index == 0) ? 0.0f : (2.0f * PI * (float)(Index - 1) / (float)RingSamples);
            const FVector SamplePoint = Location + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0f);

            if (bUseWaterVolumeProbe && IsInsideWaterPhysicsVolume(World, SamplePoint, 30.0f))
            {
                return true;
            }

            FHitResult Hit;
            if (World->LineTraceSingleByChannel(
                Hit,
                SamplePoint + FVector(0.0f, 0.0f, 100000.0f),
                SamplePoint + FVector(0.0f, 0.0f, -100000.0f),
                ECC_WorldStatic,
                QueryParams))
            {
                if (IsHitWaterLocation(Hit))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool IsRoadLikeTaggedActorOrComponent(const AActor* HitActor, const UPrimitiveComponent* HitComp)
    {
        if (IsWaterActorOrComponent(HitActor, HitComp))
        {
            return false;
        }

        if (HitActor && HitActor->ActorHasTag(TEXT("RaidRoadSpline")))
        {
            return true;
        }

        auto HasAnyKeyword = [](const FString& Source, const TArray<const TCHAR*>& Keywords) -> bool
            {
                if (Source.IsEmpty())
                {
                    return false;
                }

                const FString Lower = Source.ToLower();
                for (const TCHAR* Keyword : Keywords)
                {
                    if (Lower.Contains(Keyword))
                    {
                        return true;
                    }
                }
                return false;
            };

        auto HasRoadLikeTag = [&](const TArray<FName>& Tags) -> bool
            {
                for (const FName& Tag : Tags)
                {
                    static const TArray<const TCHAR*> TagKeywords =
                    {
                        TEXT("road"),
                        TEXT("path"),
                        TEXT("trail"),
                        TEXT("street")
                    };
                    if (HasAnyKeyword(Tag.ToString(), TagKeywords))
                    {
                        return true;
                    }
                }
                return false;
            };

        static const TArray<const TCHAR*> StrongRoadKeywords =
        {
            TEXT("landscapespline"),
            TEXT("landscape_spline"),
            TEXT("splinemeshesactor"),
            TEXT("road"),
            TEXT("street"),
            TEXT("lane"),
            TEXT("asphalt"),
            TEXT("highway")
        };

        static const TArray<const TCHAR*> SoftRoadKeywords =
        {
            TEXT("path"),
            TEXT("trail"),
            TEXT("track")
        };

        const FString ActorClass = HitActor ? HitActor->GetClass()->GetName() : FString();
        const FString CompClass = HitComp ? HitComp->GetClass()->GetName() : FString();
        const FString ActorName = HitActor ? HitActor->GetName() : FString();
        const FString CompName = HitComp ? HitComp->GetName() : FString();

        const bool bStrongRoadKeywordMatch =
            HasAnyKeyword(ActorClass, StrongRoadKeywords) ||
            HasAnyKeyword(CompClass, StrongRoadKeywords) ||
            HasAnyKeyword(ActorName, StrongRoadKeywords) ||
            HasAnyKeyword(CompName, StrongRoadKeywords);

        const bool bSoftRoadKeywordMatch =
            HasAnyKeyword(ActorClass, SoftRoadKeywords) ||
            HasAnyKeyword(CompClass, SoftRoadKeywords) ||
            HasAnyKeyword(ActorName, SoftRoadKeywords) ||
            HasAnyKeyword(CompName, SoftRoadKeywords);

        const bool bRoadTagMatch =
            (HitActor && HasRoadLikeTag(HitActor->Tags)) ||
            (HitComp && HasRoadLikeTag(HitComp->ComponentTags));

        const FString CompClassLower = CompClass.ToLower();
        const bool bSplineMeshLikeComp =
            CompClassLower.Contains(TEXT("splinemesh")) ||
            CompClassLower.Contains(TEXT("spline"));

        if (bStrongRoadKeywordMatch)
        {
            return true;
        }

        if (bRoadTagMatch && bSplineMeshLikeComp)
        {
            return true;
        }

        return bSoftRoadKeywordMatch && (bRoadTagMatch || bSplineMeshLikeComp);
    }

    bool IsRoadLikeSplineComponent(const USplineComponent* SplineComp)
    {
        if (!IsValid(SplineComp))
        {
            return false;
        }

        const AActor* Owner = SplineComp->GetOwner();
        if (IsWaterActorOrComponent(Owner, SplineComp))
        {
            return false;
        }

        if (IsRoadLikeTaggedActorOrComponent(Owner, nullptr))
        {
            return true;
        }

        const FString SplineClassLower = SplineComp->GetClass()->GetName().ToLower();
        if (SplineClassLower.Contains(TEXT("landscapespline")) || SplineClassLower.Contains(TEXT("road")))
        {
            return true;
        }

        for (const FName& Tag : SplineComp->ComponentTags)
        {
            const FString TagLower = Tag.ToString().ToLower();
            if (TagLower.Contains(TEXT("road")) || TagLower.Contains(TEXT("path")) || TagLower.Contains(TEXT("trail")) || TagLower.Contains(TEXT("street")))
            {
                return true;
            }
        }

        return false;
    }

    void GatherRoadSplineComponents(UWorld* World, TArray<const USplineComponent*>& OutRoadSplines)
    {
        OutRoadSplines.Reset();
        if (!World)
        {
            return;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor))
            {
                continue;
            }

            TInlineComponentArray<USplineComponent*> SplineComps(Actor);
            for (USplineComponent* SplineComp : SplineComps)
            {
                if (IsRoadLikeSplineComponent(SplineComp))
                {
                    OutRoadSplines.Add(SplineComp);
                }
            }
        }
    }

    void GatherLandscapeSplineRoadWidthSamples(UWorld* World, TArray<FRoadSplineWidthSample>& OutSamples)
    {
        OutSamples.Reset();
        if (!World)
        {
            return;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor))
            {
                continue;
            }

            TInlineComponentArray<ULandscapeSplinesComponent*> LandscapeSplineComps(Actor);
            for (ULandscapeSplinesComponent* LandscapeSplineComp : LandscapeSplineComps)
            {
                if (!IsValid(LandscapeSplineComp))
                {
                    continue;
                }

                const FTransform SplineToWorld = LandscapeSplineComp->GetComponentTransform();

                const TArray<TObjectPtr<ULandscapeSplineControlPoint>>& ControlPoints = LandscapeSplineComp->GetControlPoints();
                for (const TObjectPtr<ULandscapeSplineControlPoint>& ControlPoint : ControlPoints)
                {
                    if (!IsValid(ControlPoint))
                    {
                        continue;
                    }

                    const FVector CenterWorld = SplineToWorld.TransformPosition(ControlPoint->Location);
                    const float SideFalloffScale = FMath::Max(ControlPoint->LeftSideFalloffFactor, ControlPoint->RightSideFalloffFactor);
                    const float EffectiveRadius = FMath::Max(20.0f, ControlPoint->Width + ControlPoint->SideFalloff * SideFalloffScale);
                    FRoadSplineWidthSample Sample;
                    Sample.CenterXY = FVector2D(CenterWorld.X, CenterWorld.Y);
                    Sample.Radius = EffectiveRadius;
                    OutSamples.Add(Sample);
                }

                const TArray<TObjectPtr<ULandscapeSplineSegment>>& Segments = LandscapeSplineComp->GetSegments();
                for (const TObjectPtr<ULandscapeSplineSegment>& Segment : Segments)
                {
                    if (!IsValid(Segment))
                    {
                        continue;
                    }

                    const TArray<FLandscapeSplineInterpPoint>& Points = Segment->GetPoints();
                    for (const FLandscapeSplineInterpPoint& Point : Points)
                    {
                        const FVector CenterWorld = SplineToWorld.TransformPosition(Point.Center);
                        const FVector LeftWorld = SplineToWorld.TransformPosition(Point.Left);
                        const FVector RightWorld = SplineToWorld.TransformPosition(Point.Right);
                        const FVector FalloffLeftWorld = SplineToWorld.TransformPosition(Point.FalloffLeft);
                        const FVector FalloffRightWorld = SplineToWorld.TransformPosition(Point.FalloffRight);

                        const float HalfWidth = FMath::Max(
                            FVector2D::Distance(FVector2D(CenterWorld.X, CenterWorld.Y), FVector2D(LeftWorld.X, LeftWorld.Y)),
                            FVector2D::Distance(FVector2D(CenterWorld.X, CenterWorld.Y), FVector2D(RightWorld.X, RightWorld.Y)));
                        const float SideFalloff = FMath::Max(
                            FVector2D::Distance(FVector2D(CenterWorld.X, CenterWorld.Y), FVector2D(FalloffLeftWorld.X, FalloffLeftWorld.Y)),
                            FVector2D::Distance(FVector2D(CenterWorld.X, CenterWorld.Y), FVector2D(FalloffRightWorld.X, FalloffRightWorld.Y)));

                        FRoadSplineWidthSample Sample;
                        Sample.CenterXY = FVector2D(CenterWorld.X, CenterWorld.Y);
                        Sample.Radius = FMath::Max(20.0f, FMath::Max(HalfWidth, SideFalloff));
                        OutSamples.Add(Sample);
                    }
                }
            }
        }
    }

    bool IsLocationNearLandscapeSplineWidthSamples(
        const FVector& Location,
        float AvoidanceRadius,
        const TArray<FRoadSplineWidthSample>& Samples)
    {
        if (AvoidanceRadius <= 1.0f || Samples.Num() == 0)
        {
            return false;
        }

        const FVector2D CandidateXY(Location.X, Location.Y);
        for (const FRoadSplineWidthSample& Sample : Samples)
        {
            const float EffectiveRadius = FMath::Max(1.0f, Sample.Radius + AvoidanceRadius);
            if (FVector2D::DistSquared(CandidateXY, Sample.CenterXY) <= FMath::Square(EffectiveRadius))
            {
                return true;
            }
        }
        return false;
    }

    bool IsPointInsidePolygonXY(const FVector2D& Point, const TArray<FVector2D>& Polygon)
    {
        const int32 Count = Polygon.Num();
        if (Count < 3)
        {
            return false;
        }

        bool bInside = false;
        for (int32 I = 0, J = Count - 1; I < Count; J = I++)
        {
            const FVector2D& A = Polygon[I];
            const FVector2D& B = Polygon[J];
            const float Denominator = (B.Y - A.Y);
            if (FMath::IsNearlyZero(Denominator))
            {
                continue;
            }

            const bool bCross = ((A.Y > Point.Y) != (B.Y > Point.Y)) &&
                (Point.X < (B.X - A.X) * (Point.Y - A.Y) / Denominator + A.X);
            if (bCross)
            {
                bInside = !bInside;
            }
        }
        return bInside;
    }

    float DistanceSqPointToSegmentXY(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
    {
        const FVector2D AB = B - A;
        const float Denom = AB.SizeSquared();
        if (Denom <= KINDA_SMALL_NUMBER)
        {
            return FVector2D::DistSquared(Point, A);
        }

        const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / Denom, 0.0f, 1.0f);
        const FVector2D Closest = A + AB * T;
        return FVector2D::DistSquared(Point, Closest);
    }

    float DistanceToPolygonEdgeXY(const FVector2D& Point, const TArray<FVector2D>& Polygon)
    {
        const int32 Count = Polygon.Num();
        if (Count < 2)
        {
            return TNumericLimits<float>::Max();
        }

        float MinDistSq = TNumericLimits<float>::Max();
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector2D& A = Polygon[I];
            const FVector2D& B = Polygon[(I + 1) % Count];
            MinDistSq = FMath::Min(MinDistSq, DistanceSqPointToSegmentXY(Point, A, B));
        }
        return (MinDistSq < TNumericLimits<float>::Max()) ? FMath::Sqrt(MinDistSq) : TNumericLimits<float>::Max();
    }

    void GatherWaterBodySplineRules(UWorld* World, TArray<FWaterBodySplineRule>& OutRules)
    {
        OutRules.Reset();
        if (!World)
        {
            return;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor))
            {
                continue;
            }

            const FString ClassLower = Actor->GetClass()->GetName().ToLower();
            const bool bOcean = ClassLower.Contains(TEXT("waterbodyocean"));
            const bool bLake = ClassLower.Contains(TEXT("waterbodylake"));
            if (!bOcean && !bLake)
            {
                continue;
            }

            TInlineComponentArray<USplineComponent*> SplineComps(Actor);
            for (USplineComponent* SplineComp : SplineComps)
            {
                if (!IsValid(SplineComp))
                {
                    continue;
                }

                const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
                if (NumPoints < 3)
                {
                    continue;
                }

                FWaterBodySplineRule Rule;
                Rule.bAllowOnlyInside = bOcean;
                Rule.PolygonXY.Reserve(NumPoints);
                for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
                {
                    const FVector WorldPoint = SplineComp->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
                    Rule.PolygonXY.Add(FVector2D(WorldPoint.X, WorldPoint.Y));
                }

                if (Rule.PolygonXY.Num() >= 3)
                {
                    OutRules.Add(MoveTemp(Rule));
                }
            }
        }
    }

    bool IsLocationBlockedByWaterBodySplineRules(
        const FVector& Location,
        const TArray<FWaterBodySplineRule>& Rules,
        float EdgeBufferDistance,
        float OceanExtraBufferDistance,
        float LakeExtraBufferDistance)
    {
        if (Rules.Num() == 0)
        {
            return false;
        }

        const FVector2D XY(Location.X, Location.Y);
        const float SafeEdgeBuffer = FMath::Max(0.0f, EdgeBufferDistance);
        const float SafeOceanEdgeBuffer = SafeEdgeBuffer + FMath::Max(0.0f, OceanExtraBufferDistance);
        const float SafeLakeEdgeBuffer = SafeEdgeBuffer + FMath::Max(0.0f, LakeExtraBufferDistance);

        bool bHasOceanRule = false;
        bool bInsideAnyOcean = false;

        for (const FWaterBodySplineRule& Rule : Rules)
        {
            if (Rule.PolygonXY.Num() < 3)
            {
                continue;
            }

            const bool bInside = IsPointInsidePolygonXY(XY, Rule.PolygonXY);
            const float DistToEdge = DistanceToPolygonEdgeXY(XY, Rule.PolygonXY);
            const float EffectiveEdgeBuffer = Rule.bAllowOnlyInside ? SafeOceanEdgeBuffer : SafeLakeEdgeBuffer;
            const bool bNearEdge = EffectiveEdgeBuffer > 0.0f && DistToEdge <= EffectiveEdgeBuffer;

            if (Rule.bAllowOnlyInside)
            {
                bHasOceanRule = true;
                if (bInside)
                {
                    bInsideAnyOcean = true;
                    if (bNearEdge)
                    {
                        return true;
                    }
                }
            }
            else
            {
                // Lake: only outside should spawn.
                if (bInside || bNearEdge)
                {
                    return true;
                }
            }
        }

        // Ocean: outside spline should not spawn.
        if (bHasOceanRule && !bInsideAnyOcean)
        {
            return true;
        }

        return false;
    }

    float EstimateWaterRuleAllowedRatioOnBounds(
        const FBox2D& Bounds,
        const TArray<FWaterBodySplineRule>& Rules,
        float EdgeBufferDistance,
        float OceanExtraBufferDistance,
        float LakeExtraBufferDistance,
        int32 GridResolution)
    {
        if (!Bounds.bIsValid || Rules.Num() == 0)
        {
            return 1.0f;
        }

        const int32 Res = FMath::Clamp(GridResolution, 8, 128);
        int32 Total = 0;
        int32 Allowed = 0;
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Res);
                const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Res);
                const FVector Sample(
                    FMath::Lerp(Bounds.Min.X, Bounds.Max.X, U),
                    FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, V),
                    0.0f);
                ++Total;
                if (!IsLocationBlockedByWaterBodySplineRules(
                    Sample,
                    Rules,
                    EdgeBufferDistance,
                    OceanExtraBufferDistance,
                    LakeExtraBufferDistance))
                {
                    ++Allowed;
                }
            }
        }

        return Total > 0 ? (static_cast<float>(Allowed) / static_cast<float>(Total)) : 1.0f;
    }

    float HaltonSequence(int32 Index, int32 Base)
    {
        int32 SafeIndex = FMath::Max(1, Index);
        const int32 SafeBase = FMath::Max(2, Base);

        float Result = 0.0f;
        float Fraction = 1.0f / static_cast<float>(SafeBase);
        while (SafeIndex > 0)
        {
            Result += Fraction * static_cast<float>(SafeIndex % SafeBase);
            SafeIndex /= SafeBase;
            Fraction /= static_cast<float>(SafeBase);
        }
        return Result;
    }

    bool TryBuildFootprintFromPrimitiveComponent(const UPrimitiveComponent* PrimitiveComp, FBox2D& OutFootprint)
    {
        if (!IsValid(PrimitiveComp))
        {
            return false;
        }

        const FBox Bounds = PrimitiveComp->Bounds.GetBox();
        if (!Bounds.IsValid)
        {
            return false;
        }

        OutFootprint = FBox2D(
            FVector2D(Bounds.Min.X, Bounds.Min.Y),
            FVector2D(Bounds.Max.X, Bounds.Max.Y));
        return OutFootprint.bIsValid;
    }

    bool ShouldIgnoreActorForPreplacedFootprintCache(const AActor* Actor, const AActor* LayoutManager)
    {
        if (!IsValid(Actor))
        {
            return true;
        }

        if (Actor == LayoutManager || Actor->GetOwner() == LayoutManager)
        {
            return true;
        }

        if (Actor->IsA<ARaidLayoutManager>() || Actor->IsA<ARaidRoomActor>())
        {
            return true;
        }

        if (Actor->ActorHasTag(TEXT("RaidBackgroundScenery")) ||
            Actor->ActorHasTag(TEXT("RaidRoadSpline")) ||
            Actor->ActorHasTag(TEXT("RaidDoorBlocker")) ||
            Actor->ActorHasTag(TEXT("RaidRoomGenerated")))
        {
            return true;
        }

        if (IsWaterActorOrComponent(Actor, nullptr))
        {
            return true;
        }

        const FString ClassLower = Actor->GetClass()->GetName().ToLower();
        const bool bLandscapeLike =
            ClassLower.Contains(TEXT("landscape"));
        const bool bWaterSystemLike =
            ClassLower.Contains(TEXT("waterbody")) ||
            ClassLower.Contains(TEXT("waterzone")) ||
            ClassLower.Contains(TEXT("watermesh")) ||
            ClassLower.Contains(TEXT("waterbrush"));
        const bool bFoliageSystemLike =
            ClassLower.Contains(TEXT("instancedfoliageactor")) ||
            ClassLower.Contains(TEXT("foliageactor"));

        if (bLandscapeLike || bWaterSystemLike || bFoliageSystemLike)
        {
            return true;
        }

        return false;
    }

    bool IsPreplacedStaticMeshLikePrimitive(const UPrimitiveComponent* PrimitiveComp)
    {
        if (!IsValid(PrimitiveComp) || !PrimitiveComp->IsRegistered())
        {
            return false;
        }

        const AActor* Owner = PrimitiveComp->GetOwner();
        if (!IsValid(Owner))
        {
            return false;
        }

        if (IsWaterActorOrComponent(Owner, PrimitiveComp))
        {
            return false;
        }

        if (!Cast<const UStaticMeshComponent>(PrimitiveComp))
        {
            return false;
        }

        const FString CompClassLower = PrimitiveComp->GetClass()->GetName().ToLower();
        if (CompClassLower.Contains(TEXT("landscape")) || CompClassLower.Contains(TEXT("foliage")))
        {
            return false;
        }

        if (PrimitiveComp->Mobility == EComponentMobility::Movable)
        {
            return false;
        }

        if (!PrimitiveComp->IsCollisionEnabled() && !PrimitiveComp->IsVisible())
        {
            return false;
        }

        return PrimitiveComp->Bounds.SphereRadius > 20.0f;
    }

    bool IsPreplacedFootprintUsable(const FBox2D& Footprint)
    {
        if (!Footprint.bIsValid)
        {
            return false;
        }

        const FVector2D Size = Footprint.GetSize();
        const float MinDim = FMath::Min(Size.X, Size.Y);
        const float MaxDim = FMath::Max(Size.X, Size.Y);
        return MinDim > 8.0f && MaxDim <= 50000.0f;
    }

    void GatherPreplacedStaticMeshFootprints(UWorld* World, const AActor* LayoutManager, TArray<FBox2D>& OutFootprints)
    {
        OutFootprints.Reset();
        if (!World)
        {
            return;
        }

        OutFootprints.Reserve(1024);
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const AActor* Actor = *It;
            if (ShouldIgnoreActorForPreplacedFootprintCache(Actor, LayoutManager))
            {
                continue;
            }

            TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps(Actor);
            for (const UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
            {
                if (!IsPreplacedStaticMeshLikePrimitive(PrimitiveComp))
                {
                    continue;
                }

                FBox2D Footprint(EForceInit::ForceInit);
                if (TryBuildFootprintFromPrimitiveComponent(PrimitiveComp, Footprint))
                {
                    if (IsPreplacedFootprintUsable(Footprint))
                    {
                        OutFootprints.Add(Footprint);
                    }
                }
            }
        }
    }

    bool IsLocationNearFootprints(
        const FVector& Location,
        float AvoidanceRadius,
        const TArray<FBox2D>& Footprints)
    {
        if (AvoidanceRadius <= 1.0f || Footprints.Num() == 0)
        {
            return false;
        }

        const FVector2D XY(Location.X, Location.Y);
        for (const FBox2D& Footprint : Footprints)
        {
            if (!Footprint.bIsValid)
            {
                continue;
            }

            FBox2D Expanded = Footprint;
            Expanded.Min -= FVector2D(AvoidanceRadius, AvoidanceRadius);
            Expanded.Max += FVector2D(AvoidanceRadius, AvoidanceRadius);
            if (Expanded.IsInside(XY))
            {
                return true;
            }
        }

        return false;
    }

    bool IsRoomFootprintOverlappingFootprints(
        const FVector& CenterLocation,
        float RoomHalfExtent,
        float Padding,
        const TArray<FBox2D>& Footprints)
    {
        if (Footprints.Num() == 0)
        {
            return false;
        }

        const float SafeHalfExtent = FMath::Max(0.0f, RoomHalfExtent);
        const FBox2D RoomFootprint(
            FVector2D(CenterLocation.X - SafeHalfExtent, CenterLocation.Y - SafeHalfExtent),
            FVector2D(CenterLocation.X + SafeHalfExtent, CenterLocation.Y + SafeHalfExtent));

        return IsFootprintOverlappingAny(Footprints, RoomFootprint, FMath::Max(0.0f, Padding));
    }

    bool IsRoadLikePrimitiveComponent(const UPrimitiveComponent* PrimitiveComp)
    {
        if (!IsValid(PrimitiveComp))
        {
            return false;
        }

        const AActor* Owner = PrimitiveComp->GetOwner();
        if (IsWaterActorOrComponent(Owner, PrimitiveComp))
        {
            return false;
        }

        if (!IsRoadLikeTaggedActorOrComponent(Owner, PrimitiveComp))
        {
            return false;
        }

        const FString CompClassLower = PrimitiveComp->GetClass()->GetName().ToLower();
        const bool bSplineLikeComp =
            CompClassLower.Contains(TEXT("splinemesh")) ||
            CompClassLower.Contains(TEXT("spline")) ||
            PrimitiveComp->IsA<USplineMeshComponent>();
        if (bSplineLikeComp)
        {
            return true;
        }

        const FString OwnerClassLower = Owner ? Owner->GetClass()->GetName().ToLower() : FString();
        return OwnerClassLower.Contains(TEXT("landscapespline")) || OwnerClassLower.Contains(TEXT("spline"));
    }

    bool IsRoadFootprintUsable(const FBox2D& Footprint)
    {
        if (!Footprint.bIsValid)
        {
            return false;
        }

        const FVector2D Size = Footprint.GetSize();
        const float MinDim = FMath::Min(Size.X, Size.Y);
        const float MaxDim = FMath::Max(Size.X, Size.Y);

        // Reject oversized XY footprints that behave like global blockers.
        return MinDim > 10.0f && MinDim <= 3500.0f && MaxDim <= 180000.0f;
    }

    bool IsLandscapeLikeSpawnDomainActor(const AActor* Actor)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        const FString ClassLower = Actor->GetClass()->GetName().ToLower();
        return
            ClassLower.Contains(TEXT("landscapeproxy")) ||
            ClassLower.Contains(TEXT("landscapestreamingproxy")) ||
            ClassLower.Equals(TEXT("landscape"));
    }

    bool TryGetLandscapeXYSpawnBounds(UWorld* World, FBox2D& OutBounds)
    {
        OutBounds = FBox2D(EForceInit::ForceInit);
        if (!World)
        {
            return false;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsLandscapeLikeSpawnDomainActor(Actor))
            {
                continue;
            }

            const FBox Bounds = Actor->GetComponentsBoundingBox(true);
            if (!Bounds.IsValid)
            {
                continue;
            }

            OutBounds += FVector2D(Bounds.Min.X, Bounds.Min.Y);
            OutBounds += FVector2D(Bounds.Max.X, Bounds.Max.Y);
        }

        if (!OutBounds.bIsValid)
        {
            return false;
        }

        const FVector2D Size = OutBounds.GetSize();
        return Size.X >= 1000.0f && Size.Y >= 1000.0f;
    }

    void GatherRoadFootprints(UWorld* World, TArray<FBox2D>& OutRoadFootprints)
    {
        OutRoadFootprints.Reset();
        if (!World)
        {
            return;
        }

        OutRoadFootprints.Reserve(256);
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor))
            {
                continue;
            }

            TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps(Actor);
            for (UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
            {
                if (!IsRoadLikePrimitiveComponent(PrimitiveComp))
                {
                    continue;
                }

                FBox2D Footprint(EForceInit::ForceInit);
                if (TryBuildFootprintFromPrimitiveComponent(PrimitiveComp, Footprint))
                {
                    if (IsRoadFootprintUsable(Footprint))
                    {
                        OutRoadFootprints.Add(Footprint);
                    }
                }
            }
        }
    }

    bool IsLocationNearRoadFootprints(
        const FVector& Location,
        float AvoidanceRadius,
        const TArray<FBox2D>& RoadFootprints)
    {
        return IsLocationNearFootprints(Location, AvoidanceRadius, RoadFootprints);
    }

    bool IsLocationNearRoadSplineComponents(
        const FVector& Location,
        float AvoidanceRadius,
        const TArray<const USplineComponent*>& RoadSplines)
    {
        if (AvoidanceRadius <= 1.0f || RoadSplines.Num() == 0)
        {
            return false;
        }

        const float RadiusSqXY = FMath::Square(AvoidanceRadius);
        for (const USplineComponent* SplineComp : RoadSplines)
        {
            if (!IsValid(SplineComp))
            {
                continue;
            }

            const FVector Closest = SplineComp->FindLocationClosestToWorldLocation(Location, ESplineCoordinateSpace::World);
            if (FVector::DistSquaredXY(Closest, Location) <= RadiusSqXY)
            {
                return true;
            }
        }
        return false;
    }

    bool IsLocationNearLandscapeSplineRoad(
        UWorld* World,
        const FVector& Location,
        float AvoidanceRadius,
        const FCollisionQueryParams& QueryParams,
        const TArray<const USplineComponent*>* CachedRoadSplines,
        const TArray<FBox2D>* CachedRoadFootprints,
        const TArray<FRoadSplineWidthSample>* CachedRoadWidthSamples)
    {
        if (!World || AvoidanceRadius <= 1.0f)
        {
            return false;
        }

        const float EffectiveWidthSamplePadding = FMath::Clamp(AvoidanceRadius, 120.0f, 2200.0f);
        if (CachedRoadWidthSamples &&
            IsLocationNearLandscapeSplineWidthSamples(Location, EffectiveWidthSamplePadding, *CachedRoadWidthSamples))
        {
            return true;
        }

        if (CachedRoadFootprints &&
            IsLocationNearRoadFootprints(Location, AvoidanceRadius, *CachedRoadFootprints))
        {
            return true;
        }

        if (CachedRoadSplines &&
            IsLocationNearRoadSplineComponents(Location, AvoidanceRadius, *CachedRoadSplines))
        {
            return true;
        }

        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

        TArray<FOverlapResult> Overlaps;
        if (World->OverlapMultiByObjectType(
            Overlaps,
            Location,
            FQuat::Identity,
            ObjQuery,
            FCollisionShape::MakeSphere(AvoidanceRadius),
            QueryParams))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                if (IsRoadLikeTaggedActorOrComponent(Overlap.GetActor(), Overlap.Component.Get()))
                {
                    return true;
                }
            }
        }

        constexpr int32 RingSamples = 6;
        for (int32 Index = 0; Index <= RingSamples; ++Index)
        {
            const float Dist = (Index == 0) ? 0.0f : AvoidanceRadius;
            const float Angle = (Index == 0) ? 0.0f : (2.0f * PI * (float)(Index - 1) / (float)RingSamples);
            const FVector SamplePoint = Location + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0f);

            FHitResult Hit;
            if (World->LineTraceSingleByChannel(
                Hit,
                SamplePoint + FVector(0.0f, 0.0f, 100000.0f),
                SamplePoint + FVector(0.0f, 0.0f, -100000.0f),
                ECC_WorldStatic,
                QueryParams))
            {
                if (IsRoadLikeTaggedActorOrComponent(Hit.GetActor(), Hit.GetComponent()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool IsFootprintNearLandscapeSplineRoad(
        UWorld* World,
        const FBox2D& Footprint,
        float AvoidanceRadius,
        const FCollisionQueryParams& QueryParams,
        const TArray<const USplineComponent*>* CachedRoadSplines,
        const TArray<FBox2D>* CachedRoadFootprints,
        const TArray<FRoadSplineWidthSample>* CachedRoadWidthSamples);

    bool IsLocationWithinShorelineBuffer(
        UWorld* World,
        const FVector& Location,
        float AvoidanceRadius,
        float ShorelineExtraDistance,
        int32 ProbeSampleCount,
        bool bUseWaterVolumeProbe,
        const FCollisionQueryParams& QueryParams)
    {
        if (!World || ShorelineExtraDistance <= 1.0f || ProbeSampleCount <= 0)
        {
            return false;
        }

        const int32 SafeSampleCount = FMath::Clamp(ProbeSampleCount, 4, 32);
        const float OuterProbeRadius = FMath::Max(80.0f, AvoidanceRadius + ShorelineExtraDistance);
        const float MidProbeRadius = FMath::Max(60.0f, AvoidanceRadius + ShorelineExtraDistance * 0.5f);

        auto IsWaterAtProbePoint = [&](const FVector& SamplePoint) -> bool
            {
                if (bUseWaterVolumeProbe && IsInsideWaterPhysicsVolume(World, SamplePoint, 30.0f))
                {
                    return true;
                }

                FHitResult Hit;
                if (World->LineTraceSingleByChannel(
                    Hit,
                    SamplePoint + FVector(0.0f, 0.0f, 100000.0f),
                    SamplePoint + FVector(0.0f, 0.0f, -100000.0f),
                    ECC_WorldStatic,
                    QueryParams))
                {
                    if (IsHitWaterLocation(Hit))
                    {
                        return true;
                    }
                }

                if (World->LineTraceSingleByChannel(
                    Hit,
                    SamplePoint + FVector(0.0f, 0.0f, 100000.0f),
                    SamplePoint + FVector(0.0f, 0.0f, -100000.0f),
                    ECC_Visibility,
                    QueryParams))
                {
                    if (IsHitWaterLocation(Hit))
                    {
                        return true;
                    }
                }

                return false;
            };

        for (int32 SampleIndex = 0; SampleIndex < SafeSampleCount; ++SampleIndex)
        {
            const float Angle = (2.0f * PI * static_cast<float>(SampleIndex)) / static_cast<float>(SafeSampleCount);
            const FVector2D UnitDir(FMath::Cos(Angle), FMath::Sin(Angle));

            const FVector OuterSample = Location + FVector(UnitDir.X * OuterProbeRadius, UnitDir.Y * OuterProbeRadius, 0.0f);
            if (IsWaterAtProbePoint(OuterSample))
            {
                return true;
            }

            const FVector MidSample = Location + FVector(UnitDir.X * MidProbeRadius, UnitDir.Y * MidProbeRadius, 0.0f);
            if (IsWaterAtProbePoint(MidSample))
            {
                return true;
            }
        }

        return false;
    }

    bool IsRoomFootprintNearLandscapeSplineRoad(
        UWorld* World,
        const FVector& CenterLocation,
        float RoomHalfExtent,
        float AvoidanceRadius,
        const FCollisionQueryParams& QueryParams,
        const TArray<const USplineComponent*>* CachedRoadSplines,
        const TArray<FBox2D>* CachedRoadFootprints,
        const TArray<FRoadSplineWidthSample>* CachedRoadWidthSamples)
    {
        if (!World)
        {
            return false;
        }

        const float SafeHalfExtent = FMath::Max(0.0f, RoomHalfExtent);
        const float SampleRadius = SafeHalfExtent + 200.0f;
        const FBox2D RoomFootprint(
            FVector2D(CenterLocation.X - SafeHalfExtent, CenterLocation.Y - SafeHalfExtent),
            FVector2D(CenterLocation.X + SafeHalfExtent, CenterLocation.Y + SafeHalfExtent));

        if (IsFootprintNearLandscapeSplineRoad(
            World,
            RoomFootprint,
            AvoidanceRadius,
            QueryParams,
            CachedRoadSplines,
            CachedRoadFootprints,
            CachedRoadWidthSamples))
        {
            return true;
        }

        if (CachedRoadFootprints && RoomFootprint.bIsValid)
        {
            for (const FBox2D& RoadBox : *CachedRoadFootprints)
            {
                if (!RoadBox.bIsValid)
                {
                    continue;
                }

                FBox2D ExpandedRoadBox = RoadBox;
                ExpandedRoadBox.Min -= FVector2D(AvoidanceRadius, AvoidanceRadius);
                ExpandedRoadBox.Max += FVector2D(AvoidanceRadius, AvoidanceRadius);
                if (ExpandedRoadBox.Intersect(RoomFootprint))
                {
                    return true;
                }
            }
        }

        if (IsLocationNearLandscapeSplineRoad(
            World,
            CenterLocation,
            AvoidanceRadius,
            QueryParams,
            CachedRoadSplines,
            CachedRoadFootprints,
            CachedRoadWidthSamples))
        {
            return true;
        }

        if (SampleRadius <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        static const FVector2D UnitOffsets[] =
        {
            FVector2D(1.0f, 0.0f),
            FVector2D(-1.0f, 0.0f),
            FVector2D(0.0f, 1.0f),
            FVector2D(0.0f, -1.0f),
            FVector2D(0.70710678f, 0.70710678f),
            FVector2D(-0.70710678f, 0.70710678f),
            FVector2D(-0.70710678f, -0.70710678f),
            FVector2D(0.70710678f, -0.70710678f)
        };

        for (const FVector2D& UnitOffset : UnitOffsets)
        {
            const FVector SamplePoint = CenterLocation + FVector(UnitOffset.X * SampleRadius, UnitOffset.Y * SampleRadius, 0.0f);
            if (IsLocationNearLandscapeSplineRoad(
                World,
                SamplePoint,
                AvoidanceRadius,
                QueryParams,
                CachedRoadSplines,
                CachedRoadFootprints,
                CachedRoadWidthSamples))
            {
                return true;
            }
        }

        return false;
    }

    bool IsFootprintNearLandscapeSplineRoad(
        UWorld* World,
        const FBox2D& Footprint,
        float AvoidanceRadius,
        const FCollisionQueryParams& QueryParams,
        const TArray<const USplineComponent*>* CachedRoadSplines,
        const TArray<FBox2D>* CachedRoadFootprints,
        const TArray<FRoadSplineWidthSample>* CachedRoadWidthSamples)
    {
        if (!World || !Footprint.bIsValid)
        {
            return false;
        }

        const FVector2D Min = Footprint.Min;
        const FVector2D Max = Footprint.Max;
        const FVector2D Size = Footprint.GetSize();
        const float ExtraPadding = FMath::Clamp(FMath::Max(Size.X, Size.Y) * 0.12f, 60.0f, 900.0f);
        const float EffectiveAvoidanceRadius = AvoidanceRadius + ExtraPadding;

        // Dense grid sampling catches narrow/long road intersections that center+corner probes miss.
        constexpr int32 GridAxisSamples = 5;
        for (int32 SampleY = 0; SampleY < GridAxisSamples; ++SampleY)
        {
            const float V = (GridAxisSamples <= 1)
                ? 0.5f
                : static_cast<float>(SampleY) / static_cast<float>(GridAxisSamples - 1);
            for (int32 SampleX = 0; SampleX < GridAxisSamples; ++SampleX)
            {
                const float U = (GridAxisSamples <= 1)
                    ? 0.5f
                    : static_cast<float>(SampleX) / static_cast<float>(GridAxisSamples - 1);
                const FVector2D XY(
                    FMath::Lerp(Min.X, Max.X, U),
                    FMath::Lerp(Min.Y, Max.Y, V));
                const FVector SamplePoint(XY.X, XY.Y, 0.0f);
                if (IsLocationNearLandscapeSplineRoad(
                    World,
                    SamplePoint,
                    EffectiveAvoidanceRadius,
                    QueryParams,
                    CachedRoadSplines,
                    CachedRoadFootprints,
                    CachedRoadWidthSamples))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool IsFootprintBlockedByWaterBodySplineRules(
        const FBox2D& Footprint,
        const TArray<FWaterBodySplineRule>& Rules,
        float EdgeBufferDistance,
        float OceanExtraBufferDistance,
        float LakeExtraBufferDistance)
    {
        if (!Footprint.bIsValid || Rules.Num() == 0)
        {
            return false;
        }

        const FVector2D Min = Footprint.Min;
        const FVector2D Max = Footprint.Max;
        static const FVector2D LocalSamples[] =
        {
            FVector2D(0.0f, 0.0f),   // center
            FVector2D(-1.0f, -1.0f), // corners
            FVector2D(1.0f, -1.0f),
            FVector2D(-1.0f, 1.0f),
            FVector2D(1.0f, 1.0f),
            FVector2D(-1.0f, 0.0f),  // edge midpoints
            FVector2D(1.0f, 0.0f),
            FVector2D(0.0f, -1.0f),
            FVector2D(0.0f, 1.0f)
        };

        for (const FVector2D& Sample : LocalSamples)
        {
            const FVector2D XY(
                FMath::Lerp(Min.X, Max.X, (Sample.X + 1.0f) * 0.5f),
                FMath::Lerp(Min.Y, Max.Y, (Sample.Y + 1.0f) * 0.5f));
            if (IsLocationBlockedByWaterBodySplineRules(
                FVector(XY.X, XY.Y, 0.0f),
                Rules,
                EdgeBufferDistance,
                OceanExtraBufferDistance,
                LakeExtraBufferDistance))
            {
                return true;
            }
        }

        return false;
    }

    bool IsRoomFootprintNearWater(
        UWorld* World,
        const FVector& CenterLocation,
        float RoomHalfExtent,
        float AvoidanceRadius,
        bool bUseWaterVolumeProbe,
        float ShorelineExtraDistance,
        int32 ShorelineProbeSampleCount,
        bool bUseWaterVolumeShorelineProbe,
        const FCollisionQueryParams& QueryParams)
    {
        if (!World)
        {
            return false;
        }

        const float SafeHalfExtent = FMath::Max(0.0f, RoomHalfExtent);
        const float SampleRadius = SafeHalfExtent + 250.0f;
        const float EffectiveShorelineExtraDistance =
            FMath::Max(0.0f, ShorelineExtraDistance) + FMath::Min(SafeHalfExtent * 0.22f, 1400.0f);

        if (IsLocationNearWater(World, CenterLocation, AvoidanceRadius, QueryParams, bUseWaterVolumeProbe))
        {
            return true;
        }

        if (IsLocationWithinShorelineBuffer(
            World,
            CenterLocation,
            AvoidanceRadius,
            EffectiveShorelineExtraDistance,
            ShorelineProbeSampleCount,
            bUseWaterVolumeShorelineProbe,
            QueryParams))
        {
            return true;
        }

        if (SampleRadius <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        static const FVector2D UnitOffsets[] =
        {
            FVector2D(1.0f, 0.0f),
            FVector2D(-1.0f, 0.0f),
            FVector2D(0.0f, 1.0f),
            FVector2D(0.0f, -1.0f),
            FVector2D(0.70710678f, 0.70710678f),
            FVector2D(-0.70710678f, 0.70710678f),
            FVector2D(-0.70710678f, -0.70710678f),
            FVector2D(0.70710678f, -0.70710678f)
        };

        for (const FVector2D& UnitOffset : UnitOffsets)
        {
            const FVector SamplePoint = CenterLocation + FVector(UnitOffset.X * SampleRadius, UnitOffset.Y * SampleRadius, 0.0f);
            if (IsLocationNearWater(World, SamplePoint, AvoidanceRadius, QueryParams, bUseWaterVolumeProbe))
            {
                return true;
            }
            if (IsLocationWithinShorelineBuffer(
                World,
                SamplePoint,
                AvoidanceRadius,
                EffectiveShorelineExtraDistance,
                ShorelineProbeSampleCount,
                bUseWaterVolumeShorelineProbe,
                QueryParams))
            {
                return true;
            }
        }

        return false;
    }
}

ARaidLayoutManager::ARaidLayoutManager()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    if (USceneComponent* SceneRoot = Cast<USceneComponent>(RootComponent))
    {
        SceneRoot->SetMobility(EComponentMobility::Static);
    }
}

void ARaidLayoutManager::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    EnsurePreBeginPieAutoBakeHook();
#endif
}

void ARaidLayoutManager::BeginDestroy()
{
#if WITH_EDITOR
    RemovePreBeginPieAutoBakeHook();
#endif
    Super::BeginDestroy();
}

#if WITH_EDITOR
void ARaidLayoutManager::EnsurePreBeginPieAutoBakeHook()
{
    if (PreBeginPieDelegateHandle.IsValid())
    {
        return;
    }

    if (IsTemplate() || HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return;
    }

    PreBeginPieDelegateHandle = FEditorDelegates::PreBeginPIE.AddUObject(this, &ARaidLayoutManager::HandlePreBeginPie);
}

void ARaidLayoutManager::RemovePreBeginPieAutoBakeHook()
{
    if (!PreBeginPieDelegateHandle.IsValid())
    {
        return;
    }

    FEditorDelegates::PreBeginPIE.Remove(PreBeginPieDelegateHandle);
    PreBeginPieDelegateHandle.Reset();
}

void ARaidLayoutManager::HandlePreBeginPie(bool bIsSimulating)
{
    if (bIsSimulating || !bAutoBakeLayoutBeforePIE)
    {
        return;
    }

    if (IsTemplate() || HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return;
    }

    if (!GEditor)
    {
        return;
    }

    UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
    if (!EditorWorld || World != EditorWorld)
    {
        return;
    }

    FPlatformMemoryStats PrePieMemoryStats;
    if (!IsPrePieAutoBakeMemoryHealthy(&PrePieMemoryStats))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Pre-PIE auto-bake skipped (low memory guard). AvailablePhysical=%.2f GiB AvailableVirtual=%.2f GiB"),
            BytesToGiB(PrePieMemoryStats.AvailablePhysical),
            BytesToGiB(PrePieMemoryStats.AvailableVirtual));
        return;
    }

    bool bHasPrebuiltRooms = false;
    for (TActorIterator<ARaidRoomActor> It(World); It; ++It)
    {
        if (IsValid(*It))
        {
            bHasPrebuiltRooms = true;
            break;
        }
    }

    if (bAutoBakeOnlyIfNoPrebuiltRooms && bHasPrebuiltRooms)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Pre-PIE auto-bake skipped (prebuilt rooms already exist)."));
        return;
    }

    if (!IsValid(ChapterConfig) || !IsValid(ChapterConfig->LevelDataTable))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Pre-PIE auto-bake skipped (ChapterConfig or LevelDataTable is missing)."));
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidLayout] Pre-PIE auto-bake started. Mode=%s"),
        bAutoBakeOnlyIfNoPrebuiltRooms ? TEXT("IfMissing") : TEXT("Always"));

    SpawnRaidLayout();
}
#endif

void ARaidLayoutManager::BeginPlay()
{
    Super::BeginPlay();

    // Runtime guardrail: keep dense trees visually rich but prevent expensive actor/shadow budgets
    // from regressing performance when old map instances still carry legacy high values.
    if (bUseDenseTreeFastMode)
    {
        DenseTreeActorBudget = FMath::Clamp(DenseTreeActorBudget, 0, 24);
        DenseTreeActorRadius = FMath::Clamp(DenseTreeActorRadius, 1000.0f, 8000.0f);
        DenseTreeActorShadowRadius = FMath::Clamp(DenseTreeActorShadowRadius, 1000.0f, 4500.0f);
    }

    WindTreeActorMaxCount = FMath::Clamp(WindTreeActorMaxCount, 0, 24);
    WindTreeActorSpawnRadius = FMath::Clamp(WindTreeActorSpawnRadius, 1000.0f, 8000.0f);
    WindTreeActorShadowRadius = FMath::Clamp(WindTreeActorShadowRadius, 1000.0f, 4500.0f);

    BackgroundISMCNearCullEnd = FMath::Clamp(BackgroundISMCNearCullEnd, 0, 26000);
    BackgroundISMCMidCullStart = FMath::Clamp(BackgroundISMCMidCullStart, 0, 9000);
    BackgroundISMCMidCullEnd = FMath::Clamp(BackgroundISMCMidCullEnd, 0, 52000);
    BackgroundISMCFarCullEnd = FMath::Clamp(BackgroundISMCFarCullEnd, 0, 120000);
    bBackgroundISMCMidCastShadow = false;

    WaterAvoidanceRadius = FMath::Max(100.0f, WaterAvoidanceRadius);
    ShorelineSpawnBufferDistance = FMath::Clamp(ShorelineSpawnBufferDistance, 0.0f, 12000.0f);
    ShorelineProbeSampleCount = FMath::Clamp(ShorelineProbeSampleCount, 4, 32);
    LandscapeSplineRoadAvoidanceRadius = FMath::Clamp(LandscapeSplineRoadAvoidanceRadius, 50.0f, 8000.0f);
    WaterBodySplineEdgeBufferDistance = FMath::Clamp(WaterBodySplineEdgeBufferDistance, 0.0f, 10000.0f);
    WaterBodyOceanExtraBufferDistance = FMath::Clamp(WaterBodyOceanExtraBufferDistance, 0.0f, 20000.0f);
    WaterBodyLakeExtraBufferDistance = FMath::Clamp(WaterBodyLakeExtraBufferDistance, 0.0f, 20000.0f);
    PreplacedStaticMeshBufferDistance = FMath::Clamp(PreplacedStaticMeshBufferDistance, 0.0f, 5000.0f);

    if (bApplyOpenWorldVsmGuardrail)
    {
        ApplyRaidVsmGuardrailCVars();
    }

    UWorld* World = GetWorld();
    bool bHasPrebuiltRooms = false;
    if (World)
    {
        for (TActorIterator<ARaidRoomActor> It(World); It; ++It)
        {
            if (IsValid(*It))
            {
                bHasPrebuiltRooms = true;
                break;
            }
        }
    }

    const bool bShouldRuntimeSpawn = bAutoSpawnLayoutOnBeginPlay || !bHasPrebuiltRooms;
    if (bShouldRuntimeSpawn)
    {
        SpawnRaidLayout();
        return;
    }

    // Keep editor-saved raid layout as-is in play and only restore runtime combat registration.
    URaidCombatSubsystem* CombatSub = World ? World->GetSubsystem<URaidCombatSubsystem>() : nullptr;
    if (!CombatSub)
    {
        return;
    }

    CombatSub->ResetSubsystem();

    TArray<ARaidRoomActor*> ExistingRooms;
    for (TActorIterator<ARaidRoomActor> It(World); It; ++It)
    {
        if (ARaidRoomActor* Room = *It)
        {
            ExistingRooms.Add(Room);
        }
    }

    ExistingRooms.Sort([](const ARaidRoomActor& A, const ARaidRoomActor& B)
        {
            return A.GetNodeId() < B.GetNodeId();
        });

    TMap<int32, FLevelNodeRow> LevelRowsByNodeId;
    if (IsValid(ChapterConfig) && IsValid(ChapterConfig->LevelDataTable))
    {
        TArray<FLevelNodeRow*> TableRows;
        ChapterConfig->LevelDataTable->GetAllRows<FLevelNodeRow>(TEXT("RaidLayout.PrebuiltBeginPlay"), TableRows);
        for (const FLevelNodeRow* TableRow : TableRows)
        {
            if (!TableRow || TableRow->NodeId <= 0)
            {
                continue;
            }

            LevelRowsByNodeId.FindOrAdd(TableRow->NodeId) = *TableRow;
        }
    }

    const bool bIsGameWorld = World && World->IsGameWorld();
    int32 ReboundRoomConfigCount = 0;
    int32 SyncedNodeRowFromTableCount = 0;
    int32 RegeneratedRoomLayoutCount = 0;
    int32 AutoRecoveredRoomLayoutCount = 0;
    ARaidRoomActor* StartRoom = nullptr;
    for (ARaidRoomActor* Room : ExistingRooms)
    {
        if (!IsValid(Room))
        {
            continue;
        }

        if (IsValid(ChapterConfig))
        {
            FLevelNodeRow EffectiveNodeRow = Room->GetNodeRow();
            int32 EffectiveNodeId = Room->GetNodeId() > 0 ? Room->GetNodeId() : EffectiveNodeRow.NodeId;

            if (const FLevelNodeRow* TableRow = LevelRowsByNodeId.Find(EffectiveNodeId))
            {
                EffectiveNodeRow = *TableRow;
                EffectiveNodeId = EffectiveNodeRow.NodeId;
                ++SyncedNodeRowFromTableCount;
            }

            Room->SetNodeData(EffectiveNodeId, EffectiveNodeRow, ChapterConfig);
            ++ReboundRoomConfigCount;
        }

        bool bRoomLayoutRegenerated = false;

        if (bIsGameWorld &&
            !bRegeneratePrebuiltRoomLayoutOnBeginPlay &&
            bAutoRecoverInvalidPrebuiltRoomGeometryAtRuntime &&
            IsValid(ChapterConfig))
        {
            const FLevelNodeRow& RoomNodeRow = Room->GetNodeRow();
            FString ResolvedThemeKey;
            const FModularMeshKit* ResolvedThemeKit = nullptr;
            ChapterConfig->ResolveThemeKitForNode(RoomNodeRow, ResolvedThemeKey, ResolvedThemeKit);

            const int32 ExpectedFloorVariations =
                CountRuntimeUsableVariationsForChannel(ResolvedThemeKit, ERaidVariationOffsetChannel::Floor);
            const int32 ExpectedWallVariations =
                CountRuntimeUsableVariationsForChannel(ResolvedThemeKit, ERaidVariationOffsetChannel::Wall);
            const int32 ExpectedObstacleVariations =
                CountRuntimeUsableVariationsForChannel(ResolvedThemeKit, ERaidVariationOffsetChannel::Obstacle);
            const int32 ExpectedDecorationVariations =
                CountRuntimeUsableVariationsForChannel(ResolvedThemeKit, ERaidVariationOffsetChannel::Decoration);

            const int32 ExistingFloorGeometry = CountRoomTaggedGeometry(Room, 0);
            const int32 ExistingWallGeometry = CountRoomTaggedGeometry(Room, 1);
            const int32 ExistingObstacleGeometry = CountRoomTaggedGeometry(Room, 2);
            const int32 ExistingDecorationGeometry = CountRoomTaggedGeometry(Room, 3);

            const bool bExpectedCoreGeometry = (ExpectedFloorVariations + ExpectedWallVariations) > 0;
            const bool bMissingCoreGeometry = (ExistingFloorGeometry + ExistingWallGeometry) <= 0;
            const bool bExpectedScatterGeometry = (ExpectedObstacleVariations + ExpectedDecorationVariations) > 0;
            const bool bMissingScatterGeometry = (ExistingObstacleGeometry + ExistingDecorationGeometry) <= 0;

            const bool bNeedsAutoRecovery =
                (bExpectedCoreGeometry && bMissingCoreGeometry) ||
                (bExpectedScatterGeometry && bMissingScatterGeometry);

            if (bNeedsAutoRecovery)
            {
                Room->GenerateRoomLayout();
                ++RegeneratedRoomLayoutCount;
                ++AutoRecoveredRoomLayoutCount;
                bRoomLayoutRegenerated = true;

                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidLayout] Auto-recovered prebuilt room geometry. Node=%d Type=%s Theme=%s Env=%s Expected(Floor=%d Wall=%d Obs=%d Deco=%d) Existing(Floor=%d Wall=%d Obs=%d Deco=%d)"),
                    Room->GetNodeId(),
                    *RoomNodeRow.RoomType,
                    *RoomNodeRow.Theme,
                    *RoomNodeRow.EnvType,
                    ExpectedFloorVariations,
                    ExpectedWallVariations,
                    ExpectedObstacleVariations,
                    ExpectedDecorationVariations,
                    ExistingFloorGeometry,
                    ExistingWallGeometry,
                    ExistingObstacleGeometry,
                    ExistingDecorationGeometry);
            }
        }

        if (bIsGameWorld && bRegeneratePrebuiltRoomLayoutOnBeginPlay && !bRoomLayoutRegenerated)
        {
            Room->GenerateRoomLayout();
            ++RegeneratedRoomLayoutCount;
        }

        CombatSub->RegisterRoom(Room);
        if (!StartRoom && Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            StartRoom = Room;
        }
    }

    CombatSub->UpdateCompassForNextRooms(StartRoom);

    if (!bEnableDynamicWavesFromLayoutManager)
    {
        CombatSub->ConfigureDynamicWaves(false, 1, 0.0f, 60.0f, 1);
    }
    else
    {
        const URaidWaveProfile* WaveProfileToApply = nullptr;
        if (IsValid(WaveProfileOverride))
        {
            WaveProfileToApply = WaveProfileOverride;
        }
        else if (ChapterConfig && IsValid(ChapterConfig->WaveProfile))
        {
            WaveProfileToApply = ChapterConfig->WaveProfile;
        }
        CombatSub->ApplyWaveProfile(WaveProfileToApply);
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidLayout] BeginPlay kept prebuilt layout. Registered rooms=%d ReboundConfig=%d SyncedNodeRows=%d RegeneratedLayout=%d AutoRecovered=%d RuntimeRegeneration=%s"),
        ExistingRooms.Num(),
        ReboundRoomConfigCount,
        SyncedNodeRowFromTableCount,
        RegeneratedRoomLayoutCount,
        AutoRecoveredRoomLayoutCount,
        bRegeneratePrebuiltRoomLayoutOnBeginPlay ? TEXT("On") : TEXT("Off"));
}

// 🔥 [신규] CSV 기반 맞춤형 더미 자동 생성 함수
void ARaidLayoutManager::AutoGenerateWhiteboxFromCSV()
{
    if (!ChapterConfig) { UE_LOG(LogTemp, Error, TEXT("ChapterConfig가 비어있습니다!")); return; }
    ChapterConfig->Modify(); Modify();

    TSoftObjectPtr<UStaticMesh> Cube(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
    TSoftObjectPtr<UStaticMesh> Sphere(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    TSoftObjectPtr<UStaticMesh> Cylinder(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    TSoftObjectPtr<UStaticMesh> Plane(FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")));

    auto MakeVar = [](TSoftObjectPtr<UStaticMesh> InMesh, FVector InScale, bool bRand = false) {
        FMeshVariation V; V.Mesh = InMesh; V.Offset.SetScale3D(InScale);
        V.bUseRandomScale = bRand; if (bRand) { V.RandomScaleMin = 0.7f; V.RandomScaleMax = 1.4f; }
        return V;
        };

    TSet<FString> UsedThemes;
    if (ChapterConfig->LevelDataTable) {
        TArray<FLevelNodeRow*> Rows; ChapterConfig->LevelDataTable->GetAllRows<FLevelNodeRow>(TEXT(""), Rows);
        for (FLevelNodeRow* Row : Rows) { if (Row && !Row->Theme.IsEmpty()) UsedThemes.Add(Row->Theme); }
    }
    if (UsedThemes.Num() == 0) { UsedThemes.Add(TEXT("Jungle")); UsedThemes.Add(TEXT("Urban")); }

    ChapterConfig->ThemeRegistry.Empty();
    for (const FString& ThemeName : UsedThemes) {
        FModularMeshKit Kit;
        Kit.bIsOrganicTheme = ThemeName.Contains(TEXT("Jungle")) || ThemeName.Contains(TEXT("Nature"));
        Kit.ThemeTags.AddUnique(ThemeName);
        {
            FMeshVariationIndexGroup FloorIndex;
            FloorIndex.IndexName = TEXT("Floor_00");
            FloorIndex.Variations.Add(MakeVar(Plane, FVector(4.f, 4.f, 1.f)));
            Kit.FloorVariationIndices.Add(FloorIndex);
        }
        {
            FMeshVariationIndexGroup ObstacleIndex;
            ObstacleIndex.IndexName = TEXT("Obstacle_00");
            ObstacleIndex.Variations.Add(MakeVar(Cube, FVector(1.f, 2.f, 1.5f), true));
            Kit.ObstacleVariationIndices.Add(ObstacleIndex);
        }
        if (Kit.bIsOrganicTheme) {
            Kit.ThemeTags.AddUnique(TEXT("outdoor"));
            Kit.ThemeTags.AddUnique(TEXT("nature"));
            Kit.ThemeTags.AddUnique(TEXT("forest"));
            FMeshCluster TreeCls; TreeCls.ClusterName = TEXT("Foliage_Trees"); TreeCls.Variations.Add(MakeVar(Cylinder, FVector(0.5f, 0.5f, 2.f), true));
            FMeshCluster RockCls; RockCls.ClusterName = TEXT("Foliage_Rocks"); RockCls.Variations.Add(MakeVar(Sphere, FVector(1.2f, 1.2f, 1.2f), true));
            Kit.FoliageClusters.Add(TreeCls); Kit.FoliageClusters.Add(RockCls);
        }
        else {
            Kit.ThemeTags.AddUnique(TEXT("indoor"));
            Kit.ThemeTags.AddUnique(TEXT("urban"));
            Kit.ThemeTags.AddUnique(TEXT("city"));
            FMeshVariationIndexGroup WallIndex;
            WallIndex.IndexName = TEXT("Wall_00");
            WallIndex.Variations.Add(MakeVar(Cube, FVector(4.f, 0.2f, 3.f)));
            Kit.WallVariationIndices.Add(WallIndex);
        }
        ChapterConfig->ThemeRegistry.Add(ThemeName, Kit);
    }

    BackgroundClusters.Empty();
    FMeshCluster BgTree; BgTree.ClusterName = TEXT("Background_Trees"); BgTree.SpawnRadius = BackgroundRadius; BgTree.MinDistanceBetweenInstances = 1500.0f; BgTree.Variations.Add(MakeVar(Cylinder, FVector(1.f, 1.f, 4.f), true));
    FMeshCluster BgRock; BgRock.ClusterName = TEXT("Background_Rocks"); BgRock.SpawnRadius = BackgroundRadius; BgRock.MinDistanceBetweenInstances = 3000.0f; BgRock.Variations.Add(MakeVar(Sphere, FVector(3.f, 3.f, 2.f), true));
    FMeshCluster BgBush; BgBush.ClusterName = TEXT("Background_Bushes_NoCol"); BgBush.SpawnRadius = BackgroundRadius; BgBush.MinDistanceBetweenInstances = 800.0f; BgBush.Variations.Add(MakeVar(Sphere, FVector(1.f, 1.f, 0.5f), true));
    FMeshCluster BgStruct; BgStruct.ClusterName = TEXT("Background_Structures"); BgStruct.SpawnRadius = BackgroundRadius; BgStruct.MinDistanceBetweenInstances = 4000.0f; BgStruct.Variations.Add(MakeVar(Cube, FVector(2.f, 2.f, 4.f), true));
    BackgroundClusters.Add(BgTree); BackgroundClusters.Add(BgRock); BackgroundClusters.Add(BgBush); BackgroundClusters.Add(BgStruct);

    UE_LOG(LogTemp, Warning, TEXT("[Raid UX] CSV 기반 테마 및 배경 슬롯 자동 생성 완료!"));
}

void ARaidLayoutManager::AddSelectedAssetsToConfiguredTarget()
{
#if !WITH_EDITOR
    UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Bulk add is editor-only."));
#else
    TArray<FAssetData> SelectedAssets;
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
    if (SelectedAssets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: select assets in Content Browser first."));
        return;
    }

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
            NewCluster.ClusterName = RequestedName.IsEmpty() ? TEXT("NewCluster") : RequestedName;
            NewCluster.SpawnRadius = BackgroundRadius;
            NewCluster.SpawnCountMin = 1.0f;
            NewCluster.SpawnCountMax = 1.0f;
            NewCluster.MinDistanceBetweenInstances = 200.0f;
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

    auto ResolveThemeKit =
        [&](const FString& RequestedTheme, bool bCreateMissing, FString& OutResolvedThemeKey) -> FModularMeshKit*
        {
            if (!ChapterConfig)
            {
                return nullptr;
            }

            const FString TrimmedTheme = RequestedTheme.TrimStartAndEnd();
            if (!TrimmedTheme.IsEmpty())
            {
                if (FModularMeshKit* Exact = ChapterConfig->ThemeRegistry.Find(TrimmedTheme))
                {
                    OutResolvedThemeKey = TrimmedTheme;
                    return Exact;
                }

                for (TPair<FString, FModularMeshKit>& Pair : ChapterConfig->ThemeRegistry)
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
            return &ChapterConfig->ThemeRegistry.Add(OutResolvedThemeKey, FModularMeshKit());
        };

    TArray<FMeshVariation>* TargetVariations = nullptr;
    FString TargetLabel;
    FString ResolvedThemeKey;
    const FString RequestedClusterName = BulkAddClusterName.TrimStartAndEnd();

    switch (BulkAddTarget)
    {
    case ERaidBulkAddTarget::BackgroundCluster:
    {
        FMeshCluster* TargetCluster = FindOrAddClusterByName(BackgroundClusters, RequestedClusterName, bBulkAddCreateMissingTargets);
        if (!TargetCluster)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: background cluster '%s' not found."), *RequestedClusterName);
            return;
        }
        TargetVariations = &TargetCluster->Variations;
        TargetLabel = FString::Printf(TEXT("BackgroundCluster[%s]"), *TargetCluster->ClusterName);
        break;
    }
    case ERaidBulkAddTarget::ThemeFloorVariations:
    case ERaidBulkAddTarget::ThemeWallVariations:
    case ERaidBulkAddTarget::ThemeObstacleVariations:
    case ERaidBulkAddTarget::ThemeDecorationVariations:
    case ERaidBulkAddTarget::ThemeDoorBlockerVariations:
    case ERaidBulkAddTarget::ThemeFoliageCluster:
    {
        if (!ChapterConfig)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: ChapterConfig is null."));
            return;
        }

        FModularMeshKit* TargetTheme = ResolveThemeKit(BulkAddThemeKey, bBulkAddCreateMissingTargets, ResolvedThemeKey);
        if (!TargetTheme)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: theme '%s' not found."), *BulkAddThemeKey);
            return;
        }

        if (BulkAddTarget == ERaidBulkAddTarget::ThemeFloorVariations)
        {
            FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
                TargetTheme->FloorVariationIndices,
                BulkAddVariationIndexName,
                bBulkAddCreateMissingTargets);
            if (!TargetGroup)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: floor index '%s' not found in theme '%s'."),
                    *BulkAddVariationIndexName,
                    *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetGroup->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].FloorIndices[%s]"), *ResolvedThemeKey, *TargetGroup->IndexName);
        }
        else if (BulkAddTarget == ERaidBulkAddTarget::ThemeWallVariations)
        {
            FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
                TargetTheme->WallVariationIndices,
                BulkAddVariationIndexName,
                bBulkAddCreateMissingTargets);
            if (!TargetGroup)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: wall index '%s' not found in theme '%s'."),
                    *BulkAddVariationIndexName,
                    *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetGroup->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].WallIndices[%s]"), *ResolvedThemeKey, *TargetGroup->IndexName);
        }
        else if (BulkAddTarget == ERaidBulkAddTarget::ThemeObstacleVariations)
        {
            FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
                TargetTheme->ObstacleVariationIndices,
                BulkAddVariationIndexName,
                bBulkAddCreateMissingTargets);
            if (!TargetGroup)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: obstacle index '%s' not found in theme '%s'."),
                    *BulkAddVariationIndexName,
                    *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetGroup->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].ObstacleIndices[%s]"), *ResolvedThemeKey, *TargetGroup->IndexName);
        }
        else if (BulkAddTarget == ERaidBulkAddTarget::ThemeDecorationVariations)
        {
            FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
                TargetTheme->DecorationVariationIndices,
                BulkAddVariationIndexName,
                bBulkAddCreateMissingTargets);
            if (!TargetGroup)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: decoration index '%s' not found in theme '%s'."),
                    *BulkAddVariationIndexName,
                    *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetGroup->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].DecorationIndices[%s]"), *ResolvedThemeKey, *TargetGroup->IndexName);
        }
        else if (BulkAddTarget == ERaidBulkAddTarget::ThemeDoorBlockerVariations)
        {
            FMeshVariationIndexGroup* TargetGroup = FindOrAddIndexGroupByName(
                TargetTheme->DoorBlockerVariationIndices,
                BulkAddVariationIndexName,
                bBulkAddCreateMissingTargets);
            if (!TargetGroup)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: door blocker index '%s' not found in theme '%s'."),
                    *BulkAddVariationIndexName,
                    *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetGroup->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].DoorBlockerIndices[%s]"), *ResolvedThemeKey, *TargetGroup->IndexName);
        }
        else
        {
            FMeshCluster* TargetCluster = FindOrAddClusterByName(TargetTheme->FoliageClusters, RequestedClusterName, bBulkAddCreateMissingTargets);
            if (!TargetCluster)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: foliage cluster '%s' not found in theme '%s'."),
                    *RequestedClusterName, *ResolvedThemeKey);
                return;
            }
            TargetVariations = &TargetCluster->Variations;
            TargetLabel = FString::Printf(TEXT("Theme[%s].FoliageCluster[%s]"), *ResolvedThemeKey, *TargetCluster->ClusterName);
        }
        break;
    }
    default:
        break;
    }

    if (!TargetVariations)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BulkAdd failed: could not resolve target array."));
        return;
    }

    const FVector SafeDefaultScale(
        FMath::Clamp(BulkAddDefaultScale.X, 0.01f, 10.0f),
        FMath::Clamp(BulkAddDefaultScale.Y, 0.01f, 10.0f),
        FMath::Clamp(BulkAddDefaultScale.Z, 0.01f, 10.0f));
    const float SafeDefaultWeight = FMath::Max(0.01f, BulkAddDefaultSpawnWeight);

    auto BuildVariationFromAsset = [&](const FAssetData& AssetData, FMeshVariation& OutVariation) -> bool
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
    if (ChapterConfig)
    {
        ChapterConfig->Modify();
    }

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
        if (ChapterConfig)
        {
            ChapterConfig->MarkPackageDirty();
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidLayout] BulkAdd done -> %s | Selected=%d Added=%d Duplicates=%d Unsupported=%d"),
        *TargetLabel,
        SelectedAssets.Num(),
        AddedCount,
        DuplicateCount,
        UnsupportedCount);
#endif
}

void ARaidLayoutManager::ClearAllRooms()
{
    if (UWorld* World = GetWorld()) {
        TSet<FString> RegisteredBlueprintClassPaths;
        if (bPurgeRegisteredBlueprintActorsOnClear)
        {
            auto AddBlueprintPathFromVariation = [&RegisteredBlueprintClassPaths](const FMeshVariation& Variation)
                {
                    if (!Variation.BlueprintPrefab.IsNull())
                    {
                        const FString ClassPath = Variation.BlueprintPrefab.ToSoftObjectPath().ToString();
                        if (!ClassPath.IsEmpty())
                        {
                            RegisteredBlueprintClassPaths.Add(ClassPath);
                        }
                    }
                };

            if (ChapterConfig)
            {
                for (const TPair<FString, FModularMeshKit>& ThemePair : ChapterConfig->ThemeRegistry)
                {
                    const FModularMeshKit& ThemeKit = ThemePair.Value;
                    TArray<FMeshVariation> ThemeChannelVariations;
                    auto CollectThemeChannel = [&](ERaidVariationOffsetChannel Channel)
                        {
                            ThemeKit.GetAllRawVariationsForChannel(Channel, ThemeChannelVariations);
                            for (const FMeshVariation& Variation : ThemeChannelVariations)
                            {
                                AddBlueprintPathFromVariation(Variation);
                            }
                        };

                    CollectThemeChannel(ERaidVariationOffsetChannel::Floor);
                    CollectThemeChannel(ERaidVariationOffsetChannel::Wall);
                    CollectThemeChannel(ERaidVariationOffsetChannel::Obstacle);
                    CollectThemeChannel(ERaidVariationOffsetChannel::Decoration);
                    CollectThemeChannel(ERaidVariationOffsetChannel::DoorBlocker);

                    for (const FMeshCluster& Cluster : ThemeKit.FoliageClusters)
                    {
                        for (const FMeshVariation& Variation : Cluster.Variations)
                        {
                            AddBlueprintPathFromVariation(Variation);
                        }
                    }
                }
            }

            for (const FMeshCluster& Cluster : BackgroundClusters)
            {
                for (const FMeshVariation& Variation : Cluster.Variations)
                {
                    AddBlueprintPathFromVariation(Variation);
                }
            }
        }

        for (TActorIterator<ARaidRoomActor> It(World); It; ++It)
        {
            if (ARaidRoomActor* Room = *It)
            {
                Room->ClearAllMeshInstances();
                Room->Destroy();
            }
        }

        int32 DestroyedLegacyTaggedActors = 0;
        int32 DestroyedLegacyAttachedActors = 0;
        int32 DestroyedLegacyRegisteredClassActors = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor == this)
            {
                continue;
            }

            bool bTaggedByRaidRoomGeneration = Actor->ActorHasTag(TEXT("RaidRoomGenerated"));
            bool bHasMeshTypeTag = false;
            if (!bTaggedByRaidRoomGeneration)
            {
                for (const FName& Tag : Actor->Tags)
                {
                    if (Tag.ToString().StartsWith(TEXT("MeshType_")))
                    {
                        bHasMeshTypeTag = true;
                    }
                    if (Tag.ToString().StartsWith(TEXT("RaidRoomNode_")))
                    {
                        bTaggedByRaidRoomGeneration = true;
                    }
                }
            }

            const bool bLegacyDynamicTag =
                bHasMeshTypeTag ||
                Actor->ActorHasTag(TEXT("ObstacleBlueprint")) ||
                Actor->ActorHasTag(TEXT("RaidBackgroundScenery"));

            const bool bOwnedByRoomActor = Cast<ARaidRoomActor>(Actor->GetOwner()) != nullptr;
            const bool bAttachedToRoomActor = Cast<ARaidRoomActor>(Actor->GetAttachParentActor()) != nullptr;
            const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();
            const bool bRegisteredBlueprintClassMatch =
                bPurgeRegisteredBlueprintActorsOnClear &&
                !ActorClassPath.IsEmpty() &&
                RegisteredBlueprintClassPaths.Contains(ActorClassPath);

            if (Actor->ActorHasTag(TEXT("RaidDoorBlocker")) || Actor->ActorHasTag(TEXT("RaidRoadSpline")) || bTaggedByRaidRoomGeneration || bLegacyDynamicTag)
            {
                Actor->Destroy();
                ++DestroyedLegacyTaggedActors;
                continue;
            }

            if (bRegisteredBlueprintClassMatch)
            {
                Actor->Destroy();
                ++DestroyedLegacyRegisteredClassActors;
                continue;
            }

            if (bOwnedByRoomActor || bAttachedToRoomActor)
            {
                Actor->Destroy();
                ++DestroyedLegacyAttachedActors;
            }
        }

        if (DestroyedLegacyTaggedActors > 0 || DestroyedLegacyAttachedActors > 0 || DestroyedLegacyRegisteredClassActors > 0)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidLayout] ClearAllRooms purged legacy actors: Tagged=%d AttachedOrOwnedByRoom=%d RegisteredClass=%d"),
                DestroyedLegacyTaggedActors,
                DestroyedLegacyAttachedActors,
                DestroyedLegacyRegisteredClassActors);
        }
    }

    SpawnedRooms.Empty();
    ClearBackgroundScenery();

    for (AActor* Road : SpawnedRoadActors)
    {
        if (IsValid(Road)) Road->Destroy();
    }
    SpawnedRoadActors.Empty();
}

void ARaidLayoutManager::SpawnRaidLayout()
{
    if (!ChapterConfig || !ChapterConfig->LevelDataTable) return;
    if (bApplyOpenWorldVsmGuardrail)
    {
        ApplyRaidVsmGuardrailCVars();
    }

    ClearAllRooms();
    EnsureBackgroundClustersInitialized();
    URaidCombatSubsystem* CombatSub = GetWorld()->GetSubsystem<URaidCombatSubsystem>();
    if (CombatSub) CombatSub->ResetSubsystem();

    TArray<FLevelNodeRow*> Rows; ChapterConfig->LevelDataTable->GetAllRows<FLevelNodeRow>(TEXT(""), Rows);
    Rows.Sort([](const FLevelNodeRow& A, const FLevelNodeRow& B)
        {
            return A.NodeId < B.NodeId;
        });

    TArray<const USplineComponent*> CachedRoadSplines;
    TArray<FBox2D> CachedRoadFootprints;
    TArray<FRoadSplineWidthSample> CachedRoadWidthSamples;
    if (bAvoidLandscapeSplineRoads)
    {
        GatherRoadSplineComponents(GetWorld(), CachedRoadSplines);
        GatherRoadFootprints(GetWorld(), CachedRoadFootprints);
        GatherLandscapeSplineRoadWidthSamples(GetWorld(), CachedRoadWidthSamples);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Cached road-like spline components: %d footprints: %d widthSamples: %d"),
            CachedRoadSplines.Num(),
            CachedRoadFootprints.Num(),
            CachedRoadWidthSamples.Num());
    }

    TArray<FWaterBodySplineRule> CachedWaterBodySplineRules;
    const float EffectiveOceanWaterBodyEdgeBufferDistance = WaterBodySplineEdgeBufferDistance + WaterBodyOceanExtraBufferDistance;
    const float EffectiveLakeWaterBodyEdgeBufferDistance = WaterBodySplineEdgeBufferDistance + WaterBodyLakeExtraBufferDistance;
    if (bUseWaterBodySplineBoundaryRules)
    {
        GatherWaterBodySplineRules(GetWorld(), CachedWaterBodySplineRules);
        int32 OceanRuleCount = 0;
        int32 LakeRuleCount = 0;
        for (const FWaterBodySplineRule& Rule : CachedWaterBodySplineRules)
        {
            if (Rule.bAllowOnlyInside) ++OceanRuleCount;
            else ++LakeRuleCount;
        }
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] WaterBody spline rules: Total=%d Ocean=%d Lake=%d EdgeBuffer(Base=%.0f Ocean=%.0f Lake=%.0f)"),
            CachedWaterBodySplineRules.Num(),
            OceanRuleCount,
            LakeRuleCount,
            WaterBodySplineEdgeBufferDistance,
            EffectiveOceanWaterBodyEdgeBufferDistance,
            EffectiveLakeWaterBodyEdgeBufferDistance);
    }

    TArray<FBox2D> CachedPreplacedStaticMeshFootprints;
    const float EffectivePreplacedBufferForRooms = FMath::Clamp(PreplacedStaticMeshBufferDistance, 0.0f, 70.0f);
    if (bAvoidPreplacedStaticMeshGeometry)
    {
        GatherPreplacedStaticMeshFootprints(GetWorld(), this, CachedPreplacedStaticMeshFootprints);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Preplaced static geometry cache: Footprints=%d Buffer=%.0f EffectiveRoomBuffer=%.0f"),
            CachedPreplacedStaticMeshFootprints.Num(),
            PreplacedStaticMeshBufferDistance,
            EffectivePreplacedBufferForRooms);
    }

    FBox2D LandscapeSpawnBounds(EForceInit::ForceInit);
    const bool bHasLandscapeSpawnBounds = TryGetLandscapeXYSpawnBounds(GetWorld(), LandscapeSpawnBounds);
    const FVector2D LandscapeSpawnCenter = bHasLandscapeSpawnBounds
        ? LandscapeSpawnBounds.GetCenter()
        : FVector2D(GetActorLocation().X, GetActorLocation().Y);
    const float LandscapeSpawnHalfMinExtent = bHasLandscapeSpawnBounds
        ? 0.5f * FMath::Min(LandscapeSpawnBounds.GetSize().X, LandscapeSpawnBounds.GetSize().Y)
        : 0.0f;
    const float EffectiveRoomLayoutMaxRadius = bHasLandscapeSpawnBounds
        ? FMath::Max(RoomLayoutMaxRadius, LandscapeSpawnHalfMinExtent * 0.95f)
        : RoomLayoutMaxRadius;
    if (bHasLandscapeSpawnBounds)
    {
        const FVector2D BoundsSize = LandscapeSpawnBounds.GetSize();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Landscape spawn domain: Min=(%.0f,%.0f) Max=(%.0f,%.0f) Size=(%.0f,%.0f) EffectiveRoomRadius=%.0f"),
            LandscapeSpawnBounds.Min.X,
            LandscapeSpawnBounds.Min.Y,
            LandscapeSpawnBounds.Max.X,
            LandscapeSpawnBounds.Max.Y,
            BoundsSize.X,
            BoundsSize.Y,
            EffectiveRoomLayoutMaxRadius);
        if (bUseWaterBodySplineBoundaryRules && CachedWaterBodySplineRules.Num() > 0)
        {
            const float AllowedRatio = EstimateWaterRuleAllowedRatioOnBounds(
                LandscapeSpawnBounds,
                CachedWaterBodySplineRules,
                WaterBodySplineEdgeBufferDistance,
                WaterBodyOceanExtraBufferDistance,
                WaterBodyLakeExtraBufferDistance,
                48);
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidLayout] Water rule allowed area over landscape: %.1f%%"),
                AllowedRatio * 100.0f);
        }
    }

    float MinX = TNumericLimits<float>::Max();
    float MaxX = -TNumericLimits<float>::Max();
    float MinY = TNumericLimits<float>::Max();
    float MaxY = -TNumericLimits<float>::Max();
    int32 ValidCount = 0;
    int32 OutdoorHints = 0;
    int32 IndoorHints = 0;

    for (FLevelNodeRow* Row : Rows)
    {
        if (!Row) continue;
        ValidCount++;
        MinX = FMath::Min(MinX, Row->PosX);
        MaxX = FMath::Max(MaxX, Row->PosX);
        MinY = FMath::Min(MinY, Row->PosY);
        MaxY = FMath::Max(MaxY, Row->PosY);

        const FString Meta = (Row->EnvType + TEXT(" ") + Row->Theme + TEXT(" ") + Row->NodeTags + TEXT(" ") + Row->RoomRole).ToLower();
        if (Meta.Contains(TEXT("openworld")) || Meta.Contains(TEXT("open world")) || Meta.Contains(TEXT("outdoor")) ||
            Meta.Contains(TEXT("오픈월드")) || Meta.Contains(TEXT("야외")) || Row->EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
            Row->EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase))
        {
            OutdoorHints++;
        }
        if (Meta.Contains(TEXT("tarkov")) || Meta.Contains(TEXT("cqb")) || Meta.Contains(TEXT("indoor")) ||
            Meta.Contains(TEXT("factory")) || Meta.Contains(TEXT("warehouse")) || Meta.Contains(TEXT("mall")) ||
            Meta.Contains(TEXT("실내")) || Meta.Contains(TEXT("타르코프")) || Row->EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase))
        {
            IndoorHints++;
        }
    }

    const float SpanX = (ValidCount > 0) ? (MaxX - MinX) : 0.0f;
    const float SpanY = (ValidCount > 0) ? (MaxY - MinY) : 0.0f;
    const bool bLooksCollapsed = ValidCount >= 3 && SpanX < 1000.0f && SpanY < 1000.0f;
    const bool bOpenWorldIntent = OutdoorHints >= IndoorHints;

    TMap<int32, FVector2D> AutoPosOverrides;
    if (bLooksCollapsed)
    {
        const float Spread = bOpenWorldIntent
            ? FMath::Clamp(EffectiveRoomLayoutMaxRadius * 0.90f, 32000.0f, 220000.0f)
            : FMath::Clamp(EffectiveRoomLayoutMaxRadius * 0.55f, 12000.0f, 140000.0f);
        int32 Index = 0;
        for (FLevelNodeRow* Row : Rows)
        {
            if (!Row) continue;

            const float T = (float)Index / (float)FMath::Max(1, Rows.Num() - 1);
            const float Angle = (float)Index * 2.39996323f;
            const float Radius = bOpenWorldIntent
                ? FMath::Sqrt(FMath::Max(0.08f, T)) * Spread
                : FMath::Lerp(2200.0f, Spread * 0.88f, T);
            FVector2D Pos(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);

            if (Row->RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
            {
                Pos = FVector2D(-Spread * 0.48f, -Spread * 0.12f);
            }
            else if (Row->NodeId == 1 && !Row->RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
            {
                // 1번 룸은 Start에서 가장 먼저 접근 가능한 초반 목표가 되도록 가깝게 배치.
                Pos = FVector2D(-Spread * 0.36f, -Spread * 0.08f);
            }
            else if (Row->RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase))
            {
                Pos = FVector2D(Spread * 0.48f, Spread * 0.12f);
            }
            else if (Row->RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
            {
                Pos = FVector2D(Spread * 0.34f, bOpenWorldIntent ? Spread * 0.24f : Spread * 0.06f);
            }

            AutoPosOverrides.Add(Row->NodeId, Pos);
            Index++;
        }

        UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Auto redistributed collapsed node positions (%s mode)."),
            bOpenWorldIntent ? TEXT("OpenWorld") : TEXT("CQB"));
    }

    auto ResolvePlannedXY = [&](const FLevelNodeRow* Row) -> FVector2D
        {
            if (!Row)
            {
                return FVector2D::ZeroVector;
            }
            if (const FVector2D* Overridden = AutoPosOverrides.Find(Row->NodeId))
            {
                return *Overridden;
            }
            return FVector2D(Row->PosX, Row->PosY);
        };

    FVector2D LayoutCenter2D = FVector2D::ZeroVector;
    float LayoutScale = 1.0f;
    if (ValidCount > 0)
    {
        float LocalMinX = TNumericLimits<float>::Max();
        float LocalMaxX = -TNumericLimits<float>::Max();
        float LocalMinY = TNumericLimits<float>::Max();
        float LocalMaxY = -TNumericLimits<float>::Max();

        for (const FLevelNodeRow* Row : Rows)
        {
            if (!Row) continue;
            const FVector2D Planned = ResolvePlannedXY(Row);
            LocalMinX = FMath::Min(LocalMinX, Planned.X);
            LocalMaxX = FMath::Max(LocalMaxX, Planned.X);
            LocalMinY = FMath::Min(LocalMinY, Planned.Y);
            LocalMaxY = FMath::Max(LocalMaxY, Planned.Y);
        }

        LayoutCenter2D = FVector2D((LocalMinX + LocalMaxX) * 0.5f, (LocalMinY + LocalMaxY) * 0.5f);

        float MaxDistFromCenter = 0.0f;
        for (const FLevelNodeRow* Row : Rows)
        {
            if (!Row) continue;
            const FVector2D Planned = ResolvePlannedXY(Row);
            MaxDistFromCenter = FMath::Max(MaxDistFromCenter, FVector2D::Distance(Planned, LayoutCenter2D));
        }

        const float AllowedRadius = FMath::Max(5000.0f, EffectiveRoomLayoutMaxRadius);
        const bool bCanUpscaleOpenWorldLayout =
            bOpenWorldIntent &&
            ValidCount >= 6 &&
            MaxDistFromCenter > 100.0f;
        if (bCanUpscaleOpenWorldLayout)
        {
            const float DesiredRadius = FMath::Clamp(AllowedRadius * 0.80f, 24000.0f, AllowedRadius);
            if (MaxDistFromCenter < DesiredRadius * 0.55f)
            {
                LayoutScale = DesiredRadius / MaxDistFromCenter;
                UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Room layout scaled up to %.3f (MaxDist=%.1f, Desired=%.1f, Allowed=%.1f)"),
                    LayoutScale, MaxDistFromCenter, DesiredRadius, AllowedRadius);
            }
        }
        if (MaxDistFromCenter > AllowedRadius)
        {
            LayoutScale = AllowedRadius / MaxDistFromCenter;
            UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Room layout scaled down to %.3f (MaxDist=%.1f, Allowed=%.1f)"),
                LayoutScale, MaxDistFromCenter, AllowedRadius);
        }
    }

    auto ToWorldIdealLocation = [&](const FVector2D& PlannedXY) -> FVector
        {
            FVector2D LocalXY = PlannedXY;
            if (bCenterRoomLayoutAroundManager)
            {
                LocalXY -= LayoutCenter2D;
            }
            LocalXY *= LayoutScale;
            return FVector(LandscapeSpawnCenter.X + LocalXY.X, LandscapeSpawnCenter.Y + LocalXY.Y, GetActorLocation().Z);
        };

    constexpr float RoomToRoomMinSpacing = 1800.0f;
    auto IsRoomCandidateTooCloseToExistingRooms = [&](const FVector& CandidateLocation, float CandidateHalfExtentUU) -> bool
        {
            for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& ExistingPair : SpawnedRooms)
            {
                const ARaidRoomActor* ExistingRoom = ExistingPair.Value.Get();
                if (!IsValid(ExistingRoom))
                {
                    continue;
                }

                const FVector ExistingLocation = ExistingRoom->GetActorLocation();
                const FVector ExistingExtent = ExistingRoom->GetRoomExtent();
                const float ExistingHalfExtent = FMath::Max(100.0f, FMath::Max(ExistingExtent.X, ExistingExtent.Y));
                const float RequiredDistance = CandidateHalfExtentUU + ExistingHalfExtent + RoomToRoomMinSpacing;
                if (FVector::DistSquaredXY(CandidateLocation, ExistingLocation) < FMath::Square(RequiredDistance))
                {
                    return true;
                }
            }
            return false;
        };

    auto TryFindSafeGroundAround = [&](const FVector& Origin, float SearchRadius, float RoomHalfExtentUU, float AngleOffsetRadians, const FCollisionQueryParams& QueryParams, FVector& OutLocation) -> bool
        {
            const float SafeRadius = FMath::Max(500.0f, SearchRadius);
            constexpr int32 Rings = 10;
            for (int32 Ring = 0; Ring <= Rings; ++Ring)
            {
                const float Alpha = (float)Ring / (float)Rings;
                const float Radius = Alpha * SafeRadius;
                const int32 SampleCount = FMath::Max(8, 10 + Ring * 6);
                for (int32 Sample = 0; Sample < SampleCount; ++Sample)
                {
                    const float Angle = ((2.0f * PI * (float)Sample) / (float)SampleCount) + (Ring * 0.37f) + AngleOffsetRadians;
                    const FVector TestLoc = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
                    FHitResult HitResult;
                    if (!TryResolveGroundHit(GetWorld(), TestLoc, bPreferLandscapeGroundHit, true, QueryParams, HitResult))
                    {
                        continue;
                    }
                    if (IsRoomFootprintNearWater(
                        GetWorld(),
                        HitResult.ImpactPoint,
                        RoomHalfExtentUU,
                        WaterAvoidanceRadius,
                        bUseWaterVolumeForSpawnAvoidance,
                        ShorelineSpawnBufferDistance,
                        ShorelineProbeSampleCount,
                        bUseWaterVolumeInShorelineProbe,
                        QueryParams))
                    {
                        continue;
                    }
                    if (bAvoidLandscapeSplineRoads &&
                        IsRoomFootprintNearLandscapeSplineRoad(
                            GetWorld(),
                            HitResult.ImpactPoint,
                            RoomHalfExtentUU,
                            LandscapeSplineRoadAvoidanceRadius,
                            QueryParams,
                            &CachedRoadSplines,
                            &CachedRoadFootprints,
                            &CachedRoadWidthSamples))
                    {
                        continue;
                    }

                    const FBox2D CandidateRoomFootprint(
                        FVector2D(HitResult.ImpactPoint.X - RoomHalfExtentUU, HitResult.ImpactPoint.Y - RoomHalfExtentUU),
                        FVector2D(HitResult.ImpactPoint.X + RoomHalfExtentUU, HitResult.ImpactPoint.Y + RoomHalfExtentUU));
                    if (bUseWaterBodySplineBoundaryRules &&
                        (IsLocationBlockedByWaterBodySplineRules(
                            HitResult.ImpactPoint,
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance) ||
                         IsFootprintBlockedByWaterBodySplineRules(
                            CandidateRoomFootprint,
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance)))
                    {
                        continue;
                    }

                    if (bAvoidPreplacedStaticMeshGeometry &&
                        IsRoomFootprintOverlappingFootprints(
                            HitResult.ImpactPoint,
                            RoomHalfExtentUU,
                            EffectivePreplacedBufferForRooms,
                            CachedPreplacedStaticMeshFootprints))
                    {
                        continue;
                    }
                    if (IsRoomCandidateTooCloseToExistingRooms(HitResult.ImpactPoint, RoomHalfExtentUU))
                    {
                        continue;
                    }

                    OutLocation = HitResult.ImpactPoint;
                    return true;
                }
            }
            return false;
        };

    ARaidRoomActor* StartRoom = nullptr;

    auto EstimateRoomHalfExtentUU = [](const FLevelNodeRow& InRow, TSubclassOf<ARaidRoomActor> InRoomClass) -> float
        {
            float TileSizeLocal = 400.0f;
            int32 GridSizeLocal = 13;
            if (InRoomClass)
            {
                if (const ARaidRoomActor* CDO = InRoomClass->GetDefaultObject<ARaidRoomActor>())
                {
                    TileSizeLocal = FMath::Max(50.0f, CDO->TileSize);
                    GridSizeLocal = FMath::Max(1, CDO->GridSize);
                }
            }

            if (InRow.RoomSize.Equals(TEXT("Small"), ESearchCase::IgnoreCase))
            {
                GridSizeLocal = 9;
            }
            else if (InRow.RoomSize.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
            {
                GridSizeLocal = 13;
            }
            else if (InRow.RoomSize.Equals(TEXT("Large"), ESearchCase::IgnoreCase))
            {
                GridSizeLocal = 21;
            }
            else if (InRow.RoomSize.Equals(TEXT("Massive"), ESearchCase::IgnoreCase))
            {
                GridSizeLocal = 31;
            }

            return 0.5f * (float)GridSizeLocal * TileSizeLocal;
        };

    for (FLevelNodeRow* Row : Rows)
    {
        if (!Row || SpawnedRooms.Contains(Row->NodeId)) continue;
        FLevelNodeRow EffectiveRow = *Row;
        if (ChapterConfig)
        {
            FString ResolvedThemeKey;
            const FModularMeshKit* ResolvedThemeKit = nullptr;
            if (ChapterConfig->ResolveThemeKitForNode(EffectiveRow, ResolvedThemeKey, ResolvedThemeKit) && !ResolvedThemeKey.IsEmpty())
            {
                EffectiveRow.Theme = ResolvedThemeKey;
            }
        }

        TSubclassOf<ARaidRoomActor> ClassToSpawn = ChapterConfig->RoomClass;
        if (ChapterConfig->PrefabRegistry && !EffectiveRow.RoomPrefabId.IsEmpty()) {
            if (TSubclassOf<ARaidRoomActor> PrefabClass = ChapterConfig->PrefabRegistry->Resolve(EffectiveRow.RoomPrefabId)) ClassToSpawn = PrefabClass;
        }
        if (!ClassToSpawn) continue;
        const float RoomHalfExtentUU = EstimateRoomHalfExtentUU(EffectiveRow, ClassToSpawn);
        const float NodeAngleOffsetRadians = FMath::DegreesToRadians(FMath::Fmod((float)EffectiveRow.NodeId * 137.50776f, 360.0f));

        const FVector2D SpawnXY = ResolvePlannedXY(Row);
        const FVector IdealLoc = ToWorldIdealLocation(SpawnXY);
        FVector FinalSpawnLoc = IdealLoc;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidRoomSpawnGround), false);
        QueryParams.bTraceComplex = false;
        QueryParams.AddIgnoredActor(this);
        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Existing : SpawnedRooms)
        {
            if (Existing.Value) QueryParams.AddIgnoredActor(Existing.Value);
        }

        bool bFoundSafeSpot = false;

        for (int32 Radius = 0; Radius <= 16000; Radius += 1000) {
            int32 AngleStep = (Radius == 0) ? 360 : 45;
            for (int32 Angle = 0; Angle < 360; Angle += AngleStep) {
                float Rad = FMath::DegreesToRadians((float)Angle) + NodeAngleOffsetRadians;
                FVector TestLoc = IdealLoc + FVector(FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius, 0.0f);
                FHitResult HitResult;
                if (TryResolveGroundHit(GetWorld(), TestLoc, bPreferLandscapeGroundHit, true, QueryParams, HitResult))
                {
                    const FVector HitPoint = HitResult.ImpactPoint;
                    const bool bNearWater = IsRoomFootprintNearWater(
                        GetWorld(),
                        HitPoint,
                        RoomHalfExtentUU,
                        WaterAvoidanceRadius,
                        bUseWaterVolumeForSpawnAvoidance,
                        ShorelineSpawnBufferDistance,
                        ShorelineProbeSampleCount,
                        bUseWaterVolumeInShorelineProbe,
                        QueryParams);
                    const bool bNearRoad = bAvoidLandscapeSplineRoads &&
                        IsRoomFootprintNearLandscapeSplineRoad(
                            GetWorld(),
                            HitPoint,
                            RoomHalfExtentUU,
                            LandscapeSplineRoadAvoidanceRadius,
                            QueryParams,
                            &CachedRoadSplines,
                            &CachedRoadFootprints,
                            &CachedRoadWidthSamples);
                    const FBox2D CandidateRoomFootprint(
                        FVector2D(HitPoint.X - RoomHalfExtentUU, HitPoint.Y - RoomHalfExtentUU),
                        FVector2D(HitPoint.X + RoomHalfExtentUU, HitPoint.Y + RoomHalfExtentUU));
                    const bool bBlockedByWaterBodyBoundary =
                        bUseWaterBodySplineBoundaryRules &&
                        (IsLocationBlockedByWaterBodySplineRules(
                            HitPoint,
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance) ||
                         IsFootprintBlockedByWaterBodySplineRules(
                            CandidateRoomFootprint,
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance));
                    const bool bBlockedByPreplacedGeometry =
                        bAvoidPreplacedStaticMeshGeometry &&
                        IsRoomFootprintOverlappingFootprints(
                            HitPoint,
                            RoomHalfExtentUU,
                            EffectivePreplacedBufferForRooms,
                            CachedPreplacedStaticMeshFootprints);
                    const bool bTooCloseToExistingRoom =
                        IsRoomCandidateTooCloseToExistingRooms(HitPoint, RoomHalfExtentUU);
                    if (!bNearWater && !bNearRoad && !bBlockedByWaterBodyBoundary && !bBlockedByPreplacedGeometry && !bTooCloseToExistingRoom)
                    {
                        FinalSpawnLoc = HitPoint;
                        bFoundSafeSpot = true;
                        break;
                    }
                }
            }
            if (bFoundSafeSpot) break;
        }

        if (!bFoundSafeSpot) {
            if (!TryFindSafeGroundAround(
                IdealLoc,
                FMath::Max(12000.0f, EffectiveRoomLayoutMaxRadius * 0.35f),
                RoomHalfExtentUU,
                NodeAngleOffsetRadians,
                QueryParams,
                FinalSpawnLoc))
            {
                if (!TryFindSafeGroundAround(
                    FVector(LandscapeSpawnCenter.X, LandscapeSpawnCenter.Y, GetActorLocation().Z),
                    FMath::Max(EffectiveRoomLayoutMaxRadius, BackgroundRadius * 0.75f),
                    RoomHalfExtentUU,
                    NodeAngleOffsetRadians,
                    QueryParams,
                    FinalSpawnLoc))
                {
                    UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Failed to place Room %d on safe ground. Skipping spawn."),
                        Row->NodeId);
                    continue;
                }
            }
        }

        FTransform SpawnTransform(FRotator::ZeroRotator, FinalSpawnLoc);
        if (ARaidRoomActor* NewRoom = GetWorld()->SpawnActorDeferred<ARaidRoomActor>(ClassToSpawn, SpawnTransform)) {
            NewRoom->SetNodeData(EffectiveRow.NodeId, EffectiveRow, ChapterConfig);
            ApplyRoomOptimizationToRoom(NewRoom, false);
            NewRoom->FinishSpawning(SpawnTransform);
            SpawnedRooms.Add(EffectiveRow.NodeId, NewRoom);
            if (CombatSub) CombatSub->RegisterRoom(NewRoom);
            if (EffectiveRow.RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
            {
                StartRoom = NewRoom;
            }
#if WITH_EDITOR
            NewRoom->SetActorLabel(FString::Printf(TEXT("Room_%02d_[%s]"), EffectiveRow.NodeId, *EffectiveRow.RoomType));
#endif
        }
    }
    ConnectRoomDoors(); ScatterBackgroundScenery();
    // =========================================================
    // [핵심 추가] 맵 생성이 끝나면 콤파스 시스템을 강제로 1회 초기화!
    // =========================================================
    if (CombatSub)
    {
        if (!StartRoom)
        {
            for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : SpawnedRooms)
            {
                ARaidRoomActor* Room = Pair.Value.Get();
                if (!Room) continue;
                if (Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
                {
                    StartRoom = Room;
                    break;
                }
            }
        }
        CombatSub->UpdateCompassForNextRooms(StartRoom);

        if (!bEnableDynamicWavesFromLayoutManager)
        {
            CombatSub->ConfigureDynamicWaves(false, 1, 0.0f, 60.0f, 1);
        }
        else
        {
            const URaidWaveProfile* WaveProfileToApply = nullptr;
            if (IsValid(WaveProfileOverride))
            {
                WaveProfileToApply = WaveProfileOverride;
            }
            else if (ChapterConfig && IsValid(ChapterConfig->WaveProfile))
            {
                WaveProfileToApply = ChapterConfig->WaveProfile;
            }

            CombatSub->ApplyWaveProfile(WaveProfileToApply);
        }
    }
}

void ARaidLayoutManager::GenerateRoadSplineNetwork(const FString& DominantEnv)
{
    if (SpawnedRooms.Num() < 2) return;

    int32 OutdoorVotes = 0;
    int32 UrbanVotes = 0;
    int32 IndoorKeywordVotes = 0;
    float MinX = TNumericLimits<float>::Max();
    float MaxX = -TNumericLimits<float>::Max();
    float MinY = TNumericLimits<float>::Max();
    float MaxY = -TNumericLimits<float>::Max();
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : SpawnedRooms)
    {
        ARaidRoomActor* Room = Pair.Value.Get();
        if (!Room) continue;
        const FLevelNodeRow& Row = Room->GetNodeRow();
        const FVector Loc = Room->GetActorLocation();
        MinX = FMath::Min(MinX, Loc.X);
        MaxX = FMath::Max(MaxX, Loc.X);
        MinY = FMath::Min(MinY, Loc.Y);
        MaxY = FMath::Max(MaxY, Loc.Y);
        const FString Meta = (Row.EnvType + TEXT(" ") + Row.Theme + TEXT(" ") + Row.NodeTags + TEXT(" ") + Row.RoomRole).ToLower();

        if (Meta.Contains(TEXT("openworld")) || Meta.Contains(TEXT("open world")) || Meta.Contains(TEXT("outdoor")) ||
            Meta.Contains(TEXT("오픈월드")) || Meta.Contains(TEXT("야외")) ||
            Row.EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) || Row.EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase))
        {
            OutdoorVotes++;
        }
        if (Row.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase))
        {
            UrbanVotes++;
        }
        if (Meta.Contains(TEXT("tarkov")) || Meta.Contains(TEXT("cqb")) || Meta.Contains(TEXT("indoor")) ||
            Meta.Contains(TEXT("factory")) || Meta.Contains(TEXT("warehouse")) || Meta.Contains(TEXT("mall")) ||
            Meta.Contains(TEXT("실내")) || Meta.Contains(TEXT("타르코프")))
        {
            IndoorKeywordVotes++;
        }
    }

    const float SpanX = MaxX - MinX;
    const float SpanY = MaxY - MinY;
    const bool bCompactLayout = SpanX < 35000.0f && SpanY < 35000.0f;
    const bool bIndoorFocused = (IndoorKeywordVotes > 0) || ((UrbanVotes > OutdoorVotes + 1) && bCompactLayout);
    if (bIndoorFocused)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] Skipping outdoor road splines for indoor/CQB-focused layout."));
        return;
    }

    for (auto& Pair : SpawnedRooms) {
        ARaidRoomActor* RoomA = Pair.Value; if (!RoomA) continue;
        int32 IdA = RoomA->GetNodeId(); TArray<int32> ConnectedIds = RoomA->GetNodeRow().GetConnectionIds();
        for (int32 IdB : ConnectedIds) {
            if (IdA >= IdB || !SpawnedRooms.Contains(IdB)) continue;
            ARaidRoomActor* RoomB = SpawnedRooms[IdB]; if (!RoomB) continue;
            FVector StartPos = RoomA->GetActorLocation(); FVector EndPos = RoomB->GetActorLocation();
            float Distance = FVector::Dist2D(StartPos, EndPos);
            if (Distance > 20000.0f || Distance < 1000.0f) continue;

            FActorSpawnParameters SpawnParams; SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            SpawnParams.ObjectFlags |= ResolveRaidSpawnObjectFlags(GetWorld());
            AActor* RoadActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
            if (!RoadActor) continue;
            RoadActor->Tags.AddUnique(FName(TEXT("RaidRoadSpline")));
#if WITH_EDITOR
            RoadActor->SetActorLabel(FString::Printf(TEXT("Road_Path_%02d_to_%02d"), IdA, IdB));
#endif
            USplineComponent* RoadSpline = NewObject<USplineComponent>(RoadActor);
            RoadActor->SetRootComponent(RoadSpline); RoadSpline->RegisterComponent(); RoadSpline->SetMobility(EComponentMobility::Static); RoadSpline->ClearSplinePoints();
            SpawnedRoadActors.Add(RoadActor);

            int32 NumSteps = FMath::Max(2, FMath::CeilToInt(Distance / 500.0f));
            for (int32 step = 0; step <= NumSteps; ++step) {
                float Alpha = (float)step / (float)NumSteps;
                FVector BasePos = FMath::Lerp(StartPos, EndPos, Alpha);
                if (step > 0 && step < NumSteps) { BasePos.X += FMath::RandRange(-300.0f, 300.0f); BasePos.Y += FMath::RandRange(-300.0f, 300.0f); }

                auto TryResolveRoadPoint = [&](const FVector& CandidateXY, FVector& OutPoint) -> bool
                    {
                        if (!GetWorld())
                        {
                            return false;
                        }

                        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidRoadSplineGroundSnap), false);
                        QueryParams.bTraceComplex = false;
                        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& RoomPair : SpawnedRooms)
                        {
                            if (RoomPair.Value)
                            {
                                QueryParams.AddIgnoredActor(RoomPair.Value);
                            }
                        }

                        FVector ResolvedPoint = CandidateXY;
                        FHitResult GroundHit;
                        if (TryResolveGroundHit(GetWorld(), CandidateXY, true, true, QueryParams, GroundHit))
                        {
                            if (IsHitWaterLocation(GroundHit) || IsInsideWaterPhysicsVolume(GetWorld(), GroundHit.ImpactPoint, 80.0f))
                            {
                                return false;
                            }

                            ResolvedPoint.Z = GroundHit.ImpactPoint.Z + 20.0f;
                        }
                        else
                        {
                            ResolvedPoint.Z = FMath::Lerp(StartPos.Z, EndPos.Z, Alpha) + 35.0f;
                        }

                        // Roads should not pass through room obstacle blueprints/obstacle meshes.
                        if (IsPointNearObstacleTaggedGeometry(GetWorld(), ResolvedPoint, 360.0f))
                        {
                            return false;
                        }

                        OutPoint = ResolvedPoint;
                        return true;
                    };

                FVector FinalSplinePoint = BasePos;
                if (!TryResolveRoadPoint(BasePos, FinalSplinePoint))
                {
                    const FVector Dir2D = (EndPos - StartPos).GetSafeNormal2D();
                    const FVector Perp2D(-Dir2D.Y, Dir2D.X, 0.0f);
                    static const float LateralOffsets[] = { 550.0f, -550.0f, 900.0f, -900.0f, 1300.0f, -1300.0f };

                    bool bFoundDetour = false;
                    for (const float Offset : LateralOffsets)
                    {
                        const FVector Candidate = BasePos + (Perp2D * Offset);
                        if (TryResolveRoadPoint(Candidate, FinalSplinePoint))
                        {
                            bFoundDetour = true;
                            break;
                        }
                    }

                    if (!bFoundDetour)
                    {
                        FinalSplinePoint.Z = FMath::Lerp(StartPos.Z, EndPos.Z, Alpha) + 50.0f;
                    }
                }

                RoadSpline->AddSplinePoint(FinalSplinePoint, ESplineCoordinateSpace::World, true);
            }
            for (int32 pt = 0; pt < RoadSpline->GetNumberOfSplinePoints(); ++pt) RoadSpline->SetSplinePointType(pt, ESplinePointType::Curve, true);
            RoadSpline->UpdateSpline();
        }
    }
}

void ARaidLayoutManager::ScatterBackgroundScenery()
{
    ClearBackgroundScenery();
    EnsureBackgroundClustersInitialized();
    if (BackgroundClusters.Num() == 0) return;

    auto IsWindTreeCluster = [this](const FMeshCluster& Cluster) -> bool
        {
            if (
                Cluster.ClusterName.Contains(TEXT("Tree"), ESearchCase::IgnoreCase) ||
                Cluster.ClusterName.Contains(TEXT("Sapling"), ESearchCase::IgnoreCase))
            {
                return true;
            }

            // Theme-generated cluster names may not include "Tree" in the label.
            return ClusterContainsTreeLikeVariation(Cluster);
        };

    struct FBackgroundISMCDistanceBandRuntime
    {
        int32 CullStart = 0;
        int32 CullEnd = 0;
        bool bCastShadow = true;
    };

    auto SanitizeCullPair = [](int32 InStart, int32 InEnd, int32& OutStart, int32& OutEnd)
        {
            OutStart = FMath::Max(0, InStart);
            OutEnd = FMath::Max(0, InEnd);
            if (OutEnd > 0 && OutEnd < OutStart)
            {
                OutEnd = OutStart;
            }
        };

    const float SafeNearMaxDistance = FMath::Max(1000.0f, BackgroundISMCNearMaxDistance);
    const float SafeMidMaxDistance = FMath::Max(SafeNearMaxDistance + 500.0f, BackgroundISMCMidMaxDistance);
    const float NearBandMaxDistanceSq = FMath::Square(SafeNearMaxDistance);
    const float MidBandMaxDistanceSq = FMath::Square(SafeMidMaxDistance);

    FBackgroundISMCDistanceBandRuntime DistanceBandRuntime[3];
    if (bEnableBackgroundISMCDistanceBands)
    {
        SanitizeCullPair(BackgroundISMCNearCullStart, BackgroundISMCNearCullEnd, DistanceBandRuntime[0].CullStart, DistanceBandRuntime[0].CullEnd);
        SanitizeCullPair(BackgroundISMCMidCullStart, BackgroundISMCMidCullEnd, DistanceBandRuntime[1].CullStart, DistanceBandRuntime[1].CullEnd);
        SanitizeCullPair(BackgroundISMCFarCullStart, BackgroundISMCFarCullEnd, DistanceBandRuntime[2].CullStart, DistanceBandRuntime[2].CullEnd);
        DistanceBandRuntime[0].bCastShadow = bBackgroundISMCNearCastShadow;
        DistanceBandRuntime[1].bCastShadow = bBackgroundISMCMidCastShadow;
        DistanceBandRuntime[2].bCastShadow = bBackgroundISMCFarCastShadow;
    }
    else
    {
        DistanceBandRuntime[0] = FBackgroundISMCDistanceBandRuntime();
        DistanceBandRuntime[1] = DistanceBandRuntime[0];
        DistanceBandRuntime[2] = DistanceBandRuntime[0];
    }

    static const FName DistanceBandTags[3] =
    {
        FName(TEXT("RaidBGDist_Near")),
        FName(TEXT("RaidBGDist_Mid")),
        FName(TEXT("RaidBGDist_Far"))
    };

    TMap<uint64, UHierarchicalInstancedStaticMeshComponent*> MeshShadowToISMC;
    MeshShadowToISMC.Reserve(256);
    auto MakeMeshShadowKey = [](const UStaticMesh* InMesh, bool bCastShadow, bool bNoCollision, int32 DistanceBandIndex) -> uint64
        {
            const uint64 MeshBits = static_cast<uint64>(reinterpret_cast<UPTRINT>(InMesh));
            const uint64 ShadowBits = bCastShadow ? 1ull : 0ull;
            const uint64 CollisionBits = bNoCollision ? 1ull : 0ull;
            const uint64 BandBits = static_cast<uint64>(FMath::Clamp(DistanceBandIndex, 0, 3));
            return (MeshBits << 4) | ShadowBits | (CollisionBits << 1) | (BandBits << 2);
        };

    auto GetOrCreateBackgroundISMC =
        [&](UStaticMesh* LoadedMesh, const FMeshCluster& SourceCluster, bool bNoCollision, bool bBaseCastShadow, int32 DistanceBandIndex) -> UHierarchicalInstancedStaticMeshComponent*
        {
            if (!IsValid(LoadedMesh))
            {
                return nullptr;
            }

            const int32 SafeBandIndex = FMath::Clamp(DistanceBandIndex, 0, 2);
            const FName BandTag = DistanceBandTags[SafeBandIndex];
            const FBackgroundISMCDistanceBandRuntime& BandRuntime = DistanceBandRuntime[SafeBandIndex];
            const bool bCastShadow = bBaseCastShadow && BandRuntime.bCastShadow;
            const int32 CullStart = BandRuntime.CullStart;
            const int32 CullEnd = BandRuntime.CullEnd;
            const FName CollisionProfileName = bNoCollision ? FName(TEXT("NoCollision")) : FName(TEXT("BlockAll"));
            const uint64 Key = MakeMeshShadowKey(LoadedMesh, bCastShadow, bNoCollision, SafeBandIndex);
            if (UHierarchicalInstancedStaticMeshComponent** Found = MeshShadowToISMC.Find(Key))
            {
                if (IsValid(*Found))
                {
                    return *Found;
                }
            }

            for (UHierarchicalInstancedStaticMeshComponent* ExistingISMC : BackgroundISMC_Pool)
            {
                if (IsValid(ExistingISMC) &&
                    ExistingISMC->GetStaticMesh() == LoadedMesh &&
                    ExistingISMC->CastShadow == bCastShadow &&
                    ExistingISMC->GetCollisionProfileName() == CollisionProfileName &&
                    ExistingISMC->ComponentTags.Contains(BandTag))
                {
                    MeshShadowToISMC.Add(Key, ExistingISMC);
                    return ExistingISMC;
                }
            }

            UHierarchicalInstancedStaticMeshComponent* ISMC =
                NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional);
            if (!IsValid(ISMC))
            {
                return nullptr;
            }

            UWorld* WorldForISMC = GetWorld();
            const bool bPersistEditorInstance = (WorldForISMC && !WorldForISMC->IsGameWorld());

            ISMC->CreationMethod = EComponentCreationMethod::Instance;
            ISMC->SetStaticMesh(LoadedMesh);
            ISMC->SetupAttachment(RootComponent);
            ISMC->SetMobility(EComponentMobility::Static);
            ISMC->SetCollisionProfileName(CollisionProfileName);
            ISMC->SetCanEverAffectNavigation(false);
            ISMC->SetCastShadow(bCastShadow);
            ISMC->bCastDynamicShadow = bCastShadow;
            ISMC->bCastStaticShadow = bCastShadow;
            ISMC->bAffectDistanceFieldLighting = bCastShadow;
            ISMC->SetCullDistances(CullStart, CullEnd);
            ISMC->SetNumCustomDataFloats(2);
            ISMC->ComponentTags.AddUnique(BandTag);
            ISMC->ComponentTags.AddUnique(FName(TEXT("RaidBackgroundScenery")));

            if (LoadedMesh->GetPathName().StartsWith(TEXT("/Engine/")))
            {
                UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
                if (BaseMat)
                {
                    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
                    FLinearColor Tint = FLinearColor(0.15f, 0.4f, 0.15f, 1.0f);
                    if (SourceCluster.ClusterName.Contains(TEXT("Rock"))) Tint = FLinearColor(0.4f, 0.25f, 0.15f, 1.0f);
                    else if (SourceCluster.ClusterName.Contains(TEXT("Bush"))) Tint = FLinearColor(0.3f, 0.6f, 0.2f, 1.0f);
                    else if (SourceCluster.ClusterName.Contains(TEXT("Structure"))) Tint = FLinearColor(0.4f, 0.4f, 0.45f, 1.0f);
                    MID->SetVectorParameterValue(TEXT("Color"), Tint);
                    MID->SetVectorParameterValue(TEXT("BaseColor"), Tint);
                    MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                    ISMC->SetMaterial(0, MID);
                }
            }

            if (bPersistEditorInstance)
            {
                AddInstanceComponent(ISMC);
                ISMC->OnComponentCreated();
                ISMC->SetFlags(RF_Transactional);
            }

            ISMC->RegisterComponent();
            BackgroundISMC_Pool.Add(ISMC);
            MeshShadowToISMC.Add(Key, ISMC);
            return ISMC;
        };

    for (const FMeshCluster& Cluster : BackgroundClusters)
    {
        const bool bNoCollision = Cluster.ClusterName.Contains(TEXT("NoCol"));
        const bool bWindTreeCluster = IsWindTreeCluster(Cluster);
        const bool bNoCollisionForClusterInstances =
            bNoCollision || (bReduceShadowsForInstancedBackgroundTrees && bWindTreeCluster);
        const bool bCastShadowForClusterInstances =
            !((bReduceShadowsForNoCollisionBackground && bNoCollision) ||
                (bReduceShadowsForInstancedBackgroundTrees && bWindTreeCluster));
        const int32 BandPrecreateCount = bEnableBackgroundISMCDistanceBands ? 3 : 1;

        for (const FMeshVariation& Var : Cluster.Variations)
        {
            if (Var.Mesh.IsNull())
            {
                continue;
            }

            UStaticMesh* LoadedMesh = Var.Mesh.LoadSynchronous();
            if (!LoadedMesh)
            {
                continue;
            }

            for (int32 BandIndex = 0; BandIndex < BandPrecreateCount; ++BandIndex)
            {
                GetOrCreateBackgroundISMC(LoadedMesh, Cluster, bNoCollisionForClusterInstances, bCastShadowForClusterInstances, BandIndex);
            }
        }
    }

    int32 SpawnedTreeActorCount = 0;
    int32 SpawnedInstancedCount = 0;
    int32 SpawnedInstancedNearCount = 0;
    int32 SpawnedInstancedMidCount = 0;
    int32 SpawnedInstancedFarCount = 0;
    int32 SpawnedInstancedNoShadowCount = 0;
    int32 SpawnedTreeActorNoShadowCount = 0;
    int32 SpawnedBlueprintActorCount = 0;
    int32 BlueprintSpawnFailureCount = 0;
    int32 BlueprintRejectedByFootprintCount = 0;
    int32 RejectByRoomBoundsCount = 0;
    int32 RejectByClusterMinDistanceCount = 0;
    int32 RejectByGlobalSpacingCount = 0;
    int32 RejectByRoadCount = 0;
    int32 RejectByWaterRuleCount = 0;
    int32 RejectByWaterProximityCount = 0;
    int32 RejectByPreplacedGeometryCount = 0;
    int32 RejectBySteepSlopeCount = 0;
    int32 RejectByNarrowSupportCount = 0;
    int32 WindActorFallbackByBudget = 0;
    int32 WindActorFallbackByDistance = 0;
    const int32 EffectiveWindTreeActorMaxCount = bUseDenseTreeFastMode
        ? FMath::Min(WindTreeActorMaxCount, FMath::Max(0, DenseTreeActorBudget))
        : WindTreeActorMaxCount;
    const float EffectiveWindTreeActorSpawnRadius = bUseDenseTreeFastMode
        ? FMath::Min(WindTreeActorSpawnRadius, FMath::Max(1000.0f, DenseTreeActorRadius))
        : WindTreeActorSpawnRadius;
    const float EffectiveWindTreeActorShadowRadius = bUseDenseTreeFastMode
        ? FMath::Min(WindTreeActorShadowRadius, FMath::Max(1000.0f, DenseTreeActorShadowRadius))
        : WindTreeActorShadowRadius;
    int32 WindActorBudgetLeft = bSpawnWindAnimatedTreesAsActors ? FMath::Max(0, EffectiveWindTreeActorMaxCount) : 0;
    const float WindActorRadiusSq = FMath::Square(FMath::Max(1000.0f, EffectiveWindTreeActorSpawnRadius));
    const float WindActorShadowRadiusSq = FMath::Square(FMath::Max(1000.0f, EffectiveWindTreeActorShadowRadius));
    FBox2D LandscapeScatterBounds(EForceInit::ForceInit);
    const bool bHasLandscapeScatterBounds = TryGetLandscapeXYSpawnBounds(GetWorld(), LandscapeScatterBounds);
    const FVector2D ScatterBoundsCenter2D = bHasLandscapeScatterBounds
        ? LandscapeScatterBounds.GetCenter()
        : FVector2D(GetActorLocation().X, GetActorLocation().Y);
    const FVector LayoutCenter(ScatterBoundsCenter2D.X, ScatterBoundsCenter2D.Y, GetActorLocation().Z);
    const FVector2D ScatterBoundsSize = bHasLandscapeScatterBounds ? LandscapeScatterBounds.GetSize() : FVector2D::ZeroVector;
    const float ScatterDomainHalfMinExtent = bHasLandscapeScatterBounds
        ? 0.5f * FMath::Min(ScatterBoundsSize.X, ScatterBoundsSize.Y)
        : BackgroundRadius;
    const float EffectiveBackgroundScatterMaxSeconds =
        (bHasLandscapeScatterBounds && FMath::Min(ScatterBoundsSize.X, ScatterBoundsSize.Y) >= 180000.0f)
        ? FMath::Max(BackgroundScatterMaxSeconds, 14.0f)
        : BackgroundScatterMaxSeconds;
    if (bHasLandscapeScatterBounds)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background scatter domain: Min=(%.0f,%.0f) Max=(%.0f,%.0f) Size=(%.0f,%.0f) TimeBudget=%.1fs"),
            LandscapeScatterBounds.Min.X,
            LandscapeScatterBounds.Min.Y,
            LandscapeScatterBounds.Max.X,
            LandscapeScatterBounds.Max.Y,
            ScatterBoundsSize.X,
            ScatterBoundsSize.Y,
            EffectiveBackgroundScatterMaxSeconds);
    }
    const bool bStrictOverlapChecks = bUseStrictBackgroundOverlapChecks || !bUseFastBackgroundScatter;
    const int32 EffectiveAttemptMultiplier = FMath::Clamp(BackgroundScatterAttemptMultiplier, 2, 40);
    const double ScatterStartSeconds = FPlatformTime::Seconds();
    bool bReachedScatterTimeBudget = false;

    FCollisionQueryParams BaseGroundTraceParams(SCENE_QUERY_STAT(RaidBackgroundScatterGround), false);
    BaseGroundTraceParams.bTraceComplex = false;
    BaseGroundTraceParams.AddIgnoredActor(this);
    for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& RoomPair : SpawnedRooms)
    {
        if (RoomPair.Value)
        {
            BaseGroundTraceParams.AddIgnoredActor(RoomPair.Value);
        }
    }

    TArray<const USplineComponent*> CachedRoadSplines;
    TArray<FBox2D> CachedRoadFootprints;
    TArray<FRoadSplineWidthSample> CachedRoadWidthSamples;
    if (bAvoidLandscapeSplineRoads)
    {
        GatherRoadSplineComponents(GetWorld(), CachedRoadSplines);
        GatherRoadFootprints(GetWorld(), CachedRoadFootprints);
        GatherLandscapeSplineRoadWidthSamples(GetWorld(), CachedRoadWidthSamples);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background road exclusion cache: SplineComps=%d Footprints=%d WidthSamples=%d"),
            CachedRoadSplines.Num(),
            CachedRoadFootprints.Num(),
            CachedRoadWidthSamples.Num());
    }

    TArray<FWaterBodySplineRule> CachedWaterBodySplineRules;
    const float EffectiveOceanWaterBodyEdgeBufferDistance = WaterBodySplineEdgeBufferDistance + WaterBodyOceanExtraBufferDistance;
    const float EffectiveLakeWaterBodyEdgeBufferDistance = WaterBodySplineEdgeBufferDistance + WaterBodyLakeExtraBufferDistance;
    if (bUseWaterBodySplineBoundaryRules)
    {
        GatherWaterBodySplineRules(GetWorld(), CachedWaterBodySplineRules);
        int32 OceanRuleCount = 0;
        int32 LakeRuleCount = 0;
        for (const FWaterBodySplineRule& Rule : CachedWaterBodySplineRules)
        {
            if (Rule.bAllowOnlyInside) ++OceanRuleCount;
            else ++LakeRuleCount;
        }
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background WaterBody rules: Total=%d Ocean=%d Lake=%d EdgeBuffer(Base=%.0f Ocean=%.0f Lake=%.0f)"),
            CachedWaterBodySplineRules.Num(),
            OceanRuleCount,
            LakeRuleCount,
            WaterBodySplineEdgeBufferDistance,
            EffectiveOceanWaterBodyEdgeBufferDistance,
            EffectiveLakeWaterBodyEdgeBufferDistance);
    }

    TArray<FBox2D> CachedPreplacedStaticMeshFootprints;
    const float EffectivePreplacedBufferForBackground = FMath::Clamp(PreplacedStaticMeshBufferDistance, 0.0f, 35.0f);
    if (bAvoidPreplacedStaticMeshGeometry)
    {
        GatherPreplacedStaticMeshFootprints(GetWorld(), this, CachedPreplacedStaticMeshFootprints);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background preplaced static geometry cache: Footprints=%d Buffer=%.0f EffectiveBGBuffer=%.0f"),
            CachedPreplacedStaticMeshFootprints.Num(),
            PreplacedStaticMeshBufferDistance,
            EffectivePreplacedBufferForBackground);
    }

    TArray<FBox2D> GlobalSpawnFootprints;
    GlobalSpawnFootprints.Reserve(4096);
    if (bStrictOverlapChecks && bAvoidPreplacedStaticMeshGeometry && CachedPreplacedStaticMeshFootprints.Num() > 0)
    {
        GlobalSpawnFootprints.Append(CachedPreplacedStaticMeshFootprints);
    }
    TMap<FIntPoint, TArray<FVector2D>> GlobalSpawnCellPoints;
    const float GlobalSpawnCellSize = 1000.0f;
    auto GetSpawnCell = [&](const FVector& InLocation) -> FIntPoint
        {
            return FIntPoint(
                FMath::FloorToInt(InLocation.X / GlobalSpawnCellSize),
                FMath::FloorToInt(InLocation.Y / GlobalSpawnCellSize));
        };
    auto IsOverlappingGlobalSpawnPoints = [&](const FVector& InLocation, float MinDistance) -> bool
        {
            if (MinDistance <= KINDA_SMALL_NUMBER)
            {
                return false;
            }

            const float MinDistanceSq = FMath::Square(MinDistance);
            const FIntPoint CenterCell = GetSpawnCell(InLocation);
            const int32 CellRange = FMath::Max(1, FMath::CeilToInt(MinDistance / GlobalSpawnCellSize));
            const FVector2D CandidateXY(InLocation.X, InLocation.Y);
            for (int32 CellX = CenterCell.X - CellRange; CellX <= CenterCell.X + CellRange; ++CellX)
            {
                for (int32 CellY = CenterCell.Y - CellRange; CellY <= CenterCell.Y + CellRange; ++CellY)
                {
                    if (const TArray<FVector2D>* Bucket = GlobalSpawnCellPoints.Find(FIntPoint(CellX, CellY)))
                    {
                        for (const FVector2D& Existing : *Bucket)
                        {
                            if (FVector2D::DistSquared(Existing, CandidateXY) < MinDistanceSq)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        };
    auto RegisterGlobalSpawnPoint = [&](const FVector& InLocation)
        {
            const FIntPoint Cell = GetSpawnCell(InLocation);
            GlobalSpawnCellPoints.FindOrAdd(Cell).Add(FVector2D(InLocation.X, InLocation.Y));
        };

    auto IsNearWaterForBackgroundScatter = [&](const FVector& InLocation) -> bool
        {
            if (!GetWorld())
            {
                return false;
            }

            if (bUseWaterVolumeForSpawnAvoidance &&
                IsInsideWaterPhysicsVolume(GetWorld(), InLocation, WaterAvoidanceRadius))
            {
                return true;
            }

            // Fast path: overlap check still catches tagged water actors/components
            // even when detailed ring-trace mode is disabled.
            FCollisionObjectQueryParams ObjQuery;
            ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
            ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
            TArray<FOverlapResult> WaterOverlaps;
            if (GetWorld()->OverlapMultiByObjectType(
                WaterOverlaps,
                InLocation,
                FQuat::Identity,
                ObjQuery,
                FCollisionShape::MakeSphere(WaterAvoidanceRadius),
                BaseGroundTraceParams))
            {
                for (const FOverlapResult& Overlap : WaterOverlaps)
                {
                    if (IsWaterActorOrComponent(Overlap.GetActor(), Overlap.Component.Get()))
                    {
                        return true;
                    }
                }
            }

            if (bUseDetailedWaterAvoidanceForBackgroundScatter)
            {
                if (IsLocationNearWater(
                    GetWorld(),
                    InLocation,
                    WaterAvoidanceRadius,
                    BaseGroundTraceParams,
                    bUseWaterVolumeForSpawnAvoidance))
                {
                    return true;
                }
            }

            if (IsLocationWithinShorelineBuffer(
                GetWorld(),
                InLocation,
                WaterAvoidanceRadius,
                ShorelineSpawnBufferDistance,
                ShorelineProbeSampleCount,
                bUseWaterVolumeInShorelineProbe,
                BaseGroundTraceParams))
            {
                return true;
            }

            return false;
        };

    FRandomStream Stream(FMath::Rand());
    bool bReachedGlobalBackgroundBudget = false;
    for (const FMeshCluster& Cluster : BackgroundClusters) {
        if (bReachedGlobalBackgroundBudget)
        {
            break;
        }
        if (Cluster.Variations.Num() == 0) continue;
        const bool bWindTreeCluster = IsWindTreeCluster(Cluster);
        const float EffectiveBackgroundRadius = bHasLandscapeScatterBounds
            ? FMath::Max(BackgroundRadius, ScatterDomainHalfMinExtent * 0.95f)
            : BackgroundRadius;
        float ClusterScatterRadius = EffectiveBackgroundRadius;
        if (bUseDenseTreeFastMode && bWindTreeCluster && !bHasLandscapeScatterBounds)
        {
            ClusterScatterRadius *= FMath::Clamp(DenseTreeScatterRadiusScale, 0.2f, 1.0f);
        }
        ClusterScatterRadius = FMath::Max(1000.0f, ClusterScatterRadius);
        int32 TargetSpawnCount = Cluster.CalculateSpawnCount(ClusterScatterRadius, Stream);
        if (TargetSpawnCount <= 5) {
            float AreaSq = FMath::Square(ClusterScatterRadius * 2.0f);
            float DistSq = FMath::Max(40000.0f, FMath::Square(Cluster.MinDistanceBetweenInstances));
            TargetSpawnCount = FMath::Clamp(FMath::RoundToInt((AreaSq / DistSq) * 0.15f), 50, 5000);
        }
        TargetSpawnCount = FMath::Max(1, FMath::RoundToInt((float)TargetSpawnCount * FMath::Max(0.2f, BackgroundAutoDensityScale)));
        if (bUseDenseTreeFastMode && bWindTreeCluster)
        {
            // Allow high-density forests (e.g., 5x) while still bounded by global spawn budgets.
            TargetSpawnCount = FMath::Max(1, FMath::RoundToInt((float)TargetSpawnCount * FMath::Clamp(DenseTreeSpawnScale, 1.0f, 8.0f)));
        }
        else if (bUseDenseTreeFastMode && bPrioritizeTreeClusterDensity)
        {
            TargetSpawnCount = FMath::Max(1, FMath::RoundToInt((float)TargetSpawnCount * FMath::Clamp(NonTreeSpawnScaleWhenTreePriority, 0.2f, 1.0f)));
        }

        bool bNoCollision = Cluster.ClusterName.Contains(TEXT("NoCol"));
        const bool bDenseTreeNoCollision = bUseDenseTreeFastMode && bWindTreeCluster && bDenseTreeDisableCollisionOnInstances;
        const bool bNoCollisionForClusterInstances =
            bNoCollision || bDenseTreeNoCollision || (bReduceShadowsForInstancedBackgroundTrees && bWindTreeCluster);
        const bool bCastShadowForClusterInstances =
            !((bReduceShadowsForNoCollisionBackground && bNoCollision) ||
                (bReduceShadowsForInstancedBackgroundTrees && bWindTreeCluster));
        const float EffectiveClusterMinDistance =
            (bUseDenseTreeFastMode && bWindTreeCluster)
            ? FMath::Max(120.0f, Cluster.MinDistanceBetweenInstances * FMath::Clamp(DenseTreeMinDistanceScale, 0.2f, 1.0f))
            : Cluster.MinDistanceBetweenInstances;
        const float MinDistanceFloor =
            bNoCollision
            ? 300.0f
            : ((bUseDenseTreeFastMode && bWindTreeCluster) ? 700.0f : 1200.0f);
        float MinDistSq = FMath::Max(FMath::Square(EffectiveClusterMinDistance), FMath::Square(MinDistanceFloor));
        float ExclusionMargin = bNoCollision ? 600.0f : 1500.0f;
        if (bHasLandscapeScatterBounds && FMath::Min(ScatterBoundsSize.X, ScatterBoundsSize.Y) >= 180000.0f)
        {
            ExclusionMargin = bNoCollision ? 420.0f : 980.0f;
        }
        int32 Spawned = 0; TArray<FVector> SpawnedLocations;
        int32 MaxAttempts = TargetSpawnCount * EffectiveAttemptMultiplier;
        const float ClusterSampleOffsetU = Stream.FRand();
        const float ClusterSampleOffsetV = Stream.FRand();
        const float ClusterRotationRad = Stream.FRandRange(0.0f, 2.0f * PI);
        const float ClusterRotationCos = FMath::Cos(ClusterRotationRad);
        const float ClusterRotationSin = FMath::Sin(ClusterRotationRad);
        bool bClusterHasBlueprintVariation = false;
        for (const FMeshVariation& CandidateVariation : Cluster.Variations)
        {
            if (CandidateVariation.Mesh.IsNull() && !CandidateVariation.BlueprintPrefab.IsNull())
            {
                bClusterHasBlueprintVariation = true;
                break;
            }
        }

        const bool bEnableAdaptiveGroundSamplingForCluster =
            bUseAdaptiveGroundMultiSampleForBackgroundScatter &&
            !bWindTreeCluster &&
            !bNoCollision &&
            (bClusterHasBlueprintVariation || EffectiveClusterMinDistance >= 950.0f);
        const int32 GroundSupportRadialSampleCountForCluster = bEnableAdaptiveGroundSamplingForCluster
            ? (bClusterHasBlueprintVariation ? 6 : 4)
            : 0;
        const float GroundSupportSampleRadiusForCluster = bEnableAdaptiveGroundSamplingForCluster
            ? FMath::Clamp(EffectiveClusterMinDistance * 0.22f, 80.0f, 320.0f)
            : 0.0f;

        for (int32 i = 0; i < MaxAttempts && Spawned < TargetSpawnCount; ++i) {
            if (EffectiveBackgroundScatterMaxSeconds > 0.0f)
            {
                const double ElapsedSeconds = FPlatformTime::Seconds() - ScatterStartSeconds;
                if (ElapsedSeconds >= static_cast<double>(EffectiveBackgroundScatterMaxSeconds))
                {
                    bReachedScatterTimeBudget = true;
                    bReachedGlobalBackgroundBudget = true;
                    break;
                }
            }

            if (bEnableBackgroundGlobalSpawnBudget)
            {
                const int32 TotalActorBackgroundCount = SpawnedTreeActorCount + SpawnedBlueprintActorCount;
                if (SpawnedInstancedCount >= BackgroundMaxInstancedCount &&
                    TotalActorBackgroundCount >= BackgroundMaxActorCount)
                {
                    bReachedGlobalBackgroundBudget = true;
                    break;
                }
            }

            const float U = FMath::Frac(HaltonSequence(i + 1, 2) + ClusterSampleOffsetU);
            const float V = FMath::Frac(HaltonSequence(i + 1, 3) + ClusterSampleOffsetV);
            FVector Point = LayoutCenter;
            if (bHasLandscapeScatterBounds)
            {
                Point.X = FMath::Lerp(LandscapeScatterBounds.Min.X, LandscapeScatterBounds.Max.X, U);
                Point.Y = FMath::Lerp(LandscapeScatterBounds.Min.Y, LandscapeScatterBounds.Max.Y, V);
            }
            else
            {
                const float LocalX = FMath::Lerp(-ClusterScatterRadius, ClusterScatterRadius, U);
                const float LocalY = FMath::Lerp(-ClusterScatterRadius, ClusterScatterRadius, V);
                const float RotX = (LocalX * ClusterRotationCos) - (LocalY * ClusterRotationSin);
                const float RotY = (LocalX * ClusterRotationSin) + (LocalY * ClusterRotationCos);
                Point = LayoutCenter + FVector(RotX, RotY, 0.0f);
            }

            if (bUseWaterBodySplineBoundaryRules &&
                IsLocationBlockedByWaterBodySplineRules(
                    Point,
                    CachedWaterBodySplineRules,
                    WaterBodySplineEdgeBufferDistance,
                    WaterBodyOceanExtraBufferDistance,
                    WaterBodyLakeExtraBufferDistance))
            {
                ++RejectByWaterRuleCount;
                continue;
            }
            if (bAvoidLandscapeSplineRoads &&
                IsLocationNearLandscapeSplineRoad(
                    GetWorld(),
                    Point,
                    LandscapeSplineRoadAvoidanceRadius,
                    BaseGroundTraceParams,
                    &CachedRoadSplines,
                    &CachedRoadFootprints,
                    &CachedRoadWidthSamples))
            {
                ++RejectByRoadCount;
                continue;
            }
            bool bInsideRoom = false;
            for (auto& Pair : SpawnedRooms) {
                if (ARaidRoomActor* Room = Pair.Value) {
                    if (FMath::Abs(Point.X - Room->GetActorLocation().X) < (Room->GetRoomExtent().X + ExclusionMargin) && FMath::Abs(Point.Y - Room->GetActorLocation().Y) < (Room->GetRoomExtent().Y + ExclusionMargin)) {
                        bInsideRoom = true; break;
                    }
                }
            }
            if (bInsideRoom)
            {
                ++RejectByRoomBoundsCount;
                continue;
            }

            bool bOverlaps = false;
            for (const FVector& ExistingLoc : SpawnedLocations) { if (FVector::DistSquaredXY(ExistingLoc, Point) < MinDistSq) { bOverlaps = true; break; } }
            if (bOverlaps)
            {
                ++RejectByClusterMinDistanceCount;
                continue;
            }

            const float GlobalSpacing = FMath::Max(240.0f, EffectiveClusterMinDistance * (bNoCollision ? 0.35f : 0.55f));
            if (IsOverlappingGlobalSpawnPoints(Point, GlobalSpacing))
            {
                ++RejectByGlobalSpacingCount;
                continue;
            }

            FHitResult Hit;
            if (TryResolveGroundHit(
                GetWorld(),
                Point,
                bPreferLandscapeGroundHit,
                true,
                BaseGroundTraceParams,
                Hit,
                GroundSupportSampleRadiusForCluster,
                GroundSupportRadialSampleCountForCluster,
                false))
            {
                if (IsNearWaterForBackgroundScatter(Hit.ImpactPoint))
                {
                    ++RejectByWaterProximityCount;
                    continue;
                }
                if (bAvoidLandscapeSplineRoads &&
                    IsLocationNearLandscapeSplineRoad(
                        GetWorld(),
                        Hit.ImpactPoint,
                        LandscapeSplineRoadAvoidanceRadius,
                        BaseGroundTraceParams,
                        &CachedRoadSplines,
                        &CachedRoadFootprints,
                        &CachedRoadWidthSamples))
                {
                    ++RejectByRoadCount;
                    continue;
                }
                if (bUseWaterBodySplineBoundaryRules &&
                    IsLocationBlockedByWaterBodySplineRules(
                        Hit.ImpactPoint,
                        CachedWaterBodySplineRules,
                        WaterBodySplineEdgeBufferDistance,
                        WaterBodyOceanExtraBufferDistance,
                        WaterBodyLakeExtraBufferDistance))
                {
                    ++RejectByWaterRuleCount;
                    continue;
                }
                if (bRejectSteepOrNarrowGroundForBackgroundScatter)
                {
                    const float GroundNormalZ = FMath::Clamp(Hit.ImpactNormal.GetSafeNormal().Z, -1.0f, 1.0f);
                    const float GroundSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(GroundNormalZ));
                    if (GroundSlopeDeg > FMath::Clamp(BackgroundMaxAllowedSlopeDeg, 10.0f, 85.0f))
                    {
                        ++RejectBySteepSlopeCount;
                        continue;
                    }

                    const float SupportSampleRadius = FMath::Clamp(
                        FMath::Max(
                            GroundSupportSampleRadiusForCluster,
                            EffectiveClusterMinDistance * 0.28f),
                        90.0f,
                        460.0f);
                    float SupportHitRatio = 1.0f;
                    float SupportHeightRange = 0.0f;
                    if (MeasureGroundSupportStats(
                        GetWorld(),
                        Hit.ImpactPoint,
                        bPreferLandscapeGroundHit,
                        true,
                        BaseGroundTraceParams,
                        SupportSampleRadius,
                        BackgroundGroundSupportRadialSamples,
                        SupportHitRatio,
                        SupportHeightRange))
                    {
                        const float MinSupportHitRatio = FMath::Clamp(BackgroundMinGroundSupportHitRatio, 0.10f, 1.00f);
                        const float MaxSupportHeightRange = FMath::Clamp(BackgroundMaxGroundSupportHeightRange, 5.0f, 1200.0f);
                        if (SupportHitRatio < MinSupportHitRatio || SupportHeightRange > MaxSupportHeightRange)
                        {
                            ++RejectByNarrowSupportCount;
                            continue;
                        }
                    }
                }
                if (const FMeshVariation* RandomVar = RaidMeshUtils::PickRandomVariation(Cluster.Variations, Stream)) {
                    FRotator BaseRot = FRotator::ZeroRotator;
                    if (bNoCollision || Cluster.ClusterName.Contains(TEXT("Rock"))) { BaseRot = FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator(); BaseRot.Yaw = Stream.FRandRange(0.0f, 360.0f); }
                    else { BaseRot = FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f); }

                    FMeshVariation EffectiveVariation = *RandomVar;
                    if (bUseGlobalBackgroundLocationOffset)
                    {
                        FTransform EffectiveOffset = EffectiveVariation.Offset;
                        EffectiveOffset.SetLocation(GlobalBackgroundLocationOffset);
                        EffectiveVariation.Offset = EffectiveOffset;
                    }

                    FTransform HitTransform(BaseRot, Hit.ImpactPoint);
                    FTransform FinalTrans = Cluster.GetClusterRandomizedTransform(EffectiveVariation, HitTransform, Stream);
                    const bool bCandidateBlueprint = EffectiveVariation.Mesh.IsNull() && !EffectiveVariation.BlueprintPrefab.IsNull();

                    FBox2D CandidateFootprint(EForceInit::ForceInit);
                    bool bHasCandidateFootprint = false;
                    const float FootprintPadding = bCandidateBlueprint
                        ? FMath::Clamp(EffectiveClusterMinDistance * 0.12f, 35.0f, 220.0f)
                        : FMath::Clamp(EffectiveClusterMinDistance * 0.12f, 25.0f, 180.0f);

                    const bool bNeedCandidateFootprintForFilters =
                        bStrictOverlapChecks ||
                        bAvoidLandscapeSplineRoads ||
                        bUseWaterBodySplineBoundaryRules ||
                        bAvoidPreplacedStaticMeshGeometry;

                    if (bNeedCandidateFootprintForFilters && !EffectiveVariation.Mesh.IsNull())
                    {
                        if (UStaticMesh* FootprintMesh = EffectiveVariation.Mesh.LoadSynchronous())
                        {
                            bHasCandidateFootprint = TryBuildFootprintFromStaticMesh(FootprintMesh, FinalTrans, CandidateFootprint);
                        }
                    }
                    if (bNeedCandidateFootprintForFilters && !bHasCandidateFootprint)
                    {
                        const float ExtentFactor = bCandidateBlueprint ? 0.45f : 0.45f;
                        const float FallbackMinExtent = bCandidateBlueprint ? 180.0f : (bNoCollision ? 120.0f : 220.0f);
                        const float FallbackExtent = FMath::Max(Cluster.MinDistanceBetweenInstances * ExtentFactor, FallbackMinExtent);
                        const FVector SpawnLoc = FinalTrans.GetLocation();
                        CandidateFootprint = FBox2D(
                            FVector2D(SpawnLoc.X - FallbackExtent, SpawnLoc.Y - FallbackExtent),
                            FVector2D(SpawnLoc.X + FallbackExtent, SpawnLoc.Y + FallbackExtent));
                        bHasCandidateFootprint = CandidateFootprint.bIsValid;
                    }

                    // Variation offsets can shift the final actor/instance onto roads/water even if
                    // the original ground-hit point passed checks, so validate using final transform too.
                    if (bAvoidLandscapeSplineRoads)
                    {
                        if (IsLocationNearLandscapeSplineRoad(
                            GetWorld(),
                            FinalTrans.GetLocation(),
                            LandscapeSplineRoadAvoidanceRadius,
                            BaseGroundTraceParams,
                            &CachedRoadSplines,
                            &CachedRoadFootprints,
                            &CachedRoadWidthSamples))
                        {
                            ++RejectByRoadCount;
                            continue;
                        }
                        if (bHasCandidateFootprint &&
                            IsFootprintNearLandscapeSplineRoad(
                                GetWorld(),
                                CandidateFootprint,
                                LandscapeSplineRoadAvoidanceRadius,
                                BaseGroundTraceParams,
                                &CachedRoadSplines,
                                &CachedRoadFootprints,
                                &CachedRoadWidthSamples))
                        {
                            ++RejectByRoadCount;
                            continue;
                        }
                    }

                    if (IsNearWaterForBackgroundScatter(FinalTrans.GetLocation()))
                    {
                        ++RejectByWaterProximityCount;
                        continue;
                    }
                    if (bUseWaterBodySplineBoundaryRules &&
                        IsLocationBlockedByWaterBodySplineRules(
                            FinalTrans.GetLocation(),
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance))
                    {
                        ++RejectByWaterRuleCount;
                        continue;
                    }
                    if (bUseWaterBodySplineBoundaryRules &&
                        bHasCandidateFootprint &&
                        IsFootprintBlockedByWaterBodySplineRules(
                            CandidateFootprint,
                            CachedWaterBodySplineRules,
                            WaterBodySplineEdgeBufferDistance,
                            WaterBodyOceanExtraBufferDistance,
                            WaterBodyLakeExtraBufferDistance))
                    {
                        ++RejectByWaterRuleCount;
                        continue;
                    }
                    if (bAvoidPreplacedStaticMeshGeometry &&
                        bHasCandidateFootprint &&
                        IsFootprintOverlappingAny(
                            CachedPreplacedStaticMeshFootprints,
                            CandidateFootprint,
                            EffectivePreplacedBufferForBackground))
                    {
                        ++RejectByPreplacedGeometryCount;
                        continue;
                    }

                    if (bStrictOverlapChecks &&
                        bHasCandidateFootprint &&
                        IsFootprintOverlappingAny(GlobalSpawnFootprints, CandidateFootprint, FootprintPadding))
                    {
                        continue;
                    }
                    // Hard scene overlap gate: also reject candidates colliding with already placed raid obstacles/scenery actors.
                    if (bStrictOverlapChecks)
                    {
                        FCollisionObjectQueryParams ObjQuery;
                        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                        FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(RaidBackgroundScatterOverlapGate), false);
                        OverlapParams.bTraceComplex = false;
                        OverlapParams.AddIgnoredActor(this);
                        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& RoomPair : SpawnedRooms)
                        {
                            if (RoomPair.Value) OverlapParams.AddIgnoredActor(RoomPair.Value);
                        }

                        const FVector CandidateLoc = FinalTrans.GetLocation();
                        float CandidateRadius = bCandidateBlueprint
                            ? FMath::Max(EffectiveClusterMinDistance * 0.30f, 140.0f)
                            : FMath::Max(EffectiveClusterMinDistance * 0.45f, 180.0f);
                        if (bHasCandidateFootprint)
                        {
                            const FVector2D FootprintSize = CandidateFootprint.GetSize();
                            const float FootprintRadiusScale = bCandidateBlueprint ? 0.35f : 0.55f;
                            CandidateRadius = FMath::Max(CandidateRadius, FMath::Max(FootprintSize.X, FootprintSize.Y) * FootprintRadiusScale + FootprintPadding);
                        }

                        TArray<FOverlapResult> Overlaps;
                        if (GetWorld()->OverlapMultiByObjectType(
                            Overlaps,
                            CandidateLoc,
                            FQuat::Identity,
                            ObjQuery,
                            FCollisionShape::MakeSphere(CandidateRadius),
                            OverlapParams))
                        {
                            bool bRejectBySceneOverlap = false;
                            for (const FOverlapResult& Overlap : Overlaps)
                            {
                                const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                                const AActor* HitActor = Overlap.GetActor();
                                if (!IsValid(HitComp))
                                {
                                    continue;
                                }

                                const bool bRaidObstacleActor =
                                    IsValid(HitActor) &&
                                    (HitActor->ActorHasTag(TEXT("RaidBackgroundScenery")) ||
                                        HitActor->ActorHasTag(TEXT("MeshType_2")) ||
                                        HitActor->ActorHasTag(TEXT("ObstacleBlueprint")));
                                const bool bRaidObstacleComponent =
                                    HitComp->ComponentTags.Contains(TEXT("MeshType_2")) ||
                                    HitComp->ComponentTags.Contains(TEXT("ObstacleBlueprint"));
                                if (bRaidObstacleActor || bRaidObstacleComponent)
                                {
                                    bRejectBySceneOverlap = true;
                                    break;
                                }
                            }

                            if (bRejectBySceneOverlap)
                            {
                                continue;
                            }
                        }
                    }

                    bool bSpawnedThisAttempt = false;
                    FBox2D FinalSpawnFootprint(EForceInit::ForceInit);
                    bool bHasFinalSpawnFootprint = false;

                    if (!EffectiveVariation.Mesh.IsNull()) {
                        if (UStaticMesh* LoadedMesh = EffectiveVariation.Mesh.LoadSynchronous()) {
                            const float VariationDeltaZ = FinalTrans.GetLocation().Z - HitTransform.GetLocation().Z;
                            const bool bGroundAnchoredCluster = bWindTreeCluster || bNoCollision;
                            const float EffectiveVariationDeltaZ = bGroundAnchoredCluster
                                ? FMath::Min(0.0f, VariationDeltaZ)
                                : VariationDeltaZ;
                            const FBox LocalBounds = LoadedMesh->GetBoundingBox();
                            if (LocalBounds.IsValid)
                            {
                                float CurrentMinZ = TNumericLimits<float>::Max();
                                const FVector BoundsMin = LocalBounds.Min;
                                const FVector BoundsMax = LocalBounds.Max;
                                const FVector Corners[8] =
                                {
                                    FVector(BoundsMin.X, BoundsMin.Y, BoundsMin.Z),
                                    FVector(BoundsMin.X, BoundsMin.Y, BoundsMax.Z),
                                    FVector(BoundsMin.X, BoundsMax.Y, BoundsMin.Z),
                                    FVector(BoundsMin.X, BoundsMax.Y, BoundsMax.Z),
                                    FVector(BoundsMax.X, BoundsMin.Y, BoundsMin.Z),
                                    FVector(BoundsMax.X, BoundsMin.Y, BoundsMax.Z),
                                    FVector(BoundsMax.X, BoundsMax.Y, BoundsMin.Z),
                                    FVector(BoundsMax.X, BoundsMax.Y, BoundsMax.Z)
                                };

                                for (const FVector& Corner : Corners)
                                {
                                    const FVector WorldCorner = FinalTrans.TransformPosition(Corner);
                                    CurrentMinZ = FMath::Min(CurrentMinZ, WorldCorner.Z);
                                }

                                if (CurrentMinZ < TNumericLimits<float>::Max())
                                {
                                    FVector AdjustedLocation = FinalTrans.GetLocation();
                                    const float TargetBottomZ = Hit.ImpactPoint.Z + EffectiveVariationDeltaZ + 2.0f;
                                    AdjustedLocation.Z += (TargetBottomZ - CurrentMinZ);
                                    FinalTrans.SetLocation(AdjustedLocation);
                                }
                            }

                            const bool bWithinWindRadius = FVector::DistSquaredXY(FinalTrans.GetLocation(), LayoutCenter) <= WindActorRadiusSq;
                            const bool bActorBudgetAvailable =
                                !bEnableBackgroundGlobalSpawnBudget ||
                                ((SpawnedTreeActorCount + SpawnedBlueprintActorCount) < BackgroundMaxActorCount);
                            const bool bSpawnAsWindActor =
                                bSpawnWindAnimatedTreesAsActors &&
                                bWindTreeCluster &&
                                (WindActorBudgetLeft > 0) &&
                                bActorBudgetAvailable &&
                                bWithinWindRadius;

                            if (bSpawnAsWindActor)
                            {
                                FActorSpawnParameters SpawnParams;
                                SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                                SpawnParams.ObjectFlags |= ResolveRaidSpawnObjectFlags(GetWorld());
                                if (AStaticMeshActor* NewBackgroundActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FinalTrans, SpawnParams))
                                {
                                    bool bActorCastShadow = true;
                                    if (UStaticMeshComponent* MeshComp = NewBackgroundActor->GetStaticMeshComponent())
                                    {
                                        bActorCastShadow = FVector::DistSquaredXY(FinalTrans.GetLocation(), LayoutCenter) <= WindActorShadowRadiusSq;
                                        MeshComp->SetStaticMesh(LoadedMesh);
                                        MeshComp->SetCollisionProfileName((bNoCollision || bDenseTreeNoCollision) ? TEXT("NoCollision") : TEXT("BlockAll"));
                                        MeshComp->SetMobility(EComponentMobility::Static);
                                        MeshComp->SetCanEverAffectNavigation(false);
                                        MeshComp->SetCastShadow(bActorCastShadow);
                                        MeshComp->bCastDynamicShadow = bActorCastShadow;
                                        MeshComp->bCastStaticShadow = bActorCastShadow;
                                        MeshComp->bAffectDistanceFieldLighting = bActorCastShadow;
                                        ApplyWindPhaseDesync(MeshComp, Stream, bForceUniqueWindPhasePerTree);
                                    }
                                    NewBackgroundActor->Tags.AddUnique(FName(TEXT("RaidBackgroundScenery")));
                                    NewBackgroundActor->SetOwner(this);
                                    if (bStrictOverlapChecks && TryBuildFootprintFromActor(NewBackgroundActor, FinalSpawnFootprint))
                                    {
                                        bHasFinalSpawnFootprint = true;
                                        if (IsFootprintOverlappingAny(GlobalSpawnFootprints, FinalSpawnFootprint, FootprintPadding))
                                        {
                                            NewBackgroundActor->Destroy();
                                            continue;
                                        }
                                    }
                                    SpawnedBackgroundActors.Add(NewBackgroundActor);
                                    SpawnedLocations.Add(NewBackgroundActor->GetActorLocation());
                                    ++SpawnedTreeActorCount;
                                    if (!bActorCastShadow)
                                    {
                                        ++SpawnedTreeActorNoShadowCount;
                                    }
                                    --WindActorBudgetLeft;
                                    bSpawnedThisAttempt = true;
                                }
                            }
                            else
                            {
                                if (bSpawnWindAnimatedTreesAsActors && bWindTreeCluster)
                                {
                                    if (WindActorBudgetLeft <= 0) ++WindActorFallbackByBudget;
                                    else if (!bWithinWindRadius) ++WindActorFallbackByDistance;
                                }

                                int32 DistanceBandIndex = 0;
                                if (bEnableBackgroundISMCDistanceBands)
                                {
                                    const float DistSqFromCenter = FVector::DistSquaredXY(FinalTrans.GetLocation(), LayoutCenter);
                                    if (DistSqFromCenter <= NearBandMaxDistanceSq) DistanceBandIndex = 0;
                                    else if (DistSqFromCenter <= MidBandMaxDistanceSq) DistanceBandIndex = 1;
                                    else DistanceBandIndex = 2;
                                }

                                if (UHierarchicalInstancedStaticMeshComponent* ISMC = GetOrCreateBackgroundISMC(LoadedMesh, Cluster, bNoCollisionForClusterInstances, bCastShadowForClusterInstances, DistanceBandIndex))
                                {
                                    if (bEnableBackgroundGlobalSpawnBudget && SpawnedInstancedCount >= BackgroundMaxInstancedCount)
                                    {
                                        continue;
                                    }
                                    const int32 InstanceIndex = ISMC->AddInstance(FinalTrans, true);
                                    if (InstanceIndex != INDEX_NONE)
                                    {
                                        if (bWindTreeCluster)
                                        {
                                            ISMC->SetCustomDataValue(InstanceIndex, 0, Stream.FRandRange(0.0f, 1.0f), false);
                                            ISMC->SetCustomDataValue(InstanceIndex, 1, Stream.FRandRange(0.0f, 6.283185f), true);
                                        }
                                        ++SpawnedInstancedCount;
                                        if (!ISMC->CastShadow)
                                        {
                                            ++SpawnedInstancedNoShadowCount;
                                        }
                                        if (DistanceBandIndex == 0) ++SpawnedInstancedNearCount;
                                        else if (DistanceBandIndex == 1) ++SpawnedInstancedMidCount;
                                        else ++SpawnedInstancedFarCount;
                                        SpawnedLocations.Add(FinalTrans.GetLocation());
                                        bSpawnedThisAttempt = true;
                                    }
                                }
                            }
                        }
                    }
                    else if (!EffectiveVariation.BlueprintPrefab.IsNull())
                    {
                        if (bEnableBackgroundGlobalSpawnBudget &&
                            (SpawnedTreeActorCount + SpawnedBlueprintActorCount) >= BackgroundMaxActorCount)
                        {
                            continue;
                        }
                        if (UClass* BlueprintClass = EffectiveVariation.BlueprintPrefab.LoadSynchronous())
                        {
                            FActorSpawnParameters SpawnParams;
                            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
                            SpawnParams.ObjectFlags |= ResolveRaidSpawnObjectFlags(GetWorld());
                            if (AActor* SpawnedBackgroundActor = GetWorld()->SpawnActor<AActor>(BlueprintClass, FinalTrans, SpawnParams))
                            {
                                const float BlueprintVariationDeltaZ = FinalTrans.GetLocation().Z - HitTransform.GetLocation().Z;
                                const bool bGroundAnchoredBlueprintCluster = bWindTreeCluster || bNoCollision;
                                const float EffectiveBlueprintVariationDeltaZ = bGroundAnchoredBlueprintCluster
                                    ? FMath::Min(0.0f, BlueprintVariationDeltaZ)
                                    : BlueprintVariationDeltaZ;
                                const FBox BlueprintBounds = SpawnedBackgroundActor->GetComponentsBoundingBox(true);
                                if (BlueprintBounds.IsValid)
                                {
                                    const float TargetBottomZ = Hit.ImpactPoint.Z + EffectiveBlueprintVariationDeltaZ + 2.0f;
                                    const float DeltaToGround = TargetBottomZ - BlueprintBounds.Min.Z;
                                    if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                                    {
                                        SpawnedBackgroundActor->AddActorWorldOffset(
                                            FVector(0.0f, 0.0f, DeltaToGround),
                                            false,
                                            nullptr,
                                            ETeleportType::TeleportPhysics);
                                    }
                                }

                                SpawnedBackgroundActor->Tags.AddUnique(FName(TEXT("RaidBackgroundScenery")));
                                SpawnedBackgroundActor->SetOwner(this);
                                bHasFinalSpawnFootprint = bStrictOverlapChecks && TryBuildFootprintFromActor(SpawnedBackgroundActor, FinalSpawnFootprint);
                                if (bStrictOverlapChecks &&
                                    bHasFinalSpawnFootprint &&
                                    IsFootprintOverlappingAny(GlobalSpawnFootprints, FinalSpawnFootprint, FootprintPadding))
                                {
                                    SpawnedBackgroundActor->Destroy();
                                    ++BlueprintRejectedByFootprintCount;
                                    continue;
                                }

                                SpawnedBackgroundActors.Add(SpawnedBackgroundActor);
                                SpawnedLocations.Add(SpawnedBackgroundActor->GetActorLocation());
                                ++SpawnedBlueprintActorCount;
                                bSpawnedThisAttempt = true;
                            }
                            else
                            {
                                ++BlueprintSpawnFailureCount;
                            }
                        }
                        else
                        {
                            ++BlueprintSpawnFailureCount;
                        }
                    }

                    if (!bSpawnedThisAttempt)
                    {
                        continue;
                    }

                    if (bStrictOverlapChecks && !bHasFinalSpawnFootprint && bHasCandidateFootprint)
                    {
                        FinalSpawnFootprint = CandidateFootprint;
                        bHasFinalSpawnFootprint = true;
                    }
                    if (bStrictOverlapChecks && bHasFinalSpawnFootprint)
                    {
                        GlobalSpawnFootprints.Add(FinalSpawnFootprint);
                    }

                    RegisterGlobalSpawnPoint(FinalTrans.GetLocation());

                    Spawned++;
                }
            }
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidLayout] Background scatter complete. TreeActors=%d Instanced=%d (Near=%d Mid=%d Far=%d) BlueprintActors=%d (InstancedNoShadow=%d, TreeActorsNoShadow=%d, BlueprintFail=%d, BlueprintFootprintReject=%d, WindTreeActorMode=%s, UniqueWindPhase=%s, MaxActors=%d, ActorRadius=%.0f, ActorShadowRadius=%.0f, FallbackBudget=%d, FallbackDistance=%d, ISMCBands=%s Near=%.0f Mid=%.0f, FastScatter=%s StrictOverlap=%s DetailedWater=%s AttemptsX=%d Time=%.2fs TimeBudgetHit=%s, TreePriority=%s NonTreeScale=%.2f DenseMinDistScale=%.2f, Reject(Room=%d MinDist=%d Global=%d Road=%d WaterRule=%d WaterNear=%d Preplaced=%d Steep=%d Narrow=%d))"),
        SpawnedTreeActorCount,
        SpawnedInstancedCount,
        SpawnedInstancedNearCount,
        SpawnedInstancedMidCount,
        SpawnedInstancedFarCount,
        SpawnedBlueprintActorCount,
        SpawnedInstancedNoShadowCount,
        SpawnedTreeActorNoShadowCount,
        BlueprintSpawnFailureCount,
        BlueprintRejectedByFootprintCount,
        bSpawnWindAnimatedTreesAsActors ? TEXT("On") : TEXT("Off"),
        bForceUniqueWindPhasePerTree ? TEXT("On") : TEXT("Off"),
        EffectiveWindTreeActorMaxCount,
        EffectiveWindTreeActorSpawnRadius,
        EffectiveWindTreeActorShadowRadius,
        WindActorFallbackByBudget,
        WindActorFallbackByDistance,
        bEnableBackgroundISMCDistanceBands ? TEXT("On") : TEXT("Off"),
        SafeNearMaxDistance,
        SafeMidMaxDistance,
        bUseFastBackgroundScatter ? TEXT("On") : TEXT("Off"),
        bStrictOverlapChecks ? TEXT("On") : TEXT("Off"),
        bUseDetailedWaterAvoidanceForBackgroundScatter ? TEXT("On") : TEXT("Off"),
        EffectiveAttemptMultiplier,
        FPlatformTime::Seconds() - ScatterStartSeconds,
        bReachedScatterTimeBudget ? TEXT("Yes") : TEXT("No"),
        bPrioritizeTreeClusterDensity ? TEXT("On") : TEXT("Off"),
        NonTreeSpawnScaleWhenTreePriority,
        DenseTreeMinDistanceScale,
        RejectByRoomBoundsCount,
        RejectByClusterMinDistanceCount,
        RejectByGlobalSpacingCount,
        RejectByRoadCount,
        RejectByWaterRuleCount,
        RejectByWaterProximityCount,
        RejectByPreplacedGeometryCount,
        RejectBySteepSlopeCount,
        RejectByNarrowSupportCount);

    if (bEnableBackgroundGlobalSpawnBudget && bReachedGlobalBackgroundBudget && !bReachedScatterTimeBudget)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background global budget reached. Instanced=%d/%d Actor=%d/%d"),
            SpawnedInstancedCount,
            BackgroundMaxInstancedCount,
            SpawnedTreeActorCount + SpawnedBlueprintActorCount,
            BackgroundMaxActorCount);
    }

    if (bReachedScatterTimeBudget)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] Background scatter stopped by time budget %.2fs. Instanced=%d Actor=%d"),
            EffectiveBackgroundScatterMaxSeconds,
            SpawnedInstancedCount,
            SpawnedTreeActorCount + SpawnedBlueprintActorCount);
    }

#if WITH_EDITOR
    if (UWorld* WorldForDirty = GetWorld(); WorldForDirty && !WorldForDirty->IsGameWorld())
    {
        Modify();
        MarkPackageDirty();
    }
#endif
}

void ARaidLayoutManager::EnsureBackgroundClustersInitialized()
{
    auto MakeVar = [](TSoftObjectPtr<UStaticMesh> InMesh, FVector InScale, bool bRand = false) {
        FMeshVariation V;
        V.Mesh = InMesh;
        V.Offset.SetScale3D(InScale);
        V.bUseRandomScale = bRand;
        if (bRand)
        {
            V.RandomScaleMin = 0.7f;
            V.RandomScaleMax = 1.4f;
        }
        return V;
    };

    auto MakeFallbackClusters = [&]() {
        if (BackgroundClusters.Num() > 0) return;

        TSoftObjectPtr<UStaticMesh> Cube(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        TSoftObjectPtr<UStaticMesh> Sphere(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        TSoftObjectPtr<UStaticMesh> Cylinder(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));

        FMeshCluster BgTree;
        BgTree.ClusterName = TEXT("Background_Trees");
        BgTree.SpawnRadius = BackgroundRadius;
        BgTree.MinDistanceBetweenInstances = 1500.0f;
        BgTree.Variations.Add(MakeVar(Cylinder, FVector(1.f, 1.f, 4.f), true));

        FMeshCluster BgRock;
        BgRock.ClusterName = TEXT("Background_Rocks");
        BgRock.SpawnRadius = BackgroundRadius;
        BgRock.MinDistanceBetweenInstances = 3000.0f;
        BgRock.Variations.Add(MakeVar(Sphere, FVector(3.f, 3.f, 2.f), true));

        FMeshCluster BgBush;
        BgBush.ClusterName = TEXT("Background_Bushes_NoCol");
        BgBush.SpawnRadius = BackgroundRadius;
        BgBush.MinDistanceBetweenInstances = 800.0f;
        BgBush.Variations.Add(MakeVar(Sphere, FVector(1.f, 1.f, 0.5f), true));

        FMeshCluster BgStruct;
        BgStruct.ClusterName = TEXT("Background_Structures");
        BgStruct.SpawnRadius = BackgroundRadius;
        BgStruct.MinDistanceBetweenInstances = 4000.0f;
        BgStruct.Variations.Add(MakeVar(Cube, FVector(2.f, 2.f, 4.f), true));

        BackgroundClusters.Add(BgTree);
        BackgroundClusters.Add(BgRock);
        BackgroundClusters.Add(BgBush);
        BackgroundClusters.Add(BgStruct);

        UE_LOG(LogTemp, Warning, TEXT("[RaidLayout] BackgroundClusters fallback auto-generated (4 default clusters)."));
    };

    auto NormalizeCluster = [&](FMeshCluster& Cluster) {
        Cluster.SpawnRadius = FMath::Max(Cluster.SpawnRadius, BackgroundRadius);
        Cluster.MinDistanceBetweenInstances = FMath::Max(100.0f, Cluster.MinDistanceBetweenInstances);
        Cluster.SpawnCountMin = FMath::Max(0.1f, Cluster.SpawnCountMin);
        Cluster.SpawnCountMax = FMath::Max(Cluster.SpawnCountMin, Cluster.SpawnCountMax);
    };

    if (!ChapterConfig)
    {
        MakeFallbackClusters();
        return;
    }

    if (!bAutoBuildBackgroundClustersFromThemes)
    {
        for (FMeshCluster& ExistingCluster : BackgroundClusters)
        {
            NormalizeCluster(ExistingCluster);
        }
        MakeFallbackClusters();
        return;
    }

    // If user already configured BackgroundClusters in RaidLayoutManager,
    // never auto-merge ThemeRegistry foliage (room foliage leakage prevention).
    if (BackgroundClusters.Num() > 0)
    {
        for (FMeshCluster& ExistingCluster : BackgroundClusters)
        {
            NormalizeCluster(ExistingCluster);
        }
        return;
    }

    TArray<FMeshCluster> GeneratedClusters;
    GeneratedClusters.Reserve(16);
    auto IsExplicitBackgroundClusterName = [](const FString& Name) -> bool
        {
            const FString Lower = Name.ToLower();
            return
                Lower.StartsWith(TEXT("bg")) ||
                Lower.Contains(TEXT("background")) ||
                Lower.Contains(TEXT("scenery"));
        };

    for (const TPair<FString, FModularMeshKit>& ThemePair : ChapterConfig->ThemeRegistry)
    {
        const FString& ThemeName = ThemePair.Key;
        const FModularMeshKit& ThemeKit = ThemePair.Value;

        for (const FMeshCluster& SourceCluster : ThemeKit.FoliageClusters)
        {
            if (SourceCluster.Variations.Num() == 0) continue;
            if (!IsExplicitBackgroundClusterName(SourceCluster.ClusterName))
            {
                continue;
            }

            FMeshCluster NewCluster = SourceCluster;
            if (NewCluster.ClusterName.IsEmpty())
            {
                NewCluster.ClusterName = FString::Printf(TEXT("%s_BackgroundFoliage"), *ThemeName);
            }
            else if (!NewCluster.ClusterName.StartsWith(ThemeName))
            {
                NewCluster.ClusterName = FString::Printf(TEXT("%s_%s"), *ThemeName, *NewCluster.ClusterName);
            }
            NormalizeCluster(NewCluster);
            GeneratedClusters.Add(NewCluster);
        }
    }

    const bool bBuiltFromThemeRegistry = GeneratedClusters.Num() > 0;
    BackgroundClusters = MoveTemp(GeneratedClusters);
    for (FMeshCluster& ExistingCluster : BackgroundClusters)
    {
        NormalizeCluster(ExistingCluster);
    }

    MakeFallbackClusters();
    if (BackgroundClusters.Num() > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] BackgroundClusters initialized (%d). Source=%s"),
            BackgroundClusters.Num(),
            bBuiltFromThemeRegistry ? TEXT("ThemeRegistry(Background-tagged only)") : TEXT("Fallback"));
    }
}

void ARaidLayoutManager::ClearBackgroundScenery()
{
    int32 DestroyedTrackedActorCount = 0;
    int32 DestroyedTaggedActorCount = 0;
    int32 DestroyedTrackedISMCCount = 0;
    int32 DestroyedTaggedOwnedISMCCount = 0;
    UWorld* CurrentWorld = GetWorld();
    const bool bEditorPersistenceWorld = CurrentWorld && !CurrentWorld->IsGameWorld();

    for (AActor* BackgroundActor : SpawnedBackgroundActors)
    {
        if (IsValid(BackgroundActor))
        {
            BackgroundActor->Destroy();
            ++DestroyedTrackedActorCount;
        }
    }
    SpawnedBackgroundActors.Empty();

    for (UHierarchicalInstancedStaticMeshComponent* ISMC : BackgroundISMC_Pool)
    {
        if (IsValid(ISMC))
        {
            if (bEditorPersistenceWorld)
            {
                RemoveInstanceComponent(ISMC);
            }
            ISMC->DestroyComponent();
            ++DestroyedTrackedISMCCount;
        }
    }
    BackgroundISMC_Pool.Empty();

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor == this)
            {
                continue;
            }

            if (Actor->ActorHasTag(TEXT("RaidBackgroundScenery")))
            {
                Actor->Destroy();
                ++DestroyedTaggedActorCount;
            }
        }
    }

    TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> OwnedISMComponents(this);
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : OwnedISMComponents)
    {
        if (!IsValid(ISMC))
        {
            continue;
        }

        const bool bTaggedBackground = ISMC->ComponentTags.Contains(TEXT("RaidBackgroundScenery"));
        const bool bRuntimeGenerated = (ISMC->CreationMethod == EComponentCreationMethod::Instance);
        if (bTaggedBackground || bRuntimeGenerated)
        {
            if (bEditorPersistenceWorld)
            {
                RemoveInstanceComponent(ISMC);
            }
            ISMC->DestroyComponent();
            ++DestroyedTaggedOwnedISMCCount;
        }
    }

    if (DestroyedTrackedActorCount > 0 || DestroyedTaggedActorCount > 0 || DestroyedTrackedISMCCount > 0 || DestroyedTaggedOwnedISMCCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidLayout] ClearBackgroundScenery destroyed TrackedActors=%d TaggedActors=%d TrackedISMC=%d TaggedOwnedISMC=%d"),
            DestroyedTrackedActorCount,
            DestroyedTaggedActorCount,
            DestroyedTrackedISMCCount,
            DestroyedTaggedOwnedISMCCount);
    }

#if WITH_EDITOR
    if (bEditorPersistenceWorld)
    {
        Modify();
        MarkPackageDirty();
    }
#endif
}

void ARaidLayoutManager::ApplyProceduralLandscapeDeformation(const FString& DominantEnv) {}
void ARaidLayoutManager::AutoSetupPrototypeRaid() { AutoGenerateWhiteboxFromCSV(); SpawnRaidLayout(); }
void ARaidLayoutManager::AutoFinalizeImportedData() { SpawnRaidLayout(); }
void ARaidLayoutManager::OneClickCsvImportBuild() {
#if WITH_EDITOR
    FString CsvPath; if (URaidEditorPipelineLibrary::PickCsvFile(CsvPath)) { FString OutMsg; URaidEditorPipelineLibrary::OneClickImportAndBuild(CsvPath, TEXT("/Game/Raid/Data/DT_AI_Raid_Design"), ChapterConfig, this, true, true, false, OutMsg); }
#endif
}
void ARaidLayoutManager::RunFullContentAuditAndRepair() {
#if WITH_EDITOR
    FString OutReport, OutSummary; URaidEditorPipelineLibrary::AuditAllProjectContent(true, false, OutReport, OutSummary);
#endif
}
bool ARaidLayoutManager::ApplyOpenWorldSpecFromCsvPath(const FString& CsvPath) { LastOpenWorldSpecDirectory = FPaths::GetPath(CsvPath); return true; }
