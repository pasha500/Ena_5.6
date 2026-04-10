#include "Raid/RaidRoomActor.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidLootRegistry.h"
#include "Raid/RaidRegionBannerWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "UObject/UnrealType.h"

namespace
{
    TWeakObjectPtr<URaidRegionBannerWidget> GSharedRegionBannerWidget;
    const FName RaidLootOutlineActiveTag(TEXT("RaidLootOutlineActive"));
    TSet<const UWorld*> GLootOutlinePPAttemptedWorlds;
    const FName RaidRoomGeneratedTag(TEXT("RaidRoomGenerated"));

    FName MakeRaidRoomNodeTag(const int32 NodeId)
    {
        return FName(*FString::Printf(TEXT("RaidRoomNode_%d"), NodeId));
    }

    EObjectFlags ResolveRoomSpawnObjectFlags(const UWorld* World)
    {
        return (World && World->IsGameWorld())
            ? (RF_Transient | RF_DuplicateTransient | RF_TextExportTransient)
            : RF_NoFlags;
    }

    void ApplyGridSizeFromRoomSizeToken(const FString& SizeToken, int32& InOutGridSize)
    {
        if (SizeToken.Equals(TEXT("Small"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 9;
        }
        else if (SizeToken.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 13;
        }
        else if (SizeToken.Equals(TEXT("Large"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 21;
        }
        else if (SizeToken.Equals(TEXT("Massive"), ESearchCase::IgnoreCase))
        {
            InOutGridSize = 31;
        }
        else if (InOutGridSize <= 0)
        {
            InOutGridSize = 13;
        }
    }

    FString ResolveBannerTitleFromNodeTags(const FLevelNodeRow& NodeRow)
    {
        TArray<FString> TagTokens;
        NodeRow.NodeTags.ParseIntoArray(TagTokens, TEXT(","), true);
        for (FString& Token : TagTokens)
        {
            Token.TrimStartAndEndInline();
            if (!Token.IsEmpty() && !Token.Contains(TEXT("[")))
            {
                return Token;
            }
        }

        if (!NodeRow.NodeTags.IsEmpty() && !NodeRow.NodeTags.Contains(TEXT("[")))
        {
            return NodeRow.NodeTags;
        }

        return FString();
    }

    FString ResolveBannerSubtitleFromRoomType(const FLevelNodeRow& NodeRow)
    {
        FString RoomTypeLabel;
        const FString& RoomType = NodeRow.RoomType;
        if (RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("시작 구역 (Start)");
        }
        else if (RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("교전 구역 (Combat)");
        }
        else if (RoomType.Equals(TEXT("Loot"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("루팅 구역 (Loot)");
        }
        else if (RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("보스 구역 (Boss)");
        }
        else if (RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase))
        {
            RoomTypeLabel = TEXT("탈출 구역 (Exit)");
        }
        else
        {
            RoomTypeLabel = TEXT("일반 구역 (Sector)");
        }

        if (NodeRow.EnvType.IsEmpty())
        {
            return RoomTypeLabel;
        }

        return FString::Printf(TEXT("%s | %s"), *RoomTypeLabel, *NodeRow.EnvType);
    }

    bool HasBlendableMaterial(const FPostProcessSettings& Settings, const UMaterialInterface* Material)
    {
        if (!IsValid(Material))
        {
            return false;
        }

        for (const FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
        {
            if (Blendable.Object == Material)
            {
                return true;
            }
        }
        return false;
    }

    bool IsPawnInsideRoomBannerZone(const ARaidRoomActor* Room, const APawn* Pawn)
    {
        if (!Room || !Pawn)
        {
            return false;
        }

        const FVector LocalPlayerLoc = Room->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
        const FVector RoomExtent = Room->GetRoomExtent();
        constexpr float BannerZoneInset = 320.0f;
        const float ZoneX = FMath::Max(300.0f, RoomExtent.X - BannerZoneInset);
        const float ZoneY = FMath::Max(300.0f, RoomExtent.Y - BannerZoneInset);

        return
            FMath::Abs(LocalPlayerLoc.X) <= ZoneX &&
            FMath::Abs(LocalPlayerLoc.Y) <= ZoneY;
    }

    bool IsOutdoorStyleRoom(const FLevelNodeRow& NodeRow)
    {
        const FString Meta = (NodeRow.EnvType + TEXT(" ") + NodeRow.Theme + TEXT(" ") + NodeRow.NodeTags + TEXT(" ") + NodeRow.RoomRole).ToLower();
        const bool bForceIndoor =
            Meta.Contains(TEXT("tarkov")) ||
            Meta.Contains(TEXT("cqb")) ||
            Meta.Contains(TEXT("indoor")) ||
            Meta.Contains(TEXT("factory")) ||
            Meta.Contains(TEXT("warehouse")) ||
            Meta.Contains(TEXT("mall")) ||
            Meta.Contains(TEXT("실내")) ||
            Meta.Contains(TEXT("타르코프"));
        const bool bForceOutdoor =
            Meta.Contains(TEXT("openworld")) ||
            Meta.Contains(TEXT("open world")) ||
            Meta.Contains(TEXT("outdoor")) ||
            Meta.Contains(TEXT("오픈월드")) ||
            Meta.Contains(TEXT("야외"));
        const bool bEnvOutdoor =
            NodeRow.EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
            NodeRow.EnvType.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
            NodeRow.EnvType.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
        return bForceOutdoor || (!bForceIndoor && bEnvOutdoor);
    }

    ERaidVariationOffsetChannel ResolveOffsetChannelForMeshType(int32 MeshType)
    {
        switch (MeshType)
        {
        case 0:
            return ERaidVariationOffsetChannel::Floor;
        case 1:
            return ERaidVariationOffsetChannel::Wall;
        case 2:
            return ERaidVariationOffsetChannel::Obstacle;
        case 3:
            return ERaidVariationOffsetChannel::Decoration;
        case 4:
            return ERaidVariationOffsetChannel::Doorway;
        case 5:
            return ERaidVariationOffsetChannel::DoorBlocker;
        case 6:
        case 7:
        case 8:
            return ERaidVariationOffsetChannel::Foliage;
        default:
            return ERaidVariationOffsetChannel::Default;
        }
    }

    bool IsInsideWaterPhysicsVolume(UWorld* World, const FVector& Location, float SphereRadius = 20.0f)
    {
        if (!World) return false;

        for (FConstPhysicsVolumeIterator It = World->GetNonDefaultPhysicsVolumeIterator(); It; ++It)
        {
            const TWeakObjectPtr<APhysicsVolume>& WeakVolume = *It;
            const APhysicsVolume* PhysicsVolume = WeakVolume.Get();
            if (!PhysicsVolume || !PhysicsVolume->bWaterVolume) continue;
            if (PhysicsVolume->EncompassesPoint(Location, SphereRadius)) return true;
        }
        return false;
    }

    bool IsWaterHit(const FHitResult& Hit)
    {
        if (const AActor* HitActor = Hit.GetActor())
        {
            if (const APhysicsVolume* PhysicsVolume = Cast<APhysicsVolume>(HitActor))
            {
                if (PhysicsVolume->bWaterVolume) return true;
            }
            if (HitActor->ActorHasTag(TEXT("Water"))) return true;
            const FString ActorClass = HitActor->GetClass()->GetName();
            if (ActorClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase) ||
                ActorClass.Contains(TEXT("Lake"), ESearchCase::IgnoreCase) ||
                ActorClass.Contains(TEXT("River"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        if (const UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HitComp->ComponentHasTag(TEXT("Water"))) return true;
            const FString CompClass = HitComp->GetClass()->GetName();
            if (CompClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase)) return true;
        }
        return false;
    }

    bool IsLandscapeLikeHit(const FHitResult& Hit)
    {
        auto HasLandscapeKeyword = [](const FString& ClassName) -> bool
            {
                return
                    ClassName.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
                    ClassName.Contains(TEXT("LandProxy"), ESearchCase::IgnoreCase);
            };

        if (const AActor* HitActor = Hit.GetActor())
        {
            if (HasLandscapeKeyword(HitActor->GetClass()->GetName()))
            {
                return true;
            }
        }

        if (const UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HasLandscapeKeyword(HitComp->GetClass()->GetName()))
            {
                return true;
            }
        }

        return false;
    }

    bool IsTreeLikeMeshName(const FString& InName)
    {
        const FString Lower = InName.ToLower();
        static const TCHAR* Keywords[] = {
            TEXT("tree"), TEXT("sapling"), TEXT("pine"), TEXT("oak"), TEXT("beech"),
            TEXT("birch"), TEXT("fir"), TEXT("spruce"), TEXT("palm"), TEXT("cypress"),
            TEXT("willow"), TEXT("trunk")
        };

        for (const TCHAR* Keyword : Keywords)
        {
            if (Lower.Contains(Keyword))
            {
                return true;
            }
        }
        return false;
    }

    bool ReadNumericPropertyAsDouble(const FProperty* Property, const void* Container, double& OutValue)
    {
        if (!Property || !Container)
        {
            return false;
        }

        if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            OutValue = DoubleProperty->GetPropertyValue_InContainer(Container);
            return true;
        }

        if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            OutValue = static_cast<double>(FloatProperty->GetPropertyValue_InContainer(Container));
            return true;
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Container);
            if (!ValuePtr)
            {
                return false;
            }

            OutValue = NumericProperty->IsFloatingPoint()
                ? NumericProperty->GetFloatingPointPropertyValue(ValuePtr)
                : static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
            return true;
        }

        return false;
    }

    bool WriteNumericPropertyFromDouble(FProperty* Property, void* Container, double InValue)
    {
        if (!Property || !Container || !FMath::IsFinite(InValue))
        {
            return false;
        }

        if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            DoubleProperty->SetPropertyValue_InContainer(Container, InValue);
            return true;
        }

        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            FloatProperty->SetPropertyValue_InContainer(Container, static_cast<float>(InValue));
            return true;
        }

        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Container);
            if (!ValuePtr)
            {
                return false;
            }

            if (NumericProperty->IsFloatingPoint())
            {
                NumericProperty->SetFloatingPointPropertyValue(ValuePtr, InValue);
            }
            else
            {
                NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToInt64(InValue)));
            }
            return true;
        }

        return false;
    }

    struct FRaidLootRuntimeRowData
    {
        bool bValid = false;
        FString RowName;
        FString TypeValue;
        FString MainClassPath;
        bool bHasMaxQuantity = false;
        double MaxQuantity = 0.0;
        bool bHasCurrentQuantity = false;
        double CurrentQuantity = 0.0;
        bool bHasParam1 = false;
        double Param1 = 0.0;
        bool bHasParam2 = false;
        double Param2 = 0.0;
        bool bHasPickOnlyWhenBackpack = false;
        bool bPickOnlyWhenBackpack = false;
    };

    FString NormalizeClassPathForComparison(const FString& InPath)
    {
        FString Normalized = InPath;
        Normalized.TrimStartAndEndInline();
        Normalized.ReplaceInline(TEXT("\""), TEXT(""));

        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (Normalized.FindChar(TEXT('\''), FirstQuote) && Normalized.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
        {
            Normalized = Normalized.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
        }

        const FString Prefix = TEXT("/Script/Engine.BlueprintGeneratedClass");
        if (Normalized.StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            Normalized.RightChopInline(Prefix.Len(), EAllowShrinking::No);
        }

        Normalized.TrimStartAndEndInline();
        return Normalized;
    }

    bool IsPropertyNameMatchLoose(const FString& PropertyName, const FString& Token)
    {
        const FString LowerName = PropertyName.ToLower();
        const FString LowerToken = Token.ToLower();
        return
            LowerName.Equals(LowerToken, ESearchCase::CaseSensitive) ||
            LowerName.StartsWith(LowerToken + TEXT("_"), ESearchCase::CaseSensitive) ||
            LowerName.Contains(LowerToken, ESearchCase::CaseSensitive);
    }

    bool IsPropertyNameMatchAnyLoose(const FString& PropertyName, const std::initializer_list<const TCHAR*>& Tokens)
    {
        for (const TCHAR* Token : Tokens)
        {
            if (IsPropertyNameMatchLoose(PropertyName, FString(Token)))
            {
                return true;
            }
        }
        return false;
    }

    FString NormalizeIdentifierToken(const FString& InValue)
    {
        FString Normalized;
        Normalized.Reserve(InValue.Len());
        for (const TCHAR Char : InValue)
        {
            if (FChar::IsAlnum(Char))
            {
                Normalized.AppendChar(FChar::ToLower(Char));
            }
        }
        return Normalized;
    }

    bool IdentifierTokenMatches(const FString& LeftToken, const FString& RightToken)
    {
        if (LeftToken.IsEmpty() || RightToken.IsEmpty())
        {
            return false;
        }

        return
            LeftToken.Equals(RightToken, ESearchCase::CaseSensitive) ||
            LeftToken.Contains(RightToken, ESearchCase::CaseSensitive) ||
            RightToken.Contains(LeftToken, ESearchCase::CaseSensitive);
    }

    bool ResolveEnumValueByRowName(const UEnum* EnumDef, const FName& RowName, int64& OutValue)
    {
        if (!EnumDef || RowName.IsNone())
        {
            return false;
        }

        const FString RowToken = NormalizeIdentifierToken(RowName.ToString());
        if (RowToken.IsEmpty())
        {
            return false;
        }

        for (int32 EnumIdx = 0; EnumIdx < EnumDef->NumEnums(); ++EnumIdx)
        {
            if (EnumDef->HasMetaData(TEXT("Hidden"), EnumIdx))
            {
                continue;
            }

            const int64 EnumValue = EnumDef->GetValueByIndex(EnumIdx);
            if (EnumValue == INDEX_NONE)
            {
                continue;
            }

            const FString NameToken = NormalizeIdentifierToken(EnumDef->GetNameStringByIndex(EnumIdx));
            if (!NameToken.IsEmpty() && NameToken.EndsWith(TEXT("max"), ESearchCase::CaseSensitive))
            {
                continue;
            }

            const FString DisplayToken = NormalizeIdentifierToken(EnumDef->GetDisplayNameTextByIndex(EnumIdx).ToString());
            if (IdentifierTokenMatches(RowToken, NameToken) || IdentifierTokenMatches(RowToken, DisplayToken))
            {
                OutValue = EnumValue;
                return true;
            }
        }

        return false;
    }

    int32 ApplyLootRowModelIndexOverrideToObject(UObject* TargetObject, const FName& InDataRowName)
    {
        if (!IsValid(TargetObject) || InDataRowName.IsNone())
        {
            return 0;
        }

        int32 AppliedCount = 0;
        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeModelIndexProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("RifleModelIndex"),
                    TEXT("WeaponModelIndex"),
                    TEXT("GunModelIndex")
                });

            if (!bLooksLikeModelIndexProperty)
            {
                continue;
            }

            int64 ResolvedEnumValue = INDEX_NONE;

            if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                if (!ResolveEnumValueByRowName(EnumProp->GetEnum(), InDataRowName, ResolvedEnumValue))
                {
                    continue;
                }

                if (FNumericProperty* UnderlyingProperty = EnumProp->GetUnderlyingProperty())
                {
                    void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(TargetObject);
                    if (ValuePtr)
                    {
                        UnderlyingProperty->SetIntPropertyValue(ValuePtr, ResolvedEnumValue);
                        ++AppliedCount;
                    }
                }
                continue;
            }

            if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
            {
                if (!ResolveEnumValueByRowName(ByteProp->Enum, InDataRowName, ResolvedEnumValue))
                {
                    continue;
                }

                ByteProp->SetPropertyValue_InContainer(TargetObject, static_cast<uint8>(FMath::Clamp<int64>(ResolvedEnumValue, 0, 255)));
                ++AppliedCount;
                continue;
            }
        }

        return AppliedCount;
    }

    int32 ApplyLootDataRowNameOverrideToObject(UObject* TargetObject, const FName& InDataRowName)
    {
        if (!IsValid(TargetObject) || InDataRowName.IsNone())
        {
            return 0;
        }

        const FString DataRowString = InDataRowName.ToString();
        int32 AppliedCount = 0;

        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeRowNameProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("DataRowName"),
                    TEXT("LootDataRowName"),
                    TEXT("ItemDataRowName"),
                    TEXT("WeaponDataRowName"),
                    TEXT("RifleDataRowName"),
                    TEXT("RifleAssetRowName"),
                    TEXT("RifleRowName"),
                    TEXT("WeaponRowName")
                });

            if (!bLooksLikeRowNameProperty)
            {
                continue;
            }

            if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                NameProp->SetPropertyValue_InContainer(TargetObject, InDataRowName);
                ++AppliedCount;
                continue;
            }

            if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                StrProp->SetPropertyValue_InContainer(TargetObject, DataRowString);
                ++AppliedCount;
                continue;
            }
        }

        AppliedCount += ApplyLootRowModelIndexOverrideToObject(TargetObject, InDataRowName);
        return AppliedCount;
    }

    bool HasExplicitLootDataRowOverride(const UObject* TargetObject)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
        {
            const FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            const FString PropName = Property->GetName();
            const bool bLooksLikeRowNameProperty = IsPropertyNameMatchAnyLoose(
                PropName,
                {
                    TEXT("DataRowName"),
                    TEXT("LootDataRowName"),
                    TEXT("ItemDataRowName"),
                    TEXT("WeaponDataRowName"),
                    TEXT("RifleDataRowName"),
                    TEXT("RifleAssetRowName"),
                    TEXT("RifleRowName"),
                    TEXT("WeaponRowName")
                });

            if (!bLooksLikeRowNameProperty)
            {
                continue;
            }

            if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                if (!NameProp->GetPropertyValue_InContainer(TargetObject).IsNone())
                {
                    return true;
                }
                continue;
            }

            if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                FString RowValue = StrProp->GetPropertyValue_InContainer(TargetObject);
                RowValue.TrimStartAndEndInline();
                if (!RowValue.IsEmpty())
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool TryExtractLootRuntimeRowData(const FName RowName, const UScriptStruct* RowStruct, const uint8* RowData, FRaidLootRuntimeRowData& OutData)
    {
        if (!RowStruct || !RowData)
        {
            return false;
        }

        FRaidLootRuntimeRowData Result;
        Result.RowName = RowName.ToString();

        for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
        {
            const FProperty* Property = *PropIt;
            const FString PropName = Property->GetName();

            if (const FClassProperty* ClassProp = CastField<FClassProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    if (const UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue_InContainer(RowData)))
                    {
                        Result.MainClassPath = ClassValue->GetPathName();
                    }
                }
                continue;
            }

            if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    const FSoftObjectPtr& SoftClass = SoftClassProp->GetPropertyValue_InContainer(RowData);
                    Result.MainClassPath = SoftClass.ToString();
                }
                continue;
            }

            if (const FSoftObjectProperty* SoftObjectProp = CastField<FSoftObjectProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    const FSoftObjectPtr& SoftObj = SoftObjectProp->GetPropertyValue_InContainer(RowData);
                    Result.MainClassPath = SoftObj.ToString();
                }
                continue;
            }

            if (const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    if (const UObject* Obj = ObjectProp->GetPropertyValue_InContainer(RowData))
                    {
                        Result.MainClassPath = Obj->GetPathName();
                    }
                }
                continue;
            }

            if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
            {
                const FString Value = StrProp->GetPropertyValue_InContainer(RowData);
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    Result.MainClassPath = Value;
                }
                else if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
            {
                const FString Value = NameProp->GetPropertyValue_InContainer(RowData).ToString();
                if (IsPropertyNameMatchLoose(PropName, TEXT("MainClass")))
                {
                    Result.MainClassPath = Value;
                }
                else if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FTextProperty* TextProp = CastField<FTextProperty>(Property))
            {
                const FString Value = TextProp->GetPropertyValue_InContainer(RowData).ToString();
                if (IsPropertyNameMatchLoose(PropName, TEXT("Type")))
                {
                    Result.TypeValue = Value;
                }
                continue;
            }

            if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
            {
                if (IsPropertyNameMatchLoose(PropName, TEXT("PickOnlyWhenBackpack")))
                {
                    Result.bHasPickOnlyWhenBackpack = true;
                    Result.bPickOnlyWhenBackpack = BoolProp->GetPropertyValue_InContainer(RowData);
                }
                continue;
            }

            double NumericValue = 0.0;
            if (!ReadNumericPropertyAsDouble(Property, RowData, NumericValue))
            {
                continue;
            }

            if (IsPropertyNameMatchLoose(PropName, TEXT("MaxQuantity")))
            {
                Result.bHasMaxQuantity = true;
                Result.MaxQuantity = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("CurrentQuantity")))
            {
                Result.bHasCurrentQuantity = true;
                Result.CurrentQuantity = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("Param1")))
            {
                Result.bHasParam1 = true;
                Result.Param1 = NumericValue;
            }
            else if (IsPropertyNameMatchLoose(PropName, TEXT("Param2")))
            {
                Result.bHasParam2 = true;
                Result.Param2 = NumericValue;
            }
        }

        Result.MainClassPath = NormalizeClassPathForComparison(Result.MainClassPath);
        Result.bValid = true;
        OutData = Result;
        return true;
    }

    UDataTable* GetLootItemsDataTable()
    {
        static TWeakObjectPtr<UDataTable> CachedTable;
        if (CachedTable.IsValid())
        {
            return CachedTable.Get();
        }

        static const FSoftObjectPath LootTablePath(TEXT("/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable"));
        UDataTable* LootTable = Cast<UDataTable>(LootTablePath.TryLoad());
        CachedTable = LootTable;
        return LootTable;
    }

    bool ResolveLootRuntimeRowForSpawnedClass(
        const FRaidLootCandidate* Candidate,
        const UClass* SpawnedClass,
        FRaidLootRuntimeRowData& OutData)
    {
        UDataTable* LootTable = GetLootItemsDataTable();
        if (!IsValid(LootTable) || !LootTable->GetRowStruct() || !SpawnedClass)
        {
            return false;
        }

        const FString SpawnedClassPath = NormalizeClassPathForComparison(SpawnedClass->GetPathName());
        const FString SpawnedClassName = SpawnedClass->GetName();

        auto TryReadRow = [LootTable](const FName InRowName, FRaidLootRuntimeRowData& OutRow) -> bool
        {
            const uint8* const* Found = LootTable->GetRowMap().Find(InRowName);
            if (!Found || !*Found)
            {
                return false;
            }
            return TryExtractLootRuntimeRowData(InRowName, LootTable->GetRowStruct(), *Found, OutRow);
        };

        if (Candidate && !Candidate->DataRowName.IsNone())
        {
            FRaidLootRuntimeRowData CandidateRow;
            if (TryReadRow(Candidate->DataRowName, CandidateRow))
            {
                OutData = CandidateRow;
                return true;
            }
        }

        for (const TPair<FName, uint8*>& RowPair : LootTable->GetRowMap())
        {
            FRaidLootRuntimeRowData RowData;
            if (!TryExtractLootRuntimeRowData(RowPair.Key, LootTable->GetRowStruct(), RowPair.Value, RowData))
            {
                continue;
            }

            const FString RowClassPath = NormalizeClassPathForComparison(RowData.MainClassPath);
            const bool bClassPathMatch =
                !RowClassPath.IsEmpty() &&
                (RowClassPath.Equals(SpawnedClassPath, ESearchCase::IgnoreCase) ||
                    RowClassPath.Contains(SpawnedClassPath, ESearchCase::IgnoreCase) ||
                    SpawnedClassPath.Contains(RowClassPath, ESearchCase::IgnoreCase));

            const bool bClassNameMatch =
                !RowClassPath.IsEmpty() &&
                RowClassPath.Contains(SpawnedClassName, ESearchCase::IgnoreCase);

            if (bClassPathMatch || bClassNameMatch)
            {
                OutData = RowData;
                return true;
            }
        }

        return false;
    }

    int32 ApplyLootRuntimeRowDataToActor(
        AActor* LootActor,
        const FRaidLootRuntimeRowData& RowData,
        const bool bApplyParamValues,
        const bool bApplyQuantityValues,
        const bool bApplyPickupRestrictionValues,
        const bool bAllowModelIndexFromParam1)
    {
        if (!IsValid(LootActor) || !RowData.bValid)
        {
            return 0;
        }

        auto ApplyToObject = [&RowData, bApplyParamValues, bApplyQuantityValues, bApplyPickupRestrictionValues, bAllowModelIndexFromParam1](UObject* TargetObject) -> int32
            {
                if (!IsValid(TargetObject))
                {
                    return 0;
                }

                const bool bHasExplicitRowOverride = HasExplicitLootDataRowOverride(TargetObject);
                int32 AppliedCountLocal = 0;
                for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
                {
                    FProperty* Property = *PropIt;
                    if (!Property)
                    {
                        continue;
                    }

                    const FString PropName = Property->GetName();

                    if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
                    {
                        if (bApplyPickupRestrictionValues && RowData.bHasPickOnlyWhenBackpack && IsPropertyNameMatchLoose(PropName, TEXT("PickOnlyWhenBackpack")))
                        {
                            const bool bCurrent = BoolProp->GetPropertyValue_InContainer(TargetObject);
                            if (bCurrent != RowData.bPickOnlyWhenBackpack)
                            {
                                const_cast<FBoolProperty*>(BoolProp)->SetPropertyValue_InContainer(TargetObject, RowData.bPickOnlyWhenBackpack);
                                ++AppliedCountLocal;
                            }
                        }
                        continue;
                    }

                    if (bApplyQuantityValues && RowData.bHasMaxQuantity && IsPropertyNameMatchLoose(PropName, TEXT("MaxQuantity")))
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.MaxQuantity))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (bApplyQuantityValues && RowData.bHasCurrentQuantity && IsPropertyNameMatchLoose(PropName, TEXT("CurrentQuantity")))
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.CurrentQuantity))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bParam1Field = IsPropertyNameMatchLoose(PropName, TEXT("Param1"));
                    if (bApplyParamValues && RowData.bHasParam1 && bParam1Field)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param1))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bModelIndexField = IsPropertyNameMatchAnyLoose(
                        PropName,
                        {
                            TEXT("RifleModelIndex"),
                            TEXT("WeaponModelIndex"),
                            TEXT("GunModelIndex")
                        });

                    if (bApplyParamValues && RowData.bHasParam1 && bModelIndexField && bAllowModelIndexFromParam1 && !bHasExplicitRowOverride)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param1))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    const bool bParam2Field =
                        IsPropertyNameMatchLoose(PropName, TEXT("Param2"));

                    if (bApplyParamValues && RowData.bHasParam2 && bParam2Field)
                    {
                        if (WriteNumericPropertyFromDouble(Property, TargetObject, RowData.Param2))
                        {
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
                    {
                        if (!RowData.RowName.IsEmpty() &&
                            IsPropertyNameMatchAnyLoose(
                                PropName,
                                {
                                    TEXT("DataRowName"),
                                    TEXT("LootDataRowName"),
                                    TEXT("ItemDataRowName"),
                                    TEXT("WeaponDataRowName"),
                                    TEXT("RifleDataRowName"),
                                    TEXT("RifleAssetRowName"),
                                    TEXT("RifleRowName"),
                                    TEXT("WeaponRowName")
                                }))
                        {
                            const_cast<FNameProperty*>(NameProp)->SetPropertyValue_InContainer(TargetObject, FName(*RowData.RowName));
                            ++AppliedCountLocal;
                        }
                        continue;
                    }

                    if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
                    {
                        if (!RowData.RowName.IsEmpty() &&
                            IsPropertyNameMatchAnyLoose(
                                PropName,
                                {
                                    TEXT("DataRowName"),
                                    TEXT("LootDataRowName"),
                                    TEXT("ItemDataRowName"),
                                    TEXT("WeaponDataRowName"),
                                    TEXT("RifleDataRowName"),
                                    TEXT("RifleAssetRowName"),
                                    TEXT("RifleRowName"),
                                    TEXT("WeaponRowName")
                                }))
                        {
                            const_cast<FStrProperty*>(StrProp)->SetPropertyValue_InContainer(TargetObject, RowData.RowName);
                            ++AppliedCountLocal;
                        }
                        continue;
                    }
                }

                return AppliedCountLocal;
            };

        int32 AppliedCount = 0;
        AppliedCount += ApplyToObject(LootActor);

        TInlineComponentArray<UActorComponent*> Components(LootActor);
        for (UActorComponent* Component : Components)
        {
            AppliedCount += ApplyToObject(Component);
        }

        return AppliedCount;
    }

    bool BuildWorldFootprintFromLocalBounds(const FBox& LocalBounds, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!LocalBounds.IsValid)
        {
            return false;
        }

        FBox2D Footprint(EForceInit::ForceInit);
        const FVector BoundsMin = LocalBounds.Min;
        const FVector BoundsMax = LocalBounds.Max;

        const FVector Corners[8] =
        {
            FVector(BoundsMin.X, BoundsMin.Y, BoundsMin.Z),
            FVector(BoundsMin.X, BoundsMin.Y, BoundsMax.Z),
            FVector(BoundsMin.X, BoundsMax.Y, BoundsMin.Z),
            FVector(BoundsMin.X, BoundsMax.Y, BoundsMax.Z),
            FVector(BoundsMax.X, BoundsMin.Y, BoundsMin.Z),
            FVector(BoundsMax.X, BoundsMin.Y, BoundsMax.Z),
            FVector(BoundsMax.X, BoundsMax.Y, BoundsMin.Z),
            FVector(BoundsMax.X, BoundsMax.Y, BoundsMax.Z)
        };

        for (const FVector& Corner : Corners)
        {
            const FVector WorldCorner = WorldTransform.TransformPosition(Corner);
            Footprint += FVector2D(WorldCorner.X, WorldCorner.Y);
        }

        if (!Footprint.bIsValid)
        {
            return false;
        }

        OutFootprint = Footprint;
        return true;
    }

    bool TryBuildFootprintFromStaticMesh(const UStaticMesh* Mesh, const FTransform& WorldTransform, FBox2D& OutFootprint)
    {
        if (!Mesh)
        {
            return false;
        }

        return BuildWorldFootprintFromLocalBounds(Mesh->GetBoundingBox(), WorldTransform, OutFootprint);
    }

    bool TryBuildFootprintFromActor(const AActor* Actor, FBox2D& OutFootprint)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        const FBox ActorBounds = Actor->GetComponentsBoundingBox(false);
        if (!ActorBounds.IsValid)
        {
            return false;
        }

        OutFootprint = FBox2D(
            FVector2D(ActorBounds.Min.X, ActorBounds.Min.Y),
            FVector2D(ActorBounds.Max.X, ActorBounds.Max.Y));
        return OutFootprint.bIsValid;
    }

    bool HasSimpleCollisionGeometry(const UBodySetup* BodySetup)
    {
        if (!BodySetup)
        {
            return false;
        }

        const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
        return
            AggGeom.BoxElems.Num() > 0 ||
            AggGeom.SphereElems.Num() > 0 ||
            AggGeom.SphylElems.Num() > 0 ||
            AggGeom.ConvexElems.Num() > 0;
    }

    bool ShouldForceWalkableCollisionForMeshType(const int32 MeshType)
    {
        // Limit aggressive collision fallback to core obstacle geometry only.
        // Applying this to decoration/foliage made movement on top of meshes unstable.
        return (MeshType == 2);
    }

    void EnsureMeshWalkableCollisionForRoom(UStaticMesh* Mesh, const int32 MeshType)
    {
        if (!IsValid(Mesh) || !ShouldForceWalkableCollisionForMeshType(MeshType))
        {
            return;
        }

        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (!BodySetup)
        {
            return;
        }

        if (HasSimpleCollisionGeometry(BodySetup) || BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
        {
            return;
        }

#if WITH_EDITOR
        Mesh->Modify();
        BodySetup->Modify();
#endif
        BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
#if WITH_EDITOR
        Mesh->MarkPackageDirty();
#endif
    }

    bool ShouldRejectGroundSupportHit(UWorld* World, const FHitResult& Hit)
    {
        if (!Hit.bBlockingHit)
        {
            return true;
        }

        if (IsWaterHit(Hit) || IsInsideWaterPhysicsVolume(World, Hit.ImpactPoint, 120.0f))
        {
            return true;
        }

        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        if (IsValid(HitActor) && HitActor->ActorHasTag(TEXT("RaidDoorBlocker")))
        {
            return true;
        }

        if (IsValid(HitComp))
        {
            if (HitComp->bHiddenInGame || !HitComp->IsVisible())
            {
                return true;
            }
        }

        if (IsValid(HitActor))
        {
            if (HitActor->IsHidden())
            {
                return true;
            }

            if (HitActor->ActorHasTag(RaidRoomGeneratedTag))
            {
                const bool bHiddenGenerated =
                    HitActor->IsHidden() ||
                    (IsValid(HitComp) && (HitComp->bHiddenInGame || !HitComp->IsVisible()));
                if (bHiddenGenerated)
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool TryResolveSingleGroundHitAlongTrace(
        UWorld* World,
        const FVector& TraceStart,
        const FVector& TraceEnd,
        bool bPreferLandscape,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit)
    {
        if (!World)
        {
            return false;
        }

        TArray<FHitResult> GroundHits;
        if (!World->LineTraceMultiByChannel(
            GroundHits,
            TraceStart,
            TraceEnd,
            ECC_WorldStatic,
            QueryParams))
        {
            return false;
        }

        const FHitResult* FirstValidHit = nullptr;
        const FHitResult* PreferredLandscapeHit = nullptr;
        for (const FHitResult& CandidateHit : GroundHits)
        {
            if (ShouldRejectGroundSupportHit(World, CandidateHit))
            {
                continue;
            }

            if (!FirstValidHit)
            {
                FirstValidHit = &CandidateHit;
            }

            if (IsLandscapeLikeHit(CandidateHit))
            {
                PreferredLandscapeHit = &CandidateHit;
                break;
            }
        }

        const FHitResult* SelectedGroundHit =
            (bPreferLandscape && PreferredLandscapeHit)
            ? PreferredLandscapeHit
            : FirstValidHit;
        if (!SelectedGroundHit)
        {
            return false;
        }

        OutHit = *SelectedGroundHit;
        return true;
    }

    bool TryResolveActorLowestSupportZ(const AActor* Actor, float& OutMinZ)
    {
        OutMinZ = TNumericLimits<float>::Max();
        if (!IsValid(Actor))
        {
            return false;
        }

        bool bFoundCollisionEnabledPrimitive = false;
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
        Actor->GetComponents(PrimitiveComps);

        for (const UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
        {
            if (!IsValid(PrimitiveComp) || !PrimitiveComp->IsRegistered())
            {
                continue;
            }

            if (!PrimitiveComp->IsCollisionEnabled())
            {
                continue;
            }

            const FBox PrimitiveBounds = PrimitiveComp->Bounds.GetBox();
            if (!PrimitiveBounds.IsValid)
            {
                continue;
            }

            OutMinZ = FMath::Min(OutMinZ, PrimitiveBounds.Min.Z);
            bFoundCollisionEnabledPrimitive = true;
        }

        if (bFoundCollisionEnabledPrimitive && OutMinZ < TNumericLimits<float>::Max())
        {
            return true;
        }

        const FBox FallbackBounds = Actor->GetComponentsBoundingBox(true);
        if (FallbackBounds.IsValid)
        {
            OutMinZ = FallbackBounds.Min.Z;
            return true;
        }

        return false;
    }

    bool IsFootprintOverlappingAny(const TArray<FBox2D>& ExistingFootprints, const FBox2D& CandidateFootprint, float Padding)
    {
        if (!CandidateFootprint.bIsValid)
        {
            return false;
        }

        FBox2D ExpandedCandidate = CandidateFootprint;
        const FVector2D Padding2D(FMath::Max(0.0f, Padding), FMath::Max(0.0f, Padding));
        ExpandedCandidate.Min -= Padding2D;
        ExpandedCandidate.Max += Padding2D;

        for (const FBox2D& Existing : ExistingFootprints)
        {
            if (!Existing.bIsValid)
            {
                continue;
            }
            if (ExpandedCandidate.Intersect(Existing))
            {
                return true;
            }
        }

        return false;
    }

    bool TryResolveRoomSingleGroundHitAtPoint(
        UWorld* World,
        const FVector& XYLocation,
        bool bPreferLandscape,
        const FCollisionQueryParams& QueryParams,
        FHitResult& OutHit)
    {
        return TryResolveSingleGroundHitAlongTrace(
            World,
            XYLocation + FVector(0.0f, 0.0f, 120000.0f),
            XYLocation + FVector(0.0f, 0.0f, -120000.0f),
            bPreferLandscape,
            QueryParams,
            OutHit);
    }

    void BuildGroundSupportOffsets(int32 SampleCount, float SampleRadius, TArray<FVector2D>& OutOffsets)
    {
        OutOffsets.Reset();
        OutOffsets.Add(FVector2D::ZeroVector);

        if (SampleCount <= 1 || SampleRadius <= 1.0f)
        {
            return;
        }

        OutOffsets.Add(FVector2D(SampleRadius, 0.0f));
        OutOffsets.Add(FVector2D(-SampleRadius, 0.0f));
        OutOffsets.Add(FVector2D(0.0f, SampleRadius));
        OutOffsets.Add(FVector2D(0.0f, -SampleRadius));

        if (SampleCount >= 9)
        {
            const float Diag = SampleRadius * 0.70710678f;
            OutOffsets.Add(FVector2D(Diag, Diag));
            OutOffsets.Add(FVector2D(-Diag, Diag));
            OutOffsets.Add(FVector2D(Diag, -Diag));
            OutOffsets.Add(FVector2D(-Diag, -Diag));
        }
    }

    bool IsLikelyWindPhaseParamName(const FString& LowerParamName)
    {
        const bool bHasPhaseLikeKeyword =
            LowerParamName.Contains(TEXT("phase")) ||
            LowerParamName.Contains(TEXT("timeoffset")) ||
            LowerParamName.Contains(TEXT("random")) ||
            LowerParamName.Contains(TEXT("offset")) ||
            LowerParamName.Contains(TEXT("variation"));
        if (!bHasPhaseLikeKeyword)
        {
            return false;
        }

        // Never randomize amplitude/speed/strength controls: that causes unstable, unnatural motion.
        if (LowerParamName.Contains(TEXT("strength")) ||
            LowerParamName.Contains(TEXT("intensity")) ||
            LowerParamName.Contains(TEXT("speed")) ||
            LowerParamName.Contains(TEXT("sway")) ||
            LowerParamName.Contains(TEXT("bend")) ||
            LowerParamName.Contains(TEXT("amplitude")) ||
            LowerParamName.Contains(TEXT("weight")) ||
            LowerParamName.Contains(TEXT("gust")))
        {
            return false;
        }

        return
            LowerParamName.Contains(TEXT("wind")) ||
            LowerParamName.Contains(TEXT("tree")) ||
            LowerParamName.Contains(TEXT("foliage")) ||
            LowerParamName.Contains(TEXT("phase")) ||
            LowerParamName.Contains(TEXT("random"));
    }

    void GatherLikelyWindPhaseScalarParams(UMaterialInterface* Material, TArray<FName>& OutParamNames)
    {
        if (!Material)
        {
            return;
        }

        TArray<FMaterialParameterInfo> ScalarInfos;
        TArray<FGuid> ScalarIds;
        Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
        for (const FMaterialParameterInfo& Info : ScalarInfos)
        {
            const FString LowerName = Info.Name.ToString().ToLower();
            if (IsLikelyWindPhaseParamName(LowerName))
            {
                OutParamNames.AddUnique(Info.Name);
            }
        }
    }

    float ResolveWindDesyncValueForParam(const FString& LowerParamName, FRandomStream& Stream)
    {
        if (LowerParamName.Contains(TEXT("phase")))
        {
            return Stream.FRandRange(-PI, PI);
        }
        if (LowerParamName.Contains(TEXT("timeoffset")) || LowerParamName.Contains(TEXT("offset")))
        {
            return Stream.FRandRange(-1.0f, 1.0f);
        }
        if (LowerParamName.Contains(TEXT("random")) || LowerParamName.Contains(TEXT("variation")))
        {
            return Stream.FRandRange(0.0f, 1.0f);
        }
        return Stream.FRandRange(0.0f, 1.0f);
    }

    void ApplyRoomTreeWindPhaseDesync(UStaticMeshComponent* MeshComp, FRandomStream& Stream)
    {
        if (!IsValid(MeshComp))
        {
            return;
        }

        // Keep CPD values normalized (0..1). Some foliage materials remap these to
        // phase internally, and large raw values can cause unstable motion.
        MeshComp->SetCustomPrimitiveDataFloat(0, Stream.FRandRange(0.0f, 1.0f));
        MeshComp->SetCustomPrimitiveDataFloat(1, Stream.FRandRange(0.0f, 1.0f));

        static const FName FallbackParamNames[] = {
            TEXT("WindPhaseOffset"),
            TEXT("WindPhase"),
            TEXT("WindTimeOffset"),
            TEXT("PerInstanceRandom"),
            TEXT("TreeWindOffset"),
            TEXT("FoliageRandom")
        };

        const int32 MaterialCount = MeshComp->GetNumMaterials();
        for (int32 MatIndex = 0; MatIndex < MaterialCount; ++MatIndex)
        {
            UMaterialInterface* BaseMat = MeshComp->GetMaterial(MatIndex);
            if (!BaseMat)
            {
                continue;
            }

            TArray<FName> ParamNamesToSet;
            GatherLikelyWindPhaseScalarParams(BaseMat, ParamNamesToSet);
            if (ParamNamesToSet.Num() == 0)
            {
                for (const FName ParamName : FallbackParamNames)
                {
                    ParamNamesToSet.Add(ParamName);
                }
            }

            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BaseMat);
            if (!MID)
            {
                MID = UMaterialInstanceDynamic::Create(BaseMat, MeshComp);
                if (!MID)
                {
                    continue;
                }
                MeshComp->SetMaterial(MatIndex, MID);
            }

            for (const FName ParamName : ParamNamesToSet)
            {
                const FString LowerParamName = ParamName.ToString().ToLower();
                const float ParamValue = ResolveWindDesyncValueForParam(LowerParamName, Stream);
                MID->SetScalarParameterValue(ParamName, ParamValue);
            }
        }
    }
}

ARaidRoomActor::ARaidRoomActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    PrimaryActorTick.TickInterval = RoomTickInterval;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    RootComponent = SceneRoot;

    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
    Trigger->SetMobility(EComponentMobility::Movable);
    Trigger->SetupAttachment(RootComponent);
    Trigger->OnComponentBeginOverlap.AddDynamic(this, &ARaidRoomActor::OnOverlap);
    Trigger->ShapeColor = FColor::Green; Trigger->SetLineThickness(5.0f);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCanEverAffectNavigation(false);
    // Keep trigger out of typical WorldDynamic object traces used by weapon systems.
    Trigger->SetCollisionObjectType(ECC_Destructible);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Trigger->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);

    StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
    StatusText->SetMobility(EComponentMobility::Movable);
    StatusText->SetupAttachment(RootComponent);

    // =========================================================================
    // 1. 글씨 크기 50% 축소: 기존 800.0f -> 400.0f
    StatusText->SetWorldSize(400.0f);

    // 2. 높이 20m & 중심점 맞춤: X=0, Y=0 (정중앙), Z=2000.0f (20미터 상공)
    StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 2000.0f));
    // =========================================================================

    StatusText->SetHorizontalAlignment(EHTA_Center);
    StatusText->SetVerticalAlignment(EVRTA_TextCenter);
    StatusText->SetTextRenderColor(FColor(244, 244, 170, 255));
    StatusText->bAlwaysRenderAsText = true;
    StatusText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusText->SetCollisionResponseToAllChannels(ECR_Ignore);
}

