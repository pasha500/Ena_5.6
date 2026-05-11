// Copyright Epic Games, Inc. All Rights Reserved.

//include components
#include "ClimbingNavigationBPLibrary.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"

//include gameFramework
#include "GameFramework/Actor.h"
//include math
#include "Kismet/KismetMathLibrary.h"

//include engine
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

//Enums Collision
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"

#include "ClimbNavigationStorageActor.h"
#include "ClimbingNavigation.h"

//Navigation components
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavMesh/RecastNavMesh.h"

#define KML UKismetMathLibrary 

bool LinePlaneIntersection(const FVector& LineStart, const FVector& LineEnd, const FVector& PlaneOrigin, const FVector& PlaneNormal, FVector& IntersectionPoint)
{
	FVector LineDirection = LineEnd - LineStart;
	float DotProduct = FVector::DotProduct(LineDirection, PlaneNormal);

	// Sprawdzamy, czy linia nie jest r�wnoleg�a do p�aszczyzny
	if (FMath::Abs(DotProduct) < KINDA_SMALL_NUMBER)
	{
		return false; // Brak przeci�cia
	}

	float T = FVector::DotProduct(PlaneOrigin - LineStart, PlaneNormal) / DotProduct;

	// Sprawdzamy, czy punkt przeci�cia znajduje si� na odcinku linii
	if (T < 0.0f || T > 1.0f)
	{
		return false; // Punkt przeci�cia jest poza odcinkiem
	}

	IntersectionPoint = LineStart + T * LineDirection;
	return true;
}

bool IntersectLineWithBox(const FBox& Box, const FVector& LineStart, const FVector& LineEnd, FVector& Intersection)
{
	FVector BoxMin = Box.Min;
	FVector BoxMax = Box.Max;

	// Definicja p�aszczyzn FBox
	TArray<FPlane> BoxPlanes = {
		FPlane(BoxMin, FVector(-1, 0, 0)), // Lewa p�aszczyzna
		FPlane(BoxMax, FVector(1, 0, 0)),  // Prawa p�aszczyzna
        FPlane(BoxMin, FVector(0, -1, 0)), // Bottom plane
        FPlane(BoxMax, FVector(0, 1, 0)),  // Top plane
        FPlane(BoxMin, FVector(0, 0, -1)), // Back plane
        FPlane(BoxMax, FVector(0, 0, 1))   // Front plane
	};
    // Check intersections against each plane
	for (const FPlane& Plane : BoxPlanes)
	{
		if (LinePlaneIntersection(LineStart, LineEnd, Plane.GetOrigin(), Plane.GetNormal(), Intersection))
		{
			return true; // Znaleziono przeci�cie
		}
	}

	return false; // Brak przeci�cia
}


UClimbingNavigationBPLibrary::UClimbingNavigationBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}


void UClimbingNavigationBPLibrary::TryFindPathForClimbNav(const UObject* WorldContextObject, bool& Succesful, float& TotalLenght, TArray<FClimbNav_SingleClimbPoint>& PathPoints, FVector StartLocation, FVector EndLocation, AClimbNavigationStorageActor* NavMeshInstance)
{
	if (NavMeshInstance)
	{
		NavMeshInstance->FindPathBetweenClimbPoints(Succesful, TotalLenght, PathPoints, StartLocation, EndLocation);
	}
	else
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(WorldContextObject, AClimbNavigationStorageActor::StaticClass(), FoundActors);

		for (AActor* Actor : FoundActors)
		{
			AClimbNavigationStorageActor* StorageActor = Cast<AClimbNavigationStorageActor>(Actor);
			if (StorageActor)
			{
				NavMeshInstance->FindPathBetweenClimbPoints(Succesful, TotalLenght, PathPoints, StartLocation, EndLocation);
				return;
			}
		}
	}
}


