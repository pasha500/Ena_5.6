

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "Animation/AnimInstance.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "AGLS_AI_AnimInstanceBase.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API UAGLS_AI_AnimInstanceBase : public UAnimInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	bool OffsetRootBoneEnabledC = true;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	FPoseSearchTrajectoryData TrajectoryConfigIdle;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	FPoseSearchTrajectoryData TrajectoryConfigMoving;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	float ReadyStateDuration = 3.0;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	float IsPivotingDeltaTrigger = 40.0;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	float IsStartingSpeedOffset = 100.0;

	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
	float IsSpinningDeltaTrigger = 105.0;

	UPROPERTY(BlueprintReadWrite, Category = "Config|IK Settings", meta = (AllowPrivateAccess = "True"))
	FName FootIK_L_CurveName = TEXT("Enable_FootIK_L");

	UPROPERTY(BlueprintReadWrite, Category = "Config|IK Settings", meta = (AllowPrivateAccess = "True"))
	FName FootIK_R_CurveName = TEXT("Enable_FootIK_R");

	UPROPERTY(BlueprintReadWrite, Category = "Config|IK Settings", meta = (AllowPrivateAccess = "True"))
	float FootTraceRadius = 0.1;

	UPROPERTY(BlueprintReadWrite, Category = "References", meta = (AllowPrivateAccess = "True"))
	ACharacter* CharacterC = nullptr;

	UCharacterMovementComponent* MovementComp = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "References", meta = (AllowPrivateAccess = "True"))
	float dt = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "References", meta = (AllowPrivateAccess = "True"))
	float TimeDilatationC = 1.0;

	float timer1 = 0.0;
	float timer2 = 0.0;

	// =============================================================================================================================   STATES VALUES AS INDEX

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_MovementState MovementStateC = CALS_MovementState::Grounded;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_MovementState PrevMovementStateC = CALS_MovementState::Grounded;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_Gait GaitC = CALS_Gait::Running;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_OverlayState OverlayStateC = CALS_OverlayState::Default;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_RotationMode RotationModeC = CALS_RotationMode::LookingDirection;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_Stance StanceC = CALS_Stance::Standing;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_MovementAction MovementActionC = CALS_MovementAction::None;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CALS_GroundedMoveMode GroundedMoveModeC = CALS_GroundedMoveMode::Normal;

	UPROPERTY(BlueprintReadWrite, Category = "States Values", meta = (AllowPrivateAccess = "True"))
	CMC_ActionTypeC ClimbingActionStateC = CMC_ActionTypeC::None;

	//------------------------------------------------------------------------------------------------------------------------------   ---> Essensial VALUES <---

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool ShouldMoveC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool PickUpLootItemC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool IsTurnInPlaceAimingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool IsCoveringC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool LeftSideCoverC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool CoverDirectionChangedC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	bool IsDeadC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	float SpeedC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector VelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector FutureVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector VelocityLastFrameC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector LastNonZeroVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector AccelerationC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FVector LandVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FRotator AimingRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FRotator PrevAimingRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	FRotator RotationVelocityC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Values", meta = (AllowPrivateAccess = "True"))
	float HitReactionStrenghtC = 0.0;

	//------------------------------------------------------------------------------------------------------------------------------    ---> Movement Analize <---

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool IsMovingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool PrevIsMovingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool HasMovementInputC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool CapsuleCollidingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool JustLandedC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool OnStairsC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Movement Analize", meta = (AllowPrivateAccess = "True"))
	bool StanceTransitionC = false;

	//------------------------------------------------------------------------------------------------------------------------------    ---> M O T I O N   M A T C H I N G <---

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FTransformTrajectory Trajectory;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	UPoseSearchDatabase* CurrentSelectedDatabase;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	float PreviousDesiredControllerYawC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	bool InterruptOnDatabaseC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FTransform CharacterTransformC = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FTransform RootTransformC = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FTransform InteractionTransformC = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FRotator FutureMovementAngleC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	TArray<FName> CurrentDatabaseTags;

	UPROPERTY(BlueprintReadWrite, Category = "Motion Matching", meta = (AllowPrivateAccess = "True"))
	FTransform PrevRootTransform;

	//------------------------------------------------------------------------------------------------------------------------------    --->  (A) (I) (M) (I) (N) (G)  <---

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	FRotator SpineRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	FRotator SmoothedAimingRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	FVector2D AimingAngleC = FVector2D(0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	FVector2D SmoothedAimingAngleC = FVector2D(0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	float AimSweepTimeC = 0.5;

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	float InputYawOffsetTimeC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	float ForwardYawTimeC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	float AimYawRateC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Aiming Values", meta = (AllowPrivateAccess = "True"))
	float RootYawChangeSpeedC = 0.0;

	//------------------------------------------------------------------------------------------------------------------------------    --->  |L|A|Y|E|R|  |B|L|E|N|D|I|N|G|  <---

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float StrideBlendC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float BasePoseN = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float BasePoseCLF = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float ArmL_LS = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float ArmL_MS = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float ArmR_LS = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float ArmR_MS = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float Hand_L = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float Hand_R = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float SecondaryMotionMaskC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	float BendDownAlphaC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	int OverlayOverrideStateC = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Layer Blending", meta = (AllowPrivateAccess = "True"))
	CALS_OverlayPosesType OverlayPosesType = CALS_OverlayPosesType::Relaxed;

	//------------------------------------------------------------------------------------------------------------------------------    --->  [F] [O] [O] [T] [S]   [I]. [K]. <---

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	float FootLockL_AlphaC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	float FootLockR_AlphaC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	FVector FootOffset_L_LocC = FVector(0,0,0);

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	FRotator FootOffset_L_RotC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	FVector FootOffset_R_LocC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	FRotator FootOffset_R_RotC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	FVector PelvisOffsetC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Foot IK", meta = (AllowPrivateAccess = "True"))
	float PelvisOffsetAlphaC = 0.0;

	FVector FootLock_L_Location = FVector(0, 0, 0);
	FVector FootLock_R_Location = FVector(0, 0, 0);

	// /\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\ Optimization

	UPROPERTY(BlueprintReadWrite, Category = "Optimization", meta = (AllowPrivateAccess = "True"))
	AGLS_LOD_State LOD_State = AGLS_LOD_State::LOD0;

	UPROPERTY(BlueprintReadWrite, Category = "Optimization", meta = (AllowPrivateAccess = "True"))
	bool LOD_ToUp = false;

public:

	virtual void NativeInitializeAnimation() override;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:

	void CreateOverlayPosesModeState();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Generate Trajectory", Keywords = "Motion Matching", BlueprintThreadSafe, AdvancedDisplay = "HistoryCount,InPredictionInterval,PreditionCount"))
	FTransformTrajectory GenerateTrajectory(int HistoryCount = 30, float InPredictionInterval = 0.1, int PreditionCount = 15);

	virtual FTransformTrajectory GenerateTrajectory_Implementation(int HistoryCount = 30, float InPredictionInterval = 0.1, int PreditionCount = 15);

	UFUNCTION(BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Make Is Collide Value", Keywords = "Motion Matching", AdvancedDisplay = "DebugIndex,DebugTime,IgnoreCharacters"))
	void MakeIsCollideValue(TArray<TEnumAsByte<EObjectTypeQuery>> TraceObjects, int DebugIndex = 0, float DebugTime = 0.1, bool IgnoreCharacters = false);

	UFUNCTION(BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Update Essential Values", Keywords = "Motion Matching", BlueprintThreadSafe))
	void UpdateEssentialValues();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Set Root Transform", Keywords = "Motion Matching", BlueprintThreadSafe))
	void SetRootTransform();

	virtual void SetRootTransform_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Update Rotation Values", Keywords = "Motion Matching", BlueprintThreadSafe))
	void UpdateRotationValues();

	virtual void UpdateRotationValues_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Update Cover Values", Keywords = "Motion Matching", BlueprintThreadSafe))
	void UpdateCoverValues();

	virtual void UpdateCoverValues_Implementation();



	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Foot IK Core", meta = (DisplayName = "Update Foots IK", Keywords = "Foots IK", AdvancedDisplay = "TraceAboveFoot,TraceBelowFoot,FootHeight,ActiveOnLOD,TraceDebugIndex,DebugTime"))
	void UpdateFootsIK(
		ECollisionChannel TraceChannel = ECollisionChannel::ECC_Visibility, 
		bool UseFootsLock = false, 
		float TraceAboveFoot = 50.0, 
		float TraceBelowFoot = 45.0, 
		float FootHeight = 13.5, 
		int ActiveOnLOD = 1, 
		int TraceDebugIndex = 0, 
		float DebugTime = 0.2
	);

	virtual void UpdateFootsIK_Implementation(
		ECollisionChannel TraceChannel = ECollisionChannel::ECC_Visibility, 
		bool UseFootsLock = false, 
		float TraceAboveFoot = 50.0, 
		float TraceBelowFoot = 45.0, 
		float FootHeight = 13.5, 
		int ActiveOnLOD = 1, 
		int TraceDebugIndex = 0, 
		float DebugTime = 0.2
	);

	UFUNCTION(BlueprintCallable, Category = "Foot IK Core", meta = (DisplayName = "Set Foot Offsets", Keywords = "Foots IK"))
	bool SetFootOffsets(
		FName Enable_FootIK_Curve, 
		FName IKFootBone, 
		FName RootBone, 
		UPARAM(ref) FVector& CurrentLocationTarget,
		UPARAM(ref) FVector& CurrentLocationOffset, 
		UPARAM(ref) FRotator& CurrentRotationOffset,
		ECollisionChannel TraceChannel = ECollisionChannel::ECC_Visibility,
		float TraceAboveFoot = 50.0, 
		float TraceBelowFoot = 45.0, 
		float FootHeight = 13.5, 
		int TraceDebugIndex = 0, 
		float DebugTime = 0.2
	);

	UFUNCTION(BlueprintCallable, Category = "Foot IK Core", meta = (DisplayName = "Set Pelvis IK Offset", Keywords = "Foots IK"))
	void SetPelvisIK_Offset(FVector FootOffset_L_Target, FVector FootOffset_R_Target);

	UFUNCTION(BlueprintCallable, Category = "Foot IK Core", meta = (DisplayName = "Reset IK Offsets", Keywords = "Foots IK"))
	void ResetIK_Offsets(float InterpSpeed = 15.0);


	UFUNCTION(BlueprintCallable, Category = "Motion Matching Core", meta = (DisplayName = "Update Layering Values", Keywords = "Motion Matching", BlueprintThreadSafe))
	void UpdateLayeringValues();

	/*This function is used to determine if the character is starting to move by checking if the future velocity is greater than the current velocity. If the current Database asset is a pivot database, 
	this function will always return false. This prevents the Motion Matching system from interrupting a pivot, since the second half of a pivot is very similar to a start.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Is Starting", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool IsStarting();

	/*This function is used to determine if the character is starting to move by checking if the future velocity is greater than the current velocity. If the current Database asset is a pivot database, 
	this function will always return false. This prevents the Motion Matching system from interrupting a pivot, since the second half of a pivot is very similar to a start.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Is Stopping", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool IsStopping();

	/*This function is used to determine if the character is pivoting by checking if the character’s future trajectory is moving in a much different direction than the character’s current trajectory. 
	The Rotation Modes have a different threshold, since 45 degree pivots work nicely during strafing, but are not necessary during Orient to Movement.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Is Pivoting", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool IsPivoting();

	/*If the root bone rotation and character’s capsule rotations are very different while moving, this function will allow a spin transition animation to play. Spin transitions are locomotion animations 
	that rotate the character while moving in a fixed world direction, and are useful when switching rotation modes. 
	For example, if the character is running toward the camera using the Orient to Movement mode and then switching to strafe, this would require the character to spin 180 degrees very quickly. 
	A spin transition animation would be an ideal transition for this gameplay scenario. Currently, we are using refacing starts in place of spin transitions, but plan to provide actual spin transition 
	data in a future release.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Should Spin Transition", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool ShouldSpinTransition();

	/*If the character has just landed and the land velocity was less than the heavy land speed threshold, play light land animations.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Just Laned Light", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool JustLanedLight();

	/*If the character has just landed and the land velocity was greater than the heavy land speed threshold, play heavy land animations.*/
	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Just Laned Heavy", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool JustLanedHeavy();

	UFUNCTION(BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Just Laned Neutral", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool JustLanedNeutral();

	/*This function is used to determine if the character is turning in place by checking if the root bone rotation is different from the character’s capsule rotation. For this project, 
	if the rotation is greater than 50 degrees and the character is currently aiming, the character should be turned in place. We also allow turn in places to play if the character has just stopped, 
	which gives us a "Stick Flick" behavior. Turn in place behavior during the aiming state is still WIP. 
	Additional limits need to be applied to the steering or root offset node to prevent the character from lagging too far behind.*/
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "MM Movement Analyze", meta = (DisplayName = "Should Turn In Place", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool ShouldTurnInPlace();

	virtual bool ShouldTurnInPlace_Implementation();

	//
	// Root Bone Offset Configurations Functions -----------------------------------------------------------------------------------------------------------------------------------------
	//

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "MM Root Offset", meta = (DisplayName = "Get Offset Root Rotation Mode", Keywords = "Motion Matching", BlueprintThreadSafe))
	int GetOffsetRootRotationMode();

	virtual int GetOffsetRootRotationMode_Implementation();

	/*This function is used to determine the Offset Root Translation mode. If we are currently playing a montage in the default slot, if we are in the air, or if we are on the ground but not moving, 
	we do not want to maintain any Translational offset.
	The Release Enum essentially blends out any offset, after which it will be locked to the capsule location, just as it would be without a root offset node. 
	The Interpolate Enum means the root is allowed to deviate slightly from the capsule location based on root motion, but will always try to interpolate back toward center. \
	This is helpful when the animation data and capsule movement are not perfectly matched, such as during starts, pivots, and other complex movements.*/
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "MM Root Offset", meta = (DisplayName = "Get Offset Root Location Mode", Keywords = "Motion Matching", BlueprintThreadSafe))
	int GetOffsetRootLocationMode();

	virtual int GetOffsetRootLocationMode_Implementation();

	UFUNCTION(BlueprintPure, Category = "MM Root Offset", meta = (DisplayName = "Get Offset Root Translation Half Life", Keywords = "Motion Matching", BlueprintThreadSafe))
	float GetOffsetRootTranslationHalfLife();

	//
	// Motion Matching ANIM GRAPH NODE CONFIG AND DATA ---------------------------------------------------------------------------------------------------------------------------------
	//

	/* This function is used to change the blend time of the Motion Matching node, based on the current and previous states.
	In the future, we plan to allow blend times to be more directly set from the chosen databases.*/
	UFUNCTION(BlueprintPure, Category = "MM Pose Search", meta = (DisplayName = "Get MM Blend Time", Keywords = "Motion Matching", BlueprintThreadSafe))
	float Get_MMBlendTime();

	/*This function is called after the Motion Matching node has selected an animation. In this case, we cache the database the selected animation is in, in order to grab the tags in the 
	EventGraph (due to a thread safety issue).
	In the future, we plan to use this function to control additional things such as blend time and blend profiles based on the selected animation.*/
	UFUNCTION(BlueprintCallable, Category = "MM Pose Search", meta = (DisplayName = "Update Motion Matching Post Selection", Keywords = "Motion Matching", BlueprintThreadSafe))
	void UpdateMotionMatchingPostSelection(UPARAM(ref) FAnimUpdateContext& Context, UPARAM(ref) FAnimNodeReference& Node);

	/*This function determines if root motion from the animations can be steered by checking if the character is moving or in the air. This prevents idle animations from getting steered, which could cause sliding.*/
	UFUNCTION(BlueprintPure, Category = "MM Pose Search", meta = (DisplayName = "Enable Steering", Keywords = "Motion Matching", BlueprintThreadSafe))
	bool EnableSterring();

	/*This function gives the steering node a target rotation. This target is calculated using the future facing direction from the predicted trajectory. This allows the steering node to 
	rotate toward a future direction, rather than always steering toward the current actor rotation, which could cause it to lag too far behind.*/
	UFUNCTION(BlueprintPure, Category = "MM Pose Search", meta = (DisplayName = "Get Desired Facing", Keywords = "Motion Matching", BlueprintThreadSafe))
	FQuat GetDesiredFacing();

private:
	float VectorLenghtXY(FVector In);

};
