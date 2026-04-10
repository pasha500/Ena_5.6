import unreal


def run():
    path = "/Game/AdvancedLocomotionV4/Blueprints/Interfaces/ALS_PickableWeapon_BPI.ALS_PickableWeapon_BPI"
    unreal.log_warning(f"[PickableInterfaceProbe] loading {path}")
    obj = unreal.load_object(None, path)
    unreal.log_warning(f"[PickableInterfaceProbe] object={obj}")
    if obj and hasattr(obj, "generated_class"):
        cls = getattr(obj, "generated_class")
        if callable(cls):
            cls = cls()
        unreal.log_warning(f"[PickableInterfaceProbe] generated_class={cls}")
        if cls:
            cdo = unreal.get_default_object(cls)
            unreal.log_warning(f"[PickableInterfaceProbe] cdo={cdo}")


if __name__ == "__main__":
    run()
