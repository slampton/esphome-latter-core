"""Tarjan SCC include-graph cycle detector and L0-L4 DAG layer boundary enforcement."""

from __future__ import annotations

import ast
import re
from pathlib import Path
from typing import Final

from .common import ROOT, iter_repo_files, strip_cpp_comments

L0_UI_HEADERS: Final[frozenset[str]] = frozenset(
    {
        "ui_panel_traits.h",
        "ui_color.h",
        "ui_surface.h",
        "ui_widget_ids.h",
        "ui_panel_metrics.h",
        "ui_hit_table.h",
        "ui_page_geoms.h",
        "ui_types.h",
    }
)
L2_UI_HEADERS: Final[frozenset[str]] = frozenset(
    {
        "ui_mdi.h",
        "ui_touch_recognizer.h",
        "ui_dirty_band.h",
        "ui_frame_context.h",
        "ui_cadence.h",
        "ui_gfx.h",
        "ui_layout.h",
        "ui_widgets.h",
        "ui_compositor.h",
    }
)
L3_UI_HEADERS: Final[frozenset[str]] = frozenset(
    {
        "ui_pages.h",
        "ui_touch_engine.h",
        "ui_esphome_display.h",
    }
)

LOCAL_INC_RE: Final[re.Pattern[str]] = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
QUOTED_INC_RE: Final[re.Pattern[str]] = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def _resolve_local_include(src_path: Path, inc_spec: str) -> Path | None:
    """Resolve a quoted `#include "..."` to an in-repo header/source path if one exists."""
    search_bases = (
        src_path.parent,
        ROOT / "include",
        ROOT / "components" / "latter_core",
        ROOT / "components" / "latter_ui",
        ROOT / "components" / "latter_beacon",
    )
    for base in search_bases:
        cand = (base / inc_spec).resolve()
        if cand.is_file() and ROOT in cand.parents:
            return cand
    return None


def _tarjan_sccs(graph: dict[str, set[str]]) -> list[list[str]]:
    """Compute Tarjan strongly connected components returning cycles (`len > 1` or self-loop)."""
    index = 0
    indices: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    cycles: list[list[str]] = []

    def strongconnect(node: str) -> None:
        nonlocal index
        indices[node] = index
        lowlink[node] = index
        index += 1
        stack.append(node)
        on_stack.add(node)

        for nxt in sorted(graph.get(node, ())):
            if nxt not in indices:
                strongconnect(nxt)
                lowlink[node] = min(lowlink[node], lowlink[nxt])
            elif nxt in on_stack:
                lowlink[node] = min(lowlink[node], indices[nxt])

        if lowlink[node] == indices[node]:
            scc: list[str] = []
            while True:
                w = stack.pop()
                on_stack.remove(w)
                scc.append(w)
                if w == node:
                    break
            if len(scc) > 1 or node in graph.get(node, ()):
                cycles.append(sorted(scc))

    for vertex in sorted(graph):
        if vertex not in indices:
            strongconnect(vertex)
    return cycles


def verify_include_dag_no_cycles(failures: list[str]) -> None:
    """Build `#include "..."` graph across `include/` and `components/` and assert zero SCC cycles."""
    cpp_files = [
        p
        for base in (ROOT / "include", ROOT / "components")
        for p in iter_repo_files(base)
        if p.suffix.lower() in {".h", ".hpp", ".cpp"}
    ]
    graph: dict[str, set[str]] = {}
    for path in cpp_files:
        rel = path.relative_to(ROOT).as_posix()
        clean = strip_cpp_comments(path.read_text(encoding="utf-8"))
        edges: set[str] = set()
        for match in QUOTED_INC_RE.finditer(clean):
            resolved = _resolve_local_include(path, match.group(1))
            if resolved is not None:
                edges.add(resolved.relative_to(ROOT).as_posix())
        graph[rel] = edges

    failures.extend(
        f"[DAG CYCLE] Circular #include dependency detected: {' <-> '.join(cyc)}"
        for cyc in _tarjan_sccs(graph)
    )


