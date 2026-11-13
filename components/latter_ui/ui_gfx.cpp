#include "esphome/core/defines.h"
#include "ui_gfx.h"
#include "ui_compositor.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_FONT
#include "esphome/components/font/font.h"
#endif

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_timer.h>
// Hardware between-frames clear is restricted to ESP32-P4, whose 2D PPA Fill engine writes AXI PSRAM
// in ~2.5 ms without source reads or Flash XIP stalls. On Octal-PSRAM XIP chips without PPA (ESP32-S3),
// PSRAM-to-PSRAM GDMA + cache msync contends directly with instruction fetches and PSRAM wake-word inference.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_FB_DMA_CLEAR 1
#define LATTER_FB_PPA 1
#include <atomic>
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

#include <cstdio>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Framebuffer Endianness Probe & Between-Frames Async GDMA / PPA Clear
// ------------------------------------------------------------------------------
void probe_frame_buffer_endian(esphome::display::Display &it, uint16_t *buf16) {
  if (buf16 == nullptr) {
    active_frame_endian_mode() = 0;
    return;
  }
  const uint16_t orig_px0 = buf16[0];
  it.draw_pixel_at(0, 0, esphome::Color(255, 0, 0));
  const uint16_t red_probe = buf16[0];
  buf16[0] = orig_px0;
  if (red_probe == 0xF800U) {
    active_frame_endian_mode() = 1;
  } else if (red_probe == 0x00F8U) {
    active_frame_endian_mode() = 2;
  } else if (red_probe == 0x001FU) {
    active_frame_endian_mode() = 3;
  } else if (red_probe == 0x1F00U) {
    active_frame_endian_mode() = 4;
  } else {
    active_frame_endian_mode() = 0;
  }
  ESP_LOGI("disp_fb", "Bound direct RGB565 framebuffer: ptr=%p endian_mode=%d",
           (void *)buf16, active_frame_endian_mode());
}

