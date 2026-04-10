


#include "EnvQuery_CompositeScoreDefine.h"

UEnvQuery_CompositeScoreDefine::UEnvQuery_CompositeScoreDefine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TestPurpose = EEnvTestPurpose::FilterAndScore;
	Context = nullptr;
	bUseContextAsActors = true;

	DistributionMode = ECompositeScoreDistribution::Linear;
	CustomCurve = nullptr;
	ScoreFactor = 1.0;
	bTryNormalize = true;

	FilterType = EEnvTestFilterType::Minimum;
	BoolValue.DefaultValue = true;
	FloatValueMin = 0.0f;
	FloatValueMax = 1.0f;

	bShowFilterOptions = (TestPurpose != EEnvTestPurpose::Score); // Pocz¹tkowa wartoœæ

}

float UEnvQuery_CompositeScoreDefine::SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors)
{
	return 0.0f;
}

float UEnvQuery_CompositeScoreDefine::ApplyFilter(float CurrentScore)
{
	if (TestPurpose == EEnvTestPurpose::Score) { return CurrentScore; }

	float ClampedScore = CurrentScore;

	switch (FilterType)
	{
	case EEnvTestFilterType::Minimum:
		if (CurrentScore <= FloatValueMin) ClampedScore = -1.0; 
		break;
	case EEnvTestFilterType::Maximum:
		if (CurrentScore >= FloatValueMax) ClampedScore = -1.0;
		break;
	case EEnvTestFilterType::Match:
		if (CurrentScore <= FloatValueMin) ClampedScore = -1.0;
		if (CurrentScore >= FloatValueMax) ClampedScore = -1.0;
		break;
	default:
		ClampedScore = CurrentScore;
		break;
	}
	return ClampedScore;

}

float UEnvQuery_CompositeScoreDefine::ScaleByCurve(float InValue)
{
	if (DistributionMode == ECompositeScoreDistribution::Linear)
	{
		return InValue;
	}
	else if (DistributionMode == ECompositeScoreDistribution::inverseLinear)
	{
		return FMath::Clamp<float>(1.0 - InValue, 0.0, 1.0);
	}
	else if (DistributionMode == ECompositeScoreDistribution::Squared)
	{
		return FMath::Pow(InValue, 2.0);
	}
	else if(DistributionMode == ECompositeScoreDistribution::Custom && IsValid(CustomCurve))
	{
		return CustomCurve->GetFloatValue(InValue);
	}
	else
	{
		return InValue;
	}
}



#if WITH_EDITOR
void UEnvQuery_CompositeScoreDefine::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UEnvQuery_CompositeScoreDefine, TestPurpose))
		{
			// Ukryj opcje filtrowania, jeœli TestPurpose == Score
			bShowFilterOptions = (TestPurpose != EEnvTestPurpose::Score);
		}
	}
}
#endif