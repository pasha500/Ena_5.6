


#include "AGLS_AI_ZombieAnimInstCore.h"


float MapRange(float Value, float InA, float InB, float OutA, float OutB)
{
	return FMath::GetMappedRangeValueClamped(FVector2D(InA, InB), FVector2D(OutA, OutB), Value);
}

float MapRangeNormalized(float Value, float InA, float InB)
{
	return FMath::GetMappedRangeValueClamped(FVector2D(InA, InB), FVector2D(0.0, 1.0), Value);
}



void UAGLS_AI_ZombieAnimInstCore::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (TryGetPawnOwner())
	{
		IAGLS_AI_CharacterInterface* MainInterface = Cast<IAGLS_AI_CharacterInterface>(TryGetPawnOwner());
		if (MainInterface)
		{
			CharInterface = MainInterface;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Interface 'IAGLS_AI_CharacterInterface' from TryGetPawnOwner() NOT valid durning initialize animation"));
		}
	}

	IsPivotingDeltaTrigger = 20.0;
	IsStartingSpeedOffset = 20.0;

	BlendProfile_SlowHead = GetBlendProfileByName("SlowUpperBodyWeigth");
	BlendProfile_FastFoots = GetBlendProfileByName("FastFootsSlowHead");

	LandVelocityC = FVector(999, 999, 9999);

}



void UAGLS_AI_ZombieAnimInstCore::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	UpdatePerFrameOnTick();
}



void UAGLS_AI_ZombieAnimInstCore::UpdatePerFrameOnTick_Implementation()
{
	if (!CharacterC || dt == 0.0) return;

	SetCorrectedIsCollideValueC();
	FootsStepsPlayerC();
	SetAimAtEnemyPropertiesC();
	SetAlphaForNearAttackingC();
}



