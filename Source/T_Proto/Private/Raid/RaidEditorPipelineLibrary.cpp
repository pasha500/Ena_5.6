#include "Raid/RaidEditorPipelineLibrary.h"

#include "Raid/RaidChapterConfig.h"
#include "Raid/RaidLayoutManager.h"
#include "Raid/LevelNodeRow.h"

#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructUtils/UserDefinedStructEditorUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif

namespace
{
    bool ResolveDataTablePath(const FString& InPath, FString& OutPackageName, FString& OutAssetName, FString& OutError)
    {
        FString Path = InPath.TrimStartAndEnd();
        if (Path.IsEmpty())
        {
            OutError = TEXT("DataTableAssetPath가 비어 있습니다. 예: /Game/Raid/Data/DT_AI_Raid_Design");
            return false;
        }

        FString Left;
        FString Right;
        if (Path.Split(TEXT("."), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            Path = Left;
        }

        if (!Path.StartsWith(TEXT("/Game/")))
        {
            OutError = TEXT("DataTableAssetPath는 /Game/ 경로여야 합니다.");
            return false;
        }

        OutPackageName = Path;
        OutAssetName = FPackageName::GetLongPackageAssetName(Path);

        if (OutAssetName.IsEmpty())
        {
            OutError = TEXT("DataTableAssetPath에서 AssetName을 추출하지 못했습니다.");
            return false;
        }

        return true;
    }

    FString CsvEscaped(const FString& In)
    {
        FString Out = In;
        Out.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Out);
    }
}

bool URaidEditorPipelineLibrary::PickCsvFile(FString& OutCsvFilePath)
{
#if WITH_EDITOR
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        OutCsvFilePath = TEXT("");
        return false;
    }

    TArray<FString> PickedFiles;
    const bool bPicked = DesktopPlatform->OpenFileDialog(
        nullptr,
        TEXT("Select AI Raid CSV"),
        FPaths::ProjectDir(),
        TEXT(""),
        TEXT("CSV Files (*.csv)|*.csv"),
        EFileDialogFlags::None,
        PickedFiles
    );

    if (!bPicked || PickedFiles.Num() == 0)
    {
        OutCsvFilePath = TEXT("");
        return false;
    }

    OutCsvFilePath = PickedFiles[0];
    return true;
#else
    OutCsvFilePath = TEXT("");
    return false;
#endif
}

