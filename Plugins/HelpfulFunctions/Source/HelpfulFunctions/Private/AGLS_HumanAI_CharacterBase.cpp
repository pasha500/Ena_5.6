


#include "AGLS_HumanAI_CharacterBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "KismetAnimationLibrary.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HelpfulFunctionsBPLibrary.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"


#define KML UKismetMathLibrary
#define KSL UKismetSystemLibrary
#define HFL UHelpfulFunctionsBPLibrary
#define CapHeight GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
#define CapRadius GetCapsuleComponent()->GetScaledCapsuleRadius()
#define Cap GetCapsuleComponent
#define NormToYawVector UHelpfulFunctionsBPLibrary::NormalToVector
#define VecEqual UKismetMathLibrary::EqualEqual_VectorVector
#define AnimInst GetMesh()->GetAnimInstance

FVector AAGLS_HumanAI_CharacterBase::N_to_R_Vector(FVector In)
{
	FRotator VecAsRot = KML::MakeRotFromX(In);
	VecAsRot = FRotator(0, VecAsRot.Yaw + 180, 0);
	return KML::GetRightVector(VecAsRot);
}

bool AAGLS_HumanAI_CharacterBase::GetIsHaveSniperRifle()
{
	FName TempName;
	int TempEnumIndex;
	USkeletalMesh* TempMainMesh = nullptr;
	UStaticMesh* TempMagazineMesh = nullptr;
	FName TempMagSocketName;
	bool TempHeadshotInstantDead = false;
	float TempDamageStrength = 0.0f;
	float TempAutoShootTime = 0.0f;
	int TempMaxAmmoPerMag = 0;
	UAnimMontage* TempReloadAnim = nullptr;

	bool bIsSniperRifle = false;
	
	Execute_BPI_AI_Get_RifleAssetProperties(
		this, 
		TempName, 
		TempEnumIndex, 
		TempMainMesh, 
		TempMagazineMesh, 
		TempMagSocketName, 
		TempHeadshotInstantDead, 
		TempDamageStrength, 
		TempAutoShootTime, 
		bIsSniperRifle, 
		TempMaxAmmoPerMag, 
		TempReloadAnim
	);
	return bIsSniperRifle;
}

float AAGLS_HumanAI_CharacterBase::GetSurfaceDurability(EPhysicalSurface InSurface)
{
	return 1.0f;
}

void AAGLS_HumanAI_CharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); //Call Parent Tick

	if (RotationChangeTimer > 0.0) //Check the timer is enabled
	{
		RotationChangeTimer = RotationChangeTimer - DeltaTime; //Update timer elapsed time
		if (RotationChangeTimer <= 0.0) //Check can timer end 
		{
			RotationChangeTimer = -1.0; //Disable Timer
		}
	}

}


AAGLS_HumanAI_CharacterBase::AAGLS_HumanAI_CharacterBase()
{
	AsyncFunctionsComponent = CreateDefaultSubobject<UAsyncFunctionsPlayer>(TEXT("AsyncFunctionsComponent"));
}


void AAGLS_HumanAI_CharacterBase::CalculateGroundedRotation()
{
	//if (RotateCapsuleToControlInIdle == false) //Call to parent function when overrided code is not required
	//{
	//	Super::CalculateGroundedRotation();
	//	return;
	//}
	CalculateCustomCharacterRotation_Implementation();
}


