


#include "NavClimbingComponentCore.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "CollisionQueryParams.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "MotionWarpingComponent.h"

#if WITH_EDITOR
	#include "Engine/GameViewportClient.h"
#endif

#define KML UKismetMathLibrary
#define KSL UKismetSystemLibrary
#define ActorLoc GetOwner()->GetActorLocation()
#define ActorForward GetOwner()->GetActorForwardVector()
#define ActorRot GetOwner()->GetActorRotation()
#define CurrentCapLocation ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform).GetLocation()
#define NextCapLocation ScaleForwardCapOffset(NextLedgeParams.ActorTransform).GetLocation()
#define CurrentCapRotation ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform).Rotator()
#define NextCapRotation ScaleForwardCapOffset(NextLedgeParams.ActorTransform).Rotator()

static TAutoConsoleVariable<bool> CVarAglsNavClimbShowPath(
	TEXT("AGLS.Components.NavClimb.ShowPath"),
	false,
	TEXT("Shows or hides debugging of the currently generated NavClimbing path, which can be retrieved from the GetPathPointsStruct() function"));

static TAutoConsoleVariable<bool> CVarAglsNavClimbShowOtherShapes(
	TEXT("AGLS.Components.NavClimb.ShowOtherDebugShapes"),
	false,
	TEXT("Shows or hides more debug shapes such as CurrentLedgePosition or NextLedge Position "));

static TAutoConsoleVariable<int32> CVarAglsNavClimbShowTraces(
	TEXT("AGLS.Components.NavClimb.ShowTracesType"),
	0,
	TEXT("Shows all traces executed by NavCLimbComponent. 0 = No Debug, 1 = For One Frame, 2 = For Duration (by default 2.0s)"));


// Sets default values for this component's properties
UNavClimbingComponentCore::UNavClimbingComponentCore()
{
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UNavClimbingComponentCore::BeginPlay()
{
	Super::BeginPlay();
	InitializeComponent();

#if WITH_EDITOR

	// Inicjalizacja Debuggera
	Debugger.Initialize(FVector2D(100.0f, 200.0f), FVector2D(300.0f, 100.0f), 50);

	// Dodanie wykresów
	Debugger.AddGraph({ FName("FollowingTimer"), FLinearColor::Red, 2.0f, FVector2D(0.0f, 1.0f), false });
	Debugger.AddGraph({ FName("MainActionTimer"), FLinearColor::Green, 2.0f, FVector2D(0.0f, 1.0f), false });

#endif
}


FTransform UNavClimbingComponentCore::ScaleForwardCapOffset(FTransform InTransform)
{
	return FTransform(InTransform.GetRotation(), InTransform.GetLocation() + (InTransform.GetRotation().GetForwardVector() * -1.0 * CapConstForwardOffset), FVector(1, 1, 1));
}


float UNavClimbingComponentCore::TimerFinishedTollerance()
{
	return FMath::Clamp<float>(0.96 - KML::MapRangeClamped(MovementSpeedScale, 1.0, 5.0, 0.04, 0.1), 0.85, 0.98);
}


TArray<FClimbNav_SingleClimbPoint> UNavClimbingComponentCore::GetPathPointsStruct()
{
	return PathPointsData;
}


void UNavClimbingComponentCore::SetTransformWhen(FVector NewLocation, FRotator NewRotation, bool bSweep)
{
	if (IsClimbing && !StartedReleaseLedge)
	{
		GetOwner()->SetActorLocationAndRotation(NewLocation, NewRotation.Quaternion(), bSweep);
	}
}


void UNavClimbingComponentCore::InitializeComponent_Implementation()
{
	Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer
		(
			FollowPlayerTimerHandle,
			this,
			&UNavClimbingComponentCore::FollowPlayerTimer,
			FollowingTickInterval,
			true
		);
		LogInfo(TEXT("Follow Player Timer is correctly initialized on component initialization"));
	}
	
}


bool UNavClimbingComponentCore::TryPerformMovingOnClimbPath_Implementation()
{
	//Function config values
	const float DistTollerance01 = 80.0;
	const float WaitTimeScale = 2.0;

	//Make sure pathpoints is not empty
	if (GetPathPointsStruct().Num() < 1) 
	{ 
		LogInfo(TEXT("TryPerformMovingOnClimbPath(): condition GetPathPointsStruct().Num() < 1 was not met. Function abort"));
		return false; 
	}

	//reset global variables values
	CurrentLedgeIndex = 0;
	MovingTime = 0.0;
	IsInterpolatingToNext = false;
	IsInterpolatingToStart = false;
	//set current ledge params as first PathPoint index. Make sure is valid befor set attempt
	if (GetPathPointsStruct().IsValidIndex(0)) { CurrentLedgeParams = GetPathPointsStruct()[0]; }

	bool bIsEndingPoint; float NextActionDuration;
	SwitchToNextPathPoint(bIsEndingPoint, NextActionDuration, true, WaitTimeScale);

	CurrentLedgeIndex = 0; //  <-------------------------------------------------------------------------------------------------- ???

	if ((CurrentLedgeParams.ActorTransform.GetLocation() - GetOwner()->GetActorLocation()).Length() > DistTollerance01)
	{
		LogInfo(TEXT("TryPerformMovingOnClimbPath(): condition with DistTollerance01 is met"));
		IsInterpolatingToStart = true;
		NextLedgeParams = GetPathPointsStruct()[0];

		FTransform CurrentLedge = FTransform(ActorRot, ActorLoc + FVector(0, 0, 60.0) + (ActorForward * 15.0), FVector(1, 1, 1));
		CurrentLedgeParams.LedgeCenter = CurrentLedge;
		CurrentLedgeParams.LedgeLeft = CurrentLedge;
		CurrentLedgeParams.LedgeRight = CurrentLedge;
		CurrentLedgeParams.ActorTransform = GetOwner()->GetActorTransform();
		CurrentLedgeParams.LedgeID = -1;
		CurrentLedgeParams.PointID = -1;

		CurrentLedgeIndex = 0;
		SetTargetActionState();
		PrepareAnimInstParams(true); // Nie wiadomo czy zostawic !!!
		UpdateAnimValuesWhenClimb();
		return true;
	}
	else
	{
		LogInfo(TEXT("TryPerformMovingOnClimbPath(): condition with DistTollerance01 is NOT met, then perform Interpolation To NEXT Ledge"));
		IsInterpolatingToNext = true;

		CurrentLedgeParams = GetPathPointsStruct()[0];
		NextLedgeParams = GetPathPointsStruct()[FMath::Clamp<int>(1,0, GetPathPointsStruct().Num() - 1)];

		SetTargetActionState();
		PrepareAnimInstParams(true); // Nie wiadomo czy zostawic !!!
		UpdateAnimValuesWhenClimb();
		return true;
	}
}