bool UClimbingNavigationBPLibrary::IsPointInsideCylinderSegment(const FRotator& CenterAngle, const FVector& StartPosition, const UObject* WorldContextObject, const FVector& PointToCheck, float HalfAngle, float Radius, float Height, float DrawDebug)
{

	if (DrawDebug >= 0.f)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
		DrawDebugPartCylinder(WorldContextObject, CenterAngle, StartPosition, HalfAngle, Radius, Height, FLinearColor::Blue, DrawDebug, 0.8, 12);
		//DrawDebugSphere(World, PointToCheck, 5.0, 4, FColor::Cyan, false, DrawDebug, 0, 1.0);
	}

	const FVector AxisUpDirection = KML::GetUpVector(CenterAngle);
	const FVector PlanePoint = StartPosition + (AxisUpDirection * (Height * 0.5));

	FVector ProjectedPoint = KML::ProjectPointOnToPlane(PointToCheck, PlanePoint, AxisUpDirection);

	if ((PointToCheck - ProjectedPoint).Length() <= Height / 2)
	{
		if ((PointToCheck - PlanePoint).Length() <= Radius)
		{
			FVector PointDirection = (ProjectedPoint - PlanePoint).GetSafeNormal();
			const float DotProduct = FVector::DotProduct(PointDirection, KML::GetForwardVector(CenterAngle));
			const float PointAngle = abs(FMath::RadiansToDegrees(FMath::Acos(DotProduct)));

			if (PointAngle <= HalfAngle * 2)
			{
				if (DrawDebug >= 0.f)
				{
					UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
					DrawDebugSphere(World, PointToCheck, 6.0, 4, FColor::Red, false, DrawDebug, 0, 1.0);
				}
				return true;
			}
		}
	}
	return false;
}


void UClimbingNavigationBPLibrary::DrawDebugPartCylinder(const UObject* WorldContextObject, const FRotator CenterAngle, const FVector StartPosition, float HalfAngle, float Radius, float Height, FLinearColor Color, float LifeTime, float Thickness, int Segments)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (World)
	{
		FVector CylinderAxis = CenterAngle.Vector(); // Wektor osi cylindra

		FVector StartDirection = FRotationMatrix(CenterAngle).GetUnitAxis(EAxis::X); // Kierunek pocz�tkowy cylindra

		FColor MainColor = Color.ToFColor(true);

		DrawDebugArc(WorldContextObject, StartPosition, Radius, StartDirection, HalfAngle * 2, Segments, MainColor, LifeTime, Thickness);

		// Krok 3: Narysuj g�r� cylindra
		const FVector CylinderUpDirection = KML::GetUpVector(CenterAngle);
		FVector TopPosition = StartPosition + CylinderUpDirection * Height;
		DrawDebugArc(WorldContextObject, TopPosition, Radius, StartDirection, HalfAngle * 2, Segments, MainColor, LifeTime , Thickness);

		DrawDebugLine(World, StartPosition, TopPosition, MainColor, false, LifeTime, 0, Thickness);

		const FVector RotatedAxisA = KML::RotateAngleAxis(CylinderAxis, HalfAngle * 2, KML::GetUpVector(CenterAngle));
		const FVector RotatedAxisB = KML::RotateAngleAxis(CylinderAxis, -HalfAngle * 2, KML::GetUpVector(CenterAngle));

		DrawDebugLine(World, StartPosition + (RotatedAxisA * Radius), TopPosition + (RotatedAxisA * Radius), MainColor, false, LifeTime, 0, Thickness);
		DrawDebugLine(World, StartPosition + (RotatedAxisB * Radius), TopPosition + (RotatedAxisB * Radius), MainColor, false, LifeTime, 0, Thickness);
	}


}


