
#include "AGLS_AI_AnimInstanceBase.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/Vector.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"

#define KML UKismetMathLibrary

float UAGLS_AI_AnimInstanceBase::VectorLenghtXY(FVector In)
{
	const FVector VectorXY = FVector(In.X, In.Y, 0.0);
	return VectorXY.Length();
}

// INITIALIZE ANIM INSTANCE
void UAGLS_AI_AnimInstanceBase::NativeInitializeAnimation()
{
	CharacterC = Cast<ACharacter>(TryGetPawnOwner()); // Get Main Character Reference
	if (CharacterC)
	{
		MovementComp = CharacterC->GetCharacterMovement(); // Try Get Character Movement Component
	}
}

// TICK EVENT
void UAGLS_AI_AnimInstanceBase::NativeUpdateAnimation(float DeltaSeconds)
{
	CreateOverlayPosesModeState(); // Create Overlay Poses Modes
}

void UAGLS_AI_AnimInstanceBase::CreateOverlayPosesModeState()
{
	if (RotationModeC == CALS_RotationMode::Aiming)
	{
		OverlayPosesType = CALS_OverlayPosesType::Aiming;
	}
	else
	{
		if (OverlayPosesType == CALS_OverlayPosesType::Aiming)
		{
			OverlayPosesType = CALS_OverlayPosesType::Ready;
			timer1 = ReadyStateDuration;
		}

		if (OverlayPosesType == CALS_OverlayPosesType::Ready)
		{
			timer1 = FMath::Clamp<float>(timer1 - dt, 0.0, ReadyStateDuration);

			if (timer1 < 0.2 || 
				(timer1 < (ReadyStateDuration * 0.5) && IsMovingC) || 
				GaitC == CALS_Gait::Sprinting || 
				MovementActionC != CALS_MovementAction::None || 
				MovementStateC == CALS_MovementState::InAir)
			{
				OverlayPosesType = CALS_OverlayPosesType::Relaxed;
				timer1 = 0.0;
			}
		}
	}
}


FTransformTrajectory UAGLS_AI_AnimInstanceBase::GenerateTrajectory_Implementation(int HistoryCount, float InPredictionInterval, int PreditionCount)
{
	FPoseSearchTrajectoryData ConfigData = TrajectoryConfigIdle;
	if (SpeedC > 1.0)
	{
		ConfigData = TrajectoryConfigMoving;
	}

	FTransformTrajectory OutTrajectory;

	//UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory

	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(this, ConfigData, dt, Trajectory, PreviousDesiredControllerYawC, OutTrajectory, -1.0, HistoryCount, InPredictionInterval, PreditionCount);
	OutTrajectory = Trajectory;

	FutureVelocityC = (Trajectory.GetSampleAtTime(0.5, false).Position - Trajectory.GetSampleAtTime(0.4, false).Position) * 10.0;

	return OutTrajectory;
}

