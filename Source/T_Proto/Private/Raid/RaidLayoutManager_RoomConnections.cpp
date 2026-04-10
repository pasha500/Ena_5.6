#include "Raid/RaidLayoutManager.h"
#include "Raid/RaidRoomActor.h"

void ARaidLayoutManager::ConnectRoomDoors()
{
    for (auto& Pair : SpawnedRooms) { if (ARaidRoomActor* Room = Pair.Value) { Room->NeighborNorth = Room->NeighborSouth = Room->NeighborEast = Room->NeighborWest = -1; Room->bDoorNorth = Room->bDoorSouth = Room->bDoorEast = Room->bDoorWest = false; } }
    for (auto& Pair : SpawnedRooms) {
        ARaidRoomActor* Room = Pair.Value; if (!Room) continue;
        const FVector MyLoc = Room->GetActorLocation(); const TArray<int32> Connections = Room->GetNodeRow().GetConnectionIds();
        for (int32 ConnectedId : Connections) {
            TObjectPtr<ARaidRoomActor>* ConnectedRoomPtr = SpawnedRooms.Find(ConnectedId);
            if (!ConnectedRoomPtr || !ConnectedRoomPtr->Get()) continue;
            ARaidRoomActor* ConnectedRoom = ConnectedRoomPtr->Get();
            const FVector Dir = ConnectedRoom->GetActorLocation() - MyLoc;
            if (FMath::Abs(Dir.X) >= FMath::Abs(Dir.Y)) { if (Dir.X > 0) { Room->NeighborNorth = ConnectedId; Room->bDoorNorth = true; } else { Room->NeighborSouth = ConnectedId; Room->bDoorSouth = true; } }
            else { if (Dir.Y > 0) { Room->NeighborEast = ConnectedId; Room->bDoorEast = true; } else { Room->NeighborWest = ConnectedId; Room->bDoorWest = true; } }
        }
    }
    for (auto& Pair : SpawnedRooms) { if (ARaidRoomActor* Room = Pair.Value) Room->GenerateRoomLayout(); }
}
