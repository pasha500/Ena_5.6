#include "Raid/RoomPrefabRegistry.h"
#include "Raid/RaidRoomActor.h"

TSubclassOf<ARaidRoomActor> URoomPrefabRegistry::Resolve(const FString& PrefabId) const
{
	if (const TSoftClassPtr<ARaidRoomActor>* Found = PrefabMap.Find(PrefabId))
	{
		if (Found->IsNull())
		{
			return nullptr;
		}
		// 동기 로드 (초기 챕터 빌드 시점)
		return Found->LoadSynchronous();
	}
	return nullptr;
}
