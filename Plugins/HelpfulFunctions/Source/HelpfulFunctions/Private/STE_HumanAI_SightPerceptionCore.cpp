


#include "STE_HumanAI_SightPerceptionCore.h"
#include "Kismet/KismetMathLibrary.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/CapsuleComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "AGLS_AI_CharacterInterface.h"
#include "ALS_HumanAI_InterfaceCpp.h"
#include "HelpfulFunctionsBPLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"

#define HFL UHelpfulFunctionsBPLibrary
#define KML UKismetMathLibrary
#define MapRANGE UKismetMathLibrary::MapRangeClamped
#define HAINTERFACE IALS_HumanAI_InterfaceCpp
#define KSL UKismetSystemLibrary
#define SIGHTPROPERTIES SightPerceptionCopy
#define GAME UGameplayStatics
#define FAILEDACTOR DrawFailedPerceivedActorInfo
#define PRINTEXECUTED GEngine->AddOnScreenDebugMessage(-1, 0.2, FColor::White, "EXECUTED")

namespace
{
	bool CanAccessStateTreePropertyRefs(const FStateTreeExecutionContext& Context)
	{
		return Context.IsValid() && Context.GetCurrentlyProcessedFrame() != nullptr;
	}
}

void USTE_HumanAI_SightPerceptionCore::TreeStart(FStateTreeExecutionContext& Context)
{
	if (Actor) { Char = Cast<ACharacter>(Actor); }

	if (BlackboardData)
	{
		BlackboardData->SetValueAsObject("StateTreePercpetionEvaluatorRef", this);
	}
}


void USTE_HumanAI_SightPerceptionCore::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	if (bRunSourceLogicOnTick)
	{
		if (!CanAccessStateTreePropertyRefs(Context))
		{
			return;
		}

		//Read From State Tree Parameter
		ResolveStateTreeProperties(Context);

		const bool RefreshParams = CheckShouldRefresh(DeltaTime);
		if (RefreshParams)
		{
			bool bDisableVisionPerception = false;
			static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("AGLS.HumanAI.Controller.DisableVisionPerception"));
			if (CVar)
			{ bDisableVisionPerception = CVar->GetBool(); }

			if (bDisableVisionPerception == true) //Check The SightPerception is disabled by using Console Command. If that set the default sight properties
			{
				SIGHTPROPERTIES.EnemySpottedHim = false;
				SIGHTPROPERTIES.DetectedEnemy = false;
				SIGHTPROPERTIES.ReactionProgressTime = 0.0;
				DesiredSightDetectionValue = -1;
			}
			else
			{
				UpdateSightPerceptionValues(); // <----- Get Characters from SightPerceptionComponent and choose best enemy Actor
			}
			// DRAW DEBUG SHAPE
			if (DrawDebugMode == 2) { DrawDebugPoint(GetWorld(), Char->GetActorLocation(), 12, FColor(0, 222, 50, 100), false, 0.1, DrawingShapesDepth); }
		}
		//MAIN functions
		UpdatePerFrameSightValues(DeltaTime);
		DrawDebugAboutResultOnTick();

		if (TimerToForceSetParams > 0.0)
		{
			TimerToForceSetParams = TimerToForceSetParams - DeltaTime;
			if (TimerToForceSetParams <= 0.0)
			{
				TimerToForceSetParams = -1;
				ForceSetSightEnemyParams(CachedSightParams, CachedSightStatus, false);
			}
		}
		//Write to State Tree Parameter
		WriteToStateTreeProperties(Context, BehaviorModeCopy, SightPerceptionCopy);

	}

}


void USTE_HumanAI_SightPerceptionCore::ResolveStateTreeProperties(FStateTreeExecutionContext& Context)
{
	if (!CanAccessStateTreePropertyRefs(Context))
	{
		return;
	}

	if (FAGLS_HumanAI_EnemyTags* Sight =
		SightPerceptionRef.GetMutablePtr<FAGLS_HumanAI_EnemyTags>(Context))
	{
		SightPerceptionCopy = *Sight;
	}

	if (auto* Behavior =
		MainBehaviorModeRef.GetMutablePtr<AGLS_HumanAI_MainBehaviorMode>(Context))
	{
		BehaviorModeCopy = *Behavior;
	}

}


void USTE_HumanAI_SightPerceptionCore::WriteToStateTreeProperties(FStateTreeExecutionContext& Context, AGLS_HumanAI_MainBehaviorMode& ModeState, FAGLS_HumanAI_EnemyTags& EnemyTags) const
{
	if (!CanAccessStateTreePropertyRefs(Context))
	{
		return;
	}

	if (FAGLS_HumanAI_EnemyTags* SightPtr =
		SightPerceptionRef.GetMutablePtr<FAGLS_HumanAI_EnemyTags>(Context))
	{
		*SightPtr = EnemyTags; // skopiuj ca�y struct
	}
}


