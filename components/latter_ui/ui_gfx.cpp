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

void ui_psram_cache_invalidate(const void *ptr, size_t bytes) noexcept {
#if LATTER_FB_PPA
  if (ptr == nullptr || bytes == 0U) return;
  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t lo = addr & ~kMask;
  const uintptr_t hi = (addr + bytes + kMask) & ~kMask;
  if (hi > lo) {
    (void)esp_cache_msync(reinterpret_cast<void *>(lo), static_cast<size_t>(hi - lo),
                          ESP_CACHE_MSYNC_FLAG_DIR_M2C);
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

bool psram_headroom_below(size_t min_free_bytes, size_t min_largest_block_bytes) noexcept {
#ifdef USE_ESP32
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < min_free_bytes) {
    return true;
  }
  return (min_largest_block_bytes > 0U) &&
         (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < min_largest_block_bytes);
#else
  (void)min_free_bytes;
  (void)min_largest_block_bytes;
  return false;
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
  bool scale_verified = false;
  bool scale_warned = false;
  bool stage_evictor_registered = false;
  ppa_client_handle_t ppa = nullptr;
  uint16_t *stage_buf = nullptr;
  size_t stage_bytes = 0U;
  uint32_t blits = 0U;
};

PpaSrmBlitState &ppa_srm_blit_state() {
  static PpaSrmBlitState st{};
  return st;
}

void ppa_srm_stage_evict() noexcept {
  auto &st = ppa_srm_blit_state();
  if (st.stage_buf != nullptr) {
    ui_psram_free(st.stage_buf);
    st.stage_buf = nullptr;
    st.stage_bytes = 0U;
  }
}

uint16_t *ppa_srm_ensure_stage(size_t needed_bytes) noexcept {
  auto &st = ppa_srm_blit_state();
  const size_t req = align_up_psram(needed_bytes);
  if (req == 0U) return nullptr;
  if (st.stage_buf != nullptr && st.stage_bytes < req) {
    ui_psram_free(st.stage_buf);
    st.stage_buf = nullptr;
    st.stage_bytes = 0U;
  }
  if (st.stage_buf == nullptr) {
    st.stage_buf = static_cast<uint16_t *>(ui_psram_aligned_alloc(req));
    if (st.stage_buf == nullptr) return nullptr;
    st.stage_bytes = req;
    if (!st.stage_evictor_registered) {
      st.stage_evictor_registered = register_psram_cache_evictor(&ppa_srm_stage_evict, true);
    }
  }
  return st.stage_buf;
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
  bool uniform_ok = true;
  bool uniform_verified = false;
  bool uniform_warned = false;
  bool glass_ok = true;
  bool glass_verified = false;
  bool evictor_registered = false;
  ppa_client_handle_t ppa = nullptr;
  uint8_t *alpha_mask = nullptr;
  int cached_falloff_drop = -1;
  uint8_t *glass_mask = nullptr;
  size_t glass_mask_cap = 0U;
  int glass_key_w = 0;
  int glass_key_h = 0;
  int glass_key_rel_y = 0;
  int glass_key_rect_h = 0;
  int glass_key_at = -1;
  int glass_key_ab = -1;
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
  if (st.glass_mask != nullptr) {
    ui_psram_free(st.glass_mask);
    st.glass_mask = nullptr;
    st.glass_mask_cap = 0U;
    st.glass_key_at = -1;
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
      st.evictor_registered = register_psram_cache_evictor(&ppa_blend_wash_evict, true);
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

uint8_t *ppa_blend_glass_ensure_mask(int core_w, int core_h, int core_y0, int rect_y0,
                                     int rect_h, int a_top, int a_bot, bool need_gradient) noexcept {
  auto &st = ppa_blend_wash_state();
  const size_t raw_bytes = static_cast<size_t>(core_w) * static_cast<size_t>(core_h);
  const size_t req_bytes = align_up_psram(raw_bytes);
  if (req_bytes == 0U) return nullptr;
  if (st.glass_mask != nullptr && st.glass_mask_cap < req_bytes) {
    ui_psram_free(st.glass_mask);
    st.glass_mask = nullptr;
    st.glass_mask_cap = 0U;
    st.glass_key_at = -1;
  }
  if (st.glass_mask == nullptr) {
    st.glass_mask = static_cast<uint8_t *>(ui_psram_aligned_alloc(req_bytes));
    if (st.glass_mask == nullptr) return nullptr;
    st.glass_mask_cap = req_bytes;
    st.glass_key_at = -1;
    if (!st.evictor_registered) {
      st.evictor_registered = register_psram_cache_evictor(&ppa_blend_wash_evict, true);
    }
  }
  if (!need_gradient) return st.glass_mask;
  const int rel_y = core_y0 - rect_y0;
  if (st.glass_key_w != core_w || st.glass_key_h != core_h ||
      st.glass_key_rel_y != rel_y || st.glass_key_rect_h != rect_h ||
      st.glass_key_at != a_top || st.glass_key_ab != a_bot) {
    const int den = std::max(1, rect_h - 1);
    for (int r = 0; r < core_h; ++r) {
      const int py_rel = rel_y + r;
      const int a_q8 = std::clamp((rect_h > 1) ? (a_top + ((a_bot - a_top) * py_rel) / den) : a_top, 0, 256);
      const int a_8 = std::clamp((a_q8 * 255 + 128) >> 8, 0, 255);
      std::memset(st.glass_mask + static_cast<size_t>(r) * static_cast<size_t>(core_w), a_8,
                  static_cast<size_t>(core_w));
    }
    st.glass_key_w = core_w;
    st.glass_key_h = core_h;
    st.glass_key_rel_y = rel_y;
    st.glass_key_rect_h = rect_h;
    st.glass_key_at = a_top;
    st.glass_key_ab = a_bot;
  }
  return st.glass_mask;
}

bool ppa_blend_glass_rect_rgb565(uint16_t *fb, int &inout_x0, int &inout_x1,
                                 int &inout_y0, int &inout_y1, int rect_y0, int rect_h,
                                 UiColor tint, int a_top, int a_bot, int mode) noexcept {
  if (fb == nullptr || (mode != 1 && mode != 3)) return false;
  if (!ppa_blend_wash_init()) return false;
  auto &st = ppa_blend_wash_state();
  if (!st.glass_ok) return false;

  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  const uintptr_t rem = reinterpret_cast<uintptr_t>(fb) & kMask;
  if ((rem & 1U) != 0U) return false;
  const size_t pad_bytes = (rem == 0U) ? 0U : (kPsramCacheLineAlign - rem);
  const int pad_px = static_cast<int>(pad_bytes >> 1);

  int x0 = std::max(inout_x0, pad_px);
  int x1 = std::min(inout_x1, SCREEN_W - 1);
  const int y0 = std::max(inout_y0, 0);
  const int y1 = std::min(inout_y1, (pad_px > 0) ? (SCREEN_H - 2) : (SCREEN_H - 1));
  if (x0 > x1 || y0 > y1) return false;

  const int eff_x0 = ((x0 - pad_px) + 7) & ~7;
  x0 = eff_x0 + pad_px;
  const int core_w = (x1 - x0 + 1) & ~15;
  x1 = x0 + core_w - 1;
  const int core_h = y1 - y0 + 1;
  if (core_w < 64 || core_h < 16 || (core_w * core_h) < 4096) return false;

  const size_t avail_bytes =
      (static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H) - static_cast<size_t>(pad_px)) * sizeof(uint16_t);
  const size_t aligned_avail = avail_bytes & ~kMask;
  const size_t out_pic_h = static_cast<size_t>(y1 + 1);
  const size_t out_pic_len = static_cast<size_t>(SCREEN_W) * out_pic_h * sizeof(uint16_t);
  if (out_pic_len > aligned_avail) return false;

  const bool need_grad = (a_top != a_bot);
  uint8_t *const mask = ppa_blend_glass_ensure_mask(core_w, core_h, y0, rect_y0, rect_h, a_top, a_bot, need_grad);
  if (mask == nullptr) return false;

  uint16_t *const aligned_fb = fb + pad_px;
  ppa_blend_oper_config_t bcfg = {};
  bcfg.in_bg.buffer = aligned_fb;
  bcfg.in_bg.pic_w = static_cast<uint32_t>(SCREEN_W);
  bcfg.in_bg.pic_h = static_cast<uint32_t>(out_pic_h);
  bcfg.in_bg.block_w = static_cast<uint32_t>(core_w);
  bcfg.in_bg.block_h = static_cast<uint32_t>(core_h);
  bcfg.in_bg.block_offset_x = static_cast<uint32_t>(eff_x0);
  bcfg.in_bg.block_offset_y = static_cast<uint32_t>(y0);
  bcfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

  bcfg.in_fg.buffer = mask;
  bcfg.in_fg.pic_w = static_cast<uint32_t>(core_w);
  bcfg.in_fg.pic_h = static_cast<uint32_t>(core_h);
  bcfg.in_fg.block_w = static_cast<uint32_t>(core_w);
  bcfg.in_fg.block_h = static_cast<uint32_t>(core_h);
  bcfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;

  bcfg.out.buffer = aligned_fb;
  bcfg.out.buffer_size = static_cast<uint32_t>(aligned_avail);
  bcfg.out.pic_w = static_cast<uint32_t>(SCREEN_W);
  bcfg.out.pic_h = static_cast<uint32_t>(out_pic_h);
  bcfg.out.block_offset_x = static_cast<uint32_t>(eff_x0);
  bcfg.out.block_offset_y = static_cast<uint32_t>(y0);
  bcfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

  bcfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  bcfg.bg_alpha_fix_val = 255U;
  if (!need_grad) {
    const int a_8 = std::clamp((std::clamp(a_top, 0, 256) * 255 + 128) >> 8, 0, 255);
    bcfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    bcfg.fg_alpha_fix_val = static_cast<uint32_t>(a_8);
  } else {
    bcfg.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  }
  bcfg.fg_fix_rgb_val.r = (mode == 3) ? tint.b : tint.r;
  bcfg.fg_fix_rgb_val.g = tint.g;
  bcfg.fg_fix_rgb_val.b = (mode == 3) ? tint.r : tint.b;
  bcfg.mode = PPA_TRANS_MODE_BLOCKING;

  const int64_t t0_us = esp_timer_get_time();
  const esp_err_t blend_err = ppa_do_blend(st.ppa, &bcfg);
  const int64_t t_blend_us = esp_timer_get_time();
  if (blend_err != ESP_OK) {
    st.glass_ok = false;
    ESP_LOGW("disp_fb", "PSRAM PPA Blend glass failed (err=%d); disabling", static_cast<int>(blend_err));
    return false;
  }
  ui_psram_cache_invalidate(aligned_fb + static_cast<size_t>(y0) * static_cast<size_t>(SCREEN_W),
                            static_cast<size_t>(core_h) * static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
  const int64_t t_post_us = esp_timer_get_time();
  st.washes++;
  if (!st.glass_verified) {
    st.glass_verified = true;
    ESP_LOGI("disp_fb",
             "PSRAM PPA Blend glass verified: %dx%d at (%d,%d) in %ld us (blend=%ldus post=%ldus)",
             core_w, core_h, x0, y0,
             static_cast<long>(t_post_us - t0_us),
             static_cast<long>(t_blend_us - t0_us),
             static_cast<long>(t_post_us - t_blend_us));
  }
  inout_x0 = x0;
  inout_x1 = x1;
  inout_y0 = y0;
  inout_y1 = y1;
  return true;
}
#endif

// 2-lane SWAR span blitter using shared blend_swar_q8_px<Mode> and 2-pixel Mode-2 bswap bursts.
template <int Mode>
inline void blend_swar_q8_span(uint16_t *dst_row, const uint16_t *src_row, int x0, int x1,
                               uint32_t inv, const uint32_t base_rb[4],
                               const uint32_t base_g_s5[4]) noexcept {
  if (x1 < x0) return;

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
    bool has_cache = false;
    uint32_t c_in01 = 0U, c_in23 = 0U, c_p01 = 0U, c_p23 = 0U;
    for (; px <= x_end4; px += 4, dst32 += 8, src32 += 8) {
      uint32_t in01 = 0U, in23 = 0U;
      std::memcpy(&in01, src32, 4U);
      std::memcpy(&in23, src32 + 4, 4U);
      if (has_cache && in01 == c_in01 && in23 == c_in23) {
        std::memcpy(dst32, &c_p01, 4U);
        std::memcpy(dst32 + 4, &c_p23, 4U);
        continue;
      }
      uint32_t p01 = 0U, p23 = 0U;
      if constexpr (Mode == 2) {
        const uint32_t sw01 = bswap16x2_u32(in01);
        const uint32_t sw23 = bswap16x2_u32(in23);
        const uint32_t out0 = blend_swar_q8_px<1>(static_cast<uint16_t>(sw01), inv, rb0, g0);
        const uint32_t out1 = blend_swar_q8_px<1>(static_cast<uint16_t>(sw01 >> 16), inv, rb1, g1);
        const uint32_t out2 = blend_swar_q8_px<1>(static_cast<uint16_t>(sw23), inv, rb2, g2);
        const uint32_t out3 = blend_swar_q8_px<1>(static_cast<uint16_t>(sw23 >> 16), inv, rb3, g3);
        p01 = bswap16x2_u32(out0 | (out1 << 16));
        p23 = bswap16x2_u32(out2 | (out3 << 16));
      } else {
        const uint32_t out0 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in01), inv, rb0, g0);
        const uint32_t out1 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in01 >> 16), inv, rb1, g1);
        const uint32_t out2 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in23), inv, rb2, g2);
        const uint32_t out3 = blend_swar_q8_px<Mode>(static_cast<uint16_t>(in23 >> 16), inv, rb3, g3);
        p01 = out0 | (out1 << 16);
        p23 = out2 | (out3 << 16);
      }
      c_in01 = in01;
      c_in23 = in23;
      c_p01 = p01;
      c_p23 = p23;
      has_cache = true;
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

