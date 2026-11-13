#!/usr/bin/env python3
"""Cross-platform static architecture, HAL, UI, protocol, and security verifier for esphome-latter-core.

Enforces hardware, DMA, light-sleep, audio, Protocol v3, Device UI, and security guardrails
identically in local development and GitHub Actions CI using Python 3.11+ standard library only.

Usage:
    python tools/verify_core.py          # Full static + protocol/board/sound tools + C++20 host tests
    python tools/verify_core.py --fast   # Fast static/AST/semantic pass (<250ms, skips subprocesses)
"""

from __future__ import annotations

import argparse
import ast
import base64
import difflib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent

SKIP_DIRS = {".git", "__pycache__", ".pytest_cache", ".mypy_cache", ".ruff_cache", ".esphome", "node_modules"}
TEXT_EXTS = {".yaml", ".yml", ".h", ".hpp", ".cpp", ".c", ".py", ".json", ".md", ".sh", ".html"}

REQUIRED_BOARD_FLAGS = (
    "LATTER_SCREEN_W",
    "LATTER_SCREEN_H",
    "LATTER_ROUND_DISPLAY",
    "LATTER_HAS_IMU",
    "LATTER_HAS_BATTERY",
    "LATTER_IS_STATIONARY",
    "LATTER_IS_WEARABLE",
    "LATTER_HAS_TOUCH",
    "LATTER_PANEL_IPS",
    "LATTER_PANEL_PPI",
)

RETIRED_PATHS = (
    "mixins",
    "components/latter_hal",
    "include/hal_types.h",
    "include/va_cancel_helper.h",
    "include/hal_pmu_axp2101.h",
    "include/ui_core.h",
    "include/ui_touch_router.h",
    "include/ui_gesture_engine.h",
    "include/ui_icons.h",
    "include/ui_clock_home.h",
    "include/ui_music.h",
    "include/ui_area_home_hub.h",
    "include/ui_remote.h",
    "include/ui_weather_sky.h",
    "include/ui_tools.h",
    "include/ui_games.h",
    "include/ui_ambient_va.h",
    "include/ui_system.h",
    "components/latter_ui/gesture_engine.cpp",
    "modules/features",
    "modules/latter_client.yaml",
    "modules/area_control.yaml",
    "modules/display_ui.yaml",
    "modules/pmu_none.yaml",
    "modules/imu_none.yaml",
    "modules/pmu_axp2101.yaml",
    "modules/imu_qmi8658.yaml",
    "modules/settings_entities.yaml",
    "guis/universal/touch_handlers.yaml",
    "tests/host/test_latter_state.cpp",
    "docs/UPSTREAM_ESPHOME_VA_PR.md",
)

RETIRED_HALF_DUPLEX_RE = re.compile(
    r"\b("
    + "kAudio"
    + "FullDuplex|"
    + "USE_I2S_AUDIO_"
    + "FULL_DUPLEX|"
    + "(?:hal_)?route_i2s_"
    + r"to_(?:spk|mic))\b"
)
LOSSY_UI_RE = re.compile(r"\bit\.(?:image|draw_pixels_at)\s*\(|0x[cC]718\b|0x[eE]79[cC]\b")
RAW_IT_DRAW_RE = re.compile(
    r"(?<![\w.>])it\.(?:line|rectangle|filled_rectangle|circle|filled_circle|triangle|filled_triangle|print|printf)\s*\("
)
BANNED_TERMS_RE = re.compile(r"\b(household|households)\b", re.IGNORECASE)
DEVICE_SLUG_RE = re.compile(
    r"\b(?:pocket-assistant|wrist-assist|mobile-assistant|remote-assistant|desk-assistant)\b",
    re.IGNORECASE,
)
INCLUDE_RE = re.compile(r"!include\s+([^\s#]+)")
STRAPPING_PINS: dict[str, set[int]] = {
    "ESP32-S3": {0, 3, 45, 46},
    "ESP32-P4": {34, 35, 36, 37, 38},
}


def collect_yaml_include_chain(yaml_path: Path, seen: set[Path] | None = None) -> list[Path]:
    """Return included YAML files in package merge order (dependencies first, root file last)."""
    if seen is None:
        seen = set()
    resolved = yaml_path.resolve()
    if resolved in seen or not resolved.is_file():
        return []
    seen.add(resolved)
    text = resolved.read_text(encoding="utf-8")
    chain: list[Path] = []
    for raw_line in text.splitlines():
        uncommented = raw_line.split("#", 1)[0]
        for match in INCLUDE_RE.finditer(uncommented):
            rel = match.group(1).strip("'\"")
            inc_path = (resolved.parent / rel).resolve()
            if inc_path.is_file() and ROOT in inc_path.parents:
                chain.extend(collect_yaml_include_chain(inc_path, seen))
    chain.append(resolved)
    return chain


def extract_substitutions(chain: list[Path]) -> dict[str, str]:
    """Extract merged ESPHome substitutions across an include chain (later files override earlier defaults)."""
    subs: dict[str, str] = {}
    for yf in chain:
        in_subs = False
        subs_indent = -1
        for raw_line in yf.read_text(encoding="utf-8").splitlines():
            code = raw_line.split(" #", 1)[0] if " #" in raw_line and raw_line.count('"') % 2 == 0 else raw_line
            if code.lstrip().startswith("#") or not code.strip():
                continue
            indent = len(code) - len(code.lstrip(" "))
            m_hdr = re.match(r"^(\s*)substitutions\s*:\s*$", code.rstrip())
            if m_hdr:
                in_subs = True
                subs_indent = len(m_hdr.group(1))
                continue
            if in_subs:
                if indent <= subs_indent:
                    in_subs = False
                else:
                    m_kv = re.match(r"^\s*([A-Za-z0-9_]+)\s*:\s*(.+?)\s*$", code.rstrip())
                    if m_kv:
                        val = m_kv.group(2).strip().strip('"').strip("'")
                        subs[m_kv.group(1)] = val
    return subs


def resolve_substitutions(value: str, subs: dict[str, str]) -> str:
    def _repl(m: re.Match[str]) -> str:
        key = m.group(1)
        return subs.get(key, m.group(0))

    out = value
    for _ in range(4):
        nxt = re.sub(r"\$\{([A-Za-z0-9_]+)\}", _repl, out)
        if nxt == out:
            break
        out = nxt
    return out


def verify_strapping_pins(failures: list[str]) -> None:
    """Ensure ignore_strapping_warning: true only appears on genuine strapping GPIOs for each target SoC."""
    facts_dir = ROOT / "boards" / "facts"
    if facts_dir.is_dir():
        for fp in sorted(facts_dir.glob("*.json")):
            if fp.name == "schema.json":
                continue
            fdata = json.loads(fp.read_text(encoding="utf-8"))
            soc = fdata.get("silicon", {}).get("soc", "")
            allowed = STRAPPING_PINS.get(soc)
            if allowed is None:
                continue
            for pin in fdata.get("pins", []):
                g = pin.get("gpio")
                if pin.get("strapping") and isinstance(g, int) and g not in allowed:
                    failures.append(
                        f"[STRAPPING PIN] boards/facts/{fp.name}: GPIO{g} has strapping=true, "
                        f"which is invalid on {soc} (valid: {sorted(allowed)})"
                    )

    targets: list[Path] = (
        sorted((ROOT / "boards").glob("*.yaml"))
        + sorted((ROOT / "devices").glob("*.yaml"))
        + sorted((ROOT / "examples").glob("*.yaml"))
        + sorted((ROOT / "examples" / "bringup").glob("*.yaml"))
    )
    reported: set[tuple[str, str, int]] = set()
    for target in targets:
        chain = collect_yaml_include_chain(target)
        subs = extract_substitutions(chain)
        merged_uncommented = "\n".join(
            ln
            for yf in chain
            for ln in yf.read_text(encoding="utf-8").splitlines()
            if not ln.lstrip().startswith("#")
        )
        soc = "ESP32-P4" if re.search(r"(?m)^\s*variant\s*:\s*esp32p4\b", merged_uncommented) else "ESP32-S3"
        allowed = STRAPPING_PINS[soc]
        target_rel = target.relative_to(ROOT).as_posix()

        for yf in chain:
            yf_rel = yf.relative_to(ROOT).as_posix()
            lines = yf.read_text(encoding="utf-8").splitlines()
            for idx, raw_line in enumerate(lines):
                if raw_line.lstrip().startswith("#"):
                    continue
                code = raw_line.split(" #", 1)[0] if " #" in raw_line and raw_line.count('"') % 2 == 0 else raw_line
                if not re.search(r"\bignore_strapping_warning\s*:\s*(?:true|True|yes|1)\b", code):
                    continue

                pin_expr: str | None = None
                m_inline = re.search(r"\bnumber\s*:\s*([^\s,}#]+)", code)
                if m_inline:
                    pin_expr = m_inline.group(1).strip("'\"")
                else:
                    stripped = code.lstrip(" ")
                    if stripped.startswith("- "):
                        key_indent = len(code) - len(stripped) + 2
                    else:
                        key_indent = len(code) - len(stripped)
                    for up in range(idx - 1, -1, -1):
                        ul = lines[up]
                        if ul.lstrip().startswith("#") or not ul.strip():
                            continue
                        ucode = ul.split(" #", 1)[0] if " #" in ul and ul.count('"') % 2 == 0 else ul
                        u_indent = len(ucode) - len(ucode.lstrip(" "))
                        if ucode.lstrip(" ").startswith("- ") and u_indent + 2 == key_indent:
                            m_num = re.match(r"^\s*-\s*number\s*:\s*(\S+)", ucode.rstrip())
                            if m_num:
                                pin_expr = m_num.group(1).strip("'\"")
                            break
                        if u_indent < key_indent:
                            break
                        if u_indent == key_indent:
                            m_num = re.match(r"^\s*number\s*:\s*(\S+)", ucode.rstrip())
                            if m_num:
                                pin_expr = m_num.group(1).strip("'\"")
                                break
                    if pin_expr is None:
                        for dn in range(idx + 1, len(lines)):
                            dl = lines[dn]
                            if dl.lstrip().startswith("#") or not dl.strip():
                                continue
                            dcode = dl.split(" #", 1)[0] if " #" in dl and dl.count('"') % 2 == 0 else dl
                            d_indent = len(dcode) - len(dcode.lstrip(" "))
                            if d_indent < key_indent or dcode.lstrip(" ").startswith("- "):
                                break
                            if d_indent == key_indent:
                                m_num = re.match(r"^\s*number\s*:\s*(\S+)", dcode.rstrip())
                                if m_num:
                                    pin_expr = m_num.group(1).strip("'\"")
                                    break

                key = (soc, yf_rel, idx + 1)
                if pin_expr is None:
                    if key not in reported:
                        reported.add(key)
                        failures.append(
                            f"[STRAPPING PIN] {target_rel} ({yf_rel}:{idx + 1}): "
                            "ignore_strapping_warning: true without resolvable sibling number:"
                        )
                    continue

                resolved_pin = resolve_substitutions(pin_expr, subs)
                m_gpio = re.match(r"^(?:GPIO)?(\d+)$", resolved_pin)
                if not m_gpio:
                    if key not in reported:
                        reported.add(key)
                        failures.append(
                            f"[STRAPPING PIN] {target_rel} ({yf_rel}:{idx + 1}): "
                            f"could not resolve GPIO number from {pin_expr!r} (-> {resolved_pin!r})"
                        )
                    continue

                gpio_num = int(m_gpio.group(1))
                if gpio_num not in allowed:
                    sig = (f"{soc}:{gpio_num}", yf_rel, idx + 1)
                    if sig not in reported:
                        reported.add(sig)
                        valid_str = ", ".join(f"GPIO{n}" for n in sorted(allowed))
                        failures.append(
                            f"[STRAPPING PIN] {target_rel} ({yf_rel}:{idx + 1}): "
                            f"GPIO{gpio_num} has ignore_strapping_warning: true, which is invalid on {soc} "
                            f"(strapping pins: {valid_str})"
                        )


