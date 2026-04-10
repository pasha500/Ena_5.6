#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RaidWaveProfile.generated.h"

class APawn;

USTRUCT(BlueprintType)
struct T_PROTO_API FRaidWaveEnemyDescriptor
{
    GENERATED_BODY()

    // Actor class priority: if valid, this class is spawned first.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Enemy")
    TSoftClassPtr<APawn> EnemyClass;

    // Fallback preset when EnemyClass is empty or failed to load.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Enemy")
    FName EnemyPreset = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Enemy", meta = (ClampMin = "1", ClampMax = "256"))
    int32 SpawnCount = 4;
};

USTRUCT(BlueprintType)
struct T_PROTO_API FRaidWaveStage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
    FText WaveLabel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
    TArray<FRaidWaveEnemyDescriptor> Enemies;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Distance", meta = (ClampMin = "1500.0", ClampMax = "180000.0"))
    float SpawnMinDistanceFromPlayer = 5500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Distance", meta = (ClampMin = "2000.0", ClampMax = "220000.0"))
    float SpawnMaxDistanceFromPlayer = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave|Distance", meta = (ClampMin = "200.0", ClampMax = "8000.0"))
    float SpawnScatterRadius = 1400.0f;
};

UCLASS(BlueprintType)
class T_PROTO_API URaidWaveProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General")
    bool bEnableDynamicWaves = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General", meta = (ClampMin = "1", ClampMax = "100"))
    int32 TotalDynamicWaves = 10;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General", meta = (ClampMin = "0.0", ClampMax = "600.0"))
    float FirstWaveDelaySeconds = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General", meta = (ClampMin = "5.0", ClampMax = "600.0"))
    float WaveIntervalSeconds = 70.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General", meta = (ClampMin = "1", ClampMax = "512"))
    int32 MaxAliveWaveEnemies = 64;

    // If TotalDynamicWaves is larger than Stages.Num(), repeat the last stage.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave|General")
    bool bLoopLastStageWhenShort = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
    TArray<FRaidWaveStage> Stages;
};

