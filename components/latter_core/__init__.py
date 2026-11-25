"""Latter Core component: validated board schema, HAL drivers, Protocol v3 state, RPC outbox & actions."""

from __future__ import annotations

import logging
import re
from pathlib import Path
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_HEIGHT, CONF_ID, CONF_PIN, CONF_WIDTH, Framework
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@slampton"]
DEPENDENCIES = ["esp32"]

CONF_BOARD_ID = "board_id"
CONF_BLE_MINOR = "ble_minor"
CONF_GUI = "gui"
CONF_CHIP = "chip"
CONF_PSRAM_MB = "psram_mb"
CONF_DISPLAY = "display"
CONF_BACKLIGHT = "backlight"
CONF_POWER = "power"
CONF_AUDIO = "audio"
CONF_CAPABILITIES = "capabilities"
CONF_DRIVERS = "drivers"
CONF_DIAGNOSTICS = "diagnostics"

# Display keys
CONF_PPI = "ppi"
CONF_ROUND = "round"
CONF_IPS = "ips"
CONF_DCS_SLEEP = "dcs_sleep"
CONF_DC_PIN = "dc_pin"
CONF_CORNER_R = "corner_r"
CONF_VIEW_SCALE = "view_scale"
CONF_DSI_IRAM_SAFE = "dsi_iram_safe"
CONF_SLOT_COUNT = "slot_count"
CONF_CARD_SLOT_COUNT = "card_slot_count"
CONF_BRI_GAMMA_X100 = "bri_gamma_x100"
CONF_BRI_DCS_MIN = "bri_dcs_min"
CONF_BRI_DCS_MAX = "bri_dcs_max"
CONF_TOUCH_INT_PIN = "touch_int_pin"
CONF_TOUCH_RST_PIN = "touch_rst_pin"

# Backlight keys
CONF_EN_PIN = "en_pin"
CONF_EN_INVERTED = "en_inverted"
CONF_INVERTED = "inverted"
CONF_CUSTOM = "custom"
CONF_PWM_HZ = "pwm_hz"
CONF_PWM_BITS = "pwm_bits"
CONF_DUTY_MIN_PM = "duty_min_pm"
CONF_DUTY_MAX_PM = "duty_max_pm"

# Power keys
CONF_BATTERY_MAH = "battery_mah"
CONF_PWR_KEY_PIN = "pwr_key_pin"
CONF_PWR_KEY_ACTIVE_HIGH = "pwr_key_active_high"
CONF_PWR_KEY_DEEP_SLEEP_WAKE = "pwr_key_deep_sleep_wake"
CONF_ACTION_BTN_PIN = "action_btn_pin"
CONF_IMU_INT1_PIN = "imu_int1_pin"
CONF_C6_RESET_PIN = "c6_reset_pin"
CONF_PMU_TS_DISABLE = "pmu_ts_disable"
CONF_PMU_DCDC_KEEP_MASK = "pmu_dcdc_keep_mask"
CONF_PMU_LDO_KEEP_MASK = "pmu_ldo_keep_mask"

# Audio keys
CONF_OUT_RATE = "out_rate"
CONF_OUT_CHANNELS = "out_channels"
CONF_OUT_BITS = "out_bits"
CONF_IN_RATE = "in_rate"

# Capability keys
CONF_TOUCH = "touch"
CONF_MIC = "mic"
CONF_SPEAKER = "speaker"
CONF_IMU = "imu"
CONF_BATTERY = "battery"
CONF_RTC = "rtc"
CONF_STATIONARY = "stationary"
CONF_WEARABLE = "wearable"
CONF_VIDEO_STREAM = "video_stream"
CONF_HW_JPEG = "hw_jpeg"

# Driver keys (consumed by Wave 3 modular HAL driver guards)
CONF_PMU_AXP2101 = "pmu_axp2101"
CONF_RTC_PCF85063 = "rtc_pcf85063"
CONF_IMU_QMI8658 = "imu_qmi8658"
CONF_CODEC_ES8311 = "codec_es8311"

# Diagnostics keys
CONF_NVS_GUARD = "nvs_guard"
CONF_TWDT_BREADCRUMB = "twdt_breadcrumb"

