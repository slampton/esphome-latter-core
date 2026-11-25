"""Subprocess tool gates, 6-geometry ASan/UBSan/-Werror host test runner, and trait matrix checks."""

from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from .common import ROOT, run_trusted_cmd, strip_cpp_comments
from .host_stubs_idf import write_beacon_host_test, write_host_ui_stubs
from .yaml_cpp_checks import (
    write_yaml_full_lambdas_syntax_harness,
    write_yaml_lambdas_syntax_harness,
)

ESP32_ONLY_CORE_TUS: Final[frozenset[str]] = frozenset(
    {
        "audio_es8311.cpp",
        "esphome_bridge.cpp",
        "imu_qmi8658.cpp",
        "touch_hal.cpp",
    }
)

FIVE_GEOM_CONFIGS: Final[tuple[tuple[str, tuple[str, ...]], ...]] = (
    (
        "GEOM_320x480_P4",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=320",
            "LATTER_SCREEN_H=480",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=500",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=1",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=165",
            "LATTER_VIEW_SCALE_PCT=145",
        ),
    ),
    (
        "GEOM_410x502_WRIST",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=410",
            "LATTER_SCREEN_H=502",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=1",
            "LATTER_IS_STATIONARY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=400",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=1",
            "LATTER_CHIP_P4=0",
            "LATTER_CORNER_R=98",
            "LATTER_PANEL_PPI=315",
            "LATTER_VIEW_SCALE_PCT=100",
        ),
    ),
    (
        "GEOM_466x466_POCKET",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=466",
            "LATTER_SCREEN_H=466",
            "LATTER_ROUND_DISPLAY=1",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=500",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=0",
            "LATTER_CORNER_R=233",
            "LATTER_PANEL_PPI=266",
            "LATTER_VIEW_SCALE_PCT=100",
        ),
    ),
    (
        "GEOM_480x480_MOBILE",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=480",
            "LATTER_SCREEN_H=480",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=1000",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=1",
            "LATTER_CHIP_P4=0",
            "LATTER_CORNER_R=76",
            "LATTER_PANEL_PPI=313",
            "LATTER_VIEW_SCALE_PCT=100",
        ),
    ),
    (
        "GEOM_720x720_DESK",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=720",
            "LATTER_SCREEN_H=720",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=1",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=1",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=254",
            "LATTER_VIEW_SCALE_PCT=145",
        ),
    ),
    (
        "GEOM_360x360_MINI_ROUND",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=360",
            "LATTER_SCREEN_H=360",
            "LATTER_ROUND_DISPLAY=1",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=400",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=0",
            "LATTER_CORNER_R=180",
            "LATTER_PANEL_PPI=255",
            "LATTER_VIEW_SCALE_PCT=100",
        ),
    ),
    (
        "GEOM_800x480_LANDSCAPE",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=800",
            "LATTER_SCREEN_H=480",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=1",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=1",
            "LATTER_HAS_HW_JPEG=1",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=187",
            "LATTER_VIEW_SCALE_PCT=135",
        ),
    ),
    (
        "GEOM_1024x600_LANDSCAPE",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=1024",
            "LATTER_SCREEN_H=600",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=1",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=1",
            "LATTER_HAS_HW_JPEG=1",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=170",
            "LATTER_VIEW_SCALE_PCT=145",
        ),
    ),
    (
        "GEOM_1280x800_TABLET",
        (
            "LATTER_HOST_TEST=1",
            "LATTER_SCREEN_W=1280",
            "LATTER_SCREEN_H=800",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_IS_WEARABLE=0",
            "LATTER_IS_STATIONARY=1",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_CHIP_P4=1",
            "LATTER_HAS_HW_JPEG=1",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=149",
            "LATTER_VIEW_SCALE_PCT=150",
        ),
    ),
)

