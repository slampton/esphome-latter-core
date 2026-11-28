"""LZSS bucket codec, C++ array formatters, and C++20 scanline runtime."""

from __future__ import annotations

from .glyph_raster import BUCKET_ICONS


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


def format_u8_array(data: bytes, cols: int = 24) -> str:
    return "\n".join(
        f"    {', '.join(f'{b}' for b in data[i : i + cols])},"
        for i in range(0, len(data), cols)
    )


def format_u16_array(vals: list[int], cols: int = 16) -> str:
    return "\n".join(
        f"    {', '.join(f'{v}U' for v in vals[i : i + cols])},"
        for i in range(0, len(vals), cols)
    )


def format_u32_array(vals: list[int], cols: int = 10) -> str:
    return "\n".join(
        f"    {', '.join(f'0x{v:08X}U' for v in vals[i : i + cols])},"
        for i in range(0, len(vals), cols)
    )


CPP_RUNTIME = (
    r"""
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

#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32P4)
constexpr std::size_t kCacheSlots = 16;
#else
constexpr std::size_t kCacheSlots = 6;
#endif
constexpr std::size_t kMaxCachedBytes = 1536;

struct DecodedMdiEntry {
  uint32_t hash = 0;
  uint32_t stamp = 0;
  uint16_t len = 0;
  uint8_t stream[kMaxCachedBytes]{};
};

struct MdiEdgeHit {
  float x;
  int8_t dir;
};

struct MdiRuntimeScratch {
  DecodedMdiEntry cache[kCacheSlots]{};
  uint32_t stamp = 0;
  uint8_t bucket[kMdiMaxBucketUncompBytes]{};
  uint16_t cov[256]{};
  MdiEdgeHit hits[96]{};
};

#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32P4)
constexpr std::size_t kRasterCacheSlots = 64;
constexpr std::size_t kMaxCachedSpans = 2048;
#else
constexpr std::size_t kRasterCacheSlots = 24;
constexpr std::size_t kMaxCachedSpans = 1024;
#endif

struct MdiRasterCache {
  uint32_t hashes[kRasterCacheSlots]{};
  uint32_t stamps[kRasterCacheSlots]{};
  uint16_t sizes[kRasterCacheSlots]{};
  uint16_t counts[kRasterCacheSlots]{};
  uint32_t stamp = 0;
  MdiSpan spans[kRasterCacheSlots][kMaxCachedSpans]{};
};

MdiRuntimeScratch *mdi_scratch() noexcept {
#if defined(USE_ESP32) || defined(ESP_PLATFORM)
  static MdiRuntimeScratch *s_ptr = nullptr;
  if (s_ptr == nullptr) {
"""
    "    void *p = heap_caps_calloc("
    "1, sizeof(MdiRuntimeScratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n"
    r"""    if (p == nullptr) {
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

// PSRAM-only span cache: avoids re-running 4x scanline rasterization for shadow+fg
// and across frames without consuming internal RAM if PSRAM is unavailable.
#if defined(USE_ESP32) || defined(ESP_PLATFORM)
MdiRasterCache *&mdi_raster_cache_ptr() noexcept {
  static MdiRasterCache *s_ptr = nullptr;
  return s_ptr;
}
#else
MdiRasterCache &mdi_raster_cache_host() noexcept {
  static MdiRasterCache s_host{};
  return s_host;
}
#endif

MdiRasterCache *mdi_raster_cache() noexcept {
#if defined(USE_ESP32) || defined(ESP_PLATFORM)
  MdiRasterCache *&s_ptr = mdi_raster_cache_ptr();
  if (s_ptr == nullptr) {
"""
    "    void *p = heap_caps_calloc("
    "1, sizeof(MdiRasterCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n"
    r"""    s_ptr = static_cast<MdiRasterCache *>(p);
  }
  return s_ptr;
#else
  return &mdi_raster_cache_host();
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

"""
    "bool lzss_decode_bucket(std::size_t bucket_idx, uint8_t *dst, "
    "std::size_t dst_cap, std::size_t &out_len) noexcept {\n"
    r"""  if (bucket_idx >= kMdiBucketCount) return false;
  const uint32_t c0 = kMdiBucketCompOffsets[bucket_idx];
  const uint32_t c1 = kMdiBucketCompOffsets[bucket_idx + 1];
  const std::size_t first_id = bucket_idx * kMdiBucketIcons;
"""
    "  const std::size_t last_id = "
    "((first_id + kMdiBucketIcons) < kMdiIconCount)"
    " ? (first_id + kMdiBucketIcons) : kMdiIconCount;\n"
    "  std::size_t expected = 0;\n"
    "  for (std::size_t id = first_id; id < last_id; ++id)"
    " expected += kMdiIconByteLengths[id];\n"
    r"""  if (expected > dst_cap) return false;

  std::size_t pos = c0;
  std::size_t wr = 0;
  while (pos < c1 && wr < expected) {
    const uint8_t flags = kMdiCompBlob[pos++];
    for (int bit = 0; bit < 8 && pos < c1 && wr < expected; ++bit) {
      if ((flags >> bit) & 1U) {
        if (pos + 1 >= c1) return false;
"""
    "        const uint16_t code = static_cast<uint16_t>("
    "(static_cast<uint16_t>(kMdiCompBlob[pos]) << 8)"
    " | kMdiCompBlob[pos + 1]);\n"
    r"""        pos += 2;
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
  if (static_cast<std::size_t>(dst.len) + 3U >= kMaxCachedBytes) return;
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
    if (in_contour && static_cast<std::size_t>(dst.len) < kMaxCachedBytes) {
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

void mdi_raster_cache_drop() noexcept {
#if defined(USE_ESP32) || defined(ESP_PLATFORM)
  MdiRasterCache *&s_ptr = mdi_raster_cache_ptr();
  if (s_ptr != nullptr) {
    heap_caps_free(s_ptr);
    s_ptr = nullptr;
  }
#else
  auto &rc = mdi_raster_cache_host();
  for (std::size_t i = 0; i < kRasterCacheSlots; ++i) {
    rc.hashes[i] = 0;
    rc.stamps[i] = 0;
    rc.sizes[i] = 0;
    rc.counts[i] = 0;
  }
  rc.stamp = 0;
#endif
}

namespace {

int rasterize_mdi_hash_internal(uint32_t hash, int x0_box, int y0_box, int size_px,
                                MdiSpanEmitFn emit_fn, void *user, bool &out_ok) noexcept {
  out_ok = false;
  if (hash == 0U || size_px < 4) return -1;
  if (size_px > 255) size_px = 255;

  MdiRasterCache *rc = mdi_raster_cache();
  const uint16_t sz16 = static_cast<uint16_t>(size_px);
  if (rc != nullptr) {
    for (std::size_t i = 0; i < kRasterCacheSlots; ++i) {
      if (rc->hashes[i] == hash && rc->sizes[i] == sz16) {
        rc->stamps[i] = ++rc->stamp;
        if (emit_fn != nullptr) {
          const uint16_t cnt = rc->counts[i];
          const MdiSpan *sp = rc->spans[i];
          for (uint16_t k = 0; k < cnt; ++k) {
            const MdiSpan &s = sp[k];
"""
    "            emit_fn(user, x0_box + static_cast<int>(s.x0),"
    " x0_box + static_cast<int>(s.x1),\n"
    r"""                    y0_box + static_cast<int>(s.y), s.alpha);
          }
        }
        out_ok = true;
        return static_cast<int>(i);
      }
    }
  }

  const DecodedMdiEntry *entry = get_or_decode_mdi(hash);
  if (entry == nullptr) return -1;

  int victim_idx = -1;
  MdiSpan *dst_spans = nullptr;
  uint16_t span_cnt = 0;
  bool cache_overflow = false;
  if (rc != nullptr) {
    std::size_t victim = 0;
    for (std::size_t i = 1; i < kRasterCacheSlots; ++i) {
      if (rc->stamps[i] < rc->stamps[victim]) victim = i;
    }
    victim_idx = static_cast<int>(victim);
    rc->hashes[victim] = 0;
    rc->sizes[victim] = 0;
    rc->counts[victim] = 0;
    dst_spans = rc->spans[victim];
  }

  // svg_coord = q * 0.1 - 0.5; pixel_coord = svg_coord * (size_px / 24.0)
  // => pixel_coord = (q - 5.0f) * (size_px / 240.0f)
  const float q_to_px = static_cast<float>(size_px) / 240.0f;
  const float px_to_q = 240.0f / static_cast<float>(size_px);

  MdiRuntimeScratch *sc = mdi_scratch();
  if (sc == nullptr) return -1;
  uint16_t *const cov = sc->cov;
  MdiEdgeHit *const hits = sc->hits;

  for (int py = 0; py < size_px; ++py) {
    for (int i = 0; i < size_px; ++i) cov[i] = 0;

    for (int sub = 0; sub < 4; ++sub) {
"""
    "      const float y_px = static_cast<float>(py) +"
    " (static_cast<float>(sub) + 0.5f) * 0.25f;\n"
    r"""      const float y_q = y_px * px_to_q + 5.0f;

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
        const MdiEdgeHit key = hits[i];
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
"""
    "          float xe = (hits[i].x > static_cast<float>(size_px))"
    " ? static_cast<float>(size_px) : hits[i].x;\n"
    r"""          if (xe > xs) {
            const int p0 = static_cast<int>(xs);
            const int p1 = static_cast<int>(xe);
            if (p0 == p1) {
              if (p0 >= 0 && p0 < size_px) {
"""
    "                cov[p0] = static_cast<uint16_t>("
    "cov[p0] + static_cast<int>((xe - xs) * 64.0f + 0.5f));\n"
    r"""              }
            } else {
              if (p0 >= 0 && p0 < size_px) {
"""
    "                cov[p0] = static_cast<uint16_t>(cov[p0] +"
    " static_cast<int>((static_cast<float>(p0 + 1) - xs) * 64.0f + 0.5f));\n"
    r"""              }
              const int mid_end = (p1 < size_px) ? p1 : size_px;
              for (int px = (p0 < 0 ? 0 : p0 + 1); px < mid_end; ++px) {
                cov[px] = static_cast<uint16_t>(cov[px] + 64U);
              }
              if (p1 >= 0 && p1 < size_px && xe > static_cast<float>(p1)) {
"""
    "                cov[p1] = static_cast<uint16_t>(cov[p1] +"
    " static_cast<int>((xe - static_cast<float>(p1)) * 64.0f + 0.5f));\n"
    r"""              }
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
      if (emit_fn != nullptr) {
        emit_fn(user, x0_box + px, x0_box + run_end, y0_box + py, alpha);
      }
      if (dst_spans != nullptr && !cache_overflow) {
        if (static_cast<std::size_t>(span_cnt) < kMaxCachedSpans) {
          dst_spans[span_cnt++] = MdiSpan{
              static_cast<uint8_t>(py),
              static_cast<uint8_t>(px),
              static_cast<uint8_t>(run_end),
              alpha,
          };
        } else {
          cache_overflow = true;
        }
      }
      px = run_end;
    }
  }
  out_ok = true;
  if (rc != nullptr && victim_idx >= 0 && !cache_overflow) {
    rc->hashes[victim_idx] = hash;
    rc->sizes[victim_idx] = sz16;
    rc->counts[victim_idx] = span_cnt;
    rc->stamps[victim_idx] = ++rc->stamp;
    return victim_idx;
  }
  return -1;
}

}  // namespace

bool rasterize_mdi_icon(std::string_view name, int cx, int cy, int size_px,
                        MdiSpanEmitFn emit_fn, void *user) noexcept {
  if (emit_fn == nullptr || size_px < 4) return false;
  if (size_px > 255) size_px = 255;

  const uint32_t hash = mdi_icon_hash(name);
  if (hash == 0U) return false;

  const int x0_box = cx - (size_px / 2);
  const int y0_box = cy - (size_px / 2);
  bool ok = false;
  (void)rasterize_mdi_hash_internal(hash, x0_box, y0_box, size_px, emit_fn, user, ok);
  return ok;
}

MdiSpanListView get_mdi_raster_spans(std::string_view name, int size_px) noexcept {
  if (size_px < 4) return {};
  if (size_px > 255) size_px = 255;

  const uint32_t hash = mdi_icon_hash(name);
  if (hash == 0U) return {};

  bool ok = false;
  const int slot = rasterize_mdi_hash_internal(hash, 0, 0, size_px, nullptr, nullptr, ok);
  if (!ok || slot < 0) return {};
  MdiRasterCache *rc = mdi_raster_cache();
  return (rc != nullptr)
      ? MdiSpanListView{rc->spans[slot], rc->counts[slot]}
      : MdiSpanListView{};
}
"""
)


def render_mdi_pack_cpp(
    *,
    mdi_ver: str,
    sha_hex: str,
    icon_count: int,
    bucket_count: int,
    max_bucket_uncomp: int,
    sorted_hashes: list[int],
    sorted_ids: list[int],
    byte_lengths: list[int],
    bucket_offsets: list[int],
    comp_blob: bytes,
) -> str:
    cpp_banner = "// " + "=" * 78
    return f"""{cpp_banner}
// GENERATED FILE — DO NOT EDIT BY HAND
// Generated by tools/gen_mdi_pack.py from Pictogrammers @mdi/js v{mdi_ver}.
// Icon Artwork License: Pictogrammers Free License / Apache-2.0
//   https://pictogrammers.com/docs/general/license/
// MDI_PACK_SHA256: {sha_hex}
// MDI_ICON_COUNT: {icon_count}
{cpp_banner}

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
{format_u32_array(bucket_offsets, cols=10)}
}};

inline constexpr uint8_t kMdiCompBlob[{len(comp_blob)}] = {{
{format_u8_array(comp_blob)}
}};

}}  // namespace
{CPP_RUNTIME}
}}  // namespace latter_ui
"""
