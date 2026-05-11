


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


// Zwraca najkr�tsz� �cie�k� jako TArray punkt�w
TArray<FClimbNav_SingleClimbPoint> AClimbNavigationStorageActor::FindPathBetweenTwoIndex(int StartPointIndex, int EndPointIndex)
{
    TSet<int> VisitedPoints; // Zestaw punkt�w ju� odwiedzonych
    TArray<int> PointsToVisit; // Punkty do odwiedzenia
    TMap<int, float> Distances; // Minimalne odleg�o�ci do ka�dego punktu
    TMap<int, int> PreviousPoints; // Poprzednie punkty w �cie�ce

    // Inicjalizacja
    Distances.Add(StartPointIndex, 0.0f);
    PointsToVisit.Add(StartPointIndex);

    for (int i = 0; i < NavigationCompleteMap.Num(); i++)
    {
        if (i != StartPointIndex)
        {
            Distances.Add(i, FLT_MAX); // Ustawiamy niesko�czono�� dla punkt�w innych ni� pocz�tkowy
        }
        PreviousPoints.Add(i, -1); // Brak poprzednika na pocz�tku
    }

    // Algorytm Dijkstra z dodatkowymi warunkami
    while (PointsToVisit.Num() > 0)
    {
        // Znajd� punkt o najmniejszej odleg�o�ci, kt�ry jeszcze nie zosta� odwiedzony
        int CurrentPointIndex = GetPointWithSmallestDistance(Distances, PointsToVisit);
        PointsToVisit.Remove(CurrentPointIndex); // Usu� ten punkt z listy do odwiedzenia
        VisitedPoints.Add(CurrentPointIndex); // Zaznacz jako odwiedzony

        // Je�eli doszli�my do punktu ko�cowego, przerywamy
        if (CurrentPointIndex == EndPointIndex)
        {
            break;
        }

        const FClimbNav_SingleClimbPoint& CurrentPoint = NavigationCompleteMap[CurrentPointIndex];

        // Przetwarzanie s�siad�w bie��cego punktu
        for (const FClimbNav_OtherPointParams& Neighbor : CurrentPoint.PossibleNextPoints)
        {
            int NeighborIndex = Neighbor.InArrayIndex;

            // Je�eli punkt s�siaduj�cy by� ju� odwiedzony, pomijamy go
            if (VisitedPoints.Contains(NeighborIndex))
            {
                continue;
            }

            const FClimbNav_SingleClimbPoint& NeighborPoint = NavigationCompleteMap[NeighborIndex];

            // Oblicz dystans
            float NewDistance = Distances[CurrentPointIndex] + Neighbor.Distance;

            // Warunek maksymalnej akceptowalnej odleg�o�ci
            if (Neighbor.Distance > 500.0)
            {
                continue; // Pomijamy punkty, kt�re s� zbyt oddalone
            }

            // Preferencje r�nicy wysoko�ci
            float HeightDifference = FMath::Abs(CurrentPoint.LedgeCenter.GetLocation().Z - NeighborPoint.LedgeCenter.GetLocation().Z);
            float HeightFactor = FMath::Lerp(1.0f, HeightDifference, PreferLowerAltitudesWeight); // Gdy PreferLowerAltitudesWeight = 1, r�nica wysoko�ci ma pe�ne znaczenie

            // Preferencje k�ta
            FVector CurrentForward = CurrentPoint.LedgeCenter.GetRotation().GetForwardVector();
            FVector NeighborForward = NeighborPoint.LedgeCenter.GetRotation().GetForwardVector();
            float AngleValue = FMath::Acos(FVector::DotProduct(CurrentForward, NeighborForward)); // R�nica k�ta w radianach
            float AngleDifference = FMath::GetMappedRangeValueClamped(FVector2D(0.0, 180.0), FVector2D(1.0, 10.0), AngleValue);
            float AngleFactor = FMath::Lerp(1.0f, AngleDifference, SameAnglePreferenceWeight); // Gdy SameAnglePreferenceWeight = 1, r�nica k�ta ma pe�ne znaczenie

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

            // Finalny "koszt" s�siada uwzgl�dniaj�cy wag� r�nicy wysoko�ci i k�ta
            float NeighborCost = NewDistance * HeightFactor * AngleFactor;

            // Je�eli nowa odleg�o�� jest mniejsza, aktualizujemy
            if (NeighborCost < Distances[NeighborIndex])
            {
                Distances[NeighborIndex] = NeighborCost;
                PreviousPoints[NeighborIndex] = CurrentPointIndex;

                // Dodaj s�siada do kolejki do odwiedzenia, je�eli nie by� jeszcze odwiedzony
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
    // Recreate the path from end point to start point
    // Path reconstruction
    return ReconstructPath(PreviousPoints, EndPointIndex);
}
// Helper: find the point with the smallest distance in queue
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
// Helper: reconstruct the path
TArray<FClimbNav_SingleClimbPoint> AClimbNavigationStorageActor::ReconstructPath(const TMap<int, int>& PreviousPoints, int EndPointIndex)
{
    TArray<FClimbNav_SingleClimbPoint> Path;
    int CurrentIndex = EndPointIndex;

    while (CurrentIndex != -1)
    {
        Path.Insert(NavigationCompleteMap[CurrentIndex], 0); // Insert at path start
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