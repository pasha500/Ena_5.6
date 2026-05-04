

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "IWALS_BaseAttributeSet.h"
#include "IWALS_GameplayAbilitySet.h"
#include "Abilities/GameplayAbility.h"
#include <GameplayEffectTypes.h>
#include "GameplayTagContainer.h"
#include "GameFramework/Character.h"
#include "GAS_MainCharacterCpp.generated.h"

class UUserWidget;
class UActorComponent;
class UTexture2D;
class UWidgetComponent;
class USpringArmComponent;
class UCameraComponent;

// Secondary tick function that fires in TG_PostUpdateWork — after all BP EventTicks.
// This guarantees SweepNonTargetIcons runs after zombie BP can re-show icons.
USTRUCT()
struct FLockOnSweepTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()
	class AGAS_MainCharacterCpp* Target = nullptr;
	IWALS_ABILITYSYSTEM_API virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	IWALS_ABILITYSYSTEM_API virtual FString DiagnosticMessage() override;
};

template<>
struct TStructOpsTypeTraits<FLockOnSweepTickFunction> : public TStructOpsTypeTraitsBase2<FLockOnSweepTickFunction>
{
	enum { WithCopy = false };
};

USTRUCT(BlueprintType)
struct FMovementSettingsStrafe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	FVector WalkSpeed = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	FVector RunSpeed = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	FVector SprintSpeed = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve")
	UCurveVector* MovementCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve")
	UCurveFloat* RotationRateCurve = nullptr;
};


