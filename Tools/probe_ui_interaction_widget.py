import unreal


def run():
    path = "/Game/AdvancedLocomotionV4/Blueprints/UI/UI_InteractionInputWithInfo.UI_InteractionInputWithInfo"
    unreal.log_warning(f"[UIProbe] loading {path}")
    obj = unreal.load_object(None, path)
    unreal.log_warning(f"[UIProbe] object={obj}")
    if obj and hasattr(obj, "generated_class"):
        cls = getattr(obj, "generated_class")
        if callable(cls):
            cls = cls()
        unreal.log_warning(f"[UIProbe] generated_class={cls}")
        if cls:
            cdo = unreal.get_default_object(cls)
            unreal.log_warning(f"[UIProbe] cdo={cdo}")


if __name__ == "__main__":
    run()
