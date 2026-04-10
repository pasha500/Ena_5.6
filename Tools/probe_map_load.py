import unreal


def run():
    target_map = "/Game/Levels/AI_Level_Test"
    unreal.log_warning(f"[MapProbe] loading map: {target_map}")
    loaded_world = unreal.EditorLoadingAndSavingUtils.load_map(target_map)
    unreal.log_warning(f"[MapProbe] load result: {loaded_world}")
    if loaded_world:
        unreal.log_warning(f"[MapProbe] world name: {loaded_world.get_name()}")


if __name__ == "__main__":
    run()