void AAGLS_HumanAI_CharacterBase::FindCoverFast(bool& CanCover, float& WallHeigh, FVector& Impact, FVector& Normal, FVector StartLocation, FVector StartDirection, int DebugIndex, ECollisionChannel TraceChannel, float ForwardTraceLenght, float GroundOffset)
{
	FVector BasePosition = GetActorLocation();
	FVector BaseDirection = GetActorForwardVector();

	if (KML::NotEqual_VectorVector(StartLocation, FVector(0, 0, 0), 0.5) == true && KML::NotEqual_VectorVector(StartDirection, FVector(0, 0, 0)) == true)
	{
		BasePosition = StartLocation; BaseDirection = StartDirection;
	}

	ETraceTypeQuery TraceTypeQuery = UEngineTypes::ConvertToTraceType(TraceChannel);
	TArray<AActor*> ActorsToIgnore = {};
	EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
	if (DebugIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
	if (DebugIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
	FHitResult TraceResult;

	FVector TraceStart = BasePosition - FVector(0, 0, CapHeight) + FVector(0, 0, GroundOffset);
	FVector TraceEnd = TraceStart + (BaseDirection * ForwardTraceLenght);

	const bool LineTraceValid = KSL::LineTraceSingle(this, TraceStart, TraceEnd, TraceTypeQuery, false, ActorsToIgnore, DebugTrace, TraceResult, true, FColor::Orange, FColor::Yellow, 0.5);

	if (LineTraceValid == false)
	{ CanCover = false; WallHeigh = 0.0; return; }

	TraceStart = TraceResult.ImpactPoint + (NormToYawVector(TraceResult.ImpactNormal) * 5.0) + FVector(0, 0, 140.0);
	TraceEnd = TraceStart - FVector(0, 0, 145);

	FHitResult WallHitResult; FHitResult WallWidthResult;
	bool WallHeightValid = HFL::FindGapBySphereTrace(this, TraceStart, TraceEnd, 0.4, TraceTypeQuery, false, ActorsToIgnore, DebugIndex, WallHitResult, true, FColor::Cyan, FColor::Emerald, 0.3, 6);


	if (WallHeightValid == true && KML::NotEqual_VectorVector(WallHitResult.ImpactPoint, WallHitResult.TraceStart, 0.1) == true)
	{
		for (int i = 0; i <= 1; i++)
		{
			float DirectionOffset = 30.0;
			if (i > 0) { DirectionOffset = DirectionOffset * -1.0; }

			TraceStart = WallHitResult.ImpactPoint - FVector(0, 0, 5) + (N_to_R_Vector(TraceResult.ImpactNormal) * DirectionOffset);
			TraceEnd = TraceStart + (NormToYawVector(TraceResult.ImpactNormal) * 10.0);
			TraceStart = TraceStart + (NormToYawVector(TraceResult.ImpactNormal) * -25.0);

			bool WallWidthValid = KSL::SphereTraceSingle(this, TraceStart, TraceEnd, 5.0, TraceTypeQuery, false, ActorsToIgnore, DebugTrace, WallWidthResult, true, FColor::Purple, FColor::Magenta, 0.3);

			if(WallWidthValid == false) { CanCover = false; WallHeigh = 0.0; return; }

			if (VecEqual(WallWidthResult.ImpactPoint, WallWidthResult.TraceStart, 0.5) == true || VecEqual(WallWidthResult.ImpactPoint, WallWidthResult.TraceEnd, 0.5))
			{
				CanCover = false; WallHeigh = 0.0; return;
			}
		}
	}
	CanCover = LineTraceValid && WallHeightValid;
	WallHeigh = WallHitResult.ImpactPoint.Z - (GetActorLocation().Z - CapHeight);
	Impact = WallHitResult.ImpactPoint;
	Normal = TraceResult.ImpactNormal;
}


void AAGLS_HumanAI_CharacterBase::CheckHealthPerFrame_Implementation(bool Update)
{

}


bool AAGLS_HumanAI_CharacterBase::StartDyingSequence_Implementation(FVector ImpulseNormal, FVector ImpulseLocation, CALS_DeathType DeathType, FName HitBoneName, int RifleModelIndex)
{
	return false;
}


bool AAGLS_HumanAI_CharacterBase::GetRifleAmmoData_Implementation(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount)
{
	CurrentAmmoCount = 10; CurrentMagazinesCount = 10; MaxAmmoCount = 10; MaxMagazinesCount = 10;
	return false; // Overrided In BLUEPRINT!!!
}


bool AAGLS_HumanAI_CharacterBase::GetPistolAmmoData_Implementation(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount)
{
	return false; // Overrided In BLUEPRINT!!!
}

// Ragoll Functions ========================================================================================================================================= RAGDOLL START

void AAGLS_HumanAI_CharacterBase::RagdollStart_Implementation()
{
	//Step 1: Clear the Character Movement Mode and set teh Movement State to Ragdoll
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_None);
	Execute_BPI_AI_Set_MovementState(this, CALS_MovementState::Ragdoll);

	//Step 2: Disable capsule collision and enable mesh physics simulation starting from the pelvis.
	Cap()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionObjectType(ECollisionChannel::ECC_PhysicsBody);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);

	//Step 3: Stop any active montages.
	if (GetMesh()->GetAnimInstance())
	{
		GetMesh()->GetAnimInstance()->StopAllMontages(0.2);
	}
}

void AAGLS_HumanAI_CharacterBase::RagdollEnd_Implementation()
{
	//Step 1: Save a snapshot of the current Ragdoll Pose for use in AnimGraph to blend out of the ragdoll
	if (AnimInst())
	{
		AnimInst()->SavePoseSnapshot(TEXT("RagdollPose"));
	}

	//Step 2: If the ragdoll is on the ground, set the movement mode to walking and play a Get Up animation. 
	// If not, set the movement mode to falling and update teh character movement velocity to match the last ragdoll velocity.
	if (RagdollOnGround)
	{
		GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);
		if (AsyncFunctionsComponent && AnimInst())
		{
			AsyncFunctionsComponent->MontagePlayAsync(AnimInst(), GetGetUpAnimation(), 1.0, 0.0, false);
		}

	}
	else
	{
		GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Falling);
		GetCharacterMovement()->Velocity = LastRagdollVelocity;
	}
	//Step 3: Re-Enable capsule collision, and disable physics simulation on the mesh.
	Cap()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetAllBodiesSimulatePhysics(false);
}