_BOARD_ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,47}$")
_VALID_CHIPS = ("esp32p4", "esp32s3", "esp32s2", "esp32c6", "esp32c3", "esp32")

# Top-level ::latter_core namespace (never esphome::latter_core) to avoid ambiguity under `using namespace esphome;`.
latter_core_ns = cg.global_ns.namespace("latter_core")
LatterLoopProbe = latter_core_ns.class_("LatterLoopProbe", cg.Component)


def _validate_board_id(value: Any) -> str:
    s = cv.string_strict(value).strip()
    if not _BOARD_ID_RE.fullmatch(s):
        raise cv.Invalid(
            f"board_id must match ^[a-z0-9][a-z0-9_-]{{0,47}}$ (got {value!r})"
        )
    return s


_GPIO_NUM = cv.int_range(min=0, max=63)

_DISPLAY_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_WIDTH, default=0): cv.int_range(min=0, max=2048),
        cv.Optional(CONF_HEIGHT, default=0): cv.int_range(min=0, max=2048),
        cv.Optional(CONF_PPI, default=0): cv.int_range(min=0, max=1000),
        cv.Optional(CONF_ROUND, default=False): cv.boolean,
        cv.Optional(CONF_IPS, default=False): cv.boolean,
        cv.Optional(CONF_DCS_SLEEP, default=False): cv.boolean,
        cv.Optional(CONF_DC_PIN): _GPIO_NUM,
        cv.Optional(CONF_CORNER_R): cv.int_range(min=0, max=256),
        cv.Optional(CONF_VIEW_SCALE): cv.float_range(min=0.75, max=2.0),
        cv.Optional(CONF_DSI_IRAM_SAFE, default=False): cv.boolean,
        cv.Optional(CONF_SLOT_COUNT): cv.int_range(min=1, max=16),
        cv.Optional(CONF_CARD_SLOT_COUNT): cv.int_range(min=1, max=16),
        cv.Optional(CONF_BRI_GAMMA_X100): cv.int_range(min=50, max=400),
        cv.Optional(CONF_BRI_DCS_MIN): cv.int_range(min=0, max=255),
        cv.Optional(CONF_BRI_DCS_MAX): cv.int_range(min=1, max=255),
        cv.Optional(CONF_TOUCH_INT_PIN): _GPIO_NUM,
        cv.Optional(CONF_TOUCH_RST_PIN): _GPIO_NUM,
    }
)

_BACKLIGHT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PIN): _GPIO_NUM,
        cv.Optional(CONF_EN_PIN): _GPIO_NUM,
        cv.Optional(CONF_EN_INVERTED, default=False): cv.boolean,
        cv.Optional(CONF_INVERTED, default=False): cv.boolean,
        cv.Optional(CONF_CUSTOM, default=False): cv.boolean,
        cv.Optional(CONF_PWM_HZ): cv.int_range(min=100, max=40000),
        cv.Optional(CONF_PWM_BITS): cv.int_range(min=8, max=13),
        cv.Optional(CONF_DUTY_MIN_PM): cv.int_range(min=0, max=1000),
        cv.Optional(CONF_DUTY_MAX_PM): cv.int_range(min=0, max=1000),
    }
)

_POWER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_BATTERY_MAH): cv.int_range(min=0, max=20000),
        cv.Optional(CONF_PWR_KEY_PIN): _GPIO_NUM,
        cv.Optional(CONF_PWR_KEY_ACTIVE_HIGH, default=True): cv.boolean,
        cv.Optional(CONF_PWR_KEY_DEEP_SLEEP_WAKE, default=False): cv.boolean,
        cv.Optional(CONF_ACTION_BTN_PIN): _GPIO_NUM,
        cv.Optional(CONF_IMU_INT1_PIN): _GPIO_NUM,
        cv.Optional(CONF_C6_RESET_PIN): _GPIO_NUM,
        cv.Optional(CONF_PMU_TS_DISABLE, default=False): cv.boolean,
        cv.Optional(CONF_PMU_DCDC_KEEP_MASK): cv.int_range(min=0, max=0xFF),
        cv.Optional(CONF_PMU_LDO_KEEP_MASK): cv.int_range(min=0, max=0xFFF),
    }
)

