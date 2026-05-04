


#include "GAS_MainCharacterCpp.h"
#include "AGLS_ZombieAttacksComponentCore.h"
#include "GameplayTagsManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
//include Kismet
#include "KismetAnimationLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
//include math
#include "Math/Vector.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InputComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"

void FLockOnSweepTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValid(Target))
	{
		Target->SweepNonTargetIcons();
	}
}

FString FLockOnSweepTickFunction::DiagnosticMessage()
{
	return TEXT("FLockOnSweepTickFunction [LockOn Icon Sweep]");
}

namespace
{
	enum class ENativeCombatFuryLockOnMode : uint8
	{
		Off = 0,
		Soft = 1,
		Hard = 2
	};

	const TCHAR* CombatFuryLockOnTypeEnumPath = TEXT("/Game/CombatFury/CF_Assets/Components/LockOn_Component/E_LockOnType.E_LockOnType");

	void ShowNativeLockOnModeFeedback(AGAS_MainCharacterCpp* Character, const FString& Message, const FColor& Color, float DurationSeconds = 0.75f)
	{
		if (!IsValid(Character) || !Character->bShowNativeLockOnModeFeedback || !GEngine)
		{
			return;
		}

		const APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
		if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
		{
			return;
		}

		const int32 StableMessageKey = static_cast<int32>(reinterpret_cast<UPTRINT>(Character) & 0x7FFFFFFF);
		GEngine->AddOnScreenDebugMessage(StableMessageKey, DurationSeconds, Color, Message);
	}

