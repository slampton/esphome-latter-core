#!/usr/bin/env python3
"""Check that the firmware and protocol/latter_v3.json describe the same device <-> Home Assistant contract.

Outbound: every `homeassistant.action` calling `latter.<service>` must name a service in `services`, and
send only that service's `fields`; every protocol service must be called by the firmware.
Inbound: every `api: actions:` entry must match a `device_actions` entry, with identical variable names
and types, and every protocol device action must exist in the firmware.

Standard library only (a small indentation scanner, not a YAML parser), so it runs anywhere.
Usage: python tools/check_protocol_usage.py
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
SCAN_DIRS = ("modules", "guis", "devices", "boards")
KEY_RE = re.compile(r"^(\s*)(?:-\s+)?([A-Za-z_][A-Za-z0-9_]*):(?:\s+(.*))?$")


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _child_keys(lines: list[str], start: int, parent_indent: int) -> tuple[dict[str, str], int]:
    """Return {key: value} for the direct children of the block opened at lines[start - 1]."""
    keys: dict[str, str] = {}
    child_indent = -1
    i = start
    while i < len(lines):
        line = lines[i]
        if not line.strip() or line.lstrip().startswith("#"):
            i += 1
            continue
        ind = _indent(line)
        if ind <= parent_indent:
            break
        if child_indent < 0:
            child_indent = ind
        if ind == child_indent:
            m = KEY_RE.match(line)
            if m:
                keys[m.group(2)] = (m.group(3) or "").strip()
        i += 1
    return keys, i


def scan_file(path: Path, calls: list[tuple[str, str, set[str]]], actions: dict[str, list[tuple[str, dict[str, str]]]]) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    rel = path.relative_to(ROOT).as_posix()
    for n, line in enumerate(lines):
        stripped = line.strip()
        if stripped in ("- homeassistant.action:", "homeassistant.action:"):
            # Options sit one level deeper than the action key (after "- " when it is a list item).
            base = _indent(line) + (2 if stripped.startswith("- ") else 0)
            opts, _ = _child_keys(lines, n + 1, base)
            service = opts.get("action", "")
            if not service.startswith("latter."):
                continue
            data_keys: set[str] = set()
            for j in range(n + 1, len(lines)):
                if lines[j].strip() == "data:" and _indent(lines[j]) > base:
                    data, _ = _child_keys(lines, j + 1, _indent(lines[j]))
                    data_keys = set(data)
                    break
                if lines[j].strip() and _indent(lines[j]) <= base:
                    break
            calls.append((f"{rel}:{n + 1}", service.split(".", 1)[1], data_keys))
        m = re.match(r"^(\s*)-\s+action:\s+([a-z_][a-z0-9_]*)\s*$", line)
        if m:
            item_indent = len(m.group(1)) + 2
            opts, _ = _child_keys(lines, n + 1, item_indent - 1)
            # Device actions declare variables (or nothing); script/automation steps never use "- action:".
            variables: dict[str, str] = {}
            for j in range(n + 1, len(lines)):
                if lines[j].strip() and _indent(lines[j]) < item_indent:
                    break
                if lines[j].strip() == "variables:" and _indent(lines[j]) == item_indent:
                    variables, _ = _child_keys(lines, j + 1, item_indent)
                    break
            if "then" in opts:
                actions.setdefault(m.group(2), []).append((f"{rel}:{n + 1}", variables))


def main() -> int:
    schema = json.loads((ROOT / "protocol" / "latter_v3.json").read_text(encoding="utf-8"))
    services: dict[str, dict] = schema["services"]
    device_actions: dict[str, dict] = schema["device_actions"]

    calls: list[tuple[str, str, set[str]]] = []
    actions: dict[str, list[tuple[str, dict[str, str]]]] = {}
    for d in SCAN_DIRS:
        for path in sorted((ROOT / d).rglob("*.yaml")):
            scan_file(path, calls, actions)

    errors: list[str] = []
    called: set[str] = set()
    for where, svc, keys in calls:
        called.add(svc)
        if svc not in services:
            errors.append(f"{where}: latter.{svc} is not a service in protocol/latter_v3.json")
            continue
        extra = keys - set(services[svc].get("fields", []))
        if extra:
            errors.append(f"{where}: latter.{svc} sends {sorted(extra)}, not in the protocol's fields")
    for svc in sorted(set(services) - called):
        errors.append(f"protocol service latter.{svc} is never called by the firmware")

    for name, spec in device_actions.items():
        if name not in actions:
            errors.append(f"protocol device action {name} is not declared in the firmware's api: actions:")
            continue
        want = {k: str(v) for k, v in spec.get("variables", {}).items()}
        for where, variables in actions[name]:
            if variables != want:
                errors.append(f"{where}: device action {name} variables {variables} != protocol {want}")
    for name in sorted(set(actions) - set(device_actions)):
        errors.append(f"{actions[name][0][0]}: device action {name} is not in protocol/latter_v3.json device_actions")

    if errors:
        print("\n".join(errors))
        return 1
    print(f"protocol usage OK: {len(calls)} service calls, {sum(map(len, actions.values()))} device action declarations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
