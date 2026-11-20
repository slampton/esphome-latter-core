#if __has_include("hal_video_stream.h")
#include "hal_video_stream.h"
#else
#include "../../include/hal_video_stream.h"
#endif

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif
#if __has_include("esphome/core/static_task.h")
#include "esphome/core/static_task.h"
#define LATTER_VIDEO_HAS_STATIC_TASK 1
#else
#define LATTER_VIDEO_HAS_STATIC_TASK 0
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#if defined(USE_ESP32) && !defined(LATTER_HOST_TEST) && __has_include(<esp_http_client.h>) && \
    __has_include(<freertos/semphr.h>)
#define LATTER_VIDEO_ESP32_NET 1

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if __has_include(<esp_crt_bundle.h>) && defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
#include <esp_crt_bundle.h>
#define LATTER_VIDEO_HAS_CRT_BUNDLE 1
#else
#define LATTER_VIDEO_HAS_CRT_BUNDLE 0
#endif

#if LATTER_CHIP_P4 && __has_include(<driver/jpeg_decode.h>)
#include <driver/jpeg_decode.h>
#define LATTER_VIDEO_HW_JPEG 1
#else
#define LATTER_VIDEO_HW_JPEG 0
#endif

#if !LATTER_VIDEO_HW_JPEG && __has_include("JPEGDEC.h")
#include "JPEGDEC.h"
#define LATTER_VIDEO_HAS_JPEGDEC 1
#else
#define LATTER_VIDEO_HAS_JPEGDEC 0
#endif

#if !LATTER_VIDEO_HW_JPEG && !LATTER_VIDEO_HAS_JPEGDEC && __has_include(<rom/tjpgd.h>)
#include <rom/tjpgd.h>
#define LATTER_VIDEO_HAS_ROM_TJPGD 1
#elif !LATTER_VIDEO_HW_JPEG && !LATTER_VIDEO_HAS_JPEGDEC && __has_include("tjpgd.h")
#include "tjpgd.h"
#define LATTER_VIDEO_HAS_ROM_TJPGD 1
#else
#define LATTER_VIDEO_HAS_ROM_TJPGD 0
#endif

#else
#define LATTER_VIDEO_ESP32_NET 0
#define LATTER_VIDEO_HW_JPEG 0
#define LATTER_VIDEO_HAS_JPEGDEC 0
#define LATTER_VIDEO_HAS_ROM_TJPGD 0
#endif

namespace latter_ui {

#if LATTER_VIDEO_ESP32_NET

namespace {

constexpr const char *kVideoTag = "latter_video";
constexpr size_t kVideoHttpRxBufBytes = 4096U;
constexpr uint32_t kVideoTaskStackBytes = 8192U;
#ifdef tskIDLE_PRIORITY
constexpr UBaseType_t kVideoTaskPriority = tskIDLE_PRIORITY + 2U;
#else
constexpr UBaseType_t kVideoTaskPriority = 2U;
#endif

inline uint32_t video_now_ms() noexcept {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

inline uint64_t video_now_us() noexcept {
  return static_cast<uint64_t>(esp_timer_get_time());
}

struct HttpHeaderCtx {
  std::string content_type;
};

esp_err_t video_http_event_handler(esp_http_client_event_t *evt) {
  if (evt == nullptr || evt->user_data == nullptr) return ESP_OK;
  if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != nullptr && evt->header_value != nullptr) {
    if (video_ascii_iequals(evt->header_key, "content-type")) {
      auto *ctx = static_cast<HttpHeaderCtx *>(evt->user_data);
      ctx->content_type = evt->header_value;
    }
  }
  return ESP_OK;
}

#if LATTER_VIDEO_HAS_JPEGDEC
struct JpegDecDrawCtx {
  uint16_t *dst = nullptr;
  int img_w = 0;
  int img_h = 0;
  int max_w = 0;
  int max_h = 0;
};

// Accessed strictly from the single `latter_video` worker task.
JpegDecDrawCtx *g_active_jpegdec_ctx = nullptr;

int video_jpegdec_draw_cb(JPEGDRAW *pDraw) {
  JpegDecDrawCtx *ctx = g_active_jpegdec_ctx;
  if (ctx == nullptr || ctx->dst == nullptr || pDraw == nullptr || pDraw->pPixels == nullptr) return 0;
  const int x0 = pDraw->x;
  const int y0 = pDraw->y;
  if (x0 >= ctx->img_w || y0 >= ctx->img_h) return 1;
  const int src_stride = pDraw->iWidth;
  if (src_stride <= 0 || pDraw->iHeight <= 0) return 1;
  int copy_w = (pDraw->iWidthUsed > 0 && pDraw->iWidthUsed < src_stride) ? pDraw->iWidthUsed : src_stride;
  if (x0 + copy_w > ctx->img_w) copy_w = ctx->img_w - x0;
  int copy_h = pDraw->iHeight;
  if (y0 + copy_h > ctx->img_h) copy_h = ctx->img_h - y0;
  if (copy_w <= 0 || copy_h <= 0) return 1;

  for (int r = 0; r < copy_h; ++r) {
    const uint16_t *src_row = pDraw->pPixels + static_cast<size_t>(r) * static_cast<size_t>(src_stride);
    uint16_t *dst_row = ctx->dst + static_cast<size_t>(y0 + r) * static_cast<size_t>(ctx->img_w) + x0;
    std::memcpy(dst_row, src_row, static_cast<size_t>(copy_w) * sizeof(uint16_t));
  }
  return 1;
}
#endif

#if LATTER_VIDEO_HAS_ROM_TJPGD
constexpr size_t kTjpgdWorkBytes = 4096U;

struct RomTjpgdCtx {
  const uint8_t *src = nullptr;
  size_t src_len = 0U;
  size_t src_pos = 0U;
  uint16_t *dst = nullptr;
  int img_w = 0;
  int img_h = 0;
};

UINT video_tjpgd_in_cb(JDEC *jd, BYTE *buff, UINT nbyte) {
  auto *ctx = static_cast<RomTjpgdCtx *>(jd->device);
  if (ctx == nullptr || ctx->src == nullptr || ctx->src_pos >= ctx->src_len) return 0;
  const size_t avail = ctx->src_len - ctx->src_pos;
  const size_t take = (static_cast<size_t>(nbyte) < avail) ? static_cast<size_t>(nbyte) : avail;
  if (buff != nullptr && take > 0U) {
    std::memcpy(buff, ctx->src + ctx->src_pos, take);
  }
  ctx->src_pos += take;
  return static_cast<UINT>(take);
}

int video_tjpgd_out_cb(JDEC *jd, void *bitmap, JRECT *rect) {
  auto *ctx = static_cast<RomTjpgdCtx *>(jd->device);
  if (ctx == nullptr || ctx->dst == nullptr || bitmap == nullptr || rect == nullptr) return 0;
  const int rx0 = static_cast<int>(rect->left);
  const int ry0 = static_cast<int>(rect->top);
  const int rx1 = static_cast<int>(rect->right);
  const int ry1 = static_cast<int>(rect->bottom);
  const int bw = rx1 - rx0 + 1;
  const int bh = ry1 - ry0 + 1;
  if (bw <= 0 || bh <= 0) return 1;

  // ESP32-S3 ROM TJpgDec is compiled with JD_FORMAT == 0 (RGB888, 3 bytes/pixel).
  const auto *rgb = static_cast<const uint8_t *>(bitmap);
  for (int r = 0; r < bh; ++r) {
    const int dy = ry0 + r;
    if (dy < 0 || dy >= ctx->img_h) continue;
    uint16_t *dst_row = ctx->dst + static_cast<size_t>(dy) * static_cast<size_t>(ctx->img_w);
    const uint8_t *src_row = rgb + static_cast<size_t>(r * bw) * 3U;
    for (int c = 0; c < bw; ++c) {
      const int dx = rx0 + c;
      if (dx < 0 || dx >= ctx->img_w) continue;
      const uint16_t r5 = static_cast<uint16_t>(src_row[c * 3 + 0] & 0xF8U) << 8;
      const uint16_t g6 = static_cast<uint16_t>(src_row[c * 3 + 1] & 0xFCU) << 3;
      const uint16_t b5 = static_cast<uint16_t>(src_row[c * 3 + 2]) >> 3;
      dst_row[dx] = static_cast<uint16_t>(r5 | g6 | b5);
    }
  }
  return 1;
}
#endif

}  // namespace

struct LatterVideoClient::Impl {
  LatterVideoClient *owner{nullptr};

