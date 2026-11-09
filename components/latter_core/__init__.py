"""Latter Core SDK component: stages shared C++20 headers into the ESPHome build directory."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@slampton"]

CONF_GUI = "gui"

latter_core_ns = cg.esphome_ns.namespace("latter_core")
# Main-loop pass probe for [PROF:WAKE] (latter_core.h): counts component-phase passes, nothing else.
LatterLoopProbe = latter_core_ns.class_("LatterLoopProbe", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LatterLoopProbe),
        cv.Optional(CONF_GUI, default="universal"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)

CORE_HEADERS: tuple[str, ...] = (
    "latter_prof.h",
    "audio_router.h",
    "va_cancel_helper.h",
    "latter_protocol.h",
    "latter_state.h",
    "hal_imu_qmi8658.h",
    "hal_power_pm.h",
    "hal_battery.h",
    "hal_link_report.h",
    "ui_core.h",
    "ui_layout.h",
    "ui_touch_router.h",
    "ui_ambient_va.h",
    "ui_weather_sky.h",
    "ui_games.h",
    "ui_tools.h",
    "ui_music.h",
    "ui_clock_home.h",
    "ui_area_home_hub.h",
    "ui_remote.h",
    "ui_system.h",
)


async def to_code(config: dict[str, Any]) -> None:
    """Copy Latter Core C++20 headers into the build src/ directory and register global includes."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    for hdr in CORE_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter Core header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        shutil.copyfile(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))

    probe = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(probe, config)
    cg.add_define("USE_LATTER_LOOP_PROBE")

    if "wifi" in CORE.config:
        # Latter drives Wi-Fi modem power save through ESPHome's request_high_performance() /
        # set_power_save_mode() (latter_ui::apply_wifi_power_policy in ui_layout.h).
        from esphome.components import wifi

        wifi.enable_runtime_power_save_control()
