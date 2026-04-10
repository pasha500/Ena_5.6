#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void URaidCombatSubsystem::LogTrackedEnemyState(APawn* EnemyPawn, int32 RoomId, const TCHAR* Reason)
{
    if (!bEnableRoomEnemyTracking || RoomId != TrackedRoomId || !IsValid(EnemyPawn))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const FVector PawnLoc = EnemyPawn->GetActorLocation();
    const FVector PawnVel = EnemyPawn->GetVelocity();
    const TWeakObjectPtr<APawn> WeakPawn(EnemyPawn);
    EnemyTrackedLastObservedLocationByPawn.Add(WeakPawn, PawnLoc);

    int32 MovementMode = -1;
    bool bIsMovingOnGround = false;
    float FloorDistance = -1.0f;
    FString MovementBaseName = TEXT("None");

    if (const ACharacter* Character = Cast<ACharacter>(EnemyPawn))
    {
        if (const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
        {
            MovementMode = (int32)MoveComp->MovementMode;
            bIsMovingOnGround = MoveComp->IsMovingOnGround();
            if (MoveComp->CurrentFloor.bBlockingHit)
            {
                FloorDistance = MoveComp->CurrentFloor.FloorDist;
            }
            if (const UPrimitiveComponent* MoveBase = MoveComp->GetMovementBase())
            {
                MovementBaseName = GetNameSafe(MoveBase->GetOwner());
            }
        }
    }

    int32 RootCollisionEnabled = -1;
    bool bRootSimulatingPhysics = false;
    if (const UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(EnemyPawn->GetRootComponent()))
    {
        RootCollisionEnabled = (int32)RootPrim->GetCollisionEnabled();
        bRootSimulatingPhysics = RootPrim->IsSimulatingPhysics();
    }

    int32 CapsuleCollisionEnabled = -1;
    float CapsuleRadius = 0.0f;
    float CapsuleHalfHeight = 0.0f;
    if (const UCapsuleComponent* Capsule = EnemyPawn->FindComponentByClass<UCapsuleComponent>())
    {
        CapsuleCollisionEnabled = (int32)Capsule->GetCollisionEnabled();
        CapsuleRadius = Capsule->GetScaledCapsuleRadius();
        CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    }

    bool bHasGroundHit = false;
    FString GroundActorName = TEXT("None");
    float GroundDeltaZ = 0.0f;
    float GroundZ = 0.0f;

    if (const TObjectPtr<ARaidRoomActor>* RoomPtr = RoomById.Find(RoomId))
    {
        if (ARaidRoomActor* Room = RoomPtr->Get())
        {
            FHitResult GroundHit;
            bHasGroundHit = TryResolveAIGroundHitInternal(World, Room, PawnLoc, GroundHit);
            if (bHasGroundHit)
            {
                GroundActorName = GetNameSafe(GroundHit.GetActor());
                GroundZ = GroundHit.ImpactPoint.Z;
                GroundDeltaZ = PawnLoc.Z - (GroundHit.ImpactPoint.Z + FMath::Max(40.0f, CapsuleHalfHeight) + 6.0f);
            }
        }
    }

    bool bProjectedToNav = false;
    float NavDeltaZ = 0.0f;
    if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
    {
        FNavLocation NavLoc;
        bProjectedToNav = NavSys->ProjectPointToNavigation(PawnLoc, NavLoc, FVector(260.0f, 260.0f, 360.0f));
        if (bProjectedToNav)
        {
            NavDeltaZ = PawnLoc.Z - NavLoc.Location.Z;
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidCombat][RoomTrack] Reason=%s Room=%d Enemy='%s' Loc=%s Vel=%s MoveMode=%d OnGround=%d FloorDist=%.1f MoveBase='%s' RootColl=%d RootSimPhys=%d CapsuleColl=%d Capsule=%.1f/%.1f GroundHit=%d GroundActor='%s' GroundZ=%.1f GroundDeltaZ=%.1f NavProjected=%d NavDeltaZ=%.1f"),
        Reason ? Reason : TEXT("Unknown"),
        RoomId,
        *GetNameSafe(EnemyPawn),
        *PawnLoc.ToCompactString(),
        *PawnVel.ToCompactString(),
        MovementMode,
        bIsMovingOnGround ? 1 : 0,
        FloorDistance,
        *MovementBaseName,
        RootCollisionEnabled,
        bRootSimulatingPhysics ? 1 : 0,
        CapsuleCollisionEnabled,
        CapsuleRadius,
        CapsuleHalfHeight,
        bHasGroundHit ? 1 : 0,
        *GroundActorName,
        GroundZ,
        GroundDeltaZ,
        bProjectedToNav ? 1 : 0,
        NavDeltaZ);
}