// 카메라 빌보드(Billboard) 로직 (가장 가벼운 연산)
void ARaidRoomActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
    const bool bHasPlayer = IsValid(PlayerPawn) && PlayerPawn->IsPlayerControlled();
    const float MaxTickWorkDistanceSq = FMath::Square(FMath::Max(1000.0f, StatusTextFacingMaxDistance));
    const bool bNearRoom = bHasPlayer && FVector::DistSquared2D(PlayerPawn->GetActorLocation(), GetActorLocation()) <= MaxTickWorkDistanceSq;
    const float NearTickInterval = FMath::Max(0.01f, RoomTickInterval);
    const float FarTickInterval = FMath::Max(0.22f, NearTickInterval * 2.75f);
    const float DesiredTickInterval = bNearRoom ? NearTickInterval : FarTickInterval;
    if (!FMath::IsNearlyEqual(PrimaryActorTick.TickInterval, DesiredTickInterval, 0.01f))
    {
        PrimaryActorTick.TickInterval = DesiredTickInterval;
    }

    if (!bEntryBannerShown)
    {
        if (bNearRoom)
        {
            const bool bInsideBannerZone = IsPawnInsideRoomBannerZone(this, PlayerPawn);
            const bool bEnteredBannerZone = bInsideBannerZone && !bWasPlayerInsideBannerZone;

            if (bEnteredBannerZone && !bPendingBannerRetry)
            {
                bPendingBannerRetry = true;
                NextBannerAttemptTimeSeconds = 0.0;
            }

            if (bPendingBannerRetry)
            {
                if (!bInsideBannerZone)
                {
                    bPendingBannerRetry = false;
                    NextBannerAttemptTimeSeconds = 0.0;
                }
                else if (World->GetTimeSeconds() >= NextBannerAttemptTimeSeconds)
                {
                    bPendingBannerRetry = !TryShowRegionBanner(PlayerPawn);
                }
            }

            bWasPlayerInsideBannerZone = bInsideBannerZone;
        }
        else
        {
            bWasPlayerInsideBannerZone = false;
        }
    }

    if (bEnableRoomProximityAutoStart && bHasPlayer && !bCombatStarted && !bCombatCleared && IsProximityAutoStartEligibleRoomType())
    {
        const float TriggerDistanceUU = ResolveProximityAutoStartDistanceUU();
        if (TriggerDistanceUU > KINDA_SMALL_NUMBER)
        {
            const float DistSq2D = FVector::DistSquared2D(PlayerPawn->GetActorLocation(), GetActorLocation());
            if (DistSq2D <= FMath::Square(TriggerDistanceUU))
            {
                if (URaidCombatSubsystem* CombatSubsystem = World->GetSubsystem<URaidCombatSubsystem>())
                {
                    CombatSubsystem->StartCombatForRoom(this);
                    if (IsValid(Trigger))
                    {
                        Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    }
                }
            }
        }
    }

    if (World->GetTimeSeconds() >= NextLootOutlineUpdateTimeSeconds)
    {
        const APawn* OutlinePlayerPawn = bHasPlayer ? PlayerPawn : nullptr;
        UpdateLootProximityOutline(OutlinePlayerPawn);
        NextLootOutlineUpdateTimeSeconds = World->GetTimeSeconds() + FMath::Max(0.02f, LootOutlineUpdateInterval);
    }

    if (StatusText && bNearRoom)
    {
        APlayerCameraManager* CamManager = PC ? PC->PlayerCameraManager : nullptr;
        if (CamManager)
        {
            FVector CamLoc = CamManager->GetCameraLocation();
            FVector TextLoc = StatusText->GetComponentLocation();

            // 카메라를 쳐다보는 각도 계산 (현재 프로젝트 텍스트 전면 기준에 맞춤)
            FRotator LookAtRot = (CamLoc - TextLoc).Rotation();
            LookAtRot.Yaw += 360.0f;
            LookAtRot.Pitch = 0.0f; // 좌우로만 회전하게 고정 (위아래로 누우면 찌그러져 보임)

            StatusText->SetWorldRotation(LookAtRot);
        }
    }
}
void ARaidRoomActor::BeginPlay()
{
    Super::BeginPlay();
    if (!ChapterConfigRef)
    {
        ChapterConfigRef = ChapterConfigAsset.Get();
    }
    PrimaryActorTick.TickInterval = FMath::Max(0.01f, RoomTickInterval);
    EnsureLootOutlinePostProcess(GetWorld());

    UClass* WarmupWidgetClass = RegionBannerWidgetClass.IsNull()
        ? nullptr
        : RegionBannerWidgetClass.LoadSynchronous();
    if (!WarmupWidgetClass)
    {
        WarmupWidgetClass = LoadClass<URaidRegionBannerWidget>(
            nullptr,
            TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C"));
    }
    CachedRegionBannerWidgetClass = WarmupWidgetClass;

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (SharedWidget->GetWorld() != GetWorld())
        {
            if (SharedWidget->IsInViewport())
            {
                SharedWidget->RemoveFromParent();
            }
            GSharedRegionBannerWidget.Reset();
        }
    }
}

void ARaidRoomActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    PendingNavUpdateISMCs.Reset();

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RegionBannerHideTimerHandle);
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (SharedWidget->IsInViewport())
        {
            SharedWidget->RemoveFromParent();
        }
    }
    GSharedRegionBannerWidget.Reset();

    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->RemoveFromParent();
    }
    ActiveRegionBannerWidget = nullptr;
    bPendingBannerRetry = false;

    for (const TWeakObjectPtr<AActor>& WeakLootActor : SpawnedLootActors)
    {
        if (AActor* LootActor = WeakLootActor.Get())
        {
            SetLootActorProximityFx(LootActor, false);
            SetLootActorOutline(LootActor, false);
            SetLootActorDotWidget(LootActor, false);
        }
    }
    SpawnedLootActors.Reset();
    ClearLootProximityFxCache();
    ClearLootDotWidgetCache();
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;

    Super::EndPlay(EndPlayReason);
}

void ARaidRoomActor::SetNodeData(int32 InNodeId, const FLevelNodeRow& InNodeRow, const URaidChapterConfig* InConfig)
{
    for (const TWeakObjectPtr<AActor>& WeakLootActor : SpawnedLootActors)
    {
        if (AActor* LootActor = WeakLootActor.Get())
        {
            SetLootActorProximityFx(LootActor, false);
            SetLootActorOutline(LootActor, false);
            SetLootActorDotWidget(LootActor, false);
        }
    }
    SpawnedLootActors.Reset();
    ClearLootProximityFxCache();
    ClearLootDotWidgetCache();
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;

    CachedResolvedThemeKit = nullptr;
    CachedResolvedThemeKey.Reset();

    NodeId = InNodeId;
    NodeRow = InNodeRow;
    ChapterConfigRef = InConfig;
    ChapterConfigAsset = const_cast<URaidChapterConfig*>(InConfig);
    CurrentRoomType = RaidRoomParsing::ParseRoomType(NodeRow.RoomType);
    RoomRandomStream.Initialize(NodeRow.Seed);
    if (ChapterConfigRef)
    {
        ChapterConfigRef->ResolveThemeKitForNode(NodeRow, CachedResolvedThemeKey, CachedResolvedThemeKit);
    }

    ApplyGridSizeFromRoomSizeToken(NodeRow.RoomSize, GridSize);
    bNodeDataInitialized = true; bLootAlreadySpawned = false; bEntryBannerShown = false; bPendingBannerRetry = false; bWasPlayerInsideBannerZone = false; NextBannerAttemptTimeSeconds = 0.0; NextLootOutlineUpdateTimeSeconds = 0.0; CachedProximityAutoStartDistanceUU = -1.0f;
    CachedLootProximityFxTemplate = nullptr;
    CachedLootProximityFxTemplatePath.Reset();
    bLootProximityFxTemplateResolveAttempted = false;
    bLootProximityFxTemplateResolveLogged = false;
    CachedLootDotWidgetClass = nullptr;
    CachedLootDotWidgetClassPath.Reset();
    bLootDotWidgetResolveAttempted = false;
    bLootDotWidgetResolveLogged = false;
}

void ARaidRoomActor::SetLootActorOutline(AActor* LootActor, bool bEnable) const
{
    if (!IsValid(LootActor))
    {
        return;
    }

    auto ApplyOutlineToActor = [this, bEnable](AActor* TargetActor)
    {
        if (!IsValid(TargetActor))
        {
            return;
        }

        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(TargetActor);
        for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
        {
            if (!IsValid(PrimitiveComponent))
            {
                continue;
            }

            PrimitiveComponent->SetRenderCustomDepth(bEnable);
            if (bEnable)
            {
                PrimitiveComponent->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
                PrimitiveComponent->SetCustomDepthStencilValue(FMath::Clamp(LootOutlineStencilValue, 1, 255));
            }
        }
    };

    ApplyOutlineToActor(LootActor);

    TArray<AActor*> AttachedActors;
    LootActor->GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        ApplyOutlineToActor(AttachedActor);
    }

    if (bEnable)
    {
        LootActor->Tags.AddUnique(RaidLootOutlineActiveTag);
    }
    else
    {
        LootActor->Tags.Remove(RaidLootOutlineActiveTag);
    }
}

void ARaidRoomActor::EnsureLootOutlinePostProcess(UWorld* World)
{
    if (!World || !bEnableLootProximityOutline || !bAutoInstallLootOutlinePostProcess)
    {
        return;
    }

    if (GLootOutlinePPAttemptedWorlds.Contains(World))
    {
        return;
    }
    GLootOutlinePPAttemptedWorlds.Add(World);

    UMaterialInterface* OutlineMaterial = LootOutlinePostProcessMaterial.Get();
    if (!OutlineMaterial && !LootOutlinePostProcessMaterial.IsNull())
    {
        OutlineMaterial = LootOutlinePostProcessMaterial.LoadSynchronous();
    }
    if (!IsValid(OutlineMaterial))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Loot outline post-process material is missing: %s"),
            *LootOutlinePostProcessMaterial.ToString());
        return;
    }

    APostProcessVolume* CandidateUnboundVolume = nullptr;
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        APostProcessVolume* Volume = *It;
        if (!IsValid(Volume))
        {
            continue;
        }

        if (HasBlendableMaterial(Volume->Settings, OutlineMaterial))
        {
            return;
        }

        if (Volume->bUnbound && !CandidateUnboundVolume)
        {
            CandidateUnboundVolume = Volume;
        }
    }

    if (IsValid(CandidateUnboundVolume))
    {
        CandidateUnboundVolume->Settings.AddBlendable(OutlineMaterial, 1.0f);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Added loot outline blendable to existing unbound post-process volume '%s'."),
            *GetNameSafe(CandidateUnboundVolume));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= ResolveRoomSpawnObjectFlags(World);
    APostProcessVolume* SpawnedVolume = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!IsValid(SpawnedVolume))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidRoom] Failed to spawn fallback post-process volume for loot outline."));
        return;
    }

    SpawnedVolume->bUnbound = true;
    SpawnedVolume->Priority = 1000.0f;
    SpawnedVolume->BlendWeight = 1.0f;
    SpawnedVolume->Settings.AddBlendable(OutlineMaterial, 1.0f);
    SpawnedVolume->SetActorHiddenInGame(true);
    SpawnedVolume->SetCanBeDamaged(false);

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidRoom] Spawned fallback unbound post-process volume for loot outline: '%s'."),
        *GetNameSafe(SpawnedVolume));
}

UParticleSystem* ARaidRoomActor::ResolveLootProximityFxTemplate()
{
    const FSoftObjectPath RequestedPath = LootProximityFxTemplate.ToSoftObjectPath();
    if (bLootProximityFxTemplateResolveAttempted && RequestedPath == CachedLootProximityFxTemplatePath)
    {
        return CachedLootProximityFxTemplate;
    }

    bLootProximityFxTemplateResolveAttempted = true;
    CachedLootProximityFxTemplatePath = RequestedPath;
    CachedLootProximityFxTemplate = LootProximityFxTemplate.Get();

    if (!CachedLootProximityFxTemplate && !LootProximityFxTemplate.IsNull())
    {
        CachedLootProximityFxTemplate = LootProximityFxTemplate.LoadSynchronous();
    }

    if (!CachedLootProximityFxTemplate)
    {
        static const FSoftObjectPath FallbackPath(TEXT("/Game/TemplesOfCambodia/Demo/EpicContent/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust"));
        CachedLootProximityFxTemplate = Cast<UParticleSystem>(FallbackPath.TryLoad());
        if (CachedLootProximityFxTemplate)
        {
            LootProximityFxTemplate = TSoftObjectPtr<UParticleSystem>(FallbackPath);
            CachedLootProximityFxTemplatePath = FallbackPath;
            bLootProximityFxTemplateResolveAttempted = true;
            if (!bLootProximityFxTemplateResolveLogged)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] LootProximityFxTemplate invalid. Falling back to %s"),
                    *FallbackPath.ToString());
                bLootProximityFxTemplateResolveLogged = true;
            }
        }
    }

    return CachedLootProximityFxTemplate;
}