	FGameplayTag GetPairedZombieAttackTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Action.PairedZombieAttack")), false);
		return Tag;
	}

	FGameplayTag GetZombieGrabVictimTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Action.Melee.GrabVic")), false);
		return Tag;
	}

	FGameplayTag GetActionMeleeRootTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Action.Melee")), false);
		return Tag;
	}

	bool HasActionMeleeTag(const UAbilitySystemComponent* AbilitySystemComponent)
	{
		if (!IsValid(AbilitySystemComponent))
		{
			return false;
		}

		const FGameplayTag ActionMeleeTag = GetActionMeleeRootTag();
		return ActionMeleeTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(ActionMeleeTag);
	}

	bool IsZombieGrabAbilityName(const FString& Name)
	{
		return
			Name.Contains(TEXT("PairedZombieAttack"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("PairedAttack"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("ZombieGrab"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("GrabVic"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Grab"), ESearchCase::IgnoreCase);
	}

	bool HasInvokableNoInputSignature(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			const FProperty* Property = *It;
			const bool bIsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOutOnly = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (!bIsReturn && !bIsOutOnly)
			{
				return false;
			}
		}

		return true;
	}

	UFunction* FindFirstFunctionByNames(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, bool bRequireNoInputParams = false)
	{
		if (!Object)
		{
			return nullptr;
		}

		for (const TCHAR* RawName : FunctionNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			if (UFunction* Found = Object->FindFunction(FName(RawName)))
			{
				if (bRequireNoInputParams && !HasInvokableNoInputSignature(Found))
				{
					continue;
				}
				return Found;
			}
		}

		return nullptr;
	}

	void InitFunctionParams(UFunction* Function, uint8* Params)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
	}

	void DestroyFunctionParams(UFunction* Function, uint8* Params)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
	}

	bool CallFunctionNoParams(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames, true);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);
		DestroyFunctionParams(Function, Params);
		return true;
	}

	bool CallFunctionSetActorArg(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, AActor* ActorArg)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);

		bool bSetAnyActorInput = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bIsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOutOnly = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (bIsReturn || bIsOutOnly)
			{
				continue;
			}

			FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty)
			{
				continue;
			}

			if (ObjectProperty->PropertyClass && ActorArg && ActorArg->IsA(ObjectProperty->PropertyClass))
			{
				ObjectProperty->SetObjectPropertyValue_InContainer(Params, ActorArg);
				bSetAnyActorInput = true;
				break;
			}
		}

		Object->ProcessEvent(Function, Params);
		DestroyFunctionParams(Function, Params);
		return bSetAnyActorInput;
	}

	bool CallFunctionSetObjectArg(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, UObject* ObjectArg)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function || !ObjectArg)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);

		bool bSetAnyObjectInput = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bIsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOutOnly = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (bIsReturn || bIsOutOnly)
			{
				continue;
			}

			FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty)
			{
				continue;
			}

			if (ObjectProperty->PropertyClass && ObjectArg->IsA(ObjectProperty->PropertyClass))
			{
				ObjectProperty->SetObjectPropertyValue_InContainer(Params, ObjectArg);
				bSetAnyObjectInput = true;
				break;
			}
		}

		Object->ProcessEvent(Function, Params);
		DestroyFunctionParams(Function, Params);
		return bSetAnyObjectInput;
	}

	AActor* CallFunctionGetActor(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return nullptr;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);

		AActor* OutActor = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bCanReadAsOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm) || Property->HasAnyPropertyFlags(CPF_OutParm);
			if (!bCanReadAsOutput)
			{
				continue;
			}

			FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty)
			{
				continue;
			}

			UObject* MaybeObject = ObjectProperty->GetObjectPropertyValue_InContainer(Params);
			if (AActor* MaybeActor = Cast<AActor>(MaybeObject))
			{
				OutActor = MaybeActor;
				break;
			}
		}

		DestroyFunctionParams(Function, Params);
		return OutActor;
	}

	bool CallFunctionGetBool(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, bool& OutValue)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);

		bool bFound = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bCanReadAsOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm) || Property->HasAnyPropertyFlags(CPF_OutParm);
			if (!bCanReadAsOutput)
			{
				continue;
			}

			FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property);
			if (!BoolProperty)
			{
				continue;
			}

			OutValue = BoolProperty->GetPropertyValue_InContainer(Params);
			bFound = true;
			break;
		}

		DestroyFunctionParams(Function, Params);
		return bFound;
	}

	bool CallFunctionGetInt(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, int32& OutValue)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);

		bool bFound = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bCanReadAsOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm) || Property->HasAnyPropertyFlags(CPF_OutParm);
			if (!bCanReadAsOutput)
			{
				continue;
			}

			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsInteger())
				{
					OutValue = (int32)NumericProperty->GetSignedIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Params));
					bFound = true;
					break;
				}
			}
			else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
				{
					OutValue = (int32)UnderlyingProperty->GetSignedIntPropertyValue(UnderlyingProperty->ContainerPtrToValuePtr<void>(Params));
					bFound = true;
					break;
				}
			}
		}

		DestroyFunctionParams(Function, Params);
		return bFound;
	}

	bool CallFunctionGetFloat(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, float& OutValue)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);

		bool bFound = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bCanReadAsOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm) || Property->HasAnyPropertyFlags(CPF_OutParm);
			if (!bCanReadAsOutput)
			{
				continue;
			}

			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsFloatingPoint())
				{
					const double NumericValue = NumericProperty->GetFloatingPointPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Params));
					OutValue = static_cast<float>(NumericValue);
					bFound = true;
					break;
				}

				if (NumericProperty->IsInteger())
				{
					const int64 NumericValue = NumericProperty->GetSignedIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Params));
					OutValue = static_cast<float>(NumericValue);
					bFound = true;
					break;
				}
			}
		}

		DestroyFunctionParams(Function, Params);
		return bFound;
	}

	UObject* CallFunctionGetObject(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return nullptr;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);
		Object->ProcessEvent(Function, Params);

		UObject* OutObject = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bCanReadAsOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm) || Property->HasAnyPropertyFlags(CPF_OutParm);
			if (!bCanReadAsOutput)
			{
				continue;
			}

			FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty)
			{
				continue;
			}

			OutObject = ObjectProperty->GetObjectPropertyValue_InContainer(Params);
			break;
		}

		DestroyFunctionParams(Function, Params);
		return OutObject;
	}

	bool CallFunctionSetBoolArg(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, bool bValue)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);

		bool bSetAnyInput = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				continue;
			}
			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				BoolProperty->SetPropertyValue_InContainer(Params, bValue);
				bSetAnyInput = true;
				break;
			}
		}

		Object->ProcessEvent(Function, Params);
		DestroyFunctionParams(Function, Params);
		return bSetAnyInput;
	}

	// Writes bValue to EVERY bool property in PropertyNames that exists on Object (not just the first).
	// Use this for actor-level targeting bools where multiple variables may coexist.
	int32 WriteBoolPropertyByNamesAll(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, bool bValue)
	{
		if (!Object)
		{
			return 0;
		}

		int32 Count = 0;
		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				BoolProperty->SetPropertyValue_InContainer(Object, bValue);
				++Count;
			}
		}

		return Count;
	}

	bool ReadBoolPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, bool& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				OutValue = BoolProperty->GetPropertyValue_InContainer(Object);
				return true;
			}
		}

		return false;
	}

	bool ReadFloatPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, float& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsFloatingPoint())
				{
					const double NumericValue = NumericProperty->GetFloatingPointPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Object));
					OutValue = static_cast<float>(NumericValue);
					return true;
				}

				if (NumericProperty->IsInteger())
				{
					const int64 NumericValue = NumericProperty->GetSignedIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Object));
					OutValue = static_cast<float>(NumericValue);
					return true;
				}
			}
		}

		return false;
	}

	bool ReadIntPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, int32& OutValue)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsInteger())
				{
					OutValue = (int32)NumericProperty->GetSignedIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Object));
					return true;
				}
			}
			else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
				{
					OutValue = (int32)UnderlyingProperty->GetSignedIntPropertyValue(UnderlyingProperty->ContainerPtrToValuePtr<void>(Object));
					return true;
				}
			}
		}

		return false;
	}

	UObject* ReadObjectPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames)
	{
		if (!Object)
		{
			return nullptr;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				return ObjectProperty->GetObjectPropertyValue_InContainer(Object);
			}
		}

		return nullptr;
	}

	bool WriteObjectPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, UObject* Value)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				if (!Value || !ObjectProperty->PropertyClass || Value->IsA(ObjectProperty->PropertyClass))
				{
					ObjectProperty->SetObjectPropertyValue_InContainer(Object, Value);
					return true;
				}
			}
		}

		return false;
	}

	bool WriteBoolPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, bool bValue)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				BoolProperty->SetPropertyValue_InContainer(Object, bValue);
				return true;
			}
		}

		return false;
	}

	bool WriteIntPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, int64 Value)
	{
		if (!Object)
		{
			return false;
		}

		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsInteger())
				{
					NumericProperty->SetIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Object), static_cast<uint64>(Value));
					return true;
				}
			}
			else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
				{
					UnderlyingProperty->SetIntPropertyValue(UnderlyingProperty->ContainerPtrToValuePtr<void>(Object), static_cast<uint64>(Value));
					return true;
				}
			}
			else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				ByteProperty->SetPropertyValue_InContainer(Object, static_cast<uint8>(FMath::Clamp<int64>(Value, 0, 255)));
				return true;
			}
		}

		return false;
	}

	bool WriteNamePropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, FName Value)
	{
		if (!Object)
		{
			return false;
		}

		const FString ValueAsString = Value.ToString();
		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				NameProperty->SetPropertyValue_InContainer(Object, Value);
				return true;
			}
			if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
			{
				StrProperty->SetPropertyValue_InContainer(Object, ValueAsString);
				return true;
			}
		}

		return false;
	}

	bool WriteLinearColorPropertyByNames(UObject* Object, std::initializer_list<const TCHAR*> PropertyNames, const FLinearColor& Value)
	{
		if (!Object)
		{
			return false;
		}

		const UScriptStruct* LinearColorStruct = TBaseStructure<FLinearColor>::Get();
		for (const TCHAR* RawName : PropertyNames)
		{
			if (RawName == nullptr || RawName[0] == '\0')
			{
				continue;
			}

			FProperty* Property = Object->GetClass()->FindPropertyByName(FName(RawName));
			if (!Property)
			{
				continue;
			}

			FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty || !StructProperty->Struct)
			{
				continue;
			}

			if (StructProperty->Struct == LinearColorStruct)
			{
				*StructProperty->ContainerPtrToValuePtr<FLinearColor>(Object) = Value;
				return true;
			}
		}

		return false;
	}

	bool CallFunctionSetIntArg(UObject* Object, std::initializer_list<const TCHAR*> FunctionNames, int64 IntValue)
	{
		UFunction* Function = FindFirstFunctionByNames(Object, FunctionNames);
		if (!Object || !Function)
		{
			return false;
		}

		uint8* Params = (uint8*)FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Params, Function->ParmsSize);
		InitFunctionParams(Function, Params);

		bool bSetAnyNumericInput = false;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			const bool bIsReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOutOnly = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (bIsReturn || bIsOutOnly)
			{
				continue;
			}

			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
				{
					UnderlyingProperty->SetIntPropertyValue(UnderlyingProperty->ContainerPtrToValuePtr<void>(Params), static_cast<uint64>(IntValue));
					bSetAnyNumericInput = true;
					break;
				}
			}
			else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
			{
				if (NumericProperty->IsInteger())
				{
					NumericProperty->SetIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Params), static_cast<uint64>(IntValue));
					bSetAnyNumericInput = true;
					break;
				}
			}
		}

		Object->ProcessEvent(Function, Params);
		DestroyFunctionParams(Function, Params);
		return bSetAnyNumericInput;
	}

	UEnum* LoadCombatFuryLockOnTypeEnum()
	{
		static TWeakObjectPtr<UEnum> CachedEnum;
		if (CachedEnum.IsValid())
		{
			return CachedEnum.Get();
		}

		UObject* EnumObject = StaticLoadObject(UEnum::StaticClass(), nullptr, CombatFuryLockOnTypeEnumPath);
		UEnum* EnumAsset = Cast<UEnum>(EnumObject);
		if (IsValid(EnumAsset))
		{
			CachedEnum = EnumAsset;
		}

		return EnumAsset;
	}

	int64 ResolveCombatFuryLockOnModeValue(UEnum* LockOnTypeEnum, ENativeCombatFuryLockOnMode Mode)
	{
		const int64 FallbackOff = 0;
		const int64 FallbackSoft = 1;
		const int64 FallbackHard = 2;
		if (!IsValid(LockOnTypeEnum))
		{
			switch (Mode)
			{
			case ENativeCombatFuryLockOnMode::Soft: return FallbackSoft;
			case ENativeCombatFuryLockOnMode::Hard: return FallbackHard;
			default: return FallbackOff;
			}
		}

		TArray<FString> SearchTokens;
		switch (Mode)
		{
		case ENativeCombatFuryLockOnMode::Hard:
			SearchTokens = { TEXT("Hard"), TEXT("Lock"), TEXT("Target") };
			break;
		case ENativeCombatFuryLockOnMode::Soft:
			SearchTokens = { TEXT("Soft"), TEXT("Track"), TEXT("Aim") };
			break;
		default:
			SearchTokens = { TEXT("Off"), TEXT("None"), TEXT("Free"), TEXT("Idle") };
			break;
		}

		for (int32 EnumIndex = 0; EnumIndex < LockOnTypeEnum->NumEnums(); ++EnumIndex)
		{
			const FString NameString = LockOnTypeEnum->GetNameStringByIndex(EnumIndex);
			if (NameString.Contains(TEXT("MAX"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			for (const FString& Token : SearchTokens)
			{
				if (NameString.Contains(Token, ESearchCase::IgnoreCase))
				{
					return LockOnTypeEnum->GetValueByIndex(EnumIndex);
				}
			}
		}

		const int32 EnumCount = LockOnTypeEnum->NumEnums();
		const int32 UsableCount = EnumCount > 0 ? EnumCount - 1 : 0;
		const int32 SoftIndex = FMath::Clamp(1, 0, FMath::Max(UsableCount - 1, 0));
		const int32 HardIndex = FMath::Clamp(2, 0, FMath::Max(UsableCount - 1, 0));
		switch (Mode)
		{
		case ENativeCombatFuryLockOnMode::Soft:
			return LockOnTypeEnum->GetValueByIndex(SoftIndex);
		case ENativeCombatFuryLockOnMode::Hard:
			return LockOnTypeEnum->GetValueByIndex(HardIndex);
		default:
			return LockOnTypeEnum->GetValueByIndex(0);
		}
	}

	bool WriteCombatFuryLockOnType(UObject* LockOnObject, ENativeCombatFuryLockOnMode Mode)
	{
		if (!IsValid(LockOnObject))
		{
			return false;
		}

		UEnum* LockOnTypeEnum = LoadCombatFuryLockOnTypeEnum();
		const int64 EnumValue = ResolveCombatFuryLockOnModeValue(LockOnTypeEnum, Mode);

		bool bApplied = false;
		bApplied |= WriteIntPropertyByNames(LockOnObject,
			{
				TEXT("LockOnType"),
				TEXT("LockonType"),
				TEXT("CurrentLockOnType"),
				TEXT("CurrentLockType"),
				TEXT("LockType"),
				TEXT("TargetingType")
			},
			EnumValue);

		for (TFieldIterator<FProperty> It(LockOnObject->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const FString PropertyName = Property->GetName();
			const bool bLooksLikeLockOnType =
				PropertyName.Contains(TEXT("LockOnType"), ESearchCase::IgnoreCase) ||
				PropertyName.Contains(TEXT("LockType"), ESearchCase::IgnoreCase);
			if (!bLooksLikeLockOnType)
			{
				continue;
			}

			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
				{
					UnderlyingProperty->SetIntPropertyValue(UnderlyingProperty->ContainerPtrToValuePtr<void>(LockOnObject), static_cast<uint64>(EnumValue));
					bApplied = true;
				}
			}
			else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				ByteProperty->SetPropertyValue_InContainer(LockOnObject, static_cast<uint8>(FMath::Clamp<int64>(EnumValue, 0, 255)));
				bApplied = true;
			}
		}

		bApplied |= CallFunctionSetIntArg(LockOnObject,
			{
				TEXT("SetLockOnType"),
				TEXT("SetLockonType"),
				TEXT("SetCurrentLockOnType"),
				TEXT("SetTargetingType"),
				TEXT("SetLockType")
			},
			EnumValue);

		return bApplied;
	}

	bool MatchesConfiguredSlotIndex(int32 SlotIndex, int32 ConfigSlotIndex)
	{
		if (SlotIndex == INDEX_NONE)
		{
			return false;
		}

		// Accept both direct and 0-based index interpretation.
		return SlotIndex == ConfigSlotIndex || (SlotIndex + 1) == ConfigSlotIndex;
	}

	bool DoesWeaponLookMelee(const UObject* WeaponObject)
	{
		if (!IsValid(WeaponObject))
		{
			return false;
		}

		auto LooksMeleeByString = [](const FString& SourceText) -> bool
		{
			return
				SourceText.Contains(TEXT("Melee"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Sword"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Knife"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Blade"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Axe"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Katana"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Hammer"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Mace"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Club"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Spear"), ESearchCase::IgnoreCase) ||
				SourceText.Contains(TEXT("Bat"), ESearchCase::IgnoreCase);
		};

		const FString CombinedName = WeaponObject->GetName() + TEXT(" ") + WeaponObject->GetClass()->GetName() + TEXT(" ") + WeaponObject->GetPathName();
		if (LooksMeleeByString(CombinedName))
		{
			return true;
		}

		if (const AActor* WeaponActor = Cast<AActor>(WeaponObject))
		{
			TArray<UStaticMeshComponent*> StaticMeshComponents;
			WeaponActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
			for (const UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
			{
				if (!IsValid(StaticMeshComponent))
				{
					continue;
				}

				const FString ComponentName = StaticMeshComponent->GetName() + TEXT(" ") + StaticMeshComponent->GetPathName();
				if (LooksMeleeByString(ComponentName))
				{
					return true;
				}

				if (const UStaticMesh* StaticMeshAsset = StaticMeshComponent->GetStaticMesh())
				{
					const FString MeshName = StaticMeshAsset->GetName() + TEXT(" ") + StaticMeshAsset->GetPathName();
					if (LooksMeleeByString(MeshName))
					{
						return true;
					}
				}
			}

			TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
			WeaponActor->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
			for (const USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
			{
				if (!IsValid(SkeletalMeshComponent))
				{
					continue;
				}

				const FString ComponentName = SkeletalMeshComponent->GetName() + TEXT(" ") + SkeletalMeshComponent->GetPathName();
				if (LooksMeleeByString(ComponentName))
				{
					return true;
				}

				if (const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComponent->GetSkeletalMeshAsset())
				{
					const FString MeshName = SkeletalMeshAsset->GetName() + TEXT(" ") + SkeletalMeshAsset->GetPathName();
					if (LooksMeleeByString(MeshName))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool DoesAttachedEquipmentLookMelee(const AActor* OwnerActor)
	{
		if (!IsValid(OwnerActor))
		{
			return false;
		}

		TArray<AActor*> AttachedActors;
		const_cast<AActor*>(OwnerActor)->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			if (DoesWeaponLookMelee(AttachedActor) || DoesWeaponLookMelee(AttachedActor ? AttachedActor->GetClass() : nullptr))
			{
				return true;
			}
		}

		TInlineComponentArray<UChildActorComponent*> ChildActorComponents(const_cast<AActor*>(OwnerActor));
		for (UChildActorComponent* ChildActorComponent : ChildActorComponents)
		{
			if (!IsValid(ChildActorComponent))
			{
				continue;
			}

			AActor* ChildActor = ChildActorComponent->GetChildActor();
			if (DoesWeaponLookMelee(ChildActor) || DoesWeaponLookMelee(ChildActor ? ChildActor->GetClass() : nullptr))
			{
				return true;
			}
		}

		return false;
	}

	FVector ResolveLockOnAimPoint(const AActor* TargetActor)
	{
		if (!IsValid(TargetActor))
		{
			return FVector::ZeroVector;
		}

		if (const USkeletalMeshComponent* SkeletalMesh = TargetActor->FindComponentByClass<USkeletalMeshComponent>())
		{
			static const FName PreferredSockets[] =
			{
				FName(TEXT("LockOnSocket")),
				FName(TEXT("LockOn")),
				FName(TEXT("head")),
				FName(TEXT("Head")),
				FName(TEXT("neck_01")),
				FName(TEXT("spine_03")),
				FName(TEXT("spine_02")),
				FName(TEXT("chest")),
				FName(TEXT("Chest"))
			};

			for (const FName& SocketName : PreferredSockets)
			{
				if (SkeletalMesh->DoesSocketExist(SocketName))
				{
					return SkeletalMesh->GetSocketLocation(SocketName);
				}
			}
		}

		FVector BoundsOrigin = TargetActor->GetActorLocation();
		FVector BoundsExtent = FVector::ZeroVector;
		TargetActor->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		if (BoundsExtent.Z > KINDA_SMALL_NUMBER)
		{
			const float ZOffset = FMath::Max(80.0f, BoundsExtent.Z * 0.50f);
			return BoundsOrigin + FVector(0.0f, 0.0f, ZOffset);
		}

		return TargetActor->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f);
	}

	bool IsActorAliveForLockOn(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return false;
		}

		if (Actor->IsHidden())
		{
			return false;
		}

		if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
		{
			if (RootPrimitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				return false;
			}
		}

		static const FName DeadTags[] =
		{
			FName(TEXT("Dead")),
			FName(TEXT("Dying")),
			FName(TEXT("Corpse")),
			FName(TEXT("KnockedOut")),
			FName(TEXT("Eliminated")),
			FName(TEXT("Death")),
			FName(TEXT("Ragdoll")),
			FName(TEXT("DeadState")),
			FName(TEXT("Status.Dead")),
			FName(TEXT("State.Dead"))
		};
		for (const FName& DeadTag : DeadTags)
		{
			if (Actor->ActorHasTag(DeadTag))
			{
				return false;
			}
		}

		bool bIsDead = false;
		const bool bDeadFromProperty =
			ReadBoolPropertyByNames(Actor,
				{
					TEXT("bIsDead"),
					TEXT("IsDead"),
					TEXT("bDead"),
					TEXT("Dead"),
					TEXT("bDied"),
					TEXT("bIsDying"),
					TEXT("IsDying"),
					TEXT("bDeathState"),
					// ALS / AGLS patterns
					TEXT("RagdollStateC"),
					TEXT("bRagdollState"),
					TEXT("bIsRagdoll"),
					TEXT("IsRagdollC"),
					TEXT("DeadC"),
					TEXT("bDeadC"),
					TEXT("IsDeadC"),
					TEXT("bIsDeadC"),
					TEXT("IsDeathC"),     // AAGLS_ZombieCharacterCore: bool IsDeathC
					TEXT("bIsDeathC"),
					TEXT("CharacterDead"),
					TEXT("bCharacterDead"),
					TEXT("DeathAnimStarted"),
					TEXT("bDeathAnimStarted")
				},
				bIsDead);
		if (bDeadFromProperty && bIsDead)
		{
			return false;
		}

		if (CallFunctionGetBool(Actor,
			{
				TEXT("GetIsDeadState"),
				TEXT("IsDead"),
				TEXT("GetIsDead"),
				TEXT("Get_DeadState"),
				TEXT("GetDeadState"),
				TEXT("GetCharacterIsDead"),
				TEXT("Get_Character_Is_Dead"),
				TEXT("GetDeadC"),
				TEXT("GetRagdollState"),
				TEXT("GetRagdollStateC"),
				TEXT("IsRagdolling")
			},
			bIsDead) && bIsDead)
		{
			return false;
		}

		bool bIsAlive = true;
		const bool bAliveFromProperty =
			ReadBoolPropertyByNames(Actor,
				{
					TEXT("bIsAlive"),
					TEXT("IsAlive"),
					TEXT("bAlive"),
					TEXT("Alive"),
					TEXT("bCharacterIsAlive"),
					TEXT("CharacterIsAlive")
				},
				bIsAlive);
		const bool bAliveFromFunction =
			CallFunctionGetBool(Actor,
				{
					TEXT("IsAlive"),
					TEXT("GetIsAlive"),
					TEXT("Get_Character_Is_Alive"),
					TEXT("GetCharacterIsAlive")
				},
				bIsAlive);
		if ((bAliveFromProperty || bAliveFromFunction) && !bIsAlive)
		{
			return false;
		}

	float HealthValue = 0.0f;
	const bool bHasHealthByFunction =
		CallFunctionGetFloat(Actor,
				{
					TEXT("GetCurrentHealth"),
					TEXT("GetHealth"),
					TEXT("GetHP"),
					TEXT("GetHitPoint"),
					TEXT("GetHitPoints")
				},
				HealthValue);
	const bool bHasHealthByProperty =
		!bHasHealthByFunction &&
		ReadFloatPropertyByNames(Actor,
			{
				TEXT("CurrentHealth"),
				TEXT("CurrentHealthPoints"),  // AAGLS_ZombieCharacterCore: float CurrentHealthPoints
				TEXT("Health"),
				TEXT("HP"),
				TEXT("HitPoint"),
				TEXT("HitPoints")
			},
			HealthValue);

	float MaxHealthValue = 0.0f;
	const bool bHasMaxHealthByFunction =
		CallFunctionGetFloat(Actor,
			{
				TEXT("GetMaxHealth"),
				TEXT("GetMaxHP"),
				TEXT("GetMaxHitPoint"),
				TEXT("GetMaxHitPoints")
			},
			MaxHealthValue);
	const bool bHasMaxHealthByProperty =
		!bHasMaxHealthByFunction &&
		ReadFloatPropertyByNames(Actor,
			{
				TEXT("MaxHealth"),
				TEXT("MaxHP"),
				TEXT("MaxHitPoint"),
				TEXT("MaxHitPoints")
			},
			MaxHealthValue);
	const bool bHasMaxHealth = bHasMaxHealthByFunction || bHasMaxHealthByProperty;
	const bool bHasHealth = bHasHealthByFunction || bHasHealthByProperty;
	const bool bHealthIndicatesZero = bHasHealth && (HealthValue <= KINDA_SMALL_NUMBER);
	if (bHealthIndicatesZero && (bHasMaxHealth || bAliveFromProperty || bAliveFromFunction || bDeadFromProperty))
	{
		return false;
	}

	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		bool bComponentDead = false;
		if (ReadBoolPropertyByNames(Component,
			{
				TEXT("bIsDead"),
				TEXT("IsDead"),
				TEXT("bDead"),
				TEXT("Dead"),
				TEXT("bIsDying"),
				TEXT("IsDying")
			},
			bComponentDead) && bComponentDead)
		{
			return false;
		}

		float ComponentHealth = 0.0f;
		const bool bHasComponentHealthByFunction =
			CallFunctionGetFloat(Component,
				{
					TEXT("GetCurrentHealth"),
					TEXT("GetHealth"),
					TEXT("GetHP")
				},
				ComponentHealth);
		const bool bHasComponentHealthByProperty =
			!bHasComponentHealthByFunction &&
			ReadFloatPropertyByNames(Component,
				{
					TEXT("CurrentHealth"),
					TEXT("Health"),
					TEXT("HP")
				},
				ComponentHealth);

		float ComponentMaxHealth = 0.0f;
		const bool bHasComponentMaxHealthByFunction =
			CallFunctionGetFloat(Component,
				{
					TEXT("GetMaxHealth"),
					TEXT("GetMaxHP")
				},
				ComponentMaxHealth);
		const bool bHasComponentMaxHealthByProperty =
			!bHasComponentMaxHealthByFunction &&
			ReadFloatPropertyByNames(Component,
				{
					TEXT("MaxHealth"),
					TEXT("MaxHP")
				},
				ComponentMaxHealth);

		if ((bHasComponentHealthByFunction || bHasComponentHealthByProperty) &&
			ComponentHealth <= KINDA_SMALL_NUMBER &&
			(bHasComponentMaxHealthByFunction || bHasComponentMaxHealthByProperty))
		{
			return false;
		}
	}

	// ALS/AGLS death detection: DisableMovement() sets MOVE_None; ragdoll enables mesh physics.
	if (const ACharacter* AsCharacter = Cast<ACharacter>(Actor))
	{
		const UCharacterMovementComponent* MoveComp = AsCharacter->GetCharacterMovement();
		const USkeletalMeshComponent* MeshComp = AsCharacter->GetMesh();
		const bool bMovementDisabled = IsValid(MoveComp) && (MoveComp->MovementMode == MOVE_None);
		const bool bMeshRagdoll = IsValid(MeshComp) && MeshComp->IsSimulatingPhysics();
		if (bMovementDisabled && bMeshRagdoll)
		{
			return false;
		}
	}

	return true;
}

	AActor* ResolveDamageInstigatorActor(const AActor* SelfActor, AController* EventInstigator, AActor* DamageCauser)
	{
		auto IsValidAttacker = [SelfActor](AActor* Candidate)
		{
			return IsValid(Candidate) && Candidate != SelfActor && !Candidate->IsActorBeingDestroyed();
		};

		if (IsValidAttacker(DamageCauser))
		{
			if (APawn* DamagePawn = Cast<APawn>(DamageCauser))
			{
				return DamagePawn;
			}

			if (AActor* DamageOwner = DamageCauser->GetOwner(); IsValidAttacker(DamageOwner))
			{
				if (APawn* OwnerPawn = Cast<APawn>(DamageOwner))
				{
					return OwnerPawn;
				}
				return DamageOwner;
			}

			if (AController* DamageInstigatorController = DamageCauser->GetInstigatorController())
			{
				if (APawn* InstigatorPawn = DamageInstigatorController->GetPawn(); IsValidAttacker(InstigatorPawn))
				{
					return InstigatorPawn;
				}
			}

			return DamageCauser;
		}

		if (IsValid(EventInstigator))
		{
			if (APawn* InstigatorPawn = EventInstigator->GetPawn(); IsValidAttacker(InstigatorPawn))
			{
				return InstigatorPawn;
			}
		}

		return nullptr;
	}
}


// Sets default values
AGAS_MainCharacterCpp::AGAS_MainCharacterCpp()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComp");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	Attributes = CreateDefaultSubobject<UIWALS_BaseAttributeSet>("Attributes");

}

float AGAS_MainCharacterCpp::GetTargetSpeedWithStrafeC(FVector SpeedVector)
{

	if (StrafeSpeedMapCurveC)
	{
		float StrafeSpeedMap = StrafeSpeedMapCurveC->GetFloatValue(abs(UKismetAnimationLibrary::CalculateDirection(this->GetCharacterMovement()->Velocity,GetActorRotation())));
		if (StrafeSpeedMap < 1.0)
		{
			return UKismetMathLibrary::MapRangeClamped(StrafeSpeedMap, 0.0, 1.0, SpeedVector.X, SpeedVector.Y);
		}
		else
		{
			return UKismetMathLibrary::MapRangeClamped(StrafeSpeedMap, 1.0, 2.0, SpeedVector.Y, SpeedVector.Z);
		}
	}
	return 300.0;
}

float AGAS_MainCharacterCpp::GetMappedSpeedC(float SpeedScale)
{
	const float LocWalkSpeed = GetTargetSpeedWithStrafeC(CurrentMovementSettingsC.WalkSpeed);
	const float LocRunSpeed = GetTargetSpeedWithStrafeC(CurrentMovementSettingsC.RunSpeed);
	const float LocSprintSpeed = GetTargetSpeedWithStrafeC(CurrentMovementSettingsC.SprintSpeed);

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

void AGAS_MainCharacterCpp::SmoothCharacterRotationC(FRotator TargetRotation, float ActorInterpSpeed)
{
	
	FRotator NewRotation = UKismetMathLibrary::RInterpTo(GetActorRotation(), TargetRotation, GetWorld()->DeltaTimeSeconds, ActorInterpSpeed);
	const FQuat TargetQuat = UKismetMathLibrary::Conv_RotatorToQuaternion(NewRotation);
	SetActorRotation(TargetQuat, ETeleportType::None);
}

float AGAS_MainCharacterCpp::CalculateGroundedRotationSpeedC(float Scale, FVector2D YawScaleRange)
{
	if (IsValid(CurrentMovementSettingsC.RotationRateCurve) == false) { return 15.0; }

	const float InterpRate = CurrentMovementSettingsC.RotationRateCurve->GetFloatValue(GetMappedSpeedC());
	const float YawScale = UKismetMathLibrary::MapRangeClamped(AimYawRateC, 0.0, 300, YawScaleRange.X, YawScaleRange.Y);
	return InterpRate * YawScale * UKismetMathLibrary::SelectFloat(1.2, 1.0, IsStartedMovementOnTargetC);
}

void AGAS_MainCharacterCpp::GetControlVectorsC(FVector& ForwardVector,FVector& RightVector)
{
	ForwardVector = UKismetMathLibrary::GetForwardVector(FRotator(0.0, GetControlRotation().Yaw, 0.0));
	RightVector = UKismetMathLibrary::GetRightVector(FRotator(0.0, GetControlRotation().Yaw, 0.0));
}

FVector AGAS_MainCharacterCpp::GetCapsuleBaseLocationC(float ZOffset)
{
	if (GetCapsuleComponent())
	{
		const UCapsuleComponent* CC = GetCapsuleComponent();
		return CC->GetComponentLocation() - (CC->GetUpVector() * (CC->GetScaledCapsuleHalfHeight() + ZOffset));
	}
	return GetActorLocation();
}

FVector AGAS_MainCharacterCpp::FloorToCapsuleLocationC(FVector BaseLocation, float ZOffset, bool ByDefSize)
{
	return BaseLocation + FVector(0, 0, (UKismetMathLibrary::SelectFloat(DefCapsuleSizeC.Y, GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), ByDefSize) + ZOffset));
}

// Called when the game starts or when spawned
void AGAS_MainCharacterCpp::BeginPlay()
{
	Super::BeginPlay();

	DefCapsuleSizeC.X = GetCapsuleComponent()->GetScaledCapsuleRadius();
	DefCapsuleSizeC.Y = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	bDisableZombieGrabAbilities = true;
	ZombieGrabAbilityCancelElapsed = 0.0f;
	LastCombatFuryRecentDamageInstigator = nullptr;
	LastCombatFuryRecentDamageInstigatorTime = -1.0f;
	bWasInActionMeleeTag = HasActionMeleeTag(AbilitySystemComponent);
	bLastMeleeLockOnDetectionValid = false;
	bWasMeleeLockOnSlotEquipped = IsMeleeLockOnSlotEquipped();
	bWasMiddleMousePressed = false;
	PreviousMouseWheelAxisValue = 0.0f;
	NativeSoftTargetElapsed = 0.0f;
	NativeSoftAttackCorrectionRemaining = 0.0f;
	bWasNativeSoftAttackTagActive = false;
	bNativeHardLockActive = false;
	NativeCombatFuryLockOnTarget = nullptr;
	NativeCombatFurySoftTarget = nullptr;
	ActiveCombatFuryTargetWidget = nullptr;
	ActiveCombatFuryTargetActor = nullptr;
	PreviousCombatFuryTargetActor = nullptr;
	LastCandidateTargets.Reset();
	CachedLockOnSpringArm = nullptr;
	CachedLockOnCamera = nullptr;
	bHasStoredHardLockCameraDefaults = false;
	DefaultSpringArmLength = 0.0f;
	DefaultSpringArmSocketOffset = FVector::ZeroVector;
	DefaultSpringArmTargetOffset = FVector::ZeroVector;
	DefaultCameraRelativeLocation = FVector::ZeroVector;
	DefaultCameraRelativeRotation = FRotator::ZeroRotator;
	StoreDefaultCameraValues();
	EnsureRaidCompassWidget();
	EnforceNoZombieGrabAbilities();

	LockOnSweepTick.Target = this;
	LockOnSweepTick.TickGroup = TG_PostUpdateWork;
	LockOnSweepTick.bCanEverTick = true;
	LockOnSweepTick.bStartWithTickEnabled = true;
	LockOnSweepTick.RegisterTickFunction(GetLevel());
}

void AGAS_MainCharacterCpp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LockOnSweepTick.UnRegisterTickFunction();
	Super::EndPlay(EndPlayReason);
}


// Called every frame
void AGAS_MainCharacterCpp::Tick(float DeltaTime)
{
	// Update Essential Values
	float SafeDelta = GetWorld()->DeltaTimeSeconds;
	if (SafeDelta == 0.0) { SafeDelta = 0.01; }

	AccelerationC = (GetVelocity() - PreviousVelocityC) / SafeDelta;
	MovementSpeedDifferenceC = FVector(GetVelocity().X, GetVelocity().Y, 0.0).Length() - FVector(PreviousVelocityC.X, PreviousVelocityC.Y, 0.0).Length();

	SpeedC = FVector(GetVelocity().X, GetVelocity().Y, 0.0).Length();
	if (SpeedC > 1.0) { IsMovingC = true; }
	else { IsMovingC = false; }

	FVector AccelerationXY = this->GetCharacterMovement()->GetCurrentAcceleration();
	AccelerationXY.Z = 0.0;
	MovementInputAmountC = AccelerationXY.Length() / this->GetCharacterMovement()->GetMaxAcceleration();
	if (MovementInputAmountC > 0.0) { HasMovementInputC = true; }
	else { HasMovementInputC = false; }

	AimYawRateC = abs((GetControlRotation().Yaw - PreviousAimYawC) / SafeDelta);

	IsSwimmingC = this->GetCharacterMovement()->IsSwimming();

	//Update Base Rotation Values
	if (HasMovementInputC == true)
	{
		LastMovementInputRotationC = UKismetMathLibrary::MakeRotFromX(this->GetCharacterMovement()->GetCurrentAcceleration());
	}
	if (IsMovingC == true)
	{
		LastVelocityRotationC = UKismetMathLibrary::MakeRotFromX(GetVelocity());
	}

	//Update Cached Variables
	PreviousVelocityC = GetVelocity();
	PreviousAimYawC = GetControlRotation().Yaw;

	/* Calculate Floor Velocity */
	if (this->GetCharacterMovement()->CurrentFloor.bBlockingHit == true && this->GetCharacterMovement()->CurrentFloor.HitResult.Location.IsNearlyZero() == false)
	{
		FloorVelocityC = (this->GetCharacterMovement()->CurrentFloor.HitResult.Location - PrevFloorLocation) / DeltaTime;
		FloorVelocityC = FloorVelocityC - FVector(this->GetCharacterMovement()->Velocity.X, this->GetCharacterMovement()->Velocity.Y, 0.0);
		PrevFloorLocation = this->GetCharacterMovement()->CurrentFloor.HitResult.Location;
	}
	else
	{
		FloorVelocityC = FVector(0, 0, 0);
		PrevFloorLocation = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	/* Experimental function. Improves the behavior of the capsule in a non-inertial reference frame (the floor moves relative to the world space) */
	if (CorrectNonInertialFloor)
	{
		if (this->GetCharacterMovement()->IsFalling() == true)
		{
			if (AddedFloorForce == false)
			{
				LaunchCharacter(FVector(PrevFloorVelocityC.X, PrevFloorVelocityC.Y, 0.0), false, false);
				AddedFloorForce = true;
			}
		}
		else
		{
			PrevFloorVelocityC = FloorVelocityC;
			AddedFloorForce = false;
		}
	}

	// Overlay Unequip Events
	if (OverlayStateLeavingStarted == true)
	{
		DoWhenOverlayLeaving(DeltaTime);
		CanUpdateFromDesiredOverlay = true;
	}
	if (OverlayStateLeavingStarted == false && CanUpdateFromDesiredOverlay == true)
	{
		CanUpdateFromDesiredOverlay = false;
		OverlayLeavingFinshed();
	}

	if (bDisableZombieGrabAbilities)
	{
		ZombieGrabEnforceTimer += DeltaTime;
		if (ZombieGrabEnforceTimer >= 0.5f)
		{
			ZombieGrabEnforceTimer = 0.0f;
			EnforceNoZombieGrabAbilities();
		}
	}

	HandleMiddleMouseLockOnFallback();

	// Reset per-tick cache so IsMeleeLockOnContextActive evaluates slot state once this frame.
	bMeleeContextCacheValid = false;
	UpdateNativeSoftTargeting(DeltaTime);

	Super::Tick(DeltaTime);

	UpdateTargetIconVisibility();
	UpdateLockOnSpringArmCollision(DeltaTime);
	UpdateLockOnCameraTransition(DeltaTime);

	// 하드/소프트 락온 대상의 뷰포트 좌표 갱신 → UMG 오프스크린 화살표용 (Ghost of Tsushima 스타일)
	{
		NativeTargetScreenPosition = FVector2D::ZeroVector;
		bNativeTargetIsOnScreen = false;

		if (AActor* ScreenTarget = GetCurrentNativeTarget())
		{
			if (IsLockOnTargetUsable(ScreenTarget))
			{
				if (const APlayerController* PC = Cast<APlayerController>(GetController()))
				{
					FVector2D ScreenPos;
					if (PC->ProjectWorldLocationToScreen(ResolveLockOnAimPoint(ScreenTarget), ScreenPos, true))
					{
						int32 VX = 0, VY = 0;
						PC->GetViewportSize(VX, VY);
						bNativeTargetIsOnScreen =
							ScreenPos.X >= 0.f && ScreenPos.X <= static_cast<float>(VX) &&
							ScreenPos.Y >= 0.f && ScreenPos.Y <= static_cast<float>(VY);
						NativeTargetScreenPosition = ScreenPos;
					}
				}
			}
		}
	}

}

float AGAS_MainCharacterCpp::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float DamageForTracking = AppliedDamage > 0.0f ? AppliedDamage : DamageAmount;
	RecordRecentDamageInstigator(EventInstigator, DamageCauser, DamageForTracking);
	return AppliedDamage;
}

bool AGAS_MainCharacterCpp::IsMeleeLockOnSlotEquipped() const
{
	if (!bAutoLockOnWhenMeleeSlotEquipped)
	{
		return false;
	}

	int32 CurrentSlotIndex = INDEX_NONE;
	bool bHasCurrentSlotIndex = false;
	FString SlotSignalSource = TEXT("None");

	int32 CurrentWeaponIndex = INDEX_NONE;
	bool bHasCurrentWeaponIndex = false;
	FString WeaponIndexSignalSource = TEXT("None");
	int32 OverlayStateIndex = INDEX_NONE;
	bool bHasOverlayStateIndex = false;
	FString OverlaySignalSource = TEXT("None");

	UObject* CurrentWeaponObject = nullptr;

	auto ProbeSignalsFromObject = [&](UObject* SourceObject, const FString& SourceName)
	{
		if (!IsValid(SourceObject))
		{
			return;
		}

		int32 ProbedSlotIndex = INDEX_NONE;
		bool bFoundSlotIndex =
			CallFunctionGetInt(SourceObject,
				{
					TEXT("GetCurrentSlotIndex"),
					TEXT("GetCurrentWeaponSlotIndex"),
					TEXT("GetCurrentWeaponSlot"),
					TEXT("GetWeaponSlotIndex"),
					TEXT("GetCurrentSlot"),
					TEXT("GetEquippedSlotIndex"),
					TEXT("GetEquippedWeaponSlot"),
					TEXT("GetWeaponSlot"),
					TEXT("GetCurrentEquipSlot"),
					TEXT("GetCurrentWeaponSelect"),
					TEXT("GetWeaponSelect"),
					TEXT("GetSelectedWeaponSlot"),
					TEXT("GetSelectedSlotIndex")
				},
				ProbedSlotIndex);
		if (!bFoundSlotIndex)
		{
			bFoundSlotIndex = ReadIntPropertyByNames(SourceObject,
				{
					TEXT("CurrentSlotIndex"),
					TEXT("CurrentWeaponSlotIndex"),
					TEXT("CurrentWeaponSlot"),
					TEXT("CurrentSlot"),
					TEXT("CurrentEquipSlotIndex"),
					TEXT("EquippedSlotIndex"),
					TEXT("EquippedWeaponSlot"),
					TEXT("WeaponSlotIndex"),
					TEXT("WeaponSlot"),
					TEXT("WeaponSelect"),
					TEXT("CurrentWeaponSelect"),
					TEXT("SelectedWeaponSlot"),
					TEXT("SelectedSlotIndex")
				},
				ProbedSlotIndex);
		}
		if (bFoundSlotIndex && !bHasCurrentSlotIndex)
		{
			CurrentSlotIndex = ProbedSlotIndex;
			bHasCurrentSlotIndex = true;
			SlotSignalSource = SourceName;
		}

		int32 ProbedWeaponIndex = INDEX_NONE;
		bool bFoundWeaponIndex =
			CallFunctionGetInt(SourceObject,
				{
					TEXT("GetCurrentWeaponIndex"),
					TEXT("GetCurrentPistolSlotIndex"),
					TEXT("GetWeaponIndex"),
					TEXT("GetCurrentWeaponTypeIndex")
				},
				ProbedWeaponIndex);
		if (!bFoundWeaponIndex)
		{
			bFoundWeaponIndex = ReadIntPropertyByNames(SourceObject,
				{
					TEXT("CurrentWeaponIndex"),
					TEXT("CurrentPistolSlotIndex"),
					TEXT("WeaponIndex"),
					TEXT("CurrentWeaponTypeIndex")
				},
				ProbedWeaponIndex);
		}
		if (bFoundWeaponIndex && !bHasCurrentWeaponIndex)
		{
			CurrentWeaponIndex = ProbedWeaponIndex;
			bHasCurrentWeaponIndex = true;
			WeaponIndexSignalSource = SourceName;
		}

		int32 ProbedOverlayState = INDEX_NONE;
		bool bFoundOverlayState =
			CallFunctionGetInt(SourceObject,
				{
					TEXT("GetOverlayState"),
					TEXT("GetOverlayStateC"),
					TEXT("GetCurrentOverlayState"),
					TEXT("GetCurrentOverlay")
				},
				ProbedOverlayState);
		if (!bFoundOverlayState)
		{
			bFoundOverlayState = ReadIntPropertyByNames(SourceObject,
				{
					TEXT("OverlayStateC"),
					TEXT("OverlayState"),
					TEXT("CurrentOverlayState"),
					TEXT("CurrentOverlay")
				},
				ProbedOverlayState);
		}
		if (bFoundOverlayState && !bHasOverlayStateIndex)
		{
			OverlayStateIndex = ProbedOverlayState;
			bHasOverlayStateIndex = true;
			OverlaySignalSource = SourceName;
		}

		if (!IsValid(CurrentWeaponObject))
		{
			CurrentWeaponObject =
				CallFunctionGetObject(SourceObject,
					{
						TEXT("GetCurrentWeapon"),
						TEXT("GetCurrentWeaponActor"),
						TEXT("GetEquippedWeapon"),
						TEXT("GetCurrentItem"),
						TEXT("GetEquippedItem"),
						TEXT("GetCurrentMeleeWeapon")
					});
			if (!CurrentWeaponObject)
			{
				CurrentWeaponObject = ReadObjectPropertyByNames(SourceObject,
					{
						TEXT("CurrentWeapon"),
						TEXT("EquippedWeapon"),
						TEXT("CurrentItem"),
						TEXT("EquippedItem"),
						TEXT("CurrentMeleeWeapon")
					});
			}
		}
	};

	ProbeSignalsFromObject(const_cast<AGAS_MainCharacterCpp*>(this), TEXT("Owner"));

	TInlineComponentArray<UActorComponent*> OwnerComponentsArray(const_cast<AGAS_MainCharacterCpp*>(this));
	for (UActorComponent* OwnedComponent : OwnerComponentsArray)
	{
		if (!IsValid(OwnedComponent))
		{
			continue;
		}

		ProbeSignalsFromObject(OwnedComponent, OwnedComponent->GetClass()->GetName());
		if (bHasCurrentSlotIndex && bHasCurrentWeaponIndex && bHasOverlayStateIndex && IsValid(CurrentWeaponObject))
		{
			break;
		}
	}

	const bool bHasValidCurrentSlotSignal = bHasCurrentSlotIndex && CurrentSlotIndex != INDEX_NONE;
	const bool bCurrentSlotMatches = bHasValidCurrentSlotSignal && MatchesConfiguredSlotIndex(CurrentSlotIndex, MeleeLockOnSlotIndex);
	const bool bWeaponIndexMatchesDirect = bHasCurrentWeaponIndex && MatchesConfiguredSlotIndex(CurrentWeaponIndex, MeleeLockOnSlotIndex);
	const bool bWeaponIndexMatchesPlusOne = bHasCurrentWeaponIndex && MatchesConfiguredSlotIndex(CurrentWeaponIndex + 1, MeleeLockOnSlotIndex);
	const bool bWeaponIndexMatchesPlusTwo = bHasCurrentWeaponIndex && MatchesConfiguredSlotIndex(CurrentWeaponIndex + 2, MeleeLockOnSlotIndex);
	const bool bWeaponIndexSuggestsMeleeSlot = bWeaponIndexMatchesDirect || bWeaponIndexMatchesPlusOne || bWeaponIndexMatchesPlusTwo;
	const bool bSlotMatches = bCurrentSlotMatches || bWeaponIndexSuggestsMeleeSlot;
	const bool bWeaponLooksMelee = DoesWeaponLookMelee(CurrentWeaponObject);
	const bool bAttachedEquipmentLooksMelee = DoesAttachedEquipmentLookMelee(this);
	const bool bHasAuthoritativeSlotSignal = bHasValidCurrentSlotSignal;
	const bool bHasSlotSignal = bHasAuthoritativeSlotSignal || bHasCurrentWeaponIndex;
	const bool bHasDetectionSignal = bHasSlotSignal || IsValid(CurrentWeaponObject);
	bLastMeleeLockOnDetectionValid = bHasDetectionSignal;
	const auto LooksRangedByString = [](const FString& SourceText) -> bool
	{
		return
			SourceText.Contains(TEXT("Rifle"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("Gun"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("Pistol"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("Shotgun"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("SMG"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("Sniper"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("Launcher"), ESearchCase::IgnoreCase) ||
			SourceText.Contains(TEXT("AR"), ESearchCase::IgnoreCase);
	};
	const FString CurrentWeaponName =
		IsValid(CurrentWeaponObject)
		? (CurrentWeaponObject->GetName() + TEXT(" ") + CurrentWeaponObject->GetClass()->GetName() + TEXT(" ") + CurrentWeaponObject->GetPathName())
		: FString();
	const bool bWeaponLooksRanged = LooksRangedByString(CurrentWeaponName);

	// CALS_OverlayState enum (HelpfulFunctions/Public/ALS_StructuresAndEnumsCpp.h):
	// Rifle=5, Pistol1H=6, Pistol2H=7, Bow=8, Axe=14, Knife=15
	const bool bOverlayIndicatesRanged =
		bHasOverlayStateIndex &&
		(OverlayStateIndex == 5 || OverlayStateIndex == 6 || OverlayStateIndex == 7 || OverlayStateIndex == 8);
	const bool bOverlayIndicatesMelee =
		bHasOverlayStateIndex &&
		(OverlayStateIndex == 14 || OverlayStateIndex == 15);

	bool bMeleeSlotEquipped = false;
	if (bHasAuthoritativeSlotSignal)
	{
		// Trust explicit slot signal first when available.
		bMeleeSlotEquipped = bCurrentSlotMatches;
	}
	else
	{
		// Slot signal is missing in this project runtime, so use explicit overlay state as the primary fallback.
		// This keeps rifle/pistol/bow states off while allowing melee overlays.
		if (bOverlayIndicatesRanged)
		{
			bMeleeSlotEquipped = false;
		}
		else if (bOverlayIndicatesMelee)
		{
			bMeleeSlotEquipped = true;
		}
		else
		{
			// Secondary fallback only when overlay is unavailable/neutral:
			// require both melee-looking weapon and index pattern that maps to configured melee slot.
			bMeleeSlotEquipped =
				bWeaponLooksMelee &&
				!bWeaponLooksRanged &&
				bWeaponIndexSuggestsMeleeSlot;
		}
	}

	// Prefer slot 2 gate; if slot signal is unavailable, overlay-based fallback controls leakage.
	return bMeleeSlotEquipped;
}

void AGAS_MainCharacterCpp::HandleMiddleMouseLockOnFallback()
{
	if (!bEnableMiddleMouseLockOnFallback)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	const bool bMiddleMouseDownNow = PC->IsInputKeyDown(EKeys::MiddleMouseButton);
	const bool bPressedByEdge = bMiddleMouseDownNow && !bWasMiddleMousePressed;
	const bool bPressedByEvent = PC->WasInputKeyJustPressed(EKeys::MiddleMouseButton);
	const bool bPressedThisFrame = bPressedByEdge || (!bMiddleMouseDownNow && bPressedByEvent);
	const float WheelAxisFromKey = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	const float WheelAxisValue =
		(FMath::Abs(WheelAxisFromKey) > FMath::Abs(PreviousMouseWheelAxisValue))
		? WheelAxisFromKey
		: PreviousMouseWheelAxisValue;

	// 수평 마우스 플릭 — NativeMouseFlickWindowSeconds 내 누적 에너지 방식 (쫀득한 조작감)
	const float MouseXDelta    = PC->GetInputAnalogKeyState(EKeys::MouseX);
	const float FlickThreshold = FMath::Max(1.0f, NativeMouseFlickThreshold);
	const float FlickWindow    = FMath::Max(0.01f, NativeMouseFlickWindowSeconds);
	{
		const float FrameDT = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
		// 방향 전환 또는 윈도우 만료 시 누적 초기화
		if (NativeMouseFlickAccumulator != 0.0f)
		{
			const bool bSignChanged = (MouseXDelta * NativeMouseFlickAccumulator < 0.0f);
			if (bSignChanged || NativeMouseFlickWindowElapsed >= FlickWindow)
			{
				NativeMouseFlickAccumulator   = 0.0f;
				NativeMouseFlickWindowElapsed = 0.0f;
			}
		}
		NativeMouseFlickAccumulator   += MouseXDelta;
		NativeMouseFlickWindowElapsed += FrameDT;
	}
	const bool bFlickRight     = (NativeMouseFlickAccumulator >  FlickThreshold);
	const bool bFlickLeft      = (NativeMouseFlickAccumulator < -FlickThreshold);
	const bool bFlickThisFrame = bFlickRight || bFlickLeft;
	// 플릭 발동 후 누적 초기화 (같은 윈도우 내 중복 발동 방지)
	if (bFlickThisFrame)
	{
		NativeMouseFlickAccumulator   = 0.0f;
		NativeMouseFlickWindowElapsed = 0.0f;
	}

	const auto CommitInputState = [this, bMiddleMouseDownNow]()
	{
		bWasMiddleMousePressed = bMiddleMouseDownNow;
		PreviousMouseWheelAxisValue = 0.0f;
	};

	if (!bPressedThisFrame && !bFlickThisFrame)
	{
		CommitInputState();
		return;
	}

	const UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;

	const bool bMeleeContextActive = IsMeleeLockOnContextActive();

	if (bMiddleMouseLockOnRequiresMeleeSlot && !bMeleeContextActive)
	{
		CommitInputState();
		return;
	}

	if (bNativeHardLockActive && !IsLockOnTargetUsable(NativeCombatFuryLockOnTarget.Get()))
	{
		ClearNativeCombatFuryLockOnTarget();
	}

	const bool bHasHardLockOn = bNativeHardLockActive && IsLockOnTargetUsable(NativeCombatFuryLockOnTarget.Get());

	// 마우스 플릭으로 하드락 타겟 전환 (휠 스크롤 대체)
	if (bFlickThisFrame && bHasHardLockOn)
	{
		const float TimeSinceSwitch = CurrentTime - LastMouseFlickSwitchTimeSeconds;
		const float FlickCooldown   = FMath::Max(0.0f, NativeMouseFlickCooldownSeconds);
		const float DirLockDuration = FMath::Max(FlickCooldown, NativeFlickDirectionalLockSeconds);
		const int32 FlickDir = bFlickRight ? 1 : -1;

		// 방향성 잠금: DirectionalLockSeconds 내 반대 방향(Back-dash)은 차단
		// (쿨다운 만료 직후 남은 반동 움직임이 역방향 전환을 유발하는 현상 방지)
		const bool bOppositeBlocked =
			(TimeSinceSwitch < DirLockDuration) &&
			(LastFlickLockedDirection != 0) &&
			(FlickDir == -LastFlickLockedDirection);

		if (!bOppositeBlocked && TimeSinceSwitch >= FlickCooldown)
		{
			FString SwitchReason;
			const bool bSwitched = SwitchNativeTargetByDirection(FlickDir, SwitchReason);
			if (bSwitched)
			{
				LastMouseFlickSwitchTimeSeconds = CurrentTime;
				LastFlickLockedDirection = FlickDir;
			}
			else if (SwitchReason == TEXT("NoOtherCandidates"))
			{
				ShowNativeLockOnModeFeedback(this, TEXT("NO TARGET"), FColor::Yellow, 0.45f);
			}
		}
	}

	if (!bPressedThisFrame)
	{
		CommitInputState();
		return;
	}

	const float ToggleCooldown = FMath::Max(0.05f, MiddleMouseToggleCooldownSeconds);
	if ((CurrentTime - LastMiddleMouseToggleTimeSeconds) < ToggleCooldown)
	{
		CommitInputState();
		return;
	}
	LastMiddleMouseToggleTimeSeconds = CurrentTime;

	if (bHasHardLockOn)
	{
		ClearNativeCombatFuryLockOnTarget();
		ShowNativeLockOnModeFeedback(this, TEXT("SOFT LOCK"), FColor::Yellow);
		CommitInputState();
		return;
	}

	AActor* InitialTarget =
		IsLockOnTargetUsable(NativeCombatFurySoftTarget.Get())
		? NativeCombatFurySoftTarget.Get()
		: GetNearestCombatFuryEnemy(ResolveEffectiveLockOnMaxDistance());
	const bool bLocked = SetNativeCombatFuryLockOnTarget(InitialTarget, false);
	const FString LockFeedback =
		bLocked
		? TEXT("HARD LOCK")
		: TEXT("NO TARGET");
	ShowNativeLockOnModeFeedback(this, LockFeedback, bLocked ? FColor::Cyan : FColor::Red);
	CommitInputState();
}

void AGAS_MainCharacterCpp::EnforceNoZombieGrabAbilities()
{
	if (!bDisableZombieGrabAbilities || !IsValid(AbilitySystemComponent))
	{
		return;
	}

	FGameplayTagContainer TagsToCancel;
	const FGameplayTag PairedTag = GetPairedZombieAttackTag();
	const FGameplayTag GrabTag = GetZombieGrabVictimTag();
	if (PairedTag.IsValid())
	{
		TagsToCancel.AddTag(PairedTag);
	}
	if (GrabTag.IsValid())
	{
		TagsToCancel.AddTag(GrabTag);
	}

	if (TagsToCancel.IsEmpty())
	{
		// Keep running class-based cancellation below even if gameplay tags are unavailable.
	}

	if (!TagsToCancel.IsEmpty())
	{
		AbilitySystemComponent->CancelAbilities(&TagsToCancel, nullptr, nullptr);
		for (const FGameplayTag& Tag : TagsToCancel)
		{
			if (AbilitySystemComponent->HasMatchingGameplayTag(Tag))
			{
				AbilitySystemComponent->RemoveLooseGameplayTag(Tag);
			}
		}
	}

	TArray<FGameplayAbilitySpecHandle> HandlesToCancel;
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		bool bShouldCancel = false;

		if (IsValid(Spec.Ability))
		{
			bShouldCancel = IsZombieGrabAbilityName(Spec.Ability->GetName()) || IsZombieGrabAbilityName(Spec.Ability->GetClass()->GetName());
		}

		if (!bShouldCancel)
		{
			if (const UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
			{
				bShouldCancel = IsZombieGrabAbilityName(PrimaryInstance->GetName()) || IsZombieGrabAbilityName(PrimaryInstance->GetClass()->GetName());
			}
		}

		if (bShouldCancel)
		{
			HandlesToCancel.Add(Spec.Handle);
		}
	}

	const bool bCanClearGrantedAbility = HasAuthority();
	for (const FGameplayAbilitySpecHandle Handle : HandlesToCancel)
	{
		AbilitySystemComponent->CancelAbilityHandle(Handle);
		if (bCanClearGrantedAbility)
		{
			AbilitySystemComponent->ClearAbility(Handle);
		}
	}
}

AActor* AGAS_MainCharacterCpp::GetNearestCombatFuryEnemy(float MaxDistance) const
{
	TArray<AActor*> RankedCandidates;
	BuildCombatFuryLockOnCandidates(RankedCandidates, MaxDistance);
	return RankedCandidates.Num() > 0 ? RankedCandidates[0] : nullptr;
}

float AGAS_MainCharacterCpp::ResolveEffectiveLockOnMaxDistance(float MaxDistanceOverride) const
{
	if (MaxDistanceOverride > 0.0f)
	{
		return MaxDistanceOverride;
	}

	if (NativeSoftTargetMaxDistance > 0.0f)
	{
		return NativeSoftTargetMaxDistance;
	}

	if (CombatFuryLockOnMaxAcquireDistance > 0.0f)
	{
		return CombatFuryLockOnMaxAcquireDistance;
	}

	// Melee lock-on should not be unbounded by default.
	return 3000.0f;
}

bool AGAS_MainCharacterCpp::IsLockOnTargetUsable(AActor* Candidate, bool bApplyHardLockHysteresis) const
{
	if (!IsMeleeLockOnContextActive())
	{
		return false;
	}

	if (!IsValid(Candidate) || Candidate == this || Candidate->IsActorBeingDestroyed() || !IsActorAliveForLockOn(Candidate))
	{
		return false;
	}

	if (Candidate->IsHidden())
	{
		return false;
	}

	if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Candidate->GetRootComponent()))
	{
		if (RootPrimitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			return false;
		}
	}

	float EffectiveMaxDistance = ResolveEffectiveLockOnMaxDistance();
	if (bApplyHardLockHysteresis && bNativeHardLockActive && NativeHardLockBreakDistanceMultiplier > 1.0f)
	{
		EffectiveMaxDistance *= NativeHardLockBreakDistanceMultiplier;
	}
	if (EffectiveMaxDistance > 0.0f)
	{
		const float DistanceSq = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSq > FMath::Square(EffectiveMaxDistance))
		{
			return false;
		}
	}

	if (bPreferLineOfSightLockOnTarget && !HasLineOfSightToLockOnCandidate(Candidate))
	{
		return false;
	}

	return true;
}

bool AGAS_MainCharacterCpp::IsMeleeLockOnContextActive() const
{
	if (!bNativeSoftTargetRequiresMeleeContext)
	{
		return true;
	}

	// Use the per-tick cache to avoid calling IsMeleeLockOnSlotEquipped() O(N_candidates) times.
	// Cache is reset at the start of each native lock-on Tick block.
	if (bMeleeContextCacheValid)
	{
		return bCachedMeleeContextThisTick;
	}

	const bool bResult = IsMeleeLockOnSlotEquipped();
	bCachedMeleeContextThisTick = bResult;
	bMeleeContextCacheValid = true;
	return bResult;
}

USpringArmComponent* AGAS_MainCharacterCpp::ResolveLockOnSpringArm() const
{
	if (USpringArmComponent* Cached = CachedLockOnSpringArm.Get())
	{
		return Cached;
	}

	TArray<USpringArmComponent*> SpringArms;
	GetComponents<USpringArmComponent>(SpringArms);
	USpringArmComponent* Selected = nullptr;
	for (USpringArmComponent* Candidate : SpringArms)
	{
		if (!IsValid(Candidate))
		{
			continue;
		}

		const FString Name = Candidate->GetName();
		if (Name.Contains(TEXT("Camera"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Boom"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Spring"), ESearchCase::IgnoreCase))
		{
			Selected = Candidate;
			break;
		}
	}

	if (!Selected && SpringArms.Num() > 0)
	{
		Selected = SpringArms[0];
	}

	CachedLockOnSpringArm = Selected;
	return Selected;
}

UCameraComponent* AGAS_MainCharacterCpp::ResolveLockOnCamera() const
{
	if (UCameraComponent* Cached = CachedLockOnCamera.Get())
	{
		return Cached;
	}

	TArray<UCameraComponent*> Cameras;
	GetComponents<UCameraComponent>(Cameras);
	UCameraComponent* Selected = nullptr;
	for (UCameraComponent* Candidate : Cameras)
	{
		if (!IsValid(Candidate))
		{
			continue;
		}

		const FString Name = Candidate->GetName();
		if (Name.Contains(TEXT("Follow"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Main"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Camera"), ESearchCase::IgnoreCase))
		{
			Selected = Candidate;
			break;
		}
	}

	if (!Selected && Cameras.Num() > 0)
	{
		Selected = Cameras[0];
	}

	CachedLockOnCamera = Selected;
	return Selected;
}

void AGAS_MainCharacterCpp::StoreDefaultCameraValues()
{
	if (bHasStoredHardLockCameraDefaults)
	{
		return;
	}

	if (USpringArmComponent* SpringArm = ResolveLockOnSpringArm())
	{
		DefaultSpringArmLength = SpringArm->TargetArmLength;
		DefaultSpringArmSocketOffset = SpringArm->SocketOffset;
		DefaultSpringArmTargetOffset = SpringArm->TargetOffset;
	}

	if (UCameraComponent* CameraComp = ResolveLockOnCamera())
	{
		DefaultCameraRelativeLocation = CameraComp->GetRelativeLocation();
		DefaultCameraRelativeRotation = CameraComp->GetRelativeRotation();
	}

	bHasStoredHardLockCameraDefaults = true;
}

void AGAS_MainCharacterCpp::ResetHardLockCamera()
{
	StoreDefaultCameraValues();

	if (USpringArmComponent* SpringArm = ResolveLockOnSpringArm())
	{
		SpringArm->TargetArmLength = DefaultSpringArmLength;
		SpringArm->SocketOffset = DefaultSpringArmSocketOffset;
		SpringArm->TargetOffset = DefaultSpringArmTargetOffset;
	}

	if (UCameraComponent* CameraComp = ResolveLockOnCamera())
	{
		CameraComp->SetRelativeLocation(DefaultCameraRelativeLocation);
		CameraComp->SetRelativeRotation(DefaultCameraRelativeRotation);
	}
}

void AGAS_MainCharacterCpp::UpdateLockOnSpringArmCollision(float DeltaTime)
{
	USpringArmComponent* SpringArm = ResolveLockOnSpringArm();
	if (!IsValid(SpringArm) || !bHasStoredHardLockCameraDefaults)
	{
		return;
	}

	// 항상 충돌 프로브 유지
	SpringArm->bDoCollisionTest = true;

	const bool bLockActive = IsMeleeLockOnContextActive() && IsValid(GetCurrentNativeTarget());

	const auto ApplyMeshDitherAlpha = [this](float TargetAlpha, float DeltaTime)
	{
		CurrentMeshDitherAlpha = FMath::FInterpTo(CurrentMeshDitherAlpha, TargetAlpha, FMath::Max(0.0f, DeltaTime), 8.0f);
		if (USkeletalMeshComponent* Mesh = GetMesh())
		{
			for (const FName& ParamName : TArray<FName>{
				TEXT("DitherAlpha"), TEXT("OpacityMask"), TEXT("DitherTemporalAAOpacity"),
				TEXT("Opacity"), TEXT("CharacterOpacity"), TEXT("BodyOpacity")})
			{
				Mesh->SetScalarParameterValueOnMaterials(ParamName, CurrentMeshDitherAlpha);
			}
		}
	};

	if (!bLockActive)
	{
		// 락온 해제 후 팔 길이 부드럽게 복원
		if (!FMath::IsNearlyEqual(SpringArm->TargetArmLength, DefaultSpringArmLength, 0.5f))
		{
			SpringArm->TargetArmLength = FMath::FInterpTo(
				SpringArm->TargetArmLength, DefaultSpringArmLength,
				FMath::Max(0.0f, DeltaTime), NativeCameraRestoreInterpSpeed);
		}
		// 메쉬 불투명도 복원
		if (!FMath::IsNearlyEqual(CurrentMeshDitherAlpha, 1.0f, 0.01f))
		{
			ApplyMeshDitherAlpha(1.0f, DeltaTime);
		}
		return;
	}

	// 카메라 피봇에서 원하는 끝점까지 구체 스윕으로 충돌 감지
	const UCameraComponent* CameraComp = ResolveLockOnCamera();
	if (!IsValid(CameraComp))
	{
		return;
	}

	const FVector PivotLoc = SpringArm->GetComponentLocation();
	const FVector ToCamDir = (CameraComp->GetComponentLocation() - PivotLoc).GetSafeNormal();
	if (ToCamDir.IsNearlyZero())
	{
		return;
	}

	const FVector DesiredEndpoint = PivotLoc + ToCamDir * DefaultSpringArmLength;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LockOnCamProbe), false, this);
	FHitResult Hit;
	const bool bBlocked = GetWorld()->SweepSingleByChannel(
		Hit,
		PivotLoc,
		DesiredEndpoint,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(FMath::Max(1.0f, SpringArm->ProbeSize)),
		Params);

	const float SafeArmLength = bBlocked
		? FMath::Max(SpringArm->ProbeSize * 2.0f, Hit.Distance - SpringArm->ProbeSize)
		: DefaultSpringArmLength;

	// 충돌 시 빠른 단축, 해소 시 느린 복원 — 자연스러운 카메라 퇴피 느낌
	const float InterpSpeed = (SafeArmLength < SpringArm->TargetArmLength)
		? NativeCameraCollisionInterpSpeed
		: NativeCameraRestoreInterpSpeed;

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength, SafeArmLength,
		FMath::Max(0.0f, DeltaTime), InterpSpeed);

	// 사각지대 보완: 팔 길이가 Dither 임계값 미만이면 캐릭터 메쉬를 반투명하게 처리
	if (NativeCameraDitherArmThreshold > 0.0f)
	{
		const float DitherStart = NativeCameraDitherArmThreshold;
		const float DitherEnd   = FMath::Max(SpringArm->ProbeSize * 2.0f, DitherStart * 0.1f);
		const float ArmRatio = FMath::Clamp(
			(SpringArm->TargetArmLength - DitherEnd) / FMath::Max(1.0f, DitherStart - DitherEnd),
			0.0f, 1.0f);
		const float TargetAlpha = FMath::Lerp(
			FMath::Clamp(NativeCameraDitherMinOpacity, 0.0f, 1.0f), 1.0f, ArmRatio);
		ApplyMeshDitherAlpha(TargetAlpha, DeltaTime);
	}
}

void AGAS_MainCharacterCpp::UpdateLockOnCameraTransition(float DeltaTime)
{
	if (!bLockOnCameraTransitionActive)
	{
		return;
	}

	if (!bNativeHardLockActive || NativeCameraTransitionSpeed <= 0.0f)
	{
		bLockOnCameraTransitionActive = false;
		return;
	}

	AActor* HardTarget = NativeCombatFuryLockOnTarget.Get();
	if (!IsLockOnTargetUsable(HardTarget))
	{
		bLockOnCameraTransitionActive = false;
		return;
	}

	AController* PC = GetController();
	if (!PC)
	{
		bLockOnCameraTransitionActive = false;
		return;
	}

	const FVector AimPoint = ResolveLockOnAimPoint(HardTarget);
	FVector ToTarget = AimPoint - GetActorLocation();
	if (ToTarget.IsNearlyZero())
	{
		bLockOnCameraTransitionActive = false;
		return;
	}

	// 짐벌락 방지: 타겟이 너무 가깝거나 거의 수직 방향이면 LookAtRotation이 불안정 → 보간 중단
	{
		const float GimbalMinSq = FMath::Square(FMath::Max(1.0f, NativeCameraGimbalLockMinDist));
		const float ToTargetXYSq = FVector(ToTarget.X, ToTarget.Y, 0.0f).SizeSquared();
		if (ToTarget.SizeSquared() < GimbalMinSq || ToTargetXYSq < FMath::Square(20.0f))
		{
			// 거리가 회복되면 보간이 재개될 수 있도록 플래그는 유지, 이번 프레임만 스킵
			LockOnCameraTransitionElapsed += DeltaTime;
			if (LockOnCameraTransitionElapsed > 0.8f)
				bLockOnCameraTransitionActive = false;
			return;
		}
	}

	FRotator DesiredRot = ToTarget.Rotation();
	DesiredRot.Roll = 0.0f;
	DesiredRot.Pitch = FMath::Clamp(DesiredRot.Pitch, NativeHardLockMinPitch, NativeHardLockMaxPitch);

	// GetNormalized()으로 정규화하여 RInterpTo 내부 delta가 최단 경로를 취하도록 보장
	const FRotator CurrentRot = PC->GetControlRotation().GetNormalized();
	const FRotator NewRot = FMath::RInterpTo(
		CurrentRot, DesiredRot,
		FMath::Max(0.0f, DeltaTime), NativeCameraTransitionSpeed);
	PC->SetControlRotation(NewRot);

	LockOnCameraTransitionElapsed += DeltaTime;

	// FindDeltaAngleDegrees: 180/-180 경계를 정확히 처리하는 최단 각도 차이 계산
	const float YawDiff   = FMath::Abs(FMath::FindDeltaAngleDegrees(NewRot.Yaw,   DesiredRot.Yaw));
	const float PitchDiff = FMath::Abs(FMath::FindDeltaAngleDegrees(NewRot.Pitch, DesiredRot.Pitch));
	if ((YawDiff < 2.0f && PitchDiff < 2.0f) || LockOnCameraTransitionElapsed > 0.8f)
	{
		bLockOnCameraTransitionActive = false;
	}
}

UWidgetComponent* AGAS_MainCharacterCpp::FindLockOnWidget(AActor* TargetActor) const
{
	if (!IsValid(TargetActor))
	{
		return nullptr;
	}

	TArray<UWidgetComponent*> Widgets;
	TargetActor->GetComponents<UWidgetComponent>(Widgets);
	if (Widgets.Num() == 1)
	{
		return Widgets[0];
	}

	for (UWidgetComponent* Candidate : Widgets)
	{
		if (!IsValid(Candidate))
		{
			continue;
		}
		const FString Name = Candidate->GetName();
		if (Name.Contains(TEXT("Lock"),   ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Target"), ESearchCase::IgnoreCase) ||
			Name.Contains(TEXT("Icon"),   ESearchCase::IgnoreCase))
		{
			return Candidate;
		}
	}

	return Widgets.Num() > 0 ? Widgets[0] : nullptr;
}

void AGAS_MainCharacterCpp::HideTargetIconForActor(AActor* TargetActor)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	auto HideWidgetComp = [](UWidgetComponent* WidgetComp)
	{
		if (!IsValid(WidgetComp)) return;
		if (UUserWidget* WidgetObject = WidgetComp->GetUserWidgetObject())
		{
			WidgetObject->SetVisibility(ESlateVisibility::Collapsed);
			WriteBoolPropertyByNames(WidgetObject,
				{
					TEXT("IsLockOn"),       TEXT("bIsLockOn"),
					TEXT("IsTargeted"),     TEXT("bIsTargeted"),
					TEXT("ShowTargetIcon"), TEXT("bShowTargetIcon"),
					TEXT("ShowLockOn"),     TEXT("bShowLockOn")
				},
				false);
		}
		if (WidgetComp->IsVisible() || !WidgetComp->bHiddenInGame)
		{
			WidgetComp->SetVisibility(false, true);
			WidgetComp->SetHiddenInGame(true, true);
		}
		if (WidgetComp->IsComponentTickEnabled())
		{
			WidgetComp->SetComponentTickEnabled(false);
		}
	};

	// Primary icon via FindLockOnWidget.
	if (UWidgetComponent* ResolvedWidget = FindLockOnWidget(TargetActor))
	{
		HideWidgetComp(ResolvedWidget);
	}

	// Sweep all lock-on-like widget components to catch secondary icons.
	TArray<UWidgetComponent*> Widgets;
	TargetActor->GetComponents<UWidgetComponent>(Widgets);
	for (UWidgetComponent* WidgetComp : Widgets)
	{
		if (!IsValid(WidgetComp)) continue;
		const FString CompName = WidgetComp->GetName();
		if (CompName.Contains(TEXT("Lock"),   ESearchCase::IgnoreCase) ||
			CompName.Contains(TEXT("Target"), ESearchCase::IgnoreCase) ||
			CompName.Contains(TEXT("Icon"),   ESearchCase::IgnoreCase))
		{
			HideWidgetComp(WidgetComp);
		}
	}

	TArray<AActor*> AttachedActors;
	TargetActor->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (!IsValid(AttachedActor)) continue;
		TArray<UWidgetComponent*> AttachedWidgets;
		AttachedActor->GetComponents<UWidgetComponent>(AttachedWidgets);
		for (UWidgetComponent* WidgetComp : AttachedWidgets)
		{
			HideWidgetComp(WidgetComp);
		}
	}

	if (ActiveCombatFuryTargetActor.Get() == TargetActor)
	{
		ActiveCombatFuryTargetActor = nullptr;
		ActiveCombatFuryTargetWidget = nullptr;
	}
}

void AGAS_MainCharacterCpp::ShowTargetIconForActor(AActor* TargetActor, bool bHardLock)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	UWidgetComponent* TargetWidget = FindLockOnWidget(TargetActor);
	if (!IsValid(TargetWidget))
	{
		return;
	}

	TargetWidget->SetVisibility(true, true);
	TargetWidget->SetHiddenInGame(false, true);
	TargetWidget->SetComponentTickEnabled(true);
	if (!TargetWidget->IsActive())
	{
		TargetWidget->Activate(true);
	}

	if (UUserWidget* WidgetObject = TargetWidget->GetUserWidgetObject())
	{
		WidgetObject->SetVisibility(ESlateVisibility::HitTestInvisible);

		WriteBoolPropertyByNames(WidgetObject,
			{
				TEXT("IsLockOn"),       TEXT("bIsLockOn"),
				TEXT("IsTargeted"),     TEXT("bIsTargeted"),
				TEXT("ShowTargetIcon"), TEXT("bShowTargetIcon"),
				TEXT("ShowLockOn"),     TEXT("bShowLockOn")
			},
			true);
		CallFunctionNoParams(WidgetObject,
			{
				TEXT("ShowTargetIcon"),
				TEXT("ShowLockOn"),
				TEXT("LockOnEvent"),
				TEXT("OnLockOn")
			});

		const FLinearColor ModeColor = bHardLock ? FLinearColor(0.10f, 0.90f, 1.00f, 1.00f) : FLinearColor::Yellow;
		WriteLinearColorPropertyByNames(WidgetObject,
			{
				TEXT("TargetColor"),
				TEXT("LockOnColor"),
				TEXT("IconColor"),
				TEXT("TargetIconColor"),
				TEXT("ImageColor"),
				TEXT("TintColor")
			},
			ModeColor);

		// 위젯에 우선순위 점수 기록 — BP에서 TargetScore/IconPriority 프로퍼티를 읽어 크기·알파 연출 가능
		for (const FName& FloatPropName : TArray<FName>{
			TEXT("TargetScore"), TEXT("IconPriority"), TEXT("TargetPriority"), TEXT("LockOnScore")})
		{
			if (FProperty* Prop = WidgetObject->GetClass()->FindPropertyByName(FloatPropName))
			{
				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					FloatProp->SetPropertyValue_InContainer(WidgetObject, NativeCurrentTargetScore);
					break;
				}
			}
		}
	}

	TargetWidget->SetWorldScale3D(FVector(bHardLock ? 1.12f : 0.95f));
	ActiveCombatFuryTargetWidget = TargetWidget;
	ActiveCombatFuryTargetActor = TargetActor;
}