void UClimbingNavigationBPLibrary::DrawDebugArc(const UObject* WorldContextObject, const FVector& Center, float Radius, const FVector& Direction, float AngleWidth, int Segments, FLinearColor Color, float LifeTime, float Thickness)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (World == nullptr || Segments <= 0)
	{
		return;
	}

	// Kolor dla debugowania
	FColor DebugColor = Color.ToFColor(true);

	// Oblicz krok k�ta dla ka�dego segmentu
	float AngleStep = AngleWidth * 2 / Segments;  // AngleWidth to po�owa �uku, wi�c ca�y �uk to 2 * AngleWidth

	const FVector UpDirection = KML::GetUpVector(KML::MakeRotFromX(Direction));
	const FVector RotatedAxisA = KML::RotateAngleAxis(Direction, -AngleWidth, UpDirection);

	// Punkt pocz�tkowy �uku
	FVector PreviousPoint = Center + RotatedAxisA * Radius;

	// Rysuj linie segment po segmencie
	for (int32 i = 1; i <= Segments; ++i)
	{
		float CurrentAngle = -AngleWidth + i * AngleStep; // Oblicz bie��cy k�t
		FVector CurrentDirection = KML::RotateAngleAxis(Direction, CurrentAngle, UpDirection);
		FVector CurrentPoint = Center + CurrentDirection * Radius;

		// Rysuj lini� pomi�dzy poprzednim punktem a bie��cym punktem
		DrawDebugLine(World, PreviousPoint, CurrentPoint, DebugColor, false, LifeTime, 0, Thickness);

		// Uaktualnij poprzedni punkt
		PreviousPoint = CurrentPoint;
	}

	// Rysuj linie od ko�c�w �uku do �rodka
	FVector FirstPoint = Center + KML::RotateAngleAxis(Direction, -AngleWidth, UpDirection) * Radius;
	FVector LastPoint = PreviousPoint;

	// Linia od pierwszego punktu �uku do �rodka
	DrawDebugLine(World, Center, FirstPoint, DebugColor, false, LifeTime, 0, Thickness);

	// Linia od ostatniego punktu �uku do �rodka
	DrawDebugLine(World, Center, LastPoint, DebugColor, false, LifeTime, 0, Thickness);
}


bool UClimbingNavigationBPLibrary::IsPointOnSegment(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point, float Tolerance)
{
	// Sprawdzenie, czy punkt le�y na prostej wyznaczonej przez odcinek (z uwzgl�dnieniem tolerancji)
	FVector SegmentVector = SegmentEnd - SegmentStart;
	FVector PointVector = Point - SegmentStart;

	// Obliczenie projekcji punktu na wektor odcinka
	float Projection = FVector::DotProduct(PointVector, SegmentVector.GetSafeNormal());

	// Sprawdzenie, czy punkt le�y w przedziale odcinka
	if (Projection < 0 || Projection > SegmentVector.Size())
	{
		return false; // Punkt jest poza odcinkiem
	}

	// Obliczenie najbli�szego punktu na prostej przechodz�cej przez odcinek
	FVector ClosestPointOnLine = SegmentStart + SegmentVector.GetSafeNormal() * Projection;

	// Sprawdzenie, czy odleg�o�� punktu od prostej mie�ci si� w tolerancji
	float DistanceToLine = FVector::Dist(ClosestPointOnLine, Point);
	return DistanceToLine <= Tolerance;
}


