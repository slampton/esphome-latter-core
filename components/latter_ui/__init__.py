"""Latter UI component: partitions Toolkit vs Universal Apps and stages C++20 headers."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["latter_core"]

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

# Toolkit headers: pure C++20 rendering leaves, GFX, MDI, widgets, compositor, touch engine & display bridge.
UI_TOOLKIT_HEADERS: tuple[str, ...] = (
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
    # Touch Engine & ESPHome Display Bridge
    "ui_touch_engine.h",
    "ui_esphome_display.h",
)

# Universal App headers: compiled and staged only when latter_core.gui == "universal".
UI_UNIVERSAL_APP_HEADERS: tuple[str, ...] = (
    "ui_pages.h",
)

# Complete header manifest (Toolkit + Universal Apps) for DAG & staging verification.
UI_HEADERS: tuple[str, ...] = UI_TOOLKIT_HEADERS + UI_UNIVERSAL_APP_HEADERS

# Flat TU partition (ESPHome recursive_sources = False):
# Toolkit TUs always compile when latter_ui is loaded.
UI_TOOLKIT_SOURCES: tuple[str, ...] = (
    "hit_table.cpp",
    "ui_gfx.cpp",
    "ui_mdi.cpp",
    "ui_widgets.cpp",
    "ui_compositor.cpp",
    "overlay_notification.cpp",
    "overlay_quick_actions.cpp",
    "overlay_takeover.cpp",
    "overlay_voice_assistant.cpp",
    "touch_engine.cpp",
    "touch_dispatch.cpp",
)

# Universal App TUs compile only when latter_core.gui == "universal".
UI_UNIVERSAL_APP_SOURCES: tuple[str, ...] = (
    "page_area_hub.cpp",
    "page_calendar.cpp",
    "page_cameras.cpp",
    "page_clock.cpp",
    "page_games.cpp",
    "page_home.cpp",
    "page_home_hub.cpp",
    "page_music.cpp",
    "page_pinned.cpp",
    "page_remote.cpp",
    "page_router.cpp",
    "page_system.cpp",
    "page_time.cpp",
    "page_tools.cpp",
    "page_weather.cpp",
    "screensaver_fx.cpp",
    "showcase_engine.cpp",
)


def _gui_is_universal() -> bool:
    core_cfg = CORE.config.get("latter_core") if isinstance(CORE.config, dict) else None
    if not isinstance(core_cfg, dict):
        return True
    return str(core_cfg.get("gui", "universal")).strip().lower() == "universal"


def _has_display_capability() -> bool:
    core_cfg = CORE.config.get("latter_core") if isinstance(CORE.config, dict) else None
    if not isinstance(core_cfg, dict):
        return True
    caps = core_cfg.get("capabilities")
    if isinstance(caps, dict) and "display" in caps:
        return bool(caps["display"])
    return _gui_is_universal()


def FILTER_SOURCE_FILES() -> list[str]:
    """Exclude Universal App TUs when gui != 'universal' (and Toolkit TUs when headless)."""
    if not _has_display_capability():
        return list(UI_TOOLKIT_SOURCES + UI_UNIVERSAL_APP_SOURCES)
    if not _gui_is_universal():
        return list(UI_UNIVERSAL_APP_SOURCES)
    return []


def _copy_if_changed(src_file: Path, dst_file: Path) -> None:
    """Copy header bytes only when changed to preserve Ninja incremental build caches."""
    src_bytes = src_file.read_bytes()
    if dst_file.is_file() and dst_file.read_bytes() == src_bytes:
        return
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    dst_file.write_bytes(src_bytes)


def _stage_ui_headers() -> None:
    """Copy Latter UI C++20 headers into the build src/ directory and register global includes."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    headers = UI_HEADERS if _gui_is_universal() else UI_TOOLKIT_HEADERS
    for hdr in headers:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter UI header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        _copy_if_changed(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))


async def to_code(config: dict[str, Any]) -> None:
    """ESPHome component code generation hook."""
    del config
    _stage_ui_headers()