UI_TRAIT_MATRICES: Final[tuple[tuple[str, tuple[str, ...]], ...]] = (
    (
        "ROUND_BATT_HOST",
        (
            "LATTER_SCREEN_W=466",
            "LATTER_SCREEN_H=466",
            "LATTER_ROUND_DISPLAY=1",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=400",
            "LATTER_HAS_IMU=1",
            "LATTER_IS_STATIONARY=0",
        ),
    ),
    (
        "RECT_WALL_HOST",
        (
            "LATTER_SCREEN_W=480",
            "LATTER_SCREEN_H=480",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_IS_STATIONARY=1",
        ),
    ),
    (
        "S3_ESP32",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_API",
            "USE_WIFI",
            "USE_FONT",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "CONFIG_IDF_TARGET_ESP32S3=1",
            "LATTER_SCREEN_W=410",
            "LATTER_SCREEN_H=502",
            "LATTER_PANEL_PPI=315",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=400",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=1",
            "LATTER_IS_STATIONARY=0",
        ),
    ),
    (
        "P4_320X480_ESP32",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_API",
            "USE_WIFI",
            "USE_FONT",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_BEACON_HOSTED",
            "CONFIG_IDF_TARGET_ESP32P4=1",
            "CONFIG_ESP_TIMER_IN_IRAM=1",
            "USE_ESP32_VARIANT_ESP32P4=1",
            "LATTER_CHIP_P4=1",
            "LATTER_SCREEN_W=320",
            "LATTER_SCREEN_H=480",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_CORNER_R=0",
            "LATTER_PANEL_PPI=165",
            "LATTER_VIEW_SCALE_PCT=145",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=500",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=0",
        ),
    ),
    (
        "P4_ESP32",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_API",
            "USE_WIFI",
            "USE_FONT",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_BEACON_HOSTED",
            "CONFIG_IDF_TARGET_ESP32P4=1",
            "CONFIG_ESP_TIMER_IN_IRAM=1",
            "USE_ESP32_VARIANT_ESP32P4=1",
            "LATTER_CHIP_P4=1",
            "LATTER_SCREEN_W=720",
            "LATTER_SCREEN_H=720",
            "LATTER_PANEL_PPI=254",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=1",
        ),
    ),
    (
        "TOUCHLESS_AUDIOLESS_ESP32",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_API",
            "USE_WIFI",
            "USE_FONT",
            "CONFIG_IDF_TARGET_ESP32S3=1",
            "LATTER_SCREEN_W=800",
            "LATTER_SCREEN_H=480",
            "LATTER_PANEL_PPI=187",
            "LATTER_ROUND_DISPLAY=0",
            "LATTER_CORNER_R=0",
            "LATTER_VIEW_SCALE_PCT=135",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=1",
            "LATTER_HAS_TOUCH=0",
            "LATTER_HAS_AUDIO=0",
            "LATTER_HAS_SPEAKER=0",
            "LATTER_HAS_MIC=0",
            "LATTER_HAS_HW_JPEG=0",
        ),
    ),
)