namespace {

struct FbClearState {
  esphome::Component *owner = nullptr;
  uint16_t *fb = nullptr;
  bool init_tried = false;
  bool ok = false;
  bool post_stamp_pending = false;
  bool pending_start = false;
  bool active = false;
  bool ready = false;
  bool error = false;
  uint16_t color_hw = 0;
  uint16_t pattern_hw = 0;
  bool pattern_valid = false;
  uint8_t *interior = nullptr;
  size_t interior_len = 0;
  uint32_t hits = 0;
  uint32_t misses = 0;
  uint32_t starts = 0;
  const char *why = nullptr;
  int why_err = 0;
#if LATTER_FB_DMA_CLEAR
  async_memcpy_handle_t mcp = nullptr;
  uint16_t *pattern = nullptr;
  size_t align = 64;
#endif
#if LATTER_FB_PPA
  ppa_client_handle_t ppa = nullptr;
  bool use_ppa = false;
  bool ppa_checked = false;
  int64_t ppa_t0_us = 0;
#endif
};

FbClearState &fb_clear_state() {
  static FbClearState st;
  return st;
}

void fb_clear_fail(const char *why, int err = 0) {
  auto &st = fb_clear_state();
  if (st.why != why || st.why_err != err) {
    ESP_LOGW("disp_fb", "DMA clear skipped: %s (err=%d)", why, err);
  }
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
int64_t g_fb_clear_done_us = 0;

bool IRAM_ATTR fb_clear_ppa_done(ppa_client_handle_t, ppa_event_data_t *, void *) {
  g_fb_clear_done_us = esp_timer_get_time();
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
  if (err != ESP_OK) {
    fb_clear_fail("install", static_cast<int>(err));
  } else if (st.pattern == nullptr) {
    fb_clear_fail("pattern_alloc");
  }
  ESP_LOGI("disp_fb", "Between-frames DMA clear %s (err=%d, align=%u, chunks=%u)", st.ok ? "enabled" : "unavailable",
           static_cast<int>(err), static_cast<unsigned>(st.align), static_cast<unsigned>(kFbClearChunks));
  fb_clear_log_heap("after install");
  return st.ok;
}
#endif

void fb_clear_start() {
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  st.pending_start = false;
  if (st.active) return;
  if (st.fb == nullptr) {
    fb_clear_fail("no_fb");
    return;
  }
  if (!fb_clear_init()) return;
  const uintptr_t base = reinterpret_cast<uintptr_t>(st.fb);
  const size_t bytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
  const uintptr_t lo = (base + st.align - 1) & ~(static_cast<uintptr_t>(st.align) - 1);
  const uintptr_t hi = (base + bytes) & ~(static_cast<uintptr_t>(st.align) - 1);
  if (hi <= lo) {
    fb_clear_fail("range");
    return;
  }
  st.interior = reinterpret_cast<uint8_t *>(lo);
  st.interior_len = hi - lo;
  st.color_hw = frame_base_state().color_hw;
#if LATTER_FB_PPA
  if (st.use_ppa) {
    const size_t rows = st.interior_len / kFbPpaRowBytes;
    if (rows > kFbPpaMaxRows) {
      fb_clear_fail("ppa_rows", static_cast<int>(rows));
      return;
    }
    ppa_fill_oper_config_t cfg = {};
    cfg.out.buffer = st.interior;
    cfg.out.buffer_size = static_cast<uint32_t>(st.interior_len);
    cfg.out.pic_w = kFbPpaRowPx;
    cfg.out.pic_h = static_cast<uint32_t>(rows);
    cfg.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
    cfg.fill_block_w = kFbPpaRowPx;
    cfg.fill_block_h = static_cast<uint32_t>(rows);
    const uint32_t hw = st.color_hw;
    const uint32_t r8 = ((hw >> 11) & 0x1FU) << 3;
    const uint32_t g8 = ((hw >> 5) & 0x3FU) << 2;
    const uint32_t b8 = (hw & 0x1FU) << 3;
    cfg.fill_argb_color.val = 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
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
  if (sync_err != ESP_OK) {
    fb_clear_fail("msync", static_cast<int>(sync_err));
    return;
  }
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

void fb_clear_bind(esphome::Component *owner, uint16_t *fb) {
  auto &st = fb_clear_state();
  st.owner = owner;
  st.fb = fb;
}

void fb_clear_after_update() {
  auto &st = fb_clear_state();
  if (st.post_stamp_pending) {
    st.post_stamp_pending = false;
    last_disp_update_end_ms() = esphome::millis();
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
             static_cast<long>(g_fb_clear_done_us - st.ppa_t0_us));
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
  if (st.ready) {
    st.ready = false;
    st.active = false;
  }
  if (frame_base_state().has_color && (st.owner == nullptr || st.fb == nullptr)) fb_clear_fail("no_fb");
  if (!frame_base_state().has_color || st.owner == nullptr || st.fb == nullptr || st.pending_start) return;
  if (st.init_tried && !st.ok) return;
  st.pending_start = true;
#endif
}

// ------------------------------------------------------------------------------
// 2. WCAG 4.5:1 Frosted Glass Backdrop Sampling & Luminance LUT
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

float relative_luminance(esphome::Color c) {
  const float *lut = srgb_linear_lut();
  return 0.2126f * lut[c.r] + 0.7152f * lut[c.g] + 0.0722f * lut[c.b];
}

float contrast_ratio(float la, float lb) {
  const float hi = std::max(la, lb), lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

esphome::Color mix_color_q8(esphome::Color c_under, esphome::Color c_over, int a) {
  a = std::max(0, std::min(256, a));
  const int ia = 256 - a;
  return esphome::Color(static_cast<uint8_t>((c_over.r * a + c_under.r * ia) >> 8),
                        static_cast<uint8_t>((c_over.g * a + c_under.g * ia) >> 8),
                        static_cast<uint8_t>((c_over.b * a + c_under.b * ia) >> 8));
}

GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0), xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, y0), ye = std::min(SCREEN_H, y0 + h);
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const float *lut = srgb_linear_lut();
  const int mode = active_frame_endian_mode();
  const int step = std::max(3, h / 12);
  uint32_t cnt[32] = {0}, sr[32] = {0}, sg[32] = {0}, sb[32] = {0};
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + y * SCREEN_W;
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t hw = row[x];
      const uint16_t p = (mode == 1) ? decode_hw_rgb565_mode<1>(hw)
                       : ((mode == 2) ? decode_hw_rgb565_mode<2>(hw)
                       : ((mode == 4) ? decode_hw_rgb565_mode<4>(hw) : decode_hw_rgb565_mode<3>(hw)));
      const int r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
      const int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
      const float lum = 0.2126f * lut[r] + 0.7152f * lut[g] + 0.0722f * lut[b];
      const int bin = std::min(31, static_cast<int>(lum * 32.0f));
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
  out.bright = esphome::Color(static_cast<uint8_t>(ar / acc),
                              static_cast<uint8_t>(ag / acc),
                              static_cast<uint8_t>(ab / acc));
  out.mean = esphome::Color(static_cast<uint8_t>(tr / n),
                            static_cast<uint8_t>(tg / n),
                            static_cast<uint8_t>(tb / n));
  out.valid = true;
  return out;
}

int glass_alpha_for_text(esphome::Color tint, esphome::Color backdrop, esphome::Color text) {
  const float lt = relative_luminance(text);
  for (int a = kGlassAlphaMinQ8; a < 256; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrast) return a;
  }
  return 256;
}

int glass_alpha_smooth(int &state, int target) {
  if (state <= 0 || target >= state) state = target;
  else state = std::max(target, state - kGlassAlphaFallQ8);
  return state;
}

void resolve_glass_fill(const GlassBackdrop &bd, esphome::Color tint, esphome::Color ref_text,
                        esphome::Color &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8) {
  const int bg_lvl = bd.valid
      ? ((static_cast<int>(bd.mean.r) + static_cast<int>(bd.mean.g) + static_cast<int>(bd.mean.b)) / 3)
      : 0;
  const int tint_lvl = (static_cast<int>(tint.r) + static_cast<int>(tint.g) + static_cast<int>(tint.b)) / 3;
  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  int frost_q8 = (dark_tint && bg_lvl < 42) ? (32 - (bg_lvl * 32) / 42) : 0;
  if (dark_tint && bg_lvl >= 18 && bg_lvl < 84 && tint_lvl <= bg_lvl + 8) {
    frost_q8 = std::max(frost_q8, 22);
  }
  out_tint = (frost_q8 > 0) ? mix_color_q8(tint, kChromeHomePill, frost_q8) : tint;
  const esphome::Color contrast_fg = dark_tint ? kChromeGlassSheen : ref_text;
  const int base_a = (explicit_alpha_q8 >= 0)
      ? explicit_alpha_q8
      : (bd.valid ? glass_alpha_for_text(out_tint, bd.bright, contrast_fg) : 168);
  const int da = dark_tint ? ((bg_lvl < 42) ? (22 - (bg_lvl * 8) / 42) : 14) : 0;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(kGlassAlphaMinQ8 - 16, base_a - (da * 2) / 3);
  const int rim_boost = (bg_lvl < 32) ? ((32 - bg_lvl) * 18) / 32 : 8;
  out_rim_top = std::min(256, kGlassSheenTopQ8 + rim_boost);
  out_rim_bot = std::min(256, kGlassSheenBotQ8 + (rim_boost * 2) / 3);
}

esphome::Color sample_glass_surface_color(int x0, int y0, int w, int h, esphome::Color tint) {
  const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, tint, kChromeGlassSheen, eff_tint, a_top, a_bot, rim_top, rim_bot);
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

// ------------------------------------------------------------------------------
// 3. UTF-8 Sanitizer, Memoized Text Fitting & 1-bpp PSRAM Span Glyph Cache
// ------------------------------------------------------------------------------
namespace {

uint32_t latter_utf8_next(const char *&p) {
  const uint8_t c0 = static_cast<uint8_t>(p[0]);
  if (c0 == 0) return 0;
  if (c0 < 0x80) {
    p += 1;
    return c0;
  }
  if ((c0 & 0xE0) == 0xC0 && p[1] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
    p += 2;
    return cp;
  }
  if ((c0 & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x0F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[2]) & 0x3F);
    p += 3;
    return cp;
  }
  if ((c0 & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x07) << 18) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[3]) & 0x3F);
    p += 4;
    return cp;
  }
  p += 1;
  return 0xFFFD;
}

