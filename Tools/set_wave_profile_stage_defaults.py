import unreal


ASSET_PATH_CANDIDATES = [
    "/Game/Game/AILevel/Data/DT_RaidWaveProfile.DT_RaidWaveProfile",
    "/Game/Game/AILevel/Data/DT_RaidWaveProfile",
    "/Game/AILevel/Data/DT_RaidWaveProfile.DT_RaidWaveProfile",
    "/Game/AILevel/Data/DT_RaidWaveProfile",
]


STAGE_SPECS = [
    {"label": "Wave 01 - Contact", "min": 6500.0, "max": 12000.0, "scatter": 1800.0},
    {"label": "Wave 02 - Probe", "min": 6300.0, "max": 11800.0, "scatter": 1750.0},
    {"label": "Wave 03 - Pressure", "min": 6100.0, "max": 11500.0, "scatter": 1700.0},
    {"label": "Wave 04 - Fireline", "min": 5900.0, "max": 11200.0, "scatter": 1650.0},
    {"label": "Wave 05 - Encircle", "min": 5700.0, "max": 10800.0, "scatter": 1600.0},
    {"label": "Wave 06 - Reinforce", "min": 5500.0, "max": 10400.0, "scatter": 1550.0},
    {"label": "Wave 07 - Hunt", "min": 5300.0, "max": 10000.0, "scatter": 1450.0},
    {"label": "Wave 08 - Collapse", "min": 5000.0, "max": 9600.0, "scatter": 1350.0},
    {"label": "Wave 09 - Last Push", "min": 4700.0, "max": 9000.0, "scatter": 1250.0},
    {"label": "Wave 10 - Boss", "min": 4400.0, "max": 8400.0, "scatter": 1150.0},
]


def _to_text(label: str):
    try:
        return unreal.Text.from_string(label)
    except Exception:
        return label


def _load_profile_asset():
    for path in ASSET_PATH_CANDIDATES:
        asset = unreal.load_asset(path)
        if asset:
            return asset
    return None


def _apply_stage_defaults(profile_asset):
    class_name = profile_asset.get_class().get_name()
    if class_name != "RaidWaveProfile":
        raise RuntimeError(
            f"Asset class mismatch. Expected RaidWaveProfile, got {class_name}. "
            f"Asset={profile_asset.get_path_name()}"
        )

    new_stages = []
    for spec in STAGE_SPECS:
        stage = unreal.RaidWaveStage()
        stage.set_editor_property("wave_label", _to_text(spec["label"]))
        stage.set_editor_property("spawn_min_distance_from_player", float(spec["min"]))
        stage.set_editor_property("spawn_max_distance_from_player", float(spec["max"]))
        stage.set_editor_property("spawn_scatter_radius", float(spec["scatter"]))
        stage.set_editor_property("enemies", [])
        new_stages.append(stage)

    profile_asset.set_editor_property("stages", new_stages)

    # Ensure total waves is consistent with template.
    if int(profile_asset.get_editor_property("total_dynamic_waves")) != len(STAGE_SPECS):
        profile_asset.set_editor_property("total_dynamic_waves", len(STAGE_SPECS))

    profile_asset.modify(True)

    object_path = profile_asset.get_path_name()
    package_path = object_path.split(".")[0]
    unreal.EditorAssetLibrary.save_asset(package_path, only_if_is_dirty=False)
    unreal.log(f"[WaveProfile] Updated and saved: {package_path}")


def main():
    profile_asset = _load_profile_asset()
    if not profile_asset:
        raise RuntimeError(f"DT_RaidWaveProfile not found in candidates: {ASSET_PATH_CANDIDATES}")

    _apply_stage_defaults(profile_asset)


if __name__ == "__main__":
    main()

