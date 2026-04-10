

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PairedAnimSequenceData.generated.h"


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
1) Initjacja - Moment wejscia do sparowanej sekwencji pomiêdzy dwoma charakterami.
2) Struggle/Loop - To moment kiedy gracz ma czas na wykonanie jakiejs interakcji np. szybkie naciskanie przycisku 'E'
3) Success - Wyjscie z czcsci Struggle oraz odegranie sekwencji, która nie koñczy siê smiercia gracza
4) Failed - Dane z tej czesci zostana aktywowane w momencie kiedy gracz nie ukonczy interakcji wymaganej w czesci 'Struggle'. Zazwyczaj 
bêdzie to oznacza³o smierc Charakteru gracza

*/
UCLASS()
class HELPFULFUNCTIONS_API UPairedAnimSequenceData : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Global")
	TArray<FName> AssetTags;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	float StrugglePartMaxTimeValue = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paired Sequences Data|Data - Struggle")
	float StrugglePartCriticalValue = 0.98;

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
	
};
