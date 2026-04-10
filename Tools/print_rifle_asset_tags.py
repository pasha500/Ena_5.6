import unreal


def run():
    object_path = "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene/BP_I_Rifle_Actor_For_Player.BP_I_Rifle_Actor_For_Player"
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_data = registry.get_asset_by_object_path(object_path)
    if not asset_data or not asset_data.is_valid():
        unreal.log_error(f"[AssetTags] not found: {object_path}")
        return

    unreal.log_warning(f"[AssetTags] object={object_path}")
    for tag_name in ["ParentClass", "GeneratedClass", "NativeParentClass", "BlueprintType"]:
        try:
            value = asset_data.get_tag_value(tag_name)
            unreal.log_warning(f"[AssetTags] {tag_name}={value}")
        except Exception as exc:
            unreal.log_warning(f"[AssetTags] {tag_name}=<error {exc}>")

    all_tags = asset_data.tags_and_values
    unreal.log_warning(f"[AssetTags] tags_count={len(all_tags)}")
    for k, v in all_tags.items():
        unreal.log_warning(f"[AssetTags] {k}={v}")


if __name__ == "__main__":
    run()
