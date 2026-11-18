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
import hashlib
import math
from pathlib import Path
import re
import sys
import tarfile

ROOT = Path(__file__).resolve().parent.parent
OUT_CPP = ROOT / "components" / "latter_ui" / "ui_mdi.cpp"

BUCKET_ICONS = 16
OP_MOVE = 251
OP_HLINE = 252
OP_VLINE = 253
OP_CUBIC = 254
OP_CLOSE = 255

NUM_RE = re.compile(r"\s*,?\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)")
FLAG_RE = re.compile(r"\s*,?\s*([01])")
CMD_RE = re.compile(r"\s*([MmZzLlHhVvCcSsQqTtAa])")


def fnv1a_32(norm_slug: str) -> int:
    h = 0x811C9DC5
    for b in norm_slug.encode("ascii"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def q_coord(v: float) -> int:
    return max(0, min(250, int(round((v + 0.5) * 10.0))))


def arc_to_cubics(
    x0: float,
    y0: float,
    rx: float,
    ry: float,
    phi_deg: float,
    large_arc: int,
    sweep: int,
    x: float,
    y: float,
) -> list[tuple[float, float, float, float, float, float]]:
    rx = abs(rx)
    ry = abs(ry)
    if rx < 1e-5 or ry < 1e-5 or (abs(x - x0) < 1e-6 and abs(y - y0) < 1e-6):
        return []
    phi = math.radians(phi_deg % 360.0)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)
    dx2 = (x0 - x) * 0.5
    dy2 = (y0 - y) * 0.5
    x1p = cos_phi * dx2 + sin_phi * dy2
    y1p = -sin_phi * dx2 + cos_phi * dy2
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s_lam = math.sqrt(lam)
        rx *= s_lam
        ry *= s_lam
    rx2, ry2 = rx * rx, ry * ry
    num = max(0.0, rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p)
    den = rx2 * y1p * y1p + ry2 * x1p * x1p
    coef = math.sqrt(num / den) if den > 1e-12 else 0.0
    if large_arc == sweep:
        coef = -coef
    cxp = coef * (rx * y1p / ry)
    cyp = coef * (-ry * x1p / rx)
    acx = cos_phi * cxp - sin_phi * cyp + (x0 + x) * 0.5
    acy = sin_phi * cxp + cos_phi * cyp + (y0 + y) * 0.5

    def vec_angle(ux: float, uy: float, vx: float, vy: float) -> float:
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    ux, uy = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx, vy = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    theta1 = vec_angle(1.0, 0.0, ux, uy)
    dtheta = vec_angle(ux, uy, vx, vy)
    if not sweep and dtheta > 0:
        dtheta -= 2.0 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2.0 * math.pi

    segs = max(1, int(math.ceil(abs(dtheta) / (math.pi / 2.0 + 1e-6))))
    dt = dtheta / segs
    alpha = (4.0 / 3.0) * math.tan(dt * 0.25)
    out = []
    for i in range(segs):
        t0 = theta1 + i * dt
        t1 = t0 + dt
        c0, s0 = math.cos(t0), math.sin(t0)
        c1, s1 = math.cos(t1), math.sin(t1)
        p1x, p1y = c0 - alpha * s0, s0 + alpha * c0
        p2x, p2y = c1 + alpha * s1, s1 - alpha * c1
        p3x, p3y = c1, s1

        def xform(px: float, py: float) -> tuple[float, float]:
            return (
                cos_phi * rx * px - sin_phi * ry * py + acx,
                sin_phi * rx * px + cos_phi * ry * py + acy,
            )

        bx1, by1 = xform(p1x, p1y)
        bx2, by2 = xform(p2x, p2y)
        bx3, by3 = (x, y) if i == segs - 1 else xform(p3x, p3y)
        out.append((bx1, by1, bx2, by2, bx3, by3))
    return out


