"""Latter UI component: stages the 8 clean Latter UI C++20 headers and compiles page/widget TUs."""

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
    "ui_types.h",
    "ui_gfx.h",
    "ui_hit_table.h",
    "ui_layout.h",
    "ui_widgets.h",
    "ui_compositor.h",
    "ui_touch_engine.h",
    "ui_pages.h",
)


async def to_code(config: dict[str, Any]) -> None:
    """Copy Latter UI C++20 headers into the build src/ directory and register global includes."""
    del config
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
