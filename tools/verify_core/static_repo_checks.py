"""UTF-8/JSON/LF/Markdown checks, AGENTS.md budget, terminology, product options, and SDK/version lockstep."""

from __future__ import annotations

import base64
import json
import re
from pathlib import Path
from typing import Final

from .common import (
    BANNED_TERMS_RE,
    DEVICE_SLUG_RE,
    ROOT,
    TEXT_EXTS,
    extract_substitutions,
    iter_repo_files,
    strip_cpp_comments,
    strip_yaml_comments,
)

NOISE_KEY_BYTES: Final[int] = 32
AGENTS_MD_MAX_BYTES: Final[int] = 12000
MIN_SDK_REV: Final[int] = 2026112201
REQUIRED_PRODUCT_OPTIONS: Final[tuple[str, ...]] = (
    "Minimal Digital",
    "Modern Analog",
    "Hybrid Digital",
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
)


def _check_markdown_links_in_file(path: Path, rel: str, txt: str, failures: list[str]) -> None:
    """Validate relative links inside a Markdown file outside fenced code blocks."""
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
            if rel_target and not (path.parent / rel_target).resolve().exists():
                failures.append(f"[MD LINK] {rel}:{line_no} broken relative link '{href}'")


def _check_yaml_noise_keys(rel: str, txt: str, failures: list[str]) -> None:
    """Verify literal Noise encryption keys decode to 32 bytes."""
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
        if not in_enc:
            continue
        if indent <= enc_indent:
            in_enc = False
            continue
        m_key = re.match(r"^\s*key\s*:\s*(.+?)\s*$", stripped)
        if not m_key:
            continue
        val = m_key.group(1).strip().strip('"').strip("'")
        if val.startswith(("${", "!secret")):
            continue
        try:
            decoded_key = base64.b64decode(val, validate=True)
        except ValueError as exc:
            failures.append(f"[API KEY] {rel}:{line_no} encryption key is not valid base64: {exc}")
        else:
            if len(decoded_key) != NOISE_KEY_BYTES:
                failures.append(
                    f"[API KEY] {rel}:{line_no} encryption key decodes to {len(decoded_key)} bytes (must be 32)"
                )


def verify_encoding_and_hygiene(failures: list[str]) -> None:
    """Enforce UTF-8 no-BOM, JSON syntax, LF shell endings, Markdown links, and Noise keys."""
    for path in iter_repo_files(ROOT):
        suffix = path.suffix.lower()
        if suffix not in TEXT_EXTS:
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
        if suffix == ".sh" and b"\r" in raw:
            failures.append(f"[SHELL LF] {rel} contains CRLF line endings (must be LF)")
        if suffix == ".json":
            try:
                json.loads(txt)
            except ValueError as exc:
                failures.append(f"[JSON] {rel} failed JSON parse: {exc}")
        elif suffix == ".md":
            _check_markdown_links_in_file(path, rel, txt, failures)
        elif suffix in {".yaml", ".yml"}:
            _check_yaml_noise_keys(rel, txt, failures)


def verify_agents_md(failures: list[str]) -> None:
    """Validate AGENTS.md presence, frontmatter prohibition, and byte budget."""
    agents_md = ROOT / "AGENTS.md"
    if not agents_md.is_file():
        failures.append("[AGENTS MD] AGENTS.md is missing")
        return
    atxt = agents_md.read_text(encoding="utf-8")
    if atxt.startswith("---"):
        failures.append("[AGENTS MD] Standalone AGENTS.md must not start with YAML frontmatter")
    size_bytes = agents_md.stat().st_size
    if size_bytes >= AGENTS_MD_MAX_BYTES:
        failures.append(f"[AGENTS MD] AGENTS.md ({size_bytes} B) exceeds {AGENTS_MD_MAX_BYTES} B cap")


def _check_cpp_hygiene_file(rel: str, txt: str, failures: list[str]) -> None:
    clean_cpp = strip_cpp_comments(txt)
    m_slug = DEVICE_SLUG_RE.search(clean_cpp)
    if m_slug:
        failures.append(
            f"[TRAITS NOT NAMES] {rel} hardcodes product slug {m_slug.group(0)!r} (use LATTER_* traits)"
        )
    if re.search(
        r"(?:__attribute__\s*\(\([^)]*\)\)|IRAM_ATTR|DRAM_ATTR|RTC_NOINIT_ATTR)\s*\[\[",
        clean_cpp,
    ):
        failures.append(
            f"[GCC ATTR ORDER] {rel} places C++ [[...]] attribute after GNU __attribute__/IRAM_ATTR "
            "(put [[...]] before __attribute__)"
        )
    if not rel.startswith("components/latter_beacon/") and not re.search(r"\bnamespace\s+esphome\b", clean_cpp):
        m_unqual_hal = re.search(r"(?<![:\w.>])(?:millis|micros)\s*\(", clean_cpp)
        if m_unqual_hal:
            failures.append(
                f"[ESPHOME NAMESPACE] {rel} calls unqualified {m_unqual_hal.group(0).strip()!r} "
                "outside namespace esphome (use esphome::millis() / esphome::micros())"
            )


