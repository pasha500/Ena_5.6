#include "Raid/RaidCombatSubsystem.h"
#include "Raid/RaidRoomActor.h"
#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidEnemyPresetRegistry.h"
#include "Raid/RaidRegionBannerWidget.h"
#include "Raid/RaidWaveProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "Perception/AISense_Hearing.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/Volume.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
    bool IsWaterHit(const FHitResult& Hit);
    const FName RaidTraceSanitizedTag(TEXT("RaidTraceSanitized"));
    constexpr int32 RaidWaveRoomIdBase = -50000;

    int32 MakeWaveRoomId(const int32 WaveNumber)
    {
        return RaidWaveRoomIdBase - FMath::Max(1, WaveNumber);
    }

    bool TryDecodeWaveRoomId(const int32 RoomId, int32& OutWaveNumber)
    {
        if (RoomId >= RaidWaveRoomIdBase)
        {
            return false;
        }

        OutWaveNumber = RaidWaveRoomIdBase - RoomId;
        return OutWaveNumber > 0;
    }

    bool IsCombatSpawnRoomType(const FString& RoomType)
    {
        return
            RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase) ||
            RoomType.Equals(TEXT("Normal"), ESearchCase::IgnoreCase) ||
            RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase);
    }

    void ForceAllGameTraceChannelsToBlock(UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return;
        }

        for (int32 ChannelIdx = (int32)ECC_GameTraceChannel1; ChannelIdx <= (int32)ECC_GameTraceChannel18; ++ChannelIdx)
        {
            Primitive->SetCollisionResponseToChannel((ECollisionChannel)ChannelIdx, ECR_Block);
        }
    }

    void ForceAllGameTraceChannelsToIgnore(UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return;
        }

        for (int32 ChannelIdx = (int32)ECC_GameTraceChannel1; ChannelIdx <= (int32)ECC_GameTraceChannel18; ++ChannelIdx)
        {
            Primitive->SetCollisionResponseToChannel((ECollisionChannel)ChannelIdx, ECR_Ignore);
        }
    }

    bool ContainsAnyToken(const FString& Value, const std::initializer_list<const TCHAR*>& Tokens)
    {
        for (const TCHAR* Token : Tokens)
        {
            if (Value.Contains(Token, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    bool HasMeshTypeLikeTag(const TArray<FName>& Tags)
    {
        for (const FName& Tag : Tags)
        {
            const FString TagName = Tag.ToString();
            if (TagName.StartsWith(TEXT("MeshType_")) || TagName.StartsWith(TEXT("RaidRoomNode_")))
            {
                return true;
            }
        }

        return false;
    }

    bool IsRaidRoomGameplayTraceComponent(AActor* Candidate, UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Candidate) || !IsValid(Primitive))
        {
            return false;
        }

        const FString ActorClassName = Candidate->GetClass()->GetName();
        const bool bActorIsRaidRoomLike =
            ActorClassName.Contains(TEXT("RaidRoomActor"), ESearchCase::IgnoreCase);

        const bool bActorTaggedAsRoomGameplay =
            Candidate->ActorHasTag(TEXT("RaidRoomGenerated")) ||
            Candidate->ActorHasTag(TEXT("RaidDoorBlocker")) ||
            Candidate->ActorHasTag(TEXT("ObstacleBlueprint")) ||
            HasMeshTypeLikeTag(Candidate->Tags);

        const bool bComponentTaggedAsRoomGameplay =
            Primitive->ComponentHasTag(TEXT("RaidRoomRuntimeISMC")) ||
            Primitive->ComponentHasTag(TEXT("RaidRuntimeISMC")) ||
            Primitive->ComponentHasTag(TEXT("ObstacleBlueprint")) ||
            HasMeshTypeLikeTag(Primitive->ComponentTags);

        return bActorIsRaidRoomLike || bActorTaggedAsRoomGameplay || bComponentTaggedAsRoomGameplay;
    }

    bool ReadBoolPropertyValue(const UObject* TargetObject, const FName PropertyName, bool& OutValue)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        const FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(TargetObject->GetClass(), PropertyName);
        if (!BoolProperty)
        {
            return false;
        }

        OutValue = BoolProperty->GetPropertyValue_InContainer(TargetObject);
        return true;
    }

    bool ReadFloatPropertyValue(const UObject* TargetObject, const FName PropertyName, float& OutValue)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        const FProperty* Property = TargetObject->GetClass()->FindPropertyByName(PropertyName);
        if (!Property)
        {
            return false;
        }

        if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            OutValue = FloatProperty->GetPropertyValue_InContainer(TargetObject);
            return true;
        }

        if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            OutValue = static_cast<float>(DoubleProperty->GetPropertyValue_InContainer(TargetObject));
            return true;
        }

        if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            if (NumericProperty->IsFloatingPoint())
            {
                const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(TargetObject);
                OutValue = static_cast<float>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
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
                return true;
            }

            NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToInt64(InValue)));
            return true;
        }

        return false;
    }

    bool IsMedicalLootIdentifier(const FString& Value)
    {
        return
            ContainsAnyToken(Value, { TEXT("Medical"), TEXT("Bandage"), TEXT("Syringe"), TEXT("Medkit"), TEXT("BP_LootPickableItem_MedicalItem") });
    }

    bool SetBoolPropertyIfExists(UObject* TargetObject, const FName PropertyName, const bool bNewValue)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(TargetObject->GetClass(), PropertyName))
        {
            const bool bOldValue = BoolProperty->GetPropertyValue_InContainer(TargetObject);
            if (bOldValue != bNewValue)
            {
                BoolProperty->SetPropertyValue_InContainer(TargetObject, bNewValue);
            }
            return true;
        }

        return false;
    }

    bool SetClassPropertyToNullIfExists(UObject* TargetObject, const FName PropertyName)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        if (FClassProperty* ClassProperty = FindFProperty<FClassProperty>(TargetObject->GetClass(), PropertyName))
        {
            if (ClassProperty->GetObjectPropertyValue_InContainer(TargetObject) != nullptr)
            {
                ClassProperty->SetObjectPropertyValue_InContainer(TargetObject, nullptr);
            }
            return true;
        }

        if (FSoftClassProperty* SoftClassProperty = FindFProperty<FSoftClassProperty>(TargetObject->GetClass(), PropertyName))
        {
            const FSoftObjectPtr EmptyValue;
            SoftClassProperty->SetPropertyValue_InContainer(TargetObject, EmptyValue);
            return true;
        }

        return false;
    }

    bool IsDropSoulLikeNamePath(const FString& ClassName, const FString& ObjectName, const FString& ObjectPath)
    {
        return
            ContainsAnyToken(ClassName, { TEXT("DropSoul"), TEXT("Drop_Soul"), TEXT("BP_DropSoul") }) ||
            ContainsAnyToken(ObjectName, { TEXT("DropSoul"), TEXT("Drop_Soul"), TEXT("BP_DropSoul") }) ||
            ContainsAnyToken(ObjectPath, { TEXT("/Drop_VFX/"), TEXT("DropSoul"), TEXT("Drop_Soul"), TEXT("BP_DropSoul") });
    }

    bool IsDropSoulObject(const UObject* Candidate)
    {
        if (!IsValid(Candidate))
        {
            return false;
        }

        const FString ClassName = Candidate->GetClass()->GetName();
        const FString ObjectName = Candidate->GetName();
        const FString ObjectPath = Candidate->GetPathName();
        return IsDropSoulLikeNamePath(ClassName, ObjectName, ObjectPath);
    }

    bool IsVisualOnlyTraceHelperComponent(const UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return false;
        }

        if (IsDropSoulObject(Primitive) || IsDropSoulObject(Primitive->GetOwner()))
        {
            // Core gameplay pickup VFX: never classify as disposable trace helper.
            return false;
        }

        const FString PrimitiveClassName = Primitive->GetClass()->GetName();
        const FString PrimitiveName = Primitive->GetName();

        return
            ContainsAnyToken(
                PrimitiveClassName,
                {
                    TEXT("NiagaraComponent"),
                    TEXT("ParticleSystemComponent"),
                    TEXT("CascadeParticleSystemComponent"),
                    TEXT("BillboardComponent"),
                    TEXT("MaterialBillboardComponent"),
                    TEXT("WidgetComponent"),
                    TEXT("TextRenderComponent"),
                    TEXT("ArrowComponent")
                }) ||
            ContainsAnyToken(
                PrimitiveName,
                {
                    TEXT("Rain"),
                    TEXT("Precip"),
                    TEXT("Splash"),
                    TEXT("Drop"),
                    TEXT("Weather"),
                    TEXT("Niagara"),
                    TEXT("Particle"),
                    TEXT("Billboard"),
                    TEXT("Widget"),
                    TEXT("RainFX"),
                    TEXT("Blood")
                }) ||
            Primitive->ComponentHasTag(TEXT("Rain")) ||
            Primitive->ComponentHasTag(TEXT("Weather")) ||
            Primitive->ComponentHasTag(TEXT("TraceIgnore"));
    }

    bool IsWaterLikeActor(const AActor* Candidate)
    {
        if (!IsValid(Candidate))
        {
            return false;
        }

        if (const APhysicsVolume* PhysicsVolume = Cast<APhysicsVolume>(Candidate))
        {
            if (PhysicsVolume->bWaterVolume)
            {
                return true;
            }
        }

        if (Candidate->ActorHasTag(TEXT("Water")))
        {
            return true;
        }

        const FString ClassName = Candidate->GetClass()->GetName();
        const FString ActorName = Candidate->GetName();
        return
            ContainsAnyToken(ClassName, { TEXT("Water"), TEXT("Ocean"), TEXT("River") }) ||
            ContainsAnyToken(ActorName, { TEXT("Water"), TEXT("Ocean"), TEXT("River") });
    }

    bool IsWeatherVisualTraceBlocker(AActor* Candidate, UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Candidate) || !IsValid(Primitive))
        {
            return false;
        }

        const FString ActorClassName = Candidate->GetClass()->GetName();
        const FString ActorName = Candidate->GetName();
        const FString PrimitiveName = Primitive->GetName();

        const bool bActorLooksWeatherLike =
            ContainsAnyToken(ActorClassName, { TEXT("UltraDynamicWeather"), TEXT("Ultra_Dynamic_Weather"), TEXT("UltraDynamicSky"), TEXT("Ultra_Dynamic_Sky"), TEXT("UDW"), TEXT("UDS"), TEXT("Weather"), TEXT("Precipitation") }) ||
            ContainsAnyToken(ActorName, { TEXT("UltraDynamicWeather"), TEXT("Ultra_Dynamic_Weather"), TEXT("UltraDynamicSky"), TEXT("Ultra_Dynamic_Sky"), TEXT("UDW"), TEXT("UDS"), TEXT("Weather"), TEXT("Precipitation"), TEXT("Rain") }) ||
            Candidate->ActorHasTag(TEXT("Weather")) ||
            Candidate->ActorHasTag(TEXT("Rain")) ||
            Candidate->ActorHasTag(TEXT("UltraDynamicWeather")) ||
            Candidate->ActorHasTag(TEXT("UltraDynamicSky"));

        const bool bVisualHelperComponent = IsVisualOnlyTraceHelperComponent(Primitive);

        const bool bComponentLooksRainLike =
            ContainsAnyToken(PrimitiveName, { TEXT("Rain"), TEXT("Precip"), TEXT("Weather"), TEXT("Splash"), TEXT("Drop") }) ||
            Primitive->ComponentHasTag(TEXT("Weather")) ||
            Primitive->ComponentHasTag(TEXT("Rain"));

        return bActorLooksWeatherLike || (bVisualHelperComponent && bComponentLooksRainLike);
    }

    bool ShouldSanitizeTraceBlocker(AActor* Candidate, UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Candidate) || !IsValid(Primitive))
        {
            return false;
        }

        if (IsRaidRoomGameplayTraceComponent(Candidate, Primitive))
        {
            // Raid room gameplay geometry must keep trace/collision responses.
            // Sanitizing these components breaks climb/mantle detection on spawned room meshes.
            return false;
        }

        if (IsDropSoulObject(Candidate) || IsDropSoulObject(Primitive) || IsDropSoulObject(Primitive->GetOwner()))
        {
            return false;
        }

        const FString ActorClassName = Candidate->GetClass()->GetName();
        const FString ActorName = Candidate->GetName();
        const FString PrimitiveClassName = Primitive->GetClass()->GetName();
        const FString PrimitiveName = Primitive->GetName();
        const bool bBloodLikeFx =
            ContainsAnyToken(ActorClassName, { TEXT("Blood"), TEXT("DropSoul"), TEXT("FightBlood"), TEXT("Splatter") }) ||
            ContainsAnyToken(ActorName, { TEXT("Blood"), TEXT("DropSoul"), TEXT("FightBlood"), TEXT("Splatter") }) ||
            ContainsAnyToken(PrimitiveClassName, { TEXT("Blood"), TEXT("DropSoul"), TEXT("NiagaraBlood"), TEXT("Splatter") }) ||
            ContainsAnyToken(PrimitiveName, { TEXT("Blood"), TEXT("DropSoul"), TEXT("NiagaraBlood"), TEXT("Splatter") });
        if (bBloodLikeFx)
        {
            // Blood FX lifecycle is controlled by its own blueprints/niagara pooling.
            return false;
        }

        const bool bActorLooksFoliageLike =
            ContainsAnyToken(ActorClassName, { TEXT("InstancedFoliageActor"), TEXT("ProceduralFoliage"), TEXT("Foliage") }) ||
            ContainsAnyToken(ActorName, { TEXT("ProceduralFoliage"), TEXT("Foliage") }) ||
            Candidate->ActorHasTag(TEXT("Foliage")) ||
            Candidate->ActorHasTag(TEXT("ProceduralFoliage"));

        const bool bComponentLooksFoliageLike =
            ContainsAnyToken(PrimitiveClassName, { TEXT("Foliage"), TEXT("InstancedStaticMesh"), TEXT("HierarchicalInstancedStaticMesh"), TEXT("ProceduralFoliage") }) ||
            ContainsAnyToken(PrimitiveName, { TEXT("Foliage"), TEXT("InstancedFoliage"), TEXT("ProceduralFoliage") }) ||
            Primitive->ComponentHasTag(TEXT("Foliage")) ||
            Primitive->ComponentHasTag(TEXT("ProceduralFoliage"));

        const bool bProceduralFoliageVolume =
            ContainsAnyToken(ActorClassName, { TEXT("ProceduralFoliageVolume") }) ||
            ContainsAnyToken(ActorName, { TEXT("ProceduralFoliageVolume") });

        const bool bPhysicsVolume = Candidate->IsA<APhysicsVolume>();
        const bool bWaterLike = IsWaterLikeActor(Candidate);
        const bool bWeatherTraceBlocker = IsWeatherVisualTraceBlocker(Candidate, Primitive);
        return bActorLooksFoliageLike || bComponentLooksFoliageLike || bProceduralFoliageVolume || bPhysicsVolume || bWaterLike || bWeatherTraceBlocker;
    }

    bool ShouldDisableQueryCollisionForTraceBlocker(AActor* Candidate, UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Candidate) || !IsValid(Primitive))
        {
            return false;
        }

        if (IsRaidRoomGameplayTraceComponent(Candidate, Primitive))
        {
            return false;
        }

        if (IsDropSoulObject(Candidate) || IsDropSoulObject(Primitive) || IsDropSoulObject(Primitive->GetOwner()))
        {
            return false;
        }

        const FString ActorClassName = Candidate->GetClass()->GetName();
        const FString ActorName = Candidate->GetName();
        const FString PrimitiveClassName = Primitive->GetClass()->GetName();
        const FString PrimitiveName = Primitive->GetName();

        const bool bLandscapeGrassComponent =
            ContainsAnyToken(PrimitiveName, { TEXT("GrassInstancedStaticMeshComponent"), TEXT("LandscapeGrass") }) ||
            (ContainsAnyToken(ActorClassName, { TEXT("Landscape") }) &&
                ContainsAnyToken(PrimitiveClassName, { TEXT("InstancedStaticMesh"), TEXT("HierarchicalInstancedStaticMesh") }));

        const bool bProceduralFoliageVolumeBrush =
            (ContainsAnyToken(ActorClassName, { TEXT("ProceduralFoliageVolume") }) ||
                ContainsAnyToken(ActorName, { TEXT("ProceduralFoliageVolume") })) &&
            ContainsAnyToken(PrimitiveClassName, { TEXT("BrushComponent") });

        const bool bWaterVisualSurface =
            !Candidate->IsA<APhysicsVolume>() &&
            IsWaterLikeActor(Candidate) &&
            (ContainsAnyToken(PrimitiveName, { TEXT("Plane"), TEXT("Water"), TEXT("Surface") }) ||
                ContainsAnyToken(PrimitiveClassName, { TEXT("StaticMeshComponent"), TEXT("SplineMeshComponent") }));

        const bool bWeatherVisualTraceBlocker = IsWeatherVisualTraceBlocker(Candidate, Primitive);
        return bLandscapeGrassComponent || bProceduralFoliageVolumeBrush || bWaterVisualSurface || bWeatherVisualTraceBlocker;
    }

    UCapsuleComponent* FindNamedCapsuleComponent(APawn* Enemy, const FName ComponentName)
    {
        if (!IsValid(Enemy))
        {
            return nullptr;
        }

        TInlineComponentArray<UCapsuleComponent*> Capsules;
        Enemy->GetComponents(Capsules);
        for (UCapsuleComponent* Capsule : Capsules)
        {
            if (IsValid(Capsule) && Capsule->GetFName() == ComponentName)
            {
                return Capsule;
            }
        }
        return nullptr;
    }

    void EnsureTraceProxyCapsule(APawn* Enemy, const FName ComponentName, ECollisionChannel ObjectType, float Radius, float HalfHeight)
    {
        if (!IsValid(Enemy))
        {
            return;
        }

        UCapsuleComponent* ProxyCapsule = FindNamedCapsuleComponent(Enemy, ComponentName);
        if (!ProxyCapsule)
        {
            ProxyCapsule = NewObject<UCapsuleComponent>(Enemy, ComponentName);
            if (!IsValid(ProxyCapsule))
            {
                return;
            }

            if (USceneComponent* Root = Enemy->GetRootComponent())
            {
                ProxyCapsule->SetupAttachment(Root);
            }
            ProxyCapsule->RegisterComponent();
        }

        ProxyCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        ProxyCapsule->SetCollisionObjectType(ObjectType);
        ProxyCapsule->SetCollisionResponseToAllChannels(ECR_Block);
        ProxyCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        ProxyCapsule->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
        ProxyCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        ProxyCapsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        ForceAllGameTraceChannelsToBlock(ProxyCapsule);
        ProxyCapsule->SetCanEverAffectNavigation(false);
        ProxyCapsule->SetGenerateOverlapEvents(false);
        ProxyCapsule->SetHiddenInGame(true);
        ProxyCapsule->SetVisibility(false, true);
        ProxyCapsule->SetCapsuleSize(FMath::Max(20.0f, Radius), FMath::Max(40.0f, HalfHeight), true);
        ProxyCapsule->SetRelativeLocation(FVector::ZeroVector);
    }

    FString BuildTraceChannelSnapshot(const UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return TEXT("None");
        }

        TArray<FString> BlockedChannels;
        BlockedChannels.Reserve(20);

        if (Primitive->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
        {
            BlockedChannels.Add(TEXT("Visibility"));
        }
        if (Primitive->GetCollisionResponseToChannel(ECC_Camera) == ECR_Block)
        {
            BlockedChannels.Add(TEXT("Camera"));
        }

        for (int32 ChannelIdx = (int32)ECC_GameTraceChannel1; ChannelIdx <= (int32)ECC_GameTraceChannel18; ++ChannelIdx)
        {
            if (Primitive->GetCollisionResponseToChannel((ECollisionChannel)ChannelIdx) == ECR_Block)
            {
                BlockedChannels.Add(FString::Printf(TEXT("GameTrace%d"), ChannelIdx - (int32)ECC_GameTraceChannel1 + 1));
            }
        }

        return FString::Printf(
            TEXT("Enabled=%d ObjType=%d Blocked=[%s]"),
            (int32)Primitive->GetCollisionEnabled(),
            (int32)Primitive->GetCollisionObjectType(),
            *FString::Join(BlockedChannels, TEXT(","))
        );
    }

    bool SetByteOrEnumProperty(UObject* TargetObject, const FName PropertyName, uint8 DesiredValue)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        FProperty* Property = TargetObject->GetClass()->FindPropertyByName(PropertyName);
        if (!Property)
        {
            return false;
        }

        if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            if (FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty())
            {
                void* ValuePtr = EnumProperty->ContainerPtrToValuePtr<void>(TargetObject);
                const int64 CurrentValue = UnderlyingProperty->GetSignedIntPropertyValue(ValuePtr);
                if (CurrentValue != DesiredValue)
                {
                    UnderlyingProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(DesiredValue));
                    return true;
                }
                return false;
            }
            return false;
        }

        if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            const uint8 CurrentValue = ByteProperty->GetPropertyValue_InContainer(TargetObject);
            if (CurrentValue != DesiredValue)
            {
                ByteProperty->SetPropertyValue_InContainer(TargetObject, DesiredValue);
                return true;
            }
            return false;
        }

        if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
        {
            void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(TargetObject);
            const int64 CurrentValue = NumericProperty->GetSignedIntPropertyValue(ValuePtr);
            if (CurrentValue != DesiredValue)
            {
                NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(DesiredValue));
                return true;
            }
            return false;
        }

        return false;
    }

    bool SetObjectPropertyIfExists(UObject* TargetObject, const FName PropertyName, UObject* NewValue)
    {
        if (!IsValid(TargetObject))
        {
            return false;
        }

        FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(TargetObject->GetClass(), PropertyName);
        if (!ObjectProperty)
        {
            return false;
        }

        const UObject* CurrentValue = ObjectProperty->GetObjectPropertyValue_InContainer(TargetObject);
        if (CurrentValue == NewValue)
        {
            return true;
        }

        ObjectProperty->SetObjectPropertyValue_InContainer(TargetObject, NewValue);
        return true;
    }

    UObject* FindFirstNiagaraLikeComponent(AActor* OwnerActor)
    {
        if (!IsValid(OwnerActor))
        {
            return nullptr;
        }

        TInlineComponentArray<UActorComponent*> Components;
        OwnerActor->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (!IsValid(Component))
            {
                continue;
            }

            const FString ClassName = Component->GetClass()->GetName();
            if (ClassName.Contains(TEXT("NiagaraComponent"), ESearchCase::IgnoreCase))
            {
                return Component;
            }
        }

        return nullptr;
    }

    bool LooksLikeBloodEffectObject(const UObject* Candidate)
    {
        if (!IsValid(Candidate))
        {
            return false;
        }

        if (IsDropSoulObject(Candidate))
        {
            // DropSoul is a core gameplay pickup effect. Never patch/sanitize it through blood cleanup logic.
            return false;
        }

        const FString ClassName = Candidate->GetClass()->GetName();
        const FString ObjectName = Candidate->GetName();
        const FString ObjectPath = Candidate->GetPathName();
        const bool bBloodLikeName =
            ContainsAnyToken(ClassName, { TEXT("Blood"), TEXT("Decal"), TEXT("BloodDrops"), TEXT("Splatter"), TEXT("FightBlood"), TEXT("Stalter") }) ||
            ContainsAnyToken(ObjectName, { TEXT("Blood"), TEXT("Decal"), TEXT("BloodDrops"), TEXT("Splatter"), TEXT("FightBlood"), TEXT("Stalter") }) ||
            ContainsAnyToken(ObjectPath, { TEXT("/Blood_VFX/"), TEXT("/NiagaraBloodFX/"), TEXT("FightBlood"), TEXT("BloodSplatter") });
        return bBloodLikeName;
    }

    bool PatchBloodTraceChannelProperty(UObject* TargetObject, ETraceTypeQuery DesiredTraceType)
    {
        if (IsDropSoulObject(TargetObject))
        {
            return false;
        }

        if (!LooksLikeBloodEffectObject(TargetObject))
        {
            return false;
        }

        const uint8 DesiredValue = static_cast<uint8>(DesiredTraceType);
        bool bPatched = false;
        bPatched |= SetByteOrEnumProperty(TargetObject, TEXT("TraceChannel"), DesiredValue);
        bPatched |= SetByteOrEnumProperty(TargetObject, TEXT("DecalTraceChannel"), DesiredValue);
        bPatched |= SetByteOrEnumProperty(TargetObject, TEXT("SurfaceTraceChannel"), DesiredValue);
        bPatched |= SetByteOrEnumProperty(TargetObject, TEXT("ImpactTraceChannel"), DesiredValue);
        bPatched |= SetByteOrEnumProperty(TargetObject, TEXT("HitTraceChannel"), DesiredValue);
        return bPatched;
    }

    bool IsBloodDecalMaterial(const UMaterialInterface* Material)
    {
        if (!IsValid(Material))
        {
            return false;
        }

        const FString MaterialName = Material->GetName();
        const FString MaterialPath = Material->GetPathName();
        if (ContainsAnyToken(MaterialName, { TEXT("DropSoul"), TEXT("Drop_Soul") }) ||
            ContainsAnyToken(MaterialPath, { TEXT("/Drop_VFX/"), TEXT("DropSoul"), TEXT("Drop_Soul") }))
        {
            return false;
        }
        const bool bLooksLikeBlood =
            ContainsAnyToken(MaterialName, { TEXT("Blood"), TEXT("Decal"), TEXT("Splatter"), TEXT("FightBlood"), TEXT("Stalter") }) ||
            ContainsAnyToken(MaterialPath, { TEXT("/Blood_VFX/"), TEXT("/NiagaraBloodFX/"), TEXT("Blood_Decal"), TEXT("BloodOnFloor"), TEXT("BloodSplatter"), TEXT("FightBlood"), TEXT("M_BloodStalter") });
        return bLooksLikeBlood;
    }

    bool PrimitiveUsesBloodMaterial(const UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return false;
        }

        const UMeshComponent* MeshComp = Cast<UMeshComponent>(Primitive);
        if (!IsValid(MeshComp))
        {
            return false;
        }

        const int32 MaterialCount = MeshComp->GetNumMaterials();
        for (int32 MatIndex = 0; MatIndex < MaterialCount; ++MatIndex)
        {
            if (IsBloodDecalMaterial(MeshComp->GetMaterial(MatIndex)))
            {
                return true;
            }
        }

        return false;
    }

    const UObject* GetObjectPropertyValueIfExists(const UObject* TargetObject, const FName PropertyName)
    {
        if (!IsValid(TargetObject))
        {
            return nullptr;
        }

        const FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(TargetObject->GetClass(), PropertyName);
        if (!ObjectProperty)
        {
            return nullptr;
        }

        return ObjectProperty->GetObjectPropertyValue_InContainer(TargetObject);
    }

    bool IsBloodNiagaraPrimitive(const UPrimitiveComponent* Primitive)
    {
        if (!IsValid(Primitive))
        {
            return false;
        }

        if (IsDropSoulObject(Primitive) || IsDropSoulObject(Primitive->GetOwner()))
        {
            return false;
        }

        const FString PrimitiveClassName = Primitive->GetClass()->GetName();
        if (!PrimitiveClassName.Contains(TEXT("NiagaraComponent"), ESearchCase::IgnoreCase))
        {
            return false;
        }

        const UObject* AssetObject = GetObjectPropertyValueIfExists(Primitive, TEXT("Asset"));
        if (!AssetObject)
        {
            AssetObject = GetObjectPropertyValueIfExists(Primitive, TEXT("NiagaraSystem"));
        }
        if (!AssetObject)
        {
            AssetObject = GetObjectPropertyValueIfExists(Primitive, TEXT("TemplateAsset"));
        }

        const FString AssetPath = AssetObject ? AssetObject->GetPathName() : FString();
        const bool bAssetLooksBlood =
            ContainsAnyToken(AssetPath, { TEXT("/Blood_VFX/"), TEXT("/NiagaraBloodFX/"), TEXT("Blood"), TEXT("FightBlood"), TEXT("Splatter") });
        if (bAssetLooksBlood)
        {
            return true;
        }

        return
            ContainsAnyToken(Primitive->GetPathName(), { TEXT("/Blood_VFX/"), TEXT("/NiagaraBloodFX/") }) ||
            ContainsAnyToken(GetNameSafe(Primitive->GetOwner()), { TEXT("Blood"), TEXT("FightBlood"), TEXT("Splatter") });
    }

    bool PatchBloodPrimitiveCollision(UObject* TargetObject)
    {
        UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(TargetObject);
        if (!IsValid(Primitive))
        {
            return false;
        }
        if (IsDropSoulObject(Primitive) || IsDropSoulObject(Primitive->GetOwner()))
        {
            return false;
        }
        if (!LooksLikeBloodEffectObject(Primitive) &&
            !LooksLikeBloodEffectObject(Primitive->GetOwner()) &&
            !PrimitiveUsesBloodMaterial(Primitive) &&
            !IsBloodNiagaraPrimitive(Primitive))
        {
            return false;
        }

        Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
        Primitive->SetGenerateOverlapEvents(false);
        return true;
    }

    bool HasReliableBloodSurfaceSupport(UWorld* World, const FVector& Location, const FQuat& Orientation, const FCollisionQueryParams& BaseQueryParams)
    {
        if (!World)
        {
            return false;
        }

        auto IsValidSurfaceHit = [](const FHitResult& Hit) -> bool
        {
            if (!Hit.bBlockingHit)
            {
                return false;
            }

            AActor* HitActor = Hit.GetActor();
            UPrimitiveComponent* HitComp = Hit.GetComponent();
            if (IsWaterHit(Hit))
            {
                return false;
            }
            if (ShouldSanitizeTraceBlocker(HitActor, HitComp))
            {
                return false;
            }
            return true;
        };

        FCollisionQueryParams QueryParams(BaseQueryParams);
        QueryParams.bTraceComplex = false;

        const FVector Forward = Orientation.GetForwardVector();
        const FVector Right = Orientation.GetRightVector();
        const FVector Up = Orientation.GetUpVector();
        const FVector Directions[] =
        {
            Forward, -Forward,
            Right, -Right,
            Up, -Up,
            FVector::DownVector,
            FVector::UpVector
        };

        for (const FVector& Direction : Directions)
        {
            FHitResult SurfaceHit;
            const FVector Start = Location + Direction * 8.0f;
            const FVector End = Location - Direction * 120.0f;
            if (World->LineTraceSingleByChannel(SurfaceHit, Start, End, ECC_WorldStatic, QueryParams))
            {
                if (IsValidSurfaceHit(SurfaceHit))
                {
                    return true;
                }
            }
        }

        return false;
    }

    void LogEnemyTraceCollisionSnapshot(const APawn* Enemy)
    {
        if (!IsValid(Enemy))
        {
            return;
        }

        const UCapsuleComponent* Capsule = Enemy->FindComponentByClass<UCapsuleComponent>();
        const USkeletalMeshComponent* MeshComp = Enemy->FindComponentByClass<USkeletalMeshComponent>();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Collision snapshot Enemy='%s' Capsule={%s} Mesh={%s}"),
            *GetNameSafe(Enemy),
            *BuildTraceChannelSnapshot(Capsule),
            *BuildTraceChannelSnapshot(MeshComp));
    }

    void ForceEnemyTraceCollision(APawn* Enemy)
    {
        if (!IsValid(Enemy))
        {
            return;
        }

        if (Enemy->Tags.Contains(TEXT("RaidEnemyDead")))
        {
            return;
        }

        Enemy->SetCanBeDamaged(true);
        Enemy->SetActorEnableCollision(true);

        float ProxyRadius = 42.0f;
        float ProxyHalfHeight = 88.0f;

        if (UCapsuleComponent* Capsule = Enemy->FindComponentByClass<UCapsuleComponent>())
        {
            ProxyRadius = Capsule->GetUnscaledCapsuleRadius();
            ProxyHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
            Capsule->SetCollisionProfileName(TEXT("Pawn"), false);
            Capsule->SetCollisionObjectType(ECC_Pawn);
            Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
            Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
            Capsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
            Capsule->SetCollisionResponseToChannel(ECC_Destructible, ECR_Block);
            Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
            ForceAllGameTraceChannelsToBlock(Capsule);
        }

        if (USkeletalMeshComponent* MeshComp = Enemy->FindComponentByClass<USkeletalMeshComponent>())
        {
            // Keep one query body as WorldDynamic because many weapon blueprints use Object traces.
            MeshComp->SetCollisionObjectType(ECC_WorldDynamic);
            if (MeshComp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
            {
                MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            }
            MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
            MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
            ForceAllGameTraceChannelsToBlock(MeshComp);
        }

        // Redundant trace proxies to survive collision profile resets from external blueprints/plugins.
        EnsureTraceProxyCapsule(Enemy, TEXT("RaidHitProxy_WorldDynamic"), ECC_WorldDynamic, ProxyRadius, ProxyHalfHeight);
        EnsureTraceProxyCapsule(Enemy, TEXT("RaidHitProxy_PhysicsBody"), ECC_PhysicsBody, ProxyRadius, ProxyHalfHeight);
    }

    int32 GetRoomTypePriority(const FString& RoomType)
    {
        if (RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase)) return 100;
        if (RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase)) return 80;
        if (RoomType.Equals(TEXT("Loot"), ESearchCase::IgnoreCase)) return 60;
        if (RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) return 40;
        return 10;
    }

    void BuildPresetCandidates(const FLevelNodeRow& Row, TArray<FName>& OutCandidates)
    {
        OutCandidates.Reset();

        const FString PresetRaw = Row.EnemyPreset.TrimStartAndEnd();
        const bool bRequestedNone = PresetRaw.IsEmpty() || PresetRaw.Equals(TEXT("None"), ESearchCase::IgnoreCase);

        if (!bRequestedNone)
        {
            OutCandidates.AddUnique(FName(*PresetRaw));
        }

        const FString RoleLower = Row.RoomRole.ToLower();
        const FString TagLower = Row.NodeTags.ToLower();
        const FString ThemeLower = Row.Theme.ToLower();
        const FString EnvLower = Row.EnvType.ToLower();
        const FString MetaLower = (RoleLower + TEXT(" ") + TagLower + TEXT(" ") + ThemeLower + TEXT(" ") + EnvLower);

        if (Row.RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
        {
            OutCandidates.AddUnique(TEXT("Boss"));
            OutCandidates.AddUnique(TEXT("BossGuard"));
            OutCandidates.AddUnique(TEXT("BossElite"));
            OutCandidates.AddUnique(TEXT("Raider"));
            OutCandidates.AddUnique(TEXT("Scavenger"));
        }
        else if (MetaLower.Contains(TEXT("sniper")))
        {
            OutCandidates.AddUnique(TEXT("Sniper"));
            OutCandidates.AddUnique(TEXT("Raider"));
            OutCandidates.AddUnique(TEXT("Scavenger"));
        }
        else if (MetaLower.Contains(TEXT("assault")) || MetaLower.Contains(TEXT("breach")))
        {
            OutCandidates.AddUnique(TEXT("Raider"));
            OutCandidates.AddUnique(TEXT("Scavenger"));
            OutCandidates.AddUnique(TEXT("Default"));
        }
        else if (MetaLower.Contains(TEXT("stealth")) || MetaLower.Contains(TEXT("loot")) || MetaLower.Contains(TEXT("scavenge")))
        {
            OutCandidates.AddUnique(TEXT("Scavenger"));
            OutCandidates.AddUnique(TEXT("Raider"));
            OutCandidates.AddUnique(TEXT("Default"));
        }
        else if (Row.EnvType.Equals(TEXT("Jungle"), ESearchCase::IgnoreCase))
        {
            OutCandidates.AddUnique(TEXT("Scavenger"));
            OutCandidates.AddUnique(TEXT("Raider"));
        }
        else if (Row.EnvType.Equals(TEXT("Urban"), ESearchCase::IgnoreCase))
        {
            OutCandidates.AddUnique(TEXT("Raider"));
            OutCandidates.AddUnique(TEXT("Scavenger"));
            OutCandidates.AddUnique(TEXT("Sniper"));
        }
        else
        {
            OutCandidates.AddUnique(TEXT("Scavenger"));
            OutCandidates.AddUnique(TEXT("Raider"));
        }

        OutCandidates.AddUnique(TEXT("Default"));
    }

    bool IsWaterHit(const FHitResult& Hit)
    {
        if (AActor* HitActor = Hit.GetActor())
        {
            if (HitActor->ActorHasTag(TEXT("Water"))) return true;
            const FString ActorClass = HitActor->GetClass()->GetName();
            if (ActorClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase)) return true;
        }

        if (UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HitComp->ComponentHasTag(TEXT("Water"))) return true;
            const FString CompClass = HitComp->GetClass()->GetName();
            if (CompClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase)) return true;
        }

        return false;
    }

    bool IsLandscapeLikeHit(const FHitResult& Hit)
    {
        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        if (!HitActor && !HitComp) return false;

        const FString ActorClass = HitActor ? HitActor->GetClass()->GetName() : FString();
        const FString CompClass = HitComp ? HitComp->GetClass()->GetName() : FString();
        return
            ActorClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Landscape"), ESearchCase::IgnoreCase) ||
            ActorClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase) ||
            CompClass.Contains(TEXT("Terrain"), ESearchCase::IgnoreCase);
    }

    bool IsRoomComponentHitWithTag(const FHitResult& Hit, const ARaidRoomActor* Room, const FName& Tag)
    {
        if (!Room || Hit.GetActor() != Room) return false;
        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        return HitComp && HitComp->ComponentTags.Contains(Tag);
    }

    bool IsActorOwnedOrAttachedToRoom(const AActor* CandidateActor, const ARaidRoomActor* Room)
    {
        if (!CandidateActor || !Room)
        {
            return false;
        }

        if (CandidateActor == Room || CandidateActor->GetOwner() == Room)
        {
            return true;
        }

        const AActor* AttachParent = CandidateActor->GetAttachParentActor();
        if (AttachParent == Room || (AttachParent && AttachParent->GetOwner() == Room))
        {
            return true;
        }

        return false;
    }

    bool IsRoomFloorHit(const FHitResult& Hit, const ARaidRoomActor* Room)
    {
        return IsRoomComponentHitWithTag(Hit, Room, TEXT("MeshType_0"));
    }

    bool IsRoomObstacleHit(const FHitResult& Hit, const ARaidRoomActor* Room)
    {
        const AActor* HitActor = Hit.GetActor();
        if (!Room || !HitActor) return false;

        if (HitActor != Room && !IsActorOwnedOrAttachedToRoom(HitActor, Room))
        {
            return false;
        }

        const UPrimitiveComponent* HitComp = Hit.GetComponent();
        const bool bCompTaggedObstacle =
            HitComp &&
            (
                HitComp->ComponentTags.Contains(TEXT("MeshType_1")) ||
                HitComp->ComponentTags.Contains(TEXT("MeshType_2")) ||
                HitComp->ComponentTags.Contains(TEXT("MeshType_3")) ||
                HitComp->ComponentTags.Contains(TEXT("MeshType_6")) ||
                HitComp->ComponentTags.Contains(TEXT("MeshType_7")) ||
                HitComp->ComponentTags.Contains(TEXT("MeshType_8")) ||
                HitComp->ComponentTags.Contains(TEXT("ObstacleBlueprint"))
            );

        const bool bActorTaggedObstacle =
            HitActor->ActorHasTag(TEXT("MeshType_1")) ||
            HitActor->ActorHasTag(TEXT("MeshType_2")) ||
            HitActor->ActorHasTag(TEXT("MeshType_3")) ||
            HitActor->ActorHasTag(TEXT("MeshType_6")) ||
            HitActor->ActorHasTag(TEXT("MeshType_7")) ||
            HitActor->ActorHasTag(TEXT("MeshType_8")) ||
            HitActor->ActorHasTag(TEXT("ObstacleBlueprint")) ||
            HitActor->ActorHasTag(TEXT("RaidDoorBlocker"));

        return bCompTaggedObstacle || bActorTaggedObstacle;
    }

    bool TryResolveAIGroundHit(UWorld* World, ARaidRoomActor* Room, const FVector& XYLocation, FHitResult& OutHit)
    {
        if (!World) return false;

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidAIGroundResolve), false);
        QueryParams.bTraceComplex = false;
        QueryParams.bReturnPhysicalMaterial = false;

        TArray<FHitResult> Hits;
        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

        const FVector Start(XYLocation.X, XYLocation.Y, 5000.0f);
        const FVector End(XYLocation.X, XYLocation.Y, -5000.0f);
        bool bHasHit = World->LineTraceMultiByObjectType(Hits, Start, End, ObjQuery, QueryParams);
        if (!bHasHit || Hits.Num() == 0)
        {
            Hits.Reset();
            bHasHit = World->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams);
        }

        if (!bHasHit || Hits.Num() == 0)
        {
            return false;
        }

        const FHitResult* BestRoomFloor = nullptr;
        const FHitResult* BestLandscape = nullptr;
        const FHitResult* BestGeneral = nullptr;

        for (const FHitResult& Hit : Hits)
        {
            if (!Hit.bBlockingHit) continue;
            if (const AActor* HitActor = Hit.GetActor())
            {
                if (HitActor->IsA<APawn>())
                {
                    continue;
                }
            }
            if (IsWaterHit(Hit)) continue;
            if (IsRoomObstacleHit(Hit, Room)) continue;

            if (IsRoomFloorHit(Hit, Room))
            {
                if (!BestRoomFloor || Hit.Distance < BestRoomFloor->Distance)
                {
                    BestRoomFloor = &Hit;
                }
                continue;
            }

            if (IsLandscapeLikeHit(Hit))
            {
                if (!BestLandscape || Hit.Distance < BestLandscape->Distance)
                {
                    BestLandscape = &Hit;
                }
            }

            if (!BestGeneral || Hit.Distance < BestGeneral->Distance)
            {
                BestGeneral = &Hit;
            }
        }

        const FHitResult* Selected = BestRoomFloor ? BestRoomFloor : (BestLandscape ? BestLandscape : BestGeneral);
        if (!Selected)
        {
            return false;
        }

        OutHit = *Selected;
        return true;
    }

    bool IsNearRoomObstacle(UWorld* World, const ARaidRoomActor* Room, const FVector& Location, float Radius)
    {
        if (!World || !Room) return false;

        FCollisionObjectQueryParams ObjQuery;
        ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjQuery.AddObjectTypesToQuery(ECC_WorldDynamic);

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidAIObstacleOverlap), false);
        QueryParams.bTraceComplex = false;

        TArray<FOverlapResult> Overlaps;
        if (!World->OverlapMultiByObjectType(
            Overlaps,
            Location,
            FQuat::Identity,
            ObjQuery,
            FCollisionShape::MakeSphere(Radius),
            QueryParams))
        {
            return false;
        }

        for (const FOverlapResult& Overlap : Overlaps)
        {
            const UPrimitiveComponent* Comp = Overlap.Component.Get();
            const AActor* Owner = Overlap.GetActor();
            if (!Comp) continue;

            const bool bIsRoomOwnedComponent = (Owner == Room);
            const bool bIsOwnedOrAttached = IsActorOwnedOrAttachedToRoom(Owner, Room);
            const bool bIsRoomOwnedDoorBlocker = Owner && Owner->ActorHasTag(TEXT("RaidDoorBlocker")) && bIsOwnedOrAttached;
            if (!bIsRoomOwnedComponent && !bIsOwnedOrAttached && !bIsRoomOwnedDoorBlocker) continue;
            if (Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;
            if (Comp->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;

            const bool bComponentTaggedObstacle =
                Comp->ComponentTags.Contains(TEXT("MeshType_1")) ||
                Comp->ComponentTags.Contains(TEXT("MeshType_2")) ||
                Comp->ComponentTags.Contains(TEXT("MeshType_3")) ||
                Comp->ComponentTags.Contains(TEXT("MeshType_6")) ||
                Comp->ComponentTags.Contains(TEXT("MeshType_7")) ||
                Comp->ComponentTags.Contains(TEXT("MeshType_8")) ||
                Comp->ComponentTags.Contains(TEXT("ObstacleBlueprint"));

            const bool bActorTaggedObstacle =
                Owner &&
                (
                    Owner->ActorHasTag(TEXT("MeshType_1")) ||
                    Owner->ActorHasTag(TEXT("MeshType_2")) ||
                    Owner->ActorHasTag(TEXT("MeshType_3")) ||
                    Owner->ActorHasTag(TEXT("MeshType_6")) ||
                    Owner->ActorHasTag(TEXT("MeshType_7")) ||
                    Owner->ActorHasTag(TEXT("MeshType_8")) ||
                    Owner->ActorHasTag(TEXT("ObstacleBlueprint")) ||
                    Owner->ActorHasTag(TEXT("RaidDoorBlocker"))
                );

            if (bComponentTaggedObstacle ||
                bActorTaggedObstacle ||
                bIsRoomOwnedDoorBlocker)
            {
                return true;
            }
        }

        return false;
    }

    void ResolvePawnCapsuleSize(TSubclassOf<APawn> EnemyClass, float& OutRadius, float& OutHalfHeight)
    {
        OutRadius = 42.0f;
        OutHalfHeight = 88.0f;

        if (!EnemyClass) return;
        const APawn* DefaultPawn = EnemyClass->GetDefaultObject<APawn>();
        const ACharacter* DefaultCharacter = Cast<ACharacter>(DefaultPawn);
        if (!DefaultCharacter) return;

        const UCapsuleComponent* Capsule = DefaultCharacter->GetCapsuleComponent();
        if (!Capsule) return;

        OutRadius = FMath::Max(20.0f, Capsule->GetScaledCapsuleRadius());
        OutHalfHeight = FMath::Max(40.0f, Capsule->GetScaledCapsuleHalfHeight());
    }

    bool IsCapsuleBlockedForPawn(
        UWorld* World,
        const FVector& PawnActorLocation,
        float CapsuleRadius,
        float CapsuleHalfHeight,
        const AActor* ActorToIgnore = nullptr)
    {
        if (!World) return true;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidAICapsuleCheck), false);
        QueryParams.bTraceComplex = false;
        if (IsValid(ActorToIgnore))
        {
            QueryParams.AddIgnoredActor(ActorToIgnore);
        }
        return World->OverlapBlockingTestByProfile(
            PawnActorLocation,
            FQuat::Identity,
            TEXT("Pawn"),
            FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
            QueryParams);
    }

    void ResolvePawnInstanceCapsuleSize(const APawn* Pawn, float& OutRadius, float& OutHalfHeight)
    {
        OutRadius = FMath::Max(20.0f, OutRadius);
        OutHalfHeight = FMath::Max(40.0f, OutHalfHeight);

        if (!IsValid(Pawn))
        {
            return;
        }

        if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
        {
            OutRadius = FMath::Max(20.0f, Capsule->GetScaledCapsuleRadius());
            OutHalfHeight = FMath::Max(40.0f, Capsule->GetScaledCapsuleHalfHeight());
            return;
        }

        if (const ACharacter* Character = Cast<ACharacter>(Pawn))
        {
            if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
            {
                OutRadius = FMath::Max(20.0f, Capsule->GetScaledCapsuleRadius());
                OutHalfHeight = FMath::Max(40.0f, Capsule->GetScaledCapsuleHalfHeight());
            }
        }
    }

    bool TryResolveSafeAIPawnSpawnLocation(
        UWorld* World,
        ARaidRoomActor* Room,
        UNavigationSystemV1* NavSys,
        const FVector& XYLocation,
        float CapsuleRadius,
        float CapsuleHalfHeight,
        FVector& OutActorLocation)
    {
        if (!World || !Room) return false;

        FHitResult GroundHit;
        if (!TryResolveAIGroundHit(World, Room, XYLocation, GroundHit))
        {
            return false;
        }

        FVector CandidateGroundLoc = GroundHit.ImpactPoint;
        FVector CandidateGroundNormal = GroundHit.ImpactNormal;

        if (NavSys)
        {
            FNavLocation NavLoc;
            if (NavSys->ProjectPointToNavigation(CandidateGroundLoc + FVector(0.0f, 0.0f, 50.0f), NavLoc, FVector(260.0f, 260.0f, 260.0f)))
            {
                FHitResult NavGroundHit;
                if (TryResolveAIGroundHit(World, Room, NavLoc.Location, NavGroundHit))
                {
                    CandidateGroundLoc = NavGroundHit.ImpactPoint;
                    CandidateGroundNormal = NavGroundHit.ImpactNormal;
                }
                else
                {
                    CandidateGroundLoc = NavLoc.Location;
                }
            }
        }

        // Avoid steep normals that usually mean wall/side-surface hits.
        if (CandidateGroundNormal.Z < 0.55f)
        {
            return false;
        }

        const FVector CandidateActorLoc = CandidateGroundLoc + FVector(0.0f, 0.0f, CapsuleHalfHeight + 6.0f);
        if (IsNearRoomObstacle(World, Room, CandidateActorLoc, CapsuleRadius + 70.0f))
        {
            return false;
        }
        if (IsCapsuleBlockedForPawn(World, CandidateActorLoc, CapsuleRadius, CapsuleHalfHeight))
        {
            return false;
        }

        OutActorLocation = CandidateActorLoc;
        return true;
    }

    bool TryResolveNearbyFallbackSpawnLocation(
        UWorld* World,
        ARaidRoomActor* Room,
        UNavigationSystemV1* NavSys,
        const FVector& SeedActorLocation,
        float CapsuleRadius,
        float CapsuleHalfHeight,
        FRandomStream& Stream,
        FVector& OutActorLocation)
    {
        static const FVector2D DirectionSamples[] =
        {
            FVector2D(1.0f, 0.0f),
            FVector2D(-1.0f, 0.0f),
            FVector2D(0.0f, 1.0f),
            FVector2D(0.0f, -1.0f),
            FVector2D(0.7071f, 0.7071f),
            FVector2D(0.7071f, -0.7071f),
            FVector2D(-0.7071f, 0.7071f),
            FVector2D(-0.7071f, -0.7071f)
        };
        static const float RingRadii[] = { 140.0f, 260.0f, 380.0f };

        const int32 DirectionCount = static_cast<int32>(sizeof(DirectionSamples) / sizeof(DirectionSamples[0]));
        const int32 RingCount = static_cast<int32>(sizeof(RingRadii) / sizeof(RingRadii[0]));
        if (DirectionCount <= 0 || RingCount <= 0)
        {
            return false;
        }

        for (int32 RingIndex = 0; RingIndex < RingCount; ++RingIndex)
        {
            const float RingDistance = RingRadii[RingIndex] + CapsuleRadius;
            const int32 StartDirection = Stream.RandRange(0, DirectionCount - 1);
            for (int32 OffsetIdx = 0; OffsetIdx < DirectionCount; ++OffsetIdx)
            {
                const FVector2D Dir = DirectionSamples[(StartDirection + OffsetIdx) % DirectionCount];
                const FVector XYCandidate(
                    SeedActorLocation.X + Dir.X * RingDistance + Stream.FRandRange(-22.0f, 22.0f),
                    SeedActorLocation.Y + Dir.Y * RingDistance + Stream.FRandRange(-22.0f, 22.0f),
                    SeedActorLocation.Z);

                if (TryResolveSafeAIPawnSpawnLocation(World, Room, NavSys, XYCandidate, CapsuleRadius, CapsuleHalfHeight, OutActorLocation))
                {
                    return true;
                }
            }
        }

        return false;
    }
}

void URaidCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SyncBandageHealFromLootDataTable();
}

void URaidCombatSubsystem::RegisterRoom(ARaidRoomActor* Room)
{
    if (!Room) return;
    RoomById.Add(Room->GetNodeId(), Room);
    PrimeRuntimeAssets(Room->GetChapterConfig());
    if (Room->GetNodeRow().RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
    {
        StartRoomId = Room->GetNodeId();
        bStartFlowInitialized = false;
        bPlayerSpawnedInsideStartRoom = false;
        bStartPendingClearOnExit = false;
    }
    StartTraceCollisionEnforcer();
}

void URaidCombatSubsystem::RegisterRoomAsPOI(ARaidRoomActor* InRoom)
{
    if (!InRoom) return;
    RegisterRoom(InRoom);

    const FString Type = InRoom->GetNodeRow().RoomType;
    if (Type.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
    {
        return;
    }
    AddPOI(InRoom->GetActorLocation(), FName(*Type));
}

bool URaidCombatSubsystem::IsPawnDeadLike(const APawn* EnemyPawn) const
{
    if (!IsValid(EnemyPawn))
    {
        return true;
    }

    if (EnemyPawn->IsActorBeingDestroyed())
    {
        return true;
    }

    if (EnemyPawn->Tags.Contains(TEXT("RaidEnemyDead")) ||
        EnemyPawn->Tags.Contains(TEXT("Dead")) ||
        EnemyPawn->Tags.Contains(TEXT("Corpse")))
    {
        return true;
    }

    bool bBoolValue = false;
    if ((ReadBoolPropertyValue(EnemyPawn, TEXT("IsDeathC"), bBoolValue) && bBoolValue) ||
        (ReadBoolPropertyValue(EnemyPawn, TEXT("bIsDead"), bBoolValue) && bBoolValue) ||
        (ReadBoolPropertyValue(EnemyPawn, TEXT("IsDead"), bBoolValue) && bBoolValue) ||
        (ReadBoolPropertyValue(EnemyPawn, TEXT("Dead"), bBoolValue) && bBoolValue) ||
        (ReadBoolPropertyValue(EnemyPawn, TEXT("bDead"), bBoolValue) && bBoolValue) ||
        (ReadBoolPropertyValue(EnemyPawn, TEXT("bDeath"), bBoolValue) && bBoolValue))
    {
        return true;
    }

    float HealthValue = 0.0f;
    if ((ReadFloatPropertyValue(EnemyPawn, TEXT("CurrentHealthPoints"), HealthValue) && HealthValue <= KINDA_SMALL_NUMBER) ||
        (ReadFloatPropertyValue(EnemyPawn, TEXT("CurrentHealth"), HealthValue) && HealthValue <= KINDA_SMALL_NUMBER) ||
        (ReadFloatPropertyValue(EnemyPawn, TEXT("Health"), HealthValue) && HealthValue <= KINDA_SMALL_NUMBER))
    {
        return true;
    }

    return false;
}

bool URaidCombatSubsystem::IsPlayerLikelyMakingGunfireNoise(const APawn* PlayerPawn) const
{
    if (!IsValid(PlayerPawn))
    {
        return false;
    }

    static const FName FireLikeFlags[] =
    {
        TEXT("bIsFiring"),
        TEXT("IsFiring"),
        TEXT("bWantsToFire"),
        TEXT("WantsToFire"),
        TEXT("bShotPressed"),
        TEXT("ShotPressed"),
        TEXT("bShootPressed"),
        TEXT("ShootPressed"),
        TEXT("bFirePressed"),
        TEXT("FirePressed"),
        TEXT("bLMBDown"),
        TEXT("LMBDown")
    };

    bool bValue = false;
    for (const FName PropertyName : FireLikeFlags)
    {
        if (ReadBoolPropertyValue(PlayerPawn, PropertyName, bValue) && bValue)
        {
            return true;
        }
    }

    TInlineComponentArray<UActorComponent*> Components;
    PlayerPawn->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (!IsValid(Component))
        {
            continue;
        }

        const FString ClassName = Component->GetClass()->GetName();
        const FString CompName = Component->GetName();
        const bool bLooksWeaponComponent =
            ContainsAnyToken(ClassName, { TEXT("Weapon"), TEXT("Gun"), TEXT("Rifle"), TEXT("Pistol"), TEXT("Firearm") }) ||
            ContainsAnyToken(CompName, { TEXT("Weapon"), TEXT("Gun"), TEXT("Rifle"), TEXT("Pistol"), TEXT("Firearm") });
        if (!bLooksWeaponComponent)
        {
            continue;
        }

        for (const FName PropertyName : FireLikeFlags)
        {
            if (ReadBoolPropertyValue(Component, PropertyName, bValue) && bValue)
            {
                return true;
            }
        }
    }

    return false;
}

bool URaidCombatSubsystem::IsRoomCombatAlertActive(int32 RoomId, double NowSeconds) const
{
    if (RoomId < 0)
    {
        return false;
    }

    const double AlertUntil = RoomCombatAlertUntilByRoomId.FindRef(RoomId);
    return AlertUntil > NowSeconds;
}

void URaidCombatSubsystem::RaiseRoomCombatAlert(int32 RoomId, double NowSeconds)
{
    if (RoomId < 0)
    {
        return;
    }

    const double HoldSeconds = FMath::Max(1.0, static_cast<double>(RoomCombatAlertHoldSeconds));
    const double NewAlertUntil = NowSeconds + HoldSeconds;
    double& ExistingAlertUntil = RoomCombatAlertUntilByRoomId.FindOrAdd(RoomId);
    ExistingAlertUntil = FMath::Max(ExistingAlertUntil, NewAlertUntil);
}

double URaidCombatSubsystem::ResolveEnemySearchActivationTime(int32 RoomId, const FVector& EnemyLocation, double NowSeconds, const APawn* PlayerPawn) const
{
    double DelaySeconds = FMath::Max(0.0f, EnemySearchStartDelay);
    const double ImmediateDelay = FMath::Max(0.0f, EnemySearchImmediateStartDelay);

    if (IsRoomCombatAlertActive(RoomId, NowSeconds))
    {
        DelaySeconds = FMath::Min(DelaySeconds, ImmediateDelay);
    }

    if (IsValid(PlayerPawn))
    {
        const float DistToPlayer = FVector::Dist2D(EnemyLocation, PlayerPawn->GetActorLocation());
        const float NearDistance = FMath::Max(800.0f, EnemyAutoChaseDistance * 1.15f);
        if (DistToPlayer <= NearDistance)
        {
            DelaySeconds = FMath::Min(DelaySeconds, ImmediateDelay);
        }
    }

    return NowSeconds + DelaySeconds;
}

double URaidCombatSubsystem::ResolveControllerSpawnDelaySeconds(const APawn* SpawnedEnemy, int32 RoomId, int32 SpawnOrderIndex) const
{
    const float DelayStep = FMath::Max(0.0f, EnemyControllerSpawnDelayStep);
    float DelaySeconds = DelayStep * static_cast<float>(FMath::Clamp(SpawnOrderIndex, 0, 24));
    if (DelaySeconds <= KINDA_SMALL_NUMBER)
    {
        return 0.0;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return DelaySeconds;
    }

    const double NowSeconds = World->GetTimeSeconds();
    const bool bRoomAlert = IsRoomCombatAlertActive(RoomId, NowSeconds);

    bool bNearPlayer = false;
    if (IsValid(SpawnedEnemy))
    {
        if (const APawn* PlayerPawn = GetPrimaryPlayerPawn())
        {
            const float DistToPlayer = FVector::Dist2D(SpawnedEnemy->GetActorLocation(), PlayerPawn->GetActorLocation());
            bNearPlayer = DistToPlayer <= FMath::Max(300.0f, EnemyControllerSpawnDelayNearPlayerDistance);
        }
    }

    if (bRoomAlert || bNearPlayer)
    {
        const float Scale = FMath::Clamp(EnemyControllerSpawnDelayScaleWhenAlerted, 0.0f, 1.0f);
        DelaySeconds *= Scale;
    }

    if (SpawnOrderIndex <= 0 && (bRoomAlert || bNearPlayer))
    {
        DelaySeconds = 0.0f;
    }

    return FMath::Max(0.0f, DelaySeconds);
}

void URaidCombatSubsystem::DisableDeadEnemyDamageSources(APawn* EnemyPawn, const TCHAR* Reason)
{
    if (!IsValid(EnemyPawn))
    {
        return;
    }

    EnemyPawn->Tags.AddUnique(TEXT("RaidEnemyDead"));
    EnemyPawn->SetCanBeDamaged(false);

    TInlineComponentArray<UActorComponent*> Components;
    EnemyPawn->GetComponents(Components);

    int32 DisabledCount = 0;
    for (UActorComponent* Component : Components)
    {
        if (!IsValid(Component))
        {
            continue;
        }

        const FString ClassName = Component->GetClass()->GetName();
        const FString ComponentName = Component->GetName();
        const bool bLikelyDamageComponent =
            ContainsAnyToken(ClassName, { TEXT("Hitbox"), TEXT("Attack"), TEXT("Melee"), TEXT("Damage"), TEXT("Bite"), TEXT("ZombieAttacks"), TEXT("WeaponTrace") }) ||
            ContainsAnyToken(ComponentName, { TEXT("Hitbox"), TEXT("Attack"), TEXT("Melee"), TEXT("Damage"), TEXT("Bite"), TEXT("ZombieAttacks"), TEXT("WeaponTrace") }) ||
            Component->ComponentHasTag(TEXT("Damage")) ||
            Component->ComponentHasTag(TEXT("AttackHitbox"));

        if (bLikelyDamageComponent)
        {
            SetBoolPropertyIfExists(Component, TEXT("bAttackStarted"), false);
            SetBoolPropertyIfExists(Component, TEXT("bCanAttack"), false);
            SetBoolPropertyIfExists(Component, TEXT("CanAttack"), false);
            SetBoolPropertyIfExists(Component, TEXT("bCanDamage"), false);
            SetBoolPropertyIfExists(Component, TEXT("CanDamage"), false);

            if (UFunction* StopAttackTimerFunc = Component->FindFunction(TEXT("CorrectlySetAttackTimer")))
            {
                struct FStopAttackTimerParams
                {
                    bool StartAttacking = false;
                    float InDuration = 0.0f;
                    bool ReturnValue = false;
                };

                FStopAttackTimerParams Params;
                Params.StartAttacking = false;
                Params.InDuration = 0.0f;
                Component->ProcessEvent(StopAttackTimerFunc, &Params);
            }

            Component->SetComponentTickEnabled(false);
            Component->Deactivate();
            Component->SetActive(false, false);
            ++DisabledCount;
        }

        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
        {
            const bool bShouldDisableCollision =
                bLikelyDamageComponent ||
                Primitive->ComponentHasTag(TEXT("Damage")) ||
                Primitive->ComponentHasTag(TEXT("AttackHitbox"));
            if (bShouldDisableCollision)
            {
                Primitive->SetGenerateOverlapEvents(false);
                Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Primitive->SetCanEverAffectNavigation(false);
            }
        }
    }

    if (bEnableCombatPerfLogs && DisabledCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Disabled %d dead-enemy damage component(s) for '%s' Reason=%s"),
            DisabledCount,
            *GetNameSafe(EnemyPawn),
            Reason ? Reason : TEXT("Unknown"));
    }
}