void UNavClimbingComponentCore::SwitchToNextPathPoint(bool& ItsEndingPoint, float& NextActionDelay, bool UpdateTimer, float WaitTimeScale)
{
	if (!CurrentNavStorageActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: SwitchToNextPathPoint() is called but object CurrentNavStorageActor is empty"));
		return;
	}

	CurrentLedgeIndex = FMath::Clamp<int>(CurrentLedgeIndex + 1, -1, GetPathPointsStruct().Num() - 1);

	CurrentLedgeParams = NextLedgeParams;
	if (GetPathPointsStruct().IsValidIndex(CurrentLedgeIndex))
	{
		NextLedgeParams = GetPathPointsStruct()[CurrentLedgeIndex];
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: SwitchToNextPathPoint() tried to set NextLedgeParams as index from GetPathPointsStruct() but is NOT valid"));
		return;
	}

	bool bIsEnd = CurrentLedgeIndex == (GetPathPointsStruct().Num() - 1);

	if (!UpdateTimer) 
	{ 
		ItsEndingPoint = bIsEnd; 
		NextActionDelay = 0.0; 
		return; 
	}

	float FixedWaitTime = WaitTimeBetweenJumps * WaitTimeScale;
	if (CurrentLedgeParams.LedgeID == NextLedgeParams.LedgeID) 
	{ 
		FixedWaitTime = 0.0; 
		CurrentLedgeParams.ActorTransform = GetOwner()->GetActorTransform();
	}

	float ActionTime = 0.5;

	if (GetWorld())
	{
		LogInfo(TEXT("SwitchToNextPathPoint(): preparing to calculate action duration"));
		const FVector LedgeCapLocationA = ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform).GetLocation();
		const FVector LedgeCapLocationB = ScaleForwardCapOffset(NextLedgeParams.ActorTransform).GetLocation();

		ActionTime = KML::MapRangeClamped((LedgeCapLocationA - LedgeCapLocationB).Length(), 50.0, MaxDistanceBetweenLedges, 0.8, 1.2);
		ActionTime = ActionTime / MovementSpeedScale;

		GetWorld()->GetTimerManager().SetTimer //Initialize timer
		(
			MainTimerHandle,
			this,
			&UNavClimbingComponentCore::MainTimer,
			ActionTime,
			false
		);

		TimerDuration = ActionTime; // < ----------------------------------------------------- !!! ----------------------------------------

		if (CurrentLedgeIndex >= 0)
		{
			LogInfo(TEXT("SwitchToNextPathPoint(): CurrentLedgeIndex >= 0 then IsInterpolatingToNext has been set to true"));
			IsInterpolatingToNext = true;

			if (FixedWaitTime > 0.0)
			{
				LogInfo(TEXT("SwitchToNextPathPoint(): FixedWaitTime > 0. then PauseTimer is required"));
				GetWorld()->GetTimerManager().PauseTimer(MainTimerHandle); //PauseTimer

				GetWorld()->GetTimerManager().SetTimer //Initialize timer
				(
					WaitTimerHandle,
					this,
					&UNavClimbingComponentCore::WaitTimerEvent,
					WaitTimeBetweenJumps,
					false
				);
			}

			IsInterpolatingToStart = false;
			ItsEndingPoint = bIsEnd;
			NextActionDelay = FixedWaitTime;
			return;

		}
		else
		{
			ItsEndingPoint = bIsEnd;
			NextActionDelay = 0.0;
			return;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: SwitchToNextPathPoint() tried to initialize timers but GetWorld() is Not valid"));
		return;
	}
}

// -■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■-■ Main System Tick Function
void UNavClimbingComponentCore::UpdateMovementOnPath_Implementation()
{
	if (!IsClimbing) { return; }

	// 00 Check The Main Timer finished counting
	bool MainTimerStateChanged = false;

	if (bMainTimerFinished == true)
	{
		LogInfo(TEXT("UpdateMovementOnPath(): Main Timer is finished counting. Rejestrating change moment "));
		MainTimerStateChanged = true;
		bMainTimerFinished = false;
	}


	// ● Sequence then 01
	if (IsInterpolatingToStart && GetTimerIsActivated(MainTimerHandle))
	{
		FVector NewLoc = KML::VLerp(CurrentCapLocation, NextCapLocation, NormalizedMainTimer());
		FRotator NewRot = KML::RLerp(CurrentCapRotation, NextCapRotation, NormalizedMainTimer(), true);
		
		SetTransformWhen(NewLoc, NewRot, false);
	}

	// ●● Sequence then 02
	if (IsInterpolatingToNext && GetTimerIsActivated(MainTimerHandle))
	{
		FTransform NewTransform = ChooseInterpolationMethod();
		SetTransformWhen(NewTransform.GetLocation(), NewTransform.Rotator(), false);
	}

	// ●●● Sequence then 03
	if (IsInterpolatingToStart || IsInterpolatingToNext)
	{
		//GEngine->AddOnScreenDebugMessage(22, 4.0, FColor::Orange, TEXT(" IsInterpolation Condition == true"), true);

		if ((NormalizedMainTimer() >= TimerFinishedTollerance() || MainTimerStateChanged) && KML::Vector_Distance(NextCapLocation, ActorLoc) <= CorrectDistanceTollerance)
		{
			//GEngine->AddOnScreenDebugMessage(22, 4.0, FColor::Orange, TEXT("NOT UNPAUSED!!!!!!"), true);

			if (!PauseCurrentMove)
			{
				bool bIsEndingPoint; float NextActionDuration;
				SwitchToNextPathPoint(bIsEndingPoint, NextActionDuration, true, 1.0);

				LogInfo(TEXT("UpdateMovementOnPath(): Switching to NEXT path point: "));

				if (NextActionDuration > 0.0)
				{
					LogInfo(TEXT("UpdateMovementOnPath(): Switching to NEXT path point: NextActionDelay is greater that 0.0. This means system is waiting to unpause main timer"));
					SetNewActionState(0);
				}
				else
				{
					LogInfo(TEXT("UpdateMovementOnPath(): Switching to NEXT path point: NextActionDelay == 0.0. System not required pause timer. Then change action states immediately"));
					SetTargetActionState();
					UpdateAnimValuesWhenClimb();
				}
			}
			else
			{
				LogInfo(TEXT("UpdateMovementOnPath(): Switching to NEXT path point: Conditions is correct, but system is currently PAUSED"));
			}
		}
	}

	// ●●●● Sequence then 04
	if (PauseCurrentMove)
	{
		if (GetTimerIsActivated(MainTimerHandle) == false)
		{
			IsInterpolatingToNext = false;
			SetNewActionState(0);
		}
	}

	// ●●●●● Sequence then 05
	if (!PauseCurrentMove)
	{
		if (PauseCurrentMove != PrevPauseMove)
		{
			bool bIsEndingPoint; float NextActionDuration;
			SwitchToNextPathPoint(bIsEndingPoint, NextActionDuration, true, 1.0);

			SetTargetActionState();
			UpdateAnimValuesWhenClimb();
		}
	}

	// ●●●●●● Sequence then 06
	// Interface call
	PrevPauseMove = PauseCurrentMove;

	// ●●●●●●● Sequence then 07
	// Interface call

	// ●●●●●●●● Sequence then 08
	if (Character)
	{
		EMovementMode CurrentMode = Character->GetCharacterMovement()->MovementMode;
		if (CurrentMode == EMovementMode::MOVE_None)
		{
			Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Custom);
		}
		if (CurrentMode == EMovementMode::MOVE_Flying && GetTimerIsActivated(JumpToFirstLedgeHandle) == false)
		{
			LogInfo(TEXT("UpdateMovementOnPath(): Update Movement mode to MOVE_Custom becouse GetTimerIsActivated(JumpToFirstLedgeHandle) == false"));
			Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Custom);
		}
	}
}


