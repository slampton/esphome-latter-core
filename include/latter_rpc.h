#pragma once

// ==============================================================================
// Latter Core SDK: Zero-Privilege Diagnostic Outbox RPC Framing & PIN Wrap Crypto
// (include/latter_rpc.h)
// Encodes Device -> Home Assistant RPC calls onto the bound "System: RPC Outbox"
// text_sensor (<boot_hex>:<seq>:<part>/<total>|<json_slice>, <= 230 bytes/frame,
// UTF-8 codepoint-boundary safe, paced >= 35 ms per frame, atomic (seq) eviction,
// and v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32> HMAC-SHA256 PIN session wrap).
// ==============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif
#ifdef USE_ESP32
#include <esp_random.h>
#if __has_include("esphome/core/hal.h")
#include "esphome/core/hal.h"
#endif
#endif

namespace latter_rpc {

static constexpr size_t kMaxRpcFrameBytes = 230;
static constexpr size_t kMaxRpcSlices = 16;
static constexpr size_t kRpcQueueCapacity = 64;
static constexpr uint32_t kMinInterFrameMs = 35U;

static constexpr size_t kPinSessionKeyBytes = 32;
static constexpr size_t kMinPinDigits = 4;
static constexpr size_t kMaxPinDigits = 12;

// ------------------------------------------------------------------------------
// Compiler-Barrier Volatile RAM Zeroization
// ------------------------------------------------------------------------------
inline void secure_zero_bytes(void *ptr, size_t len) noexcept {
  if (ptr == nullptr || len == 0U) return;
  volatile unsigned char *vp = static_cast<volatile unsigned char *>(ptr);
  for (size_t i = 0; i < len; ++i) {
    vp[i] = 0U;
  }
}

inline void secure_zero_string(std::string &s) noexcept {
  if (!s.empty()) {
    volatile char *vp = s.data();
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
      vp[i] = '\0';
    }
    s.clear();
  }
}

// ------------------------------------------------------------------------------
// Pure C++20 SHA-256 & HMAC-SHA256 (FIPS 180-4 / RFC 2104, Zero External Deps)
// ------------------------------------------------------------------------------
namespace detail {

inline constexpr uint32_t rotr32(uint32_t x, unsigned n) noexcept {
  return (x >> n) | (x << (32U - n));
}

struct Sha256Ctx {
  uint32_t state[8]{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };
  uint64_t bit_len = 0ULL;
  uint8_t buf[64]{};
  size_t buf_len = 0U;

  void transform_block(const uint8_t block[64]) noexcept {
    static constexpr uint32_t kK[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t w[64];
    for (size_t i = 0; i < 16U; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4U]) << 24U) |
             (static_cast<uint32_t>(block[i * 4U + 1U]) << 16U) |
             (static_cast<uint32_t>(block[i * 4U + 2U]) << 8U) |
             static_cast<uint32_t>(block[i * 4U + 3U]);
    }
    for (size_t i = 16U; i < 64U; ++i) {
      const uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
      const uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
      w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0; i < 64U; ++i) {
      const uint32_t s1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
      const uint32_t s0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
    secure_zero_bytes(w, sizeof(w));
  }

  void update(const uint8_t *data, size_t len) noexcept {
    if (data == nullptr || len == 0U) return;
    bit_len += static_cast<uint64_t>(len) * 8ULL;
    size_t pos = 0U;
    if (buf_len > 0U) {
      const size_t take = (len < (64U - buf_len)) ? len : (64U - buf_len);
      std::memcpy(buf + buf_len, data, take);
      buf_len += take;
      pos += take;
      if (buf_len == 64U) {
        transform_block(buf);
        buf_len = 0U;
      }
    }
    while (pos + 64U <= len) {
      transform_block(data + pos);
      pos += 64U;
    }
    if (pos < len) {
      buf_len = len - pos;
      std::memcpy(buf, data + pos, buf_len);
    }
  }

  void update_sv(std::string_view sv) noexcept {
    update(reinterpret_cast<const uint8_t *>(sv.data()), sv.size());
  }