bool UClimbingNavigationBPLibrary::FindClosestEdgeOnNavMesh(const UObject* WorldContextObject, const FVector& BasePoint, FVector& OutClosestEdgePoint, float Radius)
{
	// Pobranie systemu nawigacji
	UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("Navigation System is not valid!"));
		return false;
	}

	// Pobranie instancji NavMesh
	const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);

	if (!RecastNavMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RecastNavMesh is not valid!"));
		return false;
	}

	// Znalezienie najbli�szego punktu na NavMesh
	FNavLocation ProjectedPoint;
	if (!NavSys->ProjectPointToNavigation(BasePoint, ProjectedPoint, FVector(Radius, Radius, Radius)))
	{
		UE_LOG(LogTemp, Warning, TEXT("Point is outside of NavMesh bounds."));
		return false;
	}

	FVector ClosestPointOnEdge = FVector::ZeroVector;
	float MinDistance = FLT_MAX;

	// Iteracja przez wielok�ty w promieniu
	TArray<NavNodeRef> NearbyPolys;
	if (!RecastNavMesh->GetPolysWithinPathingDistance(ProjectedPoint.Location, Radius, NearbyPolys))
	{
		//UE_LOG(LogTemp, Warning, TEXT("Failed to find any nearby polys within the radius."));
		return false;
	}

	for (NavNodeRef PolyRef : NearbyPolys)
	{
		TArray<FVector> PolyVertices;
		if (RecastNavMesh->GetPolyVerts(PolyRef, PolyVertices))
		{
			// Iteracja przez kraw�dzie wielok�ta
			for (int32 i = 0; i < PolyVertices.Num(); ++i)
			{
				const FVector& EdgeStart = PolyVertices[i];
				const FVector& EdgeEnd = PolyVertices[(i + 1) % PolyVertices.Num()];

				// Znalezienie punktu �rodkowego kraw�dzi
				FVector MidPoint = (EdgeStart + EdgeEnd) * 0.5f;
                // Compute edge direction vector
				FVector EdgeDirection = (EdgeEnd - EdgeStart).GetSafeNormal();
                // Compute perpendicular vector
                FVector Perpendicular = FVector(-EdgeDirection.Y, EdgeDirection.X, 0.0f); // Assuming XY-plane edges
                // Check points on both sides of the edge
                FVector TestPoint1 = MidPoint + Perpendicular * 4.0f; // Perpendicular offset in one direction
                FVector TestPoint2 = MidPoint - Perpendicular * 4.0f; // Perpendicular offset in the opposite direction
                // Check whether points are on NavMesh
				FNavLocation NavLoc1, NavLoc2;
				bool bIsTestPoint1OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint1, NavLoc1, FVector(1.0f, 1.0f, 1.0f));
				bool bIsTestPoint2OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint2, NavLoc2, FVector(1.0f, 1.0f, 1.0f));
                // If both points are on NavMesh, this is an internal edge, so skip it
				if (bIsTestPoint1OnNavMesh && bIsTestPoint2OnNavMesh)
				{
					continue;
				}

				// Znalezienie najbli�szego punktu na kraw�dzi
				FVector ClosestPoint = FMath::ClosestPointOnSegment(BasePoint, EdgeStart, EdgeEnd);

				// Obliczenie odleg�o�ci od punktu bazowego
				float Distance = FVector::Dist(BasePoint, ClosestPoint);
				if (Distance < MinDistance)
				{
					MinDistance = Distance;
					ClosestPointOnEdge = ClosestPoint;
				}
			}
		}
	}

	// Sprawdzenie, czy znaleziono punkt na kraw�dzi w podanym promieniu
	if (MinDistance <= Radius)
	{
		OutClosestEdgePoint = ClosestPointOnEdge;
		return true;
	}

	//UE_LOG(LogTemp, Warning, TEXT("No edge point found within the specified radius."));
	return false;
}


