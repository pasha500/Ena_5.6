#include "Raid/RaidRoomActor.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidLootRegistry.h"
#include "Raid/RaidRegionBannerWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/EngineTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "LandscapeProxy.h"
#if WITH_EDITOR
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#endif

namespace
{
    TWeakObjectPtr<URaidRegionBannerWidget> GSharedRegionBannerWidget;
    const FName RaidLootOutlineActiveTag(TEXT("RaidLootOutlineActive"));
    TSet<const UWorld*> GLootOutlinePPAttemptedWorlds;
    const FName RaidRoomGeneratedTag(TEXT("RaidRoomGenerated"));

    FName MakeRaidRoomNodeTag(const int32 NodeId)
    {
        return FName(*FString::Printf(TEXT("RaidRoomNode_%d"), NodeId));
    }

    EObjectFlags ResolveRoomSpawnObjectFlags(const UWorld* World)
    {
        return (World && World->IsGameWorld())
            ? (RF_Transient | RF_DuplicateTransient | RF_TextExportTransient)
            : RF_NoFlags;
    }

    void ApplyGridSizeFromRoomSizeToken(const FString& SizeToken, int32& InOutGridSize)
    {
        if (SizeToken.Equals(TEXT("Small"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 9;
        }
        else if (SizeToken.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 13;
        }
        else if (SizeToken.Equals(TEXT("Large"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 21;
        }
        else if (SizeToken.Equals(TEXT("Massive"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 31;
        }
        else if (InOutGridSize <= 0)
        {
            InOutGridSize = 13;
        }
    }

    FString ResolveBannerTitleFromNodeTags(const FLevelNodeRow& NodeRow)
    {
        TArray<FString> TagTokens;
        NodeRow.NodeTags.ParseIntoArray(TagTokens, TEXT(","), true);
        for (FString& Token : TagTokens)
        {
            Token.TrimStartAndEndInline();
            if (!Token.IsEmpty() && !Token.Contains(TEXT("[")))
            {
                return Token;
            }
        }

        if (!NodeRow.NodeTags.IsEmpty() && !NodeRow.NodeTags.Contains(TEXT("[")))
        {
            return NodeRow.NodeTags;
        }

        return FString();
    }

    FString ResolveBannerSubtitleFromRoomType(const FLevelNodeRow& NodeRow)
    {
        FString RoomTypeLabel;
        const FString& RoomType = NodeRow.RoomType;
        if (RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("시작 구역 (Start)");
        }
        else if (RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("교전 구역 (Combat)");
        }
        else if (RoomType.Equals(TEXT("Loot"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("루팅 구역 (Loot)");
        }
        else if (RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("보스 구역 (Boss)");
        }
        else if (RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("탈출 구역 (Exit)");
        }
        else
        {
            RoomTypeLabel = TEXT("일반 구역 (Sector)");
        }

        if (NodeRow.EnvType.IsEmpty())
        {
            return RoomTypeLabel;
        }

        return FString::Printf(TEXT("%s | %s"), *RoomTypeLabel, *NodeRow.EnvType);
    }

    bool HasBlendableMaterial(const FPostProcessSettings& Settings, const UMaterialInterface* Material)
    {
        if (!IsValid(Material))
        {
            return false;
        }

        for (const FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
        {
            if (Blendable.Object == Material)
            {
                return true;
            }
        }
        return false;
    }

    bool IsPawnInsideRoomBannerZone(const ARaidRoomActor* Room, const APawn* Pawn)
    {
        if (!Room || !Pawn)
        {
            return false;
        }

        const FVector LocalPlayerLoc = Room->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
        const FVector RoomExtent = Room->GetRoomExtent();
        constexpr float BannerZoneInset = 320.0f;
        const float ZoneX = FMath::Max(300.0f, RoomExtent.X - BannerZoneInset);
        const float ZoneY = FMath::Max(300.0f, RoomExtent.Y - BannerZoneInset);

        return
            FMath::Abs(LocalPlayerLoc.X) <= ZoneX &&
            FMath::Abs(LocalPlayerLoc.Y) <= ZoneY;
    }

    bool IsOutdoorStyleRoom(const FLevelNodeRow& NodeRow)
    {
        const FString Meta = (NodeRow.EnvType + TEXT(" ") + NodeRow.Theme + TEXT(" ") + NodeRow.NodeTags + TEXT(" ") + NodeRow.RoomRole).ToLower();
        const bool bForceIndoor =
            Meta.Contains(TEXT("tarkov")) ||
            Meta.Contains(TEXT("cqb")) ||
            Meta.Contains(TEXT("indoor")) ||
            Meta.Contains(TEXT("factory")) ||
            Meta.Contains(TEXT("warehouse")) ||
            Meta.Contains(TEXT("mall")) ||
            Meta.Contains(TEXT("실내")) ||
            Meta.Contains(TEXT("타르코프"));
        const bool bForceOutdoor =
            Meta.Contains(TEXT("openworld")) ||
            Meta.Contains(TEXT("open world")) ||
            Meta.Contains(TEXT("outdoor")) ||
            Meta.Contains(TEXT("오픈월드")) ||
            Meta.Contains(TEXT("야외"));
        const bool bEnvOutdoor =
            NodeRow.EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
            NodeRow.EnvType.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
            NodeRow.EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase) ||
            NodeRow.Theme.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
            NodeRow.Theme.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
            NodeRow.Theme.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
        return bForceOutdoor || (!bForceIndoor && bEnvOutdoor);
    }

    bool IsRuntimePrototypeEngineMesh(const UWorld* World, const UStaticMesh* Mesh)
    {
        if (!World || !World->IsGameWorld() || !IsValid(Mesh))
        {
            return false;
        }

        const FString MeshPath = Mesh->GetPathName();
        return MeshPath.StartsWith(TEXT("/Engine/BasicShapes/"));
    }

    ERaidVariationOffsetChannel ResolveOffsetChannelForMeshType(int32 MeshType)
    {
        switch (MeshType)
        {
        case 0:
            return ERaidVariationOffsetChannel::Floor;
        case 1:
            return ERaidVariationOffsetChannel::Wall;
        case 2:
            return ERaidVariationOffsetChannel::Obstacle;
        case 3:
            return ERaidVariationOffsetChannel::Decoration;
        case 4:
            return ERaidVariationOffsetChannel::Doorway;
        case 5:
            return ERaidVariationOffsetChannel::DoorBlocker;
        case 6:
        case 7:
        case 8:
            return ERaidVariationOffsetChannel::Foliage;
        default:
            return ERaidVariationOffsetChannel::Default;
        }
    }

    bool RoomActor_IsInsideWaterPhysicsVolume(UWorld* World, const FVector& Location, float SphereRadius = 20.0f)
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

    bool IsWaterHit(const FHitResult& Hit)
    {
        if (const AActor* HitActor = Hit.GetActor())
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

        if (const UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HitComp->ComponentHasTag(TEXT("Water"))) return true;
            const FString CompClass = HitComp->GetClass()->GetName();
            if (CompClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase)) return true;
        }
        return false;
    }

    bool IsLandscapeLikeHit(const FHitResult& Hit)
    {
        auto HasLandscapeKeyword = [](const FString& ClassName) -> bool
            {
                return
                    ClassName.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
                    ClassName.Contains(TEXT("LandProxy"), ESearchCase::IgnoreCase);
            };

        if (const AActor* HitActor = Hit.GetActor())
        {
            if (HasLandscapeKeyword(HitActor->GetClass()->GetName()))
            {
                return true;
            }
        }

        if (const UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HasLandscapeKeyword(HitComp->GetClass()->GetName()))
            {
                return true;
            }
        }

        return false;
    }

    bool ContainsKeywordWithLetterBoundary(const FString& LowerSource, const TCHAR* Keyword)
    {
        if (LowerSource.IsEmpty() || !Keyword || !*Keyword)
        {
            return false;
        }

        const int32 KeywordLen = FCString::Strlen(Keyword);
        int32 SearchStart = 0;
        while (SearchStart < LowerSource.Len())
        {
            const int32 FoundIndex = LowerSource.Find(Keyword, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
            if (FoundIndex == INDEX_NONE)
            {
                break;
            }

            const int32 EndIndex = FoundIndex + KeywordLen;
            const bool bPrevIsLetter = (FoundIndex > 0) && FChar::IsAlpha(LowerSource[FoundIndex - 1]);
            const bool bNextIsLetter = (EndIndex < LowerSource.Len()) && FChar::IsAlpha(LowerSource[EndIndex]);
            if (!bPrevIsLetter && !bNextIsLetter)
            {
                return true;
            }

            SearchStart = FoundIndex + 1;
        }

        return false;
    }

#if WITH_EDITOR
    float ResolveAutoFlattenRadiusFromMeshBounds(const UStaticMesh* Mesh, const FVector& WorldScaleAbs)
    {
        if (!IsValid(Mesh))
        {
            return 0.0f;
        }

        const FBox MeshBounds = Mesh->GetBoundingBox();
        if (!MeshBounds.IsValid)
        {
            return 0.0f;
        }

        const FVector Extent = MeshBounds.GetExtent();
        const float ExtentXY = FMath::Max(Extent.X * WorldScaleAbs.X, Extent.Y * WorldScaleAbs.Y);
        if (ExtentXY <= KINDA_SMALL_NUMBER)
        {
            return 0.0f;
        }

        return FMath::Clamp(ExtentXY * 0.95f, 180.0f, 3200.0f);
    }

    uint32 HashMix32(uint32 Value)
    {
        Value ^= Value >> 16;
        Value *= 0x7feb352du;
        Value ^= Value >> 15;
        Value *= 0x846ca68bu;
        Value ^= Value >> 16;
        return Value;
    }

    float HashCell01(int32 X, int32 Y, uint32 Seed)
    {
        uint32 Hash = Seed;
        Hash ^= HashMix32(static_cast<uint32>(X));
        Hash = HashMix32(Hash ^ (HashMix32(static_cast<uint32>(Y)) + 0x9E3779B9u));
        return static_cast<float>(Hash & 0x00FFFFFFu) / 16777216.0f;
    }

    double BytesToMiB(const uint64 Bytes)
    {
        return static_cast<double>(Bytes) / (1024.0 * 1024.0);
    }

    bool ShouldSkipLandscapeFlattenForLowMemory(FPlatformMemoryStats& OutStats)
    {
        OutStats = FPlatformMemory::GetStats();
        constexpr uint64 MinAvailablePhysicalBytes = 1024ull * 1024ull * 1024ull; // 1.0 GiB
        constexpr uint64 MinAvailableVirtualBytes = 512ull * 1024ull * 1024ull;   // 0.5 GiB
        return OutStats.AvailablePhysical < MinAvailablePhysicalBytes ||
               OutStats.AvailableVirtual < MinAvailableVirtualBytes;
    }

    bool HasLandscapeFlattenMemoryBudget(
        const uint64 RequiredWorkingBytes,
        const uint64 SafetyReserveBytes,
        FPlatformMemoryStats& OutStats)
    {
        OutStats = FPlatformMemory::GetStats();
        if (RequiredWorkingBytes > (TNumericLimits<uint64>::Max() - SafetyReserveBytes))
        {
            return false;
        }

        const uint64 RequiredWithReserve = RequiredWorkingBytes + SafetyReserveBytes;
        return OutStats.AvailablePhysical > RequiredWithReserve &&
               OutStats.AvailableVirtual > RequiredWithReserve;
    }

    void LogLandscapeFlattenMemorySkip(const TCHAR* Reason, const FPlatformMemoryStats& Stats)
    {
        static double LastLogSeconds = -1000.0;
        const double NowSeconds = FPlatformTime::Seconds();
        if ((NowSeconds - LastLogSeconds) < 1.0)
        {
            return;
        }

        LastLogSeconds = NowSeconds;
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom][TerrainStabilize] Editor landscape flatten skipped (%s). AvailPhysical=%.1f MiB AvailVirtual=%.1f MiB"),
            Reason,
            BytesToMiB(Stats.AvailablePhysical),
            BytesToMiB(Stats.AvailableVirtual));
    }

    int32 ApplyEditorLandscapeFlattenBlob(
        UWorld* World,
        AActor* SplineOwner,
        const FVector& Center,
        float YawDeg,
        float FlattenRadius,
        float SideFalloff,
        float TargetZ,
        bool bRaiseHeights,
        bool bLowerHeights,
        float EdgeCliffRatio,
        float EdgeErosionRatio,
        float EdgePatchSize,
        float EdgeErosionStrength,
        float EdgeSmoothStrength)
    {
        static_cast<void>(SplineOwner);
        static_cast<void>(YawDeg);

        if (!World)
        {
            return 0;
        }
        // Landscape height data edits are editor-authoring behavior only.
        // Running this during PIE/GameWorld can race with runtime physics/collision state (Chaos).
        if (World->IsGameWorld())
        {
            return 0;
        }

        FPlatformMemoryStats MemoryStats;
        if (ShouldSkipLandscapeFlattenForLowMemory(MemoryStats))
        {
            LogLandscapeFlattenMemorySkip(TEXT("LowMemory"), MemoryStats);
            return 0;
        }

        const float SafeRadius = FMath::Clamp(FlattenRadius, 10.0f, 10000.0f);
        const float SafeFalloff = FMath::Clamp(SideFalloff, 0.0f, 6000.0f);
        const float SafeCliffRatio = FMath::Clamp(EdgeCliffRatio, 0.0f, 0.8f);
        const float SafeErosionRatio = FMath::Clamp(EdgeErosionRatio, 0.0f, 1.0f - SafeCliffRatio);
        const float SafePatchSize = FMath::Clamp(EdgePatchSize, 120.0f, 6000.0f);
        const float SafeErosionStrength = FMath::Clamp(EdgeErosionStrength, 0.0f, 1.0f);
        const float SafeSmoothStrength = FMath::Clamp(EdgeSmoothStrength, 0.0f, 1.0f);
        const float EffectiveSmoothStrength = FMath::Clamp(SafeSmoothStrength * 1.35f + 0.45f, 0.0f, 1.0f);
        // Keep cliff/erosion as subtle accents only; smooth remains dominant.
        const float SubtleCliffInfluence = FMath::Clamp(
            SafeCliffRatio * (1.0f - SafeSmoothStrength * 0.75f),
            0.0f,
            0.08f);
        const float SubtleErosionInfluence = FMath::Clamp(
            SafeErosionRatio * SafeErosionStrength * (1.0f - SafeSmoothStrength * 0.65f),
            0.0f,
            0.12f);
        const float SubtleErosionJitterAmplitude = FMath::Clamp(SubtleErosionInfluence * 8.0f, 0.0f, 1.2f);
        const float SpikeClampMinBase = FMath::Clamp(
            5.0f + (1.0f - EffectiveSmoothStrength) * 10.0f,
            5.0f,
            14.0f);
        const float SpikeClampMaxBase = FMath::Clamp(
            14.0f + (1.0f - EffectiveSmoothStrength) * 20.0f,
            14.0f,
            34.0f);
        const float FlattenOuterRadius = SafeRadius + SafeFalloff;
        // Boundary-warp params: distort flatten perimeter itself so stamp-like circles disappear.
        const float FlattenBoundaryNoisePatchSize = FMath::Clamp(SafePatchSize * 0.72f, 96.0f, 2600.0f);
        const float FlattenBoundaryNoiseRadiusJitter = FMath::Clamp(
            FMath::Max(SafeFalloff * 0.30f, SafeRadius * 0.14f),
            60.0f,
            1600.0f);
        const float FlattenBoundaryNoiseStartRadius = SafeRadius + (SafeFalloff * 0.42f);
        // Keep all deformation strictly outside flatten area so spawned objects inside
        // the flattened zone are not perturbed by edge styling.
        const float OuterOnlyStartOffset = FMath::Clamp(
            FMath::Max(SafeFalloff * 0.09f, SafeRadius * 0.05f),
            28.0f,
            320.0f);
        const float ProtectedCoreRadius = FMath::Max(SafeRadius + (SafeFalloff * 0.90f), SafeRadius + 75.0f);
        // Stronger outside ring to hide circular flatten stamp from long distances.
        const float EdgeSmoothExtraRadius = FMath::Clamp(
            FMath::Max(SafeFalloff * 1.45f, SafeRadius * 0.46f),
            300.0f,
            6000.0f);
        const float EdgeSmoothInnerRadius = ProtectedCoreRadius;
        const float EdgeSmoothOuterRadius = ProtectedCoreRadius + EdgeSmoothExtraRadius;
        // Break perfect-circle artifacts on the transition ring with local radius jitter.
        const float EdgeNoiseRadiusJitter = FMath::Clamp(
            FMath::Max(SafeFalloff * 0.38f, SafeRadius * 0.19f),
            80.0f,
            2200.0f);

        if (SafeRadius <= KINDA_SMALL_NUMBER)
        {
            return 0;
        }
        if (!bRaiseHeights && !bLowerHeights)
        {
            return 0;
        }

        TSet<ULandscapeInfo*> ProcessedLandscapeInfos;
        int32 AppliedLandscapeCount = 0;
        const float AffectedRadius = EdgeSmoothOuterRadius + 256.0f;
        const float AffectedRadiusSq = FMath::Square(AffectedRadius);
        for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
        {
            ALandscapeProxy* Landscape = *It;
            if (!IsValid(Landscape))
            {
                continue;
            }

            const FBox LandscapeBounds = Landscape->GetComponentsBoundingBox(true);
            if (!LandscapeBounds.IsValid)
            {
                continue;
            }

            const FVector Closest = LandscapeBounds.GetClosestPointTo(Center);
            if (FVector::DistSquaredXY(Closest, Center) > AffectedRadiusSq)
            {
                continue;
            }

            ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
            if (!LandscapeInfo || ProcessedLandscapeInfos.Contains(LandscapeInfo))
            {
                continue;
            }

            int32 ExtentMinX = 0;
            int32 ExtentMinY = 0;
            int32 ExtentMaxX = 0;
            int32 ExtentMaxY = 0;
            if (!LandscapeInfo->GetLandscapeExtent(ExtentMinX, ExtentMinY, ExtentMaxX, ExtentMaxY))
            {
                continue;
            }

            const FTransform LandscapeToWorld = Landscape->LandscapeActorToWorld();
            const FVector LandscapeScaleAbs = LandscapeToWorld.GetScale3D().GetAbs();
            const float SafeScaleX = FMath::Max(LandscapeScaleAbs.X, KINDA_SMALL_NUMBER);
            const float SafeScaleY = FMath::Max(LandscapeScaleAbs.Y, KINDA_SMALL_NUMBER);

            const FVector WorldTarget(Center.X, Center.Y, TargetZ);
            const FVector LocalTarget = LandscapeToWorld.InverseTransformPosition(WorldTarget);
            const float LocalCenterX = LocalTarget.X;
            const float LocalCenterY = LocalTarget.Y;
            const float LocalTargetZ = LocalTarget.Z;

            const float CoreRadiusLocalX = SafeRadius / SafeScaleX;
            const float CoreRadiusLocalY = SafeRadius / SafeScaleY;
            const float FalloffRadiusLocalX = SafeFalloff / SafeScaleX;
            const float FalloffRadiusLocalY = SafeFalloff / SafeScaleY;
            const float EdgeSmoothOuterRadiusLocalX = EdgeSmoothOuterRadius / SafeScaleX;
            const float EdgeSmoothOuterRadiusLocalY = EdgeSmoothOuterRadius / SafeScaleY;

            int32 X1 = FMath::FloorToInt(LocalCenterX - EdgeSmoothOuterRadiusLocalX);
            int32 Y1 = FMath::FloorToInt(LocalCenterY - EdgeSmoothOuterRadiusLocalY);
            int32 X2 = FMath::CeilToInt(LocalCenterX + EdgeSmoothOuterRadiusLocalX);
            int32 Y2 = FMath::CeilToInt(LocalCenterY + EdgeSmoothOuterRadiusLocalY);
            X1 = FMath::Clamp(X1, ExtentMinX, ExtentMaxX);
            Y1 = FMath::Clamp(Y1, ExtentMinY, ExtentMaxY);
            X2 = FMath::Clamp(X2, ExtentMinX, ExtentMaxX);
            Y2 = FMath::Clamp(Y2, ExtentMinY, ExtentMaxY);
            if (X1 > X2 || Y1 > Y2)
            {
                continue;
            }

            FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
            const int32 Width = X2 - X1 + 1;
            const int32 Height = Y2 - Y1 + 1;
            const uint64 HeightSampleCount = static_cast<uint64>(Width) * static_cast<uint64>(Height);
            const uint64 EstimatedWorkingBytes = HeightSampleCount * sizeof(uint16) * 4ull;
            constexpr uint64 FlattenSafetyReserveBytes = 256ull * 1024ull * 1024ull; // 256 MiB
            FPlatformMemoryStats FlattenBudgetStats;
            if (!HasLandscapeFlattenMemoryBudget(EstimatedWorkingBytes, FlattenSafetyReserveBytes, FlattenBudgetStats))
            {
                LogLandscapeFlattenMemorySkip(TEXT("InsufficientBudget"), FlattenBudgetStats);
                continue;
            }

            TArray<uint16> HeightData;
            HeightData.SetNumUninitialized(Width * Height);
            LandscapeEdit.GetHeightDataFast(X1, Y1, X2, Y2, HeightData.GetData(), 0);
            TArray<uint16> OriginalHeightData = HeightData;

            const uint16 TargetTexHeight = LandscapeDataAccess::GetTexHeight(LocalTargetZ);
            bool bChanged = false;

            for (int32 LocalY = Y1; LocalY <= Y2; ++LocalY)
            {
                for (int32 LocalX = X1; LocalX <= X2; ++LocalX)
                {
                    const float DeltaWorldX = (static_cast<float>(LocalX) - LocalCenterX) * SafeScaleX;
                    const float DeltaWorldY = (static_cast<float>(LocalY) - LocalCenterY) * SafeScaleY;
                    const float DistanceWorld = FMath::Sqrt(DeltaWorldX * DeltaWorldX + DeltaWorldY * DeltaWorldY);
                    const float WorldX = Center.X + DeltaWorldX;
                    const float WorldY = Center.Y + DeltaWorldY;
                    const int32 BoundaryCellX = FMath::FloorToInt(WorldX / FlattenBoundaryNoisePatchSize);
                    const int32 BoundaryCellY = FMath::FloorToInt(WorldY / FlattenBoundaryNoisePatchSize);
                    const float BoundaryNoiseCoarse = HashCell01(BoundaryCellX, BoundaryCellY, 0x2A6D9F17u);
                    const float BoundaryNoiseFine = HashCell01(LocalX, LocalY, 0x9A4E13C1u);
                    const float BoundaryNoise = FMath::Lerp(BoundaryNoiseCoarse, BoundaryNoiseFine, 0.40f);
                    const float AngleRad = FMath::Atan2(DeltaWorldY, DeltaWorldX);
                    const float BoundaryAngularNoise =
                        (FMath::Sin(AngleRad * 3.0f + (BoundaryNoiseCoarse * 2.0f * PI)) * 0.52f) +
                        (FMath::Sin(AngleRad * 5.0f + (BoundaryNoiseFine * 2.0f * PI)) * 0.48f);
                    float LocalFlattenOuterRadius = FlattenOuterRadius;
                    if (DistanceWorld >= FlattenBoundaryNoiseStartRadius)
                    {
                        const float BoundaryShift =
                            ((BoundaryNoise - 0.5f) * 2.0f * FlattenBoundaryNoiseRadiusJitter) +
                            (BoundaryAngularNoise * FlattenBoundaryNoiseRadiusJitter * 0.16f);
                        LocalFlattenOuterRadius += BoundaryShift;
                    }
                    LocalFlattenOuterRadius = FMath::Clamp(
                        LocalFlattenOuterRadius,
                        SafeRadius + 60.0f,
                        FlattenOuterRadius + FlattenBoundaryNoiseRadiusJitter);
                    if (DistanceWorld > LocalFlattenOuterRadius)
                    {
                        continue;
                    }

                    float BlendAlpha = 1.0f;
                    if (DistanceWorld > SafeRadius && LocalFlattenOuterRadius > SafeRadius + KINDA_SMALL_NUMBER)
                    {
                        const float LocalFalloffRadius = FMath::Max(LocalFlattenOuterRadius - SafeRadius, 1.0f);
                        BlendAlpha = 1.0f - (DistanceWorld - SafeRadius) / LocalFalloffRadius;
                    }
                    BlendAlpha = FMath::Clamp(BlendAlpha, 0.0f, 1.0f);
                    BlendAlpha = BlendAlpha * BlendAlpha * (3.0f - 2.0f * BlendAlpha); // smoothstep

                    const int32 DataIndex = (LocalY - Y1) * Width + (LocalX - X1);
                    const uint16 CurrentHeight = HeightData[DataIndex];
                    const int32 CurrentI = static_cast<int32>(CurrentHeight);
                    const int32 TargetI = static_cast<int32>(TargetTexHeight);
                    int32 NewHeightI = CurrentI;

                    if (bRaiseHeights && bLowerHeights)
                    {
                        NewHeightI = FMath::RoundToInt(FMath::Lerp(
                            static_cast<float>(CurrentI),
                            static_cast<float>(TargetI),
                            BlendAlpha));
                    }
                    else if (bRaiseHeights)
                    {
                        if (TargetI > CurrentI)
                        {
                            const int32 Blended = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(CurrentI),
                                static_cast<float>(TargetI),
                                BlendAlpha));
                            NewHeightI = FMath::Max(CurrentI, Blended);
                        }
                    }
                    else if (bLowerHeights)
                    {
                        if (TargetI < CurrentI)
                        {
                            const int32 Blended = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(CurrentI),
                                static_cast<float>(TargetI),
                                BlendAlpha));
                            NewHeightI = FMath::Min(CurrentI, Blended);
                        }
                    }

                    NewHeightI = FMath::Clamp(NewHeightI, 0, static_cast<int32>(LandscapeDataAccess::MaxValue));
                    const uint16 NewHeight = static_cast<uint16>(NewHeightI);
                    if (NewHeight != CurrentHeight)
                    {
                        HeightData[DataIndex] = NewHeight;
                        bChanged = true;
                    }
                }
            }

            // Secondary pass: smooth inside flattened area and the inner transition band
            // so the interior/edge connection does not look like a hard circular stamp.
            if (Width >= 3 && Height >= 3)
            {
                const float InnerSmoothStartRadius = FMath::Max(SafeRadius * 0.12f, 45.0f);
                const float InnerSmoothEndRadius = FMath::Max(
                    FlattenOuterRadius + (SafeFalloff * 0.30f),
                    InnerSmoothStartRadius + 80.0f);
                const float InnerSmoothWidth = FMath::Max(InnerSmoothEndRadius - InnerSmoothStartRadius, KINDA_SMALL_NUMBER);
                const int32 InnerSmoothIterations = FMath::Clamp(
                    FMath::RoundToInt(2.0f + SafeSmoothStrength * 3.0f),
                    2,
                    5);

                for (int32 Iteration = 0; Iteration < InnerSmoothIterations; ++Iteration)
                {
                    const TArray<uint16> PrevHeightData = HeightData;
                    bool bIterationChanged = false;

                    for (int32 LocalY = Y1 + 1; LocalY <= Y2 - 1; ++LocalY)
                    {
                        for (int32 LocalX = X1 + 1; LocalX <= X2 - 1; ++LocalX)
                        {
                            const float DeltaWorldX = (static_cast<float>(LocalX) - LocalCenterX) * SafeScaleX;
                            const float DeltaWorldY = (static_cast<float>(LocalY) - LocalCenterY) * SafeScaleY;
                            const float DistanceWorld = FMath::Sqrt(DeltaWorldX * DeltaWorldX + DeltaWorldY * DeltaWorldY);
                            if (DistanceWorld > InnerSmoothEndRadius)
                            {
                                continue;
                            }

                            const float BlendT = FMath::Clamp(
                                (DistanceWorld - InnerSmoothStartRadius) / InnerSmoothWidth,
                                0.0f,
                                1.0f);
                            float SmoothBandAlpha = 0.26f + BlendT * 0.60f;
                            SmoothBandAlpha *= (0.70f + SafeSmoothStrength * 0.45f);
                            SmoothBandAlpha = FMath::Clamp(SmoothBandAlpha, 0.0f, 1.0f);
                            if (SmoothBandAlpha <= KINDA_SMALL_NUMBER)
                            {
                                continue;
                            }

                            const int32 DataIndex = (LocalY - Y1) * Width + (LocalX - X1);
                            const int32 CurrentI = static_cast<int32>(PrevHeightData[DataIndex]);
                            const int32 OriginalI = static_cast<int32>(OriginalHeightData[DataIndex]);

                            int32 NeighborhoodSum = 0;
                            for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                            {
                                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                                {
                                    const int32 SampleIndex =
                                        (LocalY + OffsetY - Y1) * Width + (LocalX + OffsetX - X1);
                                    NeighborhoodSum += static_cast<int32>(PrevHeightData[SampleIndex]);
                                }
                            }
                            const int32 NeighborAvgI = FMath::RoundToInt(static_cast<float>(NeighborhoodSum) / 9.0f);
                            const int32 TargetI = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(NeighborAvgI),
                                static_cast<float>(OriginalI),
                                0.24f));

                            int32 NewHeightI = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(CurrentI),
                                static_cast<float>(TargetI),
                                SmoothBandAlpha));

                            const int32 ClampDelta = FMath::Max(6, FMath::RoundToInt(FMath::Lerp(12.0f, 24.0f, BlendT)));
                            NewHeightI = FMath::Clamp(NewHeightI, NeighborAvgI - ClampDelta, NeighborAvgI + ClampDelta);

                            if (!bRaiseHeights && NewHeightI > CurrentI)
                            {
                                NewHeightI = CurrentI;
                            }
                            if (!bLowerHeights && NewHeightI < CurrentI)
                            {
                                NewHeightI = CurrentI;
                            }

                            NewHeightI = FMath::Clamp(NewHeightI, 0, static_cast<int32>(LandscapeDataAccess::MaxValue));
                            const uint16 NewHeight = static_cast<uint16>(NewHeightI);
                            if (NewHeight != HeightData[DataIndex])
                            {
                                HeightData[DataIndex] = NewHeight;
                                bIterationChanged = true;
                                bChanged = true;
                            }
                        }
                    }

