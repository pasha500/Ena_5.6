


#include "EnvQueryTest_CustomScore.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"

UEnvQueryTest_CustomScore::UEnvQueryTest_CustomScore(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    ScoreModifyClass = UEnvQuery_CustomScoreDefine::StaticClass();
    Cost = EEnvTestCost::Low;
    ValidItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnvQueryTest_CustomScore::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	if (QueryOwner == nullptr)
	{
		return;
	}

    // SprawdŸ, czy klasa ScoreModifyClass zosta³a ustawiona
    if (!ScoreModifyClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("ScoreModifyClass is not set in UEnvQueryTest_CustomScore"));
        return;
    }

    // Utwórz instancjê klasy ScoreModifyClass
    UEnvQuery_CustomScoreDefine* ScoreModifier = NewObject<UEnvQuery_CustomScoreDefine>(QueryOwner, ScoreModifyClass);
    if (!ScoreModifier)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to create an instance of ScoreModifyClass"));
        return;
    }

    // Przygotuj kontekst
    TArray<AActor*> ContextActors;
    if (!QueryInstance.PrepareContext(CharactersContext, ContextActors))
    {
        return;
    }

    // Pobierz minimalny i maksymalny próg
    FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
    float MinThresholdValue = FloatValueMin.GetValue();

    FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
    float MaxThresholdValue = FloatValueMax.GetValue();

    //TArray<FVector> QueryPoints;
    //QueryInstance.GetAllAsLocations(QueryPoints);
    

    // Pobierz wszystkie punkty zapytania
    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        if (!QueryInstance.Items[It.GetIndex()].IsValid()) continue;

        if (ExecuteScoreTestOnlyWhen == true && QueryInstance.GetItemScore(It.GetIndex()) <= NotValidPointScoreTreshold)
        {
            //UE_LOG(LogTemp, Warning, TEXT("SKIP POINT"));
            if (DrawDebugOnSkippedPoint && GetWorld())
            {
                DrawDebugPoint(GetWorld(), GetItemLocation(QueryInstance, It.GetIndex()), 15, FColor::Red, false, 1.0, 1);
            }
            continue;
        }

        const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());
        FVector QuerierLocation = FVector(0,0,0);
        AActor* OwnerAsActor = Cast<AActor>(QueryOwner);
        if (OwnerAsActor)
        {
            QuerierLocation = OwnerAsActor->GetActorLocation();
        }
        ScoreModifier->ContextFromEQS = QueryInstance.World;
        // Wywo³aj funkcjê Blueprint dla danego punktu
        float ReturnScore = 0.0f;
        ScoreModifier->SetScorePerPoint(ReturnScore, ItemLocation, QuerierLocation, OwnerAsActor, ContextActors, It.GetIndex());

        // Zastosuj wynik do punktu
        It.SetScore(TestPurpose, FilterType, ReturnScore, MinThresholdValue, MaxThresholdValue);
    }

}

FText UEnvQueryTest_CustomScore::GetDescriptionTitle() const
{
	return FText::FromString(TEXT("Blueprint Based Score Logic"));
}

FText UEnvQueryTest_CustomScore::GetDescriptionDetails() const
{
    // Pobierz nazwê klasy z ScoreModifyClass
    FString ScoreClassName = ScoreModifyClass ? GetNameSafe(ScoreModifyClass.Get()) : TEXT("None");

    // Po³¹cz opis z nazw¹ klasy
    return FText::FromString(FString::Printf(TEXT("Custom score logic defined in Blueprint.\nScore Modify Class: %s"), *ScoreClassName));
}
