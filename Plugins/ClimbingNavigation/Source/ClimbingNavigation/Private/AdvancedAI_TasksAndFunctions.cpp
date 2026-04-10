


#include "AdvancedAI_TasksAndFunctions.h"

//include components
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"

//include gameFramework
#include "GameFramework/Character.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"

//include math
#include "Kismet/KismetMathLibrary.h"

//include engine
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Kismet/GameplayStatics.h"

//Enums Collision
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"

//Include Navigation
#include "NavigationSystem.h"

#define KML UKismetMathLibrary
#define KSL UKismetSystemLibrary
#define CapHeight RefChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
#define CNL UClimbingNavigationBPLibrary

#define DEBUGDRAW_LINE DrawDebugLine
#define DEBUGDRAW_SPHERE DrawDebugSphere
#define DEBUGDRAW_POINT DrawDebugPoint
#define DEBUGDRAW_STRING DrawDebugString
#define DEBUGDRAW_ARROW DrawDebugArrow

// Sets default values for this component's properties
UAdvancedAI_TasksAndFunctions::UAdvancedAI_TasksAndFunctions()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0;
}


// Called when the game starts
void UAdvancedAI_TasksAndFunctions::BeginPlay()
{
	Super::BeginPlay();
	
	AController* IsController = Cast<AController>(GetOwner());

	if (IsController)
	{
		RefChar = Cast<ACharacter>(IsController->GetPawn());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Advanced AI functions component: References character is NULL"));
	}

}


// Called every frame
void UAdvancedAI_TasksAndFunctions::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!RefChar)
	{
		AController* IsController = Cast<AController>(GetOwner());

		if (IsController)
		{
			RefChar = Cast<ACharacter>(IsController->GetPawn());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Advanced AI functions component: References character is NULL"));
		}
	}


	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UAdvancedAI_TasksAndFunctions::TryFindHindingPositionSync(bool& Succesful, FTransform& ChoosedTransform, float& PathLenght, FVector TracingOrigin, UNavQuery_HidingLocSearchParams* QueryParamsData, TSubclassOf<UHidingLocSearch_EnemyProperties> CustomOverrideData)
{
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 0: Main Variables
	TArray<ACharacter*> EnemysInRadius;
	TArray<FHitResult> DetectedPoints;
	FVector InstanceLoc = FVector(0, 0, 0);
	FRotator InstanceRot = FRotator(0, 0, 0);
	TArray<float> PointsWeight;
	TArray<FTransform> FirstPossiblePoints;
	TArray<FTransform> FinalPossiblePoints;

	Succesful = false;

	if (!QueryParamsData) return;
	if (!RefChar) { UE_LOG(LogTemp, Error, TEXT("TryFindHindingPositionSync: References character is NULL")); return; }

	// Tworzymy instancję klasy przekazanej jako typ
	UHidingLocSearch_EnemyProperties* OverrideInstance = NewObject<UHidingLocSearch_EnemyProperties>(this, CustomOverrideData);

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 1: Find Enemy Characters in Radius
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TSubclassOf<AActor> ActorClassFilter = ACharacter::StaticClass();

	TArray<AActor*> OverlappedActors;

	TArray<AActor*> ToIgnore;
	ToIgnore.Add(RefChar);
	// Perform Searching
	KSL::SphereOverlapActors(RefChar, TracingOrigin, QueryParamsData->EnemySearchRadius, ObjectTypes, ActorClassFilter, ToIgnore, OverlappedActors);

	for (AActor* CurrentAct : OverlappedActors)
	{
		ACharacter* CurrentChar = Cast<ACharacter>(CurrentAct);
		if (CurrentChar)
		{
			//Potrzebna aktualizacja w celu filtracji
			if (QueryParamsData->AlwaysIgnorePlayerAsEnemy)
			{
				if (!CurrentChar->IsPlayerControlled())
				{
					EnemysInRadius.Add(CurrentChar);
				}
			}
			else
			{
				EnemysInRadius.Add(CurrentChar);
			}

		}
	}

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 2: Find CoverPoints

	EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
	if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }

	ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(QueryParamsData->PointsFindingChannel); // Tylko Testowanie QueryParamsData->PointsFindingChannel

	KSL::SphereTraceMulti(
		RefChar, 
		TracingOrigin, 
		TracingOrigin + (RefChar->GetActorForwardVector() * 20.0), 
		QueryParamsData->FindingPointsRadius, 
		TraceType,
		false, 
		ToIgnore, 
		TraceDebugType,
		DetectedPoints, 
		true, 
		FLinearColor::Black, 
		FLinearColor::Blue, 
		DebugDuration
	);

	bool CurrentPointIsValid = false;

	for (FHitResult CurrentPoint : DetectedPoints)
	{
		CurrentPointIsValid = false;
		UInstancedStaticMeshComponent* InstanceClass = Cast<UInstancedStaticMeshComponent>(CurrentPoint.GetComponent());
		if (InstanceClass)
		{
			FTransform InstanceTransform = FTransform::Identity;
			CurrentPointIsValid = InstanceClass->GetInstanceTransform(CurrentPoint.Item, InstanceTransform, true);

			InstanceLoc = InstanceTransform.GetLocation();
			InstanceRot = InstanceTransform.Rotator();
		}

		if (CurrentPointIsValid == false) { break; } //If Current Point is not valid then breake loop

		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 3: Check Points
		if (CheckPointIsNotInVisibilityRange(EnemysInRadius, InstanceLoc, InstanceRot, QueryParamsData->MinValidPointDistToEnemy, QueryParamsData->WallCheckTraceRadius, OverrideInstance, false) == true)
		{
			UNavigationPath* CurrentPath = TryFindPathToLocationSync(RefChar->GetActorLocation() - FVector(0, 0, CapHeight * 0.75), InstanceLoc);
			if (CurrentPath)
			{
				float CurrentWeight = 0.0;

				// Make Point Weight

				CurrentWeight = KML::SafeDivide
				(
					CurrentPath->GetPathLength() * 0.1 * MakeDistributionWeight(QueryParamsData->PathLenghtToPointWeight),
					DistanceToNearsetEnemy(EnemysInRadius, InstanceLoc) * 0.1 * MakeDistributionWeight(QueryParamsData->DistanceToNearestEnemyWeight)
				);

				CurrentWeight = CurrentWeight + (KML::Vector_Distance(InstanceLoc, TracingOrigin) * 0.1 * MakeDistributionWeight(QueryParamsData->ReachOriginWeight)); // Try reach origin

				FVector TotalAngleDirection;
				CurrentWeight = CurrentWeight * 
					KML::Lerp(1.0,CalculateAngleWeight(
						TotalAngleDirection, 
						EnemysInRadius, 
						InstanceRot, 
						TracingOrigin, 
						QueryParamsData->ApplyAbsToAngleCalculationMap
					), 
						MakeDistributionWeight(QueryParamsData->PointAngleWeight)
					);

				PointsWeight.Add(CurrentWeight);
				FirstPossiblePoints.Add(FTransform(InstanceRot, InstanceLoc, FVector(1, 1, 1)));

				if (bDrawDebugPointInfo) { DrawDebugPointInfo(InstanceLoc, CurrentWeight); } // *DEBUG FUNCTION*
			}
		}
	}
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 4: Check Points
	if (PointsWeight.Num() > 0)
	{
		TArray<float> PathVisiblityWeight;
		PathVisiblityWeight.SetNum(QueryParamsData->MaxPotencialPathsCreation);

		TArray<float> PathsLenght;

		int CurrentMinIndex = 0;
		float CurrentMinValue = 0.0;
		FTransform CurrentMinTansform = FTransform::Identity;

		if (PathVisiblityWeight.Num() == 0) { Succesful = false; return; } // ZMIANA 2025 dla AGLS v1.6

		for (int i = 0; i < PathVisiblityWeight.Num(); i++)
		{
			KML::MinOfFloatArray(PointsWeight, CurrentMinIndex, CurrentMinValue);
			CurrentMinTansform = FirstPossiblePoints[CurrentMinIndex];

			UNavigationPath* CurrentPath2 = TryFindPathToLocationSync(RefChar->GetActorLocation() - FVector(0, 0, CapHeight * 0.75), CurrentMinTansform.GetLocation());
			if (CurrentPath2)
			{
				TArray<FVector> PathPoints = CurrentPath2->PathPoints;
				SubdividePath(PathPoints, PathPoints, QueryParamsData->ConstPathSubdividor, QueryParamsData->UseDynamicSubdividion, QueryParamsData->DynamicSubSegmentLenght);
				PathsLenght.Add(CurrentPath2->GetPathLength());

				int ActorVisibleCount = 0;
				float CalculatedWeight = 0.0;

				for (int ii = 1; ii < PathPoints.Num(); ii++)
				{
					if (PathPoints.IsValidIndex(ii))
					{
						CalculatedWeight = MakePointVisiblityWeight(
							EnemysInRadius, 
							PathPoints[ii] + FVector(0, 0, QueryParamsData->CoverPointHeightOffset), 
							FRotator(0, 0, 0), 
							QueryParamsData->MinValidPointDistToEnemy, 
							QueryParamsData->WallCheckTraceRadius,
							QueryParamsData->NormalizePathWeights,
							OverrideInstance,
							false
						);

						PathVisiblityWeight[i] = PathVisiblityWeight[i] + CalculatedWeight; //!!!
						ActorVisibleCount++;
					}
				}

				if (PathVisiblityWeight.IsValidIndex(i)) //Set Array element
				{
					PathVisiblityWeight[i] = KML::SafeDivide(PathVisiblityWeight[i], (float)ActorVisibleCount) - (i * 0.01);

					FinalPossiblePoints.Add(CurrentMinTansform);

					PointsWeight.RemoveAt(CurrentMinIndex);
					FirstPossiblePoints.RemoveAt(CurrentMinIndex);
					 
					if(bDrawDebugPathInfo) { DrawDebugPath(PathPoints, PathVisiblityWeight[i]); } // *DEBUG FUNCTION*
				}

			}
		}

		// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 5: RETURN
		TArray<float> PathWeightsWithTreshold;
		TArray<FTransform> PathTransformWithTreshold;
		TArray<float> PathsLenghtTreshold;

		for (int iii = 0; iii < PathVisiblityWeight.Num(); iii++)
		{
			if (PathVisiblityWeight[iii] > QueryParamsData->AsCorrectPathWeightMin)
			{
				PathWeightsWithTreshold.Add(PathVisiblityWeight[iii]);
				PathTransformWithTreshold.Add(FinalPossiblePoints[iii]);
				PathsLenghtTreshold.Add(PathsLenght[iii]);
			}
		}

		if (PathWeightsWithTreshold.Num() > 0 && PathTransformWithTreshold.Num() > 0)
		{
			int MaxIndex = 0; float MaxValue = 1.0;
			KML::MaxOfFloatArray(PathWeightsWithTreshold, MaxIndex, MaxValue);
			
			Succesful = PathTransformWithTreshold.IsValidIndex(MaxIndex);
			// Return TRANSFORM
			ChoosedTransform = PathTransformWithTreshold[MaxIndex];
			PathLenght = PathsLenghtTreshold[MaxIndex];
		}
		else
		{
			Succesful = false;

			int MaxIndex = 0; float MaxValue = 1.0;
			KML::MaxOfFloatArray(PathVisiblityWeight, MaxIndex, MaxValue);

			ChoosedTransform = FinalPossiblePoints[MaxIndex];
			PathLenght = 0.0;
		}

	}

}