                    if (!bIterationChanged)
                    {
                        break;
                    }
                }
            }

            const float EdgeSmoothWidth = FMath::Max(EdgeSmoothOuterRadius - EdgeSmoothInnerRadius, KINDA_SMALL_NUMBER);
            if (Width >= 3 && Height >= 3 && EdgeSmoothWidth > KINDA_SMALL_NUMBER)
            {
                const int32 SmoothingIterations = FMath::Clamp(
                    FMath::RoundToInt(11.0f + (SafeSmoothStrength * 6.0f)),
                    12,
                    18);
                const float StrongSmoothBoost = 0.96f;
                const int32 CenterCellX = FMath::FloorToInt(Center.X / SafePatchSize);
                const int32 CenterCellY = FMath::FloorToInt(Center.Y / SafePatchSize);
                const float AxisNoise = HashCell01(CenterCellX, CenterCellY, 0xB5297A4Du);
                const float AxisAngleRad = AxisNoise * (2.0f * PI);
                const FVector2D PrimaryAxis(FMath::Cos(AxisAngleRad), FMath::Sin(AxisAngleRad));

                for (int32 Iteration = 0; Iteration < SmoothingIterations; ++Iteration)
                {
                    const TArray<uint16> PrevHeightData = HeightData;
                    bool bIterationChanged = false;

                    for (int32 LocalY = Y1 + 1; LocalY <= Y2 - 1; ++LocalY)
                    {
                        for (int32 LocalX = X1 + 1; LocalX <= X2 - 1; ++LocalX)
                        {
                            const float DeltaWorldX = (static_cast<float>(LocalX) - LocalCenterX) * SafeScaleX;
                            const float DeltaWorldY = (static_cast<float>(LocalY) - LocalCenterY) * SafeScaleY;
                            const float DistanceWorld = FMath::Sqrt(DeltaWorldX * DeltaWorldX + DeltaWorldY * DeltaWorldY);
                            const float WorldX = Center.X + DeltaWorldX;
                            const float WorldY = Center.Y + DeltaWorldY;
                            const int32 PatchX = FMath::FloorToInt(WorldX / SafePatchSize);
                            const int32 PatchY = FMath::FloorToInt(WorldY / SafePatchSize);
                            const float PatchStyleNoise = HashCell01(PatchX, PatchY, 0x68E31DA4u);
                            const float ErosionNoise = HashCell01(PatchX, PatchY, 0x1B56C4E9u);
                            const float RadiusNoiseCoarse = HashCell01(PatchX, PatchY, 0xD18A57F3u);
                            const float RadiusNoiseFine = HashCell01(LocalX, LocalY, 0x7B64C18Du);
                            const float RadiusNoise = FMath::Lerp(RadiusNoiseCoarse, RadiusNoiseFine, 0.35f);
                            const float AngleRad = FMath::Atan2(DeltaWorldY, DeltaWorldX);
                            const float AngularWobble =
                                (FMath::Sin((AngleRad + AxisAngleRad) * 3.0f + (RadiusNoiseCoarse * 2.0f * PI)) * 0.50f) +
                                (FMath::Sin((AngleRad - AxisAngleRad * 0.55f) * 5.0f + (RadiusNoiseFine * 2.0f * PI)) * 0.50f);
                            const float LocalRadiusShift =
                                ((RadiusNoise - 0.5f) * 2.0f * EdgeNoiseRadiusJitter) +
                                (AngularWobble * EdgeNoiseRadiusJitter * 0.22f);
                            const int32 BoundaryCellX = FMath::FloorToInt(WorldX / FlattenBoundaryNoisePatchSize);
                            const int32 BoundaryCellY = FMath::FloorToInt(WorldY / FlattenBoundaryNoisePatchSize);
                            const float BoundaryNoiseCoarse = HashCell01(BoundaryCellX, BoundaryCellY, 0x2A6D9F17u);
                            const float BoundaryNoiseFine = HashCell01(LocalX, LocalY, 0x9A4E13C1u);
                            const float BoundaryNoise = FMath::Lerp(BoundaryNoiseCoarse, BoundaryNoiseFine, 0.40f);
                            const float BoundaryAngularNoise =
                                (FMath::Sin((AngleRad + AxisAngleRad * 0.20f) * 3.0f + (BoundaryNoiseCoarse * 2.0f * PI)) * 0.52f) +
                                (FMath::Sin((AngleRad - AxisAngleRad * 0.16f) * 5.0f + (BoundaryNoiseFine * 2.0f * PI)) * 0.48f);
                            const float BoundaryShift =
                                ((BoundaryNoise - 0.5f) * 2.0f * FlattenBoundaryNoiseRadiusJitter) +
                                (BoundaryAngularNoise * FlattenBoundaryNoiseRadiusJitter * 0.16f);
                            const float LocalFlattenOuterRadius = FMath::Clamp(
                                FlattenOuterRadius + BoundaryShift,
                                SafeRadius + 60.0f,
                                FlattenOuterRadius + FlattenBoundaryNoiseRadiusJitter);
                            const float LocalProtectedRadius = FMath::Max(
                                ProtectedCoreRadius,
                                LocalFlattenOuterRadius + (OuterOnlyStartOffset * 0.55f));
                            const float LocalInnerRadius = FMath::Max(
                                LocalProtectedRadius,
                                EdgeSmoothInnerRadius + FMath::Max(0.0f, LocalRadiusShift * 0.09f));
                            const float LocalOuterRadius = FMath::Max(
                                LocalInnerRadius + 88.0f,
                                EdgeSmoothOuterRadius + LocalRadiusShift);
                            if (DistanceWorld < LocalInnerRadius || DistanceWorld > LocalOuterRadius)
                            {
                                continue;
                            }

                            const float LocalEdgeWidth = FMath::Max(LocalOuterRadius - LocalInnerRadius, KINDA_SMALL_NUMBER);
                            const float BlendT = FMath::Clamp(
                                (DistanceWorld - LocalInnerRadius) / LocalEdgeWidth,
                                0.0f,
                                1.0f);
                            float SmoothAlpha = 1.0f - BlendT;
                            SmoothAlpha = SmoothAlpha * SmoothAlpha * (3.0f - 2.0f * SmoothAlpha); // smoothstep
                            SmoothAlpha = FMath::Pow(SmoothAlpha, 0.40f);
                            const float RingMidBoost = 1.0f - FMath::Abs((BlendT * 2.0f) - 1.0f);
                            SmoothAlpha = FMath::Clamp(SmoothAlpha + RingMidBoost * 0.62f, 0.0f, 1.0f);
                            if (SmoothAlpha <= KINDA_SMALL_NUMBER)
                            {
                                continue;
                            }

                            const int32 DataIndex = (LocalY - Y1) * Width + (LocalX - X1);
                            const int32 CurrentI = static_cast<int32>(PrevHeightData[DataIndex]);
                            const int32 OriginalI = static_cast<int32>(OriginalHeightData[DataIndex]);

                            int32 NeighborhoodSum = 0;
                            int32 NeighborhoodMin = TNumericLimits<int32>::Max();
                            int32 WideNeighborhoodSum = 0;
                            int32 WideNeighborhoodCount = 0;
                            for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                            {
                                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                                {
                                    const int32 SampleIndex =
                                        (LocalY + OffsetY - Y1) * Width + (LocalX + OffsetX - X1);
                                    const int32 SampleI = static_cast<int32>(PrevHeightData[SampleIndex]);
                                    NeighborhoodSum += SampleI;
                                    NeighborhoodMin = FMath::Min(NeighborhoodMin, SampleI);
                                }
                            }
                            for (int32 OffsetY = -2; OffsetY <= 2; ++OffsetY)
                            {
                                for (int32 OffsetX = -2; OffsetX <= 2; ++OffsetX)
                                {
                                    if (OffsetX == 0 && OffsetY == 0)
                                    {
                                        continue;
                                    }
                                    const int32 SampleLocalX = FMath::Clamp(LocalX + OffsetX, X1, X2);
                                    const int32 SampleLocalY = FMath::Clamp(LocalY + OffsetY, Y1, Y2);
                                    const int32 SampleIndex =
                                        (SampleLocalY - Y1) * Width + (SampleLocalX - X1);
                                    WideNeighborhoodSum += static_cast<int32>(PrevHeightData[SampleIndex]);
                                    ++WideNeighborhoodCount;
                                }
                            }

                            const int32 NeighborAvgI = FMath::RoundToInt(static_cast<float>(NeighborhoodSum) / 9.0f);
                            const int32 WideNeighborAvgI = WideNeighborhoodCount > 0
                                ? FMath::RoundToInt(static_cast<float>(WideNeighborhoodSum) / static_cast<float>(WideNeighborhoodCount))
                                : NeighborAvgI;
                            const FVector2D RadialDir = FVector2D(DeltaWorldX, DeltaWorldY).GetSafeNormal();
                            const float AxisAlignment = FMath::Abs(FVector2D::DotProduct(RadialDir, PrimaryAxis));
                            const bool bStrongSmoothZone = AxisAlignment >= 0.72f;

                            const float StyleNoise = FMath::Lerp(PatchStyleNoise, RadiusNoise, 0.22f);
                            const float CliffBandThreshold = FMath::Clamp(SubtleCliffInfluence * 2.6f, 0.0f, 0.18f);
                            const float ErosionBandThreshold = FMath::Clamp(
                                CliffBandThreshold + (SubtleErosionInfluence * 2.2f),
                                CliffBandThreshold,
                                0.32f);
                            const bool bUseCliff = (BlendT > 0.86f) && (StyleNoise < CliffBandThreshold);
                            const bool bUseErosion = !bUseCliff && (StyleNoise < ErosionBandThreshold);

                            const int32 SmoothTargetI = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(WideNeighborAvgI),
                                static_cast<float>(OriginalI),
                                0.36f));
                            int32 BlendTargetI = SmoothTargetI;
                            float StyleStrength = EffectiveSmoothStrength + (bStrongSmoothZone ? StrongSmoothBoost : 0.58f);
                            if (bUseCliff)
                            {
                                const int32 CliffNeighborI = FMath::RoundToInt(FMath::Lerp(
                                    static_cast<float>(NeighborAvgI),
                                    static_cast<float>(WideNeighborAvgI),
                                    0.35f));
                                BlendTargetI = FMath::RoundToInt(FMath::Lerp(
                                    static_cast<float>(SmoothTargetI),
                                    static_cast<float>(CliffNeighborI),
                                    0.25f));
                                StyleStrength *= FMath::Clamp(0.35f + (SubtleCliffInfluence * 2.8f), 0.20f, 0.45f);
                            }
                            else if (bUseErosion)
                            {
                                const int32 ErosionBaseI = FMath::RoundToInt(FMath::Lerp(
                                    static_cast<float>(WideNeighborAvgI),
                                    static_cast<float>(NeighborhoodMin),
                                    0.22f));
                                BlendTargetI = FMath::RoundToInt(FMath::Lerp(
                                    static_cast<float>(SmoothTargetI),
                                    static_cast<float>(ErosionBaseI),
                                    0.32f));
                                StyleStrength *= FMath::Clamp(0.40f + (SubtleErosionInfluence * 2.6f), 0.25f, 0.55f);
                            }

                            StyleStrength = FMath::Clamp(StyleStrength, 0.0f, 1.0f);
                            const float EffectiveBlend = FMath::Clamp(
                                StyleStrength * (0.88f + FMath::Pow(SmoothAlpha, 0.40f)),
                                0.0f,
                                1.0f);

                            int32 NewHeightI = FMath::RoundToInt(FMath::Lerp(
                                static_cast<float>(CurrentI),
                                static_cast<float>(BlendTargetI),
                                EffectiveBlend));

                            if (bUseErosion && SubtleErosionJitterAmplitude > KINDA_SMALL_NUMBER)
                            {
                                const float ErosionJitter = (ErosionNoise - 0.5f) * SubtleErosionJitterAmplitude * SmoothAlpha;
                                NewHeightI += FMath::RoundToInt(ErosionJitter);
                            }

                            // Hard safety clamp against local outlier spikes on the boundary ring.
                            const int32 MaxDeltaToWide = FMath::Max(
                                5,
                                FMath::RoundToInt(FMath::Lerp(SpikeClampMinBase, SpikeClampMaxBase, BlendT)));
                            const int32 MinAllowedI = WideNeighborAvgI - MaxDeltaToWide;
                            const int32 MaxAllowedI = WideNeighborAvgI + MaxDeltaToWide;
                            NewHeightI = FMath::Clamp(NewHeightI, MinAllowedI, MaxAllowedI);

                            if (!bRaiseHeights && NewHeightI > CurrentI)
                            {
                                NewHeightI = CurrentI;
                            }
                            if (!bLowerHeights && NewHeightI < CurrentI)
                            {
                                NewHeightI = CurrentI;
                            }

                            NewHeightI = FMath::Clamp(NewHeightI, 0, static_cast<int32>(LandscapeDataAccess::MaxValue));
                            const uint16 NewHeight = static_cast<uint16>(NewHeightI);
                            if (NewHeight != HeightData[DataIndex])
                            {
                                HeightData[DataIndex] = NewHeight;
                                bIterationChanged = true;
                                bChanged = true;
                            }
                        }
                    }

                    if (!bIterationChanged)
                    {
                        break;
                    }
                }
            }

            if (!bChanged)
            {
                continue;
            }

            Landscape->Modify();
            LandscapeEdit.SetHeightData(
                X1,
                Y1,
                X2,
                Y2,
                HeightData.GetData(),
                0,
                true);
            ProcessedLandscapeInfos.Add(LandscapeInfo);
            ++AppliedLandscapeCount;
        }

        // Heightmap visual update alone can leave stale collision in PIE if collision rebuild
        // does not run before game world starts. Force collision sync for edited landscapes.
        for (ULandscapeInfo* EditedLandscapeInfo : ProcessedLandscapeInfos)
        {
            if (!EditedLandscapeInfo)
            {
                continue;
            }

            EditedLandscapeInfo->RecreateCollisionComponents();
        }

        return AppliedLandscapeCount;
    }
#endif

    bool IsTreeLikeMeshName(const FString& InName)
    {
        const FString Lower = InName.ToLower();
        auto IsExplicitWindExcludedAssetPath = [](const FString& LowerAssetPath) -> bool
        {
            static const TCHAR* ExcludedPathKeywords[] = {
                TEXT("/game/sajeongjeoncomplex/cheonchujeon/map/bp_cheonchujeon"),
                TEXT("/game/sajeongjeoncomplex/sajeongjeon/map/bp_sajeongjeon"),
                TEXT("/game/templesofcambodia/environment/bigrock_01/sm_bigrock_01_01"),
                TEXT("/game/templesofcambodia/environment/bigrock_01/sm_bigrock_01_02"),
                TEXT("/game/templesofcambodia/environment/bigrock_01/sm_bigrock_01_03"),
                TEXT("/game/templesofcambodia/environment/bigrock_02/sm_bigrock_02_01"),
                TEXT("/game/templesofcambodia/environment/bigrock_03/sm_bigrock_03_01")
            };

            for (const TCHAR* Keyword : ExcludedPathKeywords)
            {
                if (LowerAssetPath.Contains(Keyword))
                {
                    return true;
                }
            }
            return false;
        };

        if (IsExplicitWindExcludedAssetPath(Lower))
        {
            return false;
        }

        static const TCHAR* Keywords[] = {
            TEXT("tree"),
            TEXT("sapling"),
            TEXT("pine"),
            TEXT("oak"),
            TEXT("beech"),
            TEXT("birch"),
            TEXT("fir"),
            TEXT("spruce"),
            TEXT("palm"),
            TEXT("cypress"),
            TEXT("willow"),
            TEXT("trunk"),
            TEXT("pinetree"),
            TEXT("oaktree"),
            TEXT("beechtree"),
            TEXT("birchtree"),
            TEXT("firtree"),
            TEXT("sprucetree"),
            TEXT("palmtree"),
            TEXT("cypresstree"),
            TEXT("willowtree")
        };

        for (const TCHAR* Keyword : Keywords)
        {
            if (ContainsKeywordWithLetterBoundary(Lower, Keyword))
            {
                return true;
            }
        }
        return false;
    }

    bool ReadNumericPropertyAsDouble(const FProperty* Property, const void* Container, double& OutValue)
    {
        if (!Property || !Container)
        {
            return false;
        }

        if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            OutValue = DoubleProperty->GetPropertyValue_InContainer(Container);
            return true;
        }

        if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            OutValue = static_cast<double>(FloatProperty->GetPropertyValue_InContainer(Container));
            return true;
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Container);
            if (!ValuePtr)
            {
                return false;
            }

            OutValue = NumericProperty->IsFloatingPoint()
                ? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
                : static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
            return true;
        }

        return false;
    }

    bool WriteNumericPropertyFromDouble(FProperty* Property, void* Container, double InValue)
    {
        if (!Property || !Container || !FMath::IsFinite(InValue))
        {
            return false;
        }

        if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            DoubleProperty->SetPropertyValue_InContainer(Container, InValue);
            return true;
        }

        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            FloatProperty->SetPropertyValue_InContainer(Container, static_cast<float>(InValue));
            return true;
        }

        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Container);
            if (!ValuePtr)
            {
                return false;
            }

            if (NumericProperty->IsFloatingPoint())
            {
                NumericProperty->SetFloatingPointPropertyValue(ValuePtr, InValue);
            }
            else
            {
                NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToInt64(InValue)));
            }
            return true;
        }

        return false;
    }

    struct FRaidLootRuntimeRowData
    {
        bool bValid = false;
        FString RowName;
        FString TypeValue;
        FString MainClassPath;
        bool bHasMaxQuantity = false;
        double MaxQuantity = 0.0;
        bool bHasCurrentQuantity = false;
        double CurrentQuantity = 0.0;
        bool bHasParam1 = false;
        double Param1 = 0.0;
        bool bHasParam2 = false;
        double Param2 = 0.0;
        bool bHasPickOnlyWhenBackpack = false;
        bool bPickOnlyWhenBackpack = false;
    };

    FString NormalizeClassPathForComparison(const FString& InPath)
    {
        FString Normalized = InPath;
        Normalized.TrimStartAndEndInline();
        Normalized.ReplaceInline(TEXT("\""), TEXT(""));

        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (Normalized.FindChar(TEXT('\''), FirstQuote) && Normalized.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
        {
            Normalized = Normalized.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
        }

        const FString Prefix = TEXT("/Script/Engine.BlueprintGeneratedClass");
        if (Normalized.StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            Normalized.RightChopInline(Prefix.Len(), EAllowShrinking::No);
        }

        Normalized.TrimStartAndEndInline();
        return Normalized;
    }

    bool IsPropertyNameMatchLoose(const FString& PropertyName, const FString& Token)
    {
        const FString LowerName = PropertyName.ToLower();
        const FString LowerToken = Token.ToLower();
        return
            LowerName.Equals(LowerToken, ESearchCase::CaseSensitive) ||
            LowerName.StartsWith(LowerToken + TEXT("_"), ESearchCase::CaseSensitive) ||
            LowerName.Contains(LowerToken, ESearchCase::CaseSensitive);
    }

    bool IsPropertyNameMatchAnyLoose(const FString& PropertyName, const std::initializer_list<const TCHAR*>& Tokens)
    {
        for (const TCHAR* Token : Tokens)
        {
            if (IsPropertyNameMatchLoose(PropertyName, FString(Token)))
            {
                return true;
            }
        }
        return false;
    }

    FString NormalizeIdentifierToken(const FString& InValue)
    {
        FString Normalized;
        Normalized.Reserve(InValue.Len());
        for (const TCHAR Char : InValue)
        {
            if (FChar::IsAlnum(Char))
            {
                Normalized.AppendChar(FChar::ToLower(Char));
            }
        }
        return Normalized;
    }

    bool IdentifierTokenMatches(const FString& LeftToken, const FString& RightToken)
    {
        if (LeftToken.IsEmpty() || RightToken.IsEmpty())
        {
            return false;
        }

        return
            LeftToken.Equals(RightToken, ESearchCase::CaseSensitive) ||
            LeftToken.Contains(RightToken, ESearchCase::CaseSensitive) ||
            RightToken.Contains(LeftToken, ESearchCase::CaseSensitive);
    }

    bool ResolveEnumValueByRowName(const UEnum* EnumDef, const FName& RowName, int64& OutValue)
    {
        if (!EnumDef || RowName.IsNone())
        {
            return false;
        }

        const FString RowToken = NormalizeIdentifierToken(RowName.ToString());
        if (RowToken.IsEmpty())
        {
            return false;
        }

        for (int32 EnumIdx = 0; EnumIdx < EnumDef->NumEnums(); ++EnumIdx)
        {
            bool bIsHiddenEntry = false;
#if WITH_EDITOR
            bIsHiddenEntry = EnumDef->HasMetaData(TEXT("Hidden"), EnumIdx);
#endif
            if (bIsHiddenEntry)
            {
                continue;
            }

            const int64 EnumValue = EnumDef->GetValueByIndex(EnumIdx);
            if (EnumValue == INDEX_NONE)
            {
                continue;
            }

            const FString NameToken = NormalizeIdentifierToken(EnumDef->GetNameStringByIndex(EnumIdx));
            if (!NameToken.IsEmpty() && NameToken.EndsWith(TEXT("max"), ESearchCase::CaseSensitive))
            {
                continue;
            }

            const FString DisplayToken = NormalizeIdentifierToken(EnumDef->GetDisplayNameTextByIndex(EnumIdx).ToString());
            if (IdentifierTokenMatches(RowToken, NameToken) || IdentifierTokenMatches(RowToken, DisplayToken))
            {
                OutValue = EnumValue;
                return true;
            }
        }

        return false;
    }

    int32 ApplyLootRowModelIndexOverrideToObject(UObject* TargetObject, const FName& InDataRowName)
    {
        if (!IsValid(TargetObject) || InDataRowName.IsNone())
        {
            return 0;
        }

        int32 AppliedCount = 0;
        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeModelIndexProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("RifleModelIndex"),
                    TEXT("WeaponModelIndex"),
                    TEXT("GunModelIndex")
                });

            if (!bLooksLikeModelIndexProperty)
            {
                continue;
            }

            int64 ResolvedEnumValue = INDEX_NONE;

            if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                if (!ResolveEnumValueByRowName(EnumProp->GetEnum(), InDataRowName, ResolvedEnumValue))
                {
                    continue;
                }

                if (FNumericProperty* UnderlyingProperty = EnumProp->GetUnderlyingProperty())
                {
                    void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(TargetObject);
                    if (ValuePtr)
                    {
                        UnderlyingProperty->SetIntPropertyValue(ValuePtr, ResolvedEnumValue);
                        ++AppliedCount;
                    }
                }
                continue;
            }

            if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
            {
                if (!ResolveEnumValueByRowName(ByteProp->Enum, InDataRowName, ResolvedEnumValue))
                {
                    continue;
                }

                ByteProp->SetPropertyValue_InContainer(TargetObject, static_cast<uint8>(FMath::Clamp<int64>(ResolvedEnumValue, 0, 255)));
                ++AppliedCount;
                continue;
            }
        }

        return AppliedCount;
    }

    int32 ApplyLootDataRowNameOverrideToObject(UObject* TargetObject, const FName& InDataRowName)
    {
        if (!IsValid(TargetObject) || InDataRowName.IsNone())
        {
            return 0;
        }

        const FString DataRowString = InDataRowName.ToString();
        int32 AppliedCount = 0;

        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeRowNameProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("DataRowName"),
                    TEXT("LootDataRowName"),
                    TEXT("ItemDataRowName"),
                    TEXT("WeaponDataRowName"),
                    TEXT("RifleDataRowName"),
                    TEXT("RifleAssetRowName"),
                    TEXT("RifleRowName"),
                    TEXT("WeaponRowName")
                });

            if (!bLooksLikeRowNameProperty)
            {
                continue;
            }

            if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                NameProp->SetPropertyValue_InContainer(TargetObject, InDataRowName);
                ++AppliedCount;
                continue;
            }

            if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                StrProp->SetPropertyValue_InContainer(TargetObject, DataRowString);
                ++AppliedCount;
                continue;
            }
        }

        AppliedCount += ApplyLootRowModelIndexOverrideToObject(TargetObject, InDataRowName);
        return AppliedCount;
    }

    bool HasExplicitLootDataRowOverride(const UObject* TargetObject)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            const FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeRowNameProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("DataRowName"),
                    TEXT("LootDataRowName"),
                    TEXT("ItemDataRowName"),
                    TEXT("WeaponDataRowName"),
                    TEXT("RifleDataRowName"),
                    TEXT("RifleAssetRowName"),
                    TEXT("RifleRowName"),
                    TEXT("WeaponRowName")
                });

            if (!bLooksLikeRowNameProperty)
            {
                continue;
            }

            if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                if (!NameProp->GetPropertyValue_InContainer(TargetObject).IsNone())
                {
                    return true;
                }
                continue;
            }

            if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                FString RowValue = StrProp->GetPropertyValue_InContainer(TargetObject);
                RowValue.TrimStartAndEndInline();
                if (!RowValue.IsEmpty())
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool TryExtractLootRuntimeRowData(const FName RowName, const UScriptStruct* RowStruct, const uint8* RowData, FRaidLootRuntimeRowData& OutData)
    {
        if (!RowStruct || !RowData)
        {
            return false;
        }

        FRaidLootRuntimeRowData Result;
        Result.RowName = RowName.ToString();

        for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
        {
            const FProperty* Property = *PropIt;
            const FString PropName = Property->GetName();

            if (const FClassProperty* ClassProp = CastField<FClassProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    if (const UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue_InContainer(RowData)))
                    {
                        Result.MainClassPath = ClassValue->GetPathName();
                    }
                }
                continue;
            }

            if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    const FSoftObjectPtr& SoftClass = SoftClassProp->GetPropertyValue_InContainer(RowData);
                    Result.MainClassPath = SoftClass.ToString();
                }
                continue;
            }

            if (const FSoftObjectProperty* SoftObjectProp = CastField<FSoftObjectProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    const FSoftObjectPtr& SoftObj = SoftObjectProp->GetPropertyValue_InContainer(RowData);
                    Result.MainClassPath = SoftObj.ToString();
                }
                continue;
            }

            if (const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    if (const UObject* Obj = ObjectProp->GetPropertyValue_InContainer(RowData))
                    {
                        Result.MainClassPath = Obj->GetPathName();
                    }
                }
                continue;
            }

            if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                const FString Value = StrProp->GetPropertyValue_InContainer(RowData);
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    Result.MainClassPath = Value;
                }
                else if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                const FString Value = NameProp->GetPropertyValue_InContainer(RowData).ToString();
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    Result.MainClassPath = Value;
                }
                else if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FTextProperty* TextProp = CastField<FTextProperty>(Property))
            {
                const FString Value = TextProp->GetPropertyValue_InContainer(RowData).ToString();
                if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("PickOnlyWhenBackpack")))
                {
                    Result.bHasPickOnlyWhenBackpack = true;
                    Result.bPickOnlyWhenBackpack = BoolProp->GetPropertyValue_InContainer(RowData);
                }
                continue;
            }

            double NumericValue = 0.0;
            if (!ReadNumericPropertyAsDouble(Property, RowData, NumericValue))
            {
                continue;
            }

            if (IsPropertyNameMatchLoose(PropName, TEXT("MaxQuantity")))
            {
                Result.bHasMaxQuantity = true;
                Result.MaxQuantity = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("CurrentQuantity")))
            {
                Result.bHasCurrentQuantity = true;
                Result.CurrentQuantity = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("Param1")))
            {
                Result.bHasParam1 = true;
                Result.Param1 = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("Param2")))
            {
                Result.bHasParam2 = true;
                Result.Param2 = NumericValue;
            }
        }

        Result.MainClassPath = NormalizeClassPathForComparison(Result.MainClassPath);
        Result.bValid = true;
        OutData = Result;
        return true;
    }

    UDataTable* GetLootItemsDataTable()
    {
        static TWeakObjectPtr<UDataTable> CachedTable;
        if (CachedTable.IsValid())
        {
            return CachedTable.Get();
        }

        static const FSoftObjectPath LootTablePath(TEXT("/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable"));
        UDataTable* LootTable = Cast<UDataTable>(LootTablePath.TryLoad());
        CachedTable = LootTable;
        return LootTable;
    }

    bool ResolveLootRuntimeRowForSpawnedClass(
        const FRaidLootCandidate* Candidate,
        const UClass* SpawnedClass,
        FRaidLootRuntimeRowData& OutData)
    {
        UDataTable* LootTable = GetLootItemsDataTable();
        if (!IsValid(LootTable) || !LootTable->GetRowStruct() || !SpawnedClass)
        {
            return false;
        }

        const FString SpawnedClassPath = NormalizeClassPathForComparison(SpawnedClass->GetPathName());
        const FString SpawnedClassName = SpawnedClass->GetName();

        auto TryReadRow = [LootTable](const FName InRowName, FRaidLootRuntimeRowData& OutRow) -> bool
        {
            const uint8* const* Found = LootTable->GetRowMap().Find(InRowName);
            if (!Found || !*Found)
            {
                return false;
            }
            return TryExtractLootRuntimeRowData(InRowName, LootTable->GetRowStruct(), *Found, OutRow);
        };

        if (Candidate && !Candidate->DataRowName.IsNone())
        {
            FRaidLootRuntimeRowData CandidateRow;
            if (TryReadRow(Candidate->DataRowName, CandidateRow))
            {
                OutData = CandidateRow;
                return true;
            }
        }

        for (const TPair<FName, uint8*>& RowPair : LootTable->GetRowMap())
        {
            FRaidLootRuntimeRowData RowData;
            if (!TryExtractLootRuntimeRowData(RowPair.Key, LootTable->GetRowStruct(), RowPair.Value, RowData))
            {
                continue;
            }

            const FString RowClassPath = NormalizeClassPathForComparison(RowData.MainClassPath);
            const bool bClassPathMatch =
                !RowClassPath.IsEmpty() &&
                (RowClassPath.Equals(SpawnedClassPath, ESearchCase::IgnoreCase) ||
                    RowClassPath.Contains(SpawnedClassPath, ESearchCase::IgnoreCase) ||
                    SpawnedClassPath.Contains(RowClassPath, ESearchCase::IgnoreCase));

            const bool bClassNameMatch =
                !RowClassPath.IsEmpty() &&
                RowClassPath.Contains(SpawnedClassName, ESearchCase::IgnoreCase);

            if (bClassPathMatch || bClassNameMatch)
            {
                OutData = RowData;
                return true;
            }
        }

        return false;
    }

    int32 ApplyLootRuntimeRowDataToActor(
        AActor* LootActor,
        const FRaidLootRuntimeRowData& RowData,
        const bool bApplyParamValues,
        const bool bApplyQuantityValues,
        const bool bApplyPickupRestrictionValues,
        const bool bAllowModelIndexFromParam1)
    {
        if (!IsValid(LootActor) || !RowData.bValid)
        {
            return 0;
        }

        auto ApplyToObject = [&RowData, bApplyParamValues, bApplyQuantityValues, bApplyPickupRestrictionValues, bAllowModelIndexFromParam1](UObject* TargetObject) -> int32
            {
                if (!IsValid(TargetObject))
                {
                    return 0;
                }

                const bool bHasExplicitRowOverride = HasExplicitLootDataRowOverride(TargetObject);
                int32 AppliedCountLocal = 0;
                for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
                {
                    FProperty* Property = *PropIt;
                    if (!Property)
                    {
                        continue;
                    }

                    const FString PropName = Property->GetName();

                    if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
                    {
                        if (bApplyPickupRestrictionValues && RowData.bHasPickOnlyWhenBackpack && IsPropertyNameMatchLoose(PropName, TEXT("PickOnlyWhenBackpack")))
                        {
                            const bool bCurrent = BoolProp->GetPropertyValue_InContainer(TargetObject);
                            if (bCurrent != RowData.bPickOnlyWhenBackpack)
                            {
                                const_cast<FBoolProperty*>(BoolProp)->SetPropertyValue_InContainer(TargetObject, RowData.bPickOnlyWhenBackpack);
                                ++AppliedCountLocal;
                            }
                        }
                        continue;
                    }

                    if (bApplyQuantityValues && RowData.bHasMaxQuantity && IsPropertyNameMatchLoose(PropName, TEXT("MaxQuantity")))
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.MaxQuantity))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (bApplyQuantityValues && RowData.bHasCurrentQuantity && IsPropertyNameMatchLoose(PropName, TEXT("CurrentQuantity")))
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.CurrentQuantity))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bParam1Field = IsPropertyNameMatchLoose(PropName, TEXT("Param1"));
                    if (bApplyParamValues && RowData.bHasParam1 && bParam1Field)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param1))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bModelIndexField = IsPropertyNameMatchAnyLoose(
                        PropName,
                        {
                            TEXT("RifleModelIndex"),
                            TEXT("WeaponModelIndex"),
                            TEXT("GunModelIndex")
                        });

                    if (bApplyParamValues && RowData.bHasParam1 && bModelIndexField && bAllowModelIndexFromParam1 && !bHasExplicitRowOverride)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param1))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bParam2Field =
                        IsPropertyNameMatchLoose(PropName, TEXT("Param2"));

                    if (bApplyParamValues && RowData.bHasParam2 && bParam2Field)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param2))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
                    {
                        if (!RowData.RowName.IsEmpty() &&
                            IsPropertyNameMatchAnyLoose(
                                PropName,
                                {
                                    TEXT("DataRowName"),
                                    TEXT("LootDataRowName"),
                                    TEXT("ItemDataRowName"),
                                    TEXT("WeaponDataRowName"),
                                    TEXT("RifleDataRowName"),
                                    TEXT("RifleAssetRowName"),
                                    TEXT("RifleRowName"),
                                    TEXT("WeaponRowName")
                                }))
                        {
                            const_cast<FNameProperty*>(NameProp)->SetPropertyValue_InContainer(TargetObject, FName(*RowData.RowName));
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
                    {
                        if (!RowData.RowName.IsEmpty() &&
                            IsPropertyNameMatchAnyLoose(
                                PropName,
                                {
                                    TEXT("DataRowName"),
                                    TEXT("LootDataRowName"),
                                    TEXT("ItemDataRowName"),
                                    TEXT("WeaponDataRowName"),
                                    TEXT("RifleDataRowName"),
                                    TEXT("RifleAssetRowName"),
                                    TEXT("RifleRowName"),
                                    TEXT("WeaponRowName")
                                }))
                        {
                            const_cast<FStrProperty*>(StrProp)->SetPropertyValue_InContainer(TargetObject, RowData.RowName);
                            ++AppliedCountLocal;
                        }
                        continue;
                    }
                }

                return AppliedCountLocal;
            };

        int32 AppliedCount = 0;
        AppliedCount += ApplyToObject(LootActor);

        TInlineComponentArray<UActorComponent*> Components(LootActor);
        for (UActorComponent* Component : Components)
        {
            AppliedCount += ApplyToObject(Component);
        }

        return AppliedCount;
    }

    bool RoomActor_BuildWorldFootprintFromLocalBounds(const FBox& LocalBounds, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!LocalBounds.IsValid)
        {
            return false;
        }

        FBox2D Footprint(EForceInit::ForceInit);
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

    bool RoomActor_TryBuildFootprintFromStaticMesh(const UStaticMesh* Mesh, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!Mesh)
        {
            return false;
        }

        return RoomActor_BuildWorldFootprintFromLocalBounds(Mesh->GetBoundingBox(), WorldTransform, OutFootprint);
    }

    bool RoomActor_TryBuildFootprintFromActor(const AActor* Actor, FBox2D& OutFootprint)
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

    float ResolveCoverageRadiusFromFootprint(const FBox2D& Footprint, float CoverageScale)
    {
        if (!Footprint.bIsValid)
        {
            return 0.0f;
        }

        const FVector2D Half2D = (Footprint.Max - Footprint.Min) * 0.5f;
        const float DiagonalRadius = Half2D.Size();
        if (DiagonalRadius <= KINDA_SMALL_NUMBER)
        {
            return 0.0f;
        }

        return DiagonalRadius * FMath::Clamp(CoverageScale, 1.0f, 2.0f);
    }

    bool HasSimpleCollisionGeometry(const UBodySetup* BodySetup)
    {
        if (!BodySetup)
        {
            return false;
        }

        const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
        return
            AggGeom.BoxElems.Num() > 0 ||
            AggGeom.SphereElems.Num() > 0 ||
            AggGeom.SphylElems.Num() > 0 ||
            AggGeom.ConvexElems.Num() > 0;
    }

    constexpr float RoomTraversalWalkableSlopeAngleDeg = 62.0f;

    bool ShouldForceTraversalWalkableSlopeForMeshType(const int32 MeshType)
    {
        // Core traversal targets:
        // 2 = obstacle, 3 = decoration prop (often large rocks/ruins), 8 = foliage-rock cluster.
        return (MeshType == 2 || MeshType == 3 || MeshType == 8);
    }

    bool ShouldForceWalkableCollisionForMesh(const UStaticMesh* Mesh, const int32 MeshType)
    {
        if (!IsValid(Mesh))
        {
            return false;
        }

        // Always force reliable collision for gameplay obstacles.
        if (MeshType == 2)
        {
            return true;
        }

        // Large deco/rock meshes are also used as traversal surfaces in rooms.
        // Keep small props untouched to avoid unstable footing on tiny details.
        if (MeshType == 3 || MeshType == 8)
        {
            const FBox Bounds = Mesh->GetBoundingBox();
            if (!Bounds.IsValid)
            {
                return false;
            }

            const FVector Extent = Bounds.GetExtent();
            const float DiameterXY = 2.0f * FMath::Max(Extent.X, Extent.Y);
            const float Height = 2.0f * Extent.Z;
            return (DiameterXY >= 140.0f || Height >= 110.0f);
        }

        return false;
    }

    void EnsureMeshWalkableCollisionForRoom(UStaticMesh* Mesh, const int32 MeshType)
    {
        if (!IsValid(Mesh) || !ShouldForceWalkableCollisionForMesh(Mesh, MeshType))
        {
            return;
        }

        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (!BodySetup)
        {
            return;
        }

        if (HasSimpleCollisionGeometry(BodySetup) || BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
        {
            return;
        }

#if WITH_EDITOR
        Mesh->Modify();
        BodySetup->Modify();
#endif
        BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
#if WITH_EDITOR
        Mesh->MarkPackageDirty();
#endif
    }

    bool ShouldRejectGroundSupportHit(UWorld* World, const FHitResult& Hit)
    {
        if (!Hit.bBlockingHit)
        {
            return true;
        }

        // WaterBodyOcean/Lake physics volumes can encompass nearby shoreline terrain.
        // Reject explicit water hits, but do not reject valid landscape support solely
        // because the point is inside a broad water physics volume.
        if (IsWaterHit(Hit))
        {
            return true;
        }
        if (RoomActor_IsInsideWaterPhysicsVolume(World, Hit.ImpactPoint, 120.0f) && !IsLandscapeLikeHit(Hit))
        {
            return true;
        }

        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        if (IsValid(HitActor) && HitActor->ActorHasTag(TEXT("RaidDoorBlocker")))
        {
            return true;
        }

        if (IsValid(HitComp))
        {
            if (HitComp->bHiddenInGame || !HitComp->IsVisible())
            {
                return true;
            }
        }

        if (IsValid(HitActor))
        {
            if (HitActor->IsHidden())
            {
                return true;
            }

            if (HitActor->ActorHasTag(RaidRoomGeneratedTag))
            {
                const bool bHiddenGenerated =
                    HitActor->IsHidden() ||
                    (IsValid(HitComp) && (HitComp->bHiddenInGame || !HitComp->IsVisible()));
                if (bHiddenGenerated)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool TryResolveSingleGroundHitAlongTrace(
        UWorld* World,
        const FVector& TraceStart,
        const FVector& TraceEnd,
        bool bPreferLandscape,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit)
    {
        if (!World)
        {
            return false;
        }

        TArray<FHitResult> GroundHits;
        if (!World->LineTraceMultiByChannel(
            GroundHits,
            TraceStart,
            TraceEnd,
            ECC_WorldStatic,
            QueryParams))
        {
            return false;
        }

        const FHitResult* FirstValidHit = nullptr;
        const FHitResult* PreferredLandscapeHit = nullptr;
        for (const FHitResult& CandidateHit : GroundHits)
        {
            if (ShouldRejectGroundSupportHit(World, CandidateHit))
            {
                continue;
            }

            if (!FirstValidHit)
            {
                FirstValidHit = &CandidateHit;
            }

            if (IsLandscapeLikeHit(CandidateHit))
            {
                PreferredLandscapeHit = &CandidateHit;
                break;
            }
        }

        const FHitResult* SelectedGroundHit =
            (bPreferLandscape && PreferredLandscapeHit)
            ? PreferredLandscapeHit
            : FirstValidHit;
        if (!SelectedGroundHit)
        {
            return false;
        }

        OutHit = *SelectedGroundHit;
        return true;
    }

    bool TryResolveActorLowestSupportZ(const AActor* Actor, float& OutMinZ)
    {
        OutMinZ = TNumericLimits<float>::Max();
        if (!IsValid(Actor))
        {
            return false;
        }

        bool bFoundCollisionEnabledPrimitive = false;
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
        Actor->GetComponents(PrimitiveComps);

        for (const UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
        {
            if (!IsValid(PrimitiveComp) || !PrimitiveComp->IsRegistered())
            {
                continue;
            }

            if (!PrimitiveComp->IsCollisionEnabled())
            {
                continue;
            }

            const FBox PrimitiveBounds = PrimitiveComp->Bounds.GetBox();
            if (!PrimitiveBounds.IsValid)
            {
                continue;
            }

            OutMinZ = FMath::Min(OutMinZ, PrimitiveBounds.Min.Z);
            bFoundCollisionEnabledPrimitive = true;
        }

        if (bFoundCollisionEnabledPrimitive && OutMinZ < TNumericLimits<float>::Max())
        {
            return true;
        }

        const FBox FallbackBounds = Actor->GetComponentsBoundingBox(true);
        if (FallbackBounds.IsValid)
        {
            OutMinZ = FallbackBounds.Min.Z;
            return true;
        }

        return false;
    }

    bool RoomActor_IsFootprintOverlappingAny(const TArray<FBox2D>& ExistingFootprints, const FBox2D& CandidateFootprint, float Padding)
    {
        if (!CandidateFootprint.bIsValid)
        {
            return false;
        }

        FBox2D ExpandedCandidate = CandidateFootprint;
        const FVector2D Padding2D(FMath::Max(0.0f, Padding), FMath::Max(0.0f, Padding));
        ExpandedCandidate.Min -= Padding2D;
        ExpandedCandidate.Max += Padding2D;

        for (const FBox2D& Existing : ExistingFootprints)
        {
            if (!Existing.bIsValid)
            {
                continue;
            }
            if (ExpandedCandidate.Intersect(Existing))
            {
                return true;
            }
        }

        return false;
    }

    float ComputeFootprintArea2D(const FBox2D& Footprint)
    {
        if (!Footprint.bIsValid)
        {
            return 0.0f;
        }

        const FVector2D Size = Footprint.GetSize();
        return FMath::Max(0.0f, Size.X) * FMath::Max(0.0f, Size.Y);
    }

    float ComputeFootprintGap2D(const FBox2D& A, const FBox2D& B)
    {
        if (!A.bIsValid || !B.bIsValid)
        {
            return TNumericLimits<float>::Max();
        }

        const float GapX = FMath::Max(0.0f, FMath::Max(A.Min.X - B.Max.X, B.Min.X - A.Max.X));
        const float GapY = FMath::Max(0.0f, FMath::Max(A.Min.Y - B.Max.Y, B.Min.Y - A.Max.Y));
        return FMath::Sqrt((GapX * GapX) + (GapY * GapY));
    }

    bool TryResolveRoomSingleGroundHitAtPoint(
        UWorld* World,
        const FVector& XYLocation,
        bool bPreferLandscape,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit)
    {
        return TryResolveSingleGroundHitAlongTrace(
            World,
            XYLocation + FVector(0.0f, 0.0f, 120000.0f),
            XYLocation + FVector(0.0f, 0.0f, -120000.0f),
            bPreferLandscape,
            QueryParams,
            OutHit);
    }

    void BuildGroundSupportOffsets(int32 SampleCount, float SampleRadius, TArray<FVector2D>& OutOffsets)
    {
        OutOffsets.Reset();
        OutOffsets.Add(FVector2D::ZeroVector);

        if (SampleCount <= 1 || SampleRadius <= 1.0f)
        {
            return;
        }

        OutOffsets.Add(FVector2D(SampleRadius, 0.0f));
        OutOffsets.Add(FVector2D(-SampleRadius, 0.0f));
        OutOffsets.Add(FVector2D(0.0f, SampleRadius));
        OutOffsets.Add(FVector2D(0.0f, -SampleRadius));

        if (SampleCount >= 9)
        {
            const float Diag = SampleRadius * 0.70710678f;
            OutOffsets.Add(FVector2D(Diag, Diag));
            OutOffsets.Add(FVector2D(-Diag, Diag));
            OutOffsets.Add(FVector2D(Diag, -Diag));
            OutOffsets.Add(FVector2D(-Diag, -Diag));
        }
    }

    bool IsLikelyWindPhaseParamName(const FString& LowerParamName)
    {
        const bool bHasPhaseLikeKeyword =
            LowerParamName.Contains(TEXT("phase")) ||
            LowerParamName.Contains(TEXT("timeoffset")) ||
            LowerParamName.Contains(TEXT("windoffset")) ||
            LowerParamName.Contains(TEXT("perinstance")) ||
            LowerParamName.Contains(TEXT("random")) ||
            LowerParamName.Contains(TEXT("variation"));
        if (!bHasPhaseLikeKeyword)
        {
            return false;
        }

        // Never randomize amplitude/speed/strength controls: that causes unstable, unnatural motion.
        if (LowerParamName.Contains(TEXT("strength")) ||
            LowerParamName.Contains(TEXT("intensity")) ||
            LowerParamName.Contains(TEXT("speed")) ||
            LowerParamName.Contains(TEXT("sway")) ||
            LowerParamName.Contains(TEXT("bend")) ||
            LowerParamName.Contains(TEXT("amplitude")) ||
            LowerParamName.Contains(TEXT("weight")) ||
            LowerParamName.Contains(TEXT("gust")))
        {
            return false;
        }

        return
            LowerParamName.Contains(TEXT("wind")) ||
            LowerParamName.Contains(TEXT("tree")) ||
            LowerParamName.Contains(TEXT("foliage")) ||
            LowerParamName.Contains(TEXT("phase")) ||
            LowerParamName.Contains(TEXT("random"));
    }

    void GatherLikelyWindPhaseScalarParams(UMaterialInterface* Material, TArray<FName>& OutParamNames)
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
            if (IsLikelyWindPhaseParamName(LowerName))
            {
                OutParamNames.AddUnique(Info.Name);
            }
        }
    }

    float ResolveWindDesyncValueForParam(const FString& LowerParamName, FRandomStream& Stream)
    {
        if (LowerParamName.Contains(TEXT("phase")))
        {
            return Stream.FRandRange(-PI, PI);
        }
        if (LowerParamName.Contains(TEXT("timeoffset")) || LowerParamName.Contains(TEXT("windoffset")))
        {
            return Stream.FRandRange(-1.0f, 1.0f);
        }
        if (LowerParamName.Contains(TEXT("random")) || LowerParamName.Contains(TEXT("variation")))
        {
            return Stream.FRandRange(0.0f, 1.0f);
        }
        return Stream.FRandRange(0.0f, 1.0f);
    }

    void ApplyRoomTreeWindPhaseDesync(UStaticMeshComponent* MeshComp, FRandomStream& Stream)
    {
        if (!IsValid(MeshComp))
        {
            return;
        }

        // Keep CPD values normalized (0..1). Some foliage materials remap these to
        // phase internally, and large raw values can cause unstable motion.
        MeshComp->SetCustomPrimitiveDataFloat(0, Stream.FRandRange(0.0f, 1.0f));
        MeshComp->SetCustomPrimitiveDataFloat(1, Stream.FRandRange(0.0f, 1.0f));

        static const FName FallbackParamNames[] = {
            TEXT("WindPhaseOffset"),
            TEXT("WindPhase"),
            TEXT("WindTimeOffset"),
            TEXT("PerInstanceRandom"),
            TEXT("TreeWindOffset"),
            TEXT("FoliageRandom")
        };

        const int32 MaterialCount = MeshComp->GetNumMaterials();
        for (int32 MatIndex = 0; MatIndex < MaterialCount; ++MatIndex)
        {
            UMaterialInterface* BaseMat = MeshComp->GetMaterial(MatIndex);
            if (!BaseMat)
            {
                continue;
            }

            TArray<FName> ParamNamesToSet;
            GatherLikelyWindPhaseScalarParams(BaseMat, ParamNamesToSet);
            if (ParamNamesToSet.Num() == 0)
            {
                for (const FName ParamName : FallbackParamNames)
                {
                    ParamNamesToSet.Add(ParamName);
                }
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

            for (const FName ParamName : ParamNamesToSet)
            {
                const FString LowerParamName = ParamName.ToString().ToLower();
                const float ParamValue = ResolveWindDesyncValueForParam(LowerParamName, Stream);
                MID->SetScalarParameterValue(ParamName, ParamValue);
            }
        }
    }
}

ARaidRoomActor::ARaidRoomActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    PrimaryActorTick.TickInterval = RoomTickInterval;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    RootComponent = SceneRoot;

    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
    Trigger->SetMobility(EComponentMobility::Movable);
    Trigger->SetupAttachment(RootComponent);
    Trigger->OnComponentBeginOverlap.AddDynamic(this, &ARaidRoomActor::OnOverlap);
    Trigger->ShapeColor = FColor::Green; Trigger->SetLineThickness(5.0f);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCanEverAffectNavigation(false);
    // Keep trigger out of typical WorldDynamic object traces used by weapon systems.
    Trigger->SetCollisionObjectType(ECC_Destructible);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Trigger->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);

    StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
    StatusText->SetMobility(EComponentMobility::Movable);
    StatusText->SetupAttachment(RootComponent);

    // =========================================================================
    // 1. 글씨 크기 50% 축소: 기존 800.0f -> 400.0f
    StatusText->SetWorldSize(400.0f);

    // 2. 높이 20m & 중심점 맞춤: X=0, Y=0 (정중앙), Z=2000.0f (20미터 상공)
    StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 2000.0f));
    // =========================================================================

    StatusText->SetHorizontalAlignment(EHTA_Center);
    StatusText->SetVerticalAlignment(EVRTA_TextCenter);
    StatusText->SetTextRenderColor(FColor(244, 244, 170, 255));
    StatusText->bAlwaysRenderAsText = true;
    StatusText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusText->SetCollisionResponseToAllChannels(ECR_Ignore);
}