UClass* ARaidRoomActor::ResolveLootDotWidgetClass()
{
    const FSoftObjectPath RequestedPath = LootDotWidgetClass.ToSoftObjectPath();
    if (bLootDotWidgetResolveAttempted && RequestedPath == CachedLootDotWidgetClassPath)
    {
        return CachedLootDotWidgetClass.Get();
    }

    bLootDotWidgetResolveAttempted = true;
    CachedLootDotWidgetClassPath = RequestedPath;
    CachedLootDotWidgetClass = LootDotWidgetClass.Get();

    if (!CachedLootDotWidgetClass && !LootDotWidgetClass.IsNull())
    {
        CachedLootDotWidgetClass = LootDotWidgetClass.LoadSynchronous();
    }

    if (!CachedLootDotWidgetClass)
    {
        static const FSoftObjectPath FallbackPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_ItemDot.WBP_ItemDot_C"));
        CachedLootDotWidgetClass = TSubclassOf<UUserWidget>(LoadClass<UUserWidget>(nullptr, *FallbackPath.ToString()));
        if (CachedLootDotWidgetClass)
        {
            LootDotWidgetClass = TSoftClassPtr<UUserWidget>(FallbackPath);
            CachedLootDotWidgetClassPath = FallbackPath;
            bLootDotWidgetResolveAttempted = true;
            if (!bLootDotWidgetResolveLogged)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidRoom] LootDotWidgetClass invalid. Falling back to %s"),
                    *FallbackPath.ToString());
                bLootDotWidgetResolveLogged = true;
            }
        }
    }

    return CachedLootDotWidgetClass.Get();
}

bool ARaidRoomActor::ShouldSuppressLootProximityIndicators(const AActor* LootActor) const
{
    if (!IsValid(LootActor))
    {
        return true;
    }

    if (LootActor->IsHidden() || LootActor->IsActorBeingDestroyed())
    {
        return true;
    }

    const AActor* AttachParentActor = LootActor->GetAttachParentActor();
    if (IsValid(AttachParentActor))
    {
        if (AttachParentActor->IsA<APawn>() || AttachParentActor->IsA<ACharacter>())
        {
            return true;
        }
    }

    return false;
}

void ARaidRoomActor::SetLootActorDotWidget(AActor* LootActor, bool bEnable)
{
    if (!IsValid(LootActor))
    {
        return;
    }

    TWeakObjectPtr<AActor> LootKey(LootActor);
    TWeakObjectPtr<UWidgetComponent>* FoundComponentPtr = LootDotWidgetComponents.Find(LootKey);
    UWidgetComponent* DotComponent = FoundComponentPtr ? FoundComponentPtr->Get() : nullptr;

    if (!bEnable)
    {
        if (IsValid(DotComponent))
        {
            DotComponent->SetVisibility(false, true);
        }
        return;
    }

    UClass* DotWidgetClass = ResolveLootDotWidgetClass();
    if (!IsValid(DotWidgetClass))
    {
        return;
    }

    if (!IsValid(DotComponent))
    {
        DotComponent = NewObject<UWidgetComponent>(LootActor);
        if (!IsValid(DotComponent))
        {
            return;
        }

        DotComponent->CreationMethod = EComponentCreationMethod::Instance;
        DotComponent->SetWidgetSpace(EWidgetSpace::Screen);
        DotComponent->SetDrawAtDesiredSize(true);
        DotComponent->SetCanEverAffectNavigation(false);
        DotComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DotComponent->SetGenerateOverlapEvents(false);
        DotComponent->SetVisibility(false, true);
        DotComponent->SetWidgetClass(DotWidgetClass);

        LootActor->AddInstanceComponent(DotComponent);
        DotComponent->RegisterComponent();
        LootDotWidgetComponents.Add(LootKey, DotComponent);
    }

    if (DotComponent->GetWidgetClass() != DotWidgetClass)
    {
        DotComponent->SetWidgetClass(DotWidgetClass);
    }

    const FVector DotWorldLocation = LootActor->GetActorLocation() + FVector(0.0f, 0.0f, LootDotHeightOffset);
    DotComponent->SetWorldLocation(DotWorldLocation);
    DotComponent->SetVisibility(true, true);
}

void ARaidRoomActor::SetLootActorProximityFx(AActor* LootActor, bool bEnable)
{
    if (!IsValid(LootActor))
    {
        return;
    }

    TWeakObjectPtr<AActor> LootKey(LootActor);
    TWeakObjectPtr<UParticleSystemComponent>* FoundComponentPtr = LootProximityFxComponents.Find(LootKey);
    UParticleSystemComponent* FxComponent = FoundComponentPtr ? FoundComponentPtr->Get() : nullptr;

    if (!bEnable)
    {
        if (IsValid(FxComponent))
        {
            FxComponent->DeactivateSystem();
            FxComponent->SetVisibility(false, true);
        }
        return;
    }

    UParticleSystem* FxTemplate = ResolveLootProximityFxTemplate();
    if (!FxTemplate)
    {
        return;
    }

    if (!IsValid(FxComponent))
    {
        FxComponent = NewObject<UParticleSystemComponent>(LootActor);
        if (!IsValid(FxComponent))
        {
            return;
        }

        FxComponent->CreationMethod = EComponentCreationMethod::Instance;
        FxComponent->SetTemplate(FxTemplate);
        FxComponent->bAutoActivate = false;
        FxComponent->bAutoDestroy = false;
        FxComponent->SetCanEverAffectNavigation(false);
        FxComponent->SetAbsolute(false, false, false);
        FxComponent->SetHiddenInGame(false, true);
        FxComponent->SetRelativeLocation(LootProximityFxOffset);
        FxComponent->SetRelativeScale3D(FVector(LootProximityFxScale));

        if (USceneComponent* RootComp = LootActor->GetRootComponent())
        {
            FxComponent->SetupAttachment(RootComp);
        }
        else
        {
            FxComponent->SetWorldLocation(LootActor->GetActorLocation() + LootProximityFxOffset);
        }

        LootActor->AddInstanceComponent(FxComponent);
        FxComponent->RegisterComponent();
        LootProximityFxComponents.Add(LootKey, FxComponent);
    }

    FxComponent->SetTemplate(FxTemplate);
    if (!FxComponent->IsVisible())
    {
        FxComponent->SetVisibility(true, true);
    }

    if (USceneComponent* RootComp = LootActor->GetRootComponent())
    {
        if (FxComponent->GetAttachParent() != RootComp)
        {
            FxComponent->AttachToComponent(RootComp, FAttachmentTransformRules::KeepRelativeTransform);
        }
        FxComponent->SetRelativeLocation(LootProximityFxOffset);
    }
    else
    {
        FxComponent->SetWorldLocation(LootActor->GetActorLocation() + LootProximityFxOffset);
    }
    FxComponent->SetRelativeLocation(LootProximityFxOffset);
    FxComponent->SetRelativeScale3D(FVector(LootProximityFxScale));
    if (!FxComponent->IsActive())
    {
        FxComponent->ActivateSystem(true);
    }
}

void ARaidRoomActor::ClearLootProximityFxCache()
{
    UWorld* World = GetWorld();
    const bool bSafeDestroy = !GExitPurge && World && !World->bIsTearingDown;

    for (TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UParticleSystemComponent>>& Pair : LootProximityFxComponents)
    {
        if (UParticleSystemComponent* FxComponent = Pair.Value.Get())
        {
            FxComponent->DeactivateSystem();
            FxComponent->SetVisibility(false, true);
            if (FxComponent->IsRegistered())
            {
                FxComponent->UnregisterComponent();
            }
            if (bSafeDestroy)
            {
                FxComponent->DestroyComponent();
            }
        }
    }
    LootProximityFxComponents.Reset();
}

void ARaidRoomActor::ClearLootDotWidgetCache()
{
    UWorld* World = GetWorld();
    const bool bSafeDestroy = !GExitPurge && World && !World->bIsTearingDown;

    for (TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UWidgetComponent>>& Pair : LootDotWidgetComponents)
    {
        if (UWidgetComponent* DotComponent = Pair.Value.Get())
        {
            DotComponent->SetVisibility(false, true);
            if (DotComponent->IsRegistered())
            {
                DotComponent->UnregisterComponent();
            }
            if (bSafeDestroy)
            {
                DotComponent->DestroyComponent();
            }
        }
    }
    LootDotWidgetComponents.Reset();
}

void ARaidRoomActor::UpdateLootProximityOutline(const APawn* PlayerPawn)
{
    if (SpawnedLootActors.Num() <= 0)
    {
        ClearLootProximityFxCache();
        ClearLootDotWidgetCache();
        return;
    }

    const bool bCanUseOutline =
        bEnableLootProximityOutline &&
        IsValid(PlayerPawn);
    const FVector PlayerLocation = bCanUseOutline ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    const float OutlineDistanceSq = FMath::Square(FMath::Max(50.0f, LootOutlineDistance));
    const bool bCanUseFx =
        bEnableLootProximityFx &&
        IsValid(PlayerPawn);
    const float FxDistanceSq = FMath::Square(FMath::Max(50.0f, LootProximityFxDistance));
    const bool bLootFxTemplateReady = bCanUseFx && ResolveLootProximityFxTemplate();
    const bool bCanUseDot =
        bEnableLootProximityDot &&
        IsValid(PlayerPawn);
    const float DotDistanceSq = FMath::Square(FMath::Max(50.0f, LootDotDistance));
    const bool bLootDotClassReady = bCanUseDot && IsValid(ResolveLootDotWidgetClass());

    int32 WriteIndex = 0;
    for (int32 Index = 0; Index < SpawnedLootActors.Num(); ++Index)
    {
        AActor* LootActor = SpawnedLootActors[Index].Get();
        if (!IsValid(LootActor) || LootActor->IsActorBeingDestroyed())
        {
            continue;
        }

        const bool bSuppressIndicators = ShouldSuppressLootProximityIndicators(LootActor);

        bool bShouldOutline = false;
        if (!bSuppressIndicators && bCanUseOutline)
        {
            bShouldOutline = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= OutlineDistanceSq;
        }

        bool bShouldFx = false;
        if (!bSuppressIndicators && bLootFxTemplateReady)
        {
            bShouldFx = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= FxDistanceSq;
        }

        bool bShouldDot = false;
        if (!bSuppressIndicators && bLootDotClassReady)
        {
            bShouldDot = FVector::DistSquared(LootActor->GetActorLocation(), PlayerLocation) <= DotDistanceSq;
        }

        SetLootActorProximityFx(LootActor, bShouldFx);
        SetLootActorOutline(LootActor, bShouldOutline);
        SetLootActorDotWidget(LootActor, bShouldDot);
        SpawnedLootActors[WriteIndex++] = LootActor;
    }

    if (WriteIndex < SpawnedLootActors.Num())
    {
        SpawnedLootActors.SetNum(WriteIndex, EAllowShrinking::No);
    }

    TArray<TWeakObjectPtr<AActor>> OrphanKeys;
    for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UParticleSystemComponent>>& Pair : LootProximityFxComponents)
    {
        const AActor* KeyActor = Pair.Key.Get();
        if (!IsValid(KeyActor) || !SpawnedLootActors.Contains(Pair.Key))
        {
            if (UParticleSystemComponent* FxComponent = Pair.Value.Get())
            {
                FxComponent->DeactivateSystem();
                FxComponent->DestroyComponent();
            }
            OrphanKeys.Add(Pair.Key);
        }
    }
    for (const TWeakObjectPtr<AActor>& Key : OrphanKeys)
    {
        LootProximityFxComponents.Remove(Key);
    }

    TArray<TWeakObjectPtr<AActor>> DotOrphanKeys;
    for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<UWidgetComponent>>& Pair : LootDotWidgetComponents)
    {
        const AActor* KeyActor = Pair.Key.Get();
        if (!IsValid(KeyActor) || !SpawnedLootActors.Contains(Pair.Key))
        {
            if (UWidgetComponent* DotComponent = Pair.Value.Get())
            {
                DotComponent->SetVisibility(false, true);
                DotComponent->DestroyComponent();
            }
            DotOrphanKeys.Add(Pair.Key);
        }
    }
    for (const TWeakObjectPtr<AActor>& Key : DotOrphanKeys)
    {
        LootDotWidgetComponents.Remove(Key);
    }
}

AActor* ARaidRoomActor::SpawnProceduralDoorBlocker(const FModularMeshKit& ThemeKit, const FVector& LocalLocation, float LocalYaw)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<FMeshVariation> DoorBlockerVariations;
    ThemeKit.GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::DoorBlocker, DoorBlockerVariations);
    const FMeshVariation* DoorVariation = RaidMeshUtils::PickRandomVariation(DoorBlockerVariations, RoomRandomStream);
    const bool bHasConfiguredDoorVariation = DoorVariation && (!DoorVariation->Mesh.IsNull() || !DoorVariation->BlueprintPrefab.IsNull());
    const FVector FallbackDoorScale(1.8f, 0.45f, 2.6f);
    const FVector BaseDoorScale = bHasConfiguredDoorVariation ? FVector::OneVector : FallbackDoorScale;
    const float DoorHalfHeight = 50.0f * BaseDoorScale.Z;
    const float DoorYawOffset = 0.0f;

    FVector WorldDoorLocation = GetActorTransform().TransformPosition(LocalLocation);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidDoorBlockerGroundSnap), false);
    QueryParams.bTraceComplex = false;
    QueryParams.AddIgnoredActor(this);

    FHitResult GroundHit;
    const bool bHitGround = World->LineTraceSingleByChannel(
        GroundHit,
        WorldDoorLocation + FVector(0.0f, 0.0f, 120000.0f),
        WorldDoorLocation + FVector(0.0f, 0.0f, -120000.0f),
        ECC_WorldStatic,
        QueryParams);

    if (bHitGround)
    {
        if (IsWaterHit(GroundHit) || IsInsideWaterPhysicsVolume(World, GroundHit.ImpactPoint, 80.0f))
        {
            return nullptr;
        }
        WorldDoorLocation.Z = GroundHit.ImpactPoint.Z + DoorHalfHeight;
    }
    else
    {
        WorldDoorLocation.Z = GetActorLocation().Z + DoorHalfHeight;
    }

    const FTransform WorldDoorTransform(
        FRotator(0.0f, LocalYaw + DoorYawOffset, 0.0f),
        WorldDoorLocation,
        BaseDoorScale);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    Params.ObjectFlags |= ResolveRoomSpawnObjectFlags(World);

    if (bHasConfiguredDoorVariation)
    {
        const FTransform FinalDoorTransform = ResolveVariationTransform(
            *DoorVariation,
            WorldDoorTransform,
            ERaidVariationOffsetChannel::DoorBlocker);

        if (!DoorVariation->BlueprintPrefab.IsNull())
        {
            if (UClass* DoorClass = DoorVariation->BlueprintPrefab.LoadSynchronous())
            {
                if (AActor* DoorActor = World->SpawnActor<AActor>(DoorClass, FinalDoorTransform, Params))
                {
                    TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
                    DoorActor->GetComponents(PrimitiveComps);
                    for (UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
                    {
                        if (!IsValid(PrimitiveComp)) continue;
                        PrimitiveComp->SetCollisionProfileName(TEXT("BlockAll"));
                    }

                    DoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
                    DoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
                    DoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                    DoorActor->SetOwner(this);
                    DoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                    return DoorActor;
                }
            }
        }
        else
        {
            if (UStaticMesh* DoorMesh = DoorVariation->Mesh.LoadSynchronous())
            {
                if (AStaticMeshActor* DoorActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FinalDoorTransform, Params))
                {
                    if (UStaticMeshComponent* MeshComp = DoorActor->GetStaticMeshComponent())
                    {
                        MeshComp->SetStaticMesh(DoorMesh);
                        MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
                        MeshComp->SetMobility(EComponentMobility::Static);
                    }
                    DoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
                    DoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
                    DoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                    DoorActor->SetOwner(this);
                    DoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                    return DoorActor;
                }
            }
        }
    }

    // No configured door blocker mesh/blueprint: keep gameplay blockage with invisible collision-only fallback.
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (AStaticMeshActor* FallbackDoorActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldDoorTransform, Params))
    {
        if (UStaticMeshComponent* MeshComp = FallbackDoorActor->GetStaticMeshComponent())
        {
            MeshComp->SetStaticMesh(CubeMesh);
            MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
            MeshComp->SetHiddenInGame(true);
            MeshComp->SetVisibility(false, true);
            MeshComp->SetCastShadow(false);
        }
        FallbackDoorActor->Tags.AddUnique(FName(TEXT("RaidDoorBlocker")));
        FallbackDoorActor->Tags.AddUnique(RaidRoomGeneratedTag);
        FallbackDoorActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
        FallbackDoorActor->SetOwner(this);
        FallbackDoorActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
        return FallbackDoorActor;
    }

    return nullptr;
}