void UAdvancedAI_TasksAndFunctions::TryFindHindingPositionSyncAdvanced(bool& Succesful, FTransform& ChoosedTransform, float& PathLenght, FVector TracingOrigin, FVector TryReachPosition, TArray<ACharacter*> Enemies, UNavQuery_HidingLocSearchParams* QueryParamsData,
	TSubclassOf<UHidingLocSearch_EnemyProperties> CustomOverrideData, TMap<FName, float> CoverPointsConstWeight, float StopWhenOneOfPathHasGoodWeight, FVector WhenPathCheckWeight, float TryIgnorePathWithLinks, int CustomDebugMode)
{
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 0: Main Variables
	TArray<ACharacter*> EnemysInRadius = Enemies;
	TArray<FHitResult> DetectedPoints;
	FVector InstanceLoc = FVector(0, 0, 0);
	FRotator InstanceRot = FRotator(0, 0, 0);
	TArray<float> PointsWeight;
	TArray<FTransform> FirstPossiblePoints;
	TArray<FTransform> FinalPossiblePoints;

	Succesful = false;

	if (!QueryParamsData) return;
	if (!RefChar) { UE_LOG(LogTemp, Error, TEXT("TryFindHindingPositionSync: References character is NULL")); return; }

	// Tworzymy instancję klasy przekazanej jako typ
	UHidingLocSearch_EnemyProperties* OverrideInstance = NewObject<UHidingLocSearch_EnemyProperties>(this, CustomOverrideData);

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 1: Find Enemy Characters in Radius
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	TArray<AActor*> ToIgnore;
	ToIgnore.Add(RefChar);


	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 2: Find CoverPoints

	EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
	if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }
	
	ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(QueryParamsData->PointsFindingChannel); // Tylko Testowanie QueryParamsData->PointsFindingChannel

	KSL::SphereTraceMulti(
		RefChar,
		TracingOrigin,
		TracingOrigin + (RefChar->GetActorForwardVector() * 20.0),
		QueryParamsData->FindingPointsRadius,
		TraceType,
		false,
		ToIgnore,
		TraceDebugType,
		DetectedPoints,
		true,
		FLinearColor::Black,
		FLinearColor::Blue,
		DebugDuration
	);

	bool CurrentPointIsValid = false;

	for (FHitResult CurrentPoint : DetectedPoints)
	{
		int Pindex = 0; Pindex++;
		CurrentPointIsValid = false;
		
		UInstancedStaticMeshComponent* InstanceClass = Cast<UInstancedStaticMeshComponent>(CurrentPoint.GetComponent());
		if (InstanceClass)
		{
			FTransform InstanceTransform = FTransform::Identity;
			CurrentPointIsValid = InstanceClass->GetInstanceTransform(CurrentPoint.Item, InstanceTransform, true);

			InstanceLoc = InstanceTransform.GetLocation();
			InstanceRot = InstanceTransform.Rotator();
		}

		if (CurrentPointIsValid == false) { break; } //If Current Point is not valid then breake loop

		
//DEBUG SECTION
#if WITH_EDITOR //▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
		if (HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::InSightRangeFilter && bDrawDebugPointInfo)																					//   ▒
		{																																																//   ▒
			for (ACharacter* CurrentChar : EnemysInRadius)																																				//   ▒
			{																																															//   ▒
				FVector SightOrigin; FRotator SightRotation; float SightAngle; float SightRadius; float SightHeight;																					//   ▒
				GetControllerSightProperties(SightOrigin, SightRotation, SightAngle, SightRadius, SightHeight, CurrentChar, OverrideInstance, 0);														//   ▒
				CNL::DrawDebugPartCylinder(GetWorld(), SightRotation, SightOrigin, SightAngle, SightRadius, SightHeight, FColor::Red, DebugDuration, 0.6, 8);											//   ▒
			}																																															//   ▒
		}																																																//   ▒
#endif // WITH_EDITOR ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
		
//Step 3: Check Points 
		if (CheckPointIsNotInVisibilityRange(EnemysInRadius, InstanceLoc, InstanceRot, QueryParamsData->MinValidPointDistToEnemy, QueryParamsData->WallCheckTraceRadius, OverrideInstance, false) == true)
		{
			UNavigationPath* CurrentPath = TryFindPathToLocationSync(RefChar->GetActorLocation() - FVector(0, 0, CapHeight * 0.75), InstanceLoc);
			if (CurrentPath)
			{
				float CurrentWeight = 0.0;
				float WeightNotAccumulated = 0.0; 
				bool bSkipPoint = false;

				//▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄   Distance To Point Weight ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
				FVector2D RangeOut = FVector2D(0.0, 1.0);
				if (QueryParamsData->PathLenghtToPointWeight.bInverseRange) RangeOut = FVector2D(1.0, 0.0);
				CurrentWeight = MakeDistributionWeightCorrected(QueryParamsData->PathLenghtToPointWeight, KML::MapRangeUnclamped(CurrentPath->GetPathLength(), 0.0, QueryParamsData->FindingPointsRadius, RangeOut.X, RangeOut.Y), 
					0.0, 1.0, true, CoverPointFinderWeightDebugMode::DistanceToPoint, InstanceLoc);

				if (ApplyReductionFilterOnWeight(QueryParamsData->PathLenghtToPointWeight, CurrentWeight)) { CurrentWeight = -1.0; bSkipPoint = true; }

				
				//▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄   Distance To Nearest Enemy ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
				if (QueryParamsData->DistanceToNearestEnemyWeight.Weight > 0.0 && !bSkipPoint)
				{
					RangeOut = FVector2D(0.0, 1.0);
					if (QueryParamsData->DistanceToNearestEnemyWeight.bInverseRange) RangeOut = FVector2D(1.0, 0.0);
					WeightNotAccumulated = MakeDistributionWeightCorrected(QueryParamsData->DistanceToNearestEnemyWeight, KML::MapRangeUnclamped(DistanceToNearsetEnemy(EnemysInRadius, InstanceLoc), 10.0, QueryParamsData->FindingPointsRadius,
						RangeOut.X, RangeOut.Y), 0.0, 1.0, true, CoverPointFinderWeightDebugMode::DistanceToEnemy, InstanceLoc);
					CurrentWeight = CurrentWeight + WeightNotAccumulated;

					if (ApplyReductionFilterOnWeight(QueryParamsData->DistanceToNearestEnemyWeight, WeightNotAccumulated)) { CurrentWeight = -1.0; bSkipPoint = true; }
				}


				// ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄   WALL HIT WEIGHT ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
				if (QueryParamsData->WallHitWeight.Weight > 0.0 && !bSkipPoint)
				{
					FVector TotalAngleDirection = FVector::ZeroVector;
					RangeOut = FVector2D(0.0, 1.0);
					if (QueryParamsData->WallHitWeight.bInverseRange) RangeOut = FVector2D(1.0, 0.0);
					WeightNotAccumulated = MakeDistributionWeightCorrected(QueryParamsData->WallHitWeight, GetWallHitWeight(EnemysInRadius, InstanceLoc, 2, true), 0.0, 1.0, true, CoverPointFinderWeightDebugMode::SimpleWallHit, InstanceLoc);
					CurrentWeight = CurrentWeight + WeightNotAccumulated;

					if (ApplyReductionFilterOnWeight(QueryParamsData->WallHitWeight, WeightNotAccumulated)) { CurrentWeight = -1.0; bSkipPoint = true; }
				}

				//▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄   Try Reach Orgin Weight ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
				RangeOut = FVector2D(0.0, 1.0);
				if (QueryParamsData->ReachOriginWeight.bInverseRange) RangeOut = FVector2D(1.0, 0.0);
				WeightNotAccumulated = MakeDistributionWeightCorrected(QueryParamsData->ReachOriginWeight, KML::MapRangeClamped(KML::Vector_Distance(InstanceLoc, TryReachPosition), 0.0, KML::Vector_Distance(TracingOrigin, TryReachPosition),
					RangeOut.X, RangeOut.Y), 0.0, 1.0, true, CoverPointFinderWeightDebugMode::ReachOrigin, InstanceLoc);
				CurrentWeight = CurrentWeight + WeightNotAccumulated;
				
				if (ApplyReductionFilterOnWeight(QueryParamsData->ReachOriginWeight, WeightNotAccumulated)) { CurrentWeight = -1.0; bSkipPoint = true; }


				//▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄   Angle Weight ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
				if (QueryParamsData->PointAngleWeight.Weight > 0.0 && !bSkipPoint)
				{
					FVector TotalAngleDirection = FVector::ZeroVector;
					RangeOut = FVector2D(0.0, 1.0);
					if (QueryParamsData->PointAngleWeight.bInverseRange) RangeOut = FVector2D(1.0, 0.0);
					WeightNotAccumulated = MakeDistributionWeightCorrected(QueryParamsData->PointAngleWeight,
						CalculateAngleWeight(TotalAngleDirection, EnemysInRadius, InstanceRot, TracingOrigin, QueryParamsData->ApplyAbsToAngleCalculationMap, InstanceLoc, QueryParamsData->BaseDistanceCalcOnCurrentPointAlpha,
							QueryParamsData->SlerpAngleWhenPointIsInNegativePlane), 0.0, 1.0, true, CoverPointFinderWeightDebugMode::PointAngle, InstanceLoc);
					CurrentWeight = CurrentWeight + WeightNotAccumulated;

					if (ApplyReductionFilterOnWeight(QueryParamsData->PointAngleWeight, WeightNotAccumulated)) { CurrentWeight = -1.0; bSkipPoint = true; }
				}

				if (!bSkipPoint)
				{
					PointsWeight.Add(CurrentWeight);
					FirstPossiblePoints.Add(FTransform(InstanceRot, InstanceLoc, FVector(1, 1, 1)));
				}
			}
		}
	}