bool USTE_HumanAI_SightPerceptionCore::CheckShouldRefresh(float dt)
{
	CurrentElapsedTime = CurrentElapsedTime + dt;

	if (CurrentElapsedTime > CurrentTickInterval)
	{
		CurrentElapsedTime = 0.0;

		if (RefresingPerceptionIntervalsMap.Num() > 0)
		{
			if (float* d = RefresingPerceptionIntervalsMap.Find(CurrentLOD))
			{
				CurrentTickInterval = *d;
				return true;
			}
			else
			{
				return true;
			}
		}
	}
	return false;
}


float USTE_HumanAI_SightPerceptionCore::UpdateSightPerceptionValues_Implementation()
{
	TArray<AActor*> PerceivedActorsAll;
	TArray<ACharacter*> PossibleEnemies;
	TArray<float> EnemiesWeights;

	if (!AIController->GetAIPerceptionComponent()) return -999;
	AIController->GetAIPerceptionComponent()->GetCurrentlyPerceivedActors(SenseToUse, PerceivedActorsAll);

	for (int i = 0; i < PerceivedActorsAll.Num(); i++)
	{
		ACharacter* CurrentChar = Cast<ACharacter>(PerceivedActorsAll[i]);
		if (!CurrentChar) continue;
		//Niestety SightPerception (PerceptionComponent) nie pozwala z �atwo�ci� filtrowa� wszystkich wykrytych instancji 
		// Pawn w danym czasie i promienu. Dlatego wymagane jest aby wykona� to r�cznie. Z tego wzgl�du musimy wykona� 
		// iteracj� przez obecnie zajerestrowane Aktory, i sprawdzi� jaki z nich spe�nia nast�pujace warunki takie jak: 
		// czy nie jest martwy, dla Zombie AI jest on wrogiem itd. 
		if (GetCharacterIsDead(CurrentChar) == true) { FAILEDACTOR(CurrentChar, "Skip: GetCharacterIsDead", 0); continue; }
		if (ModifySightForPlayerCompanion(CurrentChar) == false) { FAILEDACTOR(CurrentChar, "Skip: ModifySightForPlayerCompanion", 0); continue; }
		if (HFL::GetIsEnemyState(Char, Char, CurrentChar) == false) { FAILEDACTOR(CurrentChar, "Skip: GetIsEnemyState", 0); continue; }
		if (HFL::IsNotHidingInFoliage(Char, Char, CurrentChar, FoliageCollisionChannel, 0) == false) { FAILEDACTOR(CurrentChar, "Skip: IsNotHidingInFoliage", 0); continue; }
        // If only one enemy is perceived, no weight array is needed.
        // In that case, set values directly and finish this branch.
		if (PerceivedActorsAll.Num() == 1)
		{
			SetBB_EnemyActor(CurrentChar);
			SetNewEnemyButRememberPrev(CurrentChar);
			DesiredSightDetectionValue = 1.0;
			FAILEDACTOR(CurrentChar, "Choosed", 2);
			return 1.0;
		}
		else
		{
			float WeightByDistance = 1.0;
			float WeightByPlayerController = 1.0;
			float WeightByGameplayTags = 1.0;
			float WeightBySeeEachOther = 1.0;

			//Then 01
			float DistTo = FVector::Distance(Char->GetActorLocation(), CurrentChar->GetActorLocation());
			if (DistTo > 100)
			{
				if (PerceptionDistWeightCurve)
				{
					WeightByDistance = KML::Lerp(0.2, 1.0, PerceptionDistWeightCurve->GetFloatValue(MapRANGE(DistTo, 200, 1000, 1, 0)));
				}
			}
			else
			{
				WeightByDistance = 1.4;
			}

			//Then 02
			if (CurrentChar->IsPlayerControlled() == true) { WeightByPlayerController = 1.2; }

			//Then 03
			if (CurrentChar->GetClass()->ImplementsInterface(UAGLS_AI_CharacterInterface::StaticClass()))
			{
				FGameplayTagContainer CharacterTags;
				IAGLS_AI_CharacterInterface::Execute_BPI_AI_Get_MainTagsContainerData(CurrentChar, CharacterTags);
				if (CharacterTags.HasAny(TagsThatEnemyShouldNotHave) == true) { WeightByGameplayTags = 0.5; }
			}

			//Then 04
			bool PointIsInSightRange = SimpleCheckDoesEnemySee
			(
				FRotator(0, CurrentChar->GetControlRotation().Yaw, 0),
				HFL::GetPlayerCapsuleStartLocation(GetWorld(), CurrentChar),
				Char->GetActorLocation(),
				KML::SelectFloat(35, 28, CurrentChar->IsPlayerControlled()),
				1800,
				KML::SelectFloat(250, 160, CurrentChar->IsPlayerControlled()),
				-1
			);
			if (PointIsInSightRange)
			{
				WeightBySeeEachOther = MapRANGE(FVector::Dist2D(HFL::GetPlayerCapsuleStartLocation(GetWorld(), CurrentChar), Char->GetActorLocation()), 500, 1800, 0.5, 0.2);
			}
			else
			{
				WeightBySeeEachOther = 0.0;
			}
			
			//Then 05 Final Weight
			const float WeightTotal = (WeightByDistance * WeightByPlayerController * WeightByGameplayTags) + WeightBySeeEachOther;
			EnemiesWeights.Add(WeightTotal);
			PossibleEnemies.Add(CurrentChar);

			//DRAW DEBUG FUNCTION
			DrawPerEnemyDebugInfo(CurrentChar, WeightByDistance, WeightByPlayerController, WeightByGameplayTags, WeightBySeeEachOther, WeightTotal);
		}
	}
	if (PossibleEnemies.Num() == 0)
	{
		DesiredSightDetectionValue = -1;
		return -1;
	}
	else
	{
		float MaxWeight = 0; int MaxIndex = 0;
		KML::MaxOfFloatArray(EnemiesWeights, MaxIndex, MaxWeight);
		if (PossibleEnemies.IsValidIndex(MaxIndex))
		{
			if (IsValid(PossibleEnemies[MaxIndex]) == true)
			{
				SetBB_EnemyActor(PossibleEnemies[MaxIndex]);
				SetNewEnemyButRememberPrev(PossibleEnemies[MaxIndex]);
				DesiredSightDetectionValue = 1.0;
				return 1.0;
			}
			else
			{
				DesiredSightDetectionValue = -1;
				return -1;
			}
		}
	}
	return 0;
}


