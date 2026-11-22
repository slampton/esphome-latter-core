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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>

#if defined(USE_ESP32) && !defined(LATTER_HOST_TEST) && __has_include(<esp_http_client.h>) && \
    __has_include(<freertos/semphr.h>)
#define LATTER_VIDEO_ESP32_DEC 1
#include <esp_err.h>
#include <esp_heap_caps.h>

#if LATTER_HAS_HW_JPEG && __has_include(<driver/jpeg_decode.h>)
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
#define LATTER_VIDEO_ESP32_DEC 0
#define LATTER_VIDEO_HW_JPEG 0
#define LATTER_VIDEO_HAS_JPEGDEC 0
#define LATTER_VIDEO_HAS_ROM_TJPGD 0
#endif

namespace latter_ui::video_detail {

#if LATTER_VIDEO_ESP32_DEC

namespace {
constexpr const char *kVideoDecTag = "latter_video";

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

struct VideoDecoderContext {
  uint8_t *jpeg_buf{nullptr};
  size_t jpeg_buf_alloc_bytes{0U};
  uint16_t *buf[2]{nullptr, nullptr};
  size_t buf_alloc_bytes{0U};
  uint16_t alloc_w{0U};
  uint16_t alloc_h{0U};

#if LATTER_VIDEO_HW_JPEG
  jpeg_decoder_handle_t hw_dec{nullptr};
#endif
#if LATTER_VIDEO_HAS_JPEGDEC
  JPEGDEC *jpegdec{nullptr};
#endif
#if LATTER_VIDEO_HAS_ROM_TJPGD
  void *tjpgd_work{nullptr};
#endif
};

VideoDecoderContext *video_decoder_instance() noexcept {
  static VideoDecoderContext s_ctx{};
  return &s_ctx;
}

bool video_decoder_is_hw_jpeg() noexcept {
  return LATTER_VIDEO_HW_JPEG != 0;
}

void video_decoder_free(VideoDecoderContext *dec) noexcept {
  if (dec == nullptr) return;
#if LATTER_VIDEO_HW_JPEG
  if (dec->hw_dec != nullptr) {
    jpeg_del_decoder_engine(dec->hw_dec);
    dec->hw_dec = nullptr;
  }
#endif
  for (int i = 0; i < 2; ++i) {
    if (dec->buf[i] != nullptr) {
      heap_caps_free(dec->buf[i]);
      dec->buf[i] = nullptr;
    }
  }
  if (dec->jpeg_buf != nullptr) {
    heap_caps_free(dec->jpeg_buf);
    dec->jpeg_buf = nullptr;
  }
#if !LATTER_VIDEO_HW_JPEG
#if LATTER_VIDEO_HAS_JPEGDEC
  if (dec->jpegdec != nullptr) {
    dec->jpegdec->~JPEGDEC();
    heap_caps_free(dec->jpegdec);
    dec->jpegdec = nullptr;
  }
#endif
#if LATTER_VIDEO_HAS_ROM_TJPGD
  if (dec->tjpgd_work != nullptr) {
    heap_caps_free(dec->tjpgd_work);
    dec->tjpgd_work = nullptr;
  }
#endif
#endif
  dec->jpeg_buf_alloc_bytes = 0U;
  dec->buf_alloc_bytes = 0U;
  dec->alloc_w = 0U;
  dec->alloc_h = 0U;
}

bool video_decoder_ensure(VideoDecoderContext *dec, uint16_t max_w, uint16_t max_h,
                          uint8_t *&out_jpeg_buf, uint16_t *(&out_fb)[2]) noexcept {
  out_jpeg_buf = nullptr;
  out_fb[0] = nullptr;
  out_fb[1] = nullptr;
  if (dec == nullptr) return false;

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

  if (dec->alloc_w == want_w && dec->alloc_h == want_h && dec->jpeg_buf != nullptr &&
      dec->buf[0] != nullptr && dec->buf[1] != nullptr
#if LATTER_VIDEO_HW_JPEG
      && dec->hw_dec != nullptr
#elif LATTER_VIDEO_HAS_JPEGDEC
      && dec->jpegdec != nullptr
#elif LATTER_VIDEO_HAS_ROM_TJPGD
      && dec->tjpgd_work != nullptr
#endif
  ) {
    out_jpeg_buf = dec->jpeg_buf;
    out_fb[0] = dec->buf[0];
    out_fb[1] = dec->buf[1];
    return true;
  }

  video_decoder_free(dec);

#if LATTER_VIDEO_HW_JPEG
  jpeg_decode_engine_cfg_t eng_cfg{};
  eng_cfg.intr_priority = 0;
  eng_cfg.timeout_ms = 200;
  const esp_err_t eng_err = jpeg_new_decoder_engine(&eng_cfg, &dec->hw_dec);
  if (eng_err != ESP_OK || dec->hw_dec == nullptr) {
    ESP_LOGE(kVideoDecTag, "HW JPEG engine init failed (err=%d)", static_cast<int>(eng_err));
    video_decoder_free(dec);
    return false;
  }

  jpeg_decode_memory_alloc_cfg_t tx_cfg{};
  tx_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
  dec->jpeg_buf = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(kMaxJpegBytes, &tx_cfg, &dec->jpeg_buf_alloc_bytes));
  if (dec->jpeg_buf == nullptr) {
    ESP_LOGE(kVideoDecTag, "HW JPEG input PSRAM alloc failed (%uB)", static_cast<unsigned>(kMaxJpegBytes));
    video_decoder_free(dec);
    return false;
  }