void AGAS_MainCharacterCpp::HideAllTargetIcons()
{
	TArray<AActor*> ActorsToHide;
	auto AddActor = [&ActorsToHide](AActor* Actor)
	{
		if (IsValid(Actor))
		{
			ActorsToHide.AddUnique(Actor);
		}
	};

	AddActor(NativeCombatFuryLockOnTarget.Get());
	AddActor(NativeCombatFurySoftTarget.Get());
	AddActor(ActiveCombatFuryTargetActor.Get());
	AddActor(PreviousCombatFuryTargetActor.Get());

	for (const TWeakObjectPtr<AActor>& CandidatePtr : LastCandidateTargets)
	{
		AddActor(CandidatePtr.Get());
	}

	if (UWorld* World = GetWorld())
	{
		// Scan ALL non-self pawns — rely on the lock-on-like widget filter inside
		// HideWidgetsOnActor to avoid touching non-icon UI (health bars, nameplates).
		TArray<AActor*> PawnActors;
		UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), PawnActors);
		for (AActor* PawnActor : PawnActors)
		{
			if (!IsValid(PawnActor) || PawnActor == this)
			{
				continue;
			}
			AddActor(PawnActor);
		}
	}

	for (AActor* ActorToHide : ActorsToHide)
	{
		HideTargetIconForActor(ActorToHide);
	}

	ActiveCombatFuryTargetWidget = nullptr;
	ActiveCombatFuryTargetActor = nullptr;
}

