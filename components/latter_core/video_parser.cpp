#if __has_include("hal_video_stream.h")
#include "hal_video_stream.h"
#else
#include "../../include/hal_video_stream.h"
#endif

#include <cstring>

namespace latter_ui {

std::string extract_multipart_boundary(std::string_view input) {
  std::string_view sv = video_trim_sv(input);
  if (sv.empty()) return {};

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

void MjpegFrameParser::set_external_buffer(uint8_t *external_buf, size_t max_frame_bytes) noexcept {
  ext_buf_ = external_buf;
  max_frame_bytes_ = max_frame_bytes;
  reset();
}

void MjpegFrameParser::set_boundary(std::string_view boundary_or_content_type) {
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

void MjpegFrameParser::reset() noexcept {
  state_ = delimiter_.empty() ? State::SCAN_SOI : State::SEEK_BOUNDARY;
  line_len_ = 0U;
  line_overflow_ = false;
  part_content_length_ = -1;
  expected_len_ = 0U;
  skip_remaining_ = 0U;
  frame_len_ = 0U;
  prev_byte_ = 0U;
}

void MjpegFrameParser::reset_stats() noexcept {
  frames_parsed_ = 0U;
  frames_oversized_ = 0U;
  frames_corrupt_ = 0U;
}

uint8_t *MjpegFrameParser::acquire_buf() {
  if (ext_buf_ != nullptr) return ext_buf_;
  if (owned_buf_.size() < max_frame_bytes_) {
    owned_buf_.resize(max_frame_bytes_);
  }
  return owned_buf_.empty() ? nullptr : owned_buf_.data();
}

void MjpegFrameParser::enter_seek_or_soi() noexcept {
  line_len_ = 0U;
  line_overflow_ = false;
  prev_byte_ = 0U;
  state_ = delimiter_.empty() ? State::SCAN_SOI : State::SEEK_BOUNDARY;
}

bool MjpegFrameParser::matches_boundary_line(bool require_closing) const noexcept {
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

void MjpegFrameParser::parse_part_header_line() noexcept {
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

size_t MjpegFrameParser::validate_jpeg_buffer(const uint8_t *buf, size_t len) noexcept {
  if (buf == nullptr || len < 4U) return 0U;
  if (buf[0] != 0xFFU || buf[1] != 0xD8U) return 0U;
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

size_t MjpegFrameParser::feed_impl_(const uint8_t *data, size_t len, FrameCallbackFn cb, void *cb_ctx) {
  if (data == nullptr || len == 0U || cb == nullptr) return 0U;
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
          const size_t valid_len = validate_jpeg_buffer(buf, frame_len_);
          if (valid_len >= 4U) {
            ++frames_parsed_;
            ++delivered;
            cb(cb_ctx, static_cast<const uint8_t *>(buf), valid_len);
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
            cb(cb_ctx, static_cast<const uint8_t *>(buf), frame_len_);
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

}  // namespace latter_ui