#if WITH_EDITOR //▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
	if ((HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointWeightOnly || HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointAndPathWeight) && bDrawDebugPointInfo)	//  ▒ 
	{																																																		//  ▒
		if (HidePosFinderPointWeightDebugMode == CoverPointFinderWeightDebugMode::Total)																													//  ▒
		{																																																	//  ▒
			int MaxI = 0; float MaxW = 0; float MinW = 0;																																					//  ▒
			KML::MaxOfFloatArray(PointsWeight, MaxI, MaxW);																																					//  ▒
			KML::MinOfFloatArray(PointsWeight, MaxI, MinW);																																					//  ▒								
			for (int CI = 0; CI < PointsWeight.Num(); CI++)																																					//  ▒
			{																																																//  ▒
				FVector2D RangeDebug = FVector2D(0.0, 1.0); //if (QueryParamsData->UseAsMaximalizeSearcher) RangeDebug = FVector2D(1.0, 0.0);																//  ▒
				const float NormalizedWeight = KML::MapRangeClamped(PointsWeight[CI], MinW, MaxW, RangeDebug.X, RangeDebug.Y);																				//  ▒
				FLinearColor C = KML::LinearColorLerp(FLinearColor(0.0, 0.0, 0.5, 1.0), FLinearColor(0.0, 1, 0.2, 1.0), NormalizedWeight);																	//  ▒
				DEBUGDRAW_SPHERE(GetWorld(), FirstPossiblePoints[CI].GetLocation(), KML::Lerp(6,15, NormalizedWeight), 6, C.ToFColor(true), false, DebugDuration, 0, 2.5);									//  ▒
				if (NormalizedWeight > 0.5 && HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointWeightOnly)																				//  ▒
				{																																															//  ▒
					DEBUGDRAW_STRING(GetWorld(), FirstPossiblePoints[CI].GetLocation(), FString::SanitizeFloat(PointsWeight[CI]), nullptr, C.ToFColor(true), DebugDuration, true, 0.9);						//  ▒
				}																																															//  ▒
			}																																																//  ▒
		}																																																	//  ▒
	}																																																		//  ▒
#endif // WITH_EDITOR ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒



	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 4: Check Points
	if (PointsWeight.Num() > 0)
	{
		TArray<float> PathVisiblityWeight;
		PathVisiblityWeight.SetNum(FMath::Clamp<int>(QueryParamsData->MaxPotencialPathsCreation, 1, PointsWeight.Num()), EAllowShrinking::No);

		TArray<float> PathsLenght;

		int CurrentMinIndex = 0;
		float CurrentMinValue = 0.0;
		FTransform CurrentTansform = FTransform::Identity;

		if (PathVisiblityWeight.Num() == 0) { Succesful = false; return; } // ZMIANA 2025 dla AGLS v1.6

		for (int i = 0; i < PathVisiblityWeight.Num(); i++)
		{

			if (QueryParamsData->UseAsMaximalizeSearcher)
			{
				KML::MaxOfFloatArray(PointsWeight, CurrentMinIndex, CurrentMinValue); // SEARCHER MODE ◀◀◀◀◀  MAX
				CurrentTansform = FirstPossiblePoints[CurrentMinIndex];
			}
			else
			{
				KML::MinOfFloatArray(PointsWeight, CurrentMinIndex, CurrentMinValue); // SEARCHER MODE ◀◀◀◀◀  MIN
				CurrentTansform = FirstPossiblePoints[CurrentMinIndex];
			}
			
			PathVisiblityWeight[i] = PointsWeight[CurrentMinIndex]; // WAŻNE !!!!!!

			UNavigationPath* CurrentPath2 = TryFindPathToLocationSync(RefChar->GetActorLocation() - FVector(0, 0, CapHeight * 0.75), CurrentTansform.GetLocation());
			if (CurrentPath2)
			{
				TArray<FVector> PathPoints = CurrentPath2->PathPoints;
				SubdividePath(PathPoints, PathPoints, QueryParamsData->ConstPathSubdividor, QueryParamsData->UseDynamicSubdividion, QueryParamsData->DynamicSubSegmentLenght);
				PathsLenght.Add(CurrentPath2->GetPathLength());

				int ActorVisibleCount = 0;
				float CalculatedWeight = 0.0;
				const float PrevWeight = PathVisiblityWeight[i];

				for (int ii = 1; ii < PathPoints.Num(); ii++)
				{
					if (PathPoints.IsValidIndex(ii))
					{
						CalculatedWeight = MakePointVisiblityWeight(
							EnemysInRadius,
							PathPoints[ii] + FVector(0, 0, QueryParamsData->CoverPointHeightOffset),
							FRotator(0, 0, 0),
							QueryParamsData->MinValidPointDistToEnemy,
							QueryParamsData->WallCheckTraceRadius,
							QueryParamsData->NormalizePathWeights,
							OverrideInstance,
							false,
							WhenPathCheckWeight,
							CustomDebugMode
						);

						PathVisiblityWeight[i] = PathVisiblityWeight[i] + CalculatedWeight; //!!!
						ActorVisibleCount++;

						//GEngine->AddOnScreenDebugMessage(-1, 0.5, FColor::White, FString::SanitizeFloat(PathVisiblityWeight[i]));
					}
				}

#if WITH_EDITOR //▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
				if (HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointAndPathWeight && bDrawDebugPointInfo)																				//  ▒
				{																																															//  ▒
					DrawDebugPath(PathPoints, PathVisiblityWeight[i]);																																		//  ▒
					if (abs(PrevWeight - PathVisiblityWeight[i]) > 0.05)																																	//  ▒
					{																																														//  ▒
						DEBUGDRAW_SPHERE(GetWorld(), CurrentTansform.GetLocation(), 18.0, 6, FColor(180, 0, 0, 20), false, DebugDuration, 2, 5);															//  ▒
					}																																														//  ▒
				}																																															//  ▒
#endif // WITH_EDITOR ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒

				if (PathVisiblityWeight.IsValidIndex(i)) //Set Array element
				{
					//PathVisiblityWeight[i] = KML::SafeDivide(PathVisiblityWeight[i], (float)ActorVisibleCount) - (i * 0.01);
					FinalPossiblePoints.Add(CurrentTansform);

					PointsWeight.RemoveAt(CurrentMinIndex);
					FirstPossiblePoints.RemoveAt(CurrentMinIndex);

				}

				if (StopWhenOneOfPathHasGoodWeight > 0.0)
				{
					if (PathVisiblityWeight[i] > StopWhenOneOfPathHasGoodWeight)
					{
						Succesful = true;
						ChoosedTransform = CurrentTansform;
						PathLenght = CurrentPath2->GetPathLength();
						return;
					}
				}

			}
		}

		// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 5: RETURN
		TArray<float> PathWeightsWithTreshold;
		TArray<FTransform> PathTransformWithTreshold;
		TArray<float> PathsLenghtTreshold;

		for (int iii = 0; iii < PathVisiblityWeight.Num(); iii++)
		{
			if (PathVisiblityWeight[iii] > QueryParamsData->AsCorrectPathWeightMin)
			{
				PathWeightsWithTreshold.Add(PathVisiblityWeight[iii]);
				PathTransformWithTreshold.Add(FinalPossiblePoints[iii]);
				PathsLenghtTreshold.Add(PathsLenght[iii]);
			}
		}

		if (PathWeightsWithTreshold.Num() > 0 && PathTransformWithTreshold.Num() > 0)
		{
			int MaxIndex = 0; float MaxValue = 1.0;
			KML::MaxOfFloatArray(PathWeightsWithTreshold, MaxIndex, MaxValue);

			Succesful = PathTransformWithTreshold.IsValidIndex(MaxIndex);
			// Return TRANSFORM
			ChoosedTransform = PathTransformWithTreshold[MaxIndex];
			PathLenght = PathsLenghtTreshold[MaxIndex];
		}
		else
		{
			Succesful = false;

			int MaxIndex = 0; float MaxValue = 1.0;
			KML::MaxOfFloatArray(PathVisiblityWeight, MaxIndex, MaxValue);

			ChoosedTransform = FinalPossiblePoints[MaxIndex];
			PathLenght = 0.0;
		}

	}
}




