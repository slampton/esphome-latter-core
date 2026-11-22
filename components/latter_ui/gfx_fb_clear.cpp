#include "ui_gfx.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_timer.h>
// Hardware between-frames clear is restricted to ESP32-P4, whose 2D PPA Fill engine writes AXI PSRAM
// in ~2.5 ms without source reads or Flash XIP stalls. On Octal-PSRAM XIP chips without PPA (ESP32-S3),
// PSRAM-to-PSRAM GDMA + cache msync contends directly with instruction fetches and PSRAM wake-word inference.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_FB_DMA_CLEAR 1
#define LATTER_FB_PPA 1
#include <driver/ppa.h>
#include <esp_async_memcpy.h>
#include <esp_attr.h>
#include <esp_cache.h>
#include <esp_rom_sys.h>
#else
#define LATTER_FB_DMA_CLEAR 0
#endif
#else
#define LATTER_FB_DMA_CLEAR 0
#endif

#ifndef LATTER_FB_PPA
#define LATTER_FB_PPA 0
#endif

namespace latter_ui {

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

void ui_psram_free(void *ptr) noexcept {
  if (ptr == nullptr) return;
#ifdef USE_ESP32
  heap_caps_free(ptr);
#else
  std::free(ptr);
#endif
}

// 2. Between-Frames Async GDMA / PPA Framebuffer Clear & FrameProfileScope
namespace {

struct FbClearState {
  void *owner = nullptr;
  PostFrameRequestFn post_frame_hook = nullptr;
  uint16_t *fb = nullptr;
  bool init_tried = false, ok = false, post_stamp_pending = false;
  bool pending_start = false, active = false, ready = false, error = false;
  uint16_t color_hw = 0, pattern_hw = 0;
  bool pattern_valid = false;
  uint8_t *interior = nullptr;
  size_t interior_len = 0;
  uint32_t hits = 0, misses = 0, starts = 0;
  const char *why = nullptr;
  int why_err = 0;
#if LATTER_FB_DMA_CLEAR
  async_memcpy_handle_t mcp = nullptr;
  uint16_t *pattern = nullptr;
  size_t align = 64;
#endif
#if LATTER_FB_PPA
  ppa_client_handle_t ppa = nullptr;
  bool use_ppa = false, ppa_checked = false;
  int64_t ppa_t0_us = 0;
#endif
};

FbClearState &fb_clear_state() {
  static FbClearState st;
  return st;
}

[[maybe_unused]] void fb_clear_fail(const char *why, int err = 0) {
  auto &st = fb_clear_state();
  if (st.why != why || st.why_err != err) ESP_LOGW("disp_fb", "DMA clear skipped: %s (err=%d)", why, err);
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

#if LATTER_FB_DMA_CLEAR
constexpr size_t kFbClearPatternBytes = 128 * 1024;
constexpr size_t kFbClearFrameBytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
constexpr size_t kFbClearChunks = (kFbClearFrameBytes + kFbClearPatternBytes - 1) / kFbClearPatternBytes;

void fb_clear_log_heap(const char *when) {
  ESP_LOGI("disp_fb", "DMA clear heap %s: int_free=%uB int_max_blk=%uB", when,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

std::atomic<int> g_fb_clear_pending{0};

bool IRAM_ATTR fb_clear_chunk_done(async_memcpy_handle_t, async_memcpy_event_t *, void *) {
  g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
  return false;
}

#if LATTER_FB_PPA
constexpr uint32_t kFbPpaRowPx = 128;
constexpr size_t kFbPpaRowBytes = kFbPpaRowPx * sizeof(uint16_t);
constexpr size_t kFbPpaMaxRows = 16383;

bool IRAM_ATTR fb_clear_ppa_done(ppa_client_handle_t, ppa_event_data_t *, void *) {
  g_fb_clear_done_us.store(esp_timer_get_time(), std::memory_order_release);
  g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
  return false;
}
#endif

bool fb_clear_init() {
  auto &st = fb_clear_state();
  if (st.init_tried) return st.ok;
  st.init_tried = true;
#if LATTER_FB_PPA
  const int endian = active_frame_endian_mode();
  if (endian == 1 || endian == 3) {
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
      st.use_ppa = true;
      st.align = kFbPpaRowBytes;
      st.ok = true;
      ESP_LOGI("disp_fb", "Between-frames clear enabled: PPA fill (%u px rows)", static_cast<unsigned>(kFbPpaRowPx));
      return true;
    }
    fb_clear_fail("ppa_register", static_cast<int>(perr));
  }
#endif
  fb_clear_log_heap("before install");
  async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
  cfg.backlog = kFbClearChunks + 1;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  esp_err_t err = esp_async_memcpy_install_gdma_axi(&cfg, &st.mcp);
#else
  esp_err_t err = esp_async_memcpy_install_gdma_ahb(&cfg, &st.mcp);
#endif
#if defined(CONFIG_CACHE_L2_CACHE_LINE_128B)
  st.align = 128;
#else
  st.align = 64;
#endif
  if (err == ESP_OK) {
    st.pattern = static_cast<uint16_t *>(heap_caps_aligned_alloc(st.align, kFbClearPatternBytes, MALLOC_CAP_SPIRAM));
  }
  st.ok = (err == ESP_OK && st.pattern != nullptr);
  if (err != ESP_OK) fb_clear_fail("install", static_cast<int>(err));
  else if (st.pattern == nullptr) fb_clear_fail("pattern_alloc");
  ESP_LOGI("disp_fb", "Between-frames DMA clear %s (err=%d, align=%u, chunks=%u)", st.ok ? "enabled" : "unavailable",
           static_cast<int>(err), static_cast<unsigned>(st.align), static_cast<unsigned>(kFbClearChunks));
  fb_clear_log_heap("after install");
  return st.ok;
}
#endif

void fb_clear_start() {
  (void)g_fb_clear_done_us;
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  st.pending_start = false;
  if (st.active) return;
  if (st.fb == nullptr) { fb_clear_fail("no_fb"); return; }
  if (!fb_clear_init()) return;
  const uintptr_t base = reinterpret_cast<uintptr_t>(st.fb);
  const size_t bytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
  const uintptr_t lo = (base + st.align - 1) & ~(static_cast<uintptr_t>(st.align) - 1);
  const uintptr_t hi = (base + bytes) & ~(static_cast<uintptr_t>(st.align) - 1);
  if (hi <= lo) { fb_clear_fail("range"); return; }
  st.interior = reinterpret_cast<uint8_t *>(lo);
  st.interior_len = hi - lo;
  st.color_hw = frame_base_state().color_hw;
#if LATTER_FB_PPA
  if (st.use_ppa) {
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
    st.starts++;
    return;
  }
#endif
  if (!st.pattern_valid || st.pattern_hw != st.color_hw) {
    for (size_t i = 0; i < kFbClearPatternBytes / sizeof(uint16_t); i++) st.pattern[i] = st.color_hw;
    esp_cache_msync(st.pattern, kFbClearPatternBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    st.pattern_hw = st.color_hw;
    st.pattern_valid = true;
  }
  const esp_err_t sync_err =
      esp_cache_msync(st.interior, st.interior_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
  if (sync_err != ESP_OK) { fb_clear_fail("msync", static_cast<int>(sync_err)); return; }
  st.error = false;
  st.ready = false;
  st.active = true;
  size_t off = 0;
  while (off < st.interior_len) {
    const size_t n = std::min(kFbClearPatternBytes, st.interior_len - off);
    g_fb_clear_pending.fetch_add(1, std::memory_order_acq_rel);
    const esp_err_t cp_err = esp_async_memcpy(st.mcp, st.interior + off, st.pattern, n, fb_clear_chunk_done, nullptr);
    if (cp_err != ESP_OK) {
      g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
      st.error = true;
      fb_clear_fail("memcpy", static_cast<int>(cp_err));
      break;
    }
    off += n;
  }
  st.starts++;
  if (st.starts == 1 || st.starts == 256) fb_clear_log_heap(st.starts == 1 ? "after 1st clear" : "after 256 clears");
#endif
}

}  // namespace

void set_fb_post_frame_hook(PostFrameRequestFn hook) noexcept { fb_clear_state().post_frame_hook = hook; }

void fb_clear_bind_raw(void *owner, uint16_t *fb) noexcept {
  auto &st = fb_clear_state();
  st.owner = owner;
  st.fb = fb;
}

void fb_clear_after_update() {
  auto &st = fb_clear_state();
  if (st.post_stamp_pending) {
    st.post_stamp_pending = false;
    last_disp_update_end_ms() = hal_millis_now();
  }
  if (st.pending_start) fb_clear_start();
}

void fb_clear_wait() {
#if LATTER_FB_DMA_CLEAR
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
#if LATTER_FB_PPA
  if (st.use_ppa && st.ready && !st.ppa_checked) {
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
    ESP_LOGI("disp_fb", "PPA fill verified: %u KB in %ld us", static_cast<unsigned>(st.interior_len / 1024),
             static_cast<long>(g_fb_clear_done_us.load(std::memory_order_acquire) - st.ppa_t0_us));
  }
#endif
#endif
}

bool fb_clear_consume(uint16_t hw) {
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
}

void fb_clear_frame_end() {
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  if (st.ready) { st.ready = false; st.active = false; }
  if (frame_base_state().has_color && (st.owner == nullptr || st.fb == nullptr)) fb_clear_fail("no_fb");
  if (!frame_base_state().has_color || st.owner == nullptr || st.fb == nullptr || st.pending_start) return;
  if (st.init_tried && !st.ok) return;
  st.pending_start = true;
#endif
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
  last_frame_duration_ms() = render_us / 1000U;
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
    auto &fbc = fb_clear_state();
    LATTER_PROF_LOGI("[PROF:FRAME_SUM] frames=%lu fps=%.1f avg=%luus max=%luus(p%d/%d) slow=%lu dma_clear=%lu/%lu%s%s%s",
                     static_cast<unsigned long>(fr.window_frames), fps, static_cast<unsigned long>(avg_us),
                     static_cast<unsigned long>(fr.max_render_us), fr.max_render_page, fr.max_render_sub,
                     static_cast<unsigned long>(fr.slow_frames), static_cast<unsigned long>(fbc.hits),
                     static_cast<unsigned long>(fbc.hits + fbc.misses),
                     fbc.why != nullptr ? " dma_off=" : "", fbc.why != nullptr ? fbc.why : "",
                     fbc.why != nullptr ? fb_clear_why_err_str() : "");
    fbc.hits = fbc.misses = 0;
    uint32_t *sect = prof_sect_us(), any_sect = 0;
    for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) any_sect |= sect[i];
    if (any_sect != 0U) {
      const uint32_t n = fr.window_frames;
      LATTER_PROF_LOGI("[PROF:SECT] avg/frame base=%luus art=%luus sky=%luus celestial=%luus clouds=%luus precip=%luus text=%luus dock=%luus",
                       static_cast<unsigned long>(sect[6] / n), static_cast<unsigned long>(sect[7] / n),
                       static_cast<unsigned long>(sect[0] / n), static_cast<unsigned long>(sect[1] / n),
                       static_cast<unsigned long>(sect[2] / n), static_cast<unsigned long>(sect[3] / n),
                       static_cast<unsigned long>(sect[4] / n), static_cast<unsigned long>(sect[5] / n));
      for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) sect[i] = 0;
    }
    fr.window_frames = fr.window_render_us = fr.max_render_us = fr.slow_frames = 0;
    fr.last_summary_ms = now_m;
  }
}

}  // namespace latter_ui
