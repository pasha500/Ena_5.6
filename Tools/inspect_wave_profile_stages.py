import unreal
import os


ASSET_PATH_CANDIDATES = [
    "/Game/Game/AILevel/Data/DT_RaidWaveProfile.DT_RaidWaveProfile",
    "/Game/Game/AILevel/Data/DT_RaidWaveProfile",
    "/Game/AILevel/Data/DT_RaidWaveProfile.DT_RaidWaveProfile",
    "/Game/AILevel/Data/DT_RaidWaveProfile",
]


def _load_asset():
    for path in ASSET_PATH_CANDIDATES:
        asset = unreal.load_asset(path)
        if asset:
            return asset
    return None


def main():
    asset = _load_asset()
    if not asset:
        raise RuntimeError("DT_RaidWaveProfile not found.")

    lines = []
    lines.append(f"[WaveProfile] Asset={asset.get_path_name()} Class={asset.get_class().get_name()}")
    lines.append(f"[WaveProfile] TotalDynamicWaves={asset.get_editor_property('total_dynamic_waves')}")

    stages = asset.get_editor_property("stages")
    lines.append(f"[WaveProfile] StageCount={len(stages)}")
    for idx, stage in enumerate(stages, start=1):
        label = stage.get_editor_property("wave_label")
        min_dist = stage.get_editor_property("spawn_min_distance_from_player")
        max_dist = stage.get_editor_property("spawn_max_distance_from_player")
        scatter = stage.get_editor_property("spawn_scatter_radius")
        enemies = stage.get_editor_property("enemies")
        lines.append(f"[WaveProfile] {idx:02d}: label={label} min={min_dist} max={max_dist} scatter={scatter} enemies={len(enemies)}")

    out_path = r"D:\UnrealProject\T_Proto\Tools\wave_profile_inspect_output.txt"
    with open(out_path, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))


if __name__ == "__main__":
    main()