void AGAS_MainCharacterCpp::UpdateTargetIconVisibility()
{
	AActor* CurrentTarget = GetCurrentNativeTarget();
	if (!IsLockOnTargetUsable(CurrentTarget))
	{
		CurrentTarget = nullptr;
	}

	AActor* PreviousTarget = ActiveCombatFuryTargetActor.Get();
	if (CurrentTarget == PreviousTarget)
	{
		return;
	}

	if (IsValid(PreviousTarget))
	{
		HideTargetIconForActor(PreviousTarget);
	}

	if (IsValid(CurrentTarget))
	{
		ShowTargetIconForActor(CurrentTarget, bNativeHardLockActive);
	}
	else
	{
		ActiveCombatFuryTargetWidget = nullptr;
		ActiveCombatFuryTargetActor = nullptr;
	}
}

void AGAS_MainCharacterCpp::SweepNonTargetIcons()
{
	if (!IsMeleeLockOnContextActive())
	{
		return;
	}

	AActor* CurrentTarget = GetCurrentNativeTarget();
	if (!IsLockOnTargetUsable(CurrentTarget))
	{
		CurrentTarget = nullptr;
	}

	// 우선순위 집합: 현재 타겟 + 상위 3 후보 (NativeLockOnPriorityCandidates)
	// 집합 外 모든 후보는 위젯 즉시 숨김 + 틱 비활성화
	TSet<const AActor*> PrioritySet;
	if (IsValid(CurrentTarget))
	{
		PrioritySet.Add(CurrentTarget);
	}
	for (const TObjectPtr<AActor>& P : NativeLockOnPriorityCandidates)
	{
		if (AActor* A = P.Get())
		{
			PrioritySet.Add(A);
		}
	}

	const FVector SelfLoc = GetActorLocation();
	const float MaxTickDistSq = (NativeLockOnWidgetTickMaxDistance > 0.0f)
		? FMath::Square(NativeLockOnWidgetTickMaxDistance)
		: TNumericLimits<float>::Max();

	// 비우선순위 액터: 위젯 숨김 + 원거리 위젯 틱 비활성화
	auto FastHide = [&PrioritySet, &SelfLoc, MaxTickDistSq](AActor* Actor)
	{
		if (!IsValid(Actor)) return;
		const bool bIsPriority = PrioritySet.Contains(Actor);

		TArray<UWidgetComponent*> Widgets;
		Actor->GetComponents<UWidgetComponent>(Widgets);
		for (UWidgetComponent* W : Widgets)
		{
			if (!IsValid(W)) continue;
			const FString N = W->GetName();
			if (!N.Contains(TEXT("Lock"),   ESearchCase::IgnoreCase) &&
				!N.Contains(TEXT("Target"), ESearchCase::IgnoreCase) &&
				!N.Contains(TEXT("Icon"),   ESearchCase::IgnoreCase)) continue;

			if (!bIsPriority)
			{
				// 비우선순위: 즉시 숨김
				if (W->IsVisible() || !W->bHiddenInGame)
				{
					W->SetVisibility(false, true);
					W->SetHiddenInGame(true, true);
				}
				// 거리 초과 시 틱 비활성화 + 렌더 파이프라인 완전 제외
				const float DistSq = FVector::DistSquared(SelfLoc, Actor->GetActorLocation());
				if (DistSq > MaxTickDistSq)
				{
					if (W->IsComponentTickEnabled())
						W->SetComponentTickEnabled(false);
				}
			}
			else
			{
				// 우선순위 액터: 거리 제한으로 틱이 꺼진 경우 복구 (Top-3 재진입 시 위젯 갱신 보장)
				if (!W->IsComponentTickEnabled())
					W->SetComponentTickEnabled(true);
				// HiddenInGame이 걸려 있으면 해제 (ShowTargetIconForActor가 호출되기 전 렌더 준비)
				if (W->bHiddenInGame)
					W->SetHiddenInGame(false, true);
			}
		}
	};

	FastHide(ActiveCombatFuryTargetActor.Get());
	FastHide(PreviousCombatFuryTargetActor.Get());
	for (const TWeakObjectPtr<AActor>& P : LastCandidateTargets)
	{
		FastHide(P.Get());
	}
}

