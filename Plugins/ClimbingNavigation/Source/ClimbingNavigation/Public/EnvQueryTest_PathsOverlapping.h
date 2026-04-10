

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_PathsOverlapping.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UEnvQueryTest_PathsOverlapping : public UEnvQueryTest
{
	GENERATED_BODY()

public:
    UEnvQueryTest_PathsOverlapping(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

    UPROPERTY(EditDefaultsOnly, Category = "Context")
    TSubclassOf<UEnvQueryContext> CharactersContext;

    UPROPERTY(EditDefaultsOnly, Category = "Context")
    float AsOverlappingTollerance = 28.0;

    /*If we apply this method, then in such a case
the path will be checked from the EnvQuerier position to the Point Location. If such a path is found, then in such a 
we check if any of the segments overlaps with the position taken from 'Context Characters'. If we leave the value to 
true, then in such a case we do not generate the path, but it is taken from 'Context Characters'.*/
    UPROPERTY(EditDefaultsOnly, Category = "Context")
    bool bUsePathToPointMethod = false;

};