def _check_yaml_hygiene_file(rel: str, txt: str, failures: list[str]) -> None:
    yclean = strip_yaml_comments(txt)
    if re.search(r"(?m)^globals\s*:", yclean):
        failures.append(f"[ZERO YAML GLOBALS] {rel} declares forbidden YAML globals: block")
    m_dead = re.search(
        r"(?m)^\s+(screen_w|screen_h|art_shape|default_clock_face|default_start_app|"
        r"default_art_mode|default_screensaver_style|default_album_sort|"
        r"default_artist_album_sort|default_podcast_sort)\s*:",
        yclean,
    )
    if m_dead:
        failures.append(f"[DEAD SUBSTITUTION] {rel} declares retired '{m_dead.group(1)}' substitution")
    if rel.startswith("devices/"):
        m_dup = re.search(
            r"(?m)^\s+(font_[a-z_]+_size|art_resize|sendspin_art_resize|sendspin_buffer_size|"
            r"announcement_buffer_size|mww_internal_ram|audio_[a-z_]+|api_reboot_timeout)\s*:",
            yclean,
        )
        if m_dup:
            failures.append(
                f"[DEVICE SUBS] {rel} duplicates board-owned substitution '{m_dup.group(1)}' (keep in boards/*.yaml)"
            )
    for sec in ("sensor", "text_sensor", "binary_sensor"):
        m_sec = re.search(rf"(?m)^{sec}\s*:\s*\n((?:[ \t]+.*\n?)*)", yclean)
        if m_sec and re.search(r"(?m)^\s*-\s*platform:\s*homeassistant\b", m_sec.group(1)):
            failures.append(
                f"[DANGLING HA SENSOR] {rel} subscribes {sec} via platform: homeassistant (use Protocol v3)"
            )


def verify_terminology_and_yaml_globals(failures: list[str]) -> None:
    """Enforce terminology, trait-based C++ branching, zero YAML globals, and descriptive product options."""
    for folder in ("components", "include", "modules", "guis", "boards", "devices", "examples"):
        for path in iter_repo_files(ROOT / folder):
            suffix = path.suffix.lower()
            if suffix not in TEXT_EXTS:
                continue
            rel = path.relative_to(ROOT).as_posix()
            txt = path.read_text(encoding="utf-8")
            if folder != "examples":
                m_term = BANNED_TERMS_RE.search(txt)
                if m_term:
                    failures.append(f"[TERMINOLOGY] {rel} contains banned term {m_term.group(0)!r}")
            if folder in ("include", "components") and suffix in {".h", ".hpp", ".cpp"}:
                _check_cpp_hygiene_file(rel, txt, failures)
            if suffix in {".yaml", ".yml"}:
                _check_yaml_hygiene_file(rel, txt, failures)

    option_headers = sorted((ROOT / "include" / "state").glob("*.h")) + sorted((ROOT / "include").glob("hal_*.h"))
    combined_opts = "\n".join(p.read_text(encoding="utf-8") for p in option_headers if p.is_file())
    failures.extend(
        f"[PRODUCT OPTIONS] include/state/*.h + include/hal_*.h missing option {req_opt!r}"
        for req_opt in REQUIRED_PRODUCT_OPTIONS
        if f'"{req_opt}"' not in combined_opts
    )


