


#include "ClimbNavigationStorageActor.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "CollisionQueryParams.h"

// Sets default values
AClimbNavigationStorageActor::AClimbNavigationStorageActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
    Tags.Add("ClimbNavigationStorageActor");
}

// Called when the game starts or when spawned
void AClimbNavigationStorageActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AClimbNavigationStorageActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


// Zwraca najkrótsz¹ œcie¿kê jako TArray punktów
TArray<FClimbNav_SingleClimbPoint> AClimbNavigationStorageActor::FindPathBetweenTwoIndex(int StartPointIndex, int EndPointIndex)
{
    TSet<int> VisitedPoints; // Zestaw punktów ju¿ odwiedzonych
    TArray<int> PointsToVisit; // Punkty do odwiedzenia
    TMap<int, float> Distances; // Minimalne odleg³oœci do ka¿dego punktu
    TMap<int, int> PreviousPoints; // Poprzednie punkty w œcie¿ce

    // Inicjalizacja
    Distances.Add(StartPointIndex, 0.0f);
    PointsToVisit.Add(StartPointIndex);

    for (int i = 0; i < NavigationCompleteMap.Num(); i++)
    {
        if (i != StartPointIndex)
        {
            Distances.Add(i, FLT_MAX); // Ustawiamy nieskoñczonoœæ dla punktów innych ni¿ pocz¹tkowy
        }
        PreviousPoints.Add(i, -1); // Brak poprzednika na pocz¹tku
    }

    // Algorytm Dijkstra z dodatkowymi warunkami
    while (PointsToVisit.Num() > 0)
    {
        // ZnajdŸ punkt o najmniejszej odleg³oœci, który jeszcze nie zosta³ odwiedzony
        int CurrentPointIndex = GetPointWithSmallestDistance(Distances, PointsToVisit);
        PointsToVisit.Remove(CurrentPointIndex); // Usuñ ten punkt z listy do odwiedzenia
        VisitedPoints.Add(CurrentPointIndex); // Zaznacz jako odwiedzony

        // Je¿eli doszliœmy do punktu koñcowego, przerywamy
        if (CurrentPointIndex == EndPointIndex)
        {
            break;
        }

        const FClimbNav_SingleClimbPoint& CurrentPoint = NavigationCompleteMap[CurrentPointIndex];

        // Przetwarzanie s¹siadów bie¿¹cego punktu
        for (const FClimbNav_OtherPointParams& Neighbor : CurrentPoint.PossibleNextPoints)
        {
            int NeighborIndex = Neighbor.InArrayIndex;

            // Je¿eli punkt s¹siaduj¹cy by³ ju¿ odwiedzony, pomijamy go
            if (VisitedPoints.Contains(NeighborIndex))
            {
                continue;
            }

            const FClimbNav_SingleClimbPoint& NeighborPoint = NavigationCompleteMap[NeighborIndex];

            // Oblicz dystans
            float NewDistance = Distances[CurrentPointIndex] + Neighbor.Distance;

            // Warunek maksymalnej akceptowalnej odleg³oœci
            if (Neighbor.Distance > 500.0)
            {
                continue; // Pomijamy punkty, które s¹ zbyt oddalone
            }

            // Preferencje ró¿nicy wysokoœci
            float HeightDifference = FMath::Abs(CurrentPoint.LedgeCenter.GetLocation().Z - NeighborPoint.LedgeCenter.GetLocation().Z);
            float HeightFactor = FMath::Lerp(1.0f, HeightDifference, PreferLowerAltitudesWeight); // Gdy PreferLowerAltitudesWeight = 1, ró¿nica wysokoœci ma pe³ne znaczenie

            // Preferencje k¹ta
            FVector CurrentForward = CurrentPoint.LedgeCenter.GetRotation().GetForwardVector();
            FVector NeighborForward = NeighborPoint.LedgeCenter.GetRotation().GetForwardVector();
            float AngleValue = FMath::Acos(FVector::DotProduct(CurrentForward, NeighborForward)); // Ró¿nica k¹ta w radianach
            float AngleDifference = FMath::GetMappedRangeValueClamped(FVector2D(0.0, 180.0), FVector2D(1.0, 10.0), AngleValue);
            float AngleFactor = FMath::Lerp(1.0f, AngleDifference, SameAnglePreferenceWeight); // Gdy SameAnglePreferenceWeight = 1, ró¿nica k¹ta ma pe³ne znaczenie

            bool NoCollisionChecked = true;

            if (CheckCollisionWhenPathFinding)
            {
                ETraceTypeQuery TraceChannel = ETraceTypeQuery::TraceTypeQuery1;
                FHitResult TraceResult;
                TArray<AActor*> ActorsToIgnore = {};

                const bool TraceValid = UKismetSystemLibrary::LineTraceSingle(this, NeighborPoint.ActorTransform.GetLocation(), CurrentPoint.ActorTransform.GetLocation(), 
                    TraceChannel, false, ActorsToIgnore, EDrawDebugTrace::None, TraceResult, false, FColor::Emerald, FColor::Red, 1.0);

                if (TraceValid)
                {
                    NoCollisionChecked = false;
                }

            }

            // Finalny "koszt" s¹siada uwzglêdniaj¹cy wagê ró¿nicy wysokoœci i k¹ta
            float NeighborCost = NewDistance * HeightFactor * AngleFactor;

            // Je¿eli nowa odleg³oœæ jest mniejsza, aktualizujemy
            if (NeighborCost < Distances[NeighborIndex])
            {
                Distances[NeighborIndex] = NeighborCost;
                PreviousPoints[NeighborIndex] = CurrentPointIndex;

                // Dodaj s¹siada do kolejki do odwiedzenia, je¿eli nie by³ jeszcze odwiedzony
                if (!PointsToVisit.Contains(NeighborIndex))
                {
                    if (NoCollisionChecked)
                    {
                        PointsToVisit.Add(NeighborIndex);
                    }
                }
            }
        }
    }

    // Odtwarzanie œcie¿ki od punktu koñcowego do pocz¹tkowego
    // Rekonstrukcja œcie¿ki
    return ReconstructPath(PreviousPoints, EndPointIndex);
}

