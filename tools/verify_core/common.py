"""Shared constants, comment/string strippers, and YAML include expansion helpers."""

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


_CORE_SECTION_FLAG_MAP: Final[dict[str, dict[str, tuple[str, bool]]]] = {
    "display": {
        "width": ("LATTER_SCREEN_W", False),
        "height": ("LATTER_SCREEN_H", False),
        "ppi": ("LATTER_PANEL_PPI", False),
        "round": ("LATTER_ROUND_DISPLAY", True),
        "ips": ("LATTER_PANEL_IPS", True),
        "dcs_sleep": ("LATTER_PANEL_DCS_SLEEP", True),
        "dc_pin": ("LATTER_DISPLAY_DC_PIN", False),
        "corner_r": ("LATTER_CORNER_R", False),
        "view_scale": ("LATTER_VIEW_SCALE_PCT", False),
        "dsi_iram_safe": ("LATTER_DSI_IRAM_SAFE", True),
        "slot_count": ("LATTER_SLOT_COUNT", False),
        "card_slot_count": ("LATTER_CARD_SLOT_COUNT", False),
        "bri_gamma_x100": ("LATTER_BRI_GAMMA_X100", False),
        "bri_dcs_min": ("LATTER_BRI_DCS_MIN", False),
        "bri_dcs_max": ("LATTER_BRI_DCS_MAX", False),
        "touch_int_pin": ("LATTER_TOUCH_INT_PIN", False),
        "touch_rst_pin": ("LATTER_TOUCH_RST_PIN", False),
    },
    "backlight": {
        "pin": ("LATTER_BACKLIGHT_PIN", False),
        "en_pin": ("LATTER_BACKLIGHT_EN_PIN", False),
        "inverted": ("LATTER_BACKLIGHT_INVERTED", True),
        "pwm_hz": ("LATTER_BACKLIGHT_PWM_HZ", False),
        "pwm_bits": ("LATTER_BACKLIGHT_PWM_BITS", False),
        "duty_min_pm": ("LATTER_BACKLIGHT_DUTY_MIN_PM", False),
        "duty_max_pm": ("LATTER_BACKLIGHT_DUTY_MAX_PM", False),
    },
    "power": {
        "battery_mah": ("LATTER_BATTERY_MAH", False),
        "pwr_key_pin": ("LATTER_PWR_KEY_PIN", False),
        "pwr_key_active_high": ("LATTER_PWR_KEY_ACTIVE_HIGH", True),
        "pwr_key_deep_sleep_wake": ("LATTER_PWR_KEY_DEEP_SLEEP_WAKE", True),
        "action_btn_pin": ("LATTER_ACTION_BTN_PIN", False),
        "imu_int1_pin": ("LATTER_IMU_INT1_PIN", False),
        "c6_reset_pin": ("LATTER_C6_RESET_PIN", False),
        "pmu_ts_disable": ("LATTER_PMU_TS_DISABLE", True),
        "pmu_dcdc_keep_mask": ("LATTER_PMU_DCDC_KEEP_MASK", False),
        "pmu_ldo_keep_mask": ("LATTER_PMU_LDO_KEEP_MASK", False),
    },
    "capabilities": {
        "display": ("LATTER_HAS_DISPLAY", True),
        "touch": ("LATTER_HAS_TOUCH", True),
        "audio": ("LATTER_HAS_AUDIO", True),
        "mic": ("LATTER_HAS_MIC", True),
        "speaker": ("LATTER_HAS_SPEAKER", True),
        "imu": ("LATTER_HAS_IMU", True),
        "battery": ("LATTER_HAS_BATTERY", True),
        "rtc": ("LATTER_HAS_RTC", True),
        "stationary": ("LATTER_IS_STATIONARY", True),
        "wearable": ("LATTER_IS_WEARABLE", True),
        "video_stream": ("LATTER_HAS_VIDEO_STREAM", True),
        "hw_jpeg": ("LATTER_HAS_HW_JPEG", True),
    },
    "drivers": {
        "pmu_axp2101": ("LATTER_DRV_PMU_AXP2101", True),
        "rtc_pcf85063": ("LATTER_DRV_RTC_PCF85063", True),
        "imu_qmi8658": ("LATTER_DRV_IMU_QMI8658", True),
        "codec_es8311": ("LATTER_DRV_CODEC_ES8311", True),
    },
    "diagnostics": {
        "nvs_guard": ("LATTER_DIAG_NVS_GUARD", True),
        "twdt_breadcrumb": ("LATTER_DIAG_TWDT_BREADCRUMB", True),
    },
}


