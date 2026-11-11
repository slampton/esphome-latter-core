"""Latter HAL component: compiled C++20 hardware abstraction layer (PMU, battery, IMU, audio, panel, sleep)."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CODEOWNERS = ["@slampton"]

CONFIG_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

HAL_HEADERS: tuple[str, ...] = (
    "latter_prof.h",
    "hal_types.h",
    "hal_manifest.h",
    "esphome_bridge.h",
    "va_cancel_helper.h",
    "audio_router.h",
    "hal_imu_qmi8658.h",
    "hal_battery.h",
    "hal_pmu_axp2101.h",
    "hal_panel_power.h",
    "hal_power_pm.h",
    "hal_link_report.h",
)


async def to_code(config: dict[str, Any]) -> None:
    """Stage Latter HAL public headers into build src/ and register linker/power hooks."""
    del config
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    for hdr in HAL_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter HAL header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        shutil.copyfile(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))

    if CORE.is_esp32:
        if hasattr(CORE, "require_wake_loop_threadsafe"):
            CORE.require_wake_loop_threadsafe()
        # Flash-write counters for System: Link Report (nvs=, power_pm.cpp defines the wraps).
        cg.add_build_flag("-Wl,--wrap=nvs_set_blob")
        cg.add_build_flag("-Wl,--wrap=esp_flash_erase_region")

    if "wifi" in CORE.config:
        from esphome.components import wifi

        wifi.enable_runtime_power_save_control()
