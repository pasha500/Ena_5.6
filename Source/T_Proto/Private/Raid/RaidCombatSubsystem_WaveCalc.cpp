#include "Raid/RaidCombatSubsystem.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidWaveGroundTrace), false);
    QueryParams.bTraceComplex = false;
    QueryParams.AddIgnoredActor(PlayerPawn);

    for (int32 Attempt = 0; Attempt < 24; ++Attempt)
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

        const FVector TraceStart = CandidateXY + FVector(0.0f, 0.0f, 4500.0f);
        const FVector TraceEnd = CandidateXY - FVector(0.0f, 0.0f, 6500.0f);
        FHitResult GroundHit;
        if (!World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
        {
            continue;
        }
        if (!GroundHit.bBlockingHit || IsWaterHitInternal(GroundHit))
        {
            continue;
        }

        const FVector CandidateLoc = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, CapsuleHalfHeight + 6.0f);
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