CORE_TRAIT_MATRICES: Final[tuple[tuple[str, tuple[str, ...]], ...]] = (
    (
        "CORE_S3_NO_RTC",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_TOUCHSCREEN",
            "USE_API",
            "USE_WIFI",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_LOOP_PROBE",
            "CONFIG_IDF_TARGET_ESP32S3=1",
            "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=1",
            "LATTER_SCREEN_W=466",
            "LATTER_SCREEN_H=466",
            "LATTER_PANEL_PPI=266",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=500",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=0",
            "LATTER_PWR_KEY_PIN=16",
            "LATTER_PWR_KEY_ACTIVE_HIGH=1",
            "LATTER_PWR_KEY_DEEP_SLEEP_WAKE=1",
        ),
    ),
    (
        "CORE_S3_RTC",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_TOUCHSCREEN",
            "USE_API",
            "USE_WIFI",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_LOOP_PROBE",
            "CONFIG_IDF_TARGET_ESP32S3=1",
            "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=1",
            "LATTER_SCREEN_W=410",
            "LATTER_SCREEN_H=502",
            "LATTER_PANEL_PPI=315",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=400",
            "LATTER_HAS_IMU=1",
            "LATTER_HAS_RTC=1",
            "LATTER_IS_STATIONARY=0",
            "LATTER_PWR_KEY_PIN=10",
            "LATTER_PWR_KEY_ACTIVE_HIGH=1",
            "LATTER_PWR_KEY_DEEP_SLEEP_WAKE=1",
        ),
    ),
    (
        "CORE_P4_BATT_QUIET",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_TOUCHSCREEN",
            "USE_API",
            "USE_WIFI",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_LOOP_PROBE",
            "CONFIG_IDF_TARGET_ESP32P4=1",
            "CONFIG_ESP_TIMER_IN_IRAM=1",
            "USE_ESP32_VARIANT_ESP32P4=1",
            "LATTER_CHIP_P4=1",
            "LATTER_SCREEN_W=320",
            "LATTER_SCREEN_H=480",
            "LATTER_PANEL_PPI=165",
            "LATTER_C6_RESET_PIN=54",
            "LATTER_HAS_BATTERY=1",
            "LATTER_BATTERY_MAH=1000",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=0",
        ),
    ),
    (
        "CORE_P4_WALL",
        (
            "USE_ESP32",
            "ESP_PLATFORM",
            "USE_I2C",
            "USE_TOUCHSCREEN",
            "USE_API",
            "USE_WIFI",
            "USE_VOICE_ASSISTANT",
            "USE_MICRO_WAKE_WORD",
            "USE_MICROPHONE",
            "USE_LATTER_LOOP_PROBE",
            "CONFIG_IDF_TARGET_ESP32P4=1",
            "CONFIG_ESP_TIMER_IN_IRAM=1",
            "USE_ESP32_VARIANT_ESP32P4=1",
            "LATTER_CHIP_P4=1",
            "LATTER_SCREEN_W=720",
            "LATTER_SCREEN_H=720",
            "LATTER_PANEL_PPI=254",
            "LATTER_C6_RESET_PIN=54",
            "LATTER_HAS_BATTERY=0",
            "LATTER_HAS_IMU=0",
            "LATTER_HAS_RTC=0",
            "LATTER_IS_STATIONARY=1",
        ),
    ),
)


def _run_py_tool_gates(failures: list[str]) -> None:
    py_tools = (
        ("PROTOCOL CODEGEN", ["tools/gen_protocol.py", "--check"]),
        ("PROTOCOL USAGE", ["tools/check_protocol_usage.py"]),
        ("BOARD FACTS", ["tools/check_boards.py", "--check-docs"]),
        ("BRINGUP PROBES", ["tools/new_board.py", "--bringup", "--check"]),
        ("SOUND FILES", ["tools/gen_sounds.py", "--check"]),
        ("MDI ICON PACK", ["tools/gen_mdi_pack.py", "--check"]),
    )
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    for tag, cmd in py_tools:
        proc = run_trusted_cmd([sys.executable, *cmd], cwd=ROOT, env=env)
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or f"{cmd[0]} exited {proc.returncode}").strip()
            failures.append(f"[{tag}] {detail}")


