#include "Raid/RaidCombatSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/DecalComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace RaidCombatTraceSanitizePrivate
{
    const FName RaidTraceSanitizedTag(TEXT("RaidTraceSanitized"));

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

    bool IsWaterHit(const FHitResult& Hit)
    {
        if (AActor* HitActor = Hit.GetActor())
        {
            if (HitActor->ActorHasTag(TEXT("Water")))
            {
                return true;
            }

            const FString ActorClass = HitActor->GetClass()->GetName();
            if (ActorClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        if (UPrimitiveComponent* HitComp = Hit.GetComponent())
        {
            if (HitComp->ComponentHasTag(TEXT("Water")))
            {
                return true;
            }

            const FString CompClass = HitComp->GetClass()->GetName();
            if (CompClass.Contains(TEXT("Water"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    bool IsVisualOnlyTraceHelperComponent(const UPrimitiveComponent* Primitive)
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
} // namespace RaidCombatTraceSanitizePrivate

namespace RaidCombatTraceSanitizeAlias = RaidCombatTraceSanitizePrivate;

void URaidCombatSubsystem::PatchBloodEffectTraceSettings()
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        return;
    }

    const ETraceTypeQuery DesiredTraceType = UEngineTypes::ConvertToTraceType(ECC_WorldStatic);
    int32 PatchedObjectCount = 0;
    int32 PatchedCollisionComponentCount = 0;
    bool bObservedBloodObjects = false;

    static const TCHAR* BloodClassPaths[] =
    {
        TEXT("/Game/AdvancedLocomotionV4/Blood_VFX/BP_BloodDrops.BP_BloodDrops_C"),
        TEXT("/Game/AdvancedLocomotionV4/Blood_VFX/AC_BloodEffect.AC_BloodEffect_C")
    };

    for (const TCHAR* ClassPath : BloodClassPaths)
    {
        const FSoftClassPath SoftClassPath(ClassPath);
        if (UClass* LoadedClass = SoftClassPath.TryLoadClass<UObject>())
        {
            bObservedBloodObjects = true;
            if (UObject* DefaultObject = LoadedClass->GetDefaultObject())
            {
                if (RaidCombatTraceSanitizeAlias::PatchBloodTraceChannelProperty(DefaultObject, DesiredTraceType))
                {
                    ++PatchedObjectCount;
                }
                if (RaidCombatTraceSanitizeAlias::PatchBloodPrimitiveCollision(DefaultObject))
                {
                    ++PatchedCollisionComponentCount;
                }
            }
        }
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Candidate = *It;
        if (!IsValid(Candidate))
        {
            continue;
        }
        if (RaidCombatTraceSanitizeAlias::IsDropSoulObject(Candidate))
        {
            continue;
        }

        bObservedBloodObjects |= RaidCombatTraceSanitizeAlias::LooksLikeBloodEffectObject(Candidate);
        if (RaidCombatTraceSanitizeAlias::PatchBloodTraceChannelProperty(Candidate, DesiredTraceType))
        {
            ++PatchedObjectCount;
        }
        if (RaidCombatTraceSanitizeAlias::PatchBloodPrimitiveCollision(Candidate))
        {
            ++PatchedCollisionComponentCount;
        }

        TInlineComponentArray<UActorComponent*> Components;
        Candidate->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (RaidCombatTraceSanitizeAlias::IsDropSoulObject(Component) || RaidCombatTraceSanitizeAlias::IsDropSoulObject(Component ? Component->GetOwner() : nullptr))
            {
                continue;
            }
            bObservedBloodObjects |= RaidCombatTraceSanitizeAlias::LooksLikeBloodEffectObject(Component);
            if (RaidCombatTraceSanitizeAlias::PatchBloodTraceChannelProperty(Component, DesiredTraceType))
            {
                ++PatchedObjectCount;
            }
            if (RaidCombatTraceSanitizeAlias::PatchBloodPrimitiveCollision(Component))
            {
                ++PatchedCollisionComponentCount;
            }
        }
    }

    if (bEnableCombatPerfLogs && (PatchedObjectCount > 0 || PatchedCollisionComponentCount > 0))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Blood VFX patch applied. TraceChannelPatched=%d PrimitiveNoCollision=%d"),
            PatchedObjectCount,
            PatchedCollisionComponentCount);
    }

    bBloodTraceSettingsPatched = bObservedBloodObjects || (PatchedObjectCount > 0);
}

void URaidCombatSubsystem::CleanupFloatingBloodDecals()
{
    if (!bEnableBloodDecalCleanup)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        StopTraceCollisionEnforcer();
        return;
    }

    const APawn* PlayerPawn = GetPrimaryPlayerPawn();
    const bool bHasPlayer = IsValid(PlayerPawn);
    const FVector PlayerLocation = bHasPlayer ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    const float MaxPlayerDistanceSq = FMath::Square(FMath::Max(500.0f, BloodDecalCleanupMaxDistanceFromPlayer));
    const int32 MaxToEvaluate = FMath::Max(1, MaxBloodDecalsEvaluatedPerSweep);
    const int32 MaxToDestroy = FMath::Max(1, MaxBloodDecalsDestroyedPerSweep);
    const int32 MaxToScan = FMath::Max(16, MaxBloodDecalComponentsScannedPerSweep);

    int32 RemovedDecalCount = 0;
    int32 EvaluatedDecalCount = 0;
    int32 ScannedDecalCount = 0;
    FCollisionObjectQueryParams ObjQuery;
    ObjQuery.AddObjectTypesToQuery(ECC_WorldStatic);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RaidBloodDecalSurfaceCheck), false);
    QueryParams.bTraceComplex = false;
    TArray<TWeakObjectPtr<UDecalComponent>> DecalsToDestroy;
    DecalsToDestroy.Reserve(32);

    for (TObjectIterator<UDecalComponent> It; It; ++It)
    {
        ++ScannedDecalCount;
        if (ScannedDecalCount > MaxToScan)
        {
            break;
        }

        if (EvaluatedDecalCount >= MaxToEvaluate || DecalsToDestroy.Num() >= MaxToDestroy)
        {
            break;
        }

        UDecalComponent* DecalComp = *It;
        if (!IsValid(DecalComp) || DecalComp->GetWorld() != World || !DecalComp->IsRegistered())
        {
            continue;
        }
        if (RaidCombatTraceSanitizeAlias::IsDropSoulObject(DecalComp) || RaidCombatTraceSanitizeAlias::IsDropSoulObject(DecalComp->GetOwner()))
        {
            continue;
        }

        const bool bBloodLikeDecal =
            RaidCombatTraceSanitizeAlias::IsBloodDecalMaterial(DecalComp->GetDecalMaterial()) ||
            RaidCombatTraceSanitizeAlias::LooksLikeBloodEffectObject(DecalComp) ||
            RaidCombatTraceSanitizeAlias::LooksLikeBloodEffectObject(DecalComp->GetOwner());
        if (!bBloodLikeDecal)
        {
            continue;
        }

        const FVector DecalLocation = DecalComp->GetComponentLocation();
        if (bHasPlayer && FVector::DistSquared(DecalLocation, PlayerLocation) > MaxPlayerDistanceSq)
        {
            continue;
        }

        ++EvaluatedDecalCount;
        const bool bNearStaticSurface = World->OverlapAnyTestByObjectType(
            DecalLocation,
            FQuat::Identity,
            ObjQuery,
            FCollisionShape::MakeSphere(60.0f),
            QueryParams);
        const bool bHasReliableSurface = RaidCombatTraceSanitizeAlias::HasReliableBloodSurfaceSupport(World, DecalLocation, DecalComp->GetComponentQuat(), QueryParams);
        const bool bTransientBloodOwner =
            RaidCombatTraceSanitizeAlias::LooksLikeBloodEffectObject(DecalComp->GetOwner()) ||
            RaidCombatTraceSanitizeAlias::ContainsAnyToken(DecalComp->GetPathName(), { TEXT("/Blood_VFX/"), TEXT("/NiagaraBloodFX/") });
        if (bTransientBloodOwner || !bNearStaticSurface || !bHasReliableSurface)
        {
            DecalsToDestroy.Add(DecalComp);
        }
    }

    for (const TWeakObjectPtr<UDecalComponent>& WeakDecal : DecalsToDestroy)
    {
        if (UDecalComponent* DecalComp = WeakDecal.Get())
        {
            DecalComp->DestroyComponent();
            ++RemovedDecalCount;
        }
    }

    if (bEnableCombatPerfLogs && RemovedDecalCount > 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Removed floating blood decals. Decals=%d Evaluated=%d Scanned=%d"),
            RemovedDecalCount,
            EvaluatedDecalCount,
            ScannedDecalCount);
    }
}