bool URaidEditorPipelineLibrary::OneClickImportAndBuild(
    const FString& CsvFilePath,
    const FString& DataTableAssetPath,
    URaidChapterConfig* ChapterConfig,
    ARaidLayoutManager* LayoutManager,
    bool bSaveAssets,
    bool bRunContentRepair,
    bool bRunHeavyFullAudit,
    FString& OutMessage)
{
#if !WITH_EDITOR
    OutMessage = TEXT("Editor 전용 기능입니다.");
    return false;
#else
    if (!ChapterConfig)
    {
        OutMessage = TEXT("ChapterConfig가 비어 있습니다.");
        return false;
    }

    FString CsvText;
    if (CsvFilePath.IsEmpty() || !FFileHelper::LoadFileToString(CsvText, *CsvFilePath))
    {
        OutMessage = FString::Printf(TEXT("CSV 파일을 읽지 못했습니다: %s"), *CsvFilePath);
        return false;
    }

    FString PackageName;
    FString AssetName;
    FString PathError;
    if (!ResolveDataTablePath(DataTableAssetPath, PackageName, AssetName, PathError))
    {
        OutMessage = PathError;
        return false;
    }

    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
    UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *ObjectPath);
    bool bCreatedNew = false;

    if (!DataTable)
    {
        UPackage* Package = CreatePackage(*PackageName);
        if (!Package)
        {
            OutMessage = FString::Printf(TEXT("패키지를 생성하지 못했습니다: %s"), *PackageName);
            return false;
        }

        DataTable = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        if (!DataTable)
        {
            OutMessage = TEXT("DataTable 오브젝트 생성 실패");
            return false;
        }

        bCreatedNew = true;
        FAssetRegistryModule::AssetCreated(DataTable);
    }

    DataTable->Modify();
    DataTable->RowStruct = FLevelNodeRow::StaticStruct();
    DataTable->EmptyTable();

    const TArray<FString> ImportProblems = DataTable->CreateTableFromCSVString(CsvText);
    if (ImportProblems.Num() > 0)
    {
        FString ProblemText;
        const int32 MaxLines = FMath::Min(ImportProblems.Num(), 6);
        for (int32 i = 0; i < MaxLines; ++i)
        {
            ProblemText += ImportProblems[i] + TEXT("\n");
        }
        OutMessage = FString::Printf(TEXT("CSV 임포트 오류 (%d개)\n%s"), ImportProblems.Num(), *ProblemText);
        return false;
    }

    DataTable->MarkPackageDirty();

    ChapterConfig->Modify();
    ChapterConfig->LevelDataTable = DataTable;
    ChapterConfig->MarkPackageDirty();

    ARaidLayoutManager* TargetManager = LayoutManager;
    if (!TargetManager)
    {
        UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (EditorWorld)
        {
            for (TActorIterator<ARaidLayoutManager> It(EditorWorld); It; ++It)
            {
                TargetManager = *It;
                break;
            }
        }
    }

    if (!TargetManager)
    {
        OutMessage = TEXT("RaidLayoutManager를 찾지 못했습니다. 레벨에 배치하거나 인자로 넘겨주세요.");
        return false;
    }

    TargetManager->Modify();
    TargetManager->ChapterConfig = ChapterConfig;
    TargetManager->ApplyOpenWorldSpecFromCsvPath(CsvFilePath);
    TargetManager->AutoFinalizeImportedData();

    FString RepairReport;
    FString AuditSummary;
    FString AuditCsvPath;
    if (bRunContentRepair)
    {
        TArray<FString> RepairRoots;
        // OneClick 기본 복구는 프로젝트 레이드 자산만 대상으로 제한.
        // 서드파티 대형 BP 패키지(ALS/CombatFury)는 수동 점검으로 분리.
        RepairRoots.Add(TEXT("/Game/Raid"));

        // 원클릭에서는 저장 팝업 폭주/장시간 블로킹을 막기 위해 repair 단계 저장은 끈다.
        const bool bRepairOk = RepairBlueprintAndStructAssets(RepairRoots, false, RepairReport);
        if (!bRepairOk)
        {
            UE_LOG(LogTemp, Warning, TEXT("[RaidAutomation] Content repair reported issues:\n%s"), *RepairReport);
        }
        if (bRunHeavyFullAudit)
        {
            const bool bAuditOk = AuditAllProjectContent(true, false, AuditSummary, AuditCsvPath);
            if (!bAuditOk)
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidAutomation] Full content audit found errors: %s"), *AuditSummary);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[RaidAutomation] Full content audit passed: %s"), *AuditSummary);
            }
        }
        else
        {
            AuditCsvPath = TEXT("skipped (set bRunHeavyFullAuditInOneClick=true to enable)");
        }
    }

    if (bSaveAssets)
    {
        if (!DataTable->RowStruct)
        {
            // 저장 직전 RowStruct 유실 방어.
            DataTable->RowStruct = FLevelNodeRow::StaticStruct();
            DataTable->MarkPackageDirty();
            UE_LOG(LogTemp, Warning, TEXT("[RaidAutomation] DataTable RowStruct was null before save. Rebound to FLevelNodeRow."));
        }

        TArray<UPackage*> PackagesToSave;
        if (DataTable && DataTable->GetOutermost()) PackagesToSave.AddUnique(DataTable->GetOutermost());
        if (ChapterConfig && ChapterConfig->GetOutermost()) PackagesToSave.AddUnique(ChapterConfig->GetOutermost());

        if (PackagesToSave.Num() > 0)
        {
            FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
        }
    }

    OutMessage = FString::Printf(
        TEXT("완료: CSV 임포트 + ChapterConfig 연결 + Layout 빌드\nCSV: %s\nDT: %s%s\nAuditReport: %s"),
        *CsvFilePath,
        *ObjectPath,
        bCreatedNew ? TEXT(" (created)") : TEXT(" (updated)"),
        (bRunContentRepair ? *AuditCsvPath : TEXT("skipped"))
    );
    return true;
