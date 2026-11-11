#!/usr/bin/env python3
"""Cross-platform static architecture, HAL, UI, and security verifier for esphome-latter-core.

Runs in <1 second with Python 3.11+ standard library only (zero third-party dependencies),
enforcing repository guardrails identically in local development and GitHub Actions CI.
"""

from __future__ import annotations

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
BANNED_TERMS_RE = re.compile(r"\b(household|households)\b", re.IGNORECASE)
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
                # Inline mapping form: { number: GPIO10, ignore_strapping_warning: true }
                m_inline = re.search(r"\bnumber\s*:\s*([^\s,}#]+)", code)
                if m_inline:
                    pin_expr = m_inline.group(1).strip("'\"")
                else:
                    # Multi-line mapping form: find sibling `number:` at the same mapping indentation.
                    stripped = code.lstrip(" ")
                    if stripped.startswith("- "):
                        key_indent = len(code) - len(stripped) + 2
                    else:
                        key_indent = len(code) - len(stripped)
                    # Search upward to start of enclosing mapping block
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
                    # Search downward in same mapping block if `number:` came after `ignore_strapping_warning:`
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
    for p in sorted(base.rglob("*")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.is_file():
            files.append(p)
    return files


def _strip_cpp_comments(text: str) -> str:
    no_block = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", no_block)


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
    """Ensure no component lists any of its own AUTO_LOAD targets in DEPENDENCIES.

    ESPHome validates DEPENDENCIES against explicit top-level YAML keys before running a component's
    AUTO_LOAD, so placing an AUTO_LOAD target in DEPENDENCIES fails whenever users omit the auto-loaded
    sub-component from YAML.
    """
    comp_root = ROOT / "components"
    if not comp_root.is_dir():
        return
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
        if comp_name == "latter_core" and not {"latter_hal", "latter_ui"}.issubset(autoloads):
            failures.append(
                f"[COMPONENT DEPS] {rel}: AUTO_LOAD must wire latter_hal and latter_ui (found {sorted(autoloads)})"
            )


def verify_header_order(failures: list[str]) -> None:
    """Ban circular #include \"esphome.h\" in headers and ensure esphome/core/defines.h precedes #ifdef USE_*."""
    for folder in ("include", "components"):
        base = ROOT / folder
        if not base.is_dir():
            continue
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


def cpp_and_ui_sources() -> list[Path]:
    out: list[Path] = []
    for folder in ("include", "components", "guis"):
        base = ROOT / folder
        if base.is_dir():
            for p in sorted(base.rglob("*")):
                if p.is_file() and p.suffix in {".h", ".hpp", ".cpp", ".yaml"}:
                    out.append(p)
    return out


def main() -> int:
    failures: list[str] = []

    # 1. UTF-8 no-BOM, JSON validity, and LF shell scripts
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

    # 2. No mixins/ directory
    if (ROOT / "mixins").exists():
        failures.append("[NO MIXINS] mixins/ directory must not exist; shared modules belong under modules/")

    # 3. Terminology checks on source/YAML (docs document the rule itself)
    for folder in ("components", "include", "modules", "guis", "boards", "devices"):
        base = ROOT / folder
        if not base.is_dir():
            continue
        for path in iter_repo_files(base):
            if path.suffix.lower() not in TEXT_EXTS:
                continue
            txt = path.read_text(encoding="utf-8")
            m = BANNED_TERMS_RE.search(txt)
            if m:
                failures.append(
                    f"[TERMINOLOGY/PRIVACY] {path.relative_to(ROOT).as_posix()} contains banned term {m.group(0)!r}"
                )

    # 4. SDK & YAML revision lockstep
    sdk_revs: list[int] = []
    yaml_revs: list[int] = []
    for path in cpp_and_ui_sources() + list((ROOT / "modules").rglob("*.yaml")):
        if not path.is_file():
            continue
        txt = path.read_text(encoding="utf-8")
        for m in re.finditer(r"#define\s+LATTER_SDK_REV\s+(\d+)L?", txt):
            sdk_revs.append(int(m.group(1)))
        for m in re.finditer(r"constexpr\s+long\s+LATTER_YAML_REV\s*=\s*(\d+)L?\s*;", txt):
            yaml_revs.append(int(m.group(1)))
    if not sdk_revs or not yaml_revs or set(sdk_revs) != set(yaml_revs):
        failures.append(
            f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs}) and LATTER_YAML_REV ({yaml_revs}) must exist and match"
        )
    elif min(sdk_revs) < 2026110261:
        failures.append(f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs[0]}) must be >= 2026110261")

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
    if len(dev_mins) != 1 or not ci_pins or any(p != next(iter(dev_mins)) for p in ci_pins):
        failures.append(f"[ESPHOME PIN LOCKSTEP] device min_version {sorted(dev_mins)} != CI pins {ci_pins}")
    if "compile devices/" not in val_txt or "\n  promote:" not in val_txt or "refs/heads/main" not in val_txt:
        failures.append("[DEV GATE] validate.yml must compile devices and fast-forward main in promote job")

    # 7. Board capability flags, full-duplex 48 kHz audio, and light-sleep safety (with !include expansion)
    audio_voice_path = ROOT / "modules" / "audio_voice.yaml"
    sound_refs = sorted(
        set(re.findall(r"sounds/\$\{audio_out_rate\}/([a-z0-9_]+\.wav)", audio_voice_path.read_text(encoding="utf-8")))
    )
    if "rtttl:" in audio_voice_path.read_text(encoding="utf-8"):
        failures.append("[NO RTTTL] modules/audio_voice.yaml must not declare rtttl")

    board_files = sorted((ROOT / "boards").glob("*.yaml"))
    trait_targets = board_files + [ROOT / "examples" / "custom_display_template.yaml"]
    for bf in trait_targets:
        expanded = expand_local_yaml_includes(bf)
        for flag in REQUIRED_BOARD_FLAGS:
            if f"-D{flag}=" not in expanded:
                failures.append(f"[BOARD TRAITS] {bf.name} missing -D{flag}=...")
        for sub in ("sendspin_buffer_size", "mww_internal_ram", "audio_out_rate", "audio_out_channels"):
            if not re.search(rf"(?m)^\s+{sub}:\s*\S", expanded):
                failures.append(f"[BOARD AUDIO TRAITS] {bf.name} missing substitution '{sub}'")
        if re.search(r"platform:\s*resampler[\s\S]{0,300}?output_speaker:", expanded):
            failures.append(f"[NO RESAMPLING] {bf.name} declares a forbidden resampler speaker")
        if "full_duplex: true" not in expanded:
            failures.append(f"[FULL DUPLEX] {bf.name} must configure full_duplex: true on its I2S bus")
        m_out = re.search(r'(?m)^\s+audio_out_rate:\s*"?(\d+)', expanded)
        m_in = re.search(r'(?m)^\s+audio_in_rate:\s*"?(\d+)', expanded)
        if m_out and m_in and m_out.group(1) != m_in.group(1):
            if not re.search(r"platform:\s*resampler\s*\n\s*id:\s*latter_mic\b", expanded):
                failures.append(f"[FULL DUPLEX] {bf.name} needs resampler microphone 'latter_mic'")
        if m_out:
            for snd in sound_refs:
                if not (ROOT / "sounds" / m_out.group(1) / snd).is_file():
                    failures.append(f"[SOUND FILES] Missing sounds/{m_out.group(1)}/{snd} for {bf.name}")
        uncommented_yaml = "\n".join(
            line for line in expanded.splitlines() if not line.lstrip().startswith("#")
        )
        if re.search(r"\bCONFIG_RTC_CLK_SRC_INT_8MD256\s*:", uncommented_yaml):
            failures.append(f"[LIGHT SLEEP] {bf.name} must never enable CONFIG_RTC_CLK_SRC_INT_8MD256")
        if re.search(r"\bCONFIG_CACHE_L2_CACHE_LINE_128B\s*:", uncommented_yaml):
            failures.append(f"[P4 SDIO DMA] {bf.name} must never enable CONFIG_CACHE_L2_CACHE_LINE_128B")

    # 7b. Strapping-pin, component dependency & C++ header-order validation across all boards, headers, and sources
    verify_strapping_pins(failures)
    verify_component_deps(failures)
    verify_header_order(failures)

    # 8. Retired half-duplex names, zero-privilege RPC invariants & UI rendering invariants
    for folder in ("include", "components", "modules", "boards", "guis", "devices", "examples"):
        base = ROOT / folder
        if not base.is_dir():
            continue
        for path in iter_repo_files(base):
            if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".yaml"}:
                continue
            txt = path.read_text(encoding="utf-8")
            rel = path.relative_to(ROOT).as_posix()
            m_hd = RETIRED_HALF_DUPLEX_RE.search(txt)
            if m_hd:
                failures.append(f"[FULL DUPLEX] {rel} references retired symbol {m_hd.group(0)}")
            if path.suffix.lower() == ".yaml":
                uncommented_lines = [ln for ln in txt.splitlines() if not ln.lstrip().startswith("#")]
                uncommented_y = "\n".join(uncommented_lines)
                if re.search(r"(?m)^\s*-?\s*homeassistant\.action\s*:", uncommented_y):
                    failures.append(f"[ZERO PRIVILEGE RPC] {rel} uses forbidden homeassistant.action")
                if re.search(r"\bdevice_slug\b", uncommented_y):
                    failures.append(f"[DIRECT PUSH] {rel} references retired device_slug substitution")
            if folder in ("include", "components", "guis") and LOSSY_UI_RE.search(txt):
                # Strip C++ single-line comments before flagging lossy masks / draw_pixels_at
                uncommented = re.sub(r"//.*", "", txt)
                if LOSSY_UI_RE.search(uncommented):
                    failures.append(f"[UI INVARIANT] {rel} uses forbidden it.image/draw_pixels_at or lossy mask")

    # 9. Noise-encrypted ESPHome OTA + HTTPS pull-OTA in core_system.yaml; zero legacy OTA module or password
    retired_ota_mod = "ota_" + "esphome.yaml"
    retired_ota_pwd = "ota_" + "password"
    retired_ota_pwd_re = re.compile(r"\b" + retired_ota_pwd + r"\b")
    if (ROOT / "modules" / retired_ota_mod).exists():
        failures.append(f"[OTA SECURITY] modules/{retired_ota_mod} must not exist (folded into core_system.yaml)")
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

    core_sys_txt = (ROOT / "modules" / "features" / "core_system.yaml").read_text(encoding="utf-8")
    core_sys_uncommented = "\n".join(
        ln for ln in core_sys_txt.splitlines() if not ln.lstrip().startswith("#")
    )
    if not re.search(r"(?m)^\s*-\s*platform:\s*http_request\b", core_sys_uncommented) or not re.search(
        r"(?m)^\s*-\s*platform:\s*esphome\s*\n\s*id:\s*ota_esphome\b", core_sys_uncommented
    ):
        failures.append(
            "[OTA SECURITY] modules/features/core_system.yaml must define both ota http_request and esphome (id: ota_esphome)"
        )
    if (
        len(re.findall(r"\bhandle_ota_begin\s*\(", core_sys_uncommented)) != 2
        or len(re.findall(r"\bhandle_ota_progress\s*\(", core_sys_uncommented)) != 2
    ):
        failures.append(
            "[OTA SECURITY] modules/features/core_system.yaml must wire handle_ota_begin and handle_ota_progress on both OTA platforms"
        )
    if not re.search(r"(?m)^\s*ota:\s*\n\s*-\s*id:\s*!extend\s+ota_esphome\s*\n\s*encryption:\s*$", val_txt):
        failures.append(
            "[OTA SECURITY] .github/workflows/validate.yml Device Builder wrapper must extend ota_esphome with Noise encryption:"
        )

    # 9b. ESPHome 2026.11 Native API standards: Noise encryption, client-initiated outgoing_connection, and batch_delay: 0ms
    for api_mod_rel in ("modules/features/core_system.yaml", "modules/base.yaml"):
        api_mod_txt = (ROOT / api_mod_rel).read_text(encoding="utf-8")
        api_mod_uncommented = "\n".join(
            ln for ln in api_mod_txt.splitlines() if not ln.lstrip().startswith("#")
        )
        if not re.search(r"(?m)^\s*batch_delay:\s*0ms\b", api_mod_uncommented):
            failures.append(f"[API STANDARDS] {api_mod_rel} must set batch_delay: 0ms under api:")
        if not re.search(r"(?m)^\s*encryption:\s*$", api_mod_uncommented):
            failures.append(f"[API STANDARDS] {api_mod_rel} must declare encryption: under api:")
        if not re.search(
            r"(?m)^\s*outgoing_connection:\s*\n\s*delay:\s*\$\{api_outgoing_delay\}",
            api_mod_uncommented,
        ):
            failures.append(
                f"[API STANDARDS] {api_mod_rel} must declare outgoing_connection: with delay: ${{api_outgoing_delay}}"
            )

    # 10. Stage 4 compiled UI architecture, dirty-band DMA invariants, and C++20 host unit tests
    if (ROOT / "guis" / "universal" / "touch_handlers.yaml").exists():
        failures.append("[TOUCH ENGINE] guis/universal/touch_handlers.yaml must not exist (replaced by gesture_engine.cpp)")
    if not (ROOT / "components" / "latter_ui" / "gesture_engine.cpp").is_file():
        failures.append("[TOUCH ENGINE] components/latter_ui/gesture_engine.cpp missing")
    gui_yaml_txt = (ROOT / "guis" / "universal" / "gui.yaml").read_text(encoding="utf-8")
    draw_pix_calls = re.findall(r"(?<![\w.>])it\.draw_pixel_at\s*\(", gui_yaml_txt)
    if (
        "dirty_band()" not in gui_yaml_txt
        or ".begin_frame(" not in gui_yaml_txt
        or "render_system_takeovers(" not in gui_yaml_txt
        or "touch_engine()" not in gui_yaml_txt
        or not (2 <= len(draw_pix_calls) <= 4)
    ):
        failures.append(
            "[DIRTY BAND / COMPOSITOR] guis/universal/gui.yaml must wire dirty_band().begin_frame, "
            "render_system_takeovers, touch_engine(), and 2..4 it.draw_pixel_at latch calls"
        )

    host_test_targets = [
        ROOT / "tests" / "host" / "test_all.cpp",
        ROOT / "tests" / "host" / "test_latter_state.cpp",
    ]
    missing_tests = [p.name for p in host_test_targets if not p.is_file()]
    if missing_tests:
        failures.append(f"[HOST TESTS] tests/host missing: {', '.join(missing_tests)}")
    else:
        gpp = shutil.which("g++") or shutil.which("clang++")
        if gpp:
            with tempfile.TemporaryDirectory() as td:
                for idx, test_cpp in enumerate(host_test_targets):
                    bin_path = Path(td) / (f"latter_host_test_{idx}.exe" if os.name == "nt" else f"latter_host_test_{idx}")
                    comp = subprocess.run(
                        [gpp, "-std=c++20", "-O2", "-Wall", "-Wextra", str(test_cpp), "-o", str(bin_path)],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    if comp.returncode != 0:
                        failures.append(f"[HOST TESTS COMPILE] {test_cpp.name}: {(comp.stderr or comp.stdout).strip()}")
                    else:
                        run = subprocess.run([str(bin_path)], capture_output=True, text=True, check=False)
                        if run.returncode != 0:
                            failures.append(f"[HOST TESTS RUN] {test_cpp.name}: {(run.stderr or run.stdout).strip()}")
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
                    bat_lines = [
                        "@echo off",
                        f'call "{vcvars}" >nul',
                        "if errorlevel 1 exit /b 1",
                    ]
                    for idx, test_cpp in enumerate(host_test_targets):
                        exe_path = td_path / f"latter_host_test_{idx}.exe"
                        obj_path = td_path / f"latter_host_test_{idx}.obj"
                        bat_lines.append(
                            f'cl /nologo /utf-8 /std:c++20 /O2 /W4 /EHsc "{test_cpp}" '
                            f'"/Fe:{exe_path}" "/Fo:{obj_path}"'
                        )
                        bat_lines.append("if errorlevel 1 exit /b 1")
                        bat_lines.append(f'"{exe_path}"')
                        bat_lines.append("if errorlevel 1 exit /b 1")
                    bat_path = td_path / "run_host_tests.cmd"
                    bat_path.write_text("\r\n".join(bat_lines) + "\r\n", encoding="utf-8")
                    res = subprocess.run(["cmd.exe", "/c", str(bat_path)], capture_output=True, text=True, check=False)
                    if res.returncode != 0:
                        failures.append(f"[HOST TESTS MSVC] {(res.stderr or res.stdout).strip()}")

    if failures:
        print(f"verify_core.py FAILED ({len(failures)} issue(s)):")
        for item in failures:
            print(f"  - {item}")
        return 1

    print("verify_core.py PASSED (0 issues)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
