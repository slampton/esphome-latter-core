#include "ui_gfx.h"
#include "ui_dirty_band.h"
#include "ui_mdi.h"
#include "state/state_prefs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif
// Hardware framebuffer acceleration is restricted to ESP32-P4, whose 2D PPA Fill/SRM/Blend engines
// stream AXI PSRAM via DMA2D without Flash XIP stalls or GDMA IRAM-safe conflicts. On Octal-PSRAM XIP
// chips without PPA (ESP32-S3), PSRAM-to-PSRAM GDMA + cache msync contends directly with instruction
// fetches and PSRAM wake-word inference.
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_FB_PPA 1
#include <driver/ppa.h>
#include <esp_attr.h>
#include <esp_cache.h>
#include <esp_rom_sys.h>
#else
#define LATTER_FB_PPA 0
#endif

namespace latter_ui {
using namespace latter_core;

namespace {
#if LATTER_FB_PPA
#if defined(CONFIG_CACHE_L2_CACHE_LINE_128B)
constexpr size_t kPsramCacheLineAlign = 128U;
#else
constexpr size_t kPsramCacheLineAlign = 64U;
#endif
inline constexpr size_t align_up_psram(size_t bytes) noexcept {
  return (bytes + (kPsramCacheLineAlign - 1U)) & ~(kPsramCacheLineAlign - 1U);
}
#endif
}  // namespace

// 1. Retryable PSRAM / Host Heap Helpers
void *ui_psram_malloc(size_t bytes) noexcept {
  if (bytes == 0U) return nullptr;
#ifdef USE_ESP32
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
  return std::malloc(bytes);
#endif
}

void *ui_psram_calloc(size_t count, size_t elem_size) noexcept {
  if (count == 0U || elem_size == 0U) return nullptr;
#ifdef USE_ESP32
  return heap_caps_calloc(count, elem_size, MALLOC_CAP_SPIRAM);
#else
  return std::calloc(count, elem_size);
#endif
}

void *ui_psram_aligned_alloc(size_t bytes) noexcept {
  if (bytes == 0U) return nullptr;
#if LATTER_FB_PPA
  const size_t aligned = align_up_psram(bytes);
  return heap_caps_aligned_alloc(kPsramCacheLineAlign, aligned, MALLOC_CAP_SPIRAM);
#else
  return ui_psram_malloc(bytes);
#endif
}

void *ui_psram_aligned_calloc(size_t count, size_t elem_size) noexcept {
  if (count == 0U || elem_size == 0U) return nullptr;
#if LATTER_FB_PPA
  if (count > (SIZE_MAX / elem_size)) return nullptr;
  const size_t aligned = align_up_psram(count * elem_size);
  void *p = heap_caps_aligned_alloc(kPsramCacheLineAlign, aligned, MALLOC_CAP_SPIRAM);
  if (p != nullptr) std::memset(p, 0, aligned);
  return p;
#else
  return ui_psram_calloc(count, elem_size);
#endif
}

PsramPhaseBuf ui_psram_phase_alloc(size_t bytes, const void *match_addr, bool zero_init) noexcept {
  PsramPhaseBuf out{};
  if (bytes == 0U) return out;
#if LATTER_FB_PPA
  const size_t phase =
      (reinterpret_cast<uintptr_t>(match_addr) & (kPsramCacheLineAlign - 1U)) & ~static_cast<size_t>(1U);
  const size_t total = align_up_psram(phase + bytes);
  void *raw = heap_caps_aligned_alloc(kPsramCacheLineAlign, total, MALLOC_CAP_SPIRAM);
  if (raw == nullptr) return out;
  if (zero_init) std::memset(raw, 0, total);
  out.raw = raw;
  out.px = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(raw) + phase);
  return out;
#else
  (void)match_addr;
  void *raw = zero_init ? ui_psram_calloc(1U, bytes) : ui_psram_malloc(bytes);
  out.raw = raw;
  out.px = static_cast<uint16_t *>(raw);
  return out;
#endif
}

void ui_psram_phase_free(PsramPhaseBuf &buf) noexcept {
  void *const p = (buf.raw != nullptr) ? buf.raw : static_cast<void *>(buf.px);
  if (p != nullptr) ui_psram_free(p);
  buf.raw = nullptr;
  buf.px = nullptr;
}