// 카메라 빌보드(Billboard) 로직 (가장 가벼운 연산)
void ARaidRoomActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
    const bool bHasPlayer = IsValid(PlayerPawn) && PlayerPawn->IsPlayerControlled();
    const float MaxTickWorkDistanceSq = FMath::Square(FMath::Max(1000.0f, StatusTextFacingMaxDistance));
    const bool bNearRoom = bHasPlayer && FVector::DistSquared2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= MaxTickWorkDistanceSq;
    const float NearTickInterval = FMath::Max(0.01f, RoomTickInterval);
    const float FarTickInterval = FMath::Max(0.22f, NearTickInterval * 2.75f);
    const float DesiredTickInterval = bNearRoom ? NearTickInterval : FarTickInterval;
    if (!FMath::IsNearlyEqual(PrimaryActorTick.TickInterval, DesiredTickInterval, 0.01f))
    {
        PrimaryActorTick.TickInterval = DesiredTickInterval;
    }

    if (!bEntryBannerShown)
    {
        if (bNearRoom)
        {
            const bool bInsideBannerZone = IsPawnInsideRoomBannerZone(this, PlayerPawn);
            const bool bEnteredBannerZone = bInsideBannerZone && !bWasPlayerInsideBannerZone;

            if (bEnteredBannerZone && !bPendingBannerRetry)
            {
                bPendingBannerRetry = true;
                NextBannerAttemptTimeSeconds = 0.0;
            }

            if (bPendingBannerRetry)
            {
                if (!bInsideBannerZone)
                {
                    bPendingBannerRetry = false;
                    NextBannerAttemptTimeSeconds = 0.0;
                }
                else if (World->GetTimeSeconds() >= NextBannerAttemptTimeSeconds)
                {
                    bPendingBannerRetry = !TryShowRegionBanner(PlayerPawn);
                }
            }

            bWasPlayerInsideBannerZone = bInsideBannerZone;
        }
        else
        {
            bWasPlayerInsideBannerZone = false;
        }
    }

    if (bEnableRoomProximityAutoStart && bHasPlayer && !bCombatStarted && !bCombatCleared && IsProximityAutoStartEligibleRoomType())
    {
        const float TriggerDistanceUU = ResolveProximityAutoStartDistanceUU();
        if (TriggerDistanceUU > KINDA_SMALL_NUMBER)
        {
            const float DistSq2D = FVector::DistSquared2D(PlayerPawn->GetActorLocation(), GetActorLocation());
            if (DistSq2D <= FMath::Square(TriggerDistanceUU))
            {
                if (URaidCombatSubsystem* CombatSubsystem = World->GetSubsystem<URaidCombatSubsystem>())
                {
                    CombatSubsystem->StartCombatForRoom(this);
                    if (IsValid(Trigger))
                    {
                        Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    }
                }
            }
        }
    }

    if (World->GetTimeSeconds() >= NextLootOutlineUpdateTimeSeconds)
    {
        const APawn* OutlinePlayerPawn = bHasPlayer ? PlayerPawn : nullptr;
        UpdateLootProximityOutline(OutlinePlayerPawn);
        NextLootOutlineUpdateTimeSeconds = World->GetTimeSeconds() + FMath::Max(0.02f, LootOutlineUpdateInterval);
    }

    if (StatusText && bNearRoom)
    {
        APlayerCameraManager* CamManager = PC ? PC->PlayerCameraManager : nullptr;
        if (CamManager)
        {
            FVector CamLoc = CamManager->GetCameraLocation();
            FVector TextLoc = StatusText->GetComponentLocation();

            // 카메라를 쳐다보는 각도 계산 (현재 프로젝트 텍스트 전면 기준에 맞춤)
            FRotator LookAtRot = (CamLoc - TextLoc).Rotation();
            LookAtRot.Yaw += 360.0f;
            LookAtRot.Pitch = 0.0f; // 좌우로만 회전하게 고정 (위아래로 누우면 찌그러져 보임)

            StatusText->SetWorldRotation(LookAtRot);
        }
    }
}
void ARaidRoomActor::BeginPlay()
{
    Super::BeginPlay();
    if (!ChapterConfigRef)
    {
        ChapterConfigRef = ChapterConfigAsset.Get();
    }
    PrimaryActorTick.TickInterval = FMath::Max(0.01f, RoomTickInterval);
    EnsureLootOutlinePostProcess(GetWorld());

    UClass* WarmupWidgetClass = RegionBannerWidgetClass.IsNull()
        ? nullptr
        : RegionBannerWidgetClass.LoadSynchronous();
    if (!WarmupWidgetClass)
    {
        WarmupWidgetClass = LoadClass<URaidRegionBannerWidget>(
            nullptr,
            TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C"));
    }
    CachedRegionBannerWidgetClass = WarmupWidgetClass;

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (SharedWidget->GetWorld() != GetWorld())
        {
            if (SharedWidget->IsInViewport())
            {
                SharedWidget->RemoveFromParent();
            }
            GSharedRegionBannerWidget.Reset();
        }
    }
}

void ARaidRoomActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    PendingNavUpdateISMCs.Reset();

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RegionBannerHideTimerHandle);
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (SharedWidget->IsInViewport())
        {
            SharedWidget->RemoveFromParent();
        }
    }
    GSharedRegionBannerWidget.Reset();

    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->RemoveFromParent();
    }
    ActiveRegionBannerWidget = nullptr;
    bPendingBannerRetry = false;

    for (const TWeakObjectPtr<AActor>& WeakLootActor : SpawnedLootActors)
    {
        if (AActor* LootActor = WeakLootActor.Get())
        {
            SetLootActorProximityFx(LootActor, false);
            SetLootActorOutline(LootActor, false);
            SetLootActorDotWidget(LootActor, false);
        }
    }
    SpawnedLootActors.Reset();
    ClearLootProximityFxCache();
    ClearLootDotWidgetCache();
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;

    Super::EndPlay(EndPlayReason);
}

void ARaidRoomActor::SetNodeData(int32 InNodeId, const FLevelNodeRow& InNodeRow, const URaidChapterConfig* InConfig)
{
    for (const TWeakObjectPtr<AActor>& WeakLootActor : SpawnedLootActors)
    {
        if (AActor* LootActor = WeakLootActor.Get())
        {
            SetLootActorProximityFx(LootActor, false);
            SetLootActorOutline(LootActor, false);
            SetLootActorDotWidget(LootActor, false);
        }
    }
    SpawnedLootActors.Reset();
    ClearLootProximityFxCache();
    ClearLootDotWidgetCache();
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;

    CachedResolvedThemeKit = nullptr;
    CachedResolvedThemeKey.Reset();

    NodeId = InNodeId;
    NodeRow = InNodeRow;
    ChapterConfigRef = InConfig;
    ChapterConfigAsset = const_cast<URaidChapterConfig*>(InConfig);
    CurrentRoomType = RaidRoomParsing::ParseRoomType(NodeRow.RoomType);
    RoomRandomStream.Initialize(NodeRow.Seed);
    if (ChapterConfigRef)
    {
        ChapterConfigRef->ResolveThemeKitForNode(NodeRow, CachedResolvedThemeKey, CachedResolvedThemeKit);
    }

    AppliedTerrainFlattenFootprints.Reset();
    PendingBlueprintTerrainPlacements.Reset();
    ApplyGridSizeFromRoomSizeToken(NodeRow.RoomSize, GridSize);
    bNodeDataInitialized = true; bLootAlreadySpawned = false; bEntryBannerShown = false; bPendingBannerRetry = false; bWasPlayerInsideBannerZone = false; EditorLandscapeFlattenOpsApplied = 0; NextBannerAttemptTimeSeconds = 0.0; NextLootOutlineUpdateTimeSeconds = 0.0; CachedProximityAutoStartDistanceUU = -1.0f;
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;
}

void ARaidRoomActor::SetLootActorOutline(AActor* LootActor, bool bEnable) const
{
    if (!IsValid(LootActor))
    {
        return;
    }

    auto ApplyOutlineToActor = [this, bEnable](AActor* TargetActor)
    {
        if (!IsValid(TargetActor))
        {
            return;
        }

        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(TargetActor);
        for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
        {
            if (!IsValid(PrimitiveComponent))
            {
                continue;
            }

            PrimitiveComponent->SetRenderCustomDepth(bEnable);
            if (bEnable)
            {
                PrimitiveComponent->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
                PrimitiveComponent->SetCustomDepthStencilValue(FMath::Clamp(LootOutlineStencilValue, 1, 255));
            }
        }
    };

    ApplyOutlineToActor(LootActor);

    TArray<AActor*> AttachedActors;
    LootActor->GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        ApplyOutlineToActor(AttachedActor);
    }

    if (bEnable)
    {
        LootActor->Tags.AddUnique(RaidLootOutlineActiveTag);
    }
    else
    {
        LootActor->Tags.Remove(RaidLootOutlineActiveTag);
    }
}

void ARaidRoomActor::EnsureLootOutlinePostProcess(UWorld* World)
{
    if (!World || !bEnableLootProximityOutline || !bAutoInstallLootOutlinePostProcess)
    {
        return;
    }

    if (GLootOutlinePPAttemptedWorlds.Contains(World))
    {
        return;
    }
    GLootOutlinePPAttemptedWorlds.Add(World);

    UMaterialInterface* OutlineMaterial = LootOutlinePostProcessMaterial.Get();
    if (!OutlineMaterial && !LootOutlinePostProcessMaterial.IsNull())
    {
        OutlineMaterial = LootOutlinePostProcessMaterial.LoadSynchronous();
    }
    if (!IsValid(OutlineMaterial))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Loot outline post-process material is missing: %s"),
            *LootOutlinePostProcessMaterial.ToString());
        return;
    }

    APostProcessVolume* CandidateUnboundVolume = nullptr;
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        APostProcessVolume* Volume = *It;
        if (!IsValid(Volume))
        {
            continue;
        }

        if (HasBlendableMaterial(Volume->Settings, OutlineMaterial))
        {
            return;
        }

        if (Volume->bUnbound && !CandidateUnboundVolume)
        {
            CandidateUnboundVolume = Volume;
        }
    }

    if (IsValid(CandidateUnboundVolume))
    {
        CandidateUnboundVolume->Settings.AddBlendable(OutlineMaterial, 1.0f);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Added loot outline blendable to existing unbound post-process volume '%s'."),
            *GetNameSafe(CandidateUnboundVolume));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= ResolveRoomSpawnObjectFlags(World);
    APostProcessVolume* SpawnedVolume = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!IsValid(SpawnedVolume))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidRoom] Failed to spawn fallback post-process volume for loot outline."));
        return;
    }

    SpawnedVolume->bUnbound = true;
    SpawnedVolume->Priority = 1000.0f;
    SpawnedVolume->BlendWeight = 1.0f;
    SpawnedVolume->Settings.AddBlendable(OutlineMaterial, 1.0f);
    SpawnedVolume->SetActorHiddenInGame(true);
    SpawnedVolume->SetCanBeDamaged(false);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidRoom] Spawned fallback unbound post-process volume for loot outline: '%s'."),
        *GetNameSafe(SpawnedVolume));
}

UParticleSystem* ARaidRoomActor::ResolveLootProximityFxTemplate()
{
    const FSoftObjectPath RequestedPath = LootProximityFxTemplate.ToSoftObjectPath();
    if (bLootProximityFxTemplateResolveAttempted && RequestedPath == CachedLootProximityFxTemplatePath)
    {
        return CachedLootProximityFxTemplate;
    }

    bLootProximityFxTemplateResolveAttempted = true;
    CachedLootProximityFxTemplatePath = RequestedPath;
    CachedLootProximityFxTemplate = LootProximityFxTemplate.Get();

    if (!CachedLootProximityFxTemplate && !LootProximityFxTemplate.IsNull())
    {
        CachedLootProximityFxTemplate = LootProximityFxTemplate.LoadSynchronous();
    }

    if (!CachedLootProximityFxTemplate)
    {
        static const FSoftObjectPath FallbackPath(TEXT("/Game/TemplesOfCambodia/Demo/EpicContent/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust"));
        CachedLootProximityFxTemplate = Cast<UParticleSystem>(FallbackPath.TryLoad());
        if (CachedLootProximityFxTemplate)
        {
            LootProximityFxTemplate = TSoftObjectPtr<UParticleSystem>(FallbackPath);
            CachedLootProximityFxTemplatePath = FallbackPath;
            bLootProximityFxTemplateResolveAttempted = true;
            if (!bLootProximityFxTemplateResolveLogged)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] LootProximityFxTemplate invalid. Falling back to %s"),
                    *FallbackPath.ToString());
                bLootProximityFxTemplateResolveLogged = true;
            }
        }
    }

    return CachedLootProximityFxTemplate;
}

UClass* ARaidRoomActor::ResolveLootDotWidgetClass()
{
    const FSoftObjectPath RequestedPath = LootDotWidgetClass.ToSoftObjectPath();
    if (bLootDotWidgetResolveAttempted && RequestedPath == CachedLootDotWidgetClassPath)
    {
        return CachedLootDotWidgetClass.Get();
    }

    bLootDotWidgetResolveAttempted = true;
    CachedLootDotWidgetClassPath = RequestedPath;
    CachedLootDotWidgetClass = LootDotWidgetClass.Get();

    if (!CachedLootDotWidgetClass && !LootDotWidgetClass.IsNull())
    {
        CachedLootDotWidgetClass = LootDotWidgetClass.LoadSynchronous();
    }

    if (!CachedLootDotWidgetClass)
    {
        static const FSoftObjectPath FallbackPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_ItemDot.WBP_ItemDot_C"));
        CachedLootDotWidgetClass = TSubclassOf<UUserWidget>(LoadClass<UUserWidget>(nullptr, *FallbackPath.ToString()));
        if (CachedLootDotWidgetClass)
        {
            LootDotWidgetClass = TSoftClassPtr<UUserWidget>(FallbackPath);
            CachedLootDotWidgetClassPath = FallbackPath;
            bLootDotWidgetResolveAttempted = true;
            if (!bLootDotWidgetResolveLogged)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] LootDotWidgetClass invalid. Falling back to %s"),
                    *FallbackPath.ToString());
                bLootDotWidgetResolveLogged = true;
            }
        }
    }

    return CachedLootDotWidgetClass.Get();
}

bool ARaidRoomActor::ShouldSuppressLootProximityIndicators(const AActor* LootActor) const
{
    if (!IsValid(LootActor))
    {
        return true;
    }

    if (LootActor->IsHidden() || LootActor->IsActorBeingDestroyed())
    {
        return true;
    }

    const AActor* AttachParentActor = LootActor->GetAttachParentActor();
    if (IsValid(AttachParentActor))
    {
        if (AttachParentActor->IsA<APawn>() || AttachParentActor->IsA<ACharacter>())
        {
            return true;
        }
    }

    return false;
}

void ARaidRoomActor::SetLootActorDotWidget(AActor* LootActor, bool bEnable)
{
    if (!IsValid(LootActor))
    {
        return;
    }

    TWeakObjectPtr<AActor> LootKey(LootActor);
    TWeakObjectPtr<UWidgetComponent>* FoundComponentPtr = LootDotWidgetComponents.Find(LootKey);
    UWidgetComponent* DotComponent = FoundComponentPtr ? FoundComponentPtr->Get() : nullptr;

    if (!bEnable)
    {
        if (IsValid(DotComponent))
        {
            DotComponent->SetVisibility(false, true);
        }
        return;
    }

    UClass* DotWidgetClass = ResolveLootDotWidgetClass();
    if (!IsValid(DotWidgetClass))
    {
        return;
    }

    if (!IsValid(DotComponent))
    {
        DotComponent = NewObject<UWidgetComponent>(LootActor);
        if (!IsValid(DotComponent))
        {
            return;
        }

        DotComponent->CreationMethod = EComponentCreationMethod::Instance;
        DotComponent->SetWidgetSpace(EWidgetSpace::Screen);
        DotComponent->SetDrawAtDesiredSize(true);
        DotComponent->SetCanEverAffectNavigation(false);
        DotComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DotComponent->SetGenerateOverlapEvents(false);
        DotComponent->SetVisibility(false, true);
        DotComponent->SetWidgetClass(DotWidgetClass);

        LootActor->AddInstanceComponent(DotComponent);
        DotComponent->RegisterComponent();
        LootDotWidgetComponents.Add(LootKey, DotComponent);
    }

    if (DotComponent->GetWidgetClass() != DotWidgetClass)
    {
        DotComponent->SetWidgetClass(DotWidgetClass);
    }

    const FVector DotWorldLocation = LootActor->GetActorLocation() + FVector(0.0f, 0.0f, LootDotHeightOffset);
    DotComponent->SetWorldLocation(DotWorldLocation);
    DotComponent->SetVisibility(true, true);
}

void ARaidRoomActor::SetLootActorProximityFx(AActor* LootActor, bool bEnable)
{
    if (!IsValid(LootActor))
    {
        return;
    }

    TWeakObjectPtr<AActor> LootKey(LootActor);
    TWeakObjectPtr<UParticleSystemComponent>* FoundComponentPtr = LootProximityFxComponents.Find(LootKey);
    UParticleSystemComponent* FxComponent = FoundComponentPtr ? FoundComponentPtr->Get() : nullptr;

    if (!bEnable)
    {
        if (IsValid(FxComponent))
        {
            FxComponent->DeactivateSystem();
            FxComponent->SetVisibility(false, true);
        }
        return;
    }

    UParticleSystem* FxTemplate = ResolveLootProximityFxTemplate();
    if (!FxTemplate)
    {
        return;
    }

    if (!IsValid(FxComponent))
    {
        FxComponent = NewObject<UParticleSystemComponent>(LootActor);
        if (!IsValid(FxComponent))
        {
            return;
        }

        FxComponent->CreationMethod = EComponentCreationMethod::Instance;
        FxComponent->SetTemplate(FxTemplate);
        FxComponent->bAutoActivate = false;
        FxComponent->bAutoDestroy = false;
        FxComponent->SetCanEverAffectNavigation(false);
        FxComponent->SetAbsolute(false, false, false);
        FxComponent->SetHiddenInGame(false, true);
        FxComponent->SetRelativeLocation(LootProximityFxOffset);
        FxComponent->SetRelativeScale3D(FVector(LootProximityFxScale));

        if (USceneComponent* RootComp = LootActor->GetRootComponent())
        {
            FxComponent->SetupAttachment(RootComp);
        }
        else
        {
            FxComponent->SetWorldLocation(LootActor->GetActorLocation() + LootProximityFxOffset);
        }

        LootActor->AddInstanceComponent(FxComponent);
        FxComponent->RegisterComponent();
        LootProximityFxComponents.Add(LootKey, FxComponent);
    }

    FxComponent->SetTemplate(FxTemplate);
    if (!FxComponent->IsVisible())
    {
        FxComponent->SetVisibility(true, true);
    }

    if (USceneComponent* RootComp = LootActor->GetRootComponent())
    {
        if (FxComponent->GetAttachParent() != RootComp)
        {
            FxComponent->AttachToComponent(RootComp, FAttachmentTransformRules::KeepRelativeTransform);
        }
        FxComponent->SetRelativeLocation(LootProximityFxOffset);
    }
    else
    {
        FxComponent->SetWorldLocation(LootActor->GetActorLocation() + LootProximityFxOffset);
    }
    FxComponent->SetRelativeLocation(LootProximityFxOffset);
    FxComponent->SetRelativeScale3D(FVector(LootProximityFxScale));
    if (!FxComponent->IsActive())
    {
        FxComponent->ActivateSystem(true);
    }
}

void ARaidRoomActor::ClearLootProximityFxCache()
{
    UWorld* World = GetWorld();
    const bool bSafeDestroy = !GExitPurge && World && !World->bIsTearingDown;

    for (TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UParticleSystemComponent>>& Pair : LootProximityFxComponents)
    {
        if (UParticleSystemComponent* FxComponent = Pair.Value.Get())
        {
            FxComponent->DeactivateSystem();
            FxComponent->SetVisibility(false, true);
            if (FxComponent->IsRegistered())
            {
                FxComponent->UnregisterComponent();
            }
            if (bSafeDestroy)
            {
                FxComponent->DestroyComponent();
            }
        }
    }
    LootProximityFxComponents.Reset();
}

void ARaidRoomActor::ClearLootDotWidgetCache()
{
    UWorld* World = GetWorld();
    const bool bSafeDestroy = !GExitPurge && World && !World->bIsTearingDown;

    for (TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UWidgetComponent>>& Pair : LootDotWidgetComponents)
    {
        if (UWidgetComponent* DotComponent = Pair.Value.Get())
        {
            DotComponent->SetVisibility(false, true);
            if (DotComponent->IsRegistered())
            {
                DotComponent->UnregisterComponent();
            }
            if (bSafeDestroy)
            {
                DotComponent->DestroyComponent();
            }
        }
    }
    LootDotWidgetComponents.Reset();
}

