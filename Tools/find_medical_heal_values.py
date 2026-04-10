import json
import unreal


TABLE_PATH = "/Game/AdvancedLocomotionV4/Data/DataTables/LootItemsDataTable.LootItemsDataTable"


def lower_has_any(text: str, tokens):
    lower = text.lower()
    return any(t in lower for t in tokens)


def main():
    dt_lib_methods = [m for m in dir(unreal.DataTableFunctionLibrary) if not m.startswith("_")]
    unreal.log_warning(f"[LootFind] DataTableFunctionLibrary methods: {dt_lib_methods}")

    table = unreal.load_object(None, TABLE_PATH)
    if not table:
        unreal.log_error(f"[LootFind] failed to load datatable: {TABLE_PATH}")
        return

    row_struct = table.get_editor_property("row_struct")
    unreal.log_warning(f"[LootFind] table={TABLE_PATH}")
    unreal.log_warning(
        f"[LootFind] row_struct={row_struct.get_path_name() if row_struct else '<none>'}"
    )

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    unreal.log_warning(f"[LootFind] row_count={len(row_names)}")

    columns = unreal.DataTableFunctionLibrary.get_data_table_column_names(table)
    unreal.log_warning(f"[LootFind] columns={columns}")

    json_text = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if not json_text:
        unreal.log_error("[LootFind] failed to export DataTable json.")
        return

    unreal.log_warning(f"[LootFind] json_export={json_text}")

    # Dump all rows that look medical + all healing-like fields.
    medical_tokens = ["medical", "bandage", "syringe", "med", "heal", "health", "hp"]
    heal_field_tokens = ["param", "heal", "health", "hp", "quantity", "amount", "value", "recover", "restore"]

    try:
        parsed_root = json.loads(json_text)
    except Exception as ex:
        unreal.log_error(f"[LootFind] json parse failed: {ex}")
        return

    rows = []
    if isinstance(parsed_root, list):
        rows = parsed_root
    elif isinstance(parsed_root, dict):
        # UE JSON export sometimes emits dict keyed by row name.
        for k, v in parsed_root.items():
            if isinstance(v, dict):
                v = dict(v)
                v.setdefault("RowName", k)
                rows.append(v)

    unreal.log_warning(f"[LootFind] parsed_rows={len(rows)}")
    for row in rows:
        row_name_text = str(row.get("Name", row.get("RowName", "")))
        row_blob = json.dumps(row, ensure_ascii=False)
        if not lower_has_any(row_name_text, medical_tokens) and not lower_has_any(row_blob, medical_tokens):
            continue
        unreal.log_warning(f"[LootFind] ---- row: {row_name_text} ----")
        for key, value in row.items():
            key_text = str(key)
            if lower_has_any(key_text, heal_field_tokens):
                unreal.log_warning(f"[LootFind] field {row_name_text}.{key_text} = {value}")
        unreal.log_warning(f"[LootFind] row={row_blob}")


if __name__ == "__main__":
    main()
