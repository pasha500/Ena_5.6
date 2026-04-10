


#include "EnvQueryTest_Composite.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/EnvQueryContext.h"

UEnvQueryTest_Composite::UEnvQueryTest_Composite(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

    FoldingMethod = EScoreFoldingMethod::Sum;

	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
}

void UEnvQueryTest_Composite::RunTest(FEnvQueryInstance& QueryInstance) const
{
    UObject* QueryOwner = QueryInstance.Owner.Get();
    if (!QueryOwner)
    {
        return;
    }

    UObject* DataOwner = QueryInstance.Owner.Get();
    BoolValue.BindData(DataOwner, QueryInstance.QueryID);
    
    
    // Przygotuj zmienne dla kontekst
    TArray<FVector> ContextLocations;
    TArray<AActor*> ContextActors;

    FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
    float MinThresholdValue = FloatValueMin.GetValue();

    FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
    float MaxThresholdValue = FloatValueMax.GetValue();

    if (Tests.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("EQS Composite: Tests instanced not definied. Please add at least one test class"));
        return;
    }

    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());

        float ScoreFolding = 0.0;

        for (TObjectPtr<UEnvQuery_CompositeScoreDefine> TestInstance : Tests)
        {
            if (!TestInstance.Get())
            {
                continue;
            }

            if (TestInstance.Get()->Context != nullptr)
            {
                TestInstance.Get()->ContextWorld = QueryInstance.World; //<- Set WORLD

                if (TestInstance.Get()->bUseContextAsActors)
                { QueryInstance.PrepareContext(TestInstance.Get()->Context, ContextActors); }
                else
                { QueryInstance.PrepareContext(TestInstance.Get()->Context, ContextLocations); }
                
            }

            float NewScore = TestInstance.Get()->SetScorePerPoint(ItemLocation, ContextLocations, ContextActors);

            if (NewScore < -0.5)
            {
                ScoreFolding = 0.0;
                continue;
            }
            else
            {
                if (FoldingMethod == EScoreFoldingMethod::Sum)
                {
                    ScoreFolding = ScoreFolding + NewScore;
                }
                else
                {
                    ScoreFolding = ScoreFolding * NewScore;
                }
            }

            
        }

        It.SetScore(TestPurpose, FilterType, ScoreFolding, MinThresholdValue, MaxThresholdValue);
    }

}

FText UEnvQueryTest_Composite::GetDescriptionTitle() const
{
	return FText::FromString(TEXT("Composite (Not Finished)"));
}

FText UEnvQueryTest_Composite::GetDescriptionDetails() const
{
	return FText();
}
