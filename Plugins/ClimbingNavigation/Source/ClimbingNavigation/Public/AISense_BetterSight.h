

#pragma once

#include "CoreMinimal.h"
#include "Perception/AISense_Sight.h"
#include "AISense_BetterSight.generated.h"

UENUM(BlueprintType)
enum class AISense_SightTracingMode : uint8
{
	OnlyPelvis,
	PelvisAndHead,
	PelvisHeadArms,
	AllBodyParts
};

//class UAISense_BetterSight;


UCLASS()
class CLIMBINGNAVIGATION_API UAISense_BetterSight : public UAISense_Sight
{
	GENERATED_BODY()

public:

	UAISense_BetterSight(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "True", Category = "Sight"))
	AISense_SightTracingMode TrackingType = AISense_SightTracingMode::PelvisAndHead;

protected:

	TArray<FName> BonesNameToCheck = {};

	virtual float Update() override;
	
	virtual float BetterSightUpdate();

	void MyRemoveAllQueriesToTarget_Internal(const FAISightTarget::FTargetId& TargetId, const TFunction<void(const FAISightQuery&)>& OnRemoveFunc = nullptr);

	EVisibilityResult BetterComputeVisibility(UWorld* World, FAISightQuery& SightQuery, FPerceptionListener& Listener, const AActor* ListenerActor, FAISightTarget& Target, AActor* TargetActor, const FDigestedSightProperties& PropDigest, float& OutStimulusStrength, FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested) const;

	virtual bool PerformTracingToActor(FHitResult& HitResult, UWorld* World, FVector StartLocation, AActor* TargetActor, ECollisionChannel CollisionChannel, FCollisionQueryParams QueryParams, FCollisionResponseParams ResponseParams) const;
	virtual FTraceHandle PerformTracingToActorAsync(UWorld* World, FVector StartLocation, AActor* TargetActor, ECollisionChannel CollisionChannel, FCollisionQueryParams QueryParams, FCollisionResponseParams ResponseParams) const;
};