def verify_flat_component_sources(failures: list[str]) -> None:
    """Ban subdirectories containing `.cpp`/`.c` under `components/*` (`recursive_sources = False`)."""
    comp_root = ROOT / "components"
    if not comp_root.is_dir():
        return
    for comp_dir in sorted(p for p in comp_root.iterdir() if p.is_dir()):
        for src in iter_repo_files(comp_dir):
            if src.suffix.lower() in {".cpp", ".c"} and src.parent != comp_dir:
                rel = src.relative_to(ROOT).as_posix()
                failures.append(
                    f"[FLAT COMPONENT SOURCES] {rel} is in a component subdirectory; "
                    f"ESPHome ComponentManifest has recursive_sources=False — place directly in components/{comp_dir.name}/"
                )


def _check_ui_header_layers(failures: list[str]) -> None:
    """Enforce L0/L2/L3 purity on `include/ui_*.h` headers."""
    all_known_ui = L0_UI_HEADERS | L2_UI_HEADERS | L3_UI_HEADERS
    for hdr in sorted((ROOT / "include").glob("ui_*.h")):
        name = hdr.name
        rel = hdr.relative_to(ROOT).as_posix()
        if name not in all_known_ui:
            failures.append(f"[DAG LAYER] Unclassified UI header {rel} (assign to L0/L2/L3 in dag_checks.py)")
            continue
        clean = strip_cpp_comments(hdr.read_text(encoding="utf-8"))
        incs = [m.group(1) for m in LOCAL_INC_RE.finditer(clean)]
        inc_basenames = {Path(spec).name for spec in incs}

        if name != "ui_esphome_display.h" and any(spec.startswith("esphome/") for spec in incs):
            failures.append(
                f"[DAG LAYER] {rel} includes esphome/* header (only L3 ui_esphome_display.h may include esphome/*)"
            )
        if name != "ui_esphome_display.h" and any(Path(spec).name.startswith("hal_") for spec in incs):
            failures.append(
                f"[DAG LAYER] {rel} includes hal_*.h (only L3 ui_esphome_display.h bridge may include L1 hal_*.h)"
            )

        if name in L0_UI_HEADERS:
            if any(spec.startswith("state/") or Path(spec).name.startswith("state_") for spec in incs):
                failures.append(f"[DAG LAYER] L0 leaf {rel} must not include state/* headers")
            upward = sorted(inc_basenames & (L2_UI_HEADERS | L3_UI_HEADERS))
            if upward:
                failures.append(f"[DAG LAYER] L0 leaf {rel} includes L2/L3 UI header(s): {upward}")
        elif name in L2_UI_HEADERS:
            upward = sorted(inc_basenames & L3_UI_HEADERS)
            if upward:
                failures.append(f"[DAG LAYER] L2 header {rel} includes L3 UI header(s): {upward}")


def _check_core_to_ui_inversion(failures: list[str]) -> None:
    """Ensure `latter_core`, `include/state/*`, `include/hal_*`, and `latter_*.h` never depend on `latter_ui`."""
    core_files: list[Path] = [
        p
        for p in iter_repo_files(ROOT / "include")
        if p.suffix.lower() in {".h", ".hpp"} and not p.name.startswith("ui_")
    ] + [
        p
        for p in iter_repo_files(ROOT / "components" / "latter_core")
        if p.suffix.lower() in {".h", ".hpp", ".cpp"}
    ]
    ui_beacon_files = [
        p
        for p in iter_repo_files(ROOT / "components" / "latter_beacon")
        if p.suffix.lower() in {".h", ".hpp", ".cpp"}
    ]
    for path in core_files + ui_beacon_files:
        rel = path.relative_to(ROOT).as_posix()
        clean = strip_cpp_comments(path.read_text(encoding="utf-8"))
        for match in LOCAL_INC_RE.finditer(clean):
            target_name = Path(match.group(1)).name
            if target_name.startswith("ui_"):
                failures.append(f"[DAG INVERSION] {rel} includes UI header '{match.group(1)}' (L0/L1 -> L2/L3 banned)")
        if re.search(r"\bvoid\s+mark_dirty_(?:full|band)\s*\(|\blatter_ui::mark_dirty_(?:full|band)\s*\(", clean):
            failures.append(
                f"[DAG INVERSION] {rel} forward-declares or calls latter_ui::mark_dirty_* "
                "(use state.nav.ui_dirty_full / DeviceCommandEffects::mark_dirty_full)"
            )