#endif
}

bool URaidEditorPipelineLibrary::RepairBlueprintAndStructAssets(
    const TArray<FString>& RootContentPaths,
    bool bSaveAssets,
    FString& OutReport)
{
#if !WITH_EDITOR
    OutReport = TEXT("Editor 전용 기능입니다.");
    return false;
#else
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetsToProcess;
    for (const FString& RootPath : RootContentPaths)
    {
        if (RootPath.IsEmpty()) continue;
        TArray<FAssetData> Found;
        AssetRegistry.GetAssetsByPath(FName(*RootPath), Found, true, false);
        AssetsToProcess.Append(Found);
    }

    int32 StructCompiled = 0;
    int32 StructFailed = 0;
    int32 BlueprintCompiled = 0;
    int32 BlueprintFailed = 0;
    int32 ProcessedStructs = 0;
    int32 ProcessedBlueprints = 0;
    bool bTruncatedByBudget = false;
    TArray<UPackage*> PackagesToSave;
    FString ErrorLines;
    const int32 MaxStructsPerRun = 250;
    const int32 MaxBlueprintsPerRun = 500;
    const double MaxSecondsPerRun = 180.0;
    const double StartSeconds = FPlatformTime::Seconds();

    for (const FAssetData& Asset : AssetsToProcess)
    {
        const FTopLevelAssetPath ClassPath = Asset.AssetClassPath;

        if (ClassPath == UUserDefinedStruct::StaticClass()->GetClassPathName())
        {
            if (ProcessedStructs >= MaxStructsPerRun || (FPlatformTime::Seconds() - StartSeconds) > MaxSecondsPerRun)
            {
                bTruncatedByBudget = true;
                continue;
            }
            ProcessedStructs++;

            UUserDefinedStruct* StructAsset = Cast<UUserDefinedStruct>(Asset.GetAsset());
            if (!StructAsset)
            {
                StructFailed++;
                ErrorLines += FString::Printf(TEXT("Struct load failed: %s\n"), *Asset.GetObjectPathString());
                continue;
            }

            StructAsset->Modify();
            FUserDefinedStructEditorUtils::OnStructureChanged(StructAsset);

            if (StructAsset->Status == UDSS_Error)
            {
                StructFailed++;
                ErrorLines += FString::Printf(TEXT("Struct compile failed: %s\n"), *Asset.GetObjectPathString());
            }
            else
            {
                StructCompiled++;
                if (StructAsset->GetOutermost()) PackagesToSave.AddUnique(StructAsset->GetOutermost());
            }
            continue;
        }

        if (ClassPath == UBlueprint::StaticClass()->GetClassPathName())
        {
            if (ProcessedBlueprints >= MaxBlueprintsPerRun || (FPlatformTime::Seconds() - StartSeconds) > MaxSecondsPerRun)
            {
                bTruncatedByBudget = true;
                continue;
            }
            ProcessedBlueprints++;

            UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
            if (!Blueprint)
            {
                BlueprintFailed++;
                ErrorLines += FString::Printf(TEXT("Blueprint load failed: %s\n"), *Asset.GetObjectPathString());
                continue;
            }

            FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

            if (Blueprint->Status == BS_Error)
            {
                BlueprintFailed++;
                ErrorLines += FString::Printf(TEXT("Blueprint compile failed: %s\n"), *Asset.GetObjectPathString());
            }
            else
            {
                BlueprintCompiled++;
                if (Blueprint->GetOutermost()) PackagesToSave.AddUnique(Blueprint->GetOutermost());
            }
        }
    }

    if (bSaveAssets && PackagesToSave.Num() > 0)
    {
        FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
    }

    OutReport = FString::Printf(
        TEXT("Repair complete. Struct OK=%d Fail=%d (processed %d), Blueprint OK=%d Fail=%d (processed %d), Truncated=%s\n%s"),
        StructCompiled, StructFailed, ProcessedStructs, BlueprintCompiled, BlueprintFailed, ProcessedBlueprints,
        bTruncatedByBudget ? TEXT("true") : TEXT("false"), *ErrorLines
    );

    return (StructFailed == 0 && BlueprintFailed == 0);
#endif
}