void AGAS_MainCharacterCpp::UpdateNativeSoftTargeting(float DeltaTime)
{
	if (!IsMeleeLockOnContextActive())
	{
		if (bLastKnownMeleeContextActive)
		{
			// Context just became inactive (weapon slot changed away from melee).
			// Clear once on transition; avoid expensive world scan on every subsequent tick.
			bLastKnownMeleeContextActive = false;
			HideAllTargetIcons();
			if (GetCurrentNativeTarget())
			{
				ClearCurrentNativeTarget(TEXT("WeaponSlotChanged"));
			}
		}
		return;
	}
	if (!bLastKnownMeleeContextActive)
	{
		// Entering melee context (weapon slot just changed to melee).
		// LockOnWidget components on enemy BPs default to Visible, so hide all
		// before the targeting loop starts to prevent stale icons on non-targets.
		HideAllTargetIcons();
	}
	bLastKnownMeleeContextActive = true;

	if (bNativeHardLockActive)
	{
		if (!ValidateCurrentNativeTarget(TEXT("HardLockTick"), true))
		{
			ShowNativeLockOnModeFeedback(this, TEXT("SOFT LOCK"), FColor::Yellow);
		}
		else
		{
			AActor* HardTarget = NativeCombatFuryLockOnTarget.Get();
			NativeCombatFurySoftTarget = HardTarget;
			NativeSoftTargetElapsed = 0.0f;
			NativeSoftAttackCorrectionRemaining = 0.0f;
			bWasNativeSoftAttackTagActive = false;
			ApplyNativeHardLockFacingCorrection(HardTarget, DeltaTime);
			return;
		}
	}

	if (!bEnableNativeSoftTargeting)
	{
		ClearCurrentNativeTarget(TEXT("SoftTargetDisabled"));
		return;
	}

	if (NativeCombatFurySoftTarget.IsValid())
	{
		ValidateCurrentNativeTarget(TEXT("SoftLockTick"), false);
	}

	const bool bApplyAttackCorrection = ShouldApplyNativeSoftAttackCorrection(DeltaTime);
	if (bApplyAttackCorrection && bNativeSoftTargetPinDuringAttack)
	{
		AActor* PinnedTarget = IsLockOnTargetUsable(NativeCombatFurySoftTarget.Get()) ? NativeCombatFurySoftTarget.Get() : nullptr;
		if (!PinnedTarget && IsLockOnTargetUsable(NativeCombatFuryLockOnTarget.Get()))
		{
			PinnedTarget = NativeCombatFuryLockOnTarget.Get();
		}

		if (IsLockOnTargetUsable(PinnedTarget))
		{
			SetCurrentNativeTarget(PinnedTarget, false, false, TEXT("SoftAttackPin"));
			ApplyNativeSoftAttackFacingCorrection(PinnedTarget, DeltaTime);
			return;
		}
	}

	const float EffectiveInterval = FMath::Max(0.02f, NativeSoftTargetRefreshInterval);
	NativeSoftTargetElapsed += DeltaTime;
	AActor* PreviousSoftTarget = IsLockOnTargetUsable(NativeCombatFurySoftTarget.Get()) ? NativeCombatFurySoftTarget.Get() : nullptr;
	// Force refresh only when an existing target just became invalid (died/out of range).
	// Do NOT force-refresh every tick when no target exists — BuildCombatFuryLockOnCandidates
	// calls GetAllActorsWithTag×5 + GetAllActorsOfClass×1 and would run at full frame rate.
	const bool bTargetJustLost = NativeCombatFurySoftTarget.IsValid() && (PreviousSoftTarget == nullptr);
	const bool bNeedRefresh = NativeSoftTargetElapsed >= EffectiveInterval || bTargetJustLost;
	if (!bNeedRefresh)
	{
		if (IsLockOnTargetUsable(PreviousSoftTarget))
		{
			SetCurrentNativeTarget(PreviousSoftTarget, false, false, TEXT("SoftMaintain"));
			if (bApplyAttackCorrection)
			{
				ApplyNativeSoftAttackFacingCorrection(PreviousSoftTarget, DeltaTime);
			}
		}
		return;
	}

	NativeSoftTargetElapsed = 0.0f;
	TArray<AActor*> RankedCandidates;
	BuildCombatFuryLockOnCandidates(RankedCandidates, ResolveEffectiveLockOnMaxDistance(NativeSoftTargetMaxDistance));

	AActor* SoftTarget = nullptr;
	if (RankedCandidates.Num() > 0)
	{
		// 공격 중 이동 방향 기준 스마트 자석 타겟팅 — 하드락 없을 때 이동 방향 원뿔 내 최근접 적 우선 (God of War 스타일)
		if (bApplyAttackCorrection && !bNativeHardLockActive && NativeSoftMagneticConeHalfAngle > 0.0f)
		{
			AActor* MagneticTarget = FindSmartMagneticTarget(RankedCandidates);
			if (IsLockOnTargetUsable(MagneticTarget))
			{
				const bool bChanged = MagneticTarget != PreviousSoftTarget;
				SetCurrentNativeTarget(MagneticTarget, false, false, bChanged ? TEXT("MagneticAttack") : TEXT("SoftMaintain"));
				ApplyNativeSoftAttackFacingCorrection(MagneticTarget, DeltaTime);
				return;
			}
		}

		AActor* BestTarget = RankedCandidates[0];
		if (IsLockOnTargetUsable(PreviousSoftTarget))
		{
			const int32 CurrentIndex = RankedCandidates.IndexOfByKey(PreviousSoftTarget);
			if (CurrentIndex == 0 || CurrentIndex == 1)
			{
				SoftTarget = PreviousSoftTarget;
			}
			else if (CurrentIndex != INDEX_NONE)
			{
				const float CurrentDistanceSq = FVector::DistSquared(GetActorLocation(), PreviousSoftTarget->GetActorLocation());
				const float BestDistanceSq = FVector::DistSquared(GetActorLocation(), BestTarget->GetActorLocation());
				const float SwitchMarginSq = FMath::Square(200.0f);
				SoftTarget = ((BestDistanceSq + SwitchMarginSq) < CurrentDistanceSq) ? BestTarget : PreviousSoftTarget;
			}
			else
			{
				SoftTarget = BestTarget;
			}
		}
		else
		{
			SoftTarget = BestTarget;
		}
	}

	if (IsLockOnTargetUsable(SoftTarget))
	{
		const bool bChanged = SoftTarget != PreviousSoftTarget;
		SetCurrentNativeTarget(SoftTarget, false, false, bChanged ? TEXT("SoftAcquire") : TEXT("SoftMaintain"));
		if (bApplyAttackCorrection)
		{
			ApplyNativeSoftAttackFacingCorrection(SoftTarget, DeltaTime);
		}
	}
	else
	{
		ClearCurrentNativeTarget(TEXT("NoCandidate"));
	}
}