void append_utf8_cp(std::string &out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool is_supported_font_cp(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  if (cp >= 0xA1 && cp <= 0xFF && cp != 0xAD) return true;
  switch (cp) {
    case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
    case 0x0106: case 0x0107: case 0x010C: case 0x010D: case 0x010E: case 0x010F:
    case 0x0110: case 0x0111: case 0x0112: case 0x0113: case 0x0116: case 0x0117:
    case 0x0118: case 0x0119: case 0x011A: case 0x011B: case 0x011E: case 0x011F:
    case 0x012A: case 0x012B: case 0x012E: case 0x012F: case 0x0130: case 0x0131:
    case 0x0136: case 0x0137: case 0x0139: case 0x013A: case 0x013B: case 0x013C:
    case 0x013D: case 0x013E: case 0x0141: case 0x0142: case 0x0143: case 0x0144:
    case 0x0145: case 0x0146: case 0x0147: case 0x0148: case 0x014C: case 0x014D:
    case 0x0150: case 0x0151: case 0x0152: case 0x0153: case 0x0154: case 0x0155:
    case 0x0158: case 0x0159: case 0x015A: case 0x015B: case 0x015E: case 0x015F:
    case 0x0160: case 0x0161: case 0x0162: case 0x0163: case 0x0164: case 0x0165:
    case 0x016A: case 0x016B: case 0x016E: case 0x016F: case 0x0170: case 0x0171:
    case 0x0172: case 0x0173: case 0x0178: case 0x0179: case 0x017A: case 0x017B:
    case 0x017C: case 0x017D: case 0x017E:
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:
    case 0x2022: case 0x2026: case 0x20AC: case 0x2122:
      return true;
    default:
      return false;
  }
}

#if defined(USE_FONT) && defined(USE_ESP32)
constexpr int kTextCacheEntries = 32;
constexpr int kTextScratchSpans = 4096;

TextCacheEntry *text_cache_entries() {
  static TextCacheEntry *entries = static_cast<TextCacheEntry *>(
      heap_caps_calloc(kTextCacheEntries, sizeof(TextCacheEntry), MALLOC_CAP_SPIRAM));
  return entries;
}

TextSpan *text_cache_scratch() {
  static TextSpan *scratch = static_cast<TextSpan *>(
      heap_caps_malloc(sizeof(TextSpan) * kTextScratchSpans, MALLOC_CAP_SPIRAM));
  return scratch;
}

int text_decode_spans_1bpp(esphome::font::Font *font, const char *text, TextSpan *out, int cap) {
  int n = 0;
  int x_at = 0;
  for (const char *p = text; *p != 0;) {
    const auto *glyph = font->find_glyph(latter_utf8_next(p));
    if (glyph == nullptr) {
      x_at += std::max(4, font->get_height() / 3);
      continue;
    }
    const uint8_t *data = glyph->data;
    const int gx0 = x_at + glyph->offset_x;
    uint8_t bitmask = 0;
    uint8_t pixel_data = 0;
    for (int gy = 0; gy < glyph->height; gy++) {
      int run_start = -1;
      for (int gx = 0; gx < glyph->width; gx++) {
        if (bitmask == 0) {
          pixel_data = esphome::progmem_read_byte(data++);
          bitmask = 0x80;
        }
        const bool on = (pixel_data & bitmask) != 0;
        bitmask >>= 1;
        if (on) {
          if (run_start < 0) run_start = gx;
        } else if (run_start >= 0) {
          if (n >= cap) return -1;
          out[n++] = {static_cast<int16_t>(gx0 + run_start), static_cast<int16_t>(glyph->offset_y + gy),
                      static_cast<int16_t>(gx - run_start)};
          run_start = -1;
        }
      }
      if (run_start >= 0) {
        if (n >= cap) return -1;
        out[n++] = {static_cast<int16_t>(gx0 + run_start), static_cast<int16_t>(glyph->offset_y + gy),
                    static_cast<int16_t>(glyph->width - run_start)};
      }
    }
    x_at += glyph->advance;
  }
  return n;
}

const TextSpan *text_cache_get(esphome::font::Font *font, const char *text, int &count) {
  TextCacheEntry *entries = text_cache_entries();
  TextSpan *scratch = text_cache_scratch();
  const size_t len = std::strlen(text);
  if (entries == nullptr || scratch == nullptr || len >= sizeof(entries[0].text)) return nullptr;
  static uint32_t use_seq = 0;
  ++use_seq;
  TextCacheEntry *victim = &entries[0];
  for (int i = 0; i < kTextCacheEntries; i++) {
    TextCacheEntry &e = entries[i];
    if (e.font == font && std::strcmp(e.text, text) == 0) {
      e.last_use = use_seq;
      count = e.count;
      return e.spans;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }
  const int n = text_decode_spans_1bpp(font, text, scratch, kTextScratchSpans);
  if (n < 0) return nullptr;
  if (victim->cap < n) {
    if (victim->spans != nullptr) heap_caps_free(victim->spans);
    victim->spans = static_cast<TextSpan *>(heap_caps_malloc(sizeof(TextSpan) * (n > 0 ? n : 1), MALLOC_CAP_SPIRAM));
    victim->cap = (victim->spans != nullptr) ? static_cast<uint16_t>(n) : 0;
    if (victim->spans == nullptr) {
      victim->font = nullptr;
      victim->text[0] = 0;
      victim->count = 0;
      count = n;
      return scratch;
    }
  }
  if (n > 0) std::memcpy(victim->spans, scratch, sizeof(TextSpan) * n);
  victim->font = font;
  std::memcpy(victim->text, text, len + 1);
  victim->count = static_cast<uint16_t>(n);
  victim->last_use = use_seq;
  count = n;
  return victim->spans;
}
#endif

}  // namespace

std::string sanitize_utf8_for_font(const std::string &str) {
  if (str.empty()) return str;
  std::string out;
  out.reserve(str.size());
  const size_t n = str.size();
  size_t i = 0;
  while (i < n) {
    const unsigned char b0 = static_cast<unsigned char>(str[i]);
    if (b0 < 0x80) {
      if (b0 >= 0x20 && b0 <= 0x7E) out.push_back(static_cast<char>(b0));
      i++;
      continue;
    }
    uint32_t cp = 0;
    size_t len = 0;
    if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      if ((b1 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1 & 0x3F);
        if (cp >= 0x80) len = 2;
      }
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) | (static_cast<uint32_t>(b1 & 0x3F) << 6) |
             static_cast<uint32_t>(b2 & 0x3F);
        if (cp >= 0x800) len = 3;
      }
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      const unsigned char b3 = static_cast<unsigned char>(str[i + 3]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x07) << 18) | (static_cast<uint32_t>(b1 & 0x3F) << 12) |
             (static_cast<uint32_t>(b2 & 0x3F) << 6) | static_cast<uint32_t>(b3 & 0x3F);
        if (cp >= 0x10000) len = 4;
      }
    }
    if (len == 0) {
      i++;
      continue;
    }
    i += len;

    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000) {
      out.push_back(' ');
      continue;
    }
    if (cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) {
      continue;
    }
    if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2212 || cp == 0xFF0D) {
      out.push_back('-');
      continue;
    }
    if (cp == 0x2015) {
      append_utf8_cp(out, 0x2014);
      continue;
    }
    if (cp == 0x02B9 || cp == 0x02BB || cp == 0x02BC || cp == 0x201A || cp == 0x2032 ||
        cp == 0x2039 || cp == 0x203A || cp == 0xFF07) {
      out.push_back('\'');
      continue;
    }
    if (cp == 0x201E || cp == 0x2033 || cp == 0xFF02) {
      out.push_back('"');
      continue;
    }
    if (cp == 0xFF01) { out.push_back('!'); continue; }
    if (cp == 0xFF1F) { out.push_back('?'); continue; }

    if (is_supported_font_cp(cp)) {
      append_utf8_cp(out, cp);
    }
  }
  return out;
}

