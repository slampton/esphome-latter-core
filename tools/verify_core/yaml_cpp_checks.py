"""YAML C++ namespace symbol resolution, ID cross-checking, and host C++20 lambda syntax harnesses."""

from __future__ import annotations

import difflib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

from .common import (
    ROOT,
    collect_yaml_include_chain,
    extract_substitutions,
    iter_repo_files,
    strip_cpp_strings_and_comments,
    strip_yaml_comments,
    strip_yaml_comments_keep_lines,
)

MAX_ACTION_INDENT: Final[int] = 6
HOST_LAMBDA_HEADERS: Final[tuple[str, ...]] = (
    "latter_protocol.h",
    "state/device_runtime_state.h",
    "state/state_nvs_snapshot.h",
    "latter_rpc.h",
    "latter_actions.h",
    "hal_battery.h",
    "hal_link_report.h",
    "hal_manifest.h",
    "hal_panel_power.h",
    "hal_sleep_pm.h",
    "hal_radio_policy.h",
    "hal_display_lifecycle.h",
    "hal_video_stream.h",
    "hal_touch.h",
    "ui_hit_table.h",
    "ui_touch_recognizer.h",
    "ui_compositor.h",
    "ui_touch_engine.h",
)

TYPE_MAP: Final[dict[str, str]] = {
    "string": "std::string",
    "int": "int",
    "bool": "bool",
    "float": "float",
    "string[]": "std::vector<std::string>",
}

YAML_FULL_LAMBDA_PREAMBLE: Final[str] = """\
#ifndef LATTER_CHIP_P4
#define LATTER_CHIP_P4 0
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/core/time.h"
#include "esphome/components/font/font.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/voice_assistant/voice_assistant.h"
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esphome_bridge.h"
#include "latter_actions.h"
#include "state/state_nvs_snapshot.h"
#include "audio_router.h"
#include "hal_battery.h"
#include "hal_imu_qmi8658.h"
#include "hal_link_report.h"
#include "hal_touch.h"
#include "hal_video_stream.h"
#include "latter_core.h"
#include "latter_beacon.h"
#include "ui_esphome_display.h"

#include <algorithm>
#include <cctype>
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
using esphome::global_preferences;
using esphome::micros;
using esphome::millis;

struct AppStub { void feed_wdt() {} void safe_reboot() {} };
inline AppStub App;

namespace audio {
struct AudioStreamInfo {
  constexpr AudioStreamInfo(int = 16, int = 1, uint32_t = 48000) {}
};
}  // namespace audio

namespace speaker {
class Speaker {
 public:
  bool is_running() const { return false; }
  bool is_stopped() const { return true; }
  bool has_buffered_data() const { return false; }
  float get_volume() const { return 0.5f; }
  void set_volume(float) {}
  void set_audio_stream_info(const audio::AudioStreamInfo &) {}
  void start() {}
  void stop() {}
  template <typename... Args> void apply_ducking(Args &&...) {}
};
}  // namespace speaker

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
  bool operator==(bool) const { return false; }
  bool operator!=(bool) const { return true; }
};

struct AnyId : esphome::display::Display, esphome::display::BaseImage, esphome::font::Font, speaker::Speaker {
  AnyStateVal state{};
  float volume{0.5f};
  AnyValues current_values{};
  AnyValues remote_values{};
  operator esphome::display::BaseFont *() { return static_cast<esphome::font::Font *>(this); }
  operator esphome::display::BaseImage *() { return static_cast<esphome::display::BaseImage *>(this); }
  template <typename... Args> void execute(Args &&...) {}
  void stop() {}
  void start() {}
  void press() {}
  void update() {}
  template <typename... Args> void publish_state(Args &&...) {}
  bool has_state() const { return true; }
  bool is_running() const { return false; }
  bool is_active() const { return false; }
  bool is_connected() const { return true; }
  void set_profile_logging(bool) {}
  void set_uuid_hex(std::string_view) {}
  void set_mute_state(bool) {}
  bool get_mute_state() const { return false; }
  void set_mute_on() {}
  void set_mute_off() {}
  void set_probability_cutoff(uint8_t) {}
  void set_brightness(uint8_t) {}
  bool is_failed() const { return false; }
  void reset_to_construction_state() {}
  void call() {}
  void disable_loop() {}
  void enable_loop() {}
  void release() {}
  void enable() {}
  void pause() {}
  void disable() {}
  bool is_paused() const { return false; }
  bool is_transport_up() const { return false; }
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
inline esphome::latter_beacon::LatterBeacon &beacon_id_singleton() {
  static std::array<uint8_t, 16> u{};
  static esphome::latter_beacon::LatterBeacon b(u);
  return b;
}
inline ::latter_core::LatterLoopProbe &loop_probe_singleton() {
  static ::latter_core::LatterLoopProbe p;
  return p;
}
inline esphome::i2c::I2CBus &i2c_bus_singleton() { static esphome::i2c::I2CBus b; return b; }
inline esphome::wifi::WiFiComponent &wifi_comp_singleton() { static esphome::wifi::WiFiComponent w; return w; }
inline esphome::voice_assistant::VoiceAssistant &va_comp_singleton() {
  static esphome::voice_assistant::VoiceAssistant v;
  return v;
}
inline esphome::micro_wake_word::MicroWakeWord &mww_comp_singleton() {
  static esphome::micro_wake_word::MicroWakeWord m;
  return m;
}
struct AnyStrId {
  std::string state;
  bool has_state() const { return true; }
  void update() {}
  template <typename... A> void publish_state(A &&...) {}
};
struct AnyFltId {
  float state{0.0f};
  bool has_state() const { return true; }
  void update() {}
  void stop_poller() {}
  void start_poller() {}
  void set_update_interval(uint32_t) {}
  template <typename... A> void publish_state(A &&...) {}
};
inline AnyStrId &any_str_id_singleton() { static AnyStrId s; return s; }
inline AnyFltId &any_flt_id_singleton() { static AnyFltId s; return s; }
struct TouchPointStub { int x{0}, y{0}, state{1}; };
struct VaTimerStub {
  std::string id;
  std::string name;
  uint32_t total_seconds{60};
  uint32_t seconds_left{30};
  bool is_active{true};
};
struct HostApiStringRef {
  const char *p{""};
  size_t n{0U};
  const char *c_str() const noexcept { return p; }
  size_t size() const noexcept { return n; }
  size_t length() const noexcept { return n; }
  bool empty() const noexcept { return n == 0U; }
  std::string str() const { return std::string(p, n); }
  operator std::string() const { return str(); }
  operator std::string_view() const noexcept { return std::string_view(p, n); }
  bool operator==(const char *o) const noexcept { return std::string_view(p, n) == (o ? o : ""); }
  bool operator!=(const char *o) const noexcept { return !(*this == o); }
};
"""


