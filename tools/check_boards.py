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
  python tools/check_boards.py                # check every board
  python tools/check_boards.py --release      # fail on L-confidence facts
  python tools/check_boards.py --render-docs  # rewrite docs/hardware.md pin map
  python tools/check_boards.py --check-docs   # fail if docs/hardware.md stale
  python tools/check_boards.py --dump <id>    # print YAML scan for one board
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

# Deep-sleep wake capability per SoC: S3 RTC GPIO0-21, P4 LP GPIO0-15.
WAKE_CAPABLE = {"ESP32-S3": set(range(22)), "ESP32-P4": set(range(16))}
STRAPPING = {"ESP32-S3": {0, 3, 45, 46}, "ESP32-P4": {34, 35, 36, 37, 38}}
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


def scan_board_yaml(board_id: str) -> dict[str, Any]:
    path = BOARDS_DIR / f"{board_id}.yaml"
    text = path.read_text(encoding="utf-8")
    expanded = _expand_board_hal_includes(text)
    code = strip_comments(expanded)
    pins = {int(m) for m in PIN_RE.findall(code)} | {
        int(m) for m in FLAG_PIN_RE.findall(code)
    }
    flags: dict[str, str] = {}
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
    subs: dict[str, str] = {}
    for src in (base_text, board_text, dev_text):
        subs.update(_parse_top_level_substitutions(src))

    raw_val: str | None = None
    for src in (base_text, board_text, dev_text):
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


def check_board(
    board_id: str,
    facts: dict[str, Any],
    release: bool,
    errors: list[str],
    blockers: list[str],
) -> None:
    def err(msg: str) -> None:
        errors.append(f"[{board_id}] {msg}")

    for key in REQUIRED_TOP:
        if key not in facts:
            err(f"facts missing required section '{key}'")
    if facts.get("board_id") != board_id:
        err(f"facts board_id '{facts.get('board_id')}' does not match file name")
    if not (BOARDS_DIR / f"{board_id}.yaml").exists():
        err("no boards/<id>.yaml for this facts file")
        return

    soc = facts.get("silicon", {}).get("soc", "")
    if soc not in WAKE_CAPABLE:
        err(f"unknown silicon.soc '{soc}'")
        return

    scan = scan_board_yaml(board_id)

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

    # 6b. First-party Touch HAL contract.
    if str(facts.get("latter_flags", {}).get("LATTER_HAS_TOUCH")) == "1":
        t_facts = facts.get("touch")
        if not isinstance(t_facts, dict):
            err("LATTER_HAS_TOUCH=1 requires a 'touch' object in boards/facts")
        else:
            proto = str(t_facts.get("protocol", "")).lower()
            t_addr = str(t_facts.get("address", "")).lower()
            delivery = str(t_facts.get("delivery", ""))
            if proto not in {"ft6336", "ft3168", "gt911", "cst9217", "cst816s"}:
                err(
                    f"touch.protocol '{proto}' is not a registered "
                    "Latter Touch HAL protocol"
                )
            if t_addr not in known_addrs:
                err(f"touch.address '{t_addr}' is not listed in facts.i2c_devices")
            if not re.search(r"(?m)^\s*-\s*platform:\s*latter_core\b", scan["code"]):
                err("touchscreen block must use first-party 'platform: latter_core'")
            if not re.search(
                rf"(?m)^\s*protocol:\s*{re.escape(proto)}\b", scan["code"]
            ):
                err(f"touchscreen block must declare 'protocol: {proto}'")
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

    # 7. Backlight PWM floor.
    bl = facts.get("backlight") or {}
    min_pwm = _safe_int(bl.get("min_pwm_hz"))
    if min_pwm is not None and min_pwm > 0:
        hz_raw = scan["flags"].get("LATTER_BACKLIGHT_PWM_HZ")
        hz = _safe_int(hz_raw)
        if hz is None or hz < min_pwm:
            err(
                f"LATTER_BACKLIGHT_PWM_HZ={hz_raw} is below the "
                f"backlight min_pwm_hz {min_pwm}"
            )

    # 8. Battery boards carry a charge profile and a PMU.
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
        if mah is None or mah <= 0 or not power.get("pmu"):
            err("LATTER_HAS_BATTERY=1 needs power.pmu and integer power.battery_mah")
        else:
            flag_mah = _safe_int(
                facts.get("latter_flags", {}).get("LATTER_BATTERY_MAH")
            )
            if flag_mah is None or flag_mah != mah:
                err(
                    f"latter_flags.LATTER_BATTERY_MAH must equal "
                    f"power.battery_mah ({mah})"
                )
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

    # 9. Device file, unique BLE minor and release wiring.
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
        if str(facts.get("latter_flags", {}).get("LATTER_IS_STATIONARY")) == "0":
            base_mod = (REPO / "modules" / "base.yaml").read_text(encoding="utf-8")
            eff_reboot = _resolve_api_reboot_timeout(base_mod, scan["text"], dev_text)
            if eff_reboot != "0s":
                err(
                    "mobile board api.reboot_timeout must resolve to '0s', "
                    f"got {eff_reboot!r}"
                )
    release_yml = (REPO / ".github" / "workflows" / "release.yml").read_text(
        encoding="utf-8"
    )
    if dev is not None and dev.stem not in release_yml:
        err(f"{dev.stem} has no build job in .github/workflows/release.yml")

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


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
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
    args = ap.parse_args()

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
