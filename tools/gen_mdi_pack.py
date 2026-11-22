#!/usr/bin/env python3
"""Compile or verify the embedded Material Design Icons (MDI) 1:1 vector catalog.

Compiles all ~7,447 Pictogrammers Material Design Icons (@mdi/js, Apache-2.0) into
`components/latter_ui/ui_mdi.cpp` as compact 24x24 Bezier bytecode grouped into
16-icon LZSS buckets with a collision-free FNV-1a binary-search index and a 4x
anti-aliased nonzero-winding scanline rasterizer.

Usage:
  python tools/gen_mdi_pack.py --from-mdi-js <mdi.js | mdi_js.tgz>
  python tools/gen_mdi_pack.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
    tools_dir = Path(__file__).resolve().parent
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    from mdi_pack.catalog_sync import (  # noqa: PLC0415, RUF100
        build_from_mdi_js,
        check_committed,
        sync_runtime,
    )

    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--from-mdi-js",
        type=Path,
        help="Path to @mdi/js mdi.js or npm tarball",
    )
    ap.add_argument(
        "--sync-runtime",
        action="store_true",
        help="Sync C++ runtime template into committed ui_mdi.cpp",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify committed ui_mdi.cpp integrity",
    )
    args = ap.parse_args()
    if args.from_mdi_js:
        build_from_mdi_js(args.from_mdi_js)
        return check_committed()
    if args.sync_runtime:
        sync_runtime()
        return check_committed()
    if args.check:
        return check_committed()
    ap.error("Pass --from-mdi-js <path>, --sync-runtime, or --check")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