bool USTE_HumanAI_SightPerceptionCore::GetCharacterIsDead_Implementation(ACharacter* InCharacter)
{
	bool bDead = false;
	if (InCharacter->GetClass()->ImplementsInterface(UALS_HumanAI_InterfaceCpp::StaticClass()))
	{
		HAINTERFACE::Execute_HAI_GetDeathState(InCharacter, bDead);
	}
	return bDead;
}


bool USTE_HumanAI_SightPerceptionCore::ModifySightForPlayerCompanion_Implementation(ACharacter* InCharacter)
{
	if (InCharacter)
	{
		int TagIndex = InCharacter->Tags.Find("ZombieModifiedSightMode");
		if (TagIndex == -1)
		{
			return true;
		}

		switch (BehaviorModeCopy)
		{
		case AGLS_HumanAI_MainBehaviorMode::Patroling:
			return false;
		case AGLS_HumanAI_MainBehaviorMode::Finding:
			return false;
		case AGLS_HumanAI_MainBehaviorMode::Fighting:
			return true;
		case AGLS_HumanAI_MainBehaviorMode::Running:
			return true;
		case AGLS_HumanAI_MainBehaviorMode::Interacting:
			return true;
		case AGLS_HumanAI_MainBehaviorMode::None:
			return false;
		}
	}
	return true;
}


void USTE_HumanAI_SightPerceptionCore::SetBB_EnemyActor(ACharacter* InEnemy)
{
	if (BlackboardData)
	{
		BlackboardData->SetValueAsObject(EnemyActorBlackboardKey, InEnemy);
	}
}


