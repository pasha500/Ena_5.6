


#include "AGLS_Human_ControllerCoreAI.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/CapsuleComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

#define KML UKismetMathLibrary
#define PAWN ControlledCharacter
#define CONTROLROTATION_DEBUGSHAPES DrawDebugRotation
#define AIMACRO UAIBlueprintHelperLibrary
#define KSL UKismetSystemLibrary

const AGLS_HumanAI_MainBehaviorMode AAGLS_Human_ControllerCoreAI::BB_GetMainBehaviorMode()
{
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        // Pobierz wartość jako int/uint8
        uint8 Value = BB->GetValueAsEnum(TEXT("MainBehaviorMode"));
        // Rzutuj na Twój enum
        return static_cast<AGLS_HumanAI_MainBehaviorMode>(Value);
    }
    // Jeśli Blackboard nie istnieje → zwróć None
    return AGLS_HumanAI_MainBehaviorMode::None;
}


const AGLS_HumanAI_PatrolingMode AAGLS_Human_ControllerCoreAI::BB_GetPatrolingMode()
{
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        // Pobierz wartość jako int/uint8
        uint8 Value = BB->GetValueAsEnum(TEXT("PatrolingMode"));
        // Rzutuj na Twój enum
        return static_cast<AGLS_HumanAI_PatrolingMode>(Value);
    }
    // Jeśli Blackboard nie istnieje → zwróć None
    return AGLS_HumanAI_PatrolingMode::None;
}


const AGLS_HumanAI_FightingMode AAGLS_Human_ControllerCoreAI::BB_GetFightingMode()
{
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        // Pobierz wartość jako int/uint8
        uint8 Value = BB->GetValueAsEnum(TEXT("FightingMode"));
        // Rzutuj na Twój enum
        return static_cast<AGLS_HumanAI_FightingMode>(Value);
    }
    // Jeśli Blackboard nie istnieje → zwróć None
    return AGLS_HumanAI_FightingMode::None;
}


void AAGLS_Human_ControllerCoreAI::BB_SetMainBehaviorMode(AGLS_HumanAI_MainBehaviorMode NewValue)
{
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        BB->SetValueAsEnum(TEXT("MainBehaviorMode"), static_cast<uint8>(NewValue));
    }
}

bool AAGLS_Human_ControllerCoreAI::ST_SetParameterAsObject(UStateTreeAIComponent* InComponent, const FName ParameterName, UObject* ObjectToSet)
{
    if (!InComponent || ParameterName.IsNone())
        return false;

    // 1) Dobierz się do chronionego StateTreeRef (UPROPERTY) przez refleksję
    FStructProperty* StateTreeRefProp =
        FindFProperty<FStructProperty>(UStateTreeComponent::StaticClass(), TEXT("StateTreeRef"));
    if (!StateTreeRefProp)
        return false;

    FStateTreeReference* Ref = StateTreeRefProp->ContainerPtrToValuePtr<FStateTreeReference>(InComponent);
    if (!Ref || !Ref->IsValid())
        return false;

    // 2) Weź workowalny Property Bag z referencji
    //    (w UE5.6 w referencji jest UPROPERTY o nazwie "Parameters")
    FStructProperty* ParamsProp =
        FindFProperty<FStructProperty>(FStateTreeReference::StaticStruct(), TEXT("Parameters"));
    if (!ParamsProp)
        return false;

    FInstancedPropertyBag* Params = ParamsProp->ContainerPtrToValuePtr<FInstancedPropertyBag>(Ref);
    if (!Params)
        return false;

    // 3) Znajdź deskryptor parametru po nazwie i ustaw wartość jako UObject*
    const FPropertyBagPropertyDesc* Desc = Params->FindPropertyDescByName(ParameterName);
    if (!Desc)
        return false;                 // brak takiego parametru w assetcie

    const EPropertyBagResult Res = Params->SetValueObject(*Desc, ObjectToSet);
    return Res == EPropertyBagResult::Success;
}