FTransform UNavClimbingComponentCore::ChooseInterpolationMethod_Implementation()
{
	if (CurrentLedgeParams.LedgeID == NextLedgeParams.LedgeID)
	{
		return KML::TEase
		(
			ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform),
			ScaleForwardCapOffset(NextLedgeParams.ActorTransform),
			NormalizedMainTimer(),
			EEasingFunc::Linear,
			2.0
		);
	}

	FVector InterpTrajectory = FVector(NormalizedMainTimer());
	if (TrajectoryInterpCurves.Num() > 1)
	{
		UCurveVector* CurveData = TrajectoryInterpCurves.FindRef(NavClimbActionState);
		if (CurveData)
		{
			float CRangeMin; float CRangeMax;
			CurveData->GetTimeRange(CRangeMin, CRangeMax);
			InterpTrajectory = CurveData->GetVectorValue(KML::MapRangeClamped(NormalizedMainTimer(), 0.0, 1.0, CRangeMin, CRangeMax));
		}
	}
	else if (TrajectoryInterpCurves.Num() == 1)
	{
		UCurveVector* CurveData = TrajectoryInterpCurves.FindRef(ENavClimbActionState::None);
		if (CurveData)
		{
			switch (NavClimbActionState)
			{
			case ENavClimbActionState::WalkCycle:
				InterpTrajectory = FVector(CurveData->GetVectorValue(NormalizedMainTimer()).Z);
				break;
			case ENavClimbActionState::ShortJump:
				InterpTrajectory = FVector(CurveData->GetVectorValue(NormalizedMainTimer()).Y);
				break;
			case ENavClimbActionState::LongJump:
				InterpTrajectory = FVector(CurveData->GetVectorValue(NormalizedMainTimer()).X);
				break;
			default:
				InterpTrajectory = FVector(NormalizedMainTimer(), NormalizedMainTimer(), NormalizedMainTimer());
				break;
			}
		}
	}
	FTransform TA = ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform);
	FTransform TB = ScaleForwardCapOffset(NextLedgeParams.ActorTransform);
	return ExtractedInterpolation(TA, TB, InterpTrajectory.X, InterpTrajectory.Y, InterpTrajectory.Z, InterpTrajectory.Z, 0.0);

}


void UNavClimbingComponentCore::SetTargetActionState_Implementation()
{
	float MainDistance = KML::Vector_Distance(CurrentCapLocation, NextCapLocation);

	if (CurrentLedgeParams.LedgeID == NextLedgeParams.LedgeID && MainDistance > 10.0)
	{
		SetNewActionState(1);
	}
	else if (MainDistance > LongJumpMinDistance)
	{
		SetNewActionState(3);
	}
	else if(MainDistance > 20.0)
	{
		SetNewActionState(2);
	}
	else
	{
		SetNewActionState(0);
	}
}


void UNavClimbingComponentCore::UpdateAnimValuesWhenClimb_Implementation()
{
	if (!IsClimbing) { return; }

	FTransform StartTransform = ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform);

	FTransform RelativeT = KML::MakeRelativeTransform(ScaleForwardCapOffset(NextLedgeParams.ActorTransform), StartTransform);

	if (RelativeT.GetLocation().Y > 0.0)
	{
		MovementDirection = EClimbMovementDirection::Right;
	}
	else
	{
		MovementDirection = EClimbMovementDirection::Left;
	}

	FVector2D InDistancesRange = FVector2D(0, 0);

	bool UseLongJump = KML::Vector_Distance(CurrentCapLocation, NextCapLocation) > LongJumpMinDistance;
	if (UseLongJump == true)
	{
		InDistancesRange = FVector2D(100.0, MaxDistanceBetweenLedges * 0.8);
	}
	else
	{
		InDistancesRange = FVector2D(50.0, 100.0);
	}

	const FVector FindLook = KML::GetForwardVector(KML::FindRelativeLookAtRotation(StartTransform, NextCapLocation));

	AnimJumpsParams = FVector
	(
		(FindLook * KML::MapRangeClamped(KML::Vector_Distance(CurrentCapLocation, NextCapLocation), InDistancesRange.X, InDistancesRange.Y, 0.5, 1.0)).Y,
		(FindLook * KML::MapRangeClamped(KML::Vector_Distance(CurrentCapLocation, NextCapLocation), InDistancesRange.X, InDistancesRange.Y, 0.5, 1.0)).Z,
		KML::MapRangeClamped(KML::Vector_Distance(CurrentCapLocation, NextCapLocation), InDistancesRange.X, InDistancesRange.Y, 0.5, 1.0)
	);
}


void UNavClimbingComponentCore::PrepareAnimInstParams_Implementation(bool Enable)
{
	// Override in BLUEPRINT
}