bool USTE_HumanAI_SightPerceptionCore::SetNewEnemyButRememberPrev(ACharacter* NewEnemy)
{
	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (SIGHTPROPERTIES.EnemyCharacter == NewEnemy)
		{
			if (IsValid(PrevEnemyChar) == true)
			{
				if (SIGHTPROPERTIES.EnemyCharacter != NewEnemy && SIGHTPROPERTIES.EnemyCharacter != PrevEnemyChar)
				{
					PrevEnemyChar = SIGHTPROPERTIES.EnemyCharacter;
				}
			}
			else
			{
				PrevEnemyChar = NewEnemy;
			}
		}
	}
	SIGHTPROPERTIES.EnemyCharacter = NewEnemy;
	GetSightPerceptionTagsFromCharacter(SIGHTPROPERTIES.IsZombie, SIGHTPROPERTIES.EnemySpottedHim, SIGHTPROPERTIES.ShouldHideSelfFromEnemy, NewEnemy);

	//Istotna warto�� w kontek�cie tego czy Charakter mo�e atakowa� przeciwnik�w np. poprzez strzelanie. Dlaczego ta zmienna faktycznie jest taka istotna? 
	// G��wnie wynika to z tego �e dane na temat percepcji mo�na 'twardo' ustawi�, ca�kowicie omijaj�c SightPerception. To znaczy �e informacje o wrogu 
	// mog� by� kopiowana przez innych towarzyszy/kompan�w. Je�eli tak si� stanie to HumanAI zacznie strzela�, poniwa� tak b�dzie wynika� z warto�ci 
	// SightPerception. Taka sytuacja nast�puje kiedy przyk�adowo jeden z HumanAI zauwa�y wroga, co sposoduje uruchomienie systemu kt�ry poinformuje innych 
	// o zarejestrowanym wrogu. Sposoduje to skopiowanie warto�ci SightResult do pozosta�ych w pobli�u HumanAI. Jednak inni zaczn� strzela� dopiero w 
	// momencie faktycznej rejestracji wroga przez sight perception. Za to rorzu�nienie odpowiada w�a�nie ta zmienna.
	if (BlackboardData)
	{
		BlackboardData->SetValueAsBool("EnemyIsPhysicallySeen", true);
	}
	return true;
}


void USTE_HumanAI_SightPerceptionCore::GetSightPerceptionTagsFromCharacter_Implementation(bool& IsZombie, bool& EnemySpottedHim, bool& ShouldHideSelf, ACharacter* InEnemy)
{
	if (InEnemy->GetClass()->ImplementsInterface(UALS_HumanAI_InterfaceCpp::StaticClass()))
	{
		bool bSolidier = false; bool bZombie = false;
		HAINTERFACE::Execute_HAI_GetCharacterType(InEnemy, bSolidier, bZombie);

		bool bDetectedEnemy = false; ACharacter* EnemyActor = nullptr; float DetectionTime = 0;

		if (bZombie)
		{
			HAINTERFACE::Execute_HAI_GetControllerSmallValues(InEnemy, bDetectedEnemy, DetectionTime, EnemyActor);
			IsZombie = true;
			ShouldHideSelf = false;
			if (FVector::Distance(InEnemy->GetActorLocation(), Char->GetActorLocation()) > 600)
			{
				EnemySpottedHim = DetectionTime > 0.25 && EnemyActor == Char;
			}
			else
			{
				EnemySpottedHim = DetectionTime > 0.25;
			}
			return;
		}
		else if (InEnemy->IsPlayerControlled())
		{
			if (SimpleCheckDoesEnemySee(FRotator(0, InEnemy->GetControlRotation().Yaw, 0),
				HFL::GetPlayerCapsuleStartLocation(GetWorld(), InEnemy), TryGetHeadSocket(Char), 28, 2000, 220, -1) == true)
			{
				IsZombie = false;
				ShouldHideSelf = false;

				ECollisionChannel Channel = ECC_Visibility;
				TArray<AActor*> ActorsToIgnore; ActorsToIgnore.Add(Char); ActorsToIgnore.Add(InEnemy);
				FHitResult LineHitResult;

				const bool HitValid = KSL::LineTraceSingle(Char, TryGetHeadSocket(InEnemy), TryGetHeadSocket(Char), UEngineTypes::ConvertToTraceType(Channel),
					false, ActorsToIgnore, EDrawDebugTrace::None, LineHitResult, true, FColor::Blue, FColor::Red, 0.2);

				EnemySpottedHim = !HitValid;
				return;
			}
			else
			{
				IsZombie = false;
				EnemySpottedHim = false;
				ShouldHideSelf = false;
				return;
			}
		}
		else
		{
			HAINTERFACE::Execute_HAI_GetControllerSmallValues(InEnemy, bDetectedEnemy, DetectionTime, EnemyActor);
			IsZombie = false;
			ShouldHideSelf = false;
			EnemySpottedHim = DetectionTime > 0.25;
			return;
		}
	}
	IsZombie = false;
	EnemySpottedHim = true;
	ShouldHideSelf = false;
}


