#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoomPrefabRegistry.generated.h"

class ARaidRoomActor;

UCLASS(BlueprintType)
class T_PROTO_API URoomPrefabRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	// Key: RoomPrefabId (CSVÀÇ RoomPrefabId)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid|Prefabs")
	TMap<FString, TSoftClassPtr<ARaidRoomActor>> PrefabMap;

	UFUNCTION(BlueprintCallable, Category = "Raid|Prefabs")
	TSubclassOf<ARaidRoomActor> Resolve(const FString& PrefabId) const;
};
