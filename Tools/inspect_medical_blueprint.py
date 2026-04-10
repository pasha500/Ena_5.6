import unreal


BP_PATH = "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem"


def lower_has_any(text: str, tokens):
    s = text.lower()
    return any(t in s for t in tokens)


def main():
    bp = unreal.load_object(None, BP_PATH)
    if not bp:
        unreal.log_error(f"[BPInspect] failed to load blueprint: {BP_PATH}")
        return

    unreal.log_warning(f"[BPInspect] blueprint class={bp.get_class().get_path_name()}")
    unreal.log_warning(f"[BPInspect] BlueprintEditorLibrary methods={ [m for m in dir(unreal.BlueprintEditorLibrary) if not m.startswith('_')] }")

    # Blueprint variable list
    variable_names = []
    try:
        variable_names = unreal.BlueprintEditorLibrary.get_blueprint_variable_list(bp)
    except Exception as ex:
        unreal.log_warning(f"[BPInspect] get_blueprint_variable_list failed: {ex}")

    unreal.log_warning(f"[BPInspect] variable_count={len(variable_names)}")
    for var_name in variable_names:
        name_text = str(var_name)
        if lower_has_any(name_text, ["heal", "health", "param", "quantity", "amount", "value", "medical", "hp", "recover", "restore"]):
            unreal.log_warning(f"[BPInspect] variable={name_text}")

    generated_class = bp.generated_class() if hasattr(bp, "generated_class") else None
    if not generated_class:
        generated_class = unreal.load_object(None, BP_PATH + "_C")
    if not generated_class:
        unreal.log_error("[BPInspect] failed to load generated class")
        return

    cdo = unreal.get_default_object(generated_class)
    unreal.log_warning(f"[BPInspect] cdo class={cdo.get_class().get_path_name()}")

    # Fallback: probe CDO attributes directly (UE 5.6 python API without variable list helper).
    candidate_attrs = []
    for attr in dir(cdo):
        if attr.startswith("_"):
            continue
        if not lower_has_any(attr, ["heal", "health", "param", "quantity", "amount", "value", "medical", "hp", "recover", "restore"]):
            continue
        candidate_attrs.append(attr)

    unreal.log_warning(f"[BPInspect] cdo candidate_attr_count={len(candidate_attrs)}")
    for attr in sorted(set(candidate_attrs)):
        unreal.log_warning(f"[BPInspect] cdo_candidate_attr={attr}")
        try:
            value = cdo.get_editor_property(attr)
            unreal.log_warning(f"[BPInspect] cdo_prop {attr} = {value}")
        except Exception:
            pass

    # Try reading defaults by blueprint-declared variable names (if available).
    for var_name in variable_names:
        name_text = str(var_name)
        if not lower_has_any(name_text, ["heal", "health", "param", "quantity", "amount", "value", "medical", "hp", "recover", "restore"]):
            continue
        try:
            value = cdo.get_editor_property(name_text)
            unreal.log_warning(f"[BPInspect] default {name_text} = {value}")
        except Exception as ex:
            unreal.log_warning(f"[BPInspect] default {name_text} read failed: {ex}")


if __name__ == "__main__":
    main()
