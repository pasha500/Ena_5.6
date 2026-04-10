

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClimbingNavigationBPLibrary.h"
#include "DynamicNavLinkComponent.h"
#include "NavLinksStorage.h"
#include "NavLinksAutoGenerator.generated.h"

USTRUCT(BlueprintType)
struct FNavLinks_SingleLine : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edge Info")
    TArray<FVector> AllPoints = {};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edge Info")
    FVector StartPosition = FVector(0, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edge Info")
    FVector Direction = FVector(0, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edge Info")
    float TotalLength = 0.0;

};

USTRUCT(BlueprintType)
struct FNavLinks_FloorEdges : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edges")
    TArray<FNavLinks_SingleLine> NavEdges = {};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nav Poly Edges")
    int FloorID = 0;

};

/*
A class that allows the generation of Nav Links (Dynamic Nav Link Component) within a specified area.

The algorithm works by finding all navigation mesh edges within the FBox, comparing them with each other, and checking the required conditions based on the input parameters. The results are stored in a separate actor.

*/
UCLASS(HideCategories = (LevelInstance, WorldPartition, DataLayers, Physics, HLOD, Replication, Networking, Input, Mobile, RayTracing, Cooking, Rendering, Collision, Navigation, AssetUserData))
class CLIMBINGNAVIGATION_API ANavLinksAutoGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ANavLinksAutoGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

    // Komponent root (BoxCollision)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    class UBoxComponent* BoxCollision;

    // Angle Tollerance Between Two Segments. Tolerance determining how far lines can deviate from parallelism. A value of 1.0 means that the segments must be parallel to each other.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Approximation", meta = (SliderMin = "0.1", SliderMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
    float AngleTolerance = 0.8;

    // It has no major effect on function.The value should not be changed
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Approximation", meta = (SliderMin = "1", SliderMax = "45", UIMin = "1", UIMax = "45"))
    float DistanceToleranceScale = 1.8f;

    /*
    ENG:
    Specifies at what distance the section should be divided into smaller fragments. If, for example, NavPolyEdge is 400 cm long and SegmentDivisor is 100 cm, we will finally 
    get 4 points from the segment lying on one straight line (400/100 = 4). In this case, an attempt to create NavLinks will be made for 4 points. These points can be displayed 
    by checking 'bDrawDebugSegmetsPoints' as true.

    PL:
    Okresla co jaka odleglosc odcinek powinien byc podzielony na mniejsze fragmenty. Je¿eli przykladowo NavPolyEdge ma dlugosc 400cm a SegmentDivisor wynosi 100cm to finalnie 
    otrzymamy z odcinka 4 punkty lezace na jednej prostej (400/100 = 4). W takim przypadku proba utworzenia NavLinks nastapi dla 4 punkow. Punkty te mozna wyswietlic zaznaczajac 
    'bDrawDebugSegmetsPoints' jako true.
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Approximation", meta = (Units = "Centimeters"))
    float SegmentDivisor = 100.0f;

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "5", SliderMax = "500", UIMin = "5", UIMax = "500", Units = "Centimeters"))
    //float ConstDistBetweenPointsXY = 150.0f;

    // Specifies the height of a single FBox when dividing the entire area into smaller ones.It has a large impact on the generation accuracy.These segments can be displayed by setting 'bDrawBoxVolumes' to true.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "10", SliderMax = "500", UIMin = "10", UIMax = "500", Units = "Centimeters"))
    float ConstDistBetweenFloors = 50.0f;

    // It affects what the minimum height difference between two adjacent NavPolyEdges can be.If the vertical distance is smaller than this parameter, NavLink will not be generated
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "10", SliderMax = "500", UIMin = "10", UIMax = "500", Units = "Centimeters"))
    float MinStepHeightBetweenFloors = 35.0f;

    // It has the same properties as 'MinStepHeightBetweenFloors' but concerns the maximum height difference.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "10", SliderMax = "500", UIMin = "10", UIMax = "1000", Units = "Centimeters"))
    float MaxStepHeightBetweenFloors = 350.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "10", SliderMax = "500", UIMin = "20", UIMax = "400", Units = "Centimeters"))
    float MaxDist2D_ToNextLine = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (Units = "Centimeters"))
    float LowPiorityStepHeightTollerance = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (Units = "Centimeters"))
    float OnlyOneDirectionHeightTollerance = 260.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "0", SliderMax = "50", UIMin = "0", UIMax = "50", Units = "Centimeters"))
    float NavLinkBottomOffsetFromEdge = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "-50", SliderMax = "0", UIMin = "-50", UIMax = "0", Units = "Centimeters"))
    float NavLinkTopOffsetFromEdge = 0.0;

    // Shifts the beginning and end of the segment towards its center by a constant amount
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (SliderMin = "-50", SliderMax = "0", UIMin = "-50", UIMax = "0", Units = "Centimeters"))
    float NavEdgeLedgthOffset = 0.0;

    // Applly navigation edge offset (NavEdgeLedgthOffset) pre or post navigation analyze
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    bool bApplyEdgeOffsetPostNavAnalyze = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    NavLink_NavAreaMode LowPiorityNavLinkMode = NavLink_NavAreaMode::Obstacle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Trace")
    bool bCheckCapsuleHaveRoom = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Trace")
    float TraceCapsuleRadius = 28.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config|Trace")
    TEnumAsByte<ECollisionChannel> TracingChannel = ECollisionChannel::ECC_Visibility;

    bool bGlobalDebugEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float DebugDrawTime = 6.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawBoxVolumes = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawDebugNavEdges = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawDebugSegmetsPoints = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    int TracesDebugType = 0;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

    TArray<FNavLinks_FloorEdges> FindPolyEdgesFromNavMesh(bool bDrawDebugShapes);

    void AddNavLinksComponents(TArray<FNavLinks_FloorEdges> InLines, AActor* StorageActor);

    bool CheckCollisionValid(FVector A, FVector B, FVector C, FVector Offset, int DebugType, float DisplayTime);

    void InitializeRequiredObjects(AActor*& StorageActor);

    UFUNCTION(CallInEditor, Category = "0) Generator")
    void GenerateNavLinks();


};
