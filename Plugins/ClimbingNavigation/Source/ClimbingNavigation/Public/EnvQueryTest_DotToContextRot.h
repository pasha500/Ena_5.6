

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_DotToContextRot.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "Dot To Context Actors"))
class CLIMBINGNAVIGATION_API UEnvQueryTest_DotToContextRot : public UEnvQueryTest
{
	GENERATED_BODY()

public:
    UEnvQueryTest_DotToContextRot(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;


    UPROPERTY(EditDefaultsOnly, Category = "Context")
    TSubclassOf<UEnvQueryContext> CharactersContext;

    UPROPERTY(EditDefaultsOnly, Category = "Test Config")
    float SearchForNavPointInstRadius = 5.0;

    UPROPERTY(EditDefaultsOnly, Category = "Test Config")
    TEnumAsByte<ECollisionChannel> TraceForNavPointChannel;

    UPROPERTY(EditDefaultsOnly, Category = "Test Config")
    bool bRequiredTagFromHit = false;

    UPROPERTY(EditDefaultsOnly, Category = "Test Config")
    FName TagName = TEXT("");

    UPROPERTY(EditDefaultsOnly, Category = "Test Config")
    bool bTryUseControlRotation = true;

    UPROPERTY(EditDefaultsOnly, Category = "Testing")
    float PowExp01 = 0.5;

    UPROPERTY(EditDefaultsOnly, Category = "Testing")
    float PowExp02 = 1;



};
