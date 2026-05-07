

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Pasha_ASyncFunctions.generated.h"



DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDelayOneFrameOutputPin, float, InputFloatPlusOne, float, InputFloatPlusTwo);

UCLASS()
class HELPFULFUNCTIONS_API UPasha_ASyncFunctions : public UBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(BlueprintAssignable)
		FDelayOneFrameOutputPin AfterOneFrame;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Pasha_FunctionsLibrary| Flow Control")
		static UPasha_ASyncFunctions* WaitForOneFrame(const UObject* WorldContextObject, const float SomeInputVariables);

	// UBlueprintAsyncActionBase interface
	virtual void Activate() override;
	//~UBlueprintAsyncActionBase interface
private:
	UFUNCTION()
		void ExecuteAfterOneFrame();


private:
	const UObject* WorldContextObject;
	float MyFloatInput;
};