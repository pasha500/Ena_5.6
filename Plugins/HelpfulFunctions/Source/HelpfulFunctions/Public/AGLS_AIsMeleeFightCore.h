

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ALS_StructuresAndEnumsCpp.h"
#include "AI_MeleeFightSequencesData.h"
#include "AGLS_AIsMeleeFightCore.generated.h"


UCLASS(Abstract, Blueprintable, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELPFULFUNCTIONS_API UAGLS_AIsMeleeFightCore : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UAGLS_AIsMeleeFightCore();

protected:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Fight Core|Config")
	UAI_MeleeFightSequencesData* SequencesData = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|References")
	ACharacter* RefChar = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	bool bStartedFinisher = false;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	ACharacter* EnemyCharacter = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	UAnimMontage* FinisherMontageAtt = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	UAnimMontage* FinisherMontageVic = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	float ActionDuration = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	FCALS_ComponentAndTransform SeqTransformAttLS;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	FCALS_ComponentAndTransform SeqTransformVicLS;

	UPROPERTY(BlueprintReadWrite, Category = "Melee Fight Core|Finisher Action")
	FAGLS_FinishersDataForAI ChoosedActionData;

	// Called when the game starts
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Check Can Start Finisher", Keywords = "Finisher,Melee,AI"))
	bool CheckCanStartFinisher(ACharacter*& ChoosedActor, float& DistanceTo, FTransform& TransformVic, FTransform& TransformAtt, bool StartWhenCheckValid = false, float SearchRadius = 300.0, ECollisionChannel EnemyTraceChannel = ECollisionChannel::ECC_Pawn);
	virtual bool CheckCanStartFinisher_Implementation(ACharacter*& ChoosedActor, float& DistanceTo, FTransform& TransformVic, FTransform& TransformAtt, bool StartWhenCheckValid = false, float SearchRadius = 300.0, ECollisionChannel EnemyTraceChannel = ECollisionChannel::ECC_Pawn);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Try Start Finisher Action", Keywords = "Finisher,Melee,AI"))
	bool TryStartFinisherAction(bool CanStart, ACharacter* ChoosedEnemy, float DistanceTo, FTransform TransformVic, FTransform TransformAtt);
	virtual bool TryStartFinisherAction_Implementation(bool CanStart, ACharacter* ChoosedEnemy, float DistanceTo, FTransform TransformVic, FTransform TransformAtt);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Do When Finisher Action", Keywords = "Finisher,Melee,AI"))
	void DoWhenFinisherAction();
	virtual void DoWhenFinisherAction_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Try Interrupt Finisher Action", Keywords = "Finisher,Melee,AI"))
	bool TryInterruptFinisherAction();
	virtual bool TryInterruptFinisherAction_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Get Finisher Time Elapsed", Keywords = "Finisher,Melee,AI"))
	float GetFinisherTimeElapsed(bool bNormalized = true);
	virtual float GetFinisherTimeElapsed_Implementation(bool bNormalized = true);

	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Choose Best Anim Sequence", Keywords = "Finisher,Melee,AI"))
	void ChooseBestAnimSeq(bool& Valid, FAGLS_FinishersDataForAI& ReturnAssets, ACharacter* InVicChar, FTransform InTransformAtt, FTransform InTransformVic);
	virtual void ChooseBestAnimSeq_Implementation(bool& Valid, FAGLS_FinishersDataForAI& ReturnAssets, ACharacter* InVicChar, FTransform InTransformAtt, FTransform InTransformVic);

	UFUNCTION(BlueprintPure, Category = "Melee Fight Core", meta = (ForceAsFunction, DisplayName = "Get All Sequences Data", Keywords = "Finisher,Melee,AI", CompactNodeTitle = "Anims Data"))
	TMap<FName, FAGLS_FinishersDataForAI> GetAllSequencesData();

private:

	UFUNCTION(BlueprintCallable, Category = "Melee Fight Core|Debug", meta = (BlueprintProtected, DevelopmentOnly, HideSelfPin, HidePin = "Target", DisplayName = "Draw Sphere", Keywords = "Debug,Shapes", AdvancedDisplay = 1))
	const void DebugSphere(const FVector& Position, float Radius = 50.0, FLinearColor Color = FLinearColor::Blue, float Thickness = 1.2, float Duration = 0.0, int Depth = 0);

	UFUNCTION(BlueprintCallable, Category = "Melee Fight Core|Debug", meta = (BlueprintProtected, DevelopmentOnly, HideSelfPin, HidePin = "Target", DisplayName = "Draw Line", Keywords = "Debug,Shapes", AdvancedDisplay = 2))
	const void DebugLine(const FVector& Start, const FVector& End, FLinearColor Color = FLinearColor::Green, float Thickness = 1.2, float Duration = 0.0, int Depth = 0);

	UFUNCTION(BlueprintCallable, Category = "Melee Fight Core|Debug", meta = (BlueprintProtected, DevelopmentOnly, HideSelfPin, HidePin = "Target", DisplayName = "Draw Point", Keywords = "Debug,Shapes", AdvancedDisplay = 1))
	const void DebugPoint(const FVector& Position, float Size = 10.0, FLinearColor Color = FLinearColor::Blue, float Duration = 0.0, int Depth = 0);

	UFUNCTION(BlueprintCallable, Category = "Melee Fight Core|Debug", meta = (BlueprintProtected, DevelopmentOnly, HideSelfPin, HidePin = "Target", DisplayName = "Draw Path", Keywords = "Debug,Shapes", AdvancedDisplay = 1))
	const void DebugPath(TArray<FVector> Points, float Size = 10.0, float Thickness = 1.2, FLinearColor Color = FLinearColor::Blue, float Duration = 0.0, int Depth = 0);


public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;


		
};
