#!/usr/bin/env python3
"""Board Facts ledger checker for esphome-latter-core.

Every supported board has a hardware facts file, boards/facts/<board_id>.json (schema:
boards/facts/schema.json), that records each routed net with its role, polarity, rail and the
confidence of the source. This tool cross-checks the facts against the board HAL YAML
(boards/<board_id>.yaml) and the device YAML that includes it, and renders the generated pin-map
section of docs/hardware.md.

Standard library only (CI parity with tools/gen_protocol.py). YAML is scanned with tolerant regular
expressions, so !include / !extend / !lambda never need to be parsed.

Usage:
  python tools/check_boards.py                # check every board (exit 1 on errors)
  python tools/check_boards.py --release      # also fail on L-confidence facts (release gate)
  python tools/check_boards.py --render-docs  # rewrite the generated section of docs/hardware.md
  python tools/check_boards.py --check-docs   # fail if docs/hardware.md is stale
  python tools/check_boards.py --dump <id>    # print what the YAML scan found for one board
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

DOC_BEGIN = "<!-- BEGIN GENERATED: tools/check_boards.py --render-docs (do not edit by hand) -->"
DOC_END = "<!-- END GENERATED: tools/check_boards.py -->"

# Deep-sleep wake capability per SoC: S3 RTC GPIO0-21, P4 LP GPIO0-15.
WAKE_CAPABLE = {"ESP32-S3": set(range(22)), "ESP32-P4": set(range(16))}
STRAPPING = {"ESP32-S3": {0, 3, 45, 46}, "ESP32-P4": {34, 35, 36, 37, 38}}
CONFIDENCE = {"H", "M", "L"}
ROLE_UNUSABLE = {"reserved", "unrouted"}
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


def strip_comments(text: str) -> str:
    """Drop YAML (#) and C++ (//) comments so header prose never counts as pin usage."""
    out: list[str] = []
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith(("#", "//")):
            continue
        # Trailing YAML comments outside quotes.
        if " #" in line and line.count('"') % 2 == 0:
            line = line.split(" #", 1)[0]
        out.append(line)
    return "\n".join(out)


def scan_board_yaml(board_id: str) -> dict[str, Any]:
    path = BOARDS_DIR / f"{board_id}.yaml"
    text = path.read_text(encoding="utf-8")
    code = strip_comments(text)
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

    # 1. Pin facts: unique GPIOs, valid confidence, sources present.
    by_gpio: dict[int, dict[str, Any]] = {}
    for pin in facts.get("pins", []):
        g = pin.get("gpio")
        if not isinstance(g, int):
            err(f"pin entry without integer gpio: {pin}")
            continue
        if (
            g in by_gpio
            and not pin.get("shared_with")
            and not by_gpio[g].get("shared_with")
        ):
            err(
                f"GPIO{g} listed twice ({by_gpio[g].get('role')} / {pin.get('role')}) without shared_with"
            )
        by_gpio.setdefault(g, pin)
        conf = pin.get("confidence")
        if conf not in CONFIDENCE:
            err(f"GPIO{g} confidence must be H/M/L, got {conf!r}")
        elif conf == "L":
            blockers.append(
                f"[{board_id}] GPIO{g} {pin.get('role')}: L confidence ({pin.get('note', 'needs bring-up evidence')})"
            )
        if not pin.get("role"):
            err(f"GPIO{g} has no role")

    # 2. Every GPIO the HAL touches is a known, usable net.
    for g in sorted(scan["pins"]):
        pin = by_gpio.get(g)
        if pin is None:
            err(f"GPIO{g} is used in boards/{board_id}.yaml but has no fact")
            continue
        if pin.get("role") in ROLE_UNUSABLE and not pin.get("shared_with"):
            err(f"GPIO{g} is used in the HAL but the facts mark it {pin.get('role')}")

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
        if key is not None and int(key) not in WAKE_CAPABLE[soc]:
            err(
                f"PWR key GPIO{key} is a deep-sleep wake source but is not wake-capable on {soc}"
            )
    for pin in facts.get("pins", []):
        if pin.get("deep_sleep_wake") and pin["gpio"] not in WAKE_CAPABLE[soc]:
            err(
                f"GPIO{pin['gpio']} ({pin.get('role')}) is marked deep_sleep_wake but is not wake-capable on {soc}"
            )

    # 5. Interrupt traits never point at an unrouted net.
    for pin in facts.get("pins", []):
        if pin.get("role") == "unrouted" and pin["gpio"] in scan["pins"]:
            err(f"GPIO{pin['gpio']} is unrouted per the facts but the HAL uses it")

    # 6. I2C devices: every address in the HAL is a known device.
    known_addrs = {d.get("address", "").lower() for d in facts.get("i2c_devices", [])}
    for addr in sorted(scan["addresses"]):
        if addr not in known_addrs:
            err(f"I2C address {addr} appears in the HAL but not in facts.i2c_devices")

    # 7. Backlight PWM floor.
    bl = facts.get("backlight") or {}
    if bl.get("min_pwm_hz"):
        hz = scan["flags"].get("LATTER_BACKLIGHT_PWM_HZ")
        if hz is None or int(hz) < int(bl["min_pwm_hz"]):
            err(
                f"LATTER_BACKLIGHT_PWM_HZ={hz} is below the backlight min_pwm_hz {bl['min_pwm_hz']}"
            )

    # 8. Battery boards carry a charge profile and a PMU; rail facts carry a confidence too.
    power = facts.get("power", {})
    for rail in power.get("rails", []):
        if rail.get("confidence") not in CONFIDENCE:
            err(f"rail {rail.get('rail')} confidence must be H/M/L")
        elif rail["confidence"] == "L":
            blockers.append(
                f"[{board_id}] rail {rail.get('rail')}: L confidence ({rail.get('note', 'needs bring-up evidence')})"
            )
    if str(facts.get("latter_flags", {}).get("LATTER_HAS_BATTERY")) == "1":
        if not power.get("battery_mah") or not power.get("pmu"):
            err("LATTER_HAS_BATTERY=1 needs power.pmu and power.battery_mah")
        if "pmu_axp2101.yaml" not in scan["code"] and power.get("pmu") == "AXP2101":
            err("AXP2101 battery board does not include modules/pmu_axp2101.yaml")

    # 9. Device file, unique BLE minor and release wiring.
    dev = device_for_board(board_id)
    if dev is None:
        err("no devices/*.yaml includes this board")
    else:
        dev_text = dev.read_text(encoding="utf-8")
        m = MINOR_RE.search(dev_text)
        if facts.get("ble_minor") is not None:
            if not m or int(m.group(1)) != int(facts["ble_minor"]):
                err(
                    f"{dev.name} esp32_ble_beacon minor does not equal facts ble_minor {facts['ble_minor']}"
                )
            if scan["flags"].get("LATTER_BLE_MINOR") not in (
                None,
                str(facts["ble_minor"]),
            ):
                err(
                    f"LATTER_BLE_MINOR={scan['flags'].get('LATTER_BLE_MINOR')} does not equal facts ble_minor"
                )
        if str(
            facts.get("latter_flags", {}).get("LATTER_IS_STATIONARY")
        ) == "0" and "reboot_timeout: 0s" not in (dev_text + scan["text"]):
            # Mobile devices must never reboot when Home Assistant is unreachable.
            core_sys = (REPO / "modules" / "features" / "core_system.yaml").read_text(
                encoding="utf-8"
            )
            if "${api_reboot_timeout}" not in core_sys:
                err("mobile board without reboot_timeout: 0s")
    release_yml = (REPO / ".github" / "workflows" / "release.yml").read_text(
        encoding="utf-8"
    )
    if dev is not None and dev.stem not in release_yml:
        err(f"{dev.stem} has no build job in .github/workflows/release.yml")

    if release and any(b.startswith(f"[{board_id}]") for b in blockers):
        err("L-confidence facts block a release (see release blockers)")


def render_docs(all_facts: dict[str, dict[str, Any]]) -> str:
    lines = [DOC_BEGIN, "", "### Pin maps (from `boards/facts/*.json`)", ""]
    lines.append(
        "Confidence: **H** validated on hardware, **M** schematic or vendor code, **L** unconfirmed (blocks release)."
    )
    lines.append("")
    for board_id, facts in all_facts.items():
        lines.append(
            f"#### {facts['latter_device']}: {facts['product']} (`{board_id}`)"
        )
        lines.append("")
        lines.append("| GPIO | Role | Notes | Conf. |")
        lines.append("|---:|---|---|:---:|")
        for pin in sorted(facts["pins"], key=lambda p: (p["gpio"], p.get("role", ""))):
            notes = pin.get("note", "")
            if pin.get("polarity"):
                notes = f"{pin['polarity']}; {notes}" if notes else pin["polarity"]
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
        tail = existing.split(DOC_END, 1)[1]
        tail = tail.removeprefix("\n")
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
    ap.add_argument("--release", action="store_true", help="fail on L-confidence facts")
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
    for board_id in board_ids:
        if board_id not in all_facts:
            errors.append(
                f"[{board_id}] boards/{board_id}.yaml has no boards/facts/{board_id}.json"
            )
    for board_id, facts in all_facts.items():
        check_board(board_id, facts, args.release, errors, blockers)

    # BLE minors are unique across boards.
    seen: dict[int, str] = {}
    for board_id, facts in all_facts.items():
        minor = facts.get("ble_minor")
        if minor is None:
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
            "docs/hardware.md generated pin maps are stale: run python tools/check_boards.py --render-docs"
        )

    for b in blockers:
        print(f"RELEASE BLOCKER {b}")
    for e in errors:
        print(f"ERROR {e}")
    if errors:
        return 1
    print(
        f"check_boards: {len(all_facts)} boards OK ({len(blockers)} L-confidence facts pending bring-up evidence)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
