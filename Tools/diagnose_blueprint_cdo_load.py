import unreal


def _asset_data_to_path(asset_data: unreal.AssetData) -> str:
    try:
        pkg = str(asset_data.package_name)
        name = str(asset_data.asset_name)
        if pkg and name:
            return f"{pkg}.{name}"
    except Exception:
        pass
    try:
        soft_path = asset_data.get_soft_object_path()
        if soft_path:
            return soft_path.get_asset_path_string()
    except Exception:
        pass
    try:
        return asset_data.package_name
    except Exception:
        return str(asset_data)


def _collect_blueprint_assets(paths):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(
        package_paths=paths,
        recursive_paths=True,
        class_paths=[
            unreal.TopLevelAssetPath("/Script/Engine", "Blueprint"),
            unreal.TopLevelAssetPath("/Script/Engine", "AnimBlueprint"),
        ],
        recursive_classes=True,
    )
    assets = registry.get_assets(flt)
    assets.sort(key=lambda a: _asset_data_to_path(a))
    return assets


def _touch_blueprint_cdo(asset_path: str) -> bool:
    obj = unreal.load_object(None, asset_path)
    if not obj:
        unreal.log_error(f"[CDOProbe] Load failed: {asset_path}")
        return False

    generated_class = None
    if hasattr(obj, "generated_class"):
        generated_class = getattr(obj, "generated_class")
        if callable(generated_class):
            generated_class = generated_class()

    if not generated_class:
        unreal.log_warning(f"[CDOProbe] No generated class: {asset_path}")
        return False

    _ = unreal.get_default_object(generated_class)
    return True


def run():
    probe_paths = [
        "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors",
        "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene",
        "/Game/AdvancedLocomotionV4/Blueprints/Abilities",
        "/Game/Game/AILevel/Blueprints",
    ]

    unreal.log_warning("[CDOProbe] Start blueprint CDO probe")
    unreal.log_warning(f"[CDOProbe] Probe paths: {probe_paths}")

    assets = _collect_blueprint_assets(probe_paths)
    unreal.log_warning(f"[CDOProbe] Found assets: {len(assets)}")

    ok_count = 0
    for idx, asset_data in enumerate(assets, start=1):
        asset_path = _asset_data_to_path(asset_data)
        unreal.log_warning(f"[CDOProbe] [{idx}/{len(assets)}] {asset_path}")
        if _touch_blueprint_cdo(asset_path):
            ok_count += 1

    unreal.log_warning(f"[CDOProbe] Completed. Success={ok_count}/{len(assets)}")


if __name__ == "__main__":
    run()
