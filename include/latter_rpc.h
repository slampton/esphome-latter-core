#pragma once

// ==============================================================================
// Latter Core SDK: Zero-Privilege Diagnostic Outbox RPC Framing (latter_rpc.h)
// Encodes Device -> Home Assistant RPC calls onto the bound "System: RPC Outbox"
// text_sensor (<boot_hex>:<seq>:<part>/<total>|<json_slice>, <= 230 bytes/frame,
// UTF-8 codepoint-boundary safe, paced 1 frame per loop pass to prevent batching).
// ==============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_rpc {

static constexpr size_t kMaxRpcFrameBytes = 230;
static constexpr size_t kRpcQueueCapacity = 32;

enum class SlotChannel : uint8_t {
  MUSIC         = 0,
  AREA_LIST     = 1,
  AREA_ENTITIES = 2,
  CALENDAR      = 3,
  COUNT         = 4,
};

using PublishSinkFn = void (*)(void *ctx, const std::string &frame);

struct RpcQueueState {
  std::array<std::string, kRpcQueueCapacity> ring{};
  size_t head = 0;
  size_t tail = 0;
  size_t count = 0;
  uint32_t boot_id = 0;
  uint32_t next_seq = 1;
  uint32_t last_publish_ms = 0;
  uint32_t passes_since_publish = 2;
  bool published_in_pass = false;
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
  if (mode == "entities" || mode == "home") return SlotChannel::AREA_ENTITIES;
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

inline std::string build_rpc_json(
    std::string_view service,
    std::initializer_list<std::pair<const char *, std::string_view>> fields) {
  std::string out;
  out.reserve(160);
  out += "{\"s\":";
  append_json_escaped(out, service);
  out += ",\"d\":{";
  bool first = true;
  for (const auto &kv : fields) {
    if (kv.first == nullptr) continue;
    if (!first) out.push_back(',');
    first = false;
    append_json_escaped(out, kv.first);
    out.push_back(':');
    append_json_escaped(out, kv.second);
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

inline void enqueue_chunked_json(std::string_view json_body) {
  RpcQueueState &st = rpc_state();
  const uint32_t seq = st.next_seq++;
  if (st.next_seq == 0) st.next_seq = 1;

  // Reserve worst-case header budget: "<8hex>:<10dec>:<2dec>/<2dec>|" <= 27 bytes.
  static constexpr size_t kHeaderReserve = 28;
  static constexpr size_t kMaxSliceBytes = kMaxRpcFrameBytes - kHeaderReserve;

  std::array<std::string_view, 16> slices{};
  size_t total_parts = 0;
  size_t pos = 0;
  while (pos < json_body.size() && total_parts < slices.size()) {
    size_t next_pos = utf8_safe_cut(json_body, pos, kMaxSliceBytes);
    slices[total_parts++] = json_body.substr(pos, next_pos - pos);
    pos = next_pos;
  }
  if (total_parts == 0) {
    slices[0] = json_body;
    total_parts = 1;
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

    if (st.count == kRpcQueueCapacity) {
      // Drop oldest queued chunk only on extreme overflow so ring stays bounded.
      st.head = (st.head + 1) % kRpcQueueCapacity;
      --st.count;
    }
    st.ring[st.tail] = std::move(frame);
    st.tail = (st.tail + 1) % kRpcQueueCapacity;
    ++st.count;
  }
}

inline bool has_pending_frames() {
  return rpc_state().count > 0;
}

inline bool step_outbox_loop(uint32_t now_ms) {
  RpcQueueState &st = rpc_state();
  st.published_in_pass = false;
  if (st.passes_since_publish < 1000U) {
    ++st.passes_since_publish;
  }
  if (!has_pending_frames() || st.sink_fn == nullptr || st.sink_ctx == nullptr) return false;
  if (st.passes_since_publish < 2U) return true;
  if (st.last_publish_ms != 0 && static_cast<uint32_t>(now_ms - st.last_publish_ms) < 25U) {
    return true;
  }
  std::string next_frame = std::move(st.ring[st.head]);
  st.head = (st.head + 1) % kRpcQueueCapacity;
  --st.count;
  st.published_in_pass = true;
  st.passes_since_publish = 0;
  st.last_publish_ms = (now_ms == 0) ? 1U : now_ms;
  st.sink_fn(st.sink_ctx, next_frame);
  return has_pending_frames();
}

template <typename SensorT>
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

template <typename SensorT>
inline void emit(
    SensorT *sensor,
    std::string_view service,
    std::initializer_list<std::pair<const char *, std::string_view>> fields = {}) {
  bind_sensor_sink(sensor);
  enqueue_chunked_json(build_rpc_json(service, fields));
  RpcQueueState &st = rpc_state();
  if (!st.published_in_pass && st.passes_since_publish >= 2U &&
      st.count > 0 && st.sink_fn != nullptr && st.sink_ctx != nullptr) {
    std::string first_frame = std::move(st.ring[st.head]);
    st.head = (st.head + 1) % kRpcQueueCapacity;
    --st.count;
    st.published_in_pass = true;
    st.passes_since_publish = 0;
    st.sink_fn(st.sink_ctx, first_frame);
  }
}

template <typename SensorT>
inline void emit(
    SensorT *const *sensor_ptr,
    std::string_view service,
    std::initializer_list<std::pair<const char *, std::string_view>> fields = {}) {
  if (sensor_ptr != nullptr) emit(*sensor_ptr, service, fields);
}

}  // namespace latter_rpc
