"""Latter UI component: stages C++20 UI headers and compiles the universal display UI.

Screenless boards (`capabilities.display: false`) omit `latter_ui` via `latter_core.AUTO_LOAD`.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["latter_core"]

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

# All Latter UI C++20 headers staged into ESPHome's build src/ tree.
STAGED_HEADERS: tuple[str, ...] = (
    # Pure C++20 UI Leaves (color, surface, geometry & hit-table contracts)
    "ui_panel_traits.h",
    "ui_color.h",
    "ui_surface.h",
    "ui_widget_ids.h",
    "ui_panel_metrics.h",
    "ui_hit_table.h",
    "ui_page_geoms.h",
    "ui_types.h",
    # Pure C++20 UI Infrastructure (GFX, icons, layout, widgets, compositor & pages)
    "ui_mdi.h",
    "ui_touch_recognizer.h",
    "ui_dirty_band.h",
    "ui_frame_context.h",
    "ui_cadence.h",
    "ui_gfx.h",
    "ui_layout.h",
    "ui_widgets.h",
    "ui_tile_grid.h",
    "ui_compositor.h",
    "ui_pages.h",
    # Touch Engine & ESPHome Display Bridge
    "ui_touch_engine.h",
    "ui_esphome_display.h",
)

# Minimal global `#include` set emitted into main.cpp for YAML lambdas
# (`ui_esphome_display.h` transitively includes the full UI API required by `guis/universal/*.yaml`).
GLOBAL_YAML_INCLUDES: tuple[str, ...] = (
    "ui_esphome_display.h",
)


def _copy_if_changed(src_file: Path, dst_file: Path) -> None:
    """Copy header bytes only when changed to preserve Ninja incremental build caches."""
    src_bytes = src_file.read_bytes()
    if dst_file.is_file() and dst_file.read_bytes() == src_bytes:
        return
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    dst_file.write_bytes(src_bytes)


def _stage_ui_headers() -> None:
    """Copy Latter UI C++20 headers into the build src/ directory and emit minimal global includes."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    for hdr in STAGED_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter UI header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        _copy_if_changed(src_file, dst_file)

    for hdr in GLOBAL_YAML_INCLUDES:
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))


async def to_code(config: dict[str, Any]) -> None:
    """ESPHome component code generation hook."""
    del config
    _stage_ui_headers()