bool UAdvancedAI_TasksAndFunctions::DetectedByFakeSightPerception(FVector TracingOrigin, UNavQuery_HidingLocSearchParams* QueryParamsData, TSubclassOf<UHidingLocSearch_EnemyProperties> CustomOverrideData)
{

	// Tworzymy instancję klasy przekazanej jako typ
	UHidingLocSearch_EnemyProperties* OverrideInstance = NewObject<UHidingLocSearch_EnemyProperties>(this, CustomOverrideData);

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Step 1: Find Enemy Characters in Radius
	TArray<ACharacter*> EnemysInRadius;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TSubclassOf<AActor> ActorClassFilter = ACharacter::StaticClass();

	TArray<AActor*> OverlappedActors;

	TArray<AActor*> ToIgnore;
	ToIgnore.Add(RefChar);
	// Perform Searching
	KSL::SphereOverlapActors(RefChar, TracingOrigin, QueryParamsData->EnemySearchRadius, ObjectTypes, ActorClassFilter, ToIgnore, OverlappedActors);

	for (AActor* CurrentAct : OverlappedActors)
	{
		ACharacter* CurrentChar = Cast<ACharacter>(CurrentAct);
		if (CurrentChar)
		{
			//Potrzebna aktualizacja w celu filtracji
			if (QueryParamsData->AlwaysIgnorePlayerAsEnemy)
			{
				if (!CurrentChar->IsPlayerControlled())
				{
					EnemysInRadius.Add(CurrentChar);
				}
			}
			else
			{
				EnemysInRadius.Add(CurrentChar);
			}
		}
	}

	if (EnemysInRadius.Num() > 0)
	{
		for (ACharacter* CurrentChar : EnemysInRadius)
		{
			FVector SightOrigin; FRotator SightRotation; float SightAngle; float SightRadius; float SightHeight;
			GetControllerSightProperties(SightOrigin, SightRotation, SightAngle, SightRadius, SightHeight, CurrentChar, OverrideInstance, 0);

			bool PointInside = UClimbingNavigationBPLibrary::IsPointInsideCylinderSegment(SightRotation, SightOrigin, CurrentChar, RefChar->GetActorLocation(), SightAngle, SightRadius, SightHeight, -1.0);
			if (PointInside)
			{
				const FVector TStart = SightOrigin + FVector(0, 0, CurrentChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

				TArray<AActor*> ActorsToIgnore;
				ActorsToIgnore.Add(RefChar); ActorsToIgnore.Add(CurrentChar);

				EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
				if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }

				FHitResult WallResult;
				bool WallHitValid = KSL::SphereTraceSingle(RefChar, TStart, RefChar->GetActorLocation() + FVector(0, 0, CapHeight * 0.2), QueryParamsData->WallCheckTraceRadius, ETraceTypeQuery::TraceTypeQuery1, false,
					ActorsToIgnore, TraceDebugType, WallResult, true, FColor::Black, FColor::Orange, 0.25);

				if (WallHitValid == false)
				{
					return true;
				}
			}
		}
		return false;
	}
	return false;
}


bool UAdvancedAI_TasksAndFunctions::CheckPointIsNotInVisibilityRange(TArray<ACharacter*> EnemysArray, FVector PointLocation, FRotator PointRotation, float MinValidDistance, float TraceRadius, UHidingLocSearch_EnemyProperties* CustomOverrideData, bool bDrawDebug)
{
	for (ACharacter* CurrentChar : EnemysArray)
	{
		if (!CurrentChar) { break; }

		if (KML::Vector_Distance2D(CurrentChar->GetActorLocation(), PointLocation) > MinValidDistance)
		{
			FVector SightOrigin; FRotator SightRotation; float SightAngle; float SightRadius; float SightHeight;
			GetControllerSightProperties(SightOrigin, SightRotation, SightAngle, SightRadius, SightHeight, CurrentChar, CustomOverrideData, 0);

			float DebugTime = -1.0;
			if (bDrawDebugSightRange) { DebugTime = DebugDuration; }

			bool PointInSightRange = UClimbingNavigationBPLibrary::IsPointInsideCylinderSegment(SightRotation, SightOrigin, CurrentChar, PointLocation, SightAngle, SightRadius, SightHeight, -1.0);

			if (PointInSightRange == true)
			{
				const FVector TStart = SightOrigin + FVector(0, 0, CurrentChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

				TArray<AActor*> ActorsToIgnore;
				ActorsToIgnore.Add(RefChar); ActorsToIgnore.Add(CurrentChar);

				EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
				if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }

				FHitResult WallResult;
				bool WallHitValid = KSL::SphereTraceSingle(RefChar, TStart, PointLocation + FVector(0, 0, CapHeight * 0.75), TraceRadius, ETraceTypeQuery::TraceTypeQuery1, false, 
					ActorsToIgnore, TraceDebugType, WallResult, true, FColor::Black, FColor::Orange, DebugDuration);

				if (WallHitValid == false)
				{

#if WITH_EDITOR
					if (HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::InSightRangeFilter && bDrawDebugPointInfo)
					{ DEBUGDRAW_SPHERE(GetWorld(), PointLocation, 16.0, 4, FColor::Black, false, DebugDuration, 0, 2.0); }
#endif

					return false;
				}
			}
		}
		else
		{
#if WITH_EDITOR
			if (HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::InSightRangeFilter && bDrawDebugPointInfo)
			{ DEBUGDRAW_SPHERE(GetWorld(), PointLocation, 16.0, 4, FColor(250,180,180,150), false, DebugDuration, 0, 2.0); }
#endif // WITH_EDITOR

			return false;
		}
	}

#if WITH_EDITOR
	if (HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::InSightRangeFilter && bDrawDebugPointInfo)
	{ DEBUGDRAW_SPHERE(GetWorld(), PointLocation, 16.0, 4, FColor::Blue, false, DebugDuration, 0, 2.0); }
#endif // WITH_EDITOR

	return true;
}


float UAdvancedAI_TasksAndFunctions::DistanceToNearsetEnemy(TArray<ACharacter*> EnemysArray, FVector TargetLocation)
{
	float MinDistance = 99999.0;
	
	for (ACharacter* Char : EnemysArray)
	{
		const float CurrentDist = KML::Vector_Distance(Char->GetActorLocation(), TargetLocation);
		if (CurrentDist < MinDistance)
		{
			MinDistance = CurrentDist;
		}
	}
	if (EnemysArray.Num() > 0)
	{
		return MinDistance;
	}
	else
	{
		return 10.0;
	}
}


float UAdvancedAI_TasksAndFunctions::CalculateAngleWeight(FVector& TotalDistances, TArray<ACharacter*> EnemysArray, FRotator PointRotation, FVector DistanceMapTarget, bool bUseAbs, FVector AddtivePointLoc, float CurrentPointAlpha, float SlerpWhenOn)
{
	if (EnemysArray.Num() == 0) { return 1.0; }

	TArray<float> Distances;
	const FVector LerpedPositions = KML::VLerp(DistanceMapTarget, AddtivePointLoc, CurrentPointAlpha);

	//Step 1: Make All Distasnces
	for (ACharacter* Char : EnemysArray)
	{
		Distances.Add(KML::Vector_Distance(Char->GetActorLocation(), LerpedPositions));
	}

	int MinIndex = 0; int MaxIndex = 0; float MinValue = 0.0; float MaxValue = 1.0;

	//KML::MinOfFloatArray(Distances, MinIndex, MinValue);
	KML::MaxOfFloatArray(Distances, MaxIndex, MaxValue);
	MinValue = 10;

	FVector SumOfDirections = FVector(0, 0, 0);

	for (int i = 0; i < EnemysArray.Num(); i++)
	{
		const FVector ControlDirectionXY = KML::GetForwardVector(FRotator(0, EnemysArray[i]->GetControlRotation().Yaw, 0));
		const float Scale = KML::MapRangeClamped(KML::Vector_Distance(EnemysArray[i]->GetActorLocation(), LerpedPositions), MinValue, MaxValue, 1.0, 0.1);

		SumOfDirections = SumOfDirections + (ControlDirectionXY * Scale);
	}

	SumOfDirections.Normalize();

	if (SlerpWhenOn > 0)
	{
		FTransform AsRelative = KML::MakeRelativeTransform(FTransform(PointRotation, AddtivePointLoc), FTransform(KML::MakeRotFromX(SumOfDirections), EnemysArray[MinIndex]->GetActorLocation()));
		FTransform AsRelative2 = KML::MakeRelativeTransform(FTransform(PointRotation, DistanceMapTarget), FTransform(EnemysArray[MinIndex]->GetControlRotation(), EnemysArray[MinIndex]->GetActorLocation()));
		if (AsRelative2.GetLocation().X > 0)
		{
			if (AsRelative.GetLocation().X > 0)
			{
				SumOfDirections = KML::Vector_SlerpNormals(SumOfDirections, SumOfDirections * -1, SlerpWhenOn * KML::MapRangeClamped(AsRelative.GetLocation().X, 0.0, 100, 0.1, 1.0));
			}
		}
		else
		{
			if (AsRelative.GetLocation().X > 0)
			{
				SumOfDirections = KML::Vector_SlerpNormals(SumOfDirections, SumOfDirections * -1, SlerpWhenOn * KML::MapRangeClamped(AsRelative.GetLocation().X, 0.0, 100, 0.1, 1.0));
			}
		}
	}


	if ((HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointWeightOnly && HidePosFinderPointWeightDebugMode == CoverPointFinderWeightDebugMode::PointAngle) && bDrawDebugPointInfo)
	{
		DrawDebugLine(GetWorld(), AddtivePointLoc, AddtivePointLoc + (SumOfDirections * 30), FColor::Yellow, false, DebugDuration, 0, 1);
		DrawDebugLine(GetWorld(), AddtivePointLoc - FVector(0,0,1), AddtivePointLoc - FVector(0, 0, 1) + (KML::GetForwardVector(PointRotation) * 30), FColor::Cyan, false, DebugDuration, 0, 0.8);
	}


	if (bUseAbs)
	{
		return KML::MapRangeClamped(abs(1.0 - KML::Dot_VectorVector(SumOfDirections, KML::GetForwardVector(PointRotation))), 0.1, 0.9, 1.0, 0.0);
	}
	else
	{
		return KML::MapRangeClamped(1.0 - KML::Dot_VectorVector(SumOfDirections, KML::GetForwardVector(PointRotation)), 0.1, 0.9, 1.0, 0.0);
	}

}


