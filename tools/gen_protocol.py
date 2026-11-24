#!/usr/bin/env python3
"""Generate include/latter_protocol.h, DEVICE_CONTRACT.md, and sibling protocol_v3.json from protocol/latter_v3.json."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Final

_CPP_IDENT_RE: Final = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_SETTINGS_ROOT_ROUTE_ID: Final = 250
_MAX_UINT8_OPTIONS: Final = 255

_FORBIDDEN_CORE_TOP_KEYS: Final[frozenset[str]] = frozenset(
    {"studio_contract", "ha_services", "world_clock_zones", "world_clock_aliases"}
)
_REQUIRED_PREF_SPEC_KEYS: Final[frozenset[str]] = frozenset(
    {"key", "domain", "default", "options", "min", "max", "step", "capability"}
)
_PREF_DOMAIN_ENUM: Final[dict[str, str]] = {
    "select": "SELECT",
    "switch": "SWITCH",
    "number": "NUMBER",
    "text": "TEXT",
}
_PREF_CAP_ENUM: Final[dict[str, str]] = {
    "display": "DISPLAY",
    "display_imu": "DISPLAY_IMU",
    "display_battery": "DISPLAY_BATTERY",
    "display_or_audio": "DISPLAY_OR_AUDIO",
    "hw_jpeg": "HW_JPEG",
    "mic": "MIC",
    "speaker": "SPEAKER",
    "beacon": "BEACON",
    "beacon_battery": "BEACON_BATTERY",
}


def _assert_cpp_ident(ident: str, context: str) -> str:
    if not _CPP_IDENT_RE.fullmatch(ident):
        raise ValueError(f"Invalid C++ identifier {ident!r} in {context}")
    return ident


def _pascal_case(key: str) -> str:
    return "".join(part.capitalize() for part in key.split("_") if part)


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


def _is_strict_int(val: Any) -> bool:
    return isinstance(val, int) and not isinstance(val, bool)


def _validate_pref_spec(
    spec: dict[str, Any],
    device_prefs_fields: dict[str, str],
    seen_keys: set[str],
    seen_pascals: set[str],
) -> None:
    if not isinstance(spec, dict):
        raise TypeError("Each device_pref_specs entry must be a JSON object")
    key = str(spec.get("key") or "")
    if not key:
        raise ValueError("device_pref_specs entry is missing a non-empty 'key'")
    if key in seen_keys:
        raise ValueError(f"Duplicate device_pref_specs key {key!r}")
    seen_keys.add(key)

    spec_keys = set(spec.keys())
    if spec_keys != _REQUIRED_PREF_SPEC_KEYS:
        missing = sorted(_REQUIRED_PREF_SPEC_KEYS - spec_keys)
        extra = sorted(spec_keys - _REQUIRED_PREF_SPEC_KEYS)
        raise ValueError(
            f"device_pref_specs[{key!r}] key mismatch (missing={missing}, extra={extra}); "
            "UI-only metadata belongs in ha-latter/custom_components/latter/studio_contract.json"
        )

    if key not in device_prefs_fields:
        raise ValueError(
            f"device_pref_specs[{key!r}] has no matching field in domains.device_prefs.fields"
        )

    pascal = _assert_cpp_ident(_pascal_case(key), f"device_pref_specs[{key!r}].pascal")
    if pascal in seen_pascals:
        raise ValueError(f"Duplicate PascalCase identifier {pascal!r} for device_pref_specs[{key!r}]")
    seen_pascals.add(pascal)

    domain = spec["domain"]
    if domain not in _PREF_DOMAIN_ENUM:
        raise ValueError(f"device_pref_specs[{key!r}] has invalid domain {domain!r}")
    cap = spec["capability"]
    if cap not in _PREF_CAP_ENUM:
        raise ValueError(f"device_pref_specs[{key!r}] has invalid capability {cap!r}")

    options = spec["options"]
    if not isinstance(options, list):
        raise TypeError(f"device_pref_specs[{key!r}].options must be a list")
    default = spec["default"]
    min_val, max_val, step = spec["min"], spec["max"], spec["step"]

    if domain == "select":
        if not options or len(options) > _MAX_UINT8_OPTIONS:
            raise ValueError(
                f"select pref {key!r} must have 1..{_MAX_UINT8_OPTIONS} options, got {len(options)}"
            )
        if not all(isinstance(o, str) and o for o in options) or len(set(options)) != len(options):
            raise ValueError(f"select pref {key!r} options must be unique non-empty strings")
        if not isinstance(default, str) or default not in options:
            raise ValueError(f"select pref {key!r} default {default!r} must be in options")
        if any(v is not None for v in (min_val, max_val, step)):
            raise ValueError(f"select pref {key!r} must have null min/max/step")
    elif domain == "switch":
        if not isinstance(default, bool):
            raise ValueError(f"switch pref {key!r} default must be bool, got {default!r}")
        if options or any(v is not None for v in (min_val, max_val, step)):
            raise ValueError(f"switch pref {key!r} must have empty options and null min/max/step")
    elif domain == "number":
        if not all(_is_strict_int(v) for v in (default, min_val, max_val, step)):
            raise ValueError(f"number pref {key!r} default/min/max/step must be integers")
        if not (min_val <= default <= max_val) or step <= 0:
            raise ValueError(
                f"number pref {key!r} invalid bounds min={min_val}, default={default}, max={max_val}, step={step}"
            )
        if options:
            raise ValueError(f"number pref {key!r} must have empty options")
    else:  # text
        if not isinstance(default, str):
            raise ValueError(f"text pref {key!r} default must be str, got {default!r}")
        if options or any(v is not None for v in (min_val, max_val, step)):
            raise ValueError(f"text pref {key!r} must have empty options and null min/max/step")


def validate_schema(schema: dict[str, Any]) -> None:
    """Validate structural, ownership-split, index-contiguity, RouteId, and pref-spec invariants."""
    forbidden = sorted(_FORBIDDEN_CORE_TOP_KEYS.intersection(schema.keys()))
    if forbidden:
        raise ValueError(
            f"protocol/latter_v3.json must not contain HA/Studio-owned top-level keys {forbidden} "
            "(owned by ha-latter/custom_components/latter/studio_contract.json)"
        )

    header_fields = schema.get("header") or []
    if len(header_fields) != 3:
        raise ValueError(f"Expected 3 header fields, found {len(header_fields)}")
    header_indices = [int(hf["index"]) for hf in header_fields]
    if header_indices != list(range(len(header_fields))):
        raise ValueError(
            f"Header field indices must be contiguous 0..{len(header_fields) - 1}, got {header_indices}"
        )
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

    pref_specs = schema.get("device_pref_specs") or []
    if not isinstance(pref_specs, list) or not pref_specs:
        raise ValueError("Schema device_pref_specs must be a non-empty list")
    device_prefs_fields = {
        str(f["name"]): str(f["cpp_enum"])
        for f in (domains.get("device_prefs") or {}).get("fields", [])
    }
    seen_keys: set[str] = set()
    seen_pascals: set[str] = set()
    for spec in pref_specs:
        _validate_pref_spec(spec, device_prefs_fields, seen_keys, seen_pascals)


def _append_pref_contracts_cpp(
    lines: list[str],
    pref_specs: list[dict[str, Any]],
    device_prefs_fields: dict[str, str],
) -> None:
    """Emit zero-allocation C++20 constexpr pref option arrays, defaults, bounds, and lookup helpers."""
    lines.extend(
        [
            "enum class PrefContractDomain : uint8_t {",
            "  SELECT = 0,",
            "  SWITCH = 1,",
            "  NUMBER = 2,",
            "  TEXT = 3,",
            "};",
            "",
            "enum class PrefContractCap : uint8_t {",
            "  DISPLAY = 0,",
            "  DISPLAY_IMU = 1,",
            "  DISPLAY_BATTERY = 2,",
            "  DISPLAY_OR_AUDIO = 3,",
            "  HW_JPEG = 4,",
            "  MIC = 5,",
            "  SPEAKER = 6,",
            "  BEACON = 7,",
            "  BEACON_BATTERY = 8,",
            "};",
            "",
        ]
    )

    table_rows: list[str] = []
    for spec in pref_specs:
        key = str(spec["key"])
        pascal = _pascal_case(key)
        domain = str(spec["domain"])
        cap_enum = _PREF_CAP_ENUM[str(spec["capability"])]
        field_enum = device_prefs_fields[key]

        if domain == "select":
            options: list[str] = [str(o) for o in spec["options"]]
            default_str = str(spec["default"])
            default_idx = options.index(default_str)
            opts_ident = _assert_cpp_ident(f"kPrefOpts{pascal}", f"pref_opts.{key}")
            def_idx_ident = _assert_cpp_ident(f"kPrefDefault{pascal}Idx", f"pref_default_idx.{key}")
            def_ident = _assert_cpp_ident(f"kPrefDefault{pascal}", f"pref_default.{key}")
            opts_csv = ", ".join(_cpp_str(o) for o in options)
            lines.append(
                f"inline constexpr std::array<std::string_view, {len(options)}> {opts_ident} = {{{{{opts_csv}}}}};"
            )
            lines.append(f"inline constexpr uint8_t {def_idx_ident} = {default_idx}U;")
            lines.append(f"inline constexpr std::string_view {def_ident} = {_cpp_str(default_str)};")
            table_rows.append(
                f"    {{DevicePrefsField::{field_enum}, {_cpp_str(key)}, PrefContractDomain::SELECT, "
                f"PrefContractCap::{cap_enum}, std::span<const std::string_view>{{{opts_ident}}}, "
                f"{def_ident}, false, {default_idx}, {def_idx_ident}, false, 0, 0, 0}},"
            )
        elif domain == "switch":
            default_bool = bool(spec["default"])
            bool_lit = "true" if default_bool else "false"
            wire_str = '"1"' if default_bool else '"0"'
            int_val = 1 if default_bool else 0
            def_ident = _assert_cpp_ident(f"kPrefDefault{pascal}", f"pref_default.{key}")
            lines.append(f"inline constexpr bool {def_ident} = {bool_lit};")
            table_rows.append(
                f"    {{DevicePrefsField::{field_enum}, {_cpp_str(key)}, PrefContractDomain::SWITCH, "
                f"PrefContractCap::{cap_enum}, std::span<const std::string_view>{{}}, "
                f"{wire_str}, {def_ident}, {int_val}, 0U, false, 0, 0, 0}},"
            )
        elif domain == "number":
            def_i = int(spec["default"])
            min_i = int(spec["min"])
            max_i = int(spec["max"])
            step_i = int(spec["step"])
            def_ident = _assert_cpp_ident(f"kPrefDefault{pascal}", f"pref_default.{key}")
            min_ident = _assert_cpp_ident(f"kPrefMin{pascal}", f"pref_min.{key}")
            max_ident = _assert_cpp_ident(f"kPrefMax{pascal}", f"pref_max.{key}")
            step_ident = _assert_cpp_ident(f"kPrefStep{pascal}", f"pref_step.{key}")
            lines.append(
                f"inline constexpr int32_t {def_ident} = {def_i};  "
                f"// [{min_i}..{max_i}, step {step_i}]"
            )
            lines.append(f"inline constexpr int32_t {min_ident} = {min_i};")
            lines.append(f"inline constexpr int32_t {max_ident} = {max_i};")
            lines.append(f"inline constexpr int32_t {step_ident} = {step_i};")
            table_rows.append(
                f"    {{DevicePrefsField::{field_enum}, {_cpp_str(key)}, PrefContractDomain::NUMBER, "
                f"PrefContractCap::{cap_enum}, std::span<const std::string_view>{{}}, "
                f"{_cpp_str(def_i)}, false, {def_ident}, 0U, true, {min_ident}, {max_ident}, {step_ident}}},"
            )
        else:  # text
            default_str = str(spec["default"])
            def_ident = _assert_cpp_ident(f"kPrefDefault{pascal}", f"pref_default.{key}")
            lines.append(f"inline constexpr std::string_view {def_ident} = {_cpp_str(default_str)};")
            table_rows.append(
                f"    {{DevicePrefsField::{field_enum}, {_cpp_str(key)}, PrefContractDomain::TEXT, "
                f"PrefContractCap::{cap_enum}, std::span<const std::string_view>{{}}, "
                f"{def_ident}, false, 0, 0U, false, 0, 0, 0}},"
            )

    lines.extend(
        [
            "",
            "struct PrefContractSpec {",
            "  DevicePrefsField field;",
            "  std::string_view key;",
            "  PrefContractDomain domain;",
            "  PrefContractCap capability;",
            "  std::span<const std::string_view> options;",
            "  std::string_view default_str;",
            "  bool default_bool;",
            "  int32_t default_int;",
            "  uint8_t default_option_idx;",
            "  bool has_bounds;",
            "  int32_t min_val;",
            "  int32_t max_val;",
            "  int32_t step;",
            "};",
            "",
            f"inline constexpr std::array<PrefContractSpec, {len(table_rows)}> kPrefContractSpecs = {{{{",
            *table_rows,
            "}};",
            "",
            "inline constexpr const PrefContractSpec *find_pref_contract(std::string_view key) noexcept {",
            "  for (const auto &spec : kPrefContractSpecs) {",
            "    if (spec.key == key) return &spec;",
            "  }",
            "  return nullptr;",
            "}",
            "",
            "inline constexpr const PrefContractSpec *find_pref_contract(DevicePrefsField field) noexcept {",
            "  for (const auto &spec : kPrefContractSpecs) {",
            "    if (spec.field == field) return &spec;",
            "  }",
            "  return nullptr;",
            "}",
            "",
            "inline constexpr int find_pref_option_index(std::span<const std::string_view> options,",
            "                                            std::string_view value) noexcept {",
            "  for (size_t i = 0; i < options.size(); ++i) {",
            "    if (options[i] == value) return static_cast<int>(i);",
            "  }",
            "  return -1;",
            "}",
            "",
            "inline constexpr int find_pref_option_index(const PrefContractSpec &spec,",
            "                                            std::string_view value) noexcept {",
            "  return find_pref_option_index(spec.options, value);",
            "}",
            "",
            "inline constexpr std::string_view pref_option_at_clamped(std::span<const std::string_view> options,",
            "                                                         size_t idx,",
            "                                                         size_t fallback = 0U) noexcept {",
            "  if (options.empty()) return {};",
            "  if (idx < options.size()) return options[idx];",
            "  return options[(fallback < options.size()) ? fallback : 0U];",
            "}",
            "",
            "inline constexpr std::string_view pref_option_at_clamped(const PrefContractSpec &spec,",
            "                                                         size_t idx) noexcept {",
            "  return pref_option_at_clamped(spec.options, idx, spec.default_option_idx);",
            "}",
            "",
            "inline constexpr int32_t clamp_pref_number(const PrefContractSpec &spec, int32_t value) noexcept {",
            "  if (!spec.has_bounds) return value;",
            "  if (value < spec.min_val) return spec.min_val;",
            "  if (value > spec.max_val) return spec.max_val;",
            "  return value;",
            "}",
            "",
        ]
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
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace latter_core {",
        "",
        f"static constexpr int kProtocolContractVersion = {ver};",
        f"static constexpr size_t kProtocolMinRequiredFields = {len(header_fields)};",
        "static constexpr char kProtocolFieldSep = '\\x1f';",
        "static constexpr char kProtocolRecordSep = '\\x1e';",
        "",
    ]

    # Shared defaults (one definition for both repos): kDefault<Name> string constants.
    for key, value in defaults.items():
        const_ident = _assert_cpp_ident(f"kDefault{_pascal_case(key)}", f"defaults.{key}")
        lines.append(f"static constexpr const char *{const_ident} = {_cpp_str(value)};  // defaults.{key}")
    if defaults:
        lines.append("")

    shortcut_routes = (schema.get("catalogs") or {}).get("shortcut_routes") or []
    if shortcut_routes:
        lines.extend(
            [
                "enum class ShortcutRouteCap : uint8_t {",
                "  NONE = 0,",
                "  ROUND = 1,",
                "  IMU = 2,",
                "  NO_IMU = 3,",
                "};",
                "",
                "enum class RouteId : uint8_t {",
                "  NONE = 0,",
            ]
        )
        route_idents: list[str] = []
        for idx, r in enumerate(shortcut_routes, start=1):
            ident = _assert_cpp_ident(
                str(r["token"]).upper().replace(":", "_").replace("-", "_"),
                f"shortcut_routes[{idx}]",
            )
            route_idents.append(ident)
            lines.append(f"  {ident} = {idx},")
        lines.extend(
            [
                f"  SETTINGS_ROOT = {_SETTINGS_ROOT_ROUTE_ID},",
                "};",
                "",
                "struct ShortcutRouteSpec {",
                "  RouteId id;",
                "  const char *token;",
                "  const char *label;",
                "  bool dock_only;",
                "  ShortcutRouteCap cap;",
                "};",
                "",
                "inline constexpr ShortcutRouteSpec kShortcutRoutes[] = {",
            ]
        )
        cap_map = {"round": "ROUND", "imu": "IMU", "no_imu": "NO_IMU"}
        for ident, r in zip(route_idents, shortcut_routes, strict=True):
            dock_only = "true" if r.get("dock_only") else "false"
            cap_enum = cap_map.get(str(r.get("capability") or "").lower(), "NONE")
            lines.append(
                f"    {{RouteId::{ident}, {_cpp_str(r['token'])}, {_cpp_str(r['label'])}, "
                f"{dock_only}, ShortcutRouteCap::{cap_enum}}},"
            )
        lines.extend(["};", ""])

    lines.append("enum class ProtocolHeaderField : size_t {")
    for hf in header_fields:
        lines.append(f"  {hf['cpp_enum']} = {hf['index']},  // {hf['name']} ({hf['type']})")
    lines.extend([f"  COUNT = {len(header_fields)}", "};", ""])

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
        lines.extend([f"  COUNT = {max_idx + 1}", "};", ""])

    device_prefs_fields = {
        str(f["name"]): str(f["cpp_enum"])
        for f in domains["device_prefs"]["fields"]
    }
    _append_pref_contracts_cpp(lines, schema["device_pref_specs"], device_prefs_fields)

    lines.extend(["}  // namespace latter_core", ""])
    return "\n".join(lines)


def _format_pref_contract_md_range(spec: dict[str, Any]) -> str:
    domain = str(spec["domain"])
    if domain == "select":
        return " \\| ".join(f"`{o}`" for o in spec["options"])
    if domain == "number":
        return f"`{spec['min']}..{spec['max']}` (step `{spec['step']}`)"
    if domain == "switch":
        return "`0` \\| `1`"
    return "*(freeform string)*"


def build_device_contract_md(schema: dict[str, Any]) -> str:
    """Render DEVICE_CONTRACT.md from protocol/latter_v3.json."""
    ver = int(schema["contract_version"])
    header_fields = schema["header"]
    domains = schema["domains"]
    device_actions = schema["device_actions"]
    rpc_methods = schema.get("rpc_methods", {})
    pref_specs = schema.get("device_pref_specs") or []

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
        "  - On ESPHome API reconnect (`on_client_connected` from Home Assistant), the device calls `latter_core::reset_all_v3_rev_guards()` and sends `device_hello`.",
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
        lines.extend(
            [
                "### Shared Defaults",
                "",
                "Used by both the firmware (`latter_protocol.h`) and Home Assistant until a value is set.",
                "",
                "| Name | Value |",
                "| :--- | :--- |",
            ]
        )
        for key, value in defaults.items():
            lines.append(f"| `{key}` | `{value}` |")
        lines.append("")

    lines.extend(["## 2. Domain Payloads", ""])

    for dom_key, dom in domains.items():
        enum_name = dom["cpp_enum_name"]
        fields = dom["fields"]
        total_count = max(int(f["index"]) for f in fields) + 1
        lines.extend(
            [
                f"### `{dom_key}` (`{enum_name}`, `{total_count}` total fields)",
                "",
                f"{dom['description']}.",
                "",
                "| Index | Field | C++ Enum | Type | Description |",
                "| :---: | :--- | :--- | :---: | :--- |",
            ]
        )
        for f in fields:
            lines.append(
                f"| `{f['index']}` | `{f['name']}` | `{enum_name}::{f['cpp_enum']}` | `{f['type']}` | {f['description']} |"
            )
        lines.append("")

    lines.extend(
        [
            "## 3. ESPHome Device Actions (`esphome.<node>_<action>`)",
            "",
            "| Action | Variables | Description |",
            "| :--- | :--- | :--- |",
        ]
    )
    for act_name, act_info in device_actions.items():
        vars_map = act_info.get("variables") or {}
        vars_str = ", ".join(f"`{k}: {v}`" for k, v in vars_map.items()) if vars_map else "*(none)*"
        lines.append(f"| `{act_name}` | {vars_str} | {act_info['description']} |")
    lines.append("")

    lines.extend(
        [
            "## 4. Firmware RPC Outbox Methods (`latter_rpc_outbox`)",
            "",
            "| Method | Canonical Outbox Fields | Description |",
            "| :--- | :--- | :--- |",
        ]
    )
    for rpc_name, rpc_info in rpc_methods.items():
        f_str = ", ".join(f"`{f}`" for f in rpc_info["fields"])
        lines.append(f"| `{rpc_name}` | {f_str} | {rpc_info['description']} |")
    lines.append("")

    if pref_specs:
        lines.extend(
            [
                f"## 5. Declarative Device Preferences (`device_pref_specs`, `{len(pref_specs)}` entries)",
                "",
                "Pure wire and behavioral preference contracts compiled into `include/latter_protocol.h` (`kPrefContractSpecs`). Studio/HA presentation metadata lives in `ha-latter/custom_components/latter/studio_contract.json`.",
                "",
                "| Key | Domain | Default | Options / Bounds | Capability |",
                "| :--- | :---: | :--- | :--- | :---: |",
            ]
        )
        for spec in pref_specs:
            def_val = spec["default"]
            def_escaped = str(def_val).replace("|", "\\|")
            def_str = (
                ("`true`" if def_val else "`false`")
                if isinstance(def_val, bool)
                else (f"`{def_escaped}`" if def_escaped else "*(empty)*")
            )
            lines.append(
                f"| `{spec['key']}` | `{spec['domain']}` | {def_str} | "
                f"{_format_pref_contract_md_range(spec)} | `{spec['capability']}` |"
            )
        lines.append("")

    return "\n".join(lines)


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

    if args.require_sibling and not sibling_ha.is_dir():
        print(f"[PROTOCOL DRIFT] Required sibling repository not found: {sibling_ha}", file=sys.stderr)
        return 1

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    validate_schema(schema)
    raw_json_bytes = schema_path.read_bytes()
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
        if sibling_ha.is_dir() and (
            not ha_json_path.is_file() or ha_json_path.read_bytes() != raw_json_bytes
        ):
            print(f"[PROTOCOL DRIFT] {ha_json_path} is out of sync with {schema_path}", file=sys.stderr)
            ok = False
        return 0 if ok else 1

    header_path.write_text(expected_header, encoding="utf-8", newline="\n")
    doc_path.write_text(expected_doc, encoding="utf-8", newline="\n")
    print(f"Generated {header_path.name} and {doc_path.name} from {schema_path.name}")

    if sibling_ha.is_dir():
        ha_json_path.parent.mkdir(parents=True, exist_ok=True)
        ha_json_path.write_bytes(raw_json_bytes)
        print(f"Synced {ha_json_path.name} to {sibling_ha.name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