  void finish(uint8_t out[32]) noexcept {
    buf[buf_len++] = 0x80U;
    if (buf_len > 56U) {
      while (buf_len < 64U) buf[buf_len++] = 0U;
      transform_block(buf);
      buf_len = 0U;
    }
    while (buf_len < 56U) buf[buf_len++] = 0U;
    for (int i = 0; i < 8; ++i) {
      buf[56 + i] = static_cast<uint8_t>((bit_len >> (56 - i * 8)) & 0xFFU);
    }
    transform_block(buf);
    for (size_t i = 0; i < 8U; ++i) {
      out[i * 4U] = static_cast<uint8_t>((state[i] >> 24U) & 0xFFU);
      out[i * 4U + 1U] = static_cast<uint8_t>((state[i] >> 16U) & 0xFFU);
      out[i * 4U + 2U] = static_cast<uint8_t>((state[i] >> 8U) & 0xFFU);
      out[i * 4U + 3U] = static_cast<uint8_t>(state[i] & 0xFFU);
    }
    secure_zero_bytes(buf, sizeof(buf));
    secure_zero_bytes(state, sizeof(state));
  }
};

inline void hmac_sha256(
    const uint8_t *key, size_t key_len,
    std::initializer_list<std::string_view> msg_parts,
    uint8_t out[32]) noexcept {
  uint8_t k_block[64]{};
  if (key_len > 64U) {
    Sha256Ctx kh;
    kh.update(key, key_len);
    kh.finish(k_block);
  } else if (key != nullptr && key_len > 0U) {
    std::memcpy(k_block, key, key_len);
  }

  uint8_t ipad[64];
  uint8_t opad[64];
  for (size_t i = 0; i < 64U; ++i) {
    ipad[i] = static_cast<uint8_t>(k_block[i] ^ 0x36U);
    opad[i] = static_cast<uint8_t>(k_block[i] ^ 0x5cU);
  }

  uint8_t inner_digest[32];
  {
    Sha256Ctx inner;
    inner.update(ipad, sizeof(ipad));
    for (std::string_view part : msg_parts) {
      inner.update_sv(part);
    }
    inner.finish(inner_digest);
  }
  {
    Sha256Ctx outer;
    outer.update(opad, sizeof(opad));
    outer.update(inner_digest, sizeof(inner_digest));
    outer.finish(out);
  }

  secure_zero_bytes(k_block, sizeof(k_block));
  secure_zero_bytes(ipad, sizeof(ipad));
  secure_zero_bytes(opad, sizeof(opad));
  secure_zero_bytes(inner_digest, sizeof(inner_digest));
}

inline std::string_view trim_ascii_ws(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}

inline int hex_nibble(char ch) noexcept {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

inline void bytes_to_lower_hex(const uint8_t *src, size_t n, char *dst) noexcept {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    dst[i * 2U] = kHex[(src[i] >> 4U) & 0x0FU];
    dst[i * 2U + 1U] = kHex[src[i] & 0x0FU];
  }
}

struct PinSessionState {
  char key_id[9]{};
  uint8_t key_bytes[kPinSessionKeyBytes]{};
  uint32_t nonce = 0U;
  bool active = false;
};

inline PinSessionState &pin_session_state() noexcept {
  static PinSessionState st;
  return st;
}

}  // namespace detail

inline void sha256(const uint8_t *data, size_t len, uint8_t out[32]) noexcept {
  detail::Sha256Ctx ctx;
  ctx.update(data, len);
  ctx.finish(out);
}

inline void hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t out[32]) noexcept {
  const std::string_view sv(reinterpret_cast<const char *>(msg), msg_len);
  detail::hmac_sha256(key, key_len, {sv}, out);
}

inline void clear_pin_session_key() noexcept {
  auto &st = detail::pin_session_state();
  secure_zero_bytes(st.key_id, sizeof(st.key_id));
  secure_zero_bytes(st.key_bytes, sizeof(st.key_bytes));
  st.nonce = 0U;
  st.active = false;
}

inline bool has_pin_session_key() noexcept {
  return detail::pin_session_state().active;
}

