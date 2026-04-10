


#include "EnvQueryTest_PointInsideCylinder.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "CollisionQueryParams.h"
#include "ClimbingNavigationBPLibrary.h"

UEnvQueryTest_PointInsideCylinder::UEnvQueryTest_PointInsideCylinder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    HalfAngle = 20.0f;
    Radius = 500.0f;
    Height = 120.0f;
    //bUseDistanceOnScoreResult = false;
    //bCheckTraceToPoint = false;
    TraceRadius = 5.0f;
    PointOffset = FVector(0, 0, 20);
    CylinderBaseOffset = FVector(0, 0, 0);
    PointTraceOffset = FVector(0, 0, 20);
    TraceOriginOffset = FVector(0, 0, 20);
    TraceChannel = ECC_Visibility;
    bTryDrawVolumes = false;
    bRemapScore = false;
    RemapScoreRange = FVector2D(0.0, 1.0);

    Cost = EEnvTestCost::High;
    ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
}

void UEnvQueryTest_PointInsideCylinder::RunTest(FEnvQueryInstance& QueryInstance) const
{
    UObject* QueryOwner = QueryInstance.Owner.Get();
    if (!QueryOwner)
    {
        return;
    }

    UObject* DataOwner = QueryInstance.Owner.Get();
    BoolValue.BindData(DataOwner, QueryInstance.QueryID);
    UseDistanceOnScoreResult.BindData(DataOwner, QueryInstance.QueryID);
    CheckTraceToPoint.BindData(DataOwner, QueryInstance.QueryID);
    DoNotUseControlRotation.BindData(DataOwner, QueryInstance.QueryID);

    bool bUseDistanceOnScoreResult = UseDistanceOnScoreResult.GetValue();
    bool bCheckTraceToPoint = CheckTraceToPoint.GetValue();
    bool DoNotUseControlRot = DoNotUseControlRotation.GetValue();

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
        const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex()) + PointOffset;

        TArray<FVector> Distances;

        float Score = 1.0f; // Domyœlny score dla punktu, który nie spe³nia warunków
        int OverlapCount = 0;

        bool HitValid = false;

        for (AActor* CurrentActor : ContextActors)
        {
            const ACharacter* ContextCharacter = Cast<ACharacter>(CurrentActor);
            if (!ContextCharacter)
            {
                UE_LOG(LogTemp, Warning, TEXT("Context Actor is not Character. Ignoring it"))
                continue;
            }

            const FVector CylinderBase = ContextCharacter->GetActorLocation() - FVector(0, 0, ContextCharacter->GetSimpleCollisionHalfHeight()) + CylinderBaseOffset;
            FRotator CylinderRotation = ContextCharacter->GetControlRotation();
            if (DoNotUseControlRot) CylinderRotation = ContextCharacter->GetActorRotation();

            // Sprawdzenie, czy punkt znajduje siê w obszarze cylindra
            if (UClimbingNavigationBPLibrary::IsPointInsideCylinderSegment(
                CylinderRotation, CylinderBase, QueryOwner, ItemLocation, HalfAngle, Radius, Height, -1.0f))
            {
                OverlapCount++;
                HitValid = false;

                Distances.Add(CylinderBase);

                if (bCheckTraceToPoint)
                {
                    FHitResult HitResult;
                    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(TraceToPoint), false);
                    TraceParams.AddIgnoredActor(ContextCharacter);
                    if(Cast<AActor>(QueryOwner)) { TraceParams.AddIgnoredActor(Cast<AActor>(QueryOwner)); }
                    
                    AActor* OwnerActor = Cast<AActor>(QueryOwner);
                    if (OwnerActor)
                    {
                        TraceParams.AddIgnoredActor(OwnerActor);
                    }
                    TraceParams.bTraceComplex = false;

                    HitValid = QueryOwner->GetWorld()->SweepSingleByChannel(
                        HitResult,
                        CylinderBase + TraceOriginOffset,
                        ItemLocation + PointTraceOffset,
                        FQuat::Identity,
                        TraceChannel,
                        FCollisionShape::MakeSphere(TraceRadius),
                        TraceParams
                    );

                    //if (HitValid)
                    //{
                    //    DrawDebugLine(QueryOwner->GetWorld(), CylinderBase + TraceOriginOffset, HitResult.ImpactPoint, FColor::Red, false, 1.0, 0, 2.5);
                    //}
                    //else
                    //{
                    //    DrawDebugLine(QueryOwner->GetWorld(), CylinderBase + TraceOriginOffset, ItemLocation + PointTraceOffset, FColor::Blue, false, 1.0, 0, 2.5);
                    //}

                    if (HitValid)
                    {
                        if (OverlapCount > 0) OverlapCount--;
                    }
                }
            }
        }

        if (OverlapCount > 0)
        {
            if (bUseDistanceOnScoreResult && Distances.Num() > 0)
            {
                FVector AvgOrigin = FVector(1, 1, 1);
                for (FVector V1 : Distances)
                {
                    AvgOrigin += V1;
                }
                AvgOrigin = AvgOrigin / Distances.Num();
                Score = FVector::Dist(ItemLocation, AvgOrigin) / Radius;
            }
            else
            {
                if (ContextActors.Num() == 1)
                {
                    Score = 0.0;
                }
                else
                {
                    Score = 1.0f - ((float)OverlapCount / (float)ContextActors.Num()); // Score oparty na liczbie obszarów

                    if (bRemapScore)
                    {
                        Score = FMath::GetMappedRangeValueClamped(FVector2D(0.0,1.0), RemapScoreRange, Score);
                    }

                }
            }
        }

        It.SetScore(TestPurpose, FilterType, Score, MinThresholdValue, MaxThresholdValue);
    }

    if (bTryDrawVolumes)
    {
        for (AActor* CurrentActor : ContextActors)
        {
            const ACharacter* ContextCharacter = Cast<ACharacter>(CurrentActor);
            if (!ContextCharacter)
            {
                UE_LOG(LogTemp, Warning, TEXT("Context Actor is not Character. Ignoring it"))
                    continue;
            }

            const FVector CylinderBase = ContextCharacter->GetActorLocation() - FVector(0, 0, ContextCharacter->GetSimpleCollisionHalfHeight()) + CylinderBaseOffset;
            FRotator CylinderRotation = ContextCharacter->GetControlRotation();
            if (DoNotUseControlRot) CylinderRotation = ContextCharacter->GetActorRotation();

            UClimbingNavigationBPLibrary::DrawDebugPartCylinder(QueryInstance.World, CylinderRotation, CylinderBase, HalfAngle, Radius, Height, FColor::Cyan, 0.5, 2.0);
        }
    }

}

FText UEnvQueryTest_PointInsideCylinder::GetDescriptionTitle() const
{
    return FText::FromString(TEXT("Points Inside Cylinder Segments"));
}

FText UEnvQueryTest_PointInsideCylinder::GetDescriptionDetails() const
{
    FString Description = FString::Printf(
        TEXT("Checks if points are inside cylinder segments defined by context locations.\n")
        TEXT("Half Angle: %.1f, Radius: %.1f, Height: %.1f\n")
        TEXT("Use Distance: %s, Check Trace: %s, Trace Radius: %.1f"),
        HalfAngle, Radius, Height,
        UseDistanceOnScoreResult.DefaultValue ? TEXT("Yes") : TEXT("No"),
        CheckTraceToPoint.DefaultValue ? TEXT("Yes") : TEXT("No"),
        TraceRadius
    );

    return FText::FromString(Description);
}