void ui_psram_cache_writeback(const void *ptr, size_t bytes) noexcept {
#if LATTER_FB_PPA
  if (ptr == nullptr || bytes == 0U) return;
  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t lo = addr & ~kMask;
  const uintptr_t hi = (addr + bytes + kMask) & ~kMask;
  if (hi > lo) {
    (void)esp_cache_msync(reinterpret_cast<void *>(lo), static_cast<size_t>(hi - lo),
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
#else
  (void)ptr;
  (void)bytes;
#endif
}

void ui_psram_free(void *ptr) noexcept {
  if (ptr == nullptr) return;
#ifdef USE_ESP32
  heap_caps_free(ptr);
#else
  std::free(ptr);
#endif
}

// 2. Between-Frames Async PPA Framebuffer Clear, PPA SRM Band Blit & PPA Blend Wash
namespace {

struct FbClearState {
  void *owner = nullptr;
  PostFrameRequestFn post_frame_hook = nullptr;
  bool post_stamp_pending = false;
#if LATTER_FB_PPA
  uint16_t *fb = nullptr;
  bool init_tried = false, ok = false;
  bool pending_start = false, active = false, ready = false, error = false;
  uint16_t color_hw = 0;
  uint8_t *interior = nullptr;
  size_t interior_len = 0;
  uint32_t hits = 0, misses = 0;
  const char *why = nullptr;
  int why_err = 0;
  ppa_client_handle_t ppa = nullptr;
  bool ppa_checked = false;
  int64_t ppa_t0_us = 0;
#endif
};

FbClearState &fb_clear_state() {
  static FbClearState st;
  return st;
}

#if LATTER_FB_PPA
void fb_clear_fail(const char *why, int err = 0) {
  auto &st = fb_clear_state();
  if (st.why != why || st.why_err != err) ESP_LOGW("disp_fb", "PPA clear skipped: %s (err=%d)", why, err);
  st.why = why;
  st.why_err = err;
}

const char *fb_clear_why_err_str() {
  static char buf[16];
  const int err = fb_clear_state().why_err;
  if (err == 0) return "";
  std::snprintf(buf, sizeof(buf), "(%d)", err);
  return buf;
}

std::atomic<int64_t> g_fb_clear_done_us{0};
std::atomic<int> g_fb_clear_pending{0};

constexpr uint32_t kFbPpaRowPx = 128;
constexpr size_t kFbPpaRowBytes = kFbPpaRowPx * sizeof(uint16_t);
constexpr size_t kFbPpaMaxRows = 16383;

bool IRAM_ATTR fb_clear_ppa_done(ppa_client_handle_t, ppa_event_data_t *, void *) {
  g_fb_clear_done_us.store(esp_timer_get_time(), std::memory_order_release);
  g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
  return false;
}

bool fb_clear_init() {
  auto &st = fb_clear_state();
  if (st.init_tried) return st.ok;
  st.init_tried = true;
  const int endian = active_frame_endian_mode();
  if (endian != 1 && endian != 3) {
    fb_clear_fail("endian", endian);
    return false;
  }
  ppa_client_config_t pcfg = {};
  pcfg.oper_type = PPA_OPERATION_FILL;
  pcfg.max_pending_trans_num = 1;
  esp_err_t perr = ppa_register_client(&pcfg, &st.ppa);
  if (perr == ESP_OK) {
    ppa_event_callbacks_t cbs = {};
    cbs.on_trans_done = fb_clear_ppa_done;
    perr = ppa_client_register_event_callbacks(st.ppa, &cbs);
  }
  if (perr == ESP_OK) {
    st.ok = true;
    ESP_LOGD("disp_fb", "Between-frames clear enabled: PPA fill (%u px rows)", static_cast<unsigned>(kFbPpaRowPx));
    return true;
  }
  fb_clear_fail("ppa_register", static_cast<int>(perr));
  return false;
}

void fb_clear_start() {
  auto &st = fb_clear_state();
  st.pending_start = false;
  if (st.active) return;
  if (st.fb == nullptr) { fb_clear_fail("no_fb"); return; }
  if (!fb_clear_init()) return;
  const uintptr_t base = reinterpret_cast<uintptr_t>(st.fb);
  const size_t bytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
  const uintptr_t lo = (base + kFbPpaRowBytes - 1) & ~(static_cast<uintptr_t>(kFbPpaRowBytes) - 1);
  const uintptr_t hi = (base + bytes) & ~(static_cast<uintptr_t>(kFbPpaRowBytes) - 1);
  if (hi <= lo) { fb_clear_fail("range"); return; }
  st.interior = reinterpret_cast<uint8_t *>(lo);
  st.interior_len = hi - lo;
  st.color_hw = frame_base_state().color_hw;
  const size_t rows = st.interior_len / kFbPpaRowBytes;
  if (rows > kFbPpaMaxRows) { fb_clear_fail("ppa_rows", static_cast<int>(rows)); return; }
  ppa_fill_oper_config_t cfg = {};
  cfg.out.buffer = st.interior;
  cfg.out.buffer_size = static_cast<uint32_t>(st.interior_len);
  cfg.out.pic_w = kFbPpaRowPx;
  cfg.out.pic_h = static_cast<uint32_t>(rows);
  cfg.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
  cfg.fill_block_w = kFbPpaRowPx;
  cfg.fill_block_h = static_cast<uint32_t>(rows);
  const uint32_t hw = st.color_hw;
  cfg.fill_argb_color.val = 0xFF000000U | ((((hw >> 11) & 0x1FU) << 3) << 16) |
                            ((((hw >> 5) & 0x3FU) << 2) << 8) | ((hw & 0x1FU) << 3);
  cfg.mode = PPA_TRANS_MODE_NON_BLOCKING;
  st.error = false;
  st.ready = false;
  st.active = true;
  g_fb_clear_pending.store(1, std::memory_order_release);
  st.ppa_t0_us = esp_timer_get_time();
  const esp_err_t ppa_err = ppa_do_fill(st.ppa, &cfg);
  if (ppa_err != ESP_OK) {
    g_fb_clear_pending.store(0, std::memory_order_release);
    st.error = true;
    fb_clear_fail("ppa_fill", static_cast<int>(ppa_err));
  }
}
#endif

}  // namespace

void set_fb_post_frame_hook(PostFrameRequestFn hook) noexcept { fb_clear_state().post_frame_hook = hook; }

void fb_clear_bind_raw(void *owner, uint16_t *fb) noexcept {
  auto &st = fb_clear_state();
  st.owner = owner;
#if LATTER_FB_PPA
  st.fb = fb;
#else
  (void)fb;
#endif
}

void fb_clear_after_update() {
  auto &st = fb_clear_state();
  if (st.post_stamp_pending) {
    st.post_stamp_pending = false;
    last_disp_update_end_ms() = hal_millis_now();
  }
#if LATTER_FB_PPA
  if (st.pending_start) fb_clear_start();
#endif
}

void fb_clear_wait() {
#if LATTER_FB_PPA
  auto &st = fb_clear_state();
  if (!st.active || st.ready) return;
  const int64_t t0 = esp_timer_get_time();
  while (g_fb_clear_pending.load(std::memory_order_acquire) > 0) {
    if (esp_timer_get_time() - t0 > 200000) {
      st.ok = false;
      fb_clear_fail("timeout");
      break;
    }
    esp_rom_delay_us(20);
  }
  esp_cache_msync(st.interior, st.interior_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  st.ready = !st.error && st.ok;
  if (!st.ready) st.active = false;
  if (st.ready && !st.ppa_checked) {
    st.ppa_checked = true;
    const uint16_t *px = reinterpret_cast<const uint16_t *>(st.interior);
    const size_t n = st.interior_len / sizeof(uint16_t);
    for (size_t i = 0; i < 8; i++) {
      const size_t idx = (i * (n - 1)) / 7;
      if (px[idx] != st.color_hw) {
        ESP_LOGW("disp_fb", "PPA fill read-back mismatch at %u: expected 0x%04X, got 0x%04X; PPA clear off",
                 static_cast<unsigned>(idx), static_cast<unsigned>(st.color_hw), static_cast<unsigned>(px[idx]));
        st.ok = false;
        st.ready = false;
        st.active = false;
        fb_clear_fail("ppa_verify", px[idx]);
        return;
      }
    }
    ESP_LOGD("disp_fb", "PPA fill verified: %u KB in %ld us", static_cast<unsigned>(st.interior_len / 1024),
             static_cast<long>(g_fb_clear_done_us.load(std::memory_order_acquire) - st.ppa_t0_us));
  }
#endif
}

bool fb_clear_consume(uint16_t hw) {
#if LATTER_FB_PPA
  auto &st = fb_clear_state();
  if (!st.ready) return false;
  st.ready = false;
  st.active = false;
  if (hw != st.color_hw || st.fb == nullptr) {
    st.misses++;
    return false;
  }
  uint16_t *const fb = st.fb;
  uint16_t *const end = fb + static_cast<size_t>(SCREEN_W) * SCREEN_H;
  for (uint16_t *q = fb; q < reinterpret_cast<uint16_t *>(st.interior); q++) *q = hw;
  for (uint16_t *q = reinterpret_cast<uint16_t *>(st.interior + st.interior_len); q < end; q++) *q = hw;
  st.hits++;
  st.why = nullptr;
  return true;
#else
  (void)hw;
  return false;
#endif
}

void fb_clear_frame_end() {
#if LATTER_FB_PPA
  auto &st = fb_clear_state();
  if (st.ready) { st.ready = false; st.active = false; }
  if (frame_base_state().has_color && (st.owner == nullptr || st.fb == nullptr)) fb_clear_fail("no_fb");
  if (!frame_base_state().has_color || st.owner == nullptr || st.fb == nullptr || st.pending_start) return;
  if (st.init_tried && !st.ok) return;
  st.pending_start = true;
#endif
}

namespace {

#if LATTER_FB_PPA
constexpr uint32_t kPpaCopyRowPx = 256U;
constexpr size_t kPpaCopyRowBytes = kPpaCopyRowPx * sizeof(uint16_t);
constexpr size_t kPpaSrmBufAlign = 256U;

struct PpaSrmBlitState {
  bool init_tried = false;
  bool ok = false;
  bool verified = false;
  ppa_client_handle_t ppa = nullptr;
  uint32_t blits = 0U;
};

PpaSrmBlitState &ppa_srm_blit_state() {
  static PpaSrmBlitState st{};
  return st;
}

bool ppa_srm_blit_init() noexcept {
  auto &st = ppa_srm_blit_state();
  if (st.init_tried) return st.ok;
  st.init_tried = true;
  ppa_client_config_t pcfg = {};
  pcfg.oper_type = PPA_OPERATION_SRM;
  pcfg.max_pending_trans_num = 1;
  const esp_err_t err = ppa_register_client(&pcfg, &st.ppa);
  st.ok = (err == ESP_OK && st.ppa != nullptr);
  if (!st.ok) {
    ESP_LOGW("disp_fb", "PSRAM PPA SRM band blit unavailable (err=%d); using CPU memcpy",
             static_cast<int>(err));
  }
  return st.ok;
}

struct PpaBlendWashState {
  bool init_tried = false;
  bool ok = false;
  bool verified = false;
  bool evictor_registered = false;
  ppa_client_handle_t ppa = nullptr;
  uint8_t *alpha_mask = nullptr;
  int cached_falloff_drop = -1;
  uint32_t washes = 0U;
};

PpaBlendWashState &ppa_blend_wash_state() {
  static PpaBlendWashState st{};
  return st;
}

void ppa_blend_wash_evict() noexcept {
  auto &st = ppa_blend_wash_state();
  if (st.alpha_mask != nullptr) {
    ui_psram_free(st.alpha_mask);
    st.alpha_mask = nullptr;
    st.cached_falloff_drop = -1;
  }
}

bool ppa_blend_wash_init() noexcept {
  auto &st = ppa_blend_wash_state();
  if (st.init_tried) return st.ok;
  st.init_tried = true;
  ppa_client_config_t pcfg = {};
  pcfg.oper_type = PPA_OPERATION_BLEND;
  pcfg.max_pending_trans_num = 1;
  const esp_err_t err = ppa_register_client(&pcfg, &st.ppa);
  st.ok = (err == ESP_OK && st.ppa != nullptr);
  if (!st.ok) {
    ESP_LOGW("disp_fb", "PSRAM PPA Blend wash unavailable (err=%d); using CPU blend",
             static_cast<int>(err));
  }
  return st.ok;
}

uint8_t *ppa_blend_wash_ensure_mask(int falloff_drop) noexcept {
  auto &st = ppa_blend_wash_state();
  const size_t mask_bytes = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
  if (st.alpha_mask == nullptr) {
    st.alpha_mask = static_cast<uint8_t *>(ui_psram_aligned_alloc(mask_bytes));
    if (st.alpha_mask == nullptr) return nullptr;
    st.cached_falloff_drop = -1;
    if (!st.evictor_registered) {
      st.evictor_registered = register_psram_cache_evictor(&ppa_blend_wash_evict);
    }
  }
  if (st.cached_falloff_drop != falloff_drop) {
    const int span_h = std::max(1, SCREEN_H - 1);
    for (int py = 0; py < SCREEN_H; ++py) {
      const int val = std::clamp(255 - ((py * falloff_drop) / span_h), 0, 255);
      std::memset(st.alpha_mask + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W), val,
                  static_cast<size_t>(SCREEN_W));
    }
    ui_psram_cache_writeback(st.alpha_mask, mask_bytes);
    st.cached_falloff_drop = falloff_drop;
  }
  return st.alpha_mask;
}
#endif

// Bit-exact 8-bit Q8 RGB565 blend using 2-lane SWAR (R5+B5 packed with 3 guard bits each,
// G6 pre-shifted by 5) — mathematically identical to 3-channel scalar Q8 blending with zero
// clamping branches, zero per-pixel array lookups, and 32-bit aligned PSRAM pair bursts.
template <int Mode>
inline uint16_t blend_swar_q8_px(uint16_t hw, uint32_t inv, uint32_t base_rb, uint32_t base_g_s5) noexcept {
  const uint16_t p = decode_hw_rgb565_mode<Mode>(hw);
  const uint32_t rb = (static_cast<uint32_t>(p & 0xF800U) << 5) | static_cast<uint32_t>(p & 0x001FU);
  const uint32_t rb_sum = base_rb + rb * inv;
  const uint32_t g_sum = base_g_s5 + static_cast<uint32_t>(p & 0x07E0U) * inv;
  const uint16_t out = static_cast<uint16_t>(((rb_sum >> 13) & 0xF800U) |
                                             ((g_sum >> 8) & 0x07E0U) |
                                             ((rb_sum >> 8) & 0x001FU));
  return encode_hw_rgb565_mode<Mode>(out);
}

template <int Mode>
inline void blend_swar_q8_span(uint16_t *dst_row, const uint16_t *src_row, int x0, int x1,
                               uint32_t inv, const uint32_t base_rb[4],
                               const uint32_t base_g_s5[4]) noexcept {
  int px = x0;
  if (px <= x1 && (reinterpret_cast<uintptr_t>(dst_row + px) & 2U) != 0U) {
    const int k = px & 3;
    dst_row[px] = blend_swar_q8_px<Mode>(src_row[px], inv, base_rb[k], base_g_s5[k]);
    ++px;
  }
  const int phase = px & 3;
  const uint32_t rb0 = base_rb[phase],           rb1 = base_rb[(phase + 1) & 3];
  const uint32_t rb2 = base_rb[(phase + 2) & 3], rb3 = base_rb[(phase + 3) & 3];
  const uint32_t g0  = base_g_s5[phase],         g1  = base_g_s5[(phase + 1) & 3];
  const uint32_t g2  = base_g_s5[(phase + 2) & 3], g3 = base_g_s5[(phase + 3) & 3];
  const bool src_aligned4 = ((reinterpret_cast<uintptr_t>(src_row + px) & 2U) == 0U);
  const int x_end4 = x1 - 3;
  if (src_aligned4) {
    auto *dst32 = std::assume_aligned<4>(reinterpret_cast<uint8_t *>(dst_row + px));
    const auto *src32 = std::assume_aligned<4>(reinterpret_cast<const uint8_t *>(src_row + px));
    for (; px <= x_end4; px += 4, dst32 += 8, src32 += 8) {
      uint32_t in01 = 0U, in23 = 0U;
      std::memcpy(&in01, src32, 4U);
      std::memcpy(&in23, src32 + 4, 4U);
      const uint32_t out0 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in01), inv, rb0, g0);
      const uint32_t out1 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in01 >> 16), inv, rb1, g1);
      const uint32_t out2 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in23), inv, rb2, g2);
      const uint32_t out3 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in23 >> 16), inv, rb3, g3);
      const uint32_t p01 = out0 | (out1 << 16);
      const uint32_t p23 = out2 | (out3 << 16);
      std::memcpy(dst32, &p01, 4U);
      std::memcpy(dst32 + 4, &p23, 4U);
    }
  } else {
    auto *dst32 = std::assume_aligned<4>(reinterpret_cast<uint8_t *>(dst_row + px));
    for (; px <= x_end4; px += 4, dst32 += 8) {
      const uint32_t out0 = blend_swar_q8_px<Mode>(src_row[px + 0], inv, rb0, g0);
      const uint32_t out1 = blend_swar_q8_px<Mode>(src_row[px + 1], inv, rb1, g1);
      const uint32_t out2 = blend_swar_q8_px<Mode>(src_row[px + 2], inv, rb2, g2);
      const uint32_t out3 = blend_swar_q8_px<Mode>(src_row[px + 3], inv, rb3, g3);
      const uint32_t p01 = out0 | (out1 << 16);
      const uint32_t p23 = out2 | (out3 << 16);
      std::memcpy(dst32, &p01, 4U);
      std::memcpy(dst32 + 4, &p23, 4U);
    }
  }
  for (; px <= x1; ++px) {
    const int k = px & 3;
    dst_row[px] = blend_swar_q8_px<Mode>(src_row[px], inv, base_rb[k], base_g_s5[k]);
  }
}