bool AAGLS_Human_ControllerCoreAI::DoesPathUseNavLink(UNavigationPath* Path, float MaxDistanceToPoint2D, float MaxHeightDiff)
{
    if (!Path || Path->PathPoints.Num() < 2)
        return false;

    for (int i = 0; i < Path->PathPoints.Num() - 1; i++)
    {
        const FVector P1 = Path->PathPoints[i];
        const FVector P2 = Path->PathPoints[i + 1];

        if (FVector().DistXY(P1, P2) < MaxDistanceToPoint2D && abs(P1.Z - P2.Z) > MaxHeightDiff)
        {
            return true;
        }
    }
    return false;
}


float AAGLS_Human_ControllerCoreAI::PathWeightByNavLinksNumber(UNavigationPath* Path, float MaxDistanceToPoint2D, float MaxHeightDiff, float Bias, bool UseAbsOnHeight)
{
    if (!Path || Path->PathPoints.Num() < 2)
        return 1.0;

    float WeightValue = 1.0;

    for (int i = 0; i < Path->PathPoints.Num() - 1; i++)
    {
        const FVector P1 = Path->PathPoints[i];
        const FVector P2 = Path->PathPoints[i + 1];

        float HeightValue = P2.Z - P1.Z;
        if (UseAbsOnHeight) HeightValue = abs(HeightValue);

        if (FVector().DistXY(P1, P2) < MaxDistanceToPoint2D && HeightValue > MaxHeightDiff)
        {
            if (WeightValue > 0.0)
            {
                WeightValue = WeightValue - Bias;
            }
            else
            {
                return WeightValue;
            }
        }
    }
    return WeightValue;
}


//Override Default Control Rotation FUNCTION
void AAGLS_Human_ControllerCoreAI::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
    if (bUseCustomControlRotationCode)
    {
        CustomUpdateControlRotation(DeltaTime, bUpdatePawn);
        return;
    }
    Super::UpdateControlRotation(DeltaTime, bUpdatePawn);
}


void AAGLS_Human_ControllerCoreAI::CustomUpdateControlRotation_Implementation(float DeltaTime, bool bUpdatePawn)
{
    APawn* const MyPawn = GetPawn();
    if (MyPawn)
    {
        TArray<FName> ShootingBonesNames;
        ShootingBonesNames.Add("head"); ShootingBonesNames.Add("spine_01"); ShootingBonesNames.Add("spine_02");
        AdvancedControlRotationForHumanAI(DeltaTime, false, ShootingBonesNames, "BulletStart", 0.25, 0.0, "DesiredSmartObjectRotation");
    }
}