bool UClimbingNavigationBPLibrary::FindClosestNavMeshEdge(UObject* WorldContextObject, const FVector& BasePoint, FVector& EdgePointLeft, FVector& EdgePointRight, float Radius, bool TryToIgnoreNavLinks)
{
	// Pobranie systemu nawigacji
	UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("Navigation System is not valid!"));
		return false;
	}

	// Pobranie instancji NavMesh
	const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);

	if (!RecastNavMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RecastNavMesh is not valid!"));
		return false;
	}

	// Znalezienie najbli�szego punktu na NavMesh
	FNavLocation ProjectedPoint;
	if (!NavSys->ProjectPointToNavigation(BasePoint, ProjectedPoint, FVector(Radius, Radius, Radius)))
	{
		UE_LOG(LogTemp, Warning, TEXT("Point is outside of NavMesh bounds."));
		return false;
	}

	FVector ClosestEdgeStart = FVector::ZeroVector;
	FVector ClosestEdgeEnd = FVector::ZeroVector;
	float MinDistance = FLT_MAX;

	// Iteracja przez wielok�ty w promieniu
	TArray<NavNodeRef> NearbyPolys;
	if (!RecastNavMesh->GetPolysWithinPathingDistance(ProjectedPoint.Location, Radius, NearbyPolys))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to find any nearby polys within the radius."));
		return false;
	}

	for (NavNodeRef PolyRef : NearbyPolys)
	{
		TArray<FVector> PolyVertices;
		if (RecastNavMesh->GetPolyVerts(PolyRef, PolyVertices))
		{
			// Iteracja przez kraw�dzie wielok�ta
			for (int32 i = 0; i < PolyVertices.Num(); ++i)
			{
				const FVector& EdgeStart = PolyVertices[i];
				const FVector& EdgeEnd = PolyVertices[(i + 1) % PolyVertices.Num()];

				// Znalezienie punktu �rodkowego kraw�dzi
				FVector MidPoint = (EdgeStart + EdgeEnd) * 0.5f;
                // Compute edge direction vector
				FVector EdgeDirection = (EdgeEnd - EdgeStart).GetSafeNormal();
                // Compute perpendicular vector
                FVector Perpendicular = FVector(-EdgeDirection.Y, EdgeDirection.X, 0.0f); // Assuming XY-plane edges
                // Check points on both sides of the edge
                FVector TestPoint1 = MidPoint + Perpendicular * 4.0f; // Perpendicular offset in one direction
                FVector TestPoint2 = MidPoint - Perpendicular * 4.0f; // Perpendicular offset in the opposite direction
                // Check whether points are on NavMesh
				FNavLocation NavLoc1, NavLoc2;
				bool bIsTestPoint1OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint1, NavLoc1, FVector(1.0f, 1.0f, 1.0f));
				bool bIsTestPoint2OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint2, NavLoc2, FVector(1.0f, 1.0f, 1.0f));
                // If both points are on NavMesh, this is an internal edge, so skip it
				if (bIsTestPoint1OnNavMesh && bIsTestPoint2OnNavMesh)
				{
					continue;
				}

				if (TryToIgnoreNavLinks == true)
				{
					if (!bIsTestPoint1OnNavMesh && !bIsTestPoint2OnNavMesh)
					{
						continue;
					}
				}

				// Znalezienie najbli�szego punktu na kraw�dzi
				FVector ClosestPoint = FMath::ClosestPointOnSegment(BasePoint, EdgeStart, EdgeEnd);

				// Obliczenie odleg�o�ci od punktu bazowego
				float Distance = FVector::Dist(BasePoint, ClosestPoint);
				if (Distance < MinDistance)
				{
					MinDistance = Distance;
					ClosestEdgeStart = EdgeStart;
					ClosestEdgeEnd = EdgeEnd;
				}
			}
		}
	}

	// Sprawdzenie, czy znaleziono kraw�d� w podanym promieniu
	if (MinDistance <= Radius)
	{
		EdgePointLeft = ClosestEdgeStart;
		EdgePointRight = ClosestEdgeEnd;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("No edge found within the specified radius."));
	return false;
}


