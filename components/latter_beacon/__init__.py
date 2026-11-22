"""Latter presence beacon: iBeacon advertised via raw HCI with no BT host stack.

The Area presence beacon only advertises. ESPHome's esp32_ble brings the Bluedroid
host for that, and on the ESP32-S3 Bluedroid holds tens of kilobytes of internal
RAM while Bluetooth is on. This component starts only the controller and sends the
four HCI commands that advertising needs (reset, parameters, data, enable).

One component for every Latter device, with the transport chosen at build time:
  - on-chip Bluetooth controller (ESP32-S3, ...): ESP-IDF VHCI, controller-only;
  - esp_hosted co-processor (ESP32-P4 + C6, etc.): esp_hosted custom-stack HCI
    byte pipe (CONFIG_ESP_HOSTED_HOST_FEAT_BT without Bluedroid/NimBLE).
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    get_esp32_variant,
    is_idf_sdkconfig_option_enabled,
    request_bluetooth,
    request_software_coexistence,
)
from esphome.components.esp32.const import VARIANT_ESP32P4, VARIANT_ESP32S2
from esphome.const import CONF_ID, CONF_TX_POWER, CONF_UUID
from esphome.core import CORE, TimePeriod
from esphome.types import ConfigType

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["esp32"]
CONFLICTS_WITH = ["esp32_ble"]

CONF_MAJOR = "major"
CONF_MINOR = "minor"
CONF_MEASURED_POWER = "measured_power"
CONF_MIN_INTERVAL = "min_interval"
CONF_MAX_INTERVAL = "max_interval"

ESP32_HOSTED_DOMAIN = "esp32_hosted"
DEFAULT_TX_POWER_DBM = 3

latter_beacon_ns = cg.esphome_ns.namespace("latter_beacon")
LatterBeacon = latter_beacon_ns.class_("LatterBeacon", cg.Component)

TX_POWER_LEVELS = {
    -12: "ESP_PWR_LVL_N12",
    -9: "ESP_PWR_LVL_N9",
    -6: "ESP_PWR_LVL_N6",
    -3: "ESP_PWR_LVL_N3",
    0: "ESP_PWR_LVL_N0",
    3: "ESP_PWR_LVL_P3",
    6: "ESP_PWR_LVL_P6",
    9: "ESP_PWR_LVL_P9",
}

_INTERVAL = cv.All(
    cv.positive_time_period_milliseconds,
    cv.Range(min=TimePeriod(milliseconds=100), max=TimePeriod(milliseconds=10240)),
)


def _validate_intervals(config: ConfigType) -> ConfigType:
    if config[CONF_MIN_INTERVAL] > config[CONF_MAX_INTERVAL]:
        raise cv.Invalid("min_interval must be <= max_interval")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LatterBeacon),
            cv.Required(CONF_UUID): cv.uuid,
            cv.Optional(CONF_MAJOR, default=1): cv.uint16_t,
            cv.Required(CONF_MINOR): cv.uint16_t,
            cv.Optional(CONF_MEASURED_POWER, default=-59): cv.int_range(
                min=-128, max=0
            ),
            cv.Optional(CONF_MIN_INTERVAL, default="500ms"): _INTERVAL,
            cv.Optional(CONF_MAX_INTERVAL, default="1000ms"): _INTERVAL,
            cv.Optional(CONF_TX_POWER): cv.All(
                cv.decibel, cv.one_of(*TX_POWER_LEVELS, int=True)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_intervals,
)


def _is_hosted(cfg: Mapping[str, Any] | None = None) -> bool:
    active_cfg = cfg if cfg is not None else CORE.config
    return isinstance(active_cfg, Mapping) and ESP32_HOSTED_DOMAIN in active_cfg


def _final_validate(config: ConfigType) -> ConfigType:
    full_cfg = fv.full_config.get()
    variant = get_esp32_variant()
    if variant == VARIANT_ESP32S2 and not _is_hosted(full_cfg):
        raise cv.Invalid("latter_beacon needs Bluetooth; the ESP32-S2 has none")
    if variant == VARIANT_ESP32P4 and not _is_hosted(full_cfg):
        raise cv.Invalid(
            "On the ESP32-P4, latter_beacon needs esp32_hosted "
            "(Bluetooth is on the co-processor)"
        )
    if _is_hosted(full_cfg) and CONF_TX_POWER in config:
        raise cv.Invalid(
            "tx_power is not available over esp_hosted", path=[CONF_TX_POWER]
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _sdkconfig_flag_enabled(options: dict[str, object], key: str) -> bool:
    if is_idf_sdkconfig_option_enabled(key):
        return True
    raw = options.get(key)
    val = getattr(raw, "value", raw)
    return val is True or str(val).strip().strip('"').lower() == "y"


def _code_runs_from_psram() -> bool:
    """Code and rodata execute from PSRAM (XIP)."""
    framework = CORE.config.get("esp32", {}).get("framework", {})
    if framework.get("advanced", {}).get("execute_from_psram", False):
        return True
    options = framework.get("sdkconfig_options", {})
    if _sdkconfig_flag_enabled(options, "CONFIG_SPIRAM_XIP_FROM_PSRAM"):
        return True
    return all(
        _sdkconfig_flag_enabled(options, key)
        for key in ("CONFIG_SPIRAM_FETCH_INSTRUCTIONS", "CONFIG_SPIRAM_RODATA")
    )


def _light_sleep_enabled() -> bool:
    """Automatic light sleep is configured (enable_light_sleep or tickless idle)."""
    pm_cfg = CORE.config.get("power_management")
    entries = (
        pm_cfg
        if isinstance(pm_cfg, list)
        else ([pm_cfg] if isinstance(pm_cfg, dict) else [])
    )
    if any(isinstance(e, dict) and e.get("enable_light_sleep") for e in entries):
        return True
    options = (
        CORE.config.get("esp32", {})
        .get("framework", {})
        .get("sdkconfig_options", {})
    )
    return _sdkconfig_flag_enabled(options, "CONFIG_FREERTOS_USE_TICKLESS_IDLE")


async def to_code(config: ConfigType) -> None:
    cg.add_global(
        cg.RawStatement('#include "esphome/components/latter_beacon/latter_beacon.h"')
    )
    uuid = config[CONF_UUID].hex
    uuid_bytes = ", ".join(f"0x{uuid[i : i + 2]}" for i in range(0, len(uuid), 2))
    var = cg.new_Pvariable(
        config[CONF_ID], cg.RawExpression(f"std::array<uint8_t, 16>{{{uuid_bytes}}}")
    )
    await cg.register_component(var, config)
    cg.add(var.set_major(config[CONF_MAJOR]))
    cg.add(var.set_minor(config[CONF_MINOR]))
    cg.add(var.set_measured_power(config[CONF_MEASURED_POWER]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL].total_milliseconds))
    cg.add(var.set_max_interval(config[CONF_MAX_INTERVAL].total_milliseconds))
    cg.add_define("USE_LATTER_BEACON")

    if _is_hosted(CORE.config):
        add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_HOST_FEAT_BT", True)
        cg.add_define("USE_LATTER_BEACON_HOSTED")
        return

    tx_power = config.get(CONF_TX_POWER, DEFAULT_TX_POWER_DBM)
    cg.add(var.set_tx_power(cg.RawExpression(TX_POWER_LEVELS[tx_power])))
    request_bluetooth()
    if "wifi" in CORE.config or ESP32_HOSTED_DOMAIN in CORE.config:
        request_software_coexistence()
    add_idf_sdkconfig_option("CONFIG_BT_CONTROLLER_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_MAX_ACT", 1)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_SCAN_DUPL", False)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_MODEM_SLEEP", True)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1", True)
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL", True)
    if _light_sleep_enabled():
        add_idf_sdkconfig_option("CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP", True)
    if _code_runs_from_psram():
        add_idf_sdkconfig_option("CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY", True)
