import os
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(r"D:\UnrealProject\T_Proto")
OUT_REPORT = ROOT / "Tools" / "MECE_Audit_Report.md"

CODE_DIRS = [
    ROOT / "Source",
    ROOT / "Plugins",
    ROOT / "Tools",
    ROOT / "Config",
]

INCLUDE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cs", ".py", ".ini", ".json", ".md"}
EXCLUDE_DIR_NAMES = {
    ".git",
    ".vs",
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
}

TODO_PATTERNS = re.compile(r"\b(TODO|FIXME|HACK|XXX)\b", re.IGNORECASE)
ASSET_PATH_PATTERN = re.compile(r'["\'](/Game/[^"\']+)["\']')
SYNC_LOAD_PATTERN = re.compile(r"\b(LoadSynchronous|TryLoad|LoadClass<|TryLoadClass<)\b")


def iter_files():
    for base in CODE_DIRS:
        if not base.exists():
            continue
        for root, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in EXCLUDE_DIR_NAMES]
            root_path = Path(root)
            for name in files:
                path = root_path / name
                if path.suffix.lower() in INCLUDE_SUFFIXES:
                    yield path


def count_lines(path: Path) -> int:
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            return sum(1 for _ in f)
    except Exception:
        return 0


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return ""


def main():
    self_path = Path(__file__).resolve()
    out_report_path = OUT_REPORT.resolve()
    files = []
    for candidate in iter_files():
        resolved = candidate.resolve()
        if resolved == self_path or resolved == out_report_path:
            continue
        files.append(candidate)

    by_ext = Counter(p.suffix.lower() for p in files)
    line_counts = []
    todo_hits = []
    asset_path_hits = defaultdict(list)
    sync_load_hits = []
    tick_sync_load_risk = []

    for path in files:
        rel = path.relative_to(ROOT)
        text = read_text(path)
        lines = text.splitlines()
        lc = len(lines)
        line_counts.append((lc, rel))

        # TODO-like markers
        for idx, line in enumerate(lines, start=1):
            if TODO_PATTERNS.search(line):
                todo_hits.append((rel, idx, line.strip()))

        # Asset-path references
        for m in ASSET_PATH_PATTERN.finditer(text):
            asset_path_hits[m.group(1)].append(str(rel))

        # Sync load patterns
        for idx, line in enumerate(lines, start=1):
            if SYNC_LOAD_PATTERN.search(line):
                sync_load_hits.append((rel, idx, line.strip()))

        # Heuristic: Tick/Update function + sync load in same file
        has_tick = (" Tick(" in text) or ("::Tick(" in text)
        has_sync_load = bool(SYNC_LOAD_PATTERN.search(text))
        if has_tick and has_sync_load and path.suffix.lower() in {".cpp", ".h"}:
            tick_sync_load_risk.append(rel)

    line_counts.sort(reverse=True, key=lambda x: x[0])
    top_large_files = [x for x in line_counts if x[0] >= 1200][:20]

    duplicated_asset_refs = []
    for asset_path, refs in asset_path_hits.items():
        unique_refs = sorted(set(refs))
        if len(unique_refs) >= 3:
            duplicated_asset_refs.append((asset_path, unique_refs))
    duplicated_asset_refs.sort(key=lambda x: len(x[1]), reverse=True)

    sync_hits_by_file = Counter(str(r) for r, _, _ in sync_load_hits)
    top_sync_files = sync_hits_by_file.most_common(20)

    report = []
    report.append("# MECE Audit Report")
    report.append("")
    report.append(f"- Root: `{ROOT}`")
    report.append(f"- Scanned files: **{len(files)}**")
    report.append("")
    report.append("## 1) Scope Inventory")
    report.append("")
    for ext, cnt in sorted(by_ext.items(), key=lambda x: (-x[1], x[0])):
        report.append(f"- `{ext}`: {cnt}")

    report.append("")
    report.append("## 2) Complexity Hotspots (Line Count >= 1200)")
    report.append("")
    if not top_large_files:
        report.append("- None")
    else:
        for lc, rel in top_large_files:
            report.append(f"- `{rel}`: {lc} lines")

    report.append("")
    report.append("## 3) TODO/FIXME/HACK Markers")
    report.append("")
    if not todo_hits:
        report.append("- None")
    else:
        for rel, ln, line in todo_hits[:80]:
            report.append(f"- `{rel}:{ln}` `{line}`")
        if len(todo_hits) > 80:
            report.append(f"- ... ({len(todo_hits) - 80} more)")

    report.append("")
    report.append("## 4) Sync Load Usage (Potential Runtime Cost)")
    report.append("")
    if not top_sync_files:
        report.append("- None")
    else:
        for rel, cnt in top_sync_files:
            report.append(f"- `{rel}`: {cnt} hits")

    report.append("")
    report.append("### Tick + SyncLoad Heuristic Risk")
    report.append("")
    if not tick_sync_load_risk:
        report.append("- None")
    else:
        for rel in sorted(set(tick_sync_load_risk)):
            report.append(f"- `{rel}`")

    report.append("")
    report.append("## 5) Duplicated Hardcoded Asset Paths")
    report.append("")
    if not duplicated_asset_refs:
        report.append("- None")
    else:
        for asset_path, refs in duplicated_asset_refs[:30]:
            report.append(f"- `{asset_path}` referenced in {len(refs)} files")
            for ref in refs[:6]:
                report.append(f"  - `{ref}`")
            if len(refs) > 6:
                report.append(f"  - ... ({len(refs) - 6} more)")

    report.append("")
    report.append("## 6) MECE Recommendations")
    report.append("")
    report.append("- Split hotspot files by responsibility boundary (`Layout`, `Combat`, `LootBinding`, `UIBanner`, `Wave`) to reduce coupling.")
    report.append("- Keep a single source-of-truth per gameplay value (e.g. medical heal in `LootItemsDataTable`).")
    report.append("- Move hardcoded asset paths to configurable `UPROPERTY(TSoftObjectPtr<...>)` fields in data/config assets.")
    report.append("- Gate non-critical `Warning` logs behind debug flags to reduce runtime log overhead.")
    report.append("- Keep `Tools` separated into Production vs Diagnostics; avoid shipping one-off scripts in runtime path.")

    OUT_REPORT.write_text("\n".join(report), encoding="utf-8")
    print(f"[MECE-AUDIT] report generated: {OUT_REPORT}")


if __name__ == "__main__":
    main()
