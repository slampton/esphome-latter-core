"""Latter Core SDK component: Protocol v3 state, zero-privilege RPC outbox, and headless/GUI orchestration."""

from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@slampton"]

CONF_GUI = "gui"

latter_core_ns = cg.esphome_ns.namespace("latter_core")
LatterLoopProbe = latter_core_ns.class_("LatterLoopProbe", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LatterLoopProbe),
        cv.Optional(CONF_GUI, default="universal"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)

CORE_HEADERS: tuple[str, ...] = (
    "latter_protocol.h",
    "latter_state.h",
    "latter_rpc.h",
)


def AUTO_LOAD(config: dict[str, Any]) -> list[str]:
    """Load latter_hal always, and latter_ui only when a display GUI is enabled."""
    if str(config.get(CONF_GUI, "universal")).strip().lower() == "none":
        return ["latter_hal"]
    return ["latter_hal", "latter_ui"]


async def to_code(config: dict[str, Any]) -> None:
    """Stage Latter Core protocol/RPC headers and configure headless traits when gui == 'none'."""
    comp_dir = Path(__file__).resolve().parent
    repo_root = comp_dir.parent.parent
    inc_dir = repo_root / "include"
    if not inc_dir.is_dir():
        inc_dir = repo_root

    dst_src_dir = Path(CORE.relative_src_path(""))
    dst_src_dir.mkdir(parents=True, exist_ok=True)
    cg.add_build_flag(f"-I{dst_src_dir.as_posix()}")

    gui_mode = str(config.get(CONF_GUI, "universal")).strip().lower()
    if gui_mode == "none":
        cg.add_define("LATTER_HAS_DISPLAY", 0)
        cg.add_define("LATTER_HAS_TOUCH", 0)
        cg.add_define("LATTER_SCREEN_W", 0)
        cg.add_define("LATTER_SCREEN_H", 0)

    for hdr in CORE_HEADERS:
        src_file = comp_dir / hdr
        if not src_file.is_file():
            src_file = inc_dir / hdr
        if not src_file.is_file():
            src_file = repo_root / hdr
        if not src_file.is_file():
            raise cv.Invalid(f"Missing required Latter Core header: {hdr}")
        dst_file = Path(CORE.relative_src_path(hdr))
        shutil.copyfile(src_file, dst_file)
        cg.add_global(cg.RawStatement(f'#include "{hdr}"'))

    probe = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(probe, config)
    cg.add_define("USE_LATTER_LOOP_PROBE")