AActor* ARaidRoomActor::AddMeshInstance(const FMeshVariation& Variation, const FTransform& BaseTransform, int32 MeshType, UMaterialInterface* MaterialOverride)
{
    if (Variation.Mesh.IsNull() && Variation.BlueprintPrefab.IsNull()) return nullptr;
    const ERaidVariationOffsetChannel OffsetChannel = ResolveOffsetChannelForMeshType(MeshType);
    FTransform FinalTransform = ResolveVariationTransform(Variation, BaseTransform, OffsetChannel);
    FTransform WorldTransform = FinalTransform * GetActorTransform();
    UStaticMesh* PreloadedVariationMesh = nullptr;

    const bool bOutdoorStyle = IsOutdoorStyleRoom(NodeRow);
    const bool bTerrainConformType = (MeshType == 0 || MeshType == 2 || MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8);
    const bool bShouldTryGroundSnap = bTerrainConformType;
    const bool bShouldAlignToSlope = bOutdoorStyle && (MeshType == 6 || MeshType == 7 || MeshType == 8);
    const bool bVariationBlueprint = !Variation.BlueprintPrefab.IsNull();
    const float VariationDeltaLocalZ = FinalTransform.GetLocation().Z - BaseTransform.GetLocation().Z;
    bool bHasGroundHitForSnap = false;
    FHitResult CachedGroundHit;
    float ObstacleMinSpacingForFootprint = 0.0f;
    FBox2D CandidateObstacleFootprint(EForceInit::ForceInit);
    bool bHasCandidateObstacleFootprint = false;

    if (UWorld* World = GetWorld())
    {
        if (bOutdoorStyle && IsInsideWaterPhysicsVolume(World, WorldTransform.GetLocation(), 120.0f))
        {
            return nullptr;
        }

        if (bShouldTryGroundSnap)
        {
            FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidRoomObjectGroundSnap), false);
            QueryParams.bTraceComplex = false;
            QueryParams.AddIgnoredActor(this);
            for (const TObjectPtr<AActor>& SpawnedActor : SpawnedDynamicActors)
            {
                if (IsValid(SpawnedActor))
                {
                    QueryParams.AddIgnoredActor(SpawnedActor);
                }
            }
            for (const TObjectPtr<AActor>& DoorActor : SpawnedDoorActors)
            {
                if (IsValid(DoorActor))
                {
                    QueryParams.AddIgnoredActor(DoorActor);
                }
            }

            const FVector QueryLocation = WorldTransform.GetLocation();
            if (!PreloadedVariationMesh && !Variation.Mesh.IsNull())
            {
                PreloadedVariationMesh = Variation.Mesh.LoadSynchronous();
            }

            int32 GroundSupportSampleCount = 1;
            float GroundSupportRadius = 0.0f;
            if (bOutdoorStyle)
            {
                if (MeshType == 2 || bVariationBlueprint)
                {
                    GroundSupportSampleCount = 5;
                }
                else if (MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8)
                {
                    GroundSupportSampleCount = 5;
                }

                if (PreloadedVariationMesh)
                {
                    const FVector BoundsExtent = PreloadedVariationMesh->GetBoundingBox().GetExtent();
                    const FVector Scale3D = WorldTransform.GetScale3D().GetAbs();
                    const float ExtentXY = FMath::Max(BoundsExtent.X * Scale3D.X, BoundsExtent.Y * Scale3D.Y);
                    if (ExtentXY > 1.0f)
                    {
                        GroundSupportRadius = FMath::Clamp(ExtentXY * 0.28f, 70.0f, 360.0f);
                        if ((MeshType == 2 || bVariationBlueprint) && ExtentXY > 650.0f)
                        {
                            GroundSupportSampleCount = 9;
                        }
                    }
                }

                if (GroundSupportSampleCount > 1 && GroundSupportRadius <= 1.0f)
                {
                    GroundSupportRadius = bVariationBlueprint ? 190.0f : 120.0f;
                }
            }

            FHitResult CenterGroundHit;
            if (!TryResolveRoomSingleGroundHitAtPoint(World, QueryLocation, true, QueryParams, CenterGroundHit))
            {
                return nullptr;
            }

            const bool bSelectedLandscape = IsLandscapeLikeHit(CenterGroundHit);
            if (!bOutdoorStyle && !bSelectedLandscape)
            {
                // Indoor-style room metadata일 때는 임의의 static mesh를 지면으로 오인하지 않도록 스킵.
                // 단, 실제 Landscape를 찾은 경우에는 메타데이터와 무관하게 스냅한다.
            }
            else
            {
                TArray<FVector2D> GroundSupportOffsets;
                BuildGroundSupportOffsets(GroundSupportSampleCount, GroundSupportRadius, GroundSupportOffsets);

                TArray<float> SupportHeights;
                SupportHeights.Reserve(GroundSupportOffsets.Num());
                SupportHeights.Add(CenterGroundHit.ImpactPoint.Z);

                FVector SupportNormalAccum = CenterGroundHit.ImpactNormal;
                int32 SupportNormalCount = CenterGroundHit.ImpactNormal.IsNearlyZero() ? 0 : 1;

                for (int32 OffsetIndex = 1; OffsetIndex < GroundSupportOffsets.Num(); ++OffsetIndex)
                {
                    const FVector2D& Offset = GroundSupportOffsets[OffsetIndex];
                    const FVector SampleLocation = QueryLocation + FVector(Offset.X, Offset.Y, 0.0f);
                    FHitResult SupportHit;
                    if (!TryResolveRoomSingleGroundHitAtPoint(World, SampleLocation, true, QueryParams, SupportHit))
                    {
                        continue;
                    }

                    SupportHeights.Add(SupportHit.ImpactPoint.Z);
                    if (!SupportHit.ImpactNormal.IsNearlyZero())
                    {
                        SupportNormalAccum += SupportHit.ImpactNormal;
                        ++SupportNormalCount;
                    }
                }

                float SupportGroundZ = CenterGroundHit.ImpactPoint.Z;
                if (SupportHeights.Num() > 1)
                {
                    SupportHeights.Sort();
                    SupportGroundZ = SupportHeights[SupportHeights.Num() / 2];
                }

                bHasGroundHitForSnap = true;
                CachedGroundHit = CenterGroundHit;
                CachedGroundHit.ImpactPoint.Z = SupportGroundZ;
                CachedGroundHit.Location.Z = SupportGroundZ;
                if (SupportNormalCount > 0)
                {
                    CachedGroundHit.ImpactNormal = SupportNormalAccum.GetSafeNormal();
                }

                FVector SnappedLocation = WorldTransform.GetLocation();
                SnappedLocation.X = CenterGroundHit.ImpactPoint.X;
                SnappedLocation.Y = CenterGroundHit.ImpactPoint.Y;
                WorldTransform.SetLocation(SnappedLocation);

                if (bShouldAlignToSlope)
                {
                    const float PreservedYaw = WorldTransform.GetRotation().Rotator().Yaw;
                    const FVector SlopeNormal = CachedGroundHit.ImpactNormal.IsNearlyZero()
                        ? CenterGroundHit.ImpactNormal
                        : CachedGroundHit.ImpactNormal;
                    FRotator SlopeRot = FRotationMatrix::MakeFromZ(SlopeNormal).Rotator();
                    SlopeRot.Yaw = PreservedYaw;
                    WorldTransform.SetRotation(SlopeRot.Quaternion());
                }

                const bool bFoliageLikeMeshType = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                const float BaseLocalZContribution = bFoliageLikeMeshType ? BaseTransform.GetLocation().Z : 0.0f;
                const float EffectiveVariationDeltaLocalZ = bFoliageLikeMeshType
                    ? FMath::Min(0.0f, VariationDeltaLocalZ)
                    : VariationDeltaLocalZ;
                const float TargetBottomZ = SupportGroundZ + BaseLocalZContribution + EffectiveVariationDeltaLocalZ + 2.0f;
                bool bAdjustedWithBounds = false;
                if (PreloadedVariationMesh)
                {
                    const FBox LocalBounds = PreloadedVariationMesh->GetBoundingBox();
                    if (LocalBounds.IsValid)
                    {
                        float CurrentMinZ = TNumericLimits<float>::Max();
                        const FVector BoundsMin = LocalBounds.Min;
                        const FVector BoundsMax = LocalBounds.Max;
                        const FVector Corners[8] =
                        {
                            FVector(BoundsMin.X, BoundsMin.Y, BoundsMin.Z),
                            FVector(BoundsMin.X, BoundsMin.Y, BoundsMax.Z),
                            FVector(BoundsMin.X, BoundsMax.Y, BoundsMin.Z),
                            FVector(BoundsMin.X, BoundsMax.Y, BoundsMax.Z),
                            FVector(BoundsMax.X, BoundsMin.Y, BoundsMin.Z),
                            FVector(BoundsMax.X, BoundsMin.Y, BoundsMax.Z),
                            FVector(BoundsMax.X, BoundsMax.Y, BoundsMin.Z),
                            FVector(BoundsMax.X, BoundsMax.Y, BoundsMax.Z)
                        };

                        for (const FVector& Corner : Corners)
                        {
                            const FVector WorldCorner = WorldTransform.TransformPosition(Corner);
                            CurrentMinZ = FMath::Min(CurrentMinZ, WorldCorner.Z);
                        }

                        if (CurrentMinZ < TNumericLimits<float>::Max())
                        {
                            SnappedLocation = WorldTransform.GetLocation();
                            SnappedLocation.Z += (TargetBottomZ - CurrentMinZ);
                            WorldTransform.SetLocation(SnappedLocation);
                            bAdjustedWithBounds = true;
                        }
                    }
                }

                if (!bAdjustedWithBounds)
                {
                    SnappedLocation = WorldTransform.GetLocation();
                    SnappedLocation.Z = TargetBottomZ;
                    WorldTransform.SetLocation(SnappedLocation);
                }
            }
        }
    }

    if (MeshType == 2)
    {
        if (UWorld* World = GetWorld())
        {
            const float BaseSpacing = bVariationBlueprint ? BlueprintObstacleMinSpacing : ObstacleMinSpacing;
            const FVector Scale3D = WorldTransform.GetScale3D().GetAbs();
            const float ScaleFactor = FMath::Clamp(FMath::Max(Scale3D.X, Scale3D.Y), 0.6f, 4.0f);
            float MinSpacing = BaseSpacing * ScaleFactor;
            UStaticMesh* CandidateMeshForBounds = nullptr;

            if (!Variation.Mesh.IsNull())
            {
                CandidateMeshForBounds = Variation.Mesh.LoadSynchronous();
                if (CandidateMeshForBounds)
                {
                    const FVector MeshExtent = CandidateMeshForBounds->GetBoundingBox().GetExtent() * Scale3D;
                    const float MeshRequiredSpacing = FMath::Max(MeshExtent.X, MeshExtent.Y) * 1.8f;
                    MinSpacing = FMath::Max(MinSpacing, MeshRequiredSpacing);
                }
            }

            ObstacleMinSpacingForFootprint = MinSpacing;

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                const FVector CandidateWorldLoc = WorldTransform.GetLocation();

                if (CandidateMeshForBounds)
                {
                    bHasCandidateObstacleFootprint = TryBuildFootprintFromStaticMesh(CandidateMeshForBounds, WorldTransform, CandidateObstacleFootprint);
                }
                if (!bHasCandidateObstacleFootprint)
                {
                    const float FallbackExtent = bVariationBlueprint
                        ? FMath::Max(MinSpacing * 0.75f, 300.0f)
                        : FMath::Max(MinSpacing * 0.50f, 120.0f);
                    CandidateObstacleFootprint = FBox2D(
                        FVector2D(CandidateWorldLoc.X - FallbackExtent, CandidateWorldLoc.Y - FallbackExtent),
                        FVector2D(CandidateWorldLoc.X + FallbackExtent, CandidateWorldLoc.Y + FallbackExtent));
                    bHasCandidateObstacleFootprint = CandidateObstacleFootprint.bIsValid;
                }

                const float FootprintPadding = bVariationBlueprint
                    ? FMath::Clamp(MinSpacing * 0.24f, 80.0f, 480.0f)
                    : FMath::Clamp(MinSpacing * 0.18f, 35.0f, 260.0f);
                if (bHasCandidateObstacleFootprint &&
                    IsFootprintOverlappingAny(SpawnedObstacleFootprints, CandidateObstacleFootprint, FootprintPadding))
                {
                    return nullptr;
                }

                if (!bVariationBlueprint)
                {
                    for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
                    {
                        if (!IsValid(ExistingActor) || !ExistingActor->ActorHasTag(TEXT("MeshType_2")))
                        {
                            continue;
                        }

                        float RequiredSpacing = MinSpacing;
                        if (ExistingActor->ActorHasTag(TEXT("ObstacleBlueprint")))
                        {
                            RequiredSpacing = FMath::Max(RequiredSpacing, BlueprintObstacleMinSpacing);
                        }

                        if (FVector::DistSquaredXY(ExistingActor->GetActorLocation(), CandidateWorldLoc) < FMath::Square(RequiredSpacing))
                        {
                            return nullptr;
                        }
                    }

                    FCollisionObjectQueryParams ObjQuery;
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidObstacleFinalPlacementCheck), false);
                    QueryParams.bTraceComplex = false;

                    TArray<FOverlapResult> Overlaps;
                    if (World->OverlapMultiByObjectType(
                        Overlaps,
                        CandidateWorldLoc,
                        FQuat::Identity,
                        ObjQuery,
                        FCollisionShape::MakeSphere(MinSpacing * 0.55f),
                        QueryParams))
                    {
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                            const AActor* HitActor = Overlap.GetActor();
                            if (!IsValid(HitComp))
                            {
                                continue;
                            }

                            const bool bObstacleComponent = HitComp->ComponentTags.Contains(TEXT("MeshType_2"));
                            const bool bObstacleActor =
                                IsValid(HitActor) &&
                                (HitActor->ActorHasTag(TEXT("MeshType_2")) || HitActor->ActorHasTag(TEXT("ObstacleBlueprint")));
                            if (bObstacleComponent || bObstacleActor)
                            {
                                return nullptr;
                            }
                        }
                    }
                }
            }
        }
    }

    if (MeshType == 3 || MeshType == 6 || MeshType == 7 || MeshType == 8)
    {
        FBox2D CandidateNonObstacleFootprint(EForceInit::ForceInit);
        bool bHasNonObstacleFootprint = false;

        if (!Variation.Mesh.IsNull())
        {
            if (UStaticMesh* CandidateMesh = Variation.Mesh.LoadSynchronous())
            {
                bHasNonObstacleFootprint = TryBuildFootprintFromStaticMesh(CandidateMesh, WorldTransform, CandidateNonObstacleFootprint);
            }
        }

        if (!bHasNonObstacleFootprint)
        {
            const FVector CandidateWorldLoc = WorldTransform.GetLocation();
            const float FallbackExtent = (MeshType == 6)
                ? 260.0f
                : (MeshType == 8 ? 180.0f : 140.0f);
            CandidateNonObstacleFootprint = FBox2D(
                FVector2D(CandidateWorldLoc.X - FallbackExtent, CandidateWorldLoc.Y - FallbackExtent),
                FVector2D(CandidateWorldLoc.X + FallbackExtent, CandidateWorldLoc.Y + FallbackExtent));
            bHasNonObstacleFootprint = CandidateNonObstacleFootprint.bIsValid;
        }

        if (bHasNonObstacleFootprint)
        {
            const float FootprintPadding = (MeshType == 6)
                ? 120.0f
                : (MeshType == 8 ? 90.0f : 70.0f);
            if (IsFootprintOverlappingAny(SpawnedObstacleFootprints, CandidateNonObstacleFootprint, FootprintPadding))
            {
                return nullptr;
            }
        }
    }

    if (!Variation.BlueprintPrefab.IsNull())
    {
        if (UClass* LoadedClass = Variation.BlueprintPrefab.LoadSynchronous())
        {
            FActorSpawnParameters Params;
            // Spawn transform must stay deterministic; collision auto-adjust can push blueprint actors up
            // and cause persistent "floating prop" regressions in room generation.
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= ResolveRoomSpawnObjectFlags(GetWorld());
            if (AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(LoadedClass, WorldTransform, Params))
            {
                if (bShouldTryGroundSnap && bHasGroundHitForSnap)
                {
                    const bool bFoliageLikeMeshType = (MeshType == 6 || MeshType == 7 || MeshType == 8);
                    const float BaseLocalZContribution = bFoliageLikeMeshType ? BaseTransform.GetLocation().Z : 0.0f;
                    const float EffectiveVariationDeltaLocalZ = bFoliageLikeMeshType
                        ? FMath::Min(0.0f, VariationDeltaLocalZ)
                        : VariationDeltaLocalZ;
                    const float TargetBottomZ = CachedGroundHit.ImpactPoint.Z + BaseLocalZContribution + EffectiveVariationDeltaLocalZ + 2.0f;

                    float CurrentSupportMinZ = TNumericLimits<float>::Max();
                    if (TryResolveActorLowestSupportZ(SpawnedActor, CurrentSupportMinZ))
                    {
                        const float DeltaToGround = TargetBottomZ - CurrentSupportMinZ;
                        if (!FMath::IsNearlyZero(DeltaToGround, 0.1f))
                        {
                            SpawnedActor->AddActorWorldOffset(
                                FVector(0.0f, 0.0f, DeltaToGround),
                                false,
                                nullptr,
                                ETeleportType::TeleportPhysics);
                        }
                    }
                    else
                    {
                        FVector FallbackLoc = SpawnedActor->GetActorLocation();
                        FallbackLoc.Z = TargetBottomZ;
                        SpawnedActor->SetActorLocation(FallbackLoc, false, nullptr, ETeleportType::TeleportPhysics);
                    }
                }

                const bool bForceBlockCollision = (MeshType == 0 || MeshType == 1 || MeshType == 2 || MeshType == 3 || MeshType == 6 || MeshType == 8);
                const bool bForceNoCollision = (MeshType == 7);
                const bool bShouldCastShadow = (MeshType <= 2 || MeshType == 6 || MeshType == 8);
                const bool bShouldAffectNavigation = bForceBlockCollision && (MeshType == 2);

                TInlineComponentArray<UPrimitiveComponent*> PrimitiveComps;
                SpawnedActor->GetComponents(PrimitiveComps);
                for (UPrimitiveComponent* PrimitiveComp : PrimitiveComps)
                {
                    if (!IsValid(PrimitiveComp))
                    {
                        continue;
                    }

                    if (bForceBlockCollision)
                    {
                        if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimitiveComp))
                        {
                            EnsureMeshWalkableCollisionForRoom(StaticMeshComp->GetStaticMesh(), MeshType);
                        }
                        if (PrimitiveComp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
                        {
                            PrimitiveComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                        }
                        PrimitiveComp->SetCollisionObjectType(ECC_WorldStatic);
                        // Preserve asset-authored channel responses (including climb channels/volumes)
                        // and only guarantee that gameplay collision blocks character movement.
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                        PrimitiveComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
                        PrimitiveComp->CanCharacterStepUpOn = ECB_Yes;
                    }
                    else if (bForceNoCollision)
                    {
                        PrimitiveComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        PrimitiveComp->SetCollisionProfileName(TEXT("NoCollision"));
                        PrimitiveComp->CanCharacterStepUpOn = ECB_No;
                    }

                    PrimitiveComp->SetCastShadow(bShouldCastShadow);
                    PrimitiveComp->bCastDynamicShadow = bShouldCastShadow;
                    PrimitiveComp->bCastStaticShadow = bShouldCastShadow;
                    PrimitiveComp->SetCanEverAffectNavigation(bShouldAffectNavigation);
                }

                SpawnedActor->Tags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
                SpawnedActor->Tags.AddUnique(RaidRoomGeneratedTag);
                SpawnedActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
                if (MeshType == 2)
                {
                    SpawnedActor->Tags.AddUnique(FName(TEXT("ObstacleBlueprint")));
                }
                SpawnedActor->SetOwner(this);
                SpawnedActor->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                if (bShouldAffectNavigation)
                {
                    FNavigationSystem::UpdateActorAndComponentData(*SpawnedActor, true);
                }

                if (MeshType == 2)
                {
                    FBox2D FinalFootprint(EForceInit::ForceInit);
                    bool bHasFinalFootprint = TryBuildFootprintFromActor(SpawnedActor, FinalFootprint);
                    if (!bHasFinalFootprint && bHasCandidateObstacleFootprint)
                    {
                        FinalFootprint = CandidateObstacleFootprint;
                        bHasFinalFootprint = true;
                    }
                    if (!bHasFinalFootprint)
                    {
                        const FVector SpawnedLoc = SpawnedActor->GetActorLocation();
                        const float FallbackExtent = FMath::Max(
                            ObstacleMinSpacingForFootprint * (bVariationBlueprint ? 0.70f : 0.50f),
                            bVariationBlueprint ? 280.0f : 140.0f);
                        FinalFootprint = FBox2D(
                            FVector2D(SpawnedLoc.X - FallbackExtent, SpawnedLoc.Y - FallbackExtent),
                            FVector2D(SpawnedLoc.X + FallbackExtent, SpawnedLoc.Y + FallbackExtent));
                        bHasFinalFootprint = FinalFootprint.bIsValid;
                    }

                    if (bHasFinalFootprint)
                    {
                        const float FinalPadding = bVariationBlueprint
                            ? FMath::Clamp(ObstacleMinSpacingForFootprint * 0.20f, 60.0f, 320.0f)
                            : FMath::Clamp(ObstacleMinSpacingForFootprint * 0.12f, 20.0f, 140.0f);
                        if (IsFootprintOverlappingAny(SpawnedObstacleFootprints, FinalFootprint, FinalPadding))
                        {
                            SpawnedActor->Destroy();
                            return nullptr;
                        }
                        SpawnedObstacleFootprints.Add(FinalFootprint);
                    }
                }

                SpawnedDynamicActors.Add(SpawnedActor);
                return SpawnedActor;
            }

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Blueprint spawn failed after class load. RoomNode=%d MeshType=%d Class=%s"),
                NodeId,
                MeshType,
                *GetNameSafe(LoadedClass));
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Blueprint class load failed. RoomNode=%d MeshType=%d Asset=%s"),
                NodeId,
                MeshType,
                *Variation.BlueprintPrefab.ToString());
        }
        return nullptr;
    }

    UStaticMesh* LoadedMesh = PreloadedVariationMesh ? PreloadedVariationMesh : Variation.Mesh.LoadSynchronous();
    if (!LoadedMesh) return nullptr;
    MaybeEnableNaniteForMesh(LoadedMesh);
    EnsureMeshWalkableCollisionForRoom(LoadedMesh, MeshType);

    UMaterialInterface* EffectiveMaterial = MaterialOverride;

    if (!EffectiveMaterial && bUseSemanticWhiteboxColors)
    {
        if (LoadedMesh->GetPathName().StartsWith(TEXT("/Engine/")))
        {
            EffectiveMaterial = GetSemanticMaterialForType(MeshType);
        }
    }

    const bool bTreeLikeMeshAsset = IsTreeLikeMeshName(LoadedMesh->GetPathName());
    const bool bSpawnAsTreeActor =
        (MeshType == 6) &&
        bSpawnWindAnimatedRoomTreesAsActors &&
        bTreeLikeMeshAsset;

    if (bSpawnAsTreeActor)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= ResolveRoomSpawnObjectFlags(GetWorld());
        if (AStaticMeshActor* TreeActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldTransform, SpawnParams))
        {
            if (UStaticMeshComponent* MeshComp = TreeActor->GetStaticMeshComponent())
            {
                MeshComp->SetStaticMesh(LoadedMesh);
                EnsureMeshWalkableCollisionForRoom(LoadedMesh, MeshType);
                if (MeshComp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
                {
                    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                }
                MeshComp->SetCollisionObjectType(ECC_WorldStatic);
                MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
                MeshComp->CanCharacterStepUpOn = ECB_Yes;
                MeshComp->SetMobility(EComponentMobility::Static);
                MeshComp->SetCastShadow(true);
                MeshComp->bCastDynamicShadow = true;
                MeshComp->bCastStaticShadow = true;
                if (EffectiveMaterial)
                {
                    MeshComp->SetMaterial(0, EffectiveMaterial);
                }
                ApplyRoomTreeWindPhaseDesync(MeshComp, RoomRandomStream);
            }

            TreeActor->Tags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
            TreeActor->Tags.AddUnique(RaidRoomGeneratedTag);
            TreeActor->Tags.AddUnique(MakeRaidRoomNodeTag(NodeId));
            TreeActor->SetOwner(this);
            SpawnedDynamicActors.Add(TreeActor);
            return TreeActor;
        }
    }

    DynamicISMC_Pool.RemoveAll(
        [](const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Candidate)
        {
            return !IsValid(Candidate) || Candidate->IsBeingDestroyed();
        });

    UHierarchicalInstancedStaticMeshComponent* TargetISMC = nullptr;
    for (UHierarchicalInstancedStaticMeshComponent* ISMC : DynamicISMC_Pool)
    {
        if (!IsValid(ISMC) || !ISMC->IsRegistered() || ISMC->GetStaticMesh() != LoadedMesh) continue;
        const bool bTypeMatch = ISMC->ComponentTags.Contains(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
        UMaterialInterface* CurrentMat = (ISMC->GetNumMaterials() > 0) ? ISMC->GetMaterial(0) : nullptr;
        const bool bHasOverrideTag = ISMC->ComponentTags.Contains(FName(TEXT("MatOverride")));
        const bool bMaterialMatch = EffectiveMaterial ? (CurrentMat == EffectiveMaterial) : !bHasOverrideTag;
        if (bTypeMatch && bMaterialMatch) { TargetISMC = ISMC; break; }
    }

    if (!TargetISMC)
    {
        TargetISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this,
            NAME_None,
            ResolveRoomSpawnObjectFlags(GetWorld()));
        if (!IsValid(TargetISMC))
        {
            return nullptr;
        }
        TargetISMC->CreationMethod = EComponentCreationMethod::Instance; TargetISMC->SetMobility(EComponentMobility::Movable); TargetISMC->SetStaticMesh(LoadedMesh); TargetISMC->SetupAttachment(RootComponent);
        TargetISMC->ComponentTags.AddUnique(FName(*FString::Printf(TEXT("MeshType_%d"), MeshType)));
        TargetISMC->ComponentTags.AddUnique(TEXT("RaidRuntimeISMC"));
        TargetISMC->ComponentTags.AddUnique(TEXT("RaidRoomRuntimeISMC"));
        ApplyISMCOptimization(TargetISMC, MeshType);
        if (EffectiveMaterial) { TargetISMC->SetMaterial(0, EffectiveMaterial); TargetISMC->ComponentTags.AddUnique(FName(TEXT("MatOverride"))); }
        TargetISMC->RegisterComponent();
        DynamicISMC_Pool.Add(TargetISMC);
    }

    if (!IsValid(TargetISMC) || !TargetISMC->IsRegistered() || !IsValid(TargetISMC->GetStaticMesh()))
    {
        return nullptr;
    }

    // Re-normalize optimization state even when reusing an existing pool component.
    // This prevents legacy nav-affecting flags from causing expensive nav churn/ensures
    // during AddInstance() on regenerated rooms.
    ApplyISMCOptimization(TargetISMC, MeshType);

    // Only obstacle ISMC should schedule navigation updates, and those are batched later.
    const bool bNavRelevantMeshType = bEnableObstacleNavigationUpdates && (MeshType == 2);
    if (TargetISMC->CanEverAffectNavigation())
    {
        // Keep navigation updates disabled while adding instances.
        // FlushQueuedNavigationUpdates() performs one stable batched update.
        TargetISMC->SetCanEverAffectNavigation(false);
    }

    // Avoid immediate per-instance navigation update during AddInstance().
    // We queue one batched nav update in FlushQueuedNavigationUpdates() instead.
    const int32 AddedInstanceIndex = TargetISMC->AddInstance(WorldTransform, true);
    if (AddedInstanceIndex == INDEX_NONE)
    {
        return nullptr;
    }

    if (MeshType == 2)
    {
        FBox2D FinalFootprint(EForceInit::ForceInit);
        bool bHasFinalFootprint = bHasCandidateObstacleFootprint;
        if (bHasFinalFootprint)
        {
            FinalFootprint = CandidateObstacleFootprint;
        }
        else
        {
            bHasFinalFootprint = TryBuildFootprintFromStaticMesh(LoadedMesh, WorldTransform, FinalFootprint);
        }
        if (!bHasFinalFootprint)
        {
            const FVector SpawnLoc = WorldTransform.GetLocation();
            const float FallbackExtent = FMath::Max(ObstacleMinSpacingForFootprint * 0.50f, 140.0f);
            FinalFootprint = FBox2D(
                FVector2D(SpawnLoc.X - FallbackExtent, SpawnLoc.Y - FallbackExtent),
                FVector2D(SpawnLoc.X + FallbackExtent, SpawnLoc.Y + FallbackExtent));
            bHasFinalFootprint = FinalFootprint.bIsValid;
        }

        if (bHasFinalFootprint)
        {
            const float FinalPadding = FMath::Clamp(ObstacleMinSpacingForFootprint * 0.12f, 20.0f, 140.0f);
            if (IsFootprintOverlappingAny(SpawnedObstacleFootprints, FinalFootprint, FinalPadding))
            {
                if (TargetISMC->IsValidInstance(AddedInstanceIndex))
                {
                    TargetISMC->RemoveInstance(AddedInstanceIndex);
                }
                return nullptr;
            }
            SpawnedObstacleFootprints.Add(FinalFootprint);
        }
    }

    if (bNavRelevantMeshType)
    {
        PendingNavUpdateISMCs.AddUnique(TargetISMC);
    }

    return nullptr;
}

