

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AGLS_AI_HumanCharInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Category = "AGLS AI Character Core", meta = (DisplayName = "INTERFACE_CharAI_HumanCore"))
class UAGLS_AI_HumanCharInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class HELPFULFUNCTIONS_API IAGLS_AI_HumanCharInterface
{
	GENERATED_BODY()

public:

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_DeathSectionByMeleeCombat();

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_HitByThrowableObject(AActor* Actor, FVector Normal, FHitResult HitResult);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_AttackedByKnifeOrHand(bool Start, bool IgnoreCapsuleUpdate);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_MarkCharacter(bool Mark);

	////Blueprint Interface - Human Character AI: By using this call you can execute shoot action for Human Character
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_ShootAction();

	////Blueprint Interface - Human Character AI: By using this you can start reloading action for Human Character
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_ReloadingAction();

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_StartHostageMovement(bool Start, AActor* IsHostageOf);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_IsVicForStealthFinisher(bool StartSF);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_StartSitOrStand(bool StartSitting);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Set")
	void BPI_HCAI_Set_CoverPostureStrenght(float Alpha);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Set")
	void BPI_HCAI_Get_StartedCoverMode(bool& StartedCover);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Action")
	void BPI_HCAI_PushCharacter(AActor* HitteBy, FVector HitNormal, FHitResult HitResult);

	//Blueprint Interface - Human Character AI (Implemented for Human Character Class)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get")
	void BPI_HCAI_Get_IsHittedByKnifeOrHand(bool& ISHitted, bool& ByKnife);

	//Blueprint Interface - Human Character AI: Return static mesh that informing about current AI status (by default mesh have a diamond shape and it is placed above character head)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get")
	void BPI_HCAI_Get_AI_StatusMesh(UStaticMeshComponent*& MeshComp, UMaterialInstanceDynamic*& MaterialInst);

	//Blueprint Interface - Human Character AI: Returns basic informations about character like does he have a rifle or pistol. Also return visual skeletal mesh of weapons
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get", meta = (AdvancedDisplay = 2))
	void BPI_HCAI_Get_GetHumanCharacterValues(
		bool& HavePistol, 
		bool& HaveRifle, 
		bool& IsSitting, 
		bool& IsHostage, 
		AActor*& IsHostageOf, 
		USkeletalMeshComponent*& RifleComponent, 
		USkeletalMeshComponent*& PistolComponent
	);

	//Blueprint Interface - Human Character AI: Return Overlay that AI use when is in patroling state (when character dont have controller then this overlay will by set as default)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get")
	void BPI_HCAI_Get_PatrolingOverlayState(CALS_OverlayState& PatrolingOverlay) const;

	//Blueprint Interface - Human Character AI: Return mainly values about current rifle (this values is taken from rifle structure)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get", meta = (AdvancedDisplay = 1))
	void BPI_AI_Get_RifleAssetProperties(
		FName& Name,
		int& EnumIndex,
		USkeletalMesh*& MainMesh,
		UStaticMesh*& MagazineMesh,
		FName& MagSocketName,
		bool& HeadshotInstantDead,
		float& DamageStrength,
		float& AutoShootTime,
		bool& IsSniperRifle,
		int& MaxAmmoPerMag,
		UAnimMontage*& ReloadAnim
	) const;

	//Blueprint Interface - Human Character AI: Return mainly values about current pistol (this values is taken from pistol structure)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get", meta = (AdvancedDisplay = 1))
	void BPI_AI_Get_PistolAssetProperties(
		FName& Name,
		int& EnumIndex,
		USkeletalMesh*& MainMesh,
		UStaticMesh*& MagazineMesh,
		FName& MagSocketName,
		bool& HeadshotInstantDead,
		float& DamageStrength,
		float& ShootingInterval,
		bool& WithSilencer,
		int& MaxAmmoPerMag,
		UAnimMontage*& ReloadAnim
	) const;

	//Blueprint Interface - Human Character AI: Needed to properly initialize configuration parameters for the asynchronously loaded class 'Human AI Character' and controller.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Set", meta = (ForceAsFunction, AdvancedDisplay = 1))
	void BPI_HCAI_Set_InitializationData(
		bool& Updated,
		AActor* PathActor,
		CALS_OverlayState PatrolingOverlay,
		CALS_GroundedMoveMode GroundedWalkType,
		bool HaveRifle,
		bool HavePistol,
		uint8 RifleModelIndex,
		uint8 PistolModelIndex,
		bool UseSolid,
		bool HaveGolves,
		int ShirtType,
		int PantsType,
		FColor SolidColor,
		FColor SkinColor,
		FColor ShirtColor,
		FColor PantsColor,
		FColor ShoesColor,
		FColor GlovesColor
	);

	////Blueprint Interface - Human Character AI
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Set")
	void BPI_HCAI_Set_IsClimbing(bool StartClimbing);

	////Blueprint Interface - Human Character AI
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Human Character|Get")
	void BPI_HCAI_Get_IsClimbing(bool& IsClimbing);

};