bool AGAS_MainCharacterCpp::ShouldApplyNativeSoftAttackCorrection(float DeltaTime)
{
	if (!bEnableNativeSoftTargetAttackCorrection)
	{
		NativeSoftAttackCorrectionRemaining = 0.0f;
		bWasNativeSoftAttackTagActive = false;
		return false;
	}

	const bool bActionMeleeTagActive = HasActionMeleeTag(AbilitySystemComponent);
	if (bActionMeleeTagActive)
	{
		NativeSoftAttackCorrectionRemaining = NativeSoftTargetAttackCorrectionGraceSeconds;
		bWasNativeSoftAttackTagActive = true;
		return true;
	}

	if (bWasNativeSoftAttackTagActive)
	{
		NativeSoftAttackCorrectionRemaining = FMath::Max(NativeSoftAttackCorrectionRemaining, NativeSoftTargetAttackCorrectionGraceSeconds);
		bWasNativeSoftAttackTagActive = false;
	}

	if (NativeSoftAttackCorrectionRemaining > 0.0f)
	{
		NativeSoftAttackCorrectionRemaining = FMath::Max(0.0f, NativeSoftAttackCorrectionRemaining - DeltaTime);
		return true;
	}

	return false;
}

void AGAS_MainCharacterCpp::ApplyNativeSoftAttackFacingCorrection(AActor* Target, float DeltaTime)
{
	if (!bEnableNativeSoftTargetAttackCorrection || !bNativeSoftTargetForceFacingDuringAttack)
	{
		return;
	}

	if (!IsLockOnTargetUsable(Target))
	{
		return;
	}

	const float EffectiveDeltaTime = FMath::Max(0.0f, DeltaTime);
	const FVector AimPoint = ResolveLockOnAimPoint(Target);
	FVector ToTarget = AimPoint - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const float DesiredYaw = ToTarget.Rotation().Yaw;
	const float ActorInterpSpeed = FMath::Max(0.0f, NativeSoftTargetFacingInterpSpeed);
	const float ControlInterpSpeed = FMath::Max(0.0f, NativeSoftTargetControlYawInterpSpeed);

	FRotator DesiredActorRotation = GetActorRotation();
	DesiredActorRotation.Yaw = DesiredYaw;
	const FRotator NewActorRotation =
		(ActorInterpSpeed > KINDA_SMALL_NUMBER)
		? FMath::RInterpTo(GetActorRotation(), DesiredActorRotation, EffectiveDeltaTime, ActorInterpSpeed)
		: DesiredActorRotation;
	SetActorRotation(NewActorRotation, ETeleportType::None);

	if (AController* OwningController = GetController())
	{
		FRotator DesiredControlRotation = OwningController->GetControlRotation();
		DesiredControlRotation.Yaw = DesiredYaw;
		const FRotator NewControlRotation =
			(ControlInterpSpeed > KINDA_SMALL_NUMBER)
			? FMath::RInterpTo(OwningController->GetControlRotation(), DesiredControlRotation, EffectiveDeltaTime, ControlInterpSpeed)
			: DesiredControlRotation;
		OwningController->SetControlRotation(NewControlRotation);
	}
}

void AGAS_MainCharacterCpp::ApplyNativeHardLockFacingCorrection(AActor* Target, float DeltaTime)
{
	if (!bNativeHardLockActive || !IsLockOnTargetUsable(Target))
	{
		return;
	}

	const FVector TargetAimPoint = ResolveLockOnAimPoint(Target);
	FVector ToTarget = TargetAimPoint - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	FRotator DesiredActorRotation = ToTarget.Rotation();
	DesiredActorRotation.Pitch = 0.0f;
	DesiredActorRotation.Roll = 0.0f;

	const float EffectiveDeltaTime = FMath::Max(0.0f, DeltaTime);
	const float ActorInterpSpeed = FMath::Clamp(NativeHardLockFacingInterpSpeed, 10.0f, 16.0f);
	const FRotator NewActorRotation =
		(ActorInterpSpeed > KINDA_SMALL_NUMBER)
		? FMath::RInterpTo(GetActorRotation(), DesiredActorRotation, EffectiveDeltaTime, ActorInterpSpeed)
		: DesiredActorRotation;
	SetActorRotation(NewActorRotation, ETeleportType::None);
}

AActor* AGAS_MainCharacterCpp::FindSmartMagneticTarget(const TArray<AActor*>& Candidates) const
{
	if (Candidates.IsEmpty() || NativeSoftMagneticConeHalfAngle <= 0.0f)
	{
		return nullptr;
	}

	// 캐릭터 이동 입력 방향 — 없으면 정면 사용
	FVector MoveDir = FVector::ZeroVector;
	if (const UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveDir = MoveComp->GetLastInputVector();
	}
	if (MoveDir.IsNearlyZero())
	{
		MoveDir = GetActorForwardVector();
	}
	MoveDir.Z = 0.0f;
	MoveDir = MoveDir.GetSafeNormal2D();
	if (MoveDir.IsNearlyZero())
	{
		return nullptr;
	}

	const float ConeHalfCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(NativeSoftMagneticConeHalfAngle, 0.0f, 90.0f)));
	const FVector SelfLoc = GetActorLocation();

	AActor* BestActor = nullptr;
	float BestDistSq = FLT_MAX;

	for (AActor* Cand : Candidates)
	{
		if (!IsValid(Cand))
		{
			continue;
		}
		FVector ToTarget = Cand->GetActorLocation() - SelfLoc;
		ToTarget.Z = 0.0f;
		const FVector ToTargetNorm = ToTarget.GetSafeNormal2D();
		if (ToTargetNorm.IsNearlyZero())
		{
			continue;
		}
		if (FVector::DotProduct(MoveDir, ToTargetNorm) >= ConeHalfCos)
		{
			const float DistSq = ToTarget.SizeSquared2D();
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestActor = Cand;
			}
		}
	}

	return BestActor;
}

