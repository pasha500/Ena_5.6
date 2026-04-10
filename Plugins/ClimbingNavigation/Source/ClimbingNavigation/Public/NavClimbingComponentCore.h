

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ClimbingNavigationBPLibrary.h"
#include "ClimbNavigationStorageActor.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#if WITH_EDITOR
#include "GraphDebuggerCore.h"
#endif
#include "NavClimbingComponentCore.generated.h"

//meta = (SliderMin = "0.1", SliderMax = "1.0", UIMin = "0.1", UIMax = "1.0")

struct FNavClimb_CompAndTransform : public FTableRowBase
{
	FTransform Transform = FTransform::Identity;

	UPrimitiveComponent* Component = nullptr;

};


UCLASS(Blueprintable, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLIMBINGNAVIGATION_API UNavClimbingComponentCore : public UActorComponent
{
	GENERATED_BODY()

#if WITH_EDITOR
private:
	GraphDebuggerCore Debugger;

#endif

public:	
	// Sets default values for this component's properties
	UNavClimbingComponentCore();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config")
	bool FollowPlayerWhenClimb = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config")
	float CorrectDistanceTollerance = 50.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "0.1", SliderMax = "2.0", UIMin = "0.1", UIMax = "2.0"))
	float MovementSpeedScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "0.0", SliderMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float WaitTimeBetweenJumps = 0.12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config")
	float ToPlayerCorrectDistance = 200.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "100.0", SliderMax = "300.0", UIMin = "100.0", UIMax = "300.0"))
	float MaxDistanceBetweenLedges = 180;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "50.0", SliderMax = "200.0", UIMin = "50.0", UIMax = "200.0"))
	float LongJumpMinDistance = 80.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "0.1", SliderMax = "10.0", UIMin = "0.1", UIMax = "10.0"))
	float FollowingTickInterval = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config", meta = (SliderMin = "0.0", SliderMax = "50.0", UIMin = "0.0", UIMax = "50.0"))
	float CapConstForwardOffset = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config")
	TMap<ENavClimbActionState, UCurveVector*> TrajectoryInterpCurves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config|Release")
	UAnimMontage* MantleReleaseLedgeMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config|Release")
	UCurveVector* MantleReleaseTrajectoryCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config|Debug")
	bool bPrintAditionalInfoInLog = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Climb|Config|Debug")
	bool bPrintAditionalInfoOnScreen = false;






	UPROPERTY(BlueprintReadOnly, Category = "Nav Climb|References")
	ACharacter* Character = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|References")
	AClimbNavigationStorageActor* CurrentNavStorageActor = nullptr;


	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Ledge Structs")
	FClimbNav_SingleClimbPoint CurrentLedgeParams;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Ledge Structs")
	FClimbNav_SingleClimbPoint NextLedgeParams;

	TArray<FClimbNav_SingleClimbPoint> PathPointsData;


	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Basic States")
	int CurrentLedgeIndex = 0;

	bool IsInterpolatingToNext = false;

	bool IsInterpolatingToStart = false;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Basic States")
	bool IsClimbing = false;

	int ClimbingActionStateIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Basic States")
	ENavClimbActionState NavClimbActionState = ENavClimbActionState::None;

	//Timers
	float TimerDuration = 0.0;
	float MovingTime = 0.0;


	// Release Ledge Climbing
	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Release")
	bool StartedReleaseLedge = false;

	FNavClimb_CompAndTransform TargetTransformLS;

	FNavClimb_CompAndTransform CurrentTransformLS;


	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Cached")
	bool PrevPauseMove = false;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Cached")
	bool PathRefreshedWhenFollow = false;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Animation")
	EClimbMovementDirection MovementDirection = EClimbMovementDirection::Up;

	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Animation")
	FVector AnimJumpsParams = FVector(0, 0, 0);


	UPROPERTY(BlueprintReadWrite, Category = "Nav Climb|Core")
	bool PauseCurrentMove = false;

	FTimerHandle FollowPlayerTimerHandle;
	FTimerHandle MainTimerHandle;
	FTimerHandle ReleaseLedgeTimerHandle;
	FTimerHandle WaitTimerHandle;
	FTimerHandle JumpToFirstLedgeHandle;
	bool bMainTimerFinished = false;

	//Utilities functions

	UFUNCTION(BlueprintPure, Category = "Nav Climb|Utility", meta = (DisplayName = "Scale Forward Cap Offset", Keywords = "Navigation,Climbing"))
	FTransform ScaleForwardCapOffset(FTransform InTransform);

	UFUNCTION(BlueprintPure, Category = "Nav Climb|Utility", meta = (DisplayName = "Timer Finished Tollerance", Keywords = "Navigation,Climbing"))
	float TimerFinishedTollerance();

	UFUNCTION(BlueprintPure, Category = "Nav Climb|Utility", meta = (DisplayName = "Get Path Points Struct", Keywords = "Navigation,Climbing"))
	TArray< FClimbNav_SingleClimbPoint> GetPathPointsStruct();

	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Utility", meta = (DisplayName = "Set Transform When", Keywords = "Navigation,Climbing"))
	void SetTransformWhen(FVector NewLocation, FRotator NewRotation, bool bSweep);



	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Initialize", meta = (ForceAsFunction, DisplayName = "Initialize Component", Keywords = "Navigation,Climbing"))
	void InitializeComponent();
	virtual void InitializeComponent_Implementation();


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Core", meta = (ForceAsFunction, DisplayName = "Try Perform Moving On Climb Path", Keywords = "Navigation,Climbing"))
	bool TryPerformMovingOnClimbPath();
	virtual bool TryPerformMovingOnClimbPath_Implementation();

	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Core", meta = (DisplayName = "Switch To Next Path Point", Keywords = "Navigation,Climbing"))
	void SwitchToNextPathPoint(bool& ItsEndingPoint, float& NextActionDelay, bool UpdateTimer, float WaitTimeScale = 1.0);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Core", meta = (ForceAsFunction, DisplayName = "Update Movement On Path", Keywords = "Navigation,Climbing"))
	void UpdateMovementOnPath();
	virtual void UpdateMovementOnPath_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Nav Climb|Core", meta = (DisplayName = "Trajectory Interpolation Method", Keywords = "Navigation,Climbing"))
	FTransform ChooseInterpolationMethod();
	virtual FTransform ChooseInterpolationMethod_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Core", meta = (ForceAsFunction, DisplayName = "Set Target Action State", Keywords = "Navigation,Climbing"))
	void SetTargetActionState();
	virtual void SetTargetActionState_Implementation();


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Anim Control", meta = (ForceAsFunction, DisplayName = "Update Anim Values When Start Action", Keywords = "Navigation,Climbing"))
	void UpdateAnimValuesWhenClimb();
	virtual void UpdateAnimValuesWhenClimb_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Anim Control", meta = (ForceAsFunction, DisplayName = "Set Climb Enable For References", Keywords = "Navigation,Climbing"))
	void PrepareAnimInstParams(bool Enable = true);
	virtual void PrepareAnimInstParams_Implementation(bool Enable = true);


	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Control", meta = (DisplayName = "Perform Following Player", Keywords = "Navigation,Climbing"))
	void PerformFollowing();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Nav Climb|Control", meta = (ForceAsFunction, DisplayName = "Get Player Is Climbing", Keywords = "Navigation,Climbing"))
	bool GetPlayerIsClimbing(int PlayerIndex);
	virtual bool GetPlayerIsClimbing_Implementation(int PlayerIndex);


	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Starting", meta = (DisplayName = "Jump To Ledge Climb", Keywords = "Navigation,Climbing", AdvancedDisplay = "bStopMontages,WarpOffset"))
	bool JumpToLedgeClimb(FVector NavPointLocation, FVector NavPointDirection, AClimbNavigationStorageActor* NavActor, bool bStopMontages = true, FVector WarpOffset = FVector(0,0,-98.0));


