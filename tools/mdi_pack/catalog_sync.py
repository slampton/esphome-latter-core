"""MDI catalog builder, integrity verifier, and runtime template synchronizer."""

from __future__ import annotations

import hashlib
import re
import sys
import tarfile
from pathlib import Path

from .glyph_raster import (
    BUCKET_ICONS,
    encode_svg_path,
    fnv1a_32,
    verify_bytecode_stream,
)
from .pack_writer import (
    CPP_RUNTIME,
    lzss_compress,
    lzss_decompress,
    render_mdi_pack_cpp,
)

ROOT = Path(__file__).resolve().parent.parent.parent
OUT_CPP = ROOT / "components" / "latter_ui" / "ui_mdi.cpp"


def _compute_pack_sha256(
    hashes: list[int],
    ids: list[int],
    lengths: list[int],
    offsets: list[int],
    blob: bytes,
) -> str:
    digest = hashlib.sha256()
    for h in hashes:
        digest.update(h.to_bytes(4, "little"))
    for i_id in ids:
        digest.update(i_id.to_bytes(2, "little"))
    for bl in lengths:
        digest.update(bl.to_bytes(2, "little"))
    for bo in offsets:
        digest.update(bo.to_bytes(4, "little"))
    digest.update(blob)
    return digest.hexdigest()


def build_from_mdi_js(src_path: Path) -> None:
    if src_path.suffix in (".tgz", ".gz"):
        with tarfile.open(src_path) as tf:
            mdi_f = tf.extractfile("package/mdi.js")
            pkg_f = tf.extractfile("package/package.json")
            if mdi_f is None or pkg_f is None:
                raise RuntimeError(
                    "Tarball missing package/mdi.js or package/package.json"
                )
            mdi_txt = mdi_f.read().decode("utf-8")
            pkg_txt = pkg_f.read().decode("utf-8")
            m_ver = re.search(r'"version"\s*:\s*"([^"]+)"', pkg_txt)
            mdi_ver = m_ver.group(1) if m_ver else "7.4.47"
    else:
        mdi_txt = src_path.read_text(encoding="utf-8")
        mdi_ver = "7.4.47"

    pairs = re.findall(r'export var (mdi\w+)\s*=\s*"([^"]+)"', mdi_txt)
    if len(pairs) < 7000:
        raise RuntimeError(f"Expected >= 7000 MDI icons, found {len(pairs)}")

    items: list[tuple[str, int, bytes]] = []
    seen_hashes: dict[int, str] = {}
    for var_name, svg_d in pairs:
        slug = var_name[3:].lower()
        h = fnv1a_32(slug)
        if h == 0:
            raise RuntimeError(f"Zero hash for {slug}")
        if h in seen_hashes:
            raise RuntimeError(f"FNV-1a collision: {slug} vs {seen_hashes[h]}")
        seen_hashes[h] = slug
        bc = encode_svg_path(svg_d)
        verify_bytecode_stream(bc)
        items.append((slug, h, bc))

    items.sort(key=lambda it: it[0])
    icon_count = len(items)
    byte_lengths = [len(it[2]) for it in items]

    comp_blob = bytearray()
    bucket_offsets = [0]
    max_bucket_uncomp = 0
    for b_start in range(0, icon_count, BUCKET_ICONS):
        raw_bucket = b"".join(it[2] for it in items[b_start : b_start + BUCKET_ICONS])
        max_bucket_uncomp = max(max_bucket_uncomp, len(raw_bucket))
        comp_bucket = lzss_compress(raw_bucket)
        if lzss_decompress(comp_bucket, len(raw_bucket)) != raw_bucket:
            b_id = b_start // BUCKET_ICONS
            raise RuntimeError(f"LZSS round-trip mismatch in bucket {b_id}")
        comp_blob.extend(comp_bucket)
        bucket_offsets.append(len(comp_blob))

    bucket_count = len(bucket_offsets) - 1
    sorted_hash_pairs = sorted((items[idx][1], idx) for idx in range(icon_count))
    sorted_hashes = [p[0] for p in sorted_hash_pairs]
    sorted_ids = [p[1] for p in sorted_hash_pairs]

    comp_bytes = bytes(comp_blob)
    sha_hex = _compute_pack_sha256(
        sorted_hashes, sorted_ids, byte_lengths, bucket_offsets, comp_bytes
    )

    cpp_text = render_mdi_pack_cpp(
        mdi_ver=mdi_ver,
        sha_hex=sha_hex,
        icon_count=icon_count,
        bucket_count=bucket_count,
        max_bucket_uncomp=max_bucket_uncomp,
        sorted_hashes=sorted_hashes,
        sorted_ids=sorted_ids,
        byte_lengths=byte_lengths,
        bucket_offsets=bucket_offsets,
        comp_blob=comp_bytes,
    )
    OUT_CPP.write_text(cpp_text, encoding="utf-8", newline="\n")
    total_rodata = (
        icon_count * 4
        + icon_count * 2
        + icon_count * 2
        + len(bucket_offsets) * 4
        + len(comp_bytes)
    )
    print(
        f"Wrote {OUT_CPP.relative_to(ROOT)}: {icon_count} MDI v{mdi_ver} icons, "
        f"{total_rodata / 1024:.1f} KB total .rodata "
        f"(blob {len(comp_bytes) / 1024:.1f} KB, max bucket {max_bucket_uncomp} B)"
    )