void URaidCombatSubsystem::UpdateEnemySearchBehavior(APawn* EnemyPawn, int32 RoomId, double NowSeconds, APawn* PlayerPawn)
{
    if (!bEnableRoomEnemySearchBehavior || !IsValid(EnemyPawn))
    {
        return;
    }

    AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController());
    if (!IsValid(AIController))
    {
        return;
    }

    const TWeakObjectPtr<APawn> WeakPawn(EnemyPawn);
    const double ActivationTime = EnemySearchActivationTimeByPawn.FindRef(WeakPawn);
    if (ActivationTime > 0.0 && NowSeconds < ActivationTime)
    {
        return;
    }

    const double NextOrderTime = EnemySearchNextOrderTimeByPawn.FindRef(WeakPawn);
    if (NextOrderTime > NowSeconds)
    {
        return;
    }

    const bool bHasPlayer = IsValid(PlayerPawn);
    const float DistanceToPlayer = bHasPlayer ? FVector::Dist2D(EnemyPawn->GetActorLocation(), PlayerPawn->GetActorLocation()) : TNumericLimits<float>::Max();
    const bool bGunfireDetected = bHasPlayer && IsPlayerLikelyMakingGunfireNoise(PlayerPawn);
    const bool bRoomAlertActive = IsRoomCombatAlertActive(RoomId, NowSeconds);
    const float ReactiveChaseDistance = FMath::Max(700.0f, EnemyGunshotHearingDistance * 1.10f);
    const bool bShouldChasePlayer =
        bHasPlayer &&
        (((bGunfireDetected || bRoomAlertActive) && DistanceToPlayer <= ReactiveChaseDistance) ||
            DistanceToPlayer <= FMath::Max(500.0f, EnemyAutoChaseDistance));

    const FString ControllerClassName = AIController->GetClass()->GetName();
    const FString ControllerClassPath = AIController->GetClass()->GetPathName();
    const bool bIsStateTreeLikeController =
        ContainsAnyToken(ControllerClassName, { TEXT("HumanEnemyController"), TEXT("StateTree"), TEXT("AI_HumanEnemyController") }) ||
        ContainsAnyToken(ControllerClassPath, { TEXT("Human_AI_Logic"), TEXT("StateTree"), TEXT("STT_HumanAI") });

    const float AcceptanceRadius = FMath::Max(50.0f, EnemySearchAcceptanceRadius);
    double NextScheduleTime = NowSeconds + FMath::Max(0.20f, EnemySearchPatrolInterval);

    if (bDisableCustomSearchForStateTreeControllers && bIsStateTreeLikeController)
    {
        // Keep StateTree as owner of detailed behavior, but add a minimal reactive assist
        // so enemies never stay idle during obvious gunfire/close-threat moments.
        if (bHasPlayer && bEnableStateTreeReactiveSearchAssist && (bShouldChasePlayer || bRoomAlertActive))
        {
            AIController->SetFocus(PlayerPawn);
            AIController->MoveToActor(PlayerPawn, AcceptanceRadius, true, true, true, nullptr, true);
            const double AssistRepathInterval = FMath::Max(0.10f, StateTreeReactiveSearchRepathInterval);
            EnemySearchNextOrderTimeByPawn.Add(WeakPawn, NowSeconds + AssistRepathInterval);
        }
        return;
    }

    if (bShouldChasePlayer)
    {
        AIController->SetFocus(PlayerPawn);
        AIController->MoveToActor(PlayerPawn, AcceptanceRadius, true, true, true, nullptr, true);
        NextScheduleTime = NowSeconds + (bGunfireDetected ? FMath::Max(0.12f, EnemyGunshotRepathInterval) : FMath::Max(0.20f, EnemySearchPatrolInterval * 0.75f));
    }
    else
    {
        AIController->ClearFocus(EAIFocusPriority::Gameplay);

        FVector PatrolTarget = EnemyPawn->GetActorLocation();
        bool bHasPatrolTarget = false;

        if (RoomId < 0 && bHasPlayer)
        {
            const FVector EnemyLoc = EnemyPawn->GetActorLocation();
            const FVector PlayerLoc = PlayerPawn->GetActorLocation();
            const float DirectChaseDist = FMath::Max(120.0f, WaveDirectChaseDistance);
            if (DistanceToPlayer <= DirectChaseDist)
            {
                PatrolTarget = PlayerLoc;
                bHasPatrolTarget = true;
            }
            else
            {
                FVector DirFromPlayer = (EnemyLoc - PlayerLoc).GetSafeNormal2D();
                if (DirFromPlayer.IsNearlyZero())
                {
                    DirFromPlayer = FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f).GetSafeNormal2D();
                }
                const FVector Lateral(-DirFromPlayer.Y, DirFromPlayer.X, 0.0f);
                const float RingDistance = FMath::Clamp(
                    DistanceToPlayer * FMath::Clamp(WaveConvergeDistanceScale, 0.1f, 0.95f),
                    FMath::Max(100.0f, WaveConvergeMinRingDistance),
                    FMath::Max(WaveConvergeMinRingDistance, WaveConvergeMaxRingDistance));
                const float Jitter = FMath::FRandRange(-WaveConvergeLateralJitter, WaveConvergeLateralJitter);
                const FVector ApproachTarget = PlayerLoc + DirFromPlayer * RingDistance + Lateral * Jitter;

                if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
                {
                    FNavLocation RandomNavLoc;
                    if (NavSys->GetRandomReachablePointInRadius(ApproachTarget, 320.0f, RandomNavLoc))
                    {
                        PatrolTarget = RandomNavLoc.Location;
                        bHasPatrolTarget = true;
                    }
                }

                if (!bHasPatrolTarget)
                {
                    PatrolTarget = ApproachTarget;
                    bHasPatrolTarget = true;
                }
            }
        }
        else if (TObjectPtr<ARaidRoomActor>* RoomPtr = RoomById.Find(RoomId))
        {
            ARaidRoomActor* Room = RoomPtr->Get();
            if (IsValid(Room))
            {
                const FVector RoomCenter = Room->GetActorLocation();
                const FVector RoomExtent = Room->GetRoomExtent();
                const float PatrolRadius = FMath::Max(280.0f, RoomExtent.Size2D() * FMath::Max(0.10f, EnemySearchPatrolRadiusScale));

                if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
                {
                    FNavLocation RandomNavLoc;
                    if (NavSys->GetRandomReachablePointInRadius(RoomCenter, PatrolRadius, RandomNavLoc))
                    {
                        PatrolTarget = RandomNavLoc.Location;
                        bHasPatrolTarget = true;
                    }
                }
            }
        }

        if (!bHasPatrolTarget && bHasPlayer)
        {
            PatrolTarget = PlayerPawn->GetActorLocation();
            bHasPatrolTarget = true;
        }

        if (bHasPatrolTarget)
        {
            AIController->MoveToLocation(PatrolTarget, AcceptanceRadius, true, true, true, false, nullptr, true);
        }
    }

    EnemySearchNextOrderTimeByPawn.Add(WeakPawn, NextScheduleTime);
}

