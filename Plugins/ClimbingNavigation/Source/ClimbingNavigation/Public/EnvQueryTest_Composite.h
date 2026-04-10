

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQuery_CompositeScoreDefine.h"
#include "EnvQueryTest_Composite.generated.h"


UENUM()
enum class EScoreFoldingMethod :uint8
{
    Sum,
    Multiply
};

/**
 * 
 */
UCLASS(meta = (DisplayName = "Composite"))
class CLIMBINGNAVIGATION_API UEnvQueryTest_Composite : public UEnvQueryTest
{
	GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Instanced, Category = Tests)
    TArray<TObjectPtr<UEnvQuery_CompositeScoreDefine>> Tests;

    UPROPERTY(EditDefaultsOnly, Category = Tests)
    EScoreFoldingMethod FoldingMethod;

public:
    UEnvQueryTest_Composite(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

	
};