#if LATTER_FB_PPA && !LATTER_ROUND_DISPLAY
void blend_linear_wash_edges_cpu(uint16_t *dst_fb, const uint16_t *src_bg, size_t start_px, size_t count_px,
                                 int sr5, int sg6, int sb5, int sheet_q8, int falloff_drop, int mode) noexcept {
  const int span_h = std::max(1, SCREEN_H - 1);
  for (size_t i = 0U; i < count_px; ++i) {
    const size_t idx = start_px + i;
    const int py = static_cast<int>(idx / static_cast<size_t>(SCREEN_W));
    const int px = static_cast<int>(idx - static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W));
    const int falloff_q8 = 256 - ((py * falloff_drop) / span_h);
    const int a = (sheet_q8 * std::clamp(falloff_q8, 0, 256)) >> 8;
    if (a <= 0) {
      dst_fb[idx] = src_bg[idx];
      continue;
    }
    const uint32_t inv = static_cast<uint32_t>(256 - a);
    const uint32_t d = static_cast<uint32_t>(kBayer4[py & 3][px & 3]) << 4;
    const uint32_t base_rb = ((static_cast<uint32_t>(sr5 * a) + d) << 16) |
                             (static_cast<uint32_t>(sb5 * a) + d);
    const uint32_t base_g_s5 = (static_cast<uint32_t>(sg6 * a) + d) << 5;
    dst_fb[idx] = (mode == 3) ? blend_swar_q8_px<3>(src_bg[idx], inv, base_rb, base_g_s5)
                              : blend_swar_q8_px<1>(src_bg[idx], inv, base_rb, base_g_s5);
  }
}
#endif

template <int Mode>
void blend_vertical_wash_cpu_mode(uint16_t *dst_fb, const uint16_t *src_bg, int lo_y, int hi_y,
                                  int sr5, int sg6, int sb5, int sheet_q8, int falloff_drop,
                                  int round_inset, int cx) noexcept {
  const int inset = std::max(0, round_inset);
  const int span_h = std::max(1, SCREEN_H - 1 - 2 * inset);
  const bool fused_copy = (src_bg != dst_fb);
  for (int py = lo_y; py <= hi_y; ++py) {
    uint16_t *const dst_row = dst_fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W);
    const uint16_t *const src_row = src_bg + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W);
    int x0 = 0;
    int x1 = SCREEN_W - 1;
    const bool blend_row = (py >= inset && py <= SCREEN_H - 1 - inset);
#if LATTER_ROUND_DISPLAY
    if (inset > 0) {
      const int outer_hw = circle_half_span(py, 0) + 2;
      const int ox0 = std::max(0, cx - outer_hw);
      const int ox1 = std::min(SCREEN_W - 1, cx + outer_hw);
      const int inner_hw = blend_row ? circle_half_span(py, inset) : 0;
      if (inner_hw <= 0) {
        if (fused_copy && ox0 <= ox1) {
          std::memcpy(dst_row + ox0, src_row + ox0, static_cast<size_t>(ox1 - ox0 + 1) * sizeof(uint16_t));
        }
        continue;
      }
      x0 = std::max(0, cx - inner_hw);
      x1 = std::min(SCREEN_W - 1, cx + inner_hw);
      if (fused_copy) {
        if (ox0 < x0) {
          std::memcpy(dst_row + ox0, src_row + ox0, static_cast<size_t>(x0 - ox0) * sizeof(uint16_t));
        }
        if (ox1 > x1) {
          std::memcpy(dst_row + x1 + 1, src_row + x1 + 1, static_cast<size_t>(ox1 - x1) * sizeof(uint16_t));
        }
      }
    } else {
      (void)cx;
      if (!blend_row) {
        if (fused_copy) std::memcpy(dst_row, src_row, static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
        continue;
      }
    }
#else
    (void)cx;
    if (!blend_row) {
      if (fused_copy) std::memcpy(dst_row, src_row, static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
      continue;
    }
#endif
    const int falloff_q8 = 256 - (((py - inset) * falloff_drop) / span_h);
    const int a = (sheet_q8 * std::clamp(falloff_q8, 0, 256)) >> 8;
    if (a <= 0) {
      if (fused_copy && x0 <= x1) {
        std::memcpy(dst_row + x0, src_row + x0, static_cast<size_t>(x1 - x0 + 1) * sizeof(uint16_t));
      }
      continue;
    }
    const uint32_t inv = static_cast<uint32_t>(256 - a);
    const uint32_t sr_a = static_cast<uint32_t>(sr5 * a);
    const uint32_t sg_a = static_cast<uint32_t>(sg6 * a);
    const uint32_t sb_a = static_cast<uint32_t>(sb5 * a);
    const uint8_t *const bayer = kBayer4[py & 3];
    uint32_t base_rb[4], base_g_s5[4];
    for (int k = 0; k < 4; ++k) {
      const uint32_t dk = static_cast<uint32_t>(bayer[k]) << 4;
      base_rb[k] = ((sr_a + dk) << 16) | (sb_a + dk);
      base_g_s5[k] = (sg_a + dk) << 5;
    }
    blend_swar_q8_span<Mode>(dst_row, src_row, x0, x1, inv, base_rb, base_g_s5);
  }
}

}  // namespace