void ARaidRoomActor::UpdateLootProximityOutline(const APawn* PlayerPawn)
{
    if (SpawnedLootActors.Num() <= 0)
    {
        ClearLootProximityFxCache();
        ClearLootDotWidgetCache();
        return;
    }

    const bool bCanUseOutline =
        bEnableLootProximityOutline &&
        IsValid(PlayerPawn);
    const FVector PlayerLocation = bCanUseOutline ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    const float OutlineDistanceSq = FMath::Square(FMath::Max(50.0f, LootOutlineDistance));
    const bool bCanUseFx =
        bEnableLootProximityFx &&
        IsValid(PlayerPawn);
    const float FxDistanceSq = FMath::Square(FMath::Max(50.0f, LootProximityFxDistance));
    const bool bLootFxTemplateReady = bCanUseFx && ResolveLootProximityFxTemplate();
    const bool bCanUseDot =
        bEnableLootProximityDot &&
        IsValid(PlayerPawn);
    const float DotDistanceSq = FMath::Square(FMath::Max(50.0f, LootDotDistance));
    const bool bLootDotClassReady = bCanUseDot && IsValid(ResolveLootDotWidgetClass());

    int32 WriteIndex = 0;
    for (int32 Index = 0; Index < SpawnedLootActors.Num(); ++Index)
    {
        AActor* LootActor = SpawnedLootActors[Index].Get();
        if (!IsValid(LootActor) || LootActor->IsActorBeingDestroyed())
        {
            continue;
        }

        const bool bSuppressIndicators = ShouldSuppressLootProximityIndicators(LootActor);

        bool bShouldOutline = false;
        if (!bSuppressIndicators && bCanUseOutline)
        {
            bShouldOutline = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= OutlineDistanceSq;
        }

        bool bShouldFx = false;
        if (!bSuppressIndicators && bLootFxTemplateReady)
        {
            bShouldFx = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= FxDistanceSq;
        }

        bool bShouldDot = false;
        if (!bSuppressIndicators && bLootDotClassReady)
        {
            bShouldDot = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= DotDistanceSq;
        }

        SetLootActorProximityFx(LootActor, bShouldFx);
        SetLootActorOutline(LootActor, bShouldOutline);
        SetLootActorDotWidget(LootActor, bShouldDot);
        SpawnedLootActors[WriteIndex++] = LootActor;
    }

    if (WriteIndex < SpawnedLootActors.Num())
    {
        SpawnedLootActors.SetNum(WriteIndex, EAllowShrinking::No);
    }

    TArray<TWeakObjectPtr<AActor>> OrphanKeys;
    for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UParticleSystemComponent>>& Pair : LootProximityFxComponents)
    {
        const AActor* KeyActor = Pair.Key.Get();
        if (!IsValid(KeyActor) || !SpawnedLootActors.Contains(Pair.Key))
        {
            if (UParticleSystemComponent* FxComponent = Pair.Value.Get())
            {
                FxComponent->DeactivateSystem();
                FxComponent->DestroyComponent();
            }
            OrphanKeys.Add(Pair.Key);
        }
    }
    for (const TWeakObjectPtr<AActor>& Key : OrphanKeys)
    {
        LootProximityFxComponents.Remove(Key);
    }

    TArray<TWeakObjectPtr<AActor>> DotOrphanKeys;
    for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UWidgetComponent>>& Pair : LootDotWidgetComponents)
    {
        const AActor* KeyActor = Pair.Key.Get();
        if (!IsValid(KeyActor) || !SpawnedLootActors.Contains(Pair.Key))
        {
            if (UWidgetComponent* DotComponent = Pair.Value.Get())
            {
                DotComponent->SetVisibility(false, true);
                DotComponent->DestroyComponent();
            }
            DotOrphanKeys.Add(Pair.Key);
        }
    }
    for (const TWeakObjectPtr<AActor>& Key : DotOrphanKeys)
    {
        LootDotWidgetComponents.Remove(Key);
    }
}

AActor* ARaidRoomActor::SpawnProceduralDoorBlocker(const FModularMeshKit& ThemeKit, const FVector& LocalLocation, float LocalYaw)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<FMeshVariation> DoorBlockerVariations;
    ThemeKit.GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::DoorBlocker, DoorBlockerVariations);
    const FMeshVariation* DoorVariation = RaidMeshUtils::PickRandomVariation(DoorBlockerVariations, RoomRandomStream);
    const bool bHasConfiguredDoorVariation = DoorVariation && (!DoorVariation->Mesh.IsNull() || !DoorVariation->BlueprintPrefab.IsNull());
    const FVector FallbackDoorScale(1.8f, 0.45f, 2.6f);
    const FVector BaseDoorScale = bHasConfiguredDoorVariation ? FVector::OneVector : FallbackDoorScale;
    const float DoorHalfHeight = 50.0f * BaseDoorScale.Z;
    const float DoorYawOffset = 0.0f;

    FVector WorldDoorLocation = GetActorTransform().TransformPosition(LocalLocation);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidDoorBlockerGroundSnap), false);
    QueryParams.bTraceComplex = false;
    QueryParams.AddIgnoredActor(this);

    FHitResult GroundHit;
    const bool bHitGround = World->LineTraceSingleByChannel(
        GroundHit,
        WorldDoorLocation + FVector(0.0f, 0.0f, 120000.0f),
        WorldDoorLocation + FVector(0.0f, 0.0f, -120000.0f),
        ECC_WorldStatic,
        QueryParams);

    if (bHitGround)
    {
        if (IsWaterHit(GroundHit) || (RoomActor_IsInsideWaterPhysicsVolume(World, GroundHit.ImpactPoint, 80.0f) && !IsLandscapeLikeHit(GroundHit)))
        {
            return nullptr;
        }
        WorldDoorLocation.Z = GroundHit.ImpactPoint.Z + DoorHalfHeight;
    }
    else
    {
        WorldDoorLocation.Z = GetActorLocation().Z + DoorHalfHeight;
    }

    const FTransform WorldDoorTransform(
        FRotator(0.0f, LocalYaw + DoorYawOffset, 0.0f),
        WorldDoorLocation,
        BaseDoorScale);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    Params.ObjectFlags |= ResolveRoomSpawnObjectFlags(World);

    if (bHasConfiguredDoorVariation)
    {
        const FTransform FinalDoorTransform = ResolveVariationTransform(
            *DoorVariation,
            WorldDoorTransform,
            ERaidVariationOffsetChannel::DoorBlocker);

        if (!DoorVariation->BlueprintPrefab.IsNull())
        {
            if (UClass* DoorClass = DoorVariation->BlueprintPrefab.LoadSynchronous())
            {
                if (AActor* DoorActor = World->SpawnActor<AActor>(DoorClass, FinalDoorTransform, Params))
                {
                    TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
                    DoorActor->GetComponents(PrimitiveComps);
                    for (UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
                    {
                        if (!IsValid(PrimitiveComp)) continue;
                        PrimitiveComp->SetCollisionProfileName(TEXT("BlockAll"));
                    }

                    DoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
                    DoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
                    DoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                    DoorActor->SetOwner(this);
                    DoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                    return DoorActor;
                }
            }
        }
        else
        {
            if (UStaticMesh* DoorMesh = DoorVariation->Mesh.LoadSynchronous())
            {
                if (AStaticMeshActor* DoorActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FinalDoorTransform, Params))
                {
                    if (UStaticMeshComponent* MeshComp = DoorActor->GetStaticMeshComponent())
                    {
                        MeshComp->SetStaticMesh(DoorMesh);
                        MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
                        MeshComp->SetMobility(EComponentMobility::Static);
                    }
                    DoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
                    DoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
                    DoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                    DoorActor->SetOwner(this);
                    DoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                    return DoorActor;
                }
            }
        }
    }

    // No configured door blocker mesh/blueprint: keep gameplay blockage with invisible collision-only fallback.
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (AStaticMeshActor* FallbackDoorActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldDoorTransform, Params))
    {
        if (UStaticMeshComponent* MeshComp = FallbackDoorActor->GetStaticMeshComponent())
        {
            MeshComp->SetStaticMesh(CubeMesh);
            MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
            MeshComp->SetHiddenInGame(true);
            MeshComp->SetVisibility(false, true);
            MeshComp->SetCastShadow(false);
        }
        FallbackDoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
        FallbackDoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
        FallbackDoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
        FallbackDoorActor->SetOwner(this);
        FallbackDoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
        return FallbackDoorActor;
    }

    return nullptr;
}

AActor* ARaidRoomActor::AddMeshInstance(const FMeshVariation& Variation, const FTransform& BaseTransform, int32 MeshType, UMaterialInterface* MaterialOverride)
{
    if (Variation.Mesh.IsNull() && Variation.BlueprintPrefab.IsNull()) return nullptr;
    const ERaidVariationOffsetChannel OffsetChannel = ResolveOffsetChannelForMeshType(MeshType);
    FTransform FinalTransform = ResolveVariationTransform(Variation, BaseTransform, OffsetChannel);
    FTransform WorldTransform = FinalTransform * GetActorTransform();
    UStaticMesh* PreloadedVariationMesh = nullptr;

    const bool bOutdoorStyle = IsOutdoorStyleRoom(NodeRow);
    const bool bTerrainConformType = (MeshType == 0 || MeshType == 2 || MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8);
    const bool bShouldTryGroundSnap = bTerrainConformType;
    const bool bShouldAlignToSlope = bOutdoorStyle && (MeshType == 6 || MeshType == 7 || MeshType == 8);
    const bool bVariationBlueprint = !Variation.BlueprintPrefab.IsNull();
    const float ResolvedVariationDeltaLocalZ = FinalTransform.GetLocation().Z - BaseTransform.GetLocation().Z;
    const float AuthoredVariationOffsetLocalZ = Variation.Offset.GetLocation().Z;
    const float VariationDeltaLocalZ =
        !FMath::IsNearlyZero(AuthoredVariationOffsetLocalZ, KINDA_SMALL_NUMBER)
        ? AuthoredVariationOffsetLocalZ
        : ResolvedVariationDeltaLocalZ;
#if !UE_BUILD_SHIPPING
    if (MeshType == 2 && FMath::Abs(AuthoredVariationOffsetLocalZ) >= 1.0f)
    {
        static int32 GObstacleOffsetDebugLogCount = 0;
        if (GObstacleOffsetDebugLogCount < 24)
        {
            ++GObstacleOffsetDebugLogCount;
            const FString AssetPath = !Variation.Mesh.IsNull()
                ? Variation.Mesh.ToString()
                : Variation.BlueprintPrefab.ToString();
            UE_LOG(
                LogTemp,
                Display,
                TEXT("[RaidRoom][OffsetDebug] MeshType=%d Asset=%s AuthorOffsetZ=%.2f ResolvedDeltaZ=%.2f AppliedDeltaZ=%.2f"),
                MeshType,
                *AssetPath,
                AuthoredVariationOffsetLocalZ,
                ResolvedVariationDeltaLocalZ,
                VariationDeltaLocalZ);
        }
    }
#endif
    bool bHasGroundHitForSnap = false;
    FHitResult CachedGroundHit;
    bool bGroundHitOnLandscape = false;
#if WITH_EDITOR
    bool bDeferBlueprintFlattenToPostSpawn = false;
#endif
    bool bBlueprintFlattenAppliedInSpawnPass = false;
    float ObstacleMinSpacingForFootprint = 0.0f;
    FBox2D CandidateObstacleFootprint(EForceInit::ForceInit);
    bool bHasCandidateObstacleFootprint = false;

    if (UWorld* World = GetWorld())
    {
        // Do not early-reject by physics volume only.
        // Ground-hit filtering below rejects real water surfaces while allowing shoreline terrain.

        if (bShouldTryGroundSnap)
        {
            FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidRoomObjectGroundSnap), false);
            QueryParams.bTraceComplex = false;
            QueryParams.AddIgnoredActor(this);
            for (const TObjectPtr<AActor>& SpawnedActor : SpawnedDynamicActors)
            {
                if (IsValid(SpawnedActor))
                {
                    QueryParams.AddIgnoredActor(SpawnedActor);
                }
            }
            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
            {
                if (IsValid(DoorActor))
                {
                    QueryParams.AddIgnoredActor(DoorActor);
                }
            }

            const FVector QueryLocation = WorldTransform.GetLocation();
            if (!PreloadedVariationMesh && !Variation.Mesh.IsNull())
            {
                PreloadedVariationMesh = Variation.Mesh.LoadSynchronous();
            }

            int32 GroundSupportSampleCount = 1;
            float GroundSupportRadius = 0.0f;
            if (bOutdoorStyle)
            {
                if (MeshType == 2 || bVariationBlueprint)
                {
                    GroundSupportSampleCount = 5;
                }
                else if (MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8)
                {
                    GroundSupportSampleCount = 5;
                }

                if (PreloadedVariationMesh)
                {
                    const FVector BoundsExtent = PreloadedVariationMesh->GetBoundingBox().GetExtent();
                    const FVector Scale3D = WorldTransform.GetScale3D().GetAbs();
                    const float ExtentXY = FMath::Max(BoundsExtent.X * Scale3D.X, BoundsExtent.Y * Scale3D.Y);
                    if (ExtentXY > 1.0f)
                    {
                        GroundSupportRadius = FMath::Clamp(ExtentXY * 0.28f, 70.0f, 360.0f);
                        if ((MeshType == 2 || bVariationBlueprint) && ExtentXY > 650.0f)
                        {
                            GroundSupportSampleCount = 9;
                        }
                    }
                }

                if (GroundSupportSampleCount > 1 && GroundSupportRadius <= 1.0f)
                {
                    GroundSupportRadius = bVariationBlueprint ? 190.0f : 120.0f;
                }
            }

            FHitResult CenterGroundHit;
            if (!TryResolveRoomSingleGroundHitAtPoint(World, QueryLocation, true, QueryParams, CenterGroundHit))
            {
                return nullptr;
            }

            bool bSelectedLandscape = IsLandscapeLikeHit(CenterGroundHit);
            if (!bSelectedLandscape && bVariationBlueprint)
            {
                // Blueprint prefabs are often large and can line-trace against preplaced geometry first.
                // Retry once while ignoring the first blocking actor/component to recover landscape support.
                FCollisionQueryParams RetryLandscapeQueryParams(QueryParams);
                if (const AActor* FirstHitActor = CenterGroundHit.GetActor())
                {
                    RetryLandscapeQueryParams.AddIgnoredActor(FirstHitActor);
                }
                if (UPrimitiveComponent* FirstHitComp = CenterGroundHit.GetComponent())
                {
                    RetryLandscapeQueryParams.AddIgnoredComponent(FirstHitComp);
                }

                FHitResult RetryLandscapeHit;
                if (TryResolveRoomSingleGroundHitAtPoint(World, QueryLocation, true, RetryLandscapeQueryParams, RetryLandscapeHit) &&
                    IsLandscapeLikeHit(RetryLandscapeHit))
                {
                    CenterGroundHit = RetryLandscapeHit;
                    bSelectedLandscape = true;
                }
            }
            bGroundHitOnLandscape = bSelectedLandscape;
            if (!bOutdoorStyle && !bSelectedLandscape && !bVariationBlueprint)
            {
                // Indoor-style room metadata일 때는 임의의 static mesh를 지면으로 오인하지 않도록 스킵.
                // 단, 실제 Landscape를 찾은 경우에는 메타데이터와 무관하게 스냅한다.
            }
            else
            {
                TArray<FVector2D> GroundSupportOffsets;
                BuildGroundSupportOffsets(GroundSupportSampleCount, GroundSupportRadius, GroundSupportOffsets);

                auto CollectSupportData =
                    [&](FHitResult& InOutCenterHit, TArray<float>& OutSupportHeights, FVector& OutSupportNormalAccum, int32& OutSupportNormalCount, float& OutSupportGroundZ, float& OutSupportHeightRange) -> void
                    {
                        OutSupportHeights.Reset();
                        OutSupportHeights.Reserve(GroundSupportOffsets.Num());
                        OutSupportHeights.Add(InOutCenterHit.ImpactPoint.Z);

                        OutSupportNormalAccum = InOutCenterHit.ImpactNormal;
                        OutSupportNormalCount = InOutCenterHit.ImpactNormal.IsNearlyZero() ? 0 : 1;

                        float LocalMinHeight = InOutCenterHit.ImpactPoint.Z;
                        float LocalMaxHeight = InOutCenterHit.ImpactPoint.Z;

                        for (int32 OffsetIndex = 1; OffsetIndex < GroundSupportOffsets.Num(); ++OffsetIndex)
                        {
                            const FVector2D& Offset = GroundSupportOffsets[OffsetIndex];
                            const FVector SampleLocation = QueryLocation + FVector(Offset.X, Offset.Y, 0.0f);
                            FHitResult SupportHit;
                            if (!TryResolveRoomSingleGroundHitAtPoint(World, SampleLocation, true, QueryParams, SupportHit))
                            {
                                continue;
                            }

                            const float HeightZ = SupportHit.ImpactPoint.Z;
                            OutSupportHeights.Add(HeightZ);
                            LocalMinHeight = FMath::Min(LocalMinHeight, HeightZ);
                            LocalMaxHeight = FMath::Max(LocalMaxHeight, HeightZ);
                            if (!SupportHit.ImpactNormal.IsNearlyZero())
                            {
                                OutSupportNormalAccum += SupportHit.ImpactNormal;
                                ++OutSupportNormalCount;
                            }
                        }

                        OutSupportGroundZ = InOutCenterHit.ImpactPoint.Z;
                        if (OutSupportHeights.Num() > 1)
                        {
                            OutSupportHeights.Sort();
                            OutSupportGroundZ = OutSupportHeights[OutSupportHeights.Num() / 2];
                        }
                        OutSupportHeightRange = LocalMaxHeight - LocalMinHeight;
                    };

                TArray<float> SupportHeights;
                FVector SupportNormalAccum = FVector::UpVector;
                int32 SupportNormalCount = 0;
                float SupportGroundZ = CenterGroundHit.ImpactPoint.Z;
                float SupportHeightRange = 0.0f;
                CollectSupportData(
                    CenterGroundHit,
                    SupportHeights,
                    SupportNormalAccum,
                    SupportNormalCount,
                    SupportGroundZ,
                    SupportHeightRange);

                const int32 SupportSampleTotal = FMath::Max(1, GroundSupportOffsets.Num());
                const float SupportHitRatio = static_cast<float>(SupportHeights.Num()) / static_cast<float>(SupportSampleTotal);
                const FVector EffectiveSupportNormal = (SupportNormalCount > 0)
                    ? SupportNormalAccum.GetSafeNormal()
                    : CenterGroundHit.ImpactNormal.GetSafeNormal();
                const float SupportNormalZ = FMath::Clamp(EffectiveSupportNormal.Z, -1.0f, 1.0f);
                const float SupportSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(SupportNormalZ));

                // Foliage/tree-like placements become visually broken on steep or narrow edge zones.
                // Reject those candidates early so only plausible terrain gets populated.
                const bool bFoliageLikeGroundCandidate = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                if (bFoliageLikeGroundCandidate)
                {
                    constexpr float MaxAllowedSlopeDeg = 52.0f;
                    constexpr float MaxAllowedHeightRange = 170.0f;
                    constexpr float MinAllowedSupportHitRatio = 0.66f;
                    if (SupportSlopeDeg > MaxAllowedSlopeDeg ||
                        SupportHeightRange > MaxAllowedHeightRange ||
                        SupportHitRatio < MinAllowedSupportHitRatio)
                    {
                        return nullptr;
                    }
                }

#if WITH_EDITOR
                if (bEnableEditorLandscapeFlattenForMarkedVariations &&
                    !World->IsGameWorld() &&
                    Variation.bFlattenLandscapeUnderSpawn &&
                    bSelectedLandscape &&
                    (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
                {
                    if (bVariationBlueprint && Variation.FlattenRadius <= KINDA_SMALL_NUMBER)
                    {
                        bDeferBlueprintFlattenToPostSpawn = true;
                        if (bLogEditorLandscapeFlatten)
                        {
                            UE_LOG(
                                LogTemp,
                                Warning,
                                TEXT("[RaidRoom][TerrainFlatten][Defer] Node=%d MeshType=%d Reason=BlueprintAutoRadius"),
                                NodeId,
                                MeshType);
                        }
                    }
                    else
                    {
                    const float CenterNormalZ = FMath::Clamp(CenterGroundHit.ImpactNormal.GetSafeNormal().Z, -1.0f, 1.0f);
                    const float CenterSlopeDeg = FMath::RadiansToDegrees(FMath::Acos(CenterNormalZ));
                    float FlattenRadius = Variation.FlattenRadius;
                    if (FlattenRadius <= KINDA_SMALL_NUMBER)
                    {
                        FlattenRadius = ResolveAutoFlattenRadiusFromMeshBounds(
                            PreloadedVariationMesh,
                            WorldTransform.GetScale3D().GetAbs());
                    }
                    if (PreloadedVariationMesh)
                    {
                        const FVector ScaleAbs = WorldTransform.GetScale3D().GetAbs();
                        const FVector MeshExtent = PreloadedVariationMesh->GetBoundingBox().GetExtent();
                        const FVector2D MeshHalf2D(MeshExtent.X * ScaleAbs.X, MeshExtent.Y * ScaleAbs.Y);
                        const float MeshCoverageRadius = MeshHalf2D.Size() * EditorLandscapeFlattenFootprintCoverageScale;
                        FlattenRadius = FMath::Max(FlattenRadius, MeshCoverageRadius);
                    }
                    FlattenRadius += FMath::Max(0.0f, EditorLandscapeFlattenExtraMargin);
                    FlattenRadius = FMath::Clamp(FlattenRadius, 120.0f, 10000.0f);
                    const float MinFalloffRatio = FMath::Clamp(EditorLandscapeFlattenMinFalloffRatio, 0.0f, 1.0f);
                    float FlattenFalloff = FMath::Clamp(Variation.FlattenSmoothFalloff, 0.0f, 6000.0f);
                    FlattenFalloff = FMath::Max(FlattenFalloff, FlattenRadius * MinFalloffRatio);
                    FlattenFalloff = FMath::Clamp(FlattenFalloff, 0.0f, 6000.0f);
                    const float TargetFlattenZ = SupportGroundZ + Variation.FlattenHeightOffset;
                    const float FlattenTotalRadius = FlattenRadius + FlattenFalloff;
                    FBox2D CandidateFlattenFootprint(
                        FVector2D(CenterGroundHit.ImpactPoint.X - FlattenTotalRadius, CenterGroundHit.ImpactPoint.Y - FlattenTotalRadius),
                        FVector2D(CenterGroundHit.ImpactPoint.X + FlattenTotalRadius, CenterGroundHit.ImpactPoint.Y + FlattenTotalRadius));
                    const bool bHasCandidateFlattenFootprint = CandidateFlattenFootprint.bIsValid;
                    const float FlattenOverlapPadding = FMath::Clamp(EditorLandscapeFlattenOverlapPadding, 0.0f, 1200.0f);
                    const bool bFlattenOverlapBlocked =
                        bAvoidOverlappingEditorLandscapeFlatten &&
                        bHasCandidateFlattenFootprint &&
                        RoomActor_IsFootprintOverlappingAny(
                            AppliedTerrainFlattenFootprints,
                            CandidateFlattenFootprint,
                            FlattenOverlapPadding);

                    if (bFlattenOverlapBlocked)
                    {
                        if (bLogEditorLandscapeFlatten)
                        {
                            UE_LOG(
                                LogTemp,
                                Warning,
                                TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=OverlapConflict Radius=%.1f Falloff=%.1f Existing=%d Padding=%.1f"),
                                NodeId,
                                MeshType,
                                FlattenRadius,
                                FlattenFalloff,
                                AppliedTerrainFlattenFootprints.Num(),
                                FlattenOverlapPadding);
                        }
                    }
                    else
                    {
                        const int32 AppliedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                            World,
                            this,
                            FVector(CenterGroundHit.ImpactPoint.X, CenterGroundHit.ImpactPoint.Y, TargetFlattenZ),
                            WorldTransform.GetRotation().Rotator().Yaw,
                            FlattenRadius,
                            FlattenFalloff,
                            TargetFlattenZ,
                            Variation.bFlattenRaiseHeights,
                            Variation.bFlattenLowerHeights,
                            EditorLandscapeFlattenEdgeCliffRatio,
                            EditorLandscapeFlattenEdgeErosionRatio,
                            EditorLandscapeFlattenEdgePatchSize,
                            EditorLandscapeFlattenEdgeErosionStrength,
                            EditorLandscapeFlattenEdgeSmoothStrength);

                        if (AppliedLandscapeCount > 0)
                        {
                            ++EditorLandscapeFlattenOpsApplied;
                            if (bHasCandidateFlattenFootprint)
                            {
                                AppliedTerrainFlattenFootprints.Add(CandidateFlattenFootprint);
                            }

                            if (bLogEditorLandscapeFlatten)
                            {
                                UE_LOG(
                                    LogTemp,
                                    Warning,
                                    TEXT("[RaidRoom][TerrainFlatten] Node=%d MeshType=%d Radius=%.1f Falloff=%.1f HeightOffset=%.1f Slope=%.1f HeightRange=%.1f Landscapes=%d"),
                                    NodeId,
                                    MeshType,
                                    FlattenRadius,
                                    FlattenFalloff,
                                    Variation.FlattenHeightOffset,
                                    CenterSlopeDeg,
                                    SupportHeightRange,
                                    AppliedLandscapeCount);
                            }

                            FHitResult RefreshedCenterGroundHit;
                            if (TryResolveRoomSingleGroundHitAtPoint(World, QueryLocation, true, QueryParams, RefreshedCenterGroundHit))
                            {
                                CenterGroundHit = RefreshedCenterGroundHit;
                                CollectSupportData(
                                    CenterGroundHit,
                                    SupportHeights,
                                    SupportNormalAccum,
                                    SupportNormalCount,
                                    SupportGroundZ,
                                    SupportHeightRange);
                            }
                        }
                        else if (bLogEditorLandscapeFlatten)
                        {
                            UE_LOG(
                                LogTemp,
                                Warning,
                                TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=NoLandscapeApplied Radius=%.1f Falloff=%.1f"),
                                NodeId,
                            MeshType,
                            FlattenRadius,
                            FlattenFalloff);
                    }
                    }
                }
                }
                else if (bEnableEditorLandscapeFlattenForMarkedVariations &&
                         Variation.bFlattenLandscapeUnderSpawn &&
                         bLogEditorLandscapeFlatten)
                {
                    if (!bSelectedLandscape)
                    {
                        UE_LOG(
                            LogTemp,
                            Warning,
                            TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=NotLandscapeHit HitActor=%s"),
                            NodeId,
                            MeshType,
                            *GetNameSafe(CenterGroundHit.GetActor()));
                    }
                    else if (EditorLandscapeFlattenMaxOpsPerRoom > 0 &&
                             EditorLandscapeFlattenOpsApplied >= EditorLandscapeFlattenMaxOpsPerRoom)
                    {
                        UE_LOG(
                            LogTemp,
                            Warning,
                            TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=BudgetReached Applied=%d Max=%d"),
                            NodeId,
                            MeshType,
                            EditorLandscapeFlattenOpsApplied,
                            EditorLandscapeFlattenMaxOpsPerRoom);
                    }
                }
                else if (bEnableEditorLandscapeFlattenForMarkedVariations &&
                         bLogEditorLandscapeFlatten &&
                         bVariationBlueprint &&
                         !Variation.bFlattenLandscapeUnderSpawn)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=VariationFlattenDisabled Asset=%s"),
                        NodeId,
                        MeshType,
                        *Variation.BlueprintPrefab.ToString());
                }
