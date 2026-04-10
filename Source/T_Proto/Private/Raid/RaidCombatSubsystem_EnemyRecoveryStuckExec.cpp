#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

bool URaidCombatSubsystem::RecoverStuckEnemyIfBlocked(APawn* EnemyPawn, int32 RoomId, bool& bOutNeedsCull)
{
    bOutNeedsCull = false;

    if (!bEnableStuckEnemyRecovery || !IsValid(EnemyPawn))
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
    const double CheckInterval = ComputeRecoveryCheckIntervalInternal((double)StuckEnemyCheckInterval, ActiveEnemyCount);
    if (const double* NextCheck = EnemyStuckNextCheckByPawn.Find(WeakPawn))
    {
        if (*NextCheck > NowSeconds)
        {
            return false;
        }
    }
    EnemyStuckNextCheckByPawn.Add(WeakPawn, NowSeconds + CheckInterval);

    const FVector PawnLoc = EnemyPawn->GetActorLocation();
    const float Velocity2D = EnemyPawn->GetVelocity().Size2D();
    const float MinVelocity2D = FMath::Max(0.0f, StuckEnemyMinVelocity2D);
    const float MinProgress2D = FMath::Max(20.0f, StuckEnemyMinProgressDistance2D);

    const FVector* LastProgressLocPtr = EnemyStuckLastProgressLocationByPawn.Find(WeakPawn);
    const double LastProgressTime = EnemyStuckLastProgressTimeByPawn.FindRef(WeakPawn);
    if (!LastProgressLocPtr || LastProgressTime <= 0.0)
    {
        EnemyStuckLastProgressLocationByPawn.Add(WeakPawn, PawnLoc);
        EnemyStuckLastProgressTimeByPawn.Add(WeakPawn, NowSeconds);
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakPawn);
        return false;
    }

    const float ProgressDelta2D = FVector::Dist2D(PawnLoc, *LastProgressLocPtr);
    if (Velocity2D >= MinVelocity2D || ProgressDelta2D >= MinProgress2D)
    {
        EnemyStuckLastProgressLocationByPawn.Add(WeakPawn, PawnLoc);
        EnemyStuckLastProgressTimeByPawn.Add(WeakPawn, NowSeconds);
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakPawn);
        return false;
    }

    if (APawn* PlayerPawn = GetPrimaryPlayerPawn())
    {
        const float IgnoreDistance = FMath::Max(0.0f, StuckEnemyIgnoreWhenNearPlayerDistance);
        if (IgnoreDistance > 0.0f && FVector::Dist2D(PawnLoc, PlayerPawn->GetActorLocation()) <= IgnoreDistance)
        {
            EnemyStuckLastProgressTimeByPawn.Add(WeakPawn, NowSeconds);
            return false;
        }
    }

    if (const AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController()))
    {
        if (AIController->GetMoveStatus() == EPathFollowingStatus::Idle)
        {
            // Idle shooters/snipers often hold position intentionally; avoid false "stuck" teleports.
            EnemyStuckLastProgressLocationByPawn.Add(WeakPawn, PawnLoc);
            EnemyStuckLastProgressTimeByPawn.Add(WeakPawn, NowSeconds);
            EnemyStuckRecoveryFailuresByPawn.Remove(WeakPawn);
            EnemyStuckNextCheckByPawn.Add(WeakPawn, NowSeconds + FMath::Max(0.80, CheckInterval * 1.35));
            return false;
        }
    }

    if (const ACharacter* Character = Cast<ACharacter>(EnemyPawn))
    {
        if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
        {
            if (MoveComp->MovementMode == MOVE_Falling)
            {
                return false;
            }
        }
    }

    const double StuckDuration = NowSeconds - LastProgressTime;
    if (StuckDuration < FMath::Max(0.5, (double)StuckEnemyDetectionSeconds))
    {
        return false;
    }

    float CapsuleRadius = 42.0f;
    float CapsuleHalfHeight = 88.0f;
    if (const UCapsuleComponent* Capsule = EnemyPawn->FindComponentByClass<UCapsuleComponent>())
    {
        CapsuleRadius = FMath::Max(20.0f, Capsule->GetScaledCapsuleRadius());
        CapsuleHalfHeight = FMath::Max(40.0f, Capsule->GetScaledCapsuleHalfHeight());
    }

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
    const FVector RoomCenter = Room->GetActorLocation();
    const FVector RoomExtent = Room->GetRoomExtent();
    const int32 SeedA = (int32)GetTypeHash(EnemyPawn);
    const int32 SeedB = (RoomId * 3571);
    const int32 SeedC = FMath::RoundToInt((float)NowSeconds * 1000.0f);
    FRandomStream RecoveryStream(SeedA ^ SeedB ^ SeedC);

    FVector RecoveryLoc = FVector::ZeroVector;
    auto TryRecoverFromSeed = [&](const FVector& SeedLocation) -> bool
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

    bool bRecovered = TryRecoverFromSeed(PawnLoc);
    if (!bRecovered)
    {
        for (int32 Attempt = 0; Attempt < 12; ++Attempt)
        {
            const FVector RoomSeed =
                RoomCenter +
                FVector(
                    RecoveryStream.FRandRange(-RoomExtent.X * 0.72f, RoomExtent.X * 0.72f),
                    RecoveryStream.FRandRange(-RoomExtent.Y * 0.72f, RoomExtent.Y * 0.72f),
                    0.0f);
            if (TryRecoverFromSeed(RoomSeed))
            {
                bRecovered = true;
                break;
            }
        }
    }
    if (!bRecovered)
    {
        bRecovered = TryRecoverFromSeed(RoomCenter);
    }
    if (bRecovered && FVector::DistSquared2D(RecoveryLoc, PawnLoc) <= FMath::Square(FMath::Max(90.0f, MinProgress2D * 0.65f)))
    {
        bRecovered = false;
    }

    if (bRecovered)
    {
        EnemyPawn->SetActorLocation(RecoveryLoc, false, nullptr, ETeleportType::TeleportPhysics);

        if (AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController()))
        {
            AIController->StopMovement();
        }
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
                if (MoveComp->MovementMode == MOVE_None)
                {
                    MoveComp->SetMovementMode(MOVE_Walking);
                }
            }
        }

        ForceEnemyTraceCollisionInternal(EnemyPawn);
        EnemyLastKnownValidLocationByPawn.Add(WeakPawn, RecoveryLoc);
        EnemyStuckLastProgressLocationByPawn.Add(WeakPawn, RecoveryLoc);
        EnemyStuckLastProgressTimeByPawn.Add(WeakPawn, NowSeconds);
        EnemyStuckNextCheckByPawn.Add(WeakPawn, NowSeconds + FMath::Max(1.0, CheckInterval * 2.0));
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakPawn);

        if (bEnableCombatPerfLogs)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat] Recovered stuck enemy '%s' Room=%d from %s to %s (Idle=%.2fs Vel2D=%.1f Delta2D=%.1f)"),
                *GetNameSafe(EnemyPawn),
                RoomId,
                *PawnLoc.ToCompactString(),
                *RecoveryLoc.ToCompactString(),
                StuckDuration,
                Velocity2D,
                ProgressDelta2D);
        }
        LogTrackedEnemyState(EnemyPawn, RoomId, TEXT("StuckRecovered"));
        return true;
    }

    const int32 MaxFailures = FMath::Max(1, MaxStuckEnemyRecoveryFailures);
    const int32 NewFailureCount = EnemyStuckRecoveryFailuresByPawn.FindRef(WeakPawn) + 1;
    EnemyStuckRecoveryFailuresByPawn.Add(WeakPawn, NewFailureCount);
    EnemyStuckNextCheckByPawn.Add(WeakPawn, NowSeconds + FMath::Max(0.35, CheckInterval * 1.6));

    if (bEnableCombatPerfLogs || NewFailureCount >= MaxFailures)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Stuck recovery failed (%d/%d) Enemy='%s' Room=%d Loc=%s Idle=%.2fs Vel2D=%.1f Delta2D=%.1f"),
            NewFailureCount,
            MaxFailures,
            *GetNameSafe(EnemyPawn),
            RoomId,
            *PawnLoc.ToCompactString(),
            StuckDuration,
            Velocity2D,
            ProgressDelta2D);
    }

    if (NewFailureCount >= MaxFailures)
    {
        bOutNeedsCull = true;
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakPawn);
        EnemyStuckLastProgressLocationByPawn.Remove(WeakPawn);
        EnemyStuckLastProgressTimeByPawn.Remove(WeakPawn);
        EnemyStuckNextCheckByPawn.Remove(WeakPawn);
    }

    return bOutNeedsCull;
}
