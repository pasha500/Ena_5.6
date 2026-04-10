

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_PointInsideCylinder.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UEnvQueryTest_PointInsideCylinder : public UEnvQueryTest
{
	GENERATED_BODY()

public:
    UEnvQueryTest_PointInsideCylinder(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
    virtual FText GetDescriptionTitle() const override;
    virtual FText GetDescriptionDetails() const override;

private:
    /** Kontekst definiuj¹cy pozycj?i rotacjr cylindrow */
    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test")
    TSubclassOf<UEnvQueryContext> CharactersContext;

    /** Polowa kata wycinka cylindra */
    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Volume Shape", meta = (ClampMin = 0, ClampMax = 90, UImin = 0, UIMax = 90))
    float HalfAngle;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Volume Shape", meta = (ClampMin = 10))
    float Radius;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Volume Shape", meta = (ClampMin = 5, ClampMax = 500, UImin = 5, UIMax = 500))
    float Height;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Score Type")
    FAIDataProviderBoolValue UseDistanceOnScoreResult;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Score Type")
    bool bRemapScore;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Score Type")
    FVector2D RemapScoreRange;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Score Type")
    FAIDataProviderBoolValue DoNotUseControlRotation;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Offsets")
    FVector PointOffset;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Offsets")
    FVector CylinderBaseOffset;


    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Trace Check")
    FAIDataProviderBoolValue CheckTraceToPoint;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Trace Check")
    float TraceRadius;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Trace Check")
    FVector PointTraceOffset;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Trace Check")
    FVector TraceOriginOffset;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Trace Check")
    TEnumAsByte<ECollisionChannel> TraceChannel;

    UPROPERTY(EditDefaultsOnly, Category = "In Cylinder Volume Test|Addtive Debug")
    bool bTryDrawVolumes;
};