void AAGLS_HumanAI_CharacterBase::RagdollUpdate_Implementation(float OutSpringRange, float InDamping, float InForceLimit, int DebugIndex)
{
	//Set the Last Ragdoll Velocity.
	LastRagdollVelocity = GetMesh()->GetPhysicsLinearVelocity();

	//Use the Ragdoll Velocity to scale the ragdoll's joint strength for physical animation.
	if (GetMesh())
	{
		GetMesh()->SetAllMotorsAngularDriveParams(KML::MapRangeClamped(LastRagdollVelocity.Length(), 0.0, 1000, 0.0, OutSpringRange), InDamping, InForceLimit, false);
	}

	//Disable Gravity if falling faster than -4000 to prevent continual acceleration. This also prevents the ragdoll from going through the floor.
	if (GetMesh())
	{
		GetMesh()->SetEnableGravity(LastRagdollVelocity.Z > -4000.0);
	}
	else
	{ return; }

	//Update the Actor location to follow the ragdoll.
	// 3.1 Set the pelvis as the target location.
	FVector TargetRagdollLocation = GetMesh()->GetSocketLocation(TEXT("pelvis"));

	// 3.2 Determine wether the ragdoll is facing up or down and set the target rotation accordingly.
	RagdollFaceUp = GetMesh()->GetSocketRotation(TEXT("pelvis")).Roll < 0.0;
	const float PelvisYaw = GetMesh()->GetSocketRotation(TEXT("pelvis")).Yaw;
	FRotator TargetRagdollRotation = FRotator(0, KML::SelectFloat(PelvisYaw - 180, PelvisYaw, RagdollFaceUp), 0);

	// 3.3 Trace downward from the target location to offset the target location, preventing the lower half of the capsule from 
	// going through the floor when the ragdoll is laying on the ground.
	ETraceTypeQuery TraceTypeQuery = UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_Visibility);
	TArray<AActor*> ActorsToIgnore = {};
	EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
	if (DebugIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
	if (DebugIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
	FHitResult TraceResult;

	RagdollOnGround = KSL::LineTraceSingle(this, TargetRagdollLocation, TargetRagdollLocation - FVector(0, 0, CapHeight), TraceTypeQuery, false, ActorsToIgnore, DebugTrace, TraceResult, true);
	if (RagdollOnGround)
	{
		const float TraceDiff = abs(TraceResult.ImpactPoint.Z - TraceResult.TraceStart.Z);
		SetActorLocationAndRotation(TargetRagdollLocation + FVector(0,0,(CapHeight - TraceDiff) + 2.0), TargetRagdollRotation, false);
		TargetRotationC = TargetRagdollRotation;
	}
	else
	{
		SetActorLocationAndRotation(TargetRagdollLocation, TargetRagdollRotation, false);
		TargetRotationC = TargetRagdollRotation;
	}
}

// Ragoll Functions ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ RAGDOLL END

void AAGLS_HumanAI_CharacterBase::SmoothedCapsuleRadiusUpdate(float TargetHeight, float InterpSpeed, bool UpdateOverlaps)
{
	if (DefCapsuleSizeC.Y != Cap()->GetUnscaledCapsuleHalfHeight())
	{
		float NewHeight = KML::FInterpTo_Constant(Cap()->GetUnscaledCapsuleHalfHeight(), TargetHeight, GetWorld()->GetDeltaSeconds(), InterpSpeed);

		Cap()->SetCapsuleHalfHeight(NewHeight, UpdateOverlaps);
		GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, 0.0 - Cap()->GetUnscaledCapsuleHalfHeight()), false);
	}
}


bool AAGLS_HumanAI_CharacterBase::IsAlive()
{
	return IsDeathC == false && GetMesh()->GetAnimInstance()->GetCurveValue(TEXT("DeathAnim_Alpha")) < 0.2;
}

FVector AAGLS_HumanAI_CharacterBase::GetCalpsuleBaseLocation(float ZOffset)
{
	return Cap()->GetComponentLocation() - (Cap()->GetUpVector() * (CapHeight + ZOffset));
}


FVector AAGLS_HumanAI_CharacterBase::GetCapsuleLocationFromBase(FVector BaseLocation, float ZOffset)
{
	return BaseLocation + FVector(0, 0, CapHeight + ZOffset);
}


void AAGLS_HumanAI_CharacterBase::GetControlForwardRightVector(FVector& ReturnForward, FVector& ReturnRight)
{
	const FRotator ControlYaw = FRotator(0, GetControlRotation().Yaw, 0);
	ReturnForward = KML::GetForwardVector(ControlYaw);
	ReturnRight = KML::GetRightVector(ControlYaw);
	return;
}


bool AAGLS_HumanAI_CharacterBase::CanUpdateOverlay_Implementation(CALS_OverlayState NewOverlay, CALS_OverlayState CurrentOverlay, bool forced)
{
	if (forced == true) { return true; }
	if (LockOverlayChangeC == false && IsAlive() == true && StartStealthKillC == false)
	{
		if (NewOverlay == CALS_OverlayState::Rifle && HaveRifle == false) { return false; }
		if ((NewOverlay == CALS_OverlayState::Pistol1H || NewOverlay == CALS_OverlayState::Pistol2H) == true && HavePistol == false) { return false; }
		if (IsReloadingWeaponC == true || IsEquipPropC == true || MovementActionC != CALS_MovementAction::None) { return false; }
		return true;
	}
	return false;
}


CALS_OverlayState AAGLS_HumanAI_CharacterBase::ChangeToPistolWhenNoAmmo(CALS_OverlayState NewOverlay)
{
	if (NewOverlay == CALS_OverlayState::Rifle)
	{
		int CA, CM, MA, MM;
		GetRifleAmmoData(CA, CM, MA, MM);
		if (CA == 0 && CM == 0)
		{
			return CALS_OverlayState::Pistol1H;
		}
		else
		{
			return NewOverlay;
		}
	}
	else
	{
		return NewOverlay;
	}
}

