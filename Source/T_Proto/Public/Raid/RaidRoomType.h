#pragma once

#include "CoreMinimal.h"
#include "RaidRoomType.generated.h"

UENUM(BlueprintType)
enum class ERaidRoomType : uint8
{
	Start	UMETA(DisplayName = "Start"),
	Normal	UMETA(DisplayName = "Normal"),
	Combat	UMETA(DisplayName = "Combat"),
	Loot	UMETA(DisplayName = "Loot"), // [추가] 파밍 방
	Boss	UMETA(DisplayName = "Boss"),
	Exit	UMETA(DisplayName = "Exit"),
	Unknown UMETA(DisplayName = "Unknown")
};

UENUM(BlueprintType)
enum class ERaidRoomRole : uint8
{
	None	UMETA(DisplayName = "None"),
	Safe	UMETA(DisplayName = "Safe"),
	Combat	UMETA(DisplayName = "Combat"),
	Loot	UMETA(DisplayName = "Loot"), // [추가] 파밍 역할
	Boss	UMETA(DisplayName = "Boss"),
	Exit	UMETA(DisplayName = "Exit"),
	Unknown UMETA(DisplayName = "Unknown")
};

namespace RaidRoomParsing
{
	inline ERaidRoomType ParseRoomType(const FString& In)
	{
		if (In.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) return ERaidRoomType::Start;
		if (In.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) return ERaidRoomType::Normal;
		if (In.Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) return ERaidRoomType::Combat;
		if (In.Equals(TEXT("Loot"), ESearchCase::IgnoreCase)) return ERaidRoomType::Loot;
		if (In.Equals(TEXT("Boss"), ESearchCase::IgnoreCase)) return ERaidRoomType::Boss;
		if (In.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) return ERaidRoomType::Exit;
		return ERaidRoomType::Unknown;
	}

	inline ERaidRoomRole ParseRoomRole(const FString& In)
	{
		if (In.Equals(TEXT("Safe"), ESearchCase::IgnoreCase)) return ERaidRoomRole::Safe;
		if (In.Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) return ERaidRoomRole::Combat;
		if (In.Equals(TEXT("Loot"), ESearchCase::IgnoreCase)) return ERaidRoomRole::Loot;
		if (In.Equals(TEXT("Boss"), ESearchCase::IgnoreCase)) return ERaidRoomRole::Boss;
		if (In.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) return ERaidRoomRole::Exit;
		if (In.IsEmpty()) return ERaidRoomRole::None;
		return ERaidRoomRole::Unknown;
	}
}