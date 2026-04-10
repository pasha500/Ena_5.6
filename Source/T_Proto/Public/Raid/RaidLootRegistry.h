#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RaidLootRegistry.generated.h"

UENUM(BlueprintType)
enum class ERaidLootCategory : uint8 {
    Rifle,
    Pistol,
    Backpack,
    Ammo,
    Prop
};

USTRUCT(BlueprintType)
struct FRaidLootCandidate {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ERaidLootCategory Category = ERaidLootCategory::Rifle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AActor> ItemClass; // 예: BP_I_Rifle_Actor_For_Player

    // DataTable의 RowName (예: M4A1, DesertDeagle)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName DataRowName;

    // 랜덤 가중치
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct FRaidLootGroup {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FRaidLootCandidate> Candidates;
};

UCLASS(Blueprintable, BlueprintType)
class T_PROTO_API URaidLootRegistry : public UDataAsset {
    GENERATED_BODY()

public:
    // Key: "High", "Medium", "Low"
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Raid|Loot")
    TMap<FString, FRaidLootGroup> LootGroups;

    const FRaidLootCandidate* GetRandomCandidate(const FString& LootLevel) const;
};