//Use the Character Movement Mode changes to set the Movement States to the right values. This allows you to have a custom set of movement 
// states but still use the functionality of the default character movement component.
void AAGLS_HumanAI_CharacterBase::OnCharacterMovementModeChanged(EMovementMode PrevMovementMode, EMovementMode NewMovementMode, uint8  PrevCustomMode, uint8  NewCustomMode)
{
	if (NewMovementMode == EMovementMode::MOVE_Walking || NewMovementMode == EMovementMode::MOVE_NavWalking)
	{
		Execute_BPI_AI_Set_MovementState(this, CALS_MovementState::Grounded);
	}
	else if (NewMovementMode == EMovementMode::MOVE_Falling)
	{
		Execute_BPI_AI_Set_MovementState(this, CALS_MovementState::InAir);
	}
}

//OnMovementStateChanged_Implementation
void AAGLS_HumanAI_CharacterBase::OnMovementStateChanged_Implementation(CALS_MovementState NewMovementState)
{
	PrevMovementStateC = MovementStateC;
	MovementStateC = NewMovementState;
	
	//If the character enters the air, set the In Air Rotation and uncrouch if crouched. If the character is currently rolling, enable the ragdoll.
	if (MovementStateC == CALS_MovementState::InAir)
	{
		if (MovementActionC == CALS_MovementAction::None)
		{
			InAirRotationC = GetActorRotation();
			if (StanceC == CALS_Stance::Crouching)
			{
				UnCrouch();
			}
		}
		else if (MovementActionC == CALS_MovementAction::Rolling)
		{
			RagdollStart();
		}
	}
	else if (MovementStateC == CALS_MovementState::Ragdoll)
	{
		//Stop the Mantle Timeline if transitioning to the ragdoll state while mantling.
		if (PrevMovementStateC == CALS_MovementState::Mantling)
		{
			StopMantleTimeline = true;
		}
	}

}

//OnMovementActionChanged_Implementation
void AAGLS_HumanAI_CharacterBase::OnMovementActionChanged_Implementation(CALS_MovementAction NewMovementAction)
{
	CALS_MovementAction PreviousMovementAction = MovementActionC;
	MovementActionC = NewMovementAction;

	//Make the character crouch if performing a roll.
	if (MovementActionC == CALS_MovementAction::Rolling)
	{
		Crouch();
	}
	//Upon ending a roll, reset the stance back to its desired value.
	if (PreviousMovementAction == CALS_MovementAction::Rolling)
	{
		if (DesiredStanceC == CALS_Stance::Standing)
		{
			UnCrouch();
		}
		else
		{
			Crouch();
		}
	}
}

//OnStanceChanged
void AAGLS_HumanAI_CharacterBase::OnStanceChanged(CALS_Stance NewStance)
{
	StanceC = NewStance;
}

//OnRotationModeChanged_Implementation
void AAGLS_HumanAI_CharacterBase::OnRotationModeChanged_Implementation(CALS_RotationMode NewRotationMode, float LockTime)
{
	if (RotationChangeTimer < -0.1 && CanUpdateRotationMode)
	{
		RotationModeC = NewRotationMode;
		RotationChangeTimer = LockTime;
	}
}

//OnGaitChanged
void AAGLS_HumanAI_CharacterBase::OnGaitChanged(CALS_Gait NewActualGait)
{
	GaitC = NewActualGait;
}

//OnOverlayStateChanged_Implementation
void AAGLS_HumanAI_CharacterBase::OnOverlayStateChanged_Implementation(CALS_OverlayState NewOverlayState)
{
	PrevOverlayState = OverlayStateC;
	OverlayStateC = NewOverlayState;
}

void AAGLS_HumanAI_CharacterBase::DropProps_Implementation(const TArray<CALS_OverlayState>& PropsList, bool DropGunsAttachedToBody)
{
}

void AAGLS_HumanAI_CharacterBase::SetCurrentMovementParametersValues_Implementation(bool UseVectorControl)
{
}


float AAGLS_HumanAI_CharacterBase::GetTargetSpeedWithStrafe(FVector SpeedValues)
{
	if (StrafeSpeedMapCurveC)
	{
		float StrafeSpeedMap = StrafeSpeedMapCurveC->GetFloatValue(abs(UKismetAnimationLibrary::CalculateDirection(this->GetCharacterMovement()->Velocity, GetActorRotation())));
		if (StrafeSpeedMap < 1.0)
		{
			return UKismetMathLibrary::MapRangeClamped(StrafeSpeedMap, 0.0, 1.0, SpeedValues.X, SpeedValues.Y);
		}
		else
		{
			return UKismetMathLibrary::MapRangeClamped(StrafeSpeedMap, 1.0, 2.0, SpeedValues.Y, SpeedValues.Z);
		}
	}
	return 300.0;
}