void USTE_HumanAI_SightPerceptionCore::UpdatePerFrameSightValues(float dt)
{
	//Then 00 read config parameters
	float Param01 = 0; float Param02 = 0; float Param03 = 0;
	GetSightParametersFromData(Param01, Param02, Param03);

	//Then 01
	if (DesiresSightState == AGLS_HumanAI_SightStatus::NotAnymoreSee) { DesiresSightState = AGLS_HumanAI_SightStatus::SeesNothing; }

	//Then 02
	if (DesiredSightDetectionValue > 0.5)
	{
		EnemyDetectionTime = KML::FClamp(EnemyDetectionTime + (dt * DesiredSightDetectionValue * GetWhenCurrentSeeInterpSpeed() * Param02), 0.0, 1.0);
		PotencialySeeSomething = EnemyDetectionTime > 0.5 && EnemyDetectionTime <= 0.98;
		
		SIGHTPROPERTIES.DetectedEnemy = EnemyDetectionTime > 0.98;
		if (EnemyDetectionTime > 0.98)
		{
			DesiresSightState = AGLS_HumanAI_SightStatus::ActiveSeeEnemy;
		}

		if (PotencialySeeSomething)
		{
			LastDetectionTime = EnemyDetectionTime;
		}
	}
	else
	{
		EnemyDetectionTime = KML::FClamp(EnemyDetectionTime + (dt * DesiredSightDetectionValue * GetWhenCurrentNOTSeeInterpSpeed() * Param03), 0.0, 1.0);

		if (PotencialySeeSomething)
		{
			DesiresSightState = AGLS_HumanAI_SightStatus::SawSomething;

			if (EnemyDetectionTime < 0.02)
			{
				PotencialySeeSomething = false;
				DesiresSightState = AGLS_HumanAI_SightStatus::SeesNothing;
			}
		}
		else
		{
			if (SIGHTPROPERTIES.DetectedEnemy == true)
			{
				SIGHTPROPERTIES.DetectedEnemy = EnemyDetectionTime > LostSightTimeTreshold;
				if (EnemyDetectionTime > LostSightTimeTreshold)
				{
					DesiresSightState = AGLS_HumanAI_SightStatus::ActiveSeeEnemy;
				}
				else
				{
					DesiresSightState = AGLS_HumanAI_SightStatus::LostSight;
				}
			}
		}
	}

	//Then 03
	if (DesiresSightState == AGLS_HumanAI_SightStatus::LostSight && EnemyDetectionTime < 0.02)
	{
		DesiresSightState = AGLS_HumanAI_SightStatus::NotAnymoreSee;
	}

	//Then 04
	SIGHTPROPERTIES.SightStatus = DesiresSightState;
	if ((DesiresSightState == AGLS_HumanAI_SightStatus::SawSomething && abs(LastDetectionTime - EnemyDetectionTime) < 0.06) 
		|| (DesiresSightState == AGLS_HumanAI_SightStatus::ActiveSeeEnemy && EnemyDetectionTime > LostSightPointUpdateTimeTreshold))
	{
		if (SIGHTPROPERTIES.EnemyCharacter)
		{
			SIGHTPROPERTIES.LostSightLocation = SIGHTPROPERTIES.EnemyCharacter->GetActorLocation();
			if (TargetFocusActor)
			{
				TargetFocusActor->SetActorLocation(SIGHTPROPERTIES.EnemyCharacter->GetActorLocation());
			}
		}
	}

	//Then 05
	if (BlackboardData)
	{
		BlackboardData->SetValueAsBool("IsCurrentlySeeEnemy", SIGHTPROPERTIES.DetectedEnemy);
		BlackboardData->SetValueAsFloat("SeeEnemyTime", EnemyDetectionTime);
	}
	SIGHTPROPERTIES.ReactionProgressTime = EnemyDetectionTime;

	//Then 06
	FName EnemySeeKey = "EnemySeeCheckerAlpha";
	if (SIGHTPROPERTIES.EnemySpottedHim == true && IsValid(BlackboardData) == true)
	{
		BlackboardData->SetValueAsFloat(EnemySeeKey, 1.0);
	}
	else if(IsValid(BlackboardData) == true)
	{
		BlackboardData->SetValueAsFloat(EnemySeeKey, KML::FClamp(BlackboardData->GetValueAsFloat(EnemySeeKey) - (dt * Param01), 0.0, 1.0));
	}

	//Then 07
	if (DesiresSightState == AGLS_HumanAI_SightStatus::SeesNothing && EnemyDetectionTime < 0.05)
	{
		if (SIGHTPROPERTIES.EnemyCharacter)
		{
			SIGHTPROPERTIES.EnemyCharacter = nullptr;
			SIGHTPROPERTIES.IsZombie = false;
			SIGHTPROPERTIES.EnemySpottedHim = false;
			SIGHTPROPERTIES.ShouldHideSelfFromEnemy = false;
		}
	}
}


