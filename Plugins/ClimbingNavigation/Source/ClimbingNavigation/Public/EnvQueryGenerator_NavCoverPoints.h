

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "EnvQueryGenerator_NavCoverPoints.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UEnvQueryGenerator_NavCoverPoints : public UEnvQueryGenerator
{
	GENERATED_BODY()
	
public:
    UEnvQueryGenerator_NavCoverPoints(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;

    virtual void StoreNavPoints(const TArray<FNavLocation>& Points, FEnvQueryInstance& QueryInstance) const;

private:

    /** context */
    UPROPERTY(EditDefaultsOnly, Category = Generator)
    TSubclassOf<UEnvQueryContext> GenerateAround;


    /** Promieñ dla SphereTraceMultiByProfile */
    UPROPERTY(EditDefaultsOnly, Category = "Trace")
    float TraceRadius;

    /** Nazwa profilu dla SphereTraceMultiByProfile */
    UPROPERTY(EditDefaultsOnly, Category = "Trace")
    TEnumAsByte<ECollisionChannel> TraceChannel;

    /** Czy debugowaæ trace */
    UPROPERTY(EditDefaultsOnly, Category = "Debug")
    bool bDebugTrace;

};
