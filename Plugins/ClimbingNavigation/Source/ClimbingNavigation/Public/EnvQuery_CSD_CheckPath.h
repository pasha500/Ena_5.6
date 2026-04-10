

#pragma once

#include "CoreMinimal.h"
#include "EnvQuery_CompositeScoreDefine.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "EnvQuery_CSD_CheckPath.generated.h"

UENUM()
enum class EPathTestMode :uint8
{
	NoCheck,
	OnlyAsValid,
	WithDistance
};


UCLASS(meta = (DisplayName = "CheckPath"))
class CLIMBINGNAVIGATION_API UEnvQuery_CSD_CheckPath : public UEnvQuery_CompositeScoreDefine
{
	GENERATED_BODY()
	
public:

	UEnvQuery_CSD_CheckPath(const FObjectInitializer& ObjectInitializer);

	virtual float SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors) override;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Path Test")
	EPathTestMode PathTestMode = EPathTestMode::WithDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Path Test")
	bool bCheckCost = false;

	UPROPERTY(EditDefaultsOnly, Category = "Path Test")
	bool bCheckFromContext = true;

	UPROPERTY(EditDefaultsOnly, Category = "Path Test")
	TSubclassOf<UNavigationQueryFilter> NavFilter = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = Score)
	float RefNormalizationMin = 0.0;

	UPROPERTY(EditDefaultsOnly, Category = Score)
	float RefNormalizationMax = 1000.0;

};