void blit_psram_linear_fast(uint8_t *dst_ptr, const uint8_t *src_ptr, size_t total_bytes) noexcept {
  if (dst_ptr == nullptr || src_ptr == nullptr || total_bytes == 0U) return;
#if LATTER_FB_PPA
  constexpr size_t kMinPpaBytes = 32768U;
  constexpr uintptr_t kAlignMask = static_cast<uintptr_t>(kPpaSrmBufAlign - 1U);
  if (total_bytes >= kMinPpaBytes && dst_ptr != src_ptr && ppa_srm_blit_init()) {
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

}  // namespace

void blit_psram_band_fast(uint16_t *dst_fb, const uint16_t *src_cache, int y0, int y1) noexcept {
  if (dst_fb == nullptr || src_cache == nullptr) return;
  const int lo_y = std::max(0, y0);
  const int hi_y = std::min(SCREEN_H - 1, y1);
  if (lo_y > hi_y) return;

  const size_t row_bytes = static_cast<size_t>(SCREEN_W) * sizeof(uint16_t);
  const size_t off_bytes = static_cast<size_t>(lo_y) * row_bytes;
  const size_t total_bytes = static_cast<size_t>(hi_y - lo_y + 1) * row_bytes;
  blit_psram_linear_fast(reinterpret_cast<uint8_t *>(dst_fb) + off_bytes,
                         reinterpret_cast<const uint8_t *>(src_cache) + off_bytes, total_bytes);
}

bool ppa_srm_scale_rgb565(uint16_t *dst_fb, int dst_stride_px, int dst_x0, int dst_y0,
                          int dst_w, int dst_h, const uint16_t *src, int src_w, int src_h,
                          int crop_x0, int crop_y0, int crop_w, int crop_h,
                          bool sync_m2c) noexcept {
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

  const float sx = static_cast<float>(dst_w) / static_cast<float>(crop_w);
  const float sy = static_cast<float>(dst_h) / static_cast<float>(crop_h);
  if (sx < 0.0625f || sx > 16.0f || sy < 0.0625f || sy > 16.0f) return false;
  if (!ppa_srm_blit_init()) return false;

  uint32_t sx_q4 = std::max<uint32_t>(1U, static_cast<uint32_t>(sx * 16.0f));
  uint32_t sy_q4 = std::max<uint32_t>(1U, static_cast<uint32_t>(sy * 16.0f));
  auto scaled_dim = [](uint32_t q4, int in_len) noexcept -> uint32_t {
    const uint32_t i = q4 >> 4;
    const uint32_t f = q4 & 15U;
    return i * static_cast<uint32_t>(in_len) + (f * static_cast<uint32_t>(in_len)) / 16U;
  };
  uint32_t new_w = scaled_dim(sx_q4, crop_w);
  while (new_w > static_cast<uint32_t>(dst_w) && sx_q4 > 1U) {
    --sx_q4;
    new_w = scaled_dim(sx_q4, crop_w);
  }
  uint32_t new_h = scaled_dim(sy_q4, crop_h);
  while (new_h > static_cast<uint32_t>(dst_h) && sy_q4 > 1U) {
    --sy_q4;
    new_h = scaled_dim(sy_q4, crop_h);
  }
  if (new_w == 0U || new_w > static_cast<uint32_t>(dst_w) ||
      new_h == 0U || new_h > static_cast<uint32_t>(dst_h)) {
    return false;
  }

  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  const uintptr_t rem = reinterpret_cast<uintptr_t>(dst_fb) & kMask;
  if ((rem & 1U) != 0U) return false;
  const size_t pad_bytes = (rem == 0U) ? 0U : (kPsramCacheLineAlign - rem);
  const int pad_px = static_cast<int>(pad_bytes >> 1);

  enum class SrmTargetMode : uint8_t { DIRECT_ALIGNED, OFFSET_FB, STAGED };
  SrmTargetMode tmode = SrmTargetMode::STAGED;
  uint16_t *out_buf = nullptr;
  size_t out_buf_bytes = 0U;
  int out_pic_w = dst_stride_px;
  int out_pic_h = dst_y0 + dst_h;
  int out_bx0 = dst_x0;
  int out_by0 = dst_y0;
  const char *mode_name = "staged";

  if (rem == 0U) {
    tmode = SrmTargetMode::DIRECT_ALIGNED;
    out_buf = dst_fb;
    out_buf_bytes = align_up_psram(static_cast<size_t>(dst_stride_px) * static_cast<size_t>(out_pic_h) * sizeof(uint16_t));
    mode_name = "direct";
  } else {
    const size_t avail_bytes =
        (static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H) - static_cast<size_t>(pad_px)) * sizeof(uint16_t);
    const size_t aligned_avail = avail_bytes & ~kMask;
    const size_t req_out_len =
        static_cast<size_t>(SCREEN_W) * static_cast<size_t>(dst_y0 + dst_h) * sizeof(uint16_t);
    if (dst_stride_px == SCREEN_W && dst_x0 >= pad_px && req_out_len <= aligned_avail) {
      tmode = SrmTargetMode::OFFSET_FB;
      out_buf = dst_fb + pad_px;
      out_buf_bytes = aligned_avail;
      out_pic_w = SCREEN_W;
      out_pic_h = dst_y0 + dst_h;
      out_bx0 = dst_x0 - pad_px;
      out_by0 = dst_y0;
      mode_name = "offset_fb";
    } else {
      const size_t dst_bytes = static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t);
      uint16_t *const stage = ppa_srm_ensure_stage(dst_bytes);
      if (stage == nullptr) return false;
      tmode = SrmTargetMode::STAGED;
      out_buf = stage;
      out_buf_bytes = align_up_psram(dst_bytes);
      out_pic_w = dst_w;
      out_pic_h = dst_h;
      out_bx0 = 0;
      out_by0 = 0;
    }
  }

  auto &st = ppa_srm_blit_state();
  if (tmode == SrmTargetMode::OFFSET_FB || (tmode == SrmTargetMode::DIRECT_ALIGNED && dst_w < out_pic_w)) {
    ui_psram_cache_writeback(out_buf + static_cast<size_t>(out_by0) * static_cast<size_t>(out_pic_w),
                             static_cast<size_t>(out_pic_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t));
  }

  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = src;
  cfg.in.pic_w = static_cast<uint32_t>(src_w);
  cfg.in.pic_h = static_cast<uint32_t>(src_h);
  cfg.in.block_w = static_cast<uint32_t>(crop_w);
  cfg.in.block_h = static_cast<uint32_t>(crop_h);
  cfg.in.block_offset_x = static_cast<uint32_t>(crop_x0);
  cfg.in.block_offset_y = static_cast<uint32_t>(crop_y0);
  cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  cfg.out.buffer = out_buf;
  cfg.out.buffer_size = static_cast<uint32_t>(out_buf_bytes);
  cfg.out.pic_w = static_cast<uint32_t>(out_pic_w);
  cfg.out.pic_h = static_cast<uint32_t>(out_pic_h);
  cfg.out.block_offset_x = static_cast<uint32_t>(out_bx0);
  cfg.out.block_offset_y = static_cast<uint32_t>(out_by0);
  cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = static_cast<float>(sx_q4) * (1.0f / 16.0f);
  cfg.scale_y = static_cast<float>(sy_q4) * (1.0f / 16.0f);
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  const int64_t t0_us = esp_timer_get_time();
  const esp_err_t err = ppa_do_scale_rotate_mirror(st.ppa, &cfg);
  const int64_t t_srm_us = esp_timer_get_time();
  if (err != ESP_OK) {
    if (!st.scale_warned) {
      st.scale_warned = true;
      ESP_LOGW("disp_fb", "PSRAM PPA SRM scale failed (err=%d dst=%p %dx%d->%dx%d %s)",
               static_cast<int>(err), static_cast<const void *>(dst_fb), crop_w, crop_h, dst_w, dst_h, mode_name);
    }
    return false;
  }

  const bool need_edge_rep =
      (new_w < static_cast<uint32_t>(dst_w)) || (new_h < static_cast<uint32_t>(dst_h));
  if (tmode == SrmTargetMode::STAGED || sync_m2c || need_edge_rep) {
    ui_psram_cache_invalidate(out_buf + static_cast<size_t>(out_by0) * static_cast<size_t>(out_pic_w),
                              static_cast<size_t>(out_pic_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t));
  }
  if (need_edge_rep) {
    const int nw = static_cast<int>(new_w);
    const int nh = static_cast<int>(new_h);
    for (int y = 0; y < nh; ++y) {
      uint16_t *const row = out_buf + static_cast<size_t>(out_by0 + y) * static_cast<size_t>(out_pic_w) +
                            static_cast<size_t>(out_bx0);
      const uint16_t edge_px = row[nw - 1];
      for (int x = nw; x < dst_w; ++x) row[x] = edge_px;
    }
    if (nh < dst_h) {
      const uint16_t *const last_row = out_buf + static_cast<size_t>(out_by0 + nh - 1) * static_cast<size_t>(out_pic_w) +
                                       static_cast<size_t>(out_bx0);
      for (int y = nh; y < dst_h; ++y) {
        uint16_t *const row = out_buf + static_cast<size_t>(out_by0 + y) * static_cast<size_t>(out_pic_w) +
                              static_cast<size_t>(out_bx0);
        std::memcpy(row, last_row, static_cast<size_t>(dst_w) * sizeof(uint16_t));
      }
    }
    if ((tmode == SrmTargetMode::STAGED && dst_x0 == 0 && dst_w == dst_stride_px) ||
        (!sync_m2c && tmode != SrmTargetMode::STAGED)) {
      ui_psram_cache_writeback(out_buf + static_cast<size_t>(out_by0) * static_cast<size_t>(out_pic_w),
                               static_cast<size_t>(out_pic_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t));
    }
  }

  if (tmode == SrmTargetMode::STAGED) {
    const size_t dst_bytes = static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t);
    if (dst_x0 == 0 && dst_w == dst_stride_px) {
      blit_psram_linear_fast(
          reinterpret_cast<uint8_t *>(dst_fb + static_cast<size_t>(dst_y0) * static_cast<size_t>(dst_stride_px)),
          reinterpret_cast<const uint8_t *>(out_buf), dst_bytes);
    } else {
      for (int y = 0; y < dst_h; ++y) {
        std::memcpy(dst_fb + static_cast<size_t>(dst_y0 + y) * static_cast<size_t>(dst_stride_px) +
                        static_cast<size_t>(dst_x0),
                    out_buf + static_cast<size_t>(y) * static_cast<size_t>(dst_w),
                    static_cast<size_t>(dst_w) * sizeof(uint16_t));
      }
    }
  }

  const int64_t t_end_us = esp_timer_get_time();
  st.blits++;
  if (!st.scale_verified) {
    st.scale_verified = true;
    ESP_LOGI("disp_fb", "PSRAM PPA SRM scale verified: %dx%d -> %dx%d (%s) in %ld us (srm=%ldus)",
             crop_w, crop_h, dst_w, dst_h, mode_name,
             static_cast<long>(t_end_us - t0_us), static_cast<long>(t_srm_us - t0_us));
  }
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
  (void)sync_m2c;
  return false;