CALS_Gait AAGLS_HumanAI_CharacterBase::GetActualGaitStrafe(CALS_Gait AllowedGait)
{
	float LocalWalkSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.WalkSpeed);
	float LocalRunSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.RunSpeed);

	bool AccCondition = AccelerationC.Length() - GetCharacterMovement()->MaxAcceleration > -10.0;
	bool SpeedCondition = SpeedC >= (LocalRunSpeed + -20.0);
	if (GaitC == CALS_Gait::Sprinting) { SpeedCondition = SpeedC > 10.0; }

	if (AccCondition || SpeedCondition)
	{
		return AllowedGait;
	}
	else if (SpeedC >= (LocalWalkSpeed + 3.0))
	{
		return CALS_Gait::Running;
	}
	else
	{
		if (AllowedGait == CALS_Gait::Walking) { return CALS_Gait::Walking; }
		else { return CALS_Gait::Running; }
	}
}

float AAGLS_HumanAI_CharacterBase::GetMappedSpeedStrafe(float SpeedScale)
{
	const float LocWalkSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.WalkSpeed);
	const float LocRunSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.RunSpeed);
	const float LocSprintSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.SprintSpeed);

	if (SpeedC > LocRunSpeed)
	{
		return UKismetMathLibrary::MapRangeClamped(SpeedC, LocRunSpeed, LocSprintSpeed, 2.0, 3.0);
	}
	else if (SpeedC > LocWalkSpeed)
	{
		return UKismetMathLibrary::MapRangeClamped(SpeedC, LocWalkSpeed, LocRunSpeed, 1.0, 2.0);
	}
	else
	{
		return UKismetMathLibrary::MapRangeClamped(SpeedC, 0, LocWalkSpeed, 0.0, 1.0);
	}
}

void AAGLS_HumanAI_CharacterBase::UpdateGroundedMoveSettingsStrafe(CALS_Gait AllowedGait, FCALSMovementSettingsStrafe CurrentMovement)
{
	//This function is used to set the max speed for the character’s movement. Because the forwards, strafes, and backwards animations move 
	// at different speeds, we need to change the max speed of the character based on its movement direction. We use a simple curve to map 
	// different speed values to the different directions. 0 = forward, 1 = strafe L or R, 2 = Backwards.
	float MaxSpeed = 300.0;
	if (AllowedGait == CALS_Gait::Walking) { MaxSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.WalkSpeed); }
	else if(AllowedGait == CALS_Gait::Running) { MaxSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.RunSpeed); }
	else { MaxSpeed = GetTargetSpeedWithStrafe(GroundMoveSettingsVectorC.SprintSpeed); }

	//Update the Character Max Walk Speed to the configured speeds based on the currently Allowed Gait.
	GetCharacterMovement()->MaxWalkSpeed = MaxSpeed * SpeedScaleC;
	GetCharacterMovement()->MaxWalkSpeedCrouched = MaxSpeed * SpeedScaleC;

	//Get From Anim Instance Character Movement Parameters Modify
	float DecelerationBias = 0.0; float FrictionBias = 0.0;
	if (IsValid(AnimInst()))
	{
		DecelerationBias = AnimInst()->GetCurveValue(TEXT("Modify_Deceleration")) * DecelerationBiasScale;
		FrictionBias = AnimInst()->GetCurveValue(TEXT("Modify_GroundFriction")) * GroundFrictionBiasScale;
	}

	//Update the Acceleration, Deceleration, and Ground Friction using the Movement Curve. 
	// This allows for fine control over movement behavior at each speed (May not be suitable for replication).
	const FVector CurveValue = GroundMoveSettingsVectorC.MovementCurve->GetVectorValue(GetMappedSpeedStrafe(SpeedScaleC));
	GetCharacterMovement()->MaxAcceleration = CurveValue.X;
	GetCharacterMovement()->BrakingDecelerationWalking = CurveValue.Y + DecelerationBias;
	GetCharacterMovement()->GroundFriction = CurveValue.Z + FrictionBias;
}

void AAGLS_HumanAI_CharacterBase::UpdateGroundedMovementValues_Implementation(bool UseVectorControl)
{
	CALS_Gait AllowedGait = CALS_Gait::Walking;
	CALS_Gait ActualGait = CALS_Gait::Walking;

	// From DataTable Asset set current Movement Parameters Settings
	SetCurrentMovementParametersValues(UseVectorControl);

	if (UseVectorControl == false)
	{
		//Calculate the Allowed Gait. This represents the maximum Gait the character is currently allowed to be in, and can be determined by the desired gait, 
		// the rotation mode, the stance, etc. For example, if you wanted to force the character into a walking state while indoors, this could be done here.
		AllowedGait = GetAllowedGaitFast();
	}
	else
	{
		if (StanceC == CALS_Stance::Standing)
		{
			if (RotationModeC != CALS_RotationMode::Aiming)
			{
				if (DesiredGaitC == CALS_Gait::Walking) { AllowedGait = CALS_Gait::Walking; }
				else if(DesiredGaitC == CALS_Gait::Running) { AllowedGait = CALS_Gait::Running; }
				else if (DesiredGaitC == CALS_Gait::Sprinting)
				{
					if (CurrentHealthPoints > 10) { AllowedGait = CALS_Gait::Sprinting; }
					else { AllowedGait = CALS_Gait::Running; }
				}
			}
			else
			{
				if (DesiredGaitC == CALS_Gait::Walking) { AllowedGait = CALS_Gait::Walking; }
				else { AllowedGait = CALS_Gait::Running; }
			}
		}
		else
		{
			if (DesiredGaitC == CALS_Gait::Walking) { AllowedGait = CALS_Gait::Walking; }
			else { AllowedGait = CALS_Gait::Running; }
		}
	}

	//Determine the Actual Gait. If it is different from the current Gait, Set the new Gait Event.
	if (UseVectorControl == false)
	{
		ActualGait = GetActualGaitFast(AllowedGait);
	}
	else
	{
		ActualGait = GetActualGaitStrafe(AllowedGait);
	}

	if (ActualGait != GaitC) { Execute_BPI_AI_Set_Gait(this, ActualGait); }

	//Use the allowed gait to update the movement settings.
	if (UseVectorControl == false)
	{
		if(GroundMoveSettingsScalarC.MovementCurve) { UpdateMovementSettings(AllowedGait, GroundMoveSettingsScalarC); }
		
	}
	else
	{
		if (GroundMoveSettingsVectorC.MovementCurve) { UpdateGroundedMoveSettingsStrafe(AllowedGait, GroundMoveSettingsVectorC); }
	}
}

