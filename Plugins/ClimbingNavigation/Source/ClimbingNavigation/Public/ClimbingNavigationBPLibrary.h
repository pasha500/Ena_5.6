// Copyright Jakub W, All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Navigation/PathFollowingComponent.h"
//#include "ClimbNavigationStorageActor.h"
#include "ClimbingNavigationBPLibrary.generated.h"

USTRUCT(BlueprintType)
struct FClimbNav_OtherPointParams : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	int LedgeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	int PointID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	int InArrayIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FName PointName = TEXT("ClimbPoint");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float Distance = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float DistanceXY = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float AngleBetween = 0.0;

};


USTRUCT(BlueprintType)
struct FClimbNav_SingleClimbPoint : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform LedgeCenter = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform ActorTransform = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform LedgeLeft = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform LedgeRight = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(0, 0, 0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	int LedgeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	int PointID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FName PointName = TEXT("ClimbPoint");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	TArray<FClimbNav_OtherPointParams> PossibleNextPoints = {};

};

USTRUCT(BlueprintType)
struct FClimbNav_InitialLedgeParams : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation", meta = (MakeEditWidget = true))
	FTransform LeftLedgeBegin = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float LedgeLenght = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation", meta = (MakeEditWidget = true))
	TArray<FTransform> AddtivePoints = {};

};

USTRUCT(BlueprintType)
struct FClimbNav_SplineLedgeParams: public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform StartLedgeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FTransform EndLedgeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	TArray<FTransform> AllTransforms = {};

};


USTRUCT(BlueprintType)
struct FNavEdgeParams : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolyEdge")
	FVector LeftPosition = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolyEdge")
	FVector RightPosition = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolyEdge")
	float Lenght = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolyEdge")
	int EdgeID = 0;

};

UENUM(BlueprintType)
enum class EClimbMovementDirection : uint8
{
	Up UMETA(DisplayName = "Up"),
	Right UMETA(DisplayName = "Right"),
	Left UMETA(DisplayName = "Left"),
	Down UMETA(DisplayName = "Down")
};

UENUM(BlueprintType)
enum class ENavClimbActionState : uint8
{
	None,
	WalkCycle,
	ShortJump,
	LongJump,
	StartClimb,
	JumpBack
};


USTRUCT(BlueprintType)
struct FClimbNav_FullPathData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FVector LeaveClimbProjection = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FVector StartClimbProjection = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FVector LeaveLedgePosition = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	FVector ReachLedgePosition = FVector(0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	TArray<FVector> GroundedPathPoints = {};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	TArray<FClimbNav_SingleClimbPoint> ToLeaveClimbPath = {};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	TArray<FClimbNav_SingleClimbPoint> ToReachTargetClimbPath = {};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float TotalPathLenght = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float GroundedPathLenghtSegment = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	float TotalPathCost = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	bool CurrentClimbPathValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	bool GroundedPathValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climbing Navigation")
	bool NextClimbPathValid = false;

};



UCLASS()
class UClimbingNavigationBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", DisplayName = "Try Find Path For Climb Nav", Keywords = "Climbing Navigation"), Category = "Climbing Navigation System")
	static void TryFindPathForClimbNav(const UObject* WorldContextObject, bool& Succesful, float& TotalLenght, TArray<FClimbNav_SingleClimbPoint>& PathPoints, FVector StartLocation, FVector EndLocation, AClimbNavigationStorageActor* NavMeshInstance);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Point Inside Cylinder Segment", Keywords = "Cylinder,Inside,Geometry", AdvancedDisplay = "DrawDebug"), Category = "Math|Geometry")
	static bool IsPointInsideCylinderSegment(const FRotator& CenterAngle, const FVector& StartPosition, const UObject* WorldContextObject, const FVector& PointToCheck, float HalfAngle = 25, float Radius = 100, float Height = 50, float DrawDebug = -1);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", DisplayName = "Draw Debug Cylinder Part", Keywords = "Cylinder,Debug,Draw", AdvancedDisplay = "Color,LifeTime,Thickness,Segments"), Category = "Rendering|Debug")
	static void DrawDebugPartCylinder(const UObject* WorldContextObject, const FRotator CenterAngle, const FVector StartPosition, float HalfAngle, float Radius, float Height, FLinearColor Color, float LifeTime = 0.5, float Thickness = 0.5, int Segments = 12);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", DisplayName = "Draw Debug Arc", Keywords = "Cylinder,Debug,Draw", AdvancedDisplay = "Segments,Color,LifeTime,Thickness"), Category = "Rendering|Debug")
	static void DrawDebugArc(const UObject* WorldContextObject, const FVector& Center, float Radius, const FVector& Direction, float AngleWidth, int Segments, FLinearColor Color, float LifeTime = 0.5, float Thickness = 0.5);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Is Point On Segment", Keywords = "Point,Segment,Line,Math", AdvancedDisplay = "Tolerance"), Category = "Math|Geometry")
	static bool IsPointOnSegment(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point, float Tolerance = 0.02);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Find Closest Point On Nav Mesh Edge", Keywords = "Point,Navigation,Edge,Line", AdvancedDisplay = "Radius"), Category = "AI|Navigation")
	static bool FindClosestEdgeOnNavMesh(const UObject* WorldContextObject, const FVector& BasePoint, FVector& OutClosestEdgePoint, float Radius = 1000.0f);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Find Closest Nav Mesh Edge", Keywords = "Point,Navigation,Edge,Line", AdvancedDisplay = "Radius"), Category = "AI|Navigation")
	static bool FindClosestNavMeshEdge(UObject* WorldContextObject, const FVector& BasePoint, FVector& EdgePointLeft, FVector& EdgePointRight, float Radius = 1000.0f, bool TryToIgnoreNavLinks = true);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", DisplayName = "Find External Edges In Box", Keywords = "Point,Navigation,Edge,Line"), Category = "AI|Navigation")
	static TArray<FNavEdgeParams> FindExternalEdgesInBox(UObject* WorldContextObject, const FBox& SearchBox, bool bDoNotCutEdges = false);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Does Line Intersect Box", Keywords = "Point,Navigation,Edge,Line"), Category = "Math|Box")
	static bool DoesLineIntersectBox(UObject* WorldContextObject, const FVector& LinePointA, const FVector& LinePointB, const FBox& Box);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Path Following Acceptance Radius", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static void SetPathFollowingAcceptanceRadius(UPathFollowingComponent* PathFollowingComp, float NewRadius = 30.0);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Path Following Acceptance Radius", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static float GetPathFollowingAcceptanceRadius(const UPathFollowingComponent* PathFollowingComp);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Path Following Has Partial Path", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static bool GetPathFollowingHasPartialPath(const UPathFollowingComponent* PathFollowingComp);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Path Following Has Started Nav Link Move", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static bool GetPathFollowingNavLinkMove(const UPathFollowingComponent* PathFollowingComp);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Path Following Segment Is Nav Link", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static bool GetPathFollowingSegmentIsNavLink(const UPathFollowingComponent* PathFollowingComp);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Path Following Current Nav Cost", Keywords = "Navigation,Path,Following"), Category = "AI|Navigation")
	static float GetPathFollowingCurrentNavCost(const UPathFollowingComponent* PathFollowingComp);

public:

	// Allows you to create a permanent component for a specific actor.
	static UActorComponent* CreateComponentFromClassAndAddToActor(AActor* TargetActor, TSubclassOf<UActorComponent> ComponentClass, FTransform RelativeTransform, FName ComponentName = NAME_None);



};
