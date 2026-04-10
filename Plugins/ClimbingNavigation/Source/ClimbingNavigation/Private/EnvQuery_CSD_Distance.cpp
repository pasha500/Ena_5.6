


#include "EnvQuery_CSD_Distance.h"

UEnvQuery_CSD_Distance::UEnvQuery_CSD_Distance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseContextAsActors = true;
	TestPurpose = EEnvTestPurpose::Score;
}

float UEnvQuery_CSD_Distance::SetScorePerPoint(FVector CurrentPoint, TArray<FVector> ContextLocations, TArray<AActor*> ContextActors)
{
	if (ContextActors.Num() > 0)
	{
		float DistValue = 0.0;

		AActor* ContextActor = Cast<AActor>(ContextActors[0]);
		if (ContextActor)
		{
			if (bIgnoreZ)
			{
				DistValue = FVector::Dist2D(ContextActor->GetActorLocation() + FVector(0, 0, ContextOffsetZ), CurrentPoint);
			}
			else
			{
				DistValue = FVector::Distance(ContextActor->GetActorLocation() + FVector(0, 0, ContextOffsetZ), CurrentPoint);
			}
		}

		if (bTryNormalize)
		{
			float NormalizedDist = FMath::GetMappedRangeValueClamped(FVector2D(RefNormalizationMin, RefNormalizationMax), FVector2D(0.0, 1.0), DistValue);

			NormalizedDist = ScaleByCurve(NormalizedDist);
			NormalizedDist = NormalizedDist * ScoreFactor;

			return ApplyFilter(NormalizedDist);
		}
		else
		{
			return ApplyFilter(ScaleByCurve(DistValue) * ScoreFactor);
		}
	}
	return 0.0f;
}