void blit_psram_band_fast(uint16_t *dst_fb, const uint16_t *src_cache, int y0, int y1) noexcept {
  if (dst_fb == nullptr || src_cache == nullptr) return;
  const int lo_y = std::max(0, y0);
  const int hi_y = std::min(SCREEN_H - 1, y1);
  if (lo_y > hi_y) return;

  const size_t row_bytes = static_cast<size_t>(SCREEN_W) * sizeof(uint16_t);
  const size_t off_bytes = static_cast<size_t>(lo_y) * row_bytes;
  const size_t total_bytes = static_cast<size_t>(hi_y - lo_y + 1) * row_bytes;
  uint8_t *const dst_ptr = reinterpret_cast<uint8_t *>(dst_fb) + off_bytes;
  const uint8_t *const src_ptr = reinterpret_cast<const uint8_t *>(src_cache) + off_bytes;

#if LATTER_FB_PPA
  constexpr size_t kMinPpaBytes = 32768U;
  constexpr uintptr_t kAlignMask = static_cast<uintptr_t>(kPpaSrmBufAlign - 1U);
  if (total_bytes >= kMinPpaBytes && dst_fb != src_cache && ppa_srm_blit_init()) {
    auto &st = ppa_srm_blit_state();
    const uintptr_t dst_addr = reinterpret_cast<uintptr_t>(dst_ptr);
    const uintptr_t lo = (dst_addr + kAlignMask) & ~kAlignMask;
    const uintptr_t hi = (dst_addr + total_bytes) & ~kAlignMask;
    if (hi > lo) {
      const size_t head = static_cast<size_t>(lo - dst_addr);
      const size_t rows = static_cast<size_t>(hi - lo) / kPpaCopyRowBytes;
      if (rows > 0U && rows <= kFbPpaMaxRows) {
        const size_t interior_len = rows * kPpaCopyRowBytes;
        const size_t tail = total_bytes - head - interior_len;
        uint8_t *const dst_lo = dst_ptr + head;
        const uint8_t *const src_lo = src_ptr + head;

        ppa_srm_oper_config_t cfg = {};
        cfg.in.buffer = src_lo;
        cfg.in.pic_w = kPpaCopyRowPx;
        cfg.in.pic_h = static_cast<uint32_t>(rows);
        cfg.in.block_w = kPpaCopyRowPx;
        cfg.in.block_h = static_cast<uint32_t>(rows);
        cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        cfg.out.buffer = dst_lo;
        cfg.out.buffer_size = static_cast<uint32_t>(interior_len);
        cfg.out.pic_w = kPpaCopyRowPx;
        cfg.out.pic_h = static_cast<uint32_t>(rows);
        cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        cfg.scale_x = 1.0f;
        cfg.scale_y = 1.0f;
        cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
        cfg.mode = PPA_TRANS_MODE_BLOCKING;

        const int64_t t0_us = esp_timer_get_time();
        const esp_err_t srm_err = ppa_do_scale_rotate_mirror(st.ppa, &cfg);
        const int64_t t_srm_us = esp_timer_get_time();
        if (srm_err == ESP_OK) {
          const esp_err_t post_err = esp_cache_msync(dst_lo, interior_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
          const int64_t t_post_us = esp_timer_get_time();
          if (post_err == ESP_OK) {
            if (head > 0U) std::memcpy(dst_ptr, src_ptr, head);
            if (tail > 0U) std::memcpy(dst_lo + interior_len, src_lo + interior_len, tail);
            st.blits++;
            if (!st.verified) {
              st.verified = true;
              const uint16_t *const dpx = reinterpret_cast<const uint16_t *>(dst_lo);
              const uint16_t *const spx = reinterpret_cast<const uint16_t *>(src_lo);
              const size_t n = interior_len / sizeof(uint16_t);
              bool match = true;
              for (size_t i = 0U; i < 8U && n > 0U; ++i) {
                const size_t idx = (n > 1U) ? ((i * (n - 1U)) / 7U) : 0U;
                if (dpx[idx] != spx[idx]) {
                  ESP_LOGW("disp_fb",
                           "PSRAM PPA SRM blit mismatch at %u: expected 0x%04X, got 0x%04X; disabling",
                           static_cast<unsigned>(idx), static_cast<unsigned>(spx[idx]),
                           static_cast<unsigned>(dpx[idx]));
                  st.ok = false;
                  match = false;
                  break;
                }
              }
              if (!match) {
                std::memcpy(dst_ptr, src_ptr, total_bytes);
                return;
              }
              ESP_LOGI("disp_fb",
                       "PSRAM PPA SRM band blit verified: %u KB (%u x %u px) in %ld us (srm=%ldus post=%ldus)",
                       static_cast<unsigned>(interior_len / 1024U),
                       static_cast<unsigned>(kPpaCopyRowPx),
                       static_cast<unsigned>(rows),
                       static_cast<long>(t_post_us - t0_us),
                       static_cast<long>(t_srm_us - t0_us),
                       static_cast<long>(t_post_us - t_srm_us));
            }
            return;
          }
          st.ok = false;
          ESP_LOGW("disp_fb", "PSRAM PPA SRM post-msync failed (err=%d); disabling",
                   static_cast<int>(post_err));
        } else {
          st.ok = false;
          ESP_LOGW("disp_fb", "PSRAM PPA SRM blit failed (err=%d); disabling",
                   static_cast<int>(srm_err));
        }
      }
    }
  }
#endif

  std::memcpy(dst_ptr, src_ptr, total_bytes);
}

bool ppa_srm_scale_rgb565(uint16_t *dst_fb, int dst_stride_px, int dst_x0, int dst_y0,
                          int dst_w, int dst_h, const uint16_t *src, int src_w, int src_h,
                          int crop_x0, int crop_y0, int crop_w, int crop_h) noexcept {
#if LATTER_FB_PPA
  if (dst_fb == nullptr || src == nullptr || dst_fb == src || dst_stride_px <= 0 ||
      dst_x0 < 0 || dst_y0 < 0 || dst_w <= 0 || dst_h <= 0 ||
      dst_x0 + dst_w > dst_stride_px || src_w <= 0 || src_h <= 0 ||
      crop_x0 < 0 || crop_y0 < 0 || crop_w <= 0 || crop_h <= 0 ||
      crop_x0 + crop_w > src_w || crop_y0 + crop_h > src_h) {
    return false;
  }
  const int endian = active_frame_endian_mode();
  if (endian != 1 && endian != 3) return false;
  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  if ((reinterpret_cast<uintptr_t>(dst_fb) & kMask) != 0U) return false;

  const float sx = static_cast<float>(dst_w) / static_cast<float>(crop_w);
  const float sy = static_cast<float>(dst_h) / static_cast<float>(crop_h);
  if (sx < 0.0625f || sx > 16.0f || sy < 0.0625f || sy > 16.0f) return false;
  if (!ppa_srm_blit_init()) return false;

  const size_t src_bytes = static_cast<size_t>(src_w) * static_cast<size_t>(src_h) * sizeof(uint16_t);
  const size_t out_pic_h = static_cast<size_t>(dst_y0 + dst_h);
  const size_t out_bytes = align_up_psram(static_cast<size_t>(dst_stride_px) * out_pic_h * sizeof(uint16_t));
  ui_psram_cache_writeback(src, src_bytes);

  auto &st = ppa_srm_blit_state();
  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = src;
  cfg.in.pic_w = static_cast<uint32_t>(src_w);
  cfg.in.pic_h = static_cast<uint32_t>(src_h);
  cfg.in.block_w = static_cast<uint32_t>(crop_w);
  cfg.in.block_h = static_cast<uint32_t>(crop_h);
  cfg.in.block_offset_x = static_cast<uint32_t>(crop_x0);
  cfg.in.block_offset_y = static_cast<uint32_t>(crop_y0);
  cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  cfg.out.buffer = dst_fb;
  cfg.out.buffer_size = static_cast<uint32_t>(out_bytes);
  cfg.out.pic_w = static_cast<uint32_t>(dst_stride_px);
  cfg.out.pic_h = static_cast<uint32_t>(out_pic_h);
  cfg.out.block_offset_x = static_cast<uint32_t>(dst_x0);
  cfg.out.block_offset_y = static_cast<uint32_t>(dst_y0);
  cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = sx;
  cfg.scale_y = sy;
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  const esp_err_t err = ppa_do_scale_rotate_mirror(st.ppa, &cfg);
  if (err != ESP_OK) return false;
  const uintptr_t y0_addr = reinterpret_cast<uintptr_t>(dst_fb + static_cast<size_t>(dst_y0) * static_cast<size_t>(dst_stride_px));
  const uintptr_t y1_addr = reinterpret_cast<uintptr_t>(dst_fb + out_pic_h * static_cast<size_t>(dst_stride_px));
  const uintptr_t lo = y0_addr & ~kMask;
  const uintptr_t hi = (y1_addr + kMask) & ~kMask;
  if (hi > lo) {
    if (esp_cache_msync(reinterpret_cast<void *>(lo), static_cast<size_t>(hi - lo),
                        ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK) {
      return false;
    }
  }
  st.blits++;
  return true;
#else
  (void)dst_fb;
  (void)dst_stride_px;
  (void)dst_x0;
  (void)dst_y0;
  (void)dst_w;
  (void)dst_h;
  (void)src;
  (void)src_w;
  (void)src_h;
  (void)crop_x0;
  (void)crop_y0;
  (void)crop_w;
  (void)crop_h;
  return false;
#endif
}

bool blend_psram_vertical_wash_fast(uint16_t *dst_fb, const uint16_t *src_bg, int y0, int y1,
                                    uint8_t wash_r, uint8_t wash_g, uint8_t wash_b,
                                    int sheet_q8, int falloff_drop, int round_inset, int cx) noexcept {
  if (dst_fb == nullptr || src_bg == nullptr) return false;
  const int lo_y = std::max(0, y0);
  const int hi_y = std::min(SCREEN_H - 1, y1);
  if (lo_y > hi_y) return false;

  const int clamped_q8 = std::clamp(sheet_q8, 0, 256);
  if (clamped_q8 <= 0) {
    if (dst_fb != src_bg) blit_psram_band_fast(dst_fb, src_bg, lo_y, hi_y);
    return false;
  }

  const int mode = active_frame_endian_mode();
#if LATTER_FB_PPA && !LATTER_ROUND_DISPLAY
  constexpr size_t kMinPpaBytes = 32768U;
  constexpr uintptr_t kAlignMask = static_cast<uintptr_t>(kPpaSrmBufAlign - 1U);
  const size_t row_bytes = static_cast<size_t>(SCREEN_W) * sizeof(uint16_t);
  const size_t off_bytes = static_cast<size_t>(lo_y) * row_bytes;
  const size_t total_bytes = static_cast<size_t>(hi_y - lo_y + 1) * row_bytes;
  if (total_bytes >= kMinPpaBytes && dst_fb != src_bg && round_inset <= 0 &&
      (mode == 1 || mode == 3) && ppa_blend_wash_init()) {
    uint8_t *const dst_ptr = reinterpret_cast<uint8_t *>(dst_fb) + off_bytes;
    const uint8_t *const src_ptr = reinterpret_cast<const uint8_t *>(src_bg) + off_bytes;
    const uintptr_t dst_addr = reinterpret_cast<uintptr_t>(dst_ptr);
    const uintptr_t lo = (dst_addr + kAlignMask) & ~kAlignMask;
    const uintptr_t hi = (dst_addr + total_bytes) & ~kAlignMask;
    if (hi > lo) {
      const size_t head_bytes = static_cast<size_t>(lo - dst_addr);
      const size_t rows = static_cast<size_t>(hi - lo) / kPpaCopyRowBytes;
      if ((head_bytes & 1U) == 0U && rows > 0U && rows <= kFbPpaMaxRows) {
        uint8_t *const alpha_mask = ppa_blend_wash_ensure_mask(falloff_drop);
        if (alpha_mask != nullptr) {
          const size_t interior_len = rows * kPpaCopyRowBytes;
          const size_t head_px = head_bytes >> 1;
          const size_t interior_px = rows * kPpaCopyRowPx;
          const size_t tail_px = (total_bytes >> 1) - head_px - interior_px;
          const size_t base_px = static_cast<size_t>(lo_y) * static_cast<size_t>(SCREEN_W);
          uint8_t *const dst_lo = dst_ptr + head_bytes;
          const uint8_t *const src_lo = src_ptr + head_bytes;

          auto &st = ppa_blend_wash_state();
          ppa_blend_oper_config_t bcfg = {};
          bcfg.in_bg.buffer = src_lo;
          bcfg.in_bg.pic_w = kPpaCopyRowPx;
          bcfg.in_bg.pic_h = static_cast<uint32_t>(rows);
          bcfg.in_bg.block_w = kPpaCopyRowPx;
          bcfg.in_bg.block_h = static_cast<uint32_t>(rows);
          bcfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

          bcfg.in_fg.buffer = alpha_mask + base_px + head_px;
          bcfg.in_fg.pic_w = kPpaCopyRowPx;
          bcfg.in_fg.pic_h = static_cast<uint32_t>(rows);
          bcfg.in_fg.block_w = kPpaCopyRowPx;
          bcfg.in_fg.block_h = static_cast<uint32_t>(rows);
          bcfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;

          bcfg.out.buffer = dst_lo;
          bcfg.out.buffer_size = static_cast<uint32_t>(interior_len);
          bcfg.out.pic_w = kPpaCopyRowPx;
          bcfg.out.pic_h = static_cast<uint32_t>(rows);
          bcfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

          bcfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
          bcfg.bg_alpha_fix_val = 255U;
          if (clamped_q8 >= 256) {
            bcfg.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
          } else {
            bcfg.fg_alpha_update_mode = PPA_ALPHA_SCALE;
            bcfg.fg_alpha_scale_ratio = static_cast<float>(clamped_q8) * (1.0f / 256.0f);
          }
          bcfg.fg_fix_rgb_val.r = (mode == 3) ? wash_b : wash_r;
          bcfg.fg_fix_rgb_val.g = wash_g;
          bcfg.fg_fix_rgb_val.b = (mode == 3) ? wash_r : wash_b;
          bcfg.mode = PPA_TRANS_MODE_BLOCKING;

          const int64_t t0_us = esp_timer_get_time();
          const esp_err_t blend_err = ppa_do_blend(st.ppa, &bcfg);
          const int64_t t_blend_us = esp_timer_get_time();
          if (blend_err == ESP_OK) {
            const esp_err_t post_err = esp_cache_msync(dst_lo, interior_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            const int64_t t_post_us = esp_timer_get_time();
            if (post_err == ESP_OK) {
              if (head_px > 0U) {
                blend_linear_wash_edges_cpu(dst_fb, src_bg, base_px, head_px,
                                            wash_r >> 3, wash_g >> 2, wash_b >> 3,
                                            clamped_q8, falloff_drop, mode);
              }
              if (tail_px > 0U) {
                blend_linear_wash_edges_cpu(dst_fb, src_bg, base_px + head_px + interior_px, tail_px,
                                            wash_r >> 3, wash_g >> 2, wash_b >> 3,
                                            clamped_q8, falloff_drop, mode);
              }
              st.washes++;
              if (!st.verified) {
                st.verified = true;
                ESP_LOGI("disp_fb",
                         "PSRAM PPA Blend wash verified: %u KB (%u x %u px, head=%u tail=%u) in %ld us (blend=%ldus post=%ldus)",
                         static_cast<unsigned>(interior_len / 1024U),
                         static_cast<unsigned>(kPpaCopyRowPx), static_cast<unsigned>(rows),
                         static_cast<unsigned>(head_px), static_cast<unsigned>(tail_px),
                         static_cast<long>(t_post_us - t0_us),
                         static_cast<long>(t_blend_us - t0_us),
                         static_cast<long>(t_post_us - t_blend_us));
              }
              return true;
            }
            st.ok = false;
            ESP_LOGW("disp_fb", "PSRAM PPA Blend post-msync failed (err=%d); disabling",
                     static_cast<int>(post_err));
          } else {
            st.ok = false;
            ESP_LOGW("disp_fb", "PSRAM PPA Blend wash failed (err=%d); disabling",
                     static_cast<int>(blend_err));
          }
        }
      }
    }
  }
#endif

  const int sr5 = wash_r >> 3;
  const int sg6 = wash_g >> 2;
  const int sb5 = wash_b >> 3;
  if (mode == 1 || mode < 0) {
    blend_vertical_wash_cpu_mode<1>(dst_fb, src_bg, lo_y, hi_y, sr5, sg6, sb5, clamped_q8,
                                    falloff_drop, round_inset, cx);
  } else if (mode == 2) {
    blend_vertical_wash_cpu_mode<2>(dst_fb, src_bg, lo_y, hi_y, sr5, sg6, sb5, clamped_q8,
                                    falloff_drop, round_inset, cx);
  } else if (mode == 4) {
    blend_vertical_wash_cpu_mode<4>(dst_fb, src_bg, lo_y, hi_y, sr5, sg6, sb5, clamped_q8,
                                    falloff_drop, round_inset, cx);
  } else {
    blend_vertical_wash_cpu_mode<3>(dst_fb, src_bg, lo_y, hi_y, sr5, sg6, sb5, clamped_q8,
                                    falloff_drop, round_inset, cx);
  }
  return false;
}

FrameProfileScope::FrameProfileScope(int p, int s, int pw) : page(p), sub(s), pwr(pw) {
  fb_clear_wait();
  t0_us = prof_now_us();
  prev_disp_ms = last_disp_update_ms();
  prev_ambient_ms = last_ambient_anim_ms();
  const uint32_t start_m = hal_millis_now();
  last_disp_update_ms() = start_m;
  last_ambient_anim_ms() = start_m;
}

void FrameProfileScope::abort_idle() {
  t0_us = 0;
  last_disp_update_ms() = prev_disp_ms;
  last_ambient_anim_ms() = prev_ambient_ms;
}

FrameProfileScope::~FrameProfileScope() {
  reset_clip_y_band();
  if (t0_us == 0) return;
  fb_clear_frame_end();
  auto &fbs = frame_base_state();
  if (!fbs.painted && !fbs.warned) {
    fbs.warned = true;
    ESP_LOGW("disp", "Frame on page %d sub %d drew without frame_base(); it shows stale pixels", page, sub);
  }
  const uint32_t render_us = static_cast<uint32_t>(prof_now_us() - t0_us);
  const uint32_t now_m = hal_millis_now();
  auto &fbc_post = fb_clear_state();
  if (fbc_post.owner != nullptr && fbc_post.post_frame_hook != nullptr) {
    fbc_post.post_stamp_pending = true;
    fbc_post.post_frame_hook(fbc_post.owner, &fb_clear_after_update);
  } else {
    last_disp_update_end_ms() = now_m;
  }
  if (!profiler_enabled()) return;
  auto &fr = flight_recorder();
  const uint32_t dt_ms = (fr.last_frame_ms > 0 && now_m >= fr.last_frame_ms) ? (now_m - fr.last_frame_ms) : 0U;
  fr.last_frame_ms = now_m;
  fr.window_frames++;
  fr.window_render_us += render_us;
  if (render_us > fr.max_render_us) {
    fr.max_render_us = render_us;
    fr.max_render_page = page;
    fr.max_render_sub = sub;
  }
  const bool is_slow = (render_us >= kSlowFrameUs);
  if (is_slow) fr.slow_frames++;
  if (page != fr.last_logged_page || sub != fr.last_logged_sub || is_slow) {
    fr.last_logged_page = page;
    fr.last_logged_sub = sub;
    LATTER_PROF_LOGI("[PROF:FRAME] page=%d sub=%d pwr=%d render=%luus dt=%lums%s",
                     page, sub, pwr, static_cast<unsigned long>(render_us),
                     static_cast<unsigned long>(dt_ms), is_slow ? " [SLOW_FRAME]" : "");
  }
  if (fr.last_summary_ms == 0) fr.last_summary_ms = now_m;
  const uint32_t win_el = now_m - fr.last_summary_ms;
  if (win_el >= 5000U && fr.window_frames > 0) {
    const float fps = (static_cast<float>(fr.window_frames) * 1000.0f) / static_cast<float>(win_el);
    const uint32_t avg_us = fr.window_render_us / fr.window_frames;
#if LATTER_FB_PPA
    auto &fbc = fb_clear_state();
    auto &ppa_srm = ppa_srm_blit_state();
    auto &ppa_bld = ppa_blend_wash_state();
    LATTER_PROF_LOGI("[PROF:FRAME_SUM] frames=%lu fps=%.1f avg=%luus max=%luus(p%d/%d) slow=%lu ppa_clear=%lu/%lu ppa=%lu/%lu%s%s%s",
                     static_cast<unsigned long>(fr.window_frames), fps, static_cast<unsigned long>(avg_us),
                     static_cast<unsigned long>(fr.max_render_us), fr.max_render_page, fr.max_render_sub,
                     static_cast<unsigned long>(fr.slow_frames), static_cast<unsigned long>(fbc.hits),
                     static_cast<unsigned long>(fbc.hits + fbc.misses),
                     static_cast<unsigned long>(ppa_srm.blits), static_cast<unsigned long>(ppa_bld.washes),
                     fbc.why != nullptr ? " ppa_off=" : "", fbc.why != nullptr ? fbc.why : "",
                     fbc.why != nullptr ? fb_clear_why_err_str() : "");
    fbc.hits = fbc.misses = 0;
    ppa_srm.blits = 0U;
    ppa_bld.washes = 0U;
#else
    LATTER_PROF_LOGI("[PROF:FRAME_SUM] frames=%lu fps=%.1f avg=%luus max=%luus(p%d/%d) slow=%lu",
                     static_cast<unsigned long>(fr.window_frames), fps, static_cast<unsigned long>(avg_us),
                     static_cast<unsigned long>(fr.max_render_us), fr.max_render_page, fr.max_render_sub,
                     static_cast<unsigned long>(fr.slow_frames));
#endif
    uint32_t *sect = prof_sect_us(), any_sect = 0;
    for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) any_sect |= sect[i];
    if (any_sect != 0U) {
      const uint32_t n = fr.window_frames;
      LATTER_PROF_LOGI("[PROF:SECT] avg/frame base=%luus art=%luus sky=%luus clouds=%luus precip=%luus text=%luus dock=%luus",
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::BASE)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::ART)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::BG_SKY)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::BG_CLOUDS)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::BG_PRECIP)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::HOME_TEXT)] / n),
                       static_cast<unsigned long>(sect[static_cast<size_t>(ProfSect::HOME_DOCK)] / n));
      for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) sect[i] = 0;
    }
    fr.window_frames = fr.window_render_us = fr.max_render_us = fr.slow_frames = 0;
    fr.last_summary_ms = now_m;
  }
}

