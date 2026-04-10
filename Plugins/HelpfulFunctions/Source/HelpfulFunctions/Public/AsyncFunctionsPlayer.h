

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "AsyncFunctionsPlayer.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class HELPFULFUNCTIONS_API UAsyncFunctionsPlayer : public UActorComponent
{
	GENERATED_BODY()

public:	

	UAsyncFunctionsPlayer();

protected:

	UAnimInstance* TargetAnimInst = nullptr;

	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Animation|Montage|SoftRef", meta = (DisplayName = "Async Montage Play ", Keywords = "SoftRef,Montage,Async", AdvancedDisplay = "InPlayRate,StartTime,StopAllMontages"))
	void MontagePlayAsync(
		UAnimInstance* AnimInst, 
		TSoftObjectPtr<UAnimMontage> MontageToPlay, 
		float InPlayRate = 1.0, 
		float StartTime = 0.0, 
		bool StopAllMontages = true
	);

	UFUNCTION(BlueprintCallable, Category = "Animation|Montage|SoftRef", meta = (DisplayName = "Async Play Slot As Dynamic Montage ", Keywords = "SoftRef,Montage,Async", AdvancedDisplay = "BlendInTime,BlendOutTime,InPlayRate,LoopCount,BlendOutTriggerTime,StopAllMontages"))
	void PlayDynamicMontageAsync(
		UAnimInstance* AnimInst, 
		TSoftObjectPtr<UAnimSequence> Asset, 
		FName SlotName, 
		float BlendInTime = 0.25, 
		float BlendOutTime = 0.25, 
		float InPlayRate = 1.0, 
		int LoopCount = 1, 
		float BlendOutTriggerTime = -1.0, 
		float StartTime = 0.0
	);

private:

	

	void OnMontageLoaded(UAnimInstance* AnimInst, TSoftObjectPtr<UAnimMontage> LoadedMontage, float InPlayRate, float StartTime, bool StopAllMontages);
	void PlayLoadedMontage(UAnimInstance* AnimInst, UAnimMontage* MontageToPlay, float InPlayRate, float StartTime, bool StopAllMontages);

	void OnDynamicMontageLoaded(
		UAnimInstance* AnimInst, 
		TSoftObjectPtr<UAnimSequence> SequenceAsset,
		FName SlotName,
		float BlendInTime = 0.25,
		float BlendOutTime = 0.25,
		float InPlayRate = 1.0,
		int LoopCount = 1,
		float BlendOutTriggerTime = -1.0,
		float StartTime = 0.0
	);

	void PlayLoadedDynamicMontage(
		UAnimInstance* AnimInst,
		UAnimSequence* SequenceAnim,
		FName SlotName,
		float BlendInTime = 0.25,
		float BlendOutTime = 0.25,
		float InPlayRate = 1.0,
		int LoopCount = 1,
		float BlendOutTriggerTime = -1.0,
		float StartTime = 0.0
	);
		
};