std::string fit_text_to_width_font_base(const std::string &raw_str, int max_w, esphome::display::BaseFont *font) {
  if (raw_str.empty() || font == nullptr) return "";

  struct FitCacheEntry {
    const void *font_ptr = nullptr;
    int max_w = 0;
    size_t raw_len = 0;
    uint32_t fnv_hash = 0;
    std::string raw_key;
    std::string fitted;
  };
  static FitCacheEntry cache[16];
  static uint8_t next_slot = 0;

  uint32_t h = 2166136261U;
  for (unsigned char ch : raw_str) {
    h = (h ^ ch) * 16777619U;
  }
  const void *fptr = static_cast<const void *>(font);
  for (int i = 0; i < 16; i++) {
    const auto &e = cache[i];
    if (e.font_ptr == fptr && e.max_w == max_w && e.raw_len == raw_str.size() &&
        e.fnv_hash == h && e.raw_key == raw_str) {
      return e.fitted;
    }
  }

  const std::string str = sanitize_utf8_for_font(raw_str);
  if (str.empty()) return "";
  int w = 0, x_off = 0, base = 0, text_h = 0;
  font->measure(str.c_str(), &w, &x_off, &base, &text_h);
  std::string result;
  if (w <= max_w) {
    result = str;
  } else {
    std::string cur = str;
    if (cur.length() > 32) {
      while (cur.length() > 30) {
        while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
        if (!cur.empty()) cur.pop_back();
      }
    }
    result = "..";
    while (!cur.empty()) {
      const std::string test_str = cur + "..";
      font->measure(test_str.c_str(), &w, &x_off, &base, &text_h);
      if (w <= max_w) {
        result = test_str;
        break;
      }
      while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
      if (!cur.empty()) cur.pop_back();
    }
  }

  auto &slot = cache[next_slot & 15U];
  next_slot = static_cast<uint8_t>((next_slot + 1U) & 15U);
  slot.font_ptr = fptr;
  slot.max_w = max_w;
  slot.raw_len = raw_str.size();
  slot.fnv_hash = h;
  slot.raw_key = raw_str;
  slot.fitted = result;
  return result;
}