def iter_repo_files(base: Path) -> list[Path]:
    files: list[Path] = []
    if not base.is_dir():
        return files
    for p in sorted(base.rglob("*")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.is_file():
            files.append(p)
    return files


def _strip_cpp_comments(text: str) -> str:
    no_block = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", no_block)


def _strip_yaml_comments(text: str) -> str:
    out: list[str] = []
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        if " #" in line and line.count('"') % 2 == 0:
            line = line.split(" #", 1)[0]
        out.append(line)
    return "\n".join(out)


def _header_provides_esphome_defines(hdr_path: Path, seen: set[Path] | None = None) -> bool:
    """Return True if `hdr_path` includes esphome/core/defines.h (directly or transitively) before any USE_* guard."""
    if seen is None:
        seen = set()
    resolved = hdr_path.resolve()
    if resolved in seen or not resolved.is_file():
        return False
    seen.add(resolved)
    clean = _strip_cpp_comments(resolved.read_text(encoding="utf-8"))
    for line in clean.splitlines():
        if re.match(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\bUSE_[A-Z0-9_]+\b", line):
            return False
        m_inc = re.match(r'^\s*#\s*include\s*"([^"]+)"', line)
        if not m_inc:
            continue
        inc_target = m_inc.group(1)
        if inc_target == "esphome/core/defines.h":
            return True
        for cand in (resolved.parent / inc_target, ROOT / "include" / inc_target):
            if cand.is_file() and _header_provides_esphome_defines(cand, set(seen)):
                return True
    return False


def verify_component_deps(failures: list[str]) -> None:
    """Ensure 3-component architecture (latter_core, latter_ui, latter_beacon) and valid AUTO_LOAD/DEPENDENCIES."""
    comp_root = ROOT / "components"
    if not comp_root.is_dir():
        failures.append("[COMPONENTS] components/ directory missing")
        return
    comp_dirs = sorted(p.name for p in comp_root.iterdir() if p.is_dir() and p.name not in SKIP_DIRS)
    if comp_dirs != ["latter_beacon", "latter_core", "latter_ui"]:
        failures.append(
            f"[COMPONENTS] Expected exact 3 components ['latter_beacon', 'latter_core', 'latter_ui'], found {comp_dirs}"
        )

    for init_py in sorted(comp_root.glob("*/__init__.py")):
        rel = init_py.relative_to(ROOT).as_posix()
        comp_name = init_py.parent.name
        try:
            tree = ast.parse(init_py.read_text(encoding="utf-8"), filename=rel)
        except SyntaxError as exc:
            failures.append(f"[COMPONENT DEPS] {rel} failed Python AST parse: {exc}")
            continue

        deps: set[str] = set()
        autoloads: set[str] = set()
        for node in tree.body:
            if isinstance(node, (ast.Assign, ast.AnnAssign)):
                targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                names = {t.id for t in targets if isinstance(t, ast.Name)}
                if "DEPENDENCIES" in names and node.value is not None:
                    for sub in ast.walk(node.value):
                        if isinstance(sub, ast.Constant) and isinstance(sub.value, str):
                            deps.add(sub.value)
                if "AUTO_LOAD" in names and node.value is not None:
                    for sub in ast.walk(node.value):
                        if isinstance(sub, ast.Constant) and isinstance(sub.value, str):
                            autoloads.add(sub.value)
            elif isinstance(node, ast.FunctionDef) and node.name == "AUTO_LOAD":
                for sub in ast.walk(node):
                    if isinstance(sub, ast.Return) and sub.value is not None:
                        for lit in ast.walk(sub.value):
                            if isinstance(lit, ast.Constant) and isinstance(lit.value, str):
                                autoloads.add(lit.value)

        overlap = sorted(deps & autoloads)
        if overlap:
            failures.append(
                f"[COMPONENT DEPS] {rel}: DEPENDENCIES {sorted(deps)} overlaps AUTO_LOAD {sorted(autoloads)} "
                f"on {overlap} (ESPHome validates DEPENDENCIES before running AUTO_LOAD)"
            )
        if comp_name == "latter_core" and "latter_ui" not in autoloads:
            failures.append(f"[COMPONENT DEPS] {rel}: AUTO_LOAD must wire latter_ui (found {sorted(autoloads)})")
        if "latter_hal" in (deps | autoloads):
            failures.append(f"[COMPONENT DEPS] {rel}: references retired latter_hal")


def verify_header_order(failures: list[str]) -> None:
    """Ban circular #include \"esphome.h\" in headers and ensure esphome/core/defines.h precedes #ifdef USE_*."""
    for folder in ("include", "components"):
        base = ROOT / folder
        for path in iter_repo_files(base):
            ext = path.suffix.lower()
            if ext not in {".h", ".hpp", ".cpp"}:
                continue
            rel = path.relative_to(ROOT).as_posix()
            clean = _strip_cpp_comments(path.read_text(encoding="utf-8"))

            if ext in {".h", ".hpp"}:
                if re.search(r'#\s*include\s*[<"]esphome\.h[>"]', clean) or re.search(
                    r'__has_include\s*\(\s*[<"]esphome\.h[>"]\s*\)', clean
                ):
                    failures.append(
                        f"[HEADER ORDER] {rel} must not reference esphome.h "
                        "(prevents circular #pragma once truncation)"
                    )
                if re.search(r"(?m)^\s*using\s+namespace\s+latter_ui\s*;", clean):
                    failures.append(f"[CORE DOCS] {rel} must not declare global `using namespace latter_ui;`")

            lines = clean.splitlines()
            defines_seen = False
            for idx, line in enumerate(lines):
                if re.match(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\bUSE_[A-Z0-9_]+\b", line):
                    if not defines_seen:
                        failures.append(
                            f"[HEADER ORDER] {rel}:{idx + 1} checks USE_* before including esphome/core/defines.h"
                        )
                    break
                m_inc = re.match(r'^\s*#\s*include\s*"([^"]+)"', line)
                if not m_inc:
                    continue
                inc_target = m_inc.group(1)
                if inc_target == "esphome/core/defines.h":
                    defines_seen = True
                    break
                for cand in (path.parent / inc_target, ROOT / "include" / inc_target):
                    if cand.is_file() and _header_provides_esphome_defines(cand):
                        defines_seen = True
                        break
                if defines_seen:
                    break


def expand_local_yaml_includes(yaml_path: Path, seen: set[Path] | None = None) -> str:
    """Read a YAML file and recursively append local !include targets for trait verification."""
    if seen is None:
        seen = set()
    resolved = yaml_path.resolve()
    if resolved in seen or not resolved.is_file():
        return ""
    seen.add(resolved)
    text = resolved.read_text(encoding="utf-8")
    chunks = [text]
    for match in INCLUDE_RE.finditer(text):
        rel = match.group(1).strip("'\"")
        inc_path = (resolved.parent / rel).resolve()
        if inc_path.is_file() and ROOT in inc_path.parents:
            chunks.append(expand_local_yaml_includes(inc_path, seen))
    return "\n".join(chunks)


def _srgb_rel_luminance(hex_rgb: str) -> float:
    r = int(hex_rgb[0:2], 16) / 255.0
    g = int(hex_rgb[2:4], 16) / 255.0
    b = int(hex_rgb[4:6], 16) / 255.0

    def _lin(c: float) -> float:
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)


def _wcag_contrast(hex_a: str, hex_b: str) -> float:
    la = _srgb_rel_luminance(hex_a)
    lb = _srgb_rel_luminance(hex_b)
    hi, lo = (la, lb) if la >= lb else (lb, la)
    return (hi + 0.05) / (lo + 0.05)


def verify_device_ui_and_accessibility(failures: list[str]) -> None:
    """Enforce [FAST PRIMITIVES], [FRAME BASE], [DIRTY BAND], [TAKEOVER ORDER], [TOUCH MM/DEBOUNCE/HOME/BACK],
    [UI COLOR TOKENS], [FONT MM], [TEXT CONTRAST], and [DEVICE UI DOC]."""
    gui_yaml_path = ROOT / "guis" / "universal" / "gui.yaml"
    if not gui_yaml_path.is_file():
        failures.append("[GUI] guis/universal/gui.yaml missing")
        return
    gui_txt = gui_yaml_path.read_text(encoding="utf-8")
    gui_uncommented = _strip_yaml_comments(gui_txt)

    # [FRAME BASE] & [DIRTY BAND]
    if not re.search(r"(?m)^\s*auto_clear_enabled:\s*false\b", gui_uncommented):
        failures.append("[FRAME BASE] guis/universal/gui.yaml must set auto_clear_enabled: false")
    draw_pix_calls = re.findall(r"(?<![\w.>])it\.draw_pixel_at\s*\(", gui_uncommented)
    if (
        "dirty_band()" not in gui_uncommented
        or ".begin_frame(" not in gui_uncommented
        or "frame_base_begin()" not in gui_uncommented
        or "render_system_takeovers(" not in gui_uncommented
        or "touch_engine()" not in gui_uncommented
        or not (2 <= len(draw_pix_calls) <= 4)
    ):
        failures.append(
            "[DIRTY BAND / COMPOSITOR] guis/universal/gui.yaml must wire dirty_band().begin_frame, "
            "frame_base_begin(), render_system_takeovers, touch_engine(), and 2..4 it.draw_pixel_at latch calls"
        )
    if not (ROOT / "components" / "latter_ui" / "touch_engine.cpp").is_file():
        failures.append("[TOUCH ENGINE] components/latter_ui/touch_engine.cpp missing")

    # [FAST PRIMITIVES] across include/ui_*.h, components/latter_ui/*.cpp, and guis/**/*.yaml
    for path in list((ROOT / "include").glob("ui_*.h")) + list((ROOT / "components" / "latter_ui").glob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        clean = _strip_cpp_comments(path.read_text(encoding="utf-8"))
        if path.name in {"ui_gfx.h", "ui_gfx.cpp"}:
            continue
        if RAW_IT_DRAW_RE.search(clean) or re.search(r"(?<![\w.>])it\.draw_pixel_at\s*\(", clean):
            failures.append(f"[FAST PRIMITIVES] {rel} calls raw ESPHome it.* drawing primitive outside ui_gfx")
    if RAW_IT_DRAW_RE.search(gui_uncommented):
        failures.append("[FAST PRIMITIVES] guis/universal/gui.yaml calls raw ESPHome it.* primitive")

    # [TAKEOVER ORDER] Alarm/timer/find-my takeover must precede Voice Assistant takeover in ui_compositor.cpp
    comp_cpp_path = ROOT / "components" / "latter_ui" / "ui_compositor.cpp"
    if comp_cpp_path.is_file():
        comp_clean = _strip_cpp_comments(comp_cpp_path.read_text(encoding="utf-8"))
        m_alarm = re.search(r"st\.alarm_ringing\s*\|\|\s*st\.timer_ringing\s*\|\|\s*st\.find_my_active", comp_clean)
        m_va = re.search(r"st\.va_active\s*\|\|", comp_clean)
        if not m_alarm or not m_va or m_alarm.start() >= m_va.start():
            failures.append(
                "[TAKEOVER ORDER] components/latter_ui/ui_compositor.cpp must render alarm/timer/find_my "
                "takeover strictly before voice assistant takeover"
            )

    # [TOUCH MM], [TOUCH DEBOUNCE], [HOME PREDICATE], [BACK BUTTON Y], [UI COLOR TOKENS]
    hit_h = (ROOT / "include" / "ui_hit_table.h").read_text(encoding="utf-8") if (ROOT / "include" / "ui_hit_table.h").is_file() else ""
    types_h = (ROOT / "include" / "ui_types.h").read_text(encoding="utf-8") if (ROOT / "include" / "ui_types.h").is_file() else ""
    layout_h = (ROOT / "include" / "ui_layout.h").read_text(encoding="utf-8") if (ROOT / "include" / "ui_layout.h").is_file() else ""
    touch_h = (ROOT / "include" / "ui_touch_engine.h").read_text(encoding="utf-8") if (ROOT / "include" / "ui_touch_engine.h").is_file() else ""
    touch_cpp = (ROOT / "components" / "latter_ui" / "touch_engine.cpp").read_text(encoding="utf-8") if (ROOT / "components" / "latter_ui" / "touch_engine.cpp").is_file() else ""

    if "mm_to_px_for(" not in hit_h or "mm_to_px(" not in (types_h + layout_h):
        failures.append("[TOUCH MM] Millimetre conversion helpers (mm_to_px_for / mm_to_px) missing in UI headers")
    if "allow_tap(" not in (hit_h + touch_h + touch_cpp) or "TOUCH_DEBOUNCE_" not in (hit_h + touch_h + touch_cpp):
        failures.append("[TOUCH DEBOUNCE] touch_router().allow_tap(..., TOUCH_DEBOUNCE_*) missing in touch engine")
    if "can_go_home(" not in gui_uncommented or "can_go_home(" not in (layout_h + touch_cpp):
        failures.append("[HOME PREDICATE] Shared can_go_home() predicate must gate both gui.yaml and touch_engine.cpp")
    for tok in ("kChromeShadow", "kChromeHomePill", "kChromeTrack", "kChromeCardBorder", "kChromeBackLabel", "kChromeGlassTint"):
        if tok not in types_h:
            failures.append(f"[UI COLOR TOKENS] {tok} missing from include/ui_types.h")
    for page_cpp in sorted((ROOT / "components" / "latter_ui").glob("page_*.cpp")):
        pclean = _strip_cpp_comments(page_cpp.read_text(encoding="utf-8"))
        for m_back in re.finditer(r"render_back_button\s*\(([^;]+)\)", pclean):
            if "back_y" not in m_back.group(1) and "kBackLabelY" not in m_back.group(1):
                failures.append(
                    f"[BACK BUTTON Y] {page_cpp.relative_to(ROOT).as_posix()} calls render_back_button without back_y"
                )

    # [TEXT CONTRAST] WCAG >= 4.5:1 for small-text palette colors on col_bg (#000000)
    palette: dict[str, str] = {}
    for m_col in re.finditer(r'-\s*id:\s*([A-Za-z0-9_]+)\s*\n\s*hex:\s*"([0-9A-Fa-f]{6})"', gui_uncommented):
        palette[m_col.group(1)] = m_col.group(2)
    bg_hex = palette.get("col_bg", "000000")
    for cid in ("theme_primary", "theme_secondary", "col_white", "col_slate", "col_gold", "col_warm_gold", "col_orange"):
        chex = palette.get(cid)
        if not chex:
            failures.append(f"[TEXT CONTRAST] Missing palette color '{cid}' in guis/universal/gui.yaml")
            continue
        ratio = _wcag_contrast(chex, bg_hex)
        if ratio < 4.5:
            failures.append(
                f"[TEXT CONTRAST] {cid} (#{chex}) on col_bg (#{bg_hex}) has contrast {ratio:.2f}:1 (< 4.5:1)"
            )

    # [FONT MM] & [DEVICE UI DOC]
    gui_subs = extract_substitutions([gui_yaml_path])
    board_metrics: dict[str, dict[str, int]] = {}
    for bf in sorted((ROOT / "boards").glob("*.yaml")):
        btxt = bf.read_text(encoding="utf-8")
        bsubs = {**gui_subs, **extract_substitutions([bf])}
        m_ppi = re.search(r"-DLATTER_PANEL_PPI=(\d+)", btxt)
        m_w = re.search(r"-DLATTER_SCREEN_W=(\d+)", btxt)
        m_h = re.search(r"-DLATTER_SCREEN_H=(\d+)", btxt)
        if not (m_ppi and m_w and m_h):
            continue
        ppi = int(m_ppi.group(1))
        f_small = int(bsubs.get("font_small_size", "21"))
        f_micro = int(bsubs.get("font_micro_size", "14"))
        small_mm = f_small * 25.4 / ppi
        micro_mm = f_micro * 25.4 / ppi
        if small_mm < 1.95:
            failures.append(f"[FONT MM] {bf.name}: font_small_size={f_small}px at {ppi} PPI is {small_mm:.2f} mm (< 2.0 mm)")
        if micro_mm < 1.29:
            failures.append(f"[FONT MM] {bf.name}: font_micro_size={f_micro}px at {ppi} PPI is {micro_mm:.2f} mm (< 1.3 mm)")
        board_metrics[bf.stem] = {
            "w": int(m_w.group(1)),
            "h": int(m_h.group(1)),
            "ppi": ppi,
            "small": f_small,
            "micro": f_micro,
        }

    ui_doc = ROOT / "docs" / "device-ui-design.md"
    if ui_doc.is_file():
        doc_txt = ui_doc.read_text(encoding="utf-8")
        m_boards = re.search(r"(?m)^\|\s*Board file\s*\|(.+)$", doc_txt)
        m_res = re.search(r"(?m)^\|\s*Resolution\s*\|(.+)$", doc_txt)
        m_ppis = re.search(r"(?m)^\|\s*`LATTER_PANEL_PPI`\s*\|(.+)$", doc_txt)
        m_fonts = re.search(r"(?m)^\|\s*`font_small`\s*/\s*`font_micro`\s*\|(.+)$", doc_txt)
        if not (m_boards and m_res and m_ppis and m_fonts):
            failures.append("[DEVICE UI DOC] docs/device-ui-design.md missing Section 6 Per-board adaptation table rows")
        else:
            b_cols = [c.strip().strip("`") for c in m_boards.group(1).strip().rstrip("|").split("|")]
            r_cols = [c.strip() for c in m_res.group(1).strip().rstrip("|").split("|")]
            p_cols = [c.strip() for c in m_ppis.group(1).strip().rstrip("|").split("|")]
            f_cols = [c.strip() for c in m_fonts.group(1).strip().rstrip("|").split("|")]
            for idx, bname in enumerate(b_cols):
                bm = board_metrics.get(bname)
                if not bm or idx >= len(r_cols) or idx >= len(p_cols) or idx >= len(f_cols):
                    failures.append(f"[DEVICE UI DOC] Table column mismatch for board '{bname}'")
                    continue
                want_res = f"{bm['w']} × {bm['h']}"
                want_ppi = str(bm["ppi"])
                want_fonts = f"{bm['small']} / {bm['micro']}"
                if r_cols[idx] != want_res or p_cols[idx] != want_ppi or f_cols[idx] != want_fonts:
                    failures.append(
                        f"[DEVICE UI DOC] docs/device-ui-design.md out of sync with {bname}: "
                        f"doc=({r_cols[idx]}, ppi={p_cols[idx]}, fonts={f_cols[idx]}) vs "
                        f"board=({want_res}, ppi={want_ppi}, fonts={want_fonts})"
                    )


_CPP_TOKEN_RE = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*[\s\S]*?\*/|//[^\n]*')

HOST_LAMBDA_HEADERS: tuple[str, ...] = (
    "latter_protocol.h",
    "latter_state.h",
    "latter_rpc.h",
    "latter_actions.h",
    "hal_battery.h",
    "hal_link_report.h",
    "hal_manifest.h",
    "hal_panel_power.h",
    "ui_hit_table.h",
    "ui_compositor.h",
)


def _strip_cpp_strings_and_comments(text: str) -> str:
    """Mask C++ comments and single-line string/char literals in one pass while preserving exact line count."""

    def _repl(m: re.Match[str]) -> str:
        tok = m.group(0)
        if tok.startswith('"'):
            return '"' + (" " * max(0, len(tok) - 2)) + '"'
        if tok.startswith("'"):
            return "' '"
        if tok.startswith("/*"):
            return "\n" * tok.count("\n")
        return ""

    return _CPP_TOKEN_RE.sub(_repl, text)


def _strip_yaml_comments_keep_lines(text: str) -> str:
    """Strip YAML comments while preserving exact 1-based line numbers."""
    out: list[str] = []
    for line in text.splitlines():
        lstripped = line.lstrip()
        if lstripped.startswith("#") and not re.match(r"^#\s*(?:if|ifdef|ifndef|elif|else|endif|define|include)\b", lstripped):
            out.append("")
            continue
        if " #" in line and line.count('"') % 2 == 0 and line.count("'") % 2 == 0:
            line = line.split(" #", 1)[0]
        out.append(line)
    return "\n".join(out)


def _collect_namespace_symbols(paths: list[Path] | None = None) -> tuple[dict[str, set[str]], set[str]]:
    """Extract per-namespace C++ identifiers and global identifiers across given files or repo C++ roots."""
    ns_syms: dict[str, set[str]] = {}
    all_syms: set[str] = set()
    ns_open_re = re.compile(r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{")
    ident_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")

    if paths is None:
        file_list: list[Path] = []
        for base in (ROOT / "include", ROOT / "components", ROOT / "examples" / "bringup"):
            if base.is_dir():
                file_list.extend(p for p in iter_repo_files(base) if p.suffix.lower() in {".h", ".hpp", ".cpp"})
    else:
        file_list = [p for p in paths if p.is_file()]

    for path in file_list:
        clean = _strip_cpp_strings_and_comments(path.read_text(encoding="utf-8"))
        all_syms.update(ident_re.findall(clean))
        pos = 0
        n = len(clean)
        stack: list[tuple[str | None, int]] = []
        depth = 0
        while pos < n:
            m_ns = ns_open_re.search(clean, pos)
            next_l = clean.find("{", pos)
            next_r = clean.find("}", pos)
            candidates = [x for x in (next_l, next_r) if x != -1]
            if not candidates:
                if stack:
                    toks = ident_re.findall(clean[pos:])
                    for active_ns, _d in stack:
                        if active_ns:
                            ns_syms.setdefault(active_ns, set()).update(toks)
                break
            nxt = min(candidates)
            seg = clean[pos:nxt]
            if seg and stack:
                toks = ident_re.findall(seg)
                for active_ns, _d in stack:
                    if active_ns:
                        ns_syms.setdefault(active_ns, set()).update(toks)
            if m_ns and m_ns.end() - 1 == nxt:
                parts = [p for p in m_ns.group(1).split("::") if p]
                depth += 1
                for p_ns in parts:
                    stack.append((p_ns, depth))
                pos = nxt + 1
            elif nxt == next_l:
                depth += 1
                pos = nxt + 1
            else:
                while stack and stack[-1][1] == depth:
                    stack.pop()
                depth = max(0, depth - 1)
                pos = nxt + 1
    return ns_syms, all_syms


def verify_yaml_cpp_and_ids(failures: list[str]) -> None:
    """Statically verify C++ namespace symbols, hal_* calls, delimiter balance, and id()/script references in YAML."""
    ns_syms, all_syms = _collect_namespace_symbols()
    qual_re = re.compile(r"\b(latter(?:_[a-z0-9_]+)?)::([A-Za-z_][A-Za-z0-9_]*)")
    hal_re = re.compile(r"\b(hal_[A-Za-z0-9_]+)\s*\(")

    for folder in ("modules", "guis", "boards", "devices", "examples"):
        base = ROOT / folder
        if not base.is_dir():
            continue
        for yf in iter_repo_files(base):
            if yf.suffix.lower() not in {".yaml", ".yml"}:
                continue
            rel = yf.relative_to(ROOT).as_posix()
            raw_txt = yf.read_text(encoding="utf-8")
            yclean = _strip_yaml_comments_keep_lines(raw_txt)
            masked = _strip_cpp_strings_and_comments(yclean)
            for line_no, line in enumerate(masked.splitlines(), 1):
                for m_q in qual_re.finditer(line):
                    ns, sym = m_q.group(1), m_q.group(2)
                    known = ns_syms.get(ns, set())
                    if sym not in known:
                        hint_list = difflib.get_close_matches(sym, sorted(known), n=1, cutoff=0.6)
                        hint = f" (did you mean '{ns}::{hint_list[0]}'?)" if hint_list else ""
                        failures.append(f"[YAML CPP SYMBOL] {rel}:{line_no} undeclared '{ns}::{sym}'{hint}")
                for m_h in hal_re.finditer(line):
                    hsym = m_h.group(1)
                    if hsym not in all_syms:
                        failures.append(f"[YAML CPP SYMBOL] {rel}:{line_no} undeclared HAL helper '{hsym}'")

            # Check balanced (), {}, [] inside multi-line lambda blocks
            lines = raw_txt.splitlines()
            idx = 0
            while idx < len(lines):
                m_lam = re.match(r"^(\s*)(?:-\s*)?(?:\w+\s*:\s*!lambda\s*|lambda\s*:\s*)\|-?\s*$", lines[idx])
                if not m_lam:
                    idx += 1
                    continue
                base_ind = len(lines[idx]) - len(lines[idx].lstrip(" "))
                start_ln = idx + 1
                body_lines: list[str] = []
                idx += 1
                while idx < len(lines):
                    ln = lines[idx]
                    if ln.strip() and not ln.lstrip().startswith("#") and (len(ln) - len(ln.lstrip(" "))) <= base_ind:
                        break
                    body_lines.append(ln)
                    idx += 1
                clean_body = _strip_cpp_strings_and_comments("\n".join(body_lines))
                pairs = {"(": ")", "{": "}", "[": "]"}
                closing = {")": "(", "}": "{", "]": "["}
                bstack: list[str] = []
                unbalanced = False
                for ch in clean_body:
                    if ch in pairs:
                        bstack.append(ch)
                    elif ch in closing:
                        if not bstack or bstack[-1] != closing[ch]:
                            unbalanced = True
                            break
                        bstack.pop()
                if unbalanced or bstack:
                    failures.append(f"[YAML LAMBDA SYNTAX] {rel}:{start_ln} has unbalanced C++ delimiters (), {{}}, or []")

    # Verify every id(...) and script.execute/stop/wait in merged device/example include chains
    targets = (
        sorted((ROOT / "devices").glob("*.yaml"))
        + sorted((ROOT / "examples").glob("*.yaml"))
        + sorted((ROOT / "examples" / "bringup").glob("*.yaml"))
    )
    reported_ids: set[tuple[str, str, str]] = set()
    for target in targets:
        chain = collect_yaml_include_chain(target)
        merged = "\n".join(_strip_yaml_comments(p.read_text(encoding="utf-8")) for p in chain)
        defined_ids = set(re.findall(r"(?m)^\s*-?\s*id\s*:\s*(?:!extend\s+|!remove\s+)?([A-Za-z_][A-Za-z0-9_]*)\b", merged))
        for p in chain:
            prel = p.relative_to(ROOT).as_posix()
            pclean = _strip_cpp_strings_and_comments(_strip_yaml_comments_keep_lines(p.read_text(encoding="utf-8")))
            for m_id in re.finditer(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", pclean):
                ident = m_id.group(1)
                if ident not in defined_ids:
                    key = (target.name, prel, ident)
                    if key not in reported_ids:
                        reported_ids.add(key)
                        failures.append(f"[DANGLING YAML ID] {prel} (in {target.name}) references undefined id({ident})")
            for m_sc in re.finditer(r"(?m)^\s*-?\s*script\.(?:execute|stop|wait)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*$", pclean):
                s_id = m_sc.group(1)
                if s_id not in defined_ids:
                    key = (target.name, prel, s_id)
                    if key not in reported_ids:
                        reported_ids.add(key)
                        failures.append(f"[DANGLING SCRIPT ID] {prel} (in {target.name}) calls undefined script '{s_id}'")


def _write_yaml_lambdas_syntax_harness(out_cpp: Path) -> int:
    """Generate a host C++20 translation unit compiling all standalone-header YAML action lambdas."""
    ns_syms, _ = _collect_namespace_symbols()
    host_ns_syms, _ = _collect_namespace_symbols([ROOT / "include" / h for h in HOST_LAMBDA_HEADERS])
    esp_only_ns_syms: dict[str, set[str]] = {
        ns: syms - host_ns_syms.get(ns, set()) for ns, syms in ns_syms.items()
    }
    qual_re = re.compile(r"\b(latter(?:_[a-z0-9_]+)?)::([A-Za-z_][A-Za-z0-9_]*)")
    type_map = {
        "string": "std::string",
        "int": "int",
        "bool": "bool",
        "float": "float",
        "string[]": "std::vector<std::string>",
    }
    skip_re = re.compile(
        r"\b(?:id\s*\(|esp_|i2c_|gpio_|rtc_|nvs_|App\.|LATTER_PROF_LOG|ESP_LOG|delay\s*\(|micros\s*\(|x\b)"
    )
    chunks: list[str] = [
        "#include <algorithm>",
        "#include <cmath>",
        "#include <cstdint>",
        "#include <cstdio>",
        "#include <cstring>",
        "#include <string>",
        "#include <string_view>",
        "#include <vector>",
        *[f'#include "{(ROOT / "include" / h).as_posix()}"' for h in HOST_LAMBDA_HEADERS],
        "namespace { inline uint32_t millis() { return 0U; } }",
    ]
    fn_idx = 0
    for folder in ("modules", "guis"):
        base = ROOT / folder
        if not base.is_dir():
            continue
        for yf in iter_repo_files(base):
            if yf.suffix.lower() not in {".yaml", ".yml"}:
                continue
            rel = yf.relative_to(ROOT).as_posix()
            lines = yf.read_text(encoding="utf-8").splitlines()
            active_vars: list[tuple[str, str]] = []
            in_vars = False
            vars_indent = -1
            client_hook = ""
            idx = 0
            while idx < len(lines):
                raw_ln = lines[idx]
                stripped = raw_ln.split("#", 1)[0].rstrip()
                if stripped.strip():
                    ind = len(stripped) - len(stripped.lstrip(" "))
                    if re.match(r"^\s*-\s*(?:id|action)\s*:", stripped) and ind <= 6:
                        active_vars = []
                        in_vars = False
                        client_hook = ""
                    m_ch = re.match(r"^\s*(on_client_connected|on_client_disconnected)\s*:\s*$", stripped)
                    if m_ch:
                        client_hook = m_ch.group(1)
                        active_vars = []
                        in_vars = False
                    m_vh = re.match(r"^(\s*)(?:parameters|variables)\s*:\s*$", stripped)
                    if m_vh:
                        in_vars = True
                        vars_indent = len(m_vh.group(1))
                        active_vars = []
                        idx += 1
                        continue
                    if in_vars:
                        if ind <= vars_indent:
                            in_vars = False
                        else:
                            m_kv = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_\[\]]+)\s*$", stripped)
                            if m_kv and m_kv.group(2) in type_map:
                                active_vars.append((m_kv.group(1), type_map[m_kv.group(2)]))
                m_lam = re.match(r"^(\s*)-\s*lambda\s*:\s*\|-?\s*$", raw_ln)
                if not m_lam:
                    idx += 1
                    continue
                base_ind = len(raw_ln) - len(raw_ln.lstrip(" "))
                body_start_line = idx + 2
                body: list[str] = []
                idx += 1
                while idx < len(lines):
                    ln = lines[idx]
                    if ln.strip() and not ln.lstrip().startswith("#") and (len(ln) - len(ln.lstrip(" "))) <= base_ind:
                        break
                    body.append(ln)
                    idx += 1
                btxt = "\n".join(body)
                bclean = _strip_cpp_strings_and_comments(btxt)
                if skip_re.search(bclean) or "${" in btxt:
                    continue
                if any(m_q.group(2) in esp_only_ns_syms.get(m_q.group(1), set()) for m_q in qual_re.finditer(bclean)):
                    continue
                params = list(active_vars)
                if client_hook == "on_client_connected":
                    params = [("client_info", "std::string"), ("client_address", "std::string")]
                elif client_hook == "on_client_disconnected":
                    params = [("client_info", "std::string")]
                sig = ", ".join(f"{ctype} {cname}" for cname, ctype in params)
                unused = " ".join(f"(void){cname};" for cname, _ in params)
                fn_idx += 1
                chunks.append(f"[[maybe_unused]] static void yaml_action_lambda_{fn_idx}({sig}) {{")
                if unused:
                    chunks.append(f"  {unused}")
                chunks.append(f'#line {body_start_line} "{rel}"')
                chunks.extend(body)
                chunks.append("}")
    chunks.append("int main() { return 0; }")
    out_cpp.write_text("\n".join(chunks) + "\n", encoding="utf-8")
    return fn_idx


HOST_UI_STUBS: dict[str, str] = {
    "esphome/core/defines.h": "#pragma once\n",
    "sdkconfig.h": "#pragma once\n",
    "esphome/core/version.h": (
        "#pragma once\n"
        "#define VERSION_CODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))\n"
        "#define ESPHOME_VERSION_CODE VERSION_CODE(2026, 11, 0)\n"
    ),
    "esphome/core/color.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "namespace esphome {\n"
        "struct Color {\n"
        "  union { struct { uint8_t r, g, b, w; }; uint32_t raw_32; };\n"
        "  constexpr Color() : r(0), g(0), b(0), w(0) {}\n"
        "  constexpr Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue), w(0) {}\n"
        "  constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t white)\n"
        "      : r(red), g(green), b(blue), w(white) {}\n"
        "  constexpr explicit Color(uint32_t c)\n"
        "      : r((c >> 16) & 0xFF), g((c >> 8) & 0xFF), b(c & 0xFF), w((c >> 24) & 0xFF) {}\n"
        "  bool is_on() const { return raw_32 != 0; }\n"
        "  Color fade_to_black(uint8_t) const { return *this; }\n"
        "  Color fade_to_white(uint8_t) const { return *this; }\n"
        "  Color operator*(uint8_t) const { return *this; }\n"
        "  Color operator*(float) const { return *this; }\n"
        "  Color operator+(const Color &) const { return *this; }\n"
        "  bool operator==(const Color &rhs) const { return raw_32 == rhs.raw_32; }\n"
        "  bool operator!=(const Color &rhs) const { return raw_32 != rhs.raw_32; }\n"
        "  static const Color BLACK;\n"
        "  static const Color WHITE;\n"
        "};\n"
        "inline constexpr Color Color::BLACK{0, 0, 0};\n"
        "inline constexpr Color Color::WHITE{255, 255, 255};\n"
        "}  // namespace esphome\n"
    ),
    "esphome/core/hal.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "namespace esphome {\n"
        "inline uint32_t millis() { return 0; }\n"
        "inline uint32_t micros() { return 0; }\n"
        "inline void delay(uint32_t) {}\n"
        "inline void delayMicroseconds(uint32_t) {}\n"
        "inline void yield() {}\n"
        "}  // namespace esphome\n"
        "using esphome::millis;\n"
        "using esphome::micros;\n"
    ),
    "esphome/core/helpers.h": (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "namespace esphome {\n"
        "template <typename T> class RAMAllocator {\n"
        " public:\n"
        "  enum Flags : uint8_t { NONE = 0, ALLOW_FAILURE = 1, ALLOC_EXTERNAL = 2, ALLOC_INTERNAL = 4 };\n"
        "  explicit RAMAllocator(uint8_t = NONE) {}\n"
        "  T *allocate(size_t n) { return new T[n]; }\n"
        "  void deallocate(T *p, size_t) { delete[] p; }\n"
        "};\n"
        "}  // namespace esphome\n"
    ),
    "esphome/core/log.h": (
        "#pragma once\n"
        "#define ESP_LOGD(tag, fmt, ...) ((void)0)\n"
        "#define ESP_LOGI(tag, fmt, ...) ((void)0)\n"
        "#define ESP_LOGW(tag, fmt, ...) ((void)0)\n"
        "#define ESP_LOGE(tag, fmt, ...) ((void)0)\n"
        "#define ESP_LOGV(tag, fmt, ...) ((void)0)\n"
    ),
    "esphome/core/component.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include <functional>\n"
        "namespace esphome {\n"
        "class Component {\n"
        " public:\n"
        "  virtual ~Component() = default;\n"
        "  virtual void setup() {}\n"
        "  virtual void loop() {}\n"
        "  virtual float get_setup_priority() const { return 0.0f; }\n"
        "  bool status_has_error() const { return false; }\n"
        "  void status_clear_error() {}\n"
        "  void set_timeout(const char *, uint32_t, std::function<void()> &&) {}\n"
        "  void set_timeout(uint32_t, uint32_t, std::function<void()> &&) {}\n"
        "  void cancel_timeout(const char *) {}\n"
        "  void cancel_timeout(uint32_t) {}\n"
        "};\n"
        "class PollingComponent : public Component {\n"
        " public:\n"
        "  explicit PollingComponent(uint32_t = 1000) {}\n"
        "  virtual void update() {}\n"
        "  void stop_poller() {}\n"
        "  void start_poller() {}\n"
        "  void set_update_interval(uint32_t) {}\n"
        "};\n"
        "}  // namespace esphome\n"
    ),
    "esphome/core/preferences.h": (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "namespace esphome {\n"
        "class ESPPreferenceObject {\n"
        " public:\n"
        "  template <typename T> bool load(T *) { return false; }\n"
        "  template <typename T> bool save(const T *) { return true; }\n"
        "};\n"
        "class ESPPreferences {\n"
        " public:\n"
        "  template <typename T> ESPPreferenceObject make_preference(uint32_t, bool = true) { return {}; }\n"
        "  ESPPreferenceObject make_preference(size_t, uint32_t, bool = true) { return {}; }\n"
        "  bool sync() { return true; }\n"
        "};\n"
        "extern ESPPreferences *global_preferences;\n"
        "}  // namespace esphome\n"
    ),
    "esphome/core/time.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include <ctime>\n"
        "#include <string>\n"
        "namespace esphome {\n"
        "struct ESPTime {\n"
        "  uint8_t second{0}, minute{0}, hour{0}, day_of_week{1}, day_of_month{1}, month{1};\n"
        "  uint16_t day_of_year{1}, year{2026};\n"
        "  bool is_dst{false};\n"
        "  time_t timestamp{0};\n"
        "  bool is_valid() const { return year >= 2019; }\n"
        "  std::string strftime(const char *) const { return \"\"; }\n"
        "  size_t strftime(char *b, size_t n, const char *) const { if (b && n) b[0] = 0; return 0; }\n"
        "};\n"
        "}  // namespace esphome\n"
    ),
    "esphome/components/display/display.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include \"esphome/core/color.h\"\n"
        "#include \"esphome/core/component.h\"\n"
        "namespace esphome::font { class GlyphData; class Font; }\n"
        "namespace esphome::display {\n"
        "inline constexpr Color COLOR_OFF{0, 0, 0, 0};\n"
        "inline constexpr Color COLOR_ON{255, 255, 255, 255};\n"
        "enum DisplayRotation {\n"
        "  DISPLAY_ROTATION_0_DEGREES = 0,\n"
        "  DISPLAY_ROTATION_90_DEGREES = 90,\n"
        "  DISPLAY_ROTATION_180_DEGREES = 180,\n"
        "  DISPLAY_ROTATION_270_DEGREES = 270,\n"
        "};\n"
        "enum class TextAlign {\n"
        "  TOP = 0x00, CENTER_VERTICAL = 0x01, BASELINE = 0x02, BOTTOM = 0x04,\n"
        "  LEFT = 0x00, CENTER_HORIZONTAL = 0x08, RIGHT = 0x10,\n"
        "  TOP_LEFT = TOP | LEFT, TOP_CENTER = TOP | CENTER_HORIZONTAL, TOP_RIGHT = TOP | RIGHT,\n"
        "  CENTER_LEFT = CENTER_VERTICAL | LEFT, CENTER = CENTER_VERTICAL | CENTER_HORIZONTAL,\n"
        "  CENTER_RIGHT = CENTER_VERTICAL | RIGHT,\n"
        "  BASELINE_LEFT = BASELINE | LEFT, BASELINE_CENTER = BASELINE | CENTER_HORIZONTAL,\n"
        "  BASELINE_RIGHT = BASELINE | RIGHT,\n"
        "  BOTTOM_LEFT = BOTTOM | LEFT, BOTTOM_CENTER = BOTTOM | CENTER_HORIZONTAL,\n"
        "  BOTTOM_RIGHT = BOTTOM | RIGHT,\n"
        "};\n"
        "class BaseFont {\n"
        " public:\n"
        "  virtual ~BaseFont() = default;\n"
        "  virtual void print(int, int, class Display *, Color, const char *, Color = Color::BLACK) {}\n"
        "  virtual void measure(const char *, int *w, int *xo, int *bl, int *h) {\n"
        "    if (w) *w = 0;\n"
        "    if (xo) *xo = 0;\n"
        "    if (bl) *bl = 0;\n"
        "    if (h) *h = 0;\n"
        "  }\n"
        "};\n"
        "class BaseImage {\n"
        " public:\n"
        "  virtual ~BaseImage() = default;\n"
        "  virtual int get_width() const { return 0; }\n"
        "  virtual int get_height() const { return 0; }\n"
        "  virtual const uint8_t *get_data_start() const { return nullptr; }\n"
        "};\n"
        "class Display : public PollingComponent {\n"
        " public:\n"
        "  int get_width() { return 466; }\n"
        "  int get_height() { return 466; }\n"
        "  DisplayRotation get_rotation() const { return DISPLAY_ROTATION_0_DEGREES; }\n"
        "  void draw_pixel_at(int, int, Color) {}\n"
        "  void line(int, int, int, int, Color) {}\n"
        "  void horizontal_line(int, int, int, Color) {}\n"
        "  void vertical_line(int, int, int, Color) {}\n"
        "  void rectangle(int, int, int, int, Color) {}\n"
        "  void filled_rectangle(int, int, int, int, Color) {}\n"
        "  void circle(int, int, int, Color) {}\n"
        "  void filled_circle(int, int, int, Color) {}\n"
        "  void triangle(int, int, int, int, int, int, Color) {}\n"
        "  void filled_triangle(int, int, int, int, int, int, Color) {}\n"
        "  void print(int, int, BaseFont *, Color, TextAlign, const char *, Color = Color::BLACK) {}\n"
        "  void print(int, int, BaseFont *, Color, const char *, Color = Color::BLACK) {}\n"
        "  void printf(int, int, BaseFont *, Color, TextAlign, const char *, ...) {}\n"
        "  void fill(Color) {}\n"
        "  void start_clipping(int, int, int, int) {}\n"
        "  void end_clipping() {}\n"
        " protected:\n"
        "  uint8_t *buffer_{nullptr};\n"
        "};\n"
        "}  // namespace esphome::display\n"
    ),
    "esphome/components/font/font.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include <vector>\n"
        "#include \"esphome/components/display/display.h\"\n"
        "namespace esphome::font {\n"
        "struct GlyphData {\n"
        "  const char *a_char;\n"
        "  const uint8_t *data;\n"
        "  int offset_x, offset_y, width, height, advance_width;\n"
        "};\n"
        "class Glyph {\n"
        " public:\n"
        "  const GlyphData *get_glyph_data() const { return nullptr; }\n"
        "};\n"
        "class Font : public display::BaseFont {\n"
        " public:\n"
        "  const std::vector<Glyph> &get_glyphs() const { static std::vector<Glyph> g; return g; }\n"
        "  int get_baseline() const { return 12; }\n"
        "  int get_height() const { return 16; }\n"
        "  int get_bpp() const { return 1; }\n"
        "};\n"
        "}  // namespace esphome::font\n"
    ),
    "esphome/components/api/api_server.h": (
        "#pragma once\n"
        "namespace esphome::api {\n"
        "class APIServer {\n"
        " public:\n"
        "  bool is_connected() const { return true; }\n"
        "  bool is_connected_with_state_subscription() const { return true; }\n"
        "};\n"
        "extern APIServer *global_api_server;\n"
        "}  // namespace esphome::api\n"
    ),
    "esphome/components/i2c/i2c_bus.h": (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "namespace esphome::i2c {\n"
        "enum ErrorCode { ERROR_OK = 0, ERROR_UNKNOWN = 1 };\n"
        "class I2CBus {\n"
        " public:\n"
        "  ErrorCode write_readv(uint8_t, const uint8_t *, size_t, uint8_t *, size_t) { return ERROR_OK; }\n"
        "  ErrorCode write(uint8_t, const uint8_t *, size_t, bool = true) { return ERROR_OK; }\n"
        "  ErrorCode read(uint8_t, uint8_t *, size_t) { return ERROR_OK; }\n"
        "};\n"
        "}  // namespace esphome::i2c\n"
    ),
    "esphome/components/wifi/wifi_component.h": (
        "#pragma once\n"
        "namespace esphome::wifi {\n"
        "class WiFiComponent { public: bool is_connected() { return true; } };\n"
        "extern WiFiComponent *global_wifi_component;\n"
        "}  // namespace esphome::wifi\n"
    ),
    "esp_heap_caps.h": (
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <cstdlib>\n"
        "#define MALLOC_CAP_SPIRAM (1 << 10)\n"
        "#define MALLOC_CAP_INTERNAL (1 << 11)\n"
        "#define MALLOC_CAP_8BIT (1 << 2)\n"
        "#define MALLOC_CAP_DMA (1 << 3)\n"
        "inline void *heap_caps_malloc(size_t sz, uint32_t) { return std::malloc(sz); }\n"
        "inline void *heap_caps_calloc(size_t n, size_t sz, uint32_t) { return std::calloc(n, sz); }\n"
        "inline void heap_caps_free(void *p) { std::free(p); }\n"
        "inline size_t heap_caps_get_free_size(uint32_t) { return 0; }\n"
        "inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 0; }\n"
        "inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }\n"
    ),
    "esp_memory_utils.h": (
        "#pragma once\n"
        "inline bool esp_ptr_external_ram(const void *) { return false; }\n"
        "inline bool esp_ptr_internal(const void *) { return true; }\n"
    ),
    "esp_timer.h": "#pragma once\n#include <cstdint>\ninline int64_t esp_timer_get_time() { return 0; }\n",
    "esp_random.h": "#pragma once\n#include <cstdint>\ninline uint32_t esp_random() { return 42; }\n",
    "esp_sleep.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "typedef int esp_sleep_wakeup_cause_t;\n"
        "#define ESP_SLEEP_WAKEUP_UNDEFINED 0\n"
        "inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return 0; }\n"
    ),
    "esp_wifi.h": (
        "#pragma once\n"
        "typedef int wifi_ps_type_t;\n"
        "#define WIFI_PS_NONE 0\n"
        "#define WIFI_PS_MIN_MODEM 1\n"
        "inline int esp_wifi_set_ps(wifi_ps_type_t) { return 0; }\n"
    ),
    "esp_event.h": "#pragma once\n",
    "esp_attr.h": "#pragma once\n#ifndef IRAM_ATTR\n#define IRAM_ATTR\n#endif\n",
    "freertos/FreeRTOS.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "typedef uint32_t TickType_t;\n"
        "#define pdMS_TO_TICKS(x) (x)\n"
    ),
    "freertos/task.h": (
        "#pragma once\n"
        "#include \"freertos/FreeRTOS.h\"\n"
        "inline void vTaskDelay(TickType_t) {}\n"
        "inline int xPortGetCoreID() { return 0; }\n"
    ),
    "driver/gpio.h": (
        "#pragma once\n"
        "#include <cstdint>\n"
        "typedef int gpio_num_t;\n"
        "#define GPIO_NUM_NC (-1)\n"
        "inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }\n"
    ),
    "driver/ledc.h": "#pragma once\n",
}


