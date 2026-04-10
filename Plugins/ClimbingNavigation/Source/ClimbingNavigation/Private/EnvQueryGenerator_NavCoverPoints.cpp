


#include "EnvQueryGenerator_NavCoverPoints.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"

UEnvQueryGenerator_NavCoverPoints::UEnvQueryGenerator_NavCoverPoints(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    ItemType = UEnvQueryItemType_Point::StaticClass();
    TraceRadius = 100.0f;
    TraceChannel = ECollisionChannel::ECC_Vehicle;
    bDebugTrace = false;
}

void UEnvQueryGenerator_NavCoverPoints::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
    UObject* QueryOwner = QueryInstance.Owner.Get();
    if (!QueryOwner)
    {
        return;
    }

    UWorld* World = QueryOwner->GetWorld();
    if (!World)
    {
        return;
    }

    TArray<FHitResult> HitResults;

    TArray<FVector> ContextLocations;
    QueryInstance.PrepareContext(GenerateAround, ContextLocations);
    

    TArray<FNavLocation> FinededPoints;
    TArray<FVector> Points;

    EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
    if (bDebugTrace) { TraceDebugType = EDrawDebugTrace::ForDuration; }

    ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(TraceChannel);


    if (ContextLocations.Num() > 0)
    {
        for (FVector QueryOrigin : ContextLocations)
        {
            bool bHit = UKismetSystemLibrary::SphereTraceMulti(
                World,
                QueryOrigin,
                QueryOrigin + FVector(0,0,-2),
                TraceRadius,
                TraceType,
                false,
                {},
                TraceDebugType,
                HitResults,
                true,
                FColor::Blue,
                FColor::Cyan,
                0.5
            );

            if (bDebugTrace)
            {
                UKismetSystemLibrary::DrawDebugSphere(World, QueryOrigin, TraceRadius, 12, FColor::Blue, true, 5.0f);
            }

            for (const FHitResult& Hit : HitResults)
            {
                UInstancedStaticMeshComponent* InstanceClass = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
                if (InstanceClass)
                {
                    FTransform InstanceTransform;
                    if (InstanceClass->GetInstanceTransform(Hit.Item, InstanceTransform, true))
                    {
                        Points.Add(InstanceTransform.GetLocation());
                        FinededPoints.Add(FNavLocation(InstanceTransform.GetLocation()));

                        if (bDebugTrace)
                        {
                            UKismetSystemLibrary::DrawDebugSphere(World, InstanceTransform.GetLocation(), 8.0f, 6, FColor::Green, true, 5.0f);
                        }
                    }
                }
            }
        }

        StoreNavPoints(FinededPoints, QueryInstance);

    }

}


void UEnvQueryGenerator_NavCoverPoints::StoreNavPoints(const TArray<FNavLocation>& Points, FEnvQueryInstance& QueryInstance) const
{
    const int32 InitialElementsCount = QueryInstance.Items.Num();
    QueryInstance.ReserveItemData(InitialElementsCount + Points.Num());
    for (int32 Idx = 0; Idx < Points.Num(); Idx++)
    {
        // store using default function to handle creating new data entry 
        QueryInstance.AddItemData<UEnvQueryItemType_Point>(Points[Idx]);
    }
}

