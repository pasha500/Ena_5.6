

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DataAsset.h"
#include "NavQuery_HidingLocSearchParams.generated.h"

UENUM(BlueprintType)
enum class EDistributionFunctionType : uint8
{
	Linear,
	Exp,
	Exp2,
	Power,
	Sin,
	Cos,
	Sinh,
};

USTRUCT(BlueprintType)
struct FWeightFunction : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
	float Weight = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight")
	EDistributionFunctionType DistributionFunction = EDistributionFunctionType::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight", meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.1", UIMax = "10.0"))
	float PowerExponent = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float WeightBias = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight")
	bool bInverseRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight")
	float MinAccetableValueFilter = -1.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Read Only")
	FRuntimeFloatCurve DistributionCurve;

	// Funkcja aktualizuj¹ca krzyw?dystrybucji w zale¿noœci od parametr?
	void UpdateDistributionCurve()
	{
		DistributionCurve.EditorCurveData.Reset();
		const int NumPoints = 20; // Liczba punkt? do wygenerowania
		const float Step = 1.0f / (NumPoints - 1);

		for (int i = 0; i < NumPoints; ++i)
		{
			float X = i * Step;
			float Y = CalculateDistributionValue(X);

			// Dodaj punkt do krzywej
			FKeyHandle KeyHandle = DistributionCurve.EditorCurveData.AddKey(X, Y);

			// Ustaw tryb stycznych na Smart Auto dla tego punktu
			//DistributionCurve.EditorCurveData.SetKeyInterpMode(KeyHandle, ERichCurveInterpMode::RCIM_Cubic);
			//DistributionCurve.EditorCurveData.SetKeyTangentWeightMode(KeyHandle, ERichCurveTangentWeightMode::RCTWM_WeightedArrive, true);
			//DistributionCurve.EditorCurveData.SetKeyTangentMode(KeyHandle, ERichCurveTangentMode::RCTM_Auto);
			
		}

	}

private:
	// Funkcja pomocnicza obliczaj¹ca wartoœæ dystrybucji
	float CalculateDistributionValue(float X) const
	{
		switch (DistributionFunction)
		{
		case EDistributionFunctionType::Linear:
			return X;
		case EDistributionFunctionType::Exp:
			return FMath::Exp(X) - 1.0f;
		case EDistributionFunctionType::Exp2:
			return FMath::Exp2(X) - 1.0f;
		case EDistributionFunctionType::Power:
			return FMath::Pow(X, PowerExponent);
		case EDistributionFunctionType::Sin:
			return FMath::Sin(X * PI);
		case EDistributionFunctionType::Cos:
			return FMath::Cos(X * PI);
		case EDistributionFunctionType::Sinh:
			return FMath::Sinh(X * PowerExponent);
		default:
			return X;
		}
	}
};



UCLASS()
class CLIMBINGNAVIGATION_API UNavQuery_HidingLocSearchParams : public UDataAsset
{
	GENERATED_BODY()

public:

	UNavQuery_HidingLocSearchParams()
	{
		// Generowanie domyœlnych wykres? w konstruktorze
		PathLenghtToPointWeight.UpdateDistributionCurve();
		DistanceToNearestEnemyWeight.UpdateDistributionCurve();
		ReachOriginWeight.Weight = 0.0; // Zmiana wartoœci domyœlnej
		ReachOriginWeight.UpdateDistributionCurve();
		PointAngleWeight.UpdateDistributionCurve();
		WallHitWeight.Weight = 0.0;
		WallHitWeight.UpdateDistributionCurve();
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Search Radius", meta=(ClampMin=100.0, ClampMax = 6000))
	float FindingPointsRadius = 1000.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Search Radius", meta = (ClampMin = 100.0, ClampMax = 6000))
	float EnemySearchRadius = 1200.0;

/*An important value that determines the operating mode of the element evaluating the weights for individual points. 
In the default minimization mode, the points with the lowest weights will be considered. Otherwise, the function will 
seek the highest weight value for CoverPoint.*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Search Type")
	bool UseAsMaximalizeSearcher = false;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Weights")
	FWeightFunction PathLenghtToPointWeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Weights")
	FWeightFunction DistanceToNearestEnemyWeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Weights")
	FWeightFunction ReachOriginWeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Weights")
	FWeightFunction PointAngleWeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Weights")
	FWeightFunction WallHitWeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	bool NormalizePathWeights = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float AsCorrectPathWeightMin = 0.4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float MinValidPointDistToEnemy = 80.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	bool AlwaysIgnorePlayerAsEnemy = true;

	//this is the ratio of TraceOrigin to CurrentCoverPoint
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Angle Weight", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float BaseDistanceCalcOnCurrentPointAlpha = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Angle Weight", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float SlerpAngleWhenPointIsInNegativePlane = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Angle Weight")
	bool ApplyAbsToAngleCalculationMap = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Path Checker")
	int ConstPathSubdividor = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Path Checker")
	bool UseDynamicSubdividion = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Path Checker")
	float DynamicSubSegmentLenght = 80.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Accuracy", meta = (ClampMin = 1, ClampMax = 25))
	int MaxPotencialPathsCreation = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Tracing")
	float CoverPointHeightOffset = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Tracing", meta = (ClampMin = 0.1, ClampMax = 10.0))
	float SightCylinderHeightScale = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Tracing", meta = (ClampMin = 0.2, ClampMax = 20.0))
	float WallCheckTraceRadius = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Tracing")
	TEnumAsByte<ECollisionChannel> PointsFindingChannel = ECollisionChannel::ECC_PhysicsBody;


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		// Pobierz nazw?zmienionej w³aœciwoœci
		const FName PropertyName = PropertyChangedEvent.GetPropertyName();

		// Sprawd? czy zmieniono jedn?z w³aœciwoœci w strukturach
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FWeightFunction, Weight) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FWeightFunction, DistributionFunction) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FWeightFunction, PowerExponent) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FWeightFunction, WeightBias)
			)
		{
			// Odœwie?krzywe w strukturach
			PathLenghtToPointWeight.UpdateDistributionCurve();
			DistanceToNearestEnemyWeight.UpdateDistributionCurve();
			ReachOriginWeight.UpdateDistributionCurve();
			PointAngleWeight.UpdateDistributionCurve();
			WallHitWeight.UpdateDistributionCurve();
		}
	}
#endif


};