float USTE_HumanAI_SightPerceptionCore::GetWhenCurrentSeeInterpSpeed_Implementation()
{
	if (!Char) { return 0.1; }

	float ByDistanceSpeedBias = 0.0;
	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (float DistTo = FVector::Distance(SIGHTPROPERTIES.EnemyCharacter->GetActorLocation(), Char->GetActorLocation()) < 300)
		{
			ByDistanceSpeedBias = KML::MapRangeClamped(DistTo, 30, 300, 3, 0);
		}
	}
	float ByAnimCurveBias = 0.0;
	if (Char->GetMesh()->GetAnimInstance())
	{
		ByAnimCurveBias = Char->GetMesh()->GetAnimInstance()->GetCurveValue("WhenSeeDetectionSpeedBias");
	}

	if (DeltaTimesWhenSeePerState.Num() == 0) { return 0.6 + ByDistanceSpeedBias + ByAnimCurveBias; }

	float* MapValue = DeltaTimesWhenSeePerState.Find(DesiresSightState);
	return *MapValue + ByDistanceSpeedBias + ByAnimCurveBias;
}


float USTE_HumanAI_SightPerceptionCore::GetWhenCurrentNOTSeeInterpSpeed_Implementation()
{
	if (!Char) { return 0.1; }

	float ByDistanceSpeedBias = 0.0;
	float ByDeadSpeedBias = 0.0;
	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (float DistTo = FVector::Distance(SIGHTPROPERTIES.EnemyCharacter->GetActorLocation(), Char->GetActorLocation()) > 300)
		{
			ByDistanceSpeedBias = KML::MapRangeClamped(DistTo, 300, 3000, 0, 0.5);
		}

		const bool EnemyIsDead = GetCharacterIsDead(SIGHTPROPERTIES.EnemyCharacter);
		if (EnemyIsDead) { ByDeadSpeedBias = 0.2; }
	}

	if (DeltaTimesWhenNotSeePerState.Num() == 0) { return 0.2 + ByDistanceSpeedBias; }

	float* MapValue = DeltaTimesWhenNotSeePerState.Find(DesiresSightState);
	return KML::FClamp(*MapValue + ByDistanceSpeedBias + ByDeadSpeedBias, 0.005, 2.0);
}


void USTE_HumanAI_SightPerceptionCore::GetSightParametersFromData_Implementation(float& EnemySpottedEnemyReductionSpeed, float& SeeReactionIncreaseSpeed, float& SeeReactionReduceSpeed)
{
	EnemySpottedEnemyReductionSpeed = 0.2;
	SeeReactionIncreaseSpeed = 1;
	SeeReactionReduceSpeed = 1;
}


void USTE_HumanAI_SightPerceptionCore::DrawDebugAboutResultOnTick()
{
	if (DrawDebugMode == 0) return;

	DrawDebugString(GetWorld(), GetCorrectDebugLocation(0), "Status: " + UEnum::GetValueAsString(DesiresSightState), nullptr, 
		KML::SelectColor(KML::Conv_ColorToLinearColor(FColor::Red), KML::Conv_ColorToLinearColor(FColor::Cyan), DesiresSightState == AGLS_HumanAI_SightStatus::ActiveSeeEnemy).ToFColor(false), 0, false, DrawingStringFontScale);

	DrawDebugString(GetWorld(), GetCorrectDebugLocation(1), "DetectionTime: " + FString::SanitizeFloat(SIGHTPROPERTIES.ReactionProgressTime), nullptr,
		KML::LinearColorLerpUsingHSV(KML::Conv_ColorToLinearColor(FColor::Emerald), KML::Conv_ColorToLinearColor(FColor::Orange), SIGHTPROPERTIES.ReactionProgressTime).ToFColor(true), 0, false, DrawingStringFontScale);

	DrawDebugString(GetWorld(), GetCorrectDebugLocation(2), "Delta When NOT See: " + FString::SanitizeFloat(GetWhenCurrentNOTSeeInterpSpeed()), nullptr,
		FColor::Green, 0, false, DrawingStringFontScale);

	DrawDebugString(GetWorld(), GetCorrectDebugLocation(3), "Desired Sight: " + FString::SanitizeFloat(DesiredSightDetectionValue), nullptr,
		KML::SelectColor(KML::Conv_ColorToLinearColor(FColor::Black), KML::Conv_ColorToLinearColor(FColor::White), DesiredSightDetectionValue < -0.1).ToFColor(false), 0, false, DrawingStringFontScale);

	if (SIGHTPROPERTIES.EnemyCharacter)
	{
		if (GetCharacterIsDead(SIGHTPROPERTIES.EnemyCharacter) == true)
		{
			DrawDebugString(GetWorld(), GetCorrectDebugLocation(4), "Current Is DEAD", nullptr,
				FColor(170, 0, 0, 255), 0, false, DrawingStringFontScale);
		}
	}

	if (DesiresSightState == AGLS_HumanAI_SightStatus::ActiveSeeEnemy && IsValid(SIGHTPROPERTIES.EnemyCharacter))
	{
		DrawDebugSphere(GetWorld(), SIGHTPROPERTIES.EnemyCharacter->GetActorLocation(), 20, 8, FColor(200, 40, 0, 150), false, 0, DrawingShapesDepth, 1.4);
		DrawDebugLine(GetWorld(), Char->GetActorLocation(), SIGHTPROPERTIES.LostSightLocation, FColor(200, 40, 0, 30), false, 0, DrawingShapesDepth, 1.2);
	}
	else if (IsValid(SIGHTPROPERTIES.EnemyCharacter) || KML::NotEqual_VectorVector(SIGHTPROPERTIES.LostSightLocation, FVector(0,0,0)) == true)
	{
		DrawDebugSphere(GetWorld(), SIGHTPROPERTIES.LostSightLocation, 20, 8, FColor(200, 140, 0, 150), false, 0, DrawingShapesDepth, 1.4);
		DrawDebugLine(GetWorld(), Char->GetActorLocation(), SIGHTPROPERTIES.LostSightLocation, FColor(200, 140, 0, 30), false, 0, DrawingShapesDepth, 1.6);
	}

}