void UNavClimbingComponentCore::PerformFollowing()
{
	if (!FollowPlayerWhenClimb) { return; }

	if (!GetWorld()) { return; }
	ACharacter* TargetPlayer = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	bool bPlayerIsClimbing = GetPlayerIsClimbing(0);

	if(!TargetPlayer) { LogInfo(TEXT("PerformFollowing() WARNING: TargetPlayer object is null. Function cant work correctly")); }

	if (IsClimbing == true && NavClimbActionState == ENavClimbActionState::None && PauseCurrentMove == false && IsValid(TargetPlayer) == true)
	{

		LogInfo(TEXT("PerformFollowing() Following condition is valid: IsClimbing && TargetPlayer valid"));

		const float ToPlayerDist = KML::Vector_Distance(TargetPlayer->GetActorLocation(), Character->GetActorLocation());

		if (ToPlayerDist > ToPlayerCorrectDistance && bPlayerIsClimbing && !PathRefreshedWhenFollow)
		{
			FVector SearchStartLoc = Character->GetActorLocation() + FVector(0, 0, 60);
			FVector SearchEndLoc = KML::VLerp(Character->GetActorLocation(), TargetPlayer->GetActorLocation(), 0.8);

			if (KML::Vector_Distance(SearchEndLoc, Character->GetActorLocation()) > ToPlayerCorrectDistance)
			{
				SearchEndLoc = TargetPlayer->GetActorLocation() + FVector(0,0,1);
			}

			LogInfo(TEXT("PerformFollowing() trying to perform MoveToTargetOnClimbNavigation() function"));
			PathRefreshedWhenFollow = MoveToTargetOnClimbNavigation(SearchStartLoc, SearchEndLoc); // Try activate movement on PathPoints


		}
		else if(bPlayerIsClimbing && PathRefreshedWhenFollow && ToPlayerDist > (ToPlayerCorrectDistance * 2.0))
		{
			PathRefreshedWhenFollow = false;
		}
		else
		{
			FVector SearchStartLoc = Character->GetActorLocation() + FVector(0, 0, 60);
			FVector SearchEndLoc = KML::VLerp(Character->GetActorLocation(), TargetPlayer->GetActorLocation(), 0.8);

			if (KML::Vector_Distance(SearchEndLoc, Character->GetActorLocation()) > ToPlayerCorrectDistance)
			{
				SearchEndLoc = TargetPlayer->GetActorLocation() + FVector(0, 0, 1);
			}

			LogInfo(TEXT("PerformFollowing() trying to perform MoveToTargetOnClimbNavigation() function when 2 conditions failed"));
			PathRefreshedWhenFollow = MoveToTargetOnClimbNavigation(SearchStartLoc, SearchEndLoc); // Try activate movement on PathPoints

		}

	}

}


bool UNavClimbingComponentCore::GetPlayerIsClimbing_Implementation(int PlayerIndex)
{
	if (!GetWorld()) { return false; }
	ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), PlayerIndex);
	if (PlayerChar)
	{
		if (PlayerChar->GetMesh()->GetAnimInstance())
		{
			float CurveValue = PlayerChar->GetMesh()->GetAnimInstance()->GetCurveValue(TEXT("BasePose_CMC_Climbing"));
			if (CurveValue > 0.5)
			{
				return true;
			}
		}
	}
	return false;
}


bool UNavClimbingComponentCore::JumpToLedgeClimb(FVector NavPointLocation, FVector NavPointDirection, AClimbNavigationStorageActor* NavActor, bool bStopMontages, FVector WarpOffset)
{
	const float CapsuleForwardOffset = -30.0;
	const FVector CapsuleUpOffset = FVector(0, 0, -50.0);

	if (IsClimbing) { return false; }
	if (IsValid(NavActor) == false) { return false; }
	if (GetTimerIsActivated(JumpToFirstLedgeHandle) == true) { return false; }

	//Step 01
	FTransform NextLedgeTransform = FTransform
	(
		KML::MakeRotFromX(NavPointDirection),
		NavPointLocation + (NavPointDirection * CapsuleForwardOffset) + CapsuleUpOffset,
		FVector(1, 1, 1)
	);

	NextLedgeParams.ActorTransform = NextLedgeTransform;
	NextLedgeParams.LedgeCenter = FTransform(KML::MakeRotFromX(NavPointDirection), NavPointLocation);
	NextLedgeParams.LedgeLeft = FTransform(KML::MakeRotFromX(NavPointDirection), NavPointLocation);
	NextLedgeParams.LedgeRight = FTransform(KML::MakeRotFromX(NavPointDirection), NavPointLocation);
	NextLedgeParams.LedgeID = -1;
	NextLedgeParams.PointID = -1;
	NextLedgeParams.PointName = TEXT("ClimbPoint");

	CurrentLedgeParams = NextLedgeParams;

	//Step 02
	NavClimbActionState = ENavClimbActionState::None;
	IsInterpolatingToStart = true;
	CurrentLedgeIndex = 0;
	SetTargetActionState();
	CurrentNavStorageActor = NavActor;

	//Step 03
	Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Flying);
	LogInfo(TEXT("JumpToLedgeClimb() Character Movement Component - Movement Mode is configured as MOVE_FLYING"));

	//Required LOCK Rotation Interface
	//Required Motion Warping WARP Transform update
	UMotionWarpingComponent* MWC = Cast<UMotionWarpingComponent>(Character->GetComponentByClass(UMotionWarpingComponent::StaticClass()));
	if (MWC)
	{
		FVector FixedWarpOffset = NavPointDirection * WarpOffset.X;
		FixedWarpOffset = FixedWarpOffset + FVector(0, 0, WarpOffset.Z);

		MWC->AddOrUpdateWarpTargetFromTransform(TEXT("FrontLedge"), 
				FTransform(NextLedgeParams.ActorTransform.Rotator(), 
				NextLedgeParams.ActorTransform.GetLocation() + FixedWarpOffset,
				FVector(1, 1, 1)
			));
		LogInfo(TEXT("JumpToLedgeClimb() Motion Warping component is finded in character. Creating warp target with name 'FrontLedge'"));
	}


	//Step 04 Montage Activation
	if (Character->GetMesh()->GetAnimInstance())
	{
		float InPlayRate = 1.0; float InStartTime = 0.0;
		UAnimMontage* TargetMontage = GetStartingClimbMontage(InPlayRate, InStartTime);

		if (!TargetMontage)
		{
			LogInfo(TEXT("JumpToLedgeClimb() Montage Asset is empty. Make sure you currectly overrided GetStartingClimbMontage() function"));
		}

		float AnimDuration = Character->GetMesh()->GetAnimInstance()->Montage_Play(TargetMontage, InPlayRate, EMontagePlayReturnType::Duration, InStartTime, bStopMontages);

		if (AnimDuration <= 0.0) 
		{ 
			LogInfo(TEXT("JumpToLedgeClimb() Tried to play animation montage, but final AnimDuration is not greated that 0.0. Function abort"));
			return false; 
		}

		LogInfo(TEXT("JumpToLedgeClimb() Animation Montage is started"));

		TimerDuration = AnimDuration;

		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer
			(
				JumpToFirstLedgeHandle,
				this,
				&UNavClimbingComponentCore::JumpToFirstLedgeEvent,
				AnimDuration,
				false
			);
			LogInfo(TEXT("JumpToLedgeClimb() timer is initialized"));
		}
		return true;

	}

	return false;
}


UAnimMontage* UNavClimbingComponentCore::GetStartingClimbMontage_Implementation(float& PlayRate, float& StartTime)
{
	//Required override in blueprint
	PlayRate = 1.0;
	StartTime = 0.0;
	return nullptr;
}