// ------------------------------------------------------------------------------
// 3. WCAG 4.5:1 Frosted Glass Backdrop Sampling, Luminance LUT & Adaptive Fill Solver
// ------------------------------------------------------------------------------
const float *srgb_linear_lut() {
  static float lut[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; i++) {
      const float c = static_cast<float>(i) / 255.0f;
      lut[i] = (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    ready = true;
  }
  return lut;
}

float relative_luminance(UiColor c) {
  const float *lut = srgb_linear_lut();
  return 0.2126f * lut[c.r] + 0.7152f * lut[c.g] + 0.0722f * lut[c.b];
}

float contrast_ratio(float la, float lb) {
  const float hi = std::max(la, lb);
  const float lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

UiColor mix_color_q8(UiColor c_under, UiColor c_over, int a) {
  a = std::max(0, std::min(256, a));
  const int ia = 256 - a;
  return UiColor{
      static_cast<uint8_t>((c_over.r * a + c_under.r * ia) >> 8),
      static_cast<uint8_t>((c_over.g * a + c_under.g * ia) >> 8),
      static_cast<uint8_t>((c_over.b * a + c_under.b * ia) >> 8),
  };
}

uint8_t active_glass_style_raw() noexcept {
  return static_cast<uint8_t>(latter_core::persisted_prefs().glass_style_enum());
}

GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  if (!backdrop_active() || w <= 0 || h <= 0 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0);
  const int xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, std::max(clip_y0(), y0));
  const int ye = std::min(SCREEN_H, std::min(clip_y1() + 1, y0 + h));
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const float *lut = srgb_linear_lut();
  const int mode = active_frame_endian_mode();
  const int step_x = std::max(4, w / 12);
  const int step_y = std::max(3, h / 12);
  const int x_start = xs + std::min(step_x / 2, (xe - xs - 1) / 2);
  const int y_start = ys + std::min(step_y / 2, (ye - ys - 1) / 2);
  uint32_t cnt[32] = {0}, sr[32] = {0}, sg[32] = {0}, sb[32] = {0};
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = y_start; y < ye; y += step_y) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = x_start; x < xe; x += step_x) {
      const uint16_t hw = row[x];
      const uint16_t p = (mode == 1 || mode < 0)
                             ? decode_hw_rgb565_mode<1>(hw)
                             : ((mode == 2) ? decode_hw_rgb565_mode<2>(hw)
                                            : ((mode == 4) ? decode_hw_rgb565_mode<4>(hw)
                                                           : decode_hw_rgb565_mode<3>(hw)));
      const int r5 = (p >> 11) & 0x1F;
      const int g6 = (p >> 5) & 0x3F;
      const int b5 = p & 0x1F;
      const int r = (r5 << 3) | (r5 >> 2);
      const int g = (g6 << 2) | (g6 >> 4);
      const int b = (b5 << 3) | (b5 >> 2);
      const float lum = 0.2126f * lut[r] + 0.7152f * lut[g] + 0.0722f * lut[b];
      const int bin = std::min(31, static_cast<int>(lum * 32.0f));
      cnt[bin]++;
      sr[bin] += static_cast<uint32_t>(r);
      sg[bin] += static_cast<uint32_t>(g);
      sb[bin] += static_cast<uint32_t>(b);
      tr += static_cast<uint32_t>(r);
      tg += static_cast<uint32_t>(g);
      tb += static_cast<uint32_t>(b);
      n++;
    }
  }
  if (n == 0) return out;
  const uint32_t want = std::max<uint32_t>(1U, (n * kGlassBrightPercent + 99U) / 100U);
  uint32_t acc = 0, ar = 0, ag = 0, ab = 0;
  for (int bin = 31; bin >= 0 && acc < want; bin--) {
    acc += cnt[bin];
    ar += sr[bin];
    ag += sg[bin];
    ab += sb[bin];
  }
  out.bright = UiColor{
      static_cast<uint8_t>(ar / acc),
      static_cast<uint8_t>(ag / acc),
      static_cast<uint8_t>(ab / acc),
  };
  out.mean = UiColor{
      static_cast<uint8_t>(tr / n),
      static_cast<uint8_t>(tg / n),
      static_cast<uint8_t>(tb / n),
  };
  out.sample_count = static_cast<uint16_t>(std::min<uint32_t>(n, 65535U));
  out.valid = true;
  return out;
}

