import unreal


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(
        package_paths=["/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene"],
        recursive_paths=True,
        class_paths=[unreal.TopLevelAssetPath("/Script/CoreUObject", "ObjectRedirector")],
        recursive_classes=True,
    )
    assets = registry.get_assets(flt)
    unreal.log_warning(f"[RedirectorProbe] count={len(assets)}")
    for a in assets:
        try:
            path = f"{a.package_name}.{a.asset_name}"
        except Exception:
            path = str(a)
        unreal.log_warning(f"[RedirectorProbe] {path}")


if __name__ == "__main__":
    run()
