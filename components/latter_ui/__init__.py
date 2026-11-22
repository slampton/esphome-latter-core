"""Latter UI component: stages the acyclic Latter UI C++20 headers and compiles TUs."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["latter_core"]

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

UI_HEADERS: tuple[str, ...] = (
    # Pure C++20 UI Leaves (color, surface, geometry & hit-table contracts)
    "ui_panel_traits.h",
    "ui_color.h",
    "ui_surface.h",
    "ui_widget_ids.h",
    "ui_panel_metrics.h",
    "ui_hit_table.h",
    "ui_page_geoms.h",
    "ui_types.h",
    # Pure C++20 Headless UI Infrastructure (GFX, icons, layout, widgets & compositor)
    "ui_mdi.h",
    "ui_touch_recognizer.h",
    "ui_dirty_band.h",
    "ui_frame_context.h",
    "ui_cadence.h",
    "ui_gfx.h",
    "ui_layout.h",
    "ui_widgets.h",
    "ui_compositor.h",
    # Domain Pages, Touch Engine & ESPHome Display Bridge
    "ui_pages.h",
    "ui_touch_engine.h",
    "ui_esphome_display.h",
)


def _stage_ui_headers() -> None:
    """Copy Latter UI C++20 headers into the build src/ directory and register global includes."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    for hdr in UI_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter UI header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        shutil.copyfile(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))


async def to_code(config: dict[str, Any]) -> None:
    """ESPHome component code generation hook."""
    del config
    _stage_ui_headers()
