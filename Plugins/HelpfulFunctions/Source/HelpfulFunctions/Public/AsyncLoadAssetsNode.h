

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Engine/StreamableManager.h"
#include "AsyncLoadAssetsNode.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAssetsLoaded, const TArray<UObject*>&, LoadedAssets);

/*Definition offFunction that allows for asynchronous loading of many assets. At the input we provide an array in which we define which assets, 
for example: textures, animations, sounds should be loaded into memory. After completing this process, the 'OnAssetsLoaded' delegate is called 
together with a reference in the form of a HardReferences array to the indicated resources.*/
UCLASS()
class HELPFULFUNCTIONS_API UAsyncLoadAssetsNode : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
    UAsyncLoadAssetsNode();

/*Function that allows for asynchronous loading of many assets. At the input we provide an array in which we define which assets, for example: 
textures, animations, sounds should be loaded into memory. After completing this process, the 'OnAssetsLoaded' delegate is called together 
with a reference in the form of a HardReferences array to the indicated resources.*/
    UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", DisplayName = "Async Load Assets"), Category = "Asset Loading")
    static UAsyncLoadAssetsNode* AsyncLoadAssetArray(const TArray<TSoftObjectPtr<UObject>>& AssetsToLoad);

    UPROPERTY(BlueprintAssignable)
    FOnAssetsLoaded OnCompleted;

protected:
    virtual void Activate() override;

private:
    TArray<TSoftObjectPtr<UObject>> Assets;
    TArray<UObject*> LoadedAssets;
    FStreamableManager StreamableManager;

    void LoadNextAsset();
    void OnAssetLoaded(TSoftObjectPtr<UObject> LoadedAsset);
	
};
