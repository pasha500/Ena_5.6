import unreal


RIFLE_PACKAGE = "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene/BP_I_Rifle_Actor_For_Player"


def _asset_path(asset_data: unreal.AssetData) -> str:
    try:
        pkg = str(asset_data.package_name)
        name = str(asset_data.asset_name)
        if pkg and name:
            return f"{pkg}.{name}"
    except Exception:
        pass
    try:
        return asset_data.get_soft_object_path().get_asset_path_string()
    except Exception:
        return str(asset_data)


def _class_path(asset_data: unreal.AssetData) -> str:
    try:
        cp = asset_data.asset_class_path
        if hasattr(cp, "package_name") and hasattr(cp, "asset_name"):
            return f"{cp.package_name}.{cp.asset_name}"
        if hasattr(cp, "to_string"):
            return cp.to_string()
        return str(cp)
    except Exception:
        return "Unknown"


def _touch_blueprint_cdo(asset_data: unreal.AssetData):
    asset_path = _asset_path(asset_data)
    obj = unreal.load_object(None, asset_path)
    if not obj:
        unreal.log_warning(f"[RifleDepProbe] load failed: {asset_path}")
        return
    generated_class = getattr(obj, "generated_class", None)
    if callable(generated_class):
        generated_class = generated_class()
    if generated_class:
        _ = unreal.get_default_object(generated_class)


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    dep_opt = unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=True,
        include_soft_package_references=True,
        include_hard_management_references=False,
        include_soft_management_references=False,
        include_searchable_names=False,
    )
    deps = sorted([str(d) for d in registry.get_dependencies(RIFLE_PACKAGE, dep_opt)])
    unreal.log_warning(f"[RifleDepProbe] package={RIFLE_PACKAGE} deps={len(deps)}")

    idx = 0
    for dep_pkg in deps:
        if not dep_pkg.startswith("/Game/"):
            continue
        assets = registry.get_assets_by_package_name(dep_pkg, include_only_on_disk_assets=False)
        if not assets:
            continue
        for asset_data in assets:
            idx += 1
            asset_path = _asset_path(asset_data)
            class_path = _class_path(asset_data)
            unreal.log_warning(f"[RifleDepProbe] [{idx}] {asset_path} class={class_path}")
            if "Blueprint" in class_path:
                _touch_blueprint_cdo(asset_data)

    unreal.log_warning(f"[RifleDepProbe] completed asset_scan={idx}")


if __name__ == "__main__":
    run()