void USTE_HumanAI_SightPerceptionCore::DrawPerEnemyDebugInfo(ACharacter* InEnemy, float WeightDistance, float WeightPlayer, float WeightTags, float WeightSee, float Total)
{
#if WITH_EDITOR
	if (DrawDebugMode > 0 && IsValid(InEnemy))
	{
		DrawDebugWeightInfo(InEnemy, WeightDistance, 60, 25, "WeightDistance", 0.1, 1.0, 0);
		DrawDebugWeightInfo(InEnemy, WeightPlayer, 54, 25, "WeightIsPlayer", 1.0, 1.2, 0);
		DrawDebugWeightInfo(InEnemy, WeightTags, 48, 25, "WeightHasTags", 0.0, 1.0, 0);
		DrawDebugWeightInfo(InEnemy, WeightSee, 42, 25, "WeightEnemySee", 0.0, 1.0, 0);
		DrawDebugWeightInfo(InEnemy, Total, 36, 25, "Weight TOTAL", 0.0, 1.0, 1);
	}
#endif // WITH_EDITOR
}


void USTE_HumanAI_SightPerceptionCore::ForceSetPropertiesIgnoringSightComponent_Implementation(ACharacter* SourceActor, AGLS_HumanAI_MainBehaviorMode SourceBehaviorMode, 
	AGLS_HumanAI_FightingMode SourceFightingMode, FAGLS_HumanAI_EnemyTags SourceSightProperties, float WaitTime, float Chance)
{
	if (TimerToForceSetParams > -0.5) { return; }

	if (WaitTime <= 0.0)
	{
		WaitOnForceSetParams = 0.05;
		TimerToForceSetParams = 0.05;
		CachedSightParams = SourceSightProperties;
		CachedSightStatus = AGLS_HumanAI_SightStatus::ActiveSeeEnemy;
	}
	else
	{
		WaitOnForceSetParams = WaitTime;
		TimerToForceSetParams = WaitTime;
		CachedSightParams = SourceSightProperties;
		CachedSightStatus = AGLS_HumanAI_SightStatus::ActiveSeeEnemy;
	}

	if (DrawDebugMode > 0)
	{
		DrawDebugString(GetWorld(), Char->GetActorLocation(), "FORCE SET PARAMS", nullptr, FColor::Purple, 1.5, true, 1.4);
	}
}


void USTE_HumanAI_SightPerceptionCore::ForceSetSightEnemyParams(FAGLS_HumanAI_EnemyTags InSightParams, AGLS_HumanAI_SightStatus RequiredSightStatus, bool bEnemySpottedHim)
{
	if (!InSightParams.EnemyCharacter) return;

	DesiresSightState = RequiredSightStatus;
	DesiredSightDetectionValue = 1;
	EnemyDetectionTime = 0.99;
	PotencialySeeSomething = true;

	SIGHTPROPERTIES.DetectedEnemy = true;
	SIGHTPROPERTIES.SightStatus = RequiredSightStatus;
	SIGHTPROPERTIES.EnemyCharacter = InSightParams.EnemyCharacter;
	SIGHTPROPERTIES.ReactionProgressTime = 0.99;
	SIGHTPROPERTIES.LostSightLocation = InSightParams.EnemyCharacter->GetActorLocation();
	GetSightPerceptionTagsFromCharacter(SIGHTPROPERTIES.IsZombie, SIGHTPROPERTIES.EnemySpottedHim, SIGHTPROPERTIES.ShouldHideSelfFromEnemy, InSightParams.EnemyCharacter);
	SIGHTPROPERTIES.EnemySpottedHim = bEnemySpottedHim;

	if (BlackboardData)
	{
		BlackboardData->SetValueAsFloat("EnemySeeCheckerAlpha", KML::SelectFloat(1, 0, bEnemySpottedHim));
		BlackboardData->SetValueAsFloat("SeeEnemyTime", 0.99);
		if (DesiresSightState == AGLS_HumanAI_SightStatus::ActiveSeeEnemy)
		{
			BlackboardData->SetValueAsBool("IsCurrentlySeeEnemy", true);
		}
	}
}