bool UAdvancedAI_TasksAndFunctions::SubdividePath(TArray<FVector>& NewPoints, TArray<FVector> InPoints, int ConstSubdivider, bool UseDynamic, float SubSegmentLength)
{
	if (InPoints.Num() < 2 || ConstSubdivider < 1 || SubSegmentLength <= 0.0f)
	{
		return false;
	}

	NewPoints.Empty();
	NewPoints.Add(InPoints[0]);

	// Iteracja przez pary punktów
	for (int i = 0; i < InPoints.Num() - 1; ++i)
	{
		FVector StartPoint = InPoints[i];
		FVector EndPoint = InPoints[i + 1];

		if (UseDynamic)
		{
			// Oblicz dystans pomiędzy dwoma punktami
			float Distance = FVector::Dist(StartPoint, EndPoint);

			// Oblicz liczbę segmentów do dodania na podstawie SubSegmentLength
			int NumSegments = FMath::CeilToInt(Distance / SubSegmentLength);

			// Dodaj dodatkowe punkty między StartPoint a EndPoint
			for (int j = 1; j < NumSegments; ++j)
			{
				float Alpha = static_cast<float>(j) / NumSegments;
				FVector NewPoint = FMath::Lerp(StartPoint, EndPoint, Alpha);
				NewPoints.Add(NewPoint);
			}
		}
		else
		{
			// Dodaj stałą liczbę punktów między StartPoint a EndPoint
			for (int j = 1; j <= ConstSubdivider; ++j)
			{
				float Alpha = static_cast<float>(j) / (ConstSubdivider + 1);
				FVector NewPoint = FMath::Lerp(StartPoint, EndPoint, Alpha);
				NewPoints.Add(NewPoint);
			}
		}

		// Dodaj końcowy punkt odcinka
		NewPoints.Add(EndPoint);
	}
	return true;
}


float UAdvancedAI_TasksAndFunctions::MakePointVisiblityWeight(TArray<ACharacter*> EnemysArray, FVector PointLocation, FRotator PointRotation, float MinValidDistance, float TraceRadius, bool bNormalize, 
	UHidingLocSearch_EnemyProperties* CustomOverrideData, bool bDrawDebug, FVector WeightIncrementValues, int CustomDebugMode)
{
	float CurrentWeight = 0.0;

	for (ACharacter* CurrentChar : EnemysArray)
	{
		if (KML::Vector_Distance2D(CurrentChar->GetActorLocation(), PointLocation) > MinValidDistance)
		{
			FVector SightOrigin; FRotator SightRotation; float SightAngle; float SightRadius; float SightHeight;
			GetControllerSightProperties(SightOrigin, SightRotation, SightAngle, SightRadius, SightHeight, CurrentChar, CustomOverrideData, 1); //Get FAKE Sight Properties

			bool PointInSightRange = UClimbingNavigationBPLibrary::IsPointInsideCylinderSegment(SightRotation, SightOrigin, CurrentChar, PointLocation, SightAngle, SightRadius, SightHeight, -1.0);

			if (PointInSightRange == true)
			{
				const FVector TStart = SightOrigin + FVector(0, 0, CapHeight);
				TArray<AActor*> ActorsToIgnore;
				ActorsToIgnore.Add(RefChar); ActorsToIgnore.Add(CurrentChar);
				EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
				if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }
				FHitResult WallResult;

				bool WallHitValid = KSL::SphereTraceSingle(RefChar, TStart, PointLocation + FVector(0, 0, CapHeight * 0.75), TraceRadius, ETraceTypeQuery::TraceTypeQuery1, false,
					ActorsToIgnore, TraceDebugType, WallResult, true, FColor::Black, FColor::Orange, DebugDuration);

				if (WallHitValid == true)
				{
					CurrentWeight = CurrentWeight + KML::MapRangeClamped(KML::Vector_Distance(WallResult.ImpactPoint, WallResult.TraceStart), 50.0, KML::Vector_Distance(WallResult.TraceStart, WallResult.TraceEnd), WeightIncrementValues.Y, 0.0);
				}
				else
				{
					CurrentWeight = CurrentWeight - WeightIncrementValues.X;
				}
			}
			else
			{
				CurrentWeight = CurrentWeight + WeightIncrementValues.Y;
			}
		}
		else
		{
			CurrentWeight = CurrentWeight - WeightIncrementValues.Z;
		}
	}
	if (bNormalize && EnemysArray.Num() > 0)
	{
		return KML::SafeDivide(CurrentWeight, EnemysArray.Num() * 1.0);
	}
	else
	{
		return CurrentWeight;
	}

}


bool UAdvancedAI_TasksAndFunctions::GetControllerSightProperties(FVector& SightOrigin, FRotator& SightRotation, float& HalfAngle, float& Radius, float& Height, ACharacter* InCharacter, UHidingLocSearch_EnemyProperties* CustomOverrideData, int IterIndex)
{
	// Default Sight Parameters

	if(CustomOverrideData)
	{
		ACharacter* SearcherActor = Cast<ACharacter>(GetOwner());
		if (InCharacter)
		{
			CustomOverrideData->GetPredictedEnemyTransform(SightOrigin, SightRotation, InCharacter, SearcherActor, IterIndex);
			CustomOverrideData->GetCurrentEnemySightParams(HalfAngle, Radius, Height, InCharacter, SearcherActor);

			//DrawDebugPoint(GetWorld(), SightOrigin, 15, FColor::Red, false, 0.8, 0);

			if (HalfAngle < 0.0)
			{
				HalfAngle = DefaultSightHalfAngle;
				Radius = DefaultSightRadius;
				Height = DefaultSightHeight;
			}

			return true;
		}
	}

	if (InCharacter)
	{
		SightOrigin = InCharacter->GetActorLocation() - FVector(0,0, InCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		SightRotation = InCharacter->GetActorRotation();
		HalfAngle = DefaultSightHalfAngle;
		Radius = DefaultSightRadius;
		Height = DefaultSightHeight;
	}

	return false;
}


float UAdvancedAI_TasksAndFunctions::GetWallHitWeight(TArray<ACharacter*> EnemysArray, FVector PointLocation, float TraceRadius, bool bNormalize)
{
	if (EnemysArray.Num() == 0) return 1;

	float DesiredWeight = 0.0;

	for (ACharacter* Char : EnemysArray)
	{
		const FVector TStart = PointLocation + FVector(0, 0, CapHeight * 0.8);
		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(RefChar); ActorsToIgnore.Add(Char);
		EDrawDebugTrace::Type TraceDebugType = EDrawDebugTrace::None;
		if (bDrawDebugTraces) { TraceDebugType = EDrawDebugTrace::ForDuration; }
		FHitResult WallResult;

		bool WallHitValid = KSL::SphereTraceSingle(RefChar, TStart, Char->GetActorLocation(), TraceRadius, ETraceTypeQuery::TraceTypeQuery1, false,
			ActorsToIgnore, TraceDebugType, WallResult, true, FColor(120,0,0,255), FColor::Green, DebugDuration);
		if (WallHitValid) DesiredWeight = DesiredWeight + 1.0;

	}
	if (bNormalize)
	{
		return KML::SafeDivide(DesiredWeight, EnemysArray.Num());
	}
	return DesiredWeight;
}

bool UAdvancedAI_TasksAndFunctions::ApplyReductionFilterOnWeight(FWeightFunction WeightParams, float InWeight)
{
	if (WeightParams.MinAccetableValueFilter > 0.0 && WeightParams.Weight > 0.0)
	{
		if (InWeight < WeightParams.MinAccetableValueFilter) return true;
	}
	return false;
}


UNavigationPath* UAdvancedAI_TasksAndFunctions::TryFindPathToLocationSync(FVector PathStart, FVector PathEnd)
{
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("Navigation Path Finding - World is null"));
		return nullptr;
	}

	// Pobranie systemu nawigacji
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogTemp, Warning, TEXT("Navigation System is null"));
		return nullptr;
	}

	// Parametry funkcji FindPathToLocationSync
	FPathFindingQuery Query;
	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);

	// Znalezienie ścieżki synchroniczne
	FNavLocation StartLocation(PathStart);
	FNavLocation EndLocation(PathEnd);
	UNavigationPath* PathResult = NavSys->FindPathToLocationSynchronously(NavSys, PathStart, PathEnd, nullptr, DefaultNavFilter);
	return PathResult;
}



// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------




