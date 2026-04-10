# MECE Audit Report

- Root: `D:\UnrealProject\T_Proto`
- Scanned files: **301**

## 1) Scope Inventory

- `.h`: 135
- `.cpp`: 132
- `.ini`: 11
- `.py`: 11
- `.cs`: 10
- `.json`: 1
- `.md`: 1

## 2) Complexity Hotspots (Line Count >= 1200)

- `Source\T_Proto\Private\Raid\RaidCombatSubsystem.cpp`: 6503 lines
- `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`: 3259 lines
- `Source\T_Proto\Private\Raid\RaidLayoutManager.cpp`: 2474 lines
- `Plugins\HelpfulFunctions\Source\HelpfulFunctions\Private\HelpfulFunctionsBPLibrary.cpp`: 2447 lines
- `Plugins\JakubCableComponent\Source\JakubCableComponent\Private\JakubCablePhysic.cpp`: 1839 lines
- `Plugins\ClimbingNavigation\Source\ClimbingNavigation\Private\AdvancedAI_TasksAndFunctions.cpp`: 1694 lines
- `Plugins\ClimbingNavigation\Source\ClimbingNavigation\Private\NavClimbingComponentCore.cpp`: 1430 lines

## 3) TODO/FIXME/HACK Markers

- `Plugins\ClimbingNavigation\Source\ClimbingNavigation\Private\AISense_BetterSight.cpp:303` `// @todo figure out what should we do if not valid`
- `Plugins\JakubAnimNodes\Source\JakubAnimNodes\Private\MyAnimGraphNode.cpp:118` `//@TODO: Only offer this option on arrayed pins`
- `Plugins\JakubCableComponent\Source\JakubCableComponent\Private\JakubCablePhysic.cpp:509` `// TODO: Move this call to SetDynamicData_RenderThread(...) once race condition with FRayTracingGeometryManager::Tick() is addressed.`
- `Plugins\JakubCableComponent\Source\JakubCableComponent\Private\JakubCablePhysic.cpp:572` `// TODO: Checking if VertexFactory.GetType()->SupportsRayTracingDynamicGeometry() should be done when initializing bDynamicRayTracingGeometry otherwise we end up with unbuilt BLAS`
- `Plugins\JakubCableComponent\Source\JakubCableComponent\Private\JakubCablePhysic.cpp:583` `CachedRayTracingMaterials, // TODO: this copy can be avoided if FRayTracingDynamicGeometryUpdateParams supported array views`

## 4) Sync Load Usage (Potential Runtime Cost)

- `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`: 15 hits
- `Source\T_Proto\Private\Raid\RaidCombatSubsystem.cpp`: 10 hits
- `Source\T_Proto\Private\Raid\RaidEnemyPresetRegistry.cpp`: 6 hits
- `Source\T_Proto\Private\Raid\RaidLayoutManager.cpp`: 4 hits
- `Plugins\HelpfulFunctions\Source\HelpfulFunctions\Private\PairedAttackSequenceData.cpp`: 2 hits
- `Plugins\IWALS_AbilitySystem\Source\IWALS_AbilitySystem\Private\GAS_MainCharacterCpp.cpp`: 2 hits
- `Source\T_Proto\Private\Raid\RoomPrefabRegistry.cpp`: 1 hits
- `Source\T_Proto\Public\Raid\RaidEnemyPresetRegistry.h`: 1 hits

### Tick + SyncLoad Heuristic Risk

- `Plugins\HelpfulFunctions\Source\HelpfulFunctions\Private\PairedAttackSequenceData.cpp`
- `Plugins\IWALS_AbilitySystem\Source\IWALS_AbilitySystem\Private\GAS_MainCharacterCpp.cpp`
- `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`

## 5) Duplicated Hardcoded Asset Paths

- `/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable` referenced in 5 files
  - `Source\T_Proto\Private\Raid\RaidCombatSubsystem.cpp`
  - `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`
  - `Source\T_Proto\Public\Raid\RaidCombatSubsystem.h`
  - `Tools\debug_loot_runtime.py`
  - `Tools\find_medical_heal_values.py`
- `/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem` referenced in 4 files
  - `Tools\debug_loot_asset_types.py`
  - `Tools\debug_loot_runtime.py`
  - `Tools\inspect_medical_blueprint.py`
  - `Tools\inspect_medical_bp_graph.py`
- `/Game/Game/AILevel/Blueprints/Interfaces/WBP_RaidRegionBanner.WBP_RaidRegionBanner_C` referenced in 3 files
  - `Source\T_Proto\Private\Raid\RaidCombatSubsystem.cpp`
  - `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`
  - `Source\T_Proto\Public\Raid\RaidCombatSubsystem.h`
- `/Game/TemplesOfCambodia/Demo/EpicContent/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust` referenced in 3 files
  - `Source\T_Proto\Private\Raid\RaidRoomActor.cpp`
  - `Source\T_Proto\Public\Raid\RaidRoomActor.h`
  - `Tools\debug_loot_asset_types.py`

## 6) MECE Recommendations

- Split hotspot files by responsibility boundary (`Layout`, `Combat`, `LootBinding`, `UIBanner`, `Wave`) to reduce coupling.
- Keep a single source-of-truth per gameplay value (e.g. medical heal in `LootItemsDataTable`).
- Move hardcoded asset paths to configurable `UPROPERTY(TSoftObjectPtr<...>)` fields in data/config assets.
- Gate non-critical `Warning` logs behind debug flags to reduce runtime log overhead.
- Keep `Tools` separated into Production vs Diagnostics; avoid shipping one-off scripts in runtime path.