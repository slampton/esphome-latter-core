#pragma once

// ==============================================================================
// Latter Core: Zero-Privilege Diagnostic Outbox RPC Framing & PIN Wrap Crypto
// (include/latter_rpc.h)
// Encodes Device -> Home Assistant RPC calls onto the bound "System: RPC Outbox"
// text_sensor (<boot_hex>:<seq>:<part>/<total>|<json_slice>, <= 230 bytes/frame,
// UTF-8 codepoint-boundary safe, paced >= 35 ms per frame, atomic (seq) eviction,
// and v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32> HMAC-SHA256 PIN session wrap).
// ==============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

namespace latter_rpc {

static constexpr size_t kMaxRpcFrameBytes = 230;
static constexpr size_t kMaxRpcSlices = 32;
static constexpr size_t kRpcQueueCapacity = 64;
static constexpr uint32_t kMinInterFrameMs = 35U;

static constexpr size_t kPinSessionKeyBytes = 32;
static constexpr size_t kMinPinDigits = 4;
static constexpr size_t kMaxPinDigits = 12;

// ------------------------------------------------------------------------------
// Compiler-Barrier Volatile RAM Zeroization
// ------------------------------------------------------------------------------
void secure_zero_bytes(void *ptr, size_t len) noexcept;
void secure_zero_string(std::string &s) noexcept;

// ------------------------------------------------------------------------------
// Pure C++20 SHA-256 & HMAC-SHA256 (FIPS 180-4 / RFC 2104, Zero External Deps)
// ------------------------------------------------------------------------------
namespace detail {

struct PinSessionState {
  char key_id[9]{};
  uint8_t key_bytes[kPinSessionKeyBytes]{};
  uint32_t nonce = 0U;
  bool active = false;
};

PinSessionState &pin_session_state() noexcept;

}  // namespace detail

void hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *msg, size_t msg_len,
    uint8_t out[32]) noexcept;

void clear_pin_session_key() noexcept;
bool has_pin_session_key() noexcept;
bool set_pin_session_key(std::string_view key_id, std::string_view hex_key) noexcept;

// Wrap a 4..12 digit PIN into `v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32>` bound to `(entity_id, action)`.
// Returns an empty string if no session key is provisioned or `plain_code` is outside 4..12 ASCII digits.
std::string wrap_pin_code(
    std::string_view entity_id,
    std::string_view action,
    std::string_view plain_code);

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

RpcQueueState &rpc_state();
int issue_req_id(SlotChannel ch);
bool accept_req_id(SlotChannel ch, int req_id);
SlotChannel classify_browse_mode(std::string_view mode);
void append_json_escaped(std::string &out, std::string_view sv);

struct RpcField {
  const char *key = nullptr;
  std::string_view value{};
  bool include = true;

  constexpr RpcField(const char *k, std::string_view v, bool inc = true) noexcept
      : key(k), value(v), include(inc) {}
};

std::string build_rpc_json(
    std::string_view service,
    std::initializer_list<RpcField> fields);

// Back up from `max_end` so we never split a multi-byte UTF-8 codepoint across two text_sensor frames.
size_t utf8_safe_cut(std::string_view sv, size_t start, size_t max_bytes);
bool enqueue_chunked_json(std::string_view json_body);
bool has_pending_frames();
bool step_outbox_loop(uint32_t now_ms);

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

void flush_first_frame_if_idle();

void emit(
    std::nullptr_t,
    std::string_view service,
    std::initializer_list<RpcField> fields = {});

template <typename SensorT>
  requires(!std::is_pointer_v<SensorT>)
inline void emit(
    SensorT *sensor,
    std::string_view service,
    std::initializer_list<RpcField> fields = {}) {
  bind_sensor_sink(sensor);
  emit(nullptr, service, fields);
}

template <typename SensorT>
inline void emit(
    SensorT *const *sensor_ptr,
    std::string_view service,
    std::initializer_list<RpcField> fields = {}) {
  if (sensor_ptr != nullptr) emit(*sensor_ptr, service, fields);
}

}  // namespace latter_rpc
