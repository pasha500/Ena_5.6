


#include "NavLinksAutoGenerator.h"
#include "Components/BoxComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopedSlowTask.h"

#define KML UKismetMathLibrary
#define KSL UKismetSystemLibrary


// Sets default values
ANavLinksAutoGenerator::ANavLinksAutoGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

    // Ustawiamy root komponent jako BoxCollision
    BoxCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
    RootComponent = BoxCollision;

    // Domyœlne w³aœciwoœci BoxCollision
    BoxCollision->SetBoxExtent(FVector(150.0f, 150.0f, 200.0f));
    BoxCollision->ShapeColor = FColor::Blue;
    BoxCollision->SetLineThickness(1.5f);
    BoxCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoxCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoxCollision->SetGenerateOverlapEvents(false);
    BoxCollision->Mobility = EComponentMobility::Static;

    //Is ONLY EDITOR ACTOR
    bIsEditorOnlyActor = true;
    SetActorHiddenInGame(true);
    
    Tags.Add("NavLinksAutoGenerator");
   
}

// Called when the game starts or when spawned
void ANavLinksAutoGenerator::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ANavLinksAutoGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

FVector FindNormalBetweenLineAndPoint(FVector& OutDirection, float& OutDist, FVector Point, FVector LineStart, FVector LineDirection)
{
    const FVector Point2D = FVector(Point.X, Point.Y, 0.0);
    const FVector LineStart2D = FVector(LineStart.X, LineStart.Y, 0.0);
    float DistToLine2D = KML::GetPointDistanceToLine(Point2D, LineStart2D, LineDirection);

    if (DistToLine2D < 0.01)
    {
        UE_LOG(LogTemp, Warning, TEXT("NavLinks Generator: FindNormalBetweenLineAndPoint() - Distance to line is to small"));
    }

    FVector NormalToLine = KML::Cross_VectorVector(LineDirection, FVector(0, 0, 1));

    if (KML::GetPointDistanceToLine(Point2D + (NormalToLine * DistToLine2D), LineStart, LineDirection) > 2.0)
    {
        OutDirection = NormalToLine;
        OutDist = DistToLine2D;
        return NormalToLine * DistToLine2D;
    }
    else
    {
        OutDirection = NormalToLine * -1.0;
        OutDist = DistToLine2D;
        return NormalToLine * DistToLine2D * -1.0;
    }

}

FVector NormalToFVector(FVector InNormal)
{
    return KML::GetForwardVector(FRotator(0, KML::MakeRotFromX(InNormal).Yaw, 0)) * -1.0;
}

