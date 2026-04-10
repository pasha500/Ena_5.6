import unreal


def run():
    path = "/Game/AdvancedLocomotionV4/Blueprints/AnimModifiers/Create_Curves.Create_Curves"
    unreal.log_warning(f"[CreateCurvesProbe] loading {path}")
    bp = unreal.load_object(None, path)
    unreal.log_warning(f"[CreateCurvesProbe] loaded={bp}")
    if bp and hasattr(bp, "generated_class"):
        cls = getattr(bp, "generated_class")
        if callable(cls):
            cls = cls()
        unreal.log_warning(f"[CreateCurvesProbe] generated_class={cls}")
        if cls:
            cdo = unreal.get_default_object(cls)
            unreal.log_warning(f"[CreateCurvesProbe] cdo={cdo}")


if __name__ == "__main__":
    run()
