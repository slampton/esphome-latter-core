"""Shared constants, comment/string strippers, YAML include expansion, and LOC ratchet helpers."""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path
from typing import Final

ROOT: Final[Path] = Path(__file__).resolve().parent.parent.parent

SKIP_DIRS: Final[frozenset[str]] = frozenset(
    {
        ".git",
        "__pycache__",
        ".pytest_cache",
        ".mypy_cache",
        ".ruff_cache",
        ".esphome",
        "node_modules",
        ".venv",
        "venv",
    }
)
TEXT_EXTS: Final[frozenset[str]] = frozenset(
    {".yaml", ".yml", ".h", ".hpp", ".cpp", ".c", ".py", ".json", ".md", ".sh", ".html"}
)

LOC_SOFT_WARN: Final[int] = 1000
LOC_HARD_FAIL: Final[int] = 1200
LOC_EXEMPT_REL_PATHS: Final[frozenset[str]] = frozenset(
    {
        "protocol/latter_v3.json",
        "include/latter_protocol.h",
        "DEVICE_CONTRACT.md",
        "components/latter_ui/ui_mdi.cpp",
    }
)

REQUIRED_BOARD_FLAGS: Final[tuple[str, ...]] = (
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

RETIRED_PATHS: Final[tuple[str, ...]] = (
    "mixins",
    "components/latter_hal",
    "include/hal_types.h",
    "include/va_cancel_helper.h",
    "include/hal_pmu_axp2101.h",
    "include/hal_power_pm.h",
    "include/latter_state.h",
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
    "components/latter_core/power_pm.cpp",
    "components/latter_core/video_stream.cpp",
    "components/latter_ui/gesture_engine.cpp",
    "components/latter_ui/page_comms.cpp",
    "components/latter_ui/ui_gfx.cpp",
    "components/latter_ui/ui_widgets.cpp",
    "components/latter_ui/ui_compositor.cpp",
    "components/latter_ui/page_ambient_va.cpp",
    "components/latter_ui/page_clock_home.cpp",
    "components/latter_ui/page_calendar.cpp",
    "components/latter_ui/page_music.cpp",
    "modules/features",
    "modules/latter_client.yaml",
    "modules/area_control.yaml",
    "modules/display_ui.yaml",
    "modules/audio_voice.yaml",
    "modules/power_manager.yaml",
    "modules/pmu_none.yaml",
    "modules/imu_none.yaml",
    "modules/pmu_axp2101.yaml",
    "modules/imu_qmi8658.yaml",
    "modules/settings_entities.yaml",
    "guis/universal/touch_handlers.yaml",
    "tests/host/test_latter_state.cpp",
    "tests/host/test_all.cpp",
    "docs/UPSTREAM_ESPHOME_VA_PR.md",
)

RETIRED_HALF_DUPLEX_RE: Final[re.Pattern[str]] = re.compile(
    r"\b("
    + "kAudio"
    + "FullDuplex|"
    + "USE_I2S_AUDIO_"
    + "FULL_DUPLEX|"
    + "(?:hal_)?route_i2s_"
    + r"to_(?:spk|mic))\b"
)
LOSSY_UI_RE: Final[re.Pattern[str]] = re.compile(
    r"\bit\.(?:image|draw_pixels_at)\s*\(|0x[cC]718\b|0x[eE]79[cC]\b"
)
RAW_IT_DRAW_RE: Final[re.Pattern[str]] = re.compile(
    r"(?<![\w.>])it\.(?:line|rectangle|filled_rectangle|circle|filled_circle|triangle|filled_triangle|print|printf)\s*\("
)
BANNED_TERMS_RE: Final[re.Pattern[str]] = re.compile(r"\b(household|households)\b", re.IGNORECASE)
DEVICE_SLUG_RE: Final[re.Pattern[str]] = re.compile(
    r"\b(?:pocket-assistant|wrist-assist|mobile-assistant|remote-assistant|desk-assistant)\b",
    re.IGNORECASE,
)
INCLUDE_RE: Final[re.Pattern[str]] = re.compile(r"!include\s+([^\s#]+)")
CPP_TOKEN_RE: Final[re.Pattern[str]] = re.compile(
    r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*[\s\S]*?\*/|//[^\n]*'
)
STRAPPING_PINS: Final[dict[str, set[int]]] = {
    "ESP32-S3": {0, 3, 45, 46},
    "ESP32-P4": {34, 35, 36, 37, 38},
}
MAX_SUB_PASSES: Final[int] = 4


def run_trusted_cmd(
    cmd: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Execute a trusted verification tool or host compiler command."""
    return subprocess.run(  # noqa: S603, RUF100
        cmd,
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )


def iter_repo_files(base: Path) -> list[Path]:
    """Return sorted regular files under `base`, skipping build/cache directories."""
    if not base.is_dir():
        return []
    files: list[Path] = []
    for cur_root, dirnames, filenames in os.walk(base):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        root_path = Path(cur_root)
        for fname in sorted(filenames):
            fp = root_path / fname
            if fp.is_file():
                files.append(fp)
    return files


def strip_cpp_comments(text: str) -> str:
    """Strip C and C++ comments while preserving newline counts inside block comments."""
    no_block = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", no_block)


def strip_cpp_strings_and_comments(text: str) -> str:
    """Mask C++ comments and single-line string/char literals while preserving exact line count."""

    def _repl(m: re.Match[str]) -> str:
        tok = m.group(0)
        if tok.startswith('"'):
            return '"' + (" " * max(0, len(tok) - 2)) + '"'
        if tok.startswith("'"):
            return "' '"
        if tok.startswith("/*"):
            return "\n" * tok.count("\n")
        return ""

    return CPP_TOKEN_RE.sub(_repl, text)


def strip_yaml_comments(text: str) -> str:
    """Strip full-line and unquoted trailing YAML comments."""
    out: list[str] = []
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        if " #" in line and line.count('"') % 2 == 0:
            line = line.split(" #", 1)[0]
        out.append(line)
    return "\n".join(out)


def strip_yaml_comments_keep_lines(text: str) -> str:
    """Strip YAML comments while preserving exact 1-based line numbers."""
    out: list[str] = []
    for line in text.splitlines():
        lstripped = line.lstrip()
        if lstripped.startswith("#") and not re.match(
            r"^#\s*(?:if|ifdef|ifndef|elif|else|endif|define|include)\b", lstripped
        ):
            out.append("")
            continue
        if " #" in line and line.count('"') % 2 == 0 and line.count("'") % 2 == 0:
            line = line.split(" #", 1)[0]
        out.append(line)
    return "\n".join(out)


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


def extract_substitutions(chain: list[Path]) -> dict[str, str]:
    """Extract merged ESPHome substitutions across an include chain (later files override defaults)."""
    subs: dict[str, str] = {}
    for yf in chain:
        in_subs = False
        subs_indent = -1
        for raw_line in yf.read_text(encoding="utf-8").splitlines():
            code = (
                raw_line.split(" #", 1)[0]
                if " #" in raw_line and raw_line.count('"') % 2 == 0
                else raw_line
            )
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
                        subs[m_kv.group(1)] = m_kv.group(2).strip().strip('"').strip("'")
    return subs


def resolve_substitutions(value: str, subs: dict[str, str]) -> str:
    """Resolve nested `${var}` ESPHome substitutions up to 4 levels."""

    def _repl(m: re.Match[str]) -> str:
        return subs.get(m.group(1), m.group(0))

    out = value
    for _ in range(MAX_SUB_PASSES):
        nxt = re.sub(r"\$\{([A-Za-z0-9_]+)\}", _repl, out)
        if nxt == out:
            break
        out = nxt
    return out


def verify_loc_ratchet(failures: list[str], warnings: list[str]) -> None:
    """Enforce <= 1,200 LOC hard fail (<= 1,000 soft warning) on hand-authored repo files."""
    for path in iter_repo_files(ROOT):
        if path.suffix.lower() not in TEXT_EXTS:
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel in LOC_EXEMPT_REL_PATHS:
            continue
        try:
            line_count = len(path.read_text(encoding="utf-8").splitlines())
        except UnicodeDecodeError:
            continue
        if line_count > LOC_HARD_FAIL:
            failures.append(
                f"[LOC RATCHET] {rel} ({line_count} LOC) exceeds hard limit of {LOC_HARD_FAIL} LOC"
            )
        elif line_count > LOC_SOFT_WARN:
            warnings.append(
                f"[LOC SOFT] {rel} ({line_count} LOC) exceeds soft target of {LOC_SOFT_WARN} LOC"
            )
