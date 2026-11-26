#pragma once

// ==============================================================================
// Latter Core Tiered Live Video Streaming Engine (include/hal_video_stream.h)
// ==============================================================================
// Thread affinity:
//   - Worker-task only: `MjpegFrameParser` and `LatterVideoClient` internal HTTP
//     / JPEG decode callbacks (`video_detail::*`).
//   - Main-loop only: `LatterVideoClient::start()`, `stop()`, `poll_release_if_idle()`,
//     `acquire_frame()`, `release_frame()`, `consume_frame_dirty()`, `active_url()`.
//   - Worker-task/main-loop cross-thread atomics: `LatterVideoClient::state_`,
//     `frame_seq_`, `has_frame_`, `frame_dirty_`, `stop_requested_`, `generation_`.
// ------------------------------------------------------------------------------
// Hardware-tiered MJPEG-over-HTTP live camera streaming client (`LatterVideoClient`)
// and pure C++20 incremental `MjpegFrameParser` (compiles standalone on host).
//
// RGB565 pixel contract:
//   Decoded frames are tightly packed row-major native little-endian RGB565
//   (`uint16_t`: R5 in bits 15..11 [0xF800], G6 in bits 10..5 [0x07E0],
//   B5 in bits 4..0 [0x001F]) matching `latter_ui::blit_rgb565_scanline()` and
//   `latter_ui::ArtImageView`.
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace latter_core {

// ------------------------------------------------------------------------------
// 1. Pure C++20 Video Streaming Helpers & Constants
// ------------------------------------------------------------------------------

inline constexpr size_t kMaxJpegBytes = LATTER_HAS_HW_JPEG ? (192U * 1024U) : (96U * 1024U);
// kVideoMaxViewportDim (768U with HW JPEG, 512U otherwise) is canonically exported by hal_manifest.h.
inline constexpr uint32_t kVideoHttpConnectTimeoutMs = 8000U;
inline constexpr uint32_t kVideoHttpReadPollMs = 150U;
inline constexpr uint32_t kVideoStreamStallTimeoutMs = 4000U;
inline constexpr uint32_t kVideoProfIntervalMs = 5000U;

// Reconnect backoff progression (0-based attempt index): 1000 -> 2000 -> 5000 ms (capped).
inline constexpr uint32_t compute_reconnect_backoff_ms(uint32_t attempt) noexcept {
  if (attempt == 0U) return 1000U;
  if (attempt == 1U) return 2000U;
  return 5000U;
}

// ASCII case-insensitive helpers (locale-independent, zero allocations).
inline constexpr char video_ascii_lower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool video_ascii_iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (video_ascii_lower(a[i]) != video_ascii_lower(b[i])) return false;
  }
  return true;
}

inline bool video_ascii_istarts_with(std::string_view s, std::string_view prefix) noexcept {
  if (s.size() < prefix.size()) return false;
  return video_ascii_iequals(s.substr(0, prefix.size()), prefix);
}