TArray<FNavEdgeParams> UClimbingNavigationBPLibrary::FindExternalEdgesInBox(UObject* WorldContextObject, const FBox& SearchBox, bool bDoNotCutEdges)
{
	TArray<FNavEdgeParams> OutEdges;

	// Pobranie systemu nawigacji
	UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("Navigation System is not valid!"));
		return OutEdges;
	}

	// Pobranie instancji NavMesh
	const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);

	if (!RecastNavMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RecastNavMesh is not valid!"));
		return OutEdges;
	}

	// Znalezienie wielok�t�w w obszarze SearchBox
	TArray<FNavPoly> NearbyPolys;
	if (!RecastNavMesh->GetPolysInBox(SearchBox, NearbyPolys))
	{
		UE_LOG(LogTemp, Warning, TEXT("No polys found in the specified box."));
		return OutEdges;
	}

	// Iteracja przez wielok�ty
	for (const FNavPoly& Poly : NearbyPolys)
	{
		TArray<FVector> PolyVertices;
		if (RecastNavMesh->GetPolyVerts(Poly.Ref, PolyVertices))
		{
			// Iteracja przez kraw�dzie wielok�ta
			for (int32 i = 0; i < PolyVertices.Num(); ++i)
			{
				const FVector& EdgeStart = PolyVertices[i];
				const FVector& EdgeEnd = PolyVertices[(i + 1) % PolyVertices.Num()];

				// Znalezienie punktu �rodkowego kraw�dzi
				FVector MidPoint = (EdgeStart + EdgeEnd) * 0.5f;
                // Compute edge direction vector
				FVector EdgeDirection = (EdgeEnd - EdgeStart).GetSafeNormal();
                // Compute perpendicular vector
                FVector Perpendicular = FVector(-EdgeDirection.Y, EdgeDirection.X, 0.0f); // Assuming XY-plane edges
				FVector TestPoint1 = MidPoint + Perpendicular * 4.0f;
				FVector TestPoint2 = MidPoint - Perpendicular * 4.0f;

				FNavLocation NavLoc1, NavLoc2;
				bool bIsTestPoint1OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint1, NavLoc1, FVector(1.0f, 1.0f, 1.0f));
				bool bIsTestPoint2OnNavMesh = NavSys->ProjectPointToNavigation(TestPoint2, NavLoc2, FVector(1.0f, 1.0f, 1.0f));
                // If both points are on NavMesh, this is an internal edge, so skip it
				if (bIsTestPoint1OnNavMesh && bIsTestPoint2OnNavMesh)
				{
					continue;
				}

				// Przycinanie kraw�dzi do FBox
				FVector ClippedEdgeStart = EdgeStart;
				FVector ClippedEdgeEnd = EdgeEnd;

				if (bDoNotCutEdges == true)
				{
					FNavEdgeParams EdgeParams;
					EdgeParams.LeftPosition = ClippedEdgeStart;
					EdgeParams.RightPosition = ClippedEdgeEnd;
					EdgeParams.Lenght = FVector::Dist(ClippedEdgeStart, ClippedEdgeEnd);
					EdgeParams.EdgeID = OutEdges.Num();

					OutEdges.Add(EdgeParams);
					continue;
				}

				bool bStartInside = SearchBox.IsInside(EdgeStart);
				bool bEndInside = SearchBox.IsInside(EdgeEnd);

				if (!bStartInside && !bEndInside)
				{
					continue; // Obie ko�c�wki kraw�dzi s� poza obszarem
				}

				if (!bStartInside)
				{
					FVector Intersection;
					//DrawDebugSphere(World, EdgeStart, 8, 8, FColor::Green, false, 6.0, 0, 1.5);

					if (IntersectLineWithBox(SearchBox, EdgeStart, EdgeEnd, Intersection))
					{
						ClippedEdgeStart = Intersection; // Punkt przeci�cia z FBox
					}
				}

				if (!bEndInside)
				{
					FVector Intersection;
					//DrawDebugSphere(World, EdgeEnd, 8, 8, FColor::Green, false, 6.0, 0, 1.5);

					if (IntersectLineWithBox(SearchBox, EdgeEnd, EdgeStart, Intersection))
					{
						ClippedEdgeEnd = Intersection; // Punkt przeci�cia z FBox
					}
				}

				// Dodanie kraw�dzi do wynik�w
				FNavEdgeParams EdgeParams;
				EdgeParams.LeftPosition = ClippedEdgeStart;
				EdgeParams.RightPosition = ClippedEdgeEnd;
				EdgeParams.Lenght = FVector::Dist(ClippedEdgeStart, ClippedEdgeEnd);
				EdgeParams.EdgeID = OutEdges.Num();

				OutEdges.Add(EdgeParams);
			}
		}
	}

	return OutEdges;
}