  SemaphoreHandle_t ctrl_mtx{nullptr};
  SemaphoreHandle_t stats_mtx{nullptr};
#if !LATTER_VIDEO_HW_JPEG
  SemaphoreHandle_t frame_mtx{nullptr};
#else
  portMUX_TYPE idx_mux = portMUX_INITIALIZER_UNLOCKED;
  int front_idx{-1};
  int reader_idx{-1};
#endif

#if LATTER_VIDEO_HAS_STATIC_TASK
  esphome::StaticTask static_task{};
#endif
  TaskHandle_t task_handle{nullptr};
  bool task_started{false};

  // Requested stream parameters (protected by ctrl_mtx).
  std::string req_url{};
  uint16_t req_max_w{0U};
  uint16_t req_max_h{0U};

  // Worker-owned PSRAM resources (allocated only while active, freed on IDLE).
  uint8_t *http_rx_buf{nullptr};
  uint8_t *jpeg_buf{nullptr};
  size_t jpeg_buf_alloc_bytes{0U};

  uint16_t *buf[2]{nullptr, nullptr};
  size_t buf_alloc_bytes{0U};
  uint16_t alloc_w{0U};
  uint16_t alloc_h{0U};
  int buf_w[2]{0, 0};
  int buf_h[2]{0, 0};

#if LATTER_VIDEO_HW_JPEG
  jpeg_decoder_handle_t hw_dec{nullptr};
#endif
#if LATTER_VIDEO_HAS_JPEGDEC
  JPEGDEC *jpegdec{nullptr};
#endif
#if LATTER_VIDEO_HAS_ROM_TJPGD
  void *tjpgd_work{nullptr};
#endif

  // Stats snapshot (protected by stats_mtx).
  VideoStreamStats stats{};

  // Rolling 5s telemetry window (worker-only).
  uint32_t win_start_ms{0U};
  uint32_t win_frames{0U};
  uint64_t win_decode_us_sum{0ULL};
  uint32_t win_decode_us_max{0U};

  bool init_sync_primitives() noexcept {
    if (ctrl_mtx == nullptr) ctrl_mtx = xSemaphoreCreateMutex();
    if (stats_mtx == nullptr) stats_mtx = xSemaphoreCreateMutex();
#if !LATTER_VIDEO_HW_JPEG
    if (frame_mtx == nullptr) frame_mtx = xSemaphoreCreateMutex();
    return ctrl_mtx != nullptr && stats_mtx != nullptr && frame_mtx != nullptr;
#else
    return ctrl_mtx != nullptr && stats_mtx != nullptr;
#endif
  }

  void ensure_worker_started() noexcept {
    if (task_started) return;
#if LATTER_VIDEO_HAS_STATIC_TASK
#if defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
    task_started = static_task.create(&worker_entry, "latter_video", kVideoTaskStackBytes, this,
                                      kVideoTaskPriority, /*use_psram=*/true);
#endif
    if (!task_started) {
      task_started = static_task.create(&worker_entry, "latter_video", kVideoTaskStackBytes, this,
                                        kVideoTaskPriority, /*use_psram=*/false);
    }
    if (task_started) {
      task_handle = static_task.get_handle();
    }
#else
    task_started = (xTaskCreate(&worker_entry, "latter_video", kVideoTaskStackBytes, this,
                                kVideoTaskPriority, &task_handle) == pdPASS);
#endif
    if (!task_started) {
      ESP_LOGE(kVideoTag, "Failed to start latter_video worker task");
      owner->state_.store(static_cast<uint8_t>(VideoStreamState::ERROR), std::memory_order_release);
    }
  }

  void wake_worker() noexcept {
    if (task_started && task_handle != nullptr) {
      xTaskNotifyGive(task_handle);
    }
  }