inline bool set_pin_session_key(std::string_view key_id, std::string_view hex_key) noexcept {
  const std::string_view kid = detail::trim_ascii_ws(key_id);
  const std::string_view hkey = detail::trim_ascii_ws(hex_key);
  if (kid.size() != 8U || hkey.size() != (kPinSessionKeyBytes * 2U)) {
#ifdef ESP_LOGW
    ESP_LOGW("latter_rpc", "Rejected malformed set_pin_key (kid_len=%u, hex_len=%u)",
             static_cast<unsigned>(kid.size()), static_cast<unsigned>(hkey.size()));
#endif
    return false;
  }

  char norm_kid[9]{};
  for (size_t i = 0; i < 8U; ++i) {
    const int nib = detail::hex_nibble(kid[i]);
    if (nib < 0) {
#ifdef ESP_LOGW
      ESP_LOGW("latter_rpc", "Rejected set_pin_key with non-hex key_id");
#endif
      return false;
    }
    norm_kid[i] = (kid[i] >= 'A' && kid[i] <= 'F') ? static_cast<char>(kid[i] - 'A' + 'a') : kid[i];
  }
  norm_kid[8] = '\0';

  uint8_t decoded[kPinSessionKeyBytes]{};
  for (size_t i = 0; i < kPinSessionKeyBytes; ++i) {
    const int hi = detail::hex_nibble(hkey[i * 2U]);
    const int lo = detail::hex_nibble(hkey[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      secure_zero_bytes(decoded, sizeof(decoded));
#ifdef ESP_LOGW
      ESP_LOGW("latter_rpc", "Rejected set_pin_key with non-hex key material");
#endif
      return false;
    }
    decoded[i] = static_cast<uint8_t>((hi << 4) | lo);
  }

  auto &st = detail::pin_session_state();
  std::memcpy(st.key_id, norm_kid, sizeof(norm_kid));
  std::memcpy(st.key_bytes, decoded, sizeof(decoded));
  st.nonce = 0U;
  st.active = true;
  secure_zero_bytes(decoded, sizeof(decoded));
  return true;
}

// Wrap a 4..12 digit PIN into `v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32>` bound to `(entity_id, action)`.
// Returns an empty string if no session key is provisioned or `plain_code` is outside 4..12 ASCII digits.
inline std::string wrap_pin_code(
    std::string_view entity_id,
    std::string_view action,
    std::string_view plain_code) {
  auto &st = detail::pin_session_state();
  if (!st.active) return {};

  const std::string_view code = detail::trim_ascii_ws(plain_code);
  if (code.size() < kMinPinDigits || code.size() > kMaxPinDigits) return {};
  for (char ch : code) {
    if (ch < '0' || ch > '9') return {};
  }

  const uint32_t next_nonce = st.nonce + 1U;
  if (next_nonce == 0U) {
    clear_pin_session_key();
    return {};
  }
  st.nonce = next_nonce;

  char nonce_hex[9]{};
  std::snprintf(nonce_hex, sizeof(nonce_hex), "%08x", static_cast<unsigned>(next_nonce));
  const std::string_view kid_sv(st.key_id, 8U);
  const std::string_view nonce_sv(nonce_hex, 8U);
  const std::string_view ent_sv = detail::trim_ascii_ws(entity_id);
  const std::string_view act_sv = detail::trim_ascii_ws(action);

  uint8_t keystream[32]{};
  detail::hmac_sha256(st.key_bytes, kPinSessionKeyBytes, {"stream|", kid_sv, "|", nonce_sv}, keystream);

  uint8_t ct_bytes[kMaxPinDigits]{};
  for (size_t i = 0; i < code.size(); ++i) {
    ct_bytes[i] = static_cast<uint8_t>(static_cast<uint8_t>(code[i]) ^ keystream[i]);
  }
  char ct_hex[kMaxPinDigits * 2U + 1U]{};
  detail::bytes_to_lower_hex(ct_bytes, code.size(), ct_hex);
  const std::string_view ct_hex_sv(ct_hex, code.size() * 2U);

  uint8_t full_mac[32]{};
  detail::hmac_sha256(
      st.key_bytes,
      kPinSessionKeyBytes,
      {"latter-pin-v1|", kid_sv, "|", nonce_sv, "|", ent_sv, "|", act_sv, "|", ct_hex_sv},
      full_mac);

  char mac_hex32[33]{};
  detail::bytes_to_lower_hex(full_mac, 16U, mac_hex32);

  std::string out;
  out.reserve(3U + 8U + 1U + 8U + 1U + ct_hex_sv.size() + 1U + 32U);
  out += "v1:";
  out.append(kid_sv);
  out.push_back(':');
  out.append(nonce_sv);
  out.push_back(':');
  out.append(ct_hex_sv);
  out.push_back(':');
  out.append(mac_hex32, 32U);

  secure_zero_bytes(keystream, sizeof(keystream));
  secure_zero_bytes(ct_bytes, sizeof(ct_bytes));
  secure_zero_bytes(full_mac, sizeof(full_mac));
  return out;
}

// ------------------------------------------------------------------------------
// Paced Multi-Slice RPC Outbox Queue
// ------------------------------------------------------------------------------
#ifndef LATTER_RPC_SLOT_CHANNEL_DEFINED
#define LATTER_RPC_SLOT_CHANNEL_DEFINED 1
enum class SlotChannel : uint8_t {
  MUSIC         = 0,
  AREA_LIST     = 1,
  AREA_ENTITIES = 2,
  CALENDAR      = 3,
  COUNT         = 4,
};
#endif

using PublishSinkFn = void (*)(void *ctx, const std::string &frame);

struct RpcQueueState {
  std::array<std::string, kRpcQueueCapacity> ring{};
  size_t head = 0;
  size_t tail = 0;
  size_t count = 0;
  uint32_t boot_id = 0;
  uint32_t next_seq = 1;
  uint32_t last_publish_ms = 0;
  uint32_t last_loop_ms = 0;
  uint32_t passes_since_publish = 2;
  bool published_in_pass = false;
  bool awaiting_publish_stamp = false;
  void *sink_ctx = nullptr;
  PublishSinkFn sink_fn = nullptr;
  std::array<int, static_cast<size_t>(SlotChannel::COUNT)> latest_req_id{{0, 0, 0, 0}};
};

inline RpcQueueState &rpc_state() {
  static RpcQueueState st;
  if (st.boot_id == 0) {
#ifdef USE_ESP32
    uint32_t r = esp_random();
    st.boot_id = (r == 0) ? 1U : r;
#else
    st.boot_id = 0x4C415433U;
#endif
  }
  return st;
}

inline uint32_t current_rpc_millis(const RpcQueueState &st) noexcept {
#if defined(USE_ESP32) && __has_include("esphome/core/hal.h")
  const uint32_t hw_ms = ::esphome::millis();
  if (hw_ms != 0U) return hw_ms;
#endif
  return st.last_loop_ms;
}

inline int issue_req_id(SlotChannel ch) {
  auto &slot = rpc_state().latest_req_id[static_cast<size_t>(ch)];
  slot = (slot >= 1000000) ? 1 : (slot + 1);
  return slot;
}

inline bool accept_req_id(SlotChannel ch, int req_id) {
  if (req_id <= 0) return true;
  const int latest = rpc_state().latest_req_id[static_cast<size_t>(ch)];
  return req_id >= latest;
}

inline SlotChannel classify_browse_mode(std::string_view mode) {
  if (mode == "area_areas" || mode == "areas") return SlotChannel::AREA_LIST;
  if (mode == "calendar_agenda") return SlotChannel::CALENDAR;
  if (mode == "entities" || mode == "home" || mode.starts_with("area_")) return SlotChannel::AREA_ENTITIES;
  return SlotChannel::MUSIC;
}

inline void append_json_escaped(std::string &out, std::string_view sv) {
  out.push_back('"');
  for (unsigned char ch : sv) {
    switch (ch) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (ch < 0x20U) {
          char hex[7];
          std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(ch));
          out += hex;
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  out.push_back('"');
}

struct RpcField {
  const char *key = nullptr;
  std::string_view value{};
  bool include = true;

  constexpr RpcField(const char *k, std::string_view v, bool inc = true) noexcept
      : key(k), value(v), include(inc) {}
};

inline std::string build_rpc_json(
    std::string_view service,
    std::initializer_list<RpcField> fields) {
  std::string out;
  out.reserve(160);
  out += "{\"s\":";
  append_json_escaped(out, service);
  out += ",\"d\":{";
  bool first = true;
  for (const auto &kv : fields) {
    if (!kv.include || kv.key == nullptr) continue;
    if (!first) out.push_back(',');
    first = false;
    append_json_escaped(out, kv.key);
    out.push_back(':');
    append_json_escaped(out, kv.value);
  }
  out += "}}";
  return out;
}

// Back up from `max_end` so we never split a multi-byte UTF-8 codepoint across two text_sensor frames.
inline size_t utf8_safe_cut(std::string_view sv, size_t start, size_t max_bytes) {
  if (start >= sv.size()) return sv.size();
  size_t remaining = sv.size() - start;
  if (remaining <= max_bytes) return sv.size();
  size_t end = start + max_bytes;
  size_t min_back = (end > start + 3) ? (end - 3) : start;
  while (end > min_back && (static_cast<unsigned char>(sv[end]) & 0xC0U) == 0x80U) {
    --end;
  }
  return (end > start) ? end : (start + max_bytes);
}

namespace detail {

// Extract "<boot_hex>:<seq>:" prefix length so ring eviction drops all remaining slices of an evicted RPC atomically.
inline size_t frame_msg_prefix_len(std::string_view frame) noexcept {
  const size_t c1 = frame.find(':');
  if (c1 == std::string_view::npos) return 0U;
  const size_t c2 = frame.find(':', c1 + 1U);
  if (c2 == std::string_view::npos) return 0U;
  return c2 + 1U;
}

inline void evict_oldest_rpc_message(RpcQueueState &st) noexcept {
  if (st.count == 0U) return;
  const std::string &oldest = st.ring[st.head];
  const size_t plen = frame_msg_prefix_len(oldest);
  char prefix_buf[32]{};
  const bool match_seq = (plen > 0U && plen < sizeof(prefix_buf));
  if (match_seq) {
    std::memcpy(prefix_buf, oldest.data(), plen);
  }
  const std::string_view target_prefix(prefix_buf, match_seq ? plen : 0U);

  do {
    secure_zero_string(st.ring[st.head]);
    st.head = (st.head + 1U) % kRpcQueueCapacity;
    --st.count;
  } while (match_seq && st.count > 0U &&
           std::string_view(st.ring[st.head]).substr(0U, plen) == target_prefix);
}

}  // namespace detail

inline bool enqueue_chunked_json(std::string_view json_body) {
  RpcQueueState &st = rpc_state();

  // Reserve worst-case header budget: "<8hex>:<10dec>:<2dec>/<2dec>|" <= 27 bytes.
  static constexpr size_t kHeaderReserve = 28;
  static constexpr size_t kMaxSliceBytes = kMaxRpcFrameBytes - kHeaderReserve;

  std::array<std::string_view, kMaxRpcSlices> slices{};
  size_t total_parts = 0;
  size_t pos = 0;
  while (pos < json_body.size() && total_parts < slices.size()) {
    size_t next_pos = utf8_safe_cut(json_body, pos, kMaxSliceBytes);
    slices[total_parts++] = json_body.substr(pos, next_pos - pos);
    pos = next_pos;
  }
  if (pos < json_body.size()) {
#ifdef ESP_LOGW
    ESP_LOGW("latter_rpc", "Dropping oversized RPC payload (%u B exceeds %u-slice outbox cap)",
             static_cast<unsigned>(json_body.size()), static_cast<unsigned>(kMaxRpcSlices));
#endif
    return false;
  }
  if (total_parts == 0) {
    slices[0] = json_body;
    total_parts = 1;
  }

  const uint32_t seq = st.next_seq++;
  if (st.next_seq == 0) st.next_seq = 1;

  while (st.count > 0U && st.count + total_parts > kRpcQueueCapacity) {
    detail::evict_oldest_rpc_message(st);
  }

  for (size_t i = 0; i < total_parts; ++i) {
    char hdr[36];
    int hlen = std::snprintf(
        hdr, sizeof(hdr), "%08x:%u:%u/%u|",
        static_cast<unsigned>(st.boot_id),
        static_cast<unsigned>(seq),
        static_cast<unsigned>(i + 1),
        static_cast<unsigned>(total_parts));
    std::string frame;
    frame.reserve(static_cast<size_t>(hlen > 0 ? hlen : 0) + slices[i].size());
    if (hlen > 0) frame.append(hdr, static_cast<size_t>(hlen));
    frame.append(slices[i]);

    st.ring[st.tail] = std::move(frame);
    st.tail = (st.tail + 1) % kRpcQueueCapacity;
    ++st.count;
  }
  return true;
}

inline bool has_pending_frames() {
  return rpc_state().count > 0;
}

inline bool step_outbox_loop(uint32_t now_ms) {
  RpcQueueState &st = rpc_state();
  st.last_loop_ms = now_ms;
  st.published_in_pass = false;
  if (st.awaiting_publish_stamp) {
    st.last_publish_ms = (now_ms == 0) ? 1U : now_ms;
    st.awaiting_publish_stamp = false;
  }
  if (st.passes_since_publish < 1000U) {
    ++st.passes_since_publish;
  }
  if (!has_pending_frames() || st.sink_fn == nullptr || st.sink_ctx == nullptr) return false;
  if (st.passes_since_publish < 2U) return true;
  if (st.last_publish_ms != 0 && static_cast<uint32_t>(now_ms - st.last_publish_ms) < kMinInterFrameMs) {
    return true;
  }
  std::string next_frame = std::move(st.ring[st.head]);
  secure_zero_string(st.ring[st.head]);
  st.head = (st.head + 1) % kRpcQueueCapacity;
  --st.count;
  st.published_in_pass = true;
  st.passes_since_publish = 0;
  st.awaiting_publish_stamp = false;
  st.last_publish_ms = (now_ms == 0) ? 1U : now_ms;
  st.sink_fn(st.sink_ctx, next_frame);
  secure_zero_string(next_frame);
  return has_pending_frames();
}

template <typename SensorT>
  requires(!std::is_pointer_v<SensorT>)
inline void bind_sensor_sink(SensorT *sensor) {
  if (sensor == nullptr) return;
  RpcQueueState &st = rpc_state();
  st.sink_ctx = static_cast<void *>(sensor);
  st.sink_fn = [](void *ctx, const std::string &frame) {
    static_cast<SensorT *>(ctx)->publish_state(frame);
  };
}

template <typename SensorT>
inline void bind_sensor_sink(SensorT *const *sensor_ptr) {
  if (sensor_ptr != nullptr) bind_sensor_sink(*sensor_ptr);
}

inline void flush_first_frame_if_idle() {
  RpcQueueState &st = rpc_state();
  if (st.published_in_pass || st.passes_since_publish < 2U ||
      st.count == 0U || st.sink_fn == nullptr || st.sink_ctx == nullptr) {
    return;
  }
  const uint32_t now_ms = current_rpc_millis(st);
  if (st.last_publish_ms != 0U &&
      (now_ms == 0U || static_cast<uint32_t>(now_ms - st.last_publish_ms) < kMinInterFrameMs)) {
    return;
  }
  std::string first_frame = std::move(st.ring[st.head]);
  secure_zero_string(st.ring[st.head]);
  st.head = (st.head + 1) % kRpcQueueCapacity;
  --st.count;
  st.published_in_pass = true;
  st.passes_since_publish = 0;
  if (now_ms != 0U) {
    st.last_publish_ms = now_ms;
    st.awaiting_publish_stamp = false;
  } else {
    st.awaiting_publish_stamp = true;
  }
  st.sink_fn(st.sink_ctx, first_frame);
  secure_zero_string(first_frame);
}

inline void emit(
    std::nullptr_t,
    std::string_view service,
    std::initializer_list<RpcField> fields = {}) {
  std::string body = build_rpc_json(service, fields);
  const bool queued = enqueue_chunked_json(body);
  secure_zero_string(body);
  if (queued) flush_first_frame_if_idle();
}

template <typename SensorT>
  requires(!std::is_pointer_v<SensorT>)
inline void emit(
    SensorT *sensor,
    std::string_view service,
    std::initializer_list<RpcField> fields = {}) {
  bind_sensor_sink(sensor);
  std::string body = build_rpc_json(service, fields);
  const bool queued = enqueue_chunked_json(body);
  secure_zero_string(body);
  if (queued) flush_first_frame_if_idle();
}

template <typename SensorT>
inline void emit(
    SensorT *const *sensor_ptr,
    std::string_view service,
    std::initializer_list<RpcField> fields = {}) {
  if (sensor_ptr != nullptr) emit(*sensor_ptr, service, fields);
}

}  // namespace latter_rpc