bool URaidEditorPipelineLibrary::AuditAllProjectContent(
    bool bAttemptAutoFix,
    bool bSaveAssets,
    FString& OutSummary,
    FString& OutCsvReportPath)
{
#if !WITH_EDITOR
    OutSummary = TEXT("Editor 전용 기능입니다.");
    OutCsvReportPath = TEXT("");
    return false;
#else
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FString> CachedPaths;
    AssetRegistry.GetAllCachedPaths(CachedPaths);

    TSet<FString> RootMounts;
    for (const FString& Path : CachedPaths)
    {
        if (!Path.StartsWith(TEXT("/"))) continue;

        TArray<FString> Segments;
        Path.ParseIntoArray(Segments, TEXT("/"), true);
        if (Segments.Num() == 0) continue;

        const FString Mount = FString::Printf(TEXT("/%s"), *Segments[0]);
        if (Mount == TEXT("/Engine") || Mount == TEXT("/Script") || Mount == TEXT("/Temp") ||
            Mount == TEXT("/Memory") || Mount == TEXT("/__ExternalActors__") || Mount == TEXT("/__ExternalObjects__"))
        {
            continue;
        }
        RootMounts.Add(Mount);
    }

    TArray<FAssetData> AllAssets;
    TSet<FString> SeenObjectPaths;
    for (const FString& Root : RootMounts)
    {
        TArray<FAssetData> Found;
        AssetRegistry.GetAssetsByPath(FName(*Root), Found, true, false);
        for (const FAssetData& Asset : Found)
        {
            const FString ObjPath = Asset.GetObjectPathString();
            if (!SeenObjectPaths.Contains(ObjPath))
            {
                SeenObjectPaths.Add(ObjPath);
                AllAssets.Add(Asset);
            }
        }
    }

    int32 Total = 0;
    int32 LoadFail = 0;
    int32 StructFail = 0;
    int32 BlueprintFail = 0;
    int32 DataTableFail = 0;
    int32 PoseWarn = 0;
    bool bTruncatedByBudget = false;
    TArray<UPackage*> PackagesToSave;
    const int32 MaxAssetsPerRun = 4000;
    const double MaxSecondsPerRun = 240.0;
    const double StartSeconds = FPlatformTime::Seconds();

    TArray<FString> CsvLines;
    CsvLines.Add(TEXT("ObjectPath,ClassPath,Status,Detail"));

    for (const FAssetData& Asset : AllAssets)
    {
        if (Total >= MaxAssetsPerRun || (FPlatformTime::Seconds() - StartSeconds) > MaxSecondsPerRun)
        {
            bTruncatedByBudget = true;
            break;
        }

        Total++;
        const FString ObjPath = Asset.GetObjectPathString();
        const FString ClassPath = Asset.AssetClassPath.ToString();
        FString Status = TEXT("OK");
        FString Detail = TEXT("Loaded");

        UObject* LoadedAsset = Asset.GetAsset();
        if (!LoadedAsset)
        {
            LoadFail++;
            Status = TEXT("ERROR");
            Detail = TEXT("Asset load failed");
            CsvLines.Add(FString::Printf(TEXT("%s,%s,%s,%s"), *CsvEscaped(ObjPath), *CsvEscaped(ClassPath), *CsvEscaped(Status), *CsvEscaped(Detail)));
            continue;
        }

        if (Asset.AssetClassPath == UUserDefinedStruct::StaticClass()->GetClassPathName())
        {
            UUserDefinedStruct* StructAsset = Cast<UUserDefinedStruct>(LoadedAsset);
            if (!StructAsset)
            {
                StructFail++;
                Status = TEXT("ERROR");
                Detail = TEXT("UserDefinedStruct cast failed");
            }
            else
            {
                if (bAttemptAutoFix)
                {
                    StructAsset->Modify();
                    FUserDefinedStructEditorUtils::OnStructureChanged(StructAsset);
                }

                if (StructAsset->Status == UDSS_Error)
                {
                    StructFail++;
                    Status = TEXT("ERROR");
                    Detail = TEXT("Struct compile/status error");
                }
                else if (StructAsset->GetOutermost())
                {
                    PackagesToSave.AddUnique(StructAsset->GetOutermost());
                }
            }
        }
        else if (Asset.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
        {
            UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
            if (!Blueprint)
            {
                BlueprintFail++;
                Status = TEXT("ERROR");
                Detail = TEXT("Blueprint cast failed");
            }
            else
            {
                if (bAttemptAutoFix)
                {
                    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
                    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
                }

                if (Blueprint->Status == BS_Error)
                {
                    BlueprintFail++;
                    Status = TEXT("ERROR");
                    Detail = TEXT("Blueprint compile error");
                }
                else if (Blueprint->GetOutermost())
                {
                    PackagesToSave.AddUnique(Blueprint->GetOutermost());
                }
            }
        }
        else if (Asset.AssetClassPath == UDataTable::StaticClass()->GetClassPathName())
        {
            UDataTable* DT = Cast<UDataTable>(LoadedAsset);
            if (!DT || !DT->RowStruct)
            {
                DataTableFail++;
                Status = TEXT("ERROR");
                Detail = TEXT("DataTable RowStruct is null");
            }
        }

        if (ClassPath.Contains(TEXT("PoseSearch"), ESearchCase::IgnoreCase))
        {
            // PoseSearch 자산은 로딩/참조 깨짐이 잦아 별도 추적
            if (Status == TEXT("OK"))
            {
                Status = TEXT("WARN");
                Detail = TEXT("PoseSearch asset loaded (verify DB/schema manually if runtime warnings persist)");
            }
            PoseWarn++;
        }

        CsvLines.Add(FString::Printf(TEXT("%s,%s,%s,%s"), *CsvEscaped(ObjPath), *CsvEscaped(ClassPath), *CsvEscaped(Status), *CsvEscaped(Detail)));
    }

    const FString ReportsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Reports"));
    IFileManager::Get().MakeDirectory(*ReportsDir, true);
    const FString TimeStamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    OutCsvReportPath = FPaths::Combine(ReportsDir, FString::Printf(TEXT("ContentAudit_%s.csv"), *TimeStamp));
    FFileHelper::SaveStringToFile(FString::Join(CsvLines, TEXT("\n")), *OutCsvReportPath);

    if (bSaveAssets && PackagesToSave.Num() > 0)
    {
        FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
    }

    OutSummary = FString::Printf(
        TEXT("Audit complete. Total=%d, LoadFail=%d, StructFail=%d, BlueprintFail=%d, DataTableFail=%d, PoseWarn=%d, Truncated=%s, Report=%s"),
        Total, LoadFail, StructFail, BlueprintFail, DataTableFail, PoseWarn,
        bTruncatedByBudget ? TEXT("true") : TEXT("false"), *OutCsvReportPath
    );

    return (LoadFail == 0 && StructFail == 0 && BlueprintFail == 0 && DataTableFail == 0);
#endif
}