def verify_sdk_and_version_lockstep(failures: list[str]) -> None:
    """Verify LATTER_SDK_REV == LATTER_YAML_REV, ESPHome version pins, and v3.0.0 lockstep."""
    sdk_revs: list[int] = []
    yaml_revs: list[int] = []
    scan_files = (
        iter_repo_files(ROOT / "include")
        + iter_repo_files(ROOT / "components")
        + iter_repo_files(ROOT / "modules")
    )
    for path in scan_files:
        if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".yaml"}:
            continue
        txt = path.read_text(encoding="utf-8")
        sdk_revs.extend(int(m.group(1)) for m in re.finditer(r"(?m)^\s*#\s*define\s+LATTER_SDK_REV\s+(\d+)[ULul]*\b", txt))
        yaml_revs.extend(
            int(m.group(1))
            for m in re.finditer(
                r"constexpr\s+(?:long|uint32_t|unsigned(?:\s+long)?)\s+LATTER_YAML_REV\s*=\s*(\d+)[ULul]*\s*;", txt
            )
        )
    if not sdk_revs or not yaml_revs or set(sdk_revs) != set(yaml_revs):
        failures.append(
            f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs}) and LATTER_YAML_REV ({yaml_revs}) must exist and match"
        )
    elif min(sdk_revs) < MIN_SDK_REV:
        failures.append(f"[SDK REV LOCKSTEP] LATTER_SDK_REV ({sdk_revs[0]}) must be >= {MIN_SDK_REV}")

    bridge_h = ROOT / "include" / "esphome_bridge.h"
    if not bridge_h.is_file() or "VERSION_CODE(2026, 11, 0)" not in bridge_h.read_text(encoding="utf-8"):
        failures.append("[ESPHOME PIN LOCKSTEP] include/esphome_bridge.h must static_assert VERSION_CODE(2026, 11, 0)")

    base_txt = (ROOT / "modules" / "base.yaml").read_text(encoding="utf-8")
    if not re.search(r'(?m)^\s*latter_asset_ref:\s*"main"\s*$', base_txt):
        failures.append('[ASSET REF LOCKSTEP] modules/base.yaml must define `latter_asset_ref: "main"`')
    for mod_rel in ("modules/audio_pipeline.yaml", "modules/voice_assistant.yaml"):
        mod_txt = (ROOT / mod_rel).read_text(encoding="utf-8")
        if "/esphome-latter-core/main/" in mod_txt:
            failures.append(
                f"[ASSET REF LOCKSTEP] {mod_rel} hardcodes /esphome-latter-core/main/ (use ${{latter_asset_ref}})"
            )
    m_ver = re.search(r'(?m)^\s*version:\s*"([^"]+)"', base_txt)
    if not m_ver:
        failures.append("[VERSION LOCKSTEP] modules/base.yaml missing version substitution")
    else:
        core_ver = m_ver.group(1)
        for path in sorted((ROOT / "devices").glob("*.yaml")) + sorted((ROOT / "examples").glob("*.yaml")):
            psubs = extract_substitutions([path])
            if "version" in psubs:
                failures.append(
                    f"[VERSION LOCKSTEP] {path.relative_to(ROOT).as_posix()} duplicates version "
                    f"substitution ({psubs['version']!r}); single-source in modules/base.yaml"
                )
        idx_txt = (ROOT / "index.html").read_text(encoding="utf-8")
        if f">v{core_ver}<" not in idx_txt:
            failures.append(f"[VERSION LOCKSTEP] index.html nav-badge does not match v{core_ver}")

    _verify_ci_and_device_pins(failures)


def _verify_ci_and_device_pins(failures: list[str]) -> None:
    val_txt = (ROOT / ".github" / "workflows" / "validate.yml").read_text(encoding="utf-8")
    rel_txt = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
    canary_path = ROOT / ".github" / "workflows" / "esphome-canary.yml"
    if not canary_path.is_file():
        failures.append("[CI CANARY] .github/workflows/esphome-canary.yml is missing")
    for wf_path in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        wf_rel = wf_path.relative_to(ROOT).as_posix()
        wf_txt = wf_path.read_text(encoding="utf-8")
        if not re.search(r"(?m)^permissions:\s*\n\s+contents:\s*read\b", wf_txt):
            failures.append(f"[CI PERMISSIONS] {wf_rel} must set workflow-level `permissions: contents: read`")
        for line_no, raw_ln in enumerate(wf_txt.splitlines(), 1):
            m_uses = re.match(r"^\s*(?:-\s*)?uses:\s*([^\s#]+)", raw_ln)
            if not m_uses:
                continue
            ref = m_uses.group(1)
            if ref.startswith("docker://"):
                if not re.search(r"@sha256:[0-9a-f]{64}$", ref):
                    failures.append(f"[CI SHA PIN] {wf_rel}:{line_no} unpinned docker image {ref!r}")
            elif not re.search(r"@[0-9a-f]{40}$", ref):
                failures.append(f"[CI SHA PIN] {wf_rel}:{line_no} unpinned GitHub Action {ref!r}")

    ci_pins = re.findall(r"esphome==(\S+)", val_txt) + re.findall(
        r"ghcr\.io/esphome/esphome:([^@\s]+)", rel_txt
    )
    dev_mins: set[str] = set()
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
    if "desk-assistant-rev1" not in val_txt:
        failures.append("[DEV GATE] validate.yml must compile desk-assistant-rev1 silicon variant")
    if re.search(r"(?m)^\s*push:\s*\n\s+tags:", rel_txt):
        failures.append("[RELEASE GATE] release.yml must not duplicate triggers with `push: tags:`")
    if "test_latter_state" in val_txt or "test_all.cpp" in val_txt:
        failures.append("[DEV GATE] validate.yml still references retired single-file host test")


def verify_static_repo_invariants(failures: list[str]) -> None:
    """Run all general static repository hygiene and lockstep checks."""
    verify_encoding_and_hygiene(failures)
    verify_agents_md(failures)
    verify_terminology_and_yaml_globals(failures)
    verify_sdk_and_version_lockstep(failures)
