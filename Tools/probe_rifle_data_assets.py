import unreal


def run():
    paths = [
        "/Game/AdvancedLocomotionV4/Data/Structs/Rifle_Assets_Struct.Rifle_Assets_Struct",
        "/Game/AdvancedLocomotionV4/Data/DataTables/RifleAssetsDataTable.RifleAssetsDataTable",
        "/Game/AdvancedLocomotionV4/Props/Meshes/Rifle/M4A1.M4A1",
    ]
    for path in paths:
        unreal.log_warning(f"[RifleDataProbe] loading {path}")
        obj = unreal.load_object(None, path)
        unreal.log_warning(f"[RifleDataProbe] loaded {obj}")
        if isinstance(obj, unreal.DataTable):
            names = obj.get_row_names()
            unreal.log_warning(f"[RifleDataProbe] datatable rows={len(names)} first={names[:8]}")


if __name__ == "__main__":
    run()
