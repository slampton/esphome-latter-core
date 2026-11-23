"""Strapping-pin, component dependency, header order, board/DMA/sleep/audio HAL, and security checks."""

from __future__ import annotations

import ast
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .common import (
    LOSSY_UI_RE,
    REQUIRED_BOARD_FLAGS,
    RETIRED_HALF_DUPLEX_RE,
    ROOT,
    SKIP_DIRS,
    STRAPPING_PINS,
    collect_yaml_include_chain,
    expand_local_yaml_includes,
    extract_substitutions,
    iter_repo_files,
    resolve_substitutions,
    strip_cpp_comments,
    strip_yaml_comments,
)

DEFAULT_WIFI_BA_WIN: Final[int] = 16
EXPECTED_OTA_HOOK_CALLS: Final[int] = 2
PROF_WINDOW_CHARS: Final[int] = 260
ALLOWED_PM_APPLY_OWNERS: Final[frozenset[str]] = frozenset(
    {"modules/power_policy.yaml", "modules/sleep_lifecycle.yaml"}
)


def _find_sibling_pin_expr(lines: list[str], idx: int, code: str) -> str | None:
    """Locate sibling `number:` in the same YAML mapping item as `ignore_strapping_warning: true`."""
    m_inline = re.search(r"\bnumber\s*:\s*([^\s,}#]+)", code)
    if m_inline:
        return m_inline.group(1).strip("'\"")
    stripped = code.lstrip(" ")
    key_indent = (len(code) - len(stripped) + 2) if stripped.startswith("- ") else (len(code) - len(stripped))
    for up in range(idx - 1, -1, -1):
        ul = lines[up]
        if ul.lstrip().startswith("#") or not ul.strip():
            continue
        ucode = ul.split(" #", 1)[0] if " #" in ul and ul.count('"') % 2 == 0 else ul
        u_indent = len(ucode) - len(ucode.lstrip(" "))
        if ucode.lstrip(" ").startswith("- ") and u_indent + 2 == key_indent:
            m_num = re.match(r"^\s*-\s*number\s*:\s*(\S+)", ucode.rstrip())
            return m_num.group(1).strip("'\"") if m_num else None
        if u_indent < key_indent:
            break
        if u_indent == key_indent:
            m_num = re.match(r"^\s*number\s*:\s*(\S+)", ucode.rstrip())
            if m_num:
                return m_num.group(1).strip("'\"")
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
                return m_num.group(1).strip("'\"")
    return None


def _verify_strapping_facts(failures: list[str]) -> None:
    facts_dir = ROOT / "boards" / "facts"
    if not facts_dir.is_dir():
        return
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


@dataclass(frozen=True)
class _StrappingTargetCtx:
    target_rel: str
    soc: str
    allowed: set[int]
    subs: dict[str, str]


def verify_strapping_pins(failures: list[str]) -> None:
    """Ensure ignore_strapping_warning: true only appears on genuine strapping GPIOs for each SoC."""
    _verify_strapping_facts(failures)
    targets = (
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
            ln for yf in chain for ln in yf.read_text(encoding="utf-8").splitlines() if not ln.lstrip().startswith("#")
        )
        soc = "ESP32-P4" if re.search(r"(?m)^\s*variant\s*:\s*esp32p4\b", merged_uncommented) else "ESP32-S3"
        ctx = _StrappingTargetCtx(
            target_rel=target.relative_to(ROOT).as_posix(),
            soc=soc,
            allowed=STRAPPING_PINS[soc],
            subs=subs,
        )
        for yf in chain:
            _check_strapping_in_yaml(yf, ctx, reported, failures)


