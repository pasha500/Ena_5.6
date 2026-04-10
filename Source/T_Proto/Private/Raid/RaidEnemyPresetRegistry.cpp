#include "Raid/RaidEnemyPresetRegistry.h"
#include "Misc/Optional.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

namespace
{
    const FRaidEnemyPreset* FindPresetCaseInsensitive(const TMap<FName, FRaidEnemyPreset>& Presets, FName PresetId)
    {
        if (const FRaidEnemyPreset* Exact = Presets.Find(PresetId))
        {
            return Exact;
        }

        const FString Requested = PresetId.ToString().TrimStartAndEnd();
        if (Requested.IsEmpty())
        {
            return nullptr;
        }

        auto FindByString = [&](const FString& InName) -> const FRaidEnemyPreset*
            {
                for (const TPair<FName, FRaidEnemyPreset>& Pair : Presets)
                {
                    if (Pair.Key.ToString().Equals(InName, ESearchCase::IgnoreCase))
                    {
                        return &Pair.Value;
                    }
                }
                return nullptr;
            };

        if (const FRaidEnemyPreset* Found = FindByString(Requested))
        {
            return Found;
        }

        if (Requested.Equals(TEXT("Normal"), ESearchCase::IgnoreCase) ||
            Requested.Equals(TEXT("DefaultAI"), ESearchCase::IgnoreCase))
        {
            if (const FRaidEnemyPreset* Fallback = FindByString(TEXT("Default")))
            {
                return Fallback;
            }
        }

        if (Requested.Equals(TEXT("Boss"), ESearchCase::IgnoreCase))
        {
            if (const FRaidEnemyPreset* Fallback = FindByString(TEXT("BossGuard")))
            {
                return Fallback;
            }
        }

        return nullptr;
    }
}

bool URaidEnemyPresetRegistry::ResolvePreset(FName PresetId, FRaidEnemyPreset& OutPreset) const
{
    if (const FRaidEnemyPreset* Found = FindPresetCaseInsensitive(Presets, PresetId))
    {
        OutPreset = *Found;
        return true;
    }
    return false;
}

void URaidEnemyPresetRegistry::PreloadAllEnemyPresetClasses(bool bLogSummary) const
{
    int32 LoadedCount = 0;
    int32 FailedCount = 0;

    for (const TPair<FName, FRaidEnemyPreset>& Pair : Presets)
    {
        const FRaidEnemyPreset& Preset = Pair.Value;
        for (const TSoftClassPtr<APawn>& SoftEnemyClass : Preset.EnemyClasses)
        {
            if (SoftEnemyClass.IsNull())
            {
                continue;
            }

            UClass* LoadedClass = SoftEnemyClass.LoadSynchronous();
            if (LoadedClass && !LoadedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
            {
                // Touch CDO once so class defaults/components are initialized outside combat time.
                LoadedClass->GetDefaultObject();
                ++LoadedCount;
            }
            else
            {
                ++FailedCount;
            }
        }
    }

    if (!RepairFallbackSkeletalMesh.IsNull())
    {
        if (RepairFallbackSkeletalMesh.LoadSynchronous())
        {
            ++LoadedCount;
        }
        else
        {
            ++FailedCount;
        }
    }

    if (!RepairFallbackAnimClass.IsNull())
    {
        if (RepairFallbackAnimClass.LoadSynchronous())
        {
            ++LoadedCount;
        }
        else
        {
            ++FailedCount;
        }
    }

    if (bLogSummary)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RaidEnemyPresetRegistry] Preload complete. Loaded=%d Failed=%d"),
            LoadedCount,
            FailedCount);
    }
}