void ARaidRoomActor::GenerateTraversalWhiteboxKit(float RoomRadius, const FModularMeshKit* ThemeKit)
{
    if (!bEnableTraversalWhiteboxKit) return;
    FRandomStream Rng(NodeRow.Seed ^ (NodeId * 9973));
    UMaterialInterface* TraversalMat = GetTraversalMaterial();
    const bool bHasTraversalMeshOverride = !TraversalMeshOverride.IsNull();
    TMap<FSoftObjectPath, float> MeshBaseLiftCache;
    TArray<FVector> OccupiedObstacleLocations;
    int32 SpawnedBlueprintObstacleCount = 0;
    FString LastPickedObstacleVariationKey;
    TMap<FString, int32> ObstacleVariationPickCounts;
    TMap<FString, int32> ObstacleVariationPlacedCounts;
    TMap<FString, float> ObstacleVariationConfiguredWeights;
    TMap<FString, float> ObstacleVariationEffectiveWeights;
    enum class EObstacleSpawnFailReason : uint8
    {
        None,
        NoVariation,
        PlacementRejected
    };
    int32 ObstacleTargetCount = 0;
    int32 ObstacleAttemptCount = 0;
    int32 ObstaclePlacedCount = 0;
    int32 ObstacleSkipCenterCount = 0;
    int32 ObstacleNoVariationCount = 0;
    int32 ObstaclePlacementRejectCount = 0;
    TArray<FMeshVariation> ThemeFloorVariations;
    TArray<FMeshVariation> ThemeWallVariations;
    TArray<FMeshVariation> ThemeObstacleVariations;
    TArray<FMeshVariation> ThemeDecorationVariations;
    if (ThemeKit)
    {
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Floor, ThemeFloorVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Wall, ThemeWallVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Obstacle, ThemeObstacleVariations);
        ThemeKit->GetEffectiveVariationsForChannel(ERaidVariationOffsetChannel::Decoration, ThemeDecorationVariations);
    }

    auto BuildObstacleVariationDebugKey = [&](const FMeshVariation& Variation, int32 VariationIndex) -> FString
        {
            const bool bBlueprintVariation = !Variation.BlueprintPrefab.IsNull();
            const FSoftObjectPath AssetPath = bBlueprintVariation
                ? Variation.BlueprintPrefab.ToSoftObjectPath()
                : Variation.Mesh.ToSoftObjectPath();
            const FString AssetName = AssetPath.GetAssetName();
            const FString SafeAssetName = AssetName.IsEmpty()
                ? FString::Printf(TEXT("Var%d"), VariationIndex)
                : AssetName;
            return FString::Printf(TEXT("%s:%s#%d"), bBlueprintVariation ? TEXT("BP") : TEXT("SM"), *SafeAssetName, VariationIndex);
        };

    auto GetEffectiveObstacleWeight = [&](const FMeshVariation& Variation, bool bRespectBlueprintCap) -> float
        {
            const float BaseWeight = FMath::Max(0.0f, Variation.SpawnWeight);
            if (BaseWeight <= KINDA_SMALL_NUMBER)
            {
                return 0.0f;
            }

            const bool bBlueprintVariation = !Variation.BlueprintPrefab.IsNull();
            if (bBlueprintVariation && bRespectBlueprintCap && MaxBlueprintObstaclesPerRoom > 0 && SpawnedBlueprintObstacleCount >= MaxBlueprintObstaclesPerRoom)
            {
                return 0.0f;
            }

            const float TypeScale = bBlueprintVariation ? BlueprintObstacleWeightScale : StaticMeshObstacleWeightScale;
            return BaseWeight * FMath::Max(0.0f, TypeScale);
        };

    if (ThemeObstacleVariations.Num() > 0)
    {
        for (int32 VariationIndex = 0; VariationIndex < ThemeObstacleVariations.Num(); ++VariationIndex)
        {
            const FMeshVariation& Variation = ThemeObstacleVariations[VariationIndex];
            const FString VariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
            ObstacleVariationConfiguredWeights.Add(VariationKey, FMath::Max(0.0f, Variation.SpawnWeight));
            ObstacleVariationEffectiveWeights.Add(VariationKey, GetEffectiveObstacleWeight(Variation, false));
        }
    }

    auto CanSpawnObstacleVariation = [&](const FMeshVariation& Variation) -> bool
        {
            if (GetEffectiveObstacleWeight(Variation, true) <= KINDA_SMALL_NUMBER)
            {
                return false;
            }

            return true;
        };

    auto PickThemeVariationForMeshType = [&](int32 MeshType) -> const FMeshVariation*
        {
            if (!bUseThemeMeshForTraversalKit || !ThemeKit) return nullptr;

            const TArray<FMeshVariation>* CandidatePool = nullptr;
            if (MeshType == 1)
            {
                CandidatePool = &ThemeWallVariations;
            }
            else if (MeshType == 0)
            {
                CandidatePool = &ThemeFloorVariations;
            }
            else if (MeshType == 3)
            {
                CandidatePool = &ThemeDecorationVariations;
            }
            else
            {
                CandidatePool = &ThemeObstacleVariations;
            }

            if (!CandidatePool || CandidatePool->Num() <= 0)
            {
                return nullptr;
            }

            if (MeshType != 2)
            {
                LastPickedObstacleVariationKey.Reset();
                return RaidMeshUtils::PickRandomVariation(*CandidatePool, Rng);
            }

            // 장애물 타입은 가중치 + 블루프린트 룸당 캡을 같이 적용.
            float TotalWeight = 0.0f;
            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (!CanSpawnObstacleVariation(Variation)) continue;
                TotalWeight += GetEffectiveObstacleWeight(Variation, true);
            }

            if (TotalWeight <= KINDA_SMALL_NUMBER)
            {
                LastPickedObstacleVariationKey.Reset();
                return nullptr;
            }

            float Pick = Rng.FRandRange(0.0f, TotalWeight);
            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (!CanSpawnObstacleVariation(Variation)) continue;
                Pick -= GetEffectiveObstacleWeight(Variation, true);
                if (Pick <= 0.0f)
                {
                    LastPickedObstacleVariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
                    ObstacleVariationPickCounts.FindOrAdd(LastPickedObstacleVariationKey) += 1;
                    return &Variation;
                }
            }

            for (int32 VariationIndex = 0; VariationIndex < CandidatePool->Num(); ++VariationIndex)
            {
                const FMeshVariation& Variation = (*CandidatePool)[VariationIndex];
                if (CanSpawnObstacleVariation(Variation))
                {
                    LastPickedObstacleVariationKey = BuildObstacleVariationDebugKey(Variation, VariationIndex);
                    ObstacleVariationPickCounts.FindOrAdd(LastPickedObstacleVariationKey) += 1;
                    return &Variation;
                }
            }

            LastPickedObstacleVariationKey.Reset();
            return nullptr;
        };

    auto ResolveMeshBaseLift = [&](const FMeshVariation& VariationToMeasure) -> float
        {
            if (VariationToMeasure.Mesh.IsNull()) return 50.0f;

            const FSoftObjectPath MeshPath = VariationToMeasure.Mesh.ToSoftObjectPath();
            if (const float* Found = MeshBaseLiftCache.Find(MeshPath))
            {
                return *Found;
            }

            float Lift = 50.0f;
            if (UStaticMesh* Mesh = VariationToMeasure.Mesh.LoadSynchronous())
            {
                const FBoxSphereBounds Bounds = Mesh->GetBounds();
                Lift = -(Bounds.Origin.Z - Bounds.BoxExtent.Z);
            }
            MeshBaseLiftCache.Add(MeshPath, Lift);
            return Lift;
        };

    // 기본 도형을 스폰하는 람다 헬퍼 함수
    auto SpawnBox = [&](const FVector& Loc, const FVector& Scale, float Yaw, int32 MeshType, EObstacleSpawnFailReason* OutFailReason = nullptr) -> bool {
        if (OutFailReason)
        {
            *OutFailReason = EObstacleSpawnFailReason::None;
        }

        FMeshVariation V;
        bool bUsesThemeVariation = false;
        if (const FMeshVariation* ThemeVar = PickThemeVariationForMeshType(MeshType))
        {
            V = *ThemeVar;
            bUsesThemeVariation = true;
        }
        else if (bHasTraversalMeshOverride)
        {
            V.Mesh = TraversalMeshOverride;
            V.Offset = FTransform::Identity;
        }
        else
        {
            // Do not spawn a hardcoded fallback cube.
            if (OutFailReason)
            {
                *OutFailReason = EObstacleSpawnFailReason::NoVariation;
            }
            return false;
        }

        FVector BaseScale = V.Offset.GetScale3D();
        if (BaseScale.IsNearlyZero()) BaseScale = FVector(1.0f, 1.0f, 1.0f);
        if (bUsesThemeVariation && bPreserveThemeMeshScaleInTraversalKit)
        {
            // Theme mesh variations should keep their authored scale by default.
            V.Offset.SetScale3D(BaseScale);
        }
        else
        {
            V.Offset.SetScale3D(BaseScale * Scale);
        }

        FVector SpawnLoc = Loc;
        const float CubeBaselineLift = 50.0f * Scale.Z;
        const float MeshBaseLift = ResolveMeshBaseLift(V);
        const float ActualLift = MeshBaseLift * V.Offset.GetScale3D().Z;
        SpawnLoc.Z += (ActualLift - CubeBaselineLift);

        auto CountInstancesForMeshType = [&](int32 InMeshType) -> int32
            {
                const FName MeshTypeTag(*FString::Printf(TEXT("MeshType_%d"), InMeshType));
                int32 TotalCount = 0;
                for (UHierarchicalInstancedStaticMeshComponent* ISMC : DynamicISMC_Pool)
                {
                    if (!IsValid(ISMC))
                    {
                        continue;
                    }
                    if (!ISMC->ComponentTags.Contains(MeshTypeTag))
                    {
                        continue;
                    }
                    TotalCount += ISMC->GetInstanceCount();
                }
                return TotalCount;
            };

        const bool bIsObstacleMeshType = (MeshType == 2);
        const FString SelectedObstacleVariationKey = bIsObstacleMeshType ? LastPickedObstacleVariationKey : FString();
        if (bIsObstacleMeshType)
        {
            const float BaseSpacing = !V.BlueprintPrefab.IsNull() ? BlueprintObstacleMinSpacing : ObstacleMinSpacing;
            const float ScaleFactor = FMath::Clamp(FMath::Max(V.Offset.GetScale3D().X, V.Offset.GetScale3D().Y), 0.6f, 4.0f);
            float MinSpacing = BaseSpacing * ScaleFactor;
            if (!V.Mesh.IsNull())
            {
                if (UStaticMesh* CandidateMesh = V.Mesh.LoadSynchronous())
                {
                    const FVector MeshExtent = CandidateMesh->GetBoundingBox().GetExtent() * V.Offset.GetScale3D().GetAbs();
                    const float MeshRequiredSpacing = FMath::Max(MeshExtent.X, MeshExtent.Y) * 1.8f;
                    MinSpacing = FMath::Max(MinSpacing, MeshRequiredSpacing);
                }
            }
            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                for (const FVector& OccupiedLoc : OccupiedObstacleLocations)
                {
                    if (FVector::DistSquaredXY(OccupiedLoc, SpawnLoc) < FMath::Square(MinSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }
            }

            if (MinSpacing > KINDA_SMALL_NUMBER)
            {
                const FVector CandidateWorldLoc = GetActorTransform().TransformPosition(SpawnLoc);
                const bool bCandidateIsBlueprintObstacle = !V.BlueprintPrefab.IsNull();
                for (const TObjectPtr<AActor>& ExistingActor : SpawnedDynamicActors)
                {
                    if (!IsValid(ExistingActor) || !ExistingActor->ActorHasTag(TEXT("MeshType_2")))
                    {
                        continue;
                    }

                    float RequiredSpacing = MinSpacing;
                    if (bCandidateIsBlueprintObstacle || ExistingActor->ActorHasTag(TEXT("ObstacleBlueprint")))
                    {
                        RequiredSpacing = FMath::Max(RequiredSpacing, BlueprintObstacleMinSpacing);
                    }

                    if (FVector::DistSquaredXY(ExistingActor->GetActorLocation(), CandidateWorldLoc) < FMath::Square(RequiredSpacing))
                    {
                        if (OutFailReason)
                        {
                            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                        }
                        return false;
                    }
                }

                if (UWorld* World = GetWorld())
                {
                    FCollisionObjectQueryParams ObjQuery;
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
                    ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

                    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidObstacleOverlapSpacing), false);
                    QueryParams.bTraceComplex = false;

                    TArray<FOverlapResult> Overlaps;
                    if (World->OverlapMultiByObjectType(
                        Overlaps,
                        CandidateWorldLoc,
                        FQuat::Identity,
                        ObjQuery,
                        FCollisionShape::MakeSphere(MinSpacing * (bCandidateIsBlueprintObstacle ? 0.85f : 0.55f)),
                        QueryParams))
                    {
                        for (const FOverlapResult& Overlap : Overlaps)
                        {
                            const UPrimitiveComponent* HitComp = Overlap.Component.Get();
                            const AActor* HitActor = Overlap.GetActor();
                            if (!IsValid(HitComp))
                            {
                                continue;
                            }

                            const bool bObstacleComponent =
                                HitComp->ComponentTags.Contains(TEXT("MeshType_2")) ||
                                HitComp->ComponentTags.Contains(TEXT("ObstacleBlueprint"));
                            const bool bObstacleActor = IsValid(HitActor) && HitActor->ActorHasTag(TEXT("MeshType_2"));
                            if (bObstacleComponent || bObstacleActor)
                            {
                                if (OutFailReason)
                                {
                                    *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
                                }
                                return false;
                            }
                        }
                    }
                }
            }
        }

        UMaterialInterface* MaterialOverrideToUse = nullptr;
        if (!bUsesThemeVariation && !V.Mesh.IsNull())
        {
            const FString MeshPath = V.Mesh.ToSoftObjectPath().ToString();
            if (MeshPath.StartsWith(TEXT("/Engine/")))
            {
                MaterialOverrideToUse = TraversalMat;
            }
        }

        const int32 FootprintCountBeforeSpawn = SpawnedObstacleFootprints.Num();
        const bool bStaticMeshVariation = V.BlueprintPrefab.IsNull();
        const int32 MeshTypeInstanceCountBeforeSpawn = bStaticMeshVariation ? CountInstancesForMeshType(MeshType) : 0;
        AActor* SpawnedActor = AddMeshInstance(V, FTransform(FRotator(0.0f, Yaw, 0.0f), SpawnLoc), MeshType, MaterialOverrideToUse);
        const bool bSpawnedByFootprintDelta = SpawnedObstacleFootprints.Num() > FootprintCountBeforeSpawn;
        const int32 MeshTypeInstanceCountAfterSpawn = bStaticMeshVariation ? CountInstancesForMeshType(MeshType) : MeshTypeInstanceCountBeforeSpawn;
        const bool bSpawnedByInstanceDelta = bStaticMeshVariation && (MeshTypeInstanceCountAfterSpawn > MeshTypeInstanceCountBeforeSpawn);
        const bool bLikelySpawned = (SpawnedActor != nullptr) || bSpawnedByFootprintDelta || bSpawnedByInstanceDelta;
        if (bIsObstacleMeshType && bLikelySpawned)
        {
            const FVector OccupiedLoc = SpawnedActor
                ? GetActorTransform().InverseTransformPosition(SpawnedActor->GetActorLocation())
                : SpawnLoc;
            OccupiedObstacleLocations.Add(OccupiedLoc);
            if (!SelectedObstacleVariationKey.IsEmpty())
            {
                ObstacleVariationPlacedCounts.FindOrAdd(SelectedObstacleVariationKey) += 1;
            }
            if (!V.BlueprintPrefab.IsNull())
            {
                ++SpawnedBlueprintObstacleCount;
            }
        }
        if (!bLikelySpawned && OutFailReason)
        {
            *OutFailReason = EObstacleSpawnFailReason::PlacementRejected;
        }
        return bLikelySpawned;
        };

    // EnvType + Theme + NodeTags를 함께 읽어 오픈월드/실내전투 스타일을 판정한다.
    const FString Env = NodeRow.EnvType;
    const FString Meta = (NodeRow.EnvType + TEXT(" ") + NodeRow.Theme + TEXT(" ") + NodeRow.NodeTags + TEXT(" ") + NodeRow.RoomRole).ToLower();
    const bool bForceIndoor =
        Meta.Contains(TEXT("tarkov")) ||
        Meta.Contains(TEXT("cqb")) ||
        Meta.Contains(TEXT("indoor")) ||
        Meta.Contains(TEXT("factory")) ||
        Meta.Contains(TEXT("warehouse")) ||
        Meta.Contains(TEXT("mall")) ||
        Meta.Contains(TEXT("실내")) ||
        Meta.Contains(TEXT("타르코프"));
    const bool bForceOutdoor =
        Meta.Contains(TEXT("openworld")) ||
        Meta.Contains(TEXT("open world")) ||
        Meta.Contains(TEXT("outdoor")) ||
        Meta.Contains(TEXT("오픈월드")) ||
        Meta.Contains(TEXT("야외"));
    const bool bEnvOutdoor =
        Env.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase) ||
        Env.Equals(TEXT("Nature"), ESearchCase::IgnoreCase) ||
        Env.Equals(TEXT("NatureVillage"), ESearchCase::IgnoreCase);
    const bool bIsOpenWorld = bForceOutdoor || (!bForceIndoor && bEnvOutdoor);
    const float Half = RoomRadius - 200.0f; // 외곽 여백

    // =========================================================================
    // 🌳 [TRACK 1: 오픈월드/자연] 외벽 없음, 규칙 없음, 무작위 산포(Scatter)
    // =========================================================================
    if (bIsOpenWorld)
    {
        int32 ScatterCount = (GridSize >= 21) ? 12 : ((GridSize >= 13) ? 7 : 4);
        ScatterCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 6.0f);
        ScatterCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6) - 1;
        const int32 TargetScatterCount = FMath::Max(0, FMath::RoundToInt(ScatterCount * ObstacleSpawnCountScale));
        const int32 MaxAttempts = FMath::Max(TargetScatterCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetScatterCount);
        int32 PlacedScatterCount = 0;
        int32 AttemptedScatterCount = 0;
        ObstacleTargetCount += TargetScatterCount;
        while (PlacedScatterCount < TargetScatterCount && AttemptedScatterCount < MaxAttempts)
        {
            ++AttemptedScatterCount;
            ++ObstacleAttemptCount;
            FVector RandomLoc(Rng.FRandRange(-Half + 300, Half - 300), Rng.FRandRange(-Half + 300, Half - 300), 0.0f);
            if (RandomLoc.Size2D() < 800.0f)
            {
                ++ObstacleSkipCenterCount;
                continue; // 중앙은 교전을 위해 비워둠
            }

            float ScaleX = Rng.FRandRange(1.5f, 3.5f);
            float ScaleY = Rng.FRandRange(1.5f, 3.5f);
            float Height = Rng.FRandRange(1.5f, 4.0f);
            float RandomYaw = Rng.FRandRange(0.0f, 360.0f); // 360도 무작위 각도
            EObstacleSpawnFailReason FailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(RandomLoc + FVector(0, 0, Height * 50.f), FVector(ScaleX, ScaleY, Height), RandomYaw, 2, &FailReason))
            {
                ++PlacedScatterCount;
                ++ObstaclePlacedCount;
            }
            else if (FailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
            }
            else if (FailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
            }
        }
    }
    // =========================================================================
    // 🏢 [TRACK 2: 현대 전술 CQB] 밀봉된 외벽 + 스마트 문 뚫기 + 전술 엄폐물
    // =========================================================================
    else
    {
        float WallThickness = 0.5f; // 벽 두께 50cm
        float WallHeight = 4.0f;    // 층고 4m
        float DoorWidth = FMath::Lerp(2.4f, 3.8f, FMath::Clamp(NodeRow.EnterableBuildingRatio, 0.0f, 1.0f)); // 데이터 기반 문 폭

        // 스마트 벽 깎기 알고리즘 (Smart Edge Carving)
        auto BuildSmartWall = [&](bool bHasDoor, FVector CenterOffset, float Yaw) {
            if (!bHasDoor) {
                // 꽉 막힌 솔리드 벽 스폰
                SpawnBox(CenterOffset + FVector(0, 0, WallHeight * 50.0f), FVector((Half * 2.0f) / 100.0f, WallThickness, WallHeight), Yaw, 1);
            }
            else {
                // 문이 있는 경우, 양옆으로 벽을 쪼개서 스폰하고 중앙을 비움!
                float SideWidth = ((Half * 2.0f) - (DoorWidth * 100.0f)) / 2.0f;
                float LeftOffset = DoorWidth * 50.0f + SideWidth / 2.0f;

                FTransform BaseWallTrans(FRotator(0, Yaw, 0), CenterOffset);
                FVector LeftLoc = BaseWallTrans.TransformPosition(FVector(-LeftOffset, 0, WallHeight * 50.0f));
                FVector RightLoc = BaseWallTrans.TransformPosition(FVector(LeftOffset, 0, WallHeight * 50.0f));

                SpawnBox(LeftLoc, FVector(SideWidth / 100.0f, WallThickness, WallHeight), Yaw, 1);
                SpawnBox(RightLoc, FVector(SideWidth / 100.0f, WallThickness, WallHeight), Yaw, 1);

                // 문 위쪽 헤더(인방) 막아주기 (문 높이는 3m, 층고는 4m이므로 위쪽 1m를 덮음)
                FVector HeaderLoc = BaseWallTrans.TransformPosition(FVector(0, 0, WallHeight * 100.0f - 50.0f));
                SpawnBox(HeaderLoc, FVector(DoorWidth, WallThickness, 1.0f), Yaw, 1);
            }
            };

        // 매니저가 지정해준 연결 방향에만 물리적으로 문을 뚫음
        BuildSmartWall(bDoorNorth, FVector(Half, 0, 0), 0.0f);
        BuildSmartWall(bDoorSouth, FVector(-Half, 0, 0), 0.0f);
        BuildSmartWall(bDoorEast, FVector(0, Half, 0), 90.0f);
        BuildSmartWall(bDoorWest, FVector(0, -Half, 0), 90.0f);

        // 내부 전술 엄폐물 세팅 (십자 도로 대신 L자 벽, 기둥 산개 배치)
        int32 CoverCount = (GridSize >= 15) ? 7 : 3;
        CoverCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6);
        CoverCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 4.0f);
        CoverCount = FMath::Clamp(CoverCount, 4, 20);
        const int32 TargetCoverCount = FMath::Max(0, FMath::RoundToInt(CoverCount * ObstacleSpawnCountScale));
        const int32 CoverMaxAttempts = FMath::Max(TargetCoverCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetCoverCount);
        int32 CoverAttempts = 0;
        int32 PlacedCoverCount = 0;
        ObstacleTargetCount += TargetCoverCount;
        while (PlacedCoverCount < TargetCoverCount && CoverAttempts < CoverMaxAttempts) {
            ++CoverAttempts;
            ++ObstacleAttemptCount;
            FVector CoverLoc(Rng.FRandRange(-Half + 400, Half - 400), Rng.FRandRange(-Half + 400, Half - 400), WallHeight * 50.0f);

            // 문 앞(중앙 크로스 라인)은 사격 통제선(Fatal Funnel)이므로 비워둠
            if (FMath::Abs(CoverLoc.X) < 400.0f || FMath::Abs(CoverLoc.Y) < 400.0f)
            {
                ++ObstacleSkipCenterCount;
                continue;
            }

            // 기둥 생성
            EObstacleSpawnFailReason MainFailReason = EObstacleSpawnFailReason::None;
            const bool bMainPlaced = SpawnBox(CoverLoc, FVector(1.0f, 1.0f, WallHeight), 0.0f, 2, &MainFailReason);
            if (bMainPlaced)
            {
                ++PlacedCoverCount;
                ++ObstaclePlacedCount;
            }
            else if (MainFailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
                continue;
            }
            else if (MainFailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
                continue;
            }

            // 50% 확률로 기둥 옆에 벽을 덧대어 L자형 사각지대(코너) 생성
            if (Rng.FRand() < 0.5f) {
                ++ObstacleAttemptCount;
                EObstacleSpawnFailReason SideFailReason = EObstacleSpawnFailReason::None;
                const bool bSidePlaced =
                    (Rng.FRand() < 0.5f)
                    ? SpawnBox(CoverLoc + FVector(150.f, 0, 0), FVector(2.0f, 0.5f, WallHeight), 0.f, 2, &SideFailReason)
                    : SpawnBox(CoverLoc + FVector(0, 150.f, 0), FVector(0.5f, 2.0f, WallHeight), 0.f, 2, &SideFailReason);
                if (bSidePlaced)
                {
                    ++ObstaclePlacedCount;
                }
                else if (SideFailReason == EObstacleSpawnFailReason::NoVariation)
                {
                    ++ObstacleNoVariationCount;
                }
                else if (SideFailReason == EObstacleSpawnFailReason::PlacementRejected)
                {
                    ++ObstaclePlacementRejectCount;
                }
            }
        }

        // 무작위 박스/책상/엄폐물 배치
        const int32 BasePropCount = FMath::Clamp(4 + NodeRow.TraversalLaneSeeds + FMath::RoundToInt(NodeRow.ObstacleDensity * 4.0f), 4, 14);
        const int32 TargetPropCount = FMath::Max(0, FMath::RoundToInt(BasePropCount * ObstacleSpawnCountScale));
        const int32 PropMaxAttempts = FMath::Max(TargetPropCount * FMath::Max(1, ObstaclePlacementAttemptMultiplier), TargetPropCount);
        int32 PropAttempts = 0;
        int32 PlacedPropCount = 0;
        ObstacleTargetCount += TargetPropCount;
        while (PlacedPropCount < TargetPropCount && PropAttempts < PropMaxAttempts) {
            ++PropAttempts;
            ++ObstacleAttemptCount;
            FVector BoxLoc(Rng.FRandRange(-Half + 300, Half - 300), Rng.FRandRange(-Half + 300, Half - 300), 50.0f);
            if (FMath::Abs(BoxLoc.X) < 300.0f || FMath::Abs(BoxLoc.Y) < 300.0f)
            {
                ++ObstacleSkipCenterCount;
                continue;
            }

            EObstacleSpawnFailReason PropFailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(BoxLoc, FVector(1.5f, 1.0f, 1.0f), Rng.FRandRange(0.f, 360.f), 2, &PropFailReason))
            {
                ++PlacedPropCount;
                ++ObstaclePlacedCount;
            }
            else if (PropFailReason == EObstacleSpawnFailReason::NoVariation)
            {
                ++ObstacleNoVariationCount;
            }
            else if (PropFailReason == EObstacleSpawnFailReason::PlacementRejected)
            {
                ++ObstaclePlacementRejectCount;
            }
        }
    }

    // Decoration pass: spawn theme decoration variations as room props.
    if (ThemeDecorationVariations.Num() > 0)
    {
        const bool bLootLikeRoom =
            CurrentRoomType == ERaidRoomType::Loot ||
            CurrentRoomType == ERaidRoomType::Start ||
            CurrentRoomType == ERaidRoomType::Exit;
        int32 BaseDecorationCount = bIsOpenWorld ? 8 : 5;
        if (bLootLikeRoom)
        {
            BaseDecorationCount += 3;
        }
        BaseDecorationCount += FMath::Clamp(NodeRow.TraversalLaneSeeds, 1, 6) - 1;
        BaseDecorationCount += FMath::RoundToInt(FMath::Clamp(NodeRow.ObstacleDensity, 0.0f, 1.0f) * 4.0f);

        const int32 TargetDecorationCount = FMath::Max(0, FMath::RoundToInt((float)BaseDecorationCount * FMath::Max(0.4f, ObstacleSpawnCountScale)));
        const int32 DecorationMaxAttempts = FMath::Max(TargetDecorationCount * FMath::Max(2, ObstaclePlacementAttemptMultiplier / 2), TargetDecorationCount);
        int32 PlacedDecorationCount = 0;
        int32 DecorationAttempts = 0;

        while (PlacedDecorationCount < TargetDecorationCount && DecorationAttempts < DecorationMaxAttempts)
        {
            ++DecorationAttempts;
            FVector DecoLoc(
                Rng.FRandRange(-Half + 260.0f, Half - 260.0f),
                Rng.FRandRange(-Half + 260.0f, Half - 260.0f),
                45.0f);

            if (!bIsOpenWorld && (FMath::Abs(DecoLoc.X) < 220.0f || FMath::Abs(DecoLoc.Y) < 220.0f))
            {
                continue;
            }

            const float DecoScale = Rng.FRandRange(0.8f, 1.25f);
            EObstacleSpawnFailReason DecoFailReason = EObstacleSpawnFailReason::None;
            if (SpawnBox(
                DecoLoc,
                FVector(DecoScale, DecoScale, DecoScale),
                Rng.FRandRange(0.0f, 360.0f),
                3,
                &DecoFailReason))
            {
                ++PlacedDecorationCount;
            }
        }
    }

    if (bLogObstacleSpawnSummary && ObstacleAttemptCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidRoom] Node=%d obstacle summary: target=%d attempts=%d placed=%d center-skip=%d no-variation=%d placement-reject=%d bp-placed=%d bp-cap=%d density=%.2f count-scale=%.2f sm-w-scale=%.2f bp-w-scale=%.2f"),
            NodeId,
            ObstacleTargetCount,
            ObstacleAttemptCount,
            ObstaclePlacedCount,
            ObstacleSkipCenterCount,
            ObstacleNoVariationCount,
            ObstaclePlacementRejectCount,
            SpawnedBlueprintObstacleCount,
            MaxBlueprintObstaclesPerRoom,
            NodeRow.ObstacleDensity,
            ObstacleSpawnCountScale,
            StaticMeshObstacleWeightScale,
            BlueprintObstacleWeightScale);
    }

    if (bLogObstacleVariationBreakdown && (ObstacleVariationPickCounts.Num() > 0 || ObstacleVariationConfiguredWeights.Num() > 0))
    {
        TArray<FString> VariationKeys;
        ObstacleVariationConfiguredWeights.GetKeys(VariationKeys);
        VariationKeys.Sort([&](const FString& A, const FString& B)
            {
                return ObstacleVariationPickCounts.FindRef(A) > ObstacleVariationPickCounts.FindRef(B);
            });

        TArray<FString> BreakdownParts;
        for (const FString& Key : VariationKeys)
        {
            const int32 PickedCount = ObstacleVariationPickCounts.FindRef(Key);
            const int32 PlacedCount = ObstacleVariationPlacedCounts.FindRef(Key);
            const float ConfiguredWeight = ObstacleVariationConfiguredWeights.FindRef(Key);
            const float EffectiveWeight = ObstacleVariationEffectiveWeights.FindRef(Key);
            if (PickedCount <= 0 && PlacedCount <= 0 && ConfiguredWeight <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            BreakdownParts.Add(FString::Printf(
                TEXT("%s{cfg=%.2f eff=%.2f pick=%d place=%d}"),
                *Key,
                ConfiguredWeight,
                EffectiveWeight,
                PickedCount,
                PlacedCount));

            if (BreakdownParts.Num() >= 12)
            {
                break;
            }
        }

        if (BreakdownParts.Num() > 0)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidRoom] Node=%d obstacle variation breakdown: %s"),
                NodeId,
                *FString::Join(BreakdownParts, TEXT(" | ")));
        }
    }
}

