#include "Raid/RaidLootRegistry.h"

namespace
{
    const FRaidLootGroup* FindLootGroupCaseInsensitive(const TMap<FString, FRaidLootGroup>& LootGroups, const FString& Key)
    {
        if (const FRaidLootGroup* Exact = LootGroups.Find(Key))
        {
            return Exact;
        }

        const FString Normalized = Key.TrimStartAndEnd().ToLower();
        for (const TPair<FString, FRaidLootGroup>& Pair : LootGroups)
        {
            if (Pair.Key.TrimStartAndEnd().ToLower() == Normalized)
            {
                return &Pair.Value;
            }
        }
        return nullptr;
    }
}

const FRaidLootCandidate* URaidLootRegistry::GetRandomCandidate(const FString& LootLevel) const
{
    const FString Normalized = LootLevel.TrimStartAndEnd().ToLower();
    const TArray<FString> CandidateKeys = [&LootLevel, &Normalized]() -> TArray<FString>
        {
            if (Normalized == TEXT("epic"))
            {
                return { TEXT("Epic"), TEXT("High"), TEXT("Common"), TEXT("Medium"), TEXT("Low") };
            }
            if (Normalized == TEXT("high"))
            {
                return { TEXT("High"), TEXT("Epic"), TEXT("Common"), TEXT("Medium"), TEXT("Low") };
            }
            if (Normalized == TEXT("medium") || Normalized == TEXT("mid"))
            {
                return { TEXT("Medium"), TEXT("Mid"), TEXT("Common"), TEXT("Low"), TEXT("High") };
            }
            if (Normalized == TEXT("low"))
            {
                return { TEXT("Low"), TEXT("Common"), TEXT("Medium"), TEXT("High") };
            }
            if (Normalized == TEXT("common"))
            {
                return { TEXT("Common"), TEXT("Medium"), TEXT("Low"), TEXT("High") };
            }
            return { LootLevel, TEXT("Common"), TEXT("Medium"), TEXT("Low"), TEXT("High"), TEXT("Epic") };
        }();

    const FRaidLootGroup* Group = nullptr;
    for (const FString& Key : CandidateKeys)
    {
        Group = FindLootGroupCaseInsensitive(LootGroups, Key);
        if (Group && Group->Candidates.Num() > 0)
        {
            break;
        }
        Group = nullptr;
    }

    if (!Group)
    {
        for (const TPair<FString, FRaidLootGroup>& Pair : LootGroups)
        {
            if (Pair.Value.Candidates.Num() > 0)
            {
                Group = &Pair.Value;
                break;
            }
        }
    }

    if (!Group || Group->Candidates.Num() == 0) return nullptr;

    float TotalWeight = 0.0f;
    for (const FRaidLootCandidate& Candidate : Group->Candidates)
    {
        TotalWeight += FMath::Max(0.0f, Candidate.Weight);
    }

    if (TotalWeight <= KINDA_SMALL_NUMBER)
    {
        return &Group->Candidates[FMath::RandRange(0, Group->Candidates.Num() - 1)];
    }

    const float RandVal = FMath::FRandRange(0.0f, TotalWeight);
    float CurrentWeight = 0.0f;

    for (const FRaidLootCandidate& Candidate : Group->Candidates)
    {
        CurrentWeight += FMath::Max(0.0f, Candidate.Weight);
        if (RandVal <= CurrentWeight) return &Candidate;
    }

    return &Group->Candidates.Last();
}