void fast_print_impl(esphome::display::Display &it, int x, int y, esphome::display::BaseFont *base_font,
                     esphome::Color color, esphome::display::TextAlign align, const char *text,
                     esphome::Color background) {
  if (base_font == nullptr || text == nullptr) return;
#ifdef USE_FONT
  auto *font = static_cast<esphome::font::Font *>(base_font);
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr || font->get_bpp() > 4) {
    it.print(x, y, base_font, color, align, text, background);
    return;
  }

  int x_start = 0, y_start = 0, text_w = 0, text_h = 0;
  it.get_text_bounds(x, y, text, base_font, align, &x_start, &y_start, &text_w, &text_h);

  const int bpp = font->get_bpp();
  const int bpp_max = (1 << bpp) - 1;
  uint16_t lut[16];
  const float diff_r = static_cast<float>(color.r) - static_cast<float>(background.r);
  const float diff_g = static_cast<float>(color.g) - static_cast<float>(background.g);
  const float diff_b = static_cast<float>(color.b) - static_cast<float>(background.b);
  const float diff_w = static_cast<float>(color.w) - static_cast<float>(background.w);
  for (int lv = 1; lv <= bpp_max; lv++) {
    if (lv == bpp_max) {
      lut[lv] = encode_hw_rgb565(color);
    } else {
      const float on = static_cast<float>(lv) / static_cast<float>(bpp_max);
      lut[lv] = encode_hw_rgb565(esphome::Color(static_cast<uint8_t>(diff_r * on + static_cast<float>(background.r)),
                                                static_cast<uint8_t>(diff_g * on + static_cast<float>(background.g)),
                                                static_cast<uint8_t>(diff_b * on + static_cast<float>(background.b)),
                                                static_cast<uint8_t>(diff_w * on + static_cast<float>(background.w))));
    }
  }

#ifdef USE_ESP32
  if (bpp == 1) {
    int span_count = 0;
    const TextSpan *spans = text_cache_get(font, text, span_count);
    if (spans != nullptr) {
      const uint16_t hw = lut[1];
      for (int i = 0; i < span_count; i++) {
        const int py = y_start + spans[i].y;
        if (static_cast<unsigned>(py) >= static_cast<unsigned>(SCREEN_H)) continue;
        int px = x_start + spans[i].x;
        int len = spans[i].len;
        if (px < 0) {
          len += px;
          px = 0;
        }
        if (px + len > SCREEN_W) len = SCREEN_W - px;
        uint16_t *dst = fb + py * SCREEN_W + px;
        for (int k = 0; k < len; k++) dst[k] = hw;
      }
      return;
    }
  }
#endif

  int x_at = x_start;
  for (const char *p = text; *p != 0;) {
    const auto *glyph = font->find_glyph(latter_utf8_next(p));
    if (glyph == nullptr) {
      x_at += std::max(4, font->get_height() / 3);
      continue;
    }
    const uint8_t *data = glyph->data;
    const int gx0 = x_at + glyph->offset_x;
    const int gy0 = y_start + glyph->offset_y;
    uint8_t bitmask = 0;
    uint8_t pixel_data = 0;
    for (int gy = 0; gy < glyph->height; gy++) {
      const int py = gy0 + gy;
      uint16_t *row = (static_cast<unsigned>(py) < static_cast<unsigned>(SCREEN_H)) ? fb + py * SCREEN_W : nullptr;
      for (int gx = 0; gx < glyph->width; gx++) {
        uint8_t pixel = 0;
        for (int bit = 0; bit < bpp; bit++) {
          if (bitmask == 0) {
            pixel_data = esphome::progmem_read_byte(data++);
            bitmask = 0x80;
          }
          pixel <<= 1;
          if ((pixel_data & bitmask) != 0) pixel |= 1;
          bitmask >>= 1;
        }
        if (pixel != 0 && row != nullptr) {
          const int px = gx0 + gx;
          if (static_cast<unsigned>(px) < static_cast<unsigned>(SCREEN_W)) row[px] = lut[pixel];
        }
      }
    }
    x_at += glyph->advance;
  }
#else
  it.print(x, y, base_font, color, align, text, background);
#endif
}

