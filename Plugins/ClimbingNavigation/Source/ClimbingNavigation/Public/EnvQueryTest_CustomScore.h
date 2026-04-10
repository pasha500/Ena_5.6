

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQuery_CustomScoreDefine.h"
#include "EnvQueryTest_CustomScore.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UEnvQueryTest_CustomScore : public UEnvQueryTest
{
	GENERATED_BODY()
	
public:
    UEnvQueryTest_CustomScore(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

private:
    UPROPERTY(EditDefaultsOnly, Category = ScoreLogic)
    TSubclassOf<UEnvQuery_CustomScoreDefine> ScoreModifyClass;

    UPROPERTY(EditDefaultsOnly, Category = AddtiveContext)
    TSubclassOf<UEnvQueryContext> CharactersContext;

    //Execute Score Test Only When Current Point Have Score Greater That NotValidPointScoreTreshold
    UPROPERTY(EditDefaultsOnly, Category = ScoreLogic)
    bool ExecuteScoreTestOnlyWhen = false;

    UPROPERTY(EditDefaultsOnly, Category = ScoreLogic, meta = (EditCondition = "ExecuteScoreTestOnlyWhen"))
    float NotValidPointScoreTreshold = 0.0;

    UPROPERTY(EditDefaultsOnly, Category = ScoreLogic, meta = (EditCondition = "ExecuteScoreTestOnlyWhen"))
    bool DrawDebugOnSkippedPoint = false;

};
