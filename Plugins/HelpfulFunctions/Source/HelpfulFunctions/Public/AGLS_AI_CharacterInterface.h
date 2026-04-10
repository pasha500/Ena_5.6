

#pragma once

#include "CoreMinimal.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "GameplayTagContainer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Interface.h"
#include "AGLS_AI_CharacterInterface.generated.h"

/*One of the main interfaces intended for the construction of the 'UCharacter' class in the AGLS project. 
It is implemented in characters that will initially be controlled by the AI ​​Controller. 
It contains information that is retrieved by e.g. 'Anim Instance', 'AI Controller', external components and 
other full UCharater clases. */
UINTERFACE(MinimalAPI, Category = "AGLS AI Character Core", meta = (DisplayName = "INTERFACE_CharAI_Core"))
class UAGLS_AI_CharacterInterface : public UInterface
{
	GENERATED_BODY()
};

/*One of the main interfaces intended for the construction of the 'UCharacter' class in the AGLS project.
It is implemented in characters that will initially be controlled by the AI ​​Controller.
It contains information that is retrieved by e.g. 'Anim Instance', 'AI Controller', external components and
other full UCharater clases. */
class HELPFULFUNCTIONS_API IAGLS_AI_CharacterInterface
{
	GENERATED_BODY()

public:
//It allows you to get the most important information about the current states of the Character
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get", meta = (AdvancedDisplay = 2))
	void BPI_AI_Get_CurrentStates(
		TEnumAsByte<EMovementMode>& PawnMovementMode,
		CALS_MovementState& MovementState,
		CALS_MovementState& PrevMovementState,
		CALS_MovementAction& MovementAction,
		CALS_RotationMode& RotationMode,
		CALS_Gait& ActualGait,
		CALS_Stance& ActualStance,
		CALS_OverlayState& OverlayState,
		CALS_GroundedMoveMode& GroundedMoveMode
		);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get", meta = (AdvancedDisplay = 2, BlueprintThreadSafe))
	void BPI_AI_Get_CurrentStatesSafe(
		CALS_MovementState& MovementState,
		CALS_MovementState& PrevMovementState,
		CALS_MovementAction& MovementAction,
		CALS_RotationMode& RotationMode,
		CALS_Gait& ActualGait,
		CALS_Stance& ActualStance
	) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get", meta = (AdvancedDisplay = 2))
	void BPI_AI_Get_EssentialValues(
		FVector& Velocity,
		FVector& Acceleration,
		FVector& MovementInput,
		bool& IsMoving,
		bool& HasMovementInput,
		float& Speed,
		FRotator& AimingRotation,
		float& AimYawRate
	);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get", meta = (AdvancedDisplay = 2, BlueprintThreadSafe))
	void BPI_AI_Get_EssentialValuesSafe(
		FVector& Acceleration,
		bool& HasMovementInput,
		FRotator& AimingRotation
	) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get")
	void BPI_AI_Get_CurrentActivatedTask(UObject* Task);

/*ENG:
After locking the main function that determines the current capsule rotation by calling the BPI_AI_Set_LockRotationUpdate() interface function, 
this declaration allows you to return information about whether the rotation has actually been locked.

PL:
Po zablokowaniu głównej funkcji określającej aktualną rotację kapsuły poprzez wywołanie funkcji interfejsu BPI_AI_Set_LockRotationUpdate(), za 
pośrednictwem tej deklaracji można otrzymać informację, o tym czy rotacja faktycznie została zablokowana.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get")
	void BPI_AI_Get_RotationLocked(bool& RotationIsLocked);

/*Set Movement State By Changing Enum Value. The functionality of this function is similar to that in the basic version of the AdvancedLocomotionSystemV4 UE4 project.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_MovementState(CALS_MovementState NewState);

/*Set Movement Action By Changing Enum Value. The functionality of this function is similar to that in the basic version of the AdvancedLocomotionSystemV4 UE4 project.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_MovementAction(CALS_MovementAction NewAction);

/*Set Rotation Mode By Changing Enum Value. The functionality of this function is similar to that in the basic version of the AdvancedLocomotionSystemV4 UE4 project.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_RotationMode(CALS_RotationMode NewMode);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_Gait(CALS_Gait NewGait);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_DesiredGait(CALS_Gait NewDesiredGait);

/*Set Overlay State. Mainly prepared for Human AI Characters. Zombies dont have overlay states similar to ALS project. The functionality of this function is similar 
to that in the basic version of the AdvancedLocomotionSystemV4 UE4 project.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_OverlayState(CALS_OverlayState NewState, bool Forced);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_GrounedMoveMode(CALS_GroundedMoveMode NewMode);

/*ENG:
After properly overriding the function definition, it can affect the CharacterMovement Settings by setting the so-called 'Movement Models'. Some of these parameters in 
the CharacterMovementComponent can be:
- float Acceleration (taken from Curve Vector)
- float Deceleration (taken from Curve Vector)
- float GroundFriction (taken from Curve Vector)
Additionally, the Movement Model can affect the capsule interpolation speed (Rotation Curve).

PL:
Po odpowiednim nadpisaniu definicji funkcji, może ona wpływać na CharacterMovement Settings ustawiając tak zwane 'Movement Models'. Jednymi z takich parametrów ...*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_MovementSettingsByName(FName KeyName);

/*The purpose of this function is similar to 'BPI_AI_BumpReactTrigger'. However, in this case it mainly concerns 'HumanAI Characters'.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_StruckCharacter(ACharacter* From, ACharacter* To, float Damage);

/*Standard Crouch() or UnCrouch() call via interface function*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_CrouchOrUncrouch(CALS_Stance Stance);

/*ENG:
An important function often called in Gameplay Abilities and other Sequences*. Calling this interface function allows you to lock or unlock the base orientation 
control system of the character. Most UCharacter classes in the AGLS project contain their own approach to controlling the capsule rotation. However, in some 
situations, e.g. when performing a pick-up sequence, you want to use a different way of calculating rotation. To do this, you can lock the base rotation system 
for the duration of the sequence, set your own orientation with SetActorRotation() + UpdateRotationVariables(...)*, and then unlock the rotation system. It is 
important to remember to unlock by calling back this interface! Failure to use this interface may result in capsule interpolation being colored to the given 
orientation.

PL:
Ważna funkcja często wywoływana w Gameplay Abilities oraz innych sekwencjach. Wowłanie tej funkcji interfejsu pozwala na zablokowanie lub odblokowanie bazowego 
systemu sterowania orientacją charakteru. Większość klas UCharacter  w projekcie AGLS zawiera własne podejście do kontrolowania rotacją kapsuły. Jednak w niekórych 
sytuacjach np. kiedy wykonujemy sekwencję podnoszenia przedmiotu chcemy zastosować inny sposób obliczania rotacji. W tym celu na czas trwania sekwencji można 
zablokować bazowy system rotacji, ustawić własną orientację SetActorRotation() + UpdateRotationVariables(...)*, a następnie odblokować system rotacji. Ważne jest 
aby pamiętać o odblokowaniu. Nie zastosowanie tego interfejsu może prowadzić do kolidowania interpolacji kapsuły do zadanej orientacji.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_LockRotationUpdate(bool LockUpdate);

/*With this function you can activate or deactivate ragdoll [Ragdoll() or UnRagdoll()] without having to refer to the whole class*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_Set_EnableRagdoll(bool Enable);

/*Allows you to retrieve the current custom LOD state*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get")
	void BPI_AI_Get_LOD_State(AGLS_LOD_State& CurrentState);

/*ENG:
Declared mainly for the purpose of constructing the 'AGLS_HumanAI_CharacterLogicBase' class. It allows you to get information about LocomotionMode as an index 
in the form of an int variable. This is due to the fact that the 'ALSP2_MovementMode' enum class was created as a separate asset, which means that C++ does not 
have access to such a data type. For this reason, this Enum is sent in a converted form as an int, byte and state name (FName).

PL:
Zadeklarowane głównie w celu konstrukcji klasy 'AGLS_HumanAI_CharacterLogicBase'. Pozwala pobrać informacji o LocomotionMode jako indeks w postaci zmiennej int. 
Wynika to z faktu że klasa enum 'ALSP2_MovementMode' została utworzona jako oddzielny asset, co oznacza że C++ nie ma dostępu do takiego typu danych. Z tego też 
powodu Enum ten jest przesyłany w przekonwertowanej formie jako int, byte oraz nazwa stanu (FName).*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get", meta = (AdvancedDisplay = 1))
	void BPI_AI_Get_LocomotionModeIndex(int& LocomotionIndex, uint8& LocomotionByte, FName& LocomotionName);

/*ENG:
With this function you can get a reference to a PathActor instance that the AI ​​controller can follow. This interface is often called in controller classes or in the 
BehaviorTree as BT_Servces or BT_Tasks. The reference is passed as AActor* which means that to get detailed path information you will need to do CastTo<class type>

PL:
Za pomocą tej funkcji można pobrać odniesienie do instancji PathActor, po której kontroler AI może podąrzać. Interfejs ten jest często wywoływany własnie w klasach 
kontrolerów lub w BehaviorTree jako BT_Servces lub BT_Tasks. Odniesienie jest przekazywane jako AActor* co oznacza że aby otrzymać szczegółowe informacje o ścieżce 
będzie wymagane wykonanie CastTo<class type>*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get")
	void BPI_AI_GetPathFollowActor(AActor*& PathActor) const;

//Set Path To Follow Actor by using interface call
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set", meta = (ForceAsFunction))
	void BPI_AI_Set_PathFollowActor(AActor* PathActor);


/*ENG:
Function intended for constructing a system based on reacting to bumps by other character instances. It can be called when the OnHit delegate detects that the capsule 
has been hit. For example, this function is used by 'AGLS_ZombieCharacter_Base' to activate root motion animations showing the reaction to taps, stumbles or pushes.

PL:
Funkcja przeznaczona dla konstrukcji systemu polegającego na reagowaniu na popchnięcia przez inne instancje charakterów. Może być wywoływana w momencie kiedy delegate 
OnHit wykryje że kapsuła została uderzona. Przykładowo funkcja ta używana jest przez 'AGLS_ZombieCharacter_Base' do aktywowania animacji root motion przedstawiających 
reakcję na stuknięcia, potknięcia lub popchnięcia.
*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_BumpReactTrigger(ACharacter* From, FHitResult HitResult);

/*ENG:
Function usually called when the player activates the gameplay ability 'Stealth Finisher'. In such case the character that was selected as Victim can be informed about it. 
For example when we start SF then Physic Asset should be deactivated. For this purpose we can use this interface function to make changes to SkeletalMesh for a given instance.

PL:
Funkcja zazwyczaj wywoływana w momencie kiedy gracz aktywuje gameplay ability 'Stealth Finisher'. W takim przypadku charakter który zoztał wybrany jako Victim może zostać o tym 
poinformowany. Przykładowo kiedy uruchamiamy SF to wtedy Physic Asset powinien być deaktywowany. W tym celu można użyć właśnie tej funkcji interfejsu aby wprowadzić zmiany w 
SkeletalMesh dla danej instancji.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set", meta = (DisplayName="BPI_AI_WhenIsVictimForFinisher"))
	void BPI_AI_FinisherOrMeleeStarted(bool Started, int ActionIndex);



	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Set")
	void BPI_AI_AddRemove_GameplayTagInfo(FGameplayTag NewTagToAdd, bool RemoveMode, bool& ReturnUpdated);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Character|Get")
	void BPI_AI_Get_MainTagsContainerData(FGameplayTagContainer& TagsContainer);

};