def _write_host_ui_stubs(stub_dir: Path) -> None:
    for rel, content in HOST_UI_STUBS.items():
        fp = stub_dir / rel
        fp.parent.mkdir(parents=True, exist_ok=True)
        fp.write_text(content, encoding="utf-8")


YAML_FULL_LAMBDA_PREAMBLE = """\
#ifndef LATTER_CHIP_P4
#define LATTER_CHIP_P4 0
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/time.h"
#include "esphome/components/font/font.h"
#include "esphome/components/api/api_server.h"
#include "esp_heap_caps.h"
#include "esphome_bridge.h"
#include "hal_manifest.h"
#include "hal_battery.h"
#include "hal_imu_qmi8658.h"
#include "hal_link_report.h"
#include "hal_panel_power.h"
#include "hal_power_pm.h"
#include "audio_router.h"
#include "latter_prof.h"
#include "latter_protocol.h"
#include "latter_rpc.h"
#include "latter_state.h"
#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using std::isnan;
using std::to_string;
using esphome::Color;

inline size_t esp_get_free_heap_size() { return 0; }
struct AppStub { void feed_wdt() {} void safe_reboot() {} };
inline AppStub App;

namespace latter_ui {
template <typename... Args> inline void hal_set_touch_sleep_mode(Args &&...) {}
}  // namespace latter_ui

namespace media_player {
enum MediaPlayerState {
  MEDIA_PLAYER_STATE_NONE = 0,
  MEDIA_PLAYER_STATE_IDLE = 1,
  MEDIA_PLAYER_STATE_PLAYING = 2,
  MEDIA_PLAYER_STATE_PAUSED = 3,
  MEDIA_PLAYER_STATE_ANNOUNCING = 4,
};
enum MediaPlayerCommand {
  MEDIA_PLAYER_COMMAND_PLAY = 0,
  MEDIA_PLAYER_COMMAND_PAUSE = 1,
  MEDIA_PLAYER_COMMAND_STOP = 2,
};
}  // namespace media_player

namespace microphone {
struct StreamInfo { int get_bits_per_sample() const { return 16; } };
struct AudioCapture { StreamInfo stream_info; std::vector<uint8_t> data; };
}  // namespace microphone

namespace esphome::voice_assistant {
template <typename... Args> inline void on_api_client_authenticated(Args &&...) {}
template <typename... Args> inline void abort_voice_assistant(Args &&...) {}
template <typename... Args> inline void cycle_primary_wake_word(Args &&...) {}
template <typename... Args> inline void start_continuous(Args &&...) {}
template <typename... Args> inline void request_start(Args &&...) {}
}  // namespace esphome::voice_assistant

struct AnyCall {
  template <typename... Args> AnyCall &set_volume(Args &&...) { return *this; }
  template <typename... Args> AnyCall &set_brightness(Args &&...) { return *this; }
  template <typename... Args> AnyCall &set_command(Args &&...) { return *this; }
  template <typename... Args> AnyCall &set_transition_length(Args &&...) { return *this; }
  template <typename... Args> AnyCall &set_media_url(Args &&...) { return *this; }
  void perform() {}
};

struct AnyValues {
  bool is_on() const { return true; }
  float get_brightness() const { return 1.0f; }
};

struct AnyStateVal {
  operator bool() const { return false; }
  bool operator==(media_player::MediaPlayerState) const { return false; }
  bool operator!=(media_player::MediaPlayerState) const { return true; }
};

struct AnyId : esphome::display::Display, esphome::display::BaseImage, esphome::font::Font {
  AnyStateVal state{};
  float volume{0.5f};
  AnyValues current_values{};
  AnyValues remote_values{};
  operator esphome::display::BaseFont *() { return static_cast<esphome::font::Font *>(this); }
  operator esphome::display::BaseImage *() { return static_cast<esphome::display::BaseImage *>(this); }
  operator esphome::voice_assistant::VoiceAssistant *() { return nullptr; }
  operator esphome::micro_wake_word::MicroWakeWord *() { return nullptr; }
  template <typename... Args> void execute(Args &&...) {}
  void stop() {}
  void press() {}
  void update() {}
  template <typename... Args> void publish_state(Args &&...) {}
  bool is_running() const { return false; }
  bool is_active() const { return false; }
  bool is_connected() const { return true; }
  void set_profile_logging(bool) {}
  void set_uuid_hex(std::string_view) {}
  void set_volume(float) {}
  void set_mute_state(bool) {}
  bool get_mute_state() const { return false; }
  bool has_buffered_data() const { return false; }
  template <typename... Args> void apply_ducking(Args &&...) {}
  void set_brightness(uint8_t) {}
  bool is_failed() const { return false; }
  void reset_to_construction_state() {}
  void call() {}
  void disable_loop() {}
  void enable_loop() {}
  void release() {}
  void enable() {}
  void disable() {}
  void release_image() {}
  esphome::ESPTime now() const { return {}; }
  AnyCall make_call() { return {}; }
  AnyCall turn_on() { return {}; }
  AnyCall turn_off() { return {}; }
  template <typename F> void add_data_callback(F &&cb) {
    microphone::AudioCapture cap;
    cb(cap);
  }
  int get_width() const override { return 0; }
  int get_height() const override { return 0; }
};

inline AnyId &any_id_singleton() { static AnyId inst; return inst; }
inline Color &any_color_singleton() { static Color c; return c; }
struct AnyStrId {
  std::string state;
  bool has_state() const { return true; }
  template <typename... A> void publish_state(A &&...) {}
};
struct AnyFltId {
  float state{0.0f};
  bool has_state() const { return true; }
  template <typename... A> void publish_state(A &&...) {}
};
inline AnyStrId &any_str_id_singleton() { static AnyStrId s; return s; }
inline AnyFltId &any_flt_id_singleton() { static AnyFltId s; return s; }
struct TouchPointStub { int x{0}, y{0}, state{1}; };
"""