def _collect_and_validate_host_sources(failures: list[str]) -> tuple[list[Path], list[Path], list[Path], Path | None]:
    """Return `(test_cpps, host_core_cpps, geom_ui_cpps, ui_mdi_cpp)` after checking `tests/host/` structure."""
    host_dir = ROOT / "tests" / "host"
    if not (host_dir / "test_harness.h").is_file():
        failures.append("[HOST TESTS] tests/host/test_harness.h missing")
    test_cpps = sorted(host_dir.glob("*.cpp"))
    found_names = {p.name for p in test_cpps}
    if "test_main.cpp" not in found_names:
        failures.append("[HOST TESTS] tests/host/test_main.cpp missing")
    suite_tus = [name for name in found_names if name.startswith("test_") and name != "test_main.cpp"]
    if not suite_tus:
        failures.append("[HOST TESTS] tests/host/ must contain at least one test_*.cpp suite alongside test_main.cpp")
    for tf in [*test_cpps, *sorted(host_dir.glob("*.h"))]:
        clean = strip_cpp_comments(tf.read_text(encoding="utf-8"))
        if re.search(r'#\s*include\s*"[^"]+\.cpp"', clean):
            failures.append(f"[HOST TESTS] {tf.relative_to(ROOT).as_posix()} uses forbidden unity-build #include *.cpp")

    host_core_cpps = [
        p for p in sorted((ROOT / "components" / "latter_core").glob("*.cpp")) if p.name not in ESP32_ONLY_CORE_TUS
    ]
    all_ui_cpps = sorted((ROOT / "components" / "latter_ui").glob("*.cpp"))
    ui_mdi_cpp = next((p for p in all_ui_cpps if p.name == "ui_mdi.cpp"), None)
    geom_ui_cpps = [p for p in all_ui_cpps if p.name != "ui_mdi.cpp"]
    return test_cpps, host_core_cpps, geom_ui_cpps, ui_mdi_cpp


def _probe_gpp_sanitizers(gpp: str, td_path: Path) -> list[str]:
    """Return ASan/UBSan flags if the host compiler + runtime libraries support them."""
    if os.name == "nt" or os.environ.get("LATTER_DISABLE_SANITIZERS") == "1":
        return []
    probe_cpp = td_path / "san_probe.cpp"
    probe_bin = td_path / "san_probe"
    probe_cpp.write_text("int main() { int x = 0; return x; }\n", encoding="utf-8")
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    comp = run_trusted_cmd([gpp, "-std=c++20", *flags, str(probe_cpp), "-o", str(probe_bin)])
    if comp.returncode != 0:
        return []
    run = run_trusted_cmd([str(probe_bin)])
    return flags if run.returncode == 0 else []


@dataclass(frozen=True)
class _GppGeomBuildCtx:
    gpp: str
    san_flags: list[str]
    td_path: Path
    mdi_obj: Path | None
    sources: list[Path]


def _compile_and_run_single_gpp_geom(
    ctx: _GppGeomBuildCtx,
    geom_lbl: str,
    defs: tuple[str, ...],
) -> str | None:
    bin_path = ctx.td_path / (f"{geom_lbl}.exe" if os.name == "nt" else geom_lbl)
    inc_flags = [
        f"-I{ROOT / 'include'}",
        f"-I{ROOT / 'components' / 'latter_core'}",
        f"-I{ROOT / 'components' / 'latter_ui'}",
        f"-I{ROOT / 'tests' / 'host'}",
    ]
    cmd = [
        ctx.gpp,
        "-std=c++20",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        *ctx.san_flags,
        *inc_flags,
        *[f"-D{d}" for d in defs],
        *[str(s) for s in ctx.sources],
        *([str(ctx.mdi_obj)] if ctx.mdi_obj is not None else []),
        "-o",
        str(bin_path),
    ]
    comp = run_trusted_cmd(cmd)
    if comp.returncode != 0:
        return f"[HOST TESTS COMPILE ({geom_lbl})] {(comp.stderr or comp.stdout).strip()}"
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
    run = run_trusted_cmd([str(bin_path)], env=env)
    if run.returncode != 0:
        return f"[HOST TESTS RUN ({geom_lbl})] {(run.stderr or run.stdout).strip()}"
    return None


def _compile_gpp_mdi_obj(
    gpp: str,
    san_flags: list[str],
    ui_mdi_cpp: Path,
    mdi_obj: Path,
) -> str | None:
    mcomp = run_trusted_cmd(
        [
            gpp,
            "-std=c++20",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            *san_flags,
            f"-I{ROOT / 'include'}",
            "-c",
            str(ui_mdi_cpp),
            "-o",
            str(mdi_obj),
        ]
    )
    if mcomp.returncode != 0:
        return f"[UI MDI COMPILE] {(mcomp.stderr or mcomp.stdout).strip()}"
    return None