void AGAS_MainCharacterCpp::BuildCombatFuryLockOnCandidates(TArray<AActor*>& OutCandidates, float MaxDistanceOverride) const
{
	OutCandidates.Reset();
	LastCandidateTargets.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Candidates;
	auto AddCandidateUnique = [&Candidates, this](AActor* CandidateActor)
	{
		if (!IsValid(CandidateActor) || CandidateActor == this)
		{
			return;
		}

		Candidates.AddUnique(CandidateActor);
		LastCandidateTargets.AddUnique(CandidateActor);
	};

	auto GatherCandidatesByTag = [&](const FName& CandidateTag)
	{
		if (CandidateTag.IsNone())
		{
			return;
		}

		TArray<AActor*> TaggedActors;
		UGameplayStatics::GetAllActorsWithTag(World, CandidateTag, TaggedActors);
		for (AActor* TaggedActor : TaggedActors)
		{
			AddCandidateUnique(TaggedActor);
		}
	};

	TArray<FName> EnemyTagPriority;
	if (!CombatFuryEnemyTag.IsNone())
	{
		EnemyTagPriority.AddUnique(CombatFuryEnemyTag);
	}
	EnemyTagPriority.AddUnique(FName(TEXT("Enemy")));
	EnemyTagPriority.AddUnique(FName(TEXT("Hostile")));
	EnemyTagPriority.AddUnique(FName(TEXT("Zombie")));
	EnemyTagPriority.AddUnique(FName(TEXT("HumanAI")));

	for (const FName& CandidateTag : EnemyTagPriority)
	{
		GatherCandidatesByTag(CandidateTag);
	}

	// Folder/class-name fallback — always run so HumanAI actors without explicit tags
	// are discovered even when zombie-tagged actors are already in the candidate list.
	// Cost is acceptable since this function runs on a 100ms timer, not every frame.
	TArray<AActor*> PawnActors;
	UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), PawnActors);
	for (AActor* PawnActor : PawnActors)
	{
		if (!IsValid(PawnActor) || PawnActor == this)
		{
			continue;
		}

		const FString ClassPath = PawnActor->GetClass()->GetPathName();
		const bool bFromKnownEnemyBlueprintFamily =
			ClassPath.Contains(TEXT("/Blueprints/Human_AI_Logic/"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("/Blueprints/Zombie_Logic/"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("RangeAI"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("HumanAI"), ESearchCase::IgnoreCase) ||
			ClassPath.Contains(TEXT("Zombie"), ESearchCase::IgnoreCase);

		if (bFromKnownEnemyBlueprintFamily)
		{
			AddCandidateUnique(PawnActor);
		}
	}

	struct FCandidateSortInfo
	{
		AActor* Actor = nullptr;
		float DistanceSq = TNumericLimits<float>::Max();
		bool bHasLineOfSight = false;
		bool bIsCurrentTarget = false;
		bool bRecentInstigator = false;
		bool bIsThreateningNow = false;   // 현재 공격 중 (God of War 스타일 위협 우선순위)
		float Score = -TNumericLimits<float>::Max();
	};

	TArray<FCandidateSortInfo> ValidCandidates;
	ValidCandidates.Reserve(Candidates.Num());

	const FVector SelfLocation = GetActorLocation();
	const float EffectiveMaxDistance = ResolveEffectiveLockOnMaxDistance(MaxDistanceOverride);
	const float MaxDistanceSq = EffectiveMaxDistance > 0.0f ? FMath::Square(EffectiveMaxDistance) : TNumericLimits<float>::Max();
	float EffectiveMinViewDot = FMath::Clamp(NativeSoftTargetMinViewDot, -1.0f, 0.99f);
	if (bNativeHardLockActive)
	{
		// Allow wider side candidates while hard-locked for reliable wheel target switching.
		EffectiveMinViewDot = FMath::Min(EffectiveMinViewDot, -0.35f);
	}
	const float InverseViewDotRange = 1.0f / FMath::Max(KINDA_SMALL_NUMBER, 1.0f - EffectiveMinViewDot);
	const AActor* RecentDamageInstigator = LastCombatFuryRecentDamageInstigator.Get();
	const float ElapsedSinceRecentDamage = LastCombatFuryRecentDamageInstigatorTime >= 0.0f ? (World->GetTimeSeconds() - LastCombatFuryRecentDamageInstigatorTime) : TNumericLimits<float>::Max();
	const bool bRecentStillValid = CombatFuryRecentDamageMemorySeconds <= 0.0f || ElapsedSinceRecentDamage <= CombatFuryRecentDamageMemorySeconds;
	const AActor* CurrentTarget = GetCurrentNativeTarget();

	const float FacingWeight = 0.45f;
	const float CurrentTargetStabilityWeight = 0.35f;

	FVector ViewLocation = GetPawnViewLocation();
	FRotator ViewRotation = GetViewRotation();
	if (const AController* OwningController = GetController())
	{
		ViewRotation = OwningController->GetControlRotation();
		if (const APlayerController* PlayerController = Cast<APlayerController>(OwningController))
		{
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
	}
	FVector ViewForward = ViewRotation.Vector();
	ViewForward.Z = 0.0f;
	ViewForward = ViewForward.GetSafeNormal();

	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed())
		{
			continue;
		}

		if (Candidate->IsA(AController::StaticClass()))
		{
			continue;
		}

		APawn* CandidatePawn = Cast<APawn>(Candidate);
		if (!IsValid(CandidatePawn))
		{
			continue;
		}
		Candidate = CandidatePawn;

		const FString CandidateClassPath = Candidate->GetClass()->GetPathName();
		const bool bEnemyTagMatch =
			(!CombatFuryEnemyTag.IsNone() && Candidate->ActorHasTag(CombatFuryEnemyTag)) ||
			Candidate->ActorHasTag(FName(TEXT("Enemy"))) ||
			Candidate->ActorHasTag(FName(TEXT("Hostile"))) ||
			Candidate->ActorHasTag(FName(TEXT("Zombie"))) ||
			Candidate->ActorHasTag(FName(TEXT("HumanAI")));
		const bool bKnownEnemyClass =
			CandidateClassPath.Contains(TEXT("/Blueprints/Human_AI_Logic/"), ESearchCase::IgnoreCase) ||
			CandidateClassPath.Contains(TEXT("/Blueprints/Zombie_Logic/"), ESearchCase::IgnoreCase) ||
			CandidateClassPath.Contains(TEXT("RangeAI"), ESearchCase::IgnoreCase) ||
			CandidateClassPath.Contains(TEXT("HumanAI"), ESearchCase::IgnoreCase) ||
			CandidateClassPath.Contains(TEXT("Zombie"), ESearchCase::IgnoreCase);
		if (!bEnemyTagMatch && !bKnownEnemyClass)
		{
			continue;
		}

		if (!IsLockOnTargetUsable(Candidate))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(SelfLocation, Candidate->GetActorLocation());
		if (DistanceSq > MaxDistanceSq)
		{
			continue;
		}

		FVector ToCandidate = Candidate->GetActorLocation() - ViewLocation;
		ToCandidate.Z = 0.0f;
		const FVector ToCandidateDir = ToCandidate.GetSafeNormal();
		const float ViewDot = FVector::DotProduct(ViewForward, ToCandidateDir);
		if (ViewDot < EffectiveMinViewDot)
		{
			continue;
		}

		FCandidateSortInfo& Entry = ValidCandidates.Emplace_GetRef();
		Entry.Actor = Candidate;
		Entry.DistanceSq = DistanceSq;
		Entry.bHasLineOfSight = HasLineOfSightToLockOnCandidate(Candidate);
		Entry.bIsCurrentTarget = (CurrentTarget == Candidate);
		Entry.bRecentInstigator =
			bPrioritizeRecentDamageInstigator &&
			bRecentStillValid &&
			IsValid(RecentDamageInstigator) &&
			(Candidate == RecentDamageInstigator);

		// 위협 감지: UAGLS_ZombieAttacksComponentCore::bAttackStarted → 리플렉션 폴백 → 태그 순으로 확인
		if (NativeSoftTargetThreatWeight > 0.0f)
		{
			if (const UAGLS_ZombieAttacksComponentCore* AttackComp =
					Candidate->FindComponentByClass<UAGLS_ZombieAttacksComponentCore>())
			{
				Entry.bIsThreateningNow = AttackComp->bAttackStarted;
			}
			if (!Entry.bIsThreateningNow)
			{
				bool bAttackProp = false;
				if (ReadBoolPropertyByNames(Candidate,
					{ TEXT("bAttackStarted"), TEXT("IsAttacking"), TEXT("bIsAttacking"),
					  TEXT("IsAttackingC"),   TEXT("bIsAttackingC") },
					bAttackProp))
				{
					Entry.bIsThreateningNow = bAttackProp;
				}
			}
			if (!Entry.bIsThreateningNow)
			{
				Entry.bIsThreateningNow =
					Candidate->ActorHasTag(FName(TEXT("Action.Melee"))) ||
					Candidate->ActorHasTag(FName(TEXT("Attacking")))    ||
					Candidate->ActorHasTag(FName(TEXT("Action.Attack")));
			}
		}

		if (bPreferLineOfSightLockOnTarget && !Entry.bHasLineOfSight)
		{
			ValidCandidates.Pop(EAllowShrinking::No);
			continue;
		}

		const float CandidateDistance = FMath::Sqrt(DistanceSq);
		const float DistanceScore =
			(EffectiveMaxDistance > 0.0f)
			? FMath::Clamp(1.0f - (CandidateDistance / EffectiveMaxDistance), 0.0f, 1.0f)
			: (1.0f / (1.0f + (CandidateDistance / 3500.0f)));

		const float ScreenCenterScore = FMath::Clamp((ViewDot - EffectiveMinViewDot) * InverseViewDotRange, 0.0f, 1.0f);
		const float FacingScore = FMath::Clamp((ViewDot + 1.0f) * 0.5f, 0.0f, 1.0f);
		const float LineOfSightScore = (bPreferLineOfSightLockOnTarget && Entry.bHasLineOfSight) ? 1.0f : 0.0f;
		const float RecentDamageScore = Entry.bRecentInstigator ? 1.0f : 0.0f;
		const float ThreatScore = Entry.bIsThreateningNow ? 1.0f : 0.0f;
		const float CurrentTargetStabilityScore = Entry.bIsCurrentTarget ? 1.0f : 0.0f;

		Entry.Score =
			(DistanceScore * NativeSoftTargetDistanceWeight) +
			(ScreenCenterScore * NativeSoftTargetScreenCenterWeight) +
			(FacingScore * FacingWeight) +
			(LineOfSightScore * NativeSoftTargetLineOfSightWeight) +
			(RecentDamageScore * NativeSoftTargetRecentDamageWeight) +
			(ThreatScore * NativeSoftTargetThreatWeight) +
			(CurrentTargetStabilityScore * CurrentTargetStabilityWeight);
	}

	ValidCandidates.Sort([this](const FCandidateSortInfo& A, const FCandidateSortInfo& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score, KINDA_SMALL_NUMBER))
		{
			return A.Score > B.Score;
		}

		if (A.bRecentInstigator != B.bRecentInstigator)
		{
			return A.bRecentInstigator;
		}

		if (A.bIsCurrentTarget != B.bIsCurrentTarget)
		{
			return A.bIsCurrentTarget;
		}

		if (bPreferLineOfSightLockOnTarget && (A.bHasLineOfSight != B.bHasLineOfSight))
		{
			return A.bHasLineOfSight;
		}

		return A.DistanceSq < B.DistanceSq;
	});

	OutCandidates.Reserve(ValidCandidates.Num());
	for (const FCandidateSortInfo& Entry : ValidCandidates)
	{
		OutCandidates.Add(Entry.Actor);
	}

	// UI 우선순위 노출: 상위 3개 후보와 1위 점수를 Blueprint(UMG)에 전달
	NativeLockOnPriorityCandidates.Reset();
	const float TopScore = ValidCandidates.Num() > 0 ? ValidCandidates[0].Score : 0.0f;
	NativeCurrentTargetScore = (TopScore > 0.0f)
		? FMath::Clamp(TopScore / (TopScore + 1.0f), 0.0f, 1.0f)
		: 0.0f;
	const int32 TopN = FMath::Min(ValidCandidates.Num(), 3);
	for (int32 i = 0; i < TopN; ++i)
	{
		NativeLockOnPriorityCandidates.Add(ValidCandidates[i].Actor);
	}

}bool AGAS_MainCharacterCpp::SetNativeCombatFuryLockOnTarget(AActor* NewTarget, bool bTreatAsSwitch)
{
	return SetCurrentNativeTarget(NewTarget, true, bTreatAsSwitch, TEXT("HardLockRequest"));
}

void AGAS_MainCharacterCpp::ClearNativeCombatFuryLockOnTarget()
{
	AActor* SoftTarget = NativeCombatFurySoftTarget.Get();
	ResetHardLockCamera();
	if (IsLockOnTargetUsable(SoftTarget))
	{
		SetCurrentNativeTarget(SoftTarget, false, false, TEXT("HardLockOff"));
		return;
	}

	ClearCurrentNativeTarget(TEXT("HardLockOff"));
}

AActor* AGAS_MainCharacterCpp::GetCurrentNativeTarget() const
{
	if (bNativeHardLockActive && NativeCombatFuryLockOnTarget.IsValid())
	{
		return NativeCombatFuryLockOnTarget.Get();
	}

	if (NativeCombatFurySoftTarget.IsValid())
	{
		return NativeCombatFurySoftTarget.Get();
	}

	if (NativeCombatFuryLockOnTarget.IsValid())
	{
		return NativeCombatFuryLockOnTarget.Get();
	}

	return ActiveCombatFuryTargetActor.Get();
}

void AGAS_MainCharacterCpp::ClearCurrentNativeTarget(const FString& Reason)
{
	AActor* PreviousTarget = nullptr;
	if (NativeCombatFuryLockOnTarget.IsValid())
	{
		PreviousTarget = NativeCombatFuryLockOnTarget.Get();
	}
	else if (NativeCombatFurySoftTarget.IsValid())
	{
		PreviousTarget = NativeCombatFurySoftTarget.Get();
	}
	else if (ActiveCombatFuryTargetActor.IsValid())
	{
		PreviousTarget = ActiveCombatFuryTargetActor.Get();
	}

	if (IsValid(PreviousTarget))
	{
		HideTargetIconForActor(PreviousTarget);
	}

	bNativeHardLockActive = false;
	bLockOnCameraTransitionActive = false;
	LastFlickLockedDirection = 0;
	NativeCombatFuryLockOnTarget = nullptr;
	NativeCombatFurySoftTarget = nullptr;
	NativeSoftTargetElapsed = 0.0f;
	NativeSoftAttackCorrectionRemaining = 0.0f;
	bWasNativeSoftAttackTagActive = false;
	ActiveCombatFuryTargetActor = nullptr;
	ActiveCombatFuryTargetWidget = nullptr;
	PreviousCombatFuryTargetActor = nullptr;
	NativeCurrentTargetScore = 0.0f;
	NativeLockOnPriorityCandidates.Reset();
	HideAllTargetIcons();
	ResetHardLockCamera();
}

bool AGAS_MainCharacterCpp::SetCurrentNativeTarget(AActor* NewTarget, bool bHardLock, bool bTreatAsSwitch, const FString& Reason)
{
	const AActor* PreviousTarget = GetCurrentNativeTarget();
	PreviousCombatFuryTargetActor = const_cast<AActor*>(PreviousTarget);
	const bool bModeChanged = (bNativeHardLockActive != bHardLock);
	if (PreviousTarget == NewTarget && !bModeChanged)
	{
		if (!IsLockOnTargetUsable(NewTarget))
		{
			ClearCurrentNativeTarget(FString::Printf(TEXT("%s.InvalidCurrentTarget"), *Reason));
			return false;
		}

		NativeCombatFurySoftTarget = NewTarget;
		NativeCombatFuryLockOnTarget = bHardLock ? NewTarget : nullptr;
		bNativeHardLockActive = bHardLock;
		NativeSoftTargetElapsed = 0.0f;
		if (bHardLock)
		{
			StoreDefaultCameraValues();
		}
		else if (bModeChanged)
		{
			ResetHardLockCamera();
		}

		// 하드락 전환·획득 시 카메라 보간 시작 (타겟 방향으로 RInterpTo)
		if (bHardLock && NativeCameraTransitionSpeed > 0.0f)
		{
			bLockOnCameraTransitionActive = true;
			LockOnCameraTransitionElapsed = 0.0f;
		}
		else
		{
			bLockOnCameraTransitionActive = false;
		}

		UpdateTargetIconVisibility();
		return true;
	}

	if (!IsMeleeLockOnContextActive())
	{
		ClearCurrentNativeTarget(TEXT("WeaponSlotChanged"));
		return false;
	}

	if (!IsLockOnTargetUsable(NewTarget))
	{
		ClearCurrentNativeTarget(FString::Printf(TEXT("%s.InvalidTarget"), *Reason));
		return false;
	}

	if (IsValid(PreviousTarget) && PreviousTarget != NewTarget)
	{
		HideTargetIconForActor(const_cast<AActor*>(PreviousTarget));
	}

	NativeCombatFurySoftTarget = NewTarget;
	NativeCombatFuryLockOnTarget = bHardLock ? NewTarget : nullptr;
	bNativeHardLockActive = bHardLock;
	NativeSoftTargetElapsed = 0.0f;
	if (bHardLock)
	{
		StoreDefaultCameraValues();
	}
	else if (bModeChanged)
	{
		ResetHardLockCamera();
	}

	// 하드락 전환·획득 시 카메라 보간 시작
	if (bHardLock && NativeCameraTransitionSpeed > 0.0f)
	{
		bLockOnCameraTransitionActive = true;
		LockOnCameraTransitionElapsed = 0.0f;
	}
	else
	{
		bLockOnCameraTransitionActive = false;
	}

	UpdateTargetIconVisibility();
	return true;
}

