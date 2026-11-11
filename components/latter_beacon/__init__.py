"""Latter presence beacon: an iBeacon advertised with raw HCI commands and no Bluetooth host stack.

The Area presence beacon only advertises. ESPHome's esp32_ble brings the Bluedroid host for that, and on
the ESP32-S3 Bluedroid holds tens of kilobytes of internal RAM for as long as Bluetooth is on (measured:
~45 KB with the beacon on, which left a Wrist at 7 KB during a voice turn). This component starts only
the controller and sends the four HCI commands that advertising needs (reset, parameters, data, enable).
No host stack is linked.

One component for every Latter device, with the transport chosen at build time:
  - chips with an on-chip Bluetooth controller (ESP32-S3, ...): ESP-IDF VHCI, controller-only build;
  - ESP32-P4 with an esp_hosted co-processor (C6): esp_hosted's custom-stack HCI byte pipe
    (CONFIG_ESP_HOSTED_HOST_FEAT_BT without Bluedroid/NimBLE), controller lifecycle over esp_hosted RPC.
The state machine, commands, payload, logs and control surface (enable(), disable(), is_active(),
is_failed()) are shared, so the power policy and troubleshooting are the same on every device.
"""

from __future__ import annotations

import esphome.codegen as cg
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    get_esp32_variant,
    request_bluetooth,
    request_software_coexistence,
)
from esphome.components.esp32.const import VARIANT_ESP32P4, VARIANT_ESP32S2
import esphome.config_validation as cv
import esphome.final_validate as fv
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

# esp_power_level_t names (their numeric values differ between chips, so the C++ gets the name).
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
    # Legacy non-connectable advertising: 100 ms to 10.24 s.
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
            cv.Optional(CONF_MEASURED_POWER, default=-59): cv.int_range(min=-128, max=0),
            cv.Optional(CONF_MIN_INTERVAL, default="500ms"): _INTERVAL,
            cv.Optional(CONF_MAX_INTERVAL, default="1000ms"): _INTERVAL,
            # On-chip controllers only (default +3 dBm); esp_hosted offers no TX power control.
            cv.Optional(CONF_TX_POWER): cv.All(cv.decibel, cv.one_of(*TX_POWER_LEVELS, int=True)),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_intervals,
)


def _is_hosted() -> bool:
    return get_esp32_variant() == VARIANT_ESP32P4


def _final_validate(config: ConfigType) -> ConfigType:
    variant = get_esp32_variant()
    if variant == VARIANT_ESP32S2:
        raise cv.Invalid("latter_beacon needs Bluetooth; the ESP32-S2 has none")
    if _is_hosted():
        # CORE.config is not populated during final validation; the full config comes from final_validate.
        if ESP32_HOSTED_DOMAIN not in fv.full_config.get():
            raise cv.Invalid("On the ESP32-P4, latter_beacon needs esp32_hosted (Bluetooth is on the co-processor)")
        if CONF_TX_POWER in config:
            raise cv.Invalid("tx_power is not available over esp_hosted", path=[CONF_TX_POWER])
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _code_runs_from_psram() -> bool:
    """Code and rodata execute from PSRAM (XIP): flash erases keep the cache and interrupts running."""
    framework = CORE.config.get("esp32", {}).get("framework", {})
    if framework.get("advanced", {}).get("execute_from_psram", False):
        return True
    options = framework.get("sdkconfig_options", {})
    return all(
        str(options.get(key, "")).lower() == "y"
        for key in ("CONFIG_SPIRAM_FETCH_INSTRUCTIONS", "CONFIG_SPIRAM_RODATA")
    )


async def to_code(config: ConfigType) -> None:
    cg.add_global(cg.RawStatement('#include "esphome/components/latter_beacon/latter_beacon.h"'))
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

    if _is_hosted():
        # esp_hosted's stack-agnostic Bluetooth feature: controller lifecycle over RPC + HCI byte pipe.
        # With neither Bluedroid nor NimBLE enabled, its adapter resolves to the custom-stack path.
        add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_HOST_FEAT_BT", True)
        cg.add_define("USE_LATTER_BEACON_HOSTED")
        return

    tx_power = config.get(CONF_TX_POWER, DEFAULT_TX_POWER_DBM)
    cg.add(var.set_tx_power(cg.RawExpression(TX_POWER_LEVELS[tx_power])))
    request_bluetooth()
    if "wifi" in CORE.config:
        request_software_coexistence()
    # Controller only: no Bluedroid or NimBLE host is built or linked.
    add_idf_sdkconfig_option("CONFIG_BT_CONTROLLER_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", False)
    # Advertise-only controller (CONFIG_BT_CTRL_* is the ESP32-C3/S3 controller's Kconfig).
    # One legacy advertising set: the activity pool needs one slot (default 6, 828 bytes each).
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_MAX_ACT", 1)
    # Nothing scans: no scan duplicate-filter cache.
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_SCAN_DUPL", False)
    # Controller code from flash instead of ROM + IRAM (frees over 20 KB of internal RAM). Its interrupt
    # is then not IRAM-safe, and ESP-IDF warns it may stall while flash is erased. With code and rodata
    # executing from PSRAM (every Latter S3 board) a flash erase keeps the cache and interrupts running,
    # so at worst an advertising event slips; boards that execute from flash keep the ROM/IRAM controller.
    if _code_runs_from_psram():
        add_idf_sdkconfig_option("CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY", True)
