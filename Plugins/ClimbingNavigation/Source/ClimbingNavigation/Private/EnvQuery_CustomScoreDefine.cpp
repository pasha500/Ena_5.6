


#include "EnvQuery_CustomScoreDefine.h"

UWorld* UEnvQuery_CustomScoreDefine::GetWorld() const
{
	check(GetOuter() != NULL);

	UEnvQueryManager* EnvQueryManager = Cast<UEnvQueryManager>(GetOuter());
	if (EnvQueryManager != NULL)
	{
		return EnvQueryManager->GetWorld();
	}

	// Outer should always be UEnvQueryManager* in the game, which implements GetWorld() and therefore makes this
	// a correct world context.  However, in the editor the outer is /Script/AIModule (at compile time), which
	// does not explicitly implement GetWorld().  For that reason, calling GetWorld() generically in that case on the
	// AIModule calls to the base implementation, which causes a blueprint compile warning in the Editor
	// which states that the function isn't safe to call on this (due to requiring WorldContext which it doesn't
	// provide).  Simply returning NULL in this case fixes those erroneous blueprint compile warnings.

	if (ContextFromEQS)
	{
		return ContextFromEQS;
	}

	return NULL;
}

