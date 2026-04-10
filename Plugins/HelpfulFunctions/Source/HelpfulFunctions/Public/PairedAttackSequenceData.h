

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PairedAttackSequenceData.generated.h"

UENUM(BlueprintType)
enum class PairedAttackSeqPreviewMode : uint8
{
	Initialize,
	Struggle,
	Success,
	Failed
};


/*
A data asset containing a set of essential information and resources for the proper operation of so-called 'Paired Dynamic Sequences.'
This resource can be used for mechanics related to enemy character attacks. For the sequences to function correctly, appropriate
animations must be prepared, which are divided into the following categories:

1) Initiation - The moment of entering a paired sequence between two characters.
2) Struggle/Loop - This is the moment when the player has time to perform an interaction, such as rapidly pressing the 'E' button.
3) Success - Exiting the Struggle portion and playing a sequence that does not end in the player's death.
4) Failed - Data from this portion will be activated when the player fails to complete the interaction required in the 'Struggle'
portion. This will usually result in the death of the player's Character.



Data Asset zawierajacy zbior niezbednych informacji i zasobow do prawidlowego dzialania tak zwanych 'Sparowanych Dynamicznych Seqwencji'.
Taki zasob moze byc wykorzystywany do mechanik zwiazanych z atakami wrogich postaci. Do prawidlowego dzialania sekwencji niezbedne jest
przegotowanie odpowiednich animacji, ktore zostaly podzielone na odpowiednie kategorie:
1) Inicjacja - Moment wejscia do sparowanej sekwencji pomiedzy dwoma charakterami.
2) Struggle/Loop - To moment kiedy gracz ma czas na wykonanie jakiejs interakcji np. szybkie naciskanie przycisku 'E'
3) Success - Wyjscie z czesci Struggle oraz odegranie sekwencji, ktora nie konczy sie smiercia gracza
4) Failed - Dane z tej czesci zostana aktywowane w momencie kiedy gracz nie ukonczy interakcji wymaganej w czesci 'Struggle'. Zazwyczaj
bêdzie to oznacza³o smierc Charakteru gracza

*/
UCLASS(NotPlaceable, HideCategories = (Navigation, HLOD, Input, NetWorking, Replication, Mobile, DataLayers, "Actor Tick", Actor, WorldPartition, LevelInstance, Collision, Events, Physics), meta = (ChildCannotTick))
class HELPFULFUNCTIONS_API APairedAttackSequenceData : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APairedAttackSequenceData();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:

#if WITH_EDITOR
	void SetupAnimationPreview(USkeletalMesh* Mesh, UAnimMontage* InitMontage, UAnimSequence* StruggleSequence, TSoftObjectPtr<UAnimMontage> SuccessMontage, TSoftObjectPtr<UAnimMontage> FailedMontage, FVector ComOffset, FRotator CompRot, FName ObjectName);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Preview")
	bool bGeneratePreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Preview", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bGeneratePreview"))
	float AnimNormalizedTime = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Preview", meta = (EditCondition = "bGeneratePreview"))
	PairedAttackSeqPreviewMode AnimsPreviewMode = PairedAttackSeqPreviewMode::Initialize;



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	USkeletalMesh* SkeletalMeshAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	USkeletalMesh* SkeletalMeshVic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	TArray<FName> AssetTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	bool bVicCharacterIsRoot = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	FVector ConstGlobalRootsOffset = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	FRotator ConstGlobalRotationDelta = FRotator(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	bool bInverseRotationDeltaForVic = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	bool bInverseRotationDeltaForAtt = false;



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Initialize")
	UAnimMontage* MontageInitializeAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Initialize")
	UAnimMontage* MontageInitializeVic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Initialize")
	bool UsePoseSearchForInitialze = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Initialize", meta = (EditCondition = "UsePoseSearchForInitialze"))
	TArray<UAnimMontage*> ForPoseSearchMontagesAtt;



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	UAnimSequence* AnimStruggleDynamicTimeAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	UAnimSequence* AnimStruggleDynamicTimeVic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StrugglePartInitProgressValue = 0.4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StrugglePartCriticalValue = 0.95;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float StrugglePartProgressDeltaAdd = 0.08;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float StrugglePartProgressDeltaSub = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	TSoftObjectPtr<UAnimSequence> SoftAddtiveAnimStruggleLoopAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	TSoftObjectPtr<UAnimSequence> SoftAddtiveAnimStruggleLoopVic = nullptr;



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Success")
	TSoftObjectPtr<UAnimMontage> SoftMontageSuccessAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Success")
	TSoftObjectPtr<UAnimMontage> SoftMontageSuccessVic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Success")
	FVector2D SuccessMontagesTimeStartRange = FVector2D(0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Success")
	FVector SuccessLocalOffset = FVector(0, 0, 0);



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Failed")
	TSoftObjectPtr<UAnimMontage> SoftMontageFailedAtt = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Failed")
	TSoftObjectPtr<UAnimMontage> SoftMontageFailedVic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Failed")
	FVector2D FailedMontagesTimeStartRange = FVector2D(0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Failed")
	FVector FailedLocalOffset = FVector(0, 0, 0);



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Camera Control")
	FTransform CameraDesiredTransformStart = FTransform(FRotator(0, 0, 0), FVector(60, 0, 120), FVector(1, 1, 1));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Camera Control")
	FTransform CameraDesiredTransformEnd = FTransform(FRotator(0, 0, 0), FVector(60, 0, 120), FVector(1, 1, 1));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Camera Control")
	float CameraInterpSpeedBias = 5.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Camera Control")
	float CameraFOV_Bias = 0.0;


};