void URaidCombatSubsystem::OnEnemySpawned(APawn* Enemy, int32 RoomId)
{
    if (!Enemy) return;
    EnemyToRoomMap.Add(Enemy, RoomId);
    const TWeakObjectPtr<APawn> WeakEnemy(Enemy);
    const FVector SpawnLoc = Enemy->GetActorLocation();
    const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const APawn* PlayerPawn = GetPrimaryPlayerPawn();
    const double RecoveryGraceUntil = NowSeconds + FMath::Max(0.0f, (double)EnemyRecoverySpawnGraceSeconds);
    const double ActivationTime = ResolveEnemySearchActivationTime(RoomId, SpawnLoc, NowSeconds, PlayerPawn);
    EnemyLastKnownValidLocationByPawn.Add(WeakEnemy, SpawnLoc);
    EnemyStuckLastProgressLocationByPawn.Add(WeakEnemy, SpawnLoc);
    EnemyStuckLastProgressTimeByPawn.Add(WeakEnemy, NowSeconds);
    EnemyUndergroundNextCheckByPawn.Add(WeakEnemy, RecoveryGraceUntil);
    EnemyStuckNextCheckByPawn.Add(WeakEnemy, RecoveryGraceUntil);
    EnemyStuckRecoveryFailuresByPawn.Remove(WeakEnemy);
    EnemyTrackedLastObservedLocationByPawn.Add(WeakEnemy, SpawnLoc);
    EnemyTrackedNextPeriodicLogByPawn.Remove(WeakEnemy);
    EnemySearchActivationTimeByPawn.Add(WeakEnemy, ActivationTime);
    EnemySearchNextOrderTimeByPawn.Add(WeakEnemy, ActivationTime + FMath::FRandRange(0.08, 0.55));
    Enemy->OnDestroyed.AddUniqueDynamic(this, &URaidCombatSubsystem::OnEnemyDestroyed);
    RaiseRoomCombatAlert(RoomId, NowSeconds);
    LogTrackedEnemyState(Enemy, RoomId, TEXT("ExternalSpawn"));
}

void URaidCombatSubsystem::OnEnemyKilled(APawn* Enemy)
{
    if (!Enemy) return;
    DisableDeadEnemyDamageSources(Enemy, TEXT("OnEnemyKilled"));
    OnEnemyDestroyed(Enemy);
}

bool URaidCombatSubsystem::EnsureDropSoulNiagaraBinding(AActor* DropSoulActor, bool bLogRepair) const
{
    if (!IsValid(DropSoulActor) || !IsDropSoulObject(DropSoulActor))
    {
        return false;
    }

    UObject* BoundObject = const_cast<UObject*>(GetObjectPropertyValueIfExists(DropSoulActor, TEXT("Niagara")));
    if (IsValid(BoundObject))
    {
        return true;
    }

    UObject* NiagaraCandidate = FindFirstNiagaraLikeComponent(DropSoulActor);
    if (!IsValid(NiagaraCandidate))
    {
        if (bLogRepair && bEnableCombatPerfLogs)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat] DropSoul Niagara repair failed. Actor='%s' (no Niagara component found)"),
                *GetNameSafe(DropSoulActor));
        }
        return false;
    }

    bool bAssigned = false;
    bAssigned |= SetObjectPropertyIfExists(DropSoulActor, TEXT("Niagara"), NiagaraCandidate);
    bAssigned |= SetObjectPropertyIfExists(DropSoulActor, TEXT("NiagaraComponent"), NiagaraCandidate);
    bAssigned |= SetObjectPropertyIfExists(DropSoulActor, TEXT("NiagaraComp"), NiagaraCandidate);

    if (bAssigned && bLogRepair && bEnableCombatPerfLogs)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] DropSoul Niagara rebound. Actor='%s' Component='%s'"),
            *GetNameSafe(DropSoulActor),
            *GetNameSafe(Cast<UActorComponent>(NiagaraCandidate)));
    }

    return bAssigned;
}

void URaidCombatSubsystem::SpawnOrRepairDropSoulAt(const FVector& WorldLocation, int32 SourceRoomId)
{
    if (!bEnableDropSoulOnEnemyDeath)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        return;
    }

    if (DropSoulSpawnChance < 1.0f)
    {
        if (FMath::FRand() > FMath::Clamp(DropSoulSpawnChance, 0.0f, 1.0f))
        {
            return;
        }
    }

    if (DropSoulMaxPerRoom > 0 && SourceRoomId != INDEX_NONE)
    {
        const int32 SpawnedCountForRoom = DropSoulSpawnCountByRoom.FindRef(SourceRoomId);
        if (SpawnedCountForRoom >= DropSoulMaxPerRoom)
        {
            if (bLogDropSoulLifecycle && bEnableCombatPerfLogs)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidCombat][DropSoul] Skip spawn in Room %d (cap %d reached)."),
                    SourceRoomId,
                    DropSoulMaxPerRoom);
            }
            return;
        }
    }

    const float DuplicateRadiusSq = FMath::Square(FMath::Max(0.0f, DropSoulDuplicateCheckRadius));
    int32 ActiveDropSoulCount = 0;
    if (DuplicateRadiusSq > 0.0f)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* ExistingActor = *It;
            if (!IsValid(ExistingActor) || !IsDropSoulObject(ExistingActor))
            {
                continue;
            }

            ++ActiveDropSoulCount;
            if (FVector::DistSquared(ExistingActor->GetActorLocation(), WorldLocation) <= DuplicateRadiusSq)
            {
                EnsureDropSoulNiagaraBinding(ExistingActor, false);
                if (bLogDropSoulLifecycle)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[RaidCombat][DropSoul] Reused existing drop '%s' near %s"),
                        *GetNameSafe(ExistingActor),
                        *WorldLocation.ToCompactString());
                }
                return;
            }
        }
    }
    else if (DropSoulMaxActiveInWorld > 0)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const AActor* ExistingActor = *It;
            if (IsValid(ExistingActor) && IsDropSoulObject(ExistingActor))
            {
                ++ActiveDropSoulCount;
            }
        }
    }

    if (DropSoulMaxActiveInWorld > 0 && ActiveDropSoulCount >= DropSoulMaxActiveInWorld)
    {
        if (bLogDropSoulLifecycle && bEnableCombatPerfLogs)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat][DropSoul] Skip spawn (active cap %d reached)."),
                DropSoulMaxActiveInWorld);
        }
        return;
    }

    UClass* DropSoulClass = DropSoulActorClass.LoadSynchronous();
    if (!DropSoulClass)
    {
        static const FSoftClassPath DefaultDropSoulClassPath(TEXT("/Game/AdvancedLocomotionV4/Drop_VFX/BP_DropSoul.BP_DropSoul_C"));
        DropSoulClass = DefaultDropSoulClassPath.TryLoadClass<AActor>();
    }
    if (!DropSoulClass)
    {
        static bool bLoggedMissingClass = false;
        if (!bLoggedMissingClass)
        {
            bLoggedMissingClass = true;
            UE_LOG(
                LogTemp,
                Error,
                TEXT("[RaidCombat] DropSoul spawn class missing. Check '/Game/AdvancedLocomotionV4/Drop_VFX/BP_DropSoul'."));
        }
        return;
    }

    const FVector SpawnLocation = WorldLocation + FVector(0.0f, 0.0f, DropSoulSpawnZOffset);
    const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
    if (AActor* SpawnedDropSoul = World->SpawnActorDeferred<AActor>(
        DropSoulClass,
        SpawnTransform,
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn))
    {
        SpawnedDropSoul->Tags.AddUnique(TEXT("DropSoul"));
        SpawnedDropSoul->Tags.AddUnique(TEXT("RaidDropSoul"));

        SpawnedDropSoul->FinishSpawning(SpawnTransform, false);
        EnsureDropSoulNiagaraBinding(SpawnedDropSoul, false);

        if (SourceRoomId != INDEX_NONE)
        {
            DropSoulSpawnCountByRoom.FindOrAdd(SourceRoomId)++;
        }

        if (bLogDropSoulLifecycle)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat][DropSoul] Spawned '%s' at %s"),
                *GetNameSafe(SpawnedDropSoul),
                *SpawnLocation.ToCompactString());
        }
    }
}