void ARaidRoomActor::InternalSpawnLoot()
{
    if (bLootAlreadySpawned) return; bLootAlreadySpawned = true;
    if (ChapterConfigRef && ChapterConfigRef->LootRegistry)
    {
        int32 LCount = NodeRow.LootCount > 0 ? NodeRow.LootCount : 3; FVector CenterLoc = GetActorLocation();
        bool bIsCentral = NodeRow.LootStrategy.Equals(TEXT("Central_Cache"), ESearchCase::IgnoreCase);
        float MinDistance = bIsCentral ? 100.0f : 300.0f; float MaxDistance = bIsCentral ? 250.0f : ((GridSize * TileSize) / 2.0f - 200.0f); float AngleStep = 360.0f / (float)FMath::Max(1, LCount);

        for (int32 i = 0; i < LCount; ++i)
        {
            if (const FRaidLootCandidate* Candidate = ChapterConfigRef->LootRegistry->GetRandomCandidate(NodeRow.LootLevel))
            {
                if (Candidate->ItemClass)
                {
                    float Radian = FMath::DegreesToRadians(i * AngleStep + RoomRandomStream.FRandRange(-20.0f, 20.0f)); float Distance = RoomRandomStream.FRandRange(MinDistance, MaxDistance);
                    FVector Offset(FMath::Cos(Radian) * Distance, FMath::Sin(Radian) * Distance, 0.0f);
                    FVector StartLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, 1000.0f); FVector EndLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, -500.0f);

                    FHitResult HitResult; FCollisionQueryParams QueryParams; QueryParams.bTraceComplex = true; QueryParams.AddIgnoredActor(this);
                    FVector FinalSpawnLoc = CenterLoc + Offset + FVector(0.0f, 0.0f, 150.0f);
                    FRotator FinalRotation = FRotator(0.0f, RoomRandomStream.FRandRange(0.0f, 360.0f), 0.0f);

                    if (TryResolveSingleGroundHitAlongTrace(GetWorld(), StartLoc, EndLoc, false, QueryParams, HitResult)) {
                        FinalSpawnLoc = HitResult.ImpactPoint + FVector(0.0f, 0.0f, 10.0f); // 바닥에서 살짝 위
                        FRotator AlignedRot = FRotationMatrix::MakeFromZX(HitResult.ImpactNormal, FVector(FMath::Cos(FinalRotation.Yaw), FMath::Sin(FinalRotation.Yaw), 0.0f)).Rotator();
                        FinalRotation = (Candidate->Category == ERaidLootCategory::Rifle || Candidate->Category == ERaidLootCategory::Pistol) ? (AlignedRot.Quaternion() * FRotator(90.0f, 0.0f, 0.0f).Quaternion()).Rotator() : AlignedRot;
                    }

                    const FTransform SpawnTransform(FinalRotation, FinalSpawnLoc);
                    if (AActor* SpawnedItem = GetWorld()->SpawnActorDeferred<AActor>(Candidate->ItemClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
                    {
                        int32 AppliedRowNameOverrideFields = 0;
                        if (!Candidate->DataRowName.IsNone())
                        {
                            AppliedRowNameOverrideFields += ApplyLootDataRowNameOverrideToObject(SpawnedItem, Candidate->DataRowName);
                            TInlineComponentArray<UActorComponent*> PreFinishComponents(SpawnedItem);
                            for (UActorComponent* Component : PreFinishComponents)
                            {
                                AppliedRowNameOverrideFields += ApplyLootDataRowNameOverrideToObject(Component, Candidate->DataRowName);
                            }
                        }

                        FRaidLootRuntimeRowData ResolvedLootRowData;
                        int32 AppliedLootFields = AppliedRowNameOverrideFields;
                        if (bApplyLootDataTableValuesAtSpawn)
                        {
                            if (ResolveLootRuntimeRowForSpawnedClass(Candidate, SpawnedItem->GetClass(), ResolvedLootRowData))
                            {
                                AppliedLootFields += ApplyLootRuntimeRowDataToActor(
                                    SpawnedItem,
                                    ResolvedLootRowData,
                                    bApplyLootParamValuesAtSpawn,
                                    bApplyLootQuantityValuesAtSpawn,
                                    bApplyLootPickupRestrictionValuesAtSpawn,
                                    Candidate->DataRowName.IsNone());
                                if (bLogLootDataBinding)
                                {
                                    UE_LOG(
                                        LogTemp,
                                        Warning,
                                        TEXT("[RaidRoom] LootDataBinding Room=%d Item=%s Row=%s AppliedFields=%d Param1=%.2f Param2=%.2f CurrentQty=%.2f MaxQty=%.2f"),
                                        NodeId,
                                        *GetNameSafe(SpawnedItem),
                                        *ResolvedLootRowData.RowName,
                                        AppliedLootFields,
                                        ResolvedLootRowData.Param1,
                                        ResolvedLootRowData.Param2,
                                        ResolvedLootRowData.CurrentQuantity,
                                        ResolvedLootRowData.MaxQuantity);
                                }
                            }
                            else if (bLogLootDataBinding)
                            {
                                UE_LOG(
                                    LogTemp,
                                    Warning,
                                    TEXT("[RaidRoom] LootDataBinding row not found for item class: %s (RequestedRow=%s, AppliedRowOverrideFields=%d)"),
                                    *GetNameSafe(Candidate->ItemClass.Get()),
                                    *Candidate->DataRowName.ToString(),
                                    AppliedRowNameOverrideFields);
                            }
                        }

                        UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);

                        if (!Candidate->DataRowName.IsNone())
                        {
                            ApplyLootDataRowNameOverrideToObject(SpawnedItem, Candidate->DataRowName);
                            TInlineComponentArray<UActorComponent*> PostFinishComponents(SpawnedItem);
                            for (UActorComponent* Component : PostFinishComponents)
                            {
                                ApplyLootDataRowNameOverrideToObject(Component, Candidate->DataRowName);
                            }
                        }

                        // 엔진이 위치를 비키면서 공중에 띄워버렸을 경우를 대비해, 다시 바닥으로 내려주는 안전장치
                        FHitResult GroundHit;
                        if (TryResolveSingleGroundHitAlongTrace(
                            GetWorld(),
                            SpawnedItem->GetActorLocation(),
                            SpawnedItem->GetActorLocation() - FVector(0.0f, 0.0f, 500.0f),
                            false,
                            QueryParams,
                            GroundHit))
                        {
                            SpawnedItem->SetActorLocation(
                                GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 5.0f),
                                false,
                                nullptr,
                                ETeleportType::TeleportPhysics);
                        }

                        SetLootActorProximityFx(SpawnedItem, false);
                        SetLootActorOutline(SpawnedItem, false);
                        SetLootActorDotWidget(SpawnedItem, false);
                        SpawnedLootActors.AddUnique(SpawnedItem);
                    }
                }
            }
        }
    }
    StatusText->SetTextRenderColor(FColor::Green); StatusText->SetText(FText::FromString(TEXT("CLEARED!"))); OpenRoom();
}