TSubclassOf<APawn> URaidEnemyPresetRegistry::ResolveEnemyClassFromPreset(FName PresetId) const
{
    FRandomStream Rng((int32)(GetTypeHash(PresetId) ^ FMath::Rand()));

    auto IsCandidateSafe = [](UClass* EnemyClass) -> bool
    {
        if (!EnemyClass) return false;
        if (EnemyClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) return false;

        const APawn* PawnCDO = Cast<APawn>(EnemyClass->GetDefaultObject());
        return PawnCDO != nullptr;
    };

    auto PickSafeClassFromPreset = [&](const FRaidEnemyPreset* Preset) -> TSubclassOf<APawn>
        {
            if (!Preset || !Preset->IsValid()) return nullptr;

            const int32 Num = Preset->EnemyClasses.Num();
            TArray<int32> Remaining;
            Remaining.Reserve(Num);
            for (int32 Idx = 0; Idx < Num; ++Idx)
            {
                Remaining.Add(Idx);
            }

            while (Remaining.Num() > 0)
            {
                int32 PickedArrayIndex = 0;
                if (Preset->Weights.Num() == Num)
                {
                    float TotalWeight = 0.0f;
                    for (int32 CandidateIdx : Remaining)
                    {
                        TotalWeight += FMath::Max(0.0f, Preset->Weights[CandidateIdx]);
                    }

                    if (TotalWeight > KINDA_SMALL_NUMBER)
                    {
                        float Pick = Rng.FRandRange(0.0f, TotalWeight);
                        for (int32 RemainingIdx = 0; RemainingIdx < Remaining.Num(); ++RemainingIdx)
                        {
                            const int32 CandidateIdx = Remaining[RemainingIdx];
                            Pick -= FMath::Max(0.0f, Preset->Weights[CandidateIdx]);
                            if (Pick <= 0.0f)
                            {
                                PickedArrayIndex = RemainingIdx;
                                break;
                            }
                        }
                    }
                    else
                    {
                        PickedArrayIndex = Rng.RandRange(0, Remaining.Num() - 1);
                    }
                }
                else
                {
                    PickedArrayIndex = Rng.RandRange(0, Remaining.Num() - 1);
                }

                const int32 Idx = Remaining[PickedArrayIndex];
                Remaining.RemoveAtSwap(PickedArrayIndex);

                TSubclassOf<APawn> CandidateClass = Preset->EnemyClasses[Idx].LoadSynchronous();
                if (IsCandidateSafe(CandidateClass.Get()))
                {
                    return CandidateClass;
                }

                UE_LOG(LogTemp, Warning, TEXT("[RaidEnemyPresetRegistry] Rejected unsafe enemy class in preset '%s' (index %d)."),
                    *Preset->PresetId.ToString(), Idx);
            }

            return nullptr;
        };

    if (const FRaidEnemyPreset* Found = FindPresetCaseInsensitive(Presets, PresetId))
    {
        if (TSubclassOf<APawn> SafeClass = PickSafeClassFromPreset(Found))
        {
            return SafeClass;
        }
    }

    if (PresetId != TEXT("Default"))
    {
        if (const FRaidEnemyPreset* DefaultPreset = FindPresetCaseInsensitive(Presets, TEXT("Default")))
        {
            if (TSubclassOf<APawn> SafeClass = PickSafeClassFromPreset(DefaultPreset))
            {
                return SafeClass;
            }
        }
    }

    UE_LOG(LogTemp, Error, TEXT("[RaidEnemyPresetRegistry] Preset '%s' is missing or invalid. Returning nullptr for safe fail."),
        *PresetId.ToString());

    return nullptr;
}

bool URaidEnemyPresetRegistry::AddEnemyClassPath(FName PresetId, const FString& ClassPathString, float Weight)
{
    if (PresetId.IsNone() || ClassPathString.IsEmpty()) return false;

    TSoftClassPtr<APawn> SoftClass{ FSoftObjectPath(ClassPathString) };
    if (SoftClass.IsNull()) return false;

    FRaidEnemyPreset& Preset = Presets.FindOrAdd(PresetId);
    Preset.EnemyClasses.Add(SoftClass);
    Preset.Weights.Add(Weight);

    if (Preset.PresetId.IsNone()) Preset.PresetId = PresetId;

    return true;
}

bool URaidEnemyPresetRegistry::TryRepairSpawnedPawn(APawn* SpawnedPawn, FString& OutFixLog) const
{
    OutFixLog.Empty();

    if (!bAutoRepairInvalidPawnAssets)
    {
        return false;
    }

    if (!SpawnedPawn)
    {
        OutFixLog = TEXT("SpawnedPawn is null");
        return false;
    }

    USkeletalMeshComponent* SkeletalComp = SpawnedPawn->FindComponentByClass<USkeletalMeshComponent>();
    if (!SkeletalComp)
    {
        OutFixLog = TEXT("No skeletal mesh component");
        return false;
    }

    bool bAnyFixed = false;

    if (!SkeletalComp->GetSkeletalMeshAsset())
    {
        if (!RepairFallbackSkeletalMesh.IsNull())
        {
            if (USkeletalMesh* FallbackMesh = RepairFallbackSkeletalMesh.LoadSynchronous())
            {
                SkeletalComp->SetSkeletalMesh(FallbackMesh);
                bAnyFixed = true;
            }
        }
    }

    if (!SkeletalComp->GetAnimClass())
    {
        if (!RepairFallbackAnimClass.IsNull())
        {
            if (UClass* FallbackAnimClass = RepairFallbackAnimClass.LoadSynchronous())
            {
                SkeletalComp->SetAnimInstanceClass(FallbackAnimClass);
                bAnyFixed = true;
            }
        }
    }

    if (bAnyFixed)
    {
        OutFixLog = TEXT("Repaired missing SkeletalMesh/AnimClass");
        return true;
    }

    return false;
}