// ------------------------------------------------------------------------------
// 4. Album-Art / Wallpaper Composition & PSRAM ArtCanvasCache
// ------------------------------------------------------------------------------
namespace {

int compute_art_row_scale_q8(int y, bool apply_scrim, bool show_metadata, int dim_level) {
  if (dim_level >= 2) return 64;
  auto ramp = [](int yy, int y_a, int v_a, int y_b, int v_b) -> int {
    if (yy <= y_a) return v_a;
    if (yy >= y_b) return v_b;
    return v_a + ((v_b - v_a) * (yy - y_a)) / (y_b - y_a);
  };
  if (apply_scrim) {
    const int ramp_h = scale_y(12);
    int v = ramp(y, scale_y(76) - ramp_h, 96, scale_y(76) + ramp_h, 128);
    if (show_metadata) v = std::min(v, ramp(y, scale_y(324) - ramp_h, 128, scale_y(324) + ramp_h, 96));
    return v;
  }
  if (show_metadata) {
    if (y < scale_y(410)) return ramp(y, scale_y(298), 256, scale_y(358), 96);
    return ramp(y, scale_y(410), 96, scale_y(422), 256);
  }
  return 256;
}

void dim_rgb565_row_dither(uint16_t *row_buf, int span_w, int mul_q8, int x0, int y) {
  if (mul_q8 >= 256 || span_w <= 0) return;
  if (mul_q8 < 0) mul_q8 = 0;
  const uint8_t *th = kBayer4[y & 3];
  const uint32_t m = static_cast<uint32_t>(mul_q8);
  for (int i = 0; i < span_w; i++) {
    const uint32_t p = row_buf[i];
    const uint32_t t = static_cast<uint32_t>(th[(x0 + i) & 3]) * 16U + 8U;
    const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
    const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
    const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
    row_buf[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  }
}

void art_row_span(int y, int cx, int &start_x, int &end_x) {
#if LATTER_ROUND_DISPLAY
  const int hw = circle_half_span(y, 0);
  start_x = std::max(0, cx - hw);
  end_x = std::min(SCREEN_W - 1, cx + hw);
#else
  (void)y;
  (void)cx;
  start_x = 0;
  end_x = SCREEN_W - 1;
#endif
}

void draw_album_art(esphome::display::Display &it, const uint16_t *src_data, int src_w, int src_h, int cx,
                    bool is_standard, bool is_letterbox, bool apply_scrim, bool show_metadata, int dim_level) {
  static uint16_t row_buf[SCREEN_W];

  if (is_standard) {
    const ArtRect r = art_standard_rect(cx, src_w, src_h, show_metadata, apply_scrim);
    const int step_x = (src_w << 12) / r.w;
    const int step_y = (src_h << 12) / r.h;
    const int mul_q8 = (dim_level >= 2) ? 64 : (apply_scrim ? 128 : 256);
    int curr_y = 0;
    for (int y = 0; y < r.h; y++) {
      const int sy = std::min(src_h - 1, curr_y >> 12);
      curr_y += step_y;
      const int py = r.y0 + y;
      if (py < 0 || py >= SCREEN_H) continue;
      int x_start = 0;
      int x_end = r.w - 1;
      if (r.x0 + x_start < 0) x_start = -r.x0;
      if (r.x0 + x_end >= SCREEN_W) x_end = SCREEN_W - 1 - r.x0;
      const int span_w = x_end - x_start + 1;
      if (span_w <= 0 || span_w > SCREEN_W) continue;
      const uint16_t *src_row = src_data + sy * src_w;
      int curr_x = x_start * step_x;
      for (int i = 0; i < span_w; i++) {
        row_buf[i] = src_row[std::min(src_w - 1, curr_x >> 12)];
        curr_x += step_x;
      }
      dim_rgb565_row_dither(row_buf, span_w, mul_q8, r.x0 + x_start, py);
      blit_rgb565_scanline(it, r.x0 + x_start, py, span_w, row_buf, false);
    }
    fast_hline(it, r.x0 - 1, r.y0 - 1, r.w + 2, esphome::Color(88, 102, 124));
    fast_hline(it, r.x0 - 1, r.y0 + r.h, r.w + 2, esphome::Color(38, 45, 58));
    fast_vline(it, r.x0 - 1, r.y0, r.h, esphome::Color(56, 66, 82));
    fast_vline(it, r.x0 + r.w, r.y0, r.h, esphome::Color(56, 66, 82));
    return;
  }

  const int start_y = is_letterbox ? scale_y(62) : 0;
  const int end_y = is_letterbox ? (show_metadata ? scale_y(342) : scale_y(385)) : SCREEN_H;

  if (src_w == SCREEN_W && src_h == SCREEN_H) {
    for (int y = start_y; y < end_y; y++) {
      const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
      int start_x = 0, end_x = 0;
      art_row_span(y, cx, start_x, end_x);
      const int span_w = end_x - start_x + 1;
      if (span_w <= 0) continue;
      const uint16_t *src_row = src_data + y * SCREEN_W;
      if (mul_q8 >= 256) {
        blit_rgb565_scanline(it, start_x, y, span_w, src_row + start_x, false);
      } else {
        std::memcpy(row_buf, src_row + start_x, static_cast<size_t>(span_w) * sizeof(uint16_t));
        dim_rgb565_row_dither(row_buf, span_w, mul_q8, start_x, y);
        blit_rgb565_scanline(it, start_x, y, span_w, row_buf, false);
      }
    }
  } else {
    const int view_h = std::max(1, end_y - start_y);
    const int step_x = (src_w << 12) / SCREEN_W;
    const int step_y = (src_h << 12) / view_h;
    const int step_fp = std::min(step_x, step_y);
    const int vis_src_w_fp = SCREEN_W * step_fp;
    const int vis_src_h_fp = view_h * step_fp;
    const int src_x0_fp = std::max(0, ((src_w << 12) - vis_src_w_fp) / 2);
    const int src_y0_fp = std::max(0, ((src_h << 12) - vis_src_h_fp) / 2);

    static uint16_t sx_lut[SCREEN_W];
    static uint16_t scaled_row[SCREEN_W];
    {
      int curr_x = src_x0_fp;
      for (int x = 0; x < SCREEN_W; x++) {
        sx_lut[x] = static_cast<uint16_t>(std::min(src_w - 1, curr_x >> 12));
        curr_x += step_fp;
      }
    }

    int curr_y = src_y0_fp;
    int prev_sy = -1;
    for (int y = start_y; y < end_y; y++) {
      const int sy = std::min(src_h - 1, std::max(0, curr_y >> 12));
      curr_y += step_fp;
      const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
      int start_x = 0, end_x = 0;
      art_row_span(y, cx, start_x, end_x);
      const int span_w = end_x - start_x + 1;
      if (span_w <= 0) continue;
      if (sy != prev_sy) {
        const uint16_t *src_row = src_data + sy * src_w;
        for (int x = start_x; x <= end_x; x++) {
          scaled_row[x] = src_row[sx_lut[x]];
        }
        prev_sy = sy;
      }
      if (mul_q8 >= 256) {
        blit_rgb565_scanline(it, start_x, y, span_w, scaled_row + start_x, false);
      } else {
        std::memcpy(row_buf + start_x, scaled_row + start_x, static_cast<size_t>(span_w) * sizeof(uint16_t));
        dim_rgb565_row_dither(row_buf + start_x, span_w, mul_q8, start_x, y);
        blit_rgb565_scanline(it, start_x, y, span_w, row_buf + start_x, false);
      }
    }
  }

  if (is_letterbox) {
    int x0_t = 0, x1_t = 0, x0_b = 0, x1_b = 0;
    art_row_span(start_y, cx, x0_t, x1_t);
    art_row_span(end_y - 1, cx, x0_b, x1_b);
    if (x1_t > x0_t) fast_hline(it, x0_t, start_y, x1_t - x0_t + 1, esphome::Color(68, 80, 98));
    if (x1_b > x0_b) fast_hline(it, x0_b, end_y - 1, x1_b - x0_b + 1, esphome::Color(46, 54, 68));
  }
}

constexpr int kArtCacheEntries = 3;

ArtCanvasCache *art_canvas_entries() {
#ifdef USE_ESP32
  static ArtCanvasCache *entries = static_cast<ArtCanvasCache *>(
      heap_caps_calloc(kArtCacheEntries, sizeof(ArtCanvasCache), MALLOC_CAP_SPIRAM));
  return entries;
#else
  return nullptr;
#endif
}

uint32_t art_sample_hash(const uint16_t *data, int w, int h) {
  uint32_t hash = 2166136261U ^ (static_cast<uint32_t>(w) * 31U) ^ (static_cast<uint32_t>(h) << 16);
  const uint32_t n = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
  for (uint32_t i = 0; i < 64; i++) {
    const uint32_t idx = static_cast<uint32_t>((static_cast<uint64_t>(i) * (n - 1)) / 63U);
    hash = (hash ^ data[idx]) * 16777619U;
  }
  return hash;
}

void art_canvas_spans(ArtCanvasCache &e, int cx, int src_w, int src_h, bool is_standard,
                      bool is_letterbox, bool apply_scrim, bool show_metadata) {
  for (int y = 0; y < SCREEN_H; y++) {
    e.row_x0[y] = 0;
    e.row_len[y] = 0;
  }
  if (is_standard) {
    const ArtRect r = art_standard_rect(cx, src_w, src_h, show_metadata, apply_scrim);
    const int x0 = std::max(0, r.x0 - 1);
    const int x1 = std::min(SCREEN_W - 1, r.x0 + r.w);
    for (int y = std::max(0, r.y0 - 1); y <= std::min(SCREEN_H - 1, r.y0 + r.h); y++) {
      e.row_x0[y] = static_cast<int16_t>(x0);
      e.row_len[y] = static_cast<int16_t>(std::max(0, x1 - x0 + 1));
    }
    return;
  }
  const int start_y = is_letterbox ? scale_y(62) : 0;
  const int end_y = is_letterbox ? (show_metadata ? scale_y(342) : scale_y(385)) : SCREEN_H;
  for (int y = std::max(0, start_y); y < std::min(SCREEN_H, end_y); y++) {
    int start_x = 0, end_x = 0;
    art_row_span(y, cx, start_x, end_x);
    e.row_x0[y] = static_cast<int16_t>(start_x);
    e.row_len[y] = static_cast<int16_t>(std::max(0, end_x - start_x + 1));
  }
}

}  // namespace

ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim) {
  const int box = STD_ART_SIZE;
  const int art_cy = (show_metadata || apply_scrim) ? scale_y(198) : SCREEN_CY;
  int w = box;
  int h = box;
  if (src_w > src_h) {
    h = std::max(1, (box * src_h) / src_w);
  } else if (src_h > src_w) {
    w = std::max(1, (box * src_w) / src_h);
  }
  return {cx - w / 2, art_cy - h / 2, w, h};
}

