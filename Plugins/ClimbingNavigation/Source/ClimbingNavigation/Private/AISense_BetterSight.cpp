       

#include "AISense_BetterSight.h"

#include "AIHelpers.h"
#include "AISystem.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "EngineDefines.h"
#include "EngineGlobals.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISightTargetInterface.h"
#include "VisualLogger/VisualLogger.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"

//#include UE_INLINE_GENERATED_CPP_BY_NAME(AISense_Sight)

#define AISENSE_SIGHT_TIMESLICING_DEBUG 0                
#define DO_SIGHT_VLOGGING (0 && ENABLE_VISUAL_LOG)

DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight"), STAT_AI_Sense_Sight, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Update Sort"), STAT_AI_Sense_Sight_UpdateSort, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Compute visibility"), STAT_AI_Sense_Sight_ComputeVisibility, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Query operations"), STAT_AI_Sense_Sight_QueryOperations, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Listener Update"), STAT_AI_Sense_Sight_ListenerUpdate, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Register Target"), STAT_AI_Sense_Sight_RegisterTarget, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove By Listener"), STAT_AI_Sense_Sight_RemoveByListener, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove To Target"), STAT_AI_Sense_Sight_RemoveToTarget, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Process pending result"), STAT_AI_Sense_Sight_ProcessPendingQuery, STATGROUP_AI);

constexpr int32 DefaultMaxTracesPerTick = 6;
constexpr int32 DefaultMaxAsyncTracesPerTick = 10;
constexpr int32 DefaultMinQueriesPerTimeSliceCheck = 40;
constexpr float DefaultPendingQueriesBudgetReductionRatio = 0.5f;
constexpr bool bDefaultUseAsynchronousTraceForDefaultSightQueries = false;
constexpr float DefaultStimulusStrength = 1.f;
   
enum class EForEachResult : uint8
{
	Break,
	Continue,
};

template <typename T, class PREDICATE_CLASS>
EForEachResult ForEach(T& Array, const PREDICATE_CLASS& Predicate)
{
	for (typename T::ElementType& Element : Array)
	{
		if (Predicate(Element) == EForEachResult::Break)
		{
			return EForEachResult::Break;
		}
	}
	return EForEachResult::Continue;
}

enum EMyReverseForEachResult : uint8
{
	UnTouched,
	Modified,
};

template <typename T, class PREDICATE_CLASS>
EMyReverseForEachResult ReverseForEach(T& Array, const PREDICATE_CLASS& Predicate)
{
	EMyReverseForEachResult RetVal = EMyReverseForEachResult::UnTouched;
	for (int32 Index = Array.Num() - 1; Index >= 0; --Index)
	{
		if (Predicate(Array, Index) == EMyReverseForEachResult::Modified)
		{
			RetVal = EMyReverseForEachResult::Modified;
		}
	}
	return RetVal;
}

namespace UE::AIBetterSense_Sight
{
#if AISENSE_SIGHT_TIMESLICING_DEBUG
	struct FTimingSlicingInfo
	{
		FTimingSlicingInfo()
		{
			Start();
		}

		double StartTime = 0.;
		double EndTime = 0.;

		int32 InRangeCount = 0;
		int32 OutOfRangeCount = 0;

		float InRangeAgeSum = 0.f;
		float OutOfRangeAgeSum = 0.f;

		void Start() { StartTime = FPlatformTime::Seconds(); }
		void Stop() { EndTime = FPlatformTime::Seconds(); }

		void PushQueryInfo(const bool bIsInRange, const float Age)
		{
			if (bIsInRange)
			{
				++InRangeCount;
				InRangeAgeSum += Age;
			}
			else
			{
				++OutOfRangeCount;
				OutOfRangeAgeSum += Age;
			}
		}

