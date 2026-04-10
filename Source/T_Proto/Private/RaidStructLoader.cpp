#include "RaidStructLoader.h"
#include "Raid/LevelNodeRow.h"   // ¡ç ³× Struct Çì´õ

void URaidStructLoader::ForceLoadLevelNodeRow()
{
	FLevelNodeRow::StaticStruct();
}
