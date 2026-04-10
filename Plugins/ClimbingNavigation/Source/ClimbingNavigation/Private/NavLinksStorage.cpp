


#include "NavLinksStorage.h"

// Sets default values
ANavLinksStorage::ANavLinksStorage()
{
	PrimaryActorTick.bCanEverTick = false;

	SetActorLocation(FVector(0,0,0));

}

// Called when the game starts or when spawned
void ANavLinksStorage::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ANavLinksStorage::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

