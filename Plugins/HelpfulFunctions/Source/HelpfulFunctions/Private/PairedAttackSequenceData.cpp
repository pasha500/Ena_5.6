


#include "PairedAttackSequenceData.h"
#include "Components/ArrowComponent.h"

// Sets default values
APairedAttackSequenceData::APairedAttackSequenceData()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bAsyncPhysicsTickEnabled = false;
	SetActorTickEnabled(false);
	//bIsSpatiallyLoaded = false;
	SetCanBeDamaged(false);
	bFindCameraComponentWhenViewTarget = false;

}

// Called when the game starts or when spawned
void APairedAttackSequenceData::BeginPlay()
{
	Super::BeginPlay();

}


#if WITH_EDITOR
void APairedAttackSequenceData::SetupAnimationPreview(USkeletalMesh* Mesh, UAnimMontage* InitMontage, UAnimSequence* StruggleSequence, TSoftObjectPtr<UAnimMontage> SuccessMontage, TSoftObjectPtr<UAnimMontage> FailedMontage, FVector ComOffset, FRotator CompRot, FName ObjectName)
{
	if (!bGeneratePreview || !Mesh)
	{
		USkeletalMeshComponent* PreviewSkeletalMesh = Cast<USkeletalMeshComponent>(GetDefaultSubobjectByName(ObjectName));
		if (PreviewSkeletalMesh)
		{
			PreviewSkeletalMesh->DestroyComponent();
		}
		return;
	}

	UAnimSequenceBase* AnimToPreview = nullptr;
	float SeqTime = 1.0;

	switch (AnimsPreviewMode)
	{
	case PairedAttackSeqPreviewMode::Initialize:
		if (InitMontage)
		{
			AnimToPreview = InitMontage;
			SeqTime = InitMontage->GetFirstAnimReference()->GetPlayLength();
		}
		break;

	case PairedAttackSeqPreviewMode::Success:
		if (SuccessMontage.IsValid())
		{
			AnimToPreview = SuccessMontage.Get();
			SeqTime = SuccessMontage->GetFirstAnimReference()->GetPlayLength();
		}
		else if (SuccessMontage.IsPending())
		{
			SuccessMontage.LoadSynchronous();
			AnimToPreview = SuccessMontage.Get();
			SeqTime = SuccessMontage->GetFirstAnimReference()->GetPlayLength();
		}
		break;

	case PairedAttackSeqPreviewMode::Failed:
		if (FailedMontage.IsValid())
		{
			AnimToPreview = FailedMontage.Get();
			SeqTime = FailedMontage->GetFirstAnimReference()->GetPlayLength();
		}
		else if (FailedMontage.IsPending())
		{
			FailedMontage.LoadSynchronous();
			AnimToPreview = FailedMontage.Get();
			SeqTime = FailedMontage->GetFirstAnimReference()->GetPlayLength();
		}
		break;

	case PairedAttackSeqPreviewMode::Struggle:
		if (StruggleSequence)
		{
			AnimToPreview = StruggleSequence;
			SeqTime = StruggleSequence->GetPlayLength();
		}
		break;
	default:
		break;
	}

	if (!AnimToPreview)
	{
		USkeletalMeshComponent* PreviewSkeletalMesh = Cast<USkeletalMeshComponent>(GetDefaultSubobjectByName(ObjectName));
		if (PreviewSkeletalMesh)
		{
			PreviewSkeletalMesh->DestroyComponent();
		}
		return;
	}
	else if(Mesh->GetSkeleton() != AnimToPreview->GetSkeleton())
	{
		return;
	}

	//USceneComponent* SceneRoot = Cast<USceneComponent>(GetDefaultSubobjectByName("DefaultSceneRoot"));


	USkeletalMeshComponent* PreviewSkeletalMesh = Cast<USkeletalMeshComponent>(GetDefaultSubobjectByName(ObjectName));
	if (!PreviewSkeletalMesh)
	{
		PreviewSkeletalMesh = NewObject<USkeletalMeshComponent>(this, USkeletalMeshComponent::StaticClass(), ObjectName, RF_Transactional);
		//PreviewSkeletalMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

		PreviewSkeletalMesh->RegisterComponent();
		//AddInstanceComponent(PreviewSkeletalMesh); // ← dodanie do aktora
	}


	PreviewSkeletalMesh->SetSkeletalMesh(Mesh);
	PreviewSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	PreviewSkeletalMesh->SetRelativeLocation(ComOffset);
	PreviewSkeletalMesh->SetRelativeRotation(CompRot.Quaternion());


	if (AnimToPreview)
	{
		PreviewSkeletalMesh->AnimationData.AnimToPlay = AnimToPreview;
		PreviewSkeletalMesh->AnimationData.bSavedLooping = false;
		PreviewSkeletalMesh->AnimationData.bSavedPlaying = false;
		PreviewSkeletalMesh->AnimationData.SavedPosition = FMath::GetMappedRangeValueClamped(FVector2D(0.0, 1.0), FVector2D(0.0, SeqTime), AnimNormalizedTime);
	}
}


