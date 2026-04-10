

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NavQuery_FullNavPathFinding.generated.h"

/**
 * 
 */
UCLASS()
class CLIMBINGNAVIGATION_API UNavQuery_FullNavPathFinding : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Projection To Navigation")
	FVector SearchBoxExtendUp = FVector(50, 50, 500);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Projection To Navigation")
	FVector SearchBoxExtendDown = FVector(50, 50, 550);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Projection To Navigation")
	int MaxIterationsPerSide = 4;

	//X = offset for searching upper , Y = searching below SearchOring position
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Projection To Navigation")
	FVector2D ForwardOffsets = FVector2D(50, -20);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Nav Climb Actor")
	float FindingNavActorRadius = 250.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Nav Climb Actor")
	FName CollisionProfileName = TEXT("ClimbNavPoint");
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float MaxValidPathLenght = 9999.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float MinValidPathLenght = 20.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float MaxDistBetweenLedgeToTarget = 150.0;

	//Maximum accetable distance between projected point on NavigationMesh to first nearest NavClimbing Ledge point
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Query Config|Other")
	float MaxDistBetweenProjectToLedge = 150.0;

};