#endif

                bHasGroundHitForSnap = true;
                CachedGroundHit = CenterGroundHit;
                CachedGroundHit.ImpactPoint.Z = SupportGroundZ;
                CachedGroundHit.Location.Z = SupportGroundZ;
                if (SupportNormalCount > 0)
                {
                    CachedGroundHit.ImpactNormal = SupportNormalAccum.GetSafeNormal();
                }

                FVector SnappedLocation = WorldTransform.GetLocation();
                SnappedLocation.X = CenterGroundHit.ImpactPoint.X;
                SnappedLocation.Y = CenterGroundHit.ImpactPoint.Y;
                WorldTransform.SetLocation(SnappedLocation);

                if (bShouldAlignToSlope)
                {
                    const float PreservedYaw = WorldTransform.GetRotation().Rotator().Yaw;
                    const FVector SlopeNormal = CachedGroundHit.ImpactNormal.IsNearlyZero()
                        ? CenterGroundHit.ImpactNormal
                        : CachedGroundHit.ImpactNormal;
                    FRotator SlopeRot = FRotationMatrix::MakeFromZ(SlopeNormal).Rotator();
                    SlopeRot.Yaw = PreservedYaw;
                    WorldTransform.SetRotation(SlopeRot.Quaternion());
                }

                const bool bFoliageLikeMeshType = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                // 1) Ground snap should only decide "attach to ground".
                // 2) Authored variation/index Z offset should be applied AFTER snap.
                // This separation prevents authored offsets from being canceled by snap baseline logic.
                const float BaseLocalZContribution = bFoliageLikeMeshType
                    ? FMath::Clamp(BaseTransform.GetLocation().Z, -4000.0f, 4000.0f)
                    : 0.0f;
                const float ClampedVariationDeltaLocalZ = FMath::Clamp(VariationDeltaLocalZ, -20000.0f, 20000.0f);
                const float EffectiveVariationDeltaLocalZ = bFoliageLikeMeshType
                    ? FMath::Min(0.0f, ClampedVariationDeltaLocalZ)
                    : ClampedVariationDeltaLocalZ;
                const float TargetBottomZ = SupportGroundZ + BaseLocalZContribution + 2.0f;
                bool bAdjustedWithBounds = false;
                if (PreloadedVariationMesh)
                {
                    const FBox LocalBounds = PreloadedVariationMesh->GetBoundingBox();
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
                            const FVector WorldCorner = WorldTransform.TransformPosition(Corner);
                            CurrentMinZ = FMath::Min(CurrentMinZ, WorldCorner.Z);
                        }

                        if (CurrentMinZ < TNumericLimits<float>::Max())
                        {
                            SnappedLocation = WorldTransform.GetLocation();
                            SnappedLocation.Z += (TargetBottomZ - CurrentMinZ);
                            WorldTransform.SetLocation(SnappedLocation);
                            bAdjustedWithBounds = true;
                        }
                    }
                }

                if (!bAdjustedWithBounds)
                {
                    SnappedLocation = WorldTransform.GetLocation();
                    SnappedLocation.Z = TargetBottomZ;
                    WorldTransform.SetLocation(SnappedLocation);
                }

                if (!FMath::IsNearlyZero(EffectiveVariationDeltaLocalZ, 0.1f))
                {
                    SnappedLocation = WorldTransform.GetLocation();
                    SnappedLocation.Z += EffectiveVariationDeltaLocalZ;
                    WorldTransform.SetLocation(SnappedLocation);
                }
            }
        }
    }

    if (MeshType == 2)
    {
        if (UWorld* World = GetWorld())
        {
            const float BaseSpacing = bVariationBlueprint ? BlueprintObstacleMinSpacing : ObstacleMinSpacing;
            const FVector Scale3D = WorldTransform.GetScale3D().GetAbs();
            const float ScaleFactor = FMath::Clamp(FMath::Max(Scale3D.X, Scale3D.Y), 0.6f, 4.0f);
            float MinSpacing = BaseSpacing * ScaleFactor;
            UStaticMesh* CandidateMeshForBounds = nullptr;

            if (!Variation.Mesh.IsNull())
            {
                CandidateMeshForBounds = Variation.Mesh.LoadSynchronous();
                if (CandidateMeshForBounds)
                {
                    const FVector MeshExtent = CandidateMeshForBounds->GetBoundingBox().GetExtent() * Scale3D;
                    const float MeshRequiredSpacing = FMath::Max(MeshExtent.X, MeshExtent.Y) * 1.8f;
                    MinSpacing = FMath::Max(MinSpacing, MeshRequiredSpacing);
                }
            }

            ObstacleMinSpacingForFootprint = MinSpacing;

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                const FVector CandidateWorldLoc = WorldTransform.GetLocation();

                if (CandidateMeshForBounds)
                {
                    bHasCandidateObstacleFootprint = RoomActor_TryBuildFootprintFromStaticMesh(CandidateMeshForBounds, WorldTransform, CandidateObstacleFootprint);
                }
                if (!bHasCandidateObstacleFootprint)
                {
                    const float FallbackExtent = bVariationBlueprint
                        ? FMath::Max(MinSpacing * 0.75f, 300.0f)
                        : FMath::Max(MinSpacing * 0.50f, 120.0f);
                    CandidateObstacleFootprint = FBox2D(
                        FVector2D(CandidateWorldLoc.X - FallbackExtent, CandidateWorldLoc.Y - FallbackExtent),
                        FVector2D(CandidateWorldLoc.X + FallbackExtent, CandidateWorldLoc.Y + FallbackExtent));
                    bHasCandidateObstacleFootprint = CandidateObstacleFootprint.bIsValid;
                }

                const float FootprintPadding = bVariationBlueprint
                    ? FMath::Clamp(MinSpacing * 0.24f, 80.0f, 480.0f)
                    : FMath::Clamp(MinSpacing * 0.18f, 35.0f, 260.0f);
                if (bHasCandidateObstacleFootprint &&
                    RoomActor_IsFootprintOverlappingAny(SpawnedObstacleFootprints, CandidateObstacleFootprint, FootprintPadding))
                {
                    return nullptr;
                }

                if (!bVariationBlueprint)
                {
                    for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
                    {
                        if (!IsValid(ExistingActor) || !ExistingActor->ActorHasTag(TEXT("MeshType_2")))
                        {
                            continue;
                        }

                        float RequiredSpacing = MinSpacing;
                        if (ExistingActor->ActorHasTag(TEXT("ObstacleBlueprint")))
                        {
                            RequiredSpacing = FMath::Max(RequiredSpacing, BlueprintObstacleMinSpacing);
                        }

                        if (FVector::DistSquaredXY(ExistingActor->GetActorLocation(), CandidateWorldLoc) < FMath::Square(RequiredSpacing))
                        {
                            return nullptr;
                        }
                    }

                    FCollisionObjectQueryParams ObjQuery;
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidObstacleFinalPlacementCheck), false);
                    QueryParams.bTraceComplex = false;

                    TArray<FOverlapResult> Overlaps;
                    if (World->OverlapMultiByObjectType(
                        Overlaps,
                        CandidateWorldLoc,
                        FQuat::Identity,
                        ObjQuery,
                        FCollisionShape::MakeSphere(MinSpacing * 0.55f),
                        QueryParams))
                    {
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                            const AActor* HitActor = Overlap.GetActor();
                            if (!IsValid(HitComp))
                            {
                                continue;
                            }

                            const bool bObstacleComponent = HitComp->ComponentTags.Contains(TEXT("MeshType_2"));
                            const bool bObstacleActor =
                                IsValid(HitActor) &&
                                (HitActor->ActorHasTag(TEXT("MeshType_2")) || HitActor->ActorHasTag(TEXT("ObstacleBlueprint")));
                            if (bObstacleComponent || bObstacleActor)
                            {
                                return nullptr;
                            }
                        }
                    }
                }
            }
        }
    }

    if (MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8)
    {
        FBox2D CandidateNonObstacleFootprint(EForceInit::ForceInit);
        bool bHasNonObstacleFootprint = false;

        if (!Variation.Mesh.IsNull())
        {
            if (UStaticMesh* CandidateMesh = Variation.Mesh.LoadSynchronous())
            {
                bHasNonObstacleFootprint = RoomActor_TryBuildFootprintFromStaticMesh(CandidateMesh, WorldTransform, CandidateNonObstacleFootprint);
            }
        }

        if (!bHasNonObstacleFootprint)
        {
            const FVector CandidateWorldLoc = WorldTransform.GetLocation();
            const float FallbackExtent = (MeshType == 6)
                ? 260.0f
                : (MeshType == 8 ? 180.0f : 140.0f);
            CandidateNonObstacleFootprint = FBox2D(
                FVector2D(CandidateWorldLoc.X - FallbackExtent, CandidateWorldLoc.Y - FallbackExtent),
                FVector2D(CandidateWorldLoc.X + FallbackExtent, CandidateWorldLoc.Y + FallbackExtent));
            bHasNonObstacleFootprint = CandidateNonObstacleFootprint.bIsValid;
        }

        if (bHasNonObstacleFootprint)
        {
            const float FootprintPadding = (MeshType == 6)
                ? 120.0f
                : (MeshType == 8 ? 90.0f : 70.0f);
            if (RoomActor_IsFootprintOverlappingAny(SpawnedObstacleFootprints, CandidateNonObstacleFootprint, FootprintPadding))
            {
                return nullptr;
            }
        }
    }

    if (!Variation.BlueprintPrefab.IsNull())
    {
        if (UClass* LoadedClass = Variation.BlueprintPrefab.LoadSynchronous())
        {
            FActorSpawnParameters Params;
            // Spawn transform must stay deterministic; collision auto-adjust can push blueprint actors up
            // and cause persistent "floating prop" regressions in room generation.
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= ResolveRoomSpawnObjectFlags(GetWorld());
            if (AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(LoadedClass, WorldTransform, Params))
            {
                if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
                {
                    bool bContainsPrototypeEngineMesh = false;
                    TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(SpawnedActor);
                    for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
                    {
                        if (IsRuntimePrototypeEngineMesh(World, StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr))
                        {
                            bContainsPrototypeEngineMesh = true;
                            break;
                        }
                    }

                    if (bContainsPrototypeEngineMesh)
                    {
                        SpawnedActor->Destroy();
                        return nullptr;
                    }
                }

                if (bShouldTryGroundSnap && bHasGroundHitForSnap)
                {
                    const bool bFoliageLikeMeshType = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                    const float BaseLocalZContribution = bFoliageLikeMeshType
                        ? FMath::Clamp(BaseTransform.GetLocation().Z, -4000.0f, 4000.0f)
                        : 0.0f;
                    const float ClampedVariationDeltaLocalZ = FMath::Clamp(VariationDeltaLocalZ, -20000.0f, 20000.0f);
                    const float EffectiveVariationDeltaLocalZ = bFoliageLikeMeshType
                        ? FMath::Min(0.0f, ClampedVariationDeltaLocalZ)
                        : ClampedVariationDeltaLocalZ;
                    float SupportGroundZForActor = CachedGroundHit.ImpactPoint.Z;
                    float TargetBottomZ = SupportGroundZForActor + BaseLocalZContribution + 2.0f;

                    float CurrentSupportMinZ = TNumericLimits<float>::Max();
                    if (TryResolveActorLowestSupportZ(SpawnedActor, CurrentSupportMinZ))
                    {
                        const float DeltaToGround = TargetBottomZ - CurrentSupportMinZ;
                        if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                        {
                            SpawnedActor->AddActorWorldOffset(
                                FVector(0.0f, 0.0f, DeltaToGround),
                                false,
                                nullptr,
                                ETeleportType::TeleportPhysics);
                        }
                    }
                    else
                    {
                        FVector FallbackLoc = SpawnedActor->GetActorLocation();
                        FallbackLoc.Z = TargetBottomZ;
                        SpawnedActor->SetActorLocation(FallbackLoc, false, nullptr, ETeleportType::TeleportPhysics);
                    }

                    auto ReSnapBlueprintByFootprintMedian =
                        [&](float& InOutSupportGroundZ) -> void
                        {
                            UWorld* LocalWorld = GetWorld();
                            if (!LocalWorld || !IsValid(SpawnedActor))
                            {
                                return;
                            }

                            FBox2D ActorFootprint(EForceInit::ForceInit);
                            if (!RoomActor_TryBuildFootprintFromActor(SpawnedActor, ActorFootprint) || !ActorFootprint.bIsValid)
                            {
                                return;
                            }

                            const FVector2D Center2D = (ActorFootprint.Min + ActorFootprint.Max) * 0.5f;
                            const FVector2D Half2D = (ActorFootprint.Max - ActorFootprint.Min) * 0.5f;
                            const FVector2D ClampedHalf(
                                FMath::Max(20.0f, Half2D.X),
                                FMath::Max(20.0f, Half2D.Y));

                            TArray<FVector2D> SamplePoints;
                            SamplePoints.Reserve(9);
                            SamplePoints.Add(Center2D);
                            SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, 0.0f));
                            SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, 0.0f));
                            SamplePoints.Add(Center2D + FVector2D(0.0f, ClampedHalf.Y));
                            SamplePoints.Add(Center2D + FVector2D(0.0f, -ClampedHalf.Y));
                            SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, ClampedHalf.Y));
                            SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, ClampedHalf.Y));
                            SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, -ClampedHalf.Y));
                            SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, -ClampedHalf.Y));

                            FCollisionQueryParams BlueprintGroundQueryParams(SCENE_QUERY_STAT(RaidRoomBlueprintFootprintGroundSnap), false);
                            BlueprintGroundQueryParams.bTraceComplex = false;
                            BlueprintGroundQueryParams.AddIgnoredActor(this);
                            BlueprintGroundQueryParams.AddIgnoredActor(SpawnedActor);
                            for (const TObjectPtr<AActor>& ExistingSpawnedActor : SpawnedDynamicActors)
                            {
                                if (IsValid(ExistingSpawnedActor))
                                {
                                    BlueprintGroundQueryParams.AddIgnoredActor(ExistingSpawnedActor);
                                }
                            }
                            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
                            {
                                if (IsValid(DoorActor))
                                {
                                    BlueprintGroundQueryParams.AddIgnoredActor(DoorActor);
                                }
                            }

                            TArray<float> SupportHeights;
                            SupportHeights.Reserve(SamplePoints.Num());
                            for (const FVector2D& SamplePoint : SamplePoints)
                            {
                                const FVector QueryPoint(SamplePoint.X, SamplePoint.Y, SpawnedActor->GetActorLocation().Z);
                                FHitResult GroundHit;
                                if (TryResolveRoomSingleGroundHitAtPoint(LocalWorld, QueryPoint, true, BlueprintGroundQueryParams, GroundHit))
                                {
                                    SupportHeights.Add(GroundHit.ImpactPoint.Z);
                                }
                            }

                            if (SupportHeights.Num() <= 0)
                            {
                                return;
                            }

                            SupportHeights.Sort();
                            InOutSupportGroundZ = SupportHeights[SupportHeights.Num() / 2];
                            TargetBottomZ = InOutSupportGroundZ + BaseLocalZContribution + 2.0f;

                            float RecalcSupportMinZ = TNumericLimits<float>::Max();
                            if (TryResolveActorLowestSupportZ(SpawnedActor, RecalcSupportMinZ))
                            {
                                const float DeltaToGround = TargetBottomZ - RecalcSupportMinZ;
                                if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                                {
                                    SpawnedActor->AddActorWorldOffset(
                                        FVector(0.0f, 0.0f, DeltaToGround),
                                        false,
                                        nullptr,
                                        ETeleportType::TeleportPhysics);
                                }
                            }
                        };

#if WITH_EDITOR
                    UWorld* FlattenWorld = GetWorld();
                    if (FlattenWorld &&
                        !FlattenWorld->IsGameWorld() &&
                        bEnableEditorLandscapeFlattenForMarkedVariations &&
                        Variation.bFlattenLandscapeUnderSpawn &&
                        bGroundHitOnLandscape &&
                        (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
                    {
                        const bool bRunPostSpawnFlatten = bDeferBlueprintFlattenToPostSpawn || Variation.FlattenRadius > KINDA_SMALL_NUMBER;
                        if (bRunPostSpawnFlatten)
                        {
                            FBox2D ActorFootprint(EForceInit::ForceInit);
                            const bool bHasActorFootprint = RoomActor_TryBuildFootprintFromActor(SpawnedActor, ActorFootprint);

                            const FVector ActorLoc = SpawnedActor->GetActorLocation();
                            const FVector2D FlattenCenter2D = bHasActorFootprint
                                ? (ActorFootprint.Min + ActorFootprint.Max) * 0.5f
                                : FVector2D(ActorLoc.X, ActorLoc.Y);

                            float FlattenRadius = Variation.FlattenRadius;
                            if (FlattenRadius <= KINDA_SMALL_NUMBER)
                            {
                                if (bHasActorFootprint)
                                {
                                    const FVector2D Extent2D = (ActorFootprint.Max - ActorFootprint.Min) * 0.5f;
                                    FlattenRadius = FMath::Max(Extent2D.X, Extent2D.Y);
                                }
                                else
                                {
                                    FlattenRadius = 240.0f;
                                }
                            }
                            if (bHasActorFootprint)
                            {
                                const float CoverageRadius = ResolveCoverageRadiusFromFootprint(
                                    ActorFootprint,
                                    EditorLandscapeFlattenFootprintCoverageScale);
                                FlattenRadius = FMath::Max(FlattenRadius, CoverageRadius);
                            }
                            FlattenRadius += FMath::Max(0.0f, EditorLandscapeFlattenExtraMargin);
                            FlattenRadius = FMath::Clamp(FlattenRadius, 180.0f, 10000.0f);

                            const float MinFalloffRatio = FMath::Clamp(EditorLandscapeFlattenMinFalloffRatio, 0.0f, 1.0f);
                            float FlattenFalloff = FMath::Clamp(Variation.FlattenSmoothFalloff, 0.0f, 6000.0f);
                            FlattenFalloff = FMath::Max(FlattenFalloff, FlattenRadius * MinFalloffRatio);
                            FlattenFalloff = FMath::Clamp(FlattenFalloff, 0.0f, 6000.0f);
                            const float TargetFlattenZ = SupportGroundZForActor + Variation.FlattenHeightOffset;
                            const float FlattenTotalRadius = FlattenRadius + FlattenFalloff;
                            const FBox2D CandidateFlattenFootprint(
                                FVector2D(FlattenCenter2D.X - FlattenTotalRadius, FlattenCenter2D.Y - FlattenTotalRadius),
                                FVector2D(FlattenCenter2D.X + FlattenTotalRadius, FlattenCenter2D.Y + FlattenTotalRadius));
                            const bool bHasCandidateFlattenFootprint = CandidateFlattenFootprint.bIsValid;
                            const float FlattenOverlapPadding = FMath::Clamp(EditorLandscapeFlattenOverlapPadding, 0.0f, 1200.0f);
                            const bool bFlattenOverlapBlocked =
                                bAvoidOverlappingEditorLandscapeFlatten &&
                                bHasCandidateFlattenFootprint &&
                                RoomActor_IsFootprintOverlappingAny(
                                    AppliedTerrainFlattenFootprints,
                                    CandidateFlattenFootprint,
                                    FlattenOverlapPadding);

                            if (bFlattenOverlapBlocked)
                            {
                                if (bLogEditorLandscapeFlatten)
                                {
                                    UE_LOG(
                                        LogTemp,
                                        Warning,
                                        TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=PostSpawnOverlapConflict Radius=%.1f Falloff=%.1f Existing=%d Padding=%.1f"),
                                        NodeId,
                                        MeshType,
                                        FlattenRadius,
                                        FlattenFalloff,
                                        AppliedTerrainFlattenFootprints.Num(),
                                        FlattenOverlapPadding);
                                }
                            }
                            else
                            {
                                const int32 AppliedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                                    FlattenWorld,
                                    this,
                                    FVector(FlattenCenter2D.X, FlattenCenter2D.Y, TargetFlattenZ),
                                    SpawnedActor->GetActorRotation().Yaw,
                                    FlattenRadius,
                                    FlattenFalloff,
                                    TargetFlattenZ,
                                    Variation.bFlattenRaiseHeights,
                                    Variation.bFlattenLowerHeights,
                                    EditorLandscapeFlattenEdgeCliffRatio,
                                    EditorLandscapeFlattenEdgeErosionRatio,
                                    EditorLandscapeFlattenEdgePatchSize,
                                    EditorLandscapeFlattenEdgeErosionStrength,
                                    EditorLandscapeFlattenEdgeSmoothStrength);

                                if (AppliedLandscapeCount > 0)
                                {
                                    ++EditorLandscapeFlattenOpsApplied;
                                    bBlueprintFlattenAppliedInSpawnPass = true;
                                    if (bHasCandidateFlattenFootprint)
                                    {
                                        AppliedTerrainFlattenFootprints.Add(CandidateFlattenFootprint);
                                    }

                                    FCollisionQueryParams PostFlattenQueryParams(SCENE_QUERY_STAT(RaidRoomBlueprintPostFlattenGroundSnap), false);
                                    PostFlattenQueryParams.bTraceComplex = false;
                                    PostFlattenQueryParams.AddIgnoredActor(this);
                                    PostFlattenQueryParams.AddIgnoredActor(SpawnedActor);
                                    for (const TObjectPtr<AActor>& ExistingSpawnedActor : SpawnedDynamicActors)
                                    {
                                        if (IsValid(ExistingSpawnedActor))
                                        {
                                            PostFlattenQueryParams.AddIgnoredActor(ExistingSpawnedActor);
                                        }
                                    }
                                    for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
                                    {
                                        if (IsValid(DoorActor))
                                        {
                                            PostFlattenQueryParams.AddIgnoredActor(DoorActor);
                                        }
                                    }

                                    FHitResult PostFlattenGroundHit;
                                    const FVector PostFlattenQueryLocation(FlattenCenter2D.X, FlattenCenter2D.Y, SpawnedActor->GetActorLocation().Z);
                                    if (TryResolveRoomSingleGroundHitAtPoint(FlattenWorld, PostFlattenQueryLocation, true, PostFlattenQueryParams, PostFlattenGroundHit))
                                    {
                                        SupportGroundZForActor = PostFlattenGroundHit.ImpactPoint.Z;
                                        CachedGroundHit = PostFlattenGroundHit;
                                        TargetBottomZ = SupportGroundZForActor + BaseLocalZContribution + 2.0f;

                                        float RecalcSupportMinZ = TNumericLimits<float>::Max();
                                        if (TryResolveActorLowestSupportZ(SpawnedActor, RecalcSupportMinZ))
                                        {
                                            const float DeltaToGround = TargetBottomZ - RecalcSupportMinZ;
                                            if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                                            {
                                                SpawnedActor->AddActorWorldOffset(
                                                    FVector(0.0f, 0.0f, DeltaToGround),
                                                    false,
                                                    nullptr,
                                                    ETeleportType::TeleportPhysics);
                                            }
                                        }
                                    }

                                    if (bLogEditorLandscapeFlatten)
                                    {
                                        UE_LOG(
                                            LogTemp,
                                            Warning,
                                            TEXT("[RaidRoom][TerrainFlatten] Node=%d MeshType=%d Reason=BlueprintPostSpawn Radius=%.1f Falloff=%.1f HeightOffset=%.1f Landscapes=%d"),
                                            NodeId,
                                            MeshType,
                                            FlattenRadius,
                                            FlattenFalloff,
                                            Variation.FlattenHeightOffset,
                                            AppliedLandscapeCount);
                                    }
                                }
                                else if (bLogEditorLandscapeFlatten)
                                {
                                    UE_LOG(
                                        LogTemp,
                                        Warning,
                                        TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=BlueprintPostSpawnNoLandscapeApplied Radius=%.1f Falloff=%.1f"),
                                        NodeId,
                                        MeshType,
                                        FlattenRadius,
                                        FlattenFalloff);
                                }
                            }
                        }
                    }
#endif

                    ReSnapBlueprintByFootprintMedian(SupportGroundZForActor);

                    if (!FMath::IsNearlyZero(EffectiveVariationDeltaLocalZ, 0.1f))
                    {
                        SpawnedActor->AddActorWorldOffset(
                            FVector(0.0f, 0.0f, EffectiveVariationDeltaLocalZ),
                            false,
                            nullptr,
                            ETeleportType::TeleportPhysics);
                    }
                }

                const bool bForceBlockCollision = (MeshType == 0 || MeshType == 1 || MeshType == 2 || MeshType == 3 || MeshType == 6 || MeshType == 8);
                const bool bForceNoCollision = (MeshType == 7);
                const bool bShouldCastShadow = (MeshType <= 3 || MeshType == 6 || MeshType == 8);
                const bool bShouldAffectNavigation = bForceBlockCollision && (MeshType == 2);

                TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
                SpawnedActor->GetComponents(PrimitiveComps);
                for (UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
                {
                    if (!IsValid(PrimitiveComp))
                    {
                        continue;
                    }

                    if (bForceBlockCollision)
                    {
                        if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimitiveComp))
                        {
                            EnsureMeshWalkableCollisionForRoom(StaticMeshComp->GetStaticMesh(), MeshType);
                        }
                        if (PrimitiveComp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
                        {
                            PrimitiveComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                        }
                        PrimitiveComp->SetCollisionObjectType(ECC_WorldStatic);
                        // Preserve asset-authored channel responses (including climb channels/volumes)
                        // and only guarantee that gameplay collision blocks character movement.
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
                        PrimitiveComp->CanCharacterStepUpOn = ECB_Yes;
                        if (ShouldForceTraversalWalkableSlopeForMeshType(MeshType))
                        {
                            PrimitiveComp->SetWalkableSlopeOverride(
                                FWalkableSlopeOverride(WalkableSlope_Increase, RoomTraversalWalkableSlopeAngleDeg));
                        }
                        else
                        {
                            PrimitiveComp->SetWalkableSlopeOverride(
                                FWalkableSlopeOverride(WalkableSlope_Default, 0.0f));
                        }
                    }
                    else if (bForceNoCollision)
                    {
                        PrimitiveComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        PrimitiveComp->SetCollisionProfileName(TEXT("NoCollision"));
                        PrimitiveComp->CanCharacterStepUpOn = ECB_No;
                    }

                    PrimitiveComp->SetCastShadow(bShouldCastShadow);
                    PrimitiveComp->bCastDynamicShadow = bShouldCastShadow;
                    PrimitiveComp->bCastStaticShadow = bShouldCastShadow;
                    PrimitiveComp->SetCanEverAffectNavigation(bShouldAffectNavigation);
                }

                SpawnedActor->Tags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
                SpawnedActor->Tags.AddUnique(RaidRoomGeneratedTag);
                SpawnedActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                if (MeshType == 2)
                {
                    SpawnedActor->Tags.AddUnique(FName(TEXT("ObstacleBlueprint")));
                }
                SpawnedActor->SetOwner(this);
                SpawnedActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                if (bShouldAffectNavigation)
                {
                    FNavigationSystem::UpdateActorAndComponentData(*SpawnedActor, true);
                }

                if (MeshType == 2)
                {
                    FBox2D FinalFootprint(EForceInit::ForceInit);
                    bool bHasFinalFootprint = RoomActor_TryBuildFootprintFromActor(SpawnedActor, FinalFootprint);
                    if (!bHasFinalFootprint && bHasCandidateObstacleFootprint)
                    {
                        FinalFootprint = CandidateObstacleFootprint;
                        bHasFinalFootprint = true;
                    }
                    if (!bHasFinalFootprint)
                    {
                        const FVector SpawnedLoc = SpawnedActor->GetActorLocation();
                        const float FallbackExtent = FMath::Max(
                            ObstacleMinSpacingForFootprint * (bVariationBlueprint ? 0.70f : 0.50f),
                            bVariationBlueprint ? 280.0f : 140.0f);
                        FinalFootprint = FBox2D(
                            FVector2D(SpawnedLoc.X - FallbackExtent, SpawnedLoc.Y - FallbackExtent),
                            FVector2D(SpawnedLoc.X + FallbackExtent, SpawnedLoc.Y + FallbackExtent));
                        bHasFinalFootprint = FinalFootprint.bIsValid;
                    }

                    if (bHasFinalFootprint)
                    {
                        const float FinalPadding = bVariationBlueprint
                            ? FMath::Clamp(ObstacleMinSpacingForFootprint * 0.20f, 60.0f, 320.0f)
                            : FMath::Clamp(ObstacleMinSpacingForFootprint * 0.12f, 20.0f, 140.0f);
                        if (RoomActor_IsFootprintOverlappingAny(SpawnedObstacleFootprints, FinalFootprint, FinalPadding))
                        {
                            SpawnedActor->Destroy();
                            return nullptr;
                        }
                        SpawnedObstacleFootprints.Add(FinalFootprint);
                    }
                }
                const bool bFoliageLikeMeshType = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                const float BaseLocalZContribution = bFoliageLikeMeshType
                    ? FMath::Clamp(BaseTransform.GetLocation().Z, -4000.0f, 4000.0f)
                    : 0.0f;
                const float ClampedVariationDeltaLocalZ = FMath::Clamp(VariationDeltaLocalZ, -20000.0f, 20000.0f);
                const float EffectiveVariationDeltaLocalZ = bFoliageLikeMeshType
                    ? FMath::Min(0.0f, ClampedVariationDeltaLocalZ)
                    : ClampedVariationDeltaLocalZ;

                FPendingBlueprintTerrainPlacement PendingPlacement;
                PendingPlacement.Actor = SpawnedActor;
                PendingPlacement.MeshType = MeshType;
                PendingPlacement.BaseLocalZContribution = BaseLocalZContribution;
                PendingPlacement.EffectiveVariationDeltaLocalZ = EffectiveVariationDeltaLocalZ;
                PendingPlacement.bFlattenLandscapeUnderSpawn = Variation.bFlattenLandscapeUnderSpawn;
                PendingPlacement.bFlattenAppliedInSpawnPass = bBlueprintFlattenAppliedInSpawnPass;
                PendingPlacement.bGroundHitOnLandscape = bGroundHitOnLandscape && bHasGroundHitForSnap;
                PendingPlacement.FlattenRadius = Variation.FlattenRadius;
                PendingPlacement.FlattenSmoothFalloff = Variation.FlattenSmoothFalloff;
                PendingPlacement.FlattenHeightOffset = Variation.FlattenHeightOffset;
                PendingPlacement.bFlattenRaiseHeights = Variation.bFlattenRaiseHeights;
                PendingPlacement.bFlattenLowerHeights = Variation.bFlattenLowerHeights;
                PendingBlueprintTerrainPlacements.Add(PendingPlacement);

                SpawnedDynamicActors.Add(SpawnedActor);
                return SpawnedActor;
            }

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Blueprint spawn failed after class load. RoomNode=%d MeshType=%d Class=%s"),
                NodeId,
                MeshType,
                *GetNameSafe(LoadedClass));
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Blueprint class load failed. RoomNode=%d MeshType=%d Asset=%s"),
                NodeId,
                MeshType,
                *Variation.BlueprintPrefab.ToString());
        }
        return nullptr;
    }

    UStaticMesh* LoadedMesh = PreloadedVariationMesh ? PreloadedVariationMesh : Variation.Mesh.LoadSynchronous();
    if (!LoadedMesh)
    {
        if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
        {
            static TSet<FString> LoggedMissingMeshAssets;
            const FString MissingPath = Variation.Mesh.ToString();
            if (!MissingPath.IsEmpty() && !LoggedMissingMeshAssets.Contains(MissingPath))
            {
                LoggedMissingMeshAssets.Add(MissingPath);
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] Static mesh load failed. RoomNode=%d MeshType=%d Theme=%s Env=%s Asset=%s"),
                    NodeId,
                    MeshType,
                    *NodeRow.Theme,
                    *NodeRow.EnvType,
                    *MissingPath);
            }
        }
        return nullptr;
    }
    if (IsRuntimePrototypeEngineMesh(GetWorld(), LoadedMesh))
    {
        return nullptr;
    }
    MaybeEnableNaniteForMesh(LoadedMesh);
    EnsureMeshWalkableCollisionForRoom(LoadedMesh, MeshType);

    UMaterialInterface* EffectiveMaterial = MaterialOverride;

    if (!EffectiveMaterial && bUseSemanticWhiteboxColors)
    {
        if (LoadedMesh->GetPathName().StartsWith(TEXT("/Engine/")))
        {
            EffectiveMaterial = GetSemanticMaterialForType(MeshType);
        }
    }

    const bool bTreeLikeMeshAsset = IsTreeLikeMeshName(LoadedMesh->GetPathName());
    const bool bSpawnAsTreeActor =
        (MeshType == 6) &&
        bSpawnWindAnimatedRoomTreesAsActors &&
        bTreeLikeMeshAsset;

    if (bSpawnAsTreeActor)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= ResolveRoomSpawnObjectFlags(GetWorld());
        if (AStaticMeshActor* TreeActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldTransform, SpawnParams))
        {
            if (UStaticMeshComponent* MeshComp = TreeActor->GetStaticMeshComponent())
            {
                MeshComp->SetStaticMesh(LoadedMesh);
                EnsureMeshWalkableCollisionForRoom(LoadedMesh, MeshType);
                if (MeshComp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
                {
                    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                }
                MeshComp->SetCollisionObjectType(ECC_WorldStatic);
                MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
                MeshComp->CanCharacterStepUpOn = ECB_Yes;
                if (ShouldForceTraversalWalkableSlopeForMeshType(MeshType))
                {
                    MeshComp->SetWalkableSlopeOverride(
                        FWalkableSlopeOverride(WalkableSlope_Increase, RoomTraversalWalkableSlopeAngleDeg));
                }
                else
                {
                    MeshComp->SetWalkableSlopeOverride(
                        FWalkableSlopeOverride(WalkableSlope_Default, 0.0f));
                }
                MeshComp->SetMobility(EComponentMobility::Static);
                MeshComp->SetCastShadow(true);
                MeshComp->bCastDynamicShadow = true;
                MeshComp->bCastStaticShadow = true;
                if (EffectiveMaterial)
                {
                    MeshComp->SetMaterial(0, EffectiveMaterial);
                }
                ApplyRoomTreeWindPhaseDesync(MeshComp, RoomRandomStream);
            }

            TreeActor->Tags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
            TreeActor->Tags.AddUnique(RaidRoomGeneratedTag);
            TreeActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
            TreeActor->SetOwner(this);
            SpawnedDynamicActors.Add(TreeActor);
            return TreeActor;
        }
    }

    DynamicISMC_Pool.RemoveAll(
        [](const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Candidate)
        {
            return !IsValid(Candidate) || Candidate->IsBeingDestroyed();
        });

    UHierarchicalInstancedStaticMeshComponent* TargetISMC = nullptr;
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : DynamicISMC_Pool)
    {
        if (!IsValid(ISMC) || !ISMC->IsRegistered() || ISMC->GetStaticMesh() != LoadedMesh) continue;
        const bool bTypeMatch = ISMC->ComponentTags.Contains(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
        UMaterialInterface* CurrentMat = (ISMC->GetNumMaterials() > 0) ? ISMC->GetMaterial(0) : nullptr;
        const bool bHasOverrideTag = ISMC->ComponentTags.Contains(FName(TEXT("MatOverride")));
        const bool bMaterialMatch = EffectiveMaterial ? (CurrentMat == EffectiveMaterial) : !bHasOverrideTag;
        if (bTypeMatch && bMaterialMatch) { TargetISMC = ISMC; break; }
    }

    if (!TargetISMC)
    {
        TargetISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this,
            NAME_None,
            ResolveRoomSpawnObjectFlags(GetWorld()));
        if (!IsValid(TargetISMC))
        {
            return nullptr;
        }
        TargetISMC->CreationMethod = EComponentCreationMethod::Instance; TargetISMC->SetMobility(EComponentMobility::Movable); TargetISMC->SetStaticMesh(LoadedMesh); TargetISMC->SetupAttachment(RootComponent);
        TargetISMC->ComponentTags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
        TargetISMC->ComponentTags.AddUnique(TEXT("RaidRuntimeISMC"));
        TargetISMC->ComponentTags.AddUnique(TEXT("RaidRoomRuntimeISMC"));
        ApplyISMCOptimization(TargetISMC, MeshType);
        if (EffectiveMaterial) { TargetISMC->SetMaterial(0, EffectiveMaterial); TargetISMC->ComponentTags.AddUnique(FName(TEXT("MatOverride"))); }
        TargetISMC->RegisterComponent();
        DynamicISMC_Pool.Add(TargetISMC);
    }

    if (!IsValid(TargetISMC) || !TargetISMC->IsRegistered() || !IsValid(TargetISMC->GetStaticMesh()))
    {
        return nullptr;
    }

    // Re-normalize optimization state even when reusing an existing pool component.
    // This prevents legacy nav-affecting flags from causing expensive nav churn/ensures
    // during AddInstance() on regenerated rooms.
    ApplyISMCOptimization(TargetISMC, MeshType);

    // Only obstacle ISMC should schedule navigation updates, and those are batched later.
    const bool bNavRelevantMeshType = bEnableObstacleNavigationUpdates && (MeshType == 2);
    if (TargetISMC->CanEverAffectNavigation())
    {
        // Keep navigation updates disabled while adding instances.
        // FlushQueuedNavigationUpdates() performs one stable batched update.
        TargetISMC->SetCanEverAffectNavigation(false);
    }

    // Avoid immediate per-instance navigation update during AddInstance().
    // We queue one batched nav update in FlushQueuedNavigationUpdates() instead.
    const int32 AddedInstanceIndex = TargetISMC->AddInstance(WorldTransform, true);
    if (AddedInstanceIndex == INDEX_NONE)
    {
        return nullptr;
    }

    if (MeshType == 2)
    {
        FBox2D FinalFootprint(EForceInit::ForceInit);
        bool bHasFinalFootprint = bHasCandidateObstacleFootprint;
        if (bHasFinalFootprint)
        {
            FinalFootprint = CandidateObstacleFootprint;
        }
        else
        {
            bHasFinalFootprint = RoomActor_TryBuildFootprintFromStaticMesh(LoadedMesh, WorldTransform, FinalFootprint);
        }
        if (!bHasFinalFootprint)
        {
            const FVector SpawnLoc = WorldTransform.GetLocation();
            const float FallbackExtent = FMath::Max(ObstacleMinSpacingForFootprint * 0.50f, 140.0f);
            FinalFootprint = FBox2D(
                FVector2D(SpawnLoc.X - FallbackExtent, SpawnLoc.Y - FallbackExtent),
                FVector2D(SpawnLoc.X + FallbackExtent, SpawnLoc.Y + FallbackExtent));
            bHasFinalFootprint = FinalFootprint.bIsValid;
        }

        if (bHasFinalFootprint)
        {
            const float FinalPadding = FMath::Clamp(ObstacleMinSpacingForFootprint * 0.12f, 20.0f, 140.0f);
            if (RoomActor_IsFootprintOverlappingAny(SpawnedObstacleFootprints, FinalFootprint, FinalPadding))
            {
                if (TargetISMC->IsValidInstance(AddedInstanceIndex))
                {
                    TargetISMC->RemoveInstance(AddedInstanceIndex);
                }
                return nullptr;
            }
            SpawnedObstacleFootprints.Add(FinalFootprint);
        }
    }

    if (bNavRelevantMeshType)
    {
        PendingNavUpdateISMCs.AddUnique(TargetISMC);
    }

    return nullptr;
}