def parse_latter_core_flags(yaml_text: str) -> dict[str, str]:
    """Extract effective LATTER_* defines from typed `latter_core:` YAML (or legacy -DLATTER_* flags)."""
    code = strip_yaml_comments(yaml_text)
    flags: dict[str, str] = {}
    in_core = False
    section: str | None = None
    saw_core = False

    def _norm(raw: str, is_bool: bool) -> str:
        val = raw.strip().strip('"').strip("'")
        if is_bool:
            low = val.lower()
            if low in {"true", "yes", "on", "1"}:
                return "1"
            if low in {"false", "no", "off", "0"}:
                return "0"
        return val

    for raw_line in code.splitlines():
        if not raw_line.strip():
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        stripped = raw_line.strip()
        if indent == 0:
            in_core = stripped == "latter_core:"
            section = None
            if in_core:
                saw_core = True
            continue
        if not in_core:
            continue
        if indent == 2:
            if stripped.endswith(":") and ":" not in stripped[:-1]:
                section = stripped[:-1].strip()
                continue
            section = None
            if ":" not in stripped:
                continue
            key, val = stripped.split(":", 1)
            key = key.strip()
            val_clean = _norm(val, False)
            if key == "board_id":
                flags["LATTER_BOARD_ID"] = val_clean
            elif key == "ble_minor":
                flags["LATTER_BLE_MINOR"] = val_clean
            elif key == "gui":
                flags["LATTER_GUI_UNIVERSAL"] = "1" if val_clean.lower() == "universal" else "0"
            elif key == "chip":
                flags["LATTER_CHIP_P4"] = "1" if val_clean.lower() == "esp32p4" else "0"
            elif key == "psram_mb":
                flags["LATTER_PSRAM_MB"] = val_clean
        elif indent >= 4 and section in _CORE_SECTION_FLAG_MAP and ":" in stripped:
            key, val = stripped.split(":", 1)
            k_clean = key.strip()
            spec = _CORE_SECTION_FLAG_MAP[section].get(k_clean)
            if spec is not None:
                norm_v = _norm(val, spec[1])
                if section == "display" and k_clean == "view_scale":
                    norm_v = str(round(float(norm_v) * 100.0))
                flags[spec[0]] = norm_v

    if saw_core:
        gui_univ = flags.get("LATTER_GUI_UNIVERSAL", "1") == "1"
        flags.setdefault("LATTER_ROUND_DISPLAY", "0")
        flags.setdefault("LATTER_PANEL_IPS", "0")
        flags.setdefault("LATTER_HAS_DISPLAY", "1" if gui_univ else "0")
        flags.setdefault("LATTER_HAS_TOUCH", "1" if gui_univ else "0")
        flags.setdefault("LATTER_HAS_AUDIO", "1")
        flags.setdefault("LATTER_HAS_IMU", "0")
        flags.setdefault("LATTER_HAS_BATTERY", "0")
        flags.setdefault("LATTER_HAS_RTC", "0")
        flags.setdefault("LATTER_IS_STATIONARY", "0")
        flags.setdefault("LATTER_IS_WEARABLE", "0")

    for name, value in re.findall(r"-D(LATTER_[A-Z0-9_]+)=([^\"'\s]+)", code):
        flags[name] = value.strip('"').strip("\\").strip('"')
    return flags