void UAdvancedAI_TasksAndFunctions::TryFindPathToActorIncludingNavClimb(bool& Succesful, FClimbNav_FullPathData& QueryResult, AActor* StartActor, AActor* ActorToReach, AClimbNavigationStorageActor* CurrentClimbNav, bool bStartActorIsClimb, bool bEndActorIsClimb, UNavQuery_FullNavPathFinding* QueryParams)
{
	if (!StartActor || !ActorToReach || !QueryParams) //Obiekty które muszą być poprawne w celu możliwości wykonania dalszych obliczeń
	{
		UE_LOG(LogTemp, Warning, TEXT("TryFindPathToActorIncludingNavClimb(): one of the inputs actors is not valid. Query abort"));
		Succesful = false;
		return; //Nie można uruchomić funkcji
	}

	bool bShowsDebugs = false; // TO TYLKO DO KOSTRUKCJI

	FClimbNav_FullPathData FQR; // <-- Struktura w której będą przechowywane wyliczone dane

	// CASE 01 (Non of actors is in climbing mode)
	//Rozwarzenie przypadku szczególnego - Dwa obiekty zadeklarowały że NIE są w trybie wspinaczki. Jest to najprosztrza sytuacja, z tego względu że w tym przypadku 
	// trzeba przeanalizować tylko zwykłe dane Nawigacyjne (Navigation Mesh). 
	if (bStartActorIsClimb == false && bEndActorIsClimb == false)
	{
		UNavigationPath* GroundedNavPath = TryFindPathToLocationSync(StartActor->GetActorLocation() - FVector(0, 0, 50), ActorToReach->GetActorLocation() - FVector(0, 0, -50));
		if (GroundedNavPath) //Sprawdzenie czy funkcja TryFindPathToLocationSync zwróciła poprawny obiekt
		{
			if (GroundedNavPath->PathPoints.Num() > 1) //Sprawdzenie czy jakakolwiek ścieżka została wyznaczona. Jeżeli punktów było by mniej od 2 wtedy nie ma możliwości utworzenia ścieżki
			{
				FQR.GroundedPathPoints = GroundedNavPath->PathPoints; //Uzupełnienie struktury danymi z zapytania o ścieżkę
				FQR.GroundedPathLenghtSegment = GroundedNavPath->GetPathLength();
				FQR.TotalPathLenght = GroundedNavPath->GetPathLength();
				FQR.TotalPathCost = GroundedNavPath->GetPathCost();
				FQR.LeaveClimbProjection = GroundedNavPath->PathPoints[0];
				FQR.StartClimbProjection = GroundedNavPath->PathPoints[GroundedNavPath->PathPoints.Num() - 1];
				FQR.LeaveLedgePosition = FQR.LeaveClimbProjection;
				FQR.ReachLedgePosition = FQR.StartClimbProjection;
				FQR.GroundedPathValid = true;

				if (bShowsDebugs)
				{
					DrawDebugPath(GroundedNavPath->PathPoints, 0.0);
				}
				QueryResult = FQR;
				Succesful = true;
				return;
			}
			Succesful = false;
			return;

		}
		else
		{
			Succesful = false;
			return;
		}
	}

	// ❱❱❱❱❱❱❱❱❱ CASE 02 (Two Actors is in climbing mode) ❰❰❰❰❰❰❰❰❰❰❰
	if (bStartActorIsClimb == true || bEndActorIsClimb == true)
	{
		//➊ STEP 01 ● finding Navigation Climbing Storage Actor for Reach Actor
		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(StartActor);
		ActorsToIgnore.Add(ActorToReach);

		TArray<FHitResult> MultiTraceResult;

		//Trace wykonany w celu znalezienia aktora ClimbNav dla ActorToReach.
		KSL::SphereTraceMultiByProfile
		(
			ActorToReach, 
			ActorToReach->GetActorLocation(), 
			ActorToReach->GetActorLocation() + FVector(0, 0, 5), 
			QueryParams->FindingNavActorRadius, 
			QueryParams->CollisionProfileName, 
			false, 
			ActorsToIgnore, 
			EDrawDebugTrace::None, 
			MultiTraceResult, 
			true, 
			FColor::Black, 
			FColor::White, 
			1.0
		);

		AClimbNavigationStorageActor* ClimbNavDataActor = nullptr;
		//Przeszukiwania wszystkich wykrytych HitResult w celu znalezienia obiektu AClimbNavigationStorageActor*
		for (FHitResult CHR : MultiTraceResult)
		{
			ClimbNavDataActor = Cast<AClimbNavigationStorageActor>(CHR.GetActor()); 

			if (ClimbNavDataActor) { break; } //Jeżeli obiekt został odnaleziony można przerwać dalsze poszukiwanie
		}
		if (!ClimbNavDataActor) //Wyświetlenie informacji że próba odnalezienia AClimbNavigationStorageActor* zakończyła się niepowodzeniem. Mimo to funkcja będzie kontynuowana
		{
			Succesful = false;
			UE_LOG(LogTemp, Warning, TEXT("TryFindPathToActorIncludingNavClimb(): Function required AClimbNavigationStorageActor* nearby ActorToReach, but searching for this instance is not finished property"));
		}
		
		//Rozwarzenie przypadku szczególnego - Dwa obiekty zadeklarowały że są w trybie wspinaczki. W takim przypadku przwdopodobna jest sytuacja że zarówna StartActor oraz 
		// ActorToReach są w przesrzeni tej samej instancji AClimbNavigationStorageActor. Oznacza to że może istnieć scieżka bezpośrenio łącząca ze sobą te dwa obiekty 
		// używając tylko i wyłącznie trybu wspinaczki
		if (bStartActorIsClimb == true && bEndActorIsClimb == true)
		{
			Succesful = TryBuildOnlyClimbPath(FQR, StartActor, ActorToReach, CurrentClimbNav, ClimbNavDataActor, QueryParams);
			if (Succesful)
			{
				Succesful = true;
				QueryResult = FQR;
				return; //Przypadek szczególny został spełniony - można zakończyć funkcję
			}
		}


		//Zadeklarowanie zmiennych lokalnych w których będą przechowywane punkty wyznaczone przez funkcję ProjectPointToNavigation
		TArray<FVector> ProjectedPointForSelf;
		TArray<FVector> ProjectedPointForTarget;

		//➋ STEP 02 ●● Projection to default navigation mesh
		for (int i = 0; i <= 1; i++)
		{
			bool RequiredNegate = true;
			float SelectedOffset = QueryParams->ForwardOffsets.Y;
			FVector ProjectionSingleResult = FVector(0, 0, 0);
			FVector SelectedExtend = QueryParams->SearchBoxExtendDown;
			if (i > 0) 
			{ 
				SelectedExtend = QueryParams->SearchBoxExtendUp; 
				RequiredNegate = false; 
				SelectedOffset = QueryParams->ForwardOffsets.X;
			}

			bool ProjectionValid = ProjectToNavigationWhenActorClimb  // <------------------------------------------------------------------ For Start Actor
			(
				ProjectionSingleResult, //Zwrocenie wyniku funkcji
				StartActor->GetActorTransform(), //Okreslenie punktu od jakiego bedzie zaczynane szukanie punktu nalezacego do nawigacji
				SelectedExtend, //Maksymaly zasieg szukania
				QueryParams->MaxIterationsPerSide, //liczba iteracji w celu poszukiwania w kiernkach lewo/ prawo
				RequiredNegate, //odwrocenie osi pionowej
				SelectedOffset, 
				bShowsDebugs, //wlaczenie debugowania
				DebugDuration
			);

			if (ProjectionValid)
			{
				ProjectedPointForSelf.Add(ProjectionSingleResult);
			}
		}

		if (bEndActorIsClimb)
		{
			for (int i = 0; i <= 1; i++)
			{
				bool RequiredNegate = true;
				float SelectedOffset = QueryParams->ForwardOffsets.Y;
				FVector ProjectionSingleResult = FVector(0, 0, 0);
				FVector SelectedExtend = QueryParams->SearchBoxExtendDown;
				if (i > 0)
				{
					SelectedExtend = QueryParams->SearchBoxExtendUp;
					RequiredNegate = false;
					SelectedOffset = QueryParams->ForwardOffsets.X;
				}

				bool ProjectionValid = ProjectToNavigationWhenActorClimb  // <------------------------------------------------------------------ For Reach Actor
				(
					ProjectionSingleResult, //Zwrocenie wyniku funkcji
					ActorToReach->GetActorTransform(), //Okreslenie punktu od jakiego bedzie zaczynane szukanie punktu nalezacego do nawigacji
					SelectedExtend, //Maksymaly zasieg szukania
					QueryParams->MaxIterationsPerSide, //liczba iteracji w celu poszukiwania w kiernkach lewo/ prawo
					RequiredNegate, //odwrocenie osi pionowej
					SelectedOffset,
					bShowsDebugs, //wlaczenie debugowania
					DebugDuration
				);

				if (ProjectionValid)
				{
					ProjectedPointForTarget.Add(ProjectionSingleResult);
				}
			}
		}
		else
		{
			UWorld* World = GetWorld();
			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

			if (!NavSys)
			{
				UE_LOG(LogTemp, Warning, TEXT("TryFindPathToActorIncludingNavClimb(): Navigation System is not valid!"));
				Succesful = false;
				return;
			}

			// Znalezienie najbliższego punktu na NavMesh
			FNavLocation ProjectedPointResult;
			FVector ProjectionTest = ActorToReach->GetActorLocation() - FVector(0,0,30);
			bool ProjectionValid = NavSys->ProjectPointToNavigation(ProjectionTest, ProjectedPointResult, FVector(500,500,500));
			if (ProjectionValid)
			{
				ProjectedPointForTarget.Add(ProjectedPointResult.Location);
			}
		}


		//➌ STEP 03 ●●●

		if (ProjectedPointForSelf.Num() > 0 && ProjectedPointForTarget.Num() > 0)
		{
			for (FVector P1 : ProjectedPointForSelf)
			{
				for (FVector P2 : ProjectedPointForTarget)
				{
					FClimbNav_FullPathData c2137;
					bool FullPathValid = TryBuildFullPathIncludeNavClimb(c2137, P1, P2, StartActor, ActorToReach, CurrentClimbNav, ClimbNavDataActor, QueryParams);
					if (FullPathValid)
					{
						Succesful = c2137.GroundedPathValid;
						QueryResult = c2137;
						return;
					}
				}
			}
		}
		Succesful = false;
		return;
	}

}


