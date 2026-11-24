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
using IconSpanEmitFn = MdiSpanEmitFn;

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

// ==============================================================================
// Provider-Agnostic Icon Dispatch Boundary (D-078)
// Single dispatch seam where future compile-time or secondary icon packs
// (e.g., Material Symbols Rounded `ms:` / `msr:`) plug in by prefix without
// modifying any page renderer or widget call site.
// ==============================================================================
inline bool has_icon(std::string_view name) noexcept {
  return has_mdi_icon(name);
}

inline bool rasterize_icon(std::string_view name, int cx, int cy, int size_px,
                           IconSpanEmitFn emit_fn, void *user) noexcept {
  return rasterize_mdi_icon(name, cx, cy, size_px, emit_fn, user);
}

template <typename SpanFn>
inline bool rasterize_icon_fn(std::string_view name, int cx, int cy, int size_px, SpanFn &&fn) {
  auto tramp = [](void *u, int x0, int x1, int y, uint8_t a) {
    (*static_cast<SpanFn *>(u))(x0, x1, y, a);
  };
  return rasterize_icon(name, cx, cy, size_px, tramp, &fn);
}

// Pure C++20 zero-allocation state-aware MDI slug resolver (D-078):
//   state == 1 (active/on): upgrades "<base>-outline" -> "<base>" when the solid pair exists.
//   state == 0 (off/inactive): downgrades "<base>" -> "<base>-outline" in `buf` when the outline pair exists.
//   state == -1 (route/stateless) or state == 2 (unavailable): returns `slug` unchanged.
inline std::string_view resolve_state_mdi_slug(std::string_view slug, int8_t state,
                                               char *buf, std::size_t cap) noexcept {
  if (slug.empty()) return slug;
  constexpr std::string_view kOutlineSuffix = "-outline";
  const bool ends_with_outline =
      (slug.size() > kOutlineSuffix.size()) &&
      (slug.substr(slug.size() - kOutlineSuffix.size()) == kOutlineSuffix);

  if (state == 1) {
    if (ends_with_outline) {
      const std::string_view base = slug.substr(0U, slug.size() - kOutlineSuffix.size());
      if (has_mdi_icon(base)) return base;
    }
    return slug;
  }

  if (state == 0) {
    if (!ends_with_outline && buf != nullptr) {
      const std::size_t needed = slug.size() + kOutlineSuffix.size();
      if (needed <= cap) {
        for (std::size_t i = 0; i < slug.size(); ++i) buf[i] = slug[i];
        for (std::size_t i = 0; i < kOutlineSuffix.size(); ++i) buf[slug.size() + i] = kOutlineSuffix[i];
        if (needed < cap) buf[needed] = '\0';
        const std::string_view outlined(buf, needed);
        if (has_mdi_icon(outlined)) return outlined;
      }
    }
    return slug;
  }

  return slug;
}

}  // namespace latter_ui