		FString ToString() const
		{
			FString Info = FString::Format(TEXT("in {0} seconds"), { EndTime - StartTime });
			if (InRangeCount > 0)
			{
				Info.Append(FString::Format(TEXT("[{0} InRange Age:{1}]"), { InRangeCount, InRangeAgeSum / InRangeCount }));
			}
			if (OutOfRangeCount > 0)
			{
				Info.Append(FString::Format(TEXT("[{0} OutOfRange Age:{1}]"), { OutOfRangeCount, OutOfRangeAgeSum / OutOfRangeCount }));
			}
			return Info;
		}
	};
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG

	bool IsTraceConsideredVisible(const FHitResult* HitResult, const AActor* TargetActor)
	{
		if (HitResult == nullptr)
		{
			return true;
		}

		const AActor* HitResultActor = HitResult->HitObjectHandle.FetchActor();
		return (HitResultActor ? HitResultActor->IsOwnedBy(TargetActor) : false);
	}
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------  BETTER AI SIGHT PERCEPTION CONSTRUCTION  --------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

UAISense_BetterSight::UAISense_BetterSight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BonesNameToCheck.Empty();

	if (TrackingType == AISense_SightTracingMode::OnlyPelvis)
	{
		BonesNameToCheck.Add(TEXT("Pelvis"));
	}
	else if (TrackingType == AISense_SightTracingMode::PelvisAndHead)
	{
		BonesNameToCheck.Add(TEXT("Head"));
		BonesNameToCheck.Add(TEXT("Pelvis"));
	}
	else if (TrackingType == AISense_SightTracingMode::PelvisHeadArms)
	{
		BonesNameToCheck.Add(TEXT("Head"));
		BonesNameToCheck.Add(TEXT("Pelvis"));
		BonesNameToCheck.Add(TEXT("hand_l"));
		BonesNameToCheck.Add(TEXT("hand_r"));
	}
	else if (TrackingType == AISense_SightTracingMode::AllBodyParts)
	{
		BonesNameToCheck.Add(TEXT("Head"));
		BonesNameToCheck.Add(TEXT("Pelvis"));
		BonesNameToCheck.Add(TEXT("hand_l"));
		BonesNameToCheck.Add(TEXT("hand_r"));
		BonesNameToCheck.Add(TEXT("calf_l"));
		BonesNameToCheck.Add(TEXT("calf_r"));
	}

}


//Main Update Function
float UAISense_BetterSight::Update()
{
	float ReturnValue = BetterSightUpdate();
	return ReturnValue;
}

float UAISense_BetterSight::BetterSightUpdate()
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight);

	UWorld* World = GEngine->GetWorldFromContextObject(GetPerceptionSystem()->GetOuter(), EGetWorldErrorMode::LogAndReturnNull);

	if (World == nullptr)
	{
		return SuspendNextUpdate;
	}

	UE_MT_SCOPED_WRITE_ACCESS(QueriesListAccessDetector);

	// sort Sight Queries
	{
		auto RecalcScore = [](FAISightQuery& SightQuery)->EForEachResult
			{
				SightQuery.RecalcScore();
				return EForEachResult::Continue;
			};

		SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_UpdateSort);
		// Sort out of range queries
		if (bSightQueriesOutOfRangeDirty)
		{
			ForEach(SightQueriesOutOfRange, RecalcScore);
			SightQueriesOutOfRange.Sort(FAISightQuery::FSortPredicate());
			NextOutOfRangeIndex = 0;
			bSightQueriesOutOfRangeDirty = false;
		}

		// Sort in range queries
		ForEach(SightQueriesInRange, RecalcScore);
		SightQueriesInRange.Sort(FAISightQuery::FSortPredicate());
	}

	int32 TracesCount = 0;
	int32 AsyncTracesCount = FMath::Max(0, static_cast<int32>(PendingQueriesBudgetReductionRatio * SightQueriesPending.Num()));	// pending queries should be requesting async collisions traces at this frame, so we might want to restrain ourself in this update
	int32 NumQueriesProcessed = 0;
	const double TimeSliceEnd = FPlatformTime::Seconds() + MaxTimeSlicePerTick;
	bool bHitTimeSliceLimit = false;