def _check_strapping_in_yaml(
    yf: Path,
    ctx: _StrappingTargetCtx,
    reported: set[tuple[str, str, int]],
    failures: list[str],
) -> None:
    yf_rel = yf.relative_to(ROOT).as_posix()
    lines = yf.read_text(encoding="utf-8").splitlines()
    for idx, raw_line in enumerate(lines):
        if raw_line.lstrip().startswith("#"):
            continue
        code = raw_line.split(" #", 1)[0] if " #" in raw_line and raw_line.count('"') % 2 == 0 else raw_line
        if not re.search(r"\bignore_strapping_warning\s*:\s*(?:true|True|yes|1)\b", code):
            continue
        pin_expr = _find_sibling_pin_expr(lines, idx, code)
        key = (ctx.soc, yf_rel, idx + 1)
        if pin_expr is None:
            if key not in reported:
                reported.add(key)
                failures.append(
                    f"[STRAPPING PIN] {ctx.target_rel} ({yf_rel}:{idx + 1}): "
                    "ignore_strapping_warning: true without resolvable sibling number:"
                )
            continue
        resolved_pin = resolve_substitutions(pin_expr, ctx.subs)
        m_gpio = re.match(r"^(?:GPIO)?(\d+)$", resolved_pin)
        if not m_gpio:
            if key not in reported:
                reported.add(key)
                failures.append(
                    f"[STRAPPING PIN] {ctx.target_rel} ({yf_rel}:{idx + 1}): "
                    f"could not resolve GPIO number from {pin_expr!r} (-> {resolved_pin!r})"
                )
            continue
        gpio_num = int(m_gpio.group(1))
        sig = (f"{ctx.soc}:{gpio_num}", yf_rel, idx + 1)
        if gpio_num not in ctx.allowed and sig not in reported:
            reported.add(sig)
            valid_str = ", ".join(f"GPIO{n}" for n in sorted(ctx.allowed))
            failures.append(
                f"[STRAPPING PIN] {ctx.target_rel} ({yf_rel}:{idx + 1}): "
                f"GPIO{gpio_num} has ignore_strapping_warning: true, which is invalid on {ctx.soc} "
                f"(strapping pins: {valid_str})"
            )


def _extract_init_deps_and_autoloads(tree: ast.Module) -> tuple[set[str], set[str]]:
    deps: set[str] = set()
    autoloads: set[str] = set()
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            names = {t.id for t in targets if isinstance(t, ast.Name)}
            if "DEPENDENCIES" in names and node.value is not None:
                deps.update(s.value for s in ast.walk(node.value) if isinstance(s, ast.Constant) and isinstance(s.value, str))
            if "AUTO_LOAD" in names and node.value is not None:
                autoloads.update(
                    s.value for s in ast.walk(node.value) if isinstance(s, ast.Constant) and isinstance(s.value, str)
                )
        elif isinstance(node, ast.FunctionDef) and node.name == "AUTO_LOAD":
            for sub in ast.walk(node):
                if isinstance(sub, ast.Return) and sub.value is not None:
                    autoloads.update(
                        lit.value
                        for lit in ast.walk(sub.value)
                        if isinstance(lit, ast.Constant) and isinstance(lit.value, str)
                    )
    return deps, autoloads


def verify_component_deps(failures: list[str]) -> None:
    """Validate ESPHome component manifests (`DEPENDENCIES` vs `AUTO_LOAD`)."""
    comp_root = ROOT / "components"
    if not comp_root.is_dir():
        failures.append("[COMPONENTS] components/ directory missing")
        return
    comp_dirs = sorted(p.name for p in comp_root.iterdir() if p.is_dir() and p.name not in SKIP_DIRS)
    failures.extend(
        f"[COMPONENTS] Required component '{req_comp}' missing (found {comp_dirs})"
        for req_comp in ("latter_beacon", "latter_core", "latter_ui")
        if req_comp not in comp_dirs
    )

    for init_py in sorted(comp_root.glob("*/__init__.py")):
        rel = init_py.relative_to(ROOT).as_posix()
        comp_name = init_py.parent.name
        try:
            tree = ast.parse(init_py.read_text(encoding="utf-8"), filename=rel)
        except SyntaxError as exc:
            failures.append(f"[COMPONENT DEPS] {rel} failed Python AST parse: {exc}")
            continue
        deps, autoloads = _extract_init_deps_and_autoloads(tree)
        overlap = sorted(deps & autoloads)
        if overlap:
            failures.append(
                f"[COMPONENT DEPS] {rel}: DEPENDENCIES {sorted(deps)} overlaps AUTO_LOAD {sorted(autoloads)} on {overlap}"
            )
        if comp_name == "latter_core" and "latter_ui" not in autoloads:
            failures.append(f"[COMPONENT DEPS] {rel}: AUTO_LOAD must wire latter_ui (found {sorted(autoloads)})")
        if "latter_hal" in (deps | autoloads):
            failures.append(f"[COMPONENT DEPS] {rel}: references retired latter_hal")


