import unreal


MAP_PACKAGE = "/Game/Levels/AI_Level_Test"


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
        try:
            return str(asset_data.package_name)
        except Exception:
            return str(asset_data)


def _class_path(asset_data: unreal.AssetData) -> str:
    try:
        cp = asset_data.asset_class_path
        if hasattr(cp, "to_string"):
            return cp.to_string()
        return str(cp)
    except Exception:
        try:
            return asset_data.asset_class
        except Exception:
            return "Unknown"


def _is_blueprint_asset(asset_data: unreal.AssetData) -> bool:
    class_path = _class_path(asset_data)
    lowered = class_path.lower()
    return (
        "blueprint" in lowered
        or "animblueprint" in lowered
        or "widgetblueprint" in lowered
    )


def _touch_cdo(asset_data: unreal.AssetData):
    path = _asset_path(asset_data)
    obj = unreal.load_object(None, path)
    if not obj:
        unreal.log_warning(f"[MapDepProbe] load_object failed: {path}")
        return
    if not hasattr(obj, "generated_class"):
        unreal.log_warning(f"[MapDepProbe] no generated_class: {path}")
        return
    cls = getattr(obj, "generated_class")
    if callable(cls):
        cls = cls()
    if not cls:
        unreal.log_warning(f"[MapDepProbe] generated_class is None: {path}")
        return
    _ = unreal.get_default_object(cls)


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    dep_opt = unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=True,
        include_soft_package_references=True,
        include_hard_management_references=False,
        include_soft_management_references=False,
        include_searchable_names=False,
    )

    deps = registry.get_dependencies(MAP_PACKAGE, dep_opt)
    dep_list = sorted([str(d) for d in deps if str(d).startswith("/Game/")])
    unreal.log_warning(f"[MapDepProbe] map={MAP_PACKAGE} deps={len(dep_list)}")

    idx = 0
    for dep_pkg in dep_list:
        assets = registry.get_assets_by_package_name(dep_pkg, include_only_on_disk_assets=False)
        if not assets:
            continue
        for asset_data in assets:
            if not _is_blueprint_asset(asset_data):
                continue
            idx += 1
            path = _asset_path(asset_data)
            class_path = _class_path(asset_data)
            unreal.log_warning(f"[MapDepProbe] [{idx}] {path} class={class_path}")
            _touch_cdo(asset_data)

    unreal.log_warning(f"[MapDepProbe] completed. blueprint_assets={idx}")


if __name__ == "__main__":
    run()