void ARaidRoomActor::RunBlueprintTerrainStabilizationPass()
{
    if (PendingBlueprintTerrainPlacements.Num() <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        PendingBlueprintTerrainPlacements.Reset();
        return;
    }

    int32 ProcessedCount = 0;
    int32 CorrectedCount = 0;
    int32 FlattenAppliedCount = 0;
    int32 FlattenOverlapSkipCount = 0;
    int32 NoGroundCount = 0;
    int32 NoLandscapeCount = 0;
    int32 BlueprintOverlapResolvedCount = 0;
    int32 BlueprintOverlapUnresolvedCount = 0;
    int32 BlueprintOverlapCulledCount = 0;
    TSet<int32> PreFlattenAppliedPendingIndices;

    auto ResolveMedianSupportGround =
        [this, World](
            AActor* Actor,
            const FBox2D& ActorFootprint,
            float& OutGroundZ,
            bool& bOutLandscapeCenter) -> bool
        {
            if (!IsValid(Actor))
            {
                return false;
            }

            const FVector ActorLocation = Actor->GetActorLocation();
            const FVector2D Center2D = ActorFootprint.bIsValid
                ? (ActorFootprint.Min + ActorFootprint.Max) * 0.5f
                : FVector2D(ActorLocation.X, ActorLocation.Y);
            const FVector2D Half2D = ActorFootprint.bIsValid
                ? (ActorFootprint.Max - ActorFootprint.Min) * 0.5f
                : FVector2D(0.0f, 0.0f);

            TArray<FVector2D> SamplePoints;
            SamplePoints.Reserve(9);
            SamplePoints.Add(Center2D);
            if (ActorFootprint.bIsValid)
            {
                const FVector2D ClampedHalf(
                    FMath::Max(20.0f, Half2D.X),
                    FMath::Max(20.0f, Half2D.Y));
                SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, 0.0f));
                SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, 0.0f));
                SamplePoints.Add(Center2D + FVector2D(0.0f, ClampedHalf.Y));
                SamplePoints.Add(Center2D + FVector2D(0.0f, -ClampedHalf.Y));
                SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, ClampedHalf.Y));
                SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, ClampedHalf.Y));
                SamplePoints.Add(Center2D + FVector2D(ClampedHalf.X, -ClampedHalf.Y));
                SamplePoints.Add(Center2D + FVector2D(-ClampedHalf.X, -ClampedHalf.Y));
            }

            FCollisionQueryParams GroundQueryParams(SCENE_QUERY_STAT(RaidRoomBlueprintTerrainStabilizeGround), false);
            GroundQueryParams.bTraceComplex = false;
            GroundQueryParams.AddIgnoredActor(this);
            GroundQueryParams.AddIgnoredActor(Actor);
            for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
            {
                if (IsValid(ExistingActor))
                {
                    GroundQueryParams.AddIgnoredActor(ExistingActor);
                }
            }
            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
            {
                if (IsValid(DoorActor))
                {
                    GroundQueryParams.AddIgnoredActor(DoorActor);
                }
            }

            TArray<float> Heights;
            Heights.Reserve(SamplePoints.Num());
            bOutLandscapeCenter = false;
            for (int32 SampleIndex = 0; SampleIndex < SamplePoints.Num(); ++SampleIndex)
            {
                const FVector2D& SamplePoint = SamplePoints[SampleIndex];
                const FVector QueryPoint(SamplePoint.X, SamplePoint.Y, ActorLocation.Z);
                FHitResult GroundHit;
                if (TryResolveRoomSingleGroundHitAtPoint(World, QueryPoint, true, GroundQueryParams, GroundHit))
                {
                    Heights.Add(GroundHit.ImpactPoint.Z);
                    if (SampleIndex == 0)
                    {
                        bOutLandscapeCenter = IsLandscapeLikeHit(GroundHit);
                    }
                }
            }

            if (Heights.Num() <= 0)
            {
                return false;
            }

            Heights.Sort();
            OutGroundZ = Heights[Heights.Num() / 2];
            return true;
        };

    auto MeasureFootprintGroundSupportQuality =
        [this, World](
            AActor* Actor,
            const FBox2D& ActorFootprint,
            float& OutHitRatio,
            float& OutHeightRange) -> bool
        {
            OutHitRatio = 0.0f;
            OutHeightRange = 0.0f;
            if (!IsValid(Actor))
            {
                return false;
            }

            const FVector ActorLocation = Actor->GetActorLocation();
            const FVector2D Center2D = ActorFootprint.bIsValid
                ? (ActorFootprint.Min + ActorFootprint.Max) * 0.5f
                : FVector2D(ActorLocation.X, ActorLocation.Y);
            const FVector2D Half2D = ActorFootprint.bIsValid
                ? (ActorFootprint.Max - ActorFootprint.Min) * 0.5f
                : FVector2D(220.0f, 220.0f);
            const FVector2D ClampedHalf(
                FMath::Max(30.0f, Half2D.X),
                FMath::Max(30.0f, Half2D.Y));

            static const FVector2D LocalSamples[] =
            {
                FVector2D(0.0f, 0.0f),
                FVector2D(1.0f, 0.0f),
                FVector2D(-1.0f, 0.0f),
                FVector2D(0.0f, 1.0f),
                FVector2D(0.0f, -1.0f),
                FVector2D(1.0f, 1.0f),
                FVector2D(-1.0f, 1.0f),
                FVector2D(1.0f, -1.0f),
                FVector2D(-1.0f, -1.0f)
            };

            FCollisionQueryParams GroundQueryParams(SCENE_QUERY_STAT(RaidRoomTerrainSupportQuality), false);
            GroundQueryParams.bTraceComplex = false;
            GroundQueryParams.AddIgnoredActor(this);
            GroundQueryParams.AddIgnoredActor(Actor);
            for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
            {
                if (IsValid(ExistingActor))
                {
                    GroundQueryParams.AddIgnoredActor(ExistingActor);
                }
            }
            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
            {
                if (IsValid(DoorActor))
                {
                    GroundQueryParams.AddIgnoredActor(DoorActor);
                }
            }

            int32 HitCount = 0;
            float MinZ = TNumericLimits<float>::Max();
            float MaxZ = -TNumericLimits<float>::Max();
            for (const FVector2D& LocalSample : LocalSamples)
            {
                const FVector2D Sample2D = Center2D + FVector2D(LocalSample.X * ClampedHalf.X, LocalSample.Y * ClampedHalf.Y);
                const FVector QueryPoint(Sample2D.X, Sample2D.Y, ActorLocation.Z);
                FHitResult GroundHit;
                if (TryResolveRoomSingleGroundHitAtPoint(World, QueryPoint, true, GroundQueryParams, GroundHit))
                {
                    ++HitCount;
                    MinZ = FMath::Min(MinZ, GroundHit.ImpactPoint.Z);
                    MaxZ = FMath::Max(MaxZ, GroundHit.ImpactPoint.Z);
                }
            }

            OutHitRatio = static_cast<float>(HitCount) / static_cast<float>(UE_ARRAY_COUNT(LocalSamples));
            if (HitCount > 0 && MinZ < TNumericLimits<float>::Max() && MaxZ > -TNumericLimits<float>::Max())
            {
                OutHeightRange = FMath::Max(0.0f, MaxZ - MinZ);
                return true;
            }

            return false;
        };

    auto ResolveFlattenRadiusAndFalloff =
        [this](const FPendingBlueprintTerrainPlacement& Pending, const FBox2D& ActorFootprint, float& OutRadius, float& OutFalloff)
        {
            float FlattenRadius = Pending.FlattenRadius;
            if (FlattenRadius <= KINDA_SMALL_NUMBER)
            {
                if (ActorFootprint.bIsValid)
                {
                    const FVector2D FootprintExtent = (ActorFootprint.Max - ActorFootprint.Min) * 0.5f;
                    FlattenRadius = FMath::Max(FootprintExtent.X, FootprintExtent.Y);
                }
                else
                {
                    FlattenRadius = 240.0f;
                }
            }

            if (ActorFootprint.bIsValid)
            {
                const FVector2D FootprintExtent = (ActorFootprint.Max - ActorFootprint.Min) * 0.5f;
                const float FootprintDiagRadius = FootprintExtent.Size();
                const float CoverageRadius = ResolveCoverageRadiusFromFootprint(
                    ActorFootprint,
                    EditorLandscapeFlattenFootprintCoverageScale + 0.12f);
                FlattenRadius = FMath::Max(FlattenRadius, CoverageRadius);
                const float ExtraFootprintMargin = FMath::Clamp(FootprintDiagRadius * 0.10f, 40.0f, 260.0f);
                FlattenRadius += ExtraFootprintMargin;
            }

            FlattenRadius += FMath::Max(0.0f, EditorLandscapeFlattenExtraMargin);
            FlattenRadius = FMath::Clamp(FlattenRadius, 180.0f, 10000.0f);

            const float MinFalloffRatio = FMath::Clamp(EditorLandscapeFlattenMinFalloffRatio, 0.0f, 1.0f);
            float FlattenFalloff = FMath::Clamp(Pending.FlattenSmoothFalloff, 0.0f, 6000.0f);
            FlattenFalloff = FMath::Max(FlattenFalloff, FlattenRadius * MinFalloffRatio);
            FlattenFalloff = FMath::Clamp(FlattenFalloff, 0.0f, 6000.0f);

            OutRadius = FlattenRadius;
            OutFalloff = FlattenFalloff;
        };

    auto ConstrainFootprintInsideRoomBounds =
        [this](AActor* Actor, FBox2D& InOutFootprint) -> bool
        {
            if (!IsValid(Actor) || !InOutFootprint.bIsValid)
            {
                return false;
            }

            const FVector RoomExtent3D = GetRoomExtent();
            if (RoomExtent3D.X <= KINDA_SMALL_NUMBER || RoomExtent3D.Y <= KINDA_SMALL_NUMBER)
            {
                return false;
            }

            const FVector RoomCenter3D = GetActorLocation();
            const FVector2D RoomCenter(RoomCenter3D.X, RoomCenter3D.Y);
            const float RoomInset = FMath::Clamp(FMath::Min(RoomExtent3D.X, RoomExtent3D.Y) * 0.07f, 120.0f, 900.0f);
            const float MinX = RoomCenter.X - RoomExtent3D.X + RoomInset;
            const float MaxX = RoomCenter.X + RoomExtent3D.X - RoomInset;
            const float MinY = RoomCenter.Y - RoomExtent3D.Y + RoomInset;
            const float MaxY = RoomCenter.Y + RoomExtent3D.Y - RoomInset;
            const float AllowedWidthX = FMath::Max(1.0f, MaxX - MinX);
            const float AllowedWidthY = FMath::Max(1.0f, MaxY - MinY);
            const float FootprintWidthX = InOutFootprint.Max.X - InOutFootprint.Min.X;
            const float FootprintWidthY = InOutFootprint.Max.Y - InOutFootprint.Min.Y;

            float ShiftX = 0.0f;
            float ShiftY = 0.0f;
            if (FootprintWidthX >= AllowedWidthX)
            {
                ShiftX = RoomCenter.X - InOutFootprint.GetCenter().X;
            }
            else if (InOutFootprint.Min.X < MinX)
            {
                ShiftX = MinX - InOutFootprint.Min.X;
            }
            else if (InOutFootprint.Max.X > MaxX)
            {
                ShiftX = MaxX - InOutFootprint.Max.X;
            }

            if (FootprintWidthY >= AllowedWidthY)
            {
                ShiftY = RoomCenter.Y - InOutFootprint.GetCenter().Y;
            }
            else if (InOutFootprint.Min.Y < MinY)
            {
                ShiftY = MinY - InOutFootprint.Min.Y;
            }
            else if (InOutFootprint.Max.Y > MaxY)
            {
                ShiftY = MaxY - InOutFootprint.Max.Y;
            }

            if (FMath::IsNearlyZero(ShiftX, 0.5f) && FMath::IsNearlyZero(ShiftY, 0.5f))
            {
                return false;
            }

            FVector ActorLocation = Actor->GetActorLocation();
            ActorLocation.X += ShiftX;
            ActorLocation.Y += ShiftY;
            Actor->SetActorLocation(ActorLocation, false, nullptr, ETeleportType::TeleportPhysics);
            InOutFootprint.Min += FVector2D(ShiftX, ShiftY);
            InOutFootprint.Max += FVector2D(ShiftX, ShiftY);
            if (bLogEditorLandscapeFlatten)
            {
                UE_LOG(
                    LogTemp,
                    Display,
                    TEXT("[RaidRoom][TerrainStabilize][RoomClamp] Node=%d Actor=%s Shift=(%.1f,%.1f)"),
                    NodeId,
                    *GetNameSafe(Actor),
                    ShiftX,
                    ShiftY);
            }
            return true;
        };