void URaidCombatSubsystem::SanitizeProceduralFoliageCollisionForTraces()
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        return;
    }

    PatchBloodEffectTraceSettings();

    ++FoliageSanitizeRetryCount;
    const int32 MaxRetries = FMath::Max(1, MaxFoliageSanitizeRetryCount);
    if (FoliageSanitizeRetryCount > MaxRetries)
    {
        FoliageSanitizeRetryCount = MaxRetries;
    }

    int32 PatchedComponentCount = 0;
    int32 PatchedActorCount = 0;
    int32 DisabledQueryCollisionCount = 0;
    int32 ScannedActorCount = 0;
    bool bHitPatchBudget = false;
    const int32 MaxActorsToScan = FMath::Max(8, MaxTraceBlockerActorsScannedPerSweep);
    const int32 MaxComponentsToPatch = FMath::Max(1, MaxTraceBlockerComponentsPatchedPerSweep);
    const APawn* PlayerPawn = GetPrimaryPlayerPawn();
    const bool bHasPlayer = IsValid(PlayerPawn);
    const FVector PlayerLocation = bHasPlayer ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
    const float MaxPlayerDistanceSq = FMath::Square(FMath::Max(1000.0f, FoliageSanitizeMaxDistanceFromPlayer));
    TArray<FString> PatchedSamples;
    PatchedSamples.Reserve(8);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (ScannedActorCount >= MaxActorsToScan || PatchedComponentCount >= MaxComponentsToPatch)
        {
            bHitPatchBudget = (PatchedComponentCount >= MaxComponentsToPatch);
            break;
        }

        ++ScannedActorCount;
        AActor* Candidate = *It;
        if (!IsValid(Candidate))
        {
            continue;
        }

        if (bHasPlayer && FVector::DistSquared2D(Candidate->GetActorLocation(), PlayerLocation) > MaxPlayerDistanceSq)
        {
            // Keep water-like actors exempt from distance culling because they can span very large bounds.
            if (!RaidCombatTraceSanitizeAlias::IsWaterLikeActor(Candidate))
            {
                continue;
            }
        }

        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
        Candidate->GetComponents(PrimitiveComponents);
        bool bActorPatched = false;
        for (UPrimitiveComponent* Primitive : PrimitiveComponents)
        {
            if (PatchedComponentCount >= MaxComponentsToPatch)
            {
                bHitPatchBudget = true;
                break;
            }

            if (!IsValid(Primitive))
            {
                continue;
            }

            if (!RaidCombatTraceSanitizeAlias::ShouldSanitizeTraceBlocker(Candidate, Primitive))
            {
                continue;
            }

            if (Primitive->ComponentHasTag(RaidCombatTraceSanitizeAlias::RaidTraceSanitizedTag))
            {
                continue;
            }

            if (RaidCombatTraceSanitizeAlias::ShouldDisableQueryCollisionForTraceBlocker(Candidate, Primitive))
            {
                // Non-gameplay blockers (grass/PFS volume brushes/water surfaces) should never absorb bullet traces.
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
                Primitive->SetGenerateOverlapEvents(false);
                ++DisabledQueryCollisionCount;
            }
            else
            {
                // Keep physical blockers available for movement, but make them transparent for weapon traces.
                Primitive->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
                Primitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
                Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
                Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
                RaidCombatTraceSanitizeAlias::ForceAllGameTraceChannelsToIgnore(Primitive);
            }

            Primitive->ComponentTags.AddUnique(RaidCombatTraceSanitizeAlias::RaidTraceSanitizedTag);
            ++PatchedComponentCount;
            bActorPatched = true;

            if (PatchedSamples.Num() < 8)
            {
                PatchedSamples.Add(FString::Printf(TEXT("%s.%s"), *GetNameSafe(Candidate), *GetNameSafe(Primitive)));
            }
        }

        if (bActorPatched)
        {
            ++PatchedActorCount;
        }

        if (bHitPatchBudget)
        {
            break;
        }
    }

    const bool bPatchedAny = (PatchedComponentCount > 0);
    const bool bWarmupComplete = (FoliageSanitizeRetryCount >= MaxRetries);
    bFoliageTraceCollisionSanitized = bWarmupComplete;

    const float WarmupInterval = FMath::Max(0.2f, FoliageSanitizeRetryInterval);
    const float MonitorInterval = FMath::Max(WarmupInterval, FoliageSanitizeMonitorInterval);
    NextFoliageSanitizeRetryTimeSeconds = World->GetTimeSeconds() + (bWarmupComplete ? MonitorInterval : WarmupInterval);

    const bool bShouldLogSummary =
        bEnableCombatPerfLogs &&
        (bPatchedAny ||
            bHitPatchBudget ||
            (FoliageSanitizeRetryCount <= 2) ||
            (!bWarmupComplete && (FoliageSanitizeRetryCount % 5 == 0)));
    if (bShouldLogSummary)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidCombat] Foliage trace-collision sanitize completed. Sweep %d/%d, patched components: %d, disabled-query: %d, actors: %d scanned: %d%s%s"),
            FoliageSanitizeRetryCount,
            MaxRetries,
            PatchedComponentCount,
            DisabledQueryCollisionCount,
            PatchedActorCount,
            ScannedActorCount,
            bHitPatchBudget ? TEXT(" (patch-budget capped)") : TEXT(""),
            bWarmupComplete
                ? TEXT(" (warmup complete; monitor mode)")
                : (bPatchedAny ? TEXT(" (patched)") : TEXT(" (warmup)")));
    }

    if (PatchedSamples.Num() > 0 && bShouldLogSummary)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RaidCombat] Trace sanitize sample targets: %s"), *FString::Join(PatchedSamples, TEXT(", ")));
    }
}