def _run_gpp_cmd_gate(tag: str, cmd: list[str]) -> str | None:
    res = run_trusted_cmd(cmd)
    if res.returncode != 0:
        return f"[{tag}] {(res.stderr or res.stdout).strip()}"
    return None


def _run_gpp_beacon_vhci(gpp: str, beacon_inc: Path, stub_dir: Path, td_path: Path) -> str | None:
    beacon_cpp = beacon_inc / "latter_beacon.cpp"
    beacon_test_cpp = td_path / "beacon_host_test.cpp"
    write_beacon_host_test(beacon_test_cpp)
    beacon_bin = td_path / ("beacon_host_test.exe" if os.name == "nt" else "beacon_host_test")
    bcomp = run_trusted_cmd(
        [
            gpp, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-DUSE_ESP32",
            f"-I{beacon_inc}", f"-I{stub_dir}", str(beacon_cpp), str(beacon_test_cpp), "-o", str(beacon_bin),
        ]
    )
    if bcomp.returncode != 0:
        return f"[BEACON VHCI COMPILE] {(bcomp.stderr or bcomp.stdout).strip()}"
    brun = run_trusted_cmd([str(beacon_bin)])
    if brun.returncode != 0:
        return f"[BEACON VHCI RUN] {(brun.stderr or brun.stdout).strip()}"
    return None


def _submit_gpp_syntax_and_beacon_tasks(
    pool: ThreadPoolExecutor,
    gpp: str,
    td_path: Path,
) -> list[Future[str | None]]:
    futs: list[Future[str | None]] = []
    lam_cpp = td_path / "yaml_lambdas_check.cpp"
    if write_yaml_lambdas_syntax_harness(lam_cpp) > 0:
        futs.append(
            pool.submit(
                _run_gpp_cmd_gate,
                "YAML LAMBDA COMPILE",
                [gpp, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'include'}", str(lam_cpp)],
            )
        )

    stub_dir = td_path / "ui_stubs"
    write_host_ui_stubs(stub_dir)
    beacon_inc = ROOT / "components" / "latter_beacon"
    beacon_cpp = beacon_inc / "latter_beacon.cpp"
    core_inc = ROOT / "components" / "latter_core"
    ui_inc = ROOT / "components" / "latter_ui"

    if beacon_cpp.is_file():
        futs.append(pool.submit(_run_gpp_beacon_vhci, gpp, beacon_inc, stub_dir, td_path))
        futs.append(
            pool.submit(
                _run_gpp_cmd_gate,
                "BEACON HOSTED COMPILE",
                [
                    gpp, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-Werror",
                    "-DUSE_ESP32", "-DUSE_LATTER_BEACON_HOSTED", f"-I{beacon_inc}", f"-I{stub_dir}", str(beacon_cpp),
                ],
            )
        )

    core_cpps = sorted(core_inc.glob("*.cpp"))
    for cfg_lbl, defs in CORE_TRAIT_MATRICES:
        futs.append(
            pool.submit(
                _run_gpp_cmd_gate,
                f"CORE CPP SYNTAX ({cfg_lbl})",
                [
                    gpp, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'include'}", f"-I{core_inc}", f"-I{stub_dir}",
                    *[f"-D{d}" for d in defs], *[str(p) for p in core_cpps],
                ],
            )
        )

    all_ui_cpps = sorted(ui_inc.glob("*.cpp"))
    geom_ui_cpps = [p for p in all_ui_cpps if p.name != "ui_mdi.cpp"]
    full_lam_host = td_path / "yaml_full_lambdas_host.cpp"
    full_lam_esp = td_path / "yaml_full_lambdas_esp32.cpp"
    has_host_lam = write_yaml_full_lambdas_syntax_harness(full_lam_host, include_hal_boards=False) > 0
    has_esp_lam = write_yaml_full_lambdas_syntax_harness(full_lam_esp, include_hal_boards=True) > 0
    for cfg_lbl, defs in UI_TRAIT_MATRICES:
        # ui_mdi.cpp only branches on USE_ESP32 (zero geometry #ifdefs); syntax-check it once on S3_ESP32.
        if cfg_lbl == "TOUCHLESS_AUDIOLESS_ESP32":
            matrix_ui_cpps = geom_ui_cpps
            extra_tus = []
        else:
            matrix_ui_cpps = all_ui_cpps if cfg_lbl == "S3_ESP32" else geom_ui_cpps
            extra_tus = (
                ([full_lam_esp] if has_esp_lam else [])
                if "USE_ESP32" in defs
                else ([full_lam_host] if has_host_lam else [])
            )
        futs.append(
            pool.submit(
                _run_gpp_cmd_gate,
                f"UI/YAML CPP SYNTAX ({cfg_lbl})",
                [
                    gpp, "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror",
                    f"-I{ROOT / 'include'}", f"-I{core_inc}", f"-I{ui_inc}", f"-I{beacon_inc}", f"-I{stub_dir}",
                    *[f"-D{d}" for d in defs], *[str(p) for p in (*matrix_ui_cpps, *extra_tus)],
                ],
            )
        )
    return futs


