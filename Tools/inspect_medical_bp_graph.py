import unreal


BP_PATH = "/Game/AdvancedLocomotionV4/Blueprints/Actors/LootActors/BP_LootPickableItem_MedicalItem.BP_LootPickableItem_MedicalItem"


def lower_has_any(text: str, tokens):
    s = text.lower()
    return any(t in s for t in tokens)


def try_float(text: str):
    try:
        return float(text)
    except Exception:
        return None


def main():
    bp = unreal.load_object(None, BP_PATH)
    if not bp:
        unreal.log_error(f"[BPGraph] failed load blueprint: {BP_PATH}")
        return

    unreal.log_warning(f"[BPGraph] loaded blueprint: {bp.get_path_name()}")
    graph_props = ["ubergraph_pages", "function_graphs", "macro_graphs", "delegate_signature_graphs"]
    all_graphs = []
    for prop in graph_props:
        try:
            graphs = bp.get_editor_property(prop)
            if graphs:
                unreal.log_warning(f"[BPGraph] {prop} count={len(graphs)}")
                all_graphs.extend(graphs)
        except Exception:
            pass

    seen = set()
    for graph in all_graphs:
        if not graph:
            continue
        gpath = graph.get_path_name()
        if gpath in seen:
            continue
        seen.add(gpath)
        unreal.log_warning(f"[BPGraph] ---- graph: {graph.get_name()} ----")
        nodes = []
        try:
            nodes = graph.get_editor_property("nodes")
        except Exception:
            pass
        unreal.log_warning(f"[BPGraph] node_count={len(nodes)}")
        for node in nodes:
            if not node:
                continue
            node_name = node.get_name()
            node_class = node.get_class().get_name()
            title_text = ""
            try:
                title_text = str(node.get_node_title(unreal.NodeTitleType.FULL_TITLE))
            except Exception:
                title_text = node_name

            log_node = lower_has_any(node_name + " " + node_class + " " + title_text, ["health", "heal", "medical", "param", "quantity", "add", "restore", "recover"])

            pin_lines = []
            pins = []
            try:
                pins = node.get_editor_property("pins")
            except Exception:
                pass
            for pin in pins:
                try:
                    pname = pin.get_name()
                    pdefault = str(pin.get_editor_property("default_value"))
                except Exception:
                    continue

                if lower_has_any(pname + " " + pdefault, ["health", "heal", "medical", "param", "quantity", "add", "restore", "recover"]):
                    pin_lines.append(f"{pname}={pdefault}")
                    continue

                fv = try_float(pdefault)
                if fv is not None and abs(fv) > 0.0001:
                    # numeric literal pins are useful when hunting hardcoded heal amount
                    pin_lines.append(f"{pname}={pdefault}")

            if log_node or pin_lines:
                unreal.log_warning(f"[BPGraph] node {node_class} :: {title_text}")
                for line in pin_lines[:10]:
                    unreal.log_warning(f"[BPGraph]   pin {line}")


if __name__ == "__main__":
    main()
