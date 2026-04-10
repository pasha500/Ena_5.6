


#include "AGLS_AIsMeleeFightCore.h"

static TAutoConsoleVariable<int32> CVarAglsMeleeFightDebug(
	TEXT("AGLS.Components.AIsMeleeFight.DebugType"),
	0,
	TEXT("0 = No Debug, 1 = Only Check Result, 2 = Check Result and Tick durning action"));


// Sets default values for this component's properties
UAGLS_AIsMeleeFightCore::UAGLS_AIsMeleeFightCore()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UAGLS_AIsMeleeFightCore::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}

// Called every frame
void UAGLS_AIsMeleeFightCore::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}


bool UAGLS_AIsMeleeFightCore::CheckCanStartFinisher_Implementation(ACharacter*& ChoosedActor, float& DistanceTo, FTransform& TransformVic, FTransform& TransformAtt, bool StartWhenCheckValid, float SearchRadius, ECollisionChannel EnemyTraceChannel)
{
	return false;
}


bool UAGLS_AIsMeleeFightCore::TryStartFinisherAction_Implementation(bool CanStart, ACharacter* ChoosedEnemy, float DistanceTo, FTransform TransformVic, FTransform TransformAtt)
{
	return false;
}


void UAGLS_AIsMeleeFightCore::DoWhenFinisherAction_Implementation()
{
}


bool UAGLS_AIsMeleeFightCore::TryInterruptFinisherAction_Implementation()
{
	return false;
}


float UAGLS_AIsMeleeFightCore::GetFinisherTimeElapsed_Implementation(bool bNormalized)
{
	return 0.0f;
}


void UAGLS_AIsMeleeFightCore::ChooseBestAnimSeq_Implementation(bool& Valid, FAGLS_FinishersDataForAI& ReturnAssets, ACharacter* InVicChar, FTransform InTransformAtt, FTransform InTransformVic)
{
}

TMap<FName, FAGLS_FinishersDataForAI> UAGLS_AIsMeleeFightCore::GetAllSequencesData()
{
	if (SequencesData)
	{
		return SequencesData->AnimSequences;
	}
	else
	{
		return TMap<FName, FAGLS_FinishersDataForAI>();
	}
}





const void UAGLS_AIsMeleeFightCore::DebugSphere(const FVector& Position, float Radius, FLinearColor Color, float Thickness, float Duration, int Depth)
{
	if (GetWorld())
	{
		if (CVarAglsMeleeFightDebug.GetValueOnAnyThread() > 0)
		{ DrawDebugSphere(GetWorld(), Position, Radius, 10, Color.ToFColor(true), false, Duration, Depth, Thickness); }
	}
}

const void UAGLS_AIsMeleeFightCore::DebugLine(const FVector& Start, const FVector& End, FLinearColor Color, float Thickness, float Duration, int Depth)
{
	if (GetWorld())
	{
		if (CVarAglsMeleeFightDebug.GetValueOnAnyThread() > 0)
		{ DrawDebugLine(GetWorld(), Start, End, Color.ToFColor(true), false, Duration, Depth, Thickness); }
	}
}

const void UAGLS_AIsMeleeFightCore::DebugPoint(const FVector& Position, float Size, FLinearColor Color, float Duration, int Depth)
{
	if (GetWorld())
	{
		if (CVarAglsMeleeFightDebug.GetValueOnAnyThread() > 0)
		{ DrawDebugPoint(GetWorld(), Position, Size, Color.ToFColor(true), false, Duration, Depth); }
	}
}

const void UAGLS_AIsMeleeFightCore::DebugPath(TArray<FVector> Points, float Size, float Thickness, FLinearColor Color, float Duration, int Depth)
{
	if (GetWorld() && Points.Num() > 1)
	{
		if (CVarAglsMeleeFightDebug.GetValueOnAnyThread() > 0)
		{
			for (int i = 0; i < Points.Num() - 1; i++)
			{
				DrawDebugLine(GetWorld(), Points[i], Points[i + 1], Color.ToFColor(true), false, Duration, Depth, Thickness);

				if (Size > 0.1)
				{
					DrawDebugPoint(GetWorld(), Points[i], Size, Color.ToFColor(true), false, Duration, Depth);
				}
			}
			DrawDebugPoint(GetWorld(), Points[Points.Num()-1], Size, Color.ToFColor(true), false, Duration, Depth);
		}
	}
}