_AUDIO_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_OUT_RATE, default=48000): cv.int_range(min=8000, max=96000),
        cv.Optional(CONF_OUT_CHANNELS, default=1): cv.int_range(min=1, max=2),
        cv.Optional(CONF_OUT_BITS, default=16): cv.one_of(16, 24, 32, int=True),
        cv.Optional(CONF_IN_RATE, default=16000): cv.int_range(min=8000, max=48000),
    }
)

_CAPABILITIES_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_DISPLAY): cv.boolean,
        cv.Optional(CONF_TOUCH): cv.boolean,
        cv.Optional(CONF_AUDIO, default=True): cv.boolean,
        cv.Optional(CONF_MIC): cv.boolean,
        cv.Optional(CONF_SPEAKER): cv.boolean,
        cv.Optional(CONF_IMU, default=False): cv.boolean,
        cv.Optional(CONF_BATTERY, default=False): cv.boolean,
        cv.Optional(CONF_RTC, default=False): cv.boolean,
        cv.Optional(CONF_STATIONARY, default=False): cv.boolean,
        cv.Optional(CONF_WEARABLE, default=False): cv.boolean,
        cv.Optional(CONF_VIDEO_STREAM): cv.boolean,
        cv.Optional(CONF_HW_JPEG): cv.boolean,
    }
)

_DRIVERS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PMU_AXP2101, default=False): cv.boolean,
        cv.Optional(CONF_RTC_PCF85063, default=False): cv.boolean,
        cv.Optional(CONF_IMU_QMI8658, default=False): cv.boolean,
        cv.Optional(CONF_CODEC_ES8311, default=False): cv.boolean,
    }
)

_DIAGNOSTICS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_NVS_GUARD, default=True): cv.boolean,
        cv.Optional(CONF_TWDT_BREADCRUMB, default=True): cv.boolean,
    }
)


