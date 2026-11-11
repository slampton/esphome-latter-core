"""Latter UI component: stages Latter on-device GUI C++20 headers and compiled widgets."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["latter_hal", "latter_core"]

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

UI_HEADERS: tuple[str, ...] = (
    "ui_core.h",
    "ui_hit_table.h",
    "ui_icons.h",
    "ui_layout.h",
    "ui_touch_router.h",
    "ui_widgets.h",
    "ui_ambient_va.h",
    "ui_weather_sky.h",
    "ui_games.h",
    "ui_tools.h",
    "ui_music.h",
    "ui_clock_home.h",
    "ui_area_home_hub.h",
    "ui_remote.h",
    "ui_system.h",
    "ui_compositor.h",
    "ui_gesture_engine.h",
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
    cg.add_build_flag(f"-I{dst_src_dir.as_posix()}")

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
