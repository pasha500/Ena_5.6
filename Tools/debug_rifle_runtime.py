import unreal


def _is_interesting(name: str) -> bool:
    lower = name.lower()
    tokens = [
        "row",
        "rifle",
        "weapon",
        "model",
        "index",
        "table",
        "data",
        "class",
        "mesh",
    ]
    return any(t in lower for t in tokens)


def _load_any(paths):
    for p in paths:
        obj = unreal.load_object(None, p)
        if obj:
            unreal.log_warning(f"[RifleDebug] Loaded: {p} ({obj.get_class().get_name()})")
            return obj
    return None


def dump_rifle_bp_defaults():
    obj = _load_any(
        [
            "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene/BP_I_Rifle_Actor_For_Player.BP_I_Rifle_Actor_For_Player_C",
            "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene/BP_I_Rifle_Actor_For_Player.BP_I_Rifle_Actor_For_Player",
        ]
    )
    if not obj:
        unreal.log_error("[RifleDebug] Failed to load BP_I_Rifle_Actor_For_Player")
        return

    if isinstance(obj, unreal.Blueprint):
        bp_gc = obj.generated_class() if callable(obj.generated_class) else obj.generated_class
        cdo = unreal.get_default_object(bp_gc)
    elif isinstance(obj, unreal.Class):
        cdo = unreal.get_default_object(obj)
    else:
        cdo = obj

    if not cdo:
        unreal.log_error("[RifleDebug] Failed to get CDO")
        return

    unreal.log_warning(f"[RifleDebug] CDO class={cdo.get_class().get_path_name()}")

    manual_props = [
        "DataRowName",
        "LootDataRowName",
        "ItemDataRowName",
        "WeaponDataRowName",
        "RifleDataRowName",
        "RifleAssetRowName",
        "RifleRowName",
        "WeaponRowName",
        "RifleModelIndex",
        "WeaponModelIndex",
        "GunModelIndex",
        "RifleConfig",
        "RifleAssetsDataTable",
        "Rifle_Mesh",
    ]

    for p in manual_props:
        try:
            v = cdo.get_editor_property(p)
            unreal.log_warning(f"[RifleDebug] {p} = {v}")
        except Exception as ex:
            unreal.log_warning(f"[RifleDebug] {p} <missing/read failed: {ex}>")


def dump_rifle_table_rows():
    table = unreal.load_object(
        None,
        "/Game/AdvancedLocomotionV4/Data/DataTables/RifleAssetsDataTable.RifleAssetsDataTable",
    )
    if not table:
        unreal.log_error("[RifleDebug] Failed to load RifleAssetsDataTable")
        return

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    unreal.log_warning(f"[RifleDebug] RifleAssets row count={len(row_names)}")
    for row_name in row_names:
        unreal.log_warning(f"[RifleDebug] RowName {row_name}")


def main():
    unreal.log_warning("[RifleDebug] ===== start =====")
    dump_rifle_bp_defaults()
    dump_rifle_table_rows()
    unreal.log_warning("[RifleDebug] ===== done =====")


if __name__ == "__main__":
    main()