void URaidCombatSubsystem::RepairDropSoulNiagaraBindings()
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        return;
    }

    int32 SeenDropSoulCount = 0;
    int32 RepairedCount = 0;
    int32 FailedCount = 0;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Candidate = *It;
        if (!IsValid(Candidate) || !IsDropSoulObject(Candidate))
        {
            continue;
        }

        ++SeenDropSoulCount;
        const bool bWasValidBefore = IsValid(GetObjectPropertyValueIfExists(Candidate, TEXT("Niagara")));
        const bool bBound = EnsureDropSoulNiagaraBinding(Candidate, false);
        const bool bValidAfter = IsValid(GetObjectPropertyValueIfExists(Candidate, TEXT("Niagara")));

        if (!bWasValidBefore && bValidAfter && bBound)
        {
            ++RepairedCount;
        }
        else if (!bValidAfter)
        {
            ++FailedCount;
        }
    }

    if (FailedCount > 0 || (bEnableCombatPerfLogs && RepairedCount > 0))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] DropSoul Niagara repair sweep. Seen=%d Repaired=%d Failed=%d"),
            SeenDropSoulCount,
            RepairedCount,
            FailedCount);
    }
}

void URaidCombatSubsystem::ApplyPerformanceWarmupCVars() const
{
    auto SetIntCVarIfExists = [](const TCHAR* Name, int32 Value)
        {
            if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
            {
                // First try project-code priority, then force at console priority if another source pinned the value.
                CVar->Set(Value, ECVF_SetByCode);
                if (CVar->GetInt() != Value)
                {
                    CVar->Set(Value, ECVF_SetByConsole);
                }
            }
        };

    SetIntCVarIfExists(TEXT("r.PSOPrecaching"), 1);
    SetIntCVarIfExists(TEXT("r.PSOPrecache.ProxyCreationWhenPSOReady"), 1);
    SetIntCVarIfExists(TEXT("r.ShaderPipelineCache.Enabled"), 1);
    SetIntCVarIfExists(TEXT("r.ShaderPipelineCache.StartupMode"), 3);
    if (bForceNiagaraPSOPrecache)
    {
        SetIntCVarIfExists(TEXT("r.PSOPrecache.NiagaraPrecachePSOAtAssetLoadingTime"), 1);
        SetIntCVarIfExists(TEXT("fx.Niagara.Emitter.ComputePSOPrecacheMode"), 2);
    }
}

void URaidCombatSubsystem::PreloadWarmupAssets()
{
    TArray<FSoftObjectPath> WarmupAssets;
    WarmupAssets.Reserve(12 + AdditionalWarmupAssets.Num());

    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidCompass.WBP_RaidCompass_C")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/AILevel/Blueprints/Interfaces/WBP_ItemDot.WBP_ItemDot_C")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/Particles/NS_BulletTrail.NS_BulletTrail")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/Particles/NS_CartridgeCaseDrop_Mesh.NS_CartridgeCaseDrop_Mesh")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Blood_VFX/Particles/NS_BloodEffect.NS_BloodEffect")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/Particles/NiagaraBloodFX/NS_BloodEffect_02.NS_BloodEffect_02")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/Game/Particles/NiagaraBloodFX/NS_BloodBurts_01.NS_BloodBurts_01")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Drop_VFX/NS_DropSoul.NS_DropSoul")));
    WarmupAssets.AddUnique(FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/CharacterAssets/Zombie_Character/Deformer/DG_Zombie_HolesDeformWithColor.DG_Zombie_HolesDeformWithColor")));

    if (!DropSoulActorClass.IsNull())
    {
        WarmupAssets.AddUnique(DropSoulActorClass.ToSoftObjectPath());
    }

    for (const FSoftObjectPath& ExtraPath : AdditionalWarmupAssets)
    {
        if (ExtraPath.IsValid())
        {
            WarmupAssets.AddUnique(ExtraPath);
        }
    }

    if (WarmupAssets.Num() == 0)
    {
        return;
    }

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    if (!bEnableAsyncRuntimeAssetWarmup)
    {
        StreamableManager.RequestSyncLoad(WarmupAssets, false);
        bRuntimeAssetWarmupRequested = false;
        RuntimeWarmupHandle.Reset();
        return;
    }

    if (bRuntimeAssetWarmupRequested)
    {
        return;
    }

    bRuntimeAssetWarmupRequested = true;
    TWeakObjectPtr<URaidCombatSubsystem> WeakThis(this);
    RuntimeWarmupHandle = StreamableManager.RequestAsyncLoad(
        WarmupAssets,
        FStreamableDelegate::CreateLambda([WeakThis]()
            {
                if (URaidCombatSubsystem* StrongThis = WeakThis.Get())
                {
                    StrongThis->bRuntimeAssetWarmupRequested = false;
                    StrongThis->RuntimeWarmupHandle.Reset();
                }
            }));

    if (!RuntimeWarmupHandle.IsValid())
    {
        bRuntimeAssetWarmupRequested = false;
        StreamableManager.RequestSyncLoad(WarmupAssets, false);
    }
}

void URaidCombatSubsystem::SpawnHiddenWarmupEnemy(const URaidEnemyPresetRegistry* Registry)
{
    if (!bSpawnHiddenEnemyWarmup || !Registry)
    {
        return;
    }

    static const FName WarmupCandidates[] =
    {
        TEXT("Default"),
        TEXT("Raider"),
        TEXT("Scavenger"),
        TEXT("Zombie"),
        TEXT("BossGuard")
    };

    TSubclassOf<APawn> WarmupEnemyClass = nullptr;
    for (const FName Candidate : WarmupCandidates)
    {
        FRaidEnemyPreset CandidatePreset;
        if (!Registry->ResolvePreset(Candidate, CandidatePreset) || !CandidatePreset.IsValid())
        {
            continue;
        }

        WarmupEnemyClass = Registry->ResolveEnemyClassFromPreset(Candidate);
        if (WarmupEnemyClass)
        {
            break;
        }
    }

    if (!WarmupEnemyClass)
    {
        return;
    }

    APawn* PawnCDO = Cast<APawn>(WarmupEnemyClass->GetDefaultObject());
    if (!PawnCDO)
    {
        return;
    }

    // Avoid spawning/destroying temporary AI in live world (can trigger transient BP AccessedNone errors).
    // Touch critical default objects to prewarm class/controller/anim assets without runtime side effects.
    if (UClass* AIControllerClass = PawnCDO->AIControllerClass)
    {
        AIControllerClass->GetDefaultObject();
    }

    TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents;
    PawnCDO->GetComponents<USkeletalMeshComponent>(SkeletalComponents);
    for (USkeletalMeshComponent* SkeletalComp : SkeletalComponents)
    {
        if (!IsValid(SkeletalComp))
        {
            continue;
        }
        SkeletalComp->GetSkeletalMeshAsset();
        SkeletalComp->GetAnimClass();
    }
}

void URaidCombatSubsystem::SyncBandageHealFromLootDataTable()
{
    if (bBandageHealSyncedFromLootDataTable || !bSyncBandageHealFromLootDataTable)
    {
        return;
    }

    UDataTable* LootTable = nullptr;
    if (!LootItemsDataTableOverride.IsNull())
    {
        LootTable = LootItemsDataTableOverride.LoadSynchronous();
    }
    else
    {
        static const FSoftObjectPath DefaultLootTablePath(TEXT("/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable"));
        LootTable = Cast<UDataTable>(DefaultLootTablePath.TryLoad());
    }

    if (!IsValid(LootTable) || !LootTable->GetRowStruct())
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Bandage heal sync skipped: invalid LootItemsDataTable."));
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    const UScriptStruct* RowStruct = LootTable->GetRowStruct();
    if (!RowStruct)
    {
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    auto TryGetStringPropertyValue = [](const FProperty* Property, const void* Container, FString& OutValue) -> bool
        {
            if (!Property || !Container)
            {
                return false;
            }

            if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
            {
                OutValue = StrProperty->GetPropertyValue_InContainer(Container);
                return true;
            }
            if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
            {
                OutValue = NameProperty->GetPropertyValue_InContainer(Container).ToString();
                return true;
            }
            if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
            {
                OutValue = TextProperty->GetPropertyValue_InContainer(Container).ToString();
                return true;
            }
            if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
            {
                if (const UClass* ClassValue = Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(Container)))
                {
                    OutValue = ClassValue->GetPathName();
                    return true;
                }
                return false;
            }
            if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
            {
                const FSoftObjectPtr& SoftClassPtr = SoftClassProperty->GetPropertyValue_InContainer(Container);
                OutValue = SoftClassPtr.ToString();
                return !OutValue.IsEmpty();
            }
            if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
            {
                const FSoftObjectPtr& SoftObjectPtr = SoftObjectProperty->GetPropertyValue_InContainer(Container);
                OutValue = SoftObjectPtr.ToString();
                return !OutValue.IsEmpty();
            }
            if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
            {
                if (const UObject* ObjValue = ObjectProperty->GetPropertyValue_InContainer(Container))
                {
                    OutValue = ObjValue->GetPathName();
                    return true;
                }
                return false;
            }
            if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
            {
                const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
                const void* ValuePtr = Underlying ? Underlying->ContainerPtrToValuePtr<void>(Container) : nullptr;
                if (ValuePtr)
                {
                    const int64 EnumValue = Underlying->GetSignedIntPropertyValue(ValuePtr);
                    if (const UEnum* EnumDef = EnumProperty->GetEnum())
                    {
                        OutValue = EnumDef->GetNameStringByValue(EnumValue);
                        return true;
                    }
                }
                return false;
            }
            if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
            {
                if (const UEnum* EnumDef = ByteProperty->GetIntPropertyEnum())
                {
                    const int64 EnumValue = ByteProperty->GetPropertyValue_InContainer(Container);
                    OutValue = EnumDef->GetNameStringByValue(EnumValue);
                    return true;
                }
                return false;
            }

            return false;
        };

    auto TryResolveRowMedicalParam1 =
        [&](const FName RowName, const uint8* RowData, bool& bOutMedical, bool& bOutStrongMedical, double& OutParam1) -> bool
        {
            bOutMedical = IsMedicalLootIdentifier(RowName.ToString());
            bOutStrongMedical = false;
            OutParam1 = 0.0;
            bool bHasParam1 = false;

            for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
            {
                const FProperty* Property = *PropIt;
                if (!Property)
                {
                    continue;
                }

                const FString PropName = Property->GetName();
                if (!bHasParam1 &&
                    (PropName.Equals(TEXT("Param1"), ESearchCase::IgnoreCase) ||
                     PropName.Contains(TEXT("Param1"), ESearchCase::IgnoreCase) ||
                     PropName.Contains(TEXT("BandageHealthRegen"), ESearchCase::IgnoreCase) ||
                     PropName.Contains(TEXT("Heal"), ESearchCase::IgnoreCase)))
                {
                    double NumericValue = 0.0;
                    if (ReadNumericPropertyAsDouble(Property, RowData, NumericValue) &&
                        FMath::IsFinite(NumericValue) &&
                        NumericValue > KINDA_SMALL_NUMBER)
                    {
                        OutParam1 = NumericValue;
                        bHasParam1 = true;
                    }
                }

                FString ValueText;
                if (TryGetStringPropertyValue(Property, RowData, ValueText))
                {
                    if (IsMedicalLootIdentifier(ValueText))
                    {
                        bOutMedical = true;
                    }

                    if (ValueText.Contains(TEXT("BP_LootPickableItem_MedicalItem"), ESearchCase::IgnoreCase))
                    {
                        bOutMedical = true;
                        bOutStrongMedical = true;
                    }
                }
            }

            return bOutMedical && bHasParam1;
        };

    double ResolvedBandageHeal = 0.0;
    FString ResolvedRowName;
    bool bResolved = false;

    if (MedicalLootRowName != NAME_None)
    {
        const uint8* const* ExplicitRowData = LootTable->GetRowMap().Find(MedicalLootRowName);
        if (ExplicitRowData && *ExplicitRowData)
        {
            bool bMedical = false;
            bool bStrongMedical = false;
            double Param1Value = 0.0;
            if (TryResolveRowMedicalParam1(MedicalLootRowName, *ExplicitRowData, bMedical, bStrongMedical, Param1Value))
            {
                ResolvedBandageHeal = Param1Value;
                ResolvedRowName = MedicalLootRowName.ToString();
                bResolved = true;
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidCombat] MedicalLootRowName '%s' exists but was not resolved as medical Param1 row. FallbackScan=%s"),
                    *MedicalLootRowName.ToString(),
                    bFallbackScanMedicalLootRow ? TEXT("On") : TEXT("Off"));
            }
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat] MedicalLootRowName '%s' not found in LootItemsDataTable. FallbackScan=%s"),
                *MedicalLootRowName.ToString(),
                bFallbackScanMedicalLootRow ? TEXT("On") : TEXT("Off"));
        }
    }

    if (!bResolved && bFallbackScanMedicalLootRow)
    {
        bool bHasFallbackCandidate = false;
        double FallbackParam1 = 0.0;
        FString FallbackRowName;

        for (const TPair<FName, uint8*>& RowPair : LootTable->GetRowMap())
        {
            const uint8* RowData = RowPair.Value;
            if (!RowData)
            {
                continue;
            }

            bool bMedical = false;
            bool bStrongMedical = false;
            double Param1Value = 0.0;
            if (!TryResolveRowMedicalParam1(RowPair.Key, RowData, bMedical, bStrongMedical, Param1Value))
            {
                continue;
            }

            if (bStrongMedical)
            {
                ResolvedBandageHeal = Param1Value;
                ResolvedRowName = RowPair.Key.ToString();
                bResolved = true;
                break;
            }

            if (!bHasFallbackCandidate)
            {
                bHasFallbackCandidate = true;
                FallbackParam1 = Param1Value;
                FallbackRowName = RowPair.Key.ToString();
            }
        }

        if (!bResolved && bHasFallbackCandidate)
        {
            ResolvedBandageHeal = FallbackParam1;
            ResolvedRowName = FallbackRowName;
            bResolved = true;
        }
    }

    if (!bResolved || ResolvedBandageHeal <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Bandage heal sync skipped: no valid medical Param1 row found."));
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    UClass* BandageAbilityClassResolved = nullptr;
    if (!BandageUseAbilityClass.IsNull())
    {
        BandageAbilityClassResolved = BandageUseAbilityClass.LoadSynchronous();
    }
    if (!BandageAbilityClassResolved)
    {
        BandageAbilityClassResolved = LoadClass<UObject>(
            nullptr,
            TEXT("/Game/AdvancedLocomotionV4/Blueprints/Abilities/ALS_Ability_UseBandage.ALS_Ability_UseBandage_C"));
    }

    if (!BandageAbilityClassResolved)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Bandage heal sync skipped: ALS_Ability_UseBandage class load failed."));
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    const FName PropertyName = BandageHealthRegenPropertyName.IsNone()
        ? FName(TEXT("BandageHealthRegen"))
        : BandageHealthRegenPropertyName;
    FProperty* HealProperty = BandageAbilityClassResolved->FindPropertyByName(PropertyName);
    if (!HealProperty)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Bandage heal sync skipped: property '%s' not found on %s."),
            *PropertyName.ToString(),
            *BandageAbilityClassResolved->GetPathName());
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    UObject* AbilityCDO = BandageAbilityClassResolved->GetDefaultObject();
    if (!IsValid(AbilityCDO))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Bandage heal sync skipped: invalid CDO."));
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    double OldValue = 0.0;
    ReadNumericPropertyAsDouble(HealProperty, AbilityCDO, OldValue);
    if (!WriteNumericPropertyFromDouble(HealProperty, AbilityCDO, ResolvedBandageHeal))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Bandage heal sync failed: could not write property '%s'."), *PropertyName.ToString());
        bBandageHealSyncedFromLootDataTable = true;
        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[RaidCombat] Bandage heal synced from LootItemsDataTable. Row=%s Param1=%.2f Old=%.2f New=%.2f"),
        *ResolvedRowName,
        ResolvedBandageHeal,
        OldValue,
        ResolvedBandageHeal);

    bBandageHealSyncedFromLootDataTable = true;
}

void URaidCombatSubsystem::PrimeRuntimeAssets(const URaidChapterConfig* ChapterConfig)
{
    if (!bRuntimeAssetsPrimed)
    {
        if (bEnableRuntimeAssetWarmup)
        {
            ApplyPerformanceWarmupCVars();
            PreloadWarmupAssets();
        }
        bRuntimeAssetsPrimed = true;
    }

    if (!bEnemyPresetClassesPrimed && ChapterConfig && ChapterConfig->EnemyPresetRegistry)
    {
        const URaidEnemyPresetRegistry* ConstRegistry = ChapterConfig->EnemyPresetRegistry.Get();
        URaidEnemyPresetRegistry* Registry = const_cast<URaidEnemyPresetRegistry*>(ConstRegistry);
        if (bEnableAsyncEnemyPresetPreload)
        {
            if (!bEnemyPresetWarmupRequested)
            {
                TArray<FSoftObjectPath> EnemyPresetWarmupAssets;
                Registry->GatherPreloadAssetPaths(EnemyPresetWarmupAssets);

                if (EnemyPresetWarmupAssets.Num() == 0)
                {
                    bEnemyPresetClassesPrimed = true;
                }
                else
                {
                    bEnemyPresetWarmupRequested = true;
                    TWeakObjectPtr<URaidCombatSubsystem> WeakThis(this);
                    TWeakObjectPtr<URaidEnemyPresetRegistry> WeakRegistry(Registry);
                    EnemyPresetWarmupHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
                        EnemyPresetWarmupAssets,
                        FStreamableDelegate::CreateLambda([WeakThis, WeakRegistry]()
                            {
                                URaidCombatSubsystem* StrongThis = WeakThis.Get();
                                if (!StrongThis)
                                {
                                    return;
                                }

                                StrongThis->bEnemyPresetWarmupRequested = false;
                                StrongThis->EnemyPresetWarmupHandle.Reset();

                                if (URaidEnemyPresetRegistry* LoadedRegistry = WeakRegistry.Get())
                                {
                                    LoadedRegistry->PrimeLoadedEnemyPresetClassDefaults(StrongThis->bEnableCombatPerfLogs);
                                    if (StrongThis->bEnableRuntimeAssetWarmup)
                                    {
                                        StrongThis->SpawnHiddenWarmupEnemy(LoadedRegistry);
                                    }
                                }

                                StrongThis->bEnemyPresetClassesPrimed = true;
                            }));

                    if (!EnemyPresetWarmupHandle.IsValid())
                    {
                        bEnemyPresetWarmupRequested = false;
                        Registry->PreloadAllEnemyPresetClasses(bEnableCombatPerfLogs);
                        if (bEnableRuntimeAssetWarmup)
                        {
                            SpawnHiddenWarmupEnemy(Registry);
                        }
                        bEnemyPresetClassesPrimed = true;
                    }
                }
            }
        }
        else
        {
            Registry->PreloadAllEnemyPresetClasses(bEnableCombatPerfLogs);
            if (bEnableRuntimeAssetWarmup)
            {
                SpawnHiddenWarmupEnemy(Registry);
            }
            bEnemyPresetClassesPrimed = true;
        }
    }

    SyncBandageHealFromLootDataTable();
}