def check_committed() -> int:
    if not OUT_CPP.is_file():
        print(f"ERROR: {OUT_CPP} missing", file=sys.stderr)
        return 1
    txt = OUT_CPP.read_text(encoding="utf-8")
    m_sha = re.search(r"MDI_PACK_SHA256:\s*([0-9a-f]{64})", txt)
    m_cnt = re.search(r"MDI_ICON_COUNT:\s*(\d+)", txt)
    if not (m_sha and m_cnt):
        print(
            "ERROR: Missing MDI_PACK_SHA256 / MDI_ICON_COUNT header in ui_mdi.cpp",
            file=sys.stderr,
        )
        return 1
    expected_sha = m_sha.group(1)
    icon_count = int(m_cnt.group(1))
    if icon_count < 7000:
        print(f"ERROR: Expected >= 7000 icons, got {icon_count}", file=sys.stderr)
        return 1

    def extract_array(name: str) -> list[int]:
        m = re.search(rf"{name}\[[^\]]+\]\s*=\s*\{{([^}}]+)\}};", txt)
        if not m:
            raise ValueError(f"Missing array {name}")
        return [
            int(clean, 0)
            for tok in m.group(1).replace("\n", " ").split(",")
            if (clean := tok.strip().rstrip("Uu"))
        ]

    hashes = extract_array("kMdiSortedHashes")
    ids = extract_array("kMdiSortedIconIds")
    lengths = extract_array("kMdiIconByteLengths")
    offsets = extract_array("kMdiBucketCompOffsets")
    blob = bytes(extract_array("kMdiCompBlob"))

    if (
        len(hashes) != icon_count
        or len(ids) != icon_count
        or len(lengths) != icon_count
    ):
        print("ERROR: Array length mismatch in ui_mdi.cpp", file=sys.stderr)
        return 1
    if any(hashes[i] >= hashes[i + 1] for i in range(icon_count - 1)):
        print("ERROR: kMdiSortedHashes is not strictly ascending", file=sys.stderr)
        return 1

    actual_sha = _compute_pack_sha256(hashes, ids, lengths, offsets, blob)
    if actual_sha != expected_sha:
        print("ERROR: SHA-256 mismatch in ui_mdi.cpp (hand-edited?)", file=sys.stderr)
        return 1

    bucket_count = len(offsets) - 1
    for b_idx in range(bucket_count):
        first_id = b_idx * BUCKET_ICONS
        last_id = min(icon_count, first_id + BUCKET_ICONS)
        exp_len = sum(lengths[first_id:last_id])
        raw = lzss_decompress(blob[offsets[b_idx] : offsets[b_idx + 1]], exp_len)
        off = 0
        for icon_id in range(first_id, last_id):
            bl = lengths[icon_id]
            verify_bytecode_stream(raw[off : off + bl])
            off += bl
    if CPP_RUNTIME not in txt:
        print(
            "ERROR: CPP_RUNTIME mismatch in ui_mdi.cpp (run --sync-runtime)",
            file=sys.stderr,
        )
        return 1
    return 0


def sync_runtime() -> None:
    if not OUT_CPP.is_file():
        raise RuntimeError(f"{OUT_CPP} missing")
    txt = OUT_CPP.read_text(encoding="utf-8")
    def_guard = (
        '#if __has_include("esphome/core/defines.h")\n'
        '#include "esphome/core/defines.h"\n'
        "#endif\n\n"
    )
    if "esphome/core/defines.h" not in txt:
        txt = txt.replace(
            '#if __has_include("ui_mdi.h")',
            def_guard + '#if __has_include("ui_mdi.h")',
            1,
        )
    inc_old = '#include "../../include/ui_mdi.h"\n#endif\n\nnamespace latter_ui {'
    inc_new = (
        '#include "../../include/ui_mdi.h"\n#endif\n\n'
        "#if defined(USE_ESP32) || defined(ESP_PLATFORM)\n"
        "#include <esp_heap_caps.h>\n"
        "#endif\n\n"
        "namespace latter_ui {"
    )
    if inc_old in txt and "#include <esp_heap_caps.h>" not in txt:
        txt = txt.replace(inc_old, inc_new, 1)
    marker = "// " + "=" * 78 + "\n// Runtime LZSS Bucket Decoder"
    idx = txt.find(marker)
    if idx < 0:
        raise RuntimeError("Could not locate runtime marker in ui_mdi.cpp")
    prefix = txt[:idx].rstrip("\n")
    updated = f"{prefix}\n{CPP_RUNTIME}\n}}  // namespace latter_ui\n"
    OUT_CPP.write_text(updated, encoding="utf-8", newline="\n")
