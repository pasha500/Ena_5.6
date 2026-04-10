#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RaidEditorPipelineLibrary.generated.h"

class URaidChapterConfig;
class ARaidLayoutManager;

UCLASS()
class T_PROTO_API URaidEditorPipelineLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // 에디터 파일 다이얼로그에서 CSV를 선택한다.
    UFUNCTION(BlueprintCallable, Category = "Raid|Automation|Editor")
    static bool PickCsvFile(FString& OutCsvFilePath);

    // CSV -> DataTable 생성/갱신 -> ChapterConfig 연결 -> LayoutManager 자동 최종화까지 수행한다.
    UFUNCTION(BlueprintCallable, Category = "Raid|Automation|Editor")
    static bool OneClickImportAndBuild(
        const FString& CsvFilePath,
        const FString& DataTableAssetPath,
        URaidChapterConfig* ChapterConfig,
        ARaidLayoutManager* LayoutManager,
        bool bSaveAssets,
        bool bRunContentRepair,
        bool bRunHeavyFullAudit,
        FString& OutMessage
    );

    // 지정 루트(/Game/...) 하위의 UserDefinedStruct/Blueprint를 일괄 재컴파일한다.
    UFUNCTION(BlueprintCallable, Category = "Raid|Automation|Editor")
    static bool RepairBlueprintAndStructAssets(
        const TArray<FString>& RootContentPaths,
        bool bSaveAssets,
        FString& OutReport
    );

    // /Game 및 플러그인 콘텐츠 마운트 경로 전체를 스캔해 로드/컴파일 검증을 수행한다.
    UFUNCTION(BlueprintCallable, Category = "Raid|Automation|Editor")
    static bool AuditAllProjectContent(
        bool bAttemptAutoFix,
        bool bSaveAssets,
        FString& OutSummary,
        FString& OutCsvReportPath
    );
};
