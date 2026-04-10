

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "AsyncFunctionsPlayer.h"
#include "AGLS_CharacterMovementComponent.h"
#include "AGLS_AI_HumanCharInterface.h"
#include "ALS_BaseAI_CharacterCpp.h"
#include "AGLS_HumanAI_CharacterBase.generated.h"

/**
 * 
 */
UCLASS()
class HELPFULFUNCTIONS_API AAGLS_HumanAI_CharacterBase : public AALS_BaseAI_CharacterCpp, public IAGLS_AI_HumanCharInterface
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Anims Assets", meta = (AllowPrivateAccess = "True"))
	TSoftObjectPtr<UAnimSequence> LightLandMontage = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Anims Assets", meta = (AllowPrivateAccess = "True"))
	TSoftObjectPtr<UAnimSequence> HardLandMontage = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Grounded", meta = (AllowPrivateAccess = "True"))
	UCurveFloat* StrafeSpeedMapCurveC = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Grounded", meta = (AllowPrivateAccess = "True"))
	FCALSMovementSettingsStrafe GroundMoveSettingsVectorC;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Grounded", meta = (AllowPrivateAccess = "True"))
	FCALSMovementSettings GroundMoveSettingsScalarC;
	
public:

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsDeathC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool HitedByKnifeOrHandC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsEquipPropC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsReloadingWeaponC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsHostageC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsVicForSF = false; //Is Vis For Stealth Finisher Action

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool StartStealthKillC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool IsSittingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool CanUpdateStanceC = true;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool CanUpdateRotationMode = true;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool LockOverlayChangeC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool LODsUp = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	bool BreakFall = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Rotation", meta = (AllowPrivateAccess = "True"))
	FRotator TargetAiming = FRotator(0,0,0);

	// IMPORTANT VARIABLE: If CurrentHealth < 0 OR CurrentHealth > MaxHealth THEN on BeginPlay CurrentHealth = MaxHealth
	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	float CurrentHealthPoints = 1000.0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Essential", meta = (AllowPrivateAccess = "True"))
	float SpeedScaleC = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Ragdoll", meta = (AllowPrivateAccess = "True"))
	bool RagdollOnGround = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Ragdoll", meta = (AllowPrivateAccess = "True"))
	bool RagdollFaceUp = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Ragdoll", meta = (AllowPrivateAccess = "True"))
	FVector LastRagdollVelocity = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Ragdoll", meta = (AllowPrivateAccess = "True"))
	float DeathTime = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Ragdoll", meta = (AllowPrivateAccess = "True"))
	UAnimMontage* RagdollGetUpAnim = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Shooting System", meta = (AllowPrivateAccess = "True"))
	bool HaveRifle = true;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Shooting System", meta = (AllowPrivateAccess = "True"))
	bool HavePistol = true;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Shooting System", meta = (AllowPrivateAccess = "True"))
	bool CanShoot = true;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Shooting System", meta = (AllowPrivateAccess = "True"))
	float DealingDamageScalar = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Shooting System", meta = (AllowPrivateAccess = "True"))
	FVector2D DispersionOffset = FVector2D(0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	CALS_MovementState PrevMovementStateC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	CALS_RotationMode DesiredRotationModeC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	CALS_Stance DesiredStanceC;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	CALS_OverlayState PrevOverlayState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	CALS_GroundedMoveMode GroundedMoveModeC = CALS_GroundedMoveMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|States", meta = (AllowPrivateAccess = "True"))
	AGLS_LOD_State LOD_State = AGLS_LOD_State::LOD0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	FVector2D DefCapsuleSizeC = FVector2D(30.0,90.0);

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	bool IsPlayingPushAnimationC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	bool IsPlayingHitByZombieMontageC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	FVector PreviousVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	float PreviousAimYawC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	UAnimMontage* DyingMontageC = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	AActor* HostageAtt = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	TMap<CALS_OverlayState, FCALS_PropsAttachValues> PropsAttachValues;

	UPROPERTY(BlueprintReadWrite, Category = "Human AI|Cached Values", meta = (AllowPrivateAccess = "True"))
	UPrimitiveComponent* CurrentHoldingProp = nullptr;


	//Implemented in AGLS v1.2+
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True"))
	bool RotateCapsuleToControlInIdle = false;

	//Implemented in AGLS v1.6
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True"))
	bool DontCalculateCustomRotOnVelocityMode = false;

	//Implemented in AGLS v1.6
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True"))
	bool UseRotateToVelocityOnSprintGait = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True", EditCondition="RotateCapsuleToControlInIdle==true"))
	float CapsuleRotationRateInIdle = 8.0;

	//This valie is required when current playing animation has anim curve when editing Deceleration value for Character Movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True"))
	float DecelerationBiasScale = 1.0;

	//This valie is required when current playing animation has anim curve when editing GroundFriction value for Character Movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Human AI|Movement", meta = (AllowPrivateAccess = "True"))
	float GroundFrictionBiasScale = 1.0;


	bool StopMantleTimeline = false;
	float RotationChangeTimer = -1.0;

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	UAsyncFunctionsPlayer* AsyncFunctionsComponent;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	//virtual bool CanCrouch() const override;
	//virtual void Crouch(bool bClientSimulation = false) override;
	//virtual void UnCrouch(bool bClientSimulation = false) override;

	AAGLS_HumanAI_CharacterBase();

protected:

	virtual void CalculateGroundedRotation() override;

	UFUNCTION(BlueprintCallable, Category = "Human AI|Cover System", meta = (WorldContext = "WorldContextObject", DisplayName = "Find Cover", Keywords = "AGLS Character", AdvancedDisplay = "TraceChannel,ForwardTraceLenght,GroundOffset,ActiveOnLOD"))
	virtual void FindCoverFast(
		bool& CanCover, 
		float& WallHeigh, 
		FVector& Impact, 
		FVector& Normal, 
		FVector StartLocation, 
		FVector StartDirection, 
		int DebugIndex, 
		ECollisionChannel TraceChannel = ECollisionChannel::ECC_Visibility, 
		float ForwardTraceLenght = 70.0, 
		float GroundOffset = 20.0
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Health And Damage", meta = (ForceAsFunction, DisplayName = "Check Health Per Frame", Keywords = "AGLS Character"))
	void CheckHealthPerFrame(bool Update = true);
	virtual void CheckHealthPerFrame_Implementation(bool Update = true);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Health And Damage", meta = (ForceAsFunction, DisplayName = "Start Dying Sequence", Keywords = "AGLS Character"))
	bool StartDyingSequence(FVector ImpulseNormal, FVector ImpulseLocation, CALS_DeathType DeathType, FName HitBoneName, int RifleModelIndex);
	virtual bool StartDyingSequence_Implementation(FVector ImpulseNormal, FVector ImpulseLocation, CALS_DeathType DeathType, FName HitBoneName, int RifleModelIndex);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Human AI|Shooting System", meta = (DisplayName = "Get Rifle Ammo Values", Keywords = "AGLS Character"))
	bool GetRifleAmmoData(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount);
	virtual bool GetRifleAmmoData_Implementation(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Human AI|Shooting System", meta = (DisplayName = "Get Pistol Ammo Values", Keywords = "AGLS Character"))
	bool GetPistolAmmoData(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount);
	virtual bool GetPistolAmmoData_Implementation(int& CurrentAmmoCount, int& CurrentMagazinesCount, int& MaxAmmoCount, int& MaxMagazinesCount);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll Start", Keywords = "AGLS Character"))
	void RagdollStart();
	virtual void RagdollStart_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll End", Keywords = "AGLS Character"))
	void RagdollEnd();
	virtual void RagdollEnd_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Human AI|Overlay", meta = (ForceAsFunction, DisplayName = "Can Update Overlay", Keywords = "AGLS Character"))
	bool CanUpdateOverlay(CALS_OverlayState NewOverlay, CALS_OverlayState CurrentOverlay, bool forced);
	virtual bool CanUpdateOverlay_Implementation(CALS_OverlayState NewOverlay, CALS_OverlayState CurrentOverlay, bool forced);

	UFUNCTION(BlueprintPure, Category = "Human AI|Overlay", meta = (DisplayName = "Change To Pistol When No Ammo", Keywords = "ALS Character"))
	CALS_OverlayState ChangeToPistolWhenNoAmmo(CALS_OverlayState NewOverlay);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll Update", Keywords = "ALS Character", AdvancedDisplay = "OutSpringRange,InDamping,InForceLimit,DebugIndex"))
	void RagdollUpdate(float OutSpringRange = 25000.0, float InDamping = 0.0, float InForceLimit = 0.0, int DebugIndex = 0);
	virtual void RagdollUpdate_Implementation(float OutSpringRange = 25000.0, float InDamping = 0.0, float InForceLimit = 0.0, int DebugIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Human AI|Utility", meta = (DisplayName = "Smooth Capsule Radius Update", Keywords = "ALS Character"))
	void SmoothedCapsuleRadiusUpdate(float TargetHeight = 90.0, float InterpSpeed = 30.0, bool UpdateOverlaps = true);

	UFUNCTION(BlueprintPure, Category = "Human AI|Utility", meta = (DisplayName = "Is Alive", Keywords = "ALS Character"))
	bool IsAlive();

	UFUNCTION(BlueprintPure, Category = "Human AI|Utility", meta = (DisplayName = "Get Calpsule Base Location", Keywords = "ALS Character"))
	FVector GetCalpsuleBaseLocation(float ZOffset);

	UFUNCTION(BlueprintPure, Category = "Human AI|Utility", meta = (DisplayName = "Get Capsule Location From Base", Keywords = "ALS Character"))
	FVector GetCapsuleLocationFromBase(FVector BaseLocation, float ZOffset);

	UFUNCTION(BlueprintPure, Category = "Human AI|Utility", meta = (DisplayName = "Get Control Forward Right Vector", Keywords = "ALS Character"))
	void GetControlForwardRightVector(FVector& ReturnForward, FVector& ReturnRight);

	//States Functions [][][][][][][][][][][][] [S] [T] [A] [T] [E] [S]   [F] [U] [N] [C] [T] [I] [O] [N] [S]

	UFUNCTION(BlueprintCallable, Category = "Human AI|State Changes", meta = (DisplayName = "On Character Movement Mode Changed", Keywords = "ALS Character"))
	void OnCharacterMovementModeChanged(EMovementMode PrevMovementMode, EMovementMode NewMovementMode, uint8  PrevCustomMode, uint8  NewCustomMode);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|State Changes", meta = (ForceAsFunction, DisplayName = "On Movement State Changed", Keywords = "AGLS Character"))
	void OnMovementStateChanged(CALS_MovementState NewMovementState);
	virtual void OnMovementStateChanged_Implementation(CALS_MovementState NewMovementState);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|State Changes", meta = (ForceAsFunction, DisplayName = "On Movement Action Changed", Keywords = "AGLS Character"))
	void OnMovementActionChanged(CALS_MovementAction NewMovementAction);
	virtual void OnMovementActionChanged_Implementation(CALS_MovementAction NewMovementAction);

	UFUNCTION(BlueprintCallable, Category = "Human AI|State Changes", meta = (DisplayName = "On Stance Changed", Keywords = "ALS Character"))
	void OnStanceChanged(CALS_Stance NewStance);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|State Changes", meta = (ForceAsFunction, DisplayName = "On Rotation Mode Changed", Keywords = "AGLS Character"))
	void OnRotationModeChanged(CALS_RotationMode NewRotationMode, float LockTime = 0.5);
	virtual void OnRotationModeChanged_Implementation(CALS_RotationMode NewRotationMode, float LockTime = 0.5);

	UFUNCTION(BlueprintCallable, Category = "Human AI|State Changes", meta = (DisplayName = "On Gait Changed", Keywords = "ALS Character"))
	void OnGaitChanged(CALS_Gait NewActualGait);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|State Changes", meta = (ForceAsFunction, DisplayName = "On Overlay State Changed", Keywords = "AGLS Character"))
	void OnOverlayStateChanged(CALS_OverlayState NewOverlayState);
	virtual void OnOverlayStateChanged_Implementation(CALS_OverlayState NewOverlayState);

	// -------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Held Objects", meta = (ForceAsFunction, DisplayName = "Drop Props", Keywords = "AGLS Character", AutoCreateRefTerm = "PropsList"))
	void DropProps(const TArray<CALS_OverlayState>& PropsList, bool DropGunsAttachedToBody);
	virtual void DropProps_Implementation(const TArray<CALS_OverlayState>& PropsList, bool DropGunsAttachedToBody);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Essential", meta = (ForceAsFunction, DisplayName = "Set Current Movement Parameters Values", Keywords = "AGLS Character"))
	void SetCurrentMovementParametersValues(bool UseVectorControl);
	virtual void SetCurrentMovementParametersValues_Implementation(bool UseVectorControl);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Essential", meta = (ForceAsFunction, DisplayName = "Update Grounded Movement Values", Keywords = "AGLS Character"))
	void UpdateGroundedMovementValues(bool UseVectorControl);
	virtual void UpdateGroundedMovementValues_Implementation(bool UseVectorControl);

	UFUNCTION(BlueprintPure, Category = "Human AI|Essential", meta = (DisplayName = "Get Target Speed With Strafe", Keywords = "AGLS Character"))
	float GetTargetSpeedWithStrafe(FVector SpeedValues);

	CALS_Gait GetActualGaitStrafe(CALS_Gait AllowedGait);
	float GetMappedSpeedStrafe(float SpeedScale = 1.0);
	void UpdateGroundedMoveSettingsStrafe(CALS_Gait AllowedGait, FCALSMovementSettingsStrafe CurrentMovement);

	//More Functions For Human AI
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Health And Damage", meta = (ForceAsFunction, DisplayName = "Check Fall Damage", Keywords = "AGLS Character", AdvancedDisplay = "VelocityMaxRange,DamageTresholds"))
	bool CheckFallDamage(FHitResult LandHitResult, float VelocityMaxRange = 1500.0, FVector DamageTresholds = FVector(0.7, 0.55, 0.42));
	virtual bool CheckFallDamage_Implementation(FHitResult LandHitResult, float VelocityMaxRange = 1500.0, FVector DamageTresholds = FVector(0.7, 0.55, 0.42));

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Health And Damage", meta = (ForceAsFunction, DisplayName = "Death By Fall Damage", Keywords = "AGLS Character"))
	void DeathByFallDamage(FHitResult HitGroundResult);
	virtual void DeathByFallDamage_Implementation(FHitResult HitGroundResult);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Health And Damage", meta = (DisplayName = "Apply Damage Value", Keywords = "AGLS Character"))
	bool ApplyDamageValue(AActor* DamageCauser, FHitResult HitInfo, float DamageValue = 1.0, int DamageTypeIndex = 1, float UnRagdollTime = -1.0);
	virtual bool ApplyDamageValue_Implementation(AActor* DamageCauser, FHitResult HitInfo, float DamageValue = 1.0, int DamageTypeIndex = 1, float UnRagdollTime = -1.0);

	UFUNCTION(BlueprintCallable, Category = "Human AI|Held Objects", meta = (DisplayName = "Make For Props Attach Values", Keywords = "AGLS Character"))
	void MakeForPropsAttachValues();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Held Objects", meta = (DisplayName = "AttachPropTo", Keywords = "AGLS Character"))
	void AttachPropTo(CALS_OverlayState TargetOverlay, float DelayTime);
	virtual void AttachPropTo_Implementation(CALS_OverlayState TargetOverlay, float DelayTime);

	UFUNCTION(BlueprintCallable, Category = "Human AI|Held Objects", meta = (DisplayName = "Attach All Props To Default Parent", Keywords = "AGLS Character"))
	void AttachAllPropsToDefault();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Traversal", meta = (ForceAsFunction, DisplayName = "Try Simple Traversal Action", Keywords = "AGLS Character"))
	bool TrySimpleTraversalAction();
	virtual bool TrySimpleTraversalAction_Implementation();

	UFUNCTION(BlueprintPure, Category = "Human AI|Shooting System", meta = (DisplayName = "Check Can Shoot", Keywords = "AGLS Character"))
	bool CheckCanShoot();

	UFUNCTION(BlueprintCallable, Category = "Human AI|Shooting System", meta = (DisplayName = "Shot First Trace Check", Keywords = "AGLS Character", AutoCreateRefTerm = "ObjectsType", AdvancedDisplay = 2))
	bool ShotFirstTraceCheck(
		bool& StartScan,
		FHitResult& HitResult,
		FVector& OutScanOrigin,
		FVector& OutTraceDirection,
		const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectsType,
		FName HeadSocketName = FName("Head"),
		FName MuzzleSocketName = FName("Muzzle"),
		float BlendWithMuzzle = 0.5, 
		float TraceBasicLenght = 5000.0, 
		float DispersionScale = 1.0, 
		float WallPenetrationLen = 100.0,
		int DebugTraceIndex = 0
	);

	UFUNCTION(BlueprintCallable, Category = "Human AI|Shooting System", meta = (DisplayName = "Shot Scan Trace Check", Keywords = "AGLS Character", AutoCreateRefTerm = "ObjectsType", AdvancedDisplay = 2))
	bool ShotScanTraceCheck(
		FHitResult& HitResult,
		const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectsType,
		bool StartScan = false,
		FVector Origin = FVector(0, 0, 0),
		float TraceBasicLenght = 5000.0,
		int DebugTraceIndex = 0
	);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Human AI|Ragdoll", meta = (ForceAsFunction, DisplayName = "Get Get Up Animation", Keywords = "AGLS Character"))
	TSoftObjectPtr<UAnimMontage> GetGetUpAnimation();
	virtual TSoftObjectPtr<UAnimMontage> GetGetUpAnimation_Implementation();

	//AGLS v1.6  09.09.2025
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Human AI|Essential", meta = (ForceAsFunction, DisplayName = "Calculate Custom Character Rotation", Keywords = "AGLS Character"))
	void CalculateCustomCharacterRotation();
	virtual void CalculateCustomCharacterRotation_Implementation();


private:

	FVector N_to_R_Vector(FVector In);
	bool GetIsHaveSniperRifle();
	float GetSurfaceDurability(EPhysicalSurface InSurface);

};
