import unreal


def _asset_class_name(asset_data: unreal.AssetData) -> str:
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        try:
            return str(asset_data.asset_class)
        except Exception:
            return ""


def _compile_blueprint(bp: unreal.Blueprint) -> bool:
    bp_editor_lib = getattr(unreal, "BlueprintEditorLibrary", None)
    if bp_editor_lib and hasattr(bp_editor_lib, "refresh_all_nodes"):
        try:
            bp_editor_lib.refresh_all_nodes(bp)
        except Exception as exc:
            unreal.log_warning(f"[HitboxRepair] refresh_all_nodes failed for {bp.get_path_name()}: {exc}")

    if bp_editor_lib and hasattr(bp_editor_lib, "compile_blueprint"):
        try:
            bp_editor_lib.compile_blueprint(bp)
            return True
        except Exception as exc:
            unreal.log_error(f"[HitboxRepair] compile(BlueprintEditorLibrary) failed for {bp.get_path_name()}: {exc}")
            return False

    kismet_utils = getattr(unreal, "KismetEditorUtilities", None)
    if kismet_utils and hasattr(kismet_utils, "compile_blueprint"):
        try:
            kismet_utils.compile_blueprint(bp)
            return True
        except Exception as exc:
            unreal.log_error(f"[HitboxRepair] compile(KismetEditorUtilities) failed for {bp.get_path_name()}: {exc}")
            return False

    unreal.log_warning(f"[HitboxRepair] compile API unavailable for {bp.get_path_name()}, skipping compile.")
    return False


def _collect_assets_from_folders(folders):
    result = set()
    for folder in folders:
        if not unreal.EditorAssetLibrary.does_directory_exist(folder):
            unreal.log_warning(f"[HitboxRepair] Missing folder: {folder}")
            continue
        for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
            result.add(path)
    return sorted(result)


def main():
    target_folders = [
        "/Game/CombatFury/CF_Assets/Components/HitboxComponent",
        "/Game/CombatFury/CF_Assets/Components/HitReactionComponent",
        "/Game/CombatFury/CF_Assets/Components/ECB_Component/Structures",
        "/Game/AdvancedLocomotionV4/Blueprints/UI/DefaultOverlayMenu",
        "/Game/AdvancedLocomotionV4/Blueprints/UI/WheeledMenu",
        "/Game/AdvancedLocomotionV4/Data/Structs",
    ]

    target_assets = _collect_assets_from_folders(target_folders)
    if not target_assets:
        unreal.log_error("[HitboxRepair] No assets found in target folders.")
        return

    compiled_count = 0
    saved_count = 0
    skipped_count = 0

    for object_path in target_assets:
        asset_data = unreal.EditorAssetLibrary.find_asset_data(object_path)
        if not asset_data or not asset_data.is_valid():
            skipped_count += 1
            continue

        class_name = _asset_class_name(asset_data)
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        if not asset:
            skipped_count += 1
            unreal.log_warning(f"[HitboxRepair] Failed to load: {object_path}")
            continue

        is_blueprint_like = isinstance(asset, unreal.Blueprint) or class_name.endswith("Blueprint")
        if is_blueprint_like:
            if _compile_blueprint(asset):
                compiled_count += 1
            if unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                saved_count += 1
            continue

        if class_name == "UserDefinedStruct":
            if unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                saved_count += 1
            continue

        if class_name == "UserDefinedEnum":
            if unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                saved_count += 1
            continue

        skipped_count += 1

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log_warning(
        f"[HitboxRepair] Done. Compiled={compiled_count}, Saved={saved_count}, Skipped={skipped_count}, Total={len(target_assets)}"
    )


if __name__ == "__main__":
    main()
