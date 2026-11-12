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
    if "allow_tap(" not in (touch_h + touch_cpp) or "TOUCH_DEBOUNCE_" not in (touch_h + touch_cpp):
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

    gpp = shutil.which("g++") or shutil.which("clang++")
    if gpp:
        with tempfile.TemporaryDirectory() as td:
            bin_path = Path(td) / ("latter_host_test.exe" if os.name == "nt" else "latter_host_test")
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
                bat_lines = [
                    "@echo off",
                    f'call "{vcvars}" >nul',
                    "if errorlevel 1 exit /b 1",
                    f'cl /nologo /utf-8 /std:c++20 /O2 /W4 /EHsc "{test_cpp}" "/Fe:{exe_path}" "/Fo:{obj_path}"',
                    "if errorlevel 1 exit /b 1",
                    f'"{exe_path}"',
                    "if errorlevel 1 exit /b 1",
                ]
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

    # Descriptive option labels (D-020) & Power Profile options
    settings_yaml = ROOT / "modules" / "settings_entities.yaml"
    if settings_yaml.is_file():
        stxt = settings_yaml.read_text(encoding="utf-8")
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
            if f'"{req_opt}"' not in stxt:
                failures.append(f"[PRODUCT OPTIONS] modules/settings_entities.yaml missing option {req_opt!r}")

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

    # 10. Device UI & accessibility invariants
    verify_device_ui_and_accessibility(failures)

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
