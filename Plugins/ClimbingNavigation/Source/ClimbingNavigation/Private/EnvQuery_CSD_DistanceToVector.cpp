


#include "EnvQuery_CSD_DistanceToVector.h"

UEnvQuery_CSD_DistanceToVector::UEnvQuery_CSD_DistanceToVector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseContextAsActors = false;
	TestPurpose = EEnvTestPurpose::Score;
}

float UEnvQuery_CSD_DistanceToVector::SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors)
{
	if (ContextLocations.Num() > 0)
	{
		float DistValue = 0.0;

		if (bIgnoreZ)
		{
			for (FVector V1 : ContextLocations)
			{
				DistValue = DistValue + FVector::Dist2D(V1 + FVector(0, 0, ContextOffsetZ), CurrentPoint);
			}
		}
		else
		{
			for (FVector V1 : ContextLocations)
			{
				DistValue = DistValue + FVector::Distance(V1 + FVector(0, 0, ContextOffsetZ), CurrentPoint);
			}
		}

		if (bTryNormalize)
		{
			float NormalizedDist = FMath::GetMappedRangeValueClamped(FVector2D(RefNormalizationMin, RefNormalizationMax), FVector2D(0.0, 1.0), DistValue / (float)ContextLocations.Num());

			NormalizedDist = ScaleByCurve(NormalizedDist);
			NormalizedDist = NormalizedDist * ScoreFactor;

			return ApplyFilter(NormalizedDist);
		}
		else
		{
			return ApplyFilter(ScaleByCurve(DistValue / (float)ContextLocations.Num()) * ScoreFactor);
		}
	}
	return 0.0f;
}