void UAGLS_AI_AnimInstanceBase::MakeIsCollideValue(TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjects, int DebugIndex, float DebugTime, bool IgnoreCharacters)
{
	if (IsValid(MovementComp) == false) { return; }

	const FVector MainLoc = TryGetPawnOwner()->GetActorLocation();

	const FVector VelocityDeltaForce = VelocityC * ((dt * 2) / TimeDilatationC);
	const FVector AccelerationDeltaForce = MovementComp->GetCurrentAcceleration() * ((dt * dt * 2) / TimeDilatationC);
	const FVector CapsuleSizeOffset = FVector(0, 0, CharacterC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight_WithoutHemisphere());
	const FVector MeshPosition = MainLoc - (CharacterC->GetActorUpVector() * CharacterC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	
	TArray<AActor*> ActorsToIgnore = {};
	ActorsToIgnore.Add(TryGetPawnOwner());
	EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
	if (DebugIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
	if (DebugIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
	FHitResult TraceResult;

	const bool HitWall = UKismetSystemLibrary::SphereTraceSingleForObjects(CharacterC, MainLoc + VelocityDeltaForce + AccelerationDeltaForce + CapsuleSizeOffset, MainLoc + VelocityDeltaForce + AccelerationDeltaForce - 
		CapsuleSizeOffset, CharacterC->GetCapsuleComponent()->GetScaledCapsuleRadius(), TraceObjects, false, ActorsToIgnore, DebugTrace, TraceResult, true, FColor::Cyan, FColor::Red, DebugTime);

	if (HitWall == true)
	{
		if (DebugIndex > 0)
		{
			int32 MesKey = KML::Round((TryGetPawnOwner()->GetActorLocation().X + TryGetPawnOwner()->GetActorLocation().Y) * 0.1);
			GEngine->AddOnScreenDebugMessage(MesKey, DebugTime, FColor::Red, TraceResult.GetComponent()->GetName());
		}

		if (IgnoreCharacters == true)
		{
			ACharacter* HitChar = Cast<ACharacter>(TraceResult.GetActor());
			if (HitChar) { CapsuleCollidingC = false; return; }
		}
		if (TraceResult.ImpactPoint.Z - MeshPosition.Z > MovementComp->MaxStepHeight && (MovementComp->MaxWalkSpeed - SpeedC) > (MovementComp->MaxWalkSpeed * 0.3))
		{
			CapsuleCollidingC = true; return;
		}
	}

	CapsuleCollidingC = false;
	return;
}

void UAGLS_AI_AnimInstanceBase::UpdateEssentialValues()
{
	bool HasVelocity = false;

	if (TryGetPawnOwner())
	{
		CharacterTransformC = TryGetPawnOwner()->GetActorTransform();

		if (MovementComp)
		{
			//Analyze basic movement component values
			AccelerationC = MovementComp->GetCurrentAcceleration();
			VelocityLastFrameC = VelocityC;
			VelocityC = MovementComp->Velocity;
			SpeedC = VelocityC.Size2D();
			HasVelocity = SpeedC > 5.0;
			if (HasVelocity) LastNonZeroVelocityC = VelocityC;
		}
		// Create IsMoving State
		const float MoveTollerance = UKismetMathLibrary::Lerp(GetCurveValue(TEXT("BasePose_Cover")), 0.1, 0.2);
		IsMovingC = UKismetMathLibrary::NotEqual_VectorVector(VelocityC, FVector(0.0, 0.0, 0.0), MoveTollerance) 
			&& UKismetMathLibrary::NotEqual_VectorVector(FutureVelocityC, FVector(0.0, 0.0, 0.0), MoveTollerance);
		// Update Base Rotation Values
		AimYawRateC = (UKismetMathLibrary::NormalizedDeltaRotator(AimingRotationC, PrevAimingRotationC).Yaw) / dt;
		PrevAimingRotationC = TryGetPawnOwner()->GetControlRotation();
		//Calculate Root Rotation Speed
		RootYawChangeSpeedC = UKismetMathLibrary::FInterpTo(RootYawChangeSpeedC, UKismetMathLibrary::NormalizedDeltaRotator(RootTransformC.Rotator(), PrevRootTransform.Rotator()).Yaw / dt, dt, 6.0);
	}
}

void UAGLS_AI_AnimInstanceBase::SetRootTransform_Implementation() // RootTransform is not be able to set by using C++, becouse we need reference to Anim Graph Node 'Root Bone Offset'
{
}

void UAGLS_AI_AnimInstanceBase::UpdateRotationValues_Implementation()
{
}

void UAGLS_AI_AnimInstanceBase::UpdateCoverValues_Implementation()
{
	if (CharacterC && MovementComp)
	{
		bool PrevCoverDirection = false;

		const FVector AccelerationXY = FVector(MovementComp->GetCurrentAcceleration().X, MovementComp->GetCurrentAcceleration().Y, 0.0);
		if (AccelerationXY.Length() > 5.0)
		{
			FVector AccDirection = AccelerationXY; AccDirection.Normalize();
			float DotValue = KML::Dot_VectorVector(CharacterC->GetActorRightVector(), AccDirection);
			if (abs(DotValue) > 0.02)
			{
				PrevCoverDirection = LeftSideCoverC;
				LeftSideCoverC = DotValue < 0.0;
				if (LeftSideCoverC != PrevCoverDirection)
				{
					CoverDirectionChangedC = true;
					timer2 = 0.25;
				}
			}
		}
	}
	if (CoverDirectionChangedC)
	{
		timer2 = FMath::Clamp<float>(timer2 - dt, 0.0, 0.25);
		if (timer2 <= 0.02)
		{
			CoverDirectionChangedC = false;
			timer2 = -1.0;
		}
	}
}

// FOOTS IK Core Functions

void UAGLS_AI_AnimInstanceBase::UpdateFootsIK_Implementation(ECollisionChannel TraceChannel, bool UseFootsLock, float TraceAboveFoot, float TraceBelowFoot, float FootHeight, int ActiveOnLOD, int TraceDebugIndex, float DebugTime)
{
	const FVector ZeroV = FVector(0, 0, 0);
	const FRotator ZeroR = FRotator(0, 0, 0);

	FVector FootOffset_L_Target = FVector(0, 0, 0);
	FVector FootOffset_R_Target = FVector(0, 0, 0);

	int LOD_Index = static_cast<int>(LOD_State);
	if (LOD_Index < ActiveOnLOD)
	{
		if (MovementStateC == CALS_MovementState::InAir)
		{
			SetPelvisIK_Offset(ZeroV, ZeroV);
			ResetIK_Offsets(15.0);
		}
		else if (MovementStateC != CALS_MovementState::Ragdoll)
		{
			SetFootOffsets(
				FootIK_L_CurveName, 
				TEXT("ik_foot_l"), 
				TEXT("root"), 
				FootOffset_L_Target, 
				FootOffset_L_LocC, 
				FootOffset_L_RotC, 
				TraceChannel, 
				TraceAboveFoot, 
				TraceBelowFoot, 
				FootHeight, 
				TraceDebugIndex, 
				DebugTime
			);

			SetFootOffsets(
				FootIK_R_CurveName,
				TEXT("ik_foot_r"),
				TEXT("root"),
				FootOffset_R_Target,
				FootOffset_R_LocC,
				FootOffset_R_RotC,
				TraceChannel,
				TraceAboveFoot,
				TraceBelowFoot,
				FootHeight,
				TraceDebugIndex,
				DebugTime
			);

			SetPelvisIK_Offset(FootOffset_L_Target, FootOffset_R_Target);
		}
	}
	else
	{
		//Clear All IK Offsets
		FootOffset_L_LocC = ZeroV;
		FootLock_L_Location = ZeroV;
		FootOffset_L_RotC = ZeroR;
		//Clear All IK Offsets
		FootOffset_R_LocC = ZeroV;
		FootLock_R_Location = ZeroV;
		FootOffset_R_RotC = ZeroR;
	}
}

bool UAGLS_AI_AnimInstanceBase::SetFootOffsets(FName Enable_FootIK_Curve, FName IKFootBone, FName RootBone, 
	UPARAM(ref)FVector& CurrentLocationTarget, UPARAM(ref)FVector& CurrentLocationOffset, UPARAM(ref)FRotator& CurrentRotationOffset, 
	ECollisionChannel TraceChannel, float TraceAboveFoot, float TraceBelowFoot, float FootHeight, int TraceDebugIndex, float DebugTime)
{
	//Only update Foot IK offset values if the Foot IK curve has a weight. If it equals 0, clear the offset values.
	if (GetCurveValue(Enable_FootIK_Curve) > 0.0  && IsValid(MovementComp))
	{
		FVector IK_Foot_FloorLocation = GetOwningComponent()->GetSocketLocation(IKFootBone);
		IK_Foot_FloorLocation.Z = GetOwningComponent()->GetSocketLocation(RootBone).Z;
		FRotator TargetRotationOffset = FRotator(0, 0, 0);

		ETraceTypeQuery TraceTypeQuery = UEngineTypes::ConvertToTraceType(TraceChannel);
		TArray<AActor*> ActorsToIgnore = {};
		EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
		if (TraceDebugIndex == 1) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
		if (TraceDebugIndex == 2) { DebugTrace = EDrawDebugTrace::ForDuration; }
		FHitResult TraceResult;

		//Step 1: Trace downward from the foot location to find the geometry. If the surface is walkable, save the Impact Location and Normal.
		bool TraceValid = UKismetSystemLibrary::SphereTraceSingle(
			TryGetPawnOwner(), 
			IK_Foot_FloorLocation + FVector(0, 0, TraceAboveFoot), 
			IK_Foot_FloorLocation - FVector(0, 0, TraceBelowFoot), 
			FootTraceRadius,
			TraceTypeQuery, 
			false, 
			ActorsToIgnore, 
			DebugTrace, 
			TraceResult, 
			true, 
			FColor::Blue, 
			FColor::Green, 
			DebugTime
		);

		if (TraceValid && MovementComp->IsWalkable(TraceResult))
		{
			FVector ImpactPoint = TraceResult.ImpactPoint;
			FVector ImpactNormal = TraceResult.ImpactNormal;

			//Step 1.1: Find the difference in location from the Impact point and the expected (flat) floor location. 
			// These values are offset by the nomrmal multiplied by the foot height to get better behavior on angled surfaces.
			CurrentLocationTarget = (ImpactPoint + (ImpactNormal * FootHeight)) - (IK_Foot_FloorLocation + (FVector(0, 0, 1) * FootHeight));

			//Step 1.2: Calculate the Rotation offset by getting the Atan2 of the Impact Normal.
			TargetRotationOffset = FRotator(KML::DegAtan2(ImpactNormal.X, ImpactNormal.Z) * -1.0, 0.0, KML::DegAtan2(ImpactNormal.Y, ImpactNormal.Z));
		}

		//Step 2: Interp the Current Location Offset to the new target value. Interpolate at different speeds based on whether the new target is above or below the current one.
		if (CurrentLocationOffset.Z > CurrentLocationTarget.Z)
		{
			CurrentLocationOffset = KML::VInterpTo(CurrentLocationOffset, CurrentLocationTarget, dt, 30.0);
		}
		else
		{
			CurrentLocationOffset = KML::VInterpTo(CurrentLocationOffset, CurrentLocationTarget, dt, 15.0);
		}
		//Step 3: Interp the Current Rotation Offset to the new target value.
		CurrentRotationOffset = KML::RInterpTo(CurrentRotationOffset, TargetRotationOffset, dt, 30.0);
		return true;
	}
	else
	{
		CurrentLocationOffset = FVector(0, 0, 0);
		CurrentRotationOffset = FRotator(0, 0, 0);
		return false;
	}
}

void UAGLS_AI_AnimInstanceBase::SetPelvisIK_Offset(FVector FootOffset_L_Target, FVector FootOffset_R_Target)
{
	const FVector ZeroV = FVector(0, 0, 0);

	//Calculate the Pelvis Alpha by finding the average Foot IK weight. If the alpha is 0, clear the offset.
	PelvisOffsetAlphaC = (GetCurveValue(FootIK_L_CurveName) + GetCurveValue(FootIK_R_CurveName)) / 2.0;
	FVector PelvisTarget = FVector(0, 0, 0);

	if (PelvisOffsetAlphaC > 0.02)
	{
		//Step 1: Set the new Pelvis Target to be the lowest Foot Offset
		if (FootOffset_L_Target.Z < FootOffset_R_Target.Z)
		{
			PelvisTarget = FootOffset_L_Target;
		}
		else
		{
			PelvisTarget = FootOffset_R_Target;
		}
		//Step 2: Interp the Current Pelvis Offset to the new target value. 
		// Interpolate at different speeds based on whether the new target is above or below the current one.
		if (PelvisTarget.Z > PelvisOffsetC.Z)
		{
			PelvisOffsetC = KML::VInterpTo(PelvisOffsetC, PelvisTarget, dt, 10.0);
		}
		else
		{
			PelvisOffsetC = KML::VInterpTo(PelvisOffsetC, PelvisTarget, dt, 15.0);
		}
	}
	else
	{
		PelvisOffsetC = ZeroV;
	}
}

//Interp Foot IK offsets back to 0
void UAGLS_AI_AnimInstanceBase::ResetIK_Offsets(float InterpSpeed)
{
	FootOffset_L_LocC = KML::VInterpTo(FootOffset_L_LocC, FVector(0, 0, 0), dt, InterpSpeed);
	FootOffset_R_LocC = KML::VInterpTo(FootOffset_R_LocC, FVector(0, 0, 0), dt, InterpSpeed);

	FootOffset_L_RotC = KML::RInterpTo(FootOffset_L_RotC, FRotator(0, 0, 0), dt, InterpSpeed);
	FootOffset_R_RotC = KML::RInterpTo(FootOffset_R_RotC, FRotator(0, 0, 0), dt, InterpSpeed);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void UAGLS_AI_AnimInstanceBase::UpdateLayeringValues()
{
	BasePoseN = GetCurveValue(TEXT("BasePose_N"));
	BasePoseCLF = GetCurveValue(TEXT("BasePose_CLF"));

	Hand_R = GetCurveValue(TEXT("Layering_Hand_R"));
	Hand_L = GetCurveValue(TEXT("Layering_Hand_L"));

	ArmR_LS = GetCurveValue(TEXT("Layering_Arm_R_LS"));
	ArmL_LS = GetCurveValue(TEXT("Layering_Arm_L_LS"));

	ArmR_MS = (1 - UKismetMathLibrary::FFloor(ArmR_LS)) * 1.0;
	ArmL_MS = (1 - UKismetMathLibrary::FFloor(ArmL_LS)) * 1.0;
}

//  _________________________________________________  MOTION MATCHING MOVEMENT ANALYZE _________________________________________________________________________________ START
bool UAGLS_AI_AnimInstanceBase::IsStarting()
{
	bool TagMatched = false;
	if (CurrentDatabaseTags.Num() > 0)
	{
		if (CurrentDatabaseTags.Contains(TEXT("Pivots")) == true) { TagMatched = true; }
	}
	return IsMovingC && !TagMatched && (VectorLenghtXY(FutureVelocityC) >= (VectorLenghtXY(VelocityC) + IsStartingSpeedOffset));
}

bool UAGLS_AI_AnimInstanceBase::IsStopping()
{
	bool TagMatched = false;
	if (CurrentDatabaseTags.Num() > 0)
	{
		if (CurrentDatabaseTags.Contains(TEXT("Pivots")) == true) { TagMatched = true; }
	}
	return !IsMovingC && !TagMatched && (VectorLenghtXY(FutureVelocityC) < 10.0 && VectorLenghtXY(VelocityC) > 10.0);
}

bool UAGLS_AI_AnimInstanceBase::IsPivoting()
{
	FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(UKismetMathLibrary::MakeRotFromX(FutureVelocityC), UKismetMathLibrary::MakeRotFromX(VelocityC));
	return abs(DeltaRot.Yaw) >= IsPivotingDeltaTrigger;
}

bool UAGLS_AI_AnimInstanceBase::ShouldSpinTransition()
{
	const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(CharacterTransformC.Rotator(), RootTransformC.Rotator());
	bool TagMatched = false;
	if (CurrentDatabaseTags.Num() > 0)
	{
		if (CurrentDatabaseTags.Contains(TEXT("Pivots")) == true) { TagMatched = true; }
	}
	return abs(DeltaRot.Yaw) >= IsSpinningDeltaTrigger && SpeedC >= 100.0 && !TagMatched;
}

bool UAGLS_AI_AnimInstanceBase::JustLanedLight()
{
	return JustLandedC  == true && abs(LandVelocityC.Z) < 700.0;
}

bool UAGLS_AI_AnimInstanceBase::JustLanedHeavy()
{
	return JustLandedC == true && abs(LandVelocityC.Z) >= 700.0;
}

bool UAGLS_AI_AnimInstanceBase::JustLanedNeutral()
{
	return JustLandedC;
}

bool UAGLS_AI_AnimInstanceBase::ShouldTurnInPlace_Implementation()
{
	const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(CharacterTransformC.Rotator(), RootTransformC.Rotator());
	bool RotDeltaOutOfRange = false;
	if (RotationModeC != CALS_RotationMode::Aiming)
	{
		RotDeltaOutOfRange = abs(DeltaRot.Yaw) >= 60.0;
	}
	return !IsMovingC && RotDeltaOutOfRange && MovementStateC == CALS_MovementState::Grounded && GetCurveValue(TEXT("Enable_Transition")) > 0.5 && abs(AimYawRateC) < 12.0;
}

//  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  MOTION MATCHING MOVEMENT ANALYZE ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ END!


int UAGLS_AI_AnimInstanceBase::GetOffsetRootRotationMode_Implementation()
{
	return 0;
}

int UAGLS_AI_AnimInstanceBase::GetOffsetRootLocationMode_Implementation()
{
	if (IsSlotActive(TEXT("BaseLayer")) || IsSlotActive(TEXT("BaseLayer-LowerPiority")) || CapsuleCollidingC) { return 3; }

	if (MovementStateC == CALS_MovementState::Grounded || MovementStateC == CALS_MovementState::Crawl)
	{
		if (IsMovingC) { return 1; }
		else return 3;
	}
	return 3;
}

float UAGLS_AI_AnimInstanceBase::GetOffsetRootTranslationHalfLife()
{
	if (IsMovingC)
	{ return 0.2; }
	else
	{ return 0.1; }
}





float UAGLS_AI_AnimInstanceBase::Get_MMBlendTime()
{
	if (MovementStateC == CALS_MovementState::InAir)
	{
		if (VelocityC.Z > 100.0) { return 0.15; }
		else { return 0.5; }
	}
	return 0.35;
}

void UAGLS_AI_AnimInstanceBase::UpdateMotionMatchingPostSelection(UPARAM(ref) FAnimUpdateContext& Context, UPARAM(ref) FAnimNodeReference& Node)
{
	FMotionMatchingAnimNodeReference MM_Node;
	FPoseSearchBlueprintResult SearchResult;
	bool CastResult = false;
	bool SearchValid = true;

	UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNodePure(Node, MM_Node, CastResult); // Get Node Reference

	if (CastResult == true)
	{
		UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(MM_Node, SearchResult, SearchValid);
		CurrentSelectedDatabase = const_cast<UPoseSearchDatabase*>(SearchResult.SelectedDatabase.Get());
	}

}

bool UAGLS_AI_AnimInstanceBase::EnableSterring()
{
	return MovementStateC == CALS_MovementState::InAir || IsMovingC == true;
}

FQuat UAGLS_AI_AnimInstanceBase::GetDesiredFacing()
{
	return Trajectory.GetSampleAtTime(0.5, false).Facing;
}


