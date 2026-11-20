#pragma once

// ==============================================================================
// Latter Core Tiered Live Video Streaming Engine (include/hal_video_stream.h)
// ==============================================================================
// Hardware-tiered MJPEG-over-HTTP live camera streaming client (`LatterVideoClient`)
// and pure C++20 incremental `MjpegFrameParser` (compiles standalone on host).
//
// RGB565 pixel contract:
//   Decoded frames are tightly packed row-major native little-endian RGB565
//   (`uint16_t`: R5 in bits 15..11 [0xF800], G6 in bits 10..5 [0x07E0],
//   B5 in bits 4..0 [0x001F]) matching `latter_ui::blit_rgb565_scanline()` and
//   `latter_ui::ArtImageView`.
//
// Threading & Memory Hygiene:
//   - Worker task `latter_video` (priority `tskIDLE_PRIORITY + 2`) is launched
//     lazily on the first `start()` and sleeps on `ulTaskNotifyTake(portMAX_DELAY)`
//     while `IDLE` (0 CPU, never wakes light sleep).
//   - All PSRAM staging, decoder, and RGB565 frame buffers are freed immediately
//     upon transitioning to `IDLE` (`stop()`).
//   - The worker task NEVER calls display update routines directly; the main UI
//     loop polls `consume_frame_dirty()` and borrows pixels via `acquire_frame()`
//     / `release_frame()`.
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Pure C++20 Video Streaming Helpers & Constants
// ------------------------------------------------------------------------------

inline constexpr size_t kMaxJpegBytes = LATTER_CHIP_P4 ? (192U * 1024U) : (96U * 1024U);
inline constexpr uint16_t kVideoMaxViewportDim = LATTER_CHIP_P4 ? 768U : 512U;
inline constexpr uint32_t kVideoHttpConnectTimeoutMs = 4000U;
inline constexpr uint32_t kVideoHttpReadPollMs = 150U;
inline constexpr uint32_t kVideoStreamStallTimeoutMs = 4000U;
inline constexpr uint32_t kVideoProfIntervalMs = 5000U;

// Reconnect backoff progression (0-based attempt index): 1000 -> 2000 -> 5000 ms (capped).
inline constexpr uint32_t compute_reconnect_backoff_ms(uint32_t attempt) noexcept {
  if (attempt == 0U) return 1000U;
  if (attempt == 1U) return 2000U;
  return 5000U;
}