#if AISENSE_SIGHT_TIMESLICING_DEBUG
	UE::AISense_Sight::FTimingSlicingInfo SlicingInfo;
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG
	constexpr int32 InitialInvalidItemsSize = 16;
	enum class EOperationType : uint8
	{
		Remove,
		SwapList,
		MoveToPending
	};
	struct FQueryOperation
	{
		FQueryOperation(bool bInInRange, EOperationType InOpType, int32 InIndex) : bInRange(bInInRange), OpType(InOpType), Index(InIndex) {}
		bool bInRange;
		EOperationType OpType;
		int32 Index;
	};
	TArray<FQueryOperation> QueryOperations;
	TArray<FAISightTarget::FTargetId> InvalidTargets;
	QueryOperations.Reserve(InitialInvalidItemsSize);
	InvalidTargets.Reserve(InitialInvalidItemsSize);

	AIPerception::FListenerMap& ListenersMap = *GetListeners();

	int32 InRangeItr = 0;
	int32 OutOfRangeItr = 0;
	for (int32 QueryIndex = 0; QueryIndex < SightQueriesInRange.Num() + SightQueriesOutOfRange.Num(); ++QueryIndex)
	{
		// Time slice limit check - spread out checks to every N queries so we don't spend more time checking timer than doing work
		NumQueriesProcessed++;
		if ((NumQueriesProcessed % MinQueriesPerTimeSliceCheck) == 0 && FPlatformTime::Seconds() > TimeSliceEnd)
		{
			bHitTimeSliceLimit = true;
		}

		if (bHitTimeSliceLimit || TracesCount >= MaxTracesPerTick || AsyncTracesCount >= MaxAsyncTracesPerTick)
		{
			break;
		}

		// Calculate next in range query
		int32 InRangeIndex = SightQueriesInRange.IsValidIndex(InRangeItr) ? InRangeItr : INDEX_NONE;
		FAISightQuery* InRangeQuery = InRangeIndex != INDEX_NONE ? &SightQueriesInRange[InRangeIndex] : nullptr;

		// Calculate next out of range query
		int32 OutOfRangeIndex = SightQueriesOutOfRange.IsValidIndex(OutOfRangeItr) ? (NextOutOfRangeIndex + OutOfRangeItr) % SightQueriesOutOfRange.Num() : INDEX_NONE;
		FAISightQuery* OutOfRangeQuery = OutOfRangeIndex != INDEX_NONE ? &SightQueriesOutOfRange[OutOfRangeIndex] : nullptr;
		if (OutOfRangeQuery)
		{
			OutOfRangeQuery->RecalcScore();
		}

		// Compare to real find next query
		const bool bIsInRangeQuery = (InRangeQuery && OutOfRangeQuery) ? FAISightQuery::FSortPredicate()(*InRangeQuery, *OutOfRangeQuery) : !OutOfRangeQuery;
		FAISightQuery* SightQuery = bIsInRangeQuery ? InRangeQuery : OutOfRangeQuery;
		ensure(SightQuery);

#if AISENSE_SIGHT_TIMESLICING_DEBUG
		SlicingInfo.PushQueryInfo(bIsInRangeQuery, SightQuery->GetAge());
#endif //AISENSE_SIGHT_TIMESLICING_DEBUG

		bIsInRangeQuery ? ++InRangeItr : ++OutOfRangeItr;

		FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
		FAISightTarget& Target = ObservedTargets[SightQuery->TargetId];

		AActor* TargetActor = Target.Target.Get();
		UAIPerceptionComponent* ListenerPtr = Listener.Listener.Get();
		ensure(ListenerPtr);

		// @todo figure out what should we do if not valid
		if (TargetActor && ListenerPtr)
		{
			const FDigestedSightProperties& PropDigest = DigestedProperties[SightQuery->ObserverId];
			const AActor* ListenerBodyActor = ListenerPtr->GetBodyActor();
			float StimulusStrength = DefaultStimulusStrength;
			FVector SeenLocation(0.f);
			int32 NumberOfLoSChecksPerformed = 0;
			int32 NumberOfAsyncLosCheckRequested = 0;

			// ***********************************************************************************************************************************************************************************************************
			// ************************************************************************* Implementation of my fixed collision detection function *************************************************************************
			// ***********************************************************************************************************************************************************************************************************
			const EVisibilityResult VisibilityResult = BetterComputeVisibility(World, *SightQuery, Listener, ListenerBodyActor, Target, TargetActor, PropDigest, StimulusStrength, SeenLocation, NumberOfLoSChecksPerformed, NumberOfAsyncLosCheckRequested);

			TracesCount += NumberOfLoSChecksPerformed;
			AsyncTracesCount += NumberOfAsyncLosCheckRequested;

			if (VisibilityResult == EVisibilityResult::Pending)
			{
				QueryOperations.Add(FQueryOperation(bIsInRangeQuery, EOperationType::MoveToPending, bIsInRangeQuery ? InRangeIndex : OutOfRangeIndex));
			}
			else
			{
				UE_CLOG(VisibilityResult != EVisibilityResult::Visible && VisibilityResult != EVisibilityResult::NotVisible, LogAIPerception, Error, TEXT("UAISense_Sight::Update received invalid Visibility result [%d] for query between Listener %s and Target %s. We'll consider it as NotVisible"), int(VisibilityResult), *GetNameSafe(ListenerBodyActor), *GetNameSafe(TargetActor));

				const bool bIsVisible = VisibilityResult == EVisibilityResult::Visible;
				const bool bWasVisible = SightQuery->GetLastResult();
				const FVector TargetLocation = TargetActor->GetActorLocation();
				UpdateQueryVisibilityStatus(*SightQuery, Listener, bIsVisible, SeenLocation, StimulusStrength, *TargetActor, TargetLocation);

				const float SightRadiusSq = bWasVisible ? PropDigest.LoseSightRadiusSq : PropDigest.SightRadiusSq;
				SightQuery->Importance = CalcQueryImportance(Listener, TargetLocation, SightRadiusSq);
				const bool bShouldBeInRange = SightQuery->Importance > 0.0f;
				if (bIsInRangeQuery != bShouldBeInRange)
				{
					QueryOperations.Add(FQueryOperation(bIsInRangeQuery, EOperationType::SwapList, bIsInRangeQuery ? InRangeIndex : OutOfRangeIndex));
				}

				// restart query
				SightQuery->OnProcessed();
			}
		}
		else
		{
			// put this index to "to be removed" array
			QueryOperations.Add(FQueryOperation(bIsInRangeQuery, EOperationType::Remove, bIsInRangeQuery ? InRangeIndex : OutOfRangeIndex));
			if (TargetActor == nullptr)
			{
				InvalidTargets.AddUnique(SightQuery->TargetId);
			}
		}
	}
	NextOutOfRangeIndex = SightQueriesOutOfRange.Num() > 0 ? (NextOutOfRangeIndex + OutOfRangeItr) % SightQueriesOutOfRange.Num() : 0;

