

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "ALS_HumanAI_InterfaceCpp.h"
#include "AGLS_AI_CharacterInterface.h"
#include "SimpleMovementParamsData.h"
#include "ALS_DamageConfigData.h"
#include "AsyncFunctionsPlayer.h"
#include "AGLS_ZombieAttacksComponentCore.h"
#include "AGLS_ZombieCharacterCore.generated.h"

/*Structure mainly intended for advanced Physic Animation setup*/
USTRUCT(BlueprintType)
struct FRuntimePhysicAnimConfig : public FTableRowBase
{
	GENERATED_BODY()

/*ENG: This variable defines from which bone changes will be made in the physics simulation via Physic Asset. This name will usually be passed 
to the SetAllBodiesBelowSimulatePhysic() function

PL: Zmienna ta określa od jakiej kości będą wprowadzane zmiany w symulacji fizyki za pośrednictwem Physic Asset. Nazwa ta zazwyczaj będzie 
przekazywana do funkcji SetAllBodiesBelowSimulatePhysic()*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physic")
	FName SimulationInitBodyName = TEXT("Spine_01");

/*ENG: Influences whether when calling a function changing the physics simulation state for individual bodies, the name defined in 
SimulationInitBodyName should also be included in the change. For example, if SimulationInitBodyName = "Spine_01" then should we 
include the Bodies associated with this bone or not

PL: Wpływa na to czy podczas wywoływania funkcji zmieniającej stan symululowania fizyki dla poszczególnych bodies, do zmiany powinno 
się uwzględnić też nazwę zdefiniowaną w SimulationInitBodyName. Przykładowo jeżeli SimulationInitBodyName = "Spine_01" to czy powinniśmy 
uwzględnić Bodies powiązane z tą kością, cz też nie*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physic")
	bool bIncludeSelf = true;

/*ENG: Additional PhysicAsset, which is set when, for example, the Ragdoll() function is called in the character. If this variable != null, 
then the basic Physic Asset contained in the Mesh will be overwritten. After disabling Ragdoll mode, the asset will be released. It can be 
left empty (in fact, it is NOT recommended)

PL: Dodatkowy PhysicAsset, który jest ustawiany w momencie kiedy w charakterze dojdzie do wywołania np. funkcji Ragdoll(). Jeżeli zmienna 
ta != null to wtedy dojdzie do nadpisania podstawowego Physic Asset zawartego w Mesh. Po wyłączeniu trybu Ragdoll zasób zostanie zwolniony. 
Można pozostawić puste (wręcz jest to zalecane)*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physic")
	UPhysicsAsset* OverrideWhenRagdoll = nullptr;

/*ENG: An additional variable that allows you to 'skip' the enabling of physics simulation for bodies associated with declared bone names. 
For example, if we call SetAllBodiesBelowSimulatePhysic() for the Spine_01 bone, then all bones that are higher in the hierarchy will 
start to simulate physics. By entering e.g. 'hand_l' and 'hand_r' into the BodiesNameToOverride variable, we will make these bones excluded 
from the simulation. Useful when configuring Physic Animation

PL: Dodatkowa zmienna pozwalająca na 'ominięcie' włączenia symulacji fizyki dla bodies powiązanych z zadeklorowanymi nazwami kości. 
Przykłądowo jeżeli wywołamy SetAllBodiesBelowSimulatePhysic() dla kości Spine_01 to w takim przypadku wszystkie kości, które sa wyżej w 
hierarchii zaczną symulować fizykę. Wprowadzając do zmiennej BodiesNameToOverride np. 'hand_l' oraz 'hand_r' sprawimy że te kości zostaną 
wyłączone z symulacji. Pomocne przy konfiguracji Physic Aniamtion*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physic")
	TArray<FName> BodiesNameToOverride = {};

/*ENG: Important array containing names of all bones from SkeletalMesh that have assigned PhysicBodies. This is important when Character 
goes to low level of detail e.g. LOD_02 or LOD_03. For these states physics simulation should be disabled, however in this case information 
about currently simulating Bodies would be lost. Therefore it is required to manually enter all bone names. Without such solution it would 
be required to iterate through all bones in SkeletalMesh which is not very efficient approach.

PL: Ważna tabilca zawierająca nazwy wszystkich kości ze SkeletalMesh, które mają przypisane PhysicBodies. Jest to istotne w momencie kiedy 
Character przechodzi w niski poziom detail np. LOD_02  lub LOD_03. Dla tych stanów symulacja fizyki powinna zostać wyłączona, jednak w takim 
przypadku informacja o aktualnie symulujących Bodies została by utracona. Dlatego wymagane jest aby  wprowadzić ręcznie wszystkie nazwy kości. 
Bez takiego rozwiązania wymagane było by iterowanie przez wszystkie kości w SkeletalMesh co nie jest dość wydajnym podejściem.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physic")
	TArray<FName> AllCollisionBodiesInPS = {};

};

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ECharacterDetailModeFlags : uint8
{
	LOD0 = 1 << 0,
	LOD1 = 1 << 1,
	LOD2 = 1 << 2,
	LOD3 = 1 << 3,
};
ENUM_CLASS_FLAGS(ECharacterDetailModeFlags)


/*Base C++ class prepared for constructing the Zombie character. Contains many variable declarations related to further implementation and 
modifications via child classes. Through this class character has have implemented dynamic variables for CharacterMovementComponent such as:
- float Acceleration (from Curve Vector.X)
- float Deceleration (from Curve Vector.Y)
- float GroundFriction (from Curve Vector.Z)
- float MaxWalkingSpeed (From Vector where X=Forward, Y=Left/Right, Z=BackDirection)
These values ​​are set depending on the DataAsset configuration, which was specified for the USimpleMovementParamsData* MovementParamsData variable

This class also implements the capsule rotation interpolation system, ragdoll, and many other functions for constructing the final character. 
Many functions declared in C++ have an override option via Blueprint, which allows for more free modification in the child class.*/
UCLASS()
class HELPFULFUNCTIONS_API AAGLS_ZombieCharacterCore : public ACharacter, public IALS_HumanAI_InterfaceCpp, public IAGLS_AI_CharacterInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AAGLS_ZombieCharacterCore();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UAGLS_ZombieAttacksComponentCore* AttacksSystemComponent;


//Stores the Velocity value from the previous frame (n-1)
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FVector PrevVelocity = FVector::ZeroVector;

/*A variable that stores the Velocity value of the ground the character is currently on. This is needed when we use velocity 
correction in a non-inertial reference frame.*/
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FVector FloorVelocityC = FVector::ZeroVector;

/*Stores the manually calculated acceleration of the capsule by calculating the simplified derivative of velocity 
[Velocity(n) - (Velocity(n - 1) / dt]*/
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FVector AccelerationC = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential", DisplayName = "Aiming Rotation"))
	FRotator AimingRotationC = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FRotator DesiredActorRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential", DisplayName = "In Air Rotation"))
	FRotator InAirRotationC = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential", DisplayName = "Last Movement Input Rotation"))
	FRotator LastMovementInputRotationC = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FVector LastNonZeroVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FVector LastNonZeroAcceleratrion = FVector(10,0,0);

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	FCALSMovementSettingsStrafe CurrentMovementSettings;

// SpeedC = CurrentVelocity.Length()
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential", DisplayName = "Speed"))
	float SpeedC = 0.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential", DisplayName = "Speed"))
	float AimYawRotationRateC = 0.0;

// IsMoving = TRUE when SpeedC > 1.0
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	bool bIsMoving = false;

/*float MovementInputAmount = AccelerationXY.Length() / this->GetCharacterMovement()->GetMaxAcceleration();
if (MovementInputAmount > 0.0) { bHasMovementInput = true; }
else { bHasMovementInput = false; }*/
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	bool bHasMovementInput = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Essential"))
	bool IsDeathC = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Defaults"))
	float DefCapsuleRadius = 25.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Defaults"))
	float DefCapsuleHeight = 90.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Ragdoll"))
	bool bRagdollOnGround = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Ragdoll"))
	bool bRagdollFaceUp = false;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Ragdoll"))
	FVector LastRagdollVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Ragdoll"))
	float DeathTime = 0.0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Ragdoll"))
	UAnimMontage* RagdollGetUpAnim = nullptr;