void art_canvas_drop(const void *data) {
#ifdef USE_ESP32
  ArtCanvasCache *entries = art_canvas_entries();
  if (entries == nullptr || data == nullptr) return;
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = entries[i];
    if (e.data != data) continue;
    heap_caps_free(e.px);
    e.px = nullptr;
    e.data = nullptr;
    e.last_use = 0;
  }
#else
  (void)data;
#endif
}

esphome::Color sample_image_ambient_tint_view(ArtImageView img, esphome::Color base_bg) {
  if (img.data == nullptr || img.width < 16 || img.height < 16) return base_bg;
  uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
  for (int gy = 1; gy <= 4; gy++) {
    const int sy = (img.height * gy) / 5;
    const uint16_t *row = img.data + sy * img.width;
    for (int gx = 1; gx <= 4; gx++) {
      const uint16_t p = row[(img.width * gx) / 5];
      sum_r += (p & 0xF800U) >> 8;
      sum_g += (p & 0x07E0U) >> 3;
      sum_b += (p & 0x001FU) << 3;
    }
  }
  const uint8_t avg_r = static_cast<uint8_t>(sum_r >> 4);
  const uint8_t avg_g = static_cast<uint8_t>(sum_g >> 4);
  const uint8_t avg_b = static_cast<uint8_t>(sum_b >> 4);
  return esphome::Color(
      static_cast<uint8_t>(std::min(68, static_cast<int>(base_bg.r) + (avg_r >> 3))),
      static_cast<uint8_t>(std::min(74, static_cast<int>(base_bg.g) + (avg_g >> 3))),
      static_cast<uint8_t>(std::min(88, static_cast<int>(base_bg.b) + (avg_b >> 3))));
}