#if AISENSE_SIGHT_TIMESLICING_DEBUG
	SlicingInfo.Stop();
	UE_LOG(LogAIPerception, VeryVerbose, TEXT("UAISense_Sight::Update processed %d sources %s [time slice limited? %d]"), NumQueriesProcessed, *SlicingInfo.ToString(), bHitTimeSliceLimit ? 1 : 0);
#else
	UE_LOG(LogAIPerception, VeryVerbose, TEXT("UAISense_Sight::Update processed %d sources [time slice limited? %d]"), NumQueriesProcessed, bHitTimeSliceLimit ? 1 : 0);
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG

	if (QueryOperations.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_QueryOperations);

		// Sort by InRange and by descending Index 
		QueryOperations.Sort([](const FQueryOperation& LHS, const FQueryOperation& RHS)->bool
			{
				if (LHS.bInRange != RHS.bInRange)
					return LHS.bInRange;
				return LHS.Index > RHS.Index;
			});
		// Do all the removes first and save the out of range swaps because we will insert them at the right location to prevent sorting
		TArray<FAISightQuery> SightQueriesOutOfRangeToInsert;
		for (const FQueryOperation& Operation : QueryOperations)
		{
			switch (Operation.OpType)
			{
			case EOperationType::SwapList:
			{
				if (Operation.bInRange)
				{
					SightQueriesOutOfRangeToInsert.Push(SightQueriesInRange[Operation.Index]);
				}
				else
				{
					SightQueriesInRange.Add(SightQueriesOutOfRange[Operation.Index]);
				}
			}
			break;

			case EOperationType::MoveToPending:
			{
				SightQueriesPending.Add(Operation.bInRange ? SightQueriesInRange[Operation.Index] : SightQueriesOutOfRange[Operation.Index]);
			}
			break;

			case EOperationType::Remove:
				break;

			default:
				check(false);
				break;
			}

			if (Operation.bInRange)
			{
				// In range queries are always sorted at the beginning of the update
				SightQueriesInRange.RemoveAtSwap(Operation.Index, 1, EAllowShrinking::No);
			}
			else
			{
				// Preserve the list ordered
				SightQueriesOutOfRange.RemoveAt(Operation.Index, 1, EAllowShrinking::No);
				if (Operation.Index < NextOutOfRangeIndex)
				{
					NextOutOfRangeIndex--;
				}
			}
		}
		// Reinsert the saved out of range swaps
		if (SightQueriesOutOfRangeToInsert.Num() > 0)
		{
			SightQueriesOutOfRange.Insert(SightQueriesOutOfRangeToInsert.GetData(), SightQueriesOutOfRangeToInsert.Num(), NextOutOfRangeIndex);
			NextOutOfRangeIndex += SightQueriesOutOfRangeToInsert.Num();
		}

		if (InvalidTargets.Num() > 0)
		{
			// this should not be happening since UAIPerceptionSystem::OnPerceptionStimuliSourceEndPlay introduction
			UE_VLOG(GetPerceptionSystem(), LogAIPerception, Error, TEXT("Invalid sight targets found during UAISense_Sight::Update call"));

			for (const auto& TargetId : InvalidTargets)
			{
				// remove affected queries
				MyRemoveAllQueriesToTarget_Internal(TargetId);
				// remove target itself
				ObservedTargets.Remove(TargetId);
			}

			// remove holes
			ObservedTargets.Compact();
		}
	}

	//return SightQueries.Num() > 0 ? 1.f/6 : FLT_MAX;
	return 0.f;
}