void APairedAttackSequenceData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	//Super::PostEditChangeChainProperty();

	if (bGeneratePreview)
	{
		FTransform OffsetsAtt = FTransform::Identity;
		FTransform OffsetsVic = FTransform::Identity;
		if (bVicCharacterIsRoot)
		{
			OffsetsAtt = FTransform(ConstGlobalRotationDelta, ConstGlobalRootsOffset);
		}
		else
		{
			OffsetsVic = FTransform(ConstGlobalRotationDelta, ConstGlobalRootsOffset);
		}

		SetupAnimationPreview(SkeletalMeshAtt, MontageInitializeAtt, AnimStruggleDynamicTimeAtt, SoftMontageSuccessAtt, SoftMontageFailedAtt, OffsetsAtt.GetLocation(), OffsetsAtt.Rotator(), "AttPreview01");

		SetupAnimationPreview(SkeletalMeshVic, MontageInitializeVic, AnimStruggleDynamicTimeVic, SoftMontageSuccessVic, SoftMontageFailedVic, OffsetsVic.GetLocation(), OffsetsVic.Rotator(), "VicPreview01");



		UArrowComponent* ArrowComp = Cast<UArrowComponent>(GetDefaultSubobjectByName(TEXT("PreviewArrow")));
		if (!ArrowComp)
		{
			ArrowComp = NewObject<UArrowComponent>(this, UArrowComponent::StaticClass(), TEXT("PreviewArrow"), RF_Transactional);
			if (ArrowComp)
			{
				ArrowComp->RegisterComponent();
				//ArrowComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

				// Opcjonalnie konfiguracja:
				ArrowComp->ArrowSize = 0.8f;
				ArrowComp->ArrowColor = FColor::Cyan;
				ArrowComp->ArrowLength = 40;
				ArrowComp->bHiddenInGame = true; // tylko w edytorze
			}
		}
		else
		{
			const FVector CameraDesiredPosition = FMath::Lerp<FVector>(CameraDesiredTransformStart.GetLocation(), CameraDesiredTransformEnd.GetLocation(), AnimNormalizedTime);
			const FRotator CameraDesiredRotation = FMath::Lerp<FRotator>(CameraDesiredTransformStart.Rotator(), CameraDesiredTransformEnd.Rotator(), AnimNormalizedTime);
			ArrowComp->SetRelativeLocationAndRotation(CameraDesiredPosition, CameraDesiredRotation, false);

		}


	}
	else
	{
		UArrowComponent* ArrowComp = Cast<UArrowComponent>(GetDefaultSubobjectByName(TEXT("PreviewArrow")));
		if (ArrowComp)
		{
			ArrowComp->DestroyComponent();
		}
	}

}
#endif


// Called every frame
void APairedAttackSequenceData::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