// Funkcja pomocnicza do znalezienia punktu z najmniejsz¹ odleg³oœci¹ w kolejce
int AClimbNavigationStorageActor::GetPointWithSmallestDistance(const TMap<int, float>& Distances, const TArray<int>& PointsToVisit)
{
    float MinDistance = FLT_MAX;
    int BestPoint = -1;

    for (int Point : PointsToVisit)
    {
        float Distance = Distances[Point];
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            BestPoint = Point;
        }
    }

    return BestPoint;
}


// Funkcja pomocnicza do rekonstrukcji œcie¿ki
TArray<FClimbNav_SingleClimbPoint> AClimbNavigationStorageActor::ReconstructPath(const TMap<int, int>& PreviousPoints, int EndPointIndex)
{
    TArray<FClimbNav_SingleClimbPoint> Path;
    int CurrentIndex = EndPointIndex;

    while (CurrentIndex != -1)
    {
        Path.Insert(NavigationCompleteMap[CurrentIndex], 0); // Dodajemy na pocz¹tek œcie¿ki
        CurrentIndex = PreviousPoints[CurrentIndex]; // Przechodzimy do poprzedniego punktu
    }

    return Path;
}

bool AClimbNavigationStorageActor::FindNearestNavLedge(int& ReturnIndex, float& ReturnDistance, FClimbNav_SingleClimbPoint& ReturnLedgeParams, FVector InLocation)
{
    if (NavigationCompleteMap.Num() == 0) { ReturnIndex = 0; ReturnDistance = -1.0; return false; }

    float CurrentDistance = 999999.0;
    int CurrentIndex = 0;

    for (int i = 0; i < NavigationCompleteMap.Num(); i++)
    {
        const FClimbNav_SingleClimbPoint Current = NavigationCompleteMap[i];

        const FVector LedgeLocation = Current.LedgeCenter.GetLocation();

        if ((LedgeLocation - InLocation).Length() < CurrentDistance)
        {
            CurrentDistance = (LedgeLocation - InLocation).Length();
            CurrentIndex = i;
        }
    }
    ReturnLedgeParams = NavigationCompleteMap[CurrentIndex];
    ReturnDistance = CurrentDistance;
    ReturnIndex = CurrentIndex;
    return true;
}

TArray<FClimbNav_SingleClimbPoint> AClimbNavigationStorageActor::GetLastBuildedPath()
{
    if (LastBuildedPath.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClimbNav: Path is not builded but some code required path points from GetLastBuildedPath()"));
        return LastBuildedPath;
    }
    return LastBuildedPath;
}


void AClimbNavigationStorageActor::FindPathBetweenClimbPoints(bool& Succesful, float& TotalLenght, TArray<FClimbNav_SingleClimbPoint>& PathPoints, FVector StartLocation, FVector EndLocation)
{
    if (NavigationCompleteMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClimbNav: FindPath function is excuted, but NavigationMap array is empyt"));
        Succesful = false;  TotalLenght = -1.0; return;
    }

    int StartLedgeIndex = 0;
    int EndLedgeIndex = NavigationCompleteMap.Num();
    float StartDistance = 0.0;
    float EndDistance = 0.0;
    FClimbNav_SingleClimbPoint StartLedgeParams;
    FClimbNav_SingleClimbPoint EndLedgeParams;

    FindNearestNavLedge(StartLedgeIndex, StartDistance, StartLedgeParams, StartLocation);
    FindNearestNavLedge(EndLedgeIndex, EndDistance, EndLedgeParams, EndLocation);

    TArray<FClimbNav_SingleClimbPoint> BuildedPath;

    if (StartLedgeIndex != EndLedgeIndex)
    {
        BuildedPath = FindPathBetweenTwoIndex(StartLedgeIndex, EndLedgeIndex);

        if (BuildedPath.Num() > 0)
        {
            float CalculatedTotalDistance = 0.0;

            for (int i = 0; i < BuildedPath.Num() - 1; i++)
            {
                CalculatedTotalDistance = CalculatedTotalDistance + (BuildedPath[i].LedgeCenter.GetLocation() - BuildedPath[i + 1].LedgeCenter.GetLocation()).Length();
            }

            TotalLenght = CalculatedTotalDistance;
            Succesful = true;
            LastBuildedPath = BuildedPath; //Global Variable
            PathPoints = BuildedPath;
            return;
        }
    }
    Succesful = false;  TotalLenght = -1.0; return;
}