TArray<FNavLinks_FloorEdges> ANavLinksAutoGenerator::FindPolyEdgesFromNavMesh(bool bDrawDebugShapes)
{
    UBoxComponent* MainBox = BoxCollision;
    if (!MainBox) 
    {
        UE_LOG(LogTemp, Warning, TEXT("NavLinks Generator: Box Volume is NOT valid for FindPolyEdgesFromNavMesh() function"));
        return {}; 
    }

    UE_LOG(LogTemp, Log, TEXT("NavLinks Generator: Started finding PolyEdges from current Navigation Mesh data"))

    TArray<FNavLinks_FloorEdges> TotalFloorsNavEdges;
    TArray<FNavLinks_SingleLine> AllLines;

    const float Divider = MainBox->GetScaledBoxExtent().Z * 2.0 - ConstDistBetweenFloors;

    double Remainer = 0.0;
    int TotalBoxes = KML::FMod64(Divider, ConstDistBetweenFloors, Remainer);

    if (TotalBoxes <= 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("NavLinks Generator: Subdivide of Box Volume process failed. Make sure all parameters is correct setted up"));
    }


    for (int i = 0; i <= TotalBoxes - 1; i++)
    {
        AllLines.Empty();

        FVector BoxOrigin = MainBox->K2_GetComponentLocation() + FVector(0, 0, MainBox->GetScaledBoxExtent().Z - ConstDistBetweenFloors) - FVector(0, 0, ((float)i * ConstDistBetweenFloors));

        FBox CurrentBox;
        CurrentBox = KML::MakeBoxWithOrigin(BoxOrigin, FVector(MainBox->GetScaledBoxExtent().X,
            MainBox->GetScaledBoxExtent().Y, ConstDistBetweenFloors));

        //TArray<FNavEdgeParams> UClimbingNavigationBPLibrary::FindExternalEdgesInBox(UObject* WorldContextObject, const FBox& SearchBox)
        TArray<FNavEdgeParams> NavPolyEdges;
        NavPolyEdges = UClimbingNavigationBPLibrary::FindExternalEdgesInBox(this, CurrentBox);

        if (NavPolyEdges.Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("NavLinks Generator: for box: %s Navigation poly edges is NOT finded"), *FVector(i,0,0).ToString());
        }

        for (FNavEdgeParams CurrentEdge : NavPolyEdges)
        {
            FNavLinks_SingleLine MakeLine;

            TArray<FVector> LinePointsArray;

            if (NavEdgeLedgthOffset < 0.01f && CurrentEdge.Lenght > abs(NavEdgeLedgthOffset * 2.0) && !bApplyEdgeOffsetPostNavAnalyze)
            {
                FVector LDirection = CurrentEdge.LeftPosition - CurrentEdge.RightPosition; LDirection.Normalize();
                FVector LineOrigin = KML::VLerp(CurrentEdge.LeftPosition, CurrentEdge.RightPosition, 0.5);

                FVector PositionWithOffset = CurrentEdge.LeftPosition + (LDirection * NavEdgeLedgthOffset);
                if ((PositionWithOffset - LineOrigin).Length() < 5.0)
                {
                    PositionWithOffset = LineOrigin + (LDirection * -5.0);
                }
                LinePointsArray.Add(PositionWithOffset);

                MakeLine.StartPosition = PositionWithOffset;

                PositionWithOffset = CurrentEdge.RightPosition + (LDirection * -NavEdgeLedgthOffset);
                if ((PositionWithOffset - LineOrigin).Length() < 5.0)
                {
                    PositionWithOffset = LineOrigin + (LDirection * 5.0);
                }
                LinePointsArray.Add(PositionWithOffset);

                MakeLine.AllPoints = LinePointsArray;

                MakeLine.TotalLength = (MakeLine.StartPosition - PositionWithOffset).Length();

            }
            else
            {
                LinePointsArray.Add(CurrentEdge.LeftPosition);
                LinePointsArray.Add(CurrentEdge.RightPosition);

                MakeLine.AllPoints = LinePointsArray;

                MakeLine.StartPosition = CurrentEdge.LeftPosition;
                MakeLine.TotalLength = CurrentEdge.Lenght;
            }

            MakeLine.Direction = KML::GetForwardVector(KML::FindLookAtRotation(CurrentEdge.LeftPosition, CurrentEdge.RightPosition));

            AllLines.Add(MakeLine);

#if WITH_EDITOR
            if (bDrawDebugNavEdges)
            {
                float ColorAlpha = KML::MapRangeClamped(i * 1.0, 0.0, TotalBoxes, 0.0, 1.0);
                FLinearColor LineColor = KML::LinearColorLerp(FLinearColor::Blue, FLinearColor::Green, ColorAlpha);
                DrawDebugLine(GetWorld(), MakeLine.StartPosition, MakeLine.StartPosition + (MakeLine.Direction * MakeLine.TotalLength), LineColor.ToFColor(true), false, DebugDrawTime, 0, 2.0);
            }
#endif
        }

        if (AllLines.Num() > 0)
        {
            FNavLinks_FloorEdges LocalEdges;
            LocalEdges.NavEdges = AllLines;
            LocalEdges.FloorID = i;
            TotalFloorsNavEdges.Add(LocalEdges);
        }

        if (bDrawBoxVolumes)
        {
            float ColorAlpha = KML::MapRangeClamped(i * 1.0, 0.0, TotalBoxes, 0.0, 1.0);
            FLinearColor BoxColor = KML::LinearColorLerp(FLinearColor::Blue, FLinearColor::White, ColorAlpha);
            DrawDebugBox(GetWorld(), CurrentBox.GetCenter(), CurrentBox.GetSize() / 2.0, FQuat::Identity, BoxColor.ToFColor(true), false, DebugDrawTime, 0, 1.2);
        }

    }

    return TotalFloorsNavEdges;
}