void AAGLS_Human_ControllerCoreAI::AdvancedControlRotationForHumanAI(
    float DeltaTime, 
    bool bUpdatePawn, 
    TArray<FName> ShootingTargetsBonesName, 
    FName GunMuzzleSocketName, 
    float ShotDirectionIncludeVelocityAlpha, 
    float BlendTraceStartWithWeaponMuzzle, 
    FName SmartObjectRotationKey, 
    float SpeedWhenGoToSmartObject, 
    float SpeedWhenFocalPoint, 
    float SpeedWhenBumpReaction, 
    float SpeedWhenFocusOnEnemy, 
    float SpeedWhenFocusActor,
    float SpeedWhenRecivedDamage,
    FVector2D SpeedRangeWhenDefault
)
{
    const FName HeadSocket = TEXT("head");
    const bool HaveInterface = ControlledCharacter->GetClass()->ImplementsInterface(UAGLS_AI_CharacterInterface::StaticClass());

    bool bIsLocked = false;
    if (HaveInterface) { IAGLS_AI_CharacterInterface::Execute_BPI_AI_Get_RotationLocked(ControlledCharacter, bIsLocked); }

    if (bIsLocked)
    {
        SetControlRotation(KML::RInterpTo(GetControlRotation(), ControlledCharacter->GetActorRotation(), DeltaTime, 8.0));
        return;
    }

    int LocomotionIndex = 0; uint8 LocomotionByte = 0; FName LocomotionName = TEXT("Default");
    if (HaveInterface) { IAGLS_AI_CharacterInterface::Execute_BPI_AI_Get_LocomotionModeIndex(ControlledCharacter, LocomotionIndex, LocomotionByte, LocomotionName); }

    if (LocomotionIndex == 11 || LocomotionIndex == 12) { return; } //Dont Update Current Control Rotation

    // Set Default Control Rotation Mode
    FVector DesiredMoveDirection = FVector(1, 0, 0);
    switch (YawControlRotDesiredMode)
    {
    case EControllerAI_ControlRotMode::FromControlRotation:
        DesiredMoveDirection = GetControlRotation().Vector();
        break;
    case EControllerAI_ControlRotMode::FromNotZeroVelocity:
        DesiredMoveDirection = NonZeroVelocityDirection;
        break;
    case EControllerAI_ControlRotMode::FromCurrentAcceleration:
        DesiredMoveDirection = ControlledCharacter->GetCharacterMovement()->GetCurrentAcceleration();
        DesiredMoveDirection.Normalize();
        break;
    case EControllerAI_ControlRotMode::FromFocusPoint:
        break;
    case EControllerAI_ControlRotMode::FromCustomDefinition:
        DesiredMoveDirection = CustomDefaultControlRotDirection();
        break;
    }

    //Main VARIABLE
    FRotator DesiredControlRotation = KML::MakeRotFromX(DesiredMoveDirection);
    float DesiredInterpSpeed = KML::MapRangeClamped(DefaultControlInterpSpeedScale, 0.0, 1.0, SpeedRangeWhenDefault.X, SpeedRangeWhenDefault.Y);
    bool ShouldSKipUpdate = true; FRotator ControlAsHead = FRotator(0, 0, 0);

    switch (BB_GetMainBehaviorMode())
    {
    case AGLS_HumanAI_MainBehaviorMode::Patroling: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
        // 1) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Patroling
        if (BB_GetPatrolingMode() == AGLS_HumanAI_PatrolingMode::NoMoveAndStand || BB_GetPatrolingMode() == AGLS_HumanAI_PatrolingMode::NoMoveAndSit)
        {
            DesiredControlRotation = GetBlackboardComponent()->GetValueAsRotator(SmartObjectRotationKey);
            DesiredInterpSpeed = SpeedWhenGoToSmartObject;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Look At Smart Object"), FColor::Purple);
        }
        // 2) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Patroling
        if (BB_GetPatrolingMode() == AGLS_HumanAI_PatrolingMode::Interacting || BB_GetPatrolingMode() == AGLS_HumanAI_PatrolingMode::None)
        {
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Skip Updating"), FColor::Black);
            return;
        }
        // 3) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Patroling
        if (KML::NotEqual_VectorVector(FocalPointOnDamageCauser, FVector(0, 0, 0), 5.0) == true)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), FocalPointOnDamageCauser);
            DesiredInterpSpeed = SpeedWhenRecivedDamage;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Focus On DAMAGE Causer"), FColor::Orange);
        }
        // 4) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Patroling
        if (KML::NotEqual_VectorVector(FocalPointBumpReaction, FVector(0, 0, 0), 5.0) == true)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), FocalPointBumpReaction);
            DesiredInterpSpeed = SpeedWhenBumpReaction;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Focus On Bump Reaction"), FColor::Blue);
        }
        // 5) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Patroling
        if (ControlRotationAsSelfHeadSocket(ShouldSKipUpdate, ControlAsHead, DesiredControlRotation) == true)
        {
            if (ShouldSKipUpdate == false) { DesiredControlRotation = ControlAsHead; DesiredInterpSpeed = 8.0; }
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Head Bone"), FColor::Cyan);
        }

        break;
    case AGLS_HumanAI_MainBehaviorMode::Finding: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
        // 1) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Finding
        if (KML::NotEqual_VectorVector(GetCustomFocalPoint(), FVector(0, 0, 0), 5.0) == true && FVector::Distance(GetCustomFocalPoint(), ControlledCharacter->GetActorLocation()) < 5000)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), GetCustomFocalPoint());
            DesiredInterpSpeed = SpeedWhenFocalPoint;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Focal Point"), FColor::Yellow);
        }
        // 2) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Finding
        if (KML::NotEqual_VectorVector(FocalPointBumpReaction, FVector(0, 0, 0), 5.0) == true)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), FocalPointBumpReaction);
            DesiredInterpSpeed = SpeedWhenBumpReaction;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Focus On Bump Reaction"), FColor::Blue);
        }
        // 3) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Finding
        if (ControlRotationAsSelfHeadSocket(ShouldSKipUpdate, ControlAsHead, DesiredControlRotation) == true)
        {
            if (ShouldSKipUpdate == false) { DesiredControlRotation = ControlAsHead; DesiredInterpSpeed = 8.0; }
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Head Bone"), FColor::Cyan);
        }
        // 4) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Finding
        if (KML::NotEqual_VectorVector(FocalPointOnDamageCauser, FVector(0, 0, 0), 5.0) == true)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), FocalPointOnDamageCauser);
            DesiredInterpSpeed = SpeedWhenRecivedDamage;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Focus On DAMAGE Causer"), FColor::Orange);
        }
        // 5) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Finding
        if (GetFocusActor())
        {
            DesiredControlRotation = ControlRotationAsAimInEnemyHead(ShootingTargetsBonesName, GunMuzzleSocketName, HeadSocket, ShotDirectionIncludeVelocityAlpha, BlendTraceStartWithWeaponMuzzle);
            DesiredInterpSpeed = KML::MapRangeClamped(FVector::Distance(ControlledCharacter->GetActorLocation(), GetFocusActor()->GetActorLocation()), 300.0, 1600.0, 10.0, 4.0) * SpeedWhenFocusOnEnemy;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Look At ENEMY Mesh"), FColor::Red);
        }
        break;
    case AGLS_HumanAI_MainBehaviorMode::Fighting: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
        // 1) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Fighting
        if (KML::NotEqual_VectorVector(GetCustomFocalPoint(), FVector(0, 0, 0), 5.0) == true && FVector::Distance(GetCustomFocalPoint(), ControlledCharacter->GetActorLocation()) < 5000)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), GetCustomFocalPoint());
            DesiredInterpSpeed = SpeedWhenFocalPoint;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Focal Point"), FColor::Yellow);
        }
        // 2) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Fighting
        if (KML::NotEqual_VectorVector(FocalPointOnDamageCauser, FVector(0, 0, 0), 5.0) == true)
        {
            DesiredControlRotation = KML::FindLookAtRotation(ControlledCharacter->GetActorLocation(), FocalPointOnDamageCauser);
            DesiredInterpSpeed = SpeedWhenRecivedDamage;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Focus On DAMAGE Causer"), FColor::Orange);
        }
        // 3) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Fighting
        if (ControlRotationAsSelfHeadSocket(ShouldSKipUpdate, ControlAsHead, DesiredControlRotation) == true)
        {
            if (ShouldSKipUpdate == false) { DesiredControlRotation = ControlAsHead; DesiredInterpSpeed = 8.0; }
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Head Bone"), FColor::Cyan);
        }
        // 4) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Fighting
        if (GetFocusActor())
        {
            DesiredControlRotation = ControlRotationAsAimInEnemyHead(ShootingTargetsBonesName, GunMuzzleSocketName, HeadSocket, ShotDirectionIncludeVelocityAlpha, BlendTraceStartWithWeaponMuzzle);
            DesiredInterpSpeed = KML::MapRangeClamped(FVector::Distance(ControlledCharacter->GetActorLocation(), GetFocusActor()->GetActorLocation()), 300.0, 1600.0, 10.0, 4.0) * SpeedWhenFocusOnEnemy;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Look At ENEMY Mesh"), FColor::Red);
        }
        break;
    case AGLS_HumanAI_MainBehaviorMode::Running:
        break;
    case AGLS_HumanAI_MainBehaviorMode::Interacting: //░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
        // 1) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Interacting
        if (GetFocusActor())
        {
            DesiredControlRotation = ControlRotationOnFocusActor(DesiredControlRotation);
            DesiredInterpSpeed = 8.0;
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Simple Look At Focus Actor"), FColor::Emerald);
        }
        // 2) ▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂▂ Interacting
        if (ControlRotationAsSelfHeadSocket(ShouldSKipUpdate, ControlAsHead, DesiredControlRotation) == true)
        {
            if (ShouldSKipUpdate == false) { DesiredControlRotation = ControlAsHead; DesiredInterpSpeed = 8.0; }
            CONTROLROTATION_DEBUGSHAPES(DesiredControlRotation, TEXT("Rotation From Head Bone"), FColor::Cyan);
        }
        break;
    case AGLS_HumanAI_MainBehaviorMode::None:
        break;
    }

    //Timer UPDATE
    if (TimeToSetNewRandomBone > 0.0)
    { TimeToSetNewRandomBone = KML::FClamp(TimeToSetNewRandomBone - DeltaTime, 0.0, 10.0); }
    else if (TimeToSetNewRandomBone <= 0.01)
    { TimeToSetNewRandomBone = -1; }

    // FINAL ROTATION UPDATE WITH INTERPOLATION
    SetControlRotation(KML::RInterpTo(GetControlRotation(), DesiredControlRotation, DeltaTime, DesiredInterpSpeed));
    return;
}


