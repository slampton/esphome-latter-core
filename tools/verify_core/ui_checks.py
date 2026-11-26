"""Device UI fast-primitives, dirty-band, D-070/D-072/D-073 design drift, WCAG contrast, and mm checks."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Final

from .common import (
    RAW_IT_DRAW_RE,
    ROOT,
    collect_yaml_include_chain,
    extract_substitutions,
    parse_latter_core_flags,
    strip_cpp_comments,
    strip_cpp_strings_and_comments,
    strip_yaml_comments,
)

MIN_LATCH_CALLS: Final[int] = 2
MAX_LATCH_CALLS: Final[int] = 4
CARD_BOX_RAD_ARG_IDX: Final[int] = 7
MIN_CONTRAST_RATIO: Final[float] = 4.5
MIN_SMALL_FONT_MM: Final[float] = 1.95
MIN_MICRO_FONT_MM: Final[float] = 1.64
SRGB_LINEAR_CUTOFF: Final[float] = 0.04045
WEARABLE_DEFAULT_CORNER_R: Final[int] = 98
RECT_DEFAULT_CORNER_R: Final[int] = 0

LOW_LEVEL_GFX_FILES: Final[frozenset[str]] = frozenset(
    {
        "ui_surface.h",
        "ui_esphome_display.h",
        "ui_gfx.h",
        "ui_gfx.cpp",
    }
)
MODE_BAR_FAMILIES: Final[frozenset[str]] = frozenset(
    {"area_hub", "weather", "home_hub", "time", "calendar", "music", "cameras"}
)
CHROME_TOKEN_FAMILIES: Final[frozenset[str]] = frozenset(
    {"cameras", "area_hub", "home_hub", "calendar", "music", "time", "system", "remote", "pinned"}
)
LIST_SLOT5_FAMILIES: Final[frozenset[str]] = frozenset({"music", "cameras", "home_hub"})
RETIRED_UI_TOKENS: Final[tuple[str, ...]] = (
    "MUSIC_HEADER_ENQUEUE_BTN",
    "compute_music_bento_tile_geom",
    "open_music_header_action_sheet",
    "parse_intercom_targets_list",
    "INTERCOM_UNLOCK_BTN",
    "INTERCOM_SEG_CALL",
    "ENTRY_CARD_BASE",
    "resolve_doorbell_quick_unlock_target",
    "kMaxContentColW",
    "SCREEN_R",
)
RETIRED_SCALE_CALL_RE: Final[re.Pattern[str]] = re.compile(
    r"(?<!\.)\bscale_(?:x|y|s|w|min|min_f)(?:_for)?\s*\("
)
BTN_WIDGET_PAT: Final[str] = (
    r"\b(ui::(?:action_pill(?:_centered|_row)?|setting_cycle_row|slider_card_row|"
    r"list_row|grid_tile|toggle_card_row|segmented_toggle|keypad_button))"
)


def _srgb_rel_luminance(hex_rgb: str) -> float:
    r = int(hex_rgb[0:2], 16) / 255.0
    g = int(hex_rgb[2:4], 16) / 255.0
    b = int(hex_rgb[4:6], 16) / 255.0

    def _lin(c: float) -> float:
        return c / 12.92 if c <= SRGB_LINEAR_CUTOFF else ((c + 0.055) / 1.055) ** 2.4

    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)


def _wcag_contrast(hex_a: str, hex_b: str) -> float:
    la = _srgb_rel_luminance(hex_a)
    lb = _srgb_rel_luminance(hex_b)
    hi, lo = (la, lb) if la >= lb else (lb, la)
    return (hi + 0.05) / (lo + 0.05)


def _iter_balanced_calls(src: str, fn_pattern: str) -> list[tuple[str, str]]:
    """Return `(matched_fn_name, raw_argument_list)` for each call matching `fn_pattern`."""
    out: list[tuple[str, str]] = []
    for m in re.finditer(fn_pattern + r"\s*\(", src):
        fn_name = m.group(1)
        start = m.end()
        depth = 1
        pos = start
        n = len(src)
        while pos < n and depth > 0:
            ch = src[pos]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            pos += 1
        if depth == 0:
            out.append((fn_name, src[start : pos - 1]))
    return out


def _split_top_level_args(arg_str: str) -> list[str]:
    parts: list[str] = []
    cur: list[str] = []
    depth = 0
    for ch in arg_str:
        if ch == "," and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            cur.append(ch)
    if cur:
        parts.append("".join(cur).strip())
    return parts


def _page_family(filename: str) -> str:
    return filename.removesuffix(".cpp").removeprefix("page_")


def _check_gui_yaml_and_fast_primitives(gui_uncommented: str, failures: list[str]) -> None:
    if not re.search(r"(?m)^\s*auto_clear_enabled:\s*false\b", gui_uncommented):
        failures.append("[FRAME BASE] guis/universal/gui.yaml must set auto_clear_enabled: false")
    draw_pix_calls = re.findall(r"(?<![\w.>])it\.draw_pixel_at\s*\(", gui_uncommented)
    if (
        "dirty_band()" not in gui_uncommented
        or ".begin_frame(" not in gui_uncommented
        or "frame_base_begin()" not in gui_uncommented
        or "render_system_takeovers(" not in gui_uncommented
        or "touch_engine()" not in gui_uncommented
        or not (MIN_LATCH_CALLS <= len(draw_pix_calls) <= MAX_LATCH_CALLS)
    ):
        failures.append(
            "[DIRTY BAND / COMPOSITOR] guis/universal/gui.yaml must wire dirty_band().begin_frame, "
            "frame_base_begin(), render_system_takeovers, touch_engine(), and 2..4 it.draw_pixel_at latch calls"
        )
    if not (ROOT / "components" / "latter_ui" / "touch_engine.cpp").is_file():
        failures.append("[TOUCH ENGINE] components/latter_ui/touch_engine.cpp missing")

    for path in list((ROOT / "include").glob("ui_*.h")) + list((ROOT / "components" / "latter_ui").glob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8")
        masked = strip_cpp_strings_and_comments(raw)
        if RETIRED_SCALE_CALL_RE.search(masked):
            failures.append(f"[UI SCALING D-087] {rel} calls retired scale_*() helper — use dp_px()/dp_f()/SafeFrame")
        for retired_scale_tok in ("kMaxContentColW", "SCREEN_R"):
            if re.search(rf"\b{retired_scale_tok}\b", masked):
                failures.append(f"[UI SCALING D-087] {rel} references retired '{retired_scale_tok}'")
        if path.name in LOW_LEVEL_GFX_FILES:
            continue
        clean = strip_cpp_comments(raw)
        if RAW_IT_DRAW_RE.search(clean) or re.search(r"(?<![\w.>])it\.draw_pixel_at\s*\(", clean):
            failures.append(f"[FAST PRIMITIVES] {rel} calls raw ESPHome it.* drawing primitive outside ui_gfx.cpp")
    if RAW_IT_DRAW_RE.search(gui_uncommented):
        failures.append("[FAST PRIMITIVES] guis/universal/gui.yaml calls raw ESPHome it.* primitive")


def _check_takeover_and_touch_tokens(gui_uncommented: str, failures: list[str]) -> None:
    takeover_cpp = ROOT / "components" / "latter_ui" / "overlay_takeover.cpp"
    if not takeover_cpp.is_file():
        failures.append("[TAKEOVER ORDER] components/latter_ui/overlay_takeover.cpp missing")
    else:
        tclean = strip_cpp_comments(takeover_cpp.read_text(encoding="utf-8"))
        fn_idx = tclean.find("render_system_takeovers")
        fn_body = tclean[fn_idx:] if fn_idx >= 0 else tclean
        m_alarm = re.search(r"alarm_ringing|render_alert_ringing_overlay", fn_body)
        m_va = re.search(r"va_active|should_va_takeover_preempt|render_voice_assistant_overlay", fn_body)
        if fn_idx < 0 or not m_alarm or not m_va or m_alarm.start() >= m_va.start():
            failures.append(
                "[TAKEOVER ORDER] overlay_takeover.cpp must evaluate/render alarm/timer/find_my "
                "takeover strictly before voice assistant takeover"
            )
        tmasked = strip_cpp_strings_and_comments(takeover_cpp.read_text(encoding="utf-8"))
        _check_header_standard("components/latter_ui/overlay_takeover.cpp", tmasked, failures)

    metrics_h = _read_opt(ROOT / "include" / "ui_panel_metrics.h")
    hit_h = _read_opt(ROOT / "include" / "ui_hit_table.h")
    types_h = _read_opt(ROOT / "include" / "ui_types.h")
    layout_h = _read_opt(ROOT / "include" / "ui_layout.h")
    color_h = _read_opt(ROOT / "include" / "ui_color.h")
    touch_h = _read_opt(ROOT / "include" / "ui_touch_engine.h")
    touch_cpps = "\n".join(p.read_text(encoding="utf-8") for p in sorted((ROOT / "components" / "latter_ui").glob("touch_*.cpp")))

    if "mm_to_px_for(" not in (metrics_h + hit_h) or "mm_to_px(" not in (types_h + layout_h):
        failures.append("[TOUCH MM] Millimetre conversion helpers (mm_to_px_for / mm_to_px) missing in UI headers")
    if "allow_tap(" not in (hit_h + touch_h + touch_cpps) or "TOUCH_DEBOUNCE_" not in (hit_h + touch_h + touch_cpps):
        failures.append("[TOUCH DEBOUNCE] touch_router().allow_tap(..., TOUCH_DEBOUNCE_*) missing in touch engine")
    if "can_go_home(" not in gui_uncommented or "can_go_home(" not in (layout_h + touch_cpps):
        failures.append("[HOME PREDICATE] Shared can_go_home() predicate must gate both gui.yaml and touch_*.cpp")
    failures.extend(
        f"[UI COLOR TOKENS] {tok} missing from include/ui_color.h"
        for tok in ("kChromeShadow", "kChromeHomePill", "kChromeTrack", "kChromeCardBorder", "kChromeBackLabel", "kChromeGlassTint")
        if tok not in (color_h + types_h)
    )

    te_masked = strip_cpp_strings_and_comments(touch_cpps)
    failures.extend(
        f"[UI LIST STANDARD] components/latter_ui/touch_*.cpp references retired '{retired_tok}'"
        for retired_tok in RETIRED_UI_TOKENS
        if re.search(rf"\b{retired_tok}\b", te_masked)
    )


def _read_opt(path: Path) -> str:
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def _check_header_standard(rel: str, pclean: str, failures: list[str]) -> None:
    for _fn, arg_str in _iter_balanced_calls(pclean, r"\b(render_page_header)"):
        if "col_white" not in arg_str and "Color::WHITE" not in arg_str and "UiColor::WHITE" not in arg_str:
            failures.append(f"[UI HEADER STANDARD] {rel} calls render_page_header() without col_white/WHITE (D-070)")
        if "font_small" not in arg_str:
            failures.append(f"[UI HEADER STANDARD] {rel} calls render_page_header() without font_small (D-070)")


def _check_single_page_file(page_cpp: Path, family: str, failures: list[str]) -> str:
    rel = page_cpp.relative_to(ROOT).as_posix()
    raw = page_cpp.read_text(encoding="utf-8")
    nocomment = strip_cpp_comments(raw)
    failures.extend(
        f"[BACK BUTTON Y] {rel} calls render_back_button without back_y"
        for m_back in re.finditer(r"render_back_button\s*\(([^;]+)\)", nocomment)
        if "back_y" not in m_back.group(1) and "kBackLabelY" not in m_back.group(1)
    )

    pclean = strip_cpp_strings_and_comments(raw)
    if "render_in_app_mode_bar_track(" in pclean:
        failures.append(f"[UI MODE BAR] {rel} calls render_in_app_mode_bar_track() directly — use ui::mode_bar()")
    _check_header_standard(rel, pclean, failures)
    if re.search(r"\bmix_color_q8\s*\(\s*kChromeGlassTint\b", pclean):
        failures.append(f"[UI GLASS DISCIPLINE] {rel} hand-mixes kChromeGlassTint — use ui::chrome_card_bg_on()/off()")
    if re.search(r"\bui::pill_button\s*\(|\brender_modern_card\s*\(", pclean):
        failures.append(f"[UI GLASS DISCIPLINE] {rel} calls retired ui::pill_button() or render_modern_card()")

    for _fn, arg_str in _iter_balanced_calls(pclean, r"\b(render_card_box)"):
        parts = _split_top_level_args(arg_str)
        if len(parts) > CARD_BOX_RAD_ARG_IDX and re.fullmatch(
            r"(?:(?:scale_s|dp_px)\s*\(\s*)?(?:10|12|14|15|16)\s*\)?", parts[CARD_BOX_RAD_ARG_IDX]
        ):
            failures.append(
                f"[UI RADIUS TOKENS] {rel} passes magic radius '{parts[CARD_BOX_RAD_ARG_IDX]}' to render_card_box()"
            )
    for fn_name, arg_str in _iter_balanced_calls(pclean, BTN_WIDGET_PAT):
        if "font_micro" in arg_str:
            failures.append(f"[UI NO MICRO BUTTONS] {rel} passes font_micro to interactive {fn_name}()")
    if family in CHROME_TOKEN_FAMILIES and re.search(r"\b(?:Ui)?Color\s*\(\s*\d+\s*,", pclean):
        failures.append(f"[UI CHROME TOKENS] {rel} contains raw Color(r, g, b) chrome literal (D-070)")
    failures.extend(
        f"[UI LIST STANDARD] {rel} references retired '{retired_tok}' (D-072 / D-073)"
        for retired_tok in RETIRED_UI_TOKENS
        if re.search(rf"\b{retired_tok}\b", pclean)
    )
    return pclean


def _check_pages_d070_d072(failures: list[str]) -> None:
    family_blobs: dict[str, list[str]] = {}
    ui_dir = ROOT / "components" / "latter_ui"
    for page_cpp in sorted(ui_dir.glob("page_*.cpp")):
        if page_cpp.name == "page_router.cpp":
            continue
        fam = _page_family(page_cpp.name)
        pclean = _check_single_page_file(page_cpp, fam, failures)
        family_blobs.setdefault(fam, []).append(pclean)

    for fam in sorted(MODE_BAR_FAMILIES):
        combined = "\n".join(family_blobs.get(fam, ()))
        if "ui::mode_bar(" not in combined:
            failures.append(f"[UI MODE BAR] page_{fam} family must render bottom mode bar via ui::mode_bar() (D-070)")
    for fam in sorted(LIST_SLOT5_FAMILIES):
        combined = "\n".join(family_blobs.get(fam, ()))
        if "list_card_w" not in combined or "slot5_h" not in combined:
            failures.append(f"[UI LIST STANDARD] page_{fam} family must size 1-line lists via list_card_w & slot5_h")


def _check_contrast_fonts_and_ui_doc(gui_uncommented: str, failures: list[str]) -> None:
    palette = {
        m.group(1): m.group(2)
        for m in re.finditer(r'-\s*id:\s*([A-Za-z0-9_]+)\s*\n\s*hex:\s*"([0-9A-Fa-f]{6})"', gui_uncommented)
    }
    bg_hex = palette.get("col_bg", "000000")
    for cid in ("theme_primary", "theme_secondary", "col_white", "col_slate", "col_gold", "col_warm_gold", "col_orange"):
        chex = palette.get(cid)
        if not chex:
            failures.append(f"[TEXT CONTRAST] Missing palette color '{cid}' in guis/universal/gui.yaml")
            continue
        ratio = _wcag_contrast(chex, bg_hex)
        if ratio < MIN_CONTRAST_RATIO:
            failures.append(f"[TEXT CONTRAST] {cid} (#{chex}) on #{bg_hex} has contrast {ratio:.2f}:1 (< 4.5:1)")

    board_metrics = _collect_board_font_metrics(failures)
    _verify_ui_design_doc_table(board_metrics, failures)


def _device_yaml_for_board(board_stem: str) -> Path | None:
    needle = f"boards/{board_stem}.yaml"
    for dev in sorted((ROOT / "devices").glob("*.yaml")):
        if needle in dev.read_text(encoding="utf-8"):
            return dev
    return None


def _collect_board_font_metrics(failures: list[str]) -> dict[str, dict[str, int]]:
    board_metrics: dict[str, dict[str, int]] = {}
    for bf in sorted((ROOT / "boards").glob("*.yaml")):
        btxt = bf.read_text(encoding="utf-8")
        dev = _device_yaml_for_board(bf.stem)
        # Resolve effective font sizes through ESPHome's real package merge chain (hal -> latter_firmware -> device).
        bsubs = extract_substitutions(collect_yaml_include_chain(dev) if dev else [bf])
        bflags = parse_latter_core_flags(btxt)
        if not all(k in bflags for k in ("LATTER_PANEL_PPI", "LATTER_SCREEN_W", "LATTER_SCREEN_H")):
            continue
        w_px = int(bflags["LATTER_SCREEN_W"])
        h_px = int(bflags["LATTER_SCREEN_H"])
        ppi = int(bflags["LATTER_PANEL_PPI"])
        is_rnd = int(bflags.get("LATTER_ROUND_DISPLAY", "0")) != 0
        is_wear = int(bflags.get("LATTER_IS_WEARABLE", "0")) != 0
        default_rect_cr = WEARABLE_DEFAULT_CORNER_R if is_wear else RECT_DEFAULT_CORNER_R
        eff_cr = int(bflags["LATTER_CORNER_R"]) if "LATTER_CORNER_R" in bflags else (w_px // 2 if is_rnd else default_rect_cr)
        if "font_small_size" not in bsubs or "font_micro_size" not in bsubs:
            failures.append(f"[FONT MM] {bf.name}: missing effective font_small_size/font_micro_size substitution")
            continue
        f_small = int(bsubs["font_small_size"])
        f_micro = int(bsubs["font_micro_size"])
        if f_small * 25.4 / ppi < MIN_SMALL_FONT_MM:
            failures.append(f"[FONT MM] {bf.name}: font_small_size={f_small}px at {ppi} PPI < 2.0 mm")
        if f_micro * 25.4 / ppi < MIN_MICRO_FONT_MM:
            failures.append(f"[FONT MM] {bf.name}: font_micro_size={f_micro}px at {ppi} PPI < 1.65 mm")
        board_metrics[bf.stem] = {"w": w_px, "h": h_px, "ppi": ppi, "cr": eff_cr, "small": f_small, "micro": f_micro}
    return board_metrics


def _verify_ui_design_doc_table(board_metrics: dict[str, dict[str, int]], failures: list[str]) -> None:
    ui_doc = ROOT / "docs" / "device-ui-design.md"
    if not ui_doc.is_file():
        return
    doc_txt = ui_doc.read_text(encoding="utf-8")
    m_boards = re.search(r"(?m)^\|\s*Board file\s*\|(.+)$", doc_txt)
    m_res = re.search(r"(?m)^\|\s*Resolution\s*\|(.+)$", doc_txt)
    m_ppis = re.search(r"(?m)^\|\s*`LATTER_PANEL_PPI`\s*\|(.+)$", doc_txt)
    m_crs = re.search(r"(?m)^\|\s*`LATTER_CORNER_R`\s*\|(.+)$", doc_txt)
    m_fonts = re.search(r"(?m)^\|\s*`font_small`\s*/\s*`font_micro`\s*\|(.+)$", doc_txt)
    if not (m_boards and m_res and m_ppis and m_crs and m_fonts):
        failures.append("[DEVICE UI DOC] docs/device-ui-design.md missing Section 6 Per-board adaptation table rows")
        return
    b_cols = [c.strip().strip("`") for c in m_boards.group(1).strip().rstrip("|").split("|")]
    r_cols = [c.strip() for c in m_res.group(1).strip().rstrip("|").split("|")]
    p_cols = [c.strip() for c in m_ppis.group(1).strip().rstrip("|").split("|")]
    cr_cols = [c.strip() for c in m_crs.group(1).strip().rstrip("|").split("|")]
    f_cols = [c.strip() for c in m_fonts.group(1).strip().rstrip("|").split("|")]
    for idx, bname in enumerate(b_cols):
        bm = board_metrics.get(bname)
        if not bm or idx >= min(len(r_cols), len(p_cols), len(cr_cols), len(f_cols)):
            failures.append(f"[DEVICE UI DOC] Table column mismatch for board '{bname}'")
            continue
        m_doc_cr = re.match(r"^(\d+)\b", cr_cols[idx])
        doc_cr = m_doc_cr.group(1) if m_doc_cr else cr_cols[idx]
        want = (f"{bm['w']} \u00d7 {bm['h']}", str(bm["ppi"]), str(bm["cr"]), f"{bm['small']} / {bm['micro']}")
        got = (r_cols[idx], p_cols[idx], doc_cr, f_cols[idx])
        if got != want:
            failures.append(f"[DEVICE UI DOC] docs/device-ui-design.md out of sync with {bname}: {got} != {want}")


def verify_device_ui_and_accessibility(failures: list[str]) -> None:
    """Enforce Device UI compositing, fast primitives, D-070/D-072/D-073 tokens, contrast, and mm sizing."""
    gui_yaml_path = ROOT / "guis" / "universal" / "gui.yaml"
    if not gui_yaml_path.is_file():
        failures.append("[GUI] guis/universal/gui.yaml missing")
        return
    gui_uncommented = strip_yaml_comments(gui_yaml_path.read_text(encoding="utf-8"))
    _check_gui_yaml_and_fast_primitives(gui_uncommented, failures)
    _check_takeover_and_touch_tokens(gui_uncommented, failures)
    _check_pages_d070_d072(failures)
    _check_contrast_fonts_and_ui_doc(gui_uncommented, failures)