inline std::string_view video_trim_sv(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::string extract_multipart_boundary(std::string_view input);

// ------------------------------------------------------------------------------
// 2. Pure C++20 Incremental MJPEG Multipart Frame Parser (`MjpegFrameParser`)
// ------------------------------------------------------------------------------
class MjpegFrameParser {
 public:
  static constexpr size_t kDefaultMaxFrameBytes = kMaxJpegBytes;
  static constexpr size_t kMaxLineBytes = 256U;
  using FrameCallbackFn = void (*)(void *ctx, const uint8_t *jpeg_data, size_t jpeg_len);

  explicit MjpegFrameParser(size_t max_frame_bytes = kDefaultMaxFrameBytes)
      : ext_buf_(nullptr), max_frame_bytes_(max_frame_bytes) {}

  MjpegFrameParser(uint8_t *external_buf, size_t max_frame_bytes) noexcept
      : ext_buf_(external_buf), max_frame_bytes_(max_frame_bytes) {}

  void set_external_buffer(uint8_t *external_buf, size_t max_frame_bytes) noexcept;
  void set_max_frame_bytes(size_t max_bytes) noexcept { max_frame_bytes_ = max_bytes; }
  size_t max_frame_bytes() const noexcept { return max_frame_bytes_; }

  void set_boundary(std::string_view boundary_or_content_type);
  std::string_view boundary() const noexcept { return boundary_; }
  std::string_view delimiter() const noexcept { return delimiter_; }

  void reset() noexcept;
  void reset_stats() noexcept;

  uint32_t frames_parsed() const noexcept { return frames_parsed_; }
  uint32_t frames_oversized() const noexcept { return frames_oversized_; }
  uint32_t frames_corrupt() const noexcept { return frames_corrupt_; }

  template <typename CallbackFn>
  size_t feed(const uint8_t *data, size_t len, CallbackFn &&on_frame) {
    auto thunk = +[](void *ctx, const uint8_t *jpeg_data, size_t jpeg_len) {
      (*static_cast<std::remove_reference_t<CallbackFn> *>(ctx))(jpeg_data, jpeg_len);
    };
    auto &fn_ref = on_frame;
    return feed_impl_(data, len, thunk, static_cast<void *>(&fn_ref));
  }

 private:
  enum class State : uint8_t {
    SEEK_BOUNDARY = 0,
    READ_HEADERS = 1,
    READ_BODY_LEN = 2,
    SKIP_BODY_LEN = 3,
    SCAN_SOI = 4,
    SCAN_EOI = 5,
    SKIP_OVERSIZED_SCAN = 6,
  };

  size_t feed_impl_(const uint8_t *data, size_t len, FrameCallbackFn cb, void *cb_ctx);
  uint8_t *acquire_buf();
  void enter_seek_or_soi() noexcept;
  bool matches_boundary_line(bool require_closing) const noexcept;
  void parse_part_header_line() noexcept;
  static size_t validate_jpeg_buffer(const uint8_t *buf, size_t len) noexcept;

  uint8_t *ext_buf_{nullptr};
  std::vector<uint8_t> owned_buf_{};
  size_t max_frame_bytes_{kDefaultMaxFrameBytes};
  std::string boundary_{};
  std::string delimiter_{};

  State state_{State::SCAN_SOI};
  char line_buf_[kMaxLineBytes]{};
  size_t line_len_{0U};
  bool line_overflow_{false};
  int64_t part_content_length_{-1};
  size_t expected_len_{0U};
  size_t skip_remaining_{0U};
  size_t frame_len_{0U};
  uint8_t prev_byte_{0U};

  uint32_t frames_parsed_{0U};
  uint32_t frames_oversized_{0U};
  uint32_t frames_corrupt_{0U};
};

// ------------------------------------------------------------------------------
// 3. Hardware-Tiered Live Video Client (`LatterVideoClient`)
// ------------------------------------------------------------------------------
enum class VideoStreamState : uint8_t {
  IDLE = 0,
  CONNECTING = 1,
  STREAMING = 2,
  RECONNECTING = 3,
  ERROR = 4,
};

struct VideoStreamStats {
  uint16_t fps_x10 = 0;
  uint16_t avg_decode_ms = 0;
  uint16_t max_decode_ms = 0;
  uint32_t frames_decoded = 0;
  uint32_t frames_dropped = 0;
  uint32_t reconnects = 0;
  uint16_t frame_w = 0;
  uint16_t frame_h = 0;
  bool hw_jpeg = false;
};

class LatterVideoClient {
 public:
  LatterVideoClient() noexcept;
  ~LatterVideoClient() noexcept;

  LatterVideoClient(const LatterVideoClient &) = delete;
  LatterVideoClient &operator=(const LatterVideoClient &) = delete;

  void start(std::string_view url, uint16_t max_w, uint16_t max_h);
  void stop();
  void poll_release_if_idle();

  bool is_active() const noexcept;
  VideoStreamState state() const noexcept;
  uint32_t frame_seq() const noexcept;
  bool has_frame() const noexcept;

  bool acquire_frame(const uint16_t *&out_data, int &out_w, int &out_h) noexcept;
  void release_frame() noexcept;
  bool consume_frame_dirty() noexcept;

  VideoStreamStats stats() const noexcept;
  const std::string &active_url() const noexcept;

#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  void inject_host_frame_for_test(const uint16_t *pixels, int w, int h) noexcept;
  void reset_for_test() noexcept;
#endif

 private:
  struct Impl;
  Impl *impl_{nullptr};

  std::string active_url_{};
  uint16_t target_w_{0U};
  uint16_t target_h_{0U};

  std::atomic<uint8_t> state_{static_cast<uint8_t>(VideoStreamState::IDLE)};
  std::atomic<uint32_t> frame_seq_{0U};
  std::atomic<bool> has_frame_{false};
  std::atomic<bool> frame_dirty_{false};
  std::atomic<bool> stop_requested_{true};
  std::atomic<uint32_t> generation_{0U};

  static_assert(std::atomic<uint8_t>::is_always_lock_free,
                "LatterVideoClient::state_ must be lock-free across worker/main threads");
  static_assert(std::atomic<uint32_t>::is_always_lock_free,
                "LatterVideoClient counters must be lock-free across worker/main threads");
  static_assert(std::atomic<bool>::is_always_lock_free,
                "LatterVideoClient flags must be lock-free across worker/main threads");

#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  const uint16_t *host_frame_data_{nullptr};
  int host_frame_w_{0};
  int host_frame_h_{0};
  VideoStreamStats host_stats_{};
#endif
};

LatterVideoClient &video_client() noexcept;

namespace video_detail {

struct VideoDecoderContext;
using AcquireBackFn = int (*)(void *ctx) noexcept;
using CommitBackFn = void (*)(void *ctx, int back_idx, int decoded_w, int decoded_h) noexcept;
using CancelCheckFn = bool (*)(void *ctx) noexcept;

VideoDecoderContext *video_decoder_instance() noexcept;
void video_decoder_free(VideoDecoderContext *dec) noexcept;
bool video_decoder_ensure(VideoDecoderContext *dec, uint16_t max_w, uint16_t max_h,
                          uint8_t *&out_jpeg_buf, uint16_t *(&out_fb)[2]) noexcept;
bool video_decoder_is_hw_jpeg() noexcept;
bool video_decoder_decode_frame(VideoDecoderContext *dec,
                                const uint8_t *jpeg_data, size_t jpeg_len,
                                AcquireBackFn acquire_back_fn,
                                CommitBackFn commit_back_fn,
                                CancelCheckFn is_cancelled_fn,
                                void *cb_ctx,
                                int &out_w, int &out_h) noexcept;

}  // namespace video_detail

}  // namespace latter_core
