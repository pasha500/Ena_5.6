// Copyright Jakub W, All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ALS_HumanAI_InterfaceCpp.generated.h"


// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable, Category = "AGLS AI Controller Core", meta = (DisplayName = "INTERFACE_CharAI_ControllerValues"))
class UALS_HumanAI_InterfaceCpp : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class HELPFULFUNCTIONS_API IALS_HumanAI_InterfaceCpp
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

/*NOTE: This interface function is deprecated. It has been replaced by the ALS_HealthAndDamageBPI->BPI_HAD_GetIsDead() interface.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetDeathState(bool& IsDeath);


/*NOTE: This interface feature is deprecated. It has been replaced by adding appropriate Actor Tags, 
which tell other character classes and controllers whether to treat the instance as a enemy or not.

To check if a given character instance should be interpreted as an enemy, use the 'GetIsEnemyValue()' function. 
With the Blueprint Character class open, in the 'Actor' category there is a 'Tags' variable containing the 
names of enemy classes with the 'EN_' prefix which means 'Enemy'.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetEnemyState(bool& IsEnemy);

/*
ENG:
In this interface we mark whether our class should be interpreted as 'Solider' or 'Zombie'. In case of marking 'Solider' 
some systems allow to perform certain sequences e.g. Stealth Finisher or Melee Attack. Marking 'Zombie' as true causes to 
block some functionalities e.g. Melee Combat.

PL:
W tym interfejsie oznaczamy czy nasza klasa powinna być interpretowana jako 'Solider' lub 'Zombie'. W przypadku oznaczenia 
'Solider' niektóre systemy pozwalają wykonać pewnie sekwencje np. Stealth Finisher lub Melee Attack. Oznaczenia 'Zombie' 
jako true powoduje zablokowanie niektórych funkcjonalności np. Melee Combat.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetCharacterType(bool& IsSolider, bool& IsZombie);

/*NOTE: Deprecated interface function! Should not be used in any class*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetHumanAIValues(bool& HavePistol, bool& HaveRifle, bool& IsSiting, bool& IsHostage);

/*NOTE: Deprecated interface function! Should not be used in any class. 

This function call only occurs in the C++ class 'ALS_HumanAI_ControllerCpp'. This means that the character implementing 
this interface should still have a definition/override of this function.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetDetectedEnemy(bool& DetectedEnemy, bool& IsSelfEnemy);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "BPI AI Controller States|Get")
		void HAI_DrawDebugTraces(bool& DrawDebug);


/*From this interface function you can get information about detected enemies. However, this is not the only way to get this information. 
By default, without overriding this interface function does not return any information. Usually, for this purpose you need to call 
GetController()->...->GetControllerValues*(...), where only from this function you can get the current values ​​from the AI ​​controller. 
This means that 'HAI_GetControllerSmallValues' can be treated as an intermediary function.

You can use:
CharacterReference*->DoesHaveInterface(INTERFACE_CharAI_ControllerValues)->Execute_HAI_GetControllerSmallValues(...)

Or:
CharacterReference*->GetController()->DoesHaveInterface(ZombieAI_ControllerInterface)->GetControllerValues(...)

PL:
Z tej funkcji interfejsu można pobrać informacje na temat wykrytych wrogów. Jednak nie jest to jedny sposób na dostanie tych informacji. 
Bazowo bez nadpisania ta funkcja interfejsu nie  zwraca żadnych informacji. Zazwyczaj w tym celu potrzebne jest wywołanie GetController()
->...->GetControllerValues*(...), gdzie dopiero z tej funkcji można pobrać aktualne wartości z kontrolera AI. Oznacza to że 
'HAI_GetControllerSmallValues' można traktować jako pośreniczącą funkcję.*/
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "BPI AI Controller States|Get")
		void HAI_GetControllerSmallValues(bool& DetectedEnemy, float& DetectedEnemyTime , ACharacter*& EnemyActor);


};