void URaidCombatSubsystem::ResetSubsystem()
{
    RoomById.Empty();
    AliveByRoomId.Empty();
    EnemyToRoomMap.Empty();
    ProcessedEnemyDeathActors.Empty();
    ProcessedEnemyDeathKeys.Empty();
    EnemyTraceCollisionNextRefreshByPawn.Empty();
    EnemyUndergroundNextCheckByPawn.Empty();
    EnemyUndergroundRecoveryFailuresByPawn.Empty();
    EnemyLastKnownValidLocationByPawn.Empty();
    EnemyStuckLastProgressLocationByPawn.Empty();
    EnemyStuckLastProgressTimeByPawn.Empty();
    EnemyStuckNextCheckByPawn.Empty();
    EnemyStuckRecoveryFailuresByPawn.Empty();
    EnemyTrackedNextPeriodicLogByPawn.Empty();
    EnemyTrackedLastObservedLocationByPawn.Empty();
    EnemySearchActivationTimeByPawn.Empty();
    EnemySearchNextOrderTimeByPawn.Empty();
    RoomCombatAlertUntilByRoomId.Empty();
    CachedPrimaryPlayerPawn.Reset();
    NextPrimaryPawnRefreshTimeSeconds = 0.0;
    ClearPOIs();
    bInternalClearing = false;
    CurrentObjectiveRoomId = -1;
    CurrentObjectiveLocation = FVector::ZeroVector;
    LastProgressTimeSeconds = 0.0f;
    LastDistanceToObjective = TNumericLimits<float>::Max();
    WrongDirectionScore = 0.0f;
    bFoliageTraceCollisionSanitized = false;
    bBloodTraceSettingsPatched = false;
    bRuntimeAssetsPrimed = false;
    bRuntimeAssetWarmupRequested = false;
    bEnemyPresetClassesPrimed = false;
    bEnemyPresetWarmupRequested = false;
    bBandageHealSyncedFromLootDataTable = false;
    NextFoliageSanitizeRetryTimeSeconds = 0.0;
    NextBloodDecalCleanupTimeSeconds = 0.0;
    NextFallbackEnemySweepTimeSeconds = 0.0;
    NextDropSoulRepairTimeSeconds = 0.0;
    NextPlayerGunfireNoiseReportTimeSeconds = 0.0;
    RecentSpawnHeavyWorkDeferUntilSeconds = 0.0;
    if (RuntimeWarmupHandle.IsValid())
    {
        RuntimeWarmupHandle->CancelHandle();
        RuntimeWarmupHandle.Reset();
    }
    if (EnemyPresetWarmupHandle.IsValid())
    {
        EnemyPresetWarmupHandle->CancelHandle();
        EnemyPresetWarmupHandle.Reset();
    }
    DropSoulSpawnCountByRoom.Empty();
    PrewarmedSpawnPlansByRoomId.Empty();
    CurrentWaveNumber = 0;
    AliveWaveEnemyCount = 0;
    NextWaveStartTimeSeconds = 0.0;
    bWaveSchedulerInitialized = false;
    PendingRegionBanners.Empty();
    CachedRegionBannerWidgetClass = nullptr;
    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->RemoveFromParent();
    }
    ActiveRegionBannerWidget = nullptr;
    RegionBannerBusyUntilTimeSeconds = 0.0;
    bRegionBannerVisibleBySubsystem = false;
    FoliageSanitizeRetryCount = 0;
    StartRoomId = -1;
    bStartFlowInitialized = false;
    bPlayerSpawnedInsideStartRoom = false;
    bStartPendingClearOnExit = false;
    StopTraceCollisionEnforcer();
    StopRegionBannerQueueTickerIfIdle();
}

void URaidCombatSubsystem::Deinitialize()
{
    StopTraceCollisionEnforcer();

    RoomById.Empty();
    AliveByRoomId.Empty();
    EnemyToRoomMap.Empty();
    ProcessedEnemyDeathActors.Empty();
    ProcessedEnemyDeathKeys.Empty();
    EnemyTraceCollisionNextRefreshByPawn.Empty();
    EnemyUndergroundNextCheckByPawn.Empty();
    EnemyUndergroundRecoveryFailuresByPawn.Empty();
    EnemyLastKnownValidLocationByPawn.Empty();
    EnemyStuckLastProgressLocationByPawn.Empty();
    EnemyStuckLastProgressTimeByPawn.Empty();
    EnemyStuckNextCheckByPawn.Empty();
    EnemyStuckRecoveryFailuresByPawn.Empty();
    EnemyTrackedNextPeriodicLogByPawn.Empty();
    EnemyTrackedLastObservedLocationByPawn.Empty();
    EnemySearchActivationTimeByPawn.Empty();
    EnemySearchNextOrderTimeByPawn.Empty();
    RoomCombatAlertUntilByRoomId.Empty();
    CachedPrimaryPlayerPawn.Reset();
    NextPrimaryPawnRefreshTimeSeconds = 0.0;
    ActivePOIs.Empty();

    bInternalClearing = false;
    CurrentObjectiveRoomId = -1;
    CurrentObjectiveLocation = FVector::ZeroVector;
    LastProgressTimeSeconds = 0.0f;
    LastDistanceToObjective = TNumericLimits<float>::Max();
    WrongDirectionScore = 0.0f;
    bFoliageTraceCollisionSanitized = true;
    bBloodTraceSettingsPatched = false;
    bRuntimeAssetsPrimed = false;
    bRuntimeAssetWarmupRequested = false;
    bEnemyPresetClassesPrimed = false;
    bEnemyPresetWarmupRequested = false;
    bBandageHealSyncedFromLootDataTable = false;
    NextFoliageSanitizeRetryTimeSeconds = 0.0;
    NextBloodDecalCleanupTimeSeconds = 0.0;
    NextFallbackEnemySweepTimeSeconds = 0.0;
    NextDropSoulRepairTimeSeconds = 0.0;
    NextPlayerGunfireNoiseReportTimeSeconds = 0.0;
    RecentSpawnHeavyWorkDeferUntilSeconds = 0.0;
    if (RuntimeWarmupHandle.IsValid())
    {
        RuntimeWarmupHandle->CancelHandle();
        RuntimeWarmupHandle.Reset();
    }
    if (EnemyPresetWarmupHandle.IsValid())
    {
        EnemyPresetWarmupHandle->CancelHandle();
        EnemyPresetWarmupHandle.Reset();
    }
    DropSoulSpawnCountByRoom.Empty();
    PrewarmedSpawnPlansByRoomId.Empty();
    CurrentWaveNumber = 0;
    AliveWaveEnemyCount = 0;
    NextWaveStartTimeSeconds = 0.0;
    bWaveSchedulerInitialized = false;
    PendingRegionBanners.Empty();
    CachedRegionBannerWidgetClass = nullptr;
    if (ActiveRegionBannerWidget && ActiveRegionBannerWidget->IsInViewport())
    {
        ActiveRegionBannerWidget->RemoveFromParent();
    }
    ActiveRegionBannerWidget = nullptr;
    RegionBannerBusyUntilTimeSeconds = 0.0;
    bRegionBannerVisibleBySubsystem = false;
    FoliageSanitizeRetryCount = 0;
    StartRoomId = -1;
    bStartFlowInitialized = false;
    bPlayerSpawnedInsideStartRoom = false;
    bStartPendingClearOnExit = false;
    StopRegionBannerQueueTickerIfIdle();

    Super::Deinitialize();
}

int32 URaidCombatSubsystem::GetRoomTypePriorityInternal(const FString& RoomType) const
{
    return GetRoomTypePriority(RoomType);
}

int32 URaidCombatSubsystem::MakeWaveRoomIdInternal(int32 WaveNumber) const
{
    return MakeWaveRoomId(WaveNumber);
}

bool URaidCombatSubsystem::IsCombatSpawnRoomTypeInternal(const FString& RoomType) const
{
    return IsCombatSpawnRoomType(RoomType);
}

void URaidCombatSubsystem::BuildPresetCandidatesInternal(const FLevelNodeRow& Row, TArray<FName>& OutCandidates) const
{
    BuildPresetCandidates(Row, OutCandidates);
}

bool URaidCombatSubsystem::IsWaterHitInternal(const FHitResult& Hit) const
{
    return IsWaterHit(Hit);
}

bool URaidCombatSubsystem::IsCapsuleBlockedForPawnInternal(UWorld* World, const FVector& PawnActorLocation, float CapsuleRadius, float CapsuleHalfHeight, const AActor* ActorToIgnore) const
{
    return IsCapsuleBlockedForPawn(World, PawnActorLocation, CapsuleRadius, CapsuleHalfHeight, ActorToIgnore);
}

void URaidCombatSubsystem::ResolvePawnCapsuleSizeInternal(TSubclassOf<APawn> EnemyClass, float& OutRadius, float& OutHalfHeight) const
{
    ResolvePawnCapsuleSize(EnemyClass, OutRadius, OutHalfHeight);
}

void URaidCombatSubsystem::ResolvePawnInstanceCapsuleSizeInternal(const APawn* Pawn, float& OutRadius, float& OutHalfHeight) const
{
    ResolvePawnInstanceCapsuleSize(Pawn, OutRadius, OutHalfHeight);
}

bool URaidCombatSubsystem::IsNearRoomObstacleInternal(UWorld* World, const ARaidRoomActor* Room, const FVector& Location, float Radius) const
{
    return IsNearRoomObstacle(World, Room, Location, Radius);
}

bool URaidCombatSubsystem::TryResolveAIGroundHitInternal(UWorld* World, ARaidRoomActor* Room, const FVector& XYLocation, FHitResult& OutHit) const
{
    return TryResolveAIGroundHit(World, Room, XYLocation, OutHit);
}

bool URaidCombatSubsystem::TryResolveSafeAIPawnSpawnLocationInternal(UWorld* World, ARaidRoomActor* Room, UNavigationSystemV1* NavSys, const FVector& XYLocation, float CapsuleRadius, float CapsuleHalfHeight, FVector& OutActorLocation) const
{
    return TryResolveSafeAIPawnSpawnLocation(World, Room, NavSys, XYLocation, CapsuleRadius, CapsuleHalfHeight, OutActorLocation);
}

bool URaidCombatSubsystem::TryResolveNearbyFallbackSpawnLocationInternal(UWorld* World, ARaidRoomActor* Room, UNavigationSystemV1* NavSys, const FVector& SeedLocation, float CapsuleRadius, float CapsuleHalfHeight, FRandomStream& Stream, FVector& OutActorLocation) const
{
    return TryResolveNearbyFallbackSpawnLocation(World, Room, NavSys, SeedLocation, CapsuleRadius, CapsuleHalfHeight, Stream, OutActorLocation);
}

void URaidCombatSubsystem::DisableEnemyRuntimeDeformerComponentsInternal(APawn* Enemy) const
{
    if (!bDisableEnemyRuntimeMeshDeformer || !IsValid(Enemy) || Enemy->IsPlayerControlled())
    {
        return;
    }

    if (Enemy->Tags.Contains(TEXT("RaidDeformerDisabled")))
    {
        return;
    }

    TInlineComponentArray<UActorComponent*> Components;
    Enemy->GetComponents(Components);

    bool bDisabledAny = false;
    for (UActorComponent* Component : Components)
    {
        if (!IsValid(Component))
        {
            continue;
        }

        const FString ClassName = Component->GetClass()->GetName();
        const bool bLooksDeformerComponent =
            ContainsAnyToken(ClassName, { TEXT("Optimus"), TEXT("MeshDeformer"), TEXT("DeformerGraph"), TEXT("DeformerComponent") });
        if (!bLooksDeformerComponent)
        {
            continue;
        }

        SetBoolPropertyIfExists(Component, TEXT("bEnabled"), false);
        SetBoolPropertyIfExists(Component, TEXT("bUseDeformer"), false);
        Component->SetComponentTickEnabled(false);
        Component->Deactivate();
        Component->SetActive(false, false);
        Component->ComponentTags.AddUnique(TEXT("RaidDeformerDisabled"));
        bDisabledAny = true;
    }

    if (bDisabledAny)
    {
        Enemy->Tags.AddUnique(TEXT("RaidDeformerDisabled"));
    }
}

void URaidCombatSubsystem::ForceEnemyTraceCollisionInternal(APawn* Enemy) const
{
    ForceEnemyTraceCollision(Enemy);
    DisableEnemyRuntimeDeformerComponentsInternal(Enemy);
}

void URaidCombatSubsystem::EnforceNoZombieGrabInternal(APawn* Enemy) const
{
    if (!bDisableZombieGrab || !IsValid(Enemy) || Enemy->IsPlayerControlled())
    {
        return;
    }

    const bool bWasAlreadyMarked = Enemy->Tags.Contains(TEXT("RaidNoZombieGrab"));
    bool bChangedAnyState = false;

    // Apply hard block on the pawn too, not only on attack components.
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bDisablePairedAttackSequences"), true);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bAttackStarted"), false);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bCanStartPairedAttack"), false);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bCanUsePairedAttack"), false);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bCanPerformPairedAttack"), false);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bAllowPairedAttack"), false);
    bChangedAnyState |= SetBoolPropertyIfExists(Enemy, TEXT("bEnablePairedAttack"), false);
    // Keep paired attack class references intact: some BP graphs read the class every tick
    // and will spam "Accessed None" if we null them while runtime logic is still evaluating.
    bChangedAnyState |= SetObjectPropertyIfExists(Enemy, TEXT("CurrentPairedAttack"), nullptr);
    bChangedAnyState |= SetObjectPropertyIfExists(Enemy, TEXT("CurrentPairedAttackActor"), nullptr);
    bChangedAnyState |= SetObjectPropertyIfExists(Enemy, TEXT("PairedAttackTarget"), nullptr);

    TInlineComponentArray<UActorComponent*> Components;
    Enemy->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (!IsValid(Component))
        {
            continue;
        }

        const FString ClassName = Component->GetClass()->GetName();
        const FString ComponentName = Component->GetName();
        const bool bLikelyZombieAttackComponent =
            ContainsAnyToken(ClassName, { TEXT("ZombieAttacksComponentCore"), TEXT("PairedAttack"), TEXT("ZombieAttack") }) ||
            ContainsAnyToken(ComponentName, { TEXT("AttacksSystemComponent"), TEXT("PairedAttack"), TEXT("ZombieAttack") });

        // Hard-disable paired/grab state on any component exposing these properties.
        bool bChangedThisComponent = false;
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bDisablePairedAttackSequences"), true);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bAttackStarted"), false);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bCanStartPairedAttack"), false);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bCanUsePairedAttack"), false);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bCanPerformPairedAttack"), false);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bAllowPairedAttack"), false);
        bChangedThisComponent |= SetBoolPropertyIfExists(Component, TEXT("bEnablePairedAttack"), false);
        // Keep class refs valid to avoid BP AccessedNone loops; behavior is blocked via flags/timer.
        bChangedThisComponent |= SetObjectPropertyIfExists(Component, TEXT("CurrentPairedAttack"), nullptr);
        bChangedThisComponent |= SetObjectPropertyIfExists(Component, TEXT("CurrentPairedAttackActor"), nullptr);
        bChangedThisComponent |= SetObjectPropertyIfExists(Component, TEXT("PairedAttackTarget"), nullptr);

        if (UFunction* StopAttackTimerFunc = Component->FindFunction(TEXT("CorrectlySetAttackTimer")))
        {
            struct FStopAttackTimerParams
            {
                bool StartAttacking = false;
                float InDuration = 0.0f;
                bool ReturnValue = false;
            };

            FStopAttackTimerParams Params;
            Component->ProcessEvent(StopAttackTimerFunc, &Params);
            bChangedThisComponent = true;
        }

        if (bLikelyZombieAttackComponent || bChangedThisComponent)
        {
            Component->ComponentTags.AddUnique(TEXT("RaidNoZombieGrab"));
        }
        bChangedAnyState |= bChangedThisComponent;
    }

    if (bDisableZombieGrabCollisionHelpers)
    {
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
        Enemy->GetComponents(PrimitiveComponents);
        for (UPrimitiveComponent* Primitive : PrimitiveComponents)
        {
            if (!IsValid(Primitive) || Primitive->ComponentHasTag(TEXT("RaidNoZombieGrabCollision")))
            {
                continue;
            }

            const FString PrimitiveClassName = Primitive->GetClass()->GetName();
            const FString PrimitiveName = Primitive->GetName();
            const bool bLikelyGrabHitbox =
                ContainsAnyToken(PrimitiveClassName, { TEXT("Grab"), TEXT("Paired"), TEXT("Struggle"), TEXT("ZombieAttack") }) ||
                ContainsAnyToken(PrimitiveName, { TEXT("Grab"), TEXT("Paired"), TEXT("Struggle"), TEXT("Victim"), TEXT("ZombieAttack") });

            if (!bLikelyGrabHitbox)
            {
                continue;
            }

            Primitive->SetGenerateOverlapEvents(false);
            Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
            Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Primitive->SetCanEverAffectNavigation(false);
            Primitive->ComponentTags.AddUnique(TEXT("RaidNoZombieGrabCollision"));
            bChangedAnyState = true;
        }
    }

    Enemy->Tags.AddUnique(TEXT("RaidNoZombieGrab"));

    if (bChangedAnyState && bLogZombieGrabDisable && !bWasAlreadyMarked)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Zombie grab disabled for '%s'."), *GetNameSafe(Enemy));
    }
}

void URaidCombatSubsystem::LogEnemyTraceCollisionSnapshotInternal(const APawn* Enemy) const
{
    LogEnemyTraceCollisionSnapshot(Enemy);
}

void URaidCombatSubsystem::SetRoomConceptRules(const TArray<FRaidRoomConceptRule>& InRules)
{
    RoomConceptRules = InRules;
    RebuildUpcomingRoomSpawnPlans();
}

void URaidCombatSubsystem::ConfigureDropSoulPolicy(float InSpawnChance, int32 InMaxPerRoom, int32 InMaxActiveInWorld)
{
    DropSoulSpawnChance = FMath::Clamp(InSpawnChance, 0.0f, 1.0f);
    DropSoulMaxPerRoom = FMath::Clamp(InMaxPerRoom, 0, 128);
    DropSoulMaxActiveInWorld = FMath::Clamp(InMaxActiveInWorld, 0, 2048);
}

void URaidCombatSubsystem::StartCombatForRoom(ARaidRoomActor* Room)
{
    UWorld* World = GetWorld();
    if (!IsValid(Room) || Room->IsCleared())
    {
        return;
    }

    const FLevelNodeRow& Row = Room->GetNodeRow();
    const bool bStartRoom = Row.RoomType.Equals(TEXT("Start"), ESearchCase::IgnoreCase);
    const bool bExitRoom = Row.RoomType.Equals(TEXT("Exit"), ESearchCase::IgnoreCase);
    const bool bLootRoom = Row.RoomType.Equals(TEXT("Loot"), ESearchCase::IgnoreCase);
    const bool bEntryClearRoom = bStartRoom || bExitRoom || bLootRoom;

    if (Room->HasCombatStarted())
    {
        return;
    }

    if (bEntryClearRoom)
    {
        APawn* PlayerPawn = GetPrimaryPlayerPawn();
        if (!IsPawnInsideRoomBounds2D(PlayerPawn, Room))
        {
            // Entry-clear rooms must be cleared only on real room entry, not by proximity checks.
            return;
        }
    }

    PrimeRuntimeAssets(Room->GetChapterConfig());
    if (!bFoliageTraceCollisionSanitized)
    {
        // Avoid a heavy sanitize pass exactly on room-enter frame.
        // Let the enforcer perform it on its normal tick.
        NextFoliageSanitizeRetryTimeSeconds = 0.0;
    }
    StartTraceCollisionEnforcer();

    if (World && PostSpawnHeavyTaskCooldown > 0.0f)
    {
        const double DeferUntil = World->GetTimeSeconds() + (double)FMath::Max(0.0f, PostSpawnHeavyTaskCooldown);
        RecentSpawnHeavyWorkDeferUntilSeconds = FMath::Max(RecentSpawnHeavyWorkDeferUntilSeconds, DeferUntil);
    }

    RoomById.Add(Room->GetNodeId(), Room);
    Room->SetCombatStarted(true);
    if (World)
    {
        RaiseRoomCombatAlert(Room->GetNodeId(), World->GetTimeSeconds());
    }
    ClearPOIs();

    if (bStartRoom)
    {
        HandleRoomCleared(Room->GetNodeId());
        return;
    }

    if (bExitRoom)
    {
        HandleRoomCleared(Room->GetNodeId());
        return;
    }

    if (bLootRoom)
    {
        Room->InternalSpawnLoot();
        HandleRoomCleared(Room->GetNodeId());
        return;
    }

    SpawnEnemiesForRoom(Room);
}