// Floor pixel dimension down to a 16-pixel multiple (ESP32-P4 HW JPEG DMA requirement).
inline constexpr int align16_floor(int px) noexcept {
  return (px <= 0) ? 0 : (px & ~15);
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

// Extracts normalized multipart boundary token (without leading "--" or quotes) from either
// a full HTTP `Content-Type` header (`multipart/x-mixed-replace; boundary=latterframe`) or a
// raw boundary string (`latterframe` / `--latterframe`).
inline std::string extract_multipart_boundary(std::string_view input) {
  std::string_view sv = video_trim_sv(input);
  if (sv.empty()) return {};

  // Search case-insensitively for "boundary=" parameter if a full Content-Type was passed.
  constexpr std::string_view kKey = "boundary=";
  size_t found_pos = std::string_view::npos;
  if (sv.size() >= kKey.size()) {
    for (size_t i = 0; i + kKey.size() <= sv.size(); ++i) {
      if (video_ascii_iequals(sv.substr(i, kKey.size()), kKey)) {
        if (i == 0 || sv[i - 1] == ';' || sv[i - 1] == ' ' || sv[i - 1] == '\t') {
          found_pos = i + kKey.size();
          break;
        }
      }
    }
  }
  if (found_pos != std::string_view::npos) {
    sv = sv.substr(found_pos);
    const size_t semi = sv.find(';');
    if (semi != std::string_view::npos) {
      sv = sv.substr(0, semi);
    }
    sv = video_trim_sv(sv);
  }

  if (sv.size() >= 2 && ((sv.front() == '"' && sv.back() == '"') || (sv.front() == '\'' && sv.back() == '\''))) {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
    sv = video_trim_sv(sv);
  }
  if (sv.size() >= 2 && sv[0] == '-' && sv[1] == '-') {
    sv.remove_prefix(2);
  }
  return std::string(sv);
}

// ------------------------------------------------------------------------------
// 2. Pure C++20 Incremental MJPEG Multipart Frame Parser (`MjpegFrameParser`)
// ------------------------------------------------------------------------------
// Supports:
//   - HTTP `multipart/x-mixed-replace; boundary=...` framing with arbitrary byte
//     chunking (down to 1 byte per `feed()` call).
//   - Case-insensitive `Content-Length:` part header parsing for O(1) memcpy framing.
//   - Automatic SOI (`0xFF 0xD8`) .. EOI (`0xFF 0xD9`) fallback when `Content-Length`
//     is omitted or when no multipart boundary is configured.
//   - Hard frame-size cap (`kMaxJpegBytes` default) that skips oversized or corrupt
//     parts cleanly without stalling or overflowing the staging buffer.

class MjpegFrameParser {
 public:
  static constexpr size_t kDefaultMaxFrameBytes = kMaxJpegBytes;
  static constexpr size_t kMaxLineBytes = 256U;

  explicit MjpegFrameParser(size_t max_frame_bytes = kDefaultMaxFrameBytes)
      : ext_buf_(nullptr), max_frame_bytes_(max_frame_bytes) {}

  MjpegFrameParser(uint8_t *external_buf, size_t max_frame_bytes) noexcept
      : ext_buf_(external_buf), max_frame_bytes_(max_frame_bytes) {}

  void set_external_buffer(uint8_t *external_buf, size_t max_frame_bytes) noexcept {
    ext_buf_ = external_buf;
    max_frame_bytes_ = max_frame_bytes;
    reset();
  }

  void set_max_frame_bytes(size_t max_bytes) noexcept { max_frame_bytes_ = max_bytes; }
  size_t max_frame_bytes() const noexcept { return max_frame_bytes_; }

  void set_boundary(std::string_view boundary_or_content_type) {
    boundary_ = extract_multipart_boundary(boundary_or_content_type);
    if (!boundary_.empty()) {
      delimiter_.clear();
      delimiter_.reserve(boundary_.size() + 2U);
      delimiter_.append("--");
      delimiter_.append(boundary_);
    } else {
      delimiter_.clear();
    }
    reset();
  }

  std::string_view boundary() const noexcept { return boundary_; }
  std::string_view delimiter() const noexcept { return delimiter_; }

  void reset() noexcept {
    state_ = delimiter_.empty() ? State::SCAN_SOI : State::SEEK_BOUNDARY;
    line_len_ = 0U;
    line_overflow_ = false;
    part_content_length_ = -1;
    expected_len_ = 0U;
    skip_remaining_ = 0U;
    frame_len_ = 0U;
    prev_byte_ = 0U;
  }

  void reset_stats() noexcept {
    frames_parsed_ = 0U;
    frames_oversized_ = 0U;
    frames_corrupt_ = 0U;
  }

  uint32_t frames_parsed() const noexcept { return frames_parsed_; }
  uint32_t frames_oversized() const noexcept { return frames_oversized_; }
  uint32_t frames_corrupt() const noexcept { return frames_corrupt_; }

  // Feed an arbitrary byte slice into the incremental parser.
  // `on_frame` is invoked as `on_frame(const uint8_t *jpeg_data, size_t jpeg_len)` for
  // each complete, validated JPEG frame. Returns the number of frames delivered in this call.
  template <typename CallbackFn>
  size_t feed(const uint8_t *data, size_t len, CallbackFn &&on_frame) {
    if (data == nullptr || len == 0U) return 0U;
    uint8_t *buf = acquire_buf();
    if (buf == nullptr || max_frame_bytes_ < 4U) return 0U;

    size_t delivered = 0U;
    size_t pos = 0U;

    while (pos < len) {
      switch (state_) {
        case State::SEEK_BOUNDARY: {
          const uint8_t b = data[pos++];
          if (b == '\n') {
            if (!line_overflow_ && matches_boundary_line(false)) {
              if (!matches_boundary_line(true)) {
                part_content_length_ = -1;
                state_ = State::READ_HEADERS;
              }
            }
            line_len_ = 0U;
            line_overflow_ = false;
          } else if (b != '\r') {
            if (line_len_ < kMaxLineBytes) {
              line_buf_[line_len_++] = static_cast<char>(b);
            } else {
              line_overflow_ = true;
            }
          }
          break;
        }

        case State::READ_HEADERS: {
          const uint8_t b = data[pos++];
          if (b == '\n') {
            if (line_len_ == 0U && !line_overflow_) {
              // End of part headers.
              frame_len_ = 0U;
              prev_byte_ = 0U;
              if (part_content_length_ > 0) {
                const size_t cl = static_cast<size_t>(part_content_length_);
                if (cl > max_frame_bytes_) {
                  ++frames_oversized_;
                  skip_remaining_ = cl;
                  state_ = State::SKIP_BODY_LEN;
                } else {
                  expected_len_ = cl;
                  state_ = State::READ_BODY_LEN;
                }
              } else if (part_content_length_ == 0) {
                ++frames_corrupt_;
                enter_seek_or_soi();
              } else {
                // Content-Length omitted: fall back to SOI..EOI scanning.
                line_len_ = 0U;
                line_overflow_ = false;
                state_ = State::SCAN_SOI;
              }
            } else if (!line_overflow_) {
              parse_part_header_line();
            }
            line_len_ = 0U;
            line_overflow_ = false;
          } else if (b != '\r') {
            if (line_len_ < kMaxLineBytes) {
              line_buf_[line_len_++] = static_cast<char>(b);
            } else {
              line_overflow_ = true;
            }
          }
          break;
        }

        case State::READ_BODY_LEN: {
          const size_t rem = expected_len_ - frame_len_;
          const size_t avail = len - pos;
          const size_t take = (rem < avail) ? rem : avail;
          std::memcpy(buf + frame_len_, data + pos, take);
          frame_len_ += take;
          pos += take;
          if (frame_len_ == expected_len_) {
            size_t valid_len = validate_jpeg_buffer(buf, frame_len_);
            if (valid_len >= 4U) {
              ++frames_parsed_;
              ++delivered;
              on_frame(static_cast<const uint8_t *>(buf), valid_len);
            } else {
              ++frames_corrupt_;
            }
            frame_len_ = 0U;
            enter_seek_or_soi();
          }
          break;
        }

        case State::SKIP_BODY_LEN: {
          const size_t avail = len - pos;
          const size_t take = (skip_remaining_ < avail) ? skip_remaining_ : avail;
          skip_remaining_ -= take;
          pos += take;
          if (skip_remaining_ == 0U) {
            enter_seek_or_soi();
          }
          break;
        }

        case State::SCAN_SOI: {
          const uint8_t b = data[pos++];
          if (prev_byte_ == 0xFFU && b == 0xD8U) {
            buf[0] = 0xFFU;
            buf[1] = 0xD8U;
            frame_len_ = 2U;
            prev_byte_ = 0xD8U;
            line_len_ = 0U;
            line_overflow_ = false;
            state_ = State::SCAN_EOI;
            break;
          }
          prev_byte_ = b;
          if (!delimiter_.empty()) {
            if (b == '\n') {
              if (!line_overflow_ && matches_boundary_line(false)) {
                ++frames_corrupt_;
                if (!matches_boundary_line(true)) {
                  part_content_length_ = -1;
                  state_ = State::READ_HEADERS;
                } else {
                  state_ = State::SEEK_BOUNDARY;
                }
              }
              line_len_ = 0U;
              line_overflow_ = false;
            } else if (b != '\r') {
              if (line_len_ < kMaxLineBytes) {
                line_buf_[line_len_++] = static_cast<char>(b);
              } else {
                line_overflow_ = true;
              }
            }
          }
          break;
        }

        case State::SCAN_EOI: {
          const uint8_t b = data[pos++];
          if (frame_len_ < max_frame_bytes_) {
            buf[frame_len_++] = b;
            if (prev_byte_ == 0xFFU && b == 0xD9U) {
              ++frames_parsed_;
              ++delivered;
              on_frame(static_cast<const uint8_t *>(buf), frame_len_);
              frame_len_ = 0U;
              prev_byte_ = 0U;
              enter_seek_or_soi();
              break;
            }
            prev_byte_ = b;
          } else {
            ++frames_oversized_;
            frame_len_ = 0U;
            line_len_ = 0U;
            line_overflow_ = false;
            if (prev_byte_ == 0xFFU && b == 0xD9U) {
              prev_byte_ = 0U;
              enter_seek_or_soi();
            } else {
              prev_byte_ = b;
              state_ = State::SKIP_OVERSIZED_SCAN;
            }
          }
          break;
        }

        case State::SKIP_OVERSIZED_SCAN: {
          const uint8_t b = data[pos++];
          if (prev_byte_ == 0xFFU && b == 0xD9U) {
            prev_byte_ = 0U;
            enter_seek_or_soi();
            break;
          }
          prev_byte_ = b;
          if (!delimiter_.empty()) {
            if (b == '\n') {
              if (!line_overflow_ && matches_boundary_line(false)) {
                if (!matches_boundary_line(true)) {
                  part_content_length_ = -1;
                  state_ = State::READ_HEADERS;
                } else {
                  state_ = State::SEEK_BOUNDARY;
                }
              }
              line_len_ = 0U;
              line_overflow_ = false;
            } else if (b != '\r') {
              if (line_len_ < kMaxLineBytes) {
                line_buf_[line_len_++] = static_cast<char>(b);
              } else {
                line_overflow_ = true;
              }
            }
          }
          break;
        }
      }
    }
    return delivered;
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

  uint8_t *acquire_buf() {
    if (ext_buf_ != nullptr) return ext_buf_;
    if (owned_buf_.size() < max_frame_bytes_) {
      owned_buf_.resize(max_frame_bytes_);
    }
    return owned_buf_.empty() ? nullptr : owned_buf_.data();
  }

  void enter_seek_or_soi() noexcept {
    line_len_ = 0U;
    line_overflow_ = false;
    prev_byte_ = 0U;
    state_ = delimiter_.empty() ? State::SCAN_SOI : State::SEEK_BOUNDARY;
  }

  bool matches_boundary_line(bool require_closing) const noexcept {
    if (delimiter_.empty()) return false;
    std::string_view line(line_buf_, line_len_);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
      line.remove_suffix(1);
    }
    if (line.size() < delimiter_.size()) return false;
    if (line.substr(0, delimiter_.size()) != delimiter_) return false;
    const std::string_view rest = line.substr(delimiter_.size());
    if (require_closing) {
      return rest == "--";
    }
    return rest.empty() || rest == "--";
  }

  void parse_part_header_line() noexcept {
    const std::string_view line(line_buf_, line_len_);
    constexpr std::string_view kCl = "content-length:";
    if (!video_ascii_istarts_with(line, kCl)) return;
    std::string_view val = video_trim_sv(line.substr(kCl.size()));
    if (val.empty()) return;
    int64_t parsed = 0;
    for (char ch : val) {
      if (ch < '0' || ch > '9') return;
      if (parsed > 100000000LL) return;
      parsed = parsed * 10LL + static_cast<int64_t>(ch - '0');
    }
    part_content_length_ = parsed;
  }

  static size_t validate_jpeg_buffer(const uint8_t *buf, size_t len) noexcept {
    if (buf == nullptr || len < 4U) return 0U;
    if (buf[0] != 0xFFU || buf[1] != 0xD8U) return 0U;
    // Allow up to 4 trailing zero padding bytes that some cameras append inside Content-Length.
    size_t trimmed = len;
    size_t zeros_trimmed = 0U;
    while (trimmed > 4U && zeros_trimmed < 4U && buf[trimmed - 1U] == 0x00U) {
      --trimmed;
      ++zeros_trimmed;
    }
    if (buf[trimmed - 2U] == 0xFFU && buf[trimmed - 1U] == 0xD9U) {
      return trimmed;
    }
    return 0U;
  }

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

  // Start or restart MJPEG streaming. Idempotent when already active/connecting with
  // the exact same URL and viewport dimensions.
  void start(std::string_view url, uint16_t max_w, uint16_t max_h);

  // Non-blocking stop: signals the worker task to close HTTP and release all PSRAM
  // buffers immediately without stalling the UI thread.
  void stop();

  // Optional safe cleanup hook callable from the main loop while idle.
  void poll_release_if_idle();

  bool is_active() const noexcept;
  VideoStreamState state() const noexcept;
  uint32_t frame_seq() const noexcept;
  bool has_frame() const noexcept;

  // Thread-safe pixel buffer borrowing for `latter_ui` (`ArtImageView`).
  // Every successful `acquire_frame()` MUST be paired with `release_frame()`.
  bool acquire_frame(const uint16_t *&out_data, int &out_w, int &out_h) noexcept;
  void release_frame() noexcept;

  // Returns true once per newly decoded frame so the main ESPHome loop schedules
  // a dirty-band display update safely from the main thread.
  bool consume_frame_dirty() noexcept;

  VideoStreamStats stats() const noexcept;
  const std::string &active_url() const noexcept;

#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  // Host-test helper to inject a synthetic decoded RGB565 frame without HTTP/ESP-IDF.
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

#if !defined(USE_ESP32) || defined(LATTER_HOST_TEST)
  const uint16_t *host_frame_data_{nullptr};
  int host_frame_w_{0};
  int host_frame_h_{0};
  VideoStreamStats host_stats_{};
#endif
};

LatterVideoClient &video_client() noexcept;

}  // namespace latter_ui
