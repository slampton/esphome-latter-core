"""Latter Core SDK component: HAL drivers, Protocol v3 state, RPC outbox & actions."""

from __future__ import annotations

import shutil
from pathlib import Path
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@slampton"]

CONF_GUI = "gui"

latter_core_ns = cg.esphome_ns.namespace("latter_core")
LatterLoopProbe = latter_core_ns.class_("LatterLoopProbe", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LatterLoopProbe),
        cv.Optional(CONF_GUI, default="universal"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)

CORE_HEADERS: tuple[str, ...] = (
    "latter_prof.h",
    "hal_manifest.h",
    "latter_protocol.h",
    "latter_rpc.h",
    "audio_router.h",
    "state/state_common.h",
    "state/state_prefs.h",
    "state/state_shortcuts.h",
    "state/state_nav.h",
    "state/state_media.h",
    "state/state_areas.h",
    "state/state_weather_cal.h",
    "state/state_time_alarms.h",
    "state/state_alerts_intercom.h",
    "state/device_runtime_state.h",
    "state/state_nvs_snapshot.h",
    "hal_imu_qmi8658.h",
    "hal_battery.h",
    "hal_panel_power.h",
    "hal_display_lifecycle.h",
    "hal_radio_policy.h",
    "hal_sleep_pm.h",
    "hal_touch.h",
    "hal_link_report.h",
    "hal_video_stream.h",
    "esphome_bridge.h",
    "latter_actions.h",
)

_SENDSPIN_QUIET_CMAKE = "\n".join(
    [
        "function(_latter_quiet_sendspin_logs)",
        "  if(COMMAND idf_build_get_property)",
        "    idf_build_get_property(_latter_build_comps BUILD_COMPONENTS)",
        '    if("sendspin__sendspin-cpp" IN_LIST _latter_build_comps)',
        "      idf_component_get_property("
        "_latter_ss_lib sendspin__sendspin-cpp COMPONENT_LIB)",
        "      if(_latter_ss_lib AND TARGET ${_latter_ss_lib})",
        "        get_target_property("
        "_latter_ss_defs ${_latter_ss_lib} COMPILE_DEFINITIONS)",
        "        if(_latter_ss_defs)",
        '          list(FILTER _latter_ss_defs EXCLUDE REGEX "^LOG_LOCAL_LEVEL=")',
        '          list(APPEND _latter_ss_defs "LOG_LOCAL_LEVEL=ESP_LOG_WARN")',
        "          set_target_properties(${_latter_ss_lib} PROPERTIES "
        'COMPILE_DEFINITIONS "${_latter_ss_defs}")',
        "        else()",
        "          target_compile_definitions(${_latter_ss_lib} PRIVATE "
        "LOG_LOCAL_LEVEL=ESP_LOG_WARN)",
        "        endif()",
        "      endif()",
        "    endif()",
        "  endif()",
        "endfunction()",
        "cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} "
        "CALL _latter_quiet_sendspin_logs)",
    ]
)


def AUTO_LOAD(config: dict[str, Any]) -> list[str]:
    """Load latter_ui only when a display GUI is enabled."""
    if str(config.get(CONF_GUI, "universal")).strip().lower() == "none":
        return []
    return ["latter_ui"]


def _stage_core_headers(config: dict[str, Any]) -> None:
    """Copy Latter Core headers into ESPHome's generated src tree synchronously."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    gui_mode = str(config.get(CONF_GUI, "universal")).strip().lower()
    if gui_mode == "none":
        cg.add_define("LATTER_HAS_DISPLAY", 0)
        cg.add_define("LATTER_HAS_TOUCH", 0)
        cg.add_define("LATTER_SCREEN_W", 0)
        cg.add_define("LATTER_SCREEN_H", 0)

    for hdr in CORE_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter Core header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        dst_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))


async def to_code(config: dict[str, Any]) -> None:
    """Stage Latter Core headers and register ESP32 linker/power hooks."""
    _stage_core_headers(config)

    if CORE.is_esp32:
        from esphome.components import esp32

        if hasattr(CORE, "require_wake_loop_threadsafe"):
            CORE.require_wake_loop_threadsafe()
        # Flash-write counters for System: Link Report (pm_telemetry_stall.cpp).
        cg.add_build_flag("-Wl,--wrap=nvs_set_blob")
        cg.add_build_flag("-Wl,--wrap=esp_flash_erase_region")
        # Clamp sendspin-cpp LOG_LOCAL_LEVEL to ESP_LOG_WARN unless DEBUG/VERBOSE.
        logger_cfg = CORE.config.get("logger", {})
        logger_level = str(logger_cfg.get("level", "DEBUG")).upper()
        sendspin_tag_level = str(
            logger_cfg.get("logs", {}).get("esp-idf", logger_level)
        ).upper()
        if (
            "sendspin" in CORE.config
            and logger_level not in ("DEBUG", "VERBOSE", "VERY_VERBOSE")
            and sendspin_tag_level not in ("DEBUG", "VERBOSE", "VERY_VERBOSE")
        ):
            esp32.add_src_cmake(_SENDSPIN_QUIET_CMAKE)

    if "wifi" in CORE.config:
        from esphome.components import wifi

        wifi.enable_runtime_power_save_control()

    cg.add_global(
        cg.RawStatement('#include "esphome/components/latter_core/latter_core.h"')
    )
    probe = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(probe, config)
    cg.add_define("USE_LATTER_LOOP_PROBE")