void UNavClimbingComponentCore::UpdatePrepareToClimbLedge_Implementation()
{
	if (GetTimerIsActivated(JumpToFirstLedgeHandle) == true && GetTimerElapsedTime(JumpToFirstLedgeHandle) > 0.01)
	{
		// Required LOCK Rotation Update Interface Call (Override in blueprint)

		float MappedTimeElapsed = KML::MapRangeClamped(GetTimerElapsedTime(JumpToFirstLedgeHandle), 0.0, TimerDuration, 0.0, 1.0);

		if (MappedTimeElapsed > 0.2) 
		{
			PrepareAnimInstParams(true); 
		}

		if (MappedTimeElapsed > 0.75) { IsClimbing = true; }

		if (MappedTimeElapsed > 0.85) 
		{ 
			LogInfo(TEXT("UpdatePrepareToClimbLedge(): Configuted movement mode as MOVE_Custom"));
			Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Custom);
			FollowPlayerWhenClimb = true; 
		}

		if (MappedTimeElapsed > 0.6)
		{
			float AlphaMap = KML::MapRangeClamped(MappedTimeElapsed, 0.6, 0.95, 0.0, 1.0);

			FTransform NewTransform = KML::TEase(Character->GetActorTransform(), ScaleForwardCapOffset(CurrentLedgeParams.ActorTransform), AlphaMap, EEasingFunc::ExpoIn, 2.0);

			Character->SetActorLocationAndRotation(NewTransform.GetLocation(), NewTransform.GetRotation(), false);
		}

	}
}


// 𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿 Release Ledge System 𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿𖠿 
bool UNavClimbingComponentCore::CheckCanReleaseClimb(
	float& Duration, 
	bool bRunWhenCheckValid, 
	FVector LedgeOrigin, 
	FVector CheckDirection, 
	FVector ActorLocation, 
	bool PreferJumpDown, 
	int MaxSearchInterations, 
	float TraceForwardOffset, 
	FVector2D TraceOffsetUp, 
	FVector2D TraceOffsetDown, 
	ECollisionChannel TraceChannel)
{

	if (StartedReleaseLedge || !IsClimbing) { return false; }

	if (!PreferJumpDown)
	{
		const int FixedMaxIter = FMath::Clamp<int>(MaxSearchInterations, 0, 10);
		for (int i = 0; i < FixedMaxIter; i++)
		{
			FVector TraceStart = LedgeOrigin + (CheckDirection * ((float(i) * 10) + TraceForwardOffset));
			FVector TraceEnd = TraceStart + FVector(0, 0, TraceOffsetDown.X);
			TraceStart = TraceStart + FVector(0, 0, TraceOffsetUp.X);

			TArray<AActor*> ActorsToIgnore; ActorsToIgnore.Add(Character);

			FHitResult LineTraceResult;

			EDrawDebugTrace::Type DebugType = EDrawDebugTrace::None;
			if (CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 1) { DebugType = EDrawDebugTrace::ForOneFrame; }
			else if(CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 2) { DebugType = EDrawDebugTrace::ForDuration; }

			bool FirstTraceValid = KSL::LineTraceSingle
			(
				Character, 
				TraceStart, 
				TraceEnd, 
				UEngineTypes::ConvertToTraceType(TraceChannel), 
				false, 
				ActorsToIgnore, 
				DebugType, 
				LineTraceResult, 
				true, 
				FColor::Blue, 
				FColor::Yellow, 
				2.0
			);

			if (FirstTraceValid && !LineTraceResult.bStartPenetrating)
			{
				TraceStart = LineTraceResult.ImpactPoint + FVector(0,0, Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + 2.0);
				TraceEnd = LineTraceResult.ImpactPoint + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight_WithoutHemisphere() * 2.0);

				FHitResult SphereTraceResult;

				bool SecondTraceValid = KSL::SphereTraceSingle
				(
					Character,
					TraceStart,
					TraceEnd,
					Character->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.95,
					UEngineTypes::ConvertToTraceType(TraceChannel),
					false,
					ActorsToIgnore,
					DebugType,
					SphereTraceResult,
					true,
					FColor::Black,
					FColor::Red,
					2.0
				);

				if (!SecondTraceValid)
				{
					FTransform TargetCapTransform = FTransform
					(
						KML::MakeRotFromX(CheckDirection), 
						LineTraceResult.ImpactPoint + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 
						FVector(1,1,1)
					);

					if (bRunWhenCheckValid)
					{
						float AnimDuration = 0.0;
						bool CanStart = StartReleaseLedgeClimb(AnimDuration, TargetCapTransform, LineTraceResult.GetComponent(), LedgeOrigin, false);
						Duration = AnimDuration;
						return CanStart;
					}
					else
					{
						Duration = 0.0;
						return true;
					}

				}

			}

		}
		Duration = 0.0; return false;
	}
	else
	{
		const int FixedMaxIter = FMath::Clamp<int>(MaxSearchInterations, 0, 10);
		for (int i = 0; i < FixedMaxIter; i++)
		{
			FVector TraceStart = LedgeOrigin + (CheckDirection * -1.0 * ((float(i) * 10) + TraceForwardOffset));
			FVector TraceEnd = TraceStart + FVector(0, 0, TraceOffsetDown.Y);
			TraceStart = TraceStart + FVector(0, 0, TraceOffsetUp.Y);

			TArray<AActor*> ActorsToIgnore; ActorsToIgnore.Add(Character);

			FHitResult SphereTraceResult;

			EDrawDebugTrace::Type DebugType = EDrawDebugTrace::None;
			if (CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 1) { DebugType = EDrawDebugTrace::ForOneFrame; }
			else if (CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 2) { DebugType = EDrawDebugTrace::ForDuration; }

			bool FirstTraceValid = KSL::SphereTraceSingle
			(
				Character,
				TraceStart,
				TraceEnd,
				Character->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.8,
				UEngineTypes::ConvertToTraceType(TraceChannel),
				false,
				ActorsToIgnore,
				DebugType,
				SphereTraceResult,
				true,
				FColor::Blue,
				FColor::Yellow,
				2.0
			);

			if (FirstTraceValid && Character->GetCharacterMovement()->IsWalkable(SphereTraceResult))
			{
				FTransform TargetCapTransform = FTransform
				(
					KML::MakeRotFromX(CheckDirection),
					SphereTraceResult.ImpactPoint + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
					FVector(1, 1, 1)
				);

				if (bRunWhenCheckValid)
				{
					float AnimDuration = 0.0;
					bool CanStart = StartReleaseLedgeClimb(AnimDuration, TargetCapTransform, SphereTraceResult.GetComponent(), LedgeOrigin, true);
					Duration = AnimDuration;
					return CanStart;
				}
				else
				{
					Duration = 0.0;
					return true;
				}
			}
		}
		Duration = 0.0;
		return false;
	}
}