#endif
}

bool ppa_blend_uniform_dim_rgb565(uint16_t *dst_buf, const uint16_t *src_buf,
                                  int w, int h, int mul_q8) noexcept {
#if LATTER_FB_PPA && !LATTER_ROUND_DISPLAY
  if (dst_buf == nullptr || w <= 0 || h <= 0 || mul_q8 < 0 || mul_q8 >= 256) return false;
  const uint16_t *const in_bg = (src_buf != nullptr) ? src_buf : dst_buf;
  const int endian = active_frame_endian_mode();
  if (endian != 1 && endian != 3) return false;
  if (!ppa_blend_wash_init()) return false;

  auto &st = ppa_blend_wash_state();
  if (!st.uniform_ok) return false;
  constexpr uintptr_t kMask = static_cast<uintptr_t>(kPsramCacheLineAlign - 1U);
  if ((reinterpret_cast<uintptr_t>(dst_buf) & kMask) != 0U) {
    if (!st.uniform_warned) {
      st.uniform_warned = true;
      ESP_LOGW("disp_fb", "PSRAM PPA Blend uniform dim skipped: dst=%p unaligned (rem=%u)",
               static_cast<const void *>(dst_buf),
               static_cast<unsigned>(reinterpret_cast<uintptr_t>(dst_buf) & kMask));
    }
    return false;
  }

  const size_t px_count = static_cast<size_t>(w) * static_cast<size_t>(h);
  const size_t max_mask_px = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
  const size_t total_bytes = px_count * sizeof(uint16_t);
  if (px_count > max_mask_px || total_bytes < 32768U || (total_bytes % kPpaCopyRowBytes) != 0U) {
    if (!st.uniform_warned) {
      st.uniform_warned = true;
      ESP_LOGW("disp_fb", "PSRAM PPA Blend uniform dim skipped: %dx%d (%u bytes) unsupported",
               w, h, static_cast<unsigned>(total_bytes));
    }
    return false;
  }
  const size_t rows = total_bytes / kPpaCopyRowBytes;
  if (rows == 0U || rows > kFbPpaMaxRows) return false;

  uint8_t *const alpha_mask = (st.alpha_mask != nullptr) ? st.alpha_mask : ppa_blend_wash_ensure_mask(0);
  if (alpha_mask == nullptr) return false;

  ppa_blend_oper_config_t bcfg = {};
  bcfg.in_bg.buffer = in_bg;
  bcfg.in_bg.pic_w = kPpaCopyRowPx;
  bcfg.in_bg.pic_h = static_cast<uint32_t>(rows);
  bcfg.in_bg.block_w = kPpaCopyRowPx;
  bcfg.in_bg.block_h = static_cast<uint32_t>(rows);
  bcfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

  bcfg.in_fg.buffer = alpha_mask;
  bcfg.in_fg.pic_w = kPpaCopyRowPx;
  bcfg.in_fg.pic_h = static_cast<uint32_t>(rows);
  bcfg.in_fg.block_w = kPpaCopyRowPx;
  bcfg.in_fg.block_h = static_cast<uint32_t>(rows);
  bcfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;

  bcfg.out.buffer = dst_buf;
  bcfg.out.buffer_size = static_cast<uint32_t>(total_bytes);
  bcfg.out.pic_w = kPpaCopyRowPx;
  bcfg.out.pic_h = static_cast<uint32_t>(rows);
  bcfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

  bcfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  bcfg.bg_alpha_fix_val = 255U;
  bcfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  bcfg.fg_alpha_fix_val = static_cast<uint32_t>(std::clamp(256 - mul_q8, 0, 255));
  bcfg.fg_fix_rgb_val.r = 0U;
  bcfg.fg_fix_rgb_val.g = 0U;
  bcfg.fg_fix_rgb_val.b = 0U;
  bcfg.mode = PPA_TRANS_MODE_BLOCKING;

  const int64_t t0_us = esp_timer_get_time();
  const esp_err_t blend_err = ppa_do_blend(st.ppa, &bcfg);
  const int64_t t_blend_us = esp_timer_get_time();
  if (blend_err != ESP_OK) {
    st.uniform_ok = false;
    ESP_LOGW("disp_fb", "PSRAM PPA Blend uniform dim failed (err=%d); disabling", static_cast<int>(blend_err));
    return false;
  }
  const esp_err_t post_err = esp_cache_msync(dst_buf, total_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  const int64_t t_post_us = esp_timer_get_time();
  if (post_err != ESP_OK) {
    st.uniform_ok = false;
    ESP_LOGW("disp_fb", "PSRAM PPA Blend uniform dim post-msync failed (err=%d); disabling", static_cast<int>(post_err));
    return false;
  }
  st.washes++;
  if (!st.uniform_verified) {
    st.uniform_verified = true;
    ESP_LOGI("disp_fb",
             "PSRAM PPA Blend uniform dim verified: %u KB (%dx%d q8=%d, %s) in %ld us (blend=%ldus post=%ldus)",
             static_cast<unsigned>(total_bytes / 1024U), w, h, mul_q8,
             (in_bg == dst_buf) ? "in-place" : "direct",
             static_cast<long>(t_post_us - t0_us),
             static_cast<long>(t_blend_us - t0_us),
             static_cast<long>(t_post_us - t_blend_us));
  }
  return true;
#else
  (void)dst_buf;
  (void)src_buf;
  (void)w;
  (void)h;
  (void)mul_q8;
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
static constexpr float kSrgbLinearLut[256] = {
    0.0f, 0.00030352698f, 0.00060705397f, 0.00091058095f, 0.0012141079f, 0.0015176349f, 0.0018211619f, 0.0021246889f,
    0.0024282159f, 0.0027317429f, 0.0030352698f, 0.0033465358f, 0.0036765073f, 0.004024717f, 0.004391442f, 0.0047769535f,
    0.0051815167f, 0.0056053916f, 0.006048833f, 0.0065120908f, 0.0069954102f, 0.007499032f, 0.008023193f, 0.0085681256f,
    0.0091340587f, 0.0097212173f, 0.010329823f, 0.010960094f, 0.011612245f, 0.012286488f, 0.012983032f, 0.013702083f,
    0.014443844f, 0.015208514f, 0.015996293f, 0.016807376f, 0.017641954f, 0.01850022f, 0.019382361f, 0.020288563f,
    0.02121901f, 0.022173885f, 0.023153366f, 0.024157632f, 0.02518686f, 0.026241222f, 0.027320892f, 0.02842604f,
    0.029556834f, 0.030713444f, 0.031896033f, 0.033104767f, 0.034339807f, 0.035601315f, 0.03688945f, 0.038204372f,
    0.039546235f, 0.040915197f, 0.042311411f, 0.043735029f, 0.045186204f, 0.046665086f, 0.048171824f, 0.049706566f,
    0.051269458f, 0.052860647f, 0.054480276f, 0.05612849f, 0.05780543f, 0.059511238f, 0.061246054f, 0.063010018f,
    0.064803267f, 0.066625939f, 0.06847817f, 0.070360096f, 0.072271851f, 0.074213568f, 0.076185381f, 0.078187422f,
    0.08021982f, 0.082282707f, 0.084376212f, 0.086500462f, 0.088655586f, 0.090841711f, 0.093058963f, 0.095307467f,
    0.097587347f, 0.099898728f, 0.10224173f, 0.10461648f, 0.1070231f, 0.10946171f, 0.11193243f, 0.11443537f,
    0.11697067f, 0.11953843f, 0.12213877f, 0.12477182f, 0.12743768f, 0.13013648f, 0.13286832f, 0.13563333f,
    0.13843162f, 0.14126329f, 0.14412847f, 0.14702727f, 0.14995979f, 0.15292615f, 0.15592646f, 0.15896084f,
    0.16202938f, 0.16513219f, 0.1682694f, 0.1714411f, 0.1746474f, 0.17788842f, 0.18116424f, 0.18447499f,
    0.18782077f, 0.19120168f, 0.19461783f, 0.19806932f, 0.20155625f, 0.20507874f, 0.20863687f, 0.21223076f,
    0.2158605f, 0.2195262f, 0.22322796f, 0.22696587f, 0.23074005f, 0.23455058f, 0.23839757f, 0.24228112f,
    0.24620133f, 0.25015828f, 0.25415209f, 0.25818285f, 0.26225066f, 0.2663556f, 0.27049779f, 0.27467731f,
    0.27889426f, 0.28314874f, 0.28744084f, 0.29177065f, 0.29613827f, 0.30054379f, 0.30498731f, 0.30946892f,
    0.31398871f, 0.31854678f, 0.32314321f, 0.3277781f, 0.33245154f, 0.33716362f, 0.34191442f, 0.34670406f,
    0.3515326f, 0.35640014f, 0.36130678f, 0.3662526f, 0.37123768f, 0.37626212f, 0.38132601f, 0.38642943f,
    0.39157248f, 0.39675523f, 0.40197778f, 0.40724021f, 0.41254261f, 0.41788507f, 0.42326767f, 0.4286905f,
    0.43415364f, 0.43965717f, 0.44520119f, 0.45078578f, 0.45641102f, 0.462077f, 0.4677838f, 0.4735315f,
    0.47932018f, 0.48514994f, 0.49102085f, 0.496933f, 0.50288646f, 0.50888132f, 0.51491767f, 0.52099557f,
    0.52711513f, 0.5332764f, 0.53947949f, 0.54572446f, 0.5520114f, 0.55834039f, 0.56471151f, 0.57112483f,
    0.57758044f, 0.58407842f, 0.59061884f, 0.59720179f, 0.60382734f, 0.61049557f, 0.61720656f, 0.62396039f,
    0.63075714f, 0.63759687f, 0.64447968f, 0.65140564f, 0.65837482f, 0.6653873f, 0.67244316f, 0.67954247f,
    0.68668531f, 0.69387176f, 0.70110189f, 0.70837578f, 0.7156935f, 0.72305513f, 0.73046074f, 0.73791041f,
    0.74540421f, 0.75294222f, 0.7605245f, 0.76815115f, 0.77582222f, 0.78353779f, 0.79129794f, 0.79910274f,
    0.80695226f, 0.81484657f, 0.82278575f, 0.83076988f, 0.83879901f, 0.84687323f, 0.85499261f, 0.86315721f,
    0.87136712f, 0.8796224f, 0.88792312f, 0.89626935f, 0.90466117f, 0.91309865f, 0.92158186f, 0.93011086f,
    0.93868573f, 0.94730654f, 0.95597335f, 0.96468625f, 0.97344529f, 0.98225055f, 0.9911021f, 1.0f,
};

static constexpr uint16_t kLumR5Q15[32] = {
    0, 17, 36, 64, 106, 154, 214, 285, 380, 477, 588, 712, 869, 1024, 1194, 1380,
    1607, 1827, 2063, 2316, 2621, 2911, 3219, 3545, 3934, 4300, 4685, 5089, 5567, 6013, 6480, 6966,
};

static constexpr uint16_t kLumG6Q15[64] = {
    0, 28, 57, 86, 121, 164, 214, 272, 339, 413, 497, 590, 693, 805, 927, 1059,
    1239, 1395, 1561, 1739, 1928, 2129, 2341, 2565, 2801, 3050, 3311, 3584, 3870, 4169, 4481, 4806,
    5231, 5587, 5956, 6339, 6736, 7148, 7573, 8013, 8467, 8937, 9421, 9920, 10434, 10963, 11507, 12067,
    12789, 13385, 13996, 14623, 15266, 15926, 16601, 17293, 18002, 18728, 19470, 20229, 21005, 21798, 22608, 23436,
};

static constexpr uint16_t kLumB5Q15[32] = {
    0, 6, 12, 22, 36, 52, 73, 97, 129, 162, 200, 242, 295, 348, 406, 469,
    546, 620, 701, 787, 890, 989, 1093, 1204, 1336, 1460, 1591, 1728, 1891, 2042, 2201, 2366,
};

const float *srgb_linear_lut() {
  return kSrgbLinearLut;
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

namespace {

template <int Mode>
GlassBackdrop sample_glass_backdrop_mode(const uint16_t *fb, int xs, int xe, int ys, int ye, int w, int h) {
  GlassBackdrop out;
  const int step_x = std::max(4, w / 6);
  const int step_y = std::max(3, h / 6);
  const int x_start = xs + std::min(step_x / 2, (xe - xs - 1) / 2);
  const int y_start = ys + std::min(step_y / 2, (ye - ys - 1) / 2);
  uint16_t cnt[32] = {0};
  uint32_t sr[32] = {0}, sg[32] = {0}, sb[32] = {0};
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = y_start; y < ye; y += step_y) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = x_start; x < xe; x += step_x) {
      const uint16_t p = decode_hw_rgb565_mode<Mode>(row[x]);
      const uint32_t r5 = (p >> 11) & 0x1FU;
      const uint32_t g6 = (p >> 5) & 0x3FU;
      const uint32_t b5 = p & 0x1FU;
      const uint32_t r = (r5 << 3) | (r5 >> 2);
      const uint32_t g = (g6 << 2) | (g6 >> 4);
      const uint32_t b = (b5 << 3) | (b5 >> 2);
      const uint32_t lum_q15 = static_cast<uint32_t>(kLumR5Q15[r5]) +
                               static_cast<uint32_t>(kLumG6Q15[g6]) +
                               static_cast<uint32_t>(kLumB5Q15[b5]);
      const uint32_t bin = std::min<uint32_t>(31U, lum_q15 >> 10);
      cnt[bin]++;
      sr[bin] += r;
      sg[bin] += g;
      sb[bin] += b;
      tr += r;
      tg += g;
      tb += b;
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

template <int Mode>
GlassBackdrop sample_glass_mean_mode(const uint16_t *fb, int xs, int xe, int ys, int ye, int w, int h) {
  GlassBackdrop out;
  const int step = std::max(4, std::max(w, h) / 6);
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t p = decode_hw_rgb565_mode<Mode>(row[x]);
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

}  // namespace

GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  if (!backdrop_active() || w <= 0 || h <= 0 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0);
  const int xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, std::max(clip_y0(), y0));
  const int ye = std::min(SCREEN_H, std::min(clip_y1() + 1, y0 + h));
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) return sample_glass_backdrop_mode<1>(fb, xs, xe, ys, ye, w, h);
  if (mode == 2) return sample_glass_backdrop_mode<2>(fb, xs, xe, ys, ye, w, h);
  if (mode == 4) return sample_glass_backdrop_mode<4>(fb, xs, xe, ys, ye, w, h);
  return sample_glass_backdrop_mode<3>(fb, xs, xe, ys, ye, w, h);
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
  if (mode == 1 || mode < 0) return sample_glass_mean_mode<1>(fb, xs, xe, ys, ye, w, h);
  if (mode == 2) return sample_glass_mean_mode<2>(fb, xs, xe, ys, ye, w, h);
  if (mode == 4) return sample_glass_mean_mode<4>(fb, xs, xe, ys, ye, w, h);
  return sample_glass_mean_mode<3>(fb, xs, xe, ys, ye, w, h);
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

namespace {

inline UiColor resolve_glass_contrast_fg(UiColor ref_text) noexcept {
  const float raw_fg_lum = relative_luminance(ref_text);
  return ((static_cast<int>(ref_text.r) + static_cast<int>(ref_text.g) + static_cast<int>(ref_text.b)) < 48)
             ? kChromeGlassSheen
             : ((raw_fg_lum < 0.18f) ? kChromeMuted : ref_text);
}

inline UiColor resolve_glass_clear_tint(UiColor tint, int explicit_alpha_q8,
                                      int &out_a_top, int &out_a_bot) noexcept {
  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const int frost_q8 = dark_tint ? (is_semantic_glass_color(tint) ? 32 : 54) : 0;
  const UiColor clear_tint = (frost_q8 > 0) ? mix_color_q8(tint, kChromeHomePill, frost_q8) : tint;
  const int base_a = (explicit_alpha_q8 >= 0) ? explicit_alpha_q8 : kGlassClearAlphaQ8;
  const int da = dark_tint ? 12 : 0;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(0, base_a - (da * 2) / 3);
  return clear_tint;
}

inline bool glass_clear_safe_for_dim_q8(int dim_q8, UiColor clear_tint, int clear_a_top,
                                        int clear_a_bot, UiColor ref_text) noexcept {
  if (dim_q8 < 0 || dim_q8 >= 256) return false;
  const int ceil_v_i = std::min(255, ((255 * dim_q8 + 255) >> 8) + 2);
  const uint8_t ceil_v = static_cast<uint8_t>(ceil_v_i);
  const UiColor ceil_c{ceil_v, ceil_v, ceil_v};
  const float fg_lum = relative_luminance(resolve_glass_contrast_fg(ref_text));
  if (fg_lum <= relative_luminance(ceil_c)) return false;
  return (contrast_ratio(fg_lum, relative_luminance(mix_color_q8(ceil_c, clear_tint, clear_a_top))) >=
          kGlassTextContrast) &&
         (contrast_ratio(fg_lum, relative_luminance(mix_color_q8(ceil_c, clear_tint, clear_a_bot))) >=
          kGlassTextContrast);
}

}  // namespace

void resolve_glass_fill(const GlassBackdrop &bd, UiColor tint, UiColor ref_text,
                        UiColor &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8,
                        GlassVariant variant) {
  const uint8_t g_style = active_glass_style_raw();
  const bool is_readable_dark = (variant == GlassVariant::ReadableDark) || (g_style == 2U);

  const UiColor contrast_fg = resolve_glass_contrast_fg(ref_text);
  const float fg_lum = relative_luminance(contrast_fg);

  if (!is_readable_dark) {
    int clear_a_top = 0, clear_a_bot = 0;
    const UiColor clear_tint = resolve_glass_clear_tint(tint, explicit_alpha_q8, clear_a_top, clear_a_bot);
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
  bool need_bd = (eff_var == GlassVariant::ReadableDark) || (g_style == 0U && explicit_alpha_q8 < 0);
  if (need_bd && eff_var == GlassVariant::Clear && g_style == 0U && explicit_alpha_q8 < 0 &&
      !sample_surface && backdrop_active()) {
    int clear_a_top = 0, clear_a_bot = 0;
    const UiColor clear_tint = resolve_glass_clear_tint(norm_tint, explicit_alpha_q8, clear_a_top, clear_a_bot);
    if (glass_clear_safe_for_dim_q8(backdrop_max_lum_q8(), clear_tint, clear_a_top, clear_a_bot, ref_text)) {
      need_bd = false;
    }
  }
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

constexpr int kMaxCornerCacheRows = 40;

struct GlassCornerRow {
  float dy{0.0f};
  int16_t s0{0}, s1{-1}, e0{0}, e1{-1};
  bool hit{false};
  bool valid{false};
};

struct GlassRimmedCornerRow {
  float dy{0.0f}, idy{0.0f}, fdy{0.0f};
  int16_t s0{0}, s1{-1}, e0{0}, e1{-1};
  int16_t hs0{1}, hs1{0}, ie0{0}, ie1{-1};
  int16_t fs0{1}, fs1{0}, fe0{0}, fe1{-1};
  bool o_hit{false};
  bool in_hit{false};
  bool f_hit{false};
  bool valid{false};
};

inline void build_bayer_row_bases(const uint8_t *th, uint32_t tr_a, uint32_t tg_a, uint32_t tb_a,
                                  uint32_t base_rb[4], uint32_t base_g_s5[4]) noexcept {
  for (int k = 0; k < 4; ++k) {
    const uint32_t tk = static_cast<uint32_t>(th[k]) * 16U + 8U;
    base_rb[k] = ((tr_a + tk) << 16) | (tb_a + tk);
    base_g_s5[k] = (tg_a + tk) << 5;
  }
}

template <int Mode>
inline void glass_mix_px_mode(uint16_t &dst, uint32_t tr, uint32_t tg, uint32_t tb, int a, int t) noexcept {
  const uint16_t bg = decode_hw_rgb565_mode<Mode>(dst);
  const uint32_t ia = static_cast<uint32_t>(256 - a);
  const uint32_t ua = static_cast<uint32_t>(a);
  uint32_t r = ((tr * ua) >> 8) + ((bg >> 11) & 0x1FU) * ia + static_cast<uint32_t>(t);
  uint32_t g = ((tg * ua) >> 8) + ((bg >> 5) & 0x3FU) * ia + static_cast<uint32_t>(t);
  uint32_t b = ((tb * ua) >> 8) + (bg & 0x1FU) * ia + static_cast<uint32_t>(t);
  r >>= 8;
  g >>= 8;
  b >>= 8;
  if (r > 31U) r = 31U;
  if (g > 63U) g = 63U;
  if (b > 31U) b = 31U;
  dst = encode_hw_rgb565_mode<Mode>(static_cast<uint16_t>((r << 11) | (g << 5) | b));
}

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

  bool ppa_core = false;
  int ppa_x0 = 0, ppa_x1 = -1, ppa_y0 = 0, ppa_y1 = -1;
#if LATTER_FB_PPA
  if constexpr (Mode == 1 || Mode == 3) {
    if (hole == 0 && (a_top > 0 || a_bot > 0)) {
      const int r_ceil = static_cast<int>(std::ceil(o.rad));
      ppa_x0 = std::max(0, x0 + r_ceil + 1);
      ppa_x1 = std::min(SCREEN_W - 1, x0 + w - 2 - r_ceil);
      ppa_y0 = std::max(ys, y0 + 1);
      ppa_y1 = std::min(ye - 1, y0 + h - 2);
      if (ppa_x1 >= ppa_x0 && ppa_y1 >= ppa_y0) {
        ppa_core = ppa_blend_glass_rect_rgb565(fb, ppa_x0, ppa_x1, ppa_y0, ppa_y1,
                                               y0, h, tint, a_top, a_bot, Mode);
      }
    }
  }
#endif

  const uint32_t tr = (static_cast<uint32_t>(tint.r) * 31U * 256U + 127U) / 255U;
  const uint32_t tg = (static_cast<uint32_t>(tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t tb = (static_cast<uint32_t>(tint.b) * 31U * 256U + 127U) / 255U;
  GlassCornerRow corner_cache[kMaxCornerCacheRows]{};
  const int max_rad_rows = static_cast<int>(std::ceil(o.rad));
  const bool flat_alpha = (a_top == a_bot);
  uint32_t flat_tr_a = 0U, flat_tg_a = 0U, flat_tb_a = 0U;
  if (flat_alpha && a_top > 0) {
    const uint32_t ua = static_cast<uint32_t>(std::clamp(a_top, 0, 256));
    flat_tr_a = (tr * ua) >> 8;
    flat_tg_a = (tg * ua) >> 8;
    flat_tb_a = (tb * ua) >> 8;
  }

  for (int py = ys; py < ye; py++) {
    const int k_sym = std::min(py - y0, (y0 + h - 1) - py);
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    bool o_hit = false;
    if (k_sym < max_rad_rows && k_sym < kMaxCornerCacheRows) {
      GlassCornerRow &c = corner_cache[k_sym];
      int s0_l = 0, s1_l = -1, e0_l = 0, e1_l = -1;
      if (!c.valid) {
        c.hit = o.row(py, c.dy, s0_l, s1_l, e0_l, e1_l);
        c.s0 = static_cast<int16_t>(s0_l);
        c.s1 = static_cast<int16_t>(s1_l);
        c.e0 = static_cast<int16_t>(e0_l);
        c.e1 = static_cast<int16_t>(e1_l);
        c.valid = true;
      }
      o_hit = c.hit;
      dy = c.dy;
      s0 = c.s0;
      s1 = c.s1;
      e0 = c.e0;
      e1 = c.e1;
    } else {
      o_hit = o.row(py, dy, s0, s1, e0, e1);
    }
    if (!o_hit) continue;
    const int a = std::clamp((h > 1) ? (a_top + ((a_bot - a_top) * (py - y0)) / (h - 1)) : a_top, 0, 256);
    if (a <= 0) continue;
    const uint8_t *th = kBayer4[py & 3];
    uint16_t *row = fb + static_cast<size_t>(py) * SCREEN_W;
    if (hole == 1 && has_hole && dy == 0.0f && py > y0 && py < (y0 + h - 1)) {
      if (x0 >= 0 && x0 < SCREEN_W) {
        glass_mix_px_mode<Mode>(row[x0], tr, tg, tb, a, th[x0 & 3] * 16 + 8);
      }
      if (o.x_last >= 0 && o.x_last < SCREEN_W && o.x_last != x0) {
        glass_mix_px_mode<Mode>(row[o.x_last], tr, tg, tb, a, th[o.x_last & 3] * 16 + 8);
      }
      continue;
    }
    const uint32_t ua = static_cast<uint32_t>(a);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t tr_a = flat_alpha ? flat_tr_a : ((tr * ua) >> 8);
    const uint32_t tg_a = flat_alpha ? flat_tg_a : ((tg * ua) >> 8);
    const uint32_t tb_a = flat_alpha ? flat_tb_a : ((tb * ua) >> 8);
    uint32_t base_rb[4], base_g_s5[4];
    build_bayer_row_bases(th, tr_a, tg_a, tb_a, base_rb, base_g_s5);
    auto span = [&](int xa, int xb) {
      xa = std::max(xa, 0);
      xb = std::min(xb, SCREEN_W - 1);
      if (xa <= xb) blend_swar_q8_span<Mode>(row, row, xa, xb, ia, base_rb, base_g_s5);
    };
    auto edge = [&](int px) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = o.coverage(px, dy);
      if (cov <= 0) return;
      const int ae = (a * (cov + (cov >> 7)) + 128) >> 8;
      if (ae > 0) glass_mix_px_mode<Mode>(row[px], tr, tg, tb, std::min(256, ae), th[px & 3] * 16 + 8);
    };
    auto inner_edge = [&](int px, float idy) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = 255 - in.coverage(px, idy);
      if (cov <= 0) return;
      const int ae = (a * (cov + (cov >> 7)) + 128) >> 8;
      if (ae > 0) glass_mix_px_mode<Mode>(row[px], tr, tg, tb, std::min(256, ae), th[px & 3] * 16 + 8);
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
      } else if (ppa_core && py >= ppa_y0 && py <= ppa_y1) {
        if (s0 < ppa_x0) span(s0, std::min(s1, ppa_x0 - 1));
        if (s1 > ppa_x1) span(std::max(s0, ppa_x1 + 1), s1);
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

template <int Mode>
void fill_round_rect_glass_rimmed_mode(uint16_t *fb, int x0, int y0, int w, int h, float rad,
                                       UiColor rim_tint, int rim_top, int rim_bot,
                                       UiColor fill_tint, int fill_top, int fill_bot,
                                       float rad_in_override) {
  const int ys = std::max(clip_y0(), y0);
  const int ye = std::min(clip_y1() + 1, y0 + h);
  if (ye <= ys) return;

  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  const bool has_in = (w > 2 && h > 2);
  const int ix0 = x0 + 1;
  const int iy0 = y0 + 1;
  const int iw = w - 2;
  const int ih = h - 2;
  const float rim_in_rad = rad - 1.0f;
  const float fill_in_rad = (rad_in_override >= 0.0f) ? rad_in_override : rim_in_rad;
  AaRoundRect in_rim;
  AaRoundRect in_fill;
  bool same_in_geom = true;
  if (has_in) {
    in_rim.init(ix0, iy0, iw, ih, rim_in_rad);
    in_fill.init(ix0, iy0, iw, ih, fill_in_rad);
    same_in_geom = (in_rim.rad == in_fill.rad);
  }

  bool ppa_core = false;
  int ppa_x0 = 0, ppa_x1 = -1, ppa_y0 = 0, ppa_y1 = -1;
#if LATTER_FB_PPA
  if constexpr (Mode == 1 || Mode == 3) {
    if (has_in && (fill_top > 0 || fill_bot > 0)) {
      const int r_ceil = static_cast<int>(std::ceil(in_fill.rad));
      ppa_x0 = std::max(0, ix0 + r_ceil + 1);
      ppa_x1 = std::min(SCREEN_W - 1, ix0 + iw - 2 - r_ceil);
      ppa_y0 = std::max(std::max(ys, iy0 + 1), iy0);
      ppa_y1 = std::min(ye - 1, iy0 + ih - 2);
      if (ppa_x1 >= ppa_x0 && ppa_y1 >= ppa_y0) {
        ppa_core = ppa_blend_glass_rect_rgb565(fb, ppa_x0, ppa_x1, ppa_y0, ppa_y1,
                                               iy0, ih, fill_tint, fill_top, fill_bot, Mode);
      }
    }
  }
#endif

  const uint32_t rr = (static_cast<uint32_t>(rim_tint.r) * 31U * 256U + 127U) / 255U;
  const uint32_t rg = (static_cast<uint32_t>(rim_tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t rb = (static_cast<uint32_t>(rim_tint.b) * 31U * 256U + 127U) / 255U;
  const uint32_t fr = (static_cast<uint32_t>(fill_tint.r) * 31U * 256U + 127U) / 255U;
  const uint32_t fg = (static_cast<uint32_t>(fill_tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t fb_c = (static_cast<uint32_t>(fill_tint.b) * 31U * 256U + 127U) / 255U;

  GlassRimmedCornerRow corner_cache[kMaxCornerCacheRows]{};
  const float max_r = has_in ? std::max(o.rad, 1.0f + std::max(in_rim.rad, in_fill.rad)) : o.rad;
  const int r_straight = static_cast<int>(std::ceil(max_r - 0.5f)) + 1;
  const int x_fill_lo = std::max(0, ix0);
  const int x_fill_hi = std::min(SCREEN_W - 1, ix0 + iw - 1);

  for (int py = ys; py < ye; ++py) {
    const int a_rim = std::clamp((h > 1) ? (rim_top + ((rim_bot - rim_top) * (py - y0)) / (h - 1)) : rim_top, 0, 256);
    const int a_fill = (has_in && py >= iy0 && py < (iy0 + ih))
                           ? std::clamp((ih > 1) ? (fill_top + ((fill_bot - fill_top) * (py - iy0)) / (ih - 1)) : fill_top, 0, 256)
                           : 0;
    if (a_rim <= 0 && a_fill <= 0) continue;

    const int k_sym = std::min(py - y0, (y0 + h - 1) - py);
    const uint8_t *th = kBayer4[py & 3];
    uint16_t *row = fb + static_cast<size_t>(py) * SCREEN_W;

    if (k_sym >= r_straight && py > y0 && py < (y0 + h - 1)) {
      if (a_rim > 0) {
        if (x0 >= 0 && x0 < SCREEN_W) {
          glass_mix_px_mode<Mode>(row[x0], rr, rg, rb, a_rim, th[x0 & 3] * 16 + 8);
        }
        if (o.x_last >= 0 && o.x_last < SCREEN_W && o.x_last != x0) {
          glass_mix_px_mode<Mode>(row[o.x_last], rr, rg, rb, a_rim, th[o.x_last & 3] * 16 + 8);
        }
      }
      if (a_fill > 0 && x_fill_lo <= x_fill_hi) {
        const uint32_t ua = static_cast<uint32_t>(a_fill);
        const uint32_t ia = static_cast<uint32_t>(256 - a_fill);
        uint32_t base_rb[4], base_g_s5[4];
        build_bayer_row_bases(th, (fr * ua) >> 8, (fg * ua) >> 8, (fb_c * ua) >> 8, base_rb, base_g_s5);
        if (ppa_core && py >= ppa_y0 && py <= ppa_y1) {
          if (x_fill_lo < ppa_x0) {
            blend_swar_q8_span<Mode>(row, row, x_fill_lo, std::min(x_fill_hi, ppa_x0 - 1), ia, base_rb, base_g_s5);
          }
          if (x_fill_hi > ppa_x1) {
            blend_swar_q8_span<Mode>(row, row, std::max(x_fill_lo, ppa_x1 + 1), x_fill_hi, ia, base_rb, base_g_s5);
          }
        } else {
          blend_swar_q8_span<Mode>(row, row, x_fill_lo, x_fill_hi, ia, base_rb, base_g_s5);
        }
      }
      continue;
    }

    GlassRimmedCornerRow local_cr{};
    GlassRimmedCornerRow *cr = &local_cr;
    if (k_sym < kMaxCornerCacheRows) {
      cr = &corner_cache[k_sym];
    }
    if (!cr->valid) {
      int os0 = 0, os1 = -1, oe0 = 0, oe1 = -1;
      cr->o_hit = o.row(py, cr->dy, os0, os1, oe0, oe1);
      cr->s0 = static_cast<int16_t>(os0);
      cr->s1 = static_cast<int16_t>(os1);
      cr->e0 = static_cast<int16_t>(oe0);
      cr->e1 = static_cast<int16_t>(oe1);
      if (has_in) {
        int rhs0 = 1, rhs1 = 0, rie0 = 0, rie1 = -1;
        cr->in_hit = in_rim.row(py, cr->idy, rhs0, rhs1, rie0, rie1);
        cr->hs0 = static_cast<int16_t>(rhs0);
        cr->hs1 = static_cast<int16_t>(rhs1);
        cr->ie0 = static_cast<int16_t>(rie0);
        cr->ie1 = static_cast<int16_t>(rie1);
        if (same_in_geom) {
          cr->f_hit = cr->in_hit;
          cr->fdy = cr->idy;
          cr->fs0 = cr->hs0;
          cr->fs1 = cr->hs1;
          cr->fe0 = cr->ie0;
          cr->fe1 = cr->ie1;
        } else {
          int fhs0 = 1, fhs1 = 0, fie0 = 0, fie1 = -1;
          cr->f_hit = in_fill.row(py, cr->fdy, fhs0, fhs1, fie0, fie1);
          cr->fs0 = static_cast<int16_t>(fhs0);
          cr->fs1 = static_cast<int16_t>(fhs1);
          cr->fe0 = static_cast<int16_t>(fie0);
          cr->fe1 = static_cast<int16_t>(fie1);
        }
      }
      cr->valid = true;
    }

    if (cr->o_hit && a_rim > 0) {
      if (has_in && cr->dy == 0.0f && py > y0 && py < (y0 + h - 1)) {
        if (x0 >= 0 && x0 < SCREEN_W) {
          glass_mix_px_mode<Mode>(row[x0], rr, rg, rb, a_rim, th[x0 & 3] * 16 + 8);
        }
        if (o.x_last >= 0 && o.x_last < SCREEN_W && o.x_last != x0) {
          glass_mix_px_mode<Mode>(row[o.x_last], rr, rg, rb, a_rim, th[o.x_last & 3] * 16 + 8);
        }
      } else {
        const uint32_t ua = static_cast<uint32_t>(a_rim);
        const uint32_t ia = static_cast<uint32_t>(256 - a_rim);
        uint32_t r_base_rb[4], r_base_g_s5[4];
        build_bayer_row_bases(th, (rr * ua) >> 8, (rg * ua) >> 8, (rb * ua) >> 8, r_base_rb, r_base_g_s5);
        auto r_span = [&](int xa, int xb) {
          xa = std::max(xa, 0);
          xb = std::min(xb, SCREEN_W - 1);
          if (xa <= xb) blend_swar_q8_span<Mode>(row, row, xa, xb, ia, r_base_rb, r_base_g_s5);
        };
        auto r_edge = [&](int px) {
          if (px < 0 || px >= SCREEN_W) return;
          const int cov = o.coverage(px, cr->dy);
          if (cov <= 0) return;
          const int ae = (a_rim * (cov + (cov >> 7)) + 128) >> 8;
          if (ae > 0) glass_mix_px_mode<Mode>(row[px], rr, rg, rb, std::min(256, ae), th[px & 3] * 16 + 8);
        };
        if (cr->s1 >= cr->s0) {
          if (cr->in_hit && cr->hs1 >= cr->hs0) {
            r_span(cr->s0, std::min<int>(cr->s1, cr->hs0 - 1));
            r_span(std::max<int>(cr->s0, cr->hs1 + 1), cr->s1);
          } else {
            r_span(cr->s0, cr->s1);
          }
          for (int px = cr->e0; px < cr->s0; ++px) r_edge(px);
          for (int px = cr->s1 + 1; px <= cr->e1; ++px) r_edge(px);
        } else {
          for (int px = cr->e0; px <= cr->e1; ++px) r_edge(px);
        }
      }
    }

    if (cr->f_hit && a_fill > 0) {
      const uint32_t ua = static_cast<uint32_t>(a_fill);
      const uint32_t ia = static_cast<uint32_t>(256 - a_fill);
      uint32_t f_base_rb[4], f_base_g_s5[4];
      build_bayer_row_bases(th, (fr * ua) >> 8, (fg * ua) >> 8, (fb_c * ua) >> 8, f_base_rb, f_base_g_s5);
      auto f_span = [&](int xa, int xb) {
        xa = std::max(xa, 0);
        xb = std::min(xb, SCREEN_W - 1);
        if (xa <= xb) blend_swar_q8_span<Mode>(row, row, xa, xb, ia, f_base_rb, f_base_g_s5);
      };
      auto f_edge = [&](int px) {
        if (px < 0 || px >= SCREEN_W) return;
        const int cov = in_fill.coverage(px, cr->fdy);
        if (cov <= 0) return;
        const int ae = (a_fill * (cov + (cov >> 7)) + 128) >> 8;
        if (ae > 0) glass_mix_px_mode<Mode>(row[px], fr, fg, fb_c, std::min(256, ae), th[px & 3] * 16 + 8);
      };
      if (cr->fs1 >= cr->fs0) {
        if (ppa_core && py >= ppa_y0 && py <= ppa_y1) {
          if (cr->fs0 < ppa_x0) f_span(cr->fs0, std::min<int>(cr->fs1, ppa_x0 - 1));
          if (cr->fs1 > ppa_x1) f_span(std::max<int>(cr->fs0, ppa_x1 + 1), cr->fs1);
        } else {
          f_span(cr->fs0, cr->fs1);
        }
        for (int px = cr->fe0; px < cr->fs0; ++px) f_edge(px);
        for (int px = cr->fs1 + 1; px <= cr->fe1; ++px) f_edge(px);
      } else {
        for (int px = cr->fe0; px <= cr->fe1; ++px) f_edge(px);
      }
    }
  }
}

}  // namespace

namespace {

template <int Mode>
inline void blend_icon_span_mode(uint16_t *fb, int x0, int x1_excl, int y, int cy0, int cy1,
                                 uint32_t r5, uint32_t g6, uint32_t b5, uint32_t a_q8) noexcept {
  if (a_q8 == 0U || y < cy0 || y > cy1) return;
  if (x0 < 0) x0 = 0;
  if (x1_excl > SCREEN_W) x1_excl = SCREEN_W;
  if (x0 >= x1_excl) return;
  uint16_t *const dst = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
  const uint32_t inv = 256U - a_q8;
  const uint32_t base_rb = ((r5 * a_q8 + 128U) << 16) | (b5 * a_q8 + 128U);
  const uint32_t base_g_s5 = (g6 * a_q8 + 128U) << 5;
  for (int px = x0; px < x1_excl; ++px) {
    dst[px] = blend_swar_q8_px<Mode>(dst[px], inv, base_rb, base_g_s5);
  }
}

template <int Mode>
void render_mdi_icon_mode(uint16_t *fb, std::string_view eff_name, int cx, int cy, int size_px,
                          int cy0, int cy1, UiColor color, uint16_t full_hw, bool drop_shadow) {
  struct IconSpanCtx {
    uint16_t *fb;
    int cy0;
    int cy1;
    uint32_t r5;
    uint32_t g6;
    uint32_t b5;
    uint16_t full_hw;
  };
  if (drop_shadow) {
    IconSpanCtx sctx{
        fb, cy0, cy1,
        (static_cast<uint32_t>(kChromeShadow.r) * 31U + 127U) / 255U,
        (static_cast<uint32_t>(kChromeShadow.g) * 63U + 127U) / 255U,
        (static_cast<uint32_t>(kChromeShadow.b) * 31U + 127U) / 255U,
        0U,
    };
    auto emit_sh = +[](void *u, int x0, int x1_excl, int y, uint8_t a) {
      const auto &c = *static_cast<const IconSpanCtx *>(u);
      const uint32_t sa = (static_cast<uint32_t>(a) * 184U) >> 8;
      if (sa > 0U) {
        blend_icon_span_mode<Mode>(c.fb, x0, x1_excl, y, c.cy0, c.cy1, c.r5, c.g6, c.b5,
                                   sa + (sa >> 7));
      }
    };
    rasterize_icon(eff_name, cx + 1, cy + 1, size_px, emit_sh, &sctx);
  }
  IconSpanCtx ctx{
      fb, cy0, cy1,
      (static_cast<uint32_t>(color.r) * 31U + 127U) / 255U,
      (static_cast<uint32_t>(color.g) * 63U + 127U) / 255U,
      (static_cast<uint32_t>(color.b) * 31U + 127U) / 255U,
      full_hw,
  };
  auto emit_fg = +[](void *u, int x0, int x1_excl, int y, uint8_t a) {
    const auto &c = *static_cast<const IconSpanCtx *>(u);
    if (a == 255U) {
      if (y < c.cy0 || y > c.cy1) return;
      if (x0 < 0) x0 = 0;
      if (x1_excl > SCREEN_W) x1_excl = SCREEN_W;
      if (x0 < x1_excl) {
        fill_hw16_span(c.fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + x0,
                       x1_excl - x0, c.full_hw);
      }
      return;
    }
    if (a > 0U) {
      blend_icon_span_mode<Mode>(c.fb, x0, x1_excl, y, c.cy0, c.cy1, c.r5, c.g6, c.b5,
                                 static_cast<uint32_t>(a) + (static_cast<uint32_t>(a) >> 7));
    }
  };
  rasterize_icon(eff_name, cx, cy, size_px, emit_fg, &ctx);
}

template <int Mode>
void render_mdi_icon_spans_mode(uint16_t *fb, MdiSpanListView sv, int cx, int cy, int size_px,
                                int cy0, int cy1, UiColor color, uint16_t full_hw,
                                bool drop_shadow) noexcept {
  const int n = static_cast<int>(sv.count);
  const MdiSpan *const spans = sv.spans;
  if (drop_shadow) {
    const int x0_sh = (cx + 1) - (size_px / 2);
    const int y0_sh = (cy + 1) - (size_px / 2);
    for (int i = 0; i < n; ++i) {
      const MdiSpan &s = spans[i];
      const int py = y0_sh + static_cast<int>(s.y);
      if (py < cy0 || py > cy1) continue;
      const uint32_t sa = (static_cast<uint32_t>(s.alpha) * 184U) >> 8;
      if (sa == 0U) continue;
      const uint32_t a_q8 = sa + (sa >> 7);
      const uint32_t inv = 256U - a_q8;
      int xs = x0_sh + static_cast<int>(s.x0);
      int xe = x0_sh + static_cast<int>(s.x1);
      if (xs < 0) xs = 0;
      if (xe > SCREEN_W) xe = SCREEN_W;
      if (xs >= xe) continue;
      uint16_t *const dst = fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W);
      for (int px = xs; px < xe; ++px) {
        dst[px] = blend_black_q8_px<Mode>(dst[px], inv);
      }
    }
  }
  const int x0_fg = cx - (size_px / 2);
  const int y0_fg = cy - (size_px / 2);
  const uint32_t r5 = (static_cast<uint32_t>(color.r) * 31U + 127U) / 255U;
  const uint32_t g6 = (static_cast<uint32_t>(color.g) * 63U + 127U) / 255U;
  const uint32_t b5 = (static_cast<uint32_t>(color.b) * 31U + 127U) / 255U;
  for (int i = 0; i < n; ++i) {
    const MdiSpan &s = spans[i];
    const int py = y0_fg + static_cast<int>(s.y);
    if (py < cy0 || py > cy1) continue;
    int xs = x0_fg + static_cast<int>(s.x0);
    int xe = x0_fg + static_cast<int>(s.x1);
    if (xs < 0) xs = 0;
    if (xe > SCREEN_W) xe = SCREEN_W;
    if (xs >= xe) continue;
    if (s.alpha == 255U) {
      fill_hw16_span(fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W) + xs, xe - xs, full_hw);
    } else if (s.alpha > 0U) {
      const uint32_t a_q8 = static_cast<uint32_t>(s.alpha) + (static_cast<uint32_t>(s.alpha) >> 7);
      blend_icon_span_mode<Mode>(fb, xs, xe, py, cy0, cy1, r5, g6, b5, a_q8);
    }
  }
}

template <int Mode>
void blend_solid_span_q8_mode(uint16_t *dst_row, int x0, int x1, UiColor c, int a_255) noexcept {
  const uint32_t a_q8 = static_cast<uint32_t>(a_255 + (a_255 >> 7));
  const uint32_t inv = 256U - a_q8;
  const uint32_t sr5 = (static_cast<uint32_t>(c.r) * 31U + 127U) / 255U;
  const uint32_t sg6 = (static_cast<uint32_t>(c.g) * 63U + 127U) / 255U;
  const uint32_t sb5 = (static_cast<uint32_t>(c.b) * 31U + 127U) / 255U;
  const uint32_t base_rb = ((sr5 * a_q8 + 128U) << 16) | (sb5 * a_q8 + 128U);
  const uint32_t base_g_s5 = (sg6 * a_q8 + 128U) << 5;
  for (int px = x0; px <= x1; ++px) {
    dst_row[px] = blend_swar_q8_px<Mode>(dst_row[px], inv, base_rb, base_g_s5);
  }
}

}  // namespace

void fast_hline_blend_impl(Rgb565Surface &it, int x0, int w, int y, UiColor color, int alpha_255) {
  if (w <= 0 || alpha_255 <= 0 || !row_in_clip_band(y)) return;
  if (x0 < 0) {
    w += x0;
    x0 = 0;
  }
  if (x0 + w > SCREEN_W) {
    w = SCREEN_W - x0;
  }
  if (w <= 0) return;
  if (alpha_255 >= 255) {
    fast_hline(it, x0, y, w, color);
    return;
  }
  uint16_t *const fb = active_frame_buf16();
  if (fb == nullptr) {
    for (int px = x0; px < x0 + w; ++px) {
      fast_pixel_blend(it, px, y, color, alpha_255);
    }
    return;
  }
  uint16_t *const row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
  const int x1 = x0 + w - 1;
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) {
    blend_solid_span_q8_mode<1>(row, x0, x1, color, alpha_255);
  } else if (mode == 2) {
    blend_solid_span_q8_mode<2>(row, x0, x1, color, alpha_255);
  } else if (mode == 4) {
    blend_solid_span_q8_mode<4>(row, x0, x1, color, alpha_255);
  } else {
    blend_solid_span_q8_mode<3>(row, x0, x1, color, alpha_255);
  }
}

void render_mdi_icon_impl(Rgb565Surface &it, std::string_view name, int cx, int cy, int size_px,
                          UiColor color, bool drop_shadow, std::string_view fallback_name) {
  if (size_px <= 0) return;
  const int half = (size_px + 3) / 2;
  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  if ((cy + half) < cy0 || (cy - half) > cy1) return;

  uint16_t *const fb = active_frame_buf16();
  if (fb == nullptr) {
    const std::string_view eff_name = has_icon(name) ? name : fallback_name;
    if (eff_name.empty()) return;
    if (drop_shadow) {
      rasterize_icon_fn(eff_name, cx + 1, cy + 1, size_px,
                        [&](int x0, int x1_excl, int y, uint8_t a) {
                          const int sa = (static_cast<int>(a) * 184) >> 8;
                          if (sa > 0) fast_hline_blend_impl(it, x0, x1_excl - x0, y, kChromeShadow, sa);
                        });
    }
    rasterize_icon_fn(eff_name, cx, cy, size_px,
                      [&](int x0, int x1_excl, int y, uint8_t a) {
                        if (a == 255U) {
                          fast_hline(it, x0, y, x1_excl - x0, color);
                        } else if (a > 0U) {
                          fast_hline_blend_impl(it, x0, x1_excl - x0, y, color, static_cast<int>(a));
                        }
                      });
    return;
  }

  const uint16_t full_hw = encode_hw_rgb565(color);
  const int mode = active_frame_endian_mode();
  MdiSpanListView sv = get_mdi_raster_spans(name, size_px);
  if (sv.spans != nullptr) {
    if (mode == 1 || mode < 0) {
      render_mdi_icon_spans_mode<1>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
    } else if (mode == 2) {
      render_mdi_icon_spans_mode<2>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
    } else if (mode == 4) {
      render_mdi_icon_spans_mode<4>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
    } else {
      render_mdi_icon_spans_mode<3>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
    }
    return;
  }

  const std::string_view eff_name = has_icon(name) ? name : fallback_name;
  if (eff_name.empty()) return;
  if (eff_name != name) {
    sv = get_mdi_raster_spans(eff_name, size_px);
    if (sv.spans != nullptr) {
      if (mode == 1 || mode < 0) {
        render_mdi_icon_spans_mode<1>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
      } else if (mode == 2) {
        render_mdi_icon_spans_mode<2>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
      } else if (mode == 4) {
        render_mdi_icon_spans_mode<4>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
      } else {
        render_mdi_icon_spans_mode<3>(fb, sv, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
      }
      return;
    }
  }
  if (mode == 1 || mode < 0) {
    render_mdi_icon_mode<1>(fb, eff_name, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
  } else if (mode == 2) {
    render_mdi_icon_mode<2>(fb, eff_name, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
  } else if (mode == 4) {
    render_mdi_icon_mode<4>(fb, eff_name, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
  } else {
    render_mdi_icon_mode<3>(fb, eff_name, cx, cy, size_px, cy0, cy1, color, full_hw, drop_shadow);
  }
}

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

void fill_round_rect_glass_rimmed_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                       UiColor rim_tint, int rim_top, int rim_bot,
                                       UiColor fill_tint, int fill_top, int fill_bot,
                                       float rad_in) {
  if (w <= 0 || h <= 0) return;
  if (y0 + h <= clip_y0() || y0 > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fill_round_rect_aa_impl(it, x0, y0, w, h, rad, rim_tint, rim_tint, 1);
    if (w > 2 && h > 2) {
      const float r_in = (rad_in >= 0.0f) ? rad_in : std::max(0.0f, rad - 1.0f);
      fill_round_rect_aa_impl(it, x0 + 1, y0 + 1, w - 2, h - 2, r_in, fill_tint, fill_tint, 0);
    }
    return;
  }
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) {
    fill_round_rect_glass_rimmed_mode<1>(fb, x0, y0, w, h, rad, rim_tint, rim_top, rim_bot,
                                         fill_tint, fill_top, fill_bot, rad_in);
  } else if (mode == 2) {
    fill_round_rect_glass_rimmed_mode<2>(fb, x0, y0, w, h, rad, rim_tint, rim_top, rim_bot,
                                         fill_tint, fill_top, fill_bot, rad_in);
  } else if (mode == 4) {
    fill_round_rect_glass_rimmed_mode<4>(fb, x0, y0, w, h, rad, rim_tint, rim_top, rim_bot,
                                         fill_tint, fill_top, fill_bot, rad_in);
  } else {
    fill_round_rect_glass_rimmed_mode<3>(fb, x0, y0, w, h, rad, rim_tint, rim_top, rim_bot,
                                         fill_tint, fill_top, fill_bot, rad_in);
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
  backdrop_max_lum_q8() = 256;
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
        uint16_t *const row_dst =
            fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x0);
        if (byte_uniform) {
          std::memset(row_dst, static_cast<int>(hw & 0xFFU), static_cast<size_t>(len) * sizeof(uint16_t));
        } else {
          fill_hw16_span(row_dst, len, hw);
        }
      }
#else
      if (byte_uniform) {
        const size_t off = static_cast<size_t>(cy0) * static_cast<size_t>(SCREEN_W);
        const size_t nbytes =
            static_cast<size_t>(cy1 - cy0 + 1) * static_cast<size_t>(SCREEN_W) * sizeof(uint16_t);
        std::memset(fb + off, static_cast<int>(hw & 0xFFU), nbytes);
      } else {
        const size_t off = static_cast<size_t>(cy0) * static_cast<size_t>(SCREEN_W);
        const int npx = (cy1 - cy0 + 1) * SCREEN_W;
        fill_hw16_span(fb + off, npx, hw);
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
