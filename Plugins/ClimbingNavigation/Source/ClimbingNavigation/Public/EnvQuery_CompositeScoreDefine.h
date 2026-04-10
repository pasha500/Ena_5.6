

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryNode.h"
#include "Templates/SubclassOf.h"
#include "EnvironmentQuery/Items/EnvQueryItemType.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "DataProviders/AIDataProvider.h"
#include "Curves/CurveFloat.h"
#include "UObject/ObjectMacros.h"
#include "EnvQuery_CompositeScoreDefine.generated.h"

UENUM()
enum class ECompositeScoreDistribution :uint8
{
	Linear,
	inverseLinear,
	Squared,
	Custom
};


/**
 * 
 */
UCLASS(EditInlineNew, Abstract, DefaultToInstanced, meta = (Category = "Score Define"))
class CLIMBINGNAVIGATION_API UEnvQuery_CompositeScoreDefine : public UEnvQueryNode
{
	GENERATED_BODY()

public:

	UEnvQuery_CompositeScoreDefine(const FObjectInitializer& ObjectInitializer);

	virtual float SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors);

	virtual float ApplyFilter(float CurrentScore);

	virtual float ScaleByCurve(float InValue);

	/** The purpose of this test.  Should it be used for filtering possible results, scoring them, or both? */
	UPROPERTY(EditDefaultsOnly, Category = Test)
	TEnumAsByte<EEnvTestPurpose::Type> TestPurpose;

	UWorld* ContextWorld;

	/** Optional comment or explanation about what this test is for.  Useful when the purpose of tests may not be clear,
	  * especially when there are multiple tests of the same type. */
	UPROPERTY(EditDefaultsOnly, Category = Test)
	FString TestComment;

	UPROPERTY(EditDefaultsOnly, Category = Context)
	TSubclassOf<UEnvQueryContext> Context;

	bool bUseContextAsActors;

	UPROPERTY(EditDefaultsOnly, Category = Score, meta = (EditCondition = "bShowScoreOptions", EditConditionHides))
	ECompositeScoreDistribution DistributionMode;

	UPROPERTY(EditDefaultsOnly, Category = Score, meta = (EditCondition = "bShowScoreOptions", EditConditionHides))
	UCurveFloat* CustomCurve;

	UPROPERTY(EditDefaultsOnly, Category = Score, meta = (EditCondition = "bShowScoreOptions", EditConditionHides))
	float ScoreFactor;

	UPROPERTY(EditDefaultsOnly, Category = Score, meta = (EditCondition = "bShowScoreOptions", EditConditionHides))
	bool bTryNormalize;


	/** Should the filtering options be displayed? */
	bool bShowFilterOptions;
	bool bShowScoreOptions = true;

	/** Does this test filter out results that are below a lower limit, above an upper limit, or both?  Or does it just look for a matching value? */
	UPROPERTY(EditDefaultsOnly, Category = "Filter", meta = (EditCondition = "bShowFilterOptions", EditConditionHides))
	TEnumAsByte<EEnvTestFilterType::Type> FilterType;

	/** Desired boolean value of the test for scoring to occur or filtering test to pass. */
	UPROPERTY(EditDefaultsOnly, Category = "Filter", meta = (EditCondition = "bShowFilterOptions", EditConditionHides))
	FAIDataProviderBoolValue BoolValue;

	/** Minimum limit (inclusive) of valid values for the raw test value. Lower values will be discarded as invalid. */
	UPROPERTY(EditDefaultsOnly, Category = "Filter", meta = (EditCondition = "bShowFilterOptions", EditConditionHides))
	float FloatValueMin;

	/** Maximum limit (inclusive) of valid values for the raw test value. Higher values will be discarded as invalid. */
	UPROPERTY(EditDefaultsOnly, Category = "Filter", meta = (EditCondition = "bShowFilterOptions", EditConditionHides))
	float FloatValueMax;
	
#if WITH_EDITOR
protected:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};