void URaidCombatSubsystem::StartTraceCollisionEnforcer()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FTimerManager& TimerManager = World->GetTimerManager();
    if (TimerManager.IsTimerActive(TraceCollisionEnforcerHandle))
    {
        return;
    }

    TimerManager.SetTimer(
        TraceCollisionEnforcerHandle,
        this,
        &URaidCombatSubsystem::EnforceTraceCollisionOnAllAIPawns,
        FMath::Max(0.1f, TraceCollisionEnforcerTickInterval),
        true,
        0.10f);
}

void URaidCombatSubsystem::StopTraceCollisionEnforcer()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    World->GetTimerManager().ClearTimer(TraceCollisionEnforcerHandle);
}

void URaidCombatSubsystem::EnforceTraceCollisionOnAllAIPawns()
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        StopTraceCollisionEnforcer();
        return;
    }

    const double NowSeconds = World->GetTimeSeconds();
    for (auto AlertIt = RoomCombatAlertUntilByRoomId.CreateIterator(); AlertIt; ++AlertIt)
    {
        if (AlertIt.Value() <= NowSeconds)
        {
            AlertIt.RemoveCurrent();
        }
    }

    const bool bInSpawnHeavyTaskCooldown = (NowSeconds < RecentSpawnHeavyWorkDeferUntilSeconds);
    const bool bNeedFoliageSanitize = !bInSpawnHeavyTaskCooldown && (NowSeconds >= NextFoliageSanitizeRetryTimeSeconds);
    const bool bNeedBloodCleanup = !bInSpawnHeavyTaskCooldown && (NowSeconds >= NextBloodDecalCleanupTimeSeconds);
    const bool bNeedDropSoulRepair = !bInSpawnHeavyTaskCooldown && (NowSeconds >= NextDropSoulRepairTimeSeconds);
    const bool bNeedFallbackSweep = !bInSpawnHeavyTaskCooldown && (NowSeconds >= NextFallbackEnemySweepTimeSeconds);
    const bool bNeedWaveTick = bEnableDynamicWaves && TotalDynamicWaves > 0;
    const bool bHasTrackedEnemies = (EnemyToRoomMap.Num() > 0);
    const bool bNeedsAliveReconcile = (AliveByRoomId.Num() > 0 || AliveWaveEnemyCount > 0);

    if (!bHasTrackedEnemies && !bNeedsAliveReconcile && !bNeedFoliageSanitize && !bNeedBloodCleanup && !bNeedDropSoulRepair && !bNeedFallbackSweep && !bNeedWaveTick)
    {
        return;
    }

    if (bNeedFoliageSanitize)
    {
        SanitizeProceduralFoliageCollisionForTraces();
    }

    if (!bBloodTraceSettingsPatched)
    {
        PatchBloodEffectTraceSettings();
    }

    if (bNeedBloodCleanup)
    {
        CleanupFloatingBloodDecals();
        NextBloodDecalCleanupTimeSeconds = NowSeconds + FMath::Max(0.10f, BloodDecalCleanupInterval);
    }

    if (bNeedDropSoulRepair)
    {
        RepairDropSoulNiagaraBindings();
        NextDropSoulRepairTimeSeconds = NowSeconds + FMath::Max(0.10f, DropSoulRepairInterval);
    }

    TArray<TWeakObjectPtr<APawn>> PendingCullPawns;
    PendingCullPawns.Reserve(4);
    TArray<TWeakObjectPtr<APawn>> PendingDeadFinalizePawns;
    PendingDeadFinalizePawns.Reserve(4);

    APawn* PrimaryPlayerPawn = GetPrimaryPlayerPawn();
    const bool bPlayerGunfireDetected = IsValid(PrimaryPlayerPawn) && IsPlayerLikelyMakingGunfireNoise(PrimaryPlayerPawn);
    if (bPlayerGunfireDetected)
    {
        TSet<int32> AlertRoomIds;
        for (const TPair<AActor*, int32>& EnemyPair : EnemyToRoomMap)
        {
            const APawn* EnemyPawn = Cast<APawn>(EnemyPair.Key);
            if (!IsValid(EnemyPawn) || EnemyPawn->IsPlayerControlled())
            {
                continue;
            }
            AlertRoomIds.Add(EnemyPair.Value);
        }

        for (const int32 AlertRoomId : AlertRoomIds)
        {
            RaiseRoomCombatAlert(AlertRoomId, NowSeconds);
        }
    }
    if (bBridgePlayerGunfireToAISenseHearing &&
        bPlayerGunfireDetected &&
        NowSeconds >= NextPlayerGunfireNoiseReportTimeSeconds)
    {
        UAISense_Hearing::ReportNoiseEvent(
            World,
            PrimaryPlayerPawn->GetActorLocation(),
            FMath::Max(0.05f, PlayerGunfireNoiseLoudness),
            PrimaryPlayerPawn,
            FMath::Max(500.0f, EnemyGunshotHearingDistance),
            FName(TEXT("RaidGunfire")));

        NextPlayerGunfireNoiseReportTimeSeconds =
            NowSeconds + FMath::Max(0.05f, PlayerGunfireNoiseReportInterval);
    }

    TickWaveSpawning(NowSeconds, PrimaryPlayerPawn);

    int32 FixedCount = 0;
    int32 RecoveredCount = 0;
    for (auto It = EnemyToRoomMap.CreateIterator(); It; ++It)
    {
        APawn* Pawn = Cast<APawn>(It.Key());
        if (!IsValid(Pawn))
        {
            It.RemoveCurrent();
            continue;
        }
        if (Pawn->IsPlayerControlled())
        {
            continue;
        }

        const int32 RoomId = It.Value();
        const TWeakObjectPtr<APawn> WeakPawn(Pawn);

        if (IsPawnDeadLike(Pawn))
        {
            DisableDeadEnemyDamageSources(Pawn, TEXT("EnforcerDeadPawn"));
            PendingDeadFinalizePawns.Add(Pawn);
            continue;
        }

        if (bDisableZombieGrab)
        {
            EnforceNoZombieGrabInternal(Pawn);
        }

        if (bEnableRoomEnemyTracking && RoomId == TrackedRoomId)
        {
            const FVector CurrentLoc = Pawn->GetActorLocation();
            const bool bHasPrevLoc = EnemyTrackedLastObservedLocationByPawn.Contains(WeakPawn);
            const FVector PrevLoc = bHasPrevLoc ? EnemyTrackedLastObservedLocationByPawn.FindRef(WeakPawn) : CurrentLoc;
            const float SuddenZDropThreshold = FMath::Max(20.0f, TrackedEnemySuddenZDropThreshold);
            const float DeltaZ = CurrentLoc.Z - PrevLoc.Z;
            if (bHasPrevLoc && DeltaZ <= -SuddenZDropThreshold)
            {
                LogTrackedEnemyState(Pawn, RoomId, TEXT("SuddenZDrop"));
            }

            const double PeriodicInterval = FMath::Max(0.10, (double)TrackedEnemyPeriodicLogInterval);
            const double NextPeriodicLogTime = EnemyTrackedNextPeriodicLogByPawn.FindRef(WeakPawn);
            if (NextPeriodicLogTime <= NowSeconds)
            {
                LogTrackedEnemyState(Pawn, RoomId, bHasPrevLoc ? TEXT("Periodic") : TEXT("FirstSeen"));
                EnemyTrackedNextPeriodicLogByPawn.Add(WeakPawn, NowSeconds + PeriodicInterval);
            }

            EnemyTrackedLastObservedLocationByPawn.Add(WeakPawn, CurrentLoc);
        }

        UpdateEnemySearchBehavior(Pawn, RoomId, NowSeconds, PrimaryPlayerPawn);

        bool bNeedsCull = false;
        if (RecoverEnemyIfOutOfWorld(Pawn, RoomId, bNeedsCull))
        {
            if (bNeedsCull)
            {
                PendingCullPawns.Add(Pawn);
                continue;
            }
            ++RecoveredCount;
        }

        bool bStuckNeedsCull = false;
        if (RecoverStuckEnemyIfBlocked(Pawn, RoomId, bStuckNeedsCull))
        {
            if (bStuckNeedsCull)
            {
                PendingCullPawns.Add(Pawn);
                continue;
            }
            ++RecoveredCount;
        }

        const bool bForceCollisionRefresh =
            (bEnableRoomEnemyTracking && RoomId == TrackedRoomId) ||
            EnemyUndergroundNextCheckByPawn.Contains(WeakPawn) ||
            EnemyStuckNextCheckByPawn.Contains(WeakPawn);
        if (!bForceCollisionRefresh)
        {
            if (const double* NextAllowedRefreshTime = EnemyTraceCollisionNextRefreshByPawn.Find(WeakPawn))
            {
                if (*NextAllowedRefreshTime > NowSeconds)
                {
                    continue;
                }
            }
        }

        ForceEnemyTraceCollision(Pawn);
        EnemyTraceCollisionNextRefreshByPawn.Add(WeakPawn, NowSeconds + FMath::Max(0.10f, EnemyTraceCollisionRefreshInterval));
        ++FixedCount;
    }

    for (const TWeakObjectPtr<APawn>& WeakPawn : PendingCullPawns)
    {
        if (APawn* PawnToCull = WeakPawn.Get())
        {
            if (const int32* RoomIdPtr = EnemyToRoomMap.Find(PawnToCull))
            {
                LogTrackedEnemyState(PawnToCull, *RoomIdPtr, TEXT("CullAfterRecoveryFailure"));
            }
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[RaidCombat] Removing unrecoverable enemy '%s' to unblock room clear."),
                *GetNameSafe(PawnToCull));
            PawnToCull->Destroy();
        }
    }

    for (const TWeakObjectPtr<APawn>& WeakDeadPawn : PendingDeadFinalizePawns)
    {
        if (APawn* DeadPawn = WeakDeadPawn.Get())
        {
            if (EnemyToRoomMap.Contains(DeadPawn))
            {
                OnEnemyDestroyed(DeadPawn);
            }
        }
    }

    for (auto RefreshIt = EnemyTraceCollisionNextRefreshByPawn.CreateIterator(); RefreshIt; ++RefreshIt)
    {
        if (!RefreshIt.Key().IsValid())
        {
            RefreshIt.RemoveCurrent();
        }
    }
    for (auto CheckIt = EnemyUndergroundNextCheckByPawn.CreateIterator(); CheckIt; ++CheckIt)
    {
        if (!CheckIt.Key().IsValid())
        {
            CheckIt.RemoveCurrent();
        }
    }
    for (auto FailureIt = EnemyUndergroundRecoveryFailuresByPawn.CreateIterator(); FailureIt; ++FailureIt)
    {
        if (!FailureIt.Key().IsValid())
        {
            FailureIt.RemoveCurrent();
        }
    }
    for (auto LastValidIt = EnemyLastKnownValidLocationByPawn.CreateIterator(); LastValidIt; ++LastValidIt)
    {
        if (!LastValidIt.Key().IsValid())
        {
            LastValidIt.RemoveCurrent();
        }
    }
    for (auto TrackedNextIt = EnemyTrackedNextPeriodicLogByPawn.CreateIterator(); TrackedNextIt; ++TrackedNextIt)
    {
        if (!TrackedNextIt.Key().IsValid())
        {
            TrackedNextIt.RemoveCurrent();
        }
    }
    for (auto TrackedLocIt = EnemyTrackedLastObservedLocationByPawn.CreateIterator(); TrackedLocIt; ++TrackedLocIt)
    {
        if (!TrackedLocIt.Key().IsValid())
        {
            TrackedLocIt.RemoveCurrent();
        }
    }
    for (auto SearchActivationIt = EnemySearchActivationTimeByPawn.CreateIterator(); SearchActivationIt; ++SearchActivationIt)
    {
        if (!SearchActivationIt.Key().IsValid())
        {
            SearchActivationIt.RemoveCurrent();
        }
    }
    for (auto SearchOrderIt = EnemySearchNextOrderTimeByPawn.CreateIterator(); SearchOrderIt; ++SearchOrderIt)
    {
        if (!SearchOrderIt.Key().IsValid())
        {
            SearchOrderIt.RemoveCurrent();
        }
    }
    for (auto StuckLocIt = EnemyStuckLastProgressLocationByPawn.CreateIterator(); StuckLocIt; ++StuckLocIt)
    {
        if (!StuckLocIt.Key().IsValid())
        {
            StuckLocIt.RemoveCurrent();
        }
    }
    for (auto StuckTimeIt = EnemyStuckLastProgressTimeByPawn.CreateIterator(); StuckTimeIt; ++StuckTimeIt)
    {
        if (!StuckTimeIt.Key().IsValid())
        {
            StuckTimeIt.RemoveCurrent();
        }
    }
    for (auto StuckCheckIt = EnemyStuckNextCheckByPawn.CreateIterator(); StuckCheckIt; ++StuckCheckIt)
    {
        if (!StuckCheckIt.Key().IsValid())
        {
            StuckCheckIt.RemoveCurrent();
        }
    }
    for (auto StuckFailIt = EnemyStuckRecoveryFailuresByPawn.CreateIterator(); StuckFailIt; ++StuckFailIt)
    {
        if (!StuckFailIt.Key().IsValid())
        {
            StuckFailIt.RemoveCurrent();
        }
    }

    // Reconcile AliveByRoomId with the actual tracked map to prevent stuck rooms when actors disappear unexpectedly.
    if (AliveByRoomId.Num() > 0)
    {
        TMap<int32, int32> ActualAliveByRoom;
        ActualAliveByRoom.Reserve(AliveByRoomId.Num());
        for (const TPair<AActor*, int32>& Pair : EnemyToRoomMap)
        {
            APawn* Pawn = Cast<APawn>(Pair.Key);
            if (Pair.Value >= 0 && IsValid(Pawn) && !IsPawnDeadLike(Pawn))
            {
                ActualAliveByRoom.FindOrAdd(Pair.Value)++;
            }
        }

        TArray<int32> RoomsToClear;
        for (auto AliveIt = AliveByRoomId.CreateIterator(); AliveIt; ++AliveIt)
        {
            const int32 RoomId = AliveIt.Key();
            if (RoomId < 0)
            {
                AliveIt.RemoveCurrent();
                continue;
            }
            const int32 RecordedAlive = AliveIt.Value();
            const int32 ActualAlive = ActualAliveByRoom.FindRef(RoomId);
            if (RecordedAlive != ActualAlive)
            {
                if (bEnableCombatPerfLogs || FMath::Abs(RecordedAlive - ActualAlive) >= 2)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[RaidCombat] Alive count reconciled for Room %d: recorded=%d actual=%d"),
                        RoomId,
                        RecordedAlive,
                        ActualAlive);
                }
                AliveIt.Value() = ActualAlive;
            }

            if (AliveIt.Value() <= 0)
            {
                RoomsToClear.Add(RoomId);
                AliveIt.RemoveCurrent();
            }
        }

        for (const int32 RoomId : RoomsToClear)
        {
            HandleRoomCleared(RoomId);
        }
    }

    if (bNeedFallbackSweep)
    {
        const double FallbackInterval = FMath::Max(0.50f, FallbackEnemyTraceSweepInterval);
        NextFallbackEnemySweepTimeSeconds = NowSeconds + FallbackInterval;

        for (TActorIterator<APawn> It(World); It; ++It)
        {
            APawn* Pawn = *It;
            if (!IsValid(Pawn) || Pawn->IsPlayerControlled())
            {
                continue;
            }
            if (EnemyToRoomMap.Contains(Pawn))
            {
                continue;
            }

            if (IsPawnDeadLike(Pawn))
            {
                DisableDeadEnemyDamageSources(Pawn, TEXT("FallbackSweepDeadPawn"));
                continue;
            }

            if (bDisableZombieGrab)
            {
                EnforceNoZombieGrabInternal(Pawn);
            }

            ForceEnemyTraceCollision(Pawn);
            ++FixedCount;
        }
    }

    static int32 SweepCounter = 0;
    ++SweepCounter;
    if (bEnableCombatPerfLogs && SweepCounter % 20 == 0 && (FixedCount > 0 || RecoveredCount > 0))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Trace-collision enforcer sweep updated %d AI pawns (recovered=%d)."),
            FixedCount,
            RecoveredCount);
    }
}

void URaidCombatSubsystem::SpawnEnemyControllerDeferred(APawn* SpawnedEnemy, int32 RoomId, const FString& SanitizedProfile, int32 SpawnOrderIndex)
{
    if (!IsValid(SpawnedEnemy))
    {
        return;
    }

    auto ApplyControllerTags = [&](AController* SpawnedController)
        {
            if (!IsValid(SpawnedController))
            {
                return;
            }

            SpawnedController->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidRoom_%d"), RoomId)));
            SpawnedController->Tags.AddUnique(FName(TEXT("Enemy")));
            SpawnedController->Tags.AddUnique(FName(TEXT("RaidEnemy")));
            if (!SanitizedProfile.IsEmpty())
            {
                SpawnedController->Tags.AddUnique(FName(*FString::Printf(TEXT("BotProfile_%s"), *SanitizedProfile)));
            }
        };

    UWorld* World = GetWorld();
    const double DelaySeconds = ResolveControllerSpawnDelaySeconds(SpawnedEnemy, RoomId, SpawnOrderIndex);
    if (!World || DelaySeconds <= KINDA_SMALL_NUMBER)
    {
        SpawnedEnemy->SpawnDefaultController();
        ForceEnemyTraceCollision(SpawnedEnemy);
        ApplyControllerTags(SpawnedEnemy->GetController());
        return;
    }

    const TWeakObjectPtr<APawn> WeakEnemy(SpawnedEnemy);
    const int32 SafeRoomId = RoomId;
    const FString SafeProfile = SanitizedProfile;
    FTimerHandle ControllerSpawnHandle;
    World->GetTimerManager().SetTimer(
        ControllerSpawnHandle,
        FTimerDelegate::CreateWeakLambda(this, [WeakEnemy, SafeRoomId, SafeProfile]()
            {
                APawn* Enemy = WeakEnemy.Get();
                if (!IsValid(Enemy))
                {
                    return;
                }

                Enemy->SpawnDefaultController();
                ForceEnemyTraceCollision(Enemy);

                if (AController* SpawnedController = Enemy->GetController())
                {
                    SpawnedController->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidRoom_%d"), SafeRoomId)));
                    SpawnedController->Tags.AddUnique(FName(TEXT("Enemy")));
                    SpawnedController->Tags.AddUnique(FName(TEXT("RaidEnemy")));
                    if (!SafeProfile.IsEmpty())
                    {
                        SpawnedController->Tags.AddUnique(FName(*FString::Printf(TEXT("BotProfile_%s"), *SafeProfile)));
                    }
                }
            }),
        DelaySeconds,
        false);
}