GlassBackdrop sample_glass_mean(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  if (!backdrop_active() || w <= 0 || h <= 0 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0);
  const int xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, std::max(clip_y0(), y0));
  const int ye = std::min(SCREEN_H, std::min(clip_y1() + 1, y0 + h));
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const int mode = active_frame_endian_mode();
  const int step = std::max(4, std::max(w, h) / 6);
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t hw = row[x];
      const uint16_t p = (mode == 1 || mode < 0)
                             ? decode_hw_rgb565_mode<1>(hw)
                             : ((mode == 2) ? decode_hw_rgb565_mode<2>(hw)
                                            : ((mode == 4) ? decode_hw_rgb565_mode<4>(hw)
                                                           : decode_hw_rgb565_mode<3>(hw)));
      const uint32_t r5 = (p >> 11) & 0x1FU;
      const uint32_t g6 = (p >> 5) & 0x3FU;
      const uint32_t b5 = p & 0x1FU;
      tr += (r5 << 3) | (r5 >> 2);
      tg += (g6 << 2) | (g6 >> 4);
      tb += (b5 << 3) | (b5 >> 2);
      n++;
    }
  }
  if (n == 0) return out;
  out.mean = UiColor{
      static_cast<uint8_t>(tr / n),
      static_cast<uint8_t>(tg / n),
      static_cast<uint8_t>(tb / n),
  };
  out.bright = out.mean;
  out.sample_count = static_cast<uint16_t>(std::min<uint32_t>(n, 65535U));
  out.valid = true;
  return out;
}

