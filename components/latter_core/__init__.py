"""Latter Core SDK component: stages shared C++20 headers into the ESPHome build directory."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]

CONFIG_SCHEMA = cv.Schema({})

CORE_HEADERS: tuple[str, ...] = (
    "va_cancel_helper.h",
    "ui_core.h",
    "ui_layout.h",
    "ui_ambient_va.h",
    "ui_weather_sky.h",
    "ui_games.h",
    "ui_tools.h",
    "ui_music.h",
    "ui_clock_home.h",
    "ui_room_home_hub.h",
    "ui_remote.h",
    "ui_system.h",
    "ui_tools_apps.h",
)


async def to_code(config: dict[str, Any]) -> None:
    """Copy Latter Core C++20 headers into the build src/ directory and register global includes."""
    repo_root = Path(__file__).resolve().parent.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    for hdr in CORE_HEADERS:
        src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if src_file.is_file():
            dst_file = Path(CORE.relative_src_path(hdr))
            shutil.copyfile(src_file, dst_file)
            cg.add_global(cg.RawStatement(f'#include "{hdr}"'))
