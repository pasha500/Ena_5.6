import unreal


TARGET_ASSETS = [
    "/Game/AdvancedLocomotionV4/Blueprints/CharacterLogic/ALS_Base_CharacterBP",
    "/Game/AdvancedLocomotionV4/Blueprints/Abilities/HandleForItemComponent",
    "/Game/AdvancedLocomotionV4/Blueprints/Components/BP_ItemsInteractionComponent",
    "/Game/AdvancedLocomotionV4/Blueprints/Abilities/ALS_Ability_PickupLootItem",
]


def is_interesting(text: str) -> bool:
    low = text.lower()
    tokens = [
        "loot",
        "medical",
        "heal",
        "health",
        "datatable",
        "pick",
        "ability",
        "item",
        "param",
        "inventory",
    ]
    return any(t in low for t in tokens)


def dump_asset_registry_dependencies(package_name: str) -> None:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()

    dep_opts = unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=True,
        include_soft_package_references=True,
        include_hard_management_references=False,
        include_soft_management_references=False,
        include_searchable_names=True,
    )
    deps = reg.get_dependencies(package_name, dep_opts)
    unreal.log_warning(f"[ALSRef] dependency_count={len(deps)} package={package_name}")
    for dep in deps:
        dep_text = str(dep)
        if is_interesting(dep_text):
            unreal.log_warning(f"[ALSRef] DEP: {dep_text}")

    refs = reg.get_referencers(package_name, dep_opts)
    unreal.log_warning(f"[ALSRef] referencer_count={len(refs)} package={package_name}")
    for ref in refs:
        ref_text = str(ref)
        if is_interesting(ref_text):
            unreal.log_warning(f"[ALSRef] REF: {ref_text}")


def dump_cdo_interesting_properties(class_path: str):
    cls = unreal.load_object(None, class_path)
    if not cls:
        unreal.log_warning(f"[ALSRef] failed to load class: {class_path}")
        return
    cdo = unreal.get_default_object(cls)
    if not cdo:
        unreal.log_error("[ALSRef] failed to get CDO.")
        return

    props = []
    try:
        props = cdo.get_editor_property_names()
    except Exception as ex:
        unreal.log_warning(f"[ALSRef] get_editor_property_names failed: {ex}")
        props = []

    interesting = [p for p in props if is_interesting(str(p))]
    unreal.log_warning(f"[ALSRef] cdo_interesting_prop_count={len(interesting)}")
    for name in sorted(interesting):
        try:
            value = cdo.get_editor_property(name)
            text = str(value)
            if is_interesting(text) or len(text) < 180:
                unreal.log_warning(f"[ALSRef] CDO {name} = {text}")
            else:
                unreal.log_warning(f"[ALSRef] CDO {name} = <non-empty>")
        except Exception as ex:
            unreal.log_warning(f"[ALSRef] CDO {name} <read failed: {ex}>")


def main():
    for asset_path in TARGET_ASSETS:
        bp_obj = unreal.load_asset(asset_path)
        if not bp_obj:
            unreal.log_warning(f"[ALSRef] failed to load asset: {asset_path}")
            continue

        package_name = bp_obj.get_outermost().get_name()
        unreal.log_warning(f"[ALSRef] loaded_asset={bp_obj.get_path_name()} package={package_name}")
        dump_asset_registry_dependencies(package_name)
        dump_cdo_interesting_properties(f"{asset_path}.{bp_obj.get_name()}_C")


if __name__ == "__main__":
    main()