bool UAdvancedAI_TasksAndFunctions::ProjectToNavigationWhenActorClimb(FVector& ProjectedPoint, FTransform CheckingStart, FVector BoxExtend, int MaxIterPerSide, bool bNegateUpAxis, float ForwardOffset, bool bDrawDebugShapes, float DisplayDuration)
{

	FVector FixedCheckOrigin = CheckingStart.GetLocation() + (KML::GetForwardVector(CheckingStart.Rotator()) * ForwardOffset);

	for (int i = 1; i <= MaxIterPerSide * 2; i++)
	{
		int j = (i - 1) % (MaxIterPerSide + 1);

		float RightAxisScale = 1.0;
		if (i > MaxIterPerSide) { RightAxisScale = -1.0; }

		FVector FixedExtend = KML::Vector_GetAbs(KML::Quat_RotateVector(CheckingStart.GetRotation(), BoxExtend));

		FVector ProjectionTest = FixedCheckOrigin + FVector(0, 0, BoxExtend.Z * KML::SelectFloat(-1.0, 1.0, bNegateUpAxis));
		ProjectionTest = ProjectionTest + (KML::GetRightVector(CheckingStart.Rotator()) * RightAxisScale * (j * FixedExtend.Y));

		UWorld* World = GetWorld();
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

		if (!NavSys)
		{
			UE_LOG(LogTemp, Warning, TEXT("Navigation System is not valid!"));
			return false;
		}

		// Znalezienie najbliższego punktu na NavMesh
		FNavLocation ProjectedPointResult;
		bool ProjectionValid = NavSys->ProjectPointToNavigation(ProjectionTest, ProjectedPointResult, FixedExtend);

		if (bDrawDebugShapes)
		{
			DrawDebugBox(GetWorld(), ProjectionTest, FixedExtend, FColor::Blue, false, DisplayDuration, 0, 0.8);

			if (ProjectionValid)
			{
				DrawDebugLine(GetWorld(), ProjectionTest, ProjectedPointResult, FColor::Cyan, false, DisplayDuration, 0, 1.2);
			}
		}

		if (ProjectionValid)
		{
			ProjectedPoint = ProjectedPointResult;
			return true;
		}
	}
	return false;
}


bool UAdvancedAI_TasksAndFunctions::TryBuildFullPathIncludeNavClimb(FClimbNav_FullPathData& FunctionResult, FVector CPPS, FVector CPPP, 
	AActor* StartActor, AActor* ActorToReach, AClimbNavigationStorageActor* CurrentClimbNav, AClimbNavigationStorageActor* NextClimbNav, UNavQuery_FullNavPathFinding* QueryParams)
{
	if (!QueryParams || !StartActor) { return false; }

	FClimbNav_FullPathData ResultAsStruct;

	//Substep 01
	UNavigationPath* GroundedPath = TryFindPathToLocationSync(CPPS, CPPP);

	//DrawDebugSphere(GetWorld(), CPPS, 25, 12, FColor::Red, false, 0, 0, 2.0);
	//DrawDebugSphere(GetWorld(), CPPP, 25, 12, FColor::Blue, false, 0, 0, 2.0);

	if (!GroundedPath) { return false; }
	if (GroundedPath->PathPoints.Num() < 2) { return false; }

	//Trzeba sie upewnic że ścieżka jest w miarę kompletna (to znaczy że FinalPathPoint jest w sensownej pozycji od punktu CPPP
	if (KML::Vector_Distance(GroundedPath->PathPoints[GroundedPath->PathPoints.Num() - 1], CPPP) > 150)
	{
		return false;
	}

	bool CurrentClimbPathCorrect = false;
	bool NextClimbPathCorrect = false;

	//Substep 02
	bool CClimbPathValid = false; float CClimbPathLenght = 0.0;
	TArray<FClimbNav_SingleClimbPoint> CurrentClimbPathPoints;
	if (CurrentClimbNav)
	{
		CNL::TryFindPathForClimbNav(GetWorld(), CClimbPathValid, CClimbPathLenght, CurrentClimbPathPoints, StartActor->GetActorLocation(), GroundedPath->PathPoints[0], CurrentClimbNav);
	}
	
	//Substep 03
	bool NClimbPathValid = false; float NClimbPathLenght = 0.0;
	TArray<FClimbNav_SingleClimbPoint> NextClimbPathPoints;
	if (NextClimbNav)
	{
		CNL::TryFindPathForClimbNav(GetWorld(), NClimbPathValid, NClimbPathLenght, NextClimbPathPoints, GroundedPath->PathPoints[GroundedPath->PathPoints.Num() - 1], ActorToReach->GetActorLocation(), NextClimbNav);
	}

	//Substep 04
	ResultAsStruct.GroundedPathPoints = GroundedPath->PathPoints;
	ResultAsStruct.GroundedPathLenghtSegment = GroundedPath->GetPathLength();
	ResultAsStruct.TotalPathLenght = GroundedPath->GetPathLength();
	ResultAsStruct.TotalPathCost = GroundedPath->GetPathCost();
	ResultAsStruct.LeaveClimbProjection = CPPS;
	ResultAsStruct.StartClimbProjection = GroundedPath->PathPoints[GroundedPath->PathPoints.Num() - 1];
	ResultAsStruct.GroundedPathValid = true;
	FunctionResult = ResultAsStruct;

	//Substep 05
	if (CClimbPathValid == true && CurrentClimbPathPoints.Num() > 1)
	{
		float ValidDistance = KML::Vector_Distance(CurrentClimbPathPoints[CurrentClimbPathPoints.Num() - 1].ActorTransform.GetLocation(), GroundedPath->PathPoints[0]);
		if (ValidDistance < QueryParams->MaxDistBetweenLedgeToTarget)
		{
			ResultAsStruct.ToLeaveClimbPath = CurrentClimbPathPoints;
			ResultAsStruct.LeaveLedgePosition = CurrentClimbPathPoints[CurrentClimbPathPoints.Num() - 1].ActorTransform.GetLocation();
			ResultAsStruct.CurrentClimbPathValid = true;
			ResultAsStruct.TotalPathLenght = ResultAsStruct.TotalPathLenght + CClimbPathLenght;
		}
	}

	//Substep 06
	if (NClimbPathValid == true && NextClimbPathPoints.Num() > 1)
	{
		// Odległość od ostatniego punktu wspinaczki (LedgePoint), do aktualnej pozycji ActorToReach
		float ValidDistance = KML::Vector_Distance(NextClimbPathPoints[NextClimbPathPoints.Num() - 1].ActorTransform.GetLocation(), ActorToReach->GetActorLocation());
		// Odległość od pierwszego najbliżeszego punktu wspinaczki (LedgePoint), do ostatniego punktu ze ścieżki utworzonej na bazie NavigationMesh
		float ValidDistance2 = KML::Vector_Distance(NextClimbPathPoints[0].ActorTransform.GetLocation(), GroundedPath->PathPoints[GroundedPath->PathPoints.Num() - 1]);

		if (ValidDistance < QueryParams->MaxDistBetweenLedgeToTarget && ValidDistance2 < QueryParams->MaxDistBetweenProjectToLedge)
		{
			ResultAsStruct.ToReachTargetClimbPath = NextClimbPathPoints;
			ResultAsStruct.ReachLedgePosition = NextClimbPathPoints[NextClimbPathPoints.Num() - 1].ActorTransform.GetLocation();
			ResultAsStruct.NextClimbPathValid = true;
			ResultAsStruct.TotalPathLenght = ResultAsStruct.TotalPathLenght + NClimbPathLenght;
		}
	}
	
	FunctionResult = ResultAsStruct;
	return true;
}


bool UAdvancedAI_TasksAndFunctions::TryBuildOnlyClimbPath(FClimbNav_FullPathData& FunctionResult, AActor* StartActor, AActor* ActorToReach, AClimbNavigationStorageActor* CurrentClimbNav, AClimbNavigationStorageActor* NextClimbNav, UNavQuery_FullNavPathFinding* QueryParams)
{
	FClimbNav_FullPathData BuildedPathStruct;

	if (!CurrentClimbNav || !NextClimbNav)
	{
		FunctionResult = BuildedPathStruct;
		return false;
	}

	if (CurrentClimbNav == NextClimbNav) // Sprawdzenie czy zmienne wskazuja na ten sam obiekt
	{
		bool ClimbPathValid = false; float ClimbPathLenght;
		TArray<FClimbNav_SingleClimbPoint> ClimbPathPoints;
		CNL::TryFindPathForClimbNav(GetWorld(), ClimbPathValid, ClimbPathLenght, ClimbPathPoints, StartActor->GetActorLocation() + FVector(0, 0, 40), ActorToReach->GetActorLocation() + FVector(0, 0, 40), CurrentClimbNav);

		if (ClimbPathValid && ClimbPathPoints.Num() > 1)
		{
			const float StartToFirstLedge = KML::Vector_Distance(ClimbPathPoints[0].ActorTransform.GetLocation(), StartActor->GetActorLocation());
			const float EndToEndLedge = KML::Vector_Distance(ClimbPathPoints[ClimbPathPoints.Num() - 1].ActorTransform.GetLocation(), ActorToReach->GetActorLocation());

			if (EndToEndLedge < QueryParams->MaxDistBetweenLedgeToTarget && StartToFirstLedge < QueryParams->MaxDistBetweenLedgeToTarget)
			{
				BuildedPathStruct.LeaveLedgePosition = ClimbPathPoints[0].ActorTransform.GetLocation();
				BuildedPathStruct.ReachLedgePosition = ClimbPathPoints[ClimbPathPoints.Num() - 1].ActorTransform.GetLocation();
				BuildedPathStruct.ToLeaveClimbPath = ClimbPathPoints;
				BuildedPathStruct.ToReachTargetClimbPath = ClimbPathPoints;
				BuildedPathStruct.TotalPathLenght = ClimbPathLenght;
				BuildedPathStruct.GroundedPathValid = false;
				BuildedPathStruct.CurrentClimbPathValid = true;

				//GEngine->AddOnScreenDebugMessage(5, 0.5, FColor::Emerald, TEXT("wa obiekty sie wspinaja: NIBY WSZYSTKO GRA"));

				FunctionResult = BuildedPathStruct;
				return true;
			}

		}
	}
	FunctionResult = BuildedPathStruct;
	return false;
}