def _header_provides_esphome_defines(hdr_path: Path, seen: set[Path] | None = None) -> bool:
    if seen is None:
        seen = set()
    resolved = hdr_path.resolve()
    if resolved in seen or not resolved.is_file():
        return False
    seen.add(resolved)
    clean = strip_cpp_comments(resolved.read_text(encoding="utf-8"))
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


def verify_header_order(failures: list[str]) -> None:
    """Ban circular `#include "esphome.h"` in headers and ensure `esphome/core/defines.h` precedes `USE_*`."""
    for folder in ("include", "components"):
        for path in iter_repo_files(ROOT / folder):
            ext = path.suffix.lower()
            if ext not in {".h", ".hpp", ".cpp"}:
                continue
            rel = path.relative_to(ROOT).as_posix()
            clean = strip_cpp_comments(path.read_text(encoding="utf-8"))
            if ext in {".h", ".hpp"}:
                if re.search(r'#\s*include\s*[<"]esphome\.h[>"]', clean) or re.search(
                    r'__has_include\s*\(\s*[<"]esphome\.h[>"]\s*\)', clean
                ):
                    failures.append(f"[HEADER ORDER] {rel} must not reference esphome.h")
                if re.search(r"(?m)^\s*using\s+namespace\s+latter_ui\s*;", clean):
                    failures.append(f"[CORE DOCS] {rel} must not declare global `using namespace latter_ui;`")
            _check_defines_before_use_macro(path, rel, clean, failures)


