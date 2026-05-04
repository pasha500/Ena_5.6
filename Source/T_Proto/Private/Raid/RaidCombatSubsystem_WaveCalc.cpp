#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

namespace
{
    bool IsLandscapeLikeWaveHit(const FHitResult& Hit)
    {
        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        if (!HitActor && !HitComp)
        {
            return false;
        }

        const FString ActorClass = HitActor ? HitActor->GetClass()->GetName() : FString();
        const FString CompClass = HitComp ? HitComp->GetClass()->GetName() : FString();
        return
            ActorClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            ActorClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase);
    }

    bool IsActorOwnedOrAttachedToRoomForWave(const AActor* CandidateActor, const ARaidRoomActor* Room)
    {
        if (!CandidateActor || !Room)
        {
            return false;
        }

        if (CandidateActor == Room || CandidateActor->GetOwner() == Room)
        {
            return true;
        }

        const AActor* AttachParent = CandidateActor->GetAttachParentActor();
        if (AttachParent == Room || (AttachParent && AttachParent->GetOwner() == Room))
        {
            return true;
        }

        return false;
    }

    bool IsRoomFloorWaveHit(const FHitResult& Hit, const ARaidRoomActor* SourceRoom)
    {
        if (!SourceRoom)
        {
            return false;
        }

        const AActor* HitActor = Hit.GetActor();
        if (!HitActor || !IsActorOwnedOrAttachedToRoomForWave(HitActor, SourceRoom))
        {
            return false;
        }

        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        return HitComp && HitComp->ComponentTags.Contains(TEXT("MeshType_0"));
    }

    ARaidRoomActor* ResolveWaveSourceRoomFromPlayer(
        const TMap<int32, TObjectPtr<ARaidRoomActor>>& RoomById,
        const FVector& PlayerLoc)
    {
        ARaidRoomActor* BestRoom = nullptr;
        float BestDistSq = TNumericLimits<float>::Max();

        for (const TPair<int32, TObjectPtr<ARaidRoomActor>>& Pair : RoomById)
        {
            ARaidRoomActor* Candidate = Pair.Value.Get();
            if (!IsValid(Candidate))
            {
                continue;
            }

            const FVector RoomLoc = Candidate->GetActorLocation();
            const FVector RoomExtent = Candidate->GetRoomExtent();
            const bool bInsideRoom2D =
                FMath::Abs(PlayerLoc.X - RoomLoc.X) <= RoomExtent.X &&
                FMath::Abs(PlayerLoc.Y - RoomLoc.Y) <= RoomExtent.Y;

            const float DistSq = bInsideRoom2D ? 0.0f : FVector::DistSquared2D(PlayerLoc, RoomLoc);
            if (!BestRoom || DistSq < BestDistSq)
            {
                BestRoom = Candidate;
                BestDistSq = DistSq;

                if (bInsideRoom2D)
                {
                    break;
                }
            }
        }

        return BestRoom;
    }
}

bool URaidCombatSubsystem::TryResolveWaveSpawnLocation(APawn* PlayerPawn, const FRaidWaveDefinition& WaveDef, float CapsuleRadius, float CapsuleHalfHeight, FVector& OutLocation) const
{
    OutLocation = FVector::ZeroVector;

    UWorld* World = GetWorld();
    if (!World || !IsValid(PlayerPawn))
    {
        return false;
    }

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
    const FVector PlayerLoc = PlayerPawn->GetActorLocation();
    const float MinDist = FMath::Max(300.0f, WaveDef.SpawnMinDistanceFromPlayer);
    const float MaxDist = FMath::Max(MinDist + 100.0f, WaveDef.SpawnMaxDistanceFromPlayer);
    const float ScatterRadius = FMath::Max(120.0f, WaveDef.SpawnScatterRadius);
    ARaidRoomActor* SourceRoom = ResolveWaveSourceRoomFromPlayer(RoomById, PlayerLoc);

    for (int32 Attempt = 0; Attempt < 48; ++Attempt)
    {
        const float Angle = FMath::FRandRange(0.0f, 360.0f);
        const float Dist = FMath::FRandRange(MinDist, MaxDist);
        const float Radian = FMath::DegreesToRadians(Angle);
        const FVector RingCenter = PlayerLoc + FVector(FMath::Cos(Radian) * Dist, FMath::Sin(Radian) * Dist, 0.0f);

        FVector CandidateXY = RingCenter;
        if (NavSys)
        {
            FNavLocation RandomNavLoc;
            if (NavSys->GetRandomReachablePointInRadius(RingCenter, ScatterRadius, RandomNavLoc))
            {
                CandidateXY = RandomNavLoc.Location;
            }
            else if (NavSys->ProjectPointToNavigation(RingCenter, RandomNavLoc, FVector(600.0f, 600.0f, 1600.0f)))
            {
                CandidateXY = RandomNavLoc.Location;
            }
        }

        FHitResult GroundHit;
        if (!TryResolveAIGroundHitInternal(World, SourceRoom, CandidateXY, GroundHit))
        {
            continue;
        }

        if (!GroundHit.bBlockingHit || IsWaterHitInternal(GroundHit))
        {
            continue;
        }

        if (GroundHit.ImpactNormal.Z < 0.55f)
        {
            continue;
        }

        const bool bRoomFloorHit = IsRoomFloorWaveHit(GroundHit, SourceRoom);
        const bool bLandscapeLikeHit = IsLandscapeLikeWaveHit(GroundHit);
        if (!bRoomFloorHit && !bLandscapeLikeHit)
        {
            continue;
        }

        const FVector CandidateLoc = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, CapsuleHalfHeight + 6.0f);
        if (SourceRoom && IsNearRoomObstacleInternal(World, SourceRoom, CandidateLoc, CapsuleRadius + 70.0f))
        {
            continue;
        }

        if (FVector::Dist2D(CandidateLoc, PlayerLoc) < MinDist * 0.75f)
        {
            continue;
        }
        if (IsCapsuleBlockedForPawnInternal(World, CandidateLoc, CapsuleRadius, CapsuleHalfHeight))
        {
            continue;
        }

        OutLocation = CandidateLoc;
        return true;
    }

    return false;
}