def _run_gpp_gates(gpp: str, *, host_tests_only: bool, failures: list[str]) -> None:
    test_cpps, host_core_cpps, geom_ui_cpps, ui_mdi_cpp = _collect_and_validate_host_sources(failures)
    if not test_cpps:
        return
    workers = min(max(os.cpu_count() or 4, 2), 8)
    with tempfile.TemporaryDirectory() as td, ThreadPoolExecutor(max_workers=workers) as pool:
        td_path = Path(td)
        san_flags = _probe_gpp_sanitizers(gpp, td_path)
        mdi_obj: Path | None = (td_path / "ui_mdi.o") if ui_mdi_cpp is not None else None
        mdi_fut: Future[str | None] | None = (
            pool.submit(_compile_gpp_mdi_obj, gpp, san_flags, ui_mdi_cpp, mdi_obj)
            if ui_mdi_cpp is not None and mdi_obj is not None
            else None
        )
        # Overlap independent syntax/beacon checks with ui_mdi.o and the 5-geometry host builds.
        extra_futs = [] if host_tests_only else _submit_gpp_syntax_and_beacon_tasks(pool, gpp, td_path)

        mdi_err = mdi_fut.result() if mdi_fut is not None else None
        if mdi_err:
            failures.append(mdi_err)
            geom_futs: list[Future[str | None]] = []
        else:
            build_ctx = _GppGeomBuildCtx(
                gpp=gpp,
                san_flags=san_flags,
                td_path=td_path,
                mdi_obj=mdi_obj,
                sources=[*host_core_cpps, *geom_ui_cpps, *test_cpps],
            )
            geom_futs = [
                pool.submit(_compile_and_run_single_gpp_geom, build_ctx, lbl, defs)
                for lbl, defs in FIVE_GEOM_CONFIGS
            ]

        for fut in (*geom_futs, *extra_futs):
            err = fut.result()
            if err:
                failures.append(err)


def _find_msvc_vcvars() -> Path | None:
    candidates = (
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
    )
    return next((p for p in candidates if p.is_file()), None)