bool UNavClimbingComponentCore::StartReleaseLedgeClimb_Implementation(float& ReturnAnimDuration, FTransform CapTransformWS, UPrimitiveComponent* LedgeComponent, FVector LedgeOrigin, bool bForJumpDown)
{
	if (!IsClimbing) { ReturnAnimDuration = 0.0;  return false; }
	if (StartedReleaseLedge) { ReturnAnimDuration = 0.0; return false; }

	if (!Character || !LedgeComponent) 
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: function StartReleaseLedgeClimb() its called but object Character OR  LedgeComponent is null. Function abort"));
		return false; 
	}

	//Sequence then 01
	FNavClimb_CompAndTransform TransformStructWS;

	TransformStructWS.Transform = CapTransformWS;
	TransformStructWS.Component = LedgeComponent;
	TargetTransformLS = SimpleConvertSpace(TransformStructWS, true);

	TransformStructWS.Transform = Character->GetActorTransform();
	CurrentTransformLS = SimpleConvertSpace(TransformStructWS, true);

	//Sequence then 02
	Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Custom);
	NavClimbActionState = ENavClimbActionState::None;

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MainTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(JumpToFirstLedgeHandle);
		GetWorld()->GetTimerManager().ClearTimer(WaitTimerHandle);
	}

	//Sequence then 03 (if Drom from ledge climb)
	if (bForJumpDown)
	{
		DropFromLedgeHold();
		ReturnAnimDuration = -1.0;
		return true;
	}

	//Sequence then 04
	if (!MantleReleaseLedgeMontage) 
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: StartReleaseLedgeClimb() anim montage for mantle action Is NULL"));
		return false;
	}

	float MontageDuration = Character->GetMesh()->GetAnimInstance()->Montage_Play(MantleReleaseLedgeMontage, 1.0, EMontagePlayReturnType::Duration, 0.0, true);
	if (MontageDuration > 0.0)
	{
		TimerDuration = MontageDuration;

		GetWorld()->GetTimerManager().SetTimer
		(
			ReleaseLedgeTimerHandle,
			this,
			&UNavClimbingComponentCore::ReleaseLedgeTimer,
			TimerDuration,
			false
		);

		StartedReleaseLedge = true;
		ReturnAnimDuration = TimerDuration;
		return true;
	}
	return false;
}


void UNavClimbingComponentCore::DropFromLedgeHold_Implementation()
{
	if (IsClimbing == true)
	{
		StartedReleaseLedge = false;
		IsClimbing = false;
		Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Falling);

		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(ReleaseLedgeTimerHandle);
		}

		PrepareAnimInstParams(false);
	}
}


float UNavClimbingComponentCore::UpdateWhenReleaseLedge_Implementation()
{
	if (!MantleReleaseTrajectoryCurve)
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: UpdateWhenReleaseLedge(): Trajectory Curve asset for interpolation Actor transform is NULL. Function abort"));
		return -1.0;
	}


	if (StartedReleaseLedge && GetTimerElapsedTime(ReleaseLedgeTimerHandle) > 0.0)
	{
		//Sequence then 01 (Normalize timer elapsed time)
		float NormalizedTime = KML::MapRangeClamped(GetTimerElapsedTime(ReleaseLedgeTimerHandle), 0.0, TimerDuration, 0.0, 1.0);

		//Sequene then 02

		float MinTime; float MaxTime;
		MantleReleaseTrajectoryCurve->GetTimeRange(MinTime, MaxTime);
		float TrajectoryTime = KML::MapRangeClamped(NormalizedTime, 0.0, 1.0, MinTime, MaxTime);

		FTransform InterpedTransform = ExtractedInterpolation
		(
			SimpleConvertSpace(CurrentTransformLS, false).Transform,
			SimpleConvertSpace(TargetTransformLS, false).Transform,
			MantleReleaseTrajectoryCurve->GetVectorValue(TrajectoryTime).X,
			MantleReleaseTrajectoryCurve->GetVectorValue(TrajectoryTime).Y,
			MantleReleaseTrajectoryCurve->GetVectorValue(TrajectoryTime).Z,
			MantleReleaseTrajectoryCurve->GetVectorValue(TrajectoryTime).Z,
			0.0
		);
		
		//Make sure the distance between current and next transform is in accetable range
		const float MainDistance = KML::Vector_Distance(Character->GetActorLocation(), InterpedTransform.GetLocation());
		if (MainDistance > 100.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: UpdateWhenReleaseLedge(): Accetable distance condtion between Current location and next is not met. Location Update abort"));
		}
		else
		{
			Character->SetActorLocationAndRotation(InterpedTransform.GetLocation(), InterpedTransform.GetRotation(), false);
		}

		//Sequence then 03
		if (NormalizedTime > 0.6)
		{
			IsClimbing = false;
			NavClimbActionState = ENavClimbActionState::None;
			CurrentLedgeIndex = 0;
			PrepareAnimInstParams(false);
		}

		if (NormalizedTime > 0.8 && NormalizedTime < 1.0)
		{
			Character->GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);
			//In this state we need to update interface call
		}

		if (NormalizedTime > 0.89)
		{
			StartedReleaseLedge = false;
		}

		return NormalizedTime;

	}
	return -1.0;
}


void UNavClimbingComponentCore::ReleaseLedgeFinished_Implementation()
{
	//This function is optional FOR BLUEPRINT
}
//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------- END




//▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜▛▜
void UNavClimbingComponentCore::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateMovementOnPath();
	UpdatePrepareToClimbLedge();
	UpdateWhenReleaseLedge();

	DrawDebugCurrentPath(); //Debug function (only editor)