bool AGAS_MainCharacterCpp::ValidateCurrentNativeTarget(const FString& ContextReason, bool bFromHardLock)
{
	AActor* Candidate = bFromHardLock
		? NativeCombatFuryLockOnTarget.Get()
		: (NativeCombatFurySoftTarget.IsValid() ? NativeCombatFurySoftTarget.Get() : GetCurrentNativeTarget());
	if (!IsValid(Candidate))
	{
		return false;
	}

	if (!IsMeleeLockOnContextActive())
	{
		ClearCurrentNativeTarget(TEXT("WeaponSlotChanged"));
		return false;
	}

	if (!IsLockOnTargetUsable(Candidate, bFromHardLock))
	{
		FString Reason = FString::Printf(TEXT("%s.Invalid"), *ContextReason);
		if (!IsActorAliveForLockOn(Candidate))
		{
			Reason = TEXT("Dead");
		}
		else
		{
			float EffectiveMaxDistance = ResolveEffectiveLockOnMaxDistance();
			if (bFromHardLock && NativeHardLockBreakDistanceMultiplier > 1.0f)
			{
				EffectiveMaxDistance *= NativeHardLockBreakDistanceMultiplier;
			}
			if (EffectiveMaxDistance > 0.0f &&
				FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation()) > FMath::Square(EffectiveMaxDistance))
			{
				Reason = TEXT("Distance");
			}
			else if (!HasLineOfSightToLockOnCandidate(Candidate))
			{
				Reason = TEXT("LOS");
			}
			else if (!IsMeleeLockOnContextActive())
			{
				Reason = TEXT("WeaponChanged");
			}
			else if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed())
			{
				Reason = TEXT("Invalid");
			}
		}

		ClearCurrentNativeTarget(Reason);
		UpdateTargetIconVisibility();
		return false;
	}

	return true;
}

bool AGAS_MainCharacterCpp::SwitchNativeTargetByDirection(int32 Direction, FString& OutReason)
{
	OutReason = TEXT("None");
	const int32 NormalizedDirection = Direction >= 0 ? 1 : -1;
	if (!bNativeHardLockActive)
	{
		OutReason = TEXT("NotHardLock");
		return false;
	}

	if (!ValidateCurrentNativeTarget(TEXT("Switch"), true))
	{
		OutReason = TEXT("CurrentInvalid");
		return false;
	}

	// 전환 전용 거리 15m (Sekiro/Elden Ring: 근접 전투 긴장감 유지)
	const float SwitchMaxDist = (NativeHardLockSwitchMaxDistance > 0.0f)
		? NativeHardLockSwitchMaxDistance
		: ResolveEffectiveLockOnMaxDistance();

	TArray<AActor*> RankedCandidates;
	BuildCombatFuryLockOnCandidates(RankedCandidates, SwitchMaxDist);
	AActor* CurrentTarget = NativeCombatFuryLockOnTarget.Get();
	const int32 CandidateCount = RankedCandidates.Num();
	if (CandidateCount <= 1)
	{
		OutReason = TEXT("NoOtherCandidates");
		return false;
	}

	int32 NextIndex = INDEX_NONE;

	if (bUseCameraRelativeTargetSwitching)
	{
		// 화면 좌표 X 기준 전환 (Elden Ring / Ghost of Tsushima 스타일)
		// 플릭 오른쪽(Direction > 0) = 화면 X가 더 큰 대상, 왼쪽 = 화면 X가 더 작은 대상
		APlayerController* SwitchPC = Cast<APlayerController>(GetController());
		const FVector SelfLoc = GetActorLocation();

		if (!SwitchPC)
		{
			NextIndex = NormalizedDirection > 0 ? 0 : (CandidateCount - 1);
		}
		else
		{
			struct FScreenEntry
			{
				AActor* Actor   = nullptr;
				float   ScreenX = 0.0f;
			};
			TArray<FScreenEntry> ScreenEntries;
			ScreenEntries.Reserve(CandidateCount);

			for (AActor* Actor : RankedCandidates)
			{
				FVector2D ScreenPos;
				const bool bProjected = SwitchPC->ProjectWorldLocationToScreen(
					ResolveLockOnAimPoint(Actor), ScreenPos, true);
				ScreenEntries.Add({ Actor, bProjected ? (float)ScreenPos.X : TNumericLimits<float>::Max() });
			}

			ScreenEntries.Sort([&SelfLoc](const FScreenEntry& A, const FScreenEntry& B)
			{
				if (FMath::Abs(A.ScreenX - B.ScreenX) > 5.0f)
					return A.ScreenX < B.ScreenX;
				return FVector::DistSquared(SelfLoc, A.Actor->GetActorLocation())
					 < FVector::DistSquared(SelfLoc, B.Actor->GetActorLocation());
			});

			int32 SortedCurrentIdx = INDEX_NONE;
			for (int32 i = 0; i < ScreenEntries.Num(); ++i)
			{
				if (ScreenEntries[i].Actor == CurrentTarget)
				{
					SortedCurrentIdx = i;
					break;
				}
			}

			if (SortedCurrentIdx == INDEX_NONE)
			{
				// 현재 타겟이 15m 밖에 있어 목록에 없음 — 최고 점수 대상 선택
				NextIndex = 0;
			}
			else
			{
				const int32 Step = (NormalizedDirection > 0) ? 1 : -1;
				for (int32 Search = 1; Search <= CandidateCount; ++Search)
				{
					const int32 TestSorted = (SortedCurrentIdx + Step * Search + CandidateCount * 4) % CandidateCount;
					if (ScreenEntries[TestSorted].Actor != CurrentTarget)
					{
						NextIndex = RankedCandidates.IndexOfByKey(ScreenEntries[TestSorted].Actor);
						break;
					}
				}
			}
		}
	}
	else
	{
		// 점수 순위 순환 방식 (레거시)
		const int32 CurrentIndex = RankedCandidates.IndexOfByKey(CurrentTarget);
		if (CurrentIndex == INDEX_NONE)
		{
			NextIndex = NormalizedDirection > 0 ? 0 : (CandidateCount - 1);
		}
		else
		{
			for (int32 Step = 1; Step <= CandidateCount; ++Step)
			{
				const int32 CandidateIndex = (CurrentIndex + (NormalizedDirection * Step) + (CandidateCount * 4)) % CandidateCount;
				if (RankedCandidates.IsValidIndex(CandidateIndex) && RankedCandidates[CandidateIndex] != CurrentTarget)
				{
					NextIndex = CandidateIndex;
					break;
				}
			}
		}
	}

	if (!RankedCandidates.IsValidIndex(NextIndex))
	{
		OutReason = TEXT("NextNotFound");
		return false;
	}

	AActor* NextTarget = RankedCandidates[NextIndex];
	if (IsValid(CurrentTarget))
	{
		PreviousCombatFuryTargetActor = CurrentTarget;
		HideTargetIconForActor(CurrentTarget);
	}
	if (!SetCurrentNativeTarget(NextTarget, true, true, TEXT("TargetSwitch")))
	{
		OutReason = TEXT("SetTargetFailed");
		return false;
	}

	OutReason = TEXT("Success");
	ShowNativeLockOnModeFeedback(this, TEXT("TARGET SWITCHED"), FColor::Cyan, 0.55f);
	return true;
}

bool AGAS_MainCharacterCpp::HasLineOfSightToLockOnCandidate(const AActor* Candidate) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	if (!bPreferLineOfSightLockOnTarget)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TraceStart = GetPawnViewLocation();
	const FVector TraceEnd = ResolveLockOnAimPoint(Candidate);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CombatFuryLockOnLOS), true, this);
	QueryParams.AddIgnoredActor(this);
	TArray<AActor*> AttachedActors;
	const_cast<AGAS_MainCharacterCpp*>(this)->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (IsValid(AttachedActor))
		{
			QueryParams.AddIgnoredActor(AttachedActor);
		}
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	if (!bHit)
	{
		return true;
	}

	const AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return false;
	}

	return HitActor == Candidate || HitActor->IsAttachedTo(Candidate);
}

void AGAS_MainCharacterCpp::RecordRecentDamageInstigator(AController* EventInstigator, AActor* DamageCauser, float AppliedDamage)
{
	if (AppliedDamage <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AActor* ResolvedInstigator = ResolveDamageInstigatorActor(this, EventInstigator, DamageCauser);
	if (!IsValid(ResolvedInstigator))
	{
		return;
	}

	LastCombatFuryRecentDamageInstigator = ResolvedInstigator;
	LastCombatFuryRecentDamageInstigatorTime = World->GetTimeSeconds();
}

FGameplayTag AGAS_MainCharacterCpp::ConvertLiteralNameToTag(FName TagName)
{
	return FGameplayTag::RequestGameplayTag(TagName);
}

FString AGAS_MainCharacterCpp::GetSubTag(const FGameplayTag& Tag, int32 DesiredDepth)
{
	FString FullTagName = Tag.ToString();
	TArray<FString> SplitTags;
	FullTagName.ParseIntoArray(SplitTags, TEXT("."));

	// Je쐋i DesiredDepth przekracza ilo쒏 segment? lub jest ujemny, zwr槍 pusty string.
	if (DesiredDepth < 0 || DesiredDepth >= SplitTags.Num())
	{
		return FString();
	}

	// Znajd?odpowiedni?g녠boko쒏 od ko?a.
	int32 IndexFromEnd = SplitTags.Num() - DesiredDepth - 1;
	if (IndexFromEnd >= 0 && IndexFromEnd < SplitTags.Num())
	{
		return SplitTags[IndexFromEnd];
	}

	return FString();
}

bool AGAS_MainCharacterCpp::IsTagLeaf(const FGameplayTag& Tag)
{
	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	TSharedPtr<FGameplayTagNode> NodePtr = TagsManager.FindTagNode(Tag);
	const FGameplayTagNode* Node = NodePtr.Get();

	if (Node)
	{
		return Node->GetChildTagNodes().Num() == 0;
	}

	return false;
}

bool AGAS_MainCharacterCpp::SwitchOnOwnedTags(const FGameplayTag& NewState)
{
	if (IsValid(AbilitySystemComponent) == false) return false;

	FGameplayTagContainer OwnedTags;
	AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);

	const bool LastInGroup = IsTagLeaf(NewState);

	TArray<FGameplayTag> TagArray;
	OwnedTags.GetGameplayTagArray(TagArray);

	TArray<FGameplayTag> TagsToRemove = {};

	for (FGameplayTag& Tag : TagArray)
	{
		int SelectDepth = 0;
		if (LastInGroup == true) SelectDepth = 1;
		const FString a = GetSubTag(Tag, 1);
		const FString b = GetSubTag(NewState, SelectDepth);
		if (a == b)
		{
			TagsToRemove.Add(Tag);
		}
	}
	//Convert To TagContainer
	FGameplayTagContainer TagsContainerToRemove;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		TagsContainerToRemove.AddTag(Tag);
	}
	//Remove Tags
	AbilitySystemComponent->RemoveLooseGameplayTags(TagsContainerToRemove);
	if (LastInGroup == true)
	{
		AbilitySystemComponent->AddLooseGameplayTag(NewState);
	}
	return true;
}

bool AGAS_MainCharacterCpp::SwitchOnOwnedTagsWithIgnore(const FGameplayTag& NewState, const FGameplayTagContainer& DoNotEdit)
{
	if (IsValid(AbilitySystemComponent) == false) return false;

	FGameplayTagContainer OwnedTags;
	AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);

	const bool LastInGroup = IsTagLeaf(NewState);

	TArray<FGameplayTag> TagArray;
	OwnedTags.GetGameplayTagArray(TagArray);

	TArray<FGameplayTag> TagsToRemove = {};

	for (FGameplayTag& Tag : TagArray)
	{
		int SelectDepth = 0;
		if (LastInGroup == true) SelectDepth = 1;
		const FString a = GetSubTag(Tag, 1);
		const FString b = GetSubTag(NewState, SelectDepth);
		//Find Match
		FGameplayTagContainer TagAsContainer;
		TagAsContainer.AddTag(Tag);

		if (a == b && TagAsContainer.HasAllExact(DoNotEdit) == false)
		{
			TagsToRemove.Add(Tag);
		}
	}
	//Convert To TagContainer
	FGameplayTagContainer TagsContainerToRemove;
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		TagsContainerToRemove.AddTag(Tag);
	}
	//Remove Tags
	AbilitySystemComponent->RemoveLooseGameplayTags(TagsContainerToRemove);
	if (LastInGroup == true)
	{
		AbilitySystemComponent->AddLooseGameplayTag(NewState);
	}
	return true;
}

void AGAS_MainCharacterCpp::FilterTagsByRootGroup(const FGameplayTagContainer& Input, FGameplayTag RootTag, bool StopWhenFirstValid, FGameplayTagContainer& ReturnContainer)
{
	FGameplayTagContainer Container01;

	for (const FGameplayTag& Tag : Input)
	{
		if (Tag.MatchesTag(RootTag))
		{
			Container01.AddTag(Tag);
			if (StopWhenFirstValid)
			{
				ReturnContainer = Container01;
				return;
			}
		}
	}
	ReturnContainer = Container01;
	return;
}

void AGAS_MainCharacterCpp::EnsureRaidCompassWidget()
{
	if (RaidCompassWidgetInstance)
	{
		if (!RaidCompassWidgetInstance->IsInViewport())
		{
			RaidCompassWidgetInstance->AddToViewport(10);
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		PC = World->GetFirstPlayerController();
		if (!PC || PC->GetPawn() != this)
		{
			return;
		}
	}

	if (!PC->IsLocalController())
	{
		return;
	}

	UClass* WidgetClass = RaidCompassWidgetClass.Get();
	if (!WidgetClass)
	{
		WidgetClass = RaidCompassWidgetClass.LoadSynchronous();
		if (!WidgetClass)
		{
			WidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidCompass.WBP_RaidCompass_C"));
			if (WidgetClass)
			{
				RaidCompassWidgetClass = WidgetClass;
			}
		}
	}

	if (!WidgetClass)
	{
		return;
	}

	RaidCompassWidgetInstance = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!RaidCompassWidgetInstance)
	{
		return;
	}

	RaidCompassWidgetInstance->AddToViewport(10);
	UE_LOG(LogTemp, Log, TEXT("[GAS_MainCharacterCpp] Compass widget created: %s"), *GetNameSafe(WidgetClass));
}



// Called to bind functionality to input
void AGAS_MainCharacterCpp::OnMouseWheelAxisInput(float Value)
{
	PreviousMouseWheelAxisValue = Value;
}

void AGAS_MainCharacterCpp::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindAxis(TEXT("MouseWheelAxis"), this, &AGAS_MainCharacterCpp::OnMouseWheelAxisInput);
	}
}

UAbilitySystemComponent* AGAS_MainCharacterCpp::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AGAS_MainCharacterCpp::InitializeAttributes()
{
	if (AbilitySystemComponent && DefaultAttributeEffect)
	{
		FGameplayEffectContextHandle EffectContext = AbilitySystemComponent->MakeEffectContext();
		EffectContext.AddSourceObject(this);

		FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(DefaultAttributeEffect, 1, EffectContext);

		if (SpecHandle.IsValid())
		{
			FActiveGameplayEffectHandle GHandle = AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}
}

void AGAS_MainCharacterCpp::GiveAbilities()
{
	//GEngine->AddOnScreenDebugMessage(0, 1, FColor::Cyan, "Pysk", true);
	if (AbilitiesData && AbilitySystemComponent)
	{
		AbilitiesData->GiveAbilities(AbilitySystemComponent, this);
	}
}

void AGAS_MainCharacterCpp::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent->AbilityActorInfo.IsValid() == false)
	{
		GEngine->AddOnScreenDebugMessage(0, 3, FColor::Red, "GAS ERROR - AbilityActorInfo is NOT valid ", true);
		return;
	}
	AbilitySystemComponent->AbilityActorInfo->InitFromActor(this, this, AbilitySystemComponent);
	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	InitializeAttributes();
	GiveAbilities();
	EnsureRaidCompassWidget();

}

void AGAS_MainCharacterCpp::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	InitializeAttributes();
	EnsureRaidCompassWidget();
}

void AGAS_MainCharacterCpp::TryCreateInputsGAS()
{
	if (AbilitySystemComponent && InputComponent)
	{
		FTopLevelAssetPath PathToEnum = FTopLevelAssetPath(TEXT("/Script/IWALS_AbilitySystem.EIWALS_AbilityInputBinds"));
		const FGameplayAbilityInputBinds Binds("JumpAction", "AimActionType_2", PathToEnum, static_cast<int32>(EIWALS_AbilityInputBinds::JumpAction), static_cast<int32>(EIWALS_AbilityInputBinds::AimActionType_2));
		AbilitySystemComponent->BindAbilityActivationToInputComponent(InputComponent, Binds);
	}
}