	int LocomotionStateIndex = 0;
	float ConstSpeedScale = 1.0;
	bool bLockRotationUpdate = false;
	float MovementSpeedDifferenceC = 0.0;
	FVector PreviousVelocity = FVector::ZeroVector;
	FRotator LastVelocityRotation = FRotator::ZeroRotator;
	// Non inertial ground correction
	FVector PrevFloorLocation = FVector::ZeroVector;
	FVector PrevFloorVelocity = FVector::ZeroVector;
	bool AddedFloorForce = false;

	UFUNCTION(BlueprintPure, Category = "Zombie Core|Utility", meta = (Keywords = "Zombie,Curve"))
	float GetAnimCurveValue(FName CurveName = TEXT("BasePose_N"));

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	UAsyncFunctionsPlayer* AsyncFunctionsComponent;

//IMPORTANT VARIABLE: If CurrentHealth < 0 OR CurrentHealth > MaxHealth THEN on BeginPlay CurrentHealth = MaxHealth
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Damage", ExposeOnSpawn = true))
	float CurrentHealthPoints = 1000.0;

/*DataAsset from which information about CharacterMovementComponent parameters will be retrieved during game play. 
Cannot be left EMPTY.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Movement"))
	USimpleMovementParamsData* MovementParamsData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Damage"))
	TSubclassOf<UALS_DamageConfigData> HealthAndDamageConfig = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Movement"))
	UCurveFloat* StrafeSpeedMapCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Movement"))
	bool CorrectNonInertialFloor = false;

	// Set Actor Rotation as Control Rotation when is NOT Moving
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Rotation"))
	bool SetAsControlRotationWhenIdle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Movement", ExposeOnSpawn = true))
	FName DefMovementModelName = TEXT("Normal");

/*ENG:
You can enter a range from which a random value will be selected to multiply MaxWalkingSpeed. This allows you to 
introduce more variety with more instances, as they will move at different maximum speeds.

PL:
Można wprowadzić przedział z którego będzie wybrana przypadkowa wartość mnożąca MaxWalkingSpeed. Dzięki temu można wprowadzić 
większą różnorodność przy większej ilości instancji, gdyż będą poruszać się z innymi maksymalnymi prędkościami.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Movement", ClampMin = "0.5", ClampMax = "1.5", ExposeOnSpawn = true));
	FVector2D InitMovementRandomScale = FVector2D(1.0, 1.0);

/*Value that affects the fact that when dealing damage, Skeletal Mesh can be dismembered. This is directly related to physics simulation and the use of Physic Animation. 
Disconnection of PhysicsBodies parts is done by breaking the Physic Constraint. Note: This function works correctly only on a well-prepared skeletal mesh. 
Additionally, it is associated with a performance cost. It should not be used with a very large number of Character instances.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Physics"))
	bool UseBodyDismemberment = false;

/*If 'UseBodyDismemberment' is used, then we can only apply physics simulation when the first damage is taken. 
Otherwise, the simulation will be updated whenever the character is in LOD0 or LOD1.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Physics", EditCondition = "UseBodyDismemberment"))
	bool StartSimWhenDetectDamage = false;

/*Reference value defining the base PhysicAnimationStrength value for the PhysicAnimation component*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Physics", ExposeOnSpawn = true, ClampMin = "0.0", ClampMax = "1.0", EditCondition = "UseBodyDismemberment"))
	float DefaultPhysicAnimStrength = 1.0;

/*ENG:
Important variable when UseBodyDismemberment is set to true. In this part it is necessary to define which bones will have a separate health points. 
For example, if 'hand_l' = 1.0, then when damage is registered, additionally information about the hit bone will be taken from HitResult. Then an attempt 
will be made to find it in BodiesHealth and finally reduce the value of health points. These values ​​are also used to calculate the so-called 'Limb_Health' 
which determines whether the limb should be detached from the skeleton when this value is less than 0.01

PL:
Ważna zmienna w przypadku kiedy UseBodyDismemberment jest ustawiana na true. W tej części konieczne jest zdefiniowanie jakie kości będą posiadały oddzielny 
pasek zdrowia. Przykładowo jeżeli 'hand_l' = 1.0, to w momencie kiedy zostaną zajerestrowane obrażenia, to dodatkowo z HitResult zostanie pobrana informacja 
o trafinej kości. Nastepnie zajdzie próba ndnalezienia jej w BodiesHealth a finalnie zmiejszenie wartości punktów zdrowia. Wartości te są również używane do 
wyliczenia tak zwanego 'Limb_Health' który określa czy kończyna powinna zostać odczepiona od szkeletu w momencie kiedy wartość ta będzie mniejsza od 0.01*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Physics", ClampMin = "0.0", ClampMax = "2.0", EditCondition = "UseBodyDismemberment"))
	TMap<FName, float> BodiesHealth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Physics", ExposeOnSpawn = true, EditCondition = "UseBodyDismemberment"))
	FRuntimePhysicAnimConfig PhysicAnimConfigDetails;

/*ENG:
Determine which asset of type 'Deformer' SkeletalMesh should use. Note: introducing a reference to the asset for this variable, and not defining it manually 
in the skeletal mesh component, will cause longer initialization time of the Zombie instance on the scene. Otherwise, pointing the resource directly in the 
component will significantly affect the compilation time of the Blueprint.
WARNING: Using Deformer Graph is expensive in the case of many instances of this class.

PL:
Określenie z jakiego zasobu typu 'Deformer' powinien kożystać SkeletalMesh. Uwaga: wprowadzenie odnieszienia do zasobu dla tej zmiennej, a nie zdefiniowanie 
go ręcznie w skeletal mesh component będzie powodować dłóższy czas inicjacji instancji Zombie na mapie. W innym przypadku wskazanie zasobu bezpośrednio w 
komponencie wpłynie znacząco na czas kompilacji Blueprint.
UWAGA: Użycie Deformer Graph jest kosztowne w przypadku wielu instancji tej klasy.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|Deformer"))
	UMeshDeformer* MeshDeformerClass = nullptr;

	UPROPERTY(EditDefaultsOnly, meta = (Bitmask, BitmaskEnum = ECharacterDetailModeFlags, Category = "Zombie Core|Deformer"))
	int32 DeformerActivationState = 0;
	UFUNCTION(BlueprintPure, Category = "Zombie|Deformer", meta = (Keywords = "Zombie,Character,Deformer"))
	bool GetDeformerActivationLOD(AGLS_LOD_State CurrentState);

/*ENG:
Montage animations to be activated when a character registers a fall from a great height. They can be left empty because 
Landing Animations are implemented in Motion Matching

PL:
Animacje montage jakie mają być aktywowane w momencie kiedy charakter zarejestruje updadek z dużej wysokości. 
Mogą pozostać puste, ponieważ Landing Animations są zaimplementowane w Motion Matching*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zombie Core|Anim Instance|Landing", meta = (AllowPrivateAccess = "True"))
	TSoftObjectPtr<UAnimMontage> LightLandMontage = nullptr;

/*ENG:
Montage animations to be activated when a character registers a fall from a great height. They can be left empty because
Landing Animations are implemented in Motion Matching

PL:
Animacje montage jakie mają być aktywowane w momencie kiedy charakter zarejestruje updadek z dużej wysokości.
Mogą pozostać puste, ponieważ Landing Animations są zaimplementowane w Motion Matching*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zombie Core|Anim Instance|Landing", meta = (AllowPrivateAccess = "True"))
	TSoftObjectPtr<UAnimMontage> HardLandMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zombie Core|Anim Instance|Posture", meta = (AllowPrivateAccess = "True", ClampMin = "0", ClampMax = "15", ExposeOnSpawn = true))
	int AddtivePostureIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zombie Core|Anim Instance|Posture", meta = (AllowPrivateAccess = "True", ClampMin = "0.0", ClampMax = "1.0", ExposeOnSpawn = true))
	float AddtivePostureStrength = 0.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	CALS_Gait CurrentGait = CALS_Gait::Walking;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	CALS_Gait DesiredGait = CALS_Gait::Walking;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States", DisplayName = "MovementStateC"))
	CALS_MovementState MovementStateC = CALS_MovementState::Grounded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States", DisplayName = "Rotation Mode"))
	CALS_RotationMode RotationModeC = CALS_RotationMode::VelocityDirection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	CALS_GroundedMoveMode GroundedMovementMode = CALS_GroundedMoveMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	CALS_Stance CurrentStance = CALS_Stance::Standing;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	CALS_DeathType DeathState = CALS_DeathType::KilledByGun;
	
	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States", DisplayName = "LOD State"))
	AGLS_LOD_State LOD_StateC = AGLS_LOD_State::LOD0;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Zombie Core|States"))
	FGameplayTagContainer TagsContainerData;


	CALS_RotationMode PreviousRotationMode = CALS_RotationMode::VelocityDirection;
	CALS_MovementState PreviousMovementState = CALS_MovementState::Grounded;
	CALS_MovementAction CurrentMovementAction = CALS_MovementAction::None;


	// ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ FUNCTIONS SECTION ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀

	virtual void OnCharacterMoveChanged(EMovementMode PrevMode, EMovementMode NewMode);
	virtual void OnMovementStateChanged(CALS_MovementState NewAction);
	virtual void OnRotationModeChanged(CALS_RotationMode NewMode);
	virtual void OnGaitChanged(CALS_Gait NewMode);
	virtual void OnGroundedModeChanged(CALS_GroundedMoveMode NewMode, bool bUpdateMoveParams);
	virtual void OnStanceChanged(CALS_Stance NewState);
	virtual void OnDetailsLevelChanged(AGLS_LOD_State NewState);

	virtual bool UpdateMovementSettings(USimpleMovementParamsData* ParamsAsset = nullptr, FName KeyName = TEXT("Normal"));
	virtual void UpdateEssentialValues();

	UFUNCTION(BlueprintCallable, Category = "Zombie Core|Movement", meta = (Keywords = "Zombie,Movement"))
	virtual void UpdateCharacterMovementPerFrame(CALS_Gait AllowedGait);

	float GetTargetSpeedWithStrafe(FVector SpeedVector);

	UFUNCTION(BlueprintPure, Category = "Zombie Core|Movement", meta = (Keywords = "Zombie,Movement"))
	float GetMappedSpeedC(float SpeedScale = 1.0);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Zombie|Movement")
	CALS_Gait GetAllowedGait();
	virtual CALS_Gait GetAllowedGait_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Zombie|Movement")
	CALS_Gait GetActualGait(CALS_Gait AllowedGait);
	virtual CALS_Gait GetActualGait_Implementation(CALS_Gait AllowedGait);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Movement", meta = (ForceAsFunction))
	void UpdateGroundedMovement();
	virtual void UpdateGroundedMovement_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Movement", meta = (ForceAsFunction))
	void OnStartedCrawling();
	virtual void OnStartedCrawling_Implementation();



	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Zombie|Optymalization", meta = (ForceAsFunction))
	AGLS_LOD_State GetCurrentRequiredLOD();
	virtual AGLS_LOD_State GetCurrentRequiredLOD_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Optymalization", meta = (ForceAsFunction))
	void UpdateCurrentLOD_State();
	virtual void UpdateCurrentLOD_State_Implementation();


	// ▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒
	// ▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒ Physic [S] [E] [C] [T] [I] [O] [N]  ░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒
	// ▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒░░░▒

	//Update Current Bodies Physic Stats. Funkcja potrzebna do wywolania w momencie kiedy zmieniamy jakies parametry dla physic asset. Wynika to z faktu ze podczas zmiany LOD konieczne jest zapamietanie
	//ktore z Bodies byly aktywne a ktore nie.
	UFUNCTION(BlueprintCallable, Category = "Zombie Core|Physic", meta = (Keywords = "Zombie,Physic"))
	bool UpdateCurrentBodiesPhysicStats();
	TArray<FName> BodiesWithActivatedPhysic;

	//Prosta funkcja ktora aktywuje i wylacza posczagolne Bodies dla Physic Asset. Uwaga nie dziala to tak samo jak wywolanie np. EnableAllBodiesPhysic. Innformacja o tym ktore Bodies nalezy aktowowac
	//a ktore nie pobierana jest ze zmiennej TArray<FName>. Zmiany w jej wartosciach powoduje wywolanie funkcji UpdateCurrentBodiesPhysicStats()
	UFUNCTION(BlueprintCallable, Category = "Zombie Core|Physic", meta = (Keywords = "Zombie,Physic"))
	bool DisableEnableRequiredBodies(bool bEnableSimulation);

	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ Update Actor Rotation for property movement state - Section ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░

	//Smooth Character Rotation
	UFUNCTION(BlueprintCallable, Category = "Zombie Core|Rotation", meta = (Keywords = "Zombie,Movement"))
	void SmoothCharacterRotation(FRotator TargetRotation = FRotator(0,0,0), float InterpSpeed = 15.0);

	//Bazowa funkcja do aktualizacji rotacji kapsuly. W zaleznosci od takich zmiennych jak RotationCurve, MovementState, IsMoving ustalana jest docelowa rotacja oraz nastepuje interpolacja poprzez
	//uzycie SmoothCharacterRotation()
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Rotation", meta = (ForceAsFunction))
	void UpdateGroundedRotation();
	virtual void UpdateGroundedRotation_Implementation();

	//Dziala pobnie jak UpdateGroundedRotation() tylko jest aktywowana w momencie kiedy MovementState == InAir
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Rotation", meta = (ForceAsFunction))
	void UpdateFallingRotation();
	virtual void UpdateFallingRotation_Implementation();

	////Dziala pobnie jak UpdateGroundedRotation() tylko jest aktywowana w momencie kiedy MovementState == Crawl
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Rotation", meta = (ForceAsFunction))
	void UpdateCrawlingRotation();
	virtual void UpdateCrawlingRotation_Implementation();

	//⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆
	//⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆ H E A L T H  ,  D A M A G E  ,  D E A T H  ,  H I T ⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆
	//⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆⛆

	//Hit Reaction And Damage
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Behavior")
	void HitReactionAndDamage(bool& OutDeath, FVector& OutHitLocation, FVector& OutHitNormal, FName& OutBoneName, float& Outamage, AActor* DamageCauser, 
		float DamagePoints, FHitResult HitInfo, ECALS_WeaponType WeaponType, bool InstantDeath);
	virtual void HitReactionAndDamage_Implementation(bool& OutDeath, FVector& OutHitLocation, FVector& OutHitNormal, FName& OutBoneName, float& Outamage, 
		AActor* DamageCauser, float DamagePoints, FHitResult HitInfo, ECALS_WeaponType WeaponType, bool InstantDeath);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Damage", meta = (ForceAsFunction))
	void StartDeathSequence(FVector HitLocation, FVector HitNormal, FName HitBoneName, int DeathTypeIndex);
	virtual void StartDeathSequence_Implementation(FVector HitLocation, FVector HitNormal, FName HitBoneName, int DeathTypeIndex);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Damage", meta = (ForceAsFunction, DisplayName = "Check Fall Damage", Keywords = "Zombie,Character,Damage", AdvancedDisplay = "VelocityMaxRange,DamageTresholds"))
	bool CheckFallDamage(FHitResult LandHitResult, float VelocityMaxRange = 1500.0, FVector DamageTresholds = FVector(0.7, 0.55, 0.42));
	virtual bool CheckFallDamage_Implementation(FHitResult LandHitResult, float VelocityMaxRange = 1500.0, FVector DamageTresholds = FVector(0.7, 0.55, 0.42));

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Damage", meta = (ForceAsFunction, DisplayName = "Death By Fall Damage", Keywords = "Zombie,Character,Damage"))
	void DeathByFallDamage(FHitResult HitGroundResult);
	virtual void DeathByFallDamage_Implementation(FHitResult HitGroundResult);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Damage", meta = (DisplayName = "Apply Damage Value", Keywords = "Zombie,Character,Damage"))
	bool ApplyDamageValue(AActor* DamageCauser, FHitResult HitInfo, float DamageValue = 1.0, int DamageTypeIndex = 1, float UnRagdollTime = -1.0);
	virtual bool ApplyDamageValue_Implementation(AActor* DamageCauser, FHitResult HitInfo, float DamageValue = 1.0, int DamageTypeIndex = 1, float UnRagdollTime = -1.0);

	UFUNCTION(BlueprintPure, Category = "Zombie|Damage", meta = (DisplayName = "Is Alive", Keywords = "Zombie,Character,Damage"))
	bool IsAlive();

	UFUNCTION(BlueprintPure, Category = "Zombie|Damage", meta = (Keywords = "Zombie,Character,Damage", AdvancedDisplay = "RangeX,RangeZ"))
	FVector CalculateHitDirection(FHitResult InHitResult, FVector2D RangeX = FVector2D(12, 28), FVector2D RangeZ = FVector2D(120, 160));



	// ▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ R A G D O L L   S Y S T E M ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
	// ▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░▒░

	//Try RAGDOLL Start
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll Start", Keywords = "AGLS,Character,Ragdoll"))
	void RagdollStart();
	virtual void RagdollStart_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll End", Keywords = "AGLS,Character,Ragdoll"))
	void RagdollEnd();
	virtual void RagdollEnd_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Zombie|Ragdoll", meta = (ForceAsFunction, DisplayName = "Ragdoll Update", Keywords = "ALS Character", 
		AdvancedDisplay = "OutSpringRange,InDamping,InForceLimit,DebugIndex"))
	void RagdollUpdate(float OutSpringRange = 5000.0, float InDamping = 0.0, float InForceLimit = 0.0, int DebugIndex = 0);
	virtual void RagdollUpdate_Implementation(float OutSpringRange = 5000.0, float InDamping = 0.0, float InForceLimit = 0.0, int DebugIndex = 0);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Zombie|Ragdoll", meta = (ForceAsFunction, DisplayName = "Get Get Up Animation", Keywords = "AGLS,Character,Ragdoll"))
	TSoftObjectPtr<UAnimMontage> GetGetUpAnimation();
	virtual TSoftObjectPtr<UAnimMontage> GetGetUpAnimation_Implementation();

	//End Ragdoll SECTION -----------------------------------------------

// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ OTHER FUNCTIONS OR MACROS ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░

	//Get Child Bones Names Structure from Mesh 
	UFUNCTION(BlueprintPure, Category = "Zombie|Utility")
	TArray<FName> GetChildBoneNames(FName BoneName, bool bIncludeSelf = false) const;

	UFUNCTION(BlueprintPure, Category = "Zombie|Utility")
	FTransform GetRefBoneTransformInLocalSpace(USkeletalMeshComponent* InSkeletalMesh,FName BoneName) const;

	UFUNCTION(BlueprintPure, Category = "Zombie|Utility")
	bool GetIsStartedAttack() const;

	UFUNCTION(BlueprintPure, Category = "Zombie|Utility")
	float GetAttackElapsedTime() const;


	// Overrided Character Movement functions
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ INTERFACE FUNTIONS OVERRIDE SECTION ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
	// ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░

	virtual void BPI_AI_Get_CurrentStates_Implementation(
		TEnumAsByte<EMovementMode>& PawnMovementMode,
		CALS_MovementState& MovementState,
		CALS_MovementState& PrevMovementState,
		CALS_MovementAction& MovementAction,
		CALS_RotationMode& RotationMode,
		CALS_Gait& ActualGait,
		CALS_Stance& ActualStance,
		CALS_OverlayState& OverlayState,
		CALS_GroundedMoveMode& GroundedMoveMode
	) override;

	virtual void BPI_AI_Get_CurrentStatesSafe_Implementation(
		CALS_MovementState& MovementState,
		CALS_MovementState& PrevMovementState,
		CALS_MovementAction& MovementAction,
		CALS_RotationMode& RotationMode,
		CALS_Gait& ActualGait,
		CALS_Stance& ActualStance
	) const override;

	virtual void BPI_AI_Get_EssentialValues_Implementation(
		FVector& Velocity,
		FVector& Acceleration,
		FVector& MovementInput,
		bool& IsMoving,
		bool& HasMovementInput,
		float& Speed,
		FRotator& AimingRotation,
		float& AimYawRate
	) override;

	virtual void BPI_AI_Get_EssentialValuesSafe_Implementation(
		FVector& Acceleration,
		bool& HasMovementInput,
		FRotator& AimingRotation
	) const override;

	virtual void BPI_AI_Set_LockRotationUpdate_Implementation(bool LockUpdate) override;
	virtual void BPI_AI_Get_RotationLocked_Implementation(bool& RotationIsLocked) override;
	virtual void BPI_AI_CrouchOrUncrouch_Implementation(CALS_Stance Stance) override;

	virtual void BPI_AI_Set_MovementState_Implementation(CALS_MovementState NewState) override;
	virtual void BPI_AI_Set_MovementAction_Implementation(CALS_MovementAction NewAction) override;
	virtual void BPI_AI_Set_RotationMode_Implementation(CALS_RotationMode NewMode) override;
	virtual void BPI_AI_Set_Gait_Implementation(CALS_Gait NewGait) override;
	virtual void BPI_AI_Set_DesiredGait_Implementation(CALS_Gait NewDesiredGait) override;
	virtual void BPI_AI_Set_GrounedMoveMode_Implementation(CALS_GroundedMoveMode NewMode) override;
	virtual void BPI_AI_Get_LOD_State_Implementation(AGLS_LOD_State& CurrentState) override;
	virtual void BPI_AI_Set_MovementSettingsByName_Implementation(FName KeyName) override;
	virtual void BPI_AI_Get_LocomotionModeIndex_Implementation(int& LocomotionIndex, uint8& LocomotionByte, FName& LocomotionName) override;
	virtual void BPI_AI_Set_EnableRagdoll_Implementation(bool Enable) override;
	virtual void BPI_AI_AddRemove_GameplayTagInfo_Implementation(FGameplayTag NewTagToAdd, bool RemoveMode, bool& ReturnUpdated) override;
	virtual void BPI_AI_Get_MainTagsContainerData_Implementation(FGameplayTagContainer& TagsContainer) override;

};
