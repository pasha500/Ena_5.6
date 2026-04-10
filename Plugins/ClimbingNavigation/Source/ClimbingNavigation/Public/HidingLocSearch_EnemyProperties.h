

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HidingLocSearch_EnemyProperties.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class CLIMBINGNAVIGATION_API UHidingLocSearch_EnemyProperties : public UObject
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Search For Hide Location|Enemy", meta = (ForceAsFunction, DisplayName = "Get Predicted Enemy Transform", Keywords = "Hiding,Query"))
	void GetPredictedEnemyTransform(FVector& ReturnLocation, FRotator& ReturnRotation, ACharacter* CurrentEnemy, ACharacter* Searcher, int IterationIndex);
	virtual void GetPredictedEnemyTransform_Implementation(FVector& ReturnLocation, FRotator& ReturnRotation, ACharacter* CurrentEnemy, ACharacter* Searcher, int IterationIndex);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Search For Hide Location|Enemy", meta = (ForceAsFunction, DisplayName = "Get Current Enemy Sight Params", Keywords = "Hiding,Query"))
	void GetCurrentEnemySightParams(float& HalfAngle, float& SightRadius, float& SightHeight, ACharacter* CurrentEnemy, ACharacter* Searcher);
	virtual void GetCurrentEnemySightParams_Implementation(float& HalfAngle, float& SightRadius, float& SightHeight, ACharacter* CurrentEnemy, ACharacter* Searcher);

};