// ---------------------------------------------------------------------------------------------------------------- DEBUG FUNCTIONS ---------------------------------------
void UAdvancedAI_TasksAndFunctions::DrawResultFromTotalNavPath(FClimbNav_FullPathData QueryResult, float DebugTime)
{
	if (!GetWorld()) return;

	if (QueryResult.GroundedPathValid)
	{
		for (int i = 0; i < (QueryResult.GroundedPathPoints.Num() - 1); i++)
		{
			const FVector CurrentPoint = QueryResult.GroundedPathPoints[i];
			const FVector NextPoint = QueryResult.GroundedPathPoints[i + 1];
			DrawDebugLine(GetWorld(), CurrentPoint, NextPoint, FColor::Orange, false, DebugTime, 0, 1.8);
			DrawDebugPoint(GetWorld(), CurrentPoint, 12.0, FColor::Yellow, false, DebugTime, 0);
		}
	}

	if (QueryResult.CurrentClimbPathValid)
	{
		for (int ii = 0; ii < (QueryResult.ToLeaveClimbPath.Num() - 1); ii++)
		{
			const FVector CP1 = QueryResult.ToLeaveClimbPath[ii].LedgeCenter.GetLocation();
			const FVector CN1 = QueryResult.ToLeaveClimbPath[ii +1].LedgeCenter.GetLocation();
			DrawDebugLine(GetWorld(), CP1, CN1, FColor::Orange, false, DebugTime, 0, 1.8);
			DrawDebugPoint(GetWorld(), CP1, 12.0, FColor::Yellow, false, DebugTime, 0);

			if (QueryResult.GroundedPathValid)
			{
				DrawDebugLine
				(
					GetWorld(),
					QueryResult.ToLeaveClimbPath[QueryResult.ToLeaveClimbPath.Num() - 1].LedgeCenter.GetLocation(),
					QueryResult.GroundedPathPoints[0], FColor::Orange, false, DebugTime, 0, 1.8
				);
			}
		}
	}

	if (QueryResult.NextClimbPathValid)
	{
		for (int ii = 0; ii < (QueryResult.ToReachTargetClimbPath.Num() - 1); ii++)
		{
			const FVector CP1 = QueryResult.ToReachTargetClimbPath[ii].LedgeCenter.GetLocation();
			const FVector CN1 = QueryResult.ToReachTargetClimbPath[ii + 1].LedgeCenter.GetLocation();
			DrawDebugLine(GetWorld(), CP1, CN1, FColor::Orange, false, DebugTime, 0, 1.8);
			DrawDebugPoint(GetWorld(), CP1, 12.0, FColor::Yellow, false, DebugTime, 0);

			if (QueryResult.GroundedPathValid)
			{
				DrawDebugLine
				(
					GetWorld(),
					QueryResult.ToReachTargetClimbPath[0].LedgeCenter.GetLocation(),
					QueryResult.GroundedPathPoints[QueryResult.GroundedPathPoints.Num() - 1], FColor::Orange, false, DebugTime, 0, 1.8
				);
			}
		}
	}
}

void UAdvancedAI_TasksAndFunctions::DrawDebugPointInfo(FVector Location, float Weight)
{
	if (GetWorld())
	{
		const FLinearColor ColorA = FLinearColor(0.35, 1.0, 0.0, 1.0); // Limonkowo-Zielony
		const FLinearColor ColorB = FLinearColor(0.6, 0.1, 0.0, 1.0); // Pomaranczowo-Czerwnoy

		FLinearColor C = KML::LinearColorLerp(ColorA, ColorB, KML::MapRangeClamped(Weight, 0.0, 50.0, 0.0, 1.0));

		DEBUGDRAW_POINT(GetWorld(), Location, 10.0, C.ToFColor(true), false, DebugDuration, 0);
		float ScreenSize = 0.8;
		if (UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			ScreenSize = KML::MapRangeClamped(KML::Vector_Distance2D(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)->GetActorLocation(), Location), 250.0, 1000.0, 1.0, 0.5);
		}
		//(GetWorld(), Location + FVector(0, 0, 10), FString::SanitizeFloat(Weight), nullptr, C.ToFColor(true), DebugDuration, false, ScreenSize);
	}
}

void UAdvancedAI_TasksAndFunctions::DrawDebugPath(TArray<FVector> PathPoints, float Weight)
{
	if (PathPoints.Num() >= 2 && GetWorld())
	{
		const FLinearColor ColorA = FLinearColor(0.0, 0.85, 0.2, 1.0); // lekko morski zielony
		const FLinearColor ColorB = FLinearColor(1.0, 0.2, 0.0, 1.0); // jasny pomaranczowy

		FLinearColor C = KML::LinearColorLerp(ColorB, ColorA, KML::MapRangeClamped(Weight, 0.0, 1.0, 0.0, 1.0));

		for (int i = 0; i < (PathPoints.Num() - 1); i++)
		{
			const FVector CurrentPoint = PathPoints[i];
			const FVector NextPoint = PathPoints[i + 1];

			DEBUGDRAW_LINE(GetWorld(), CurrentPoint, NextPoint, C.ToFColor(true), false, DebugDuration, 0, 1.8);
			DEBUGDRAW_POINT(GetWorld(), CurrentPoint, 6.0, C.ToFColor(true), false, DebugDuration, 0);
		}

		FVector AvgPosition = KML::GetVectorArrayAverage(PathPoints);
		DEBUGDRAW_STRING(GetWorld(), AvgPosition + FVector(0, 0, 10), FString::SanitizeFloat(Weight), nullptr, C.ToFColor(true), DebugDuration, true, 1.2);

	}

}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

float UAdvancedAI_TasksAndFunctions::MakeDistributionWeight(FWeightFunction InWeightStruct)
{
	float X = InWeightStruct.Weight;
	const float B = InWeightStruct.WeightBias;

	switch (InWeightStruct.DistributionFunction)
	{
	case EDistributionFunctionType::Linear:
		return X + B;
	case EDistributionFunctionType::Exp:
		return FMath::Exp(X) - 1.0f + B;
	case EDistributionFunctionType::Exp2:
		return FMath::Exp2(X) - 1.0f + B;
	case EDistributionFunctionType::Power:
		return FMath::Pow(X, InWeightStruct.PowerExponent) + B;
	case EDistributionFunctionType::Sin:
		return FMath::Sin(X * PI) + B;
	case EDistributionFunctionType::Cos:
		return FMath::Cos(X * PI) + B;
	case EDistributionFunctionType::Sinh:
		return FMath::Sinh(X * InWeightStruct.PowerExponent) + B;
	default:
		return X + B;
	}
}

float UAdvancedAI_TasksAndFunctions::MakeDistributionWeightCorrected(FWeightFunction InWeightStruct, float InValue, float MaxValue, float MinValue, bool bDisplayDebug, CoverPointFinderWeightDebugMode DebugSubState, FVector DebugPosition)
{
	const float X = InWeightStruct.Weight;
	const float B = InWeightStruct.WeightBias;

	float AsReturnValue = 0.0;

	switch (InWeightStruct.DistributionFunction)
	{
	case EDistributionFunctionType::Linear:
		AsReturnValue = (InValue + B) * X;
		break;
	case EDistributionFunctionType::Exp:
		AsReturnValue = (FMath::Exp(InValue) - 1.0f + B) * X;
		break;
	case EDistributionFunctionType::Exp2:
		AsReturnValue = (FMath::Exp2(InValue) - 1.0f + B) * X;
		break;
	case EDistributionFunctionType::Power:
		AsReturnValue = (FMath::Pow(InValue + B, InWeightStruct.PowerExponent)) * X;
		break;
	case EDistributionFunctionType::Sin:
		AsReturnValue = (FMath::Sin(InValue * PI) + B) * X;
		break;
	case EDistributionFunctionType::Cos:
		AsReturnValue = (FMath::Cos(InValue * PI) + B) * X;
		break;
	case EDistributionFunctionType::Sinh:
		AsReturnValue = (FMath::Sinh(InValue * InWeightStruct.PowerExponent) + B) * X;
		break;
	default:
		AsReturnValue = (InValue + B) * X;
	}

#if WITH_EDITOR //▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
	if ((HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointWeightOnly || HidePosFinderDomainDebugMode == CoverPointFinderFilterDebugMode::PointAndPathWeight) && bDrawDebugPointInfo)//  ▒ 
	{																																																	 //  ▒
		if (HidePosFinderPointWeightDebugMode == DebugSubState)																																		     //  ▒
		{																																																 //  ▒
			FLinearColor C = KML::LinearColorLerp(FLinearColor(1.0, 0.1, 0.0, 1.0), FLinearColor(0.0, 1, 0.2, 1.0), KML::MapRangeClamped(AsReturnValue, 0.0, X, 0.0, 1.0));								 //  ▒
			if (AsReturnValue < -0.05) C = KML::LinearColorLerp(C, FColor(100, 0, 0, 255), 0.5);																										 //  ▒
			DEBUGDRAW_SPHERE(GetWorld(), DebugPosition, 12, 4, C.ToFColor(true), false, DebugDuration, 0, 2.5);																							 //  ▒
			if (KML::SafeDivide(AsReturnValue, X) > 0.5)																																				 //  ▒
			{																																															 //  ▒
				DEBUGDRAW_STRING(GetWorld(), DebugPosition, FString::SanitizeFloat(AsReturnValue), nullptr, C.ToFColor(true), DebugDuration, true, 0.9);												 //  ▒
			}																																															 //  ▒
		}																																																 //  ▒
	}																																																	 //  ▒
#endif // WITH_EDITOR ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒

	return AsReturnValue;
}


