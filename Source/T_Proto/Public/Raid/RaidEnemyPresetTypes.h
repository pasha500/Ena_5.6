#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "GameFramework/Pawn.h"
#include "RaidEnemyPresetTypes.generated.h"

USTRUCT(BlueprintType)
struct T_PROTO_API FRaidEnemyPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid|EnemyPreset")
	FName PresetId = NAME_None;

	/** Pawn 클래스들 (Soft Reference) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid|EnemyPreset")
	TArray<TSoftClassPtr<APawn>> EnemyClasses;

	/** 가중치 배열 (EnemyClasses와 병렬) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid|EnemyPreset")
	TArray<float> Weights;

	bool IsValid() const
	{
		return EnemyClasses.Num() > 0;
	}

	/** 가중치 기반 랜덤 선택 로직 */
	TSoftClassPtr<APawn> PickRandomSoftClass(FRandomStream& Rng) const
	{
		if (EnemyClasses.Num() == 0) return TSoftClassPtr<APawn>();

		// Weights 불일치면 균등 랜덤
		if (Weights.Num() != EnemyClasses.Num())
		{
			return EnemyClasses[Rng.RandRange(0, EnemyClasses.Num() - 1)];
		}

		float Total = 0.f;
		for (float W : Weights) Total += FMath::Max(0.f, W);

		if (Total <= KINDA_SMALL_NUMBER)
		{
			return EnemyClasses[Rng.RandRange(0, EnemyClasses.Num() - 1)];
		}

		float Pick = Rng.FRandRange(0.f, Total);
		for (int32 i = 0; i < EnemyClasses.Num(); ++i)
		{
			Pick -= FMath::Max(0.f, Weights[i]);
			if (Pick <= 0.f) return EnemyClasses[i];
		}

		return EnemyClasses.Last();
	}
};