#if WITH_EDITOR
    if (bEnableEditorLandscapeFlattenForMarkedVariations &&
        !World->IsGameWorld() &&
        bUseGroupedEditorLandscapeFlatten &&
        (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
    {
        struct FFlattenCandidate
        {
            int32 PendingIndex = INDEX_NONE;
            int32 MeshType = 0;
            FVector2D CenterXY = FVector2D::ZeroVector;
            FBox2D Footprint = FBox2D(EForceInit::ForceInit);
            float TargetZ = 0.0f;
            float Radius = 0.0f;
            float Falloff = 0.0f;
            bool bRaise = true;
            bool bLower = true;
        };

        TArray<FFlattenCandidate> FlattenCandidates;
        FlattenCandidates.Reserve(PendingBlueprintTerrainPlacements.Num());

        for (int32 PendingIndex = 0; PendingIndex < PendingBlueprintTerrainPlacements.Num(); ++PendingIndex)
        {
            FPendingBlueprintTerrainPlacement& Pending = PendingBlueprintTerrainPlacements[PendingIndex];
            AActor* CandidateActor = Pending.Actor.Get();
            if (!IsValid(CandidateActor) ||
                !Pending.bFlattenLandscapeUnderSpawn ||
                Pending.bFlattenAppliedInSpawnPass)
            {
                continue;
            }

            FBox2D CandidateFootprint(EForceInit::ForceInit);
            RoomActor_TryBuildFootprintFromActor(CandidateActor, CandidateFootprint);
            if (!CandidateFootprint.bIsValid)
            {
                const FBox ActorBounds = CandidateActor->GetComponentsBoundingBox(true);
                if (ActorBounds.IsValid)
                {
                    CandidateFootprint = FBox2D(
                        FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y),
                        FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y));
                }
            }
            if (ConstrainFootprintInsideRoomBounds(CandidateActor, CandidateFootprint))
            {
                ++CorrectedCount;
            }

            float CandidateGroundZ = CandidateActor->GetActorLocation().Z;
            bool bCandidateLandscapeCenter = Pending.bGroundHitOnLandscape;
            if (!ResolveMedianSupportGround(CandidateActor, CandidateFootprint, CandidateGroundZ, bCandidateLandscapeCenter) ||
                !bCandidateLandscapeCenter)
            {
                continue;
            }

            float CandidateRadius = 0.0f;
            float CandidateFalloff = 0.0f;
            ResolveFlattenRadiusAndFalloff(Pending, CandidateFootprint, CandidateRadius, CandidateFalloff);

            FFlattenCandidate Candidate;
            Candidate.PendingIndex = PendingIndex;
            Candidate.MeshType = Pending.MeshType;
            Candidate.Footprint = CandidateFootprint;
            Candidate.CenterXY = CandidateFootprint.bIsValid
                ? (CandidateFootprint.Min + CandidateFootprint.Max) * 0.5f
                : FVector2D(CandidateActor->GetActorLocation().X, CandidateActor->GetActorLocation().Y);
            Candidate.TargetZ = CandidateGroundZ + Pending.FlattenHeightOffset;
            Candidate.Radius = CandidateRadius;
            Candidate.Falloff = CandidateFalloff;
            Candidate.bRaise = Pending.bFlattenRaiseHeights;
            Candidate.bLower = Pending.bFlattenLowerHeights;
            FlattenCandidates.Add(Candidate);
        }

        if (FlattenCandidates.Num() > 1)
        {
            const float MergeDistance = FMath::Clamp(EditorLandscapeFlattenGroupMergeDistance, 0.0f, 4000.0f);

            TArray<int32> GroupIds;
            GroupIds.Init(INDEX_NONE, FlattenCandidates.Num());
            int32 NextGroupId = 0;

            for (int32 CandidateStart = 0; CandidateStart < FlattenCandidates.Num(); ++CandidateStart)
            {
                if (GroupIds[CandidateStart] != INDEX_NONE)
                {
                    continue;
                }

                GroupIds[CandidateStart] = NextGroupId;
                TArray<int32> Frontier;
                Frontier.Add(CandidateStart);

                for (int32 FrontierIdx = 0; FrontierIdx < Frontier.Num(); ++FrontierIdx)
                {
                    const int32 CurrentIndex = Frontier[FrontierIdx];
                    const FFlattenCandidate& Current = FlattenCandidates[CurrentIndex];

                    for (int32 TestIndex = 0; TestIndex < FlattenCandidates.Num(); ++TestIndex)
                    {
                        if (GroupIds[TestIndex] != INDEX_NONE)
                        {
                            continue;
                        }

                        const FFlattenCandidate& Test = FlattenCandidates[TestIndex];
                        const bool bCloseByFootprint =
                            Current.Footprint.bIsValid &&
                            Test.Footprint.bIsValid &&
                            ComputeFootprintGap2D(Current.Footprint, Test.Footprint) <= MergeDistance;
                        const bool bCloseByCenter =
                            FVector2D::Distance(Current.CenterXY, Test.CenterXY) <=
                            (MergeDistance + FMath::Min(Current.Radius, Test.Radius) * 0.35f);

                        if (!bCloseByFootprint && !bCloseByCenter)
                        {
                            continue;
                        }

                        GroupIds[TestIndex] = NextGroupId;
                        Frontier.Add(TestIndex);
                    }
                }

                ++NextGroupId;
            }

            TArray<TArray<int32>> Groups;
            Groups.SetNum(NextGroupId);
            for (int32 CandidateIndex = 0; CandidateIndex < GroupIds.Num(); ++CandidateIndex)
            {
                if (Groups.IsValidIndex(GroupIds[CandidateIndex]))
                {
                    Groups[GroupIds[CandidateIndex]].Add(CandidateIndex);
                }
            }

            for (const TArray<int32>& Group : Groups)
            {
                if (Group.Num() <= 1)
                {
                    continue;
                }
                if (EditorLandscapeFlattenMaxOpsPerRoom > 0 &&
                    EditorLandscapeFlattenOpsApplied >= EditorLandscapeFlattenMaxOpsPerRoom)
                {
                    break;
                }

                FBox2D GroupFootprint(EForceInit::ForceInit);
                TArray<float> GroupHeights;
                GroupHeights.Reserve(Group.Num());

                float MaxGroupRadius = 0.0f;
                float MaxGroupFalloff = 0.0f;
                bool bRaise = false;
                bool bLower = false;

                for (const int32 CandidateIndex : Group)
                {
                    const FFlattenCandidate& Candidate = FlattenCandidates[CandidateIndex];
                    GroupHeights.Add(Candidate.TargetZ);
                    MaxGroupRadius = FMath::Max(MaxGroupRadius, Candidate.Radius);
                    MaxGroupFalloff = FMath::Max(MaxGroupFalloff, Candidate.Falloff);
                    bRaise |= Candidate.bRaise;
                    bLower |= Candidate.bLower;

                    if (Candidate.Footprint.bIsValid)
                    {
                        GroupFootprint += Candidate.Footprint.Min;
                        GroupFootprint += Candidate.Footprint.Max;
                    }
                    else
                    {
                        GroupFootprint += (Candidate.CenterXY - FVector2D(Candidate.Radius, Candidate.Radius));
                        GroupFootprint += (Candidate.CenterXY + FVector2D(Candidate.Radius, Candidate.Radius));
                    }
                }

                if (!GroupFootprint.bIsValid || GroupHeights.Num() <= 0)
                {
                    continue;
                }

                GroupHeights.Sort();
                const float TargetFlattenZ = GroupHeights[GroupHeights.Num() / 2];
                const FVector2D GroupCenter = GroupFootprint.GetCenter();
                const FVector2D GroupHalf = GroupFootprint.GetSize() * 0.5f;

                float GroupRadius = FMath::Max(GroupHalf.X, GroupHalf.Y);
                GroupRadius = FMath::Max(GroupRadius, MaxGroupRadius);
                GroupRadius += FMath::Max(0.0f, EditorLandscapeFlattenExtraMargin);
                GroupRadius = FMath::Clamp(GroupRadius, 180.0f, 10000.0f);

                const float MinFalloffRatio = FMath::Clamp(EditorLandscapeFlattenMinFalloffRatio, 0.0f, 1.0f);
                float GroupFalloff = FMath::Max(MaxGroupFalloff, GroupRadius * MinFalloffRatio);
                GroupFalloff = FMath::Clamp(GroupFalloff, 0.0f, 6000.0f);

                const float TotalRadius = GroupRadius + GroupFalloff;
                const FBox2D CandidateFlattenFootprint(
                    FVector2D(GroupCenter.X - TotalRadius, GroupCenter.Y - TotalRadius),
                    FVector2D(GroupCenter.X + TotalRadius, GroupCenter.Y + TotalRadius));
                const bool bHasCandidateFlattenFootprint = CandidateFlattenFootprint.bIsValid;
                const float FlattenOverlapPadding = FMath::Clamp(EditorLandscapeFlattenOverlapPadding, 0.0f, 1200.0f);
                const bool bFlattenOverlapBlocked =
                    bAvoidOverlappingEditorLandscapeFlatten &&
                    bHasCandidateFlattenFootprint &&
                    RoomActor_IsFootprintOverlappingAny(
                        AppliedTerrainFlattenFootprints,
                        CandidateFlattenFootprint,
                        FlattenOverlapPadding);

                const bool bAllowOverlapForGroupedApply =
                    Group.Num() >= FMath::Max(2, EditorLandscapeFlattenGroupMinMembersForOverride);
                if (bFlattenOverlapBlocked && !bAllowOverlapForGroupedApply)
                {
                    ++FlattenOverlapSkipCount;
                    continue;
                }

                const int32 AppliedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                    World,
                    this,
                    FVector(GroupCenter.X, GroupCenter.Y, TargetFlattenZ),
                    GetActorRotation().Yaw,
                    GroupRadius,
                    GroupFalloff,
                    TargetFlattenZ,
                    bRaise,
                    bLower,
                    EditorLandscapeFlattenEdgeCliffRatio,
                    EditorLandscapeFlattenEdgeErosionRatio,
                    EditorLandscapeFlattenEdgePatchSize,
                    EditorLandscapeFlattenEdgeErosionStrength,
                    EditorLandscapeFlattenEdgeSmoothStrength);

                if (AppliedLandscapeCount <= 0)
                {
                    continue;
                }

                ++EditorLandscapeFlattenOpsApplied;
                ++FlattenAppliedCount;
                if (bHasCandidateFlattenFootprint)
                {
                    AppliedTerrainFlattenFootprints.Add(CandidateFlattenFootprint);
                }

                for (const int32 CandidateIndex : Group)
                {
                    if (FlattenCandidates.IsValidIndex(CandidateIndex))
                    {
                        const int32 PendingIndex = FlattenCandidates[CandidateIndex].PendingIndex;
                        PreFlattenAppliedPendingIndices.Add(PendingIndex);
                        if (PendingBlueprintTerrainPlacements.IsValidIndex(PendingIndex))
                        {
                            PendingBlueprintTerrainPlacements[PendingIndex].bFlattenAppliedInSpawnPass = true;
                        }
                    }
                }
            }

            if (bEnableRoomWideFlattenFallback &&
                (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
            {
                TArray<int32> UnresolvedCandidateIndices;
                FBox2D UnresolvedUnion(EForceInit::ForceInit);
                for (int32 CandidateIndex = 0; CandidateIndex < FlattenCandidates.Num(); ++CandidateIndex)
                {
                    const int32 PendingIndex = FlattenCandidates[CandidateIndex].PendingIndex;
                    if (PreFlattenAppliedPendingIndices.Contains(PendingIndex))
                    {
                        continue;
                    }

                    UnresolvedCandidateIndices.Add(CandidateIndex);
                    const FFlattenCandidate& Candidate = FlattenCandidates[CandidateIndex];
                    if (Candidate.Footprint.bIsValid)
                    {
                        UnresolvedUnion += Candidate.Footprint.Min;
                        UnresolvedUnion += Candidate.Footprint.Max;
                    }
                }

                const FVector RoomExtent = GetRoomExtent();
                const float RoomArea = FMath::Max(1.0f, (RoomExtent.X * 2.0f) * (RoomExtent.Y * 2.0f));
                const float Coverage = UnresolvedUnion.bIsValid
                    ? ComputeFootprintArea2D(UnresolvedUnion) / RoomArea
                    : 0.0f;
                const bool bNeedRoomWideFallback =
                    UnresolvedCandidateIndices.Num() >= FMath::Max(1, EditorLandscapeRoomWideFlattenMinActors) ||
                    Coverage >= FMath::Clamp(EditorLandscapeRoomWideFlattenCoverageThreshold, 0.0f, 1.0f);

                if (bNeedRoomWideFallback && UnresolvedCandidateIndices.Num() > 0)
                {
                    TArray<float> Heights;
                    Heights.Reserve(UnresolvedCandidateIndices.Num());
                    bool bRaise = false;
                    bool bLower = false;
                    for (const int32 CandidateIndex : UnresolvedCandidateIndices)
                    {
                        const FFlattenCandidate& Candidate = FlattenCandidates[CandidateIndex];
                        Heights.Add(Candidate.TargetZ);
                        bRaise |= Candidate.bRaise;
                        bLower |= Candidate.bLower;
                    }
                    Heights.Sort();
                    const float TargetFlattenZ = Heights[Heights.Num() / 2];

                    const FVector2D FallbackCenter = UnresolvedUnion.bIsValid
                        ? UnresolvedUnion.GetCenter()
                        : FVector2D(GetActorLocation().X, GetActorLocation().Y);
                    float FallbackRadius = FMath::Max(RoomExtent.X, RoomExtent.Y) + FMath::Max(0.0f, EditorLandscapeRoomWideFlattenMargin);
                    FallbackRadius = FMath::Clamp(FallbackRadius, 180.0f, 10000.0f);
                    const float MinFalloffRatio = FMath::Clamp(EditorLandscapeFlattenMinFalloffRatio, 0.0f, 1.0f);
                    float FallbackFalloff = FMath::Max(EditorLandscapeRoomWideFlattenFalloff, FallbackRadius * MinFalloffRatio);
                    FallbackFalloff = FMath::Clamp(FallbackFalloff, 0.0f, 6000.0f);

                    const int32 AppliedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                        World,
                        this,
                        FVector(FallbackCenter.X, FallbackCenter.Y, TargetFlattenZ),
                        GetActorRotation().Yaw,
                        FallbackRadius,
                        FallbackFalloff,
                        TargetFlattenZ,
                        bRaise,
                        bLower,
                        EditorLandscapeFlattenEdgeCliffRatio,
                        EditorLandscapeFlattenEdgeErosionRatio,
                        EditorLandscapeFlattenEdgePatchSize,
                        EditorLandscapeFlattenEdgeErosionStrength,
                        EditorLandscapeFlattenEdgeSmoothStrength);
                    if (AppliedLandscapeCount > 0)
                    {
                        ++EditorLandscapeFlattenOpsApplied;
                        ++FlattenAppliedCount;
                        for (const int32 CandidateIndex : UnresolvedCandidateIndices)
                        {
                            if (!FlattenCandidates.IsValidIndex(CandidateIndex))
                            {
                                continue;
                            }
                            const int32 PendingIndex = FlattenCandidates[CandidateIndex].PendingIndex;
                            PreFlattenAppliedPendingIndices.Add(PendingIndex);
                            if (PendingBlueprintTerrainPlacements.IsValidIndex(PendingIndex))
                            {
                                PendingBlueprintTerrainPlacements[PendingIndex].bFlattenAppliedInSpawnPass = true;
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    for (int32 PendingIndex = 0; PendingIndex < PendingBlueprintTerrainPlacements.Num(); ++PendingIndex)
    {
        FPendingBlueprintTerrainPlacement& Pending = PendingBlueprintTerrainPlacements[PendingIndex];
        AActor* Actor = Pending.Actor.Get();
        if (!IsValid(Actor))
        {
            continue;
        }

        ++ProcessedCount;

        FBox2D ActorFootprint(EForceInit::ForceInit);
        RoomActor_TryBuildFootprintFromActor(Actor, ActorFootprint);
        if (!ActorFootprint.bIsValid)
        {
            const FBox ActorBounds = Actor->GetComponentsBoundingBox(true);
            if (ActorBounds.IsValid)
            {
                ActorFootprint = FBox2D(
                    FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y),
                    FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y));
            }
        }
        if (ConstrainFootprintInsideRoomBounds(Actor, ActorFootprint))
        {
            ++CorrectedCount;
        }

        float SupportGroundZ = Actor->GetActorLocation().Z;
        bool bLandscapeAtCenter = Pending.bGroundHitOnLandscape;
        if (!ResolveMedianSupportGround(Actor, ActorFootprint, SupportGroundZ, bLandscapeAtCenter))
        {
            ++NoGroundCount;
            continue;
        }

#if WITH_EDITOR
        if (bEnableEditorLandscapeFlattenForMarkedVariations &&
            !World->IsGameWorld() &&
            Pending.bFlattenLandscapeUnderSpawn &&
            !Pending.bFlattenAppliedInSpawnPass &&
            (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
        {
            const FVector ActorLocation = Actor->GetActorLocation();
            const FVector2D FlattenCenter2D = ActorFootprint.bIsValid
                ? (ActorFootprint.Min + ActorFootprint.Max) * 0.5f
                : FVector2D(ActorLocation.X, ActorLocation.Y);

            FCollisionQueryParams CenterQueryParams(SCENE_QUERY_STAT(RaidRoomBlueprintTerrainStabilizeCenter), false);
            CenterQueryParams.bTraceComplex = false;
            CenterQueryParams.AddIgnoredActor(this);
            CenterQueryParams.AddIgnoredActor(Actor);
            for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
            {
                if (IsValid(ExistingActor))
                {
                    CenterQueryParams.AddIgnoredActor(ExistingActor);
                }
            }
            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
            {
                if (IsValid(DoorActor))
                {
                    CenterQueryParams.AddIgnoredActor(DoorActor);
                }
            }

            FHitResult CenterGroundHit;
            if (TryResolveRoomSingleGroundHitAtPoint(
                    World,
                    FVector(FlattenCenter2D.X, FlattenCenter2D.Y, ActorLocation.Z),
                    true,
                    CenterQueryParams,
                    CenterGroundHit))
            {
                bLandscapeAtCenter = IsLandscapeLikeHit(CenterGroundHit);
            }

            if (!bLandscapeAtCenter)
            {
                ++NoLandscapeCount;
            }
            else
            {
                float FlattenRadius = 0.0f;
                float FlattenFalloff = 0.0f;
                ResolveFlattenRadiusAndFalloff(Pending, ActorFootprint, FlattenRadius, FlattenFalloff);
                const float TargetFlattenZ = SupportGroundZ + Pending.FlattenHeightOffset;

                auto TryApplyFlattenWithRadius =
                    [&](float RadiusToTry, const TCHAR* ReasonTag) -> bool
                    {
                        const float Radius = FMath::Clamp(RadiusToTry, 120.0f, 10000.0f);
                        const float TotalRadius = Radius + FlattenFalloff;
                        const FBox2D CandidateFlattenFootprint(
                            FVector2D(FlattenCenter2D.X - TotalRadius, FlattenCenter2D.Y - TotalRadius),
                            FVector2D(FlattenCenter2D.X + TotalRadius, FlattenCenter2D.Y + TotalRadius));
                        const bool bHasCandidateFlattenFootprint = CandidateFlattenFootprint.bIsValid;
                        const float FlattenOverlapPadding = FMath::Clamp(EditorLandscapeFlattenOverlapPadding, 0.0f, 1200.0f);
                        const bool bFlattenOverlapBlocked =
                            bAvoidOverlappingEditorLandscapeFlatten &&
                            bHasCandidateFlattenFootprint &&
                            RoomActor_IsFootprintOverlappingAny(
                                AppliedTerrainFlattenFootprints,
                                CandidateFlattenFootprint,
                                FlattenOverlapPadding);

                        if (bFlattenOverlapBlocked)
                        {
                            ++FlattenOverlapSkipCount;
                            if (bLogEditorLandscapeFlatten)
                            {
                                UE_LOG(
                                    LogTemp,
                                    Warning,
                                    TEXT("[RaidRoom][TerrainFlatten][Skip] Node=%d MeshType=%d Reason=%s Radius=%.1f Falloff=%.1f Existing=%d Padding=%.1f"),
                                    NodeId,
                                    Pending.MeshType,
                                    ReasonTag,
                                    Radius,
                                    FlattenFalloff,
                                    AppliedTerrainFlattenFootprints.Num(),
                                    FlattenOverlapPadding);
                            }
                            return false;
                        }

                        const int32 AppliedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                            World,
                            this,
                            FVector(FlattenCenter2D.X, FlattenCenter2D.Y, TargetFlattenZ),
                            Actor->GetActorRotation().Yaw,
                            Radius,
                            FlattenFalloff,
                            TargetFlattenZ,
                            Pending.bFlattenRaiseHeights,
                            Pending.bFlattenLowerHeights,
                            EditorLandscapeFlattenEdgeCliffRatio,
                            EditorLandscapeFlattenEdgeErosionRatio,
                            EditorLandscapeFlattenEdgePatchSize,
                            EditorLandscapeFlattenEdgeErosionStrength,
                            EditorLandscapeFlattenEdgeSmoothStrength);
                        if (AppliedLandscapeCount <= 0)
                        {
                            return false;
                        }

                        ++EditorLandscapeFlattenOpsApplied;
                        ++FlattenAppliedCount;
                        if (bHasCandidateFlattenFootprint)
                        {
                            AppliedTerrainFlattenFootprints.Add(CandidateFlattenFootprint);
                        }

                        if (bLogEditorLandscapeFlatten)
                        {
                            UE_LOG(
                                LogTemp,
                                Warning,
                                TEXT("[RaidRoom][TerrainFlatten] Node=%d MeshType=%d Reason=%s Radius=%.1f Falloff=%.1f HeightOffset=%.1f Landscapes=%d"),
                                NodeId,
                                Pending.MeshType,
                                ReasonTag,
                                Radius,
                                FlattenFalloff,
                                Pending.FlattenHeightOffset,
                                AppliedLandscapeCount);
                        }

                        return true;
                    };

                bool bFlattenApplied = TryApplyFlattenWithRadius(FlattenRadius, TEXT("BlueprintStabilize"));
                if (!bFlattenApplied && bAvoidOverlappingEditorLandscapeFlatten)
                {
                    const float MinCoverageRadius = FMath::Max(
                        120.0f,
                        ResolveCoverageRadiusFromFootprint(ActorFootprint, EditorLandscapeFlattenFootprintCoverageScale));
                    const float RetryRadius = FMath::Clamp(FlattenRadius * 0.60f, MinCoverageRadius, FlattenRadius - 1.0f);
                    if (RetryRadius < FlattenRadius - KINDA_SMALL_NUMBER)
                    {
                        bFlattenApplied = TryApplyFlattenWithRadius(RetryRadius, TEXT("BlueprintStabilizeRetry"));
                    }
                }

                if (bFlattenApplied)
                {
                    Pending.bFlattenAppliedInSpawnPass = true;
                    bool bRefreshedLandscape = false;
                    float RefreshedGroundZ = SupportGroundZ;
                    if (ResolveMedianSupportGround(Actor, ActorFootprint, RefreshedGroundZ, bRefreshedLandscape))
                    {
                        SupportGroundZ = RefreshedGroundZ;
                    }

                    // Coverage safety pass: if large blueprint footprints still sit on sharp terrain
                    // after flatten, expand once to avoid partial out-of-flatten protrusion.
                    float SupportHitRatio = 1.0f;
                    float SupportHeightRange = 0.0f;
                    if (ActorFootprint.bIsValid &&
                        MeasureFootprintGroundSupportQuality(Actor, ActorFootprint, SupportHitRatio, SupportHeightRange) &&
                        (EditorLandscapeFlattenMaxOpsPerRoom <= 0 || EditorLandscapeFlattenOpsApplied < EditorLandscapeFlattenMaxOpsPerRoom))
                    {
                        const float HeightRangeThreshold = FMath::Max(EditorLandscapeFlattenMinHeightRange * 2.2f, 90.0f);
                        const float HitRatioThreshold = 0.72f;
                        const bool bNeedsExpandedCoverage =
                            SupportHeightRange > HeightRangeThreshold ||
                            SupportHitRatio < HitRatioThreshold;
                        if (bNeedsExpandedCoverage)
                        {
                            const float MinCoverageRadius = FMath::Max(
                                180.0f,
                                ResolveCoverageRadiusFromFootprint(ActorFootprint, EditorLandscapeFlattenFootprintCoverageScale * 1.18f));
                            const float ExpandedRadius = FMath::Clamp(
                                FMath::Max(FlattenRadius * 1.28f, MinCoverageRadius),
                                FlattenRadius + 80.0f,
                                10000.0f);
                            if (ExpandedRadius > FlattenRadius + KINDA_SMALL_NUMBER)
                            {
                                const int32 ExpandedLandscapeCount = ApplyEditorLandscapeFlattenBlob(
                                    World,
                                    this,
                                    FVector(FlattenCenter2D.X, FlattenCenter2D.Y, TargetFlattenZ),
                                    Actor->GetActorRotation().Yaw,
                                    ExpandedRadius,
                                    FlattenFalloff,
                                    TargetFlattenZ,
                                    Pending.bFlattenRaiseHeights,
                                    Pending.bFlattenLowerHeights,
                                    EditorLandscapeFlattenEdgeCliffRatio,
                                    EditorLandscapeFlattenEdgeErosionRatio,
                                    EditorLandscapeFlattenEdgePatchSize,
                                    EditorLandscapeFlattenEdgeErosionStrength,
                                    EditorLandscapeFlattenEdgeSmoothStrength);
                                if (ExpandedLandscapeCount > 0)
                                {
                                    ++EditorLandscapeFlattenOpsApplied;
                                    ++FlattenAppliedCount;
                                    if (bLogEditorLandscapeFlatten)
                                    {
                                        UE_LOG(
                                            LogTemp,
                                            Warning,
                                            TEXT("[RaidRoom][TerrainFlatten] Node=%d MeshType=%d Reason=BlueprintCoverageExpand Radius=%.1f Falloff=%.1f HeightRange=%.1f HitRatio=%.2f Landscapes=%d"),
                                            NodeId,
                                            Pending.MeshType,
                                            ExpandedRadius,
                                            FlattenFalloff,
                                            SupportHeightRange,
                                            SupportHitRatio,
                                            ExpandedLandscapeCount);
                                    }

                                    bool bExpandedRefreshedLandscape = false;
                                    float ExpandedRefreshedGroundZ = SupportGroundZ;
                                    if (ResolveMedianSupportGround(Actor, ActorFootprint, ExpandedRefreshedGroundZ, bExpandedRefreshedLandscape))
                                    {
                                        SupportGroundZ = ExpandedRefreshedGroundZ;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
#endif

        const float TargetBottomZ =
            SupportGroundZ +
            Pending.BaseLocalZContribution +
            2.0f +
            Pending.EffectiveVariationDeltaLocalZ;

        float CurrentSupportMinZ = TNumericLimits<float>::Max();
        bool bCorrected = false;
        if (TryResolveActorLowestSupportZ(Actor, CurrentSupportMinZ))
        {
            const float DeltaToGround = TargetBottomZ - CurrentSupportMinZ;
            if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
            {
                Actor->AddActorWorldOffset(
                    FVector(0.0f, 0.0f, DeltaToGround),
                    false,
                    nullptr,
                    ETeleportType::TeleportPhysics);
                bCorrected = true;
            }
        }
        else
        {
            FVector CorrectedLocation = Actor->GetActorLocation();
            CorrectedLocation.Z = TargetBottomZ;
            Actor->SetActorLocation(CorrectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
            bCorrected = true;
        }

        if (bCorrected)
        {
            ++CorrectedCount;
        }
    }

    auto BuildActorFootprintWithFallback =
        [](AActor* Actor, FBox2D& OutFootprint) -> bool
        {
            OutFootprint = FBox2D(EForceInit::ForceInit);
            if (!IsValid(Actor))
            {
                return false;
            }

            if (RoomActor_TryBuildFootprintFromActor(Actor, OutFootprint) && OutFootprint.bIsValid)
            {
                return true;
            }

            const FBox ActorBounds = Actor->GetComponentsBoundingBox(true);
            if (ActorBounds.IsValid)
            {
                OutFootprint = FBox2D(
                    FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y),
                    FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y));
                return OutFootprint.bIsValid;
            }

            return false;
        };

    auto ResolveBlueprintSeparationPadding =
        [this](int32 MeshType) -> float
        {
            if (MeshType == 2)
            {
                return FMath::Clamp(BlueprintObstacleMinSpacing * 0.18f, 80.0f, 360.0f);
            }
            if (MeshType == 3)
            {
                return FMath::Clamp(BlueprintDecorationMinSpacing * 0.14f, 60.0f, 280.0f);
            }
            if (MeshType == 6 || MeshType == 7 || MeshType == 8)
            {
                return FMath::Clamp(BlueprintDecorationMinSpacing * 0.10f, 40.0f, 180.0f);
            }
            return FMath::Clamp(ObstacleMinSpacing * 0.14f, 50.0f, 220.0f);
        };

    // Final blueprint de-overlap pass: terrain/room-bound corrections can re-introduce XY overlap.
    // Re-separate actors in-place and re-snap Z to support ground so overlap does not remain visible.
    {
        constexpr int32 MaxSeparationPasses = 4;
        constexpr int32 MaxRepositionAttemptsPerActor = 28;
        TArray<FBox2D> AcceptedFootprints;
        AcceptedFootprints.Reserve(PendingBlueprintTerrainPlacements.Num());

        for (int32 PassIndex = 0; PassIndex < MaxSeparationPasses; ++PassIndex)
        {
            bool bAnyActorMovedThisPass = false;
            AcceptedFootprints.Reset();

            for (int32 PendingIndex = 0; PendingIndex < PendingBlueprintTerrainPlacements.Num(); ++PendingIndex)
            {
                FPendingBlueprintTerrainPlacement& Pending = PendingBlueprintTerrainPlacements[PendingIndex];
                AActor* Actor = Pending.Actor.Get();
                if (!IsValid(Actor))
                {
                    continue;
                }

                FBox2D ActorFootprint(EForceInit::ForceInit);
                if (!BuildActorFootprintWithFallback(Actor, ActorFootprint))
                {
                    continue;
                }

                const float SeparationPadding = ResolveBlueprintSeparationPadding(Pending.MeshType);
                const float HardPadding = 0.0f;
                const bool bOverlapsPriorFootprints =
                    RoomActor_IsFootprintOverlappingAny(AcceptedFootprints, ActorFootprint, SeparationPadding);
                if (!bOverlapsPriorFootprints)
                {
                    AcceptedFootprints.Add(ActorFootprint);
                    continue;
                }

                const FVector OriginalLocation = Actor->GetActorLocation();
                bool bResolved = false;
                for (int32 PhaseIndex = 0; PhaseIndex < 2 && !bResolved; ++PhaseIndex)
                {
                    const float CheckPadding = (PhaseIndex == 0) ? SeparationPadding : HardPadding;

                    for (int32 AttemptIndex = 0; AttemptIndex < MaxRepositionAttemptsPerActor; ++AttemptIndex)
                    {
                        const float T = static_cast<float>(AttemptIndex + 1) / static_cast<float>(MaxRepositionAttemptsPerActor);
                        const float Angle =
                            ((static_cast<float>(AttemptIndex) * 2.39996323f) + (static_cast<float>(PassIndex) * 0.47f));
                        const float Radius =
                            FMath::Max(90.0f, SeparationPadding * 0.45f) +
                            (T * T) * FMath::Max(1400.0f, SeparationPadding * 6.0f);

                        FVector CandidateLocation = OriginalLocation;
                        CandidateLocation.X += FMath::Cos(Angle) * Radius;
                        CandidateLocation.Y += FMath::Sin(Angle) * Radius;
                        Actor->SetActorLocation(CandidateLocation, false, nullptr, ETeleportType::TeleportPhysics);

                        FBox2D CandidateFootprint(EForceInit::ForceInit);
                        if (!BuildActorFootprintWithFallback(Actor, CandidateFootprint))
                        {
                            continue;
                        }

                        if (ConstrainFootprintInsideRoomBounds(Actor, CandidateFootprint))
                        {
                            ++CorrectedCount;
                            if (!BuildActorFootprintWithFallback(Actor, CandidateFootprint))
                            {
                                continue;
                            }
                        }

                        if (RoomActor_IsFootprintOverlappingAny(AcceptedFootprints, CandidateFootprint, CheckPadding))
                        {
                            continue;
                        }

                        float SupportGroundZ = Actor->GetActorLocation().Z;
                        bool bLandscapeCenter = false;
                        if (ResolveMedianSupportGround(Actor, CandidateFootprint, SupportGroundZ, bLandscapeCenter))
                        {
                            const float TargetBottomZ =
                                SupportGroundZ +
                                Pending.BaseLocalZContribution +
                                2.0f +
                                Pending.EffectiveVariationDeltaLocalZ;
                            float CurrentSupportMinZ = TNumericLimits<float>::Max();
                            if (TryResolveActorLowestSupportZ(Actor, CurrentSupportMinZ))
                            {
                                const float DeltaToGround = TargetBottomZ - CurrentSupportMinZ;
                                if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                                {
                                    Actor->AddActorWorldOffset(
                                        FVector(0.0f, 0.0f, DeltaToGround),
                                        false,
                                        nullptr,
                                        ETeleportType::TeleportPhysics);
                                }
                            }
                            else
                            {
                                FVector CorrectedLocation = Actor->GetActorLocation();
                                CorrectedLocation.Z = TargetBottomZ;
                                Actor->SetActorLocation(CorrectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
                            }
                        }

                        ActorFootprint = CandidateFootprint;
                        bResolved = true;
                        bAnyActorMovedThisPass = true;
                        ++BlueprintOverlapResolvedCount;
                        ++CorrectedCount;
                        break;
                    }
                }

                if (!bResolved)
                {
                    Actor->SetActorLocation(OriginalLocation, false, nullptr, ETeleportType::TeleportPhysics);
                    FBox2D RestoredFootprint(EForceInit::ForceInit);
                    if (BuildActorFootprintWithFallback(Actor, RestoredFootprint))
                    {
                        ActorFootprint = RestoredFootprint;
                    }

                    // Final hard guard: if still physically overlapping, cull this spawn to avoid visible intersections.
                    if (ActorFootprint.bIsValid &&
                        RoomActor_IsFootprintOverlappingAny(AcceptedFootprints, ActorFootprint, HardPadding))
                    {
                        SpawnedDynamicActors.Remove(Actor);
                        Actor->Destroy();
                        Pending.Actor = nullptr;
                        ++BlueprintOverlapCulledCount;
                        ++BlueprintOverlapResolvedCount;
                        continue;
                    }

                    ++BlueprintOverlapUnresolvedCount;
                }

                AcceptedFootprints.Add(ActorFootprint);
            }

            if (!bAnyActorMovedThisPass)
            {
                break;
            }
        }
    }

    UE_LOG(
        LogTemp,
        Display,
        TEXT("[RaidRoom][TerrainStabilize] Node=%d Processed=%d Corrected=%d FlattenApplied=%d FlattenOverlapSkip=%d NoGround=%d NoLandscape=%d BPOverlapResolved=%d BPOverlapUnresolved=%d BPOverlapCulled=%d"),
        NodeId,
        ProcessedCount,
        CorrectedCount,
        FlattenAppliedCount,
        FlattenOverlapSkipCount,
        NoGroundCount,
        NoLandscapeCount,
        BlueprintOverlapResolvedCount,
        BlueprintOverlapUnresolvedCount,
        BlueprintOverlapCulledCount);

    PendingBlueprintTerrainPlacements.Reset();
}

void ARaidRoomActor::GenerateTraversalWhiteboxKit(float RoomRadius, const FModularMeshKit* ThemeKit)
{
    if (!bEnableTraversalWhiteboxKit) return;
    FRandomStream Rng(NodeRow.Seed ^ (NodeId * 9973));
    const UWorld* CurrentWorld = GetWorld();
    const bool bIsGameWorld = (CurrentWorld && CurrentWorld->IsGameWorld());
    const bool bJungleStyledRoom =
        NodeRow.EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
        NodeRow.EnvType.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
        NodeRow.EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
    UMaterialInterface* TraversalMat = GetTraversalMaterial();
    const bool bHasTraversalMeshOverride = !TraversalMeshOverride.IsNull();
    TMap<FSoftObjectPath, float> MeshBaseLiftCache;
    TArray<FVector> OccupiedObstacleLocations;
    TArray<FVector> OccupiedDecorationLocations;
    int32 SpawnedBlueprintObstacleCount = 0;
    FString LastPickedObstacleVariationKey;
    TMap<FString, int32> ObstacleVariationPickCounts;
    TMap<FString, int32> ObstacleVariationPlacedCounts;
    TMap<FString, float> ObstacleVariationConfiguredWeights;
    TMap<FString, float> ObstacleVariationEffectiveWeights;
    enum class EObstacleSpawnFailReason : uint8
    {
        None,
        NoVariation,
        PlacementRejected
    };
    int32 ObstacleTargetCount = 0;
    int32 ObstacleAttemptCount = 0;
    int32 ObstaclePlacedCount = 0;
    int32 ObstacleSkipCenterCount = 0;
    int32 ObstacleNoVariationCount = 0;
    int32 ObstaclePlacementRejectCount = 0;
    TArray<FMeshVariation> ThemeFloorVariations;
    TArray<FMeshVariation> ThemeWallVariations;
    TArray<FMeshVariation> ThemeObstacleVariations;
    TArray<FMeshVariation> ThemeDecorationVariations;
    if (ThemeKit)
    {
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Floor, ThemeFloorVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Wall, ThemeWallVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, ThemeObstacleVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Decoration, ThemeDecorationVariations);
    }

    auto BuildRuntimeUsableVariations =
        [&](const TArray<FMeshVariation>& InVariations, TArray<FMeshVariation>& OutVariations, const TCHAR* ChannelLabel)
        {
            OutVariations.Reset();
            OutVariations.Reserve(InVariations.Num());

            for (const FMeshVariation& Variation : InVariations)
            {
                const bool bHasMesh = !Variation.Mesh.IsNull();
                const bool bHasBlueprint = !Variation.BlueprintPrefab.IsNull();
                if (!bHasMesh && !bHasBlueprint)
                {
                    continue;
                }

                bool bLoadable = false;
                FString AssetPath;

                if (bHasBlueprint)
                {
                    AssetPath = Variation.BlueprintPrefab.ToString();
                    bLoadable = Variation.BlueprintPrefab.LoadSynchronous() != nullptr;
                }
                else
                {
                    AssetPath = Variation.Mesh.ToString();
                    bLoadable = Variation.Mesh.LoadSynchronous() != nullptr;
                }

                if (!bLoadable)
                {
                    if (bIsGameWorld)
                    {
                        static TSet<FString> LoggedUnusableVariationPaths;
                        if (!AssetPath.IsEmpty() && !LoggedUnusableVariationPaths.Contains(AssetPath))
                        {
                            LoggedUnusableVariationPaths.Add(AssetPath);
                            UE_LOG(
                                LogTemp,
                                Warning,
                                TEXT("[RaidRoom] Unusable variation skipped at runtime. Node=%d Channel=%s Theme=%s Env=%s Asset=%s"),
                                NodeId,
                                ChannelLabel,
                                *NodeRow.Theme,
                                *NodeRow.EnvType,
                                *AssetPath);
                        }
                    }
                    continue;
                }

                OutVariations.Add(Variation);
            }
        };

    if (bIsGameWorld)
    {
        // Shipping/runtime safety: if cooked data loses valid soft references in effective list,
        // fall back to raw channel list before generation proceeds.
        TArray<FMeshVariation> RuntimeObstacleVariations;
        BuildRuntimeUsableVariations(ThemeObstacleVariations, RuntimeObstacleVariations, TEXT("Obstacle"));
        if (RuntimeObstacleVariations.Num() <= 0 && ThemeKit)
        {
            TArray<FMeshVariation> RawObstacleVariations;
            ThemeKit->GetAllRawVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, RawObstacleVariations);
            BuildRuntimeUsableVariations(RawObstacleVariations, RuntimeObstacleVariations, TEXT("ObstacleRaw"));
        }
        if (RuntimeObstacleVariations.Num() > 0)
        {
            ThemeObstacleVariations = MoveTemp(RuntimeObstacleVariations);
        }

        // Generic fallback: if resolved theme yields no usable obstacle variations in runtime,
        // try compatible theme entries from ThemeRegistry first, then all themes as last resort.
        if (ThemeObstacleVariations.Num() <= 0 && ChapterConfigRef)
        {
            auto BuildThemeFallbackObstaclePool =
                [&](bool bOnlyCompatibleThemes, TArray<FMeshVariation>& OutFallbackVariations) -> void
                {
                    OutFallbackVariations.Reset();

                    TSet<FSoftObjectPath> AddedMeshPaths;
                    TSet<FSoftObjectPath> AddedBlueprintPaths;

                    auto AppendUniqueVariations =
                        [&](const TArray<FMeshVariation>& InVariations)
                        {
                            for (const FMeshVariation& Variation : InVariations)
                            {
                                const FSoftObjectPath MeshPath = Variation.Mesh.ToSoftObjectPath();
                                const FSoftObjectPath BlueprintPath = Variation.BlueprintPrefab.ToSoftObjectPath();

                                if (MeshPath.IsNull() && BlueprintPath.IsNull())
                                {
                                    continue;
                                }

                                bool bAlreadyAdded = false;
                                if (!MeshPath.IsNull())
                                {
                                    bAlreadyAdded = AddedMeshPaths.Contains(MeshPath);
                                }
                                else if (!BlueprintPath.IsNull())
                                {
                                    bAlreadyAdded = AddedBlueprintPaths.Contains(BlueprintPath);
                                }

                                if (bAlreadyAdded)
                                {
                                    continue;
                                }

                                if (!MeshPath.IsNull())
                                {
                                    AddedMeshPaths.Add(MeshPath);
                                }
                                if (!BlueprintPath.IsNull())
                                {
                                    AddedBlueprintPaths.Add(BlueprintPath);
                                }

                                OutFallbackVariations.Add(Variation);
                            }
                        };

                    for (const TPair<FString, FModularMeshKit>& Pair : ChapterConfigRef->ThemeRegistry)
                    {
                        const FString ThemeKey = Pair.Key;
                        bool bCompatibleTheme = false;

                        if (ThemeKey.Equals(CachedResolvedThemeKey, ESearchCase::IgnoreCase) ||
                            ThemeKey.Equals(NodeRow.Theme, ESearchCase::IgnoreCase) ||
                            ThemeKey.Equals(NodeRow.EnvType, ESearchCase::IgnoreCase))
                        {
                            bCompatibleTheme = true;
                        }
                        else
                        {
                            const FString ThemeKeyLower = ThemeKey.ToLower();
                            if (bJungleStyledRoom)
                            {
                                bCompatibleTheme =
                                    ThemeKeyLower.Contains(TEXT("jungle")) ||
                                    ThemeKeyLower.Contains(TEXT("nature"));
                            }
                            else if (NodeRow.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase) ||
                                     NodeRow.Theme.Equals(TEXT("Urban"), ESearchCase::IgnoreCase))
                            {
                                bCompatibleTheme = ThemeKeyLower.Contains(TEXT("urban"));
                            }
                        }

                        if (bOnlyCompatibleThemes && !bCompatibleTheme)
                        {
                            continue;
                        }

                        TArray<FMeshVariation> CandidateVariations;
                        Pair.Value.GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, CandidateVariations);
                        if (CandidateVariations.Num() <= 0)
                        {
                            Pair.Value.GetAllRawVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, CandidateVariations);
                        }

                        TArray<FMeshVariation> RuntimeUsable;
                        BuildRuntimeUsableVariations(CandidateVariations, RuntimeUsable, TEXT("ThemeRegistryFallback"));
                        AppendUniqueVariations(RuntimeUsable);
                    }
                };

            TArray<FMeshVariation> FallbackVariations;
            bool bUsedCompatiblePass = true;
            BuildThemeFallbackObstaclePool(true, FallbackVariations);
            if (FallbackVariations.Num() <= 0)
            {
                bUsedCompatiblePass = false;
                BuildThemeFallbackObstaclePool(false, FallbackVariations);
            }

            if (FallbackVariations.Num() > 0)
            {
                ThemeObstacleVariations = MoveTemp(FallbackVariations);
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] Obstacle variation registry fallback enabled. Node=%d Theme=%s Env=%s Count=%d CompatibleOnly=%s"),
                    NodeId,
                    *NodeRow.Theme,
                    *NodeRow.EnvType,
                    ThemeObstacleVariations.Num(),
                    bUsedCompatiblePass ? TEXT("true") : TEXT("false"));
            }
        }
    }

    if (bIsGameWorld)
    {
        static int32 GObstaclePoolLogCount = 0;
        if (GObstaclePoolLogCount < 32)
        {
            ++GObstaclePoolLogCount;
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Obstacle pool ready. Node=%d Theme=%s Env=%s Count=%d"),
                NodeId,
                *NodeRow.Theme,
                *NodeRow.EnvType,
                ThemeObstacleVariations.Num());
        }
    }

    auto BuildObstacleVariationDebugKey = [&](const FMeshVariation& Variation, int32 VariationIndex) -> FString
        {
            const bool bBlueprintVariation = !Variation.BlueprintPrefab.IsNull();
            const FSoftObjectPath AssetPath = bBlueprintVariation
                ? Variation.BlueprintPrefab.ToSoftObjectPath()
                : Variation.Mesh.ToSoftObjectPath();
            const FString AssetName = AssetPath.GetAssetName();
            const FString SafeAssetName = AssetName.IsEmpty()
                ? FString::Printf(TEXT("Var%d"), VariationIndex)
                : AssetName;
            return FString::Printf(TEXT("%s:%s#%d"), bBlueprintVariation ? TEXT("BP") : TEXT("SM"), *SafeAssetName, VariationIndex);
        };

    auto GetEffectiveObstacleWeight = [&](const FMeshVariation& Variation, bool bRespectBlueprintCap) -> float
        {
            const float BaseWeight = FMath::Max(0.0f, Variation.SpawnWeight);
            if (BaseWeight <= KINDA_SMALL_NUMBER)
            {
                return 0.0f;
            }

            const bool bBlueprintVariation = !Variation.BlueprintPrefab.IsNull();
            if (bBlueprintVariation && bRespectBlueprintCap && MaxBlueprintObstaclesPerRoom > 0 && SpawnedBlueprintObstacleCount >= MaxBlueprintObstaclesPerRoom)
            {
                return 0.0f;
            }

            const float TypeScale = bBlueprintVariation ? BlueprintObstacleWeightScale : StaticMeshObstacleWeightScale;
            return BaseWeight * FMath::Max(0.0f, TypeScale);
        };

    if (ThemeObstacleVariations.Num() > 0)
    {
        for (int32 VariationIndex = 0; VariationIndex < ThemeObstacleVariations.Num(); ++VariationIndex)
        {
            const FMeshVariation& Variation = ThemeObstacleVariations[VariationIndex];
            const FString VariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
            ObstacleVariationConfiguredWeights.Add(VariationKey, FMath::Max(0.0f, Variation.SpawnWeight));
            ObstacleVariationEffectiveWeights.Add(VariationKey, GetEffectiveObstacleWeight(Variation, false));
        }
    }

    auto CanSpawnObstacleVariation = [&](const FMeshVariation& Variation) -> bool
        {
            if (GetEffectiveObstacleWeight(Variation, true) <= KINDA_SMALL_NUMBER)
            {
                return false;
            }

            return true;
        };

    auto PickThemeVariationForMeshType = [&](int32 MeshType) -> const FMeshVariation*
        {
            if (!bUseThemeMeshForTraversalKit || !ThemeKit) return nullptr;

            const TArray<FMeshVariation>* CandidatePool = nullptr;
            if (MeshType == 1)
            {
                CandidatePool = &ThemeWallVariations;
            }
            else if (MeshType == 0)
            {
                CandidatePool = &ThemeFloorVariations;
            }
            else if (MeshType == 3)
            {
                CandidatePool = &ThemeDecorationVariations;
            }
            else
            {
                CandidatePool = &ThemeObstacleVariations;
            }

            if (!CandidatePool || CandidatePool->Num() <= 0)
            {
                return nullptr;
            }

            if (MeshType != 2)
            {
                LastPickedObstacleVariationKey.Reset();
                return RaidMeshUtils::PickRandomVariation(*CandidatePool, Rng);
            }

            // 장애물 타입은 가중치 + 블루프린트 룸당 캡을 같이 적용.
            float TotalWeight = 0.0f;
            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (!CanSpawnObstacleVariation(Variation)) continue;
                TotalWeight += GetEffectiveObstacleWeight(Variation, true);
            }

            if (TotalWeight <= KINDA_SMALL_NUMBER)
            {
                if (bIsGameWorld)
                {
                    static int32 GObstacleZeroWeightLogCount = 0;
                    if (GObstacleZeroWeightLogCount < 32)
                    {
                        ++GObstacleZeroWeightLogCount;
                        UE_LOG(
                            LogTemp,
                            Warning,
                            TEXT("[RaidRoom] Obstacle variation total weight is zero. Node=%d Theme=%s Env=%s Pool=%d StaticWeightScale=%.2f BlueprintWeightScale=%.2f"),
                            NodeId,
                            *NodeRow.Theme,
                            *NodeRow.EnvType,
                            CandidatePool ? CandidatePool->Num() : 0,
                            StaticMeshObstacleWeightScale,
                            BlueprintObstacleWeightScale);
                    }
                }
                LastPickedObstacleVariationKey.Reset();
                return nullptr;
            }

            float Pick = Rng.FRandRange(0.0f, TotalWeight);
            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (!CanSpawnObstacleVariation(Variation)) continue;
                Pick -= GetEffectiveObstacleWeight(Variation, true);
                if (Pick <= 0.0f)
                {
                    LastPickedObstacleVariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
                    ObstacleVariationPickCounts.FindOrAdd(LastPickedObstacleVariationKey) += 1;
#if !UE_BUILD_SHIPPING
                    static int32 GObstaclePickDebugLogCount = 0;
                    if (GObstaclePickDebugLogCount < 24)
                    {
                        ++GObstaclePickDebugLogCount;
                        const FString AssetPath = !Variation.Mesh.IsNull()
                            ? Variation.Mesh.ToString()
                            : Variation.BlueprintPrefab.ToString();
                        const FVector VarOffset = Variation.Offset.GetLocation();
                        UE_LOG(
                            LogTemp,
                            Display,
                            TEXT("[RaidRoom][PickDebug] Node=%d Theme=%s Env=%s MeshType=%d Key=%s Asset=%s VarOffset=(%.1f,%.1f,%.1f)"),
                            NodeId,
                            *NodeRow.Theme,
                            *NodeRow.EnvType,
                            MeshType,
                            *LastPickedObstacleVariationKey,
                            *AssetPath,
                            VarOffset.X,
                            VarOffset.Y,
                            VarOffset.Z);
                    }
#endif
                    return &Variation;
                }
            }

            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (CanSpawnObstacleVariation(Variation))
                {
                    LastPickedObstacleVariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
                    ObstacleVariationPickCounts.FindOrAdd(LastPickedObstacleVariationKey) += 1;
                    return &Variation;
                }
            }

            LastPickedObstacleVariationKey.Reset();
            return nullptr;
        };

    auto ResolveMeshBaseLift = [&](const FMeshVariation& VariationToMeasure) -> float
        {
            if (VariationToMeasure.Mesh.IsNull()) return 50.0f;

            const FSoftObjectPath MeshPath = VariationToMeasure.Mesh.ToSoftObjectPath();
            if (const float* Found = MeshBaseLiftCache.Find(MeshPath))
            {
                return *Found;
            }

            float Lift = 50.0f;
            if (UStaticMesh* Mesh = VariationToMeasure.Mesh.LoadSynchronous())
            {
                const FBoxSphereBounds Bounds = Mesh->GetBounds();
                Lift = -(Bounds.Origin.Z - Bounds.BoxExtent.Z);
            }
            MeshBaseLiftCache.Add(MeshPath, Lift);
            return Lift;
        };

    // 기본 도형을 스폰하는 람다 헬퍼 함수
    auto SpawnBox = [&](const FVector& Loc, const FVector& Scale, float Yaw, int32 MeshType, EObstacleSpawnFailReason* OutFailReason = nullptr) -> bool {
        if (OutFailReason)
        {
            *OutFailReason = EObstacleSpawnFailReason::None;
        }

        FMeshVariation V;
        bool bUsesThemeVariation = false;
        if (const FMeshVariation* ThemeVar = PickThemeVariationForMeshType(MeshType))
        {
            V = *ThemeVar;
            bUsesThemeVariation = true;
        }
        else if (bHasTraversalMeshOverride)
        {
            V.Mesh = TraversalMeshOverride;
            V.Offset = FTransform::Identity;
        }
        else
        {
            // Do not spawn a hardcoded fallback cube.
            if (OutFailReason)
            {
                *OutFailReason = EObstacleSpawnFailReason::NoVariation;
            }
            return false;
        }

        FVector BaseScale = V.Offset.GetScale3D();
        if (BaseScale.IsNearlyZero()) BaseScale = FVector(1.0f, 1.0f, 1.0f);
        if (bUsesThemeVariation && bPreserveThemeMeshScaleInTraversalKit)
        {
            // Theme mesh variations should keep their authored scale by default.
            V.Offset.SetScale3D(BaseScale);
        }
        else
        {
            V.Offset.SetScale3D(BaseScale * Scale);
        }

        FVector SpawnLoc = Loc;
        const float CubeBaselineLift = 50.0f * Scale.Z;
        const float MeshBaseLift = ResolveMeshBaseLift(V);
        const float ActualLift = MeshBaseLift * V.Offset.GetScale3D().Z;
        SpawnLoc.Z += (ActualLift - CubeBaselineLift);

        auto CountInstancesForMeshType = [&](int32 InMeshType) -> int32
            {
                const FName MeshTypeTag(*FString::Printf(TEXT("MeshType_%d"), InMeshType));
                int32 TotalCount = 0;
                for (UHierarchicalInstancedStaticMeshComponent* ISMC : DynamicISMC_Pool)
                {
                    if (!IsValid(ISMC))
                    {
                        continue;
                    }
                    if (!ISMC->ComponentTags.Contains(MeshTypeTag))
                    {
                        continue;
                    }
                    TotalCount += ISMC->GetInstanceCount();
                }
                return TotalCount;
            };

        const bool bIsObstacleMeshType = (MeshType == 2);
        const bool bIsDecorationMeshType = (MeshType == 3);
        const FString SelectedObstacleVariationKey = bIsObstacleMeshType ? LastPickedObstacleVariationKey : FString();
        if (bIsObstacleMeshType)
        {
            const float BaseSpacing = !V.BlueprintPrefab.IsNull() ? BlueprintObstacleMinSpacing : ObstacleMinSpacing;
            const float ScaleFactor = FMath::Clamp(FMath::Max(V.Offset.GetScale3D().X, V.Offset.GetScale3D().Y), 0.6f, 4.0f);
            float MinSpacing = BaseSpacing * ScaleFactor;
            if (!V.Mesh.IsNull())
            {
                if (UStaticMesh* CandidateMesh = V.Mesh.LoadSynchronous())
                {
                    const FVector MeshExtent = CandidateMesh->GetBoundingBox().GetExtent() * V.Offset.GetScale3D().GetAbs();
                    const float MeshRequiredSpacing = FMath::Max(MeshExtent.X, MeshExtent.Y) * 1.8f;
                    MinSpacing = FMath::Max(MinSpacing, MeshRequiredSpacing);
                }
            }
            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                for (const FVector& OccupiedLoc : OccupiedObstacleLocations)
                {
                    if (FVector::DistSquaredXY(OccupiedLoc, SpawnLoc) < FMath::Square(MinSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }
            }

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                const FVector CandidateWorldLoc = GetActorTransform().TransformPosition(SpawnLoc);
                const bool bCandidateIsBlueprintObstacle = !V.BlueprintPrefab.IsNull();
                for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
                {
                    if (!IsValid(ExistingActor) || !ExistingActor->ActorHasTag(TEXT("MeshType_2")))
                    {
                        continue;
                    }

                    float RequiredSpacing = MinSpacing;
                    if (bCandidateIsBlueprintObstacle || ExistingActor->ActorHasTag(TEXT("ObstacleBlueprint")))
                    {
                        RequiredSpacing = FMath::Max(RequiredSpacing, BlueprintObstacleMinSpacing);
                    }

                    if (FVector::DistSquaredXY(ExistingActor->GetActorLocation(), CandidateWorldLoc) < FMath::Square(RequiredSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }

                if (UWorld* World = GetWorld())
                {
                    FCollisionObjectQueryParams ObjQuery;
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidObstacleOverlapSpacing), false);
                    QueryParams.bTraceComplex = false;

                    TArray<FOverlapResult> Overlaps;
                    if (World->OverlapMultiByObjectType(
                        Overlaps,
                        CandidateWorldLoc,
                        FQuat::Identity,
                        ObjQuery,
                        FCollisionShape::MakeSphere(MinSpacing * (bCandidateIsBlueprintObstacle ? 0.85f : 0.55f)),
                        QueryParams))
                    {
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                            const AActor* HitActor = Overlap.GetActor();
                            if (!IsValid(HitComp))
                            {
                                continue;
                            }

                            const bool bObstacleComponent =
                                HitComp->ComponentTags.Contains(TEXT("MeshType_2")) ||
                                HitComp->ComponentTags.Contains(TEXT("ObstacleBlueprint"));
                            const bool bObstacleActor = IsValid(HitActor) && HitActor->ActorHasTag(TEXT("MeshType_2"));
                            if (bObstacleComponent || bObstacleActor)
                            {
                                if (OutFailReason)
                                {
                                    *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                                }
                                return false;
                            }
                        }
                    }
                }
            }
        }
        else if (bIsDecorationMeshType)
        {
            const bool bUrbanStyledRoom =
                NodeRow.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase) ||
                NodeRow.Theme.Equals(TEXT("Urban"), ESearchCase::IgnoreCase);
            const float UrbanSpacingScale = bUrbanStyledRoom
                ? FMath::Clamp(UrbanDecorationSpacingMultiplier, 1.0f, 3.0f)
                : 1.0f;

            const bool bCandidateIsBlueprintDecoration = !V.BlueprintPrefab.IsNull();
            const float BaseSpacing = bCandidateIsBlueprintDecoration ? BlueprintDecorationMinSpacing : DecorationMinSpacing;
            const float ScaleFactor = FMath::Clamp(FMath::Max(V.Offset.GetScale3D().X, V.Offset.GetScale3D().Y), 0.6f, 4.5f);
            float MinSpacing = BaseSpacing * ScaleFactor * UrbanSpacingScale;

            if (!V.Mesh.IsNull())
            {
                if (UStaticMesh* CandidateMesh = V.Mesh.LoadSynchronous())
                {
                    const FVector MeshExtent = CandidateMesh->GetBoundingBox().GetExtent() * V.Offset.GetScale3D().GetAbs();
                    const float MeshRequiredSpacing = FMath::Max(MeshExtent.X, MeshExtent.Y) * 1.35f;
                    MinSpacing = FMath::Max(MinSpacing, MeshRequiredSpacing);
                }
            }

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                for (const FVector& OccupiedLoc : OccupiedDecorationLocations)
                {
                    if (FVector::DistSquaredXY(OccupiedLoc, SpawnLoc) < FMath::Square(MinSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }
            }

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                const FVector CandidateWorldLoc = GetActorTransform().TransformPosition(SpawnLoc);
                for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
                {
                    if (!IsValid(ExistingActor) || !ExistingActor->ActorHasTag(TEXT("MeshType_3")))
                    {
                        continue;
                    }

                    if (FVector::DistSquaredXY(ExistingActor->GetActorLocation(), CandidateWorldLoc) < FMath::Square(MinSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }

                if (UWorld* World = GetWorld())
                {
                    FCollisionObjectQueryParams ObjQuery;
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidDecorationOverlapSpacing), false);
                    QueryParams.bTraceComplex = false;

                    TArray<FOverlapResult> Overlaps;
                    if (World->OverlapMultiByObjectType(
                        Overlaps,
                        CandidateWorldLoc,
                        FQuat::Identity,
                        ObjQuery,
                        FCollisionShape::MakeSphere(MinSpacing * 0.45f),
                        QueryParams))
                    {
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                            const AActor* HitActor = Overlap.GetActor();
                            if (!IsValid(HitComp))
                            {
                                continue;
                            }

                            const bool bDecorationComponent = HitComp->ComponentTags.Contains(TEXT("MeshType_3"));
                            const bool bDecorationActor = IsValid(HitActor) && HitActor->ActorHasTag(TEXT("MeshType_3"));
                            if (bDecorationComponent || bDecorationActor)
                            {
                                if (OutFailReason)
                                {
                                    *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                                }
                                return false;
                            }
                        }
                    }
                }
            }
        }

        UMaterialInterface* MaterialOverrideToUse = nullptr;
        if (!bUsesThemeVariation && !V.Mesh.IsNull())
        {
            const FString MeshPath = V.Mesh.ToSoftObjectPath().ToString();
            if (MeshPath.StartsWith(TEXT("/Engine/")))
            {
                MaterialOverrideToUse = TraversalMat;
            }
        }

        const int32 FootprintCountBeforeSpawn = SpawnedObstacleFootprints.Num();
        const bool bStaticMeshVariation = V.BlueprintPrefab.IsNull();
        const int32 MeshTypeInstanceCountBeforeSpawn = bStaticMeshVariation ? CountInstancesForMeshType(MeshType) : 0;
        AActor* SpawnedActor = AddMeshInstance(V, FTransform(FRotator(0.0f, Yaw, 0.0f), SpawnLoc), MeshType, MaterialOverrideToUse);
        const bool bSpawnedByFootprintDelta = SpawnedObstacleFootprints.Num() > FootprintCountBeforeSpawn;
        const int32 MeshTypeInstanceCountAfterSpawn = bStaticMeshVariation ? CountInstancesForMeshType(MeshType) : MeshTypeInstanceCountBeforeSpawn;
        const bool bSpawnedByInstanceDelta = bStaticMeshVariation && (MeshTypeInstanceCountAfterSpawn > MeshTypeInstanceCountBeforeSpawn);
        const bool bLikelySpawned = (SpawnedActor != nullptr) || bSpawnedByFootprintDelta || bSpawnedByInstanceDelta;
        if (bIsObstacleMeshType && bLikelySpawned)
        {
            const FVector OccupiedLoc = SpawnedActor
                ? GetActorTransform().InverseTransformPosition(SpawnedActor->GetActorLocation())
                : SpawnLoc;
            OccupiedObstacleLocations.Add(OccupiedLoc);
            if (!SelectedObstacleVariationKey.IsEmpty())
            {
                ObstacleVariationPlacedCounts.FindOrAdd(SelectedObstacleVariationKey) += 1;
            }
            if (!V.BlueprintPrefab.IsNull())
            {
                ++SpawnedBlueprintObstacleCount;
            }
        }
        else if (bIsDecorationMeshType && bLikelySpawned)
        {
            const FVector OccupiedLoc = SpawnedActor
                ? GetActorTransform().InverseTransformPosition(SpawnedActor->GetActorLocation())
                : SpawnLoc;
            OccupiedDecorationLocations.Add(OccupiedLoc);
        }
        if (!bLikelySpawned && OutFailReason)
        {
            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
        }
        return bLikelySpawned;
        };

    // EnvType + Theme + NodeTags를 함께 읽어 오픈월드/실내전투 스타일을 판정한다.
    const FString Env = NodeRow.EnvType;
    const FString Meta = (NodeRow.EnvType + TEXT(" ") + NodeRow.Theme + TEXT(" ") + NodeRow.NodeTags + TEXT(" ") + NodeRow.RoomRole).ToLower();
    const bool bForceIndoor =
        Meta.Contains(TEXT("tarkov")) ||
        Meta.Contains(TEXT("cqb")) ||
        Meta.Contains(TEXT("indoor")) ||
        Meta.Contains(TEXT("factory")) ||
        Meta.Contains(TEXT("warehouse")) ||
        Meta.Contains(TEXT("mall")) ||
        Meta.Contains(TEXT("실내")) ||
        Meta.Contains(TEXT("타르코프"));
    const bool bForceOutdoor =
        Meta.Contains(TEXT("openworld")) ||
        Meta.Contains(TEXT("open world")) ||
        Meta.Contains(TEXT("outdoor")) ||
        Meta.Contains(TEXT("오픈월드")) ||
        Meta.Contains(TEXT("야외"));
    const bool bEnvOutdoor =
        Env.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
        Env.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
        Env.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
        NodeRow.Theme.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
    const bool bIsOpenWorld = bForceOutdoor || (!bForceIndoor && bEnvOutdoor);
    const float Half = RoomRadius - 200.0f; // 외곽 여백

    // =========================================================================
    // 🌳 [TRACK 1: 오픈월드/자연] 외벽 없음, 규칙 없음, 무작위 산포(Scatter)
    // =========================================================================
    if (bIsOpenWorld)
    {
        int32 ScatterCount = (GridSize >= 21) ? 12 : ((GridSize >= 13) ? 7 : 4);
        ScatterCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 6.0f);
        ScatterCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6) - 1;
        const int32 TargetScatterCount = FMath::Max(0, FMath::RoundToInt(ScatterCount * ObstacleSpawnCountScale));
        const int32 MaxAttempts = FMath::Max(TargetScatterCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetScatterCount);
        int32 PlacedScatterCount = 0;
        int32 AttemptedScatterCount = 0;
        ObstacleTargetCount += TargetScatterCount;
        while (PlacedScatterCount < TargetScatterCount && AttemptedScatterCount < MaxAttempts)
        {
            ++AttemptedScatterCount;
            ++ObstacleAttemptCount;
            FVector RandomLoc(Rng.FRandRange(-Half + 300, Half - 300), Rng.FRandRange(-Half + 300, Half - 300), 0.0f);
            if (RandomLoc.Size2D() < 800.0f)
            {
                ++ObstacleSkipCenterCount;
                continue; // 중앙은 교전을 위해 비워둠
            }

            float ScaleX = Rng.FRandRange(1.5f, 3.5f);
            float ScaleY = Rng.FRandRange(1.5f, 3.5f);
            float Height = Rng.FRandRange(1.5f, 4.0f);
            float RandomYaw = Rng.FRandRange(0.0f, 360.0f); // 360도 무작위 각도
            EObstacleSpawnFailReason FailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(RandomLoc + FVector(0, 0, Height * 50.f), FVector(ScaleX, ScaleY, Height), RandomYaw, 2, &FailReason))
            {
                ++PlacedScatterCount;
                ++ObstaclePlacedCount;
            }
            else if (FailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
            }
            else if (FailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
            }
        }
    }
    // =========================================================================
    // 🏢 [TRACK 2: 현대 전술 CQB] 밀봉된 외벽 + 스마트 문 뚫기 + 전술 엄폐물
    // =========================================================================
    else
    {
        float WallThickness = 0.5f; // 벽 두께 50cm
        float WallHeight = 4.0f;    // 층고 4m
        float DoorWidth = FMath::Lerp(2.4f, 3.8f, FMath::Clamp(NodeRow.EnterableBuildingRatio, 0.0f, 1.0f)); // 데이터 기반 문 폭

        // 스마트 벽 깎기 알고리즘 (Smart Edge Carving)
        auto BuildSmartWall = [&](bool bHasDoor, FVector CenterOffset, float Yaw) {
            if (!bHasDoor) {
                // 꽉 막힌 솔리드 벽 스폰
                SpawnBox(CenterOffset + FVector(0, 0, WallHeight * 50.0f), FVector((Half * 2.0f) / 100.0f, WallThickness, WallHeight), Yaw, 1);
            }
            else {
                // 문이 있는 경우, 양옆으로 벽을 쪼개서 스폰하고 중앙을 비움!
                float SideWidth = ((Half * 2.0f) - (DoorWidth * 100.0f)) / 2.0f;
                float LeftOffset = DoorWidth * 50.0f + SideWidth / 2.0f;

                FTransform BaseWallTrans(FRotator(0, Yaw, 0), CenterOffset);
                FVector LeftLoc = BaseWallTrans.TransformPosition(FVector(-LeftOffset, 0, WallHeight * 50.0f));
                FVector RightLoc = BaseWallTrans.TransformPosition(FVector(LeftOffset, 0, WallHeight * 50.0f));

                SpawnBox(LeftLoc, FVector(SideWidth / 100.0f, WallThickness, WallHeight), Yaw, 1);
                SpawnBox(RightLoc, FVector(SideWidth / 100.0f, WallThickness, WallHeight), Yaw, 1);

                // 문 위쪽 헤더(인방) 막아주기 (문 높이는 3m, 층고는 4m이므로 위쪽 1m를 덮음)
                FVector HeaderLoc = BaseWallTrans.TransformPosition(FVector(0, 0, WallHeight * 100.0f - 50.0f));
                SpawnBox(HeaderLoc, FVector(DoorWidth, WallThickness, 1.0f), Yaw, 1);
            }
            };

        // 매니저가 지정해준 연결 방향에만 물리적으로 문을 뚫음
        BuildSmartWall(bDoorNorth, FVector(Half, 0, 0), 0.0f);
        BuildSmartWall(bDoorSouth, FVector(-Half, 0, 0), 0.0f);
        BuildSmartWall(bDoorEast, FVector(0, Half, 0), 90.0f);
        BuildSmartWall(bDoorWest, FVector(0, -Half, 0), 90.0f);

        // 내부 전술 엄폐물 세팅 (십자 도로 대신 L자 벽, 기둥 산개 배치)
        int32 CoverCount = (GridSize >= 15) ? 7 : 3;
        CoverCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6);
        CoverCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 4.0f);
        CoverCount = FMath::Clamp(CoverCount, 4, 20);
        const int32 TargetCoverCount = FMath::Max(0, FMath::RoundToInt(CoverCount * ObstacleSpawnCountScale));
        const int32 CoverMaxAttempts = FMath::Max(TargetCoverCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetCoverCount);
        int32 CoverAttempts = 0;
        int32 PlacedCoverCount = 0;
        ObstacleTargetCount += TargetCoverCount;
        while (PlacedCoverCount < TargetCoverCount && CoverAttempts < CoverMaxAttempts) {
            ++CoverAttempts;
            ++ObstacleAttemptCount;
            FVector CoverLoc(Rng.FRandRange(-Half + 400, Half - 400), Rng.FRandRange(-Half + 400, Half - 400), WallHeight * 50.0f);

            // 문 앞(중앙 크로스 라인)은 사격 통제선(Fatal Funnel)이므로 비워둠
            if (FMath::Abs(CoverLoc.X) < 400.0f || FMath::Abs(CoverLoc.Y) < 400.0f)
            {
                ++ObstacleSkipCenterCount;
                continue;
            }

            // 기둥 생성
            EObstacleSpawnFailReason MainFailReason = EObstacleSpawnFailReason::None;
            const bool bMainPlaced = SpawnBox(CoverLoc, FVector(1.0f, 1.0f, WallHeight), 0.0f, 2, &MainFailReason);
            if (bMainPlaced)
            {
                ++PlacedCoverCount;
                ++ObstaclePlacedCount;
            }
            else if (MainFailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
                continue;
            }
            else if (MainFailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
                continue;
            }

            // 50% 확률로 기둥 옆에 벽을 덧대어 L자형 사각지대(코너) 생성
            if (Rng.FRand() < 0.5f) {
                ++ObstacleAttemptCount;
                EObstacleSpawnFailReason SideFailReason = EObstacleSpawnFailReason::None;
                const bool bSidePlaced =
                    (Rng.FRand() < 0.5f)
                    ? SpawnBox(CoverLoc + FVector(150.f, 0, 0), FVector(2.0f, 0.5f, WallHeight), 0.f, 2, &SideFailReason)
                    : SpawnBox(CoverLoc + FVector(0, 150.f, 0), FVector(0.5f, 2.0f, WallHeight), 0.f, 2, &SideFailReason);
                if (bSidePlaced)
                {
                    ++ObstaclePlacedCount;
                }
                else if (SideFailReason == EObstacleSpawnFailReason::NoVariation)
                {
                    ++ObstacleNoVariationCount;
                }
                else if (SideFailReason == EObstacleSpawnFailReason::PlacementRejected)
                {
                    ++ObstaclePlacementRejectCount;
                }
            }
        }

        // 무작위 박스/책상/엄폐물 배치
        const int32 BasePropCount = FMath::Clamp(4 + NodeRow.TraversalLaneSeeds + FMath::RoundToInt(NodeRow.ObstacleDensity * 4.0f), 4, 14);
        const int32 TargetPropCount = FMath::Max(0, FMath::RoundToInt(BasePropCount * ObstacleSpawnCountScale));
        const int32 PropMaxAttempts = FMath::Max(TargetPropCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetPropCount);
        int32 PropAttempts = 0;
        int32 PlacedPropCount = 0;
        ObstacleTargetCount += TargetPropCount;
        while (PlacedPropCount < TargetPropCount && PropAttempts < PropMaxAttempts) {
            ++PropAttempts;
            ++ObstacleAttemptCount;
            FVector BoxLoc(Rng.FRandRange(-Half + 300, Half - 300), Rng.FRandRange(-Half + 300, Half - 300), 50.0f);
            if (FMath::Abs(BoxLoc.X) < 300.0f || FMath::Abs(BoxLoc.Y) < 300.0f)
            {
                ++ObstacleSkipCenterCount;
                continue;
            }

            EObstacleSpawnFailReason PropFailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(BoxLoc, FVector(1.5f, 1.0f, 1.0f), Rng.FRandRange(0.f, 360.f), 2, &PropFailReason))
            {
                ++PlacedPropCount;
                ++ObstaclePlacedCount;
            }
            else if (PropFailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
            }
            else if (PropFailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
            }
        }
    }

    // Decoration pass: spawn theme decoration variations as room props.
    if (ThemeDecorationVariations.Num() > 0)
    {
        const bool bLootLikeRoom =
            CurrentRoomType == ERaidRoomType::Loot ||
            CurrentRoomType == ERaidRoomType::Start ||
            CurrentRoomType == ERaidRoomType::Exit;
        int32 BaseDecorationCount = bIsOpenWorld ? 8 : 5;
        if (bLootLikeRoom)
        {
            BaseDecorationCount += 3;
        }
        BaseDecorationCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6) - 1;
        BaseDecorationCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 4.0f);

        const int32 TargetDecorationCount = FMath::Max(0, FMath::RoundToInt((float)BaseDecorationCount * FMath::Max(0.4f, ObstacleSpawnCountScale)));
        const int32 DecorationMaxAttempts = FMath::Max(TargetDecorationCount * FMath::Max(2, ObstaclePlacementAttemptMultiplier / 2), TargetDecorationCount);
        int32 PlacedDecorationCount = 0;
        int32 DecorationAttempts = 0;

        while (PlacedDecorationCount < TargetDecorationCount && DecorationAttempts < DecorationMaxAttempts)
        {
            ++DecorationAttempts;
            FVector DecoLoc(
                Rng.FRandRange(-Half + 260.0f, Half - 260.0f),
                Rng.FRandRange(-Half + 260.0f, Half - 260.0f),
                45.0f);

            if (!bIsOpenWorld && (FMath::Abs(DecoLoc.X) < 220.0f || FMath::Abs(DecoLoc.Y) < 220.0f))
            {
                continue;
            }

            const float DecoScale = Rng.FRandRange(0.8f, 1.25f);
            EObstacleSpawnFailReason DecoFailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(
                DecoLoc,
                FVector(DecoScale, DecoScale, DecoScale),
                Rng.FRandRange(0.0f, 360.0f),
                3,
                &DecoFailReason))
            {
                ++PlacedDecorationCount;
            }
        }
    }

    if (bLogObstacleSpawnSummary && ObstacleAttemptCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Node=%d obstacle summary: target=%d attempts=%d placed=%d center-skip=%d no-variation=%d placement-reject=%d bp-placed=%d bp-cap=%d density=%.2f count-scale=%.2f sm-w-scale=%.2f bp-w-scale=%.2f"),
            NodeId,
            ObstacleTargetCount,
            ObstacleAttemptCount,
            ObstaclePlacedCount,
            ObstacleSkipCenterCount,
            ObstacleNoVariationCount,
            ObstaclePlacementRejectCount,
            SpawnedBlueprintObstacleCount,
            MaxBlueprintObstaclesPerRoom,
            NodeRow.ObstacleDensity,
            ObstacleSpawnCountScale,
            StaticMeshObstacleWeightScale,
            BlueprintObstacleWeightScale);
    }
    else if (bIsGameWorld && ObstacleTargetCount > 0 && ObstaclePlacedCount <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Obstacle placement stalled. Node=%d Theme=%s Env=%s target=%d attempts=%d no-variation=%d placement-reject=%d"),
            NodeId,
            *NodeRow.Theme,
            *NodeRow.EnvType,
            ObstacleTargetCount,
            ObstacleAttemptCount,
            ObstacleNoVariationCount,
            ObstaclePlacementRejectCount);
    }

    if (bLogObstacleVariationBreakdown && (ObstacleVariationPickCounts.Num() > 0 || ObstacleVariationConfiguredWeights.Num() > 0))
    {
        TArray<FString> VariationKeys;
        ObstacleVariationConfiguredWeights.GetKeys(VariationKeys);
        VariationKeys.Sort([&](const FString& A, const FString& B)
            {
                return ObstacleVariationPickCounts.FindRef(A) > ObstacleVariationPickCounts.FindRef(B);
            });

        TArray<FString> BreakdownParts;
        for (const FString& Key : VariationKeys)
        {
            const int32 PickedCount = ObstacleVariationPickCounts.FindRef(Key);
            const int32 PlacedCount = ObstacleVariationPlacedCounts.FindRef(Key);
            const float ConfiguredWeight = ObstacleVariationConfiguredWeights.FindRef(Key);
            const float EffectiveWeight = ObstacleVariationEffectiveWeights.FindRef(Key);
            if (PickedCount <= 0 && PlacedCount <= 0 && ConfiguredWeight <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            BreakdownParts.Add(FString::Printf(
                TEXT("%s{cfg=%.2f eff=%.2f pick=%d place=%d}"),
                *Key,
                ConfiguredWeight,
                EffectiveWeight,
                PickedCount,
                PlacedCount));

            if (BreakdownParts.Num() >= 12)
            {
                break;
            }
        }

        if (BreakdownParts.Num() > 0)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Node=%d obstacle variation breakdown: %s"),
                NodeId,
                *FString::Join(BreakdownParts, TEXT(" | ")));
        }
    }
}