//More Functions For Human AI

bool AAGLS_HumanAI_CharacterBase::CheckFallDamage_Implementation(FHitResult LandHitResult, float VelocityMaxRange, FVector DamageTresholds)
{
	if (GetVelocity().Z >= -10.0) { return false; }

	//Map Velocity
	float MappedVelocityZ = KML::MapRangeClamped(abs(GetVelocity().Z), 0.0, VelocityMaxRange, 0.0, 1.0);

	//Select Fall Damage Type
	if (MappedVelocityZ > DamageTresholds.X)
	{
		DeathByFallDamage(LandHitResult); return true;
	}
	else if (MappedVelocityZ > DamageTresholds.Y)
	{
		const float PlayRateRandom = KML::RandomFloatInRange(0.85, 1.1);
		if (AnimInst())
		{
			AsyncFunctionsComponent->PlayDynamicMontageAsync(AnimInst(), HardLandMontage, TEXT("BaseLayer"), 0.15, 0.3, PlayRateRandom);
			ApplyDamageValue(nullptr, LandHitResult, 40.0, 3, -1.0);
			return true;
		}
	}
	else if (MappedVelocityZ > DamageTresholds.Z)
	{
		const float PlayRateRandom = KML::RandomFloatInRange(0.85, 1.1);
		if (AnimInst())
		{
			AsyncFunctionsComponent->PlayDynamicMontageAsync(AnimInst(), LightLandMontage, TEXT("BaseLayer"), 0.15, 0.3, PlayRateRandom);
			ApplyDamageValue(nullptr, LandHitResult, 25.0, 3, -1.0);
			return true;
		}
	}
	return false;
}

void AAGLS_HumanAI_CharacterBase::DeathByFallDamage_Implementation(FHitResult HitGroundResult)
{
}

bool AAGLS_HumanAI_CharacterBase::ApplyDamageValue_Implementation(AActor* DamageCauser, FHitResult HitInfo, float DamageValue, int DamageTypeIndex, float UnRagdollTime)
{
	CurrentHealthPoints = KML::FClamp(CurrentHealthPoints, 0.0, CurrentHealthPoints);
	return false;
}

void AAGLS_HumanAI_CharacterBase::MakeForPropsAttachValues()
{
	UEnum* EnumPtr = StaticEnum<CALS_OverlayState>();

	PropsAttachValues.Reset();

	UPrimitiveComponent* PropComponent = nullptr;
	FCALS_PropsAttachValues CurrentPropValues = {};

	for (int32 i = 0; i < EnumPtr->GetMaxEnumValue(); i++)
	{
		FName EnumAsName = FName(EnumPtr->GetNameStringByIndex(i));
		int32 EnumValue = EnumPtr->GetValueByIndex(i);
		CALS_OverlayState EnumAsOverlayState = static_cast<CALS_OverlayState>(EnumValue);

		PropComponent = Cast<UPrimitiveComponent>(FindComponentByTag(UPrimitiveComponent::StaticClass(), EnumAsName));
		if (PropComponent)
		{
			CurrentPropValues.TargetComponent = PropComponent;
			CurrentPropValues.ParentComponent = PropComponent->GetAttachParent();
			CurrentPropValues.AttachSocketName = PropComponent->GetAttachSocketName();
			CurrentPropValues.AttachOffset = PropComponent->GetRelativeTransform();

			PropsAttachValues.Add(EnumAsOverlayState, CurrentPropValues);
		}
	}
}

void AAGLS_HumanAI_CharacterBase::AttachPropTo_Implementation(CALS_OverlayState TargetOverlay, float DelayTime)
{
}