bool USTE_HumanAI_SightPerceptionCore::SimpleCheckDoesEnemySee(const FRotator& CenterAngle, const FVector& StartPosition, const FVector& PointToCheck, float HalfAngle, float Radius, float Height, float DrawDebug)
{
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
				return true;
			}
		}
	}
	return false;
}


FVector USTE_HumanAI_SightPerceptionCore::TryGetHeadSocket(ACharacter* InCharacter)
{
	if (InCharacter)
	{
		if (InCharacter->GetMesh()->DoesSocketExist("head"))
		{
			return InCharacter->GetMesh()->GetSocketLocation("head");
		}
		else
		{
			return InCharacter->GetActorLocation() + FVector(0, 0, 40);
		}
	}
	return FVector(-1, -1, -1);
}


void USTE_HumanAI_SightPerceptionCore::DrawDebugWeightInfo(AActor* CurrentActor, float WeightValue, float OffsetUp, float OffsetRight, FString WeightDescription, float LerpRangeMin, float LerpRangeMax, int ColorPattern)
{
	AActor* CameraActor = GAME::GetPlayerCameraManager(GetWorld(), 0);
	if (!CameraActor) return;

	float ScaleBias = KML::MapRangeClamped(FVector::Distance(CurrentActor->GetActorLocation(), CameraActor->GetActorLocation()), 300, 2000, 1, 3);
	const FVector TextLoc = CurrentActor->GetActorLocation() + (CameraActor->GetActorUpVector() * OffsetUp * ScaleBias) + (CameraActor->GetActorRightVector() * OffsetRight);
	const FString FinalText = WeightDescription + ": " + FString::SanitizeFloat(WeightValue);
	FLinearColor FinalColor;

	if (ColorPattern == 1)
	{
		FinalColor = KML::LinearColorLerpUsingHSV(KML::Conv_ColorToLinearColor(FColor::Blue), KML::Conv_ColorToLinearColor(FColor::Green), KML::MapRangeClamped(WeightValue, LerpRangeMin, LerpRangeMax, 0.0, 1.0));
	}
	else
	{
		FinalColor = KML::LinearColorLerpUsingHSV(KML::Conv_ColorToLinearColor(FColor::Red), KML::Conv_ColorToLinearColor(FColor::Emerald), KML::MapRangeClamped(WeightValue, LerpRangeMin, LerpRangeMax, 0.0, 1.0));
	}
	DrawDebugString(GetWorld(), TextLoc, FinalText, nullptr, FinalColor.ToFColor(true), CurrentTickInterval, true, DrawingStringFontScale);
	DrawDebugLine(GetWorld(), TextLoc - (CameraActor->GetActorUpVector() * 0.8), CurrentActor->GetActorLocation(), FinalColor.ToFColor(true), false, CurrentTickInterval, KML::SelectInt(2, 0, DrawingShapesDepth > 0), 0.8);
}


FVector USTE_HumanAI_SightPerceptionCore::GetCorrectDebugLocation(int PositionIndex, float BiasZ)
{
	APlayerCameraManager* PlayerCam = GAME::GetPlayerCameraManager(GetWorld(), 0);

	if (!PlayerCam)
	{
		return Char->GetActorLocation() + FVector(0, 0, BiasZ * (float)PositionIndex);
	}

	const float OffsetScale = KML::MapRangeClamped(FVector::Distance(PlayerCam->GetCameraLocation(), Char->GetActorLocation()), 10, 6000, 0, 16);
	return Char->GetActorLocation() + FVector(0, 0, BiasZ) - FVector(0,0, ((float)PositionIndex * 6 * OffsetScale));
}


void USTE_HumanAI_SightPerceptionCore::DrawFailedPerceivedActorInfo(ACharacter* InCharacter, FString t, int ColorIndex)
{
	FColor C = FColor::Red;
	if (ColorIndex == 1) FColor(200, 100, 0, 100);
	else if(ColorIndex == 2) FColor(50, 220, 0, 100);

	if (DrawDebugMode == 2)
	{
		DrawDebugCrosshairs(GetWorld(), InCharacter->GetActorLocation(), InCharacter->GetActorRotation(), 55, C, false, 0.2, DrawingShapesDepth);
		DrawDebugString(GetWorld(), InCharacter->GetActorLocation(), t, nullptr, C, 0.2, true, 0.95);
	}
}