def collect_namespace_symbols(paths: list[Path] | None = None) -> tuple[dict[str, set[str]], set[str]]:
    """Extract per-namespace C++ identifiers and global identifiers across C++ roots or `paths`."""
    ns_syms: dict[str, set[str]] = {}
    all_syms: set[str] = set()
    ns_open_re = re.compile(r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{")
    ident_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")

    if paths is None:
        file_list = [
            p
            for base in (ROOT / "include", ROOT / "components", ROOT / "examples" / "bringup")
            for p in iter_repo_files(base)
            if p.suffix.lower() in {".h", ".hpp", ".cpp"}
        ]
    else:
        file_list = [p for p in paths if p.is_file()]

    for path in file_list:
        clean = strip_cpp_strings_and_comments(path.read_text(encoding="utf-8"))
        all_syms.update(ident_re.findall(clean))
        _scan_namespaces_in_file(clean, ns_open_re, ident_re, ns_syms)
    return ns_syms, all_syms


def _record_ns_tokens(
    seg: str,
    ident_re: re.Pattern[str],
    stack: list[tuple[str | None, int]],
    ns_syms: dict[str, set[str]],
) -> None:
    if not seg or not stack:
        return
    toks = ident_re.findall(seg)
    for active_ns, _d in stack:
        if active_ns:
            ns_syms.setdefault(active_ns, set()).update(toks)


def _scan_namespaces_in_file(
    clean: str,
    ns_open_re: re.Pattern[str],
    ident_re: re.Pattern[str],
    ns_syms: dict[str, set[str]],
) -> None:
    pos = 0
    n = len(clean)
    stack: list[tuple[str | None, int]] = []
    depth = 0
    while pos < n:
        m_ns = ns_open_re.search(clean, pos)
        candidates = [x for x in (clean.find("{", pos), clean.find("}", pos)) if x != -1]
        if not candidates:
            _record_ns_tokens(clean[pos:], ident_re, stack, ns_syms)
            break
        nxt = min(candidates)
        _record_ns_tokens(clean[pos:nxt], ident_re, stack, ns_syms)
        if m_ns and m_ns.end() - 1 == nxt:
            depth += 1
            stack.extend((p, depth) for p in m_ns.group(1).split("::") if p)
        elif clean[nxt] == "{":
            depth += 1
        else:
            while stack and stack[-1][1] == depth:
                stack.pop()
            depth = max(0, depth - 1)
        pos = nxt + 1


def _check_balanced_lambda_blocks(rel: str, lines: list[str], failures: list[str]) -> None:
    pairs = {"(": ")", "{": "}", "[": "]"}
    closing = {")": "(", "}": "{", "]": "["}
    idx = 0
    while idx < len(lines):
        if not re.match(r"^(\s*)(?:-\s*)?(?:\w+\s*:\s*!lambda\s*|lambda\s*:\s*)\|-?\s*$", lines[idx]):
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
        clean_body = strip_cpp_strings_and_comments("\n".join(body_lines))
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


def verify_yaml_cpp_and_ids(failures: list[str]) -> None:
    """Statically verify C++ namespace symbols, hal_* calls, delimiter balance, and id()/script refs in YAML."""
    ns_syms, all_syms = collect_namespace_symbols()
    qual_re = re.compile(r"\b(latter(?:_[a-z0-9_]+)?)::([A-Za-z_][A-Za-z0-9_]*)")
    hal_re = re.compile(r"\b(hal_[A-Za-z0-9_]+)\s*\(")

    for folder in ("modules", "guis", "boards", "devices", "examples"):
        for yf in iter_repo_files(ROOT / folder):
            if yf.suffix.lower() not in {".yaml", ".yml"}:
                continue
            rel = yf.relative_to(ROOT).as_posix()
            raw_txt = yf.read_text(encoding="utf-8")
            masked = strip_cpp_strings_and_comments(strip_yaml_comments_keep_lines(raw_txt))
            for line_no, line in enumerate(masked.splitlines(), 1):
                for m_q in qual_re.finditer(line):
                    ns, sym = m_q.group(1), m_q.group(2)
                    known = ns_syms.get(ns, set())
                    if sym not in known:
                        hint_list = difflib.get_close_matches(sym, sorted(known), n=1, cutoff=0.6)
                        hint = f" (did you mean '{ns}::{hint_list[0]}'?)" if hint_list else ""
                        failures.append(f"[YAML CPP SYMBOL] {rel}:{line_no} undeclared '{ns}::{sym}'{hint}")
                failures.extend(
                    f"[YAML CPP SYMBOL] {rel}:{line_no} undeclared HAL helper '{m_h.group(1)}'"
                    for m_h in hal_re.finditer(line)
                    if m_h.group(1) not in all_syms
                )
            _check_balanced_lambda_blocks(rel, raw_txt.splitlines(), failures)

    _verify_dangling_yaml_ids(failures)
    _verify_package_substitutions(failures)


def _verify_package_substitutions(failures: list[str]) -> None:
    """Ensure later packages never clobber board HAL substitutions and every ${var} resolves."""
    targets = (
        sorted((ROOT / "devices").glob("*.yaml"))
        + sorted((ROOT / "examples").glob("*.yaml"))
        + sorted((ROOT / "examples" / "bringup").glob("*.yaml"))
    )
    sub_ref_re = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")
    boards_dir = (ROOT / "boards").resolve()
    reported_clobber: set[tuple[str, str]] = set()
    reported_missing: set[tuple[str, str, str]] = set()

    for target in targets:
        chain = collect_yaml_include_chain(target)
        merged_subs: dict[str, str] = {}
        board_subs: dict[str, str] = {}
        for yf in chain:
            file_subs = extract_substitutions([yf])
            if yf.parent.resolve() == boards_dir:
                board_subs.update(file_subs)
            elif board_subs and yf != target.resolve():
                overlap = sorted(set(board_subs) & set(file_subs))
                if overlap:
                    yrel = yf.relative_to(ROOT).as_posix()
                    key = (yrel, ",".join(overlap))
                    if key not in reported_clobber:
                        reported_clobber.add(key)
                        failures.append(
                            f"[SUB CLOBBER] {yrel} (loaded after boards/*.yaml in {target.name}) "
                            f"overwrites board substitution(s): {overlap}"
                        )
            merged_subs.update(file_subs)

        for yf in chain:
            yrel = yf.relative_to(ROOT).as_posix()
            yclean = strip_yaml_comments(yf.read_text(encoding="utf-8"))
            for var in sub_ref_re.findall(yclean):
                if var not in merged_subs:
                    mkey = (target.name, yrel, var)
                    if mkey not in reported_missing:
                        reported_missing.add(mkey)
                        failures.append(
                            f"[UNRESOLVED SUB] {yrel} (in {target.name}) references undefined substitution '${{{var}}}'"
                        )


def _verify_dangling_yaml_ids(failures: list[str]) -> None:
    targets = (
        sorted((ROOT / "devices").glob("*.yaml"))
        + sorted((ROOT / "examples").glob("*.yaml"))
        + sorted((ROOT / "examples" / "bringup").glob("*.yaml"))
    )
    reported_ids: set[tuple[str, str, str]] = set()
    for target in targets:
        chain = collect_yaml_include_chain(target)
        merged = "\n".join(strip_yaml_comments(p.read_text(encoding="utf-8")) for p in chain)
        defined_ids = set(re.findall(r"(?m)^\s*-?\s*id\s*:\s*(?:!extend\s+|!remove\s+)?([A-Za-z_][A-Za-z0-9_]*)\b", merged))
        for p in chain:
            prel = p.relative_to(ROOT).as_posix()
            pclean = strip_cpp_strings_and_comments(strip_yaml_comments_keep_lines(p.read_text(encoding="utf-8")))
            for m_id in re.finditer(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", pclean):
                ident = m_id.group(1)
                key = (target.name, prel, ident)
                if ident not in defined_ids and key not in reported_ids:
                    reported_ids.add(key)
                    failures.append(f"[DANGLING YAML ID] {prel} (in {target.name}) references undefined id({ident})")
            for m_sc in re.finditer(r"(?m)^\s*-?\s*script\.(?:execute|stop|wait)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*$", pclean):
                s_id = m_sc.group(1)
                key = (target.name, prel, s_id)
                if s_id not in defined_ids and key not in reported_ids:
                    reported_ids.add(key)
                    failures.append(f"[DANGLING SCRIPT ID] {prel} (in {target.name}) calls undefined script '{s_id}'")

    disp_only = ROOT / "modules" / "latter_display_only_firmware.yaml"
    if disp_only.is_file():
        chain = collect_yaml_include_chain(disp_only)
        merged = "\n".join(strip_yaml_comments(p.read_text(encoding="utf-8")) for p in chain)
        defined_ids = set(
            re.findall(r"(?m)^\s*-?\s*id\s*:\s*(?:!extend\s+|!remove\s+)?([A-Za-z_][A-Za-z0-9_]*)\b", merged)
        ) | {
            "disp1",
            "bus_internal",
            "boot_button",
            "wifi_ctrl",
            "ble_ctrl",
            "latter_firmware_update",
            "hal_release_deep_sleep_holds",
            "hal_enter_hardware_hibernate",
        }
        for p in chain:
            prel = p.relative_to(ROOT).as_posix()
            pclean = strip_cpp_strings_and_comments(strip_yaml_comments_keep_lines(p.read_text(encoding="utf-8")))
            for m_id in re.finditer(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", pclean):
                ident = m_id.group(1)
                key = (disp_only.name, prel, ident)
                if ident not in defined_ids and key not in reported_ids:
                    reported_ids.add(key)
                    failures.append(
                        f"[DANGLING DISPLAY-ONLY ID] {prel} (in {disp_only.name}) references undefined id({ident})"
                    )
            for m_sc in re.finditer(r"(?m)^\s*-?\s*script\.(?:execute|stop|wait)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*$", pclean):
                s_id = m_sc.group(1)
                key = (disp_only.name, prel, s_id)
                if s_id not in defined_ids and key not in reported_ids:
                    reported_ids.add(key)
                    failures.append(
                        f"[DANGLING DISPLAY-ONLY SCRIPT] {prel} (in {disp_only.name}) calls undefined script '{s_id}'"
                    )


def write_yaml_lambdas_syntax_harness(out_cpp: Path) -> int:
    """Generate a host C++20 translation unit compiling standalone-header YAML action lambdas."""
    ns_syms, _ = collect_namespace_symbols()
    host_paths: list[Path] = []
    for h in HOST_LAMBDA_HEADERS:
        hp = ROOT / "include" / h
        if hp.is_file():
            host_paths.extend(_collect_transitive_include_files(hp))
    host_ns_syms, _ = collect_namespace_symbols(host_paths)
    esp_only_ns_syms = {ns: syms - host_ns_syms.get(ns, set()) for ns, syms in ns_syms.items()}

    chunks: list[str] = [
        "#include <algorithm>",
        "#include <cmath>",
        "#include <cstdint>",
        "#include <cstdio>",
        "#include <cstring>",
        "#include <string>",
        "#include <string_view>",
        "#include <vector>",
        *[f'#include "{(ROOT / "include" / h).as_posix()}"' for h in HOST_LAMBDA_HEADERS if (ROOT / "include" / h).is_file()],
        "namespace {",
        "inline uint32_t millis() { return 0U; }",
        "struct HostApiStringRef {",
        '  const char *p{""};',
        "  size_t n{0U};",
        "  const char *c_str() const noexcept { return p; }",
        "  size_t size() const noexcept { return n; }",
        "  size_t length() const noexcept { return n; }",
        "  bool empty() const noexcept { return n == 0U; }",
        "  std::string str() const { return std::string(p, n); }",
        "  operator std::string() const { return str(); }",
        "  operator std::string_view() const noexcept { return std::string_view(p, n); }",
        '  bool operator==(const char *o) const noexcept { return std::string_view(p, n) == (o ? o : ""); }',
        "  bool operator!=(const char *o) const noexcept { return !(*this == o); }",
        "};",
        "}",
    ]
    fn_idx = 0
    for folder in ("modules", "guis"):
        for yf in iter_repo_files(ROOT / folder):
            if yf.suffix.lower() in {".yaml", ".yml"}:
                fn_idx = _append_standalone_yaml_lambdas(yf, esp_only_ns_syms, chunks, fn_idx)
    chunks.append("int main() { return 0; }")
    out_cpp.write_text("\n".join(chunks) + "\n", encoding="utf-8")
    return fn_idx


def _collect_transitive_include_files(hdr: Path, seen: set[Path] | None = None) -> list[Path]:
    if seen is None:
        seen = set()
    res = hdr.resolve()
    if res in seen or not res.is_file():
        return []
    seen.add(res)
    out = [res]
    for m in re.finditer(r'^\s*#\s*include\s*"([^"]+)"', res.read_text(encoding="utf-8"), re.MULTILINE):
        for base in (res.parent, ROOT / "include"):
            cand = (base / m.group(1)).resolve()
            if cand.is_file() and ROOT in cand.parents:
                out.extend(_collect_transitive_include_files(cand, seen))
    return out


@dataclass
class _StandaloneVarState:
    active_vars: list[tuple[str, str]] = field(default_factory=list)
    in_vars: bool = False
    is_api_vars: bool = False
    vars_indent: int = -1
    client_hook: str = ""

    def step_line(self, stripped: str) -> bool:
        if not stripped.strip():
            return False
        ind = len(stripped) - len(stripped.lstrip(" "))
        if re.match(r"^\s*-\s*(?:id|action)\s*:", stripped) and ind <= MAX_ACTION_INDENT:
            self.active_vars, self.in_vars, self.is_api_vars, self.client_hook = [], False, False, ""
        m_ch = re.match(r"^\s*(on_client_connected|on_client_disconnected)\s*:\s*$", stripped)
        if m_ch:
            self.client_hook, self.active_vars, self.in_vars, self.is_api_vars = m_ch.group(1), [], False, False
        m_vh = re.match(r"^(\s*)(parameters|variables)\s*:\s*$", stripped)
        if m_vh:
            self.in_vars = True
            self.is_api_vars = m_vh.group(2) == "variables"
            self.vars_indent = len(m_vh.group(1))
            self.active_vars = []
            return True
        if self.in_vars:
            if ind <= self.vars_indent:
                self.in_vars = False
            else:
                m_kv = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_\[\]]+)\s*$", stripped)
                if m_kv and m_kv.group(2) in TYPE_MAP:
                    ctype = (
                        "HostApiStringRef"
                        if (self.is_api_vars and m_kv.group(2) == "string")
                        else TYPE_MAP[m_kv.group(2)]
                    )
                    self.active_vars.append((m_kv.group(1), ctype))
        return False


def _append_standalone_yaml_lambdas(
    yf: Path,
    esp_only_ns_syms: dict[str, set[str]],
    chunks: list[str],
    fn_idx: int,
) -> int:
    qual_re = re.compile(r"\b(latter(?:_[a-z0-9_]+)?)::([A-Za-z_][A-Za-z0-9_]*)")
    skip_re = re.compile(
        r"\b(?:id\s*\(|esp_|i2c_|gpio_|rtc_|nvs_|App\.|LATTER_PROF_LOG|ESP_LOG|delay\s*\(|micros\s*\(|x\b)"
    )
    rel = yf.relative_to(ROOT).as_posix()
    lines = yf.read_text(encoding="utf-8").splitlines()
    st = _StandaloneVarState()
    idx = 0
    while idx < len(lines):
        raw_ln = lines[idx]
        if st.step_line(raw_ln.split("#", 1)[0].rstrip()):
            idx += 1
            continue
        if not re.match(r"^(\s*)-\s*lambda\s*:\s*\|-?\s*$", raw_ln):
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
        bclean = strip_cpp_strings_and_comments(btxt)
        if skip_re.search(bclean) or "${" in btxt:
            continue
        if any(m_q.group(2) in esp_only_ns_syms.get(m_q.group(1), set()) for m_q in qual_re.finditer(bclean)):
            continue
        params = list(st.active_vars)
        if st.client_hook == "on_client_connected":
            params = [("client_info", "std::string"), ("client_address", "std::string")]
        elif st.client_hook == "on_client_disconnected":
            params = [("client_info", "std::string")]
        sig = ", ".join(f"{ctype} {cname}" for cname, ctype in params)
        unused = " ".join(f"(void){cname};" for cname, _ in params)
        fn_idx += 1
        chunks.append(f"[[maybe_unused]] static void yaml_action_lambda_{fn_idx}({sig}) {{")
        if unused:
            chunks.append(f"  {unused}")
        chunks.extend([f'#line {body_start_line} "{rel}"', *body, "}"])
    return fn_idx


def _sanitize_full_lambda_cpp(code: str) -> str:
    str_ids = {
        "track_title",
        "track_artist",
        "track_album",
        "latter_ip_address",
        "batt_charge_state",
    }
    flt_ids = {
        "latter_wifi_signal",
        "accel_x",
        "accel_y",
        "accel_z",
        "imu_roll",
        "imu_pitch",
        "imu_temp",
        "motion_mag",
        "batt_level",
        "batt_voltage",
    }
    typed_singletons = {
        "ble_ctrl": "beacon_id_singleton()",
        "bus_internal": "i2c_bus_singleton()",
        "wifi_ctrl": "wifi_comp_singleton()",
        "va": "va_comp_singleton()",
        "mww": "mww_comp_singleton()",
    }
    ptr_ids = {
        "remote_album_art",
        "home_wallpaper_art",
        "screensaver_photo_art",
        "camera_snapshot_art",
        "album_art",
        "touch_screen",
        "screensaver_anim_tick",
        "ui_anim_tick",
        "ble_ctrl",
        "wifi_ctrl",
        "bus_internal",
        "va",
        "mww",
        "announcement_mixing_input",
        "sound_mixing_input",
        "media_mixing_input",
        "physical_speaker",
        "disp1",
    }
    code = re.sub(r"\$\{[A-Za-z0-9_]+\}", "0", code)
    code = re.sub(
        r"(?<!&)&(?!&)\s*id\(\s*(theme_primary|theme_secondary|col_[A-Za-z0-9_]*)\s*\)",
        "(&any_color_singleton())",
        code,
    )
    code = re.sub(
        r"(?<!&)&(?!&)\s*id\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
        lambda m: f"(&{typed_singletons.get(m.group(1), 'any_id_singleton()')})",
        code,
    )

    def repl_dot(m: re.Match[str]) -> str:
        nm = m.group(1)
        if nm in typed_singletons:
            return f"{typed_singletons[nm]}."
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
        if nm in typed_singletons:
            return f"(&{typed_singletons[nm]})"
        if nm in ptr_ids:
            return "(&any_id_singleton())"
        return "any_id_singleton()"

    return re.sub(r"\bid\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", repl_bare, code)


FULL_HOOK_RE: Final[re.Pattern[str]] = re.compile(
    r"^\s*(on_progress|on_begin|on_end|on_error|on_abort|"
    r"on_client_connected|on_client_disconnected|"
    r"on_wake_word_detected|on_listening|on_stt_vad_start|on_stt_vad_end|"
    r"on_stt_end|on_intent_progress|on_tts_start|on_tts_end|"
    r"on_timer_started|on_timer_updated|on_timer_cancelled|on_timer_finished|"
    r"on_value|on_state|on_play|on_pause|on_idle|on_announcement|on_press|on_release)\s*:\s*$"
)

HOOK_FIXED_PARAMS: Final[dict[str, list[tuple[str, str]]]] = {
    "on_progress": [("x", "float")],
    "on_wake_word_detected": [("wake_word", "std::string")],
    "on_stt_end": [("x", "std::string")],
    "on_intent_progress": [("x", "std::string")],
    "on_tts_start": [("x", "std::string")],
    "on_tts_end": [("x", "std::string")],
    "on_timer_started": [("timer", "VaTimerStub")],
    "on_timer_updated": [("timer", "VaTimerStub")],
    "on_timer_cancelled": [("timer", "VaTimerStub")],
    "on_timer_finished": [("timer", "VaTimerStub")],
}


@dataclass
class _FullYamlState:
    rel: str
    active_vars: list[tuple[str, str]] = field(default_factory=list)
    in_vars: bool = False
    vars_indent: int = -1
    section: str = ""
    hook: str = ""
    on_touch: str = ""
    in_write_action: bool = False

    def reset_item(self) -> None:
        self.active_vars = []
        self.in_vars = False
        self.hook = ""
        self.on_touch = ""
        self.in_write_action = False

    def step_line(self, stripped: str) -> bool:
        if not stripped.strip():
            return False
        ind = len(stripped) - len(stripped.lstrip(" "))
        m_sec = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*$", stripped)
        if m_sec and ind == 0:
            self.section = m_sec.group(1)
            self.reset_item()
        elif re.match(r"^\s*-\s*(?:id|action|platform|interval)\s*:", stripped) and ind <= MAX_ACTION_INDENT:
            self.reset_item()
        if re.match(r"^\s*write_action\s*:\s*$", stripped):
            self.in_write_action = True
        m_hk = FULL_HOOK_RE.match(stripped)
        if m_hk:
            self.hook = m_hk.group(1)
            if self.hook in ("on_client_connected", "on_client_disconnected"):
                self.active_vars, self.in_vars = [], False
        m_ot = re.match(r"^\s*(on_touch|on_update|on_release)\s*:\s*$", stripped)
        if m_ot and self.rel.endswith(("gui.yaml", "gui_touch.yaml")):
            self.on_touch = m_ot.group(1)
        return self._step_vars(stripped, ind)

    def _step_vars(self, stripped: str, ind: int) -> bool:
        m_vh = re.match(r"^(\s*)(parameters|variables)\s*:\s*$", stripped)
        if m_vh:
            self.in_vars, self.vars_indent, self.active_vars = True, len(m_vh.group(1)), []
            return True
        if self.in_vars:
            if ind <= self.vars_indent:
                self.in_vars = False
            else:
                m_kv = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_\[\]]+)\s*$", stripped)
                if m_kv and m_kv.group(2) in TYPE_MAP:
                    ctype = (
                        "HostApiStringRef"
                        if (self.section == "api" and m_kv.group(2) == "string")
                        else TYPE_MAP[m_kv.group(2)]
                    )
                    self.active_vars.append((m_kv.group(1), ctype))
        return False

    def resolve_params(self) -> list[tuple[str, str]]:
        if self.rel.endswith(("gui.yaml", "gui_touch.yaml")):
            gui_map: dict[str, list[tuple[str, str]]] = {
                "on_touch": [("touch", "TouchPointStub"), ("touches", "std::vector<TouchPointStub>")],
                "on_update": [("touches", "std::vector<TouchPointStub>")],
                "": [("it", "esphome::display::Display &")],
            }
            if self.on_touch in gui_map:
                return gui_map[self.on_touch]
        if self.in_write_action:
            return [("state", "float")]
        if self.hook in ("on_client_connected", "on_client_disconnected") and self.section == "api":
            return [("client_info", "std::string"), ("client_address", "std::string")]
        if self.hook == "on_error" and self.section == "voice_assistant":
            return [("code", "std::string"), ("message", "std::string")]
        if self.hook == "on_value" and self.section in {"text_sensor", "sensor"}:
            return [("x", "std::string" if self.section == "text_sensor" else "float")]
        return HOOK_FIXED_PARAMS.get(self.hook, list(self.active_vars))


