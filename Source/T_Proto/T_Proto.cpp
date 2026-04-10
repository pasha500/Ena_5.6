#include "T_Proto.h"                 // 반드시 1번
#include "Modules/ModuleManager.h"
#include "Raid/LevelNodeRow.h"       // 구조체 헤더

class FT_ProtoModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        // 에디터가 모듈 로드할 때 구조체를 강제로 생성/등록
        FLevelNodeRow::StaticStruct();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FT_ProtoModule, T_Proto, "T_Proto");
