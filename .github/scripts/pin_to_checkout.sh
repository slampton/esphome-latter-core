#!/usr/bin/env bash
# Point every remote reference to this repository at the local checkout being tested, so CI validates
# and compiles this exact worktree cleanly (including on external contributor fork PRs where the head
# SHA does not exist on slampton/esphome-latter-core):
#   - external_components (latter_hal, latter_core, latter_ui, latter_beacon): rewritten to type: local
#     pointing at the checked-out components/ directory (with git-only refresh: stripped);
#   - sounds, models, and github:// package includes: rewritten to local relative paths;
#   - dashboard_import package_import_url keeps @main (metadata only, not part of the build).
#
# Usage: .github/scripts/pin_to_checkout.sh [commit_sha]
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import re

ROOT = Path('.').resolve()
COMPONENTS_DIR = (ROOT / 'components').resolve()

GIT_SOURCE_RE = re.compile(
    r'(?m)^(?P<indent>[ ]*)-[ ]+source:\n'
    r'(?P=indent)[ ]+type:[ ]*git\n'
    r'(?P=indent)[ ]+url:[ ]*https://github\.com/[^/]+/esphome-latter-core(?:\.git)?\n'
    r'(?P=indent)[ ]+ref:[ ]*[^\n]+\n'
    r'(?P<comp>(?P=indent)[ ]+components:[ ]*\[[^\]]+\]\n)'
    r'(?:(?P=indent)[ ]+refresh:[ ]*[^\n]+\n)?'
)

for folder in ('devices', 'modules', 'guis', 'boards', 'examples'):
    base = ROOT / folder
    if not base.is_dir():
        continue
    for yml in sorted(base.rglob('*.yaml')):
        rel_comp = Path(*(['..'] * len(yml.parent.relative_to(ROOT).parts))) / 'components'
        rel_root = '/'.join(['..'] * len(yml.parent.relative_to(ROOT).parts))
        orig = yml.read_text(encoding='utf-8')
        lines = []
        for line in orig.splitlines(keepends=True):
            if 'package_import_url' not in line:
                line = re.sub(
                    r'https://raw\.githubusercontent\.com/[^/]+/esphome-latter-core/main/',
                    f'{rel_root}/',
                    line,
                )
                line = re.sub(
                    r'github://[^/]+/esphome-latter-core/([^@\s]+)@main',
                    rf'{rel_root}/\1',
                    line,
                )
            lines.append(line)
        txt = ''.join(lines)

        def _repl(m: re.Match[str]) -> str:
            ind = m.group('indent')
            comp = m.group('comp')
            return (
                f'{ind}- source:\n'
                f'{ind}    type: local\n'
                f'{ind}    path: {rel_comp.as_posix()}\n'
                f'{comp}'
            )

        txt = GIT_SOURCE_RE.sub(_repl, txt)
        if txt != orig:
            yml.write_bytes(txt.encode('utf-8'))
PY
