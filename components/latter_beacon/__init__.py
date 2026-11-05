"""Latter presence beacon: an iBeacon driven straight through the Bluetooth controller's HCI.

The Area presence beacon only advertises. ESPHome's esp32_ble brings the Bluedroid host for that, and on
the ESP32-S3 Bluedroid holds tens of kilobytes of internal RAM for as long as Bluetooth is on (measured:
~45 KB with the beacon on, which left a Wrist at 7 KB during a voice turn). This component starts only
the controller and sends the four HCI commands that advertising needs (reset, parameters, data, enable)
over VHCI. No host stack is linked.

It keeps esp32_ble's control surface (enable(), disable(), is_active(), is_failed()) so the power policy
drives either one. Chips whose Bluetooth controller is on a co-processor (ESP32-P4 with esp_hosted) keep
esp32_ble.
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
            cv.Optional(CONF_TX_POWER, default="3dBm"): cv.All(
                cv.decibel, cv.one_of(*TX_POWER_LEVELS, int=True)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_intervals,
)


def _validate_variant(config: ConfigType) -> ConfigType:
    variant = get_esp32_variant()
    if variant in (VARIANT_ESP32P4, VARIANT_ESP32S2):
        raise cv.Invalid(
            f"latter_beacon needs an on-chip Bluetooth controller; {variant} has none "
            "(use esp32_ble, which supports esp_hosted)"
        )
    return config


FINAL_VALIDATE_SCHEMA = _validate_variant


async def to_code(config: ConfigType) -> None:
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
    cg.add(var.set_tx_power(cg.RawExpression(TX_POWER_LEVELS[config[CONF_TX_POWER]])))

    request_bluetooth()
    if "wifi" in CORE.config:
        request_software_coexistence()
    # Controller only: no Bluedroid or NimBLE host is built or linked.
    add_idf_sdkconfig_option("CONFIG_BT_CONTROLLER_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", False)
    # One legacy advertising set; the controller sizes its activity pool from this (default 6).
    add_idf_sdkconfig_option("CONFIG_BT_CTRL_BLE_MAX_ACT", 2)
    cg.add_define("USE_LATTER_BEACON")
