

#pragma once

#include "CoreMinimal.h"
#include "EnvQuery_CompositeScoreDefine.h"
#include "EnvQuery_CSD_Distance.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "DistanceToActor"))
class CLIMBINGNAVIGATION_API UEnvQuery_CSD_Distance : public UEnvQuery_CompositeScoreDefine
{
	GENERATED_BODY()

public:

	UEnvQuery_CSD_Distance(const FObjectInitializer& ObjectInitializer);

	virtual float SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors) override;

protected:

	UPROPERTY(EditDefaultsOnly, Category = Distance)
	bool bIgnoreZ = false;

	UPROPERTY(EditDefaultsOnly, Category = Distance)
	float ContextOffsetZ = 0.0;

	UPROPERTY(EditDefaultsOnly, Category = Score)
	float RefNormalizationMax = 1000.0;

	UPROPERTY(EditDefaultsOnly, Category = Score)
	float RefNormalizationMin = 0.0;

};