#if WITH_EDITOR

	if (bPrintAditionalInfoOnScreen)
	{
		if (GetTimerIsActivated(MainTimerHandle))
		{
			FString t = TEXT("Main Timer Elapsed Time: ");
			t += FString::SanitizeFloat(GetTimerElapsedTime(MainTimerHandle));
			GEngine->AddOnScreenDebugMessage(2, 0.5, FColor::Green, t, false);
		}

		if (NormalizedMainTimer() > 0.0)
		{
			FString t = TEXT("Main Timer Elapsed Time (NORMALIZED): ");
			t += FString::SanitizeFloat(NormalizedMainTimer());
			GEngine->AddOnScreenDebugMessage(1, 0.5, FColor::Black, t, false);
		}

		if (GetTimerIsActivated(WaitTimerHandle))
		{
			FString t = TEXT("Wait Timer Elapsed Time: ");
			t += FString::SanitizeFloat(GetTimerElapsedTime(WaitTimerHandle));
			GEngine->AddOnScreenDebugMessage(3, 0.1, FColor::Emerald, t, false);
		}

		if (GetTimerIsActivated(JumpToFirstLedgeHandle))
		{
			FString t = TEXT("Jump To First Ledge Timer Elapsed Time: ");
			t += FString::SanitizeFloat(GetTimerElapsedTime(JumpToFirstLedgeHandle));
			GEngine->AddOnScreenDebugMessage(4, 0.5, FColor::Emerald, t, false);
		}

		if (GetTimerIsActivated(FollowPlayerTimerHandle))
		{
			FString t = TEXT("Follow PLAYER Timer Elapsed Time: ");
			t += FString::SanitizeFloat(GetTimerElapsedTime(FollowPlayerTimerHandle));
			GEngine->AddOnScreenDebugMessage(5, 0.0, FColor::Cyan, t, false);
		}

		FString S1 = TEXT("01) Bool: IsInterpolatingToStart: ");
		S1 += FString::FromInt(IsInterpolatingToStart);
		GEngine->AddOnScreenDebugMessage(10, 0.0, KML::SelectColor(FLinearColor::Green, FLinearColor::Red, IsInterpolatingToStart).ToFColor(true), S1, false);

		S1 = TEXT("02) Bool: IsInterpolatingToNext: ");
		S1 += FString::FromInt(IsInterpolatingToNext);
		GEngine->AddOnScreenDebugMessage(11, 0.0, KML::SelectColor(FLinearColor::Green, FLinearColor::Red, IsInterpolatingToNext).ToFColor(true), S1, false);

		S1 = TEXT("03) Int: CurrentLedgeIndex: ");
		S1 += FString::FromInt(CurrentLedgeIndex);
		GEngine->AddOnScreenDebugMessage(12, 0.0, FColor::Emerald, S1, false);

		S1 = TEXT("04) Enum: NavClimbActionState: ");
		const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("ENavClimbActionState"), true);
		if (EnumPtr)
		{
			FString EnumValue = EnumPtr->GetNameByValue((int32)NavClimbActionState).ToString(); // Nazwa stanu
			FString DisplayName = EnumPtr->GetDisplayNameTextByValue((int32)NavClimbActionState).ToString(); // DisplayName
			S1 += DisplayName;
			GEngine->AddOnScreenDebugMessage(13, 0.0, FColor::Cyan, S1, false);
		}

		S1 = TEXT("05) Bool: PathRefreshedWhenFollow: ");
		S1 += FString::FromInt(PathRefreshedWhenFollow);
		GEngine->AddOnScreenDebugMessage(14, 0.0, KML::SelectColor(FLinearColor::Green, FLinearColor::Red, PathRefreshedWhenFollow).ToFColor(true), S1, false);

		S1 = TEXT("06) Bool: Distance Tollerance Condition: ");
		if (KML::Vector_Distance(NextCapLocation, ActorLoc) <= CorrectDistanceTollerance) { S1 += TEXT("TRUE"); }
		else { S1 += TEXT("false"); }
		GEngine->AddOnScreenDebugMessage(15, 0.0, FColor::Magenta, S1, false);
		
	}

	if (CVarAglsNavClimbShowOtherShapes.GetValueOnAnyThread() == true)
	{
		if (GetWorld())
		{
			DrawDebugSphere(GetWorld(), CurrentCapLocation, 12.0, 8, FColor::Yellow, false, -1.0, 0, 2.0);
			DrawDebugSphere(GetWorld(), NextCapLocation, 10.0, 8, FColor::Orange, false, -1.0, 0, 2.0);
			DrawDebugLine(GetWorld(), CurrentCapLocation, NextCapLocation, FColor::Yellow, false, -1.0, 0, 1.0);

			// Aktualizacja danych
			Debugger.UpdateGraphData(FName("FollowingTimer"), GetTimerIsActivated(FollowPlayerTimerHandle));
			Debugger.UpdateGraphData(FName("MainActionTimer"), NormalizedMainTimer());
			Debugger.Draw();
		}
	}

#endif

}
//▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟▙▟




bool UNavClimbingComponentCore::MoveToTargetOnClimbNavigation_Implementation(FVector StartLocation, FVector EndLocation, float MaxPathLenght, float MaxMovingTime)
{
	if (!CurrentNavStorageActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: function MoveToTargetOnClimbNavigation() its called but object CurrentNavStorageActor is null. Function abort"));
		return false;
	}

	bool Succesful = false;
	float TotalLength = -1.0;
	TArray<FClimbNav_SingleClimbPoint> NewPathPoints;
	// MAIN PATH FINDING ALGORITHM
	CurrentNavStorageActor->FindPathBetweenClimbPoints(Succesful, TotalLength, NewPathPoints, StartLocation, EndLocation);

	if (!Succesful)
	{
		UE_LOG(LogTemp, Warning, TEXT("Nav Climbing Component: Path finding function is executed but path is not succesful created"));
		return false;
	}

	PathPointsData = NewPathPoints; // SET Path points as global variable

	//Remove Last Index when detected collision
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(Character);
	FHitResult HitResult;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectsType;
	ObjectsType.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));

	EDrawDebugTrace::Type DebugType = EDrawDebugTrace::None;
	if (CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 1) { DebugType = EDrawDebugTrace::ForOneFrame; }
	else if (CVarAglsNavClimbShowTraces.GetValueOnAnyThread() == 2) { DebugType = EDrawDebugTrace::ForDuration; }

	bool TraceHit = KSL::CapsuleTraceSingleForObjects
	(
		Character,
		GetPathPointsStruct()[GetPathPointsStruct().Num() - 1].ActorTransform.GetLocation(),
		GetPathPointsStruct()[GetPathPointsStruct().Num() - 1].ActorTransform.GetLocation() + FVector(0, 0, 2),
		28.0,
		85.0,
		ObjectsType,
		false,
		ActorsToIgnore,
		DebugType,
		HitResult,
		true,
		FColor::Black,
		FColor::Red,
		1.5
	);
	if (TraceHit)
	{
		PathPointsData.RemoveAt(PathPointsData.Num() - 1);
	}

	// Try Movement
	const bool Continue = TryPerformMovingOnClimbPath();
	if (Continue)
	{
		LogInfo(TEXT("MoveToTargetOnClimbNavigation() function TryPerformMovingOnClimbPath() finished with successful. Then call PrepareAnimInstParams(true) "));
		PrepareAnimInstParams(true);
		return true;
	}
	return false;
}


void UNavClimbingComponentCore::TryPauseMovement(bool bPause)
{
	PauseCurrentMove = bPause;
}


float UNavClimbingComponentCore::SelectFloatOnActionState(float None, float WalkCycle, float JumpShort, float JumpLong)
{
	if (NavClimbActionState == ENavClimbActionState::None)
	{
		return None;
	}
	else if (NavClimbActionState == ENavClimbActionState::WalkCycle)
	{
		return WalkCycle;
	}
	else if (NavClimbActionState == ENavClimbActionState::ShortJump)
	{
		return JumpShort;
	}
	else
	{
		return JumpLong;
	}
}


