

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AGLS_GlobalHumanAI_WardenCore.generated.h"

UCLASS()
class HELPFULFUNCTIONS_API AAGLS_GlobalHumanAI_WardenCore : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AAGLS_GlobalHumanAI_WardenCore();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	TArray<ACharacter*> CharactersIncludedInGroup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	ACharacter* CommanderCharacter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	float WhenAnyoneSeeReactionBias = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	float WhenAnyoneAttackedPerceptionBias = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	float ArenaFightRangeOuter = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "1) HumanAI Group Config", meta = (AllowPrivateAccess = "True"))
	float ArenaFightRangeInnerBias = 500.0;



protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Human AI Group", meta = (Keywords = "HumanAI,AI"))
	float GetNotSilentModeBiasForPerception();

	UFUNCTION(BlueprintPure, Category = "Human AI Group", meta = (Keywords = "HumanAI,AI"))
	float GetAgressiveModeBiasForPerception();

};
