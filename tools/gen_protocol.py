#!/usr/bin/env python3
"""Generate include/latter_protocol.h, DEVICE_CONTRACT.md, and sibling ha-latter schemas from protocol/latter_v3.json."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any

_CPP_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_SETTINGS_ROOT_ROUTE_ID = 250


def _assert_cpp_ident(ident: str, context: str) -> str:
    if not _CPP_IDENT_RE.fullmatch(ident):
        raise ValueError(f"Invalid C++ identifier {ident!r} in {context}")
    return ident


def _cpp_str(val: Any) -> str:
    """Escape an arbitrary Python value as a valid C++ UTF-8 string literal."""
    s = str(val)
    out: list[str] = ['"']
    for ch in s:
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif code < 0x20 or code == 0x7F:
            out.append(f"\\{code:03o}")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def validate_schema(schema: dict[str, Any]) -> None:
    """Validate structural, index-contiguity, RouteId, and spec invariants on protocol/latter_v3.json."""
    header_fields = schema.get("header") or []
    if len(header_fields) != 3:
        raise ValueError(f"Expected 3 header fields, found {len(header_fields)}")
    header_indices = [int(hf["index"]) for hf in header_fields]
    if header_indices != list(range(len(header_fields))):
        raise ValueError(f"Header field indices must be contiguous 0..{len(header_fields) - 1}, got {header_indices}")
    for hf in header_fields:
        _assert_cpp_ident(str(hf["cpp_enum"]), "header.cpp_enum")

    shortcut_routes = (schema.get("catalogs") or {}).get("shortcut_routes") or []
    if len(shortcut_routes) >= _SETTINGS_ROOT_ROUTE_ID:
        raise ValueError(
            f"RouteId overflow: shortcut_routes has {len(shortcut_routes)} entries "
            f"(must be < {_SETTINGS_ROOT_ROUTE_ID}, reserved for SETTINGS_ROOT)"
        )
    seen_route_idents: set[str] = {"NONE", "SETTINGS_ROOT"}
    for idx, r in enumerate(shortcut_routes, start=1):
        if idx >= _SETTINGS_ROOT_ROUTE_ID:
            raise ValueError(f"RouteId {idx} collides with SETTINGS_ROOT = {_SETTINGS_ROOT_ROUTE_ID}")
        ident = _assert_cpp_ident(
            str(r["token"]).upper().replace(":", "_").replace("-", "_"),
            f"shortcut_routes[{idx}].token",
        )
        if ident in seen_route_idents:
            raise ValueError(f"Duplicate RouteId enum identifier {ident!r}")
        seen_route_idents.add(ident)

    domains = schema.get("domains") or {}
    if not domains:
        raise ValueError("Schema domains map must not be empty")
    start_idx = len(header_fields)
    for dom_key, dom in domains.items():
        enum_name = _assert_cpp_ident(str(dom["cpp_enum_name"]), f"domains.{dom_key}.cpp_enum_name")
        fields = dom.get("fields") or []
        if not fields:
            raise ValueError(f"Domain {dom_key!r} ({enum_name}) must declare at least one body field")
        indices = [int(f["index"]) for f in fields]
        expected_indices = list(range(start_idx, start_idx + len(fields)))
        if indices != expected_indices:
            raise ValueError(
                f"Domain {dom_key!r} ({enum_name}) field indices must be contiguous "
                f"{start_idx}..{start_idx + len(fields) - 1}, got {indices}"
            )
        for f in fields:
            _assert_cpp_ident(str(f["cpp_enum"]), f"domains.{dom_key}.{f['name']}.cpp_enum")

    for spec in schema.get("device_pref_specs") or []:
        if "esphome_section" in spec:
            raise ValueError(
                f"device_pref_specs[{spec.get('key')!r}] still contains retired 'esphome_section' field"
            )


def build_cpp_header(schema: dict[str, Any]) -> str:
    """Render include/latter_protocol.h from protocol/latter_v3.json."""
    validate_schema(schema)
    ver = int(schema["contract_version"])
    header_fields = schema["header"]
    domains = schema["domains"]
    defaults = schema.get("defaults") or {}

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
    for key, value in defaults.items():
        camel = "".join(part.capitalize() for part in key.split("_"))
        const_ident = _assert_cpp_ident(f"kDefault{camel}", f"defaults.{key}")
        lines.append(f"static constexpr const char *{const_ident} = {_cpp_str(value)};  // defaults.{key}")
    if defaults:
        lines.append("")

    shortcut_routes = (schema.get("catalogs") or {}).get("shortcut_routes") or []
    if shortcut_routes:
        lines.append("enum class ShortcutRouteCap : uint8_t {")
        lines.append("  NONE = 0,")
        lines.append("  ROUND = 1,")
        lines.append("  IMU = 2,")
        lines.append("};")
        lines.append("")
        lines.append("enum class RouteId : uint8_t {")
        lines.append("  NONE = 0,")
        route_idents: list[str] = []
        for idx, r in enumerate(shortcut_routes, start=1):
            if idx >= _SETTINGS_ROOT_ROUTE_ID:
                raise ValueError(f"RouteId {idx} must be < {_SETTINGS_ROOT_ROUTE_ID}")
            ident = _assert_cpp_ident(
                str(r["token"]).upper().replace(":", "_").replace("-", "_"),
                f"shortcut_routes[{idx}]",
            )
            route_idents.append(ident)
            lines.append(f"  {ident} = {idx},")
        lines.append(f"  SETTINGS_ROOT = {_SETTINGS_ROOT_ROUTE_ID},")
        lines.append("};")
        lines.append("")
        lines.append("struct ShortcutRouteSpec {")
        lines.append("  RouteId id;")
        lines.append("  const char *token;")
        lines.append("  const char *label;")
        lines.append("  bool dock_only;")
        lines.append("  ShortcutRouteCap cap;")
        lines.append("};")
        lines.append("")
        lines.append("inline constexpr ShortcutRouteSpec kShortcutRoutes[] = {")
        for ident, r in zip(route_idents, shortcut_routes, strict=True):
            dock_only = "true" if r.get("dock_only") else "false"
            cap_raw = str(r.get("capability") or "").lower()
            cap_enum = "ROUND" if cap_raw == "round" else ("IMU" if cap_raw == "imu" else "NONE")
            lines.append(
                f"    {{RouteId::{ident}, {_cpp_str(r['token'])}, {_cpp_str(r['label'])}, "
                f"{dock_only}, ShortcutRouteCap::{cap_enum}}},"
            )
        lines.append("};")
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
        lines.append(f"// Domain: {dom_key}")
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
            lines.append(f"  {ident} = {f['index']},  // {f['name']} ({f['type']})")
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
    rpc_methods = schema.get("rpc_methods", {})
    ha_services = schema.get("ha_services", {})

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
        "- **Outbound Channel (`Device -> HA`)**: Devices emit bound RPC frames (`<boot_hex>:<seq>:<part>/<total>|<json>`) on their hidden transport `text_sensor` (`latter_rpc_outbox`), eliminating `allow_service_calls` and `target_device` spoofing.",
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
        lines.append(f"### `{dom_key}` (`{enum_name}`, `{total_count}` total fields)")
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

    lines.append("## 4. Firmware RPC Outbox Methods (`latter_rpc_outbox`)")
    lines.append("")
    lines.append("| Method | Canonical Outbox Fields | Description |")
    lines.append("| :--- | :--- | :--- |")
    for rpc_name, rpc_info in rpc_methods.items():
        f_str = ", ".join(f"`{f}`" for f in rpc_info["fields"])
        lines.append(f"| `{rpc_name}` | {f_str} | {rpc_info['description']} |")
    lines.append("")

    lines.append("## 5. Public Home Assistant Services (`latter.<service>`)")
    lines.append("")
    lines.append("| Service | Canonical Fields | Description |")
    lines.append("| :--- | :--- | :--- |")
    for svc_name, svc_info in ha_services.items():
        f_str = ", ".join(f"`{f}`" for f in svc_info["fields"])
        lines.append(f"| `latter.{svc_name}` | {f_str} | {svc_info['description']} |")
    lines.append("")

    studio_contract = schema.get("studio_contract") or {}
    if studio_contract:
        lines.append("## 6. Latter Studio Control-Plane Contract")
        lines.append("")
        lines.append(f"- **Studio Wire Revision (`studio_rev`)**: `{studio_contract['studio_rev']}`")
        actions_str = ", ".join(f"`{a}`" for a in studio_contract.get("allowed_device_actions", []))
        lines.append(f"- **Allowed Device Actions**: {actions_str}")
        sys_cmds_str = ", ".join(f"`{c}`" for c in studio_contract.get("system_commands", []))
        lines.append(f"- **System WebSocket Commands (`latter/system/command`)**: {sys_cmds_str}")
        dev_cmds_str = ", ".join(f"`{c}`" for c in studio_contract.get("device_commands", []))
        lines.append(f"- **Device WebSocket Commands (`latter/device/command`)**: {dev_cmds_str}")
        lines.append(
            f"- **Canonical World Clock Zones**: `{len(schema.get('world_clock_zones', []))}` standard `UTC-12:00`..`UTC+14:00` zones"
        )
        lines.append(
            f"- **Declarative `device_prefs` Specs**: `{len(schema.get('device_pref_specs', []))}` typed preference descriptors"
        )
        lines.append("")
    return "\n".join(lines)


def _ts_json(val: Any) -> str:
    return json.dumps(val, ensure_ascii=False)


def build_typescript_schema(schema: dict[str, Any]) -> str:
    """Render ha-latter/frontend_src/src/protocol-schema.ts from protocol/latter_v3.json."""
    catalogs = schema.get("catalogs", {})
    defaults = schema.get("defaults", {})
    studio_contract = schema.get("studio_contract") or {}
    world_clock_zones = schema.get("world_clock_zones") or []
    device_pref_specs = schema.get("device_pref_specs") or []

    lines: list[str] = [
        "// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.",
        "",
        f"export const PROTOCOL_CONTRACT_VERSION = {schema['contract_version']};",
    ]

    if "studio_rev" in studio_contract:
        lines.append(f"export const STUDIO_REV = {int(studio_contract['studio_rev'])};")
    lines.append("")

    allowed_actions = studio_contract.get("allowed_device_actions") or []
    if allowed_actions:
        lines.append("export const ALLOWED_DEVICE_ACTIONS: ReadonlyArray<string> = [")
        for act in allowed_actions:
            lines.append(f"  {_ts_json(str(act))},")
        lines.append("];")
        lines.append("")

    sys_cmds = studio_contract.get("system_commands") or []
    if sys_cmds:
        lines.append("export const SYSTEM_WS_COMMANDS: ReadonlySet<string> = new Set([")
        for cmd in sys_cmds:
            lines.append(f"  {_ts_json(str(cmd))},")
        lines.append("]);")
        lines.append("")

    dev_cmds = studio_contract.get("device_commands") or []
    if dev_cmds:
        lines.append("export const DEVICE_WS_COMMANDS: ReadonlySet<string> = new Set([")
        for cmd in dev_cmds:
            lines.append(f"  {_ts_json(str(cmd))},")
        lines.append("]);")
        lines.append("")

    lines.extend(
        [
            "export interface CatalogItem {",
            "  token: string;",
            "  label: string;",
            "  group?: string;",
            "  dockOnly?: boolean;",
            "  capability?: string;",
            "}",
            "",
        ]
    )

    for cat_name, cat_items in catalogs.items():
        const_name = f"{cat_name.upper()}_CATALOG"
        lines.append(f"export const {const_name}: ReadonlyArray<CatalogItem> = [")
        for item in cat_items:
            extras: list[str] = []
            if item.get("group"):
                extras.append(f"group: {_ts_json(item['group'])}")
            if item.get("dock_only"):
                extras.append("dockOnly: true")
            if item.get("capability"):
                extras.append(f"capability: {_ts_json(item['capability'])}")
            extra_str = (", " + ", ".join(extras)) if extras else ""
            lines.append(f"  {{token: {_ts_json(item['token'])}, label: {_ts_json(item['label'])}{extra_str}}},")
        lines.append("];")
        lines.append("")

    if world_clock_zones:
        lines.extend(
            [
                "export interface WorldClockZoneItem {",
                "  iana: string;",
                "  city: string;",
                "  label: string;",
                "  std_off: number;",
                "  dst_off: number;",
                "  std_abbr: string;",
                "  dst_abbr: string;",
                "  rule: string;",
                "}",
                "",
                "export const WORLD_CLOCK_ZONES_CATALOG: ReadonlyArray<WorldClockZoneItem> = [",
            ]
        )
        for z in world_clock_zones:
            lines.append(
                f"  {{iana: {_ts_json(z['iana'])}, city: {_ts_json(z['city'])}, label: {_ts_json(z['label'])}, "
                f"std_off: {int(z['std_off'])}, dst_off: {int(z['dst_off'])}, "
                f"std_abbr: {_ts_json(z['std_abbr'])}, dst_abbr: {_ts_json(z['dst_abbr'])}, rule: {_ts_json(z['rule'])} }},"
            )
        lines.append("];")
        lines.append("")

    if device_pref_specs:
        lines.extend(
            [
                "export interface DevicePrefSpecItem {",
                "  key: string;",
                "  label: string;",
                '  domain: "select" | "switch" | "number" | "text";',
                "  default: boolean | number | string;",
                "  section: string;",
                '  group_tier: "behavior" | "display" | "interface" | null;',
                "  options: ReadonlyArray<string>;",
                "  min: number | null;",
                "  max: number | null;",
                "  step: number | null;",
                "  unit: string;",
                "  capability: string;",
                "}",
                "",
                "export const DEVICE_PREF_SPECS_CATALOG: ReadonlyArray<DevicePrefSpecItem> = [",
            ]
        )
        for p in device_pref_specs:
            opts_ts = "[" + ", ".join(_ts_json(o) for o in (p.get("options") or [])) + "]"
            lines.append(
                f"  {{key: {_ts_json(p['key'])}, label: {_ts_json(p['label'])}, domain: {_ts_json(p['domain'])}, "
                f"default: {_ts_json(p['default'])}, section: {_ts_json(p['section'])}, "
                f"group_tier: {_ts_json(p.get('group_tier'))}, "
                f"options: {opts_ts}, min: {_ts_json(p.get('min'))}, max: {_ts_json(p.get('max'))}, "
                f"step: {_ts_json(p.get('step'))}, unit: {_ts_json(p.get('unit', ''))}, "
                f"capability: {_ts_json(p['capability'])} }},"
            )
        lines.append("];")
        lines.append("")
        lines.append(
            "export const DEVICE_PREF_SPECS_MAP: Readonly<Record<string, DevicePrefSpecItem>> = "
            "Object.fromEntries(DEVICE_PREF_SPECS_CATALOG.map((s) => [s.key, s]));"
        )
        lines.append("")

    for key, val in defaults.items():
        const_name = f"DEFAULT_{key.upper()}"
        lines.append(f"export const {const_name} = {_ts_json(val)};")
        tokens = [t.strip() for t in val.split(",") if t.strip()]
        lines.append(f"export const DEFAULT_{key.upper()}_TOKENS: ReadonlyArray<string> = [")
        for tok in tokens:
            lines.append(f"  {_ts_json(tok)},")
        lines.append("];")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify generated files match protocol/latter_v3.json without modifying disk.",
    )
    parser.add_argument(
        "--require-sibling",
        action="store_true",
        help="Fail when the sibling ha-latter repository directory is missing.",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    schema_path = repo_root / "protocol" / "latter_v3.json"
    header_path = repo_root / "include" / "latter_protocol.h"
    doc_path = repo_root / "DEVICE_CONTRACT.md"

    sibling_ha = repo_root.parent / "ha-latter"
    ha_json_path = sibling_ha / "custom_components" / "latter" / "protocol_v3.json"
    ha_ts_path = sibling_ha / "frontend_src" / "src" / "protocol-schema.ts"

    if args.require_sibling and not sibling_ha.is_dir():
        print(f"[PROTOCOL DRIFT] Required sibling repository not found: {sibling_ha}", file=sys.stderr)
        return 1

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    validate_schema(schema)
    raw_json_bytes = schema_path.read_bytes()
    expected_header = build_cpp_header(schema)
    expected_doc = build_device_contract_md(schema)
    expected_ts = build_typescript_schema(schema)

    if args.check:
        ok = True
        if not header_path.is_file() or header_path.read_text(encoding="utf-8") != expected_header:
            print(f"[PROTOCOL DRIFT] {header_path} is out of date with {schema_path}", file=sys.stderr)
            ok = False
        if not doc_path.is_file() or doc_path.read_text(encoding="utf-8") != expected_doc:
            print(f"[PROTOCOL DRIFT] {doc_path} is out of date with {schema_path}", file=sys.stderr)
            ok = False
        if sibling_ha.is_dir():
            if not ha_json_path.is_file() or ha_json_path.read_bytes() != raw_json_bytes:
                print(f"[PROTOCOL DRIFT] {ha_json_path} is out of sync with {schema_path}", file=sys.stderr)
                ok = False
            if not ha_ts_path.is_file() or ha_ts_path.read_text(encoding="utf-8") != expected_ts:
                print(f"[PROTOCOL DRIFT] {ha_ts_path} is out of date with {schema_path}", file=sys.stderr)
                ok = False
        return 0 if ok else 1

    header_path.write_text(expected_header, encoding="utf-8", newline="\n")
    doc_path.write_text(expected_doc, encoding="utf-8", newline="\n")
    print(f"Generated {header_path.name} and {doc_path.name} from {schema_path.name}")

    if sibling_ha.is_dir():
        ha_json_path.parent.mkdir(parents=True, exist_ok=True)
        ha_json_path.write_bytes(raw_json_bytes)
        print(f"Synced {ha_json_path.name} to {sibling_ha.name}")

        ha_ts_path.parent.mkdir(parents=True, exist_ok=True)
        ha_ts_path.write_text(expected_ts, encoding="utf-8", newline="\n")
        print(f"Generated {ha_ts_path.name} in {sibling_ha.name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