FVector AAGLS_Human_ControllerCoreAI::CustomDefaultControlRotDirection_Implementation()
{
    return FVector(1, 0, 0);
}


/*This function is designed to change the current direction of movement so that characters can avoid each other. The need
for this solution stems from the fact that, by default, PathFollowing doesn't account for dynamic obstacles in the path
such as other characters. This can be avoided using RuntimeNavMeshModify, but it seems to be a more expensive solution.
This function forces the controlled Character to change its movement direction via AddMovementInput when a Character is
in the path. Note that this function is not responsive and is designed to work only with HumanAI.*/
bool AAGLS_Human_ControllerCoreAI::TryToAvoidOtherCharacter_Implementation(UPARAM(ref)FVector& FollowDirection, float TraceRadiusScale, float TraceLenghtOffset, float PerGaitInputStrength, bool DrawTrace)
{
    if (!AIMACRO::GetCurrentPath(this)) 
    {
        //GEngine->AddOnScreenDebugMessage(-1, 0, FColor::Red, TEXT("TryToAvoidOtherCharacter - Failed becouse Path is Not VALID"));
        FollowDirection = FVector(0, 0, 0); return false;
    }

    if ((BB_GetMainBehaviorMode() != AGLS_HumanAI_MainBehaviorMode::Patroling && BB_GetMainBehaviorMode() != AGLS_HumanAI_MainBehaviorMode::Finding) || ControlledCharacter->HasAnyRootMotion() == true)
    {
        //GEngine->AddOnScreenDebugMessage(-1, 0, FColor::Red, TEXT("TryToAvoidOtherCharacter - Failed becouse States or root motion is not matching"));
        FollowDirection = FVector(0, 0, 0); return false;
    }

    FVector DesiredMoveInput = FVector(0, 0, 0);
    FVector NextPathPoint = AIMACRO::GetCurrentPathPoints(this)[KML::Clamp(AIMACRO::GetCurrentPathIndex(this) + 1, 0, AIMACRO::GetCurrentPathPoints(this).Num() - 1)];
    DesiredMoveInput = NextPathPoint - AIMACRO::GetCurrentPathPoints(this)[AIMACRO::GetCurrentPathIndex(this)];
    DesiredMoveInput.Normalize();

    if (FollowDirection.Length() > 0.5)
    {
        DesiredMoveInput = FollowDirection;
    }

    //Prepare Trace
    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
    ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn)); // tylko Pawn

    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(ControlledCharacter);

    EDrawDebugTrace::Type DebugTrace = EDrawDebugTrace::None;
    if (DrawTrace == true) { DebugTrace = EDrawDebugTrace::ForOneFrame; }
    FHitResult TraceResult;

    const float TraceLenght = (ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2) + TraceLenghtOffset;
    const FVector TraceEnd = ControlledCharacter->GetActorLocation() + (DesiredMoveInput * TraceLenght);

    const bool HitResult = KSL::SphereTraceSingleForObjects
    (
        this, 
        ControlledCharacter->GetActorLocation(), 
        TraceEnd, 
        ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() * TraceRadiusScale,
        ObjectTypes, 
        false, 
        ActorsToIgnore, 
        DebugTrace, 
        TraceResult, 
        true, 
        FLinearColor::Gray,
        FLinearColor::Yellow, 
        0.1
    );

    if(HitResult == false)
    {
        FollowDirection = FVector(0, 0, 0); return false;
    }

    ACharacter* HitCharacter = Cast<ACharacter>(TraceResult.GetActor());
    if(!HitCharacter) { FollowDirection = FVector(0, 0, 0); return false; }

    const FRotator TraceDirection = FRotator(0, KML::FindLookAtRotation(TraceResult.TraceStart, TraceResult.TraceEnd).Yaw, 0);
    FVector RelativeData = KML::MakeRelativeTransform(FTransform(FRotator(0, 0, 0), TraceResult.ImpactPoint, FVector(1, 1, 1)), FTransform(TraceDirection, TraceResult.TraceStart, FVector(1, 1, 1))).GetLocation();

    CALS_Gait CurrentGait = CALS_Gait::Walking;
    if (ControlledCharacter->GetClass()->ImplementsInterface(UAGLS_AI_CharacterInterface::StaticClass())) //Get Current Gait Value From Interface
    {
        IAGLS_AI_CharacterInterface::Execute_BPI_AI_Get_CurrentStates
        (
            ControlledCharacter,
            IgnoreOut<TEnumAsByte<EMovementMode>>(),
            IgnoreOut<CALS_MovementState>(),
            IgnoreOut<CALS_MovementState>(),
            IgnoreOut<CALS_MovementAction>(),
            IgnoreOut<CALS_RotationMode>(),
            CurrentGait,
            IgnoreOut<CALS_Stance>(),
            IgnoreOut<CALS_OverlayState>(),
            IgnoreOut<CALS_GroundedMoveMode>()
        );
    }
    
    float MovementScale = 1.0;
    switch (CurrentGait)
    {
    case CALS_Gait::Walking:
        MovementScale = 8.0;
        break;
    case CALS_Gait::Running:
        MovementScale = 12.0;
        break;
    case CALS_Gait::Sprinting:
        MovementScale = 20.0;
        break;
    }

    MovementScale = MovementScale * KML::MapRangeClamped(abs(RelativeData.Y), 10, 40, 1.0, 0.1) * KML::MapRangeClamped(abs(RelativeData.X), 40, TraceLenght, 1.0, 0.5);
    const FVector MovementWorldDirection = KML::GetRightVector(KML::MakeRotFromX(DesiredMoveInput)) * KML::SelectFloat(-1, 1, RelativeData.Y > 0);

    ControlledCharacter->AddMovementInput(MovementWorldDirection, MovementScale, true);
    FollowDirection = DesiredMoveInput;
    return true;
}