void UAISense_BetterSight::MyRemoveAllQueriesToTarget_Internal(const FAISightTarget::FTargetId& TargetId, const TFunction<void(const FAISightQuery&)>& OnRemoveFunc/*= nullptr */)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveToTarget);

	auto RemoveQuery = [&TargetId, &OnRemoveFunc](TArray<FAISightQuery>& SightQueries, const int32 QueryIndex)->EMyReverseForEachResult
		{
			const FAISightQuery& SightQuery = SightQueries[QueryIndex];

			if (SightQuery.TargetId == TargetId)
			{
				if (OnRemoveFunc)
				{
					OnRemoveFunc(SightQuery);
				}
				SightQueries.RemoveAtSwap(QueryIndex, 1, EAllowShrinking::No);
				return EMyReverseForEachResult::Modified;
			}
			return EMyReverseForEachResult::UnTouched;
		};
	ReverseForEach(SightQueriesInRange, RemoveQuery);
	if (ReverseForEach(SightQueriesOutOfRange, RemoveQuery) == EMyReverseForEachResult::Modified)
	{
		bSightQueriesOutOfRangeDirty = true;
	}
	ReverseForEach(SightQueriesPending, RemoveQuery);
}

// ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
// |||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||  F I X E D   C O L L I S I O N   C H E C K I N G ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| 
// ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
UAISense_Sight::EVisibilityResult UAISense_BetterSight::BetterComputeVisibility(UWorld* World, FAISightQuery& SightQuery, FPerceptionListener& Listener, const AActor* ListenerActor, FAISightTarget& Target, AActor* TargetActor, const FDigestedSightProperties& PropDigest, float& OutStimulusStrength, FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested) const
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_ComputeVisibility);

	// @Note that automagical "seeing" does not care about sight range nor vision cone
	if (ShouldAutomaticallySeeTarget(PropDigest, &SightQuery, Listener, TargetActor, OutStimulusStrength))
	{
		OutSeenLocation = FAISystem::InvalidLocation;
		return EVisibilityResult::Visible;
	}

	const FVector TargetLocation = TargetActor->GetActorLocation();
	const float SightRadiusSq = SightQuery.GetLastResult() ? PropDigest.LoseSightRadiusSq : PropDigest.SightRadiusSq;
	if (!FAISystem::CheckIsTargetInSightCone(Listener.CachedLocation, Listener.CachedDirection, PropDigest.PeripheralVisionAngleCos, PropDigest.PointOfViewBackwardOffset, PropDigest.NearClippingRadiusSq, SightRadiusSq, TargetLocation))
	{
		return EVisibilityResult::NotVisible;
	}

	if (IAISightTargetInterface* SightTargetInterface = Target.WeakSightTargetInterface.Get())
	{
		const bool bWasVisible = SightQuery.GetLastResult();
		FCanBeSeenFromContext Context;
		Context.SightQueryID = FAISightQueryID(SightQuery);
		Context.ObserverLocation = Listener.CachedLocation;
		Context.IgnoreActor = ListenerActor;
		Context.bWasVisible = &bWasVisible;

		const EVisibilityResult Result = SightTargetInterface->CanBeSeenFrom(Context, OutSeenLocation, OutNumberOfLoSChecksPerformed, OutNumberOfAsyncLosCheckRequested, OutStimulusStrength, &SightQuery.UserData, &OnPendingCanBeSeenQueryProcessedDelegate);
		if (Result == EVisibilityResult::Pending)
		{
			// we need to clear the trace info value in order to avoid interfering with the engine processed asynchronous queries
			SightQuery.SetTraceInfo(FTraceHandle());
		}
		return Result;
	}
	else
	{
		// we need to do tests ourselves
		FCollisionQueryParams QueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(AILineOfSight), true, ListenerActor);

		QueryParams.AddIgnoredActor(TargetActor);

		if (bUseAsynchronousTraceForDefaultSightQueries)
		{
			//const FTraceHandle TraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Single, Listener.CachedLocation, TargetLocation, DefaultSightCollisionChannel, QueryParams, FCollisionResponseParams::DefaultResponseParam, &OnPendingTraceQueryProcessedDelegate);
			const FTraceHandle TraceHandle = PerformTracingToActorAsync(World, Listener.CachedLocation, TargetActor, DefaultSightCollisionChannel, QueryParams, FCollisionResponseParams::DefaultResponseParam);
			if (!TraceHandle.IsValid())
			{
				return EVisibilityResult::NotVisible;
			}

			++OutNumberOfAsyncLosCheckRequested;

			// store the trace handle information here so that we can identify the associated query when we'll receive the delegate callback
			SightQuery.SetTraceInfo(TraceHandle);
			return EVisibilityResult::Pending;
		}
		else
		{
			FHitResult HitResult;
			//const bool bHit = World->LineTraceSingleByChannel(HitResult, Listener.CachedLocation, TargetLocation, DefaultSightCollisionChannel, QueryParams, FCollisionResponseParams::DefaultResponseParam);
			const bool bHit = PerformTracingToActor(HitResult, World, Listener.CachedLocation, TargetActor, DefaultSightCollisionChannel, QueryParams, FCollisionResponseParams::DefaultResponseParam);
			
			++OutNumberOfLoSChecksPerformed;

			if (UE::AIBetterSense_Sight::IsTraceConsideredVisible(bHit ? &HitResult : nullptr, TargetActor))
			{
				OutSeenLocation = TargetLocation;
				return EVisibilityResult::Visible;
			}
			else
			{
				return EVisibilityResult::NotVisible;
			}
		}
	}
}

