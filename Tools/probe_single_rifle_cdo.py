import unreal


def run():
    path = "/Game/AdvancedLocomotionV4/Blueprints/Actors/PlaceableOnScene/BP_I_Rifle_Actor_For_Player.BP_I_Rifle_Actor_For_Player"
    unreal.log_warning(f"[SingleRifleProbe] loading {path}")
    bp = unreal.load_object(None, path)
    unreal.log_warning(f"[SingleRifleProbe] loaded object={bp}")
    generated_class = getattr(bp, "generated_class", None) if bp else None
    if callable(generated_class):
        generated_class = generated_class()
    unreal.log_warning(f"[SingleRifleProbe] generated_class={generated_class}")
    cdo = unreal.get_default_object(generated_class)
    unreal.log_warning(f"[SingleRifleProbe] cdo={cdo}")


if __name__ == "__main__":
    run()
