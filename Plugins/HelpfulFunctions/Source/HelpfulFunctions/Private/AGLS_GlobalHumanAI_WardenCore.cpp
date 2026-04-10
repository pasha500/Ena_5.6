


#include "AGLS_GlobalHumanAI_WardenCore.h"

// Sets default values
AAGLS_GlobalHumanAI_WardenCore::AAGLS_GlobalHumanAI_WardenCore()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AAGLS_GlobalHumanAI_WardenCore::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AAGLS_GlobalHumanAI_WardenCore::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

float AAGLS_GlobalHumanAI_WardenCore::GetNotSilentModeBiasForPerception()
{
	return 0.0f;
}

float AAGLS_GlobalHumanAI_WardenCore::GetAgressiveModeBiasForPerception()
{
	return 0.0f;
}