def encode_svg_path(d: str) -> bytes:
    pos = 0
    n = len(d)
    cmd = ""
    cx = cy = sx = sy = prev_ctrl_x = prev_ctrl_y = 0.0
    prev_cmd = ""
    buf = bytearray()
    cur_qx = cur_qy = -1

    def emit_line(nx: float, ny: float) -> None:
        nonlocal cur_qx, cur_qy
        qx, qy = q_coord(nx), q_coord(ny)
        if qx == cur_qx and qy == cur_qy:
            return
        if qy == cur_qy:
            buf.extend((OP_HLINE, qx))
        elif qx == cur_qx:
            buf.extend((OP_VLINE, qy))
        else:
            buf.extend((qx, qy))
        cur_qx, cur_qy = qx, qy

    def emit_cubic(x1: float, y1: float, x2: float, y2: float, x3: float, y3: float) -> None:
        nonlocal cur_qx, cur_qy
        qx1, qy1 = q_coord(x1), q_coord(y1)
        qx2, qy2 = q_coord(x2), q_coord(y2)
        qx3, qy3 = q_coord(x3), q_coord(y3)
        buf.extend((OP_CUBIC, qx1, qy1, qx2, qy2, qx3, qy3))
        cur_qx, cur_qy = qx3, qy3

    def read_num() -> float:
        nonlocal pos
        m = NUM_RE.match(d, pos)
        if not m:
            raise ValueError(f"Expected number at {pos} in {d!r}")
        pos = m.end()
        return float(m.group(1))

    def read_flag() -> int:
        nonlocal pos
        m = FLAG_RE.match(d, pos)
        if not m:
            raise ValueError(f"Expected flag at {pos} in {d!r}")
        pos = m.end()
        return int(m.group(1))

    def has_more_args() -> bool:
        nonlocal pos
        while pos < n and d[pos] in " \t\r\n,":
            pos += 1
        return pos < n and d[pos] not in "MmZzLlHhVvCcSsQqTtAa"

    while pos < n:
        while pos < n and d[pos] in " \t\r\n,":
            pos += 1
        if pos >= n:
            break
        m_cmd = CMD_RE.match(d, pos)
        if m_cmd:
            cmd = m_cmd.group(1)
            pos = m_cmd.end()
        if cmd in ("Z", "z"):
            buf.append(OP_CLOSE)
            cx, cy = sx, sy
            cur_qx, cur_qy = q_coord(cx), q_coord(cy)
            prev_cmd = cmd
            cmd = ""
            continue
        first_iter = True
        while first_iter or has_more_args():
            first_iter = False
            if cmd in ("M", "m"):
                rel = cmd == "m"
                nx = read_num() + (cx if rel else 0.0)
                ny = read_num() + (cy if rel else 0.0)
                cx, cy = nx, ny
                sx, sy = cx, cy
                cur_qx, cur_qy = q_coord(cx), q_coord(cy)
                buf.extend((OP_MOVE, cur_qx, cur_qy))
                prev_cmd = cmd
                cmd = "l" if rel else "L"
            elif cmd in ("L", "l"):
                rel = cmd == "l"
                cx = read_num() + (cx if rel else 0.0)
                cy = read_num() + (cy if rel else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("H", "h"):
                cx = read_num() + (cx if cmd == "h" else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("V", "v"):
                cy = read_num() + (cy if cmd == "v" else 0.0)
                emit_line(cx, cy)
                prev_cmd = cmd
            elif cmd in ("C", "c"):
                rel = cmd == "c"
                x1 = read_num() + (cx if rel else 0.0)
                y1 = read_num() + (cy if rel else 0.0)
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                x3 = read_num() + (cx if rel else 0.0)
                y3 = read_num() + (cy if rel else 0.0)
                emit_cubic(x1, y1, x2, y2, x3, y3)
                prev_ctrl_x, prev_ctrl_y = x2, y2
                cx, cy = x3, y3
                prev_cmd = cmd
            elif cmd in ("S", "s"):
                rel = cmd == "s"
                x1 = (2.0 * cx - prev_ctrl_x) if prev_cmd in ("C", "c", "S", "s") else cx
                y1 = (2.0 * cy - prev_ctrl_y) if prev_cmd in ("C", "c", "S", "s") else cy
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                x3 = read_num() + (cx if rel else 0.0)
                y3 = read_num() + (cy if rel else 0.0)
                emit_cubic(x1, y1, x2, y2, x3, y3)
                prev_ctrl_x, prev_ctrl_y = x2, y2
                cx, cy = x3, y3
                prev_cmd = cmd
            elif cmd in ("Q", "q", "T", "t"):
                rel = cmd in ("q", "t")
                if cmd in ("Q", "q"):
                    qx1 = read_num() + (cx if rel else 0.0)
                    qy1 = read_num() + (cy if rel else 0.0)
                else:
                    qx1 = (2.0 * cx - prev_ctrl_x) if prev_cmd in ("Q", "q", "T", "t") else cx
                    qy1 = (2.0 * cy - prev_ctrl_y) if prev_cmd in ("Q", "q", "T", "t") else cy
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                emit_cubic(
                    cx + (2.0 / 3.0) * (qx1 - cx),
                    cy + (2.0 / 3.0) * (qy1 - cy),
                    x2 + (2.0 / 3.0) * (qx1 - x2),
                    y2 + (2.0 / 3.0) * (qy1 - y2),
                    x2,
                    y2,
                )
                prev_ctrl_x, prev_ctrl_y = qx1, qy1
                cx, cy = x2, y2
                prev_cmd = cmd
            elif cmd in ("A", "a"):
                rel = cmd == "a"
                rx, ry, rot = read_num(), read_num(), read_num()
                la, sw = read_flag(), read_flag()
                x2 = read_num() + (cx if rel else 0.0)
                y2 = read_num() + (cy if rel else 0.0)
                cubs = arc_to_cubics(cx, cy, rx, ry, rot, la, sw, x2, y2)
                if not cubs:
                    emit_line(x2, y2)
                else:
                    for c_seg in cubs:
                        emit_cubic(*c_seg)
                cx, cy = x2, y2
                prev_cmd = cmd
    return bytes(buf)


def lzss_compress(data: bytes) -> bytes:
    """Compress a bucket using 10-bit distance (1..1024) and 6-bit length (3..66)."""
    out = bytearray()
    n = len(data)
    pos = 0
    while pos < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if pos >= n:
                break
            best_len = 0
            best_dist = 0
            start = max(0, pos - 1024)
            max_l = min(66, n - pos)
            if max_l >= 3:
                sub = data[pos : pos + 3]
                idx = data.rfind(sub, start, pos)
                tries = 0
                while idx != -1 and tries < 32:
                    tries += 1
                    ml = 3
                    while ml < max_l and data[idx + ml] == data[pos + ml]:
                        ml += 1
                    if ml > best_len:
                        best_len = ml
                        best_dist = pos - idx
                        if best_len == max_l:
                            break
                    idx = data.rfind(sub, start, idx)
            if best_len >= 3:
                flags |= 1 << bit
                code = (((best_dist - 1) & 0x3FF) << 6) | ((best_len - 3) & 0x3F)
                out.append((code >> 8) & 0xFF)
                out.append(code & 0xFF)
                pos += best_len
            else:
                out.append(data[pos])
                pos += 1
        out[flag_pos] = flags
    return bytes(out)


def lzss_decompress(comp: bytes, expected_len: int) -> bytes:
    out = bytearray()
    pos = 0
    n = len(comp)
    while pos < n and len(out) < expected_len:
        flags = comp[pos]
        pos += 1
        for bit in range(8):
            if pos >= n or len(out) >= expected_len:
                break
            if (flags >> bit) & 1:
                code = (comp[pos] << 8) | comp[pos + 1]
                pos += 2
                dist = ((code >> 6) & 0x3FF) + 1
                length = (code & 0x3F) + 3
                src = len(out) - dist
                if src < 0:
                    raise ValueError("LZSS back-reference out of bounds")
                for _ in range(length):
                    out.append(out[src])
                    src += 1
            else:
                out.append(comp[pos])
                pos += 1
    if len(out) != expected_len:
        raise ValueError(f"LZSS length mismatch: {len(out)} != {expected_len}")
    return bytes(out)


def verify_bytecode_stream(blob: bytes) -> None:
    i = 0
    n = len(blob)
    while i < n:
        op = blob[i]
        if op == OP_MOVE:
            if i + 2 >= n or blob[i + 1] > 250 or blob[i + 2] > 250:
                raise ValueError("Invalid OP_MOVE")
            i += 3
        elif op in (OP_HLINE, OP_VLINE):
            if i + 1 >= n or blob[i + 1] > 250:
                raise ValueError("Invalid OP_HLINE/OP_VLINE")
            i += 2
        elif op == OP_CUBIC:
            if i + 6 >= n or any(b > 250 for b in blob[i + 1 : i + 7]):
                raise ValueError("Invalid OP_CUBIC")
            i += 7
        elif op == OP_CLOSE:
            i += 1
        else:
            if i + 1 >= n or op > 250 or blob[i + 1] > 250:
                raise ValueError("Invalid implicit LINE_TO")
            i += 2


def format_u8_array(data: bytes, cols: int = 24) -> str:
    lines = []
    for i in range(0, len(data), cols):
        chunk = ", ".join(f"{b}" for b in data[i : i + cols])
        lines.append(f"    {chunk},")
    return "\n".join(lines)


def format_u16_array(vals: list[int], cols: int = 16) -> str:
    lines = []
    for i in range(0, len(vals), cols):
        chunk = ", ".join(f"{v}U" for v in vals[i : i + cols])
        lines.append(f"    {chunk},")
    return "\n".join(lines)


def format_u32_array(vals: list[int], cols: int = 10) -> str:
    lines = []
    for i in range(0, len(vals), cols):
        chunk = ", ".join(f"0x{v:08X}U" for v in vals[i : i + cols])
        lines.append(f"    {chunk},")
    return "\n".join(lines)


CPP_RUNTIME = r"""
// ==============================================================================
// Runtime LZSS Bucket Decoder + 4x Supersampled Nonzero-Winding Scanline Engine
// ==============================================================================
namespace {

constexpr uint8_t kOpMove  = 251;
constexpr uint8_t kOpHLine = 252;
constexpr uint8_t kOpVLine = 253;
constexpr uint8_t kOpCubic = 254;
constexpr uint8_t kOpClose = 255;
constexpr uint8_t kContourEnd = 255;

constexpr std::size_t kCacheSlots = 6;
constexpr std::size_t kMaxCachedBytes = 1536;

struct DecodedMdiEntry {
  uint32_t hash = 0;
  uint32_t stamp = 0;
  uint16_t len = 0;
  uint8_t stream[kMaxCachedBytes]{};
};

struct MdiRuntimeScratch {
  DecodedMdiEntry cache[kCacheSlots]{};
  uint32_t stamp = 0;
  uint8_t bucket[kMdiMaxBucketUncompBytes]{};
};

MdiRuntimeScratch *mdi_scratch() noexcept {
#if defined(USE_ESP32) || defined(ESP_PLATFORM)
  static MdiRuntimeScratch *s_ptr = nullptr;
  if (s_ptr == nullptr) {
    void *p = heap_caps_calloc(1, sizeof(MdiRuntimeScratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
      p = heap_caps_calloc(1, sizeof(MdiRuntimeScratch), MALLOC_CAP_8BIT);
    }
    s_ptr = static_cast<MdiRuntimeScratch *>(p);
  }
  return s_ptr;
#else
  static MdiRuntimeScratch s_host{};
  return &s_host;
#endif
}

int find_mdi_icon_id(uint32_t hash) noexcept {
  if (hash == 0U) return -1;
  std::size_t lo = 0;
  std::size_t hi = kMdiIconCount;
  while (lo < hi) {
    const std::size_t mid = lo + ((hi - lo) >> 1);
    const uint32_t mh = kMdiSortedHashes[mid];
    if (mh == hash) return static_cast<int>(kMdiSortedIconIds[mid]);
    if (mh < hash) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return -1;
}

bool lzss_decode_bucket(std::size_t bucket_idx, uint8_t *dst, std::size_t dst_cap, std::size_t &out_len) noexcept {
  if (bucket_idx >= kMdiBucketCount) return false;
  const uint32_t c0 = kMdiBucketCompOffsets[bucket_idx];
  const uint32_t c1 = kMdiBucketCompOffsets[bucket_idx + 1];
  const std::size_t first_id = bucket_idx * kMdiBucketIcons;
  const std::size_t last_id = ((first_id + kMdiBucketIcons) < kMdiIconCount) ? (first_id + kMdiBucketIcons) : kMdiIconCount;
  std::size_t expected = 0;
  for (std::size_t id = first_id; id < last_id; ++id) expected += kMdiIconByteLengths[id];
  if (expected > dst_cap) return false;

  std::size_t pos = c0;
  std::size_t wr = 0;
  while (pos < c1 && wr < expected) {
    const uint8_t flags = kMdiCompBlob[pos++];
    for (int bit = 0; bit < 8 && pos < c1 && wr < expected; ++bit) {
      if ((flags >> bit) & 1U) {
        if (pos + 1 >= c1) return false;
        const uint16_t code = static_cast<uint16_t>((static_cast<uint16_t>(kMdiCompBlob[pos]) << 8) | kMdiCompBlob[pos + 1]);
        pos += 2;
        const std::size_t dist = static_cast<std::size_t>((code >> 6) & 0x3FFU) + 1U;
        const std::size_t len = static_cast<std::size_t>(code & 0x3FU) + 3U;
        if (dist > wr || (wr + len) > expected) return false;
        std::size_t src = wr - dist;
        for (std::size_t k = 0; k < len; ++k) dst[wr++] = dst[src++];
      } else {
        dst[wr++] = kMdiCompBlob[pos++];
      }
    }
  }
  out_len = wr;
  return wr == expected;
}

void append_qpt(DecodedMdiEntry &dst, uint8_t qx, uint8_t qy) noexcept {
  if (dst.len + 3 >= kMaxCachedBytes) return;
  if (dst.len >= 2 && dst.stream[dst.len - 1] != kContourEnd &&
      dst.stream[dst.len - 2] == qx && dst.stream[dst.len - 1] == qy) {
    return;
  }
  dst.stream[dst.len++] = qx;
  dst.stream[dst.len++] = qy;
}

void flatten_cubic_q(DecodedMdiEntry &dst, float x0, float y0, float x1, float y1,
                     float x2, float y2, float x3, float y3, int depth) noexcept {
  if (depth < 6) {
    const float dx = x3 - x0;
    const float dy = y3 - y0;
    const float d2 = ((x1 - x3) * dy - (y1 - y3) * dx);
    const float d3 = ((x2 - x3) * dy - (y2 - y3) * dx);
    const float ad2 = (d2 < 0.0f) ? -d2 : d2;
    const float ad3 = (d3 < 0.0f) ? -d3 : d3;
    // Coordinates are in 0.1 MDI units (0..250); tol = 0.9 units == 0.09 MDI px
    if ((ad2 + ad3) * (ad2 + ad3) > 0.81f * (dx * dx + dy * dy + 0.001f)) {
      const float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
      const float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
      const float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
      const float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
      const float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
      const float x0123 = (x012 + x123) * 0.5f, y0123 = (y012 + y123) * 0.5f;
      flatten_cubic_q(dst, x0, y0, x01, y01, x012, y012, x0123, y0123, depth + 1);
      flatten_cubic_q(dst, x0123, y0123, x123, y123, x23, y23, x3, y3, depth + 1);
      return;
    }
  }
  int ix = static_cast<int>(x3 + 0.5f);
  int iy = static_cast<int>(y3 + 0.5f);
  if (ix < 0) ix = 0;
  if (ix > 250) ix = 250;
  if (iy < 0) iy = 0;
  if (iy > 250) iy = 250;
  append_qpt(dst, static_cast<uint8_t>(ix), static_cast<uint8_t>(iy));
}

const DecodedMdiEntry *get_or_decode_mdi(uint32_t hash) noexcept {
  MdiRuntimeScratch *sc = mdi_scratch();
  if (sc == nullptr) return nullptr;
  for (auto &slot : sc->cache) {
    if (slot.hash == hash && slot.len > 0) {
      slot.stamp = ++sc->stamp;
      return &slot;
    }
  }
  const int icon_id = find_mdi_icon_id(hash);
  if (icon_id < 0) return nullptr;

  const std::size_t u_id = static_cast<std::size_t>(icon_id);
  const std::size_t bucket_idx = u_id / kMdiBucketIcons;
  std::size_t bucket_len = 0;
  if (!lzss_decode_bucket(bucket_idx, sc->bucket, sizeof(sc->bucket), bucket_len)) {
    return nullptr;
  }

  std::size_t off = 0;
  const std::size_t base_id = bucket_idx * kMdiBucketIcons;
  for (std::size_t id = base_id; id < u_id; ++id) off += kMdiIconByteLengths[id];
  const std::size_t bc_len = kMdiIconByteLengths[u_id];
  if (off + bc_len > bucket_len) return nullptr;

  std::size_t victim = 0;
  for (std::size_t i = 1; i < kCacheSlots; ++i) {
    if (sc->cache[i].stamp < sc->cache[victim].stamp) victim = i;
  }
  DecodedMdiEntry &dst = sc->cache[victim];
  dst.hash = hash;
  dst.stamp = ++sc->stamp;
  dst.len = 0;

  const uint8_t *bc = sc->bucket + off;
  std::size_t i = 0;
  uint8_t cx = 5, cy = 5, sx = 5, sy = 5;
  bool in_contour = false;

  auto close_cur = [&]() noexcept {
    if (in_contour && dst.len < kMaxCachedBytes) {
      dst.stream[dst.len++] = kContourEnd;
      in_contour = false;
    }
  };

  while (i < bc_len) {
    const uint8_t op = bc[i++];
    if (op == kOpMove) {
      if (i + 1 >= bc_len) break;
      close_cur();
      cx = sx = bc[i++];
      cy = sy = bc[i++];
      in_contour = true;
      append_qpt(dst, cx, cy);
    } else if (op == kOpHLine) {
      if (i >= bc_len) break;
      cx = bc[i++];
      append_qpt(dst, cx, cy);
    } else if (op == kOpVLine) {
      if (i >= bc_len) break;
      cy = bc[i++];
      append_qpt(dst, cx, cy);
    } else if (op == kOpCubic) {
      if (i + 5 >= bc_len) break;
      const uint8_t x1 = bc[i++], y1 = bc[i++];
      const uint8_t x2 = bc[i++], y2 = bc[i++];
      const uint8_t x3 = bc[i++], y3 = bc[i++];
      flatten_cubic_q(dst, static_cast<float>(cx), static_cast<float>(cy),
                      static_cast<float>(x1), static_cast<float>(y1),
                      static_cast<float>(x2), static_cast<float>(y2),
                      static_cast<float>(x3), static_cast<float>(y3), 0);
      cx = x3;
      cy = y3;
    } else if (op == kOpClose) {
      close_cur();
      cx = sx;
      cy = sy;
    } else {
      if (i >= bc_len) break;
      cx = op;
      cy = bc[i++];
      append_qpt(dst, cx, cy);
    }
  }
  close_cur();
  return (dst.len > 0) ? &dst : nullptr;
}

}  // namespace

std::size_t mdi_catalog_count() noexcept {
  return kMdiIconCount;
}

bool has_mdi_icon(std::string_view name) noexcept {
  return find_mdi_icon_id(mdi_icon_hash(name)) >= 0;
}

bool rasterize_mdi_icon(std::string_view name, int cx, int cy, int size_px,
                        MdiSpanEmitFn emit_fn, void *user) noexcept {
  if (emit_fn == nullptr || size_px < 4) return false;
  if (size_px > 128) size_px = 128;

  const DecodedMdiEntry *entry = get_or_decode_mdi(mdi_icon_hash(name));
  if (entry == nullptr) return false;

  const int x0_box = cx - (size_px / 2);
  const int y0_box = cy - (size_px / 2);
  // svg_coord = q * 0.1 - 0.5; pixel_coord = svg_coord * (size_px / 24.0)
  // => pixel_coord = (q - 5.0f) * (size_px / 240.0f)
  const float q_to_px = static_cast<float>(size_px) / 240.0f;
  const float px_to_q = 240.0f / static_cast<float>(size_px);

  uint16_t cov[128]{};
  struct EdgeHit {
    float x;
    int8_t dir;
  };
  EdgeHit hits[96];

  for (int py = 0; py < size_px; ++py) {
    for (int i = 0; i < size_px; ++i) cov[i] = 0;

    for (int sub = 0; sub < 4; ++sub) {
      const float y_px = static_cast<float>(py) + (static_cast<float>(sub) + 0.5f) * 0.25f;
      const float y_q = y_px * px_to_q + 5.0f;

      int hit_count = 0;
      std::size_t pos = 0;
      while (pos < entry->len) {
        const std::size_t c_start = pos;
        while (pos < entry->len && entry->stream[pos] != kContourEnd) pos += 2;
        const std::size_t pt_bytes = pos - c_start;
        if (pos < entry->len && entry->stream[pos] == kContourEnd) ++pos;
        if (pt_bytes < 6) continue;

        const std::size_t n_pts = pt_bytes >> 1;
        for (std::size_t k = 0; k < n_pts; ++k) {
          const std::size_t k_next = (k + 1 < n_pts) ? (k + 1) : 0;
          const float x0 = static_cast<float>(entry->stream[c_start + k * 2]);
          const float y0 = static_cast<float>(entry->stream[c_start + k * 2 + 1]);
          const float x1 = static_cast<float>(entry->stream[c_start + k_next * 2]);
          const float y1 = static_cast<float>(entry->stream[c_start + k_next * 2 + 1]);
          if ((y0 <= y_q && y_q < y1) || (y1 <= y_q && y_q < y0)) {
            if (hit_count < 96) {
              const float t = (y_q - y0) / (y1 - y0);
              const float x_q = x0 + t * (x1 - x0);
              hits[hit_count].x = (x_q - 5.0f) * q_to_px;
              hits[hit_count].dir = (y1 > y0) ? 1 : -1;
              ++hit_count;
            }
          }
        }
      }

      if (hit_count < 2) continue;
      for (int i = 1; i < hit_count; ++i) {
        const EdgeHit key = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].x > key.x) {
          hits[j + 1] = hits[j];
          --j;
        }
        hits[j + 1] = key;
      }

      int winding = 0;
      float prev_x = 0.0f;
      for (int i = 0; i < hit_count; ++i) {
        if (winding != 0) {
          float xs = (prev_x < 0.0f) ? 0.0f : prev_x;
          float xe = (hits[i].x > static_cast<float>(size_px)) ? static_cast<float>(size_px) : hits[i].x;
          if (xe > xs) {
            const int p0 = static_cast<int>(xs);
            const int p1 = static_cast<int>(xe);
            if (p0 == p1) {
              if (p0 >= 0 && p0 < size_px) {
                cov[p0] = static_cast<uint16_t>(cov[p0] + static_cast<int>((xe - xs) * 64.0f + 0.5f));
              }
            } else {
              if (p0 >= 0 && p0 < size_px) {
                cov[p0] = static_cast<uint16_t>(cov[p0] + static_cast<int>((static_cast<float>(p0 + 1) - xs) * 64.0f + 0.5f));
              }
              const int mid_end = (p1 < size_px) ? p1 : size_px;
              for (int px = (p0 < 0 ? 0 : p0 + 1); px < mid_end; ++px) {
                cov[px] = static_cast<uint16_t>(cov[px] + 64U);
              }
              if (p1 >= 0 && p1 < size_px && xe > static_cast<float>(p1)) {
                cov[p1] = static_cast<uint16_t>(cov[p1] + static_cast<int>((xe - static_cast<float>(p1)) * 64.0f + 0.5f));
              }
            }
          }
        }
        winding += hits[i].dir;
        prev_x = hits[i].x;
      }
    }

    int px = 0;
    while (px < size_px) {
      const uint16_t c_val = cov[px];
      if (c_val < 4U) {
        ++px;
        continue;
      }
      const uint8_t alpha = (c_val >= 252U) ? 255U : static_cast<uint8_t>(c_val);
      int run_end = px + 1;
      if (alpha == 255U) {
        while (run_end < size_px && cov[run_end] >= 252U) ++run_end;
      } else {
        while (run_end < size_px && cov[run_end] == c_val) ++run_end;
      }
      emit_fn(user, x0_box + px, x0_box + run_end, y0_box + py, alpha);
      px = run_end;
    }
  }
  return true;
}
"""


def build_from_mdi_js(src_path: Path) -> None:
    if src_path.suffix in (".tgz", ".gz"):
        with tarfile.open(src_path) as tf:
            mdi_txt = tf.extractfile("package/mdi.js").read().decode("utf-8")
            pkg_txt = tf.extractfile("package/package.json").read().decode("utf-8")
            m_ver = re.search(r'"version"\s*:\s*"([^"]+)"', pkg_txt)
            mdi_ver = m_ver.group(1) if m_ver else "7.4.47"
    else:
        mdi_txt = src_path.read_text(encoding="utf-8")
        mdi_ver = "7.4.47"

    pairs = re.findall(r'export var (mdi\w+)\s*=\s*"([^"]+)"', mdi_txt)
    if len(pairs) < 7000:
        raise RuntimeError(f"Expected >= 7000 MDI icons, found {len(pairs)}")

    # Sort alphabetically by normalized slug so shared icon prefixes compress together
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
        # Round-trip verify every single bucket at compile time!
        if lzss_decompress(comp_bucket, len(raw_bucket)) != raw_bucket:
            raise RuntimeError(f"LZSS round-trip mismatch in bucket {b_start // BUCKET_ICONS}")
        comp_blob.extend(comp_bucket)
        bucket_offsets.append(len(comp_blob))

    bucket_count = len(bucket_offsets) - 1
    sorted_hash_pairs = sorted((items[idx][1], idx) for idx in range(icon_count))
    sorted_hashes = [p[0] for p in sorted_hash_pairs]
    sorted_ids = [p[1] for p in sorted_hash_pairs]

    digest = hashlib.sha256()
    for h in sorted_hashes:
        digest.update(h.to_bytes(4, "little"))
    for i_id in sorted_ids:
        digest.update(i_id.to_bytes(2, "little"))
    for bl in byte_lengths:
        digest.update(bl.to_bytes(2, "little"))
    for bo in bucket_offsets:
        digest.update(bo.to_bytes(4, "little"))
    digest.update(comp_blob)
    sha_hex = digest.hexdigest()

    cpp_text = f"""// ==============================================================================
// GENERATED FILE — DO NOT EDIT BY HAND
// Generated by tools/gen_mdi_pack.py from Pictogrammers @mdi/js v{mdi_ver}.
// Icon Artwork License: Pictogrammers Free License / Apache-2.0
//   https://pictogrammers.com/docs/general/license/
// MDI_PACK_SHA256: {sha_hex}
// MDI_ICON_COUNT: {icon_count}
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#if __has_include("ui_mdi.h")
#include "ui_mdi.h"
#else
#include "../../include/ui_mdi.h"
#endif

#if defined(USE_ESP32) || defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace latter_ui {{
namespace {{

inline constexpr std::size_t kMdiIconCount = {icon_count}U;
inline constexpr std::size_t kMdiBucketIcons = {BUCKET_ICONS}U;
inline constexpr std::size_t kMdiBucketCount = {bucket_count}U;
inline constexpr std::size_t kMdiMaxBucketUncompBytes = {max_bucket_uncomp}U;

alignas(4) inline constexpr uint32_t kMdiSortedHashes[kMdiIconCount] = {{
{format_u32_array(sorted_hashes)}
}};

alignas(2) inline constexpr uint16_t kMdiSortedIconIds[kMdiIconCount] = {{
{format_u16_array(sorted_ids)}
}};

alignas(2) inline constexpr uint16_t kMdiIconByteLengths[kMdiIconCount] = {{
{format_u16_array(byte_lengths)}
}};

alignas(4) inline constexpr uint32_t kMdiBucketCompOffsets[kMdiBucketCount + 1] = {{
{format_u32_array([bo for bo in bucket_offsets], cols=10)}
}};

inline constexpr uint8_t kMdiCompBlob[{len(comp_blob)}] = {{
{format_u8_array(bytes(comp_blob))}
}};

}}  // namespace
{CPP_RUNTIME}
}}  // namespace latter_ui
"""
    # Format bucket_offsets as decimal u32 or hex u32 — both valid C++!
    OUT_CPP.write_text(cpp_text, encoding="utf-8", newline="\n")
    total_rodata = icon_count * 4 + icon_count * 2 + icon_count * 2 + len(bucket_offsets) * 4 + len(comp_blob)
    print(
        f"Wrote {OUT_CPP.relative_to(ROOT)}: {icon_count} MDI v{mdi_ver} icons, "
        f"{total_rodata / 1024:.1f} KB total .rodata (blob {len(comp_blob) / 1024:.1f} KB, "
        f"max bucket {max_bucket_uncomp} B)"
    )


def check_committed() -> int:
    if not OUT_CPP.is_file():
        print(f"ERROR: {OUT_CPP} missing", file=sys.stderr)
        return 1
    txt = OUT_CPP.read_text(encoding="utf-8")
    m_sha = re.search(r"MDI_PACK_SHA256:\s*([0-9a-f]{64})", txt)
    m_cnt = re.search(r"MDI_ICON_COUNT:\s*(\d+)", txt)
    if not (m_sha and m_cnt):
        print("ERROR: Missing MDI_PACK_SHA256 / MDI_ICON_COUNT header in ui_mdi.cpp", file=sys.stderr)
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
        out = []
        for tok in m.group(1).replace("\n", " ").split(","):
            tok = tok.strip().rstrip("Uu")
            if tok:
                out.append(int(tok, 0))
        return out

    hashes = extract_array("kMdiSortedHashes")
    ids = extract_array("kMdiSortedIconIds")
    lengths = extract_array("kMdiIconByteLengths")
    offsets = extract_array("kMdiBucketCompOffsets")
    blob = bytes(extract_array("kMdiCompBlob"))

    if len(hashes) != icon_count or len(ids) != icon_count or len(lengths) != icon_count:
        print("ERROR: Array length mismatch in ui_mdi.cpp", file=sys.stderr)
        return 1
    if any(hashes[i] >= hashes[i + 1] for i in range(icon_count - 1)):
        print("ERROR: kMdiSortedHashes is not strictly ascending", file=sys.stderr)
        return 1

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
    if digest.hexdigest() != expected_sha:
        print("ERROR: SHA-256 mismatch in ui_mdi.cpp (hand-edited?)", file=sys.stderr)
        return 1

    # Decompress every bucket and validate every icon's bytecode stream
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
        print("ERROR: CPP_RUNTIME mismatch in ui_mdi.cpp (run --sync-runtime)", file=sys.stderr)
        return 1
    return 0


def sync_runtime() -> None:
    if not OUT_CPP.is_file():
        raise RuntimeError(f"{OUT_CPP} missing")
    txt = OUT_CPP.read_text(encoding="utf-8")
    def_guard = '#if __has_include("esphome/core/defines.h")\n#include "esphome/core/defines.h"\n#endif\n\n'
    if 'esphome/core/defines.h' not in txt:
        txt = txt.replace('#if __has_include("ui_mdi.h")', def_guard + '#if __has_include("ui_mdi.h")', 1)
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
    marker = "// ==============================================================================\n// Runtime LZSS Bucket Decoder"
    idx = txt.find(marker)
    if idx < 0:
        raise RuntimeError("Could not locate runtime marker in ui_mdi.cpp")
    prefix = txt[:idx].rstrip("\n")
    updated = f"{prefix}\n{CPP_RUNTIME}\n}}  // namespace latter_ui\n"
    OUT_CPP.write_text(updated, encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--from-mdi-js", type=Path, help="Path to @mdi/js mdi.js or npm tarball")
    ap.add_argument("--sync-runtime", action="store_true", help="Sync C++ runtime template into committed ui_mdi.cpp")
    ap.add_argument("--check", action="store_true", help="Verify committed ui_mdi.cpp integrity")
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