  void reset_stats_window(uint32_t now_ms, bool reset_totals) noexcept {
    win_start_ms = now_ms;
    win_frames = 0U;
    win_decode_us_sum = 0ULL;
    win_decode_us_max = 0U;
    if (stats_mtx != nullptr && xSemaphoreTake(stats_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (reset_totals) {
        stats = VideoStreamStats{};
        stats.hw_jpeg = (LATTER_VIDEO_HW_JPEG != 0);
      } else {
        stats.fps_x10 = 0U;
        stats.avg_decode_ms = 0U;
        stats.max_decode_ms = 0U;
      }
      xSemaphoreGive(stats_mtx);
    }
  }

  void free_stream_buffers() noexcept {
    owner->has_frame_.store(false, std::memory_order_release);
    owner->frame_dirty_.store(false, std::memory_order_release);

#if LATTER_VIDEO_HW_JPEG
    portENTER_CRITICAL(&idx_mux);
    front_idx = -1;
    portEXIT_CRITICAL(&idx_mux);

    // front_idx = -1 and has_frame_ = false are already committed, so no new acquire_frame()
    // can borrow a buffer. Wait unconditionally for any in-flight UI blit to release_frame().
    for (;;) {
      portENTER_CRITICAL(&idx_mux);
      const bool busy = (reader_idx >= 0);
      portEXIT_CRITICAL(&idx_mux);
      if (!busy) break;
      vTaskDelay(1);
    }

    if (hw_dec != nullptr) {
      jpeg_del_decoder_engine(hw_dec);
      hw_dec = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
      if (buf[i] != nullptr) {
        heap_caps_free(buf[i]);
        buf[i] = nullptr;
      }
      buf_w[i] = 0;
      buf_h[i] = 0;
    }
    if (jpeg_buf != nullptr) {
      heap_caps_free(jpeg_buf);
      jpeg_buf = nullptr;
    }
#else
    if (frame_mtx != nullptr) {
      xSemaphoreTake(frame_mtx, portMAX_DELAY);
    }
    if (buf[0] != nullptr) {
      heap_caps_free(buf[0]);
      buf[0] = nullptr;
    }
    buf_w[0] = 0;
    buf_h[0] = 0;
    if (jpeg_buf != nullptr) {
      heap_caps_free(jpeg_buf);
      jpeg_buf = nullptr;
    }
#if LATTER_VIDEO_HAS_JPEGDEC
    if (jpegdec != nullptr) {
      jpegdec->~JPEGDEC();
      heap_caps_free(jpegdec);
      jpegdec = nullptr;
    }
#endif
#if LATTER_VIDEO_HAS_ROM_TJPGD
    if (tjpgd_work != nullptr) {
      heap_caps_free(tjpgd_work);
      tjpgd_work = nullptr;
    }
#endif
    if (frame_mtx != nullptr) {
      xSemaphoreGive(frame_mtx);
    }
#endif

    if (http_rx_buf != nullptr) {
      heap_caps_free(http_rx_buf);
      http_rx_buf = nullptr;
    }
    jpeg_buf_alloc_bytes = 0U;
    buf_alloc_bytes = 0U;
    alloc_w = 0U;
    alloc_h = 0U;
  }

  bool ensure_stream_buffers(uint16_t max_w, uint16_t max_h) noexcept {
    const uint16_t clamped_w = std::clamp<uint16_t>(max_w, 16U, kVideoMaxViewportDim);
    const uint16_t clamped_h = std::clamp<uint16_t>(max_h, 16U, kVideoMaxViewportDim);
#if LATTER_VIDEO_HW_JPEG
    const uint16_t want_w = static_cast<uint16_t>((clamped_w + 15U) & ~15U);
    const uint16_t want_h = static_cast<uint16_t>((clamped_h + 15U) & ~15U);
#else
    const uint16_t want_w = clamped_w;
    const uint16_t want_h = clamped_h;
#endif
    const size_t want_frame_bytes = static_cast<size_t>(want_w) * static_cast<size_t>(want_h) * sizeof(uint16_t);

    if (alloc_w == want_w && alloc_h == want_h && jpeg_buf != nullptr && buf[0] != nullptr && http_rx_buf != nullptr
#if LATTER_VIDEO_HW_JPEG
        && buf[1] != nullptr && hw_dec != nullptr
#endif
    ) {
      return true;
    }

    free_stream_buffers();

    http_rx_buf = static_cast<uint8_t *>(
        heap_caps_malloc(kVideoHttpRxBufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (http_rx_buf == nullptr) {
      ESP_LOGE(kVideoTag, "PSRAM alloc failed for HTTP RX buffer (%uB)",
               static_cast<unsigned>(kVideoHttpRxBufBytes));
      free_stream_buffers();
      return false;
    }

#if LATTER_VIDEO_HW_JPEG
    jpeg_decode_engine_cfg_t eng_cfg{};
    eng_cfg.intr_priority = 0;
    eng_cfg.timeout_ms = 200;
    const esp_err_t eng_err = jpeg_new_decoder_engine(&eng_cfg, &hw_dec);
    if (eng_err != ESP_OK || hw_dec == nullptr) {
      ESP_LOGE(kVideoTag, "HW JPEG engine init failed (err=%d)", static_cast<int>(eng_err));
      free_stream_buffers();
      return false;
    }

    jpeg_decode_memory_alloc_cfg_t tx_cfg{};
    tx_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    jpeg_buf = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(kMaxJpegBytes, &tx_cfg, &jpeg_buf_alloc_bytes));
    if (jpeg_buf == nullptr) {
      ESP_LOGE(kVideoTag, "HW JPEG input PSRAM alloc failed (%uB)", static_cast<unsigned>(kMaxJpegBytes));
      free_stream_buffers();
      return false;
    }

    jpeg_decode_memory_alloc_cfg_t rx_cfg{};
    rx_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t rx_alloc0 = 0U;
    size_t rx_alloc1 = 0U;
    buf[0] = static_cast<uint16_t *>(jpeg_alloc_decoder_mem(want_frame_bytes, &rx_cfg, &rx_alloc0));
    buf[1] = static_cast<uint16_t *>(jpeg_alloc_decoder_mem(want_frame_bytes, &rx_cfg, &rx_alloc1));
    if (buf[0] == nullptr || buf[1] == nullptr) {
      ESP_LOGE(kVideoTag, "HW JPEG double-buffer PSRAM alloc failed (2x %uB)",
               static_cast<unsigned>(want_frame_bytes));
      free_stream_buffers();
      return false;
    }
    buf_alloc_bytes = std::min(rx_alloc0, rx_alloc1);
#else
    jpeg_buf = static_cast<uint8_t *>(heap_caps_malloc(kMaxJpegBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    buf[0] = static_cast<uint16_t *>(heap_caps_malloc(want_frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (jpeg_buf == nullptr || buf[0] == nullptr) {
      ESP_LOGE(kVideoTag, "PSRAM alloc failed for S3 video buffers (jpeg=%uB fb=%uB)",
               static_cast<unsigned>(kMaxJpegBytes), static_cast<unsigned>(want_frame_bytes));
      free_stream_buffers();
      return false;
    }
    jpeg_buf_alloc_bytes = kMaxJpegBytes;
    buf_alloc_bytes = want_frame_bytes;

#if LATTER_VIDEO_HAS_JPEGDEC
    void *dec_mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dec_mem == nullptr) {
      ESP_LOGE(kVideoTag, "PSRAM alloc failed for JPEGDEC (%uB)", static_cast<unsigned>(sizeof(JPEGDEC)));
      free_stream_buffers();
      return false;
    }
    jpegdec = new (dec_mem) JPEGDEC();
#elif LATTER_VIDEO_HAS_ROM_TJPGD
    tjpgd_work = heap_caps_malloc(kTjpgdWorkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tjpgd_work == nullptr) {
      ESP_LOGE(kVideoTag, "PSRAM alloc failed for TJpgDec workspace");
      free_stream_buffers();
      return false;
    }
#endif
#endif

    alloc_w = want_w;
    alloc_h = want_h;
    return true;
  }

  void record_dropped_frame() noexcept {
    if (stats_mtx != nullptr && xSemaphoreTake(stats_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
      ++stats.frames_dropped;
      xSemaphoreGive(stats_mtx);
    }
  }

  void record_decoded_frame(uint32_t decode_us, int out_w, int out_h, uint32_t now_ms) noexcept {
    ++win_frames;
    win_decode_us_sum += decode_us;
    if (decode_us > win_decode_us_max) win_decode_us_max = decode_us;

    const uint32_t decode_ms = (decode_us + 500U) / 1000U;
    const uint32_t win_el = (now_ms >= win_start_ms) ? (now_ms - win_start_ms) : 0U;
    const uint16_t cur_avg_ms = static_cast<uint16_t>(
        std::min<uint64_t>(65535ULL, (win_decode_us_sum / win_frames + 500ULL) / 1000ULL));
    const uint16_t cur_max_ms = static_cast<uint16_t>(
        std::min<uint32_t>(65535U, (win_decode_us_max + 500U) / 1000U));
    const uint16_t cur_fps_x10 =
        (win_el >= 250U)
            ? static_cast<uint16_t>(std::min<uint64_t>(65535ULL, (static_cast<uint64_t>(win_frames) * 10000ULL) / win_el))
            : 0U;

    VideoStreamStats snap{};
    if (stats_mtx != nullptr && xSemaphoreTake(stats_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
      ++stats.frames_decoded;
      stats.frame_w = static_cast<uint16_t>(out_w);
      stats.frame_h = static_cast<uint16_t>(out_h);
      stats.hw_jpeg = (LATTER_VIDEO_HW_JPEG != 0);
      stats.avg_decode_ms = cur_avg_ms;
      stats.max_decode_ms = std::max(stats.max_decode_ms, static_cast<uint16_t>(std::min<uint32_t>(65535U, decode_ms)));
      if (cur_fps_x10 > 0U) stats.fps_x10 = cur_fps_x10;
      snap = stats;
      xSemaphoreGive(stats_mtx);
    }

    if (win_el >= kVideoProfIntervalMs && win_frames > 0U) {
      LATTER_PROF_LOGI("[PROF:CAM] fps=%.1f decode_avg=%ums decode_max=%ums decoded=%lu dropped=%lu reconn=%lu "
                       "res=%ux%u hw=%d int_free=%uKB psram_free=%uKB",
                       static_cast<double>(cur_fps_x10) * 0.1,
                       static_cast<unsigned>(cur_avg_ms),
                       static_cast<unsigned>(cur_max_ms),
                       static_cast<unsigned long>(snap.frames_decoded),
                       static_cast<unsigned long>(snap.frames_dropped),
                       static_cast<unsigned long>(snap.reconnects),
                       static_cast<unsigned>(out_w),
                       static_cast<unsigned>(out_h),
                       LATTER_VIDEO_HW_JPEG ? 1 : 0,
                       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U),
                       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
      win_start_ms = now_ms;
      win_frames = 0U;
      win_decode_us_sum = 0ULL;
      win_decode_us_max = 0U;
    }
  }

  bool decode_and_publish_frame(const uint8_t *jpeg_data, size_t jpeg_len, uint32_t local_gen) noexcept {
    if (jpeg_data == nullptr || jpeg_len < 4U) {
      record_dropped_frame();
      return false;
    }
    if (owner->stop_requested_.load(std::memory_order_acquire) ||
        owner->generation_.load(std::memory_order_acquire) != local_gen) {
      return false;
    }

    const uint64_t t0_us = video_now_us();
    int decoded_w = 0;
    int decoded_h = 0;
    bool ok = false;

#if LATTER_VIDEO_HW_JPEG
    jpeg_decode_picture_info_t pic_info{};
    if (jpeg_decoder_get_info(jpeg_data, static_cast<uint32_t>(jpeg_len), &pic_info) != ESP_OK ||
        pic_info.width == 0U || pic_info.height == 0U) {
      record_dropped_frame();
      return false;
    }
    const uint32_t padded_w = (pic_info.width + 15U) & ~15U;
    const uint32_t padded_h = (pic_info.height + 15U) & ~15U;
    const size_t needed_bytes = static_cast<size_t>(padded_w) * static_cast<size_t>(padded_h) * sizeof(uint16_t);
    if (padded_w > alloc_w || padded_h > alloc_h || needed_bytes > buf_alloc_bytes) {
      record_dropped_frame();
      return false;
    }

    int back_idx = 0;
    bool conflict = true;
    for (int wait_it = 0; wait_it < 40; ++wait_it) {
      portENTER_CRITICAL(&idx_mux);
      const int f = front_idx;
      const int r = reader_idx;
      back_idx = (f == 0) ? 1 : 0;
      conflict = (r == back_idx);
      portEXIT_CRITICAL(&idx_mux);
      if (!conflict) break;
      vTaskDelay(1);
    }
    if (conflict || buf[back_idx] == nullptr) {
      record_dropped_frame();
      return false;
    }

    jpeg_decode_cfg_t dec_cfg{};
    dec_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    // ESP-IDF esp_driver_jpeg defines JPEG_DEC_RGB_ELEMENT_ORDER_BGR as little-endian RGB565
    // (0xF800 = pure Red as uint16_t on little-endian ESP32-P4), matching blit_rgb565_scanline().
    dec_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    dec_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t out_size = 0U;
    const esp_err_t derr = jpeg_decoder_process(
        hw_dec, &dec_cfg, jpeg_data, static_cast<uint32_t>(jpeg_len),
        reinterpret_cast<uint8_t *>(buf[back_idx]), static_cast<uint32_t>(buf_alloc_bytes), &out_size);
    if (derr == ESP_OK && out_size > 0U) {
      decoded_w = static_cast<int>(pic_info.width);
      decoded_h = static_cast<int>(pic_info.height);
      if (padded_w != pic_info.width) {
        // Compact MCU-padded rows in-place so ArtImageView sees tightly packed width * height pixels.
        uint16_t *px = buf[back_idx];
        for (int row = 1; row < decoded_h; ++row) {
          std::memmove(px + static_cast<size_t>(row) * static_cast<size_t>(decoded_w),
                       px + static_cast<size_t>(row) * static_cast<size_t>(padded_w),
                       static_cast<size_t>(decoded_w) * sizeof(uint16_t));
        }
      }
      if (!owner->stop_requested_.load(std::memory_order_acquire) &&
          owner->generation_.load(std::memory_order_acquire) == local_gen) {
        portENTER_CRITICAL(&idx_mux);
        buf_w[back_idx] = decoded_w;
        buf_h[back_idx] = decoded_h;
        front_idx = back_idx;
        portEXIT_CRITICAL(&idx_mux);
        ok = true;
      }
    }
#elif LATTER_VIDEO_HAS_JPEGDEC
    if (jpegdec == nullptr || buf[0] == nullptr) {
      record_dropped_frame();
      return false;
    }
    if (jpegdec->openRAM(const_cast<uint8_t *>(jpeg_data), static_cast<int>(jpeg_len), &video_jpegdec_draw_cb) != 1) {
      record_dropped_frame();
      return false;
    }
    const int raw_w = jpegdec->getWidth();
    const int raw_h = jpegdec->getHeight();
    int scale_opt = 0;
    int eff_w = raw_w;
    int eff_h = raw_h;
    if (raw_w <= 0 || raw_h <= 0) {
      jpegdec->close();
      record_dropped_frame();
      return false;
    }
    if (raw_w > static_cast<int>(alloc_w) || raw_h > static_cast<int>(alloc_h)) {
      if ((raw_w / 2) <= static_cast<int>(alloc_w) && (raw_h / 2) <= static_cast<int>(alloc_h) &&
          raw_w >= 32 && raw_h >= 32) {
        scale_opt = JPEG_SCALE_HALF;
        eff_w = raw_w / 2;
        eff_h = raw_h / 2;
      } else {
        jpegdec->close();
        record_dropped_frame();
        return false;
      }
    }

    jpegdec->setPixelType(RGB565_LITTLE_ENDIAN);
    if (xSemaphoreTake(frame_mtx, portMAX_DELAY) == pdTRUE) {
      if (!owner->stop_requested_.load(std::memory_order_acquire) &&
          owner->generation_.load(std::memory_order_acquire) == local_gen && buf[0] != nullptr) {
        JpegDecDrawCtx ctx{buf[0], eff_w, eff_h, static_cast<int>(alloc_w), static_cast<int>(alloc_h)};
        g_active_jpegdec_ctx = &ctx;
        const int dec_rc = jpegdec->decode(0, 0, scale_opt);
        g_active_jpegdec_ctx = nullptr;
        if (dec_rc == 1) {
          buf_w[0] = eff_w;
          buf_h[0] = eff_h;
          decoded_w = eff_w;
          decoded_h = eff_h;
          ok = true;
        }
      }
      xSemaphoreGive(frame_mtx);
    }
    jpegdec->close();
#elif LATTER_VIDEO_HAS_ROM_TJPGD
    if (tjpgd_work == nullptr || buf[0] == nullptr) {
      record_dropped_frame();
      return false;
    }
    RomTjpgdCtx ctx{jpeg_data, jpeg_len, 0U, buf[0], 0, 0};
    JDEC jdec{};
    if (jd_prepare(&jdec, &video_tjpgd_in_cb, tjpgd_work, static_cast<UINT>(kTjpgdWorkBytes), &ctx) != JDR_OK) {
      record_dropped_frame();
      return false;
    }
    const int raw_w = static_cast<int>(jdec.width);
    const int raw_h = static_cast<int>(jdec.height);
    BYTE scale = 0;
    int eff_w = raw_w;
    int eff_h = raw_h;
    if (raw_w <= 0 || raw_h <= 0) {
      record_dropped_frame();
      return false;
    }
    if (raw_w > static_cast<int>(alloc_w) || raw_h > static_cast<int>(alloc_h)) {
      if ((raw_w >> 1) <= static_cast<int>(alloc_w) && (raw_h >> 1) <= static_cast<int>(alloc_h) &&
          raw_w >= 32 && raw_h >= 32) {
        scale = 1;
        eff_w = raw_w >> 1;
        eff_h = raw_h >> 1;
      } else {
        record_dropped_frame();
        return false;
      }
    }
    ctx.img_w = eff_w;
    ctx.img_h = eff_h;
    if (xSemaphoreTake(frame_mtx, portMAX_DELAY) == pdTRUE) {
      if (!owner->stop_requested_.load(std::memory_order_acquire) &&
          owner->generation_.load(std::memory_order_acquire) == local_gen && buf[0] != nullptr) {
        if (jd_decomp(&jdec, &video_tjpgd_out_cb, scale) == JDR_OK) {
          buf_w[0] = eff_w;
          buf_h[0] = eff_h;
          decoded_w = eff_w;
          decoded_h = eff_h;
          ok = true;
        }
      }
      xSemaphoreGive(frame_mtx);
    }
#else
    (void)t0_us;
#endif

    if (!ok) {
      record_dropped_frame();
      return false;
    }

    const uint32_t decode_us = static_cast<uint32_t>(video_now_us() - t0_us);
    bool published = false;
    if (ctrl_mtx != nullptr && xSemaphoreTake(ctrl_mtx, portMAX_DELAY) == pdTRUE) {
      if (!owner->stop_requested_.load(std::memory_order_acquire) &&
          owner->generation_.load(std::memory_order_acquire) == local_gen) {
        owner->has_frame_.store(true, std::memory_order_release);
        owner->frame_seq_.fetch_add(1U, std::memory_order_release);
        owner->frame_dirty_.store(true, std::memory_order_release);
        owner->state_.store(static_cast<uint8_t>(VideoStreamState::STREAMING), std::memory_order_release);
        published = true;
      }
      xSemaphoreGive(ctrl_mtx);
    }
    if (!published) return false;
    record_decoded_frame(decode_us, decoded_w, decoded_h, video_now_ms());
    return true;
  }

  void run_stream_session(const std::string &url, uint16_t max_w, uint16_t max_h, uint32_t local_gen,
                          uint32_t &reconnect_attempt) {
    if (!ensure_stream_buffers(max_w, max_h)) {
      if (ctrl_mtx != nullptr && xSemaphoreTake(ctrl_mtx, portMAX_DELAY) == pdTRUE) {
        if (!owner->stop_requested_.load(std::memory_order_acquire) &&
            owner->generation_.load(std::memory_order_acquire) == local_gen) {
          owner->state_.store(static_cast<uint8_t>(VideoStreamState::ERROR), std::memory_order_release);
        }
        xSemaphoreGive(ctrl_mtx);
      }
      return;
    }

    HttpHeaderCtx http_ctx{};
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = static_cast<int>(kVideoHttpConnectTimeoutMs);
    cfg.keep_alive_enable = true;
    cfg.buffer_size = static_cast<int>(kVideoHttpRxBufBytes);
    cfg.buffer_size_tx = 1024;
    cfg.event_handler = &video_http_event_handler;
    cfg.user_data = &http_ctx;
#if LATTER_VIDEO_HAS_CRT_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
      ESP_LOGW(kVideoTag, "esp_http_client_init failed");
      return;
    }

    auto cleanup_client = [&]() {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    };

    if (owner->stop_requested_.load(std::memory_order_acquire) ||
        owner->generation_.load(std::memory_order_acquire) != local_gen) {
      cleanup_client();
      return;
    }

    const esp_err_t open_err = esp_http_client_open(client, 0);
    if (open_err != ESP_OK) {
      ESP_LOGW(kVideoTag, "HTTP open failed (err=%d)", static_cast<int>(open_err));
      cleanup_client();
      return;
    }

    const int64_t hdr_rc = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (hdr_rc < 0 || status_code != 200) {
      ESP_LOGW(kVideoTag, "HTTP stream rejected (status=%d hdr_rc=%lld)", status_code,
               static_cast<long long>(hdr_rc));
      cleanup_client();
      return;
    }

    // Switch to short read polls so stop() or camera switching aborts within <= 150 ms.
    (void)esp_http_client_set_timeout_ms(client, static_cast<int>(kVideoHttpReadPollMs));

    MjpegFrameParser parser(jpeg_buf, kMaxJpegBytes);
    if (video_ascii_istarts_with(video_trim_sv(http_ctx.content_type), "multipart/")) {
      parser.set_boundary(http_ctx.content_type);
    } else {
      parser.set_boundary("");
    }

    uint32_t last_data_ms = video_now_ms();
    bool got_frame_this_session = false;
    uint32_t prev_parser_drops = 0U;

    while (!owner->stop_requested_.load(std::memory_order_acquire) &&
           owner->generation_.load(std::memory_order_acquire) == local_gen) {
      const uint32_t read_t0_ms = video_now_ms();
      errno = 0;
      const int n = esp_http_client_read(client, reinterpret_cast<char *>(http_rx_buf),
                                         static_cast<int>(kVideoHttpRxBufBytes));
      const uint32_t now_ms = video_now_ms();
      const uint32_t read_elapsed_ms = (now_ms >= read_t0_ms) ? (now_ms - read_t0_ms) : 0U;

      if (n > 0) {
        last_data_ms = now_ms;
        parser.feed(http_rx_buf, static_cast<size_t>(n), [&](const uint8_t *frame_ptr, size_t frame_len) {
          if (decode_and_publish_frame(frame_ptr, frame_len, local_gen)) {
            if (!got_frame_this_session) {
              got_frame_this_session = true;
              reconnect_attempt = 0U;
            }
          }
        });
        const uint32_t parser_drops = parser.frames_oversized() + parser.frames_corrupt();
        if (parser_drops > prev_parser_drops) {
          const uint32_t delta = parser_drops - prev_parser_drops;
          prev_parser_drops = parser_drops;
          if (stats_mtx != nullptr && xSemaphoreTake(stats_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
            stats.frames_dropped += delta;
            xSemaphoreGive(stats_mtx);
          }
        }
        continue;
      }

      // In ESP-IDF sync mode, select() read timeouts (ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT)
      // can return 0 with errno == 0 and !is_complete_data_received; distinguish that from a
      // genuine fast TCP FIN/EOF using read_elapsed_ms so <=6 FPS streams do not reconnect-storm.
      const bool is_poll_timeout =
          (n == -ESP_ERR_HTTP_EAGAIN) || (n == ESP_ERR_HTTP_EAGAIN) ||
          (n == 0 && !esp_http_client_is_complete_data_received(client) &&
           (errno == EAGAIN || errno == EWOULDBLOCK ||
            (errno == 0 && read_elapsed_ms >= (kVideoHttpReadPollMs / 2U))));
      if (is_poll_timeout) {
        if ((now_ms - last_data_ms) >= kVideoStreamStallTimeoutMs) {
          ESP_LOGW(kVideoTag, "MJPEG stream stalled >%lums; reconnecting",
                   static_cast<unsigned long>(kVideoStreamStallTimeoutMs));
          break;
        }
        continue;
      }

      // Clean EOF or fatal transport error -> exit session loop to reconnect.
      ESP_LOGD(kVideoTag, "MJPEG stream closed (rc=%d errno=%d elapsed=%lums)", n, errno,
               static_cast<unsigned long>(read_elapsed_ms));
      break;
    }

    cleanup_client();
  }

  static void worker_entry(void *arg) {
    auto *self = static_cast<Impl *>(arg);
    self->worker_loop();
  }

  void worker_loop() {
    uint32_t active_gen = 0U;
    uint32_t reconnect_attempt = 0U;

    for (;;) {
      std::string url;
      uint16_t max_w = 0U;
      uint16_t max_h = 0U;
      uint32_t gen = 0U;
      bool stop_req = true;

      if (ctrl_mtx != nullptr && xSemaphoreTake(ctrl_mtx, portMAX_DELAY) == pdTRUE) {
        url = req_url;
        max_w = req_max_w;
        max_h = req_max_h;
        gen = owner->generation_.load(std::memory_order_acquire);
        stop_req = owner->stop_requested_.load(std::memory_order_acquire);
        xSemaphoreGive(ctrl_mtx);
      }

      if (stop_req || url.empty() || max_w == 0U || max_h == 0U) {
        free_stream_buffers();
        reconnect_attempt = 0U;
        active_gen = gen;
        // Park with 0 CPU and never wake light sleep until next start().
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        continue;
      }

      if (gen != active_gen) {
        active_gen = gen;
        reconnect_attempt = 0U;
        reset_stats_window(video_now_ms(), /*reset_totals=*/true);
      }

      run_stream_session(url, max_w, max_h, gen, reconnect_attempt);

      if (owner->stop_requested_.load(std::memory_order_acquire) ||
          owner->generation_.load(std::memory_order_acquire) != gen) {
        continue;
      }

      // If fatal PSRAM allocation error happened, stay in ERROR until next start()/stop().
      if (owner->state() == VideoStreamState::ERROR) {
        free_stream_buffers();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        continue;
      }

      const uint32_t backoff_ms = compute_reconnect_backoff_ms(reconnect_attempt);
      if (reconnect_attempt < 10U) ++reconnect_attempt;
      if (stats_mtx != nullptr && xSemaphoreTake(stats_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
        ++stats.reconnects;
        xSemaphoreGive(stats_mtx);
      }
      if (ctrl_mtx != nullptr && xSemaphoreTake(ctrl_mtx, portMAX_DELAY) == pdTRUE) {
        const bool still_same = !owner->stop_requested_.load(std::memory_order_acquire) &&
                                owner->generation_.load(std::memory_order_acquire) == gen;
        if (still_same) {
          owner->state_.store(static_cast<uint8_t>(VideoStreamState::RECONNECTING), std::memory_order_release);
        }
        xSemaphoreGive(ctrl_mtx);
        if (!still_same) continue;
      }
      // Interruptible backoff: wakes immediately if stop() or a new start() arrives.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms));
    }
  }
};

LatterVideoClient::LatterVideoClient() noexcept {
  static Impl s_impl;
  s_impl.owner = this;
  impl_ = &s_impl;
}

LatterVideoClient::~LatterVideoClient() noexcept {
  stop();
}

void LatterVideoClient::start(std::string_view url, uint16_t max_w, uint16_t max_h) {
  if (url.empty() || max_w == 0U || max_h == 0U) {
    stop();
    return;
  }
  const VideoStreamState cur = state();
  if (cur != VideoStreamState::IDLE && cur != VideoStreamState::ERROR &&
      !stop_requested_.load(std::memory_order_acquire) &&
      active_url_ == url && target_w_ == max_w && target_h_ == max_h) {
    return;
  }

  if (impl_ == nullptr || !impl_->init_sync_primitives()) {
    state_.store(static_cast<uint8_t>(VideoStreamState::ERROR), std::memory_order_release);
    return;
  }

  const bool url_changed = (active_url_ != url);
  active_url_.assign(url.data(), url.size());
  target_w_ = max_w;
  target_h_ = max_h;

  if (xSemaphoreTake(impl_->ctrl_mtx, portMAX_DELAY) == pdTRUE) {
    impl_->req_url = active_url_;
    impl_->req_max_w = target_w_;
    impl_->req_max_h = target_h_;
    if (url_changed) {
      has_frame_.store(false, std::memory_order_release);
    }
    frame_dirty_.store(false, std::memory_order_release);
    stop_requested_.store(false, std::memory_order_release);
    generation_.fetch_add(1U, std::memory_order_acq_rel);
    state_.store(static_cast<uint8_t>(VideoStreamState::CONNECTING), std::memory_order_release);
    xSemaphoreGive(impl_->ctrl_mtx);
  }

  impl_->ensure_worker_started();
  impl_->wake_worker();
}

void LatterVideoClient::stop() {
  active_url_.clear();
  target_w_ = 0U;
  target_h_ = 0U;

  if (impl_ != nullptr && impl_->ctrl_mtx != nullptr) {
    if (xSemaphoreTake(impl_->ctrl_mtx, portMAX_DELAY) == pdTRUE) {
      impl_->req_url.clear();
      impl_->req_max_w = 0U;
      impl_->req_max_h = 0U;
      has_frame_.store(false, std::memory_order_release);
      frame_dirty_.store(false, std::memory_order_release);
      stop_requested_.store(true, std::memory_order_release);
      generation_.fetch_add(1U, std::memory_order_acq_rel);
      state_.store(static_cast<uint8_t>(VideoStreamState::IDLE), std::memory_order_release);
      xSemaphoreGive(impl_->ctrl_mtx);
    }
    impl_->wake_worker();
  } else {
    has_frame_.store(false, std::memory_order_release);
    frame_dirty_.store(false, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
    generation_.fetch_add(1U, std::memory_order_acq_rel);
    state_.store(static_cast<uint8_t>(VideoStreamState::IDLE), std::memory_order_release);
  }
}

void LatterVideoClient::poll_release_if_idle() {
  if (state() == VideoStreamState::IDLE) {
    frame_dirty_.store(false, std::memory_order_release);
    has_frame_.store(false, std::memory_order_release);
  }
}

bool LatterVideoClient::is_active() const noexcept {
  const VideoStreamState s = state();
  return s == VideoStreamState::CONNECTING || s == VideoStreamState::STREAMING ||
         s == VideoStreamState::RECONNECTING;
}

VideoStreamState LatterVideoClient::state() const noexcept {
  return static_cast<VideoStreamState>(state_.load(std::memory_order_acquire));
}

uint32_t LatterVideoClient::frame_seq() const noexcept {
  return frame_seq_.load(std::memory_order_acquire);
}

bool LatterVideoClient::has_frame() const noexcept {
  return has_frame_.load(std::memory_order_acquire);
}

bool LatterVideoClient::acquire_frame(const uint16_t *&out_data, int &out_w, int &out_h) noexcept {
  out_data = nullptr;
  out_w = 0;
  out_h = 0;
  if (impl_ == nullptr || !has_frame_.load(std::memory_order_acquire)) return false;

#if LATTER_VIDEO_HW_JPEG
  portENTER_CRITICAL(&impl_->idx_mux);
  const int idx = impl_->front_idx;
  if (idx >= 0 && idx < 2 && impl_->buf[idx] != nullptr && impl_->buf_w[idx] > 0 && impl_->buf_h[idx] > 0 &&
      has_frame_.load(std::memory_order_acquire)) {
    impl_->reader_idx = idx;
    out_data = impl_->buf[idx];
    out_w = impl_->buf_w[idx];
    out_h = impl_->buf_h[idx];
    portEXIT_CRITICAL(&impl_->idx_mux);
    return true;
  }
  portEXIT_CRITICAL(&impl_->idx_mux);
  return false;
#else
  if (impl_->frame_mtx == nullptr) return false;
  if (xSemaphoreTake(impl_->frame_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  if (!has_frame_.load(std::memory_order_acquire) || impl_->buf[0] == nullptr ||
      impl_->buf_w[0] <= 0 || impl_->buf_h[0] <= 0) {
    xSemaphoreGive(impl_->frame_mtx);
    return false;
  }
  out_data = impl_->buf[0];
  out_w = impl_->buf_w[0];
  out_h = impl_->buf_h[0];
  return true;
#endif
}

void LatterVideoClient::release_frame() noexcept {
  if (impl_ == nullptr) return;
#if LATTER_VIDEO_HW_JPEG
  portENTER_CRITICAL(&impl_->idx_mux);
  impl_->reader_idx = -1;
  portEXIT_CRITICAL(&impl_->idx_mux);
#else
  if (impl_->frame_mtx != nullptr) {
    xSemaphoreGive(impl_->frame_mtx);
  }
#endif
}

bool LatterVideoClient::consume_frame_dirty() noexcept {
  return frame_dirty_.exchange(false, std::memory_order_acq_rel);
}

VideoStreamStats LatterVideoClient::stats() const noexcept {
  VideoStreamStats out{};
  out.hw_jpeg = (LATTER_VIDEO_HW_JPEG != 0);
  if (impl_ != nullptr && impl_->stats_mtx != nullptr &&
      xSemaphoreTake(impl_->stats_mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
    out = impl_->stats;
    xSemaphoreGive(impl_->stats_mtx);
  }
  return out;
}

const std::string &LatterVideoClient::active_url() const noexcept {
  return active_url_;
}

#else  // Host test mode or synthetic verify_core.py ESP32 compile harness

struct LatterVideoClient::Impl {};

LatterVideoClient::LatterVideoClient() noexcept = default;
LatterVideoClient::~LatterVideoClient() noexcept = default;

void LatterVideoClient::start(std::string_view url, uint16_t max_w, uint16_t max_h) {
  if (url.empty() || max_w == 0U || max_h == 0U) {
    stop();
    return;
  }
  const VideoStreamState cur = state();
  if (cur != VideoStreamState::IDLE && cur != VideoStreamState::ERROR &&
      !stop_requested_.load(std::memory_order_acquire) &&
      active_url_ == url && target_w_ == max_w && target_h_ == max_h) {
    return;
  }
  if (active_url_ != url) {
    has_frame_.store(false, std::memory_order_release);
#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
    host_frame_data_ = nullptr;
    host_frame_w_ = 0;
    host_frame_h_ = 0;
#endif
  }
  active_url_.assign(url.data(), url.size());
  target_w_ = max_w;
  target_h_ = max_h;
  stop_requested_.store(false, std::memory_order_release);
  frame_dirty_.store(false, std::memory_order_release);
  generation_.fetch_add(1U, std::memory_order_acq_rel);
  state_.store(static_cast<uint8_t>(VideoStreamState::CONNECTING), std::memory_order_release);
}

void LatterVideoClient::stop() {
  active_url_.clear();
  target_w_ = 0U;
  target_h_ = 0U;
  has_frame_.store(false, std::memory_order_release);
  frame_dirty_.store(false, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  generation_.fetch_add(1U, std::memory_order_acq_rel);
  state_.store(static_cast<uint8_t>(VideoStreamState::IDLE), std::memory_order_release);
#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  host_frame_data_ = nullptr;
  host_frame_w_ = 0;
  host_frame_h_ = 0;
#endif
}

void LatterVideoClient::poll_release_if_idle() {
  if (state() == VideoStreamState::IDLE) {
    has_frame_.store(false, std::memory_order_release);
    frame_dirty_.store(false, std::memory_order_release);
  }
}

bool LatterVideoClient::is_active() const noexcept {
  const VideoStreamState s = state();
  return s == VideoStreamState::CONNECTING || s == VideoStreamState::STREAMING ||
         s == VideoStreamState::RECONNECTING;
}

VideoStreamState LatterVideoClient::state() const noexcept {
  return static_cast<VideoStreamState>(state_.load(std::memory_order_acquire));
}

uint32_t LatterVideoClient::frame_seq() const noexcept {
  return frame_seq_.load(std::memory_order_acquire);
}

bool LatterVideoClient::has_frame() const noexcept {
  return has_frame_.load(std::memory_order_acquire);
}

bool LatterVideoClient::acquire_frame(const uint16_t *&out_data, int &out_w, int &out_h) noexcept {
  out_data = nullptr;
  out_w = 0;
  out_h = 0;
#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  if (!has_frame_.load(std::memory_order_acquire) || host_frame_data_ == nullptr ||
      host_frame_w_ <= 0 || host_frame_h_ <= 0) {
    return false;
  }
  out_data = host_frame_data_;
  out_w = host_frame_w_;
  out_h = host_frame_h_;
  return true;
#else
  return false;
#endif
}

void LatterVideoClient::release_frame() noexcept {}

bool LatterVideoClient::consume_frame_dirty() noexcept {
  return frame_dirty_.exchange(false, std::memory_order_acq_rel);
}

VideoStreamStats LatterVideoClient::stats() const noexcept {
#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  return host_stats_;
#else
  VideoStreamStats out{};
  out.hw_jpeg = (LATTER_CHIP_P4 != 0);
  return out;
#endif
}

const std::string &LatterVideoClient::active_url() const noexcept {
  return active_url_;
}

#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
void LatterVideoClient::inject_host_frame_for_test(const uint16_t *pixels, int w, int h) noexcept {
  if (pixels == nullptr || w <= 0 || h <= 0 || stop_requested_.load(std::memory_order_acquire)) return;
  host_frame_data_ = pixels;
  host_frame_w_ = w;
  host_frame_h_ = h;
  ++host_stats_.frames_decoded;
  host_stats_.frame_w = static_cast<uint16_t>(w);
  host_stats_.frame_h = static_cast<uint16_t>(h);
  host_stats_.hw_jpeg = (LATTER_CHIP_P4 != 0);
  has_frame_.store(true, std::memory_order_release);
  frame_seq_.fetch_add(1U, std::memory_order_release);
  frame_dirty_.store(true, std::memory_order_release);
  state_.store(static_cast<uint8_t>(VideoStreamState::STREAMING), std::memory_order_release);
}

void LatterVideoClient::reset_for_test() noexcept {
  stop();
  frame_seq_.store(0U, std::memory_order_release);
  generation_.store(0U, std::memory_order_release);
  host_stats_ = VideoStreamStats{};
}
#endif

#endif  // LATTER_VIDEO_ESP32_NET

LatterVideoClient &video_client() noexcept {
  static LatterVideoClient instance;
  return instance;
}

}  // namespace latter_ui
