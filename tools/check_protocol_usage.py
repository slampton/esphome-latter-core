#!/usr/bin/env python3
"""Check that the firmware and protocol/latter_v3.json describe the same device <-> Home Assistant contract.

Outbound: every `latter_rpc::emit(..., "<rpc_method>", { ... })` call must name a method in `rpc_methods`, and
send only that method's `fields`; every protocol RPC method must be called by the firmware; every declared field
must be emitted by at least one call; and legacy `homeassistant.action` calls are forbidden across firmware YAML.
Inbound: every `api: actions:` entry must match a `device_actions` entry with `latter_` prefix, with identical variable names
and types, and every protocol device action must exist in the firmware.

Standard library only (zero external dependencies), so it runs anywhere.
Usage: python tools/check_protocol_usage.py
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
YAML_SCAN_DIRS = ("modules", "guis", "devices", "boards", "examples")
CPP_SCAN_DIRS = ("include", "components")
KEY_RE = re.compile(r"^(\s*)(?:-\s+)?([A-Za-z_][A-Za-z0-9_]*):(?:\s+(.*))?$")
EMIT_HEAD_RE = re.compile(r'(?:::)?latter_rpc::emit\s*\([^,\r\n]+,\s*"([a-z_][a-z0-9_]*)"')
PAIR_KEY_RE = re.compile(r'\{\s*"([a-z_][a-z0-9_]*)"\s*,')


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


def _extract_emit_calls(text: str, rel: str, calls: list[tuple[str, str, set[str]]]) -> None:
    # Blank single-line comments while preserving exact line numbers and indices
    clean_text = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
    for m in EMIT_HEAD_RE.finditer(clean_text):
        svc = m.group(1)
        line_no = clean_text.count("\n", 0, m.start()) + 1
        open_paren = clean_text.find("(", m.start())
        depth = 0
        in_str = False
        esc = False
        end_pos = len(clean_text)
        for idx in range(open_paren, len(clean_text)):
            ch = clean_text[idx]
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
                continue
            if ch == '"':
                in_str = True
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    end_pos = idx + 1
                    break
        call_slice = clean_text[m.end() : end_pos]
        data_keys = set(PAIR_KEY_RE.findall(call_slice))
        calls.append((f"{rel}:{line_no}", svc, data_keys))


def scan_yaml_file(
    path: Path,
    calls: list[tuple[str, str, set[str]]],
    actions: dict[str, list[tuple[str, dict[str, str]]]],
    errors: list[str],
) -> None:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    rel = path.relative_to(ROOT).as_posix()
    _extract_emit_calls(text, rel, calls)
    for n, line in enumerate(lines):
        stripped = line.strip()
        if stripped in ("- homeassistant.action:", "homeassistant.action:"):
            errors.append(
                f"{rel}:{n + 1}: forbidden homeassistant.action block (use latter_rpc::emit on latter_rpc_outbox)"
            )
        m = re.match(r"^(\s*)-\s+action:\s+([a-z_][a-z0-9_]*)\s*$", line)
        if m:
            item_indent = len(m.group(1)) + 2
            opts, _ = _child_keys(lines, n + 1, item_indent - 1)
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
    rpc_methods: dict[str, dict] = schema["rpc_methods"]
    device_actions: dict[str, dict] = schema["device_actions"]

    calls: list[tuple[str, str, set[str]]] = []
    actions: dict[str, list[tuple[str, dict[str, str]]]] = {}
    errors: list[str] = []

    for d in YAML_SCAN_DIRS:
        base = ROOT / d
        if base.is_dir():
            for path in sorted(base.rglob("*.yaml")):
                if any(part.startswith(".") for part in path.parts):
                    continue
                scan_yaml_file(path, calls, actions, errors)

    for d in CPP_SCAN_DIRS:
        base = ROOT / d
        if base.is_dir():
            for path in sorted(base.rglob("*")):
                if any(part.startswith(".") for part in path.parts):
                    continue
                if path.is_file() and path.suffix in {".h", ".hpp", ".cpp"}:
                    _extract_emit_calls(path.read_text(encoding="utf-8"), path.relative_to(ROOT).as_posix(), calls)

    called: set[str] = set()
    emitted_fields: dict[str, set[str]] = {}
    for where, svc, keys in calls:
        called.add(svc)
        emitted_fields.setdefault(svc, set()).update(keys)
        if svc not in rpc_methods:
            errors.append(f"{where}: RPC method latter_rpc::{svc} is not in protocol/latter_v3.json rpc_methods")
            continue
        extra = keys - set(rpc_methods[svc].get("fields", []))
        if extra:
            errors.append(f"{where}: latter_rpc::{svc} sends {sorted(extra)}, not in protocol's rpc_methods fields")

    for svc in sorted(set(rpc_methods) - called):
        errors.append(f"protocol RPC method latter_rpc::{svc} is never called by the firmware")

    for svc in sorted(called & set(rpc_methods)):
        declared = set(rpc_methods[svc].get("fields", []))
        missing = declared - emitted_fields.get(svc, set())
        if missing:
            errors.append(f"RPC method latter_rpc::{svc} declares fields {sorted(missing)} which are never emitted by firmware")

    for name, spec in device_actions.items():
        if not name.startswith("latter_"):
            errors.append(f"protocol device action {name} must have 'latter_' prefix")
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
    print(f"protocol usage OK: {len(calls)} RPC emit calls across {len(called)} methods, {sum(map(len, actions.values()))} device action declarations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