  jpeg_decode_memory_alloc_cfg_t rx_cfg{};
  rx_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  size_t rx_alloc0 = 0U;
  size_t rx_alloc1 = 0U;
  dec->buf[0] = static_cast<uint16_t *>(jpeg_alloc_decoder_mem(want_frame_bytes, &rx_cfg, &rx_alloc0));
  dec->buf[1] = static_cast<uint16_t *>(jpeg_alloc_decoder_mem(want_frame_bytes, &rx_cfg, &rx_alloc1));
  if (dec->buf[0] == nullptr || dec->buf[1] == nullptr) {
    ESP_LOGE(kVideoDecTag, "HW JPEG double-buffer PSRAM alloc failed (2x %uB)",
             static_cast<unsigned>(want_frame_bytes));
    video_decoder_free(dec);
    return false;
  }
  dec->buf_alloc_bytes = std::min(rx_alloc0, rx_alloc1);
#else
  dec->jpeg_buf = static_cast<uint8_t *>(heap_caps_malloc(kMaxJpegBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  dec->buf[0] = static_cast<uint16_t *>(heap_caps_malloc(want_frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  dec->buf[1] = static_cast<uint16_t *>(heap_caps_malloc(want_frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (dec->jpeg_buf == nullptr || dec->buf[0] == nullptr || dec->buf[1] == nullptr) {
    ESP_LOGE(kVideoDecTag, "PSRAM alloc failed for S3 double-buffered video (jpeg=%uB fb=2x%uB)",
             static_cast<unsigned>(kMaxJpegBytes), static_cast<unsigned>(want_frame_bytes));
    video_decoder_free(dec);
    return false;
  }
  dec->jpeg_buf_alloc_bytes = kMaxJpegBytes;
  dec->buf_alloc_bytes = want_frame_bytes;

#if LATTER_VIDEO_HAS_JPEGDEC
  void *dec_mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (dec_mem == nullptr) {
    ESP_LOGE(kVideoDecTag, "PSRAM alloc failed for JPEGDEC (%uB)", static_cast<unsigned>(sizeof(JPEGDEC)));
    video_decoder_free(dec);
    return false;
  }
  dec->jpegdec = new (dec_mem) JPEGDEC();
#elif LATTER_VIDEO_HAS_ROM_TJPGD
  dec->tjpgd_work = heap_caps_malloc(kTjpgdWorkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (dec->tjpgd_work == nullptr) {
    ESP_LOGE(kVideoDecTag, "PSRAM alloc failed for TJpgDec workspace");
    video_decoder_free(dec);
    return false;
  }
#endif
#endif

  dec->alloc_w = want_w;
  dec->alloc_h = want_h;
  out_jpeg_buf = dec->jpeg_buf;
  out_fb[0] = dec->buf[0];
  out_fb[1] = dec->buf[1];
  return true;
}

bool video_decoder_decode_frame(VideoDecoderContext *dec,
                                const uint8_t *jpeg_data, size_t jpeg_len,
                                AcquireBackFn acquire_back_fn,
                                CommitBackFn commit_back_fn,
                                CancelCheckFn is_cancelled_fn,
                                void *cb_ctx,
                                int &out_w, int &out_h) noexcept {
  out_w = 0;
  out_h = 0;
  if (dec == nullptr || jpeg_data == nullptr || jpeg_len < 4U ||
      acquire_back_fn == nullptr || commit_back_fn == nullptr || is_cancelled_fn == nullptr) {
    return false;
  }
  if (is_cancelled_fn(cb_ctx)) return false;

  bool ok = false;
#if LATTER_VIDEO_HW_JPEG
  jpeg_decode_picture_info_t pic_info{};
  if (jpeg_decoder_get_info(jpeg_data, static_cast<uint32_t>(jpeg_len), &pic_info) != ESP_OK ||
      pic_info.width == 0U || pic_info.height == 0U) {
    return false;
  }
  const uint32_t padded_w = (pic_info.width + 15U) & ~15U;
  const uint32_t padded_h = (pic_info.height + 15U) & ~15U;
  const size_t needed_bytes = static_cast<size_t>(padded_w) * static_cast<size_t>(padded_h) * sizeof(uint16_t);
  if (padded_w > dec->alloc_w || padded_h > dec->alloc_h || needed_bytes > dec->buf_alloc_bytes) {
    return false;
  }

  const int back_idx = acquire_back_fn(cb_ctx);
  if (back_idx < 0 || dec->buf[back_idx] == nullptr) return false;

  jpeg_decode_cfg_t dec_cfg{};
  dec_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  dec_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  dec_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

  uint32_t out_size = 0U;
  const esp_err_t derr = jpeg_decoder_process(
      dec->hw_dec, &dec_cfg, jpeg_data, static_cast<uint32_t>(jpeg_len),
      reinterpret_cast<uint8_t *>(dec->buf[back_idx]), static_cast<uint32_t>(dec->buf_alloc_bytes), &out_size);
  if (derr == ESP_OK && out_size > 0U) {
    const int decoded_w = static_cast<int>(pic_info.width);
    const int decoded_h = static_cast<int>(pic_info.height);
    if (padded_w != pic_info.width) {
      uint16_t *px = dec->buf[back_idx];
      for (int row = 1; row < decoded_h; ++row) {
        std::memmove(px + static_cast<size_t>(row) * static_cast<size_t>(decoded_w),
                     px + static_cast<size_t>(row) * static_cast<size_t>(padded_w),
                     static_cast<size_t>(decoded_w) * sizeof(uint16_t));
      }
    }
    if (!is_cancelled_fn(cb_ctx)) {
      out_w = decoded_w;
      out_h = decoded_h;
      commit_back_fn(cb_ctx, back_idx, decoded_w, decoded_h);
      ok = true;
    }
  }
#elif LATTER_VIDEO_HAS_JPEGDEC
  if (dec->jpegdec == nullptr || dec->buf[0] == nullptr || dec->buf[1] == nullptr) return false;
  if (dec->jpegdec->openRAM(const_cast<uint8_t *>(jpeg_data), static_cast<int>(jpeg_len), &video_jpegdec_draw_cb) != 1) {
    return false;
  }
  const int raw_w = dec->jpegdec->getWidth();
  const int raw_h = dec->jpegdec->getHeight();
  int scale_opt = 0;
  int eff_w = raw_w;
  int eff_h = raw_h;
  if (raw_w <= 0 || raw_h <= 0) {
    dec->jpegdec->close();
    return false;
  }
  if (raw_w > static_cast<int>(dec->alloc_w) || raw_h > static_cast<int>(dec->alloc_h)) {
    if ((raw_w / 2) <= static_cast<int>(dec->alloc_w) && (raw_h / 2) <= static_cast<int>(dec->alloc_h) &&
        raw_w >= 32 && raw_h >= 32) {
      scale_opt = JPEG_SCALE_HALF;
      eff_w = raw_w / 2;
      eff_h = raw_h / 2;
    } else {
      dec->jpegdec->close();
      return false;
    }
  }

  const int back_idx = acquire_back_fn(cb_ctx);
  if (back_idx < 0 || dec->buf[back_idx] == nullptr) {
    dec->jpegdec->close();
    return false;
  }

  dec->jpegdec->setPixelType(RGB565_LITTLE_ENDIAN);
  if (!is_cancelled_fn(cb_ctx)) {
    JpegDecDrawCtx ctx{dec->buf[back_idx], eff_w, eff_h, static_cast<int>(dec->alloc_w), static_cast<int>(dec->alloc_h)};
    g_active_jpegdec_ctx = &ctx;
    const int dec_rc = dec->jpegdec->decode(0, 0, scale_opt);
    g_active_jpegdec_ctx = nullptr;
    if (dec_rc == 1 && !is_cancelled_fn(cb_ctx)) {
      out_w = eff_w;
      out_h = eff_h;
      commit_back_fn(cb_ctx, back_idx, eff_w, eff_h);
      ok = true;
    }
  }
  dec->jpegdec->close();
#elif LATTER_VIDEO_HAS_ROM_TJPGD
  if (dec->tjpgd_work == nullptr || dec->buf[0] == nullptr || dec->buf[1] == nullptr) return false;
  const int back_idx = acquire_back_fn(cb_ctx);
  if (back_idx < 0 || dec->buf[back_idx] == nullptr) return false;

  RomTjpgdCtx ctx{jpeg_data, jpeg_len, 0U, dec->buf[back_idx], 0, 0};
  JDEC jdec{};
  if (jd_prepare(&jdec, &video_tjpgd_in_cb, dec->tjpgd_work, static_cast<UINT>(kTjpgdWorkBytes), &ctx) != JDR_OK) {
    return false;
  }
  const int raw_w = static_cast<int>(jdec.width);
  const int raw_h = static_cast<int>(jdec.height);
  BYTE scale = 0;
  int eff_w = raw_w;
  int eff_h = raw_h;
  if (raw_w <= 0 || raw_h <= 0) return false;
  if (raw_w > static_cast<int>(dec->alloc_w) || raw_h > static_cast<int>(dec->alloc_h)) {
    if ((raw_w >> 1) <= static_cast<int>(dec->alloc_w) && (raw_h >> 1) <= static_cast<int>(dec->alloc_h) &&
        raw_w >= 32 && raw_h >= 32) {
      scale = 1;
      eff_w = raw_w >> 1;
      eff_h = raw_h >> 1;
    } else {
      return false;
    }
  }
  ctx.img_w = eff_w;
  ctx.img_h = eff_h;
  if (!is_cancelled_fn(cb_ctx)) {
    if (jd_decomp(&jdec, &video_tjpgd_out_cb, scale) == JDR_OK && !is_cancelled_fn(cb_ctx)) {
      out_w = eff_w;
      out_h = eff_h;
      commit_back_fn(cb_ctx, back_idx, eff_w, eff_h);
      ok = true;
    }
  }
#endif
  return ok;
}

#else  // Host test or synthetic compile harness

struct VideoDecoderContext {};

VideoDecoderContext *video_decoder_instance() noexcept {
  static VideoDecoderContext s_ctx{};
  return &s_ctx;
}

bool video_decoder_is_hw_jpeg() noexcept {
  return LATTER_HAS_HW_JPEG != 0;
}

void video_decoder_free(VideoDecoderContext *) noexcept {}

bool video_decoder_ensure(VideoDecoderContext *, uint16_t, uint16_t,
                          uint8_t *&out_jpeg_buf, uint16_t *(&out_fb)[2]) noexcept {
  out_jpeg_buf = nullptr;
  out_fb[0] = nullptr;
  out_fb[1] = nullptr;
  return false;
}

bool video_decoder_decode_frame(VideoDecoderContext *, const uint8_t *, size_t,
                                AcquireBackFn, CommitBackFn, CancelCheckFn, void *,
                                int &out_w, int &out_h) noexcept {
  out_w = 0;
  out_h = 0;
  return false;
}

#endif  // LATTER_VIDEO_ESP32_DEC

}  // namespace latter_ui::video_detail
