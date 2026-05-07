


#include "Pasha_ASyncFunctions.h"


UPasha_ASyncFunctions::UPasha_ASyncFunctions(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer), WorldContextObject(nullptr), MyFloatInput(0.0f)
{
}

UPasha_ASyncFunctions* UPasha_ASyncFunctions::WaitForOneFrame(const UObject* WorldContextObject, const float SomeInputVariables)
{
	UPasha_ASyncFunctions* BlueprintNode = NewObject<UPasha_ASyncFunctions>();
	BlueprintNode->WorldContextObject = WorldContextObject;
	BlueprintNode->MyFloatInput = SomeInputVariables;
	return BlueprintNode;
}

void UPasha_ASyncFunctions::Activate()
{
	// Any safety checks should be performed here. Check here validity of all your pointers etc.
	// You can log any errors using FFrame::KismetExecutionMessage, like that:
	// FFrame::KismetExecutionMessage(TEXT("Valid Player Controller reference is needed for ... to start!"), ELogVerbosity::Error);
	// return;

	WorldContextObject->GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UPasha_ASyncFunctions::ExecuteAfterOneFrame);
}

void UPasha_ASyncFunctions::ExecuteAfterOneFrame()
{
	AfterOneFrame.Broadcast(MyFloatInput + 1.0f, MyFloatInput + 2.0f);
}