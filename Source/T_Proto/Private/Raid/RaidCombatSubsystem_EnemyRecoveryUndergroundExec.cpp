#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

bool URaidCombatSubsystem::RecoverEnemyIfOutOfWorld(APawn* EnemyPawn, int32 RoomId, bool& bOutNeedsCull)
{
    bOutNeedsCull = false;

    if (!bEnableUndergroundEnemyRecovery || !IsValid(EnemyPawn))
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    ARaidRoomActor* Room = nullptr;
    if (const TObjectPtr<ARaidRoomActor>* RoomPtr = RoomById.Find(RoomId))
    {
        Room = RoomPtr->Get();
    }
    if (!IsValid(Room) || Room->IsCleared())
    {
        return false;
    }

    const TWeakObjectPtr<APawn> WeakPawn(EnemyPawn);
    const double NowSeconds = World->GetTimeSeconds();
    const int32 ActiveEnemyCount = EnemyToRoomMap.Num();
    const double CheckInterval = ComputeRecoveryCheckIntervalInternal((double)UndergroundEnemyCheckInterval, ActiveEnemyCount);
    if (const double* NextCheck = EnemyUndergroundNextCheckByPawn.Find(WeakPawn))
    {
        if (*NextCheck > NowSeconds)
        {
            return false;
        }
    }
    EnemyUndergroundNextCheckByPawn.Add(WeakPawn, NowSeconds + CheckInterval);

    float CapsuleRadius = 42.0f;
    float CapsuleHalfHeight = 88.0f;
    if (const UCapsuleComponent* Capsule = EnemyPawn->FindComponentByClass<UCapsuleComponent>())
    {
        CapsuleRadius = FMath::Max(20.0f, Capsule->GetScaledCapsuleRadius());
        CapsuleHalfHeight = FMath::Max(40.0f, Capsule->GetScaledCapsuleHalfHeight());
    }

    bool bHasBrokenCollisionState = false;
    if (const UCapsuleComponent* Capsule = EnemyPawn->FindComponentByClass<UCapsuleComponent>())
    {
        bHasBrokenCollisionState |= (Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }
    if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(EnemyPawn->GetRootComponent()))
    {
        bHasBrokenCollisionState |= (RootPrimitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }

    if (bHasBrokenCollisionState)
    {
        ForceEnemyTraceCollisionInternal(EnemyPawn);
        if (ACharacter* Character = Cast<ACharacter>(EnemyPawn))
        {
            if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
            {
                if (MoveComp->MovementMode == MOVE_None)
                {
                    MoveComp->SetMovementMode(MOVE_Walking);
                }
            }
        }
    }

    const FVector PawnLoc = EnemyPawn->GetActorLocation();
    const FVector RoomLoc = Room->GetActorLocation();
    const float SafeTolerance = FMath::Max(120.0f, UndergroundEnemyZTolerance);

    FHitResult GroundHit;
    const bool bHasGroundHit = TryResolveAIGroundHitInternal(World, Room, PawnLoc, GroundHit);
    const float ExpectedPawnZ =
        bHasGroundHit
            ? (GroundHit.ImpactPoint.Z + CapsuleHalfHeight + 6.0f)
            : (RoomLoc.Z + CapsuleHalfHeight);

    const bool bBelowGroundSurface = bHasGroundHit && PawnLoc.Z < (ExpectedPawnZ - SafeTolerance);
    const bool bFarBelowRoomOrigin = PawnLoc.Z < (RoomLoc.Z - (SafeTolerance + 350.0f));
    const bool bLikelyUnderground = bBelowGroundSurface || bFarBelowRoomOrigin;

    if (!bLikelyUnderground)
    {
        EnemyUndergroundRecoveryFailuresByPawn.Remove(WeakPawn);
        EnemyLastKnownValidLocationByPawn.Add(WeakPawn, PawnLoc);
        return false;
    }

    LogTrackedEnemyState(EnemyPawn, RoomId, TEXT("UndergroundDetected"));

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
    const int32 SeedA = (int32)GetTypeHash(EnemyPawn);
    const int32 SeedB = (RoomId * 7919);
    const int32 SeedC = FMath::RoundToInt((float)NowSeconds * 1000.0f);
    FRandomStream RecoveryStream(SeedA ^ SeedB ^ SeedC);

    FVector RecoveryLoc = FVector::ZeroVector;
    auto TryRecoveryFromSeed = [&](const FVector& SeedLocation) -> bool
        {
            if (TryResolveSafeAIPawnSpawnLocationInternal(World, Room, NavSys, SeedLocation, CapsuleRadius, CapsuleHalfHeight, RecoveryLoc))
            {
                return true;
            }
            if (TryResolveNearbyFallbackSpawnLocationInternal(World, Room, NavSys, SeedLocation, CapsuleRadius, CapsuleHalfHeight, RecoveryStream, RecoveryLoc))
            {
                return true;
            }
            return false;
        };

    auto TryRecoveryFromRoomSeedWithMinDistance = [&](int32 AttemptCount, float MinDistance2D) -> bool
        {
            const FVector RoomExtent = Room->GetRoomExtent();
            const int32 Attempts = FMath::Max(1, AttemptCount);
            const float MinDistSq = FMath::Square(FMath::Max(0.0f, MinDistance2D));
            for (int32 AttemptIndex = 0; AttemptIndex < Attempts; ++AttemptIndex)
            {
                const FVector RoomSeed =
                    RoomLoc +
                    FVector(
                        RecoveryStream.FRandRange(-RoomExtent.X * 0.70f, RoomExtent.X * 0.70f),
                        RecoveryStream.FRandRange(-RoomExtent.Y * 0.70f, RoomExtent.Y * 0.70f),
                        0.0f);
                if (!TryRecoveryFromSeed(RoomSeed))
                {
                    continue;
                }

                if (MinDistSq > 0.0f && FVector::DistSquared2D(RecoveryLoc, PawnLoc) < MinDistSq)
                {
                    continue;
                }
                return true;
            }
            return false;
        };

    bool bRecovered = false;
    const FVector* LastValidLocation = EnemyLastKnownValidLocationByPawn.Find(WeakPawn);
    const bool bLikelySameSpotLoop =
        LastValidLocation &&
        FVector::DistSquared2D(*LastValidLocation, PawnLoc) <= FMath::Square(140.0f);
    const float DesiredRelocateDistance = bLikelySameSpotLoop ? 320.0f : 180.0f;

    bRecovered = TryRecoveryFromRoomSeedWithMinDistance(bLikelySameSpotLoop ? 14 : 6, DesiredRelocateDistance);
    if (!bRecovered && LastValidLocation)
    {
        const bool bLastValidFarEnough =
            FVector::DistSquared2D(*LastValidLocation, PawnLoc) >= FMath::Square(FMath::Max(120.0f, DesiredRelocateDistance * 0.75f));
        if (!bLikelySameSpotLoop && bLastValidFarEnough)
        {
            bRecovered = TryRecoveryFromSeed(*LastValidLocation);
        }
    }
    if (!bRecovered && !bLikelySameSpotLoop)
    {
        bRecovered = TryRecoveryFromSeed(PawnLoc);
    }
    if (!bRecovered)
    {
        bRecovered = TryRecoveryFromRoomSeedWithMinDistance(16, 220.0f);
    }
    if (bRecovered && FVector::DistSquared2D(RecoveryLoc, PawnLoc) <= FMath::Square(120.0f))
    {
        // Treat near-identical XY recovery as a failure so we don't get stuck in endless recover loops.
        bRecovered = false;
    }

    if (bRecovered)
    {
        EnemyPawn->SetActorLocation(RecoveryLoc, false, nullptr, ETeleportType::TeleportPhysics);

        if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(EnemyPawn->GetRootComponent()))
        {
            RootPrimitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrimitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
        if (ACharacter* Character = Cast<ACharacter>(EnemyPawn))
        {
            if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
            {
                MoveComp->StopMovementImmediately();
                MoveComp->SetMovementMode(MOVE_Walking);
            }
        }

        ForceEnemyTraceCollisionInternal(EnemyPawn);
        EnemyUndergroundRecoveryFailuresByPawn.Remove(WeakPawn);
        EnemyLastKnownValidLocationByPawn.Remove(WeakPawn);
        EnemyUndergroundNextCheckByPawn.Add(WeakPawn, NowSeconds + FMath::Max(0.12, CheckInterval * 0.50));

        if (bEnableCombatPerfLogs)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat] Recovered underground enemy '%s' Room=%d from %s to %s"),
                *GetNameSafe(EnemyPawn),
                RoomId,
                *PawnLoc.ToCompactString(),
                *RecoveryLoc.ToCompactString());
        }
        LogTrackedEnemyState(EnemyPawn, RoomId, TEXT("UndergroundRecovered"));
        return true;
    }

    const int32 MaxFailures = FMath::Max(1, MaxUndergroundEnemyRecoveryFailures);
    const int32 NewFailureCount = EnemyUndergroundRecoveryFailuresByPawn.FindRef(WeakPawn) + 1;
    EnemyUndergroundRecoveryFailuresByPawn.Add(WeakPawn, NewFailureCount);

    if (bEnableCombatPerfLogs || NewFailureCount >= MaxFailures)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Underground recovery failed (%d/%d) Enemy='%s' Room=%d Loc=%s"),
            NewFailureCount,
            MaxFailures,
            *GetNameSafe(EnemyPawn),
            RoomId,
            *PawnLoc.ToCompactString());
    }
    LogTrackedEnemyState(EnemyPawn, RoomId, TEXT("UndergroundRecoveryFailed"));

    if (NewFailureCount >= MaxFailures)
    {
        bOutNeedsCull = true;
        EnemyUndergroundRecoveryFailuresByPawn.Remove(WeakPawn);
        EnemyUndergroundNextCheckByPawn.Remove(WeakPawn);
        EnemyLastKnownValidLocationByPawn.Remove(WeakPawn);
    }

    return true;
}