void URaidCombatSubsystem::SpawnEnemiesForRoom(ARaidRoomActor* Room)
{
    UWorld* World = GetWorld();
    if (!World || !Room)
    {
        return;
    }

    {
        const int32 RoomIdNew = Room->GetNodeId();
        const bool bBossRoomNew = Room->GetNodeRow().RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase);

        int32 SpawnedCountNew = 0;
        if (TArray<FRaidEnemySpawnPlanEntry>* CachedPlan = PrewarmedSpawnPlansByRoomId.Find(RoomIdNew))
        {
            SpawnedCountNew = ExecuteRoomSpawnPlan(Room, *CachedPlan, true);
            PrewarmedSpawnPlansByRoomId.Remove(RoomIdNew);
        }

        if (SpawnedCountNew <= 0)
        {
            TArray<FRaidEnemySpawnPlanEntry> RuntimePlan;
            if (BuildRoomSpawnPlan(Room, RuntimePlan, false, true))
            {
                SpawnedCountNew = ExecuteRoomSpawnPlan(Room, RuntimePlan, false);
            }
        }

        if (SpawnedCountNew > 0)
        {
            AliveByRoomId.Add(RoomIdNew, SpawnedCountNew);
            UE_LOG(LogTemp, Display, TEXT("[RaidCombat] %d enemies spawned in Room %d"), SpawnedCountNew, RoomIdNew);
            return;
        }

        if (bBossRoomNew)
        {
            UE_LOG(LogTemp, Error, TEXT("[RaidCombat] Boss room spawn failed in Room %d. Combat reset for retry."), RoomIdNew);
            Room->SetCombatStarted(false);
            return;
        }

        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] 0 enemies spawned in Room %d! Auto-clearing."), RoomIdNew);
        HandleRoomCleared(RoomIdNew);
        return;
    }

    const FLevelNodeRow& Row = Room->GetNodeRow();
    int32 Id = Room->GetNodeId();

    const URaidChapterConfig* Config = Room->GetChapterConfig();
    if (!Config || !Config->EnemyPresetRegistry)
    {
        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] ERROR: EnemyPresetRegistry is missing in DA_ChapterConfig!"));
        HandleRoomCleared(Id);
        return;
    }

    PrimeRuntimeAssets(Config);

    TArray<FName> PresetCandidates;
    BuildPresetCandidates(Row, PresetCandidates);

    FName EffectivePreset = NAME_None;
    for (const FName Candidate : PresetCandidates)
    {
        FRaidEnemyPreset FoundPreset;
        if (Config->EnemyPresetRegistry->ResolvePreset(Candidate, FoundPreset) && FoundPreset.IsValid())
        {
            EffectivePreset = Candidate;
            break;
        }
    }

    if (EffectivePreset.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] ERROR: No valid enemy preset found for Room %d (requested='%s')."),
            Id, *Row.EnemyPreset);
        HandleRoomCleared(Id);
        return;
    }

    FRandomStream Stream(Row.Seed ^ (Id * 7919));

    const bool bBossRoom = Row.RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase);
    const bool bCombatLike = Row.RoomType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase) || Row.RoomType.Equals(TEXT("Normal"), ESearchCase::IgnoreCase) || bBossRoom;
    const float SafeDifficulty = FMath::Clamp(Row.Difficulty > 0.01f ? Row.Difficulty : 1.0f, 0.6f, 3.0f);
    const float SafeCombatWeight = FMath::Clamp(Row.CombatWeight > 0.01f ? Row.CombatWeight : 1.0f, 0.6f, 2.6f);
    const int32 BaseSpawnCount = Row.SpawnCount > 0 ? Row.SpawnCount : (bBossRoom ? 3 : 3);
    const FString BotProfile = Row.BotProfile.TrimStartAndEnd();

    int32 FinalSpawnCount = FMath::RoundToInt((float)BaseSpawnCount * (0.75f + SafeDifficulty * 0.45f) * (0.70f + SafeCombatWeight * 0.35f));
    if (bBossRoom) FinalSpawnCount = FMath::Max(2, FinalSpawnCount); // boss + helper
    else if (bCombatLike) FinalSpawnCount = FMath::Max(2, FinalSpawnCount);
    else FinalSpawnCount = FMath::Max(1, FinalSpawnCount);
    FinalSpawnCount = FMath::Clamp(FinalSpawnCount, 1, FMath::Max(1, MaxEnemiesPerRoom));

    int32 SpawnedCount = 0;
    FVector Center = Room->GetActorLocation();
    float SpawnRadius = (Room->GridSize * Room->TileSize) / 2.0f - 200.0f;
    float AngleStep = 360.0f / FinalSpawnCount;
    APawn* PrimaryPlayerPawn = GetPrimaryPlayerPawn();

    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);

    for (int32 i = 0; i < FinalSpawnCount; ++i)
    {
        FName SpawnPreset = EffectivePreset;
        // Boss room composition: first unit from boss preset, others from helper presets.
        if (bBossRoom && i > 0)
        {
            TArray<FName> BossHelperCandidates;
            BossHelperCandidates.Add(TEXT("Raider"));
            BossHelperCandidates.Add(TEXT("Scavenger"));
            BossHelperCandidates.Add(TEXT("Sniper"));
            BossHelperCandidates.Add(TEXT("Default"));
            for (const FName HelperPreset : BossHelperCandidates)
            {
                FRaidEnemyPreset FoundPreset;
                if (Config->EnemyPresetRegistry->ResolvePreset(HelperPreset, FoundPreset) && FoundPreset.IsValid())
                {
                    SpawnPreset = HelperPreset;
                    break;
                }
            }
        }

        TSubclassOf<APawn> EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(SpawnPreset);
        if (!EnemyClass)
        {
            // 2차 폴백: 후보 프리셋 순차 재시도
            for (const FName Candidate : PresetCandidates)
            {
                if (Candidate == SpawnPreset) continue;
                EnemyClass = Config->EnemyPresetRegistry->ResolveEnemyClassFromPreset(Candidate);
                if (EnemyClass)
                {
                    SpawnPreset = Candidate;
                    break;
                }
            }
            if (!EnemyClass)
            {
                UE_LOG(LogTemp, Error, TEXT("[RaidCombat] ERROR: Failed to resolve EnemyClass for all preset candidates in Room %d."), Id);
                continue;
            }
        }

        float CapsuleRadius = 42.0f;
        float CapsuleHalfHeight = 88.0f;
        ResolvePawnCapsuleSize(EnemyClass, CapsuleRadius, CapsuleHalfHeight);

        float BaseAngle = i * AngleStep;
        float RandomAngle = BaseAngle + Stream.FRandRange(-25.0f, 25.0f);
        float MinSpawnDistance = 300.0f;
        float MaxSpawnDistance = SpawnRadius;
        if (BotProfile.Equals(TEXT("Defensive"), ESearchCase::IgnoreCase))
        {
            MinSpawnDistance = FMath::Max(300.0f, SpawnRadius * 0.55f);
        }
        else if (BotProfile.Equals(TEXT("Tactical"), ESearchCase::IgnoreCase))
        {
            MinSpawnDistance = FMath::Max(300.0f, SpawnRadius * 0.42f);
            MaxSpawnDistance = FMath::Max(MinSpawnDistance + 50.0f, SpawnRadius * 0.88f);
        }
        else if (BotProfile.Equals(TEXT("Aggressive"), ESearchCase::IgnoreCase))
        {
            MaxSpawnDistance = FMath::Max(350.0f, SpawnRadius * 0.70f);
        }

        if (SpawnPreset == TEXT("Sniper"))
        {
            MinSpawnDistance = FMath::Max(MinSpawnDistance, SpawnRadius * 0.60f);
        }

        MaxSpawnDistance = FMath::Max(MinSpawnDistance + 50.0f, MaxSpawnDistance);
        float Distance = Stream.FRandRange(MinSpawnDistance, MaxSpawnDistance);

        FVector FinalSpawnLoc = FVector::ZeroVector;
        bool bFoundSafeSpawn = false;
        constexpr int32 MaxSpawnLocationAttempts = 10;

        for (int32 Attempt = 0; Attempt < MaxSpawnLocationAttempts && !bFoundSafeSpawn; ++Attempt)
        {
            const float CandidateAngle = RandomAngle + ((Attempt == 0) ? 0.0f : Stream.FRandRange(-95.0f, 95.0f));
            const float CandidateDistance = (Attempt == 0) ? Distance : Stream.FRandRange(MinSpawnDistance, MaxSpawnDistance);
            const float Radian = FMath::DegreesToRadians(CandidateAngle);
            const FVector Offset(FMath::Cos(Radian) * CandidateDistance, FMath::Sin(Radian) * CandidateDistance, 0.0f);
            const FVector XYCandidate = Center + Offset;

            FVector CandidateSpawnLoc = FVector::ZeroVector;
            if (!TryResolveSafeAIPawnSpawnLocation(World, Room, NavSys, XYCandidate, CapsuleRadius, CapsuleHalfHeight, CandidateSpawnLoc))
            {
                continue;
            }

            FinalSpawnLoc = CandidateSpawnLoc;
            bFoundSafeSpawn = true;
        }

        if (!bFoundSafeSpawn)
        {
            FVector RecoveryLoc = FVector::ZeroVector;
            if (TryResolveNearbyFallbackSpawnLocation(World, Room, NavSys, Center, CapsuleRadius, CapsuleHalfHeight, Stream, RecoveryLoc))
            {
                FinalSpawnLoc = RecoveryLoc;
                bFoundSafeSpawn = true;
            }
        }

        if (!bFoundSafeSpawn)
        {
            continue;
        }

        FRotator RandomRotation(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;

        if (APawn* SpawnedEnemy = World->SpawnActor<APawn>(EnemyClass, FinalSpawnLoc, RandomRotation, SpawnParams))
        {
            FVector EffectiveSpawnLoc = SpawnedEnemy->GetActorLocation();
            if (IsCapsuleBlockedForPawn(World, EffectiveSpawnLoc, CapsuleRadius, CapsuleHalfHeight, SpawnedEnemy) ||
                IsNearRoomObstacle(World, Room, EffectiveSpawnLoc, CapsuleRadius + 70.0f))
            {
                FVector RecoveryLoc = FVector::ZeroVector;
                if (TryResolveNearbyFallbackSpawnLocation(World, Room, NavSys, EffectiveSpawnLoc, CapsuleRadius, CapsuleHalfHeight, Stream, RecoveryLoc))
                {
                    SpawnedEnemy->SetActorLocation(RecoveryLoc, false, nullptr, ETeleportType::TeleportPhysics);
                    EffectiveSpawnLoc = RecoveryLoc;
                }
                else
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[RaidCombat] Rejected blocked spawn for '%s' in Room %d at %s"),
                        *GetNameSafe(EnemyClass),
                        Id,
                        *SpawnedEnemy->GetActorLocation().ToCompactString());
                    SpawnedEnemy->Destroy();
                    continue;
                }
            }

            FString RepairLog;
            const bool bRepaired = Config->EnemyPresetRegistry->TryRepairSpawnedPawn(SpawnedEnemy, RepairLog);
            if (bRepaired)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Auto-repaired spawned enemy '%s': %s"),
                    *GetNameSafe(SpawnedEnemy->GetClass()), *RepairLog);
            }

            // Some AI blueprints/plugins override collision right after spawn.
            // Re-apply trace collision immediately and with short delayed retries.
            ForceEnemyTraceCollision(SpawnedEnemy);

            FString SanitizedProfile = BotProfile;
            SanitizedProfile.ReplaceInline(TEXT(" "), TEXT(""));
            SpawnEnemyControllerDeferred(SpawnedEnemy, Id, SanitizedProfile, i);
            if (bEnableCombatPerfLogs)
            {
                LogEnemyTraceCollisionSnapshot(SpawnedEnemy);
            }

            if (World)
            {
                TWeakObjectPtr<APawn> EnemyWeak = SpawnedEnemy;
                for (const float DelaySeconds : { 0.05f, 0.20f, 0.60f })
                {
                    FTimerHandle RetryHandle;
                    World->GetTimerManager().SetTimer(
                        RetryHandle,
                        FTimerDelegate::CreateWeakLambda(this, [EnemyWeak]()
                            {
                                if (APawn* EnemyStrong = EnemyWeak.Get())
                                {
                                    ForceEnemyTraceCollision(EnemyStrong);
                                }
                            }),
                        DelaySeconds,
                        false);
                }
            }

            SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("RaidRoom_%d"), Id)));
            SpawnedEnemy->Tags.AddUnique(FName(TEXT("Enemy")));
            SpawnedEnemy->Tags.AddUnique(FName(TEXT("RaidEnemy")));
            if (!SanitizedProfile.IsEmpty())
            {
                SpawnedEnemy->Tags.AddUnique(FName(*FString::Printf(TEXT("BotProfile_%s"), *SanitizedProfile)));
            }

            SpawnedCount++;
            EnemyToRoomMap.Add(SpawnedEnemy, Id);
            const TWeakObjectPtr<APawn> WeakSpawnedEnemy(SpawnedEnemy);
            const FVector SpawnedLoc = SpawnedEnemy->GetActorLocation();
            const double NowSeconds = World ? World->GetTimeSeconds() : 0.0;
            const double RecoveryGraceUntil = NowSeconds + FMath::Max(0.0f, (double)EnemyRecoverySpawnGraceSeconds);
            const double ActivationTime = ResolveEnemySearchActivationTime(Id, SpawnedLoc, NowSeconds, PrimaryPlayerPawn);
            EnemyLastKnownValidLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
            EnemyStuckLastProgressLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
            EnemyStuckLastProgressTimeByPawn.Add(WeakSpawnedEnemy, NowSeconds);
            EnemyUndergroundNextCheckByPawn.Add(WeakSpawnedEnemy, RecoveryGraceUntil);
            EnemyStuckNextCheckByPawn.Add(WeakSpawnedEnemy, RecoveryGraceUntil);
            EnemyStuckRecoveryFailuresByPawn.Remove(WeakSpawnedEnemy);
            EnemyTrackedLastObservedLocationByPawn.Add(WeakSpawnedEnemy, SpawnedLoc);
            EnemyTrackedNextPeriodicLogByPawn.Remove(WeakSpawnedEnemy);
            EnemySearchActivationTimeByPawn.Add(WeakSpawnedEnemy, ActivationTime);
            EnemySearchNextOrderTimeByPawn.Add(WeakSpawnedEnemy, ActivationTime + FMath::FRandRange(0.08, 0.55));
            SpawnedEnemy->OnDestroyed.AddDynamic(this, &URaidCombatSubsystem::OnEnemyDestroyed);
            RaiseRoomCombatAlert(Id, NowSeconds);
            LogTrackedEnemyState(SpawnedEnemy, Id, TEXT("Spawned"));
        }
    }

    if (SpawnedCount > 0)
    {
        AliveByRoomId.Add(Id, SpawnedCount);
        UE_LOG(LogTemp, Display, TEXT("[RaidCombat] %d Enemies spawned successfully in Room %d"), SpawnedCount, Id);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[RaidCombat] 0 enemies spawned in Room %d! Auto-clearing."), Id);
        HandleRoomCleared(Id);
    }
}

void URaidCombatSubsystem::OnEnemyDestroyed(AActor* DestroyedActor)
{
    if (bInternalClearing) return;
    if (!IsValid(DestroyedActor)) return;

    for (auto It = ProcessedEnemyDeathActors.CreateIterator(); It; ++It)
    {
        if (!(*It).IsValid())
        {
            It.RemoveCurrent();
        }
    }

    const TWeakObjectPtr<AActor> WeakDestroyedActor(DestroyedActor);
    const FString DestroyedActorKey = DestroyedActor->GetPathName();
    if (ProcessedEnemyDeathActors.Contains(WeakDestroyedActor))
    {
        return;
    }
    if (!DestroyedActorKey.IsEmpty() && ProcessedEnemyDeathKeys.Contains(DestroyedActorKey))
    {
        return;
    }

    ProcessedEnemyDeathActors.Add(WeakDestroyedActor);
    if (!DestroyedActorKey.IsEmpty())
    {
        ProcessedEnemyDeathKeys.Add(DestroyedActorKey);
    }

    if (APawn* DestroyedPawn = Cast<APawn>(DestroyedActor))
    {
        DisableDeadEnemyDamageSources(DestroyedPawn, TEXT("OnEnemyDestroyed"));
        const TWeakObjectPtr<APawn> WeakDestroyedPawn(DestroyedPawn);
        EnemyTraceCollisionNextRefreshByPawn.Remove(WeakDestroyedPawn);
        EnemyUndergroundNextCheckByPawn.Remove(WeakDestroyedPawn);
        EnemyUndergroundRecoveryFailuresByPawn.Remove(WeakDestroyedPawn);
        EnemyLastKnownValidLocationByPawn.Remove(WeakDestroyedPawn);
        EnemyStuckLastProgressLocationByPawn.Remove(WeakDestroyedPawn);
        EnemyStuckLastProgressTimeByPawn.Remove(WeakDestroyedPawn);
        EnemyStuckNextCheckByPawn.Remove(WeakDestroyedPawn);
        EnemyStuckRecoveryFailuresByPawn.Remove(WeakDestroyedPawn);
        EnemyTrackedNextPeriodicLogByPawn.Remove(WeakDestroyedPawn);
        EnemyTrackedLastObservedLocationByPawn.Remove(WeakDestroyedPawn);
        EnemySearchActivationTimeByPawn.Remove(WeakDestroyedPawn);
        EnemySearchNextOrderTimeByPawn.Remove(WeakDestroyedPawn);
    }

    if (int32* RoomIdPtr = EnemyToRoomMap.Find(DestroyedActor))
    {
        const int32 RId = *RoomIdPtr;
        const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        RaiseRoomCombatAlert(RId, NowSeconds);
        if (IsValid(DestroyedActor))
        {
            if (bLogDropSoulLifecycle)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[RaidCombat][DropSoul] Death event Enemy='%s' Room=%d Loc=%s"),
                    *GetNameSafe(DestroyedActor),
                    RId,
                    *DestroyedActor->GetActorLocation().ToCompactString());
            }
            SpawnOrRepairDropSoulAt(DestroyedActor->GetActorLocation(), RId);
        }

        EnemyToRoomMap.Remove(DestroyedActor);

        int32 WaveNumber = 0;
        if (TryDecodeWaveRoomId(RId, WaveNumber))
        {
            if (AliveWaveEnemyCount > 0)
            {
                --AliveWaveEnemyCount;
            }
            return;
        }

        if (int32* AlivePtr = AliveByRoomId.Find(RId))
        {
            (*AlivePtr)--;
            if (*AlivePtr <= 0)
            {
                AliveByRoomId.Remove(RId);
                HandleRoomCleared(RId);
            }
        }
    }
}

void URaidCombatSubsystem::HandleRoomCleared(int32 RoomId)
{
    if (TObjectPtr<ARaidRoomActor>* RoomPtr = RoomById.Find(RoomId))
    {
        if (ARaidRoomActor* Room = RoomPtr->Get())
        {
            bInternalClearing = true;
            Room->SetCombatCleared(true);
            bInternalClearing = false;

            if (Room->GetNodeRow().RoomType.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
            {
                Room->InternalSpawnLoot();
            }

            UpdateCompassForNextRooms(Room);
            DropSoulSpawnCountByRoom.Remove(RoomId);
            PrepareUpcomingRoomSpawnPlans(Room);
        }
    }
}
