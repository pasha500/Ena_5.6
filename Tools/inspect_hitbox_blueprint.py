import unreal


TARGETS = [
    "/Game/CombatFury/CF_Assets/Components/HitboxComponent/BluePrint/AC_Hitbox",
    "/Game/CombatFury/CF_Assets/Components/HitboxComponent/BluePrint/BI_Hitbox",
]


def log(msg: str):
    unreal.log_warning(f"[HitboxInspect] {msg}")


def _safe_call(fn, *args, **kwargs):
    try:
        return fn(*args, **kwargs)
    except Exception as exc:
        log(f"{fn.__name__} failed: {exc}")
        return None


def inspect_generated_class(asset_path: str):
    klass = _safe_call(unreal.EditorAssetLibrary.load_blueprint_class, asset_path)
    if not klass:
        log(f"{asset_path}: blueprint class load failed")
        return

    cdo = _safe_call(unreal.get_default_object, klass)
    if not cdo:
        log(f"{asset_path}: CDO load failed")
        return

    trace_like = []
    for name in dir(cdo):
        lowered = name.lower()
        if "trace" in lowered or "projectile" in lowered or "hitbox" in lowered:
            trace_like.append(name)

    log(f"{asset_path}: CDO trace-related attrs count={len(trace_like)}")
    for attr in sorted(trace_like)[:120]:
        value = None
        try:
            value = getattr(cdo, attr)
        except Exception:
            value = "<unreadable>"
        rendered = str(value)
        if len(rendered) > 140:
            rendered = rendered[:140] + "..."
        log(f"  attr {attr} = {rendered}")


def inspect_blueprint_asset(asset_path: str):
    bp = _safe_call(unreal.EditorAssetLibrary.load_asset, asset_path)
    if not bp:
        log(f"{asset_path}: asset load failed")
        return

    log(f"{asset_path}: asset class={bp.get_class().get_name()}")

    bp_lib = getattr(unreal, "BlueprintEditorLibrary", None)
    if not bp_lib:
        log("BlueprintEditorLibrary not available")
        return

    if hasattr(bp_lib, "get_blueprint_variable_list") and hasattr(bp_lib, "get_blueprint_variable_type"):
        var_list = _safe_call(bp_lib.get_blueprint_variable_list, bp)
        if var_list is not None:
            log(f"{asset_path}: variable_count={len(var_list)}")
            for var_name in var_list:
                vname = str(var_name)
                if "Trace" in vname or "trace" in vname or "Projectile" in vname or "projectile" in vname:
                    vtype = _safe_call(bp_lib.get_blueprint_variable_type, bp, var_name)
                    log(f"  var {vname} type={vtype}")
    else:
        # UE python APIs vary by version; fall back to Blueprint.NewVariables reflection.
        if hasattr(bp, "new_variables"):
            vars_raw = getattr(bp, "new_variables", [])
            log(f"{asset_path}: new_variables_count={len(vars_raw)}")
            for var_desc in vars_raw:
                try:
                    vname = str(var_desc.var_name)
                    if "Trace" in vname or "trace" in vname or "Projectile" in vname or "projectile" in vname:
                        log(f"  var {vname}")
                except Exception:
                    continue
        else:
            log(f"{asset_path}: no variable introspection API available")

    if hasattr(bp_lib, "get_all_graphs"):
        graphs = _safe_call(bp_lib.get_all_graphs, bp)
        if graphs is not None:
            log(f"{asset_path}: graph_count={len(graphs)}")
            for graph in graphs:
                gname = graph.get_name()
                if "Trace" in gname or "trace" in gname or "Ranged" in gname or "Projectile" in gname:
                    log(f"  graph {gname}")
                    nodes = _safe_call(graph.get_nodes)
                    if nodes is None:
                        continue
                    log(f"    nodes={len(nodes)}")
                    for node in nodes[:80]:
                        log(f"      {node.get_class().get_name()} | {node.get_name()}")


def main():
    for path in TARGETS:
        inspect_blueprint_asset(path)
        inspect_generated_class(path)

    log("done")


if __name__ == "__main__":
    main()
