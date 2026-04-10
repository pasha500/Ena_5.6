

#pragma once

#include "CoreMinimal.h"
#include "NavLinkCustomComponent.h"
#include "AGLS_NavLinkCustomComponent.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UAGLS_NavLinkCustomComponent : public UNavLinkCustomComponent
{
	GENERATED_BODY()
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif


	virtual bool OnLinkMoveStarted(UObject* PathComp, const FVector& DestPoint) override;

	virtual void CalcAndCacheBounds() const override;

	virtual bool HasReachedLinkStart(const UObject* PathComp, const FVector& CurrentLocation, const FNavPathPoint& LinkStart, const FNavPathPoint& LinkEnd) const override;

protected:

	UPROPERTY(EditAnywhere, Category = SmartLink)
	FVector StartRelativeBoundOffset = FVector(0,0,0);

	UPROPERTY(EditAnywhere, Category = SmartLink)
	FVector EndRelativeBoundOffset = FVector(0, 0, 0);

};