def write_yaml_full_lambdas_syntax_harness(out_cpp: Path, *, include_hal_boards: bool = False) -> int:
    """Generate a host C++20 TU compiling all block & inline YAML lambdas (including `id(...)` calls)."""
    yaml_files = [
        p
        for p in sorted((ROOT / "modules").glob("*.yaml"))
        if p.name not in {"latter_display_firmware.yaml", "latter_display_only_firmware.yaml"}
    ] + [ROOT / "guis" / "universal" / "gui.yaml", ROOT / "guis" / "universal" / "gui_touch.yaml"]
    if include_hal_boards:
        yaml_files.extend(sorted((ROOT / "modules" / "hal").glob("*.yaml")))
        yaml_files.extend(sorted((ROOT / "boards").glob("*.yaml")))

    chunks = [YAML_FULL_LAMBDA_PREAMBLE]
    fn_idx = 0
    for yf in yaml_files:
        if yf.is_file():
            fn_idx = _append_full_lambdas_from_yaml(yf, chunks, fn_idx)
    chunks.append("int main() { (void)loop_probe_singleton(); return 0; }")
    out_cpp.write_text("\n".join(chunks) + "\n", encoding="utf-8")
    return fn_idx


def _append_full_lambdas_from_yaml(yf: Path, chunks: list[str], fn_idx: int) -> int:
    rel = yf.relative_to(ROOT).as_posix()
    lines = yf.read_text(encoding="utf-8").splitlines()
    st = _FullYamlState(rel=rel)
    idx = 0
    while idx < len(lines):
        raw_ln = lines[idx]
        stripped = raw_ln.split("#", 1)[0].rstrip()
        if st.step_line(stripped):
            idx += 1
            continue

        m_block = re.match(r"^(\s*)(?:(-\s*)?lambda\s*:\s*\|-?|[A-Za-z0-9_]+\s*:\s*!lambda\s*\|-?)\s*$", raw_ln)
        if m_block:
            is_cond = ("!lambda" in raw_ln) or (not raw_ln.lstrip().startswith("-"))
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
            sig = ", ".join(f"[[maybe_unused]] {ct} {cn}" for cn, ct in st.resolve_params())
            fn_idx += 1
            chunks.extend(
                [
                    f"[[maybe_unused]] static {'auto' if is_cond else 'void'} yaml_full_fn_{fn_idx}({sig}) {{",
                    f'#line {body_start} "{rel}"',
                    _sanitize_full_lambda_cpp("\n".join(body)),
                    "}",
                ]
            )
            continue

        m_inline = re.match(
            r"""^(\s*)(?:-\s*)?(?:lambda\s*:\s*|[A-Za-z0-9_]+\s*:\s*!lambda\s+)(['"])(.*)\2\s*$""",
            stripped,
        )
        if m_inline:
            sig = ", ".join(f"[[maybe_unused]] {ct} {cn}" for cn, ct in st.resolve_params())
            ret = "auto" if "return " in m_inline.group(3) else "void"
            fn_idx += 1
            chunks.extend(
                [
                    f"[[maybe_unused]] static {ret} yaml_full_fn_{fn_idx}({sig}) {{",
                    f'#line {idx + 1} "{rel}"',
                    _sanitize_full_lambda_cpp(m_inline.group(3)),
                    "}",
                ]
            )
        idx += 1
    return fn_idx