bool is_semantic_glass_color(UiColor c) {
  const int mx = std::max({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
  const int mn = std::min({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
  const int chroma = mx - mn;
  if (chroma <= 12) return false;
  if (c.r == kChromeHomePill.r && c.g == kChromeHomePill.g && c.b == kChromeHomePill.b) return false;
  if (c.r <= c.g && c.g <= c.b && mx <= 96 && chroma <= 30) return false;
  return true;
}

void normalize_glass_chrome(UiColor caller_bg, UiColor caller_brd,
                            GlassVariant &inout_variant, UiColor &out_base_tint,
                            UiColor &out_rim_tint) {
  const UiColor neutral_base =
      (inout_variant == GlassVariant::ReadableDark) ? kChromeModalBg : kChromeGlassTint;
  const bool sem_bg = is_semantic_glass_color(caller_bg);
  const bool sem_brd = is_semantic_glass_color(caller_brd);

  if (sem_bg) {
    const int bg_mx = std::max({static_cast<int>(caller_bg.r),
                                static_cast<int>(caller_bg.g),
                                static_cast<int>(caller_bg.b)});
    const UiColor wash_src = (bg_mx < 56 && sem_brd) ? caller_brd : caller_bg;
    out_base_tint = mix_color_q8(neutral_base, wash_src, kGlassSemanticWashMaxQ8);
  } else if (sem_brd) {
    out_base_tint = mix_color_q8(neutral_base, caller_brd, 28);
  } else {
    out_base_tint = neutral_base;
  }

  if (sem_brd) {
    out_rim_tint = mix_color_q8(kChromeGlassSheen, caller_brd, kGlassSemanticRimMaxQ8);
  } else if (sem_bg) {
    out_rim_tint = mix_color_q8(kChromeGlassSheen, caller_bg, kGlassSemanticRimMaxQ8);
  } else {
    out_rim_tint = kChromeGlassSheen;
  }
}

int glass_alpha_for_text(UiColor tint, UiColor backdrop, UiColor text, int min_alpha_q8) {
  const float raw_lt = relative_luminance(text);
  const float lt = (raw_lt < 0.18f) ? relative_luminance(kChromeMuted) : raw_lt;
  const int start_a = std::max(0, std::min(256, min_alpha_q8));
  for (int a = start_a; a < 256; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrast) {
      return a;
    }
  }
  for (int a = start_a; a <= 248; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrastMin) {
      return a;
    }
  }
  return 248;
}

void resolve_glass_fill(const GlassBackdrop &bd, UiColor tint, UiColor ref_text,
                        UiColor &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8,
                        GlassVariant variant) {
  const uint8_t g_style = active_glass_style_raw();
  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const bool is_readable_dark = (variant == GlassVariant::ReadableDark) || (g_style == 2U);

  const float raw_fg_lum = relative_luminance(ref_text);
  const UiColor contrast_fg =
      ((static_cast<int>(ref_text.r) + static_cast<int>(ref_text.g) + static_cast<int>(ref_text.b)) < 48)
          ? kChromeGlassSheen
          : ((raw_fg_lum < 0.18f) ? kChromeMuted : ref_text);
  const float fg_lum = relative_luminance(contrast_fg);

  if (!is_readable_dark) {
    const int frost_q8 = dark_tint ? (is_semantic_glass_color(tint) ? 32 : 54) : 0;
    const UiColor clear_tint = (frost_q8 > 0) ? mix_color_q8(tint, kChromeHomePill, frost_q8) : tint;
    const int base_a = (explicit_alpha_q8 >= 0) ? explicit_alpha_q8 : kGlassClearAlphaQ8;
    const int da = dark_tint ? 12 : 0;
    const int clear_a_top = std::min(256, base_a + da);
    const int clear_a_bot = std::max(0, base_a - (da * 2) / 3);
    const int clear_rim_top = std::min(256, kGlassSheenTopQ8 + 18);
    const int clear_rim_bot = std::min(256, kGlassSheenBotQ8 + 12);

    // GlassStyle::Auto (0): stay crisp Clear over dark backdrops, smoothly morph toward
    // WCAG >= 4.5:1 ReadableDark contrast over bright skies, wallpapers, or album art.
    if (g_style == 0U && bd.valid && explicit_alpha_q8 < 0) {
      const float c_top = contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, clear_tint, clear_a_top)));
      const float c_bot = contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, clear_tint, clear_a_bot)));
      const float c_min = std::min(c_top, c_bot);
      if (c_min < kGlassTextContrast) {
        const UiColor dark_base = is_semantic_glass_color(tint)
                                      ? mix_color_q8(kChromeModalBg, tint, kGlassSemanticWashMaxQ8)
                                      : kChromeModalBg;
        const float t = std::clamp((kGlassTextContrast - c_min) / (kGlassTextContrast - 2.5f), 0.0f, 1.0f);
        const int t_q8 = static_cast<int>(std::lround(t * 256.0f));
        UiColor adapt_tint = mix_color_q8(clear_tint, dark_base, t_q8);
        int need_a = glass_alpha_for_text(adapt_tint, bd.bright, contrast_fg, kGlassClearAlphaQ8);
        if (contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, adapt_tint, need_a))) <
            kGlassTextContrastMin) {
          adapt_tint = dark_base;
          need_a = glass_alpha_for_text(adapt_tint, bd.bright, contrast_fg, kGlassClearAlphaQ8);
        }
        out_tint = adapt_tint;
        out_a_top = std::min(256, need_a + 10);
        out_a_bot = std::max(kGlassAlphaMinQ8, need_a - 6);
        if (contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, out_tint, out_a_bot))) <
            kGlassTextContrastMin) {
          out_a_bot = need_a;
        }
        if (contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, out_tint, out_a_top))) <
            kGlassTextContrastMin) {
          out_a_top = need_a;
        }
        out_rim_top = std::min(256, kGlassSheenTopQ8 + 12);
        out_rim_bot = std::min(256, kGlassSheenBotQ8 + 8);
        return;
      }
    }

    out_tint = clear_tint;
    out_a_top = clear_a_top;
    out_a_bot = clear_a_bot;
    out_rim_top = clear_rim_top;
    out_rim_bot = clear_rim_bot;
    return;
  }

  const int bg_lvl = bd.valid
                         ? ((static_cast<int>(bd.mean.r) + static_cast<int>(bd.mean.g) + static_cast<int>(bd.mean.b)) / 3)
                         : 0;
  // If caller forced GlassStyle::Dark via direct resolve_glass_fill without Dark normalization,
  // ensure dark neutral base so white text achieves >= 4.5:1 over bright backdrops.
  out_tint = (variant == GlassVariant::ReadableDark)
                 ? tint
                 : (is_semantic_glass_color(tint)
                        ? mix_color_q8(kChromeModalBg, tint, kGlassSemanticWashMaxQ8)
                        : kChromeModalBg);

  int base_a = explicit_alpha_q8;
  if (base_a < 0) {
    base_a = bd.valid
                 ? glass_alpha_for_text(out_tint, bd.bright, contrast_fg, kGlassReadableAlphaMinQ8)
                 : kGlassReadableAlphaMinQ8;
  }

  const int da = 8;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(200, base_a - (da * 2) / 3);
  if (bd.valid && explicit_alpha_q8 < 0 &&
      contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, out_tint, out_a_bot))) < kGlassTextContrastMin) {
    out_a_bot = base_a;
  }
  const int rim_boost = (bg_lvl < 32) ? ((32 - bg_lvl) * 18) / 32 : 8;
  out_rim_top = std::min(256, kGlassSheenTopQ8 + rim_boost);
  out_rim_bot = std::min(256, kGlassSheenBotQ8 + (rim_boost * 2) / 3);
}

void resolve_styled_glass(int x0, int y0, int w, int h,
                          UiColor caller_bg, UiColor caller_brd,
                          GlassVariant explicit_variant, UiColor ref_text,
                          int explicit_alpha_q8, bool sample_surface,
                          GlassBackdrop &out_bd, UiColor &out_tint, UiColor &out_rim,
                          int &out_a_top, int &out_a_bot, int &out_rim_top, int &out_rim_bot) {
  const uint8_t g_style = active_glass_style_raw();
  GlassVariant eff_var =
      (explicit_variant == GlassVariant::ReadableDark || g_style == 2U)
          ? GlassVariant::ReadableDark
          : GlassVariant::Clear;
  UiColor norm_tint{};
  normalize_glass_chrome(caller_bg, caller_brd, eff_var, norm_tint, out_rim);
  const bool need_bd = (eff_var == GlassVariant::ReadableDark) || (g_style == 0U && explicit_alpha_q8 < 0);
  out_bd = need_bd
               ? sample_glass_backdrop(x0, y0, w, h)
               : (sample_surface ? sample_glass_mean(x0, y0, w, h) : GlassBackdrop{});
  resolve_glass_fill(out_bd, norm_tint, ref_text, out_tint, out_a_top, out_a_bot,
                     out_rim_top, out_rim_bot, explicit_alpha_q8, eff_var);
}