void ARaidRoomActor::InternalSpawnLoot()
{
    if (bLootAlreadySpawned) return; bLootAlreadySpawned = true;
    if (ChapterConfigRef && ChapterConfigRef->LootRegistry)
    {
        int32 LCount = NodeRow.LootCount > 0 ? NodeRow.LootCount : 3; FVector CenterLoc = GetActorLocation();
        bool bIsCentral = NodeRow.LootStrategy.Equals(TEXT("Central_Cache"), ESearchCase::IgnoreCase);
        float MinDistance = bIsCentral ? 100.0f : 300.0f; float MaxDistance = bIsCentral ? 250.0f : ((GridSize * TileSize) / 2.0f - 200.0f); float AngleStep = 360.0f / (float)FMath::Max(1, LCount);

        for (int32 i = 0; i < LCount; ++i)
        {
            if (const FRaidLootCandidate* Candidate = ChapterConfigRef->LootRegistry->GetRandomCandidate(NodeRow.LootLevel))
            {
                if (Candidate->ItemClass)
                {
                    float Radian = FMath::DegreesToRadians(i * AngleStep + RoomRandomStream.FRandRange(-20.0f, 20.0f)); float Distance = RoomRandomStream.FRandRange(MinDistance, MaxDistance);
                    FVector Offset(FMath::Cos(Radian) * Distance, FMath::Sin(Radian) * Distance, 0.0f);
                    FVector StartLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, 1000.0f); FVector EndLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, -500.0f);

                    FHitResult HitResult; FCollisionQueryParams QueryParams; QueryParams.bTraceComplex = true; QueryParams.AddIgnoredActor(this);
                    FVector FinalSpawnLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, 150.0f);
                    FRotator FinalRotation = FRotator(0.0f, RoomRandomStream.FRandRange(0.0f, 360.0f), 0.0f);

                    if (TryResolveSingleGroundHitAlongTrace(GetWorld(), StartLoc, EndLoc, false, QueryParams, HitResult)) {
                        FinalSpawnLoc = HitResult.ImpactPoint + FVector(0.0f, 0.0f, 10.0f); // 바닥에서 살짝 위
                        FRotator AlignedRot = FRotationMatrix::MakeFromZX(HitResult.ImpactNormal, FVector(FMath::Cos(FinalRotation.Yaw), FMath::Sin(FinalRotation.Yaw), 0.0f)).Rotator();
                        FinalRotation = (Candidate->Category == ERaidLootCategory::Rifle || Candidate->Category == ERaidLootCategory::Pistol) ? (AlignedRot.Quaternion() * FRotator(90.0f, 0.0f, 0.0f).Quaternion()).Rotator() : AlignedRot;
                    }

                    const FTransform SpawnTransform(FinalRotation, FinalSpawnLoc);
                    if (AActor* SpawnedItem = GetWorld()->SpawnActorDeferred<AActor>(Candidate->ItemClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
                    {
                        int32 AppliedRowNameOverrideFields = 0;
                        if (!Candidate->DataRowName.IsNone())
                        {
                            AppliedRowNameOverrideFields += ApplyLootDataRowNameOverrideToObject(SpawnedItem, Candidate->DataRowName);
                            TInlineComponentArray<UActorComponent*> PreFinishComponents(SpawnedItem);
                            for (UActorComponent* Component : PreFinishComponents)
                            {
                                AppliedRowNameOverrideFields += ApplyLootDataRowNameOverrideToObject(Component, Candidate->DataRowName);
                            }
                        }

                        FRaidLootRuntimeRowData ResolvedLootRowData;
                        int32 AppliedLootFields = AppliedRowNameOverrideFields;
                        if (bApplyLootDataTableValuesAtSpawn)
                        {
                            if (ResolveLootRuntimeRowForSpawnedClass(Candidate, SpawnedItem->GetClass(), ResolvedLootRowData))
                            {
                                AppliedLootFields += ApplyLootRuntimeRowDataToActor(
                                    SpawnedItem,
                                    ResolvedLootRowData,
                                    bApplyLootParamValuesAtSpawn,
                                    bApplyLootQuantityValuesAtSpawn,
                                    bApplyLootPickupRestrictionValuesAtSpawn,
                                    Candidate->DataRowName.IsNone());
                                if (bLogLootDataBinding)
                                {
                                    UE_LOG(
                                        LogTemp,
                                        Warning,
                                        TEXT("[RaidRoom] LootDataBinding Room=%d Item=%s Row=%s AppliedFields=%d Param1=%.2f Param2=%.2f CurrentQty=%.2f MaxQty=%.2f"),
                                        NodeId,
                                        *GetNameSafe(SpawnedItem),
                                        *ResolvedLootRowData.RowName,
                                        AppliedLootFields,
                                        ResolvedLootRowData.Param1,
                                        ResolvedLootRowData.Param2,
                                        ResolvedLootRowData.CurrentQuantity,
                                        ResolvedLootRowData.MaxQuantity);
                                }
                            }
                            else if (bLogLootDataBinding)
                            {
                                UE_LOG(
                                    LogTemp,
                                    Warning,
                                    TEXT("[RaidRoom] LootDataBinding row not found for item class: %s (RequestedRow=%s, AppliedRowOverrideFields=%d)"),
                                    *GetNameSafe(Candidate->ItemClass.Get()),
                                    *Candidate->DataRowName.ToString(),
                                    AppliedRowNameOverrideFields);
                            }
                        }

                        UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);

                        if (!Candidate->DataRowName.IsNone())
                        {
                            ApplyLootDataRowNameOverrideToObject(SpawnedItem, Candidate->DataRowName);
                            TInlineComponentArray<UActorComponent*> PostFinishComponents(SpawnedItem);
                            for (UActorComponent* Component : PostFinishComponents)
                            {
                                ApplyLootDataRowNameOverrideToObject(Component, Candidate->DataRowName);
                            }
                        }

                        // 엔진이 위치를 비키면서 공중에 띄워버렸을 경우를 대비해, 다시 바닥으로 내려주는 안전장치
                        FHitResult GroundHit;
                        if (TryResolveSingleGroundHitAlongTrace(
                            GetWorld(),
                            SpawnedItem->GetActorLocation(),
                            SpawnedItem->GetActorLocation() - FVector(0.0f, 0.0f, 500.0f),
                            false,
                            QueryParams,
                            GroundHit))
                        {
                            SpawnedItem->SetActorLocation(
                                GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 5.0f),
                                false,
                                nullptr,
                                ETeleportType::TeleportPhysics);
                        }

                        SetLootActorProximityFx(SpawnedItem, false);
                        SetLootActorOutline(SpawnedItem, false);
                        SetLootActorDotWidget(SpawnedItem, false);
                        SpawnedLootActors.AddUnique(SpawnedItem);
                    }
                }
            }
        }
    }
    StatusText->SetTextRenderColor(FColor::Green); StatusText->SetText(FText::FromString(TEXT("CLEARED!"))); OpenRoom();
}

void ARaidRoomActor::SetCombatCleared(bool bCleared) { bCombatCleared = bCleared; if (bCleared) { StatusText->SetTextRenderColor(FColor::Green); StatusText->SetText(FText::FromString(TEXT("CLEARED!"))); OpenRoom(); } }
void ARaidRoomActor::OpenRoom() { for (AActor* Door : SpawnedDoorActors) { if (IsValid(Door)) Door->Destroy(); } SpawnedDoorActors.Empty(); }
FVector ARaidRoomActor::GetRoomExtent() const { return FVector((GridSize * TileSize) / 2.0f); }
bool ARaidRoomActor::TryShowRegionBanner(APawn* OverlappingPawn)
{
    if (bEntryBannerShown)
    {
        bPendingBannerRetry = false;
        return true;
    }

    if (!OverlappingPawn || !OverlappingPawn->IsPlayerControlled())
    {
        return false;
    }

    UWorld* World = GetWorld();
    const double NowSeconds = World ? World->GetTimeSeconds() : 0.0;

    APlayerController* PC = Cast<APlayerController>(OverlappingPawn->GetController());
    if (!PC)
    {
        NextBannerAttemptTimeSeconds = NowSeconds + 0.08;
        return false;
    }

    if (!PC->IsLocalController())
    {
        NextBannerAttemptTimeSeconds = NowSeconds + 0.25;
        return false;
    }

    FString TitleStr = ResolveBannerTitleFromNodeTags(NodeRow);
    FString SubStr = ResolveBannerSubtitleFromRoomType(NodeRow);
    if (TitleStr.IsEmpty() || TitleStr.Contains(TEXT("[")))
    {
        TitleStr = TEXT("미확인 구역 (Unknown Sector)");
    }
    if (SubStr.IsEmpty())
    {
        SubStr = TEXT("구역 진입");
    }

    if (URaidCombatSubsystem* CombatSubsystem = World ? World->GetSubsystem<URaidCombatSubsystem>() : nullptr)
    {
        CombatSubsystem->EnqueueRegionBannerMessage(FText::FromString(TitleStr), FText::FromString(SubStr), 4.0f, true);
        bEntryBannerShown = true;
        bPendingBannerRetry = false;
        NextBannerAttemptTimeSeconds = 0.0;
        return true;
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (World && SharedWidget->GetWorld() != World)
        {
            if (SharedWidget->IsInViewport())
            {
                SharedWidget->RemoveFromParent();
            }
            GSharedRegionBannerWidget.Reset();
        }
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
        NextBannerAttemptTimeSeconds = NowSeconds + 0.30;
        return false;
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        ActiveRegionBannerWidget = SharedWidget;
    }
    else
    {
        ActiveRegionBannerWidget = CreateWidget<URaidRegionBannerWidget>(PC, WidgetClass);
        if (!ActiveRegionBannerWidget)
        {
            NextBannerAttemptTimeSeconds = NowSeconds + 0.20;
            return false;
        }
        GSharedRegionBannerWidget = ActiveRegionBannerWidget;
    }

    bEntryBannerShown = true;
    bPendingBannerRetry = false;
    NextBannerAttemptTimeSeconds = 0.0;
    if (!ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->AddToViewport(25);
    }
    ActiveRegionBannerWidget->ShowRegionTitle(FText::FromString(TitleStr), FText::FromString(SubStr), 4.0f);

    if (UWorld* WorldPtr = GetWorld())
    {
        WorldPtr->GetTimerManager().ClearTimer(RegionBannerHideTimerHandle);
        WorldPtr->GetTimerManager().SetTimer(
            RegionBannerHideTimerHandle,
            FTimerDelegate::CreateLambda([]()
                {
                    if (URaidRegionBannerWidget* Widget = GSharedRegionBannerWidget.Get())
                    {
                        if (Widget->IsInViewport())
                        {
                            Widget->RemoveFromParent();
                        }
                    }
                }),
            5.5f,
            false);
    }

    return true;
}
void ARaidRoomActor::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (APawn* OverlappingPawn = Cast<APawn>(OtherActor)) {
        if (OverlappingPawn->IsPlayerControlled())
        {
            if (IsPawnInsideRoomBannerZone(this, OverlappingPawn))
            {
                bPendingBannerRetry = !TryShowRegionBanner(OverlappingPawn);
            }
        }
        if (OverlappingPawn->IsPlayerControlled() && !bCombatStarted && !bCombatCleared) {
            if (URaidCombatSubsystem* CombatSubsystem = GetWorld()->GetSubsystem<URaidCombatSubsystem>()) { CombatSubsystem->StartCombatForRoom(this); Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
        }
    }
}

void ARaidRoomActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (!ChapterConfigRef)
    {
        ChapterConfigRef = ChapterConfigAsset.Get();
        if (!ChapterConfigRef && !ChapterConfigAsset.IsNull())
        {
            ChapterConfigRef = ChapterConfigAsset.LoadSynchronous();
        }
    }

    if (!bNodeDataInitialized)
    {
        const bool bLooksLikeSerializedNode =
            NodeId > 0 ||
            NodeRow.NodeId > 0 ||
            !NodeRow.RoomType.IsEmpty() ||
            !NodeRow.Theme.IsEmpty() ||
            !NodeRow.NodeTags.IsEmpty();

        if (bLooksLikeSerializedNode)
        {
            if (NodeId <= 0 && NodeRow.NodeId > 0)
            {
                NodeId = NodeRow.NodeId;
            }
            if (NodeRow.NodeId <= 0 && NodeId > 0)
            {
                NodeRow.NodeId = NodeId;
            }

            CurrentRoomType = RaidRoomParsing::ParseRoomType(NodeRow.RoomType);
            ApplyGridSizeFromRoomSizeToken(NodeRow.RoomSize, GridSize);
            RoomRandomStream.Initialize(NodeRow.Seed);
            bNodeDataInitialized = true;
            bLootAlreadySpawned = false;
            bEntryBannerShown = false;
            bPendingBannerRetry = false;
            bWasPlayerInsideBannerZone = false;
            NextBannerAttemptTimeSeconds = 0.0;
            NextLootOutlineUpdateTimeSeconds = 0.0;
            CachedProximityAutoStartDistanceUU = -1.0f;
        }
    }

    // 🔥 1. 트리거(마커 감지 영역)를 오픈월드 스케일에 맞게 거대하게 확장
    if (Trigger)
    {
        float RoomRadius = (GridSize * TileSize) / 2.0f;
        Trigger->SetBoxExtent(FVector(RoomRadius, RoomRadius, 10000.0f));
        Trigger->SetRelativeLocation(FVector(0.0f, 0.0f, 2000.0f));
    }

    // Runtime(PIE/Game)에서는 RaidLayoutManager가 한 번에 생성/정리하므로
    // OnConstruction 재생성은 중복 스폰/프레임 급락을 유발한다.
    if (UWorld* World = GetWorld())
    {
        const bool bEditorPreviewWorld =
            World->WorldType == EWorldType::Editor ||
            World->WorldType == EWorldType::EditorPreview;
        if (!bEditorPreviewWorld)
        {
            return;
        }
    }

    // Editor 프리뷰 월드에서만 레이아웃 미리보기 생성.
    GenerateRoomLayout();
}