void AAGLS_HumanAI_CharacterBase::AttachAllPropsToDefault()
{
	TArray<CALS_OverlayState> OverlayKeys = {};
	PropsAttachValues.GetKeys(OverlayKeys);

	for (const TPair<CALS_OverlayState, FCALS_PropsAttachValues>& d : PropsAttachValues)
	{
		CALS_OverlayState CurrentOverlay = d.Key;
		FCALS_PropsAttachValues AttachValues = d.Value;
		if (AttachValues.TargetComponent && AttachValues.ParentComponent)
		{
			AttachValues.TargetComponent->AttachToComponent(AttachValues.ParentComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachValues.AttachSocketName);
			AttachValues.TargetComponent->SetRelativeLocation(AttachValues.AttachOffset.GetLocation());
			AttachValues.TargetComponent->SetRelativeRotation(AttachValues.AttachOffset.GetRotation());
		}
	}


}

bool AAGLS_HumanAI_CharacterBase::TrySimpleTraversalAction_Implementation()
{
	return false;
}

bool AAGLS_HumanAI_CharacterBase::CheckCanShoot()
{
	if (OverlayStateC == CALS_OverlayState::Rifle || OverlayStateC == CALS_OverlayState::Pistol1H || OverlayStateC == CALS_OverlayState::Pistol2H)
	{
		if (!IsEquipPropC && !IsReloadingWeaponC && IsAlive())
		{
			int LocIndex = 0; uint8 LocByte; FName LocName;
			Execute_BPI_AI_Get_LocomotionModeIndex(this, LocIndex, LocByte, LocName);
			if (LocIndex == 0 || LocIndex == 1 || LocIndex == 3)
			{
				if (RotationModeC == CALS_RotationMode::Aiming && !StartStealthKillC && !HitedByKnifeOrHandC)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool AAGLS_HumanAI_CharacterBase::ShotFirstTraceCheck(bool& StartScan, FHitResult& HitResult, FVector& OutScanOrigin, FVector& OutTraceDirection, const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectsType, FName HeadSocketName, FName MuzzleSocketName, float BlendWithMuzzle, float TraceBasicLenght, float DispersionScale, float WallPenetrationLen, int DebugTraceIndex)
{
	if (IsValid(CurrentHoldingProp) == false) { return false; }

	const bool HoldingRifle = OverlayStateC == CALS_OverlayState::Rifle;

	TArray<AActor*> ActorsToIgnore = {};
	EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
	if (DebugTraceIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
	if (DebugTraceIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
	FHitResult TraceResult;

	FVector OriginInitLoc = KML::VLerp(CurrentHoldingProp->GetSocketLocation(MuzzleSocketName), GetMesh()->GetSocketLocation(HeadSocketName), BlendWithMuzzle);

	float TraceLenght = TraceBasicLenght;
	if (GetIsHaveSniperRifle() == true) { TraceLenght = TraceLenght + 3000.0; }

	FVector TraceStart = OriginInitLoc + (KML::GetForwardVector(SmoothTargetAimingC) * -8.0);
	FVector TraceEnd = OriginInitLoc + 
		(KML::GetForwardVector(SmoothTargetAimingC) * TraceLenght) + 
		(KML::GetForwardVector(GetControlRotation()) * DispersionOffset.X) + 
		(KML::GetRightVector(GetControlRotation()) * DispersionOffset.Y);
	
	FVector ShotDirection = KML::GetForwardVector(KML::FindLookAtRotation(TraceStart, TraceEnd));

	const bool MainTraceValid = KSL::SphereTraceSingleForObjects(this, TraceStart, TraceEnd, 6.0, ObjectsType, false, ActorsToIgnore, DebugTrace, TraceResult, true, FColor::Black, FColor::Red, 1.0);

	if (MainTraceValid && !TraceResult.bStartPenetrating)
	{
		if (LOD_State == AGLS_LOD_State::LOD0 || LOD_State == AGLS_LOD_State::LOD1)
		{
			FHitResult ScanResult;
			float PenetrationDepth = (WallPenetrationLen * GetSurfaceDurability(EPhysicalSurface::SurfaceType1)) * KML::SelectFloat(1.0, 0.8, HoldingRifle);
			const bool ScanValid = KSL::LineTraceSingleForObjects(this, TraceResult.ImpactPoint + (KML::GetForwardVector(SmoothTargetAimingC) * PenetrationDepth), TraceResult.ImpactPoint,
				ObjectsType, false, ActorsToIgnore, DebugTrace, ScanResult, true, FColor::Blue, FColor::Emerald, 1.4);

			if (ScanValid && KML::NotEqual_VectorVector(ScanResult.ImpactPoint, ScanResult.TraceStart, 0.8) && KML::NotEqual_VectorVector(ScanResult.ImpactPoint, ScanResult.TraceEnd, 0.8))
			{
				StartScan = true;
				HitResult = TraceResult;
				OutScanOrigin = ScanResult.ImpactPoint;
				OutTraceDirection = ShotDirection;
				return MainTraceValid;
			}
			else
			{
				StartScan = false;
				HitResult = TraceResult;
				OutScanOrigin = FVector(0, 0, 0);
				OutTraceDirection = ShotDirection;
				return MainTraceValid;
			}
		}
		else
		{
			StartScan = false;
			HitResult = TraceResult;
			OutScanOrigin = FVector(0, 0, 0);
			OutTraceDirection = ShotDirection;
			return MainTraceValid;
		}
	}
	return false;
}

bool AAGLS_HumanAI_CharacterBase::ShotScanTraceCheck(FHitResult& HitResult, const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectsType, bool StartScan, FVector Origin, float TraceBasicLenght, int DebugTraceIndex)
{
	if (StartScan == false) { return false; }

	TArray<AActor*> ActorsToIgnore = {};
	EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
	if (DebugTraceIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
	if (DebugTraceIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
	FHitResult TraceResult;

	const bool TraceValid = KSL::LineTraceSingleForObjects(this, Origin + (KML::GetForwardVector(SmoothTargetAimingC) * 3.0), Origin + (KML::GetForwardVector(SmoothTargetAimingC) * TraceBasicLenght), ObjectsType, 
		false, ActorsToIgnore, DebugTrace, TraceResult, true, FColor::Orange, FColor::Red, 0.6);

	HitResult = TraceResult;
	return TraceValid;
}

TSoftObjectPtr<UAnimMontage> AAGLS_HumanAI_CharacterBase::GetGetUpAnimation_Implementation()
{
	return TSoftObjectPtr<UAnimMontage>();
}



//▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒ AGLS v1.6
void AAGLS_HumanAI_CharacterBase::CalculateCustomCharacterRotation_Implementation()
{

	if (LockRotationUpdatingC == true)
	{
		TargetRotationC = Self->GetActorRotation();
		return;
	}

	if (MovementActionC == CALS_MovementAction::None)
	{
		// sprawdz czy postac sie porusza (ROTATION LOGIC WHEN CHARACTER IS MOVING)
		if (CanUpdateRotation() == true)
		{
			switch (RotationModeC)
			{
			case CALS_RotationMode::VelocityDirection: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 𝐕𝐄𝐋𝐎𝐂𝐈𝐓𝐘

				if (DontCalculateCustomRotOnVelocityMode == true)
				{
					TargetRotationC = Self->GetActorRotation();
				}
				else
				{
					// IF Moving==True [AND] RotationMode==Velocity
					SmoothedCharRotation(FRotator(0, LastVelocityRotationC.Yaw, 0), 800, CalcGroundedRotationRate(), true);
				}
				return;
			case CALS_RotationMode::LookingDirection: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 𝐋𝐎𝐎𝐊𝐈𝐍𝐆

				switch (GaitC)
				{
				case CALS_Gait::Walking:
					SmoothedCharRotation(FRotator(0, Self->GetControlRotation().Yaw, 0), 400, CalcGroundedRotationRate(), true);
					return;
				case CALS_Gait::Running:
					SmoothedCharRotation(FRotator(0, Self->GetControlRotation().Yaw, 0), 400, CalcGroundedRotationRate(), true);
					return;
				case CALS_Gait::Sprinting:

					if(UseRotateToVelocityOnSprintGait == true) { SmoothedCharRotation(FRotator(0, LastVelocityRotationC.Yaw, 0), 500, CalcGroundedRotationRate(), true); }
					else { SmoothedCharRotation(FRotator(0, Self->GetControlRotation().Yaw, 0), 500, CalcGroundedRotationRate(), true); }
					return;
				}

				return;
			case CALS_RotationMode::Aiming: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 𝐀𝐈𝐌𝐈𝐍𝐆
				SmoothedCharRotation(FRotator(0, Self->GetControlRotation().Yaw, 0), 1200, CalcGroundedRotationRate(), true);
				return;
			}
		}
		else
		{
			if (Self->GetMesh()->GetAnimInstance())
			{
				FRotator AddRotation;
				AActor* CharActor = (AActor*)Self;
				AddRotation = FRotator(0, Self->GetMesh()->GetAnimInstance()->GetCurveValue(FName(TEXT("RotationAmount"))) *
					UKismetMathLibrary::SafeDivide(UGameplayStatics::GetWorldDeltaSeconds(Self), 0.0333f), 0);
				//jesli sie NIE porusza to wykonaj to:
				if (RotationModeC == CALS_RotationMode::Aiming)
				{
					//IF Moving==FALSE [AND] RotationMode==Aiming (Limit Rotation)
					LimitRotationFast(-100, 100, 20);
					if (abs(Self->GetMesh()->GetAnimInstance()->GetCurveValue(FName(TEXT("RotationAmount")))) > 0.001)
					{
						CharActor->AddActorWorldRotation(UKismetMathLibrary::Conv_RotatorToQuaternion(AddRotation), false);
						TargetRotationC = Self->GetActorRotation();
					}
				}
				else if (!IsPlayingRootMotion())
				{
					SmoothedCharRotation
					(
						FRotator(0.0, GetControlRotation().Yaw, 0.0),
						1000,
						FMath::Lerp<float>
						(
							CapsuleRotationRateInIdle,
							0.5,
							GetMesh()->GetAnimInstance()->GetSlotMontageGlobalWeight(TEXT("BaseLayer")) +
							GetMesh()->GetAnimInstance()->GetCurveValue(TEXT("BasePose_MeleeCombat"))
						),
						true
					);

				}
			}
		}
	}
	else if (MovementActionC == CALS_MovementAction::Rolling && HasMovementInputC == true)
	{
		SmoothedCharRotation(FRotator(0, LastMovementInputRotationC.Yaw, 0), 0, 2, true);
	}
}







