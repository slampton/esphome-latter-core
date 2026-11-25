#!/usr/bin/env python3
"""Board Facts ledger checker for esphome-latter-core.

Every supported board has a hardware facts file, boards/facts/<board_id>.json
(schema: boards/facts/schema.json), that records each routed net with its role,
polarity, rail and the confidence of the source. This tool cross-checks the
facts against the board HAL YAML (boards/<board_id>.yaml) and the device YAML
that includes it, and renders the generated pin-map section of docs/hardware.md.

Standard library only (CI parity with tools/gen_protocol.py). YAML is scanned
with comment-aware tokenization and regular expressions, so !include / !extend /
!lambda never need third-party YAML parsers.

Usage:
  python tools/check_boards.py                                       # check every flagship board
  python tools/check_boards.py --release                             # fail on L-confidence facts
  python tools/check_boards.py --render-docs                         # rewrite docs/hardware.md pin map
  python tools/check_boards.py --check-docs                          # fail if docs/hardware.md stale
  python tools/check_boards.py --dump <id>                           # print YAML scan for one board
  python tools/check_boards.py --external --facts F --board B        # validate an external community board
  python tools/check_boards.py --external <board_id>                 # validate boards/<id>.yaml without flagship lock-in
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
BOARDS_DIR = REPO / "boards"
FACTS_DIR = BOARDS_DIR / "facts"
DEVICES_DIR = REPO / "devices"
HARDWARE_MD = REPO / "docs" / "hardware.md"

DOC_BEGIN = (
    "<!-- BEGIN GENERATED: tools/check_boards.py --render-docs "
    "(do not edit by hand) -->"
)
DOC_END = "<!-- END GENERATED: tools/check_boards.py -->"

# Deep-sleep wake capability per SoC: S3 RTC GPIO0-21, P4 LP GPIO0-15, S2 RTC 0-21, C6 LP 0-7, C3 LP 0-5.
WAKE_CAPABLE = {
    "ESP32-S3": set(range(22)),
    "ESP32-P4": set(range(16)),
    "ESP32-S2": set(range(22)),
    "ESP32-C6": set(range(8)),
    "ESP32-C3": set(range(6)),
    "ESP32": {0, 2, 4, 12, 13, 14, 15, 25, 26, 27, 32, 33, 34, 35, 36, 39},
}
STRAPPING = {
    "ESP32-S3": {0, 3, 45, 46},
    "ESP32-P4": {34, 35, 36, 37, 38},
    "ESP32-S2": {0, 45, 46},
    "ESP32-C6": {4, 5, 8, 9, 15},
    "ESP32-C3": {2, 8, 9},
    "ESP32": {0, 2, 5, 12, 15},
}
CONFIDENCE = {"H", "M", "L"}
ROLE_UNUSABLE = {"reserved", "unrouted"}
# Light-sleep traits -> the facts roles they may name (include/hal_sleep_pm.h).
LIGHT_SLEEP_TRAIT_ROLES = {
    "LATTER_PWR_KEY_PIN": {"pwr_key"},
    "LATTER_ACTION_BTN_PIN": {"boot_button", "key3_button"},
    "LATTER_TOUCH_INT_PIN": {"touch_int"},
    "LATTER_IMU_INT1_PIN": {"imu_int1"},
    "LATTER_TOUCH_RST_PIN": {"touch_reset"},
}
LIGHT_SLEEP_WAKE_TRAITS = {
    "LATTER_PWR_KEY_PIN",
    "LATTER_ACTION_BTN_PIN",
    "LATTER_TOUCH_INT_PIN",
    "LATTER_IMU_INT1_PIN",
}
REQUIRED_TOP = (
    "board_id",
    "product",
    "latter_device",
    "silicon",
    "power",
    "latter_flags",
    "pins",
    "sources",
)

PIN_RE = re.compile(r"\bGPIO(?:_NUM_)?(\d+)\b")
FLAG_PIN_RE = re.compile(r"-DLATTER_[A-Z0-9_]*_PIN=(\d+)")
FLAG_RE = re.compile(r"-D(LATTER_[A-Z0-9_]+)=([^\"'\s]+)")
ADDR_RE = re.compile(r"^\s*address:\s*(0x[0-9A-Fa-f]+)", re.MULTILINE)
MINOR_RE = re.compile(r"^\s*minor:\s*(\d+)", re.MULTILINE)
BOARD_INCLUDE_RE = re.compile(r"!include\s+\.\./boards/([a-z0-9_]+)\.yaml")


def _safe_int(val: Any) -> int | None:
    if isinstance(val, bool) or val is None:
        return None
    if isinstance(val, int):
        return val
    try:
        return int(str(val).strip(), 10)
    except (TypeError, ValueError):
        return None


def _axp2101_icc_floor(ma: int) -> int:
    if ma < 25:
        return 25
    if ma <= 200:
        return ma // 25 * 25
    return 1000 if ma >= 1000 else 200 + (ma - 200) // 100 * 100


def _axp2101_icc_ceil(ma: int) -> int:
    if ma <= 25:
        return 25
    if ma <= 200:
        return (ma + 24) // 25 * 25
    return 1000 if ma >= 1000 else 200 + (ma - 200 + 99) // 100 * 100


def battery_charge_ma(mah: int) -> int:
    """Constant-charge current include/hal_battery.h derives (kBattChargeMa):
    <=0.5C floored to the AXP2101 step, never above 0.8C or 500 mA.
    """
    cap = _axp2101_icc_floor(min(mah * 8 // 10, 500))
    return min(_axp2101_icc_floor(mah // 2), cap)


def _expand_board_hal_includes(text: str) -> str:
    def _sub(match: re.Match[str]) -> str:
        rel = match.group(1)
        inc_path = REPO / "modules" / "hal" / rel
        if inc_path.exists():
            return inc_path.read_text(encoding="utf-8")
        return match.group(0)

    return re.sub(r"!include\s+\.\./modules/hal/([a-zA-Z0-9_]+\.yaml)", _sub, text)


def _strip_line_comment(line: str) -> str:
    """Strip unquoted YAML (#) and C++ (//) comments while preserving URLs."""
    in_single = False
    in_double = False
    escaped = False
    n = len(line)
    for idx, ch in enumerate(line):
        if in_single:
            if ch == "'":
                in_single = False
            continue
        if in_double:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_double = False
            continue
        if ch == "'":
            in_single = True
            continue
        if ch == '"':
            in_double = True
            continue
        if ch == "#" and (idx == 0 or line[idx - 1] in (" ", "\t")):
            return line[:idx].rstrip()
        if (
            ch == "/"
            and idx + 1 < n
            and line[idx + 1] == "/"
            and (idx == 0 or line[idx - 1] != ":")
        ):
            return line[:idx].rstrip()
    return line


def strip_comments(text: str) -> str:
    """Drop YAML (#) and C++ (//) comments so header prose never counts as pins."""
    out: list[str] = []
    for raw_line in text.splitlines():
        stripped = raw_line.lstrip()
        if stripped.startswith(("#", "//")):
            continue
        out.append(_strip_line_comment(raw_line))
    return "\n".join(out)


_CORE_BOOL_TRUE = {"true", "yes", "on", "1"}
_CORE_BOOL_FALSE = {"false", "no", "off", "0"}

_CORE_SECTION_FLAG_MAP: dict[str, dict[str, tuple[str, bool, bool]]] = {
    # section -> yaml_key -> (LATTER_* macro, is_bool, is_gpio_pin)
    "display": {
        "width": ("LATTER_SCREEN_W", False, False),
        "height": ("LATTER_SCREEN_H", False, False),
        "ppi": ("LATTER_PANEL_PPI", False, False),
        "round": ("LATTER_ROUND_DISPLAY", True, False),
        "ips": ("LATTER_PANEL_IPS", True, False),
        "dcs_sleep": ("LATTER_PANEL_DCS_SLEEP", True, False),
        "dc_pin": ("LATTER_DISPLAY_DC_PIN", False, True),
        "corner_r": ("LATTER_CORNER_R", False, False),
        "view_scale": ("LATTER_VIEW_SCALE_PCT", False, False),
        "dsi_iram_safe": ("LATTER_DSI_IRAM_SAFE", True, False),
        "slot_count": ("LATTER_SLOT_COUNT", False, False),
        "card_slot_count": ("LATTER_CARD_SLOT_COUNT", False, False),
        "bri_gamma_x100": ("LATTER_BRI_GAMMA_X100", False, False),
        "bri_dcs_min": ("LATTER_BRI_DCS_MIN", False, False),
        "bri_dcs_max": ("LATTER_BRI_DCS_MAX", False, False),
        "touch_int_pin": ("LATTER_TOUCH_INT_PIN", False, True),
        "touch_rst_pin": ("LATTER_TOUCH_RST_PIN", False, True),
    },
    "backlight": {
        "pin": ("LATTER_BACKLIGHT_PIN", False, True),
        "en_pin": ("LATTER_BACKLIGHT_EN_PIN", False, True),
        "en_inverted": ("LATTER_BACKLIGHT_EN_INVERTED", True, False),
        "inverted": ("LATTER_BACKLIGHT_INVERTED", True, False),
        "custom": ("LATTER_BACKLIGHT_CUSTOM", True, False),
        "pwm_hz": ("LATTER_BACKLIGHT_PWM_HZ", False, False),
        "pwm_bits": ("LATTER_BACKLIGHT_PWM_BITS", False, False),
        "duty_min_pm": ("LATTER_BACKLIGHT_DUTY_MIN_PM", False, False),
        "duty_max_pm": ("LATTER_BACKLIGHT_DUTY_MAX_PM", False, False),
    },
    "power": {
        "battery_mah": ("LATTER_BATTERY_MAH", False, False),
        "pwr_key_pin": ("LATTER_PWR_KEY_PIN", False, True),
        "pwr_key_active_high": ("LATTER_PWR_KEY_ACTIVE_HIGH", True, False),
        "pwr_key_deep_sleep_wake": ("LATTER_PWR_KEY_DEEP_SLEEP_WAKE", True, False),
        "action_btn_pin": ("LATTER_ACTION_BTN_PIN", False, True),
        "imu_int1_pin": ("LATTER_IMU_INT1_PIN", False, True),
        "c6_reset_pin": ("LATTER_C6_RESET_PIN", False, True),
        "pmu_ts_disable": ("LATTER_PMU_TS_DISABLE", True, False),
        "pmu_dcdc_keep_mask": ("LATTER_PMU_DCDC_KEEP_MASK", False, False),
        "pmu_ldo_keep_mask": ("LATTER_PMU_LDO_KEEP_MASK", False, False),
    },
    "capabilities": {
        "display": ("LATTER_HAS_DISPLAY", True, False),
        "touch": ("LATTER_HAS_TOUCH", True, False),
        "audio": ("LATTER_HAS_AUDIO", True, False),
        "mic": ("LATTER_HAS_MIC", True, False),
        "speaker": ("LATTER_HAS_SPEAKER", True, False),
        "imu": ("LATTER_HAS_IMU", True, False),
        "battery": ("LATTER_HAS_BATTERY", True, False),
        "rtc": ("LATTER_HAS_RTC", True, False),
        "stationary": ("LATTER_IS_STATIONARY", True, False),
        "wearable": ("LATTER_IS_WEARABLE", True, False),
        "video_stream": ("LATTER_HAS_VIDEO_STREAM", True, False),
        "hw_jpeg": ("LATTER_HAS_HW_JPEG", True, False),
    },
    "drivers": {
        "pmu_axp2101": ("LATTER_DRV_PMU_AXP2101", True, False),
        "rtc_pcf85063": ("LATTER_DRV_RTC_PCF85063", True, False),
        "imu_qmi8658": ("LATTER_DRV_IMU_QMI8658", True, False),
        "codec_es8311": ("LATTER_DRV_CODEC_ES8311", True, False),
    },
    "diagnostics": {
        "nvs_guard": ("LATTER_DIAG_NVS_GUARD", True, False),
        "twdt_breadcrumb": ("LATTER_DIAG_TWDT_BREADCRUMB", True, False),
    },
}


def _norm_core_val(raw: str, is_bool: bool) -> str:
    val = raw.strip().strip('"').strip("'")
    if is_bool:
        low = val.lower()
        if low in _CORE_BOOL_TRUE:
            return "1"
        if low in _CORE_BOOL_FALSE:
            return "0"
    return val


def _parse_latter_core_block(code: str) -> tuple[dict[str, str], set[int]]:
    """Parse typed `latter_core:` YAML config into LATTER_* flags and referenced GPIO numbers."""
    flags: dict[str, str] = {}
    pins: set[int] = set()
    in_core = False
    section: str | None = None
    saw_core = False

    for raw_line in code.splitlines():
        if not raw_line.strip():
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        stripped = raw_line.strip()
        if indent == 0:
            in_core = stripped == "latter_core:"
            section = None
            if in_core:
                saw_core = True
            continue
        if not in_core:
            continue
        if indent == 2:
            if stripped.endswith(":") and ":" not in stripped[:-1]:
                section = stripped[:-1].strip()
                continue
            section = None
            if ":" not in stripped:
                continue
            key, val = stripped.split(":", 1)
            key = key.strip()
            val_clean = _norm_core_val(val, False)
            if key == "board_id":
                flags["LATTER_BOARD_ID"] = val_clean
            elif key == "ble_minor":
                flags["LATTER_BLE_MINOR"] = val_clean
            elif key == "gui":
                flags["LATTER_GUI_UNIVERSAL"] = "1" if val_clean.lower() == "universal" else "0"
            elif key == "chip":
                flags["LATTER_CHIP_P4"] = "1" if val_clean.lower() == "esp32p4" else "0"
            elif key == "psram_mb":
                flags["LATTER_PSRAM_MB"] = val_clean
        elif indent >= 4 and section in _CORE_SECTION_FLAG_MAP and ":" in stripped:
            key, val = stripped.split(":", 1)
            k_clean = key.strip()
            spec = _CORE_SECTION_FLAG_MAP[section].get(k_clean)
            if spec is None:
                continue
            macro, is_bool, is_pin = spec
            norm = _norm_core_val(val, is_bool)
            if section == "display" and k_clean == "view_scale":
                norm = str(round(float(norm) * 100.0))
            flags[macro] = norm
            if is_pin:
                pin_num = _safe_int(norm)
                if pin_num is not None:
                    pins.add(pin_num)

    if saw_core:
        gui_univ = flags.get("LATTER_GUI_UNIVERSAL", "1") == "1"
        flags.setdefault("LATTER_ROUND_DISPLAY", "0")
        flags.setdefault("LATTER_PANEL_IPS", "0")
        flags.setdefault("LATTER_HAS_DISPLAY", "1" if gui_univ else "0")
        flags.setdefault("LATTER_HAS_TOUCH", "1" if gui_univ else "0")
        flags.setdefault("LATTER_HAS_AUDIO", "1")
        flags.setdefault("LATTER_HAS_IMU", "0")
        flags.setdefault("LATTER_HAS_BATTERY", "0")
        flags.setdefault("LATTER_HAS_RTC", "0")
        flags.setdefault("LATTER_IS_STATIONARY", "0")
        flags.setdefault("LATTER_IS_WEARABLE", "0")
    return flags, pins


def scan_board_yaml(board_id: str, board_path: Path | None = None) -> dict[str, Any]:
    path = board_path if board_path is not None else (BOARDS_DIR / f"{board_id}.yaml")
    text = path.read_text(encoding="utf-8")
    expanded = _expand_board_hal_includes(text)
    code = strip_comments(expanded)
    core_flags, core_pins = _parse_latter_core_block(code)
    pins = (
        {int(m) for m in PIN_RE.findall(code)}
        | {int(m) for m in FLAG_PIN_RE.findall(code)}
        | core_pins
    )
    flags: dict[str, str] = dict(core_flags)
    for name, value in FLAG_RE.findall(code):
        flags[name] = value.strip('"').strip("\\").strip('"')
    addrs = {a.lower() for a in ADDR_RE.findall(code)}
    return {
        "path": path,
        "text": text,
        "code": code,
        "pins": pins,
        "flags": flags,
        "addresses": addrs,
    }


def _parse_top_level_substitutions(text: str) -> dict[str, str]:
    subs: dict[str, str] = {}
    in_subs = False
    for raw in strip_comments(text).splitlines():
        if not raw.strip():
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        if indent == 0:
            in_subs = raw.strip() == "substitutions:"
            continue
        if in_subs and ":" in raw:
            key, val = raw.strip().split(":", 1)
            subs[key.strip()] = val.strip().strip('"').strip("'")
    return subs


def _resolve_api_reboot_timeout(
    base_text: str, board_text: str, dev_text: str
) -> str | None:
    # Match ESPHome's real package merge order: hal (boards/*.yaml) -> latter_firmware (modules/system_core.yaml) -> devices/*.yaml.
    subs: dict[str, str] = {}
    for src in (board_text, base_text, dev_text):
        subs.update(_parse_top_level_substitutions(src))

    raw_val: str | None = None
    for src in (board_text, base_text, dev_text):
        in_api = False
        for ln in strip_comments(src).splitlines():
            if not ln.strip():
                continue
            indent = len(ln) - len(ln.lstrip(" "))
            if indent == 0:
                in_api = ln.strip() == "api:"
                continue
            if in_api and indent == 2 and ln.lstrip().startswith("reboot_timeout:"):
                raw_val = ln.split(":", 1)[1].strip().strip('"').strip("'")
    if raw_val is None:
        return None
    m_sub = re.fullmatch(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", raw_val)
    if m_sub:
        return subs.get(m_sub.group(1))
    return raw_val


def device_for_board(board_id: str) -> Path | None:
    for dev in sorted(DEVICES_DIR.glob("*.yaml")):
        m = BOARD_INCLUDE_RE.search(dev.read_text(encoding="utf-8"))
        if m and m.group(1) == board_id:
            return dev
    return None


def load_facts() -> dict[str, dict[str, Any]]:
    facts: dict[str, dict[str, Any]] = {}
    for f in sorted(FACTS_DIR.glob("*.json")):
        if f.name == "schema.json":
            continue
        facts[f.stem] = json.loads(f.read_text(encoding="utf-8"))
    return facts


def _check_driver_identity_parity(
    facts: dict[str, Any], scan: dict[str, Any], err: Any
) -> None:
    """Ensure explicit LATTER_DRV_* macros match the board's real hardware facts."""
    i2c_devs = facts.get("i2c_devices", [])
    parts = [str(d.get("part", "")).lower() for d in i2c_devs]
    expected = {
        "LATTER_DRV_PMU_AXP2101": facts.get("power", {}).get("pmu") == "AXP2101",
        "LATTER_DRV_RTC_PCF85063": any(
            str(d.get("address", "")).lower() == "0x51"
            and "pcf85063" in str(d.get("part", "")).lower()
            for d in i2c_devs
        ),
        "LATTER_DRV_IMU_QMI8658": any("qmi8658" in p for p in parts),
        "LATTER_DRV_CODEC_ES8311": any("es8311" in p for p in parts),
    }
    for macro, want in expected.items():
        actual = scan["flags"].get(macro, "0")
        want_s = "1" if want else "0"
        if str(actual) != want_s:
            err(
                f"{macro}={actual} in the HAL disagrees with hardware facts "
                f"(expected {want_s})"
            )


def _check_flagship_wiring(
    board_id: str, facts: dict[str, Any], scan: dict[str, Any], err: Any
) -> None:
    """Validate flagship repo integration (devices/*.yaml, BLE minor, reboot_timeout, release.yml)."""
    dev = device_for_board(board_id)
    if dev is None:
        err("no devices/*.yaml includes this board")
    else:
        dev_text = dev.read_text(encoding="utf-8")
        m = MINOR_RE.search(dev_text)
        fact_minor = _safe_int(facts.get("ble_minor"))
        if facts.get("ble_minor") is not None:
            dev_minor = _safe_int(m.group(1)) if m else None
            if fact_minor is None or dev_minor != fact_minor:
                err(
                    f"{dev.name} beacon minor does not equal "
                    f"facts ble_minor {facts['ble_minor']}"
                )
            if scan["flags"].get("LATTER_BLE_MINOR") not in (
                None,
                str(facts["ble_minor"]),
            ):
                err(
                    f"LATTER_BLE_MINOR={scan['flags'].get('LATTER_BLE_MINOR')} "
                    "does not equal facts ble_minor"
                )
        sys_core_mod = (REPO / "modules" / "system_core.yaml").read_text(encoding="utf-8")
        eff_reboot = _resolve_api_reboot_timeout(sys_core_mod, scan["text"], dev_text)
        stationary_flag = str(facts.get("latter_flags", {}).get("LATTER_IS_STATIONARY"))
        if stationary_flag == "0" and eff_reboot != "0s":
            err(
                "mobile board api.reboot_timeout must resolve to '0s', "
                f"got {eff_reboot!r}"
            )
        elif stationary_flag == "1" and (not eff_reboot or eff_reboot == "0s"):
            err(
                "stationary board api.reboot_timeout must be enabled (e.g. '15min'), "
                f"got {eff_reboot!r}"
            )
    release_yml = (REPO / ".github" / "workflows" / "release.yml").read_text(
        encoding="utf-8"
    )
    if dev is not None and dev.stem not in release_yml:
        err(f"{dev.stem} has no build job in .github/workflows/release.yml")


def check_board(
    board_id: str,
    facts: dict[str, Any],
    release: bool,
    errors: list[str],
    blockers: list[str],
    *,
    external: bool = False,
    board_path: Path | None = None,
) -> None:
    def err(msg: str) -> None:
        errors.append(f"[{board_id}] {msg}")

    for key in REQUIRED_TOP:
        if key not in facts:
            err(f"facts missing required section '{key}'")
    if facts.get("board_id") != board_id:
        err(f"facts board_id '{facts.get('board_id')}' does not match file name")
    resolved_board_path = (
        board_path if board_path is not None else (BOARDS_DIR / f"{board_id}.yaml")
    )
    if not resolved_board_path.exists():
        err(f"board YAML not found: {resolved_board_path}")
        return

    soc = facts.get("silicon", {}).get("soc", "")
    if soc not in WAKE_CAPABLE:
        err(f"unknown silicon.soc '{soc}'")
        return

    scan = scan_board_yaml(board_id, resolved_board_path)

    # 1. Pin facts: preserve all entries/roles per GPIO and validate shared_with.
    pins_by_gpio: dict[int, list[dict[str, Any]]] = {}
    for pin in facts.get("pins", []):
        if not isinstance(pin, dict):
            err(f"malformed pin entry: {pin!r}")
            continue
        raw_g = pin.get("gpio")
        if isinstance(raw_g, bool) or not isinstance(raw_g, int):
            err(f"pin entry without integer gpio: {pin}")
            continue
        g = raw_g
        existing_list = pins_by_gpio.get(g, [])
        if (
            existing_list
            and not pin.get("shared_with")
            and not any(prev.get("shared_with") for prev in existing_list)
        ):
            prev_roles = "/".join(str(p.get("role")) for p in existing_list)
            err(
                f"GPIO{g} listed twice ({prev_roles} / {pin.get('role')}) "
                "without shared_with"
            )
        pins_by_gpio.setdefault(g, []).append(pin)
        conf = pin.get("confidence")
        if conf not in CONFIDENCE:
            err(f"GPIO{g} confidence must be H/M/L, got {conf!r}")
        elif conf == "L":
            note = pin.get("note", "needs bring-up evidence")
            blockers.append(
                f"[{board_id}] GPIO{g} {pin.get('role')}: L confidence ({note})"
            )
        if not pin.get("role"):
            err(f"GPIO{g} has no role")
        if pin.get("strapping") and g not in STRAPPING[soc]:
            err(f"GPIO{g} is marked strapping in facts, which is invalid on {soc}")

    def roles_for_gpio(gpio_num: int) -> set[str]:
        return {
            str(entry.get("role"))
            for entry in pins_by_gpio.get(gpio_num, [])
            if entry.get("role")
        }

    # 1b. 1-lane SPI panels with DCS sleep must declare LATTER_DISPLAY_DC_PIN.
    if (
        scan["flags"].get("LATTER_PANEL_IPS") == "1"
        and scan["flags"].get("LATTER_PANEL_DCS_SLEEP") == "1"
        and facts.get("display", {}).get("bus") == "spi"
    ):
        dc_val = scan["flags"].get("LATTER_DISPLAY_DC_PIN")
        dc_gpio = _safe_int(dc_val)
        if dc_val is None:
            err(
                "1-lane SPI panel with LATTER_PANEL_DCS_SLEEP=1 must define "
                "LATTER_DISPLAY_DC_PIN"
            )
        elif dc_gpio is None or "display_dc" not in roles_for_gpio(dc_gpio):
            err(
                f"LATTER_DISPLAY_DC_PIN={dc_val} does not point at display_dc "
                "in the facts"
            )

    # 2. Every GPIO the HAL touches is a known, usable net.
    for g in sorted(scan["pins"]):
        entries = pins_by_gpio.get(g)
        if not entries:
            err(f"GPIO{g} is used in boards/{board_id}.yaml but has no fact")
            continue
        has_usable = any(
            e.get("role") not in ROLE_UNUSABLE or bool(e.get("shared_with"))
            for e in entries
        )
        if not has_usable:
            roles_desc = "/".join(sorted(roles_for_gpio(g)))
            err(f"GPIO{g} is used in the HAL but the facts mark it {roles_desc}")

    # 3. Capability flags equal the ledger.
    for name, expected in facts.get("latter_flags", {}).items():
        actual = scan["flags"].get(name)
        if actual is None:
            err(f"{name} expected {expected} but the HAL does not define it")
        elif str(actual) != str(expected):
            err(f"{name}={actual} in the HAL but the facts say {expected}")

    # 4. Deep-sleep wake pins must be RTC/LP capable.
    if scan["flags"].get("LATTER_PWR_KEY_DEEP_SLEEP_WAKE") == "1":
        key = scan["flags"].get("LATTER_PWR_KEY_PIN")
        key_gpio = _safe_int(key)
        if key is not None and (key_gpio is None or key_gpio not in WAKE_CAPABLE[soc]):
            err(
                f"PWR key GPIO{key} is a deep-sleep wake source "
                f"but is not wake-capable on {soc}"
            )
    for g, entries in pins_by_gpio.items():
        for pin in entries:
            if pin.get("deep_sleep_wake") and g not in WAKE_CAPABLE[soc]:
                err(
                    f"GPIO{g} ({pin.get('role')}) is marked deep_sleep_wake "
                    f"but is not wake-capable on {soc}"
                )

    # 5. Interrupt traits never point at an exclusively unrouted net.
    for g, entries in pins_by_gpio.items():
        if g in scan["pins"] and all(e.get("role") == "unrouted" for e in entries):
            err(f"GPIO{g} is unrouted per the facts but the HAL uses it")

    # 5b. Light-sleep traits (include/hal_sleep_pm.h) name the matching net.
    light_sleep = bool(
        re.search(r"(?m)^\s*enable_light_sleep:\s*true\b", scan["code"])
    )
    armed: set[int] = set()
    for flag, roles in LIGHT_SLEEP_TRAIT_ROLES.items():
        value = scan["flags"].get(flag)
        if value is None:
            continue
        pin_gpio = _safe_int(value)
        if pin_gpio is None or not (roles_for_gpio(pin_gpio) & roles):
            want_roles = "/".join(sorted(roles))
            err(f"{flag}={value} does not point at a {want_roles} net in the facts")
            continue
        if flag in LIGHT_SLEEP_WAKE_TRAITS:
            armed.add(pin_gpio)
    marked = {
        g
        for g, entries in pins_by_gpio.items()
        if any(e.get("light_sleep_wake") for e in entries)
    }
    if light_sleep:
        for g in sorted(marked - armed):
            err(
                f"GPIO{g} is marked light_sleep_wake "
                "but no LATTER_*_PIN wake trait arms it"
            )
        for g in sorted(armed - marked):
            err(
                f"GPIO{g} is a light-sleep wake trait "
                "but the facts do not mark it light_sleep_wake"
            )
        imu_sub = re.search(r"(?m)^\s*imu_int1_pin:\s*GPIO(\d+)", scan["code"])
        if imu_sub and scan["flags"].get("LATTER_IMU_INT1_PIN") != imu_sub.group(1):
            err("LATTER_IMU_INT1_PIN must equal the imu_int1_pin substitution")
    elif marked:
        err("facts mark light_sleep_wake pins but the HAL does not enable light sleep")

    # 6. I2C devices & LATTER_HAS_RTC.
    known_addrs = {
        str(d.get("address", "")).lower() for d in facts.get("i2c_devices", [])
    }
    for addr in sorted(scan["addresses"]):
        if addr not in known_addrs:
            err(f"I2C address {addr} appears in the HAL but not in facts.i2c_devices")
    want_rtc = 1 if "0x51" in known_addrs else 0
    rtc_flag = facts.get("latter_flags", {}).get("LATTER_HAS_RTC")
    if rtc_flag is None or str(rtc_flag) != str(want_rtc):
        rtc_desc = "0x51 PCF85063 present" if want_rtc else "no 0x51 RTC"
        err(
            f"latter_flags.LATTER_HAS_RTC must be {want_rtc} "
            f"({rtc_desc} in facts.i2c_devices)"
        )

    # 6b. Touch HAL contract.
    if str(facts.get("latter_flags", {}).get("LATTER_HAS_TOUCH")) == "1":
        t_facts = facts.get("touch")
        if not isinstance(t_facts, dict):
            err("LATTER_HAS_TOUCH=1 requires a 'touch' object in boards/facts")
        else:
            proto = str(t_facts.get("protocol", "")).lower()
            t_addr = str(t_facts.get("address", "")).lower()
            delivery = str(t_facts.get("delivery", ""))
            uses_latter_touch = bool(
                re.search(r"(?m)^\s*-\s*platform:\s*latter_core\b", scan["code"])
            )
            if not external and not uses_latter_touch:
                err("touchscreen block must use first-party 'platform: latter_core'")
            if uses_latter_touch or not external:
                if proto not in {"ft6336", "ft3168", "gt911", "cst9217", "cst816s"}:
                    err(
                        f"touch.protocol '{proto}' is not a registered "
                        "Latter Touch HAL protocol"
                    )
                if not re.search(
                    rf"(?m)^\s*protocol:\s*{re.escape(proto)}\b", scan["code"]
                ):
                    err(f"touchscreen block must declare 'protocol: {proto}'")
            if t_addr and t_addr not in known_addrs:
                err(f"touch.address '{t_addr}' is not listed in facts.i2c_devices")
            int_gpio = t_facts.get("int_gpio")
            if delivery == "irq":
                if isinstance(int_gpio, bool) or not isinstance(int_gpio, int):
                    err("touch.delivery=='irq' requires integer touch.int_gpio")
                else:
                    if "touch_int" not in roles_for_gpio(int_gpio):
                        err(
                            f"touch.int_gpio={int_gpio} does not match a "
                            "touch_int net in facts.pins"
                        )
                    if scan["flags"].get("LATTER_TOUCH_INT_PIN") != str(int_gpio):
                        err(
                            f"LATTER_TOUCH_INT_PIN must equal "
                            f"touch.int_gpio ({int_gpio})"
                        )
            elif delivery == "polled":
                if int_gpio is not None:
                    err("touch.delivery=='polled' must have touch.int_gpio = null")
            else:
                err(f"touch.delivery must be 'irq' or 'polled', got {delivery!r}")
            rst_raw = t_facts.get("rst_gpio")
            if rst_raw is not None:
                rst_gpio = _safe_int(rst_raw)
                if rst_gpio is None or "touch_reset" not in roles_for_gpio(rst_gpio):
                    err(
                        f"touch.rst_gpio={rst_raw} does not match a "
                        "touch_reset net in facts.pins"
                    )
                elif uses_latter_touch and not re.search(
                    rf"(?m)^\s*reset_pin:\s*GPIO{rst_gpio}\b", scan["code"]
                ):
                    err(
                        f"touchscreen block must declare 'reset_pin: GPIO{rst_gpio}' "
                        "(LatterTouchscreen owns non-blocking touch reset)"
                    )

    # 6c. Driver-identity parity (LATTER_DRV_* vs real hardware facts).
    _check_driver_identity_parity(facts, scan, err)

    # 7. Backlight PWM floor.
    bl = facts.get("backlight") or {}
    min_pwm = _safe_int(bl.get("min_pwm_hz"))
    if min_pwm is not None and min_pwm > 0 and scan["flags"].get("LATTER_BACKLIGHT_CUSTOM") != "1":
        hz_raw = scan["flags"].get("LATTER_BACKLIGHT_PWM_HZ")
        hz = _safe_int(hz_raw)
        if hz is None or hz < min_pwm:
            err(
                f"LATTER_BACKLIGHT_PWM_HZ={hz_raw} is below the "
                f"backlight min_pwm_hz {min_pwm}"
            )

    # 8. Battery boards carry a valid capacity (100..20000 mAh); AXP2101 boards follow its charge profile.
    power = facts.get("power", {})
    for rail in power.get("rails", []):
        if rail.get("confidence") not in CONFIDENCE:
            err(f"rail {rail.get('rail')} confidence must be H/M/L")
        elif rail["confidence"] == "L":
            r_note = rail.get("note", "needs bring-up evidence")
            blockers.append(
                f"[{board_id}] rail {rail.get('rail')}: L confidence ({r_note})"
            )
    if str(facts.get("latter_flags", {}).get("LATTER_HAS_BATTERY")) == "1":
        mah = _safe_int(power.get("battery_mah"))
        if mah is None or not (100 <= mah <= 20000) or (not external and not power.get("pmu")):
            err("LATTER_HAS_BATTERY=1 needs integer power.battery_mah (100..20000) and power.pmu")
        else:
            flag_mah = _safe_int(
                facts.get("latter_flags", {}).get("LATTER_BATTERY_MAH")
            )
            if flag_mah is None or flag_mah != mah:
                err(
                    f"latter_flags.LATTER_BATTERY_MAH must equal "
                    f"power.battery_mah ({mah})"
                )
            if power.get("pmu") == "AXP2101":
                want_ma = battery_charge_ma(mah)
                if power.get("charge_ma") != want_ma:
                    err(
                        f"power.charge_ma must be {want_ma} mA, the profile "
                        f"include/hal_battery.h derives from {mah} mAh"
                    )
        if (
            "pmu_axp2101.yaml" not in strip_comments(scan["text"])
            and power.get("pmu") == "AXP2101"
        ):
            err("AXP2101 battery board does not include modules/hal/pmu_axp2101.yaml")

    # 9. Flagship-only repo wiring (skipped in --external community-board mode).
    if not external:
        _check_flagship_wiring(board_id, facts, scan, err)

    if release and any(b.startswith(f"[{board_id}]") for b in blockers):
        err("L-confidence facts block a release (see release blockers)")


def render_docs(all_facts: dict[str, dict[str, Any]]) -> str:
    lines = [
        DOC_BEGIN,
        "",
        "### Pin maps (from `boards/facts/*.json`)",
        "",
        (
            "Confidence: **H** validated on hardware, **M** schematic or vendor code, "
            "**L** unconfirmed (blocks release)."
        ),
        "",
    ]
    for board_id, facts in all_facts.items():
        lines.append(
            f"#### {facts['latter_device']}: {facts['product']} (`{board_id}`)"
        )
        lines.append("")
        lines.append("| GPIO | Role | Notes | Conf. |")
        lines.append("|---:|---|---|:---:|")
        for pin in sorted(
            facts["pins"], key=lambda p: (p["gpio"], p.get("role", ""))
        ):
            notes = pin.get("note", "")
            if pin.get("polarity"):
                notes = f"{pin['polarity']}; {notes}" if notes else pin["polarity"]
            if pin.get("light_sleep_wake"):
                notes = f"{notes}; light-sleep wake" if notes else "light-sleep wake"
            if pin.get("shared_with"):
                notes = f"{notes} (shared with {pin['shared_with']})".strip()
            lines.append(
                f"| {pin['gpio']} | {pin['role']} | {notes} | {pin['confidence']} |"
            )
        lines.append("")
    lines.append(DOC_END)
    return "\n".join(lines) + "\n"


def splice_docs(existing: str, generated: str) -> str:
    if DOC_BEGIN in existing and DOC_END in existing:
        head = existing.split(DOC_BEGIN, 1)[0]
        tail = existing.split(DOC_END, 1)[1].removeprefix("\n")
        return head + generated + tail
    sep = (
        ""
        if existing.endswith("\n\n")
        else ("\n" if existing.endswith("\n") else "\n\n")
    )
    return existing + sep + generated


def _run_external_mode(args: argparse.Namespace) -> int:
    """Validate one or more community/external boards without flagship repo lock-in."""
    errors: list[str] = []
    blockers: list[str] = []
    targets: list[tuple[str, dict[str, Any], Path | None]] = []
    if args.facts is not None or args.board is not None:
        if args.facts is None or args.board is None:
            sys.exit("error: --facts and --board must be provided together with --external")
        facts_data = json.loads(args.facts.read_text(encoding="utf-8"))
        bid = str(facts_data.get("board_id") or args.facts.stem)
        targets.append((bid, facts_data, args.board))
    elif args.board_id:
        fp = FACTS_DIR / f"{args.board_id}.json"
        if not fp.is_file():
            sys.exit(f"error: facts file not found: {fp}")
        targets.append((args.board_id, json.loads(fp.read_text(encoding="utf-8")), None))
    else:
        for bid, fdata in load_facts().items():
            targets.append((bid, fdata, None))

    for bid, fdata, bpath in targets:
        check_board(
            bid, fdata, args.release, errors, blockers, external=True, board_path=bpath
        )
    for b in blockers:
        print(f"RELEASE BLOCKER {b}")
    for e in errors:
        print(f"ERROR {e}")
    if errors:
        return 1
    print(
        f"check_boards (--external): {len(targets)} board(s) OK "
        f"({len(blockers)} L-confidence facts)"
    )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "board_id",
        nargs="?",
        help="optional board id when running with --external",
    )
    ap.add_argument(
        "--release", action="store_true", help="fail on L-confidence facts"
    )
    ap.add_argument(
        "--render-docs",
        action="store_true",
        help="rewrite the generated docs/hardware.md section",
    )
    ap.add_argument(
        "--check-docs", action="store_true", help="fail if docs/hardware.md is stale"
    )
    ap.add_argument(
        "--dump", metavar="BOARD_ID", help="print the YAML scan for one board"
    )
    ap.add_argument(
        "--external",
        action="store_true",
        help="validate community/external board(s) without flagship release.yml/devices/AXP2101 lock-in",
    )
    ap.add_argument(
        "--facts",
        type=Path,
        default=None,
        help="with --external: explicit path to board facts JSON file",
    )
    ap.add_argument(
        "--board",
        type=Path,
        default=None,
        help="with --external: explicit path to board HAL YAML file",
    )
    args = ap.parse_args()

    if args.external:
        return _run_external_mode(args)

    if args.dump:
        scan = scan_board_yaml(args.dump)
        print(
            json.dumps(
                {
                    "pins": sorted(scan["pins"]),
                    "flags": scan["flags"],
                    "addresses": sorted(scan["addresses"]),
                },
                indent=2,
            )
        )
        return 0

    all_facts = load_facts()
    errors: list[str] = []
    blockers: list[str] = []

    board_ids = sorted(p.stem for p in BOARDS_DIR.glob("*.yaml"))
    errors.extend(
        f"[{board_id}] boards/{board_id}.yaml has no boards/facts/{board_id}.json"
        for board_id in board_ids
        if board_id not in all_facts
    )
    for board_id, facts in all_facts.items():
        check_board(board_id, facts, args.release, errors, blockers)

    seen: dict[int, str] = {}
    for board_id, facts in all_facts.items():
        minor_raw = facts.get("ble_minor")
        if minor_raw is None:
            continue
        minor = _safe_int(minor_raw)
        if minor is None:
            errors.append(f"[{board_id}] ble_minor {minor_raw!r} is not an integer")
            continue
        if minor in seen:
            errors.append(f"[{board_id}] ble_minor {minor} duplicates {seen[minor]}")
        seen[minor] = board_id

    generated = render_docs(all_facts)
    existing = HARDWARE_MD.read_text(encoding="utf-8")
    wanted = splice_docs(existing, generated)
    if args.render_docs:
        if wanted != existing:
            HARDWARE_MD.write_text(wanted, encoding="utf-8", newline="\n")
            print(f"Updated {HARDWARE_MD.relative_to(REPO)}")
    elif args.check_docs and wanted != existing:
        errors.append(
            "docs/hardware.md generated pin maps are stale: "
            "run python tools/check_boards.py --render-docs"
        )

    for b in blockers:
        print(f"RELEASE BLOCKER {b}")
    for e in errors:
        print(f"ERROR {e}")
    if errors:
        return 1
    print(
        f"check_boards: {len(all_facts)} boards OK "
        f"({len(blockers)} L-confidence facts pending bring-up evidence)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
