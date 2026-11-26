#!/usr/bin/env python3
"""Scaffold a new Latter board from its facts file, and generate bench bring-up probes.

Inputs come from boards/facts/<board_id>.json (see tools/check_boards.py and
boards/facts/schema.json). Standard library only.

Usage:
  python tools/new_board.py <board_id>                     # board HAL + device + bring-up
  python tools/new_board.py <board_id> --out DIR           # write to custom output root
  python tools/new_board.py <board_id> --repo owner/repo   # custom GitHub repo for device YAML
  python tools/new_board.py <board_id> --force             # overwrite existing files
  python tools/new_board.py --bringup                      # (re)generate examples/bringup/*.yaml
  python tools/new_board.py --bringup --check              # fail if bring-up probes are stale

Never overwrites an existing file without --force (the --bringup mode owns its files).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
FACTS_DIR = REPO / "boards" / "facts"
BRINGUP_DIR = REPO / "examples" / "bringup"
BANNER = "# " + "=" * 78
DEFAULT_LATTER_REPO = "slampton/esphome-latter-core"

INPUT_ROLES = {"pwr_key", "touch_int", "imu_int1", "imu_int2", "rtc_int"}
SWITCH_ROLES = {"speaker_pa", "haptic_motor", "backlight_enable", "sd_power_gate"}
PWM_ROLES = {"backlight_pwm", "backlight_analog_dim"}
BOARD_LIFECYCLE_SCRIPTS = (
    (
        "hal_release_deep_sleep_holds",
        "Release GPIO holds at boot (LatterTouchscreen owns touch reset)",
    ),
    (
        "hal_enter_hardware_hibernate",
        "Hold pins and cut power (called after state is saved)",
    ),
)
_SOC_CHIP_MAP = {
    "ESP32-P4": "esp32p4",
    "ESP32-S3": "esp32s3",
    "ESP32-S2": "esp32s2",
    "ESP32-C6": "esp32c6",
    "ESP32-C3": "esp32c3",
    "ESP32": "esp32",
}


def load_facts(board_id: str, facts_path: Path | None = None) -> dict[str, Any]:
    path = facts_path if facts_path is not None else (FACTS_DIR / f"{board_id}.json")
    if not path.exists():
        sys.exit(
            f"error: {path} not found (write the facts file first: onboarding gate G1)"
        )
    return json.loads(path.read_text(encoding="utf-8"))


def is_p4(facts: dict[str, Any]) -> bool:
    return facts["silicon"]["soc"] == "ESP32-P4"


def _flag_bool(flags: dict[str, Any], key: str, default: bool = False) -> bool:
    if key not in flags:
        return default
    return str(flags[key]).strip().lower() in {"1", "true", "yes", "on"}


def _yaml_bool(val: bool) -> str:
    return "true" if val else "false"


def detect_drivers(facts: dict[str, Any]) -> dict[str, bool]:
    """Derive explicit LATTER_DRV_* driver-identity booleans from hardware facts."""
    i2c_devs = facts.get("i2c_devices", [])
    parts = [str(d.get("part", "")).lower() for d in i2c_devs]
    has_pcf85063 = any(
        str(d.get("address", "")).lower() == "0x51"
        and "pcf85063" in str(d.get("part", "")).lower()
        for d in i2c_devs
    )
    return {
        "pmu_axp2101": facts.get("power", {}).get("pmu") == "AXP2101",
        "rtc_pcf85063": has_pcf85063,
        "imu_qmi8658": any("qmi8658" in p for p in parts),
        "codec_es8311": any("es8311" in p for p in parts),
    }


def compute_font_sizes(
    w_px: int,
    h_px: int,
    ppi: int,
    *,
    view_scale_pct: int = 0,
    is_stationary: bool = False,
) -> dict[str, int]:
    """Compute isotropic dp-scaled font pixel sizes matching include/ui_scale.h & latter_core."""
    del h_px
    eff_w = max(int(w_px), 120)
    eff_ppi = max(int(ppi), 50)
    eff_vs = max(75, min(200, int(view_scale_pct))) if view_scale_pct > 0 else (145 if is_stationary else 100)
    s_min = 900 if (is_stationary or eff_vs >= 120) else 760
    s = max(s_min, (eff_ppi * eff_vs * 10 + 133) // 266)

    def _dp(v: int) -> int:
        return (v * s + 500) // 1000

    def _mm_ceil(mm_x10: int) -> int:
        return (mm_x10 * eff_ppi + 253) // 254

    micro = max(12, (165 * eff_ppi + 2539) // 2540, _dp(14))
    small = max(15, _mm_ceil(20), _dp(21))
    btn_bold = max(small + 1, min(_dp(25), (eff_w * 71) // 1000))
    date = max(btn_bold + 1, min(_dp(28), (eff_w * 78 + 20) // 1000))
    chrono = max(date + 4, min(_dp(38), (eff_w * 108) // 1000))
    clock_pm = 215 if eff_vs >= 120 else 185
    time_px = max(chrono + 10, min(_dp(76), (eff_w * clock_pm + 200) // 1000))
    return {
        "font_time_size": time_px,
        "font_chrono_size": chrono,
        "font_date_size": date,
        "font_btn_bold_size": btn_bold,
        "font_small_size": small,
        "font_micro_size": micro,
        "font_games_size": chrono,
    }


def beacon_lines(facts: dict[str, Any], minor: int | str) -> list[str]:
    """Area presence iBeacon (VHCI on on-chip BT, esp_hosted on co-processor)."""
    del facts
    return [
        "latter_beacon:",
        "  id: ble_ctrl",
        '  uuid: "4C415454-4552-434F-5245-000000000220"',
        "  major: 1",
        f"  minor: {minor}",
    ]


def pin_of(facts: dict[str, Any], role: str) -> int | None:
    for p in facts["pins"]:
        if p["role"] == role:
            return int(p["gpio"])
    return None


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def esp32_block(facts: dict[str, Any]) -> list[str]:
    """Full standalone esp32: + psram: block for self-contained bring-up probes."""
    flash = facts["silicon"].get("flash_mb", 16)
    if is_p4(facts):
        return [
            "esp32:",
            "  variant: esp32p4",
            "  engineering_sample: ${p4_engineering_sample}",
            "  cpu_frequency: ${p4_cpu_frequency}",
            f"  flash_size: {flash}MB",
            "  framework:",
            "    type: esp-idf",
            "",
            "psram:",
            "  speed: 200MHz",
            "  ignore_not_found: false",
        ]
    return [
        "esp32:",
        "  board: esp32-s3-devkitc-1",
        f"  flash_size: {flash}MB",
        "  framework:",
        "    type: esp-idf",
        "",
        "psram:",
        "  mode: octal",
        "  speed: 80MHz",
        "  ignore_not_found: false",
    ]


def p4_substitutions(facts: dict[str, Any]) -> list[str]:
    if not is_p4(facts):
        return []
    return [
        (
            '  # P4 silicon profile: rev3 -> "false" / 400MHz, '
            'rev1 -> "true" / 360MHz (binaries are not cross-compatible).'
        ),
        '  p4_engineering_sample: "false"',
        '  p4_cpu_frequency: "400MHz"',
    ]


def gpio_pin_lines(
    pin: dict[str, Any], indent: str, mode_lines: list[str]
) -> list[str]:
    lines = [f"{indent}pin:", f"{indent}  number: GPIO{pin['gpio']}"]
    if pin.get("polarity") == "active_low":
        lines.append(f"{indent}  inverted: true")
    lines += [f"{indent}  {m}" for m in mode_lines]
    if pin.get("strapping"):
        lines.append(f"{indent}  ignore_strapping_warning: true")
    return lines


def _render_core_display(flags: dict[str, Any]) -> list[str]:
    lines = [
        "  display:",
        f"    width: {int(flags.get('LATTER_SCREEN_W', 466))}",
        f"    height: {int(flags.get('LATTER_SCREEN_H', 466))}",
        f"    ppi: {int(flags.get('LATTER_PANEL_PPI', 266))}",
        f"    round: {_yaml_bool(_flag_bool(flags, 'LATTER_ROUND_DISPLAY'))}",
        f"    ips: {_yaml_bool(_flag_bool(flags, 'LATTER_PANEL_IPS'))}",
    ]
    if _flag_bool(flags, "LATTER_PANEL_DCS_SLEEP"):
        lines.append("    dcs_sleep: true")
    for flag_key, yaml_key in (
        ("LATTER_DISPLAY_DC_PIN", "dc_pin"),
        ("LATTER_CORNER_R", "corner_r"),
        ("LATTER_SLOT_COUNT", "slot_count"),
        ("LATTER_CARD_SLOT_COUNT", "card_slot_count"),
        ("LATTER_BRI_GAMMA_X100", "bri_gamma_x100"),
        ("LATTER_BRI_DCS_MIN", "bri_dcs_min"),
        ("LATTER_BRI_DCS_MAX", "bri_dcs_max"),
        ("LATTER_TOUCH_INT_PIN", "touch_int_pin"),
        ("LATTER_TOUCH_RST_PIN", "touch_rst_pin"),
    ):
        if flag_key in flags:
            lines.append(f"    {yaml_key}: {flags[flag_key]}")
    if "LATTER_VIEW_SCALE_PCT" in flags:
        vs_f = int(flags["LATTER_VIEW_SCALE_PCT"]) / 100.0
        lines.append(f"    view_scale: {vs_f:.2f}")
    if _flag_bool(flags, "LATTER_DSI_IRAM_SAFE"):
        lines.append("    dsi_iram_safe: true")
    return lines


def _render_core_backlight(flags: dict[str, Any]) -> list[str]:
    entries: list[str] = []
    for flag_key, yaml_key, is_bool in (
        ("LATTER_BACKLIGHT_PIN", "pin", False),
        ("LATTER_BACKLIGHT_EN_PIN", "en_pin", False),
        ("LATTER_BACKLIGHT_EN_INVERTED", "en_inverted", True),
        ("LATTER_BACKLIGHT_INVERTED", "inverted", True),
        ("LATTER_BACKLIGHT_CUSTOM", "custom", True),
        ("LATTER_BACKLIGHT_PWM_HZ", "pwm_hz", False),
        ("LATTER_BACKLIGHT_PWM_BITS", "pwm_bits", False),
        ("LATTER_BACKLIGHT_DUTY_MIN_PM", "duty_min_pm", False),
        ("LATTER_BACKLIGHT_DUTY_MAX_PM", "duty_max_pm", False),
    ):
        if flag_key not in flags:
            continue
        if is_bool:
            if _flag_bool(flags, flag_key):
                entries.append(f"    {yaml_key}: true")
        else:
            entries.append(f"    {yaml_key}: {flags[flag_key]}")
    return ["  backlight:", *entries] if entries else []


def _render_core_power(flags: dict[str, Any]) -> list[str]:
    entries: list[str] = []
    for flag_key, yaml_key, is_bool in (
        ("LATTER_BATTERY_MAH", "battery_mah", False),
        ("LATTER_PWR_KEY_PIN", "pwr_key_pin", False),
        ("LATTER_PWR_KEY_ACTIVE_HIGH", "pwr_key_active_high", True),
        ("LATTER_PWR_KEY_DEEP_SLEEP_WAKE", "pwr_key_deep_sleep_wake", True),
        ("LATTER_ACTION_BTN_PIN", "action_btn_pin", False),
        ("LATTER_IMU_INT1_PIN", "imu_int1_pin", False),
        ("LATTER_C6_RESET_PIN", "c6_reset_pin", False),
        ("LATTER_PMU_TS_DISABLE", "pmu_ts_disable", True),
        ("LATTER_PMU_DCDC_KEEP_MASK", "pmu_dcdc_keep_mask", False),
        ("LATTER_PMU_LDO_KEEP_MASK", "pmu_ldo_keep_mask", False),
    ):
        if flag_key not in flags:
            continue
        val = _yaml_bool(_flag_bool(flags, flag_key)) if is_bool else str(flags[flag_key])
        entries.append(f"    {yaml_key}: {val}")
    return ["  power:", *entries] if entries else []


def _render_latter_core_block(facts: dict[str, Any], drivers: dict[str, bool]) -> list[str]:
    bid = facts["board_id"]
    flags = facts["latter_flags"]
    has_display = _flag_bool(flags, "LATTER_HAS_DISPLAY", True)
    chip = _SOC_CHIP_MAP.get(facts["silicon"]["soc"], "esp32s3")
    psram_mb = int(flags.get("LATTER_PSRAM_MB", facts["silicon"].get("psram_mb", 8)))
    lines: list[str] = [
        "latter_core:",
        f"  board_id: {bid}",
        f"  ble_minor: {facts.get('ble_minor', 99)}",
        f"  gui: {'universal' if has_display else 'none'}",
        f"  chip: {chip}",
        f"  psram_mb: {psram_mb}",
    ]
    if has_display:
        lines += _render_core_display(flags)
    lines += _render_core_backlight(flags)
    lines += _render_core_power(flags)
    lines += [
        "  audio:",
        "    out_rate: 48000",
        "    out_channels: 1",
        "    out_bits: 16",
        "    in_rate: 16000",
        "  capabilities:",
        f"    display: {_yaml_bool(has_display)}",
        f"    touch: {_yaml_bool(_flag_bool(flags, 'LATTER_HAS_TOUCH', has_display))}",
        f"    audio: {_yaml_bool(_flag_bool(flags, 'LATTER_HAS_AUDIO', True))}",
        f"    imu: {_yaml_bool(_flag_bool(flags, 'LATTER_HAS_IMU'))}",
        f"    battery: {_yaml_bool(_flag_bool(flags, 'LATTER_HAS_BATTERY'))}",
        f"    rtc: {_yaml_bool(_flag_bool(flags, 'LATTER_HAS_RTC'))}",
        f"    stationary: {_yaml_bool(_flag_bool(flags, 'LATTER_IS_STATIONARY'))}",
        f"    wearable: {_yaml_bool(_flag_bool(flags, 'LATTER_IS_WEARABLE'))}",
    ]
    for opt_flag, opt_key in (
        ("LATTER_HAS_VIDEO_STREAM", "video_stream"),
        ("LATTER_HAS_HW_JPEG", "hw_jpeg"),
    ):
        if opt_flag in flags:
            lines.append(f"    {opt_key}: {_yaml_bool(_flag_bool(flags, opt_flag))}")
    lines += [
        "  drivers:",
        f"    pmu_axp2101: {_yaml_bool(drivers['pmu_axp2101'])}",
        f"    rtc_pcf85063: {_yaml_bool(drivers['rtc_pcf85063'])}",
        f"    imu_qmi8658: {_yaml_bool(drivers['imu_qmi8658'])}",
        f"    codec_es8311: {_yaml_bool(drivers['codec_es8311'])}",
    ]
    return lines


def render_bringup(facts: dict[str, Any]) -> str:
    bid = facts["board_id"]
    drivers = detect_drivers(facts)
    lines: list[str] = [
        (
            f"# AUTO-GENERATED by tools/new_board.py from boards/facts/{bid}.json. "
            "Do not edit by hand:"
        ),
        "# fix the facts file and run `python tools/new_board.py --bringup`.",
        BANNER,
        f"# Bench bring-up probe: {facts['product']} ({facts['latter_device']})",
        (
            "# No Latter core or GUI. Flash over USB, open the serial log, "
            "and attach it to the board PR as"
        ),
        "# onboarding gate G2 evidence. It logs:",
        "#   [BRINGUP:SILICON] / [BRINGUP:MEMORY]  revision, flash and PSRAM",
        (
            "#   [BRINGUP:I2C]                          "
            "every facts-ledger I2C device present/missing, plus strays"
        ),
        (
            "#   [BRINGUP:PMU]                          "
            "AXP2101 status, VBAT, gauge, rail enables (battery boards)"
        ),
        (
            "#   key / interrupt edges                  "
            "press each key; touch the panel (touch INT pulses)"
        ),
        (
            "# and exposes switches for each output net "
            "(amplifier, haptics, SD power, backlight)."
        ),
        (
            "# Display, touch coordinates and audio are verified by the full "
            "firmware's [PROF:*] walk (gate G5)."
        ),
        BANNER,
        "",
        "substitutions:",
        f'  name: "bringup-{slug(bid)}"',
        *p4_substitutions(facts),
        "",
        "esphome:",
        "  name: ${name}",
        f'  friendly_name: "Bring-up {facts["latter_device"]}"',
        "  includes:",
        "    - bringup.h",
        "  on_boot:",
        "    - priority: -100",
        "      then:",
        "        - lambda: 'latter_bringup::log_silicon();'",
        "        - delay: 1s",
        "        - script.execute: bringup_probe",
        "",
        *esp32_block(facts),
        "",
        "logger:",
        "  level: DEBUG",
    ]
    if is_p4(facts):
        lines.append("  hardware_uart: UART0")
    lines.append("")

    sda, scl = pin_of(facts, "i2c_sda"), pin_of(facts, "i2c_scl")
    addrs = [d["address"].lower() for d in facts.get("i2c_devices", [])]
    if sda is not None and scl is not None:
        lines += [
            "i2c:",
            "  - id: bus_internal",
            f"    sda: GPIO{sda}",
            f"    scl: GPIO{scl}",
            "    frequency: 100kHz",
            "    scan: false",
            "",
        ]
    pmu = drivers["pmu_axp2101"]

    lines += ["script:", "  - id: bringup_probe", "    then:", "      - lambda: |-"]
    if sda is not None and addrs:
        lines.append(
            f"          static const uint8_t expected[] = {{{', '.join(addrs)}}};"
        )
        lines.append(
            "          latter_bringup::probe_i2c("
            "id(bus_internal), expected, sizeof(expected));"
        )
    if pmu:
        lines.append("          latter_bringup::log_axp2101(id(bus_internal));")
    if sda is not None and drivers["rtc_pcf85063"]:
        lines += [
            "          uint8_t rtc_reg = 0x04, rtc_buf[7] = {};",
            (
                "          if (id(bus_internal).write_readv("
                "0x51, &rtc_reg, 1, rtc_buf, sizeof(rtc_buf)) "
                "== esphome::i2c::ERROR_OK) {"
            ),
            (
                '            ESP_LOGI("bringup", '
                '"[BRINGUP:RTC] pcf85063 os=%d bcd=20%02X-%02X-%02X '
                '%02X:%02X:%02X",'
            ),
            (
                "                     (int)((rtc_buf[0] >> 7) & 1), "
                "rtc_buf[6], rtc_buf[5] & 0x1F, rtc_buf[3] & 0x3F,"
            ),
            (
                "                     rtc_buf[2] & 0x3F, "
                "rtc_buf[1] & 0x7F, rtc_buf[0] & 0x7F);"
            ),
            "          }",
        ]
    if not (sda is not None and addrs) and not pmu:
        lines.append(
            '          ESP_LOGI("bringup", "[BRINGUP:I2C] no I2C bus in the facts");'
        )
    for p in sorted(
        (p for p in facts["pins"] if p["role"] in SWITCH_ROLES),
        key=lambda x: x["gpio"],
    ):
        sid = f"out_gpio{p['gpio']}"
        lines += [
            "      - delay: 2s",
            (
                f'      - logger.log: "[BRINGUP:OUT] {p["role"]} '
                f'GPIO{p["gpio"]} HIGH for 1 s"'
            ),
            f"      - switch.turn_on: {sid}",
            "      - delay: 1s",
            f"      - switch.turn_off: {sid}",
        ]
    if any(p["role"] in PWM_ROLES for p in facts["pins"]):
        for pct in (10, 50, 100):
            lines += [
                "      - delay: 1s",
                f'      - logger.log: "[BRINGUP:OUT] backlight {pct} percent"',
                "      - light.turn_on:",
                "          id: backlight_light",
                f"          brightness: {pct}%",
            ]
    lines.append("")

    if pmu:
        lines += [
            "interval:",
            "  - interval: 10s",
            "    then:",
            "      - lambda: 'latter_bringup::log_axp2101(id(bus_internal));'",
            "",
        ]

    inputs = [
        p
        for p in facts["pins"]
        if p["role"] in INPUT_ROLES or p["role"].endswith("_button")
    ]
    if inputs:
        lines.append("binary_sensor:")
        for p in sorted(inputs, key=lambda x: x["gpio"]):
            pull = (
                ["mode:", "  input: true", "  pullup: true"]
                if p.get("polarity") in ("active_low", None)
                else ["mode:", "  input: true"]
            )
            name = f"{p['role']} GPIO{p['gpio']}"
            lines += [
                "  - platform: gpio",
                f'    name: "{name}"',
                *gpio_pin_lines(p, "    ", pull),
                "    on_state:",
                "      then:",
                "        - logger.log:",
                f'            format: "[BRINGUP:EDGE] {name} -> %s"',
                '            args: [\'x ? "ON" : "OFF"\']',
            ]
        lines.append("")

    outputs = [p for p in facts["pins"] if p["role"] in SWITCH_ROLES]
    if outputs:
        lines.append("switch:")
        for p in sorted(outputs, key=lambda x: x["gpio"]):
            label = (
                "SD power OFF (gate high)"
                if p["role"] == "sd_power_gate"
                else p["role"]
            )
            lines += [
                "  - platform: gpio",
                f"    id: out_gpio{p['gpio']}",
                f'    name: "{label} GPIO{p["gpio"]}"',
                *gpio_pin_lines({**p, "polarity": None}, "    ", []),
                "    restore_mode: ALWAYS_OFF",
            ]
        lines.append("")

    pwm = [p for p in facts["pins"] if p["role"] in PWM_ROLES]
    if pwm:
        hz = facts["latter_flags"].get("LATTER_BACKLIGHT_PWM_HZ", 2000)
        p = pwm[0]
        lines += [
            "output:",
            "  - platform: ledc",
            "    id: backlight_pwm",
            f"    pin: GPIO{p['gpio']}",
            f"    frequency: {hz}Hz",
        ]
        if p.get("polarity") == "inverted":
            lines.append("    inverted: true")
        lines += [
            "",
            "light:",
            "  - platform: monochromatic",
            "    id: backlight_light",
            '    name: "Backlight sweep"',
            "    output: backlight_pwm",
            "    restore_mode: ALWAYS_ON",
            "",
        ]
    return "\n".join(lines).rstrip("\n") + "\n"


def _render_board_packages(
    facts: dict[str, Any], flags: dict[str, Any], drivers: dict[str, bool]
) -> list[str]:
    chip_pkg = "chip_esp32p4" if is_p4(facts) else "chip_esp32s3"
    pkg_lines = [
        "packages:",
        f"  latter_chip: !include ../modules/hal/{chip_pkg}.yaml",
    ]
    has_c6 = (
        is_p4(facts)
        and (
            "c6" in str(facts["silicon"].get("coprocessor", "")).lower()
            or pin_of(facts, "c6_chip_pu") is not None
        )
    )
    if has_c6:
        pkg_lines.append("  latter_radio: !include ../modules/hal/radio_esp32c6_sdio.yaml")
    i2s_pins = [
        pin_of(facts, r) for r in ("i2s_ws", "i2s_bclk", "i2s_mclk", "i2s_din", "i2s_dout")
    ]
    if _flag_bool(flags, "LATTER_HAS_AUDIO", True) and drivers["codec_es8311"] and all(p is not None for p in i2s_pins):
        has_es7210 = any("es7210" in str(d.get("part", "")).lower() for d in facts.get("i2c_devices", []))
        audio_pkg = "audio_es8311_es7210_duplex.yaml" if has_es7210 else "audio_es8311_duplex.yaml"
        pkg_lines.append(f"  latter_audio_hal: !include ../modules/hal/{audio_pkg}")
    if drivers["imu_qmi8658"]:
        pkg_lines.append("  latter_imu: !include ../modules/hal/imu_qmi8658.yaml")
    if drivers["pmu_axp2101"]:
        pkg_lines.append("  latter_pmu: !include ../modules/hal/pmu_axp2101.yaml")
    if _flag_bool(flags, "LATTER_HAS_BATTERY"):
        pkg_lines.append("  latter_battery_service: !include ../modules/battery_service.yaml")
    return pkg_lines


def _render_touch_block(facts: dict[str, Any]) -> list[str]:
    if not _flag_bool(facts["latter_flags"], "LATTER_HAS_TOUCH", True):
        return ["# Touchless board (capabilities.touch: false)."]
    t_spec = facts.get("touch")
    if not isinstance(t_spec, dict) or not t_spec.get("protocol"):
        return ["# BRINGUP-STEP touchscreen: id touch_screen, platform: latter_core."]
    t_ms = int(t_spec.get("active_poll_ms", 10))
    lines = [
        "touchscreen:",
        "  - platform: latter_core",
        f"    protocol: {str(t_spec['protocol']).lower()}",
        "    id: touch_screen",
        "    display: disp1",
        "    i2c_id: bus_internal",
        f"    address: {t_spec['address']}",
    ]
    if t_spec.get("delivery") == "irq" and t_spec.get("int_gpio") is not None:
        lines += [
            "    interrupt_pin:",
            f"      number: GPIO{t_spec['int_gpio']}",
            "      mode: INPUT_PULLUP",
        ]
        if t_spec.get("rst_gpio") is not None:
            lines.append(f"    reset_pin: GPIO{t_spec['rst_gpio']}")
        lines += [
            f"    active_poll_interval: {t_ms}ms",
            "    update_interval: never",
        ]
    else:
        lines += [
            f"    active_poll_interval: {t_ms}ms",
            f"    update_interval: {t_ms}ms",
        ]
    return lines


def render_board(facts: dict[str, Any]) -> str:
    bid = facts["board_id"]
    flags = facts["latter_flags"]
    drivers = detect_drivers(facts)
    hdr = [
        BANNER,
        (
            f"# Hardware Abstraction Layer (HAL): {facts['product']} "
            f'("{facts["latter_device"]}")'
        ),
        f"# Scaffolded by tools/new_board.py from boards/facts/{bid}.json.",
        (
            "# Complete each BRINGUP-STEP section and keep "
            "python tools/check_boards.py green."
        ),
    ]
    for p in sorted(facts["pins"], key=lambda x: x["gpio"]):
        note = f" ({p['note']})" if p.get("note") else ""
        hdr.append(f"#   GPIO{p['gpio']:<3} {p['role']}{note} [{p['confidence']}]")
    hdr.append(BANNER)
    stationary = _flag_bool(flags, "LATTER_IS_STATIONARY")
    has_audio = _flag_bool(flags, "LATTER_HAS_AUDIO", True)
    w_px = int(flags.get("LATTER_SCREEN_W", 466))
    h_px = int(flags.get("LATTER_SCREEN_H", 466))
    ppi = int(flags.get("LATTER_PANEL_PPI", 266))
    vs_pct = int(flags.get("LATTER_VIEW_SCALE_PCT", 0))
    fonts = compute_font_sizes(w_px, h_px, ppi, view_scale_pct=vs_pct, is_stationary=stationary)
    art_sq = f"{max(w_px, h_px)}x{max(w_px, h_px)}"
    body = [
        "",
        "substitutions:",
        *p4_substitutions(facts),
        *(f'  {k}: "{v}"' for k, v in fonts.items()),
        f'  art_resize: "{w_px}x{h_px}"',
        f'  sendspin_art_resize: "{art_sq}"',
        f'  api_reboot_timeout: "{"15min" if stationary else "0s"}"',
    ]
    if has_audio:
        body += [
            f'  sendspin_buffer_size: "{"1048576" if is_p4(facts) else "262144"}"',
            # Safe PSRAM default; opt into "true" only after bench-verifying headroom.
            '  mww_internal_ram: "false"',
            '  announcement_buffer_size: "131072"',
            '  audio_out_rate: "48000"',
            '  audio_out_channels: "1"',
            '  audio_out_bits: "16"',
            '  audio_in_rate: "16000"',
        ]
        for sub_name, role in (
            ("i2s_lrclk_pin", "i2s_ws"),
            ("i2s_bclk_pin", "i2s_bclk"),
            ("i2s_mclk_pin", "i2s_mclk"),
            ("i2s_din_pin", "i2s_din"),
            ("i2s_dout_pin", "i2s_dout"),
        ):
            g = pin_of(facts, role)
            if g is not None:
                body.append(f"  {sub_name}: GPIO{g}")
    imu_pin = pin_of(facts, "imu_int1")
    if imu_pin is not None:
        body.append(f"  imu_int1_pin: GPIO{imu_pin}")
    body += [
        "",
        *_render_latter_core_block(facts, drivers),
        "",
        # chip_esp32*.yaml owns esp32.variant/board, framework, and psram; only override flash_size here.
        "esp32:",
        f"  flash_size: {facts['silicon'].get('flash_mb', 16)}MB",
        "",
    ]
    sda, scl = pin_of(facts, "i2c_sda"), pin_of(facts, "i2c_scl")
    if sda is not None and scl is not None:
        body += [
            "i2c:",
            "  - id: bus_internal",
            f"    sda: GPIO{sda}",
            f"    scl: GPIO{scl}",
            "    frequency: 400kHz",
            "    scan: false",
            "",
        ]
    body += [*_render_board_packages(facts, flags, drivers), ""]
    body += [
        (
            "# BRINGUP-STEP display: id disp1, full-frame PSRAM buffer "
            "(docs/porting-hardware.md)."
        ),
        *_render_touch_block(facts),
        *(
            [
                (
                    "# BRINGUP-STEP audio: ensure full_duplex: true I2S + "
                    "speaker_enable switch (audio_es8311_*.yaml owns codec scripts)."
                )
            ]
            if has_audio
            else ["# Audio-less board (capabilities.audio: false)."]
        ),
        "# BRINGUP-STEP binary_sensor: boot_button (and pwr_key on battery PMU boards).",
        "",
        "script:",
    ]
    for sid, purpose in BOARD_LIFECYCLE_SCRIPTS:
        body += [
            f"  - id: {sid}",
            "    mode: restart",
            "    then:",
            f"      - lambda: '(void)0;  // BRINGUP-STEP: {purpose}'",
        ]
    return "\n".join(hdr + body).rstrip("\n") + "\n"


def render_device(facts: dict[str, Any], repo: str = DEFAULT_LATTER_REPO) -> str:
    dev = slug(facts["latter_device"])
    bid = facts["board_id"]
    minor = facts.get("ble_minor", 99)
    flags = facts["latter_flags"]
    stationary = _flag_bool(flags, "LATTER_IS_STATIONARY")
    has_audio = _flag_bool(flags, "LATTER_HAS_AUDIO", True)
    has_touch = _flag_bool(flags, "LATTER_HAS_TOUCH", True)
    owner, _, repo_name = repo.partition("/")
    pages_base = (
        f"https://{owner}.github.io/{repo_name}"
        if owner and repo_name
        else f"https://github.com/{repo}"
    )
    fw_pkg = (
        "../modules/latter_display_firmware.yaml"
        if has_audio
        else "../modules/latter_display_only_firmware.yaml"
    )
    extra_pkgs = (
        ["  latter_gui_touch: !include ../guis/universal/gui_touch.yaml"]
        if (not has_audio and has_touch)
        else []
    )
    return "\n".join(
        [
            BANNER,
            f"# {facts['latter_device']} on {facts['product']}",
            f"# Scaffolded by tools/new_board.py from boards/facts/{bid}.json.",
            f"# Repository: https://github.com/{repo}",
            BANNER,
            "",
            "substitutions:",
            f'  latter_repo: "{repo}"',
            "  latter_ref: main",
            f'  name: "{dev}"',
            f'  friendly_name: "{facts["latter_device"]}"',
            (
                "  # Web Installer builds pass -s name_add_mac_suffix true for "
                "unique <name>-<mac6> hostnames;"
            ),
            "  # Device Builder package wrappers keep clean custom names.",
            '  name_add_mac_suffix: "false"',
            "",
            "dashboard_import:",
            f"  package_import_url: github://{repo}/devices/{dev}.yaml@main",
            "  import_full_config: false",
            "",
            "external_components:",
            "  - source:",
            "      type: git",
            "      url: https://github.com/${latter_repo}",
            "      ref: ${latter_ref}",
            "    components: [latter_core, latter_ui, latter_beacon]",
            "    refresh: 0s",
            "",
            "esphome:",
            "  name: ${name}",
            "  name_add_mac_suffix: ${name_add_mac_suffix}",
            "  friendly_name: ${friendly_name}",
            "  min_version: 2026.11.1",
            "  project:",
            f'    name: "latter.{dev}"',
            '    version: "${version}"',
            "",
            "update:",
            "  - platform: http_request",
            "    id: latter_firmware_update",
            '    name: "Project Firmware"',
            f'    source: "{pages_base}/manifests/{dev}.json"',
            "    update_interval: never",
            "",
            "wifi:",
            "  id: wifi_ctrl",
            "  fast_connect: true",
            "  lease_cache: true",
            *([] if stationary else ["  reboot_timeout: 0s"]),
            "",
            *beacon_lines(facts, minor),
            "",
            "improv_serial:",
            "",
            "packages:",
            f"  hal: !include ../boards/{bid}.yaml",
            f"  latter_firmware: !include {fw_pkg}",
            *extra_pkgs,
            "",
        ]
    )


def write(path: Path, text: str, force: bool) -> None:
    if path.exists() and not force:
        print(f"skip   {path} (exists; use --force)")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote  {path}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "board_id", nargs="?", help="board id with a boards/facts/<id>.json file"
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=REPO,
        help="output root (default: the repository)",
    )
    ap.add_argument(
        "--repo",
        default=DEFAULT_LATTER_REPO,
        help=f"GitHub owner/repo for device YAML URLs (default: {DEFAULT_LATTER_REPO})",
    )
    ap.add_argument(
        "--facts",
        type=Path,
        default=None,
        help="explicit path to a board facts JSON file (for external community boards)",
    )
    ap.add_argument("--force", action="store_true", help="overwrite existing files")
    ap.add_argument(
        "--bringup",
        action="store_true",
        help="generate examples/bringup/<id>.yaml for every board",
    )
    ap.add_argument(
        "--check", action="store_true", help="with --bringup: fail on stale probes"
    )
    args = ap.parse_args()

    if args.bringup:
        stale = []
        for f in sorted(FACTS_DIR.glob("*.json")):
            if f.name == "schema.json":
                continue
            facts = json.loads(f.read_text(encoding="utf-8"))
            target = BRINGUP_DIR / f"{facts['board_id']}.yaml"
            text = render_bringup(facts)
            current = target.read_text(encoding="utf-8") if target.exists() else None
            if args.check:
                if current != text:
                    stale.append(str(target.relative_to(REPO)))
            elif current != text:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text, encoding="utf-8", newline="\n")
                print(f"wrote  {target.relative_to(REPO)}")
        if stale:
            print(
                "ERROR stale bring-up probes "
                "(run python tools/new_board.py --bringup): "
                + ", ".join(stale)
            )
            return 1
        return 0

    if not args.board_id and args.facts is None:
        ap.error("board_id (or --facts) is required unless --bringup is given")
    facts = load_facts(args.board_id or "", args.facts)
    bid = str(facts["board_id"])
    out = args.out.resolve()
    write(out / "boards" / f"{bid}.yaml", render_board(facts), args.force)
    write(
        out / "devices" / f"{slug(facts['latter_device'])}.yaml",
        render_device(facts, repo=args.repo),
        args.force,
    )
    write(
        out / "examples" / "bringup" / f"{bid}.yaml",
        render_bringup(facts),
        args.force,
    )
    print(
        "next: complete BRINGUP-STEP sections, then run "
        "python tools/check_boards.py (or --external for community boards)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