public:
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Nav Climb|Starting", meta = (DisplayName = "Get Starting Climb Montage", Keywords = "Navigation,Climbing"))
	UAnimMontage* GetStartingClimbMontage(float& PlayRate, float& StartTime);
	virtual UAnimMontage* GetStartingClimbMontage_Implementation(float& PlayRate, float& StartTime);

protected:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Starting", meta = (ForceAsFunction, DisplayName = "Update Prepare To Climb Ledge", Keywords = "Navigation,Climbing"))
	void UpdatePrepareToClimbLedge();
	virtual void UpdatePrepareToClimbLedge_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Release", meta = (DisplayName = "Release Ledge Finished", Keywords = "Navigation,Climbing"))
	void ReleaseLedgeFinished();
	virtual void ReleaseLedgeFinished_Implementation();

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Core", meta = (DisplayName = "Move To Target On Climb Navigation", Keywords = "Navigation,Climbing"))
	bool MoveToTargetOnClimbNavigation(FVector StartLocation, FVector EndLocation, float MaxPathLenght = -1.0, float MaxMovingTime = 9999.0);
	virtual bool MoveToTargetOnClimbNavigation_Implementation(FVector StartLocation, FVector EndLocation, float MaxPathLenght = -1.0, float MaxMovingTime = 9999.0);

	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Control", meta = (DisplayName = "Pause Movement", Keywords = "Navigation,Climbing"))
	void TryPauseMovement(bool bPause = true);

	UFUNCTION(BlueprintCallable, Category = "Nav Climb|Release", meta = (DisplayName = "Check Can Release Climb", Keywords = "Navigation,Climbing", AdvancedDisplay = 6))
	bool CheckCanReleaseClimb(
		float& Duration,
		bool bRunWhenCheckValid,
		FVector LedgeOrigin,
		FVector CheckDirection,
		FVector ActorLocation,
		bool PreferJumpDown,
		int MaxSearchInterations = 3,
		float TraceForwardOffset = 35.0,
		FVector2D TraceOffsetUp = FVector2D(50, -25),
		FVector2D TraceOffsetDown = FVector2D(-20, -330),
		ECollisionChannel TraceChannel = ECollisionChannel::ECC_Visibility
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Release", meta = (ForceAsFunction, DisplayName = "Start Release Ledge Climb", Keywords = "Navigation,Climbing"))
	bool StartReleaseLedgeClimb(float& ReturnAnimDuration, FTransform CapTransformWS, UPrimitiveComponent* LedgeComponent, FVector LedgeOrigin, bool bForJumpDown);
	virtual bool StartReleaseLedgeClimb_Implementation(float& ReturnAnimDuration, FTransform CapTransformWS, UPrimitiveComponent* LedgeComponent, FVector LedgeOrigin, bool bForJumpDown);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Release", meta = (ForceAsFunction, DisplayName = "Drop From Ledge Hold", Keywords = "Navigation,Climbing"))
	void DropFromLedgeHold();
	virtual void DropFromLedgeHold_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Nav Climb|Release", meta = (ForceAsFunction, DisplayName = "Update When Release Ledge", Keywords = "Navigation,Climbing"))
	float UpdateWhenReleaseLedge();
	virtual float UpdateWhenReleaseLedge_Implementation();


private:

	float SelectFloatOnActionState(float None, float WalkCycle, float JumpShort, float JumpLong);

	FTransform ExtractedInterpolation(FTransform A, FTransform B, float VX, float VY, float VZ, float RYaw, float Alpha);

	float TryGetAnimCurveValue(FName CurveName);

	void SetNewActionState(int NewIndex);

	//Timers macros functions
	float GetTimerElapsedTime(FTimerHandle Handle);
	bool GetTimerIsActivated(FTimerHandle Handle);

protected:
	UFUNCTION(BlueprintPure, Category = "Nav Climb|Utility", meta = (DisplayName = "Get Action Normalized Time Elapsed", Keywords = "Navigation,Climbing"))
	float NormalizedMainTimer();

private:

	//Timers handle functions
	void FollowPlayerTimer();
	void MainTimer();
	void ReleaseLedgeTimer();
	void WaitTimerEvent();
	void JumpToFirstLedgeEvent();


	void LogInfo(FString Message);

	void DrawDebugCurrentPath();

	FNavClimb_CompAndTransform SimpleConvertSpace(FNavClimb_CompAndTransform InStruct, bool bToLocalSpace = true);
};