def _write_yaml_full_lambdas_syntax_harness(out_cpp: Path) -> int:
    """Generate a host C++20 TU compiling all block & inline YAML lambdas (including id(...) calls)."""
    yaml_files = (
        ROOT / "modules" / "base.yaml",
        ROOT / "modules" / "display_pipeline.yaml",
        ROOT / "modules" / "power_manager.yaml",
        ROOT / "modules" / "system_core.yaml",
        ROOT / "modules" / "timers_and_fetchers.yaml",
        ROOT / "guis" / "universal" / "gui.yaml",
    )
    type_map = {
        "string": "std::string",
        "int": "int",
        "bool": "bool",
        "float": "float",
        "string[]": "std::vector<std::string>",
    }
    str_ids = {"track_title", "track_artist", "track_album", "latter_ip_address", "fw_release_summary"}
    flt_ids = {"latter_wifi_signal"}
    ptr_ids = {
        "remote_album_art",
        "home_wallpaper_art",
        "camera_snapshot_art",
        "album_art",
        "touch_screen",
        "screensaver_anim_tick",
        "ui_anim_tick",
        "ble_ctrl",
        "wifi_ctrl",
        "disp1",
    }

    def sanitize_cpp(code: str) -> str:
        code = re.sub(r"\$\{[A-Za-z0-9_]+\}", "0", code)
        code = re.sub(
            r"(?<!&)&(?!&)\s*id\(\s*(theme_primary|theme_secondary|col_[A-Za-z0-9_]*)\s*\)",
            "(&any_color_singleton())",
            code,
        )
        code = re.sub(
            r"(?<!&)&(?!&)\s*id\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
            "(&any_id_singleton())",
            code,
        )

        def repl_dot(m: re.Match[str]) -> str:
            nm = m.group(1)
            if nm in str_ids:
                return "any_str_id_singleton()."
            if nm in flt_ids:
                return "any_flt_id_singleton()."
            return "any_id_singleton()."

        code = re.sub(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\.", repl_dot, code)

        def repl_bare(m: re.Match[str]) -> str:
            nm = m.group(1)
            if nm in ("theme_primary", "theme_secondary") or nm.startswith("col_"):
                return "any_color_singleton()"
            if nm in ptr_ids:
                return "(&any_id_singleton())"
            return "any_id_singleton()"

        return re.sub(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", repl_bare, code)

    chunks = [YAML_FULL_LAMBDA_PREAMBLE]
    fn_idx = 0
    for yf in yaml_files:
        if not yf.is_file():
            continue
        rel = yf.relative_to(ROOT).as_posix()
        lines = yf.read_text(encoding="utf-8").splitlines()
        active_vars: list[tuple[str, str]] = []
        in_vars = False
        vars_indent = -1
        client_hook = ""
        on_progress = False
        on_touch = ""
        in_write_action = False
        idx = 0
        while idx < len(lines):
            raw_ln = lines[idx]
            stripped = raw_ln.split("#", 1)[0].rstrip()
            if stripped.strip():
                ind = len(stripped) - len(stripped.lstrip(" "))
                if re.match(r"^\s*-\s*(?:id|action|platform|interval)\s*:", stripped) and ind <= 6:
                    active_vars = []
                    in_vars = False
                    client_hook = ""
                    on_progress = False
                    on_touch = ""
                    in_write_action = False
                if re.match(r"^\s*write_action\s*:\s*$", stripped):
                    in_write_action = True
                if re.match(r"^\s*on_progress\s*:\s*$", stripped):
                    on_progress = True
                elif re.match(r"^\s*on_(?:begin|end|error|abort)\s*:\s*$", stripped):
                    on_progress = False
                m_ot = re.match(r"^\s*(on_touch|on_update|on_release)\s*:\s*$", stripped)
                if m_ot and rel.endswith("gui.yaml"):
                    on_touch = m_ot.group(1)
                m_ch = re.match(r"^\s*(on_client_connected|on_client_disconnected)\s*:\s*$", stripped)
                if m_ch:
                    client_hook = m_ch.group(1)
                    active_vars = []
                    in_vars = False
                m_vh = re.match(r"^(\s*)(?:parameters|variables)\s*:\s*$", stripped)
                if m_vh:
                    in_vars = True
                    vars_indent = len(m_vh.group(1))
                    active_vars = []
                    idx += 1
                    continue
                if in_vars:
                    if ind <= vars_indent:
                        in_vars = False
                    else:
                        m_kv = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_\[\]]+)\s*$", stripped)
                        if m_kv and m_kv.group(2) in type_map:
                            active_vars.append((m_kv.group(1), type_map[m_kv.group(2)]))

            m_block = re.match(r"^(\s*)(?:-\s*)?lambda\s*:\s*\|-?\s*$", raw_ln)
            if m_block:
                is_cond = not raw_ln.lstrip().startswith("-")
                base_ind = len(raw_ln) - len(raw_ln.lstrip(" "))
                body_start = idx + 2
                body: list[str] = []
                idx += 1
                while idx < len(lines):
                    ln = lines[idx]
                    if ln.strip() and (len(ln) - len(ln.lstrip(" "))) <= base_ind:
                        break
                    body.append(ln)
                    idx += 1
                btxt = sanitize_cpp("\n".join(body))
                params = list(active_vars)
                if rel.endswith("gui.yaml") and on_touch == "on_touch":
                    params = [("touch", "TouchPointStub"), ("touches", "std::vector<TouchPointStub>")]
                elif rel.endswith("gui.yaml") and on_touch == "on_update":
                    params = [("touches", "std::vector<TouchPointStub>")]
                elif rel.endswith("gui.yaml") and not on_touch:
                    params = [("it", "esphome::display::Display &")]
                elif in_write_action:
                    params = [("state", "float")]
                elif client_hook in ("on_client_connected", "on_client_disconnected"):
                    params = [("client_info", "std::string"), ("client_address", "std::string")]
                elif on_progress:
                    params = [("x", "float")]
                sig = ", ".join(f"{ctype} {cname}" for cname, ctype in params)
                ret = "auto" if is_cond else "void"
                fn_idx += 1
                chunks.append(f"[[maybe_unused]] static {ret} yaml_full_fn_{fn_idx}({sig}) {{")
                chunks.append(f'#line {body_start} "{rel}"')
                chunks.append(btxt)
                chunks.append("}")
                continue

            m_inline = re.match(
                r"""^(\s*)(?:-\s*)?(?:lambda\s*:\s*|[A-Za-z0-9_]+\s*:\s*!lambda\s+)(['"])(.*)\2\s*$""",
                stripped,
            )
            if m_inline:
                is_ret = "return " in m_inline.group(3)
                btxt = sanitize_cpp(m_inline.group(3))
                params = list(active_vars)
                if client_hook in ("on_client_connected", "on_client_disconnected"):
                    params = [("client_info", "std::string"), ("client_address", "std::string")]
                elif on_progress:
                    params = [("x", "float")]
                sig = ", ".join(f"{ctype} {cname}" for cname, ctype in params)
                ret = "auto" if is_ret else "void"
                fn_idx += 1
                chunks.append(f"[[maybe_unused]] static {ret} yaml_full_fn_{fn_idx}({sig}) {{")
                chunks.append(f'#line {idx + 1} "{rel}"')
                chunks.append(btxt)
                chunks.append("}")

            idx += 1
    chunks.append("int main() { return 0; }")
    out_cpp.write_text("\n".join(chunks) + "\n", encoding="utf-8")
    return fn_idx


