"""First-party Latter Touch HAL platform (`platform: latter_core`) for ESPHome."""

from __future__ import annotations

from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c, touchscreen
from esphome.const import CONF_ID, CONF_INTERRUPT_PIN, CONF_RESET_PIN

from . import latter_core_ns

DEPENDENCIES = ["i2c", "latter_core"]

CONF_PROTOCOL = "protocol"
CONF_ACTIVE_POLL_INTERVAL = "active_poll_interval"

TouchHalProtocol = latter_core_ns.enum("TouchHalProtocol", is_class=True)

LatterTouchscreen = latter_core_ns.class_(
    "LatterTouchscreen",
    touchscreen.Touchscreen,
    i2c.I2CDevice,
)

PROTOCOLS = {
    "ft6336": TouchHalProtocol.FT6336,
    "ft3168": TouchHalProtocol.FT3168,
    "gt911": TouchHalProtocol.GT911,
    "cst9217": TouchHalProtocol.CST9217,
    "cst816s": TouchHalProtocol.CST816S,
}

CONFIG_SCHEMA = (
    touchscreen.touchscreen_schema()
    .extend(
        {
            cv.GenerateID(): cv.declare_id(LatterTouchscreen),
            cv.Required(CONF_PROTOCOL): cv.enum(PROTOCOLS, lower=True),
            cv.Optional(CONF_INTERRUPT_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_ACTIVE_POLL_INTERVAL): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=4),
                    max=cv.TimePeriod(milliseconds=50),
                ),
            ),
        }
    )
    .extend(i2c.i2c_device_schema(0x38))
)


async def to_code(config: dict[str, Any]) -> None:
    """Instantiate LatterTouchscreen and bind protocol, I2C bus, and IRQ/reset pins."""
    var = cg.new_Pvariable(config[CONF_ID])
    await touchscreen.register_touchscreen(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_protocol(config[CONF_PROTOCOL]))

    if interrupt_pin := config.get(CONF_INTERRUPT_PIN):
        pin = await cg.gpio_pin_expression(interrupt_pin)
        cg.add(var.set_interrupt_pin(pin))

    if reset_pin := config.get(CONF_RESET_PIN):
        rpin = await cg.gpio_pin_expression(reset_pin)
        cg.add(var.set_reset_pin(rpin))

    if active_poll := config.get(CONF_ACTIVE_POLL_INTERVAL):
        cg.add(var.set_active_poll_ms(active_poll.total_milliseconds))