def _run_msvc_gates(vcvars: Path, *, host_tests_only: bool, failures: list[str]) -> None:
    test_cpps, host_core_cpps, geom_ui_cpps, ui_mdi_cpp = _collect_and_validate_host_sources(failures)
    if not test_cpps:
        return
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        bat_lines = [
            "@echo off",
            f'call "{vcvars}" >nul',
            "if errorlevel 1 exit /b 1",
        ]
        mdi_obj_arg = ""
        if ui_mdi_cpp is not None:
            mdi_obj = td_path / "ui_mdi.obj"
            bat_lines.extend(
                [
                    (
                        f"cl /nologo /utf-8 /std:c++20 /O1 /W4 /WX /EHsc /D_CRT_SECURE_NO_WARNINGS "
                        f'/I"{ROOT / "include"}" /c "{ui_mdi_cpp}" "/Fo:{mdi_obj}"'
                    ),
                    "if errorlevel 1 exit /b 1",
                ]
            )
            mdi_obj_arg = f'"{mdi_obj}"'

        src_args = " ".join(f'"{p}"' for p in (*host_core_cpps, *geom_ui_cpps, *test_cpps))
        inc_args = (
            f'/I"{ROOT / "include"}" /I"{ROOT / "components" / "latter_core"}" '
            f'/I"{ROOT / "components" / "latter_ui"}" /I"{ROOT / "tests" / "host"}"'
        )
        for geom_lbl, defs in FIVE_GEOM_CONFIGS:
            obj_dir = td_path / f"objs_{geom_lbl}"
            obj_dir.mkdir(parents=True, exist_ok=True)
            exe_path = td_path / f"{geom_lbl}.exe"
            def_args = " ".join(f"/D{d}" for d in defs)
            bat_lines.extend(
                [
                    (
                        f"cl /nologo /utf-8 /std:c++20 /O1 /W4 /WX /MP /EHsc /D_CRT_SECURE_NO_WARNINGS "
                        f'{inc_args} {def_args} {src_args} {mdi_obj_arg} "/Fe:{exe_path}" "/Fo:{obj_dir}\\\\"'
                    ),
                    "if errorlevel 1 exit /b 1",
                    f'"{exe_path}"',
                    "if errorlevel 1 exit /b 1",
                ]
            )

        if not host_tests_only:
            _append_msvc_syntax_and_beacon_lines(td_path, bat_lines)

        bat_path = td_path / "run_host_tests.cmd"
        bat_path.write_text("\r\n".join(bat_lines) + "\r\n", encoding="utf-8")
        res = run_trusted_cmd(["cmd.exe", "/c", str(bat_path)])
        if res.returncode != 0:
            failures.append(f"[HOST TESTS MSVC] {(res.stderr or res.stdout).strip()}")


