import unreal


ASSET_PATHS = [
    "/Game/Game/Particles/ps_Fire_01_04_Torch.ps_Fire_01_04_Torch",
    "/Game/TemplesOfCambodia/Demo/EpicContent/StarterContent/Particles/P_Ambient_Dust.P_Ambient_Dust",
    "/Engine/Tutorial/SubEditors/TutorialAssets/ParticleSystems/P_Fire.P_Fire",
    "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem",
    "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem_C",
]


def main():
    for path in ASSET_PATHS:
        obj = unreal.load_object(None, path)
        if not obj:
            unreal.log_warning(f"[LootType] load failed: {path}")
            continue
        cls = obj.get_class()
        unreal.log_warning(
            f"[LootType] {path} -> obj={obj.get_name()} class={cls.get_name()} class_path={cls.get_path_name()}"
        )

    bp_obj = unreal.load_object(
        None,
        "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem_C",
    )
    if not bp_obj:
        unreal.log_warning("[LootType] Medical item class load failed.")
        return

    cdo = unreal.get_default_object(bp_obj)
    if not cdo:
        unreal.log_warning("[LootType] Medical item CDO load failed.")
        return

    props = []
    for attr in dir(cdo):
        lower = attr.lower()
        if (
            "pick" in lower
            or "loot" in lower
            or "quantity" in lower
            or "inventory" in lower
            or "backpack" in lower
            or "medical" in lower
            or "heal" in lower
            or "param" in lower
        ):
            props.append(attr)

    props = sorted(set(props))
    unreal.log_warning(f"[LootType] medical CDO interesting attrs: {len(props)}")
    for name in props:
        try:
            value = cdo.get_editor_property(name)
            unreal.log_warning(f"[LootType]   {name} = {value}")
        except Exception:
            unreal.log_warning(f"[LootType]   {name} (property read failed)")


if __name__ == "__main__":
    main()
