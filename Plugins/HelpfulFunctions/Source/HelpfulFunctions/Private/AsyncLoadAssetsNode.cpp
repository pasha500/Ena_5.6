


#include "AsyncLoadAssetsNode.h"
#include "Engine/AssetManager.h"

UAsyncLoadAssetsNode::UAsyncLoadAssetsNode()
{
}

UAsyncLoadAssetsNode* UAsyncLoadAssetsNode::AsyncLoadAssetArray(const TArray<TSoftObjectPtr<UObject>>& AssetsToLoad)
{
    UAsyncLoadAssetsNode* Node = NewObject<UAsyncLoadAssetsNode>();
    Node->Assets = AssetsToLoad;
    return Node;
}

void UAsyncLoadAssetsNode::Activate()
{
    Assets.RemoveAll([](const TSoftObjectPtr<UObject>& Asset)
        {
            return Asset.IsNull() || !Asset.ToSoftObjectPath().IsValid();
        });

    if (Assets.Num() == 0)
    {
        OnCompleted.Broadcast(LoadedAssets);
        return;
    }

    LoadNextAsset();
}

void UAsyncLoadAssetsNode::LoadNextAsset()
{
    if (Assets.Num() == 0)
    {
        OnCompleted.Broadcast(LoadedAssets);
        return;
    }

    TSoftObjectPtr<UObject> AssetToLoad = Assets[0];
    Assets.RemoveAt(0);

    if (AssetToLoad.IsValid())
    {
        LoadedAssets.Add(AssetToLoad.Get());
        LoadNextAsset();
    }
    else
    {
        const FSoftObjectPath AssetPath = AssetToLoad.ToSoftObjectPath();
        if (!AssetPath.IsValid())
        {
            LoadNextAsset();
            return;
        }

        StreamableManager.RequestAsyncLoad(
            AssetPath,
            FStreamableDelegate::CreateUObject(this, &UAsyncLoadAssetsNode::OnAssetLoaded, AssetToLoad)
        );
    }
}

void UAsyncLoadAssetsNode::OnAssetLoaded(TSoftObjectPtr<UObject> LoadedAsset)
{
    if (LoadedAsset.IsValid())
    {
        UObject* LoadedObject = LoadedAsset.Get();
        if (LoadedObject)
        {
            LoadedAssets.Add(LoadedObject);
        }
    }

    LoadNextAsset();
}