FTransform UNavClimbingComponentCore::ExtractedInterpolation(FTransform A, FTransform B, float VX, float VY, float VZ, float RYaw, float Alpha)
{
	const FVector UnRotA = KML::Quat_UnrotateVector(A.GetRotation(), A.GetLocation());
	const FVector UnRotB = KML::Quat_UnrotateVector(A.GetRotation(), B.GetLocation());

	FVector InterpolatedLoc =
		FVector
		(
			FMath::Lerp<float>(UnRotA.X, UnRotB.X, VY),
			FMath::Lerp<float>(UnRotA.Y, UnRotB.Y, VX),
			FMath::Lerp<float>(UnRotA.Z, UnRotB.Z, VZ)
		);

	FVector RotatedInterp = KML::Quat_RotateVector(A.GetRotation(), InterpolatedLoc);
	RotatedInterp = KML::VLerp(RotatedInterp, B.GetLocation(), Alpha);

	FRotator InterpRot = KML::RLerp(A.Rotator(), B.Rotator(), RYaw, true);

	return FTransform(InterpRot, RotatedInterp, FVector(1, 1, 1));
}


float UNavClimbingComponentCore::TryGetAnimCurveValue(FName CurveName)
{
	if (Character->GetMesh()->GetAnimInstance())
	{
		return Character->GetMesh()->GetAnimInstance()->GetCurveValue(CurveName);
	}
	return 0.0;
}

void UNavClimbingComponentCore::SetNewActionState(int NewIndex)
{
	ClimbingActionStateIndex = NewIndex;
	NavClimbActionState = static_cast<ENavClimbActionState>(NewIndex);
}

float UNavClimbingComponentCore::GetTimerElapsedTime(FTimerHandle Handle)
{
	if (!GetWorld()) { return -1.0; }

	if (GetWorld()->GetTimerManager().IsTimerActive(Handle))
	{
		float ElapsedTime = GetWorld()->GetTimerManager().GetTimerElapsed(Handle);
		return ElapsedTime;
		//UE_LOG(LogTemp, Warning, TEXT("Elapsed Time: %f seconds"), ElapsedTime);
	}
	else
	{
		return -1.0;
		//UE_LOG(LogTemp, Warning, TEXT("Timer is not active."));
	}
}

bool UNavClimbingComponentCore::GetTimerIsActivated(FTimerHandle Handle)
{
	if (!GetWorld()) { return false; }
	return GetWorld()->GetTimerManager().IsTimerActive(Handle);
}

float UNavClimbingComponentCore::NormalizedMainTimer()
{
	return KML::MapRangeClamped(GetTimerElapsedTime(MainTimerHandle), 0.0, TimerDuration, 0.0, 1.0);
}


// Timers

//Looped Timer
void UNavClimbingComponentCore::FollowPlayerTimer() 
{
	PerformFollowing();
}

void UNavClimbingComponentCore::MainTimer() { bMainTimerFinished = true; }
void UNavClimbingComponentCore::ReleaseLedgeTimer() { ReleaseLedgeFinished(); }
void UNavClimbingComponentCore::JumpToFirstLedgeEvent() {}

void UNavClimbingComponentCore::WaitTimerEvent()
{
	GetWorld()->GetTimerManager().UnPauseTimer(MainTimerHandle);
	SetTargetActionState();
	UpdateAnimValuesWhenClimb();

	LogInfo(TEXT("WaitTimerEvent(): UnPausedTimer - 'MainTimerHandle', Update State value, updated anim values"));

}


void UNavClimbingComponentCore::LogInfo(FString Message)
{
	if (bPrintAditionalInfoInLog)
	{
		float Seconds = UGameplayStatics::GetTimeSeconds(GetWorld());
		FString t1 = FString::SanitizeFloat(Seconds);
		t1 += TEXT(" : ");
		t1 += Message;

		UE_LOG(LogTemp, Log, TEXT("Nav Climbing Component: %s"), *t1);
	}

	if (bPrintAditionalInfoOnScreen)
	{
		float Seconds = UGameplayStatics::GetTimeSeconds(GetWorld());

		FString t1 = TEXT("Nav Climbing Component: ");
		t1 += FString::SanitizeFloat(Seconds);
		t1 += TEXT(" - ");
		t1 += Message;
		GEngine->AddOnScreenDebugMessage(-1, 1.0, FColor::White, Message, false);
	}

}


void UNavClimbingComponentCore::DrawDebugCurrentPath()
{

#if WITH_EDITOR
	if (CVarAglsNavClimbShowPath.GetValueOnAnyThread() == true)
	{
		if (GetPathPointsStruct().Num() >= 2 && GetWorld())
		{
			for (int i = 0; i <= GetPathPointsStruct().Num() - 2; i++)
			{
				if (GetPathPointsStruct().IsValidIndex(i + 1))
				{
					DrawDebugLine(GetWorld(), GetPathPointsStruct()[i].LedgeCenter.GetLocation(), GetPathPointsStruct()[i + 1].LedgeCenter.GetLocation(), FColor::Blue, false, -1.0, 0, 1.6);
					DrawDebugPoint(GetWorld(), GetPathPointsStruct()[i].LedgeCenter.GetLocation(), 10.0, FColor::Cyan, false, -1.0);
				}
			}
		}
	}
#endif
}


FNavClimb_CompAndTransform UNavClimbingComponentCore::SimpleConvertSpace(FNavClimb_CompAndTransform InStruct, bool bToLocalSpace)
{
	if (IsValid(InStruct.Component) == false) { return InStruct; }

	if (bToLocalSpace)
	{
		FNavClimb_CompAndTransform Local = {};
		Local.Transform = UKismetMathLibrary::Conv_MatrixToTransform(UKismetMathLibrary::Conv_TransformToMatrix(InStruct.Transform) *
			UKismetMathLibrary::Matrix_GetInverse(UKismetMathLibrary::Matrix_GetMatrixWithoutScale(UKismetMathLibrary::Conv_TransformToMatrix(InStruct.Component->GetComponentTransform()))));
		Local.Component = InStruct.Component;
		return Local;
	}
	else
	{
		FNavClimb_CompAndTransform ReturnWorld = {};
		ReturnWorld.Transform = UKismetMathLibrary::Conv_MatrixToTransform(UKismetMathLibrary::Conv_TransformToMatrix(InStruct.Transform) *
			UKismetMathLibrary::Matrix_GetMatrixWithoutScale(UKismetMathLibrary::Conv_TransformToMatrix(InStruct.Component->GetComponentTransform())));
		ReturnWorld.Component = InStruct.Component;
		return ReturnWorld;
	}
}


