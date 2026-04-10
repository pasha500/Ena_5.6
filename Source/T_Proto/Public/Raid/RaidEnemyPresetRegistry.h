#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Raid/RaidEnemyPresetTypes.h"
#include "RaidEnemyPresetRegistry.generated.h"

class USkeletalMesh;
class UAnimInstance;
class APawn;

UCLASS(BlueprintType)
class T_PROTO_API URaidEnemyPresetRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Raid|EnemyPreset")
	bool ResolvePreset(FName PresetId, FRaidEnemyPreset& OutPreset) const;

	UFUNCTION(BlueprintCallable, Category = "Raid|EnemyPreset")
	void PreloadAllEnemyPresetClasses(bool bLogSummary = false) const;

	/** PresetId로 enemy pawn class 1개를 랜덤 선택해서 반환 (SoftClassPtr LoadSynchronous) */
	UFUNCTION(BlueprintCallable, Category = "Raid|EnemyPreset")
	TSubclassOf<APawn> ResolveEnemyClassFromPreset(FName PresetId) const;

	/** 런타임에 클래스 경로를 preset에 추가 (필요시) */
	UFUNCTION(BlueprintCallable, Category = "Raid|EnemyPreset")
	bool AddEnemyClassPath(FName PresetId, const FString& ClassPathString, float Weight = 1.f);

	// 스폰된 Pawn의 메시/AnimClass가 비어 있으면 기본값으로 자동 복구
	UFUNCTION(BlueprintCallable, Category = "Raid|EnemyPreset")
	bool TryRepairSpawnedPawn(APawn* SpawnedPawn, FString& OutFixLog) const;

private:
	UPROPERTY(EditAnywhere, Category = "Raid|EnemyPreset")
	TMap<FName, FRaidEnemyPreset> Presets;

	UPROPERTY(EditAnywhere, Category = "Raid|EnemyPreset|Repair")
	bool bAutoRepairInvalidPawnAssets = true;

	UPROPERTY(EditAnywhere, Category = "Raid|EnemyPreset|Repair", meta = (EditCondition = "bAutoRepairInvalidPawnAssets"))
	TSoftObjectPtr<USkeletalMesh> RepairFallbackSkeletalMesh;

	UPROPERTY(EditAnywhere, Category = "Raid|EnemyPreset|Repair", meta = (EditCondition = "bAutoRepairInvalidPawnAssets"))
	TSoftClassPtr<UAnimInstance> RepairFallbackAnimClass;
};