void ARaidRoomActor::SetCombatCleared(bool bCleared) { bCombatCleared = bCleared; if (bCleared) { StatusText->SetTextRenderColor(FColor::Green); StatusText->SetText(FText::FromString(TEXT("CLEARED!"))); OpenRoom(); } }
void ARaidRoomActor::OpenRoom() { for (AActor* Door : SpawnedDoorActors) { if (IsValid(Door)) Door->Destroy(); } SpawnedDoorActors.Empty(); }
FVector ARaidRoomActor::GetRoomExtent() const { return FVector((GridSize * TileSize) / 2.0f); }
bool ARaidRoomActor::TryShowRegionBanner(APawn* OverlappingPawn)
{
    if (bEntryBannerShown)
    {
        bPendingBannerRetry = false;
        return true;
    }

    if (!OverlappingPawn || !OverlappingPawn->IsPlayerControlled())
    {
        return false;
    }

    UWorld* World = GetWorld();
    const double NowSeconds = World ? World->GetTimeSeconds() : 0.0;

    APlayerController* PC = Cast<APlayerController>(OverlappingPawn->GetController());
    if (!PC)
    {
        NextBannerAttemptTimeSeconds = NowSeconds + 0.08;
        return false;
    }

    if (!PC->IsLocalController())
    {
        NextBannerAttemptTimeSeconds = NowSeconds + 0.25;
        return false;
    }

    FString TitleStr = ResolveBannerTitleFromNodeTags(NodeRow);
    FString SubStr = ResolveBannerSubtitleFromRoomType(NodeRow);
    if (TitleStr.IsEmpty() || TitleStr.Contains(TEXT("[")))
    {
        TitleStr = TEXT("미확인 구역 (Unknown Sector)");
    }
    if (SubStr.IsEmpty())
    {
        SubStr = TEXT("구역 진입");
    }

    if (URaidCombatSubsystem* CombatSubsystem = World ? World->GetSubsystem<URaidCombatSubsystem>() : nullptr)
    {
        CombatSubsystem->EnqueueRegionBannerMessage(FText::FromString(TitleStr), FText::FromString(SubStr), 4.0f, true);
        bEntryBannerShown = true;
        bPendingBannerRetry = false;
        NextBannerAttemptTimeSeconds = 0.0;
        return true;
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        if (World && SharedWidget->GetWorld() != World)
        {
            if (SharedWidget->IsInViewport())
            {
                SharedWidget->RemoveFromParent();
            }
            GSharedRegionBannerWidget.Reset();
        }
    }

    UClass* WidgetClass = CachedRegionBannerWidgetClass.Get();
    if (!WidgetClass)
    {
        WidgetClass = RegionBannerWidgetClass.LoadSynchronous();
        if (!WidgetClass)
        {
            WidgetClass = LoadClass<URaidRegionBannerWidget>(nullptr, TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C"));
        }
        CachedRegionBannerWidgetClass = WidgetClass;
    }
    if (!WidgetClass)
    {
        NextBannerAttemptTimeSeconds = NowSeconds + 0.30;
        return false;
    }

    if (URaidRegionBannerWidget* SharedWidget = GSharedRegionBannerWidget.Get())
    {
        ActiveRegionBannerWidget = SharedWidget;
    }
    else
    {
        ActiveRegionBannerWidget = CreateWidget<URaidRegionBannerWidget>(PC, WidgetClass);
        if (!ActiveRegionBannerWidget)
        {
            NextBannerAttemptTimeSeconds = NowSeconds + 0.20;
            return false;
        }
        GSharedRegionBannerWidget = ActiveRegionBannerWidget;
    }

    bEntryBannerShown = true;
    bPendingBannerRetry = false;
    NextBannerAttemptTimeSeconds = 0.0;
    if (!ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->AddToViewport(25);
    }
    ActiveRegionBannerWidget->ShowRegionTitle(FText::FromString(TitleStr), FText::FromString(SubStr), 4.0f);

    if (UWorld* WorldPtr = GetWorld())
    {
        WorldPtr->GetTimerManager().ClearTimer(RegionBannerHideTimerHandle);
        WorldPtr->GetTimerManager().SetTimer(
            RegionBannerHideTimerHandle,
            FTimerDelegate::CreateLambda([]()
                {
                    if (URaidRegionBannerWidget* Widget = GSharedRegionBannerWidget.Get())
                    {
                        if (Widget->IsInViewport())
                        {
                            Widget->RemoveFromParent();
                        }
                    }
                }),
            5.5f,
            false);
    }

    return true;
}
void ARaidRoomActor::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (APawn* OverlappingPawn = Cast<APawn>(OtherActor)) {
        if (OverlappingPawn->IsPlayerControlled())
        {
            if (IsPawnInsideRoomBannerZone(this, OverlappingPawn))
            {
                bPendingBannerRetry = !TryShowRegionBanner(OverlappingPawn);
            }
        }
        if (OverlappingPawn->IsPlayerControlled() && !bCombatStarted && !bCombatCleared) {
            if (URaidCombatSubsystem* CombatSubsystem = GetWorld()->GetSubsystem<URaidCombatSubsystem>()) { CombatSubsystem->StartCombatForRoom(this); Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
        }
    }
}

void ARaidRoomActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (!ChapterConfigRef)
    {
        ChapterConfigRef = ChapterConfigAsset.Get();
        if (!ChapterConfigRef && !ChapterConfigAsset.IsNull())
        {
            ChapterConfigRef = ChapterConfigAsset.LoadSynchronous();
        }
    }

    if (!bNodeDataInitialized)
    {
        const bool bLooksLikeSerializedNode =
            NodeId > 0 ||
            NodeRow.NodeId > 0 ||
            !NodeRow.RoomType.IsEmpty() ||
            !NodeRow.Theme.IsEmpty() ||
            !NodeRow.NodeTags.IsEmpty();

        if (bLooksLikeSerializedNode)
        {
            if (NodeId <= 0 && NodeRow.NodeId > 0)
            {
                NodeId = NodeRow.NodeId;
            }
            if (NodeRow.NodeId <= 0 && NodeId > 0)
            {
                NodeRow.NodeId = NodeId;
            }

            CurrentRoomType = RaidRoomParsing::ParseRoomType(NodeRow.RoomType);
            ApplyGridSizeFromRoomSizeToken(NodeRow.RoomSize, GridSize);
            RoomRandomStream.Initialize(NodeRow.Seed);
            bNodeDataInitialized = true;
            bLootAlreadySpawned = false;
            bEntryBannerShown = false;
            bPendingBannerRetry = false;
            bWasPlayerInsideBannerZone = false;
            NextBannerAttemptTimeSeconds = 0.0;
            NextLootOutlineUpdateTimeSeconds = 0.0;
            CachedProximityAutoStartDistanceUU = -1.0f;
        }
    }

    // 🔥 1. 트리거(마커 감지 영역)를 오픈월드 스케일에 맞게 거대하게 확장
    if (Trigger)
    {
        float RoomRadius = (GridSize * TileSize) / 2.0f;
        Trigger->SetBoxExtent(FVector(RoomRadius, RoomRadius, 10000.0f));
        Trigger->SetRelativeLocation(FVector(0.0f, 0.0f, 2000.0f));
    }

    // Runtime(PIE/Game)에서는 RaidLayoutManager가 한 번에 생성/정리하므로
    // OnConstruction 재생성은 중복 스폰/프레임 급락을 유발한다.
    if (UWorld* World = GetWorld())
    {
        const bool bEditorPreviewWorld =
            World->WorldType == EWorldType::Editor ||
            World->WorldType == EWorldType::EditorPreview;
        if (!bEditorPreviewWorld)
        {
            return;
        }
    }

    // Editor 프리뷰 월드에서만 레이아웃 미리보기 생성.
    GenerateRoomLayout();
}