void AAGLS_Human_ControllerCoreAI::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (ControlledCharacter)
    {
        if (ControlledCharacter->GetVelocity().Length() > 5)
        {
            FVector VelDirection = ControlledCharacter->GetVelocity(); VelDirection.Normalize();
            NonZeroVelocityDirection = VelDirection;
        }

        MakeSmoothSpeedValueWhenAiming(DeltaTime);
    }

}


void AAGLS_Human_ControllerCoreAI::SetFocalPoint(FVector NewFocus, EAIFocusPriority::Type InPriority)
{
    Super::SetFocalPoint(NewFocus, InPriority);

    CustomFocusPoint = NewFocus;
    //GEngine->AddOnScreenDebugMessage(0, 0.6, FColor::White, NewFocus.ToString());
}


void AAGLS_Human_ControllerCoreAI::ClearFocus(EAIFocusPriority::Type InPriority)
{
    Super::ClearFocus(InPriority);

    CustomFocusPoint = FVector(0, 0, 0);
    //GEngine->AddOnScreenDebugMessage(0, 0.6, FColor::Red, TEXT("CLEAR FOCUS"));
}


FVector AAGLS_Human_ControllerCoreAI::GetCustomFocalPoint() const
{
    return CustomFocusPoint;
}


//▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
//▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
//▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒

bool AAGLS_Human_ControllerCoreAI::ControlRotationAsSelfHeadSocket(bool& SkipUpdate, FRotator& OutDesiredRotation, FRotator InDesiredRotation)
{
    CALS_RotationMode CurrentRotMode = CALS_RotationMode::LookingDirection;

    if (ControlledCharacter->GetClass()->ImplementsInterface(UAGLS_AI_CharacterInterface::StaticClass()))
    {
        IAGLS_AI_CharacterInterface::Execute_BPI_AI_Get_CurrentStates
        (
            ControlledCharacter,
            IgnoreOut<TEnumAsByte<EMovementMode>>(),
            IgnoreOut<CALS_MovementState>(),
            IgnoreOut<CALS_MovementState>(),
            IgnoreOut<CALS_MovementAction>(),
            CurrentRotMode,
            IgnoreOut<CALS_Gait>(),
            IgnoreOut<CALS_Stance>(),
            IgnoreOut<CALS_OverlayState>(),
            IgnoreOut<CALS_GroundedMoveMode>()
        );
        
        if (CurrentRotMode == CALS_RotationMode::VelocityDirection)
        {
            if (!(ControlledCharacter->GetMesh()->GetAnimInstance()))
            {
                SkipUpdate = true;
                OutDesiredRotation = InDesiredRotation;
                return false;
            }

            const float CurveValue = ControlledCharacter->GetMesh()->GetAnimInstance()->GetCurveValue(TEXT("ControlRotationFromHead"));
            if (CurveValue > 0.01)
            {
                FRotator SocketHeadRot = ControlledCharacter->GetMesh()->GetSocketRotation(TEXT("head"));
                FVector FinalDirection = KML::Vector_SlerpNormals(KML::GetRightVector(SocketHeadRot), KML::GetForwardVector(FRotator(0, KML::MakeRotFromX(KML::GetRightVector(SocketHeadRot)).Yaw, 0.0)), 0.8);
                OutDesiredRotation = KML::RLerp(InDesiredRotation, KML::MakeRotFromX(FinalDirection), KML::FClamp(CurveValue, 0, 1), true);
                SkipUpdate = false;
                return true;
            }
            else
            {
                SkipUpdate = true;
                OutDesiredRotation = InDesiredRotation;
                return false;
            }
        }

    }
    SkipUpdate = true;
    return false;
}


