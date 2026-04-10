import unreal


def is_interesting(name: str) -> bool:
    lower = name.lower()
    tokens = [
        "heal",
        "health",
        "medical",
        "med",
        "param",
        "quantity",
        "amount",
        "value",
        "restore",
        "recover",
        "outline",
        "stencil",
    ]
    return any(t in lower for t in tokens)


def dump_medical_bp_defaults():
    paths = [
        "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem_C",
        "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem",
    ]
    obj = None
    for p in paths:
        obj = unreal.load_object(None, p)
        if obj:
            unreal.log_warning(f"[LootDebug] Loaded object: {p} ({obj.get_class().get_name()})")
            break
    if not obj:
        unreal.log_error("[LootDebug] Failed to load BP_LootPickableItem_MedicalItem.")
        return

    cls = obj if isinstance(obj, unreal.Class) else obj.get_class()
    cdo = unreal.get_default_object(cls)
    if not cdo:
        unreal.log_error("[LootDebug] Failed to get class default object.")
        return

    unreal.log_warning(f"[LootDebug] CDO class: {cdo.get_class().get_path_name()}")
    props = [p for p in cdo.get_editor_property_names() if is_interesting(p)]
    props.sort()
    unreal.log_warning(f"[LootDebug] CDO interesting property count: {len(props)}")
    for p in props:
        try:
            v = cdo.get_editor_property(p)
            unreal.log_warning(f"[LootDebug] CDO {p} = {v}")
        except Exception as ex:
            unreal.log_warning(f"[LootDebug] CDO {p} <read failed: {ex}>")


def dump_loot_datatable():
    table = unreal.load_object(
        None,
        "/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable",
    )
    if not table:
        unreal.log_error("[LootDebug] Failed to load LootItemsDataTable.")
        return

    row_struct = table.get_editor_property("row_struct")
    row_struct_name = row_struct.get_path_name() if row_struct else "<none>"
    unreal.log_warning(f"[LootDebug] DataTable row struct: {row_struct_name}")

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    unreal.log_warning(f"[LootDebug] DataTable row count: {len(row_names)}")

    for row_name in row_names[:40]:
        row_json = unreal.DataTableFunctionLibrary.get_data_table_row_as_string(table, row_name)
        if not row_json:
            continue
        if any(token in row_json.lower() for token in ["medical", "bandage", "syringe", "medkit", "heal", "health"]):
            unreal.log_warning(f"[LootDebug] ROW {row_name}: {row_json}")


def main():
    unreal.log_warning("[LootDebug] ===== start =====")
    dump_medical_bp_defaults()
    dump_loot_datatable()
    unreal.log_warning("[LootDebug] ===== done =====")


if __name__ == "__main__":
    main()