def _check_ui_cpp_boundaries(failures: list[str]) -> None:
    """Enforce L2 vs L3 `.cpp` include rules and cross-page isolation inside `components/latter_ui/`."""
    ui_dir = ROOT / "components" / "latter_ui"
    if not ui_dir.is_dir():
        return
    l2_exact = {
        "ui_gfx.cpp",
        "ui_widgets.cpp",
        "ui_compositor.cpp",
        "hit_table.cpp",
        "ui_mdi.cpp",
        "overlay_takeover.cpp",
    }

    for path in sorted(ui_dir.glob("*.cpp")):
        name = path.name
        rel = path.relative_to(ROOT).as_posix()
        clean = strip_cpp_comments(path.read_text(encoding="utf-8"))
        incs = [m.group(1) for m in LOCAL_INC_RE.finditer(clean)]
        inc_names = {Path(spec).name for spec in incs}

        if name in l2_exact:
            upward = sorted(inc_names & L3_UI_HEADERS)
            if upward:
                failures.append(f"[DAG LAYER] L2 TU {rel} includes L3 header(s): {upward}")

        if name not in {"ui_gfx.cpp", "ui_mdi.cpp"} and any(
            spec in {"esp_heap_caps.h", "driver/ppa.h", "esp_async_memcpy.h", "esp_cache.h"} for spec in incs
        ):
            failures.append(
                f"[DAG LAYER] {rel} includes ESP-IDF heap/DMA header directly (use ui_psram_* in ui_gfx.h)"
            )

        if name == "page_system.cpp" and "esphome_bridge.h" in inc_names:
            failures.append("[DAG LAYER] components/latter_ui/page_system.cpp must not include esphome_bridge.h")
        if name.startswith("page_music") and "latter_actions.h" in inc_names:
            failures.append(f"[DAG LAYER] {rel} must not include latter_actions.h")
        if name.startswith("page_") and name != "page_router.cpp":
            other_pages = sorted(n for n in inc_names if n.startswith("page_") and n.endswith(".cpp"))
            if other_pages:
                failures.append(f"[DAG LAYER] {rel} includes sibling page .cpp: {other_pages}")


def _check_ui_headers_manifest(failures: list[str]) -> None:
    """Verify `components/latter_ui/__init__.py` `UI_HEADERS` lists every `include/ui_*.h` file 1:1."""
    init_py = ROOT / "components" / "latter_ui" / "__init__.py"
    if not init_py.is_file():
        failures.append("[UI MANIFEST] components/latter_ui/__init__.py missing")
        return
    tree = ast.parse(init_py.read_text(encoding="utf-8"), filename="components/latter_ui/__init__.py")
    listed: list[str] = []
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if any(isinstance(t, ast.Name) and t.id == "UI_HEADERS" for t in targets) and node.value is not None:
                listed.extend(
                    elt.value
                    for elt in ast.walk(node.value)
                    if isinstance(elt, ast.Constant) and isinstance(elt.value, str) and elt.value.endswith(".h")
                )
    on_disk = sorted(p.name for p in (ROOT / "include").glob("ui_*.h"))
    if sorted(listed) != on_disk:
        missing = sorted(set(on_disk) - set(listed))
        extra = sorted(set(listed) - set(on_disk))
        failures.append(
            f"[UI MANIFEST] components/latter_ui/__init__.py UI_HEADERS mismatch: missing={missing}, extra={extra}"
        )


def verify_dag_and_layers(failures: list[str]) -> None:
    """Run all Phase 5 DAG cycle, flat-source, and L0-L3 boundary gates."""
    verify_flat_component_sources(failures)
    verify_include_dag_no_cycles(failures)
    _check_ui_header_layers(failures)
    _check_core_to_ui_inversion(failures)
    _check_ui_cpp_boundaries(failures)
    _check_ui_headers_manifest(failures)