bool UAISense_BetterSight::PerformTracingToActor(FHitResult& HitResult, UWorld* World, FVector StartLocation, AActor* TargetActor, ECollisionChannel CollisionChannel, FCollisionQueryParams QueryParams, FCollisionResponseParams ResponseParams) const
{
	if (IsValid(World) == false) { return false; }

	ACharacter* TargetAsCharacter = Cast<ACharacter>(TargetActor);

	FVector TargetTraceLocation = TargetActor->GetActorLocation();

	if (TargetAsCharacter)
	{
		for (int i = 0; i < BonesNameToCheck.Num(); i++)
		{
			TargetTraceLocation = TargetAsCharacter->GetMesh()->GetSocketLocation(BonesNameToCheck[i]);
			const bool bHit = World->LineTraceSingleByChannel(HitResult, StartLocation, TargetTraceLocation, DefaultSightCollisionChannel, QueryParams, ResponseParams);

			bool bDrawsDebug = false;
			if (bDrawsDebug)
			{
				if (bHit)
				{ DrawDebugLine(World, StartLocation, TargetTraceLocation, FColor::Emerald, false, 0.05, 0, 0.8); }
				else
				{ DrawDebugLine(World, StartLocation, TargetTraceLocation, FColor::Red, false, 0.05, 0, 0.5); }
			}

			if (bHit == false)
			{
				return false;
			}

		}
		return true;
	}
	else
	{
		const bool bHit = World->LineTraceSingleByChannel(HitResult, StartLocation, TargetTraceLocation, DefaultSightCollisionChannel, QueryParams, ResponseParams);
		return bHit;
	}

}