def _append_msvc_syntax_and_beacon_lines(td_path: Path, bat_lines: list[str]) -> None:
    lam_cpp = td_path / "yaml_lambdas_check.cpp"
    if write_yaml_lambdas_syntax_harness(lam_cpp) > 0:
        bat_lines.extend(
            [
                (
                    f'cl /nologo /utf-8 /std:c++20 /Zs /W4 /WX /EHsc /D_CRT_SECURE_NO_WARNINGS '
                    f'/I"{ROOT / "include"}" "{lam_cpp}"'
                ),
                "if errorlevel 1 exit /b 1",
            ]
        )
    stub_dir = td_path / "ui_stubs"
    write_host_ui_stubs(stub_dir)
    beacon_inc = ROOT / "components" / "latter_beacon"
    beacon_cpp = beacon_inc / "latter_beacon.cpp"
    core_inc = ROOT / "components" / "latter_core"
    ui_inc = ROOT / "components" / "latter_ui"
    b_objs = td_path / "objs_beacon"
    b_objs.mkdir(parents=True, exist_ok=True)
    if beacon_cpp.is_file():
        beacon_test_cpp = td_path / "beacon_host_test.cpp"
        write_beacon_host_test(beacon_test_cpp)
        beacon_exe = td_path / "beacon_host_test.exe"
        bat_lines.extend(
            [
                (
                    f"cl /nologo /utf-8 /std:c++20 /O2 /W4 /WX /EHsc /DUSE_ESP32 /D_CRT_SECURE_NO_WARNINGS "
                    f'/I"{beacon_inc}" /I"{stub_dir}" "{beacon_cpp}" "{beacon_test_cpp}" '
                    f'"/Fe:{beacon_exe}" "/Fo:{b_objs}\\\\"'
                ),
                "if errorlevel 1 exit /b 1",
                f'"{beacon_exe}"',
                "if errorlevel 1 exit /b 1",
                (
                    f"cl /nologo /utf-8 /std:c++20 /Zs /W4 /WX /EHsc /DUSE_ESP32 /DUSE_LATTER_BEACON_HOSTED "
                    f'/D_CRT_SECURE_NO_WARNINGS /I"{beacon_inc}" /I"{stub_dir}" "{beacon_cpp}"'
                ),
                "if errorlevel 1 exit /b 1",
            ]
        )
    core_cpps = sorted(core_inc.glob("*.cpp"))
    if core_cpps:
        core_cpp_args = " ".join(f'"{p}"' for p in core_cpps)
        for _, defs in CORE_TRAIT_MATRICES:
            def_args = " ".join(f"/D{d}" for d in defs)
            bat_lines.extend(
                [
                    (
                        f"cl /nologo /utf-8 /std:c++20 /Zs /W4 /WX /MP /EHsc /D_CRT_SECURE_NO_WARNINGS "
                        f'/I"{ROOT / "include"}" /I"{core_inc}" /I"{stub_dir}" {def_args} {core_cpp_args}'
                    ),
                    "if errorlevel 1 exit /b 1",
                ]
            )
    all_ui_cpps = sorted(ui_inc.glob("*.cpp"))
    geom_ui_cpps = [p for p in all_ui_cpps if p.name != "ui_mdi.cpp"]
    if all_ui_cpps:
        full_lam_host = td_path / "yaml_full_lambdas_host.cpp"
        full_lam_esp = td_path / "yaml_full_lambdas_esp32.cpp"
        has_host_lam = write_yaml_full_lambdas_syntax_harness(full_lam_host, include_hal_boards=False) > 0
        has_esp_lam = write_yaml_full_lambdas_syntax_harness(full_lam_esp, include_hal_boards=True) > 0
        for cfg_lbl, defs in UI_TRAIT_MATRICES:
            # ui_mdi.cpp only branches on USE_ESP32 (zero geometry #ifdefs); syntax-check it once on S3_ESP32.
            if cfg_lbl == "TOUCHLESS_AUDIOLESS_ESP32":
                matrix_ui_cpps = geom_ui_cpps
                extra_tus = []
            else:
                matrix_ui_cpps = all_ui_cpps if cfg_lbl == "S3_ESP32" else geom_ui_cpps
                extra_tus = (
                    ([full_lam_esp] if has_esp_lam else [])
                    if "USE_ESP32" in defs
                    else ([full_lam_host] if has_host_lam else [])
                )
            ui_cpp_args = " ".join(f'"{p}"' for p in (*matrix_ui_cpps, *extra_tus))
            def_args = " ".join(f"/D{d}" for d in defs)
            # Enforce full /W4 /WX narrowing checks (no /wd4244 or /wd4267 suppressions).
            bat_lines.extend(
                [
                    (
                        f"cl /nologo /utf-8 /std:c++20 /Zs /W4 /WX /MP /EHsc /D_CRT_SECURE_NO_WARNINGS "
                        f'/I"{ROOT / "include"}" /I"{core_inc}" /I"{ui_inc}" /I"{beacon_inc}" /I"{stub_dir}" '
                        f"{def_args} {ui_cpp_args}"
                    ),
                    "if errorlevel 1 exit /b 1",
                ]
            )


def run_subprocess_gates(failures: list[str], *, host_tests_only: bool = False) -> None:
    """Run Python codegen/protocol/board/sound tools and 5-geometry C++20 host unit & syntax gates."""
    if not host_tests_only:
        _run_py_tool_gates(failures)

    gpp = shutil.which("g++") or shutil.which("clang++")
    if gpp:
        _run_gpp_gates(gpp, host_tests_only=host_tests_only, failures=failures)
        return
    if os.name == "nt":
        vcvars = _find_msvc_vcvars()
        if vcvars is not None:
            _run_msvc_gates(vcvars, host_tests_only=host_tests_only, failures=failures)
            return
    failures.append("[HOST COMPILER] No C++20 compiler (g++, clang++, or MSVC cl.exe) found on PATH")
