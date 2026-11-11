#!/usr/bin/env python3
"""Generate include/latter_protocol.h and DEVICE_CONTRACT.md from protocol/latter_v3.json."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


def build_cpp_header(schema: dict[str, Any]) -> str:
    """Render include/latter_protocol.h from protocol/latter_v3.json."""
    ver = int(schema["contract_version"])
    header_fields = schema["header"]
    domains = schema["domains"]

    lines: list[str] = [
        "#pragma once",
        "// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace latter_ui {",
        "",
        f"static constexpr int kProtocolContractVersion = {ver};",
        f"static constexpr size_t kProtocolMinRequiredFields = {len(header_fields)};",
        "static constexpr char kProtocolFieldSep = '\\x1f';",
        "static constexpr char kProtocolRecordSep = '\\x1e';",
        "",
    ]
    # Shared defaults (one definition for both repos): kDefault<Name> string constants.
    for key, value in (schema.get("defaults") or {}).items():
        camel = "".join(part.capitalize() for part in key.split("_"))
        lines.append(f'static constexpr const char *kDefault{camel} = "{value}";  // defaults.{key}')
    if schema.get("defaults"):
        lines.append("")
    lines.append("enum class ProtocolHeaderField : size_t {")
    for hf in header_fields:
        lines.append(f"  {hf['cpp_enum']} = {hf['index']},  // {hf['name']} ({hf['type']})")
    lines.append(f"  COUNT = {len(header_fields)}")
    lines.append("};")
    lines.append("")

    for dom_key, dom in domains.items():
        enum_name = dom["cpp_enum_name"]
        fields = dom["fields"]
        max_idx = max(int(f["index"]) for f in fields)
        seen_enums: set[str] = {"COUNT"}
        lines.append(f"// Domain: {dom_key} (sensor.<device>_{dom['entity_suffix']})")
        lines.append(f"enum class {enum_name} : size_t {{")
        for hf in header_fields:
            ident = str(hf["cpp_enum"])
            if ident in seen_enums:
                raise ValueError(f"Duplicate enum identifier '{ident}' in {enum_name}")
            seen_enums.add(ident)
            lines.append(f"  {ident} = {hf['index']},")
        for f in fields:
            ident = str(f["cpp_enum"])
            if ident in seen_enums:
                raise ValueError(f"Duplicate enum identifier '{ident}' in {enum_name}")
            seen_enums.add(ident)
            lines.append(
                f"  {ident} = {f['index']},  // {f['name']} ({f['type']})"
            )
        lines.append(f"  COUNT = {max_idx + 1}")
        lines.append("};")
        lines.append("")

    lines.append("}  // namespace latter_ui")
    lines.append("")
    return "\n".join(lines)


def build_device_contract_md(schema: dict[str, Any]) -> str:
    """Render DEVICE_CONTRACT.md from protocol/latter_v3.json."""
    ver = int(schema["contract_version"])
    header_fields = schema["header"]
    domains = schema["domains"]
    device_actions = schema["device_actions"]
    services = schema["services"]

    lines: list[str] = [
        "# Latter Protocol v3 Device Contract",
        "",
        "> **Auto-Generated Reference**: Generated from [`protocol/latter_v3.json`](protocol/latter_v3.json) by [`tools/gen_protocol.py`](tools/gen_protocol.py). Do not edit by hand.",
        "",
        "## 1. Overview & Transport Framing",
        "",
        f"- **Contract Version**: `{ver}`",
        "- **Field Separator**: `0x1F` (`\\x1f`, ASCII Unit Separator `␟`)",
        "- **Record Separator**: `0x1E` (`\\x1e`, ASCII Record Separator `␞`) with pipe (`|`) subfields inside record lists",
        f"- **Absent Values**: {schema['absent_value']}",
        "- **Inbound Channel (`HA -> Device`)**: Home Assistant pushes each domain's `payload` (`v3␟<epoch>␟<rev>␟...`) directly via the bound ESPHome Native API action `esphome.<node>_latter_push_v3` (`domain`, `payload`) resolved by `esphome_device_id`.",
        "- **Outbound Channel (`Device -> HA`)**: Devices emit bound RPC frames (`<boot_hex>:<seq>:<part>/<total>|<json>`) on their diagnostic `text_sensor` (`System: RPC Outbox`), eliminating `allow_service_calls` and `target_device` spoofing.",
        "- **Revision Guard & Version Tolerance**:",
        "  - `epoch` (`u32`, non-zero): Generated randomly when the Home Assistant coordinator starts. If `in_epoch != cur_epoch`, the device accepts the frame unconditionally and updates `cur_epoch` and `cur_rev`.",
        "  - `rev` (`u32`, `>= 1`): Monotonic per-domain revision counter bumped only when that domain's body fields change. Frames with `rev == 0` are rejected; within the same `epoch`, frames with `in_rev <= cur_rev` are ignored.",
        "  - Parsers accept any frame with `n >= kProtocolMinRequiredFields` (`3`), defaulting missing trailing fields to `\"\"` and logging a warning on field-count mismatch.",
        "  - On ESPHome API reconnect (`on_client_connected` from Home Assistant), the device calls `latter_ui::reset_all_v3_rev_guards()` and sends `device_hello`.",
        "",
        "### Header Fields (All Domains)",
        "",
        "| Index | Field | C++ Enum | Type | Description |",
        "| :---: | :--- | :--- | :---: | :--- |",
    ]
    for hf in header_fields:
        lines.append(
            f"| `{hf['index']}` | `{hf['name']}` | `{hf['cpp_enum']}` | `{hf['type']}` | {hf['description']} |"
        )
    lines.append("")
    defaults = schema.get("defaults") or {}
    if defaults:
        lines.append("### Shared Defaults")
        lines.append("")
        lines.append("Used by both the firmware (`latter_protocol.h`) and Home Assistant until a value is set.")
        lines.append("")
        lines.append("| Name | Value |")
        lines.append("| :--- | :--- |")
        for key, value in defaults.items():
            lines.append(f"| `{key}` | `{value}` |")
        lines.append("")
    lines.append("## 2. Domain Payloads")
    lines.append("")

    for dom_key, dom in domains.items():
        enum_name = dom["cpp_enum_name"]
        fields = dom["fields"]
        total_count = max(int(f["index"]) for f in fields) + 1
        lines.append(f"### `sensor.<device>_{dom['entity_suffix']}` (`{enum_name}`, `{total_count}` total fields)")
        lines.append("")
        lines.append(f"{dom['description']}.")
        lines.append("")
        lines.append("| Index | Field | C++ Enum | Type | Description |")
        lines.append("| :---: | :--- | :--- | :---: | :--- |")
        for f in fields:
            lines.append(
                f"| `{f['index']}` | `{f['name']}` | `{enum_name}::{f['cpp_enum']}` | `{f['type']}` | {f['description']} |"
            )
        lines.append("")

    lines.append("## 3. ESPHome Device Actions (`esphome.<node>_<action>`)")
    lines.append("")
    lines.append("| Action | Variables | Description |")
    lines.append("| :--- | :--- | :--- |")
    for act_name, act_info in device_actions.items():
        vars_map = act_info.get("variables") or {}
        if vars_map:
            vars_str = ", ".join(f"`{k}: {v}`" for k, v in vars_map.items())
        else:
            vars_str = "*(none)*"
        lines.append(f"| `{act_name}` | {vars_str} | {act_info['description']} |")
    lines.append("")

    lines.append("## 4. Outbound Home Assistant Services (`latter.<service>`)")
    lines.append("")
    lines.append("| Service | Canonical Fields | Description |")
    lines.append("| :--- | :--- | :--- |")
    for svc_name, svc_info in services.items():
        f_str = ", ".join(f"`{f}`" for f in svc_info["fields"])
        lines.append(f"| `latter.{svc_name}` | {f_str} | {svc_info['description']} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify generated files match protocol/latter_v3.json without modifying disk.",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    schema_path = repo_root / "protocol" / "latter_v3.json"
    header_path = repo_root / "include" / "latter_protocol.h"
    doc_path = repo_root / "DEVICE_CONTRACT.md"

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    expected_header = build_cpp_header(schema)
    expected_doc = build_device_contract_md(schema)

    if args.check:
        ok = True
        if not header_path.is_file() or header_path.read_text(encoding="utf-8") != expected_header:
            print(f"[PROTOCOL DRIFT] {header_path} is out of date with {schema_path}", file=sys.stderr)
            ok = False
        if not doc_path.is_file() or doc_path.read_text(encoding="utf-8") != expected_doc:
            print(f"[PROTOCOL DRIFT] {doc_path} is out of date with {schema_path}", file=sys.stderr)
            ok = False
        return 0 if ok else 1

    header_path.write_text(expected_header, encoding="utf-8", newline="\n")
    doc_path.write_text(expected_doc, encoding="utf-8", newline="\n")
    print(f"Generated {header_path.name} and {doc_path.name} from {schema_path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