void UAGLS_AI_ZombieAnimInstCore::FootsStepsPlayerC_Implementation()
{
	if (LOD_State == AGLS_LOD_State::LOD3) return;
	if (MovementStateC != CALS_MovementState::Grounded) return;

	float FootTolleranceOffset = FMath::GetMappedRangeValueClamped(FVector2D(100, 500), FVector2D(300, 600), VelocityC.Length());

	USoundBase* CurrentSoundClass = nullptr;
	if (FootStepSoundClasses.Num() == 3)
	{
		switch (GaitC)
		{
		case CALS_Gait::Walking:
			CurrentSoundClass = FootStepSoundClasses[0];
			break;
		case CALS_Gait::Running:
			CurrentSoundClass = FootStepSoundClasses[1];
			break;
		case CALS_Gait::Sprinting:
			CurrentSoundClass = FootStepSoundClasses[2];
			break;
		default:
			CurrentSoundClass = FootStepSoundClasses[0];
			break;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FootsStepsPlayerC_Implementation: Array FootStepSoundClasses does have a exacly 3 indices. "));
	}

	//FOOT RIGHT
	SingleFootStepDetectionC(CurrentSoundClass, FootPlantedTime_R, PrevFootLocation_R, RootTransformC, "foot_r", "FootSpeed_R", dt, 8.0, 10.0, 45, FootTolleranceOffset);

	//FOOT LEFT
	SingleFootStepDetectionC(CurrentSoundClass, FootPlantedTime_L, PrevFootLocation_L, RootTransformC, "foot_l", "FootSpeed_L", dt, 8.0, 10.0, 45, FootTolleranceOffset);
}



void UAGLS_AI_ZombieAnimInstCore::SingleFootStepDetectionC(USoundBase* SoundClass, UPARAM(ref) float& PlantedTime, UPARAM(ref)FVector& PrevFootLocation, FTransform RootTransform, 
	FName FootBone, FName FootCurveSpeed, float DeltaTime, float TraceOffsetUp, float TraceOffsetDown, float FootNotMoveTollerance, float FootNotMoveOffset, FLinearColor DebugColor)
{
	if (DeltaTime <= 0.0) return;

	//Calculate Foot Velocity
	const FVector SocketLocation = GetOwningComponent()->GetSocketLocation(FootBone);
	FVector CurrentFootVelocity = (SocketLocation - PrevFootLocation) / DeltaTime;

	PrevFootLocation = SocketLocation;

	//Check Velocities
	bool CurveVelocityCondition = GetCurveValue(FootCurveSpeed) < FootNotMoveTollerance;

	if (CurveVelocityCondition && CurrentFootVelocity.Length() < (FootNotMoveTollerance + FootNotMoveOffset))
	{
		if (PlantedTime < 0.05)
		{
			//Spawn Sound from CLASS
			const float VolumeScale = FMath::GetMappedRangeValueClamped(FVector2D(30, 300), FVector2D(0.4, 0.7), VelocityC.Length());

			if (SoundClass)
			{

			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Foots Steps Sounds - The SoundBase class is empty."));
			}
			PlantedTime = 1.0;
		}
	}
	else
	{
		if (PlantedTime >= 0.05)
		{
			PlantedTime = FMath::FInterpTo(PlantedTime, 0.0, DeltaTime, 16.0);
		}
	}
}



void UAGLS_AI_ZombieAnimInstCore::UpdateValuesFromCharacterC()
{
	bool bStanceChanged = false;

	CALS_MovementState InPrevMovementState;
	const CALS_MovementState PrevMovementState = MovementStateC;
	const CALS_Gait PrevGait = GaitC;
	const CALS_Stance PrevStance = StanceC;

	//IAGLS_AI_CharacterInterface* CharInterface = Cast<IAGLS_AI_CharacterInterface>(CharacterC);
	if (!CharInterface) return;

	//Interface Call
	CharInterface->Execute_BPI_AI_Get_CurrentStatesSafe(TryGetPawnOwner(), MovementStateC, InPrevMovementState, MovementActionC, RotationModeC, GaitC, StanceC); // <------ INTERFACE CALL

	bool GaitChanged = PrevGait != GaitC;
	if (PrevGait != GaitC && PrevGait == CALS_Gait::Sprinting && PrevGait == CALS_Gait::Running) { GaitChanged = false; }

	//Interrupt on Motion Matching Condition
	InterruptOnDatabaseC = GaitChanged || PrevStance != StanceC || PrevMovementState != MovementStateC || PrevCapsuleColliding != CapsuleCollidingC || IsMovingC != PrevIsMovingC || StanceTransitionC;

	PrevIsMovingC = IsMovingC;
	PrevCapsuleColliding = CapsuleCollidingC;
	bStanceChanged = PrevStance != StanceC;

	int DatabaseIndex = CurrentDatabaseTags.Find("Run_Stops");
	if (DatabaseIndex != -1 && !IsMovingC) { InterruptOnDatabaseC = false; }


	FVector Acc;
	bool HasInput;
	//Interface Call
	CharInterface->Execute_BPI_AI_Get_EssentialValuesSafe(TryGetPawnOwner(), Acc, HasInput, AimingRotationC); // <------ INTERFACE CALL

	if (!StanceTransitionC)
	{
		StanceTransitionC = bStanceChanged;
		if (bStanceChanged) StanceTransitionTimer = 0.1;
	}

	//Simple Timer
	if (StanceTransitionTimer > 0.0)
	{
		StanceTransitionTimer += dt;
		if (StanceTransitionTimer >= 0.1)
		{
			StanceTransitionTimer = -1.0;
			StanceTransitionC = false;
		}
	}

	if (GaitC == CALS_Gait::Sprinting) IsPivotingDeltaTrigger = 20.0;
	else IsPivotingDeltaTrigger = 30.0;

	//Change Bump Reaction
	if (PlayBumpReaction)
	{
		const float DistValue = GetCurveValue("DistanceCurve");

		if (DistValue > 0.8)
		{
			PlayBumpReaction = false;
		}
		else if (HasInput && DistValue > 0.2)
		{
			PlayBumpReaction = false;
		}
	}

}



void UAGLS_AI_ZombieAnimInstCore::SetCorrectedIsCollideValueC(int DebugModeIndex, float DebugTime, float CapRadiusBias, float CapHeightBias)
{

	if (!CharacterC) return;

	if ((VelocityC.Length() > 4.0 || FutureVelocityC.Length() > 4.0) && UseCollideDatabasesForCharact)
	{
		EDrawDebugTrace::Type DebugMode = EDrawDebugTrace::None;
		if (DebugModeIndex > 0) DebugMode = EDrawDebugTrace::ForOneFrame;

		FHitResult TraceResult;
		const bool Colliding = UKismetSystemLibrary::CapsuleTraceSingleForObjects(CharacterC, 
			CharacterC->GetActorLocation() + FVector(0, 0, 1), 
			CharacterC->GetActorLocation() - FVector(0, 0, 1), 
			CharacterC->GetCapsuleComponent()->GetScaledCapsuleRadius() + CapRadiusBias, 
			CharacterC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + CapHeightBias, 
			CollisionObject, false, {}, DebugMode, TraceResult, true, FColor::Black, FColor::Red, DebugTime);

		if (Colliding) { CapsuleCollidingC = true; return; }
	}
	else
	{
		MakeIsCollideValue(CollisionObject, DebugModeIndex, DebugTime, !UseCollideDatabasesForCharact);
		return;
	}

}



void UAGLS_AI_ZombieAnimInstCore::SetAimAtEnemyPropertiesC()
{
	if (LOD_State >= AGLS_LOD_State::LOD2) { LookAtEnemyAlpha = 0.0; RandomJiggleAlpha = 0.0; return; }

	IALS_HumanAI_InterfaceCpp* ControllerInterface = Cast<IALS_HumanAI_InterfaceCpp>(CharacterC);
	if (!ControllerInterface) return;
	bool bDetectedEnemy; float DetectedEnemyTime; ACharacter* EnemyActor = nullptr;
	ControllerInterface->Execute_HAI_GetControllerSmallValues(CharacterC, bDetectedEnemy, DetectedEnemyTime, EnemyActor);

	float DetectedEnemySpeed = (MapRangeNormalized(DetectedEnemyTime, 0.1, 0.8) - PrevDetectedEnemyTime)/dt;
	PrevDetectedEnemyTime = DetectedEnemyTime;

	const float Target = MapRangeNormalized(DetectedEnemyTime, 0.2, 0.5);
	float InterpSpeed = 0.4;  
	if (Target > 0.65) { InterpSpeed = 0.6; }

	LookAtEnemyAlpha = FMath::FInterpConstantTo(LookAtEnemyAlpha, Target, dt, InterpSpeed);

	//Then 01
	if (EnemyActor)
	{
		LookAtEnemyLocation = EnemyActor->GetMesh()->GetSocketLocation("head");
	}

	//Then 02
	if (DetectedEnemySpeed > 0.2)
	{ RandomJiggleAlpha = 1.0; }
	else
	{ RandomJiggleAlpha = 0.0; }

	//Then 03
	if (EnemyActor)
	{
		FVector LookingDirection = (CharacterC->GetActorLocation() - EnemyActor->GetActorLocation()); LookingDirection.Normalize();
		//FRotator LookAt = FRotationMatrix::MakeFromX(LookingDirection).Rotator();
		const FVector RootRotation = FRotationMatrix(RootTransformC.Rotator()).GetScaledAxis(EAxis::Y);
		const float Angle = FVector::DotProduct(LookingDirection, RootRotation);

		LookingSweepTime = MapRange(Angle, -1.0, 1.0, 1.0, 0.0);
	}
	else
	{
		LookingSweepTime = FMath::FInterpTo(LookingSweepTime, 0.0, dt, 8.0);
	}
}



void UAGLS_AI_ZombieAnimInstCore::SetAlphaForNearAttackingC(FVector2D DistanceRangeIn, FVector2D EnemyDetectRangeIn)
{
	
	IALS_HumanAI_InterfaceCpp* ControllerInterface = Cast<IALS_HumanAI_InterfaceCpp>(CharacterC);
	if (!ControllerInterface) return;

	bool bDetectedEnemy; float DetectedEnemyTime; ACharacter* EnemyActor = nullptr;

	ControllerInterface->Execute_HAI_GetControllerSmallValues(CharacterC, bDetectedEnemy, DetectedEnemyTime, EnemyActor);
	if (!EnemyActor) { NearEnemyAttackingAlpha = 0.0; }
	else
	{
		NearEnemyAttackingAlpha = FMath::GetMappedRangeValueClamped(EnemyDetectRangeIn, FVector2D(0.0, 1.0), DetectedEnemyTime);
		const float DistToEnemy = FVector::Distance(CharacterC->GetActorLocation(), EnemyActor->GetActorLocation());
		NearEnemyAttackingAlpha = NearEnemyAttackingAlpha * FMath::GetMappedRangeValueClamped(DistanceRangeIn, FVector2D(1.0, 0.0), DistToEnemy);
	}
}

float UAGLS_AI_ZombieAnimInstCore::GetAddtiveSlotWeight()
{
	if (MovementStateC == CALS_MovementState::InAir) { return 0.0; }
	else { return GetSlotNodeGlobalWeight("Mask_AdditiveSlot"); }
}

float UAGLS_AI_ZombieAnimInstCore::GetBaseLayerSlotWeigth()
{
	return GetSlotNodeGlobalWeight("HigherBaseLayer");
}

float UAGLS_AI_ZombieAnimInstCore::GetFootsIK_Alpha()
{
	if (MovementStateC == CALS_MovementState::None || MovementStateC == CALS_MovementState::Grounded) { return 1.0; }
	else { return 0.0; }
}