bool UClimbingNavigationBPLibrary::DoesLineIntersectBox(UObject* WorldContextObject, const FVector& LinePointA, const FVector& LinePointB, const FBox& Box)
{
	// Wyznacz wektor kierunku linii
	FVector LineDirection = LinePointB - LinePointA;

	// Je�li linia jest zdegenerowana (punkty s� takie same)
	if (LineDirection.IsNearlyZero())
	{
		return Box.IsInside(LinePointA); // Sprawd�, czy punkt A znajduje si� w Box
	}

	// Normalizacja kierunku
	LineDirection.Normalize();

	// Sprawd� przeci�cie przy pomocy FBox::Intersect
	FVector Intersected;
	return IntersectLineWithBox(Box, LinePointA, LinePointB, Intersected);
}

void UClimbingNavigationBPLibrary::SetPathFollowingAcceptanceRadius(UPathFollowingComponent* PathFollowingComp, float NewRadius)
{
	if (PathFollowingComp)
	{
		PathFollowingComp->SetAcceptanceRadius(NewRadius);
	}
}

float UClimbingNavigationBPLibrary::GetPathFollowingAcceptanceRadius(const UPathFollowingComponent* PathFollowingComp)
{
	if (PathFollowingComp)
	{
		return PathFollowingComp->GetAcceptanceRadius();
	}
	return -1.0;
}

bool UClimbingNavigationBPLibrary::GetPathFollowingHasPartialPath(const UPathFollowingComponent* PathFollowingComp)
{
	if (PathFollowingComp)
	{
		return PathFollowingComp->HasPartialPath();
	}
	return false;
}

bool UClimbingNavigationBPLibrary::GetPathFollowingNavLinkMove(const UPathFollowingComponent* PathFollowingComp)
{
	if (PathFollowingComp)
	{
		return PathFollowingComp->HasStartedNavLinkMove();
	}
	return false;
}

bool UClimbingNavigationBPLibrary::GetPathFollowingSegmentIsNavLink(const UPathFollowingComponent* PathFollowingComp)
{
	if (PathFollowingComp)
	{
		return PathFollowingComp->IsCurrentSegmentNavigationLink();
	}
	return false;
}

float UClimbingNavigationBPLibrary::GetPathFollowingCurrentNavCost(const UPathFollowingComponent* PathFollowingComp)
{
	if (PathFollowingComp)
	{
		if (PathFollowingComp->HasValidPath())
		{
			if (PathFollowingComp->GetPath().IsValid())
			{
				return PathFollowingComp->GetPath()->GetCost();
			}
		}
	}
	return -999.9;
}


UActorComponent* UClimbingNavigationBPLibrary::CreateComponentFromClassAndAddToActor(AActor* TargetActor, TSubclassOf<UActorComponent> ComponentClass, FTransform RelativeTransform, FName ComponentName)
{
	// Sprawdzenie, czy aktor i klasa komponentu s� prawid�owe
	if (!TargetActor || !*ComponentClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid actor or component class passed to AddComponentToActor!"));
		return nullptr;
	}

	// Tworzenie komponentu
	UActorComponent* NewComponent = NewObject<UActorComponent>(TargetActor, ComponentClass, ComponentName);
	if (!NewComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to create component of class %s!"), *ComponentClass->GetName());
		return nullptr;
	}

	// Je�li komponent to USceneComponent (tylko takie mog� mie� transform)
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent))
	{
		// Ustawienie transformacji
		SceneComponent->SetupAttachment(TargetActor->GetRootComponent());
		SceneComponent->SetRelativeTransform(RelativeTransform);

		// Zarejestrowanie komponentu, aby by� widoczny i dzia�a� poprawnie
		SceneComponent->RegisterComponent();
	}
	else
	{
		// Je�li nie jest to SceneComponent, tylko rejestrujemy
		NewComponent->RegisterComponent();
	}

	// Dodanie komponentu jako instancji, aby by� trwa�y
	TargetActor->AddInstanceComponent(NewComponent);

	// Oznaczenie aktora jako "brudny", aby Unreal Engine wiedzia�, �e nale�y go zapisa�
	TargetActor->MarkPackageDirty();

	// Zwr�cenie nowo utworzonego komponentu
	return NewComponent;
}