def _check_defines_before_use_macro(path: Path, rel: str, clean: str, failures: list[str]) -> None:
    defines_seen = False
    for idx, line in enumerate(clean.splitlines()):
        if re.match(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\bUSE_[A-Z0-9_]+\b", line):
            if not defines_seen:
                failures.append(f"[HEADER ORDER] {rel}:{idx + 1} checks USE_* before including esphome/core/defines.h")
            break
        m_inc = re.match(r'^\s*#\s*include\s*"([^"]+)"', line)
        if not m_inc:
            continue
        inc_target = m_inc.group(1)
        if inc_target == "esphome/core/defines.h":
            break
        for cand in (path.parent / inc_target, ROOT / "include" / inc_target):
            if cand.is_file() and _header_provides_esphome_defines(cand):
                defines_seen = True
                break
        if defines_seen:
            break


def _check_single_board_target(bf: Path, sound_refs: list[str], beacon_init_py: str, failures: list[str]) -> None:
    uncommented = strip_yaml_comments(expand_local_yaml_includes(bf))
    failures.extend(
        f"[BOARD TRAITS] {bf.name} missing -D{flag}=..."
        for flag in REQUIRED_BOARD_FLAGS
        if f"-D{flag}=" not in uncommented
    )
    if 'CONFIG_COMPILER_OPTIMIZATION_PERF: "y"' not in uncommented:
        failures.append(f'[BOARD TRAITS] {bf.name} missing CONFIG_COMPILER_OPTIMIZATION_PERF: "y" (-O2)')
    if 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"' not in uncommented:
        failures.append(f'[BOARD TRAITS] {bf.name} missing CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"')
    _check_board_wifi_and_audio(bf.name, uncommented, sound_refs, failures)
    _check_board_light_sleep(bf.name, uncommented, beacon_init_py, failures)
    _check_board_p4_and_dsi(bf.name, uncommented, failures)


def _check_board_wifi_and_audio(
    bname: str, uncommented: str, sound_refs: list[str], failures: list[str]
) -> None:
    m_static_rx = re.search(r'CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM:\s*"(\d+)"', uncommented)
    if m_static_rx:
        static_rx = int(m_static_rx.group(1))
        m_ba = re.search(r'CONFIG_ESP_WIFI_RX_BA_WIN:\s*"(\d+)"', uncommented)
        ba_win = int(m_ba.group(1)) if m_ba else DEFAULT_WIFI_BA_WIN
        if ba_win > 2 * static_rx:
            failures.append(f"[WIFI RX BA WIN] {bname} WIFI_RX_BA_WIN ({ba_win}) > 2 * STATIC_RX ({static_rx})")
        m_dyn_rx = re.search(r'CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM:\s*"(\d+)"', uncommented)
        if m_dyn_rx and ba_win > int(m_dyn_rx.group(1)):
            failures.append(f"[WIFI RX BA WIN] {bname} WIFI_RX_BA_WIN ({ba_win}) > DYNAMIC_RX ({m_dyn_rx.group(1)})")
    failures.extend(
        f"[BOARD AUDIO TRAITS] {bname} missing substitution '{sub}'"
        for sub in ("sendspin_buffer_size", "mww_internal_ram", "audio_out_rate", "audio_out_channels")
        if not re.search(rf"(?m)^\s+{sub}:\s*\S", uncommented)
    )
    if re.search(r"platform:\s*resampler[\s\S]{0,300}?output_speaker:", uncommented):
        failures.append(f"[NO RESAMPLING] {bname} declares a forbidden resampler speaker")
    if "full_duplex: true" not in uncommented:
        failures.append(f"[FULL DUPLEX] {bname} must configure full_duplex: true on its I2S bus")
    m_out = re.search(r'(?m)^\s+audio_out_rate:\s*"?(\d+)', uncommented)
    m_in = re.search(r'(?m)^\s+audio_in_rate:\s*"?(\d+)', uncommented)
    if (
        m_out
        and m_in
        and m_out.group(1) != m_in.group(1)
        and not re.search(r"platform:\s*resampler\s*\n\s*id:\s*latter_mic\b", uncommented)
    ):
        failures.append(f"[FULL DUPLEX] {bname} needs resampler microphone 'latter_mic'")
    if m_out:
        out_rate = m_out.group(1)
        failures.extend(
            f"[SOUND FILES] Missing sounds/{out_rate}/{snd} for {bname}"
            for snd in sound_refs
            if not (ROOT / "sounds" / out_rate / snd).is_file()
        )


def _check_board_light_sleep(bname: str, uncommented: str, beacon_init_py: str, failures: list[str]) -> None:
    if re.search(r"\bCONFIG_RTC_CLK_SRC_INT_8MD256\s*:|\bCONFIG_PM_SLP_SPIRAM_HALFSLEEP", uncommented):
        failures.append(f"[LIGHT SLEEP] {bname} enables forbidden RTC_CLK_SRC_INT_8MD256 or SPIRAM_HALFSLEEP")
    if not re.search(r"(?m)^\s*enable_light_sleep:\s*true\b", uncommented):
        return
    if 'CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"' not in uncommented:
        failures.append(f'[LIGHT SLEEP] {bname} missing CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"')
    if not re.search(r"(?m)^\s*min_frequency:\s*80MHz\b", uncommented):
        failures.append(f"[LIGHT SLEEP] {bname} with enable_light_sleep: true must set min_frequency: 80MHz")
    failures.extend(
        f"[LIGHT SLEEP] components/latter_beacon/__init__.py missing {bt_opt}"
        for bt_opt in (
            "CONFIG_BT_CTRL_MODEM_SLEEP",
            "CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1",
            "CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL",
            "CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP",
        )
        if bt_opt not in beacon_init_py
    )


def _check_board_p4_and_dsi(bname: str, uncommented: str, failures: list[str]) -> None:
    if re.search(r"\bCONFIG_CACHE_L2_CACHE_LINE_128B\s*:|\bCONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM\b", uncommented):
        failures.append(f"[P4 DMA] {bname} sets forbidden L2 128B cache line or nonexistent MEMPOOL_PREFER_SPIRAM")
    if "-DLATTER_CHIP_P4=1" in uncommented:
        if not re.search(r"esp32_hosted:\s*\n(?:[ \t]+.*\n)*?\s+use_psram:\s*true\b", uncommented):
            failures.append(f"[P4 DMA] {bname} must set use_psram: true under esp32_hosted:")
        if re.search(r"(?m)^power_management\s*:|\besp32_power_management\b|\bmin_frequency:\s*40MHz\b", uncommented):
            failures.append(f"[P4 PM] {bname} must never enable esp32_power_management / 40MHz XTAL DFS")
    if "platform: mipi_dsi" in uncommented:
        failures.extend(
            f"[DSI IRAM] {bname} missing required MIPI-DSI IRAM option: {dsi_req}"
            for dsi_req in (
                'CONFIG_GDMA_ISR_IRAM_SAFE: "y"',
                'CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"',
                "-DLATTER_DSI_IRAM_SAFE=1",
            )
            if dsi_req not in uncommented
        )
        if "esp_lcd_dpi_panel_register_event_callbacks" in uncommented:
            failures.append(f"[DSI IRAM] {bname} still wraps retired esp_lcd_dpi_panel_register_event_callbacks")
    if "-DLATTER_CHIP_P4=0" in uncommented and not re.search(r"(?m)^\s*execute_from_psram:\s*true\b", uncommented):
        failures.append(f"[S3 PSRAM XIP] {bname} must set execute_from_psram: true under esp32.framework.advanced")
    m_touch = re.search(r"(?m)^touchscreen:\s*\n((?:[ \t]+.*\n?)*)", uncommented)
    if m_touch and "interrupt_pin:" in m_touch.group(1) and not re.search(r"(?m)^\s*update_interval:\s*never\b", m_touch.group(1)):
        failures.append(f"[TOUCH POLLING] {bname} interrupt-driven touchscreen must set update_interval: never")


def _verify_ota_and_api_security(failures: list[str]) -> None:
    retired_ota_pwd = "ota_" + "password"
    retired_ota_pwd_re = re.compile(r"\b" + retired_ota_pwd + r"\b")
    for ypath in iter_repo_files(ROOT):
        if ypath.suffix.lower() not in {".yaml", ".yml"}:
            continue
        yrel = ypath.relative_to(ROOT).as_posix()
        ytxt = ypath.read_text(encoding="utf-8")
        if retired_ota_pwd_re.search(ytxt):
            failures.append(f"[OTA SECURITY] {yrel} references retired {retired_ota_pwd}")
        _check_ota_block_no_password(yrel, ytxt, failures)

    sys_yamls = sorted((ROOT / "modules").glob("system_*.yaml"))
    sys_uncommented = "\n".join(strip_yaml_comments(p.read_text(encoding="utf-8")) for p in sys_yamls)
    if not re.search(r"(?m)^\s*-\s*platform:\s*http_request\b", sys_uncommented) or not re.search(
        r"(?m)^\s*-\s*platform:\s*esphome\s*\n\s*id:\s*ota_esphome\b", sys_uncommented
    ):
        failures.append(
            "[OTA SECURITY] modules/system_*.yaml must define both ota http_request and esphome (id: ota_esphome)"
        )
    if (
        len(re.findall(r"\bhandle_ota_begin\s*\(", sys_uncommented)) != EXPECTED_OTA_HOOK_CALLS
        or len(re.findall(r"\bhandle_ota_progress\s*\(", sys_uncommented)) != EXPECTED_OTA_HOOK_CALLS
    ):
        failures.append(
            "[OTA SECURITY] modules/system_*.yaml must wire handle_ota_begin and handle_ota_progress on both OTA platforms"
        )
    val_txt = (ROOT / ".github" / "workflows" / "validate.yml").read_text(encoding="utf-8")
    if not re.search(r"(?m)^\s*ota:\s*\n\s*-\s*id:\s*!extend\s+ota_esphome\s*\n\s*encryption:\s*$", val_txt):
        failures.append(
            "[OTA SECURITY] .github/workflows/validate.yml wrapper must extend ota_esphome with Noise encryption:"
        )
    base_uncommented = strip_yaml_comments((ROOT / "modules" / "base.yaml").read_text(encoding="utf-8"))
    if not re.search(r"(?m)^\s*batch_delay:\s*0ms\b", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must set batch_delay: 0ms under api:")
    if not re.search(r"(?m)^\s*encryption:\s*$", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must declare encryption: under api:")
    if not re.search(r"(?m)^\s*outgoing_connection:\s*\n\s*delay:\s*\$\{api_outgoing_delay\}", base_uncommented):
        failures.append("[API STANDARDS] modules/base.yaml must declare outgoing_connection: delay: ${api_outgoing_delay}")
    if "id: latter_ping_action" not in base_uncommented:
        failures.append("[API STANDARDS] modules/base.yaml must set id: latter_ping_action on latter_ping")


def _check_ota_block_no_password(yrel: str, ytxt: str, failures: list[str]) -> None:
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
                failures.append(f"[OTA SECURITY] {yrel}:{line_no} sets legacy password: under ota:")


def _verify_core_cpp_and_prof_gates(failures: list[str]) -> None:
    panel_cpp = (ROOT / "components" / "latter_core" / "panel_power.cpp").read_text(encoding="utf-8")
    panel_hdr_path = ROOT / "include" / "hal_panel_power.h"
    panel_combined = panel_cpp + "\n" + (panel_hdr_path.read_text(encoding="utf-8") if panel_hdr_path.is_file() else "")
    if "ledc_channel_inited" not in panel_combined:
        failures.append("[OTA SAFETY] panel_power.cpp / hal_panel_power.h must provide ledc_channel_inited guard")
    if "esp_lcd_dpi_panel_register_event_callbacks" in panel_combined:
        failures.append("[DSI IRAM] panel_power contains retired esp_lcd_dpi_panel_register_event_callbacks")

    bridge_cpp = (ROOT / "components" / "latter_core" / "esphome_bridge.cpp").read_text(encoding="utf-8")
    bridge_hdr = (ROOT / "include" / "esphome_bridge.h").read_text(encoding="utf-8")
    bridge_combined = bridge_hdr + "\n" + bridge_cpp
    if "stop_poller()" not in bridge_combined or "800U" not in bridge_combined or "400U" not in bridge_combined:
        failures.append("[OTA SAFETY] esphome_bridge must call stop_poller() and throttle OTA redraws (400U/800U)")
    if "continue_conversation_" in strip_cpp_comments(bridge_combined):
        failures.append("[VOICE LIFECYCLE] esphome_bridge must never mutate continue_conversation_")

    pm_callers = [
        yp.relative_to(ROOT).as_posix()
        for yp in iter_repo_files(ROOT / "modules") + iter_repo_files(ROOT / "boards") + iter_repo_files(ROOT / "guis")
        if yp.suffix.lower() == ".yaml"
        and re.search(r"\bpm_apply\s*\(", strip_cpp_comments(strip_yaml_comments(yp.read_text(encoding="utf-8"))))
    ]
    if len(pm_callers) != 1 or pm_callers[0] not in ALLOWED_PM_APPLY_OWNERS:
        failures.append(
            f"[AWAKE LOCK OWNER] pm_apply() must be called from one power module {sorted(ALLOWED_PM_APPLY_OWNERS)} "
            f"(found: {pm_callers})"
        )

    for path in iter_repo_files(ROOT / "include") + iter_repo_files(ROOT / "components"):
        if path.suffix.lower() not in {".h", ".hpp", ".cpp"}:
            continue
        rel = path.relative_to(ROOT).as_posix()
        clean = strip_cpp_comments(path.read_text(encoding="utf-8"))
        for m_prof in re.finditer(r'ESP_LOGI\s*\([^;]*?"\[PROF:([A-Z0-9_]+)\]', clean):
            tag = m_prof.group(1)
            window = clean[max(0, m_prof.start() - PROF_WINDOW_CHARS) : m_prof.start()]
            if "prof_enabled" not in window and "profiler" not in window and "profile_logging_" not in window:
                failures.append(f"[PROF GATE] {rel} logs [PROF:{tag}] at INFO without profiler_enabled() gate")


def verify_board_hal_and_security(failures: list[str]) -> None:
    """Run strapping, component, header-order, board/audio/sleep HAL, RPC, and OTA security checks."""
    audio_yamls = [
        p
        for p in sorted((ROOT / "modules").glob("*.yaml"))
        if p.name.startswith(("audio_", "voice_assistant"))
    ]
    audio_combined = "\n".join(p.read_text(encoding="utf-8") for p in audio_yamls)
    sound_refs = sorted(set(re.findall(r"sounds/\$\{audio_out_rate\}/([a-z0-9_]+\.wav)", audio_combined)))
    if "rtttl:" in audio_combined:
        failures.append("[NO RTTTL] modules/audio_*.yaml + voice_assistant.yaml must not declare rtttl")
    if "play_alarm_sound" not in audio_combined or "resume_music_after_va" not in audio_combined:
        failures.append("[VOICE LIFECYCLE] audio/voice modules missing play_alarm_sound or resume_music_after_va")

    board_files = sorted((ROOT / "boards").glob("*.yaml"))
    for bf in board_files:
        bf_uncommented = strip_yaml_comments(bf.read_text(encoding="utf-8"))
        if "-DLATTER_HAS_RTC=" not in bf_uncommented:
            failures.append(f"[BOARD TRAITS] {bf.name} missing -DLATTER_HAS_RTC=0/1")
        m_holds = re.search(
            r"(?m)^\s*-\s*id:\s*hal_release_deep_sleep_holds\b([\s\S]*?)(?=^\s*-\s*id:|\Z)", bf_uncommented
        )
        if m_holds and ("es8311_dac" in m_holds.group(1) or "hal_set_display_brightness" in m_holds.group(1)):
            failures.append(f"[BOOT SAFETY] {bf.name} hal_release_deep_sleep_holds touches codec/backlight before setup()")

    beacon_init_py = (ROOT / "components" / "latter_beacon" / "__init__.py").read_text(encoding="utf-8")
    for bf in [*board_files, ROOT / "examples" / "custom_display_template.yaml"]:
        _check_single_board_target(bf, sound_refs, beacon_init_py, failures)

    _verify_core_cpp_and_prof_gates(failures)
    verify_strapping_pins(failures)
    verify_component_deps(failures)
    verify_header_order(failures)
    _verify_duplex_rpc_and_lossy_ui(failures)
    _verify_ota_and_api_security(failures)


def _verify_duplex_rpc_and_lossy_ui(failures: list[str]) -> None:
    for folder in ("include", "components", "modules", "boards", "guis", "devices", "examples"):
        for path in iter_repo_files(ROOT / folder):
            suffix = path.suffix.lower()
            if suffix not in {".h", ".hpp", ".cpp", ".yaml"}:
                continue
            txt = path.read_text(encoding="utf-8")
            rel = path.relative_to(ROOT).as_posix()
            m_hd = RETIRED_HALF_DUPLEX_RE.search(txt)
            if m_hd:
                failures.append(f"[FULL DUPLEX] {rel} references retired symbol {m_hd.group(0)}")
            if suffix == ".yaml":
                uncommented_y = strip_yaml_comments(txt)
                if re.search(r"(?m)^\s*-?\s*homeassistant\.action\s*:", uncommented_y):
                    failures.append(f"[ZERO PRIVILEGE RPC] {rel} uses forbidden homeassistant.action")
                if re.search(r"\bdevice_slug\b", uncommented_y):
                    failures.append(f"[DIRECT PUSH] {rel} references retired device_slug substitution")
            if folder in ("include", "components", "guis") and LOSSY_UI_RE.search(txt):
                uncommented = strip_cpp_comments(txt) if suffix != ".yaml" else strip_yaml_comments(txt)
                if LOSSY_UI_RE.search(uncommented):
                    failures.append(f"[UI INVARIANT] {rel} uses forbidden it.image/draw_pixels_at or lossy mask")