def compute_auto_font_sizes(
    width: int,
    height: int,
    ppi: int,
    *,
    view_scale_pct: int = 0,
    is_stationary: bool = False,
) -> dict[str, int]:
    """Deterministic integer 7-font ramp mirroring include/ui_scale.h compute_auto_font_sizes()."""
    del height
    eff_w = width if width > 120 else 466
    eff_ppi = ppi if ppi > 50 else 266
    if view_scale_pct > 0:
        eff_vs = max(75, min(200, int(view_scale_pct)))
    else:
        eff_vs = 145 if is_stationary else 100
    s_min = 900 if (is_stationary or eff_vs >= 120) else 760
    s = max(s_min, (eff_ppi * eff_vs * 10 + 133) // 266)

    def _dp(v: int) -> int:
        return (v * s + 500) // 1000

    def _mm_ceil(mm_x10: int) -> int:
        return (mm_x10 * eff_ppi + 253) // 254

    micro = max(12, _mm_ceil(13), _dp(14))
    small = max(15, _mm_ceil(20), _dp(21))
    btn_bold = max(small + 1, min(_dp(25), (eff_w * 71) // 1000))
    date = max(btn_bold + 1, min(_dp(28), (eff_w * 78 + 20) // 1000))
    chrono = max(date + 4, min(_dp(38), (eff_w * 108) // 1000))
    clock_pm = 215 if eff_vs >= 120 else 185
    time_px = max(chrono + 10, min(_dp(76), (eff_w * clock_pm + 200) // 1000))
    return {
        "font_time": time_px,
        "font_chrono_time": chrono,
        "font_date": date,
        "font_btn_bold": btn_bold,
        "font_small": small,
        "font_micro": micro,
        "font_games_title": chrono,
    }


def _final_validate_latter_core(config: dict[str, Any]) -> dict[str, Any]:
    caps = config.get(CONF_CAPABILITIES) or {}
    if not caps.get(CONF_DISPLAY, False):
        return config
    disp = config.get(CONF_DISPLAY) or {}
    vs_pct = (
        round(float(disp[CONF_VIEW_SCALE]) * 100.0)
        if CONF_VIEW_SCALE in disp
        else (145 if bool(caps.get(CONF_STATIONARY, False)) else 100)
    )
    auto_fonts = compute_auto_font_sizes(
        int(disp.get(CONF_WIDTH, 466)),
        int(disp.get(CONF_HEIGHT, 466)),
        int(disp.get(CONF_PPI, 266)),
        view_scale_pct=vs_pct,
        is_stationary=bool(caps.get(CONF_STATIONARY, False)),
    )
    full_cfg = fv.full_config.get(None) or CORE.config or {}
    for font_entry in full_cfg.get("font") or ():
        if not isinstance(font_entry, dict):
            continue
        fid = str(font_entry.get(CONF_ID, ""))
        if fid in auto_fonts and int(font_entry.get("size", 0)) == 1:
            font_entry["size"] = auto_fonts[fid]
    for disp_entry in full_cfg.get("display") or ():
        if isinstance(disp_entry, dict) and int(disp_entry.get("rotation", 0) or 0) != 0:
            _LOGGER.warning(
                "latter_core: display '%s' uses software rotation=%s; "
                "ui_esphome_display.h disables the direct RGB565 framebuffer fast path when rotation != 0. "
                "Prefer hardware panel MADCTL/swap_xy/mirror_x/mirror_y for full frame rate.",
                disp_entry.get(CONF_ID, "?"),
                disp_entry.get("rotation"),
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate_latter_core


def _validate_latter_core(config: dict[str, Any]) -> dict[str, Any]:
    gui_mode = config[CONF_GUI]
    caps = dict(config.get(CONF_CAPABILITIES) or {})
    disp = dict(config.get(CONF_DISPLAY) or {})
    power = dict(config.get(CONF_POWER) or {})
    bl = dict(config.get(CONF_BACKLIGHT) or {})

    if gui_mode == "none":
        if caps.get(CONF_DISPLAY, False):
            raise cv.Invalid(
                "gui: none cannot be combined with capabilities.display: true "
                "(use gui: universal for display boards, or capabilities.display: false for screenless nodes)",
                path=[CONF_GUI],
            )
        caps.setdefault(CONF_DISPLAY, False)
        caps.setdefault(CONF_TOUCH, False)
    else:
        caps.setdefault(CONF_DISPLAY, True)
        caps.setdefault(CONF_TOUCH, True)

    if caps[CONF_DISPLAY]:
        for key in (CONF_WIDTH, CONF_HEIGHT, CONF_PPI):
            if int(disp.get(key, 0)) <= 0:
                raise cv.Invalid(
                    f"display.{key} must be > 0 when capabilities.display is true",
                    path=[CONF_DISPLAY, key],
                )

    if caps.get(CONF_BATTERY, False):
        mah = power.get(CONF_BATTERY_MAH)
        if mah is None or not (100 <= int(mah) <= 20000):
            raise cv.Invalid(
                "power.battery_mah (100..20000) is required when capabilities.battery is true",
                path=[CONF_POWER, CONF_BATTERY_MAH],
            )

    if (
        CONF_DUTY_MIN_PM in bl
        and CONF_DUTY_MAX_PM in bl
        and bl[CONF_DUTY_MIN_PM] > bl[CONF_DUTY_MAX_PM]
    ):
        raise cv.Invalid(
            "backlight.duty_min_pm must be <= backlight.duty_max_pm",
            path=[CONF_BACKLIGHT, CONF_DUTY_MIN_PM],
        )

    config[CONF_CAPABILITIES] = caps
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LatterLoopProbe),
            cv.Required(CONF_BOARD_ID): _validate_board_id,
            cv.Optional(CONF_BLE_MINOR, default=1): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_GUI, default="universal"): cv.one_of(
                "universal", "none", lower=True
            ),
            cv.Optional(CONF_CHIP): cv.one_of(*_VALID_CHIPS, lower=True),
            cv.Optional(CONF_PSRAM_MB): cv.int_range(min=0, max=64),
            cv.Optional(CONF_DISPLAY, default={}): _DISPLAY_SCHEMA,
            cv.Optional(CONF_BACKLIGHT, default={}): _BACKLIGHT_SCHEMA,
            cv.Optional(CONF_POWER, default={}): _POWER_SCHEMA,
            cv.Optional(CONF_AUDIO, default={}): _AUDIO_SCHEMA,
            cv.Optional(CONF_CAPABILITIES, default={}): _CAPABILITIES_SCHEMA,
            cv.Optional(CONF_DRIVERS, default={}): _DRIVERS_SCHEMA,
            cv.Optional(CONF_DIAGNOSTICS, default={}): _DIAGNOSTICS_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework(Framework.ESP_IDF),
    _validate_latter_core,
)

STAGED_HEADERS: tuple[str, ...] = (
    "latter_prof.h",
    "latter_traits.h",
    "hal_manifest.h",
    "latter_protocol.h",
    "latter_rpc.h",
    "audio_router.h",
    "state/state_common.h",
    "state/state_tile_grid.h",
    "state/state_prefs.h",
    "state/state_shortcuts.h",
    "state/state_nav.h",
    "state/state_media.h",
    "state/state_areas.h",
    "state/state_weather_cal.h",
    "state/state_time_alarms.h",
    "state/state_alerts_intercom.h",
    "state/device_runtime_state.h",
    "state/state_nvs_snapshot.h",
    "hal_imu_qmi8658.h",
    "hal_battery.h",
    "hal_panel_power.h",
    "hal_display_lifecycle.h",
    "hal_radio_policy.h",
    "hal_sleep_pm.h",
    "hal_touch.h",
    "hal_link_report.h",
    "hal_video_stream.h",
    "esphome_bridge.h",
    "latter_actions.h",
)

# Minimal DAG roots emitted into generated main.cpp; transitively pull every staged core header
# needed by YAML lambdas without polluting main.cpp with 28 redundant top-level #include lines.
GLOBAL_YAML_INCLUDES: tuple[str, ...] = (
    "esphome_bridge.h",
    "latter_actions.h",
    "state/state_nvs_snapshot.h",
    "audio_router.h",
    "hal_battery.h",
    "hal_imu_qmi8658.h",
    "hal_link_report.h",
    "hal_touch.h",
    "hal_video_stream.h",
)

_SENDSPIN_QUIET_CMAKE = """\
function(_latter_quiet_sendspin_logs)
  if(COMMAND idf_build_get_property)
    idf_build_get_property(_latter_build_comps BUILD_COMPONENTS)
    if("sendspin__sendspin-cpp" IN_LIST _latter_build_comps)
      idf_component_get_property(_latter_ss_lib sendspin__sendspin-cpp COMPONENT_LIB)
      if(_latter_ss_lib AND TARGET ${_latter_ss_lib})
        get_target_property(_latter_ss_defs ${_latter_ss_lib} COMPILE_DEFINITIONS)
        if(_latter_ss_defs)
          list(FILTER _latter_ss_defs EXCLUDE REGEX "^LOG_LOCAL_LEVEL=")
          list(APPEND _latter_ss_defs "LOG_LOCAL_LEVEL=ESP_LOG_WARN")
          set_target_properties(${_latter_ss_lib} PROPERTIES COMPILE_DEFINITIONS "${_latter_ss_defs}")
        else()
          target_compile_definitions(${_latter_ss_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_WARN)
        endif()
      endif()
    endif()
  endif()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _latter_quiet_sendspin_logs)"""


def AUTO_LOAD(config: dict[str, Any] | None) -> list[str]:
    """Load latter_ui when a display is present (screenless nodes skip latter_ui cleanly)."""
    cfg = config if isinstance(config, dict) else {}
    gui_mode = str(cfg.get(CONF_GUI, "universal")).strip().lower()
    caps = cfg.get(CONF_CAPABILITIES) or {}
    has_display = bool(caps.get(CONF_DISPLAY, gui_mode != "none"))
    return ["latter_ui"] if has_display else []


def _copy_if_changed(src_file: Path, dst_file: Path) -> None:
    """Copy header bytes only when changed to avoid busting Ninja/CCache mtimes."""
    src_bytes = src_file.read_bytes()
    if dst_file.is_file() and dst_file.read_bytes() == src_bytes:
        return
    dst_file.parent.mkdir(parents=True, exist_ok=True)
    dst_file.write_bytes(src_bytes)


def _add_bool_define(name: str, value: bool) -> None:
    cg.add_define(name, 1 if value else 0)


def _emit_board_defines(config: dict[str, Any]) -> None:
    """Emit validated LATTER_* board capability/pin macros into esphome/core/defines.h."""
    gui_mode = config[CONF_GUI]
    disp = config[CONF_DISPLAY]
    bl = config[CONF_BACKLIGHT]
    power = config[CONF_POWER]
    audio = config[CONF_AUDIO]
    caps = config[CONF_CAPABILITIES]
    drivers = config[CONF_DRIVERS]
    diag = config[CONF_DIAGNOSTICS]

    cg.add_define("LATTER_BOARD_ID", config[CONF_BOARD_ID])
    cg.add_define("LATTER_BLE_MINOR", int(config[CONF_BLE_MINOR]))
    _add_bool_define("LATTER_GUI_UNIVERSAL", gui_mode == "universal")

    if CONF_CHIP in config:
        chip = config[CONF_CHIP]
        _add_bool_define("LATTER_CHIP_P4", chip == "esp32p4")
        _add_bool_define("LATTER_SOC_S3", chip == "esp32s3")
    if CONF_PSRAM_MB in config:
        cg.add_define("LATTER_PSRAM_MB", int(config[CONF_PSRAM_MB]))

    # Display traits
    has_display = bool(caps[CONF_DISPLAY])
    cg.add_define("LATTER_SCREEN_W", int(disp[CONF_WIDTH]) if has_display else 0)
    cg.add_define("LATTER_SCREEN_H", int(disp[CONF_HEIGHT]) if has_display else 0)
    cg.add_define("LATTER_PANEL_PPI", int(disp[CONF_PPI]) if has_display else 0)
    _add_bool_define("LATTER_ROUND_DISPLAY", bool(disp[CONF_ROUND]) if has_display else False)
    _add_bool_define("LATTER_PANEL_IPS", bool(disp[CONF_IPS]) if has_display else False)
    if disp[CONF_DCS_SLEEP]:
        cg.add_define("LATTER_PANEL_DCS_SLEEP", 1)
    if CONF_DC_PIN in disp:
        cg.add_define("LATTER_DISPLAY_DC_PIN", int(disp[CONF_DC_PIN]))
    if CONF_CORNER_R in disp:
        cg.add_define("LATTER_CORNER_R", int(disp[CONF_CORNER_R]))
    if CONF_VIEW_SCALE in disp:
        cg.add_define("LATTER_VIEW_SCALE_PCT", round(float(disp[CONF_VIEW_SCALE]) * 100.0))
    else:
        cg.add_define("LATTER_VIEW_SCALE_PCT", 145 if bool(caps[CONF_STATIONARY]) else 100)
    if disp[CONF_DSI_IRAM_SAFE]:
        cg.add_define("LATTER_DSI_IRAM_SAFE", 1)
    if CONF_SLOT_COUNT in disp:
        cg.add_define("LATTER_SLOT_COUNT", int(disp[CONF_SLOT_COUNT]))
    if CONF_CARD_SLOT_COUNT in disp:
        cg.add_define("LATTER_CARD_SLOT_COUNT", int(disp[CONF_CARD_SLOT_COUNT]))
    if CONF_BRI_GAMMA_X100 in disp:
        cg.add_define("LATTER_BRI_GAMMA_X100", int(disp[CONF_BRI_GAMMA_X100]))
    if CONF_BRI_DCS_MIN in disp:
        cg.add_define("LATTER_BRI_DCS_MIN", int(disp[CONF_BRI_DCS_MIN]))
    if CONF_BRI_DCS_MAX in disp:
        cg.add_define("LATTER_BRI_DCS_MAX", int(disp[CONF_BRI_DCS_MAX]))
    if CONF_TOUCH_INT_PIN in disp:
        cg.add_define("LATTER_TOUCH_INT_PIN", int(disp[CONF_TOUCH_INT_PIN]))
    if CONF_TOUCH_RST_PIN in disp:
        cg.add_define("LATTER_TOUCH_RST_PIN", int(disp[CONF_TOUCH_RST_PIN]))

    # Backlight traits
    if CONF_PIN in bl:
        cg.add_define("LATTER_BACKLIGHT_PIN", int(bl[CONF_PIN]))
    if CONF_EN_PIN in bl:
        cg.add_define("LATTER_BACKLIGHT_EN_PIN", int(bl[CONF_EN_PIN]))
    if bl[CONF_EN_INVERTED]:
        cg.add_define("LATTER_BACKLIGHT_EN_INVERTED", 1)
    if bl[CONF_INVERTED]:
        cg.add_define("LATTER_BACKLIGHT_INVERTED", 1)
    if bl[CONF_CUSTOM]:
        cg.add_define("LATTER_BACKLIGHT_CUSTOM", 1)
    if CONF_PWM_HZ in bl:
        cg.add_define("LATTER_BACKLIGHT_PWM_HZ", int(bl[CONF_PWM_HZ]))
    if CONF_PWM_BITS in bl:
        cg.add_define("LATTER_BACKLIGHT_PWM_BITS", int(bl[CONF_PWM_BITS]))
    if CONF_DUTY_MIN_PM in bl:
        cg.add_define("LATTER_BACKLIGHT_DUTY_MIN_PM", int(bl[CONF_DUTY_MIN_PM]))
    if CONF_DUTY_MAX_PM in bl:
        cg.add_define("LATTER_BACKLIGHT_DUTY_MAX_PM", int(bl[CONF_DUTY_MAX_PM]))

    # Power & wake-pin traits
    if CONF_BATTERY_MAH in power:
        cg.add_define("LATTER_BATTERY_MAH", int(power[CONF_BATTERY_MAH]))
    if CONF_PWR_KEY_PIN in power:
        cg.add_define("LATTER_PWR_KEY_PIN", int(power[CONF_PWR_KEY_PIN]))
    _add_bool_define("LATTER_PWR_KEY_ACTIVE_HIGH", bool(power[CONF_PWR_KEY_ACTIVE_HIGH]))
    _add_bool_define(
        "LATTER_PWR_KEY_DEEP_SLEEP_WAKE", bool(power[CONF_PWR_KEY_DEEP_SLEEP_WAKE])
    )
    if CONF_ACTION_BTN_PIN in power:
        cg.add_define("LATTER_ACTION_BTN_PIN", int(power[CONF_ACTION_BTN_PIN]))
    if CONF_IMU_INT1_PIN in power:
        cg.add_define("LATTER_IMU_INT1_PIN", int(power[CONF_IMU_INT1_PIN]))
    if CONF_C6_RESET_PIN in power:
        cg.add_define("LATTER_C6_RESET_PIN", int(power[CONF_C6_RESET_PIN]))
    _add_bool_define("LATTER_PMU_TS_DISABLE", bool(power[CONF_PMU_TS_DISABLE]))
    if CONF_PMU_DCDC_KEEP_MASK in power:
        cg.add_define("LATTER_PMU_DCDC_KEEP_MASK", int(power[CONF_PMU_DCDC_KEEP_MASK]))
    if CONF_PMU_LDO_KEEP_MASK in power:
        cg.add_define("LATTER_PMU_LDO_KEEP_MASK", int(power[CONF_PMU_LDO_KEEP_MASK]))

    # Audio traits
    cg.add_define("LATTER_AUDIO_OUT_RATE", int(audio[CONF_OUT_RATE]))
    cg.add_define("LATTER_AUDIO_OUT_CH", int(audio[CONF_OUT_CHANNELS]))
    cg.add_define("LATTER_AUDIO_OUT_BITS", int(audio[CONF_OUT_BITS]))
    cg.add_define("LATTER_AUDIO_IN_RATE", int(audio[CONF_IN_RATE]))

    # Capability flags
    has_audio = bool(caps[CONF_AUDIO])
    _add_bool_define("LATTER_HAS_DISPLAY", has_display)
    _add_bool_define("LATTER_HAS_TOUCH", bool(caps[CONF_TOUCH]))
    _add_bool_define("LATTER_HAS_AUDIO", has_audio)
    _add_bool_define("LATTER_HAS_MIC", bool(caps.get(CONF_MIC, has_audio)))
    _add_bool_define("LATTER_HAS_SPEAKER", bool(caps.get(CONF_SPEAKER, has_audio)))
    _add_bool_define("LATTER_HAS_IMU", bool(caps[CONF_IMU]))
    _add_bool_define("LATTER_HAS_BATTERY", bool(caps[CONF_BATTERY]))
    _add_bool_define("LATTER_HAS_RTC", bool(caps[CONF_RTC]))
    _add_bool_define("LATTER_IS_STATIONARY", bool(caps[CONF_STATIONARY]))
    _add_bool_define("LATTER_IS_WEARABLE", bool(caps[CONF_WEARABLE]))
    if CONF_VIDEO_STREAM in caps:
        _add_bool_define("LATTER_HAS_VIDEO_STREAM", bool(caps[CONF_VIDEO_STREAM]))
    if CONF_HW_JPEG in caps:
        _add_bool_define("LATTER_HAS_HW_JPEG", bool(caps[CONF_HW_JPEG]))

    # Optional driver flags (consumed by modular HAL drivers)
    _add_bool_define("LATTER_DRV_PMU_AXP2101", bool(drivers[CONF_PMU_AXP2101]))
    _add_bool_define("LATTER_DRV_RTC_PCF85063", bool(drivers[CONF_RTC_PCF85063]))
    _add_bool_define("LATTER_DRV_IMU_QMI8658", bool(drivers[CONF_IMU_QMI8658]))
    _add_bool_define("LATTER_DRV_CODEC_ES8311", bool(drivers[CONF_CODEC_ES8311]))

    # Diagnostics toggles
    _add_bool_define("LATTER_DIAG_NVS_GUARD", bool(diag[CONF_NVS_GUARD]))
    _add_bool_define("LATTER_DIAG_TWDT_BREADCRUMB", bool(diag[CONF_TWDT_BREADCRUMB]))


def _stage_core_headers(config: dict[str, Any]) -> None:
    """Copy Latter Core headers into ESPHome's generated src tree when content differs."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)

    _emit_board_defines(config)

    for hdr in STAGED_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter Core header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        _copy_if_changed(src_file, dst_file)

    for hdr in GLOBAL_YAML_INCLUDES:
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))


async def to_code(config: dict[str, Any]) -> None:
    """Stage Latter Core headers and register ESP32 linker/sdkconfig/power hooks."""
    _stage_core_headers(config)

    if CORE.is_esp32:
        from esphome.components import esp32

        if hasattr(CORE, "require_wake_loop_threadsafe"):
            CORE.require_wake_loop_threadsafe()

        # Required by pm_telemetry_stall.cpp (eTaskGetState in stall_loop_state).
        esp32.add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TRACE_FACILITY", True)

        # Flash-write counters for System: Link Report (pm_telemetry_stall.cpp).
        if config[CONF_DIAGNOSTICS][CONF_NVS_GUARD]:
            cg.add_build_flag("-Wl,--wrap=nvs_set_blob")
            cg.add_build_flag("-Wl,--wrap=esp_flash_erase_region")

        # Clamp sendspin-cpp LOG_LOCAL_LEVEL to ESP_LOG_WARN unless DEBUG/VERBOSE.
        logger_cfg = CORE.config.get("logger", {})
        logger_level = str(logger_cfg.get("level", "DEBUG")).upper()
        sendspin_tag_level = str(
            logger_cfg.get("logs", {}).get("esp-idf", logger_level)
        ).upper()
        if (
            "sendspin" in CORE.config
            and logger_level not in ("DEBUG", "VERBOSE", "VERY_VERBOSE")
            and sendspin_tag_level not in ("DEBUG", "VERBOSE", "VERY_VERBOSE")
        ):
            esp32.add_src_cmake(_SENDSPIN_QUIET_CMAKE)

    if "wifi" in CORE.config:
        from esphome.components import wifi

        wifi.enable_runtime_power_save_control()

    cg.add_global(
        cg.RawStatement('#include "esphome/components/latter_core/latter_core.h"')
    )
    probe = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(probe, config)
    cg.add_define("USE_LATTER_LOOP_PROBE")