void ANavLinksAutoGenerator::AddNavLinksComponents(TArray<FNavLinks_FloorEdges> InLines, AActor* StorageActor)
{
    //Step 01 Find Component To Save All NavLinks Generated (Must Be Valid)
    FString ActorGuidString = TEXT("NullGUID");

#if WITH_EDITOR
    ActorGuidString = ActorGuid.ToString();
#endif

    const FName CompName = FName(ActorGuidString);
    UDynamicNavLinkComponent* NavLinksComponent = Cast<UDynamicNavLinkComponent>(StorageActor->FindComponentByTag(UDynamicNavLinkComponent::StaticClass(), CompName));
    if (!NavLinksComponent) 
    { 
        UE_LOG(LogTemp, Error, TEXT("NavLinks Generator: Nav Links Component is null, but AddNavLinksComponents() function required this object"));
        return; 
    }

    //Step 02
    if (InLines.Num() <= 1) 
    { 
        UE_LOG(LogTemp, Warning, TEXT("NavLinks Generator: AddNavLinksComponents() function is executed, but parameter TArray<FNavLinks_FloorEdges> InLines is <= 1"));
        return; 
    }

#if WITH_EDITOR
    // Przygotowanie paska progresu
    const int32 TotalIterations = InLines.Num() * (InLines.Num() - 1) / 2; // Liczba iteracji wewnêtrznej pêtli
    FScopedSlowTask SlowTask(TotalIterations, FText::FromString(TEXT("Generating Nav Links...")));
    SlowTask.MakeDialog(true); // Wyœwietl dialog z paskiem progresu

#endif

    for (int i = 0; i < InLines.Num() - 1; i++)
    {
        for (FNavLinks_SingleLine CurrentLine : InLines[i].NavEdges)
        {
            for (int i2 = i + 1; i2 < InLines.Num(); i2++)
            {
                for (FNavLinks_SingleLine NextLine : InLines[i2].NavEdges)
                {

#if WITH_EDITOR
                    // Zg³oszenie progresu dla bie¿¹cego kroku
                    SlowTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("Processing Line %d and Line %d"), i, i2)));
#endif

                    int PointsOnSegment = 1;

                    double Remainer = 0.0;
                    PointsOnSegment = KML::FMod64(CurrentLine.TotalLength, SegmentDivisor, Remainer); 
                    if (PointsOnSegment == 0) { PointsOnSegment = 0; }

                    //Step 03 Subdivide line and make sure the projected point is laying on NextLine
                    for (int i3 = 0; i3 <= PointsOnSegment; i3++)
                    {
                        float LineAlpha = 0.5;
                        if (PointsOnSegment > 0)
                        {
                            LineAlpha = KML::MapRangeClamped(i3 * 1.0, 0.0, PointsOnSegment, 0.0, 1.0);
                        }

                        // Post Process Segment transformation (appy offset)
                        FVector SegStart = CurrentLine.AllPoints[0];
                        FVector SegEnd = CurrentLine.AllPoints[1];

                        if (NavEdgeLedgthOffset < 0.01f && (SegStart - SegEnd).Length() > abs(NavEdgeLedgthOffset * 2.0) && bApplyEdgeOffsetPostNavAnalyze)
                        {
                            FVector LineOrigin = KML::VLerp(SegStart, SegEnd, 0.5);

                            FVector PositionWithOffset = SegStart + (CurrentLine.Direction * NavEdgeLedgthOffset * -1.0);
                            if ((PositionWithOffset - LineOrigin).Length() < 5.0)
                            {
                                PositionWithOffset = LineOrigin + (CurrentLine.Direction * -5.0 * -1.0);
                            }
                            SegStart = PositionWithOffset;

                            PositionWithOffset = SegEnd + (CurrentLine.Direction * -NavEdgeLedgthOffset * -1.0);
                            if ((PositionWithOffset - LineOrigin).Length() < 5.0)
                            {
                                PositionWithOffset = LineOrigin + (CurrentLine.Direction * 5.0 * - 1.0);
                            }
                            SegEnd = PositionWithOffset;

                            CurrentLine.TotalLength = (SegStart - SegEnd).Length();

                            //if (bDrawDebugNavEdges)
                            //{
                            //    DrawDebugLine(GetWorld(), SegStart, SegEnd, FColor::Magenta, false, DebugDrawTime, 0, 2.0);
                            //}

                        }

                        FVector PointFromLine = KML::VLerp(SegStart, SegEnd, LineAlpha);
                        const FVector LineDirection2D = KML::GetForwardVector(FRotator(0.0, KML::MakeRotFromX(NextLine.Direction).Yaw, 0.0));

                        if (bDrawDebugSegmetsPoints == true && abs(PointFromLine.Z - NextLine.StartPosition.Z) > 5.0)
                        {
                            DrawDebugPoint(GetWorld(), PointFromLine, 10.0, FColor::Emerald, false, DebugDrawTime, 0); // ---> DEBUG DRAW
                            //DrawDebugLine(GetWorld(), PointFromLine, KML::VLerp(NextLine.AllPoints[0], NextLine.AllPoints[1], 0.5), FColor::Red, false, DebugDrawTime, 0, 1.6);
                        }

                        FVector SegmentStart = NextLine.StartPosition; SegmentStart.Z = 0.0;
                        FVector SegmentEnd = NextLine.StartPosition + (LineDirection2D * NextLine.TotalLength); SegmentEnd.Z = 0.0;

                        if (UClimbingNavigationBPLibrary::IsPointOnSegment(SegmentStart, SegmentEnd, FVector(PointFromLine.X, PointFromLine.Y,0.0), MaxDist2D_ToNextLine))
                        {
                            //Step 04

                            const float DotBetweenLines = abs(KML::Dot_VectorVector
                            (
                                NormalToFVector(CurrentLine.Direction),
                                NormalToFVector(NextLine.Direction)
                            ));

                            float AngleToFloor = abs(KML::Dot_VectorVector
                            (
                                KML::GetUpVector(KML::MakeRotFromX(CurrentLine.Direction)),
                                FVector(0, 0, 1)
                            ));
                            AngleToFloor = KML::DegAcos(AngleToFloor);

                            //Check Angles Conditions (Te wartosci to na razie na oko)
                            if (DotBetweenLines > AngleTolerance && AngleToFloor < 25.0 && abs(PointFromLine.Z - NextLine.StartPosition.Z) > 5.0)
                            {
                                //Step 05 Check Collision between segments, and make sure the capsule component is have free namespace

                                /*
                                zrzutowany punkt nalerz¹cy do CurrentLine na p³aszczyznê utworzon¹ poprzez
                                IloczynWektorowy pomiêdzy kierunkiem NextLine oraz osi¹ Z, oraz punktem
                                nale¿¹cym do odcinka NextLine
                                */
                                FVector ProjectToNext = PointFromLine;
                                // Local required variables to use FindNormal...
                                FVector OutFindDirection; float OutFindDist;
                                ProjectToNext = PointFromLine + FindNormalBetweenLineAndPoint(OutFindDirection, OutFindDist, PointFromLine, NextLine.StartPosition, NextLine.Direction);

                                //Obliczenie wysokoœci pomiêdzy dwoma odcinkami
                                float StepHeight = KML::GetPointDistanceToLine(ProjectToNext, NextLine.StartPosition, NextLine.Direction);

                                const FVector LL = ProjectToNext - FVector(0, 0, StepHeight);

                                if (bGlobalDebugEnabled == true)
                                {
                                    DrawDebugLine(GetWorld(), PointFromLine, ProjectToNext, FColor::Red, false, DebugDrawTime, 0, 1.0); // ---> DEBUG DRAW
                                }

                                //bool ANavLinksAutoGenerator::CheckCollisionValid(FVector A, FVector B, FVector C, FVector Offset, int DebugType, float DisplayTime)
                                if (StepHeight > MinStepHeightBetweenFloors && StepHeight <= MaxStepHeightBetweenFloors && CheckCollisionValid(PointFromLine, ProjectToNext, LL, FVector(0,0,8), TracesDebugType, DebugDrawTime))
                                {
                                    const FVector NavLinkPosA = PointFromLine - StorageActor->GetActorLocation() + (OutFindDirection * NavLinkTopOffsetFromEdge); //Final position for NavLink TOP
                                    const FVector NavLinkPosB = LL - StorageActor->GetActorLocation() + (OutFindDirection * NavLinkBottomOffsetFromEdge); //Final position for NavLink BOTTOM

                                    FNavigationLink NewLinkStruct;
                                    NewLinkStruct.Left = NavLinkPosA; NewLinkStruct.Right = NavLinkPosB;

                                    if (StepHeight > OnlyOneDirectionHeightTollerance) { NewLinkStruct.Direction = ENavLinkDirection::LeftToRight; }
                                    else { NewLinkStruct.Direction = ENavLinkDirection::BothWays; }

                                    //Add New Link To Nav Links Storage Component
                                    NavLink_NavAreaMode NavMode = NavLink_NavAreaMode::Default;
                                    if (StepHeight > LowPiorityStepHeightTollerance) { NavMode = LowPiorityNavLinkMode; }

                                    NavLinksComponent->AddNewNavLink(NewLinkStruct, 0.0, 1000.0, 30.0, NavMode);

                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (NavLinksComponent)
    {
        NavLinksComponent->RebuildNavLinks();  // TRY REBUILD NAV LINKS
    }
}

bool ANavLinksAutoGenerator::CheckCollisionValid(FVector A, FVector B, FVector C, FVector Offset, int DebugType, float DisplayTime)
{
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(this);

    ETraceTypeQuery TraceQuery = UEngineTypes::ConvertToTraceType(TracingChannel);

    EDrawDebugTrace::Type TType = EDrawDebugTrace::None;
    if (DebugType > 0) { TType = EDrawDebugTrace::ForDuration; }

    FHitResult HorizontalTraceResult;
    bool HitValid = KSL::LineTraceSingle(this, A + Offset, B + Offset, TraceQuery, false, ActorsToIgnore, TType, HorizontalTraceResult, true, FColor::Black, FColor::Red, DisplayTime);
    if (HitValid) { return false; }

    FHitResult VerticalTraceResult;
    HitValid = KSL::LineTraceSingle(this, B + Offset, C + Offset, TraceQuery, false, ActorsToIgnore, TType, HorizontalTraceResult, true, FColor::Black, FColor::Orange, DisplayTime);
    if (HitValid) { return false; }

    FHitResult CapResult;
    HitValid = KSL::CapsuleTraceSingle(this, KML::VLerp(B, C, 0.5) + FVector(0, 0, 90.0), KML::VLerp(B, C, 0.5) + FVector(0, 0, 92.0), 18.0, 80.0, TraceQuery, false, ActorsToIgnore, TType, 
        CapResult, true, FColor::Blue, FColor::Red, DisplayTime);

    if (bCheckCapsuleHaveRoom)
    {
        HitValid = KSL::SphereTraceSingle(this, A + FVector(0,0, TraceCapsuleRadius), A + FVector(0, 0, TraceCapsuleRadius + 100), 18.0, TraceQuery, false, ActorsToIgnore, TType,
            CapResult, true, FColor::Cyan, FColor::Red, DisplayTime);
    }

    return !HitValid;
}

void ANavLinksAutoGenerator::InitializeRequiredObjects(AActor*& StorageActor)
{
    // Lokalne zmienne
    ANavLinksStorage* CurrentNavLinksActor = nullptr;
    bool CompFinded = false;
    FTransform ZeroTransform = FTransform::Identity;

    // Pobierz wszystkie aktory klasy ANavLinksStorage
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANavLinksStorage::StaticClass(), FoundActors);

    if (FoundActors.Num() > 0)
    {
        CurrentNavLinksActor = Cast<ANavLinksStorage>(FoundActors[0]); // Pierwszy znaleziony aktor
    }
    else
    {
        // Jeœli nie ma istniej¹cego aktora, utwórz nowy
        CurrentNavLinksActor = GetWorld()->SpawnActor<ANavLinksStorage>(ANavLinksStorage::StaticClass(), ZeroTransform);
        UE_LOG(LogTemp, Log, TEXT("NavLinks Generator: Spawned new Storage NavLinks Actor, becouse not founded this instance on current level"));
    }

    if (!CurrentNavLinksActor)
    {
        UE_LOG(LogTemp, Error, TEXT("NavLinks Generator: Storage NavLinks actor is not Valid. Actor not found and spawn attempt failed"));
        return; // Jeœli aktor nie istnieje i nie uda³o siê go utworzyæ
    }

    StorageActor = CurrentNavLinksActor; //Return Actor

    FName GuidTag = TEXT("NullGUID");

#if WITH_EDITOR
    GuidTag = FName(ActorGuid.ToString());
#endif

    TArray<UActorComponent*> LinksList;

    for (UActorComponent* Component : CurrentNavLinksActor->GetComponents())
    {
        UDynamicNavLinkComponent* NavLinkComp = Cast<UDynamicNavLinkComponent>(Component);
        if (NavLinkComp && Component->ComponentHasTag(GuidTag))
        {
            NavLinkComp->Links.Empty();
            NavLinkComp->RebuildNavLinks();
            LinksList.Add(Component); // Dodaj komponent do listy
            CompFinded = true;
        }
    }

    if (!CompFinded)
    {
        FString CompName = TEXT("NavLinks_");
        CompName.Append(GuidTag.ToString());

        UActorComponent* NewComp = UClimbingNavigationBPLibrary::CreateComponentFromClassAndAddToActor(CurrentNavLinksActor, UDynamicNavLinkComponent::StaticClass(), ZeroTransform, FName(CompName));
        if (NewComp)
        {
            UDynamicNavLinkComponent* NavLinkComp = Cast<UDynamicNavLinkComponent>(NewComp);
            NewComp->ComponentTags.Add(GuidTag);

            NavLinkComp->Links.Empty();

            UE_LOG(LogTemp, Log, TEXT("NavLinks Generator: Created new DynamicNavLinksComponent, becouse current storage Actor dont have for this generator component with same GUID"));
        }
    }

}

void ANavLinksAutoGenerator::GenerateNavLinks()
{
//This function is only Editor
#if WITH_EDITOR
    AActor* StorageActor = nullptr;
    InitializeRequiredObjects(StorageActor);

    if (StorageActor)
    {
        TArray<FNavLinks_FloorEdges> AllEdges;
        AllEdges = FindPolyEdgesFromNavMesh(true);

        AddNavLinksComponents(AllEdges, StorageActor);
    }
#endif

}