FRotator AAGLS_Human_ControllerCoreAI::ControlRotationAsAimInEnemyHead(
    TArray<FName> InShootingTargetsBonesName, 
    FName InGunMuzzleSocketName, 
    FName ShootingStartSocketName, 
    float ShotDirectionIncludeVelocityAlpha, 
    float BlendTraceStartWithWeaponMuzzle)
{
    if (!GetFocusActor()) return GetControlRotation();

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 01 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    FVector ByVelocityOffset = FVector(0, 0, 0);
    if (PAWN->GetVelocity().Length() > 50.0)
    {
        FVector VelocityDirection = PAWN->GetVelocity(); VelocityDirection.Normalize();
        const float DotToVelocity = abs(KML::Dot_VectorVector(VelocityDirection, KML::GetForwardVector(GetControlRotation())));
        if (DotToVelocity < 0.5)
        {
            ByVelocityOffset = PAWN->GetVelocity() * KML::Lerp(1.0, 0.0, DotToVelocity) * ShotDirectionIncludeVelocityAlpha;
        }
    }

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 02 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    FVector Origin = GetFocusActor()->GetActorLocation();
    ACharacter* FocusChar = Cast<ACharacter>(GetFocusActor());
    if (FocusChar)
    {
        FName DesiredBoneName = RandomEnemyBoneName;
        if (InShootingTargetsBonesName.Num() > 0 && TimeToSetNewRandomBone <= -0.1)
        {
            const FName& RandomBone = InShootingTargetsBonesName[FMath::RandRange(0, InShootingTargetsBonesName.Num() - 1)];
            RandomEnemyBoneName = RandomBone;
            DesiredBoneName = RandomBone;
            TimeToSetNewRandomBone = 1.0;
        }
        if (FocusChar->GetMesh()->DoesSocketExist(DesiredBoneName) == true)
        {
            Origin = FocusChar->GetMesh()->GetSocketLocation(DesiredBoneName);
        }
    }

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 03 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    FVector GunMuzzlePosition = FVector(0, 0, 0);
    AAGLS_HumanAI_CharacterBase* HumanChar = Cast< AAGLS_HumanAI_CharacterBase>(PAWN);
    if (HumanChar)
    {
        if (HumanChar->CurrentHoldingProp)
        {
            if (HumanChar->CurrentHoldingProp->DoesSocketExist("BulletStart") == true)
            {
                GunMuzzlePosition = HumanChar->CurrentHoldingProp->GetSocketLocation("BulletStart");
            }
        }
    }

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 04 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    FVector HeadPosition = PAWN->GetActorLocation() + FVector(0, 0, 60);
    if (PAWN->GetMesh()->DoesSocketExist(ShootingStartSocketName) == true)
    {
        HeadPosition = PAWN->GetMesh()->GetSocketLocation(ShootingStartSocketName);
    }

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 05 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    FVector ReferencePos = FVector(0, 0, 0);
    if (KML::EqualEqual_VectorVector(GunMuzzlePosition, FVector(0, 0, 0), 5) == true)
    {
        ReferencePos = HeadPosition;
    }
    else
    {
        ReferencePos = KML::VLerp(HeadPosition, GunMuzzlePosition, BlendTraceStartWithWeaponMuzzle);
        ReferencePos = ReferencePos + (KML::GetForwardVector(KML::FindLookAtRotation(HeadPosition, Origin)) * -8.0);
    }

    //╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍➜ Then 06 ╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍╍
    return KML::FindLookAtRotation(HeadPosition, Origin + ByVelocityOffset);
}


