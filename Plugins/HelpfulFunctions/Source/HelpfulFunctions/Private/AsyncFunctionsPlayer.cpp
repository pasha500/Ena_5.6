


#include "AsyncFunctionsPlayer.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

// Sets default values for this component's properties
UAsyncFunctionsPlayer::UAsyncFunctionsPlayer()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
}


// Called when the game starts
void UAsyncFunctionsPlayer::BeginPlay()
{
	Super::BeginPlay();
	
}


// Called every frame
void UAsyncFunctionsPlayer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

}

void UAsyncFunctionsPlayer::MontagePlayAsync(UAnimInstance* AnimInst, TSoftObjectPtr<UAnimMontage> MontageToPlay, float InPlayRate, float StartTime, bool StopAllMontages)
{

	if (IsValid(AnimInst))
	{
		TargetAnimInst = AnimInst;
	}
	else
	{
		return;
	}


	// SprawdŸ, czy zasób jest ju¿ za³adowany
	if (MontageToPlay.IsValid())
	{
		// Zasób jest ju¿ za³adowany, odtwórz animacjê natychmiastowo
		PlayLoadedMontage(AnimInst, MontageToPlay.Get(), InPlayRate, StartTime, StopAllMontages);
	}
	else
	{

		// Zasób nie jest za³adowany, ³adujemy go asynchronicznie
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

		// £aduj asynchronicznie i wywo³aj callback po za³adowaniu
		Streamable.RequestAsyncLoad(MontageToPlay.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UAsyncFunctionsPlayer::OnMontageLoaded, AnimInst, MontageToPlay, InPlayRate, StartTime, StopAllMontages));
	}
}

void UAsyncFunctionsPlayer::PlayDynamicMontageAsync(UAnimInstance* AnimInst, TSoftObjectPtr<UAnimSequence> Asset, FName SlotName, float BlendInTime, float BlendOutTime, float InPlayRate, int LoopCount, float BlendOutTriggerTime, float StartTime)
{
	if (IsValid(AnimInst)) 
	{ 
		TargetAnimInst = AnimInst; 
	}
	else
	{
		return;
	}
	// SprawdŸ, czy zasób jest ju¿ za³adowany
	if (Asset.IsValid())
	{
		// Zasób jest ju¿ za³adowany, odtwórz animacjê natychmiastowo
		PlayLoadedDynamicMontage(AnimInst, Asset.Get(), SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, StartTime);
	}
	else
	{
		// Zasób nie jest za³adowany, ³adujemy go asynchronicznie
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

		// £aduj asynchronicznie i wywo³aj callback po za³adowaniu
		Streamable.RequestAsyncLoad(Asset.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UAsyncFunctionsPlayer::OnDynamicMontageLoaded, AnimInst, Asset, SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, StartTime));
	}
}




void UAsyncFunctionsPlayer::OnMontageLoaded(UAnimInstance* AnimInst, TSoftObjectPtr<UAnimMontage> LoadedMontage, float InPlayRate, float StartTime, bool StopAllMontages)
{
	// Upewnij siê, ¿e monta¿ zosta³ poprawnie za³adowany
	if (LoadedMontage.IsValid())
	{
		UAnimInstance* TargetInst = AnimInst;
		if (IsValid(TargetInst) == false) { TargetInst = TargetAnimInst; }
		PlayLoadedMontage(TargetInst, LoadedMontage.Get(), InPlayRate, StartTime, StopAllMontages);
	}
}

void UAsyncFunctionsPlayer::PlayLoadedMontage(UAnimInstance* AnimInst, UAnimMontage* MontageToPlay, float InPlayRate, float StartTime, bool StopAllMontages)
{
	if (AnimInst)
	{
		// Odtwórz monta¿ animacji (zmiana po aktualizacji)
		AnimInst->Montage_Play(MontageToPlay, InPlayRate, EMontagePlayReturnType::Duration, StartTime, StopAllMontages);
	}
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void UAsyncFunctionsPlayer::OnDynamicMontageLoaded(UAnimInstance* AnimInst, TSoftObjectPtr<UAnimSequence> SequenceAsset, FName SlotName, float BlendInTime, float BlendOutTime, float InPlayRate, int LoopCount, float BlendOutTriggerTime, float StartTime)
{
	if (SequenceAsset.IsValid())
	{
		UAnimInstance* TargetInst = AnimInst;
		if (IsValid(TargetInst) == false) { TargetInst = TargetAnimInst; }
		PlayLoadedDynamicMontage(AnimInst, SequenceAsset.Get(), SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, StartTime);
	}
}

void UAsyncFunctionsPlayer::PlayLoadedDynamicMontage(UAnimInstance* AnimInst, UAnimSequence* SequenceAnim, FName SlotName, float BlendInTime, float BlendOutTime, float InPlayRate, int LoopCount, float BlendOutTriggerTime, float StartTime)
{
	if (AnimInst)
	{
		AnimInst->PlaySlotAnimationAsDynamicMontage(SequenceAnim, SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, StartTime);
	}
}