FTraceHandle UAISense_BetterSight::PerformTracingToActorAsync(UWorld* World, FVector StartLocation, AActor* TargetActor, ECollisionChannel CollisionChannel, FCollisionQueryParams QueryParams, FCollisionResponseParams ResponseParams) const
{
	if (IsValid(World) == false) 
	{ 
		return FTraceHandle::FTraceHandle();
	}

	ACharacter* TargetAsCharacter = Cast<ACharacter>(TargetActor);

	FVector TargetTraceLocation = TargetActor->GetActorLocation();

	if (TargetAsCharacter)
	{
		FTraceHandle TraceHandle;
		for (int i = 0; i < BonesNameToCheck.Num(); i++)
		{
			TargetTraceLocation = TargetAsCharacter->GetMesh()->GetSocketLocation(BonesNameToCheck[i]);
			TraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Single, StartLocation, TargetTraceLocation, DefaultSightCollisionChannel, QueryParams, ResponseParams, &OnPendingTraceQueryProcessedDelegate);
			if (!TraceHandle.IsValid())
			{
				return TraceHandle;
			}
		}
		return TraceHandle;
	}
	else
	{
		const FTraceHandle TraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Single, StartLocation, TargetTraceLocation, DefaultSightCollisionChannel, QueryParams, ResponseParams, &OnPendingTraceQueryProcessedDelegate);
		return TraceHandle;
	}
}