FRotator AAGLS_Human_ControllerCoreAI::ControlRotationOnFocusActor(FRotator InDesiredRot)
{
    FRotator LookAt = KML::FindLookAtRotation(PAWN->GetActorLocation() + FVector(0, 0, 40), GetFocusActor()->GetActorLocation() + FVector(0, 0, 40));
    float LerpAlpha = 0.0;
    if (FVector::Distance(PAWN->GetActorLocation(), GetFocusActor()->GetActorLocation()) < 60)
    {
        LerpAlpha = KML::MapRangeClamped(FVector::Distance(PAWN->GetActorLocation(), GetFocusActor()->GetActorLocation()), 30, 60, 1, 0);
    }

    return KML::RLerp(LookAt, InDesiredRot, LerpAlpha, true);
}


void AAGLS_Human_ControllerCoreAI::DrawDebugRotation(FRotator CurrentRot, FString Description, FColor Color, float ArrowLenght)
{
    if (bDrawDebugAboutAdvancedRotation == false) return;
    FColor ArrowColor = Color;
    //ArrowColor.A = 1;
    DrawDebugLine(GetWorld(), PAWN->GetActorLocation(), PAWN->GetActorLocation() + (KML::GetForwardVector(CurrentRot) * ArrowLenght), ArrowColor, false, 0, 1, 1.2);
    DrawDebugString(GetWorld(), PAWN->GetActorLocation() + (KML::GetForwardVector(CurrentRot) * (ArrowLenght * 0.5)), Description, nullptr, Color, 0, true, 0.98);
    return;
}


void AAGLS_Human_ControllerCoreAI::MakeSmoothSpeedValueWhenAiming(float dt)
{
    if (PAWN->GetMesh()->GetAnimInstance())
    {
        if (PAWN->GetMesh()->GetAnimInstance()->GetCurveValue("Enable_SpineRotation") > 0.5)
        {
            DefaultControlInterpSpeedScale = 1.0;
        }
        else
        {
            DefaultControlInterpSpeedScale = KML::FInterpTo(DefaultControlInterpSpeedScale, 0.0, dt, 1.5);
        }
    }
}
