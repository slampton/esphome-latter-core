# Contributing to Latter Core

Thank you for contributing to `esphome-latter-core`! Latter is built as a clean, trait-driven SDK and firmware OS for Home Assistant touch displays, wearables, and companion sensors.

## License & Developer Certificate of Origin (DCO)

All contributions are licensed under the **[Apache License, Version 2.0](LICENSE)** (inbound = outbound; no separate CLA required).

Every commit must include a `Signed-off-by` trailer certifying the [Developer Certificate of Origin v1.1](https://developercertificate.org/):

```bash
git commit -s -m "Short summary of your change"
```

## Branching & CI Gate (`dev` -> `main`)

- Target all pull requests against the **`dev`** branch (never directly against `main`).
- `main` is the branch ESPHome Device Builder packages pull from, and moves **only** by automated fast-forward after `validate` and all 5 flagship device firmware compiles pass on `dev`.
- CI rewrites `external_components` and asset URLs to the checked-out worktree automatically via `.github/scripts/pin_to_checkout.sh`, so fork pull requests validate and compile out of the box.

## Architectural Ground Rules

1. **Capabilities via Traits, Never Product Names**: Core C++ and YAML modules branch strictly on `LATTER_*` hardware capability flags and screen geometry (`UiLayout`), never on board IDs or device names.
2. **Standard Terminology**: Use Home Assistant and standard systems terminology (`System`, `Sites`, `Areas`, `Devices`, `Roles`, `Policies`, `Bridges`).
3. **Hardware Onboarding**: Adding a new board follows the 12-point onboarding gates in [`docs/porting-hardware.md`](docs/porting-hardware.md), backed by schematic-sourced `boards/facts/<board_id>.json` and `python tools/check_boards.py --check-docs`.
4. **On-Device UI**: Read [`docs/device-ui-design.md`](docs/device-ui-design.md) before modifying layout, touch hit-testing, colors, or rendering primitives.
5. **Protocol v3 Parity**: `protocol/latter_v3.json` is the single source of truth for Protocol v3. After editing it, run `python tools/gen_protocol.py` to regenerate headers, docs, and the companion `ha-latter` schema.

## Local Pre-Flight Checks

Before opening a pull request, run the repository verification suite from the root of `esphome-latter-core`:

```bash
python tools/verify_core.py
python tools/gen_protocol.py --check
python tools/check_protocol_usage.py
python tools/check_boards.py --check-docs
python tools/new_board.py --bringup --check
python tools/gen_sounds.py --check
```
