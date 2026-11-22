#!/usr/bin/env python3
"""Cross-platform static architecture, DAG, HAL, UI, protocol, and C++20 host verifier for esphome-latter-core.

Usage:
    python tools/verify_core.py                   # Full static + DAG + protocol/board/sound + 5-geom C++20 tests
    python tools/verify_core.py --fast            # Fast static/AST/DAG pass (<250ms, skips subprocesses)
    python tools/verify_core.py --host-tests-only # Compile & run 5-geometry C++20 host binaries only
"""

from __future__ import annotations

import argparse
import os
import sys

from verify_core.board_hal_checks import verify_board_hal_and_security
from verify_core.dag_checks import verify_dag_and_layers
from verify_core.static_repo_checks import verify_static_repo_invariants
from verify_core.subprocess_gates import run_subprocess_gates
from verify_core.ui_checks import verify_device_ui_and_accessibility
from verify_core.yaml_cpp_checks import verify_yaml_cpp_and_ids


def main(argv: list[str] | None = None) -> int:
    """Entry point for `tools/verify_core.py`."""
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--fast", action="store_true", help="Run fast static/AST/DAG checks only.")
    ap.add_argument(
        "--host-tests-only",
        action="store_true",
        help="Compile and run the 5-geometry C++20 host test binaries only.",
    )
    args = ap.parse_args(argv)
    fast_mode = args.fast or os.environ.get("LATTER_VERIFY_FAST") == "1"

    failures: list[str] = []
    warnings: list[str] = []

    if args.host_tests_only:
        run_subprocess_gates(failures, host_tests_only=True)
    else:
        verify_dag_and_layers(failures)
        verify_static_repo_invariants(failures)
        verify_board_hal_and_security(failures)
        verify_device_ui_and_accessibility(failures)
        verify_yaml_cpp_and_ids(failures)
        if not fast_mode:
            run_subprocess_gates(failures, host_tests_only=False)

    for warn in warnings:
        print(f"  [warn] {warn}")

    if failures:
        print(f"verify_core.py FAILED ({len(failures)} issue(s)):")
        for item in failures:
            print(f"  - {item}")
        return 1

    if args.host_tests_only:
        mode_lbl = "5-geometry C++20 host unit tests"
    elif fast_mode:
        mode_lbl = "fast static + Tarjan DAG"
    else:
        mode_lbl = "full static + Tarjan DAG + protocol/boards/sounds + 5-geometry C++20 host tests"
    print(f"verify_core.py PASSED ({mode_lbl}, {len(warnings)} warning(s), 0 issues)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