void fill_round_rect_aa_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                             UiColor c_top, UiColor c_bot, int hole) {
  if (w <= 0 || h <= 0) return;
  const int ys = std::max(clip_y0(), y0);
  const int ye = std::min(clip_y1() + 1, y0 + h);
  if (ye <= ys) return;
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const bool flat = (c_top.r == c_bot.r && c_top.g == c_bot.g && c_top.b == c_bot.b);
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    uint32_t r88 = static_cast<uint32_t>(c_top.r) << 8;
    uint32_t g88 = static_cast<uint32_t>(c_top.g) << 8;
    uint32_t b88 = static_cast<uint32_t>(c_top.b) << 8;
    UiColor rc = c_top;
    if (!flat && h > 1) {
      const int num = py - y0;
      const int den = h - 1;
      r88 = static_cast<uint32_t>((static_cast<int>(c_top.r) << 8) + ((static_cast<int>(c_bot.r) - static_cast<int>(c_top.r)) * 256 * num) / den);
      g88 = static_cast<uint32_t>((static_cast<int>(c_top.g) << 8) + ((static_cast<int>(c_bot.g) - static_cast<int>(c_top.g)) * 256 * num) / den);
      b88 = static_cast<uint32_t>((static_cast<int>(c_top.b) << 8) + ((static_cast<int>(c_bot.b) - static_cast<int>(c_top.b)) * 256 * num) / den);
      rc = {static_cast<uint8_t>(r88 >> 8), static_cast<uint8_t>(g88 >> 8), static_cast<uint8_t>(b88 >> 8)};
    }
    auto span = [&](int a, int b) {
      if (b < a) return;
      if (flat) fast_hline(it, a, py, b - a + 1, rc);
      else fast_hline_dither(it, a, py, b - a + 1, r88, g88, b88);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      float idy = 0.0f;
      int ie0 = 0, ie1 = -1;
      const bool in_hit = has_hole && in.row(py, idy, hs0, hs1, ie0, ie1);
      if (in_hit && hole > 1) {
        span(s0, std::min(s1, ie0 - 1));
        span(std::max(s0, ie1 + 1), s1);
        if (hs1 >= hs0) {
          for (int px = std::max(s0, ie0); px <= std::min(s1, hs0 - 1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
          for (int px = std::max(s0, hs1 + 1); px <= std::min(s1, ie1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
        } else {
          for (int px = std::max(s0, ie0); px <= std::min(s1, ie1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
        }
      } else if (in_hit && hs1 >= hs0) {
        span(s0, std::min(s1, hs0 - 1));
        span(std::max(s0, hs1 + 1), s1);
      } else {
        span(s0, s1);
      }
    }
    if (s1 < s0) {
      for (int px = e0; px <= e1; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
    } else {
      for (int px = e0; px < s0; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
      for (int px = s1 + 1; px <= e1; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
    }
  }
}

namespace {

template <int Mode>
void fill_round_rect_glass_mode(uint16_t *fb, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole) {
  const int ys = std::max(clip_y0(), y0);
  const int ye = std::min(clip_y1() + 1, y0 + h);
  if (ye <= ys) return;
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const uint32_t tr = (static_cast<uint32_t>(tint.r) * 31U * 256U + 127U) / 255U;
  const uint32_t tg = (static_cast<uint32_t>(tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t tb = (static_cast<uint32_t>(tint.b) * 31U * 256U + 127U) / 255U;
  auto mix = [&](uint16_t &dst, int a, int t) {
    const uint16_t bg = decode_hw_rgb565_mode<Mode>(dst);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t ua = static_cast<uint32_t>(a);
    uint32_t r = ((tr * ua) / 256U + ((bg >> 11) & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t g = ((tg * ua) / 256U + ((bg >> 5) & 0x3FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t b = ((tb * ua) / 256U + (bg & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    if (r > 31U) r = 31U;
    if (g > 63U) g = 63U;
    if (b > 31U) b = 31U;
    dst = encode_hw_rgb565_mode<Mode>(static_cast<uint16_t>((r << 11) | (g << 5) | b));
  };
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    const int a = std::clamp((h > 1) ? (a_top + ((a_bot - a_top) * (py - y0)) / (h - 1)) : a_top, 0, 256);
    if (a <= 0) continue;
    const uint8_t *th = kBayer4[py & 3];
    uint16_t *row = fb + static_cast<size_t>(py) * SCREEN_W;
    const uint32_t ua = static_cast<uint32_t>(a);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t tr_a = (tr * ua) >> 8;
    const uint32_t tg_a = (tg * ua) >> 8;
    const uint32_t tb_a = (tb * ua) >> 8;
    uint32_t base_rb[4], base_g_s5[4];
    for (int k = 0; k < 4; k++) {
      const uint32_t tk = static_cast<uint32_t>(th[k]) * 16U + 8U;
      base_rb[k] = ((tr_a + tk) << 16) | (tb_a + tk);
      base_g_s5[k] = (tg_a + tk) << 5;
    }
    auto span = [&](int xa, int xb) {
      xa = std::max(xa, 0);
      xb = std::min(xb, SCREEN_W - 1);
      if (xa <= xb) blend_swar_q8_span<Mode>(row, row, xa, xb, ia, base_rb, base_g_s5);
    };
    auto edge = [&](int px) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = o.coverage(px, dy);
      if (cov <= 0) return;
      mix(row[px], (a * cov + 127) / 255, th[px & 3] * 16 + 8);
    };
    auto inner_edge = [&](int px, float idy) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = 255 - in.coverage(px, idy);
      if (cov <= 0) return;
      mix(row[px], (a * cov + 127) / 255, th[px & 3] * 16 + 8);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      float idy = 0.0f;
      int ie0 = 0, ie1 = -1;
      const bool in_hit = has_hole && in.row(py, idy, hs0, hs1, ie0, ie1);
      if (in_hit && hole > 1) {
        span(s0, std::min(s1, ie0 - 1));
        span(std::max(s0, ie1 + 1), s1);
        if (hs1 >= hs0) {
          for (int px = std::max(s0, ie0); px <= std::min(s1, hs0 - 1); px++) inner_edge(px, idy);
          for (int px = std::max(s0, hs1 + 1); px <= std::min(s1, ie1); px++) inner_edge(px, idy);
        } else {
          for (int px = std::max(s0, ie0); px <= std::min(s1, ie1); px++) inner_edge(px, idy);
        }
      } else if (in_hit && hs1 >= hs0) {
        span(s0, std::min(s1, hs0 - 1));
        span(std::max(s0, hs1 + 1), s1);
      } else {
        span(s0, s1);
      }
      for (int px = e0; px < s0; px++) edge(px);
      for (int px = s1 + 1; px <= e1; px++) edge(px);
    } else {
      for (int px = e0; px <= e1; px++) edge(px);
    }
  }
}

}  // namespace

void fill_round_rect_glass_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole) {
  if (w <= 0 || h <= 0) return;
  if (y0 + h <= clip_y0() || y0 > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fill_round_rect_aa_impl(it, x0, y0, w, h, rad, tint, tint, hole);
    return;
  }
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) {
    fill_round_rect_glass_mode<1>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 2) {
    fill_round_rect_glass_mode<2>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 4) {
    fill_round_rect_glass_mode<4>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else {
    fill_round_rect_glass_mode<3>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  }
}

// ------------------------------------------------------------------------------
// 4. Dirty-Band Aware Frame Base State
// ------------------------------------------------------------------------------
FrameBaseState &frame_base_state() {
  static FrameBaseState st;
  return st;
}

void frame_base_begin() {
  frame_base_state().painted = false;
  frame_base_state().has_color = false;
  backdrop_active() = false;
  set_clip_y_band(dirty_band().band_y0(), dirty_band().band_y1());
}

void frame_base_skip() { frame_base_state().painted = true; }
void frame_base_claim() { frame_base_state().painted = true; }

bool frame_base_impl(Rgb565Surface &it, UiColor c) {
  auto &st = frame_base_state();
  if (st.painted) return false;
  st.painted = true;
  const uint16_t hw = encode_hw_rgb565(c);
  st.color_hw = hw;
  const bool full = dirty_band().is_full();
  st.has_color = full;
  if (fb_clear_consume(hw)) return true;

  const uint64_t t0 = profiler_enabled() ? prof_now_us() : 0;
  const int y0 = full ? 0 : std::max(0, dirty_band().band_y0());
  const int y1 = full ? (SCREEN_H - 1) : std::min(SCREEN_H - 1, dirty_band().band_y1());
  const int cy0 = std::max(y0, clip_y0());
  const int cy1 = std::min(y1, clip_y1());
  if (cy1 >= cy0) {
    uint16_t *const fb = active_frame_buf16();
    const bool byte_uniform = ((hw & 0xFFU) == ((hw >> 8) & 0xFFU));
    if (fb != nullptr) {
#if LATTER_ROUND_DISPLAY
      for (int y = cy0; y <= cy1; ++y) {
        const int hw_span = circle_half_span(y, 0) + 2;
        const int x0 = std::max(0, SCREEN_CX - hw_span);
        const int x1 = std::min(SCREEN_W - 1, SCREEN_CX + hw_span);
        if (x0 > x1) continue;
        const int len = x1 - x0 + 1;
        if (byte_uniform) {
          std::memset(fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x0),
                      static_cast<int>(hw & 0xFFU), static_cast<size_t>(len) * sizeof(uint16_t));
        } else {
          fast_hline(it, x0, y, len, c);
        }
      }
#else
      if (byte_uniform) {
        const size_t off = static_cast<size_t>(cy0) * static_cast<size_t>(SCREEN_W);
        const size_t nbytes =
            static_cast<size_t>(cy1 - cy0 + 1) * static_cast<size_t>(SCREEN_W) * sizeof(uint16_t);
        std::memset(fb + off, static_cast<int>(hw & 0xFFU), nbytes);
      } else {
        fill_rect_row_major(it, 0, cy0, SCREEN_W, cy1 - cy0 + 1, c);
      }
#endif
    } else {
      fill_rect_row_major(it, 0, cy0, SCREEN_W, cy1 - cy0 + 1, c);
    }
  }
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::BASE)] += static_cast<uint32_t>(prof_now_us() - t0);
  return true;
}

}  // namespace latter_ui
