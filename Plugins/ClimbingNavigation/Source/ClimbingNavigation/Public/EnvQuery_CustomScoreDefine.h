

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectMacros.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GameFramework/Actor.h"
#include "EnvQuery_CustomScoreDefine.generated.h"

class AActor;
/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class CLIMBINGNAVIGATION_API UEnvQuery_CustomScoreDefine : public UObject
{
	GENERATED_BODY()
	
	// We need to implement GetWorld() so that blueprint functions which use a hidden WorldContextObject* will work properly.
	virtual UWorld* GetWorld() const override;

public:
	UFUNCTION(BlueprintImplementableEvent, Category = "EQS Score", meta = (DisplayName = "Set Score Per Point", Keywords = "EQS,Score"))
	void SetScorePerPoint(float& ReturnScore, FVector CurrentPointPosition, FVector QuerierLocation, AActor* QuerierActor, const TArray<AActor*>& ContextActors, int CurrentPointIndex) const;

	UWorld* ContextFromEQS = nullptr;
};