bool render_album_art_canvas_view(esphome::display::Display &it, ArtImageView target_art, int cx,
                                  bool is_standard, bool is_letterbox, bool apply_scrim,
                                  bool show_metadata, int dim_level) {
  if (!target_art.valid()) return false;
  const uint16_t *src_data = target_art.data;
  const int src_w = target_art.width;
  const int src_h = target_art.height;

#if LATTER_ROUND_DISPLAY
  frame_base(it, kChromeShadow);
#else
  if (!is_standard && !is_letterbox) {
    frame_base_claim();
  } else {
    frame_base(it, kChromeShadow);
  }
#endif
  struct ArtTimer {
    uint64_t t0;
    ~ArtTimer() { prof_art_end(t0); }
  } art_timer{prof_section_begin()};

  uint16_t *fb = active_frame_buf16();
  ArtCanvasCache *entries = art_canvas_entries();
  if (fb == nullptr || entries == nullptr) {
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
    backdrop_active() = true;
    return true;
  }

  const uint32_t mode = (is_standard ? 1U : 0U) | (is_letterbox ? 2U : 0U) | (apply_scrim ? 4U : 0U) |
                        (show_metadata ? 8U : 0U) | (static_cast<uint32_t>(dim_level & 0xF) << 4) |
                        (static_cast<uint32_t>(cx & 0xFFFF) << 8) |
                        (static_cast<uint32_t>(active_frame_endian_mode() & 0x7) << 24);
  const uint32_t sample = art_sample_hash(src_data, src_w, src_h);
  static uint32_t use_seq = 0;
  ++use_seq;

  ArtCanvasCache *hit = nullptr;
  ArtCanvasCache *victim = &entries[0];
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = entries[i];
    if (e.px != nullptr && e.data == src_data && e.w == src_w && e.h == src_h &&
        e.sample == sample && e.mode == mode) {
      hit = &e;
      break;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }

  if (hit == nullptr) {
#ifdef USE_ESP32
    if (victim->px == nullptr) {
      victim->px = static_cast<uint16_t *>(
          heap_caps_malloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H, MALLOC_CAP_SPIRAM));
    }
#endif
    if (victim->px == nullptr) {
      draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
      backdrop_active() = true;
      return true;
    }
    active_frame_buf16() = victim->px;
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
    active_frame_buf16() = fb;
    art_canvas_spans(*victim, cx, src_w, src_h, is_standard, is_letterbox, apply_scrim, show_metadata);
    victim->data = src_data;
    victim->w = static_cast<int16_t>(src_w);
    victim->h = static_cast<int16_t>(src_h);
    victim->sample = sample;
    victim->mode = mode;
    hit = victim;
  }
  hit->last_use = use_seq;

  const int y_lo = dirty_band().is_full() ? 0 : std::max(0, dirty_band().y0());
  const int y_hi = dirty_band().is_full() ? (SCREEN_H - 1) : std::min(SCREEN_H - 1, dirty_band().y1());
  for (int y = y_lo; y <= y_hi; y++) {
    const int len = hit->row_len[y];
    if (len <= 0) continue;
    const size_t off = static_cast<size_t>(y) * SCREEN_W + hit->row_x0[y];
    std::memcpy(fb + off, hit->px + off, static_cast<size_t>(len) * sizeof(uint16_t));
  }
  backdrop_active() = true;
  return true;
}

bool render_page_wallpaper_base_view(esphome::display::Display &it, ArtImageView wallpaper_img,
                                     bool has_wallpaper, int cx, esphome::Color col_bg, int dim_level) {
  if (has_wallpaper && render_album_art_canvas_view(it, wallpaper_img, cx, false, false, true, false, dim_level)) {
    return true;
  }
  frame_base(it, col_bg);
  return false;
}

// ------------------------------------------------------------------------------
// 5. Dirty-Band Aware Frame Base & FrameProfileScope
// ------------------------------------------------------------------------------
FrameBaseState &frame_base_state() {
  static FrameBaseState st;
  return st;
}

void frame_base_begin() {
  frame_base_state().painted = false;
  frame_base_state().has_color = false;
  backdrop_active() = false;
}

void frame_base_skip() {
  frame_base_state().painted = true;
}

void frame_base_claim() {
  frame_base_state().painted = true;
}

bool frame_base_impl(esphome::display::Display &it, esphome::Color c) {
  auto &st = frame_base_state();
  if (st.painted) return false;
  st.painted = true;
  const uint16_t hw = encode_hw_rgb565(c);
  st.color_hw = hw;
  const bool full = dirty_band().is_full();
  st.has_color = full;
  if (fb_clear_consume(hw)) return true;

  const uint64_t t0 = profiler_enabled() ? prof_now_us() : 0;
  if (full) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, c);
  } else {
    const int y0 = std::max(0, dirty_band().y0());
    const int y1 = std::min(SCREEN_H - 1, dirty_band().y1());
    if (y1 >= y0) {
      fill_rect_row_major(it, 0, y0, SCREEN_W, y1 - y0 + 1, c);
    }
  }
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::BASE)] += static_cast<uint32_t>(prof_now_us() - t0);
  return true;
}

FrameProfileScope::FrameProfileScope(int p, int s, int pw) : page(p), sub(s), pwr(pw) {
  fb_clear_wait();
  t0_us = prof_now_us();
  const uint32_t start_m = esphome::millis();
  last_disp_update_ms() = start_m;
  last_ambient_anim_ms() = start_m;
}

FrameProfileScope::~FrameProfileScope() {
  if (t0_us == 0) return;
  fb_clear_frame_end();
  auto &fbs = frame_base_state();
  if (!fbs.painted && !fbs.warned) {
    fbs.warned = true;
    ESP_LOGW("disp", "Frame on page %d sub %d drew without frame_base(); it shows stale pixels", page, sub);
  }
  const uint32_t render_us = static_cast<uint32_t>(prof_now_us() - t0_us);
  const uint32_t now_m = esphome::millis();
  last_frame_duration_ms() = render_us / 1000U;
  auto &fbc_post = fb_clear_state();
  if (fbc_post.owner != nullptr) {
    fbc_post.post_stamp_pending = true;
    DisplayUpdateScheduler::request_post_frame(fbc_post.owner, &fb_clear_after_update);
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

  const bool page_changed = (page != fr.last_logged_page || sub != fr.last_logged_sub);
  if (page_changed || is_slow) {
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
                     static_cast<unsigned long>(fr.window_frames), fps,
                     static_cast<unsigned long>(avg_us),
                     static_cast<unsigned long>(fr.max_render_us), fr.max_render_page, fr.max_render_sub,
                     static_cast<unsigned long>(fr.slow_frames),
                     static_cast<unsigned long>(fbc.hits),
                     static_cast<unsigned long>(fbc.hits + fbc.misses),
                     fbc.why != nullptr ? " dma_off=" : "",
                     fbc.why != nullptr ? fbc.why : "",
                     fbc.why != nullptr ? fb_clear_why_err_str() : "");
    fbc.hits = 0;
    fbc.misses = 0;
    uint32_t *sect = prof_sect_us();
    uint32_t any_sect = 0;
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
    fr.window_frames = 0;
    fr.window_render_us = 0;
    fr.max_render_us = 0;
    fr.slow_frames = 0;
    fr.last_summary_ms = now_m;
  }
}

}  // namespace latter_ui
