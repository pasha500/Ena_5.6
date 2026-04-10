#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LevelNodeRow.generated.h"

USTRUCT(BlueprintType)
struct T_PROTO_API FLevelNodeRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 NodeId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PosX = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PosY = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PosZ = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ZoneId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString RoomType = TEXT("");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString RoomRole = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Difficulty = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CombatWeight = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString BotProfile = TEXT("Tactical");

    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString EnemyPreset = TEXT("None");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString LootLevel = TEXT("Common");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString RoomSize = TEXT("Medium");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Connections = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString NodeTags = TEXT("");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString RoomPrefabId = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SpawnCount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 LootCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString EnvType = TEXT("Urban");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Theme = TEXT("Modern");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ObstacleDensity = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Seed = 1234;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString LootStrategy = TEXT("Scattered");
    // 0~1: 방 내에서 진입 가능한 건물 비율. 낮을수록 봉인 건물(우회 유도) 비율이 높다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float EnterableBuildingRatio = 0.45f;
    // 방 내부 우회/주동선 분기 수를 결정하는 시드 값(최소 1).
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 TraversalLaneSeeds = 2;

    // [핵심 수정] 무결성이 보장된 연결 ID 파싱
    TArray<int32> GetConnectionIds() const
    {
        TArray<FString> Parsed;
        Connections.ParseIntoArray(Parsed, TEXT(","), true);

        TArray<int32> Result;
        TSet<int32> UniqueIds;
        Result.Reserve(Parsed.Num());

        for (FString Token : Parsed)
        {
            Token.TrimStartAndEndInline();
            if (Token.IsEmpty()) continue;

            int32 ParsedId = INDEX_NONE;
            if (LexTryParseString(ParsedId, *Token) && ParsedId >= 0 && !UniqueIds.Contains(ParsedId))
            {
                UniqueIds.Add(ParsedId);
                Result.Add(ParsedId);
            }
        }
        return Result;
    }
};
