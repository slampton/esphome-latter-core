#pragma once

// ==============================================================================
// Latter Material Design Icons (MDI) 1:1 Vector Catalog & Scanline Rasterizer
// (include/ui_mdi.h)
// Pure C++20 (zero ESPHome headers) so host unit tests verify all 7,447 icons
// standalone. Backed by LZSS-bucketed 24x24 Bezier bytecode in ui_mdi.cpp.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace latter_ui {

using MdiSpanEmitFn = void (*)(void *user, int x0, int x1_excl, int y, uint8_t alpha_q8);

// Canonical 32-bit FNV-1a hash of an MDI slug: strips optional "mdi:" / "mdi-",
// strips '-' / '_' / whitespace, and ASCII-lowercases. Returns 0 when empty.
constexpr uint32_t mdi_icon_hash(std::string_view name) noexcept {
  while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
  if (name.size() >= 4 &&
      (name[0] == 'm' || name[0] == 'M') &&
      (name[1] == 'd' || name[1] == 'D') &&
      (name[2] == 'i' || name[2] == 'I') &&
      (name[3] == ':' || name[3] == '-')) {
    name.remove_prefix(4);
  }
  uint32_t h = 0x811C9DC5U;
  bool any = false;
  for (char ch : name) {
    if (ch == '-' || ch == '_' || ch == ' ') continue;
    unsigned char uc = static_cast<unsigned char>(ch);
    if (uc >= 'A' && uc <= 'Z') uc = static_cast<unsigned char>(uc + ('a' - 'A'));
    h ^= static_cast<uint32_t>(uc);
    h *= 0x01000193U;
    any = true;
  }
  return any ? h : 0U;
}

std::size_t mdi_catalog_count() noexcept;
bool has_mdi_icon(std::string_view name) noexcept;
bool rasterize_mdi_icon(std::string_view name, int cx, int cy, int size_px,
                        MdiSpanEmitFn emit_fn, void *user) noexcept;

template <typename SpanFn>
inline bool rasterize_mdi_icon_fn(std::string_view name, int cx, int cy, int size_px, SpanFn &&fn) {
  auto tramp = [](void *u, int x0, int x1, int y, uint8_t a) {
    (*static_cast<SpanFn *>(u))(x0, x1, y, a);
  };
  return rasterize_mdi_icon(name, cx, cy, size_px, tramp, &fn);
}

}  // namespace latter_ui