def run_subprocess_gates(failures: list[str]) -> None:
    """Run protocol, board-facts, bringup, sound checks and compile/run C++20 host unit tests."""
    py_tools = (
        ("PROTOCOL CODEGEN", ["tools/gen_protocol.py", "--check"]),
        ("PROTOCOL USAGE", ["tools/check_protocol_usage.py"]),
        ("BOARD FACTS", ["tools/check_boards.py", "--check-docs"]),
        ("BRINGUP PROBES", ["tools/new_board.py", "--bringup", "--check"]),
        ("SOUND FILES", ["tools/gen_sounds.py", "--check"]),
    )
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    for tag, cmd in py_tools:
        proc = subprocess.run([sys.executable, *cmd], cwd=ROOT, env=env, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or f"{cmd[0]} exited {proc.returncode}").strip()
            failures.append(f"[{tag}] {detail}")

    test_cpp = ROOT / "tests" / "host" / "test_all.cpp"
    if not test_cpp.is_file():
        failures.append("[HOST TESTS] tests/host/test_all.cpp missing")
        return

    ui_cpps = sorted((ROOT / "components" / "latter_ui").glob("*.cpp"))
    ui_trait_matrices = (
        (
            "ROUND_BATT",
            (
                "LATTER_SCREEN_W=466",
                "LATTER_SCREEN_H=466",
                "LATTER_ROUND_DISPLAY=1",
                "LATTER_HAS_BATTERY=1",
                "LATTER_HAS_IMU=1",
                "LATTER_IS_STATIONARY=0",
            ),
        ),
        (
            "RECT_WALL",
            (
                "LATTER_SCREEN_W=480",
                "LATTER_SCREEN_H=480",
                "LATTER_ROUND_DISPLAY=0",
                "LATTER_HAS_BATTERY=0",
                "LATTER_HAS_IMU=0",
                "LATTER_IS_STATIONARY=1",
            ),
        ),
    )

    gpp = shutil.which("g++") or shutil.which("clang++")
    if gpp:
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            bin_path = td_path / ("latter_host_test.exe" if os.name == "nt" else "latter_host_test")
            comp = subprocess.run(
                [gpp, "-std=c++20", "-O2", "-Wall", "-Wextra", str(test_cpp), "-o", str(bin_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            if comp.returncode != 0:
                failures.append(f"[HOST TESTS COMPILE] {(comp.stderr or comp.stdout).strip()}")
            else:
                run = subprocess.run([str(bin_path)], capture_output=True, text=True, check=False)
                if run.returncode != 0:
                    failures.append(f"[HOST TESTS RUN] {(run.stderr or run.stdout).strip()}")
            lam_cpp = td_path / "yaml_lambdas_check.cpp"
            if _write_yaml_lambdas_syntax_harness(lam_cpp) > 0:
                lam_comp = subprocess.run(
                    [gpp, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", str(lam_cpp)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if lam_comp.returncode != 0:
                    failures.append(f"[YAML LAMBDA COMPILE] {(lam_comp.stderr or lam_comp.stdout).strip()}")
            if ui_cpps:
                stub_dir = td_path / "ui_stubs"
                _write_host_ui_stubs(stub_dir)
                full_lam_cpp = td_path / "yaml_full_lambdas_check.cpp"
                extra_tus = [full_lam_cpp] if _write_yaml_full_lambdas_syntax_harness(full_lam_cpp) > 0 else []
                for cfg_lbl, defs in ui_trait_matrices:
                    ui_comp = subprocess.run(
                        [
                            gpp,
                            "-std=c++20",
                            "-fsyntax-only",
                            f"-I{ROOT / 'include'}",
                            f"-I{stub_dir}",
                            *[f"-D{d}" for d in defs],
                            *[str(p) for p in (*ui_cpps, *extra_tus)],
                        ],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    if ui_comp.returncode != 0:
                        failures.append(
                            f"[UI CPP SYNTAX ({cfg_lbl})] {(ui_comp.stderr or ui_comp.stdout).strip()}"
                        )
    elif os.name == "nt":
        vcvars_candidates = [
            Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
            Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"),
            Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
        ]
        vcvars = next((p for p in vcvars_candidates if p.is_file()), None)
        if vcvars is not None:
            with tempfile.TemporaryDirectory() as td:
                td_path = Path(td)
                exe_path = td_path / "latter_host_test.exe"
                obj_path = td_path / "latter_host_test.obj"
                lam_cpp = td_path / "yaml_lambdas_check.cpp"
                has_lam = _write_yaml_lambdas_syntax_harness(lam_cpp) > 0
                bat_lines = [
                    "@echo off",
                    f'call "{vcvars}" >nul',
                    "if errorlevel 1 exit /b 1",
                    f'cl /nologo /utf-8 /std:c++20 /O2 /W4 /EHsc "{test_cpp}" "/Fe:{exe_path}" "/Fo:{obj_path}"',
                    "if errorlevel 1 exit /b 1",
                    f'"{exe_path}"',
                    "if errorlevel 1 exit /b 1",
                ]
                if has_lam:
                    bat_lines.extend(
                        [
                            f'cl /nologo /utf-8 /std:c++20 /Zs /W4 /EHsc "{lam_cpp}"',
                            "if errorlevel 1 exit /b 1",
                        ]
                    )
                if ui_cpps:
                    stub_dir = td_path / "ui_stubs"
                    _write_host_ui_stubs(stub_dir)
                    full_lam_cpp = td_path / "yaml_full_lambdas_check.cpp"
                    extra_tus = [full_lam_cpp] if _write_yaml_full_lambdas_syntax_harness(full_lam_cpp) > 0 else []
                    ui_cpp_args = " ".join(f'"{p}"' for p in (*ui_cpps, *extra_tus))
                    for _, defs in ui_trait_matrices:
                        def_args = " ".join(f"/D{d}" for d in defs)
                        bat_lines.extend(
                            [
                                f'cl /nologo /utf-8 /std:c++20 /Zs /MP /EHsc /I"{ROOT / "include"}" '
                                f'/I"{stub_dir}" {def_args} {ui_cpp_args}',
                                "if errorlevel 1 exit /b 1",
                            ]
                        )
                bat_path = td_path / "run_host_tests.cmd"
                bat_path.write_text("\r\n".join(bat_lines) + "\r\n", encoding="utf-8")
                res = subprocess.run(["cmd.exe", "/c", str(bat_path)], capture_output=True, text=True, check=False)
                if res.returncode != 0:
                    failures.append(f"[HOST TESTS MSVC] {(res.stderr or res.stdout).strip()}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fast", action="store_true", help="Run fast static/AST/semantic checks only.")
    args = ap.parse_args(argv)
    fast_mode = args.fast or os.environ.get("LATTER_VERIFY_FAST") == "1"

    failures: list[str] = []

    # 1. UTF-8 no-BOM, JSON validity, LF shell scripts, and 32-byte Base64 Noise keys
    for path in iter_repo_files(ROOT):
        if path.suffix.lower() not in TEXT_EXTS:
            continue
        raw = path.read_bytes()
        rel = path.relative_to(ROOT).as_posix()
        if raw.startswith(b"\xef\xbb\xbf"):
            failures.append(f"[UTF8 NO-BOM] {rel} starts with a UTF-8 BOM")
        try:
            txt = raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            failures.append(f"[UTF8] {rel} is not valid UTF-8: {exc}")
            continue
        if path.suffix == ".sh" and b"\r" in raw:
            failures.append(f"[SHELL LF] {rel} contains CRLF line endings (must be LF)")
        if path.suffix == ".json":
            try:
                json.loads(txt)
            except ValueError as exc:
                failures.append(f"[JSON] {rel} failed JSON parse: {exc}")
        if path.suffix.lower() == ".md":
            no_comments = re.sub(r"<!--.*?-->", "", txt, flags=re.DOTALL)
            in_fence = False
            for line_no, raw_md in enumerate(no_comments.splitlines(), 1):
                if re.match(r"^\s*`{3,}|^\s*~{3,}", raw_md):
                    in_fence = not in_fence
                    continue
                if in_fence:
                    continue
                scrubbed = re.sub(r"`[^`]*`", "", raw_md)
                for m_lnk in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)", scrubbed):
                    href = m_lnk.group(1).strip()
                    if not href or href.startswith(("#", "http://", "https://", "mailto:", "file://")):
                        continue
                    rel_target = href.split("#", 1)[0].split("?", 1)[0]
                    if not rel_target:
                        continue
                    cand_target = (path.parent / rel_target).resolve()
                    if not cand_target.exists():
                        failures.append(f"[MD LINK] {rel}:{line_no} broken relative link '{href}'")
        if path.suffix.lower() in {".yaml", ".yml"}:
            in_enc = False
            enc_indent = -1
            for line_no, raw_line in enumerate(txt.splitlines(), 1):
                stripped = raw_line.split("#", 1)[0].rstrip()
                if not stripped.strip():
                    continue
                indent = len(stripped) - len(stripped.lstrip(" "))
                m_enc = re.match(r"^(\s*)encryption\s*:\s*$", stripped)
                if m_enc:
                    in_enc = True
                    enc_indent = len(m_enc.group(1))
                    continue
                if in_enc:
                    if indent <= enc_indent:
                        in_enc = False
                    else:
                        m_key = re.match(r"^\s*key\s*:\s*(.+?)\s*$", stripped)
                        if m_key:
                            val = m_key.group(1).strip().strip('"').strip("'")
                            if not val.startswith("${") and not val.startswith("!secret"):
                                try:
                                    decoded_key = base64.b64decode(val, validate=True)
                                except ValueError as exc:
                                    failures.append(
                                        f"[API KEY] {rel}:{line_no} encryption key is not valid base64: {exc}"
                                    )
                                else:
                                    if len(decoded_key) != 32:
                                        failures.append(
                                            f"[API KEY] {rel}:{line_no} encryption key decodes to "
                                            f"{len(decoded_key)} bytes (must be 32)"
                                        )

    # 2. Retired paths & AGENTS.md check
    for rel_retired in RETIRED_PATHS:
        if (ROOT / rel_retired).exists():
            failures.append(f"[RETIRED PATH] {rel_retired} must not exist in consolidated v3.0 SDK")

    agents_md = ROOT / "AGENTS.md"
    if not agents_md.is_file():
        failures.append("[AGENTS MD] AGENTS.md is missing")
    else:
        atxt = agents_md.read_text(encoding="utf-8")
        if atxt.startswith("---"):
            failures.append("[AGENTS MD] Standalone AGENTS.md must not start with YAML frontmatter")
        if agents_md.stat().st_size >= 12000:
            failures.append(f"[AGENTS MD] AGENTS.md ({agents_md.stat().st_size} B) exceeds 12000 B cap")

    # 3. Zero YAML globals, zero dead substitutions, zero dangling HA entity sensors, and terminology checks
    for folder in ("components", "include", "modules", "guis", "boards", "devices", "examples"):
        base = ROOT / folder
        for path in iter_repo_files(base):
            if path.suffix.lower() not in TEXT_EXTS:
                continue
            rel = path.relative_to(ROOT).as_posix()
            txt = path.read_text(encoding="utf-8")
            if folder != "examples":
                m = BANNED_TERMS_RE.search(txt)
                if m:
                    failures.append(f"[TERMINOLOGY] {rel} contains banned term {m.group(0)!r}")
            if folder in ("include", "components") and path.suffix.lower() in {".h", ".hpp", ".cpp"}:
                clean_cpp = _strip_cpp_comments(txt)
                m_slug = DEVICE_SLUG_RE.search(clean_cpp)
                if m_slug:
                    failures.append(
                        f"[TRAITS NOT NAMES] {rel} hardcodes product slug {m_slug.group(0)!r} (use LATTER_* traits)"
                    )
            if path.suffix.lower() in {".yaml", ".yml"}:
                yclean = _strip_yaml_comments(txt)
                if re.search(r"(?m)^globals\s*:", yclean):
                    failures.append(f"[ZERO YAML GLOBALS] {rel} declares forbidden YAML globals: block")
                if re.search(r"(?m)^\s+(?:screen_w|screen_h|art_shape)\s*:", yclean):
                    failures.append(f"[DEAD SUBSTITUTION] {rel} declares retired screen_w/screen_h/art_shape substitution")
                # Dangling HA sensors: only `time: - platform: homeassistant` is allowed; never sensor/text_sensor/binary_sensor
                for sec in ("sensor", "text_sensor", "binary_sensor"):
                    m_sec = re.search(rf"(?m)^{sec}\s*:\s*\n((?:[ \t]+.*\n?)*)", yclean)
                    if m_sec and re.search(r"(?m)^\s*-\s*platform:\s*homeassistant\b", m_sec.group(1)):
                        failures.append(
                            f"[DANGLING HA SENSOR] {rel} subscribes {sec} via platform: homeassistant (use Protocol v3)"
                        )

    # Descriptive option labels (D-020) & Power Profile options in C++ preference tables
    state_h_txt = (ROOT / "include" / "latter_state.h").read_text(encoding="utf-8")
    power_h_txt = (ROOT / "include" / "hal_power_pm.h").read_text(encoding="utf-8")
    combined_opts = state_h_txt + "\n" + power_h_txt
    for req_opt in (
        "Minimal Digital",
        "Modern Roman (Analog)",
        "Modern Digital (Bold)",
        "Nebula",
        "Solaris",
        "Rings",
        "Light Bar",
        "Dot Wave",
        "Ambient Clock",
        "Aurora",
        "Starfield",
        "Digital Rain",
        "Fireworks",
        "Portable Carry",
        "Handheld Roaming",
        "Wearable Watch",
        "Tabletop Dock",
        "Always-On Wall / Desk",
    ):
        if f'"{req_opt}"' not in combined_opts:
            failures.append(f"[PRODUCT OPTIONS] include/latter_state.h + hal_power_pm.h missing option {req_opt!r}")

    # 4. SDK & YAML revision lockstep + ESPHome version static_assert
    sdk_revs: list[int] = []
    yaml_revs: list[int] = []
    for path in iter_repo_files(ROOT / "include") + iter_repo_files(ROOT / "components") + iter_repo_files(ROOT / "modules"):
        if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".yaml"}:
            continue
        txt = path.read_text(encoding="utf-8")
        for m in re.finditer(r"(?m)^\s*#\s*define\s+LATTER_SDK_REV\s+(\d+)L?\b", txt):
            sdk_revs.append(int(m.group(1)))
        for m in re.finditer(r"constexpr\s+long\s+LATTER_YAML_REV\s*=\s*(\d+)L?\s*;", txt):
            yaml_revs.append(int(m.group(1)))
    if not sdk_revs or not yaml_revs or set(sdk_revs) != set(yaml_revs):
        failures.append(
            f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs}) and LATTER_YAML_REV ({yaml_revs}) must exist and match"
        )
    elif min(sdk_revs) < 2026111039:
        failures.append(f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs[0]}) must be >= 2026111039")

    bridge_h = ROOT / "include" / "esphome_bridge.h"
    if not bridge_h.is_file() or "VERSION_CODE(2026, 11, 0)" not in bridge_h.read_text(encoding="utf-8"):
        failures.append("[ESPHOME PIN LOCKSTEP] include/esphome_bridge.h must static_assert VERSION_CODE(2026, 11, 0)")

    # 5. Version lockstep across base.yaml, devices/*.yaml, examples/*.yaml, index.html
    base_txt = (ROOT / "modules" / "base.yaml").read_text(encoding="utf-8")
    m_ver = re.search(r'(?m)^\s*version:\s*"([^"]+)"', base_txt)
    if not m_ver:
        failures.append("[VERSION LOCKSTEP] modules/base.yaml missing version substitution")
    else:
        core_ver = m_ver.group(1)
        for path in sorted((ROOT / "devices").glob("*.yaml")) + sorted((ROOT / "examples").glob("*.yaml")):
            ptxt = path.read_text(encoding="utf-8")
            pm = re.search(r'(?m)^\s*version:\s*"([^"]+)"', ptxt)
            if not pm or pm.group(1) != core_ver:
                failures.append(
                    f"[VERSION LOCKSTEP] {path.relative_to(ROOT).as_posix()} version "
                    f"({pm.group(1) if pm else 'missing'}) != modules/base.yaml ({core_ver})"
                )
        idx_txt = (ROOT / "index.html").read_text(encoding="utf-8")
        if f">v{core_ver}<" not in idx_txt:
            failures.append(f"[VERSION LOCKSTEP] index.html nav-badge does not match v{core_ver}")

    # 6. ESPHome pin lockstep & dev gate
    val_txt = (ROOT / ".github" / "workflows" / "validate.yml").read_text(encoding="utf-8")
    rel_txt = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
    ci_pins = re.findall(r"esphome==(\S+)", val_txt) + re.findall(r"ghcr\.io/esphome/esphome:(\S+)", rel_txt)
    dev_mins = set()
    for dev_file in sorted((ROOT / "devices").glob("*.yaml")):
        dtxt = dev_file.read_text(encoding="utf-8")
        mm = re.search(r"min_version:\s*(\S+)", dtxt)
        dev_mins.add(mm.group(1) if mm else f"<missing:{dev_file.name}>")
        if not re.search(r"(?m)^improv_serial:\s*$", dtxt) or "improv_ble" in dtxt:
            failures.append(f"[PROVISIONING] {dev_file.name} must declare improv_serial: and never improv_ble")
        if not re.search(r"(?m)^latter_beacon:\s*\n\s+id:\s*ble_ctrl\b", dtxt) or re.search(
            r"(?m)^(esp32_ble|esp32_ble_beacon):", dtxt
        ):
            failures.append(f"[ONE BEACON] {dev_file.name} must declare latter_beacon (id: ble_ctrl) only")
        if "latter_hal" in dtxt:
            failures.append(f"[COMPONENTS] {dev_file.name} still references retired latter_hal")
    if len(dev_mins) != 1 or not ci_pins or any(p != next(iter(dev_mins)) for p in ci_pins):
        failures.append(f"[ESPHOME PIN LOCKSTEP] device min_version {sorted(dev_mins)} != CI pins {ci_pins}")
    if "compile devices/" not in val_txt or "\n  promote:" not in val_txt or "refs/heads/main" not in val_txt:
        failures.append("[DEV GATE] validate.yml must compile devices and fast-forward main in promote job")
    if "test_latter_state" in val_txt:
        failures.append("[DEV GATE] validate.yml still references retired test_latter_state.cpp")

    # 7. Board capability flags, P4 DMA, MIPI-DSI IRAM, 48 kHz full-duplex audio, and light-sleep safety
    audio_voice_txt = (ROOT / "modules" / "audio_voice.yaml").read_text(encoding="utf-8")
    sound_refs = sorted(set(re.findall(r"sounds/\$\{audio_out_rate\}/([a-z0-9_]+\.wav)", audio_voice_txt)))
    if "rtttl:" in audio_voice_txt:
        failures.append("[NO RTTTL] modules/audio_voice.yaml must not declare rtttl")
    if "play_alarm_sound" not in audio_voice_txt or "resume_music_after_va" not in audio_voice_txt:
        failures.append("[VOICE LIFECYCLE] modules/audio_voice.yaml missing play_alarm_sound or resume_music_after_va")

    board_files = sorted((ROOT / "boards").glob("*.yaml"))
    trait_targets = board_files + [ROOT / "examples" / "custom_display_template.yaml"]
    for bf in trait_targets:
        expanded = expand_local_yaml_includes(bf)
        uncommented_yaml = _strip_yaml_comments(expanded)
        for flag in REQUIRED_BOARD_FLAGS:
            if f"-D{flag}=" not in uncommented_yaml:
                failures.append(f"[BOARD TRAITS] {bf.name} missing -D{flag}=...")
        if 'CONFIG_COMPILER_OPTIMIZATION_PERF: "y"' not in uncommented_yaml:
            failures.append(f"[BOARD TRAITS] {bf.name} missing CONFIG_COMPILER_OPTIMIZATION_PERF: \"y\" (-O2)")
        if 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "256"' not in uncommented_yaml:
            failures.append(f"[BOARD TRAITS] {bf.name} missing CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: \"256\"")
        for sub in ("sendspin_buffer_size", "mww_internal_ram", "audio_out_rate", "audio_out_channels"):
            if not re.search(rf"(?m)^\s+{sub}:\s*\S", uncommented_yaml):
                failures.append(f"[BOARD AUDIO TRAITS] {bf.name} missing substitution '{sub}'")
        if re.search(r"platform:\s*resampler[\s\S]{0,300}?output_speaker:", uncommented_yaml):
            failures.append(f"[NO RESAMPLING] {bf.name} declares a forbidden resampler speaker")
        if "full_duplex: true" not in uncommented_yaml:
            failures.append(f"[FULL DUPLEX] {bf.name} must configure full_duplex: true on its I2S bus")
        m_out = re.search(r'(?m)^\s+audio_out_rate:\s*"?(\d+)', uncommented_yaml)
        m_in = re.search(r'(?m)^\s+audio_in_rate:\s*"?(\d+)', uncommented_yaml)
        if m_out and m_in and m_out.group(1) != m_in.group(1):
            if not re.search(r"platform:\s*resampler\s*\n\s*id:\s*latter_mic\b", uncommented_yaml):
                failures.append(f"[FULL DUPLEX] {bf.name} needs resampler microphone 'latter_mic'")
        if m_out:
            for snd in sound_refs:
                if not (ROOT / "sounds" / m_out.group(1) / snd).is_file():
                    failures.append(f"[SOUND FILES] Missing sounds/{m_out.group(1)}/{snd} for {bf.name}")
        if re.search(r"\bCONFIG_RTC_CLK_SRC_INT_8MD256\s*:", uncommented_yaml):
            failures.append(f"[LIGHT SLEEP] {bf.name} must never enable CONFIG_RTC_CLK_SRC_INT_8MD256")
        if re.search(r"\bCONFIG_PM_SLP_SPIRAM_HALFSLEEP", uncommented_yaml):
            failures.append(f"[LIGHT SLEEP] {bf.name} must never enable unimplemented CONFIG_PM_SLP_SPIRAM_HALFSLEEP_*")
        if re.search(r"(?m)^\s*enable_light_sleep:\s*true\b", uncommented_yaml):
            if 'CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"' not in uncommented_yaml:
                failures.append(
                    f"[LIGHT SLEEP] {bf.name} with enable_light_sleep: true must set "
                    'CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"'
                )
            if not re.search(r"(?m)^\s*min_frequency:\s*80MHz\b", uncommented_yaml):
                failures.append(
                    f"[LIGHT SLEEP] {bf.name} with enable_light_sleep: true must set min_frequency: 80MHz"
                )
        if re.search(r"\bCONFIG_CACHE_L2_CACHE_LINE_128B\s*:", uncommented_yaml):
            failures.append(f"[P4 DMA] {bf.name} must never enable CONFIG_CACHE_L2_CACHE_LINE_128B")
        if re.search(r"\bCONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM\b", uncommented_yaml):
            failures.append(f"[P4 DMA] {bf.name} must not set nonexistent CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM")
        if "-DLATTER_CHIP_P4=1" in uncommented_yaml:
            if not re.search(r"esp32_hosted:\s*\n(?:[ \t]+.*\n)*?\s+use_psram:\s*true\b", uncommented_yaml):
                failures.append(f"[P4 DMA] {bf.name} must set use_psram: true under esp32_hosted:")
        if "platform: mipi_dsi" in uncommented_yaml:
            for dsi_req in (
                'CONFIG_GDMA_ISR_IRAM_SAFE: "y"',
                'CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"',
                "-DLATTER_DSI_IRAM_SAFE=1",
            ):
                if dsi_req not in uncommented_yaml:
                    failures.append(f"[DSI IRAM] {bf.name} missing required MIPI-DSI IRAM option: {dsi_req}")
            if "esp_lcd_dpi_panel_register_event_callbacks" in uncommented_yaml:
                failures.append(
                    f"[DSI IRAM] {bf.name} still wraps retired esp_lcd_dpi_panel_register_event_callbacks"
                )

    panel_cpp = (ROOT / "components" / "latter_core" / "panel_power.cpp").read_text(encoding="utf-8")
    panel_hdr = (
        (ROOT / "include" / "hal_panel_power.h").read_text(encoding="utf-8")
        if (ROOT / "include" / "hal_panel_power.h").is_file()
        else ""
    )
    if "ledc_channel_inited" not in (panel_cpp + panel_hdr):
        failures.append(
            "[OTA SAFETY] panel_power.cpp / hal_panel_power.h must provide one-time ledc_channel_inited guard"
        )
    if "esp_lcd_dpi_panel_register_event_callbacks" in (panel_cpp + panel_hdr):
        failures.append(
            "[DSI IRAM] panel_power.cpp / hal_panel_power.h still contains retired "
            "esp_lcd_dpi_panel_register_event_callbacks wrapper"
        )

    bridge_cpp = (ROOT / "components" / "latter_core" / "esphome_bridge.cpp").read_text(encoding="utf-8")
    bridge_hdr = bridge_h.read_text(encoding="utf-8") if bridge_h.is_file() else ""
    bridge_combined = bridge_hdr + "\n" + bridge_cpp
    if "stop_poller()" not in bridge_combined or "800U" not in bridge_combined or "400U" not in bridge_combined:
        failures.append(
            "[OTA SAFETY] esphome_bridge.h / esphome_bridge.cpp must call stop_poller() and throttle "
            "OTA progress redraws (400 ms SPI/QSPI, 800 ms MIPI-DSI)"
        )
    if "continue_conversation_" in _strip_cpp_comments(bridge_combined):
        failures.append("[VOICE LIFECYCLE] esphome_bridge must never mutate continue_conversation_")

    # [AWAKE LOCK OWNER] pm_apply() must only be called from modules/power_manager.yaml (apply_power_policy)
    pm_callers: list[str] = []
    for ypath in iter_repo_files(ROOT / "modules") + iter_repo_files(ROOT / "boards") + iter_repo_files(ROOT / "guis"):
        if ypath.suffix.lower() != ".yaml":
            continue
        if re.search(r"\bpm_apply\s*\(", _strip_yaml_comments(ypath.read_text(encoding="utf-8"))):
            pm_callers.append(ypath.relative_to(ROOT).as_posix())
    if pm_callers != ["modules/power_manager.yaml"]:
        failures.append(
            f"[AWAKE LOCK OWNER] pm_apply() must be called exclusively from modules/power_manager.yaml "
            f"(found: {pm_callers})"
        )

    # [PROF GATE] Non-exempt [PROF:*] ESP_LOGI lines must check profiler_enabled() / profile_logging_
    # (Exempt per include/latter_prof.h: [PROF:BOOT] once-per-boot summary and [PROF:LINK] Link Report)
    for path in iter_repo_files(ROOT / "include") + iter_repo_files(ROOT / "components"):
        if path.suffix.lower() not in {".h", ".hpp", ".cpp"}:
            continue
        rel = path.relative_to(ROOT).as_posix()
        clean = _strip_cpp_comments(path.read_text(encoding="utf-8"))
        for m_prof in re.finditer(r'ESP_LOGI\s*\([^;]*?"\[PROF:([A-Z0-9_]+)\]', clean):
            tag = m_prof.group(1)
            if tag in {"BOOT", "LINK"}:
                continue
            window = clean[max(0, m_prof.start() - 260) : m_prof.start()]
            if (
                "prof_enabled" not in window
                and "profiler" not in window
                and "profile_logging_" not in window
            ):
                failures.append(f"[PROF GATE] {rel} logs [PROF:{tag}] at INFO without profiler_enabled() gate")

    # 7b. Strapping-pin, component dependency & C++ header-order validation
    verify_strapping_pins(failures)
    verify_component_deps(failures)
    verify_header_order(failures)

    # 8. Retired half-duplex names, zero-privilege RPC invariants & UI rendering invariants
    for folder in ("include", "components", "modules", "boards", "guis", "devices", "examples"):
        base = ROOT / folder
        for path in iter_repo_files(base):
            if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".yaml"}:
                continue
            txt = path.read_text(encoding="utf-8")
            rel = path.relative_to(ROOT).as_posix()
            m_hd = RETIRED_HALF_DUPLEX_RE.search(txt)
            if m_hd:
                failures.append(f"[FULL DUPLEX] {rel} references retired symbol {m_hd.group(0)}")
            if path.suffix.lower() == ".yaml":
                uncommented_y = _strip_yaml_comments(txt)
                if re.search(r"(?m)^\s*-?\s*homeassistant\.action\s*:", uncommented_y):
                    failures.append(f"[ZERO PRIVILEGE RPC] {rel} uses forbidden homeassistant.action")
                if re.search(r"\bdevice_slug\b", uncommented_y):
                    failures.append(f"[DIRECT PUSH] {rel} references retired device_slug substitution")
            if folder in ("include", "components", "guis") and LOSSY_UI_RE.search(txt):
                uncommented = _strip_cpp_comments(txt) if path.suffix.lower() != ".yaml" else _strip_yaml_comments(txt)
                if LOSSY_UI_RE.search(uncommented):
                    failures.append(f"[UI INVARIANT] {rel} uses forbidden it.image/draw_pixels_at or lossy mask")

    # 9. Noise-encrypted ESPHome OTA + HTTPS pull-OTA in modules/system_core.yaml; Native API standards in modules/base.yaml
    retired_ota_pwd = "ota_" + "password"
    retired_ota_pwd_re = re.compile(r"\b" + retired_ota_pwd + r"\b")
    for ypath in iter_repo_files(ROOT):
        if ypath.suffix.lower() not in {".yaml", ".yml"}:
            continue
        yrel = ypath.relative_to(ROOT).as_posix()
        ytxt = ypath.read_text(encoding="utf-8")
        if retired_ota_pwd_re.search(ytxt):
            failures.append(f"[OTA SECURITY] {yrel} references retired {retired_ota_pwd}")
        in_ota = False
        ota_indent = -1
        for line_no, raw_line in enumerate(ytxt.splitlines(), 1):
            stripped = raw_line.split("#", 1)[0].rstrip()
            if not stripped.strip():
                continue
            indent = len(stripped) - len(stripped.lstrip(" "))
            m_ota = re.match(r"^(\s*)ota\s*:\s*$", stripped)
            if m_ota:
                in_ota = True
                ota_indent = len(m_ota.group(1))
                continue
            if in_ota:
                if indent <= ota_indent:
                    in_ota = False
                elif re.match(r"^\s*(?:-\s*)?password\s*:", stripped):
                    failures.append(
                        f"[OTA SECURITY] {yrel}:{line_no} sets legacy password: under ota: (use Noise encryption:)"
                    )

    sys_core_txt = (ROOT / "modules" / "system_core.yaml").read_text(encoding="utf-8")
    sys_core_uncommented = _strip_yaml_comments(sys_core_txt)
    if not re.search(r"(?m)^\s*-\s*platform:\s*http_request\b", sys_core_uncommented) or not re.search(
        r"(?m)^\s*-\s*platform:\s*esphome\s*\n\s*id:\s*ota_esphome\b", sys_core_uncommented
    ):
        failures.append(
            "[OTA SECURITY] modules/system_core.yaml must define both ota http_request and esphome (id: ota_esphome)"
        )
    if (
        len(re.findall(r"\bhandle_ota_begin\s*\(", sys_core_uncommented)) != 2
        or len(re.findall(r"\bhandle_ota_progress\s*\(", sys_core_uncommented)) != 2
    ):
        failures.append(
            "[OTA SECURITY] modules/system_core.yaml must wire handle_ota_begin and handle_ota_progress on both OTA platforms"
        )
    if not re.search(r"(?m)^\s*ota:\s*\n\s*-\s*id:\s*!extend\s+ota_esphome\s*\n\s*encryption:\s*$", val_txt):
        failures.append(
            "[OTA SECURITY] .github/workflows/validate.yml Device Builder wrapper must extend ota_esphome with Noise encryption:"
        )

    base_uncommented = _strip_yaml_comments(base_txt)
    if not re.search(r"(?m)^\s*batch_delay:\s*0ms\b", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must set batch_delay: 0ms under api:")
    if not re.search(r"(?m)^\s*encryption:\s*$", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must declare encryption: under api:")
    if not re.search(r"(?m)^\s*outgoing_connection:\s*\n\s*delay:\s*\$\{api_outgoing_delay\}", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must declare outgoing_connection: with delay: ${api_outgoing_delay}")
    if "id: latter_ping_action" not in base_uncommented:
        failures.append("[API STANDARDS] modules/base.yaml must set id: latter_ping_action on latter_ping")

    # 10. Device UI & accessibility invariants + YAML C++ symbol/ID resolution
    verify_device_ui_and_accessibility(failures)
    verify_yaml_cpp_and_ids(failures)

    # 11. Subprocess tool gates & C++20 host unit tests (skipped only in --fast mode)
    if not fast_mode:
        run_subprocess_gates(failures)

    if failures:
        print(f"verify_core.py FAILED ({len(failures)} issue(s)):")
        for item in failures:
            print(f"  - {item}")
        return 1

    mode_lbl = "fast static" if fast_mode else "full static + protocol/boards/sounds + C++20 host tests"
    print(f"verify_core.py PASSED ({mode_lbl}, 0 issues)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
