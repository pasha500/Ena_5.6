import unreal


def run():
    cls = unreal.load_class(None, "/Script/HelpfulFunctions.InteractiveActor")
    unreal.log_warning(f"[InteractiveNativeProbe] class={cls}")
    if cls:
        cdo = unreal.get_default_object(cls)
        unreal.log_warning(f"[InteractiveNativeProbe] cdo={cdo}")


if __name__ == "__main__":
    run()