UCLASS()
class IWALS_ABILITYSYSTEM_API AGAS_MainCharacterCpp : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AGAS_MainCharacterCpp();

	//Define Base Variables For ALS Character
	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		FVector2D DefCapsuleSizeC = FVector2D(30,90);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool IsMovingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool HasMovementInputC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool IsStartedMovementOnTargetC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True", DisplayName = "Start Interaction With Dynamic Prop C"))
		bool InteractionWithPropC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool IsLayBackC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool IsSwimmingC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Ragdoll System", meta = (AllowPrivateAccess = "True"))
		bool RagdollOnGroundC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Ragdoll System", meta = (AllowPrivateAccess = "True"))
		bool RagdollFaceUpC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		FVector AccelerationC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		FVector RelativeAcceleractionC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		FRotator LastVelocityRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		FRotator LastMovementInputRotationC = FRotator(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Cached Variables", meta = (AllowPrivateAccess = "True"))
		FVector PreviousVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Ragdoll System", meta = (AllowPrivateAccess = "True"))
		FVector LastRagdollVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		float SpeedC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		float MovementInputAmountC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		float MovementSpeedDifferenceC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		float AimYawRateC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Cached Variables", meta = (AllowPrivateAccess = "True"))
		float PreviousAimYawC = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Cached Variables", meta = (AllowPrivateAccess = "True"))
		FGameplayAbilitySpecHandle AbilityHandle;

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		UCurveFloat* StrafeSpeedMapCurveC = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Movement System", meta = (AllowPrivateAccess = "True"))
		FMovementSettingsStrafe CurrentMovementSettingsC;

	UPROPERTY(BlueprintReadWrite, Category = "Movement System", meta = (AllowPrivateAccess = "True"))
		FVector FloorVelocityC = FVector(0,0,0);

	UPROPERTY(BlueprintReadWrite, Category = "Movement System", meta = (AllowPrivateAccess = "True"))
		FVector PrevFloorVelocityC = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "Essential Information", meta = (AllowPrivateAccess = "True"))
		bool OverlayStateLeavingStarted = false;

		bool CanUpdateFromDesiredOverlay = false;

	/* Experimental function. Improves the behavior of the capsule in a non-inertial reference frame (the floor moves relative to the world space) */
	UPROPERTY(BlueprintReadWrite, Category = "Config", meta = (AllowPrivateAccess = "True"))
		bool CorrectNonInertialFloor = true;

		FVector PrevFloorLocation = FVector(0, 0, 0);
		bool AddedFloorForce = false;

	UFUNCTION(BlueprintPure, Category = "Movement System", meta = (DisplayName = "Get Target Speed With Strafe", Keywords = "Movement"))
		virtual float GetTargetSpeedWithStrafeC(FVector SpeedVector);

	UFUNCTION(BlueprintPure, Category = "Movement System", meta = (DisplayName = "Get Mapped Speed", Keywords = "Movement"))
		virtual float GetMappedSpeedC(float SpeedScale = 1.0);

	UFUNCTION(BlueprintCallable, Category = "Rotation System", meta = (DisplayName = "Smooth Character Rotation", Keywords = "Rotation"))
		virtual void SmoothCharacterRotationC(FRotator TargetRotation = FRotator(0,0,0), float ActorInterpSpeed = 10.0);

	UFUNCTION(BlueprintPure, Category = "Rotation System", meta = (DisplayName = "Calculate Grounded Rotation Speed", Keywords = "Rotation"))
		virtual float CalculateGroundedRotationSpeedC(float Scale = 1.0, FVector2D YawScaleRange = FVector2D(1.0,3.0));

	UFUNCTION(BlueprintPure, Category = "Utility", meta = (DisplayName = "Get Control Vectors", Keywords = "Others"))
		virtual void GetControlVectorsC(FVector& ForwardVector, FVector& RightVector);

	UFUNCTION(BlueprintPure, Category = "Utility", meta = (DisplayName = "Get Capsule Base Location", Keywords = "Others"))
		virtual FVector GetCapsuleBaseLocationC(float ZOffset);

	UFUNCTION(BlueprintPure, Category = "Utility", meta = (DisplayName = "Floor To Capsule Location", Keywords = "Others"))
		virtual FVector FloorToCapsuleLocationC(FVector BaseLocation, float ZOffset, bool ByDefSize);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Raid", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UUserWidget> RaidCompassWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidCompass.WBP_RaidCompass_C")));

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI|Raid", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UUserWidget> RaidCompassWidgetInstance = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Safety", meta = (AllowPrivateAccess = "true"))
	bool bDisableZombieGrabAbilities = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Safety", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0", EditCondition = "bDisableZombieGrabAbilities"))
	float ZombieGrabAbilityCancelInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true"))
	bool bAutoLockOnWhenMeleeSlotEquipped = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0", EditCondition = "bAutoLockOnWhenMeleeSlotEquipped"))
	int32 MeleeLockOnSlotIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true"))
	FName CombatFuryEnemyTag = TEXT("Enemy");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true"))
	bool bPreferLineOfSightLockOnTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CombatFuryLockOnMaxAcquireDistance = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true"))
	bool bPrioritizeRecentDamageInstigator = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", EditCondition = "bPrioritizeRecentDamageInstigator"))
	float CombatFuryRecentDamageMemorySeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true"))
	bool bEnableMiddleMouseLockOnFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	bool bMiddleMouseLockOnRequiresMeleeSlot = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	bool bAllowMouseWheelSwitchInSoftLock = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float MouseWheelAxisTriggerThreshold = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "0.5", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float MouseWheelTargetSwitchCooldownSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn", meta = (AllowPrivateAccess = "true", ClampMin = "0.05", ClampMax = "0.5", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float MiddleMouseToggleCooldownSeconds = 0.18f;

	// 하드락 상태에서 타겟 전환을 트리거하는 단일 프레임 마우스 X 이동 임계값 (마우스 스크롤 대체).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn",
		meta = (AllowPrivateAccess = "true", ClampMin = "1.0", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float NativeMouseFlickThreshold = 10.0f;

	// 마우스 플릭 타겟 전환 연속 스위칭 방지 쿨다운 (초).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.05", ClampMax = "0.5", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float NativeMouseFlickCooldownSeconds = 0.25f;

	// 방향성 잠금 지속 시간 (초). 쿨다운보다 길게 설정하면 반대 방향 Back-dash 입력을 추가로 차단.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float NativeFlickDirectionalLockSeconds = 0.35f;

	// 플릭 에너지 누적 윈도우 (초). 이 시간 내 MouseX 합산값이 임계값 초과 시 전환 발동 (단일 프레임 오감지 방지).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Fury|LockOn",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.05", ClampMax = "0.5", EditCondition = "bEnableMiddleMouseLockOnFallback"))
	float NativeMouseFlickWindowSeconds = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native", meta = (AllowPrivateAccess = "true"))
	bool bEnableNativeSoftTargeting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native", meta = (AllowPrivateAccess = "true"))
	bool bNativeSoftTargetRequiresMeleeContext = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native", meta = (AllowPrivateAccess = "true", ClampMin = "0.02", ClampMax = "1.0"))
	float NativeSoftTargetRefreshInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeSoftTargetMaxDistance = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeSoftTargetDistanceWeight = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeSoftTargetScreenCenterWeight = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeSoftTargetLineOfSightWeight = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", EditCondition = "bPrioritizeRecentDamageInstigator"))
	float NativeSoftTargetRecentDamageWeight = 1.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "-1.0", ClampMax = "0.99"))
	float NativeSoftTargetMinViewDot = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true"))
	bool bEnableNativeSoftTargetAttackCorrection = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true", EditCondition = "bEnableNativeSoftTargetAttackCorrection"))
	bool bNativeSoftTargetPinDuringAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true", EditCondition = "bEnableNativeSoftTargetAttackCorrection"))
	bool bNativeSoftTargetForceFacingDuringAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bEnableNativeSoftTargetAttackCorrection && bNativeSoftTargetForceFacingDuringAttack"))
	float NativeSoftTargetFacingInterpSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "60.0", EditCondition = "bEnableNativeSoftTargetAttackCorrection && bNativeSoftTargetForceFacingDuringAttack"))
	float NativeSoftTargetControlYawInterpSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "0.5", EditCondition = "bEnableNativeSoftTargetAttackCorrection"))
	float NativeSoftTargetAttackCorrectionGraceSeconds = 0.10f;

	// 공격 시 이동 방향 기준 스마트 자석 타겟팅 원뿔 반각 (도). 0이면 비활성. God of War 스타일.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Attack Correction",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "90.0",
				EditCondition = "bEnableNativeSoftTargetAttackCorrection"))
	float NativeSoftMagneticConeHalfAngle = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "80.0"))
	float NativeHardLockFacingInterpSpeed = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "80.0"))
	float NativeHardLockControlRotationInterpSpeed = 9.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float NativeHardLockYawWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float NativeHardLockPitchWeight = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "-89.0", ClampMax = "0.0"))
	float NativeHardLockMinPitch = -25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "89.0"))
	float NativeHardLockMaxPitch = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "150.0"))
	float NativeHardLockCameraTargetVerticalOffset = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "800.0"))
	float NativeHardLockMinDistance = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float NativeHardLockFocusBlend = 0.65f;

	// 하드락 전환 전용 반경 (15m). 0이면 소프트락 획득 거리 사용.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeHardLockSwitchMaxDistance = 1500.0f;

	// 카메라-상대 방향 전환 (플릭 오른쪽=화면 우측 적). Elden Ring 스타일.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true"))
	bool bUseCameraRelativeTargetSwitching = true;

	// ── Camera ──────────────────────────────────────────────────────────────
	// 락온 중 카메라 충돌 감지 시 SpringArm 팔 단축 보간 속도 (빠를수록 즉각 반응).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "1.0", ClampMax = "30.0"))
	float NativeCameraCollisionInterpSpeed = 12.0f;

	// 충돌 해소 후 SpringArm 원래 길이로 복원하는 보간 속도 (느릴수록 부드러운 복귀).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "1.0", ClampMax = "20.0"))
	float NativeCameraRestoreInterpSpeed = 4.0f;

	// 하드락 타겟 전환 시 카메라를 새 타겟 방향으로 보간하는 속도. 0이면 비활성.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "30.0"))
	float NativeCameraTransitionSpeed = 8.0f;

	// SpringArm 팔 길이가 이 값 미만으로 단축되면 캐릭터 메쉬 Dither Alpha를 적용하여 시야를 확보
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeCameraDitherArmThreshold = 120.0f;

	// Dither 상태에서의 최소 불투명도 (0 = 완전 투명, 1 = 불투명). 0.3이면 반투명 실루엣.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float NativeCameraDitherMinOpacity = 0.3f;

	// 짐벌락 방지 최소 거리 (UU). 타겟과의 거리 또는 XY 평면 거리가 이 값 미만이면 카메라 전환 보간을 중단.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Camera",
		meta = (AllowPrivateAccess = "true", ClampMin = "10.0", ClampMax = "200.0"))
	float NativeCameraGimbalLockMinDist = 50.0f;

	// 하드락 이탈 히스테리시스 배율. 획득 거리 × 이 값에서 해제됨 (1.5 = 4500 UU). Elden Ring 스타일.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Hard Lock", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", ClampMax = "3.0"))
	float NativeHardLockBreakDistanceMultiplier = 1.5f;

	// 현재 공격 중인 적에 대한 소프트락 점수 가산 가중치 (God of War 스타일 위협 우선순위).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|Scoring", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeSoftTargetThreatWeight = 0.30f;

	// 현재 락온 대상의 뷰포트 내 픽셀 좌표. UMG 오프스크린 화살표에 활용 (Ghost of Tsushima 스타일).
	UPROPERTY(Transient, BlueprintReadOnly, Category = "LockOn|Native|UI", meta = (AllowPrivateAccess = "true"))
	FVector2D NativeTargetScreenPosition = FVector2D::ZeroVector;

	// 현재 락온 대상이 뷰포트 안에 있으면 true. false이면 NativeTargetScreenPosition 방향으로 화살표 표시.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "LockOn|Native|UI", meta = (AllowPrivateAccess = "true"))
	bool bNativeTargetIsOnScreen = false;

	// 현재 타겟의 정규화 점수 (0~1). UMG 위젯 노출 우선순위 표시용.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "LockOn|Native|UI", meta = (AllowPrivateAccess = "true"))
	mutable float NativeCurrentTargetScore = 0.0f;

	// 점수 상위 최대 3개 후보 배열 (0=현재 타겟, 1·2=차순위). UMG에서 다수 아이콘 표시 구현 시 활용.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "LockOn|Native|UI", meta = (AllowPrivateAccess = "true"))
	mutable TArray<TObjectPtr<AActor>> NativeLockOnPriorityCandidates;

	// 이 거리 이상의 비우선순위 적은 위젯 틱을 비활성화하여 UI 비용을 낮춤 (0 = 비활성).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Native|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float NativeLockOnWidgetTickMaxDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Presentation", meta = (AllowPrivateAccess = "true"))
	bool bShowNativeLockOnModeFeedback = true;



protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void EnsureRaidCompassWidget();
	void EnforceNoZombieGrabAbilities();

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		class UAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(Transient)
	float ZombieGrabAbilityCancelElapsed = 0.0f;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Abilities")
		UIWALS_GameplayAbilitySet* AbilitiesData;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Abilities")
		TSubclassOf<class UGameplayEffect> DefaultAttributeEffect;

	UPROPERTY()
		class UIWALS_BaseAttributeSet* Attributes;

	// Gameplay Tag Functions
	// -----------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Convert Literal Name To Tag", Keywords = "Gameplay Tag"))
		virtual FGameplayTag ConvertLiteralNameToTag(FName TagName);

	UFUNCTION(BlueprintPure, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Get Sub Tag", Keywords = "Gameplay,Tag"))
		virtual FString GetSubTag(const FGameplayTag& Tag, int32 DesiredDepth);

	UFUNCTION(BlueprintPure, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Is Tag Leaf", Keywords = "Gameplay,Tag"))
		virtual bool IsTagLeaf(const FGameplayTag& Tag);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Switch On Owned Tags", Keywords = "Gameplay,Tag"))
		virtual bool SwitchOnOwnedTags(const FGameplayTag& NewState);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Switch On Owned Tags With Ignore", Keywords = "Gameplay,Tag"))
		virtual bool SwitchOnOwnedTagsWithIgnore(const FGameplayTag& NewState, const FGameplayTagContainer& DoNotEdit);

	UFUNCTION(BlueprintPure, Category = "Gameplay Tags|Tag Container|Custom", meta = (DisplayName = "Filter Tags By Root Group", Keywords = "Gameplay,Tag"))
		virtual void FilterTagsByRootGroup(const FGameplayTagContainer& Input, FGameplayTag RootTag, bool StopWhenFirstValid, FGameplayTagContainer& ReturnContainer);




	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual class UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	virtual void InitializeAttributes();
	virtual void GiveAbilities();

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	UFUNCTION(BlueprintCallable, Category = "Pawn|Input", meta = (DisplayName = "Try Create Inputs Binds For GAS", Keywords = "Inputs Player"))
		virtual void TryCreateInputsGAS();

	UFUNCTION(BlueprintPure, Category = "LockOn", meta = (DisplayName = "Get Nearest Enemy", Keywords = "LockOn Enemy"))
	AActor* GetNearestCombatFuryEnemy(float MaxDistance = 0.0f) const;


	// For Overlay System
	UFUNCTION(BlueprintImplementableEvent)
		void DoWhenOverlayLeaving(float DeltaSecond);

	UFUNCTION(BlueprintImplementableEvent)
		void OverlayLeavingFinshed();

	friend struct FLockOnSweepTickFunction;

private:
	bool HasLineOfSightToLockOnCandidate(const AActor* Candidate) const;
	void RecordRecentDamageInstigator(class AController* EventInstigator, AActor* DamageCauser, float AppliedDamage);
	void OnMouseWheelAxisInput(float Value);
	UWidgetComponent* FindLockOnWidget(AActor* TargetActor) const;
	void HideTargetIconForActor(AActor* TargetActor);
	void ShowTargetIconForActor(AActor* TargetActor, bool bHardLock);
	void HideAllTargetIcons();
	void UpdateTargetIconVisibility();
	void SweepNonTargetIcons();
	bool IsMeleeLockOnSlotEquipped() const;
	void HandleMiddleMouseLockOnFallback();
	void BuildCombatFuryLockOnCandidates(TArray<AActor*>& OutCandidates, float MaxDistanceOverride = 0.0f) const;
	float ResolveEffectiveLockOnMaxDistance(float MaxDistanceOverride = 0.0f) const;
	bool SetNativeCombatFuryLockOnTarget(AActor* NewTarget, bool bTreatAsSwitch);
	void ClearNativeCombatFuryLockOnTarget();
	bool SetCurrentNativeTarget(AActor* NewTarget, bool bHardLock, bool bTreatAsSwitch, const FString& Reason);
	void ClearCurrentNativeTarget(const FString& Reason);
	bool ValidateCurrentNativeTarget(const FString& ContextReason, bool bFromHardLock);
	bool SwitchNativeTargetByDirection(int32 Direction, FString& OutReason);
	AActor* FindSmartMagneticTarget(const TArray<AActor*>& Candidates) const;
	AActor* GetCurrentNativeTarget() const;
	USpringArmComponent* ResolveLockOnSpringArm() const;
	UCameraComponent* ResolveLockOnCamera() const;
	void StoreDefaultCameraValues();
	void ResetHardLockCamera();
	bool IsLockOnTargetUsable(AActor* Candidate, bool bApplyHardLockHysteresis = false) const;
	bool IsMeleeLockOnContextActive() const;
	void UpdateNativeSoftTargeting(float DeltaTime);
	void UpdateLockOnSpringArmCollision(float DeltaTime);
	void UpdateLockOnCameraTransition(float DeltaTime);
	bool ShouldApplyNativeSoftAttackCorrection(float DeltaTime);
	void ApplyNativeSoftAttackFacingCorrection(AActor* Target, float DeltaTime);
	void ApplyNativeHardLockFacingCorrection(AActor* Target, float DeltaTime);

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastCombatFuryRecentDamageInstigator = nullptr;

	UPROPERTY(Transient)
	float LastCombatFuryRecentDamageInstigatorTime = -1.0f;

	bool bWasInActionMeleeTag = false;
	bool bWasMeleeLockOnSlotEquipped = false;
	mutable bool bLastMeleeLockOnDetectionValid = false;
	mutable bool bCachedMeleeContextThisTick = false;
	float ZombieGrabEnforceTimer = 0.0f;
	mutable bool bMeleeContextCacheValid = false;
	bool bLastKnownMeleeContextActive = false;
	bool bWasMiddleMousePressed = false;
	float PreviousMouseWheelAxisValue = 0.0f;
	float LastMouseWheelSwitchTimeSeconds = -1000.0f;
	float LastMouseFlickSwitchTimeSeconds = -1000.0f;
	float LastMiddleMouseToggleTimeSeconds = -1000.0f;
	bool bLockOnCameraTransitionActive = false;
	float LockOnCameraTransitionElapsed = 0.0f;
	int32 LastFlickLockedDirection = 0;
	float CurrentMeshDitherAlpha = 1.0f;
	float NativeMouseFlickAccumulator = 0.0f;  // 누적 플릭 에너지 (윈도우 내 MouseX 합산)
	float NativeMouseFlickWindowElapsed = 0.0f; // 현재 윈도우 경과 시간
	float NativeSoftTargetElapsed = 0.0f;
	float NativeSoftAttackCorrectionRemaining = 0.0f;
	bool bWasNativeSoftAttackTagActive = false;
	bool bNativeHardLockActive = false;

	FLockOnSweepTickFunction LockOnSweepTick;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> NativeCombatFuryLockOnTarget = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> NativeCombatFurySoftTarget = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWidgetComponent> ActiveCombatFuryTargetWidget = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ActiveCombatFuryTargetActor = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PreviousCombatFuryTargetActor = nullptr;

	UPROPERTY(Transient)
	mutable TArray<TWeakObjectPtr<AActor>> LastCandidateTargets;

	UPROPERTY(Transient)
	mutable TWeakObjectPtr<USpringArmComponent> CachedLockOnSpringArm = nullptr;

	UPROPERTY(Transient)
	mutable TWeakObjectPtr<UCameraComponent> CachedLockOnCamera = nullptr;

	UPROPERTY(Transient)
	bool bHasStoredHardLockCameraDefaults = false;

	UPROPERTY(Transient)
	float DefaultSpringArmLength = 0.0f;

	UPROPERTY(Transient)
	FVector DefaultSpringArmSocketOffset = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector DefaultSpringArmTargetOffset = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector DefaultCameraRelativeLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator DefaultCameraRelativeRotation = FRotator::ZeroRotator;
};
