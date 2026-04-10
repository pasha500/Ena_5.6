


#include "EnvQueryTest_DotToContextRot.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"

UEnvQueryTest_DotToContextRot::UEnvQueryTest_DotToContextRot(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TraceForNavPointChannel = ECC_Vehicle;

    Cost = EEnvTestCost::High;
    ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();

}

void UEnvQueryTest_DotToContextRot::RunTest(FEnvQueryInstance& QueryInstance) const
{

    UObject* QueryOwner = QueryInstance.Owner.Get();
    if (!QueryOwner)
    {
        return;
    }

    UObject* DataOwner = QueryInstance.Owner.Get();

    // Przygotuj kontekst
    TArray<FVector> ContextLocations;
    TArray<AActor*> ContextActors;

    if (!QueryInstance.PrepareContext(CharactersContext, ContextActors))
    {
        return;
    }

    FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
    float MinThresholdValue = FloatValueMin.GetValue();

    FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
    float MaxThresholdValue = FloatValueMax.GetValue();

    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());

        TArray<AActor*> ActorToIgnore;

        ETraceTypeQuery QueryTraceQuery = UEngineTypes::ConvertToTraceType(TraceForNavPointChannel);

        TArray<FHitResult> HitResult;
        const bool HitValid = UKismetSystemLibrary::SphereTraceMulti
        (
            QueryOwner, 
            ItemLocation + FVector(0, 0, 2), 
            ItemLocation, 
            SearchForNavPointInstRadius, 
            QueryTraceQuery, 
            false, 
            ActorToIgnore, 
            EDrawDebugTrace::None, 
            HitResult, 
            true, 
            FColor::Black, 
            FColor::Red, 
            0.8
        );

        if (HitResult.Num() == 0) { return; }

        FRotator PointRotation = FRotator(0, 0, 0);
        float WeightSum = 0.0;


        for (FHitResult Hit : HitResult)
        {
            UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
            if (ISMC)
            {
                bool TagMatched = true;

                if (bRequiredTagFromHit)
                {
                    TagMatched = ISMC->ComponentHasTag(TagName);
                }

                if (TagMatched)
                {
                    FTransform InstTransform = FTransform::Identity;
                    ISMC->GetInstanceTransform(Hit.Item, InstTransform, true);

                    PointRotation = InstTransform.Rotator();
                }

            }
        }

        for (AActor* CurrentActor : ContextActors)
        {
            FRotator ActorTargetRot = CurrentActor->GetActorRotation();
            if (bTryUseControlRotation)
            {
                ACharacter* AsCharacter = Cast<ACharacter>(CurrentActor);
                if (AsCharacter) ActorTargetRot = AsCharacter->GetControlRotation();
            }

            const FTransform ParentTransform = FTransform(ActorTargetRot, CurrentActor->GetActorLocation(), FVector(1, 1, 1));
            FTransform WorldTransform = FTransform(PointRotation, ItemLocation, FVector(1, 1, 1));
            const FTransform RelativeTransform = WorldTransform.GetRelativeTransform(ParentTransform);

            float InvertAxis = 1.0; if (RelativeTransform.GetLocation().X < 0.0) { InvertAxis = -1.0; }
            
            const float DotResult = FVector::DotProduct(ActorTargetRot.Vector(), PointRotation.Vector()) * InvertAxis;
            if (DotResult > 0.0f)
            {
                WeightSum = WeightSum + FMath::Pow(FMath::Lerp<float>(1.0, 0.0, abs(DotResult)), PowExp02);
            }
            else
            {
                WeightSum = WeightSum + FMath::Pow(abs(DotResult), PowExp01);
            }

        }

        It.SetScore(TestPurpose, FilterType, WeightSum/ (float)ContextActors.Num(), MinThresholdValue, MaxThresholdValue);

    }

}

FText UEnvQueryTest_DotToContextRot::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("Dot To Context Actors"));
}

FText UEnvQueryTest_DotToContextRot::GetDescriptionDetails() const
{
    FString Description = FString::Printf(
        TEXT("Allows to determine which CoverNavPoints are most \n")
        TEXT("parallel to the Actors rotation vector.\n")
        TEXT("Require Tag: %s, Use ControlRot: %s"),
        bRequiredTagFromHit ? TEXT("Yes") : TEXT("No"),
        bTryUseControlRotation ? TEXT("Yes") : TEXT("No")
    );

    return FText::FromString(Description);
}
