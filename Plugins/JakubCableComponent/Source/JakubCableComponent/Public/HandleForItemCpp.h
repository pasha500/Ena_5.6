

#pragma once

#include "CoreMinimal.h"
#include "JakubSimpleParticleComponent.h"
#include "HandleForItemCpp.generated.h"

UENUM(BlueprintType)
namespace EIWALS_HandleItemType
{
	enum Type : int
	{
		None				UMETA(DisplayName = "None"),
		Binoculars			UMETA(DisplayName = "Binoculars"),
		Bow					UMETA(DisplayName = "Bow"),
		Axe					UMETA(DisplayName = "Axe"),
		Knife				UMETA(DisplayName = "Knife"),
		Sword				UMETA(DisplayName = "Sword"),
		Food				UMETA(DisplayName = "Food"),
		FirstAidKit			UMETA(DisplayName = "FirstAidKit"),
		Grenade_1			UMETA(DisplayName = "Grenade_1"),
		Grenade_2			UMETA(DisplayName = "Grenade_2"),
		Prop_1				UMETA(DisplayName = "Prop_1"),
		Prop_2				UMETA(DisplayName = "Prop_2")
	};
}

//A class that builds a component whose purpose is to create appropriate holders for player props.
UCLASS(Blueprintable, ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class JAKUBCABLECOMPONENT_API UHandleForItemCpp : public UJakubSimpleParticleComponent
{
	GENERATED_BODY()

public:

	/*
	A variable defining the type of the 'item socket'. This is an identifier that many other 
	components will use. This value determines whether the Actor associated with HandleForItem 
	should be attached to the hand or remain in place.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Socket Type", Keywords = "Physic Handle Item"))
		TEnumAsByte<EIWALS_HandleItemType::Type> SocketTypeC = EIWALS_HandleItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Attach Socket Name", Keywords = "Physic Handle Item"))
		FName AttachSocketNameC = TEXT("Socket_For_Item_1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Attach Rule", Keywords = "Physic Handle Item"))
		EAttachmentRule AttachRuleC = EAttachmentRule::SnapToTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Attach To Backpack", Keywords = "Physic Handle Item"))
		bool AttachToBackpackC = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Can Equip Item", Keywords = "Physic Handle Item"))
		bool CanEquipItemC = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Prop Attach Offset", Keywords = "Physic Handle Item"))
		FTransform PropAttachOffsetC = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(1, 1, 1));

	/*ENG:
	Leaving this option enabled causes HandleForItem to attach the prop Actor to the appropriate 
	component associated with the hand position. Such attachment of the Actor will occur when the 
	player changes the Overlay State to the one corresponding to the Handle component (this is 
	determined by the SocketType variable). The EquipWaitDelay variable affects the delay of this 
	attachment. It is worth remembering that despite enabling this functionality, there are cases 
	when the attachment will be automatically ignored. This applies to the case when we add elements 
	to the 'PreOverlayStateToIgnoreAttach' variable.
	
	PL:
	Pozostawienie tej opcji wlaczonej powoduje ze HandleForItem bedzie doczepiac Aktora rekwizytu do 
	odpowiedniego komponentu powiazanego z pozycja reki. Takie doczepienie aktora bedzie nastepowac 
	w momencie kiedy gracz zmieni stan nakladki (Overlay State) na ten odpowiadajacy komponentowi 
	Handle (okresla to zmienna SocketType). Zmienna EquipWaitDelay wplywa na opoznienie tego 
	doczepienia. Warto pamietac ze mimo wlaczenia tej funkcjonalnosci, sa przypadki kiedy
	automatyczne doczepienie zostanie z ignorowane. Dotyczy to przypadku kiedy dodamy elementy do 
	zmiennej 'PreOverlayStateToIgnoreAttach'. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Use Auto Attach", Keywords = "Physic Handle Item"))
	bool UseAutoAttachToHand = true;

	/*
	Variable defining how long after changing the overlay state the item should be attached to the correct hand 
	(This may not necessarily mean that the Actor will be attached to the Mesh IK bone. It may be related to 
	another component that defines the position the item should hold)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Equip Wait Delay", Keywords = "Physic Handle Item", 
		ClampMin = "0.0", ClampMax = "2.0", EditCondition = "UseAutoAttachToHand"))
	float EquipWaitDelay = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handle Config", meta = (DisplayName = "Drop Item Delay", Keywords = "Physic Handle Item"))
		float DropItemDelay = 0.0;

	//System Variables
	UPROPERTY(BlueprintReadWrite, Category = "Props Handle|Main", meta = (DisplayName = "Is In Equipment", Keywords = "Physic Handle Item"))
		bool IsInEquipmentC = false;

	UPROPERTY(BlueprintReadWrite, Category = "Props Handle|Main", meta = (DisplayName = "Character", Keywords = "Physic Handle Item"))
		ACharacter* CharacterC = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Props Handle|Main", meta = (DisplayName = "Item To Pick", Keywords = "Physic Handle Item"))
		AActor* ItemToPickC = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Props Handle|Main", meta = (DisplayName = "Backpack Actor", Keywords = "Physic Handle Item"))
		AActor* BackpackActorC = nullptr;


	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Props Handle Component", meta = (ForceAsFunction, DisplayName = "Attach Item To Handle", Keywords = "Props,Handle"))
	void AttachItemToHandleC(AActor* TargetItem, bool AutoHandAttach, int TypeIndex);
	virtual void AttachItemToHandleC_Implementation(AActor* TargetItem, bool AutoHandAttach, int TypeIndex);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Props Handle Component", meta = (ForceAsFunction, DisplayName = "Attach To Hand", Keywords = "Props,Handle"))
	void AttachToHandC();
	virtual void AttachToHandC_Implementation();


};
