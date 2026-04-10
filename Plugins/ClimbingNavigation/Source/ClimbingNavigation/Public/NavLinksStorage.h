

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavLinksStorage.generated.h"

/*
An empty actor whose purpose is to store data generated from NavLiksAutoGenerator. 

NOTE: You should not place an actor manually on the scene, and there should only be one instance on the selected level
*/
UCLASS(HideCategories = (LevelInstance, WorldPartition, DataLayers, Physics, HLOD, Replication, Networking, Input, Mobile, RayTracing, Cooking, Rendering, Collision, Navigation))
class CLIMBINGNAVIGATION_API ANavLinksStorage : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ANavLinksStorage();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
