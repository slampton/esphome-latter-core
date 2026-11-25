# Contributing to Latter Core (`esphome-latter-core`)

Thank you for contributing to `esphome-latter-core`! Latter Core is an open-source, modular, trait-driven C++20 smart companion OS platform, hardware catalog, and standalone raw-HCI BLE beacon component for Home Assistant displays and wearables (`ESPHome 2026.11.1+`, `ESP-IDF 5.5+`).

## License & Developer Certificate of Origin (DCO)

All contributions are licensed under the **[Apache License, Version 2.0](LICENSE)** (inbound = outbound; no separate CLA required). Third-party attributions live in **[NOTICE](NOTICE)**.

Every commit must include a `Signed-off-by` trailer certifying the [Developer Certificate of Origin v1.1](https://developercertificate.org/):

```bash
git commit -s -m "Short summary of your change"
```

## Branching & CI Gate (`dev` -> `main`)

- Target all pull requests against the **`dev`** branch (never directly against `main`).
- `main` is the branch ESPHome Device Builder packages pull from, and moves **only** by automated fast-forward after `validate` and all flagship device firmware compiles (`pocket-assistant`, `mobile-assistant`, `wrist-assist`, `remote-assistant`, and `desk-assistant` `rev1` + `rev3`) pass on `dev`.
- CI rewrites `external_components`, `${latter_asset_ref}` sound/model URLs, and package includes to the checked-out worktree automatically via `.github/scripts/pin_to_checkout.sh`, so fork pull requests validate and compile out of the box.

## Architectural Ground Rules

1. **Zero-Cycle DAG & Coarse Component Boundaries**: Every header and `.cpp` translation unit sits inside an acyclic `#include` graph enforced by Tarjan SCC plus coarse component/header boundaries in `tools/verify_core/` (see [`AGENTS.md`](AGENTS.md) and [`docs/architecture.md`](docs/architecture.md)). `latter_core` (`namespace latter_core`) and `latter_beacon` never include `ui_*.h` or call `latter_ui::mark_dirty_*`; inside `include/ui_*.h` (`namespace latter_ui`, partitioned into Toolkit vs Universal Apps), only `ui_esphome_display.h` may include `esphome/*` or `hal_*.h`, keeping all 14 `latter_ui` page renderers pure C++20 over `Rgb565Surface` for native 6-geometry host testing.
2. **Typed `latter_core:` Board Schema, Never Product Names**: Core C++ and YAML modules branch strictly on `LATTER_*` hardware capability traits, `LATTER_DRV_*` driver-identity guards, and screen geometry (`UiLayout`), configured via typed `latter_core:` YAML in `boards/*.yaml` (never raw `-DLATTER_*` `build_flags` or board names), with zero legacy shims.
3. **Standard Terminology**: Use Home Assistant and standard systems terminology (`System`, `Sites`, `Areas`, `Devices`, `Roles`, `Policies`, `Bridges`).
4. **Hardware Onboarding**: Adding a new board follows the 12-point onboarding gates in [`docs/porting-hardware.md`](docs/porting-hardware.md), backed by schematic-sourced `boards/facts/<board_id>.json` and `python tools/check_boards.py --release --check-docs` (or `--external <board.yaml>` for community boards).
5. **On-Device UI**: Read [`docs/device-ui-design.md`](docs/device-ui-design.md) before modifying layout, `FrameHitTable` (`kMaxRegions = 128`, `D-019` `>= 7.0 mm` targets), adaptive `glass_style` (`Auto` | `Clear` | `Dark`), colors (`>= 4.5:1` WCAG AA contrast), or row-major RGB565 `fast_*` rendering primitives.
6. **Protocol v3 Parity**: `protocol/latter_v3.json` is the single source of truth for Protocol v3 wire/behavioral contracts. After editing it, run `python tools/gen_protocol.py` to regenerate `include/latter_protocol.h`, `DEVICE_CONTRACT.md`, and the sibling `ha-latter/custom_components/latter/protocol_v3.json` (then run `python ha-latter/tools/gen_studio_schema.py` when `studio_contract.json` or preference specs change).

## Local Pre-Flight Checks

Before opening a pull request, run the repository verification suite from `esphome-latter-core/` (including the 6-geometry C++20 RGB565 framebuffer & `FrameHitTable` host binaries, compiled with ASan + UBSan on Linux):

```bash
python tools/verify_core.py
python tools/gen_protocol.py --check
python tools/check_protocol_usage.py
python tools/check_boards.py --release --check-docs
python tools/new_board.py --bringup --check
python tools/gen_sounds.py --check
python tools/gen_mdi_pack.py --check
```

When working in a dual-repo checkout alongside `ha-latter`, run the unified cross-repo gate (`python .agents/scripts/verify_workspace.py`) from the workspace root.
