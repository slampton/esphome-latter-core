#pragma once

// ==============================================================================
// Latter UI Pure C++20 Color Value, Glass Variant & Chrome Tokens (include/ui_color.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// Seamlessly interoperates with external RGB types (e.g. esphome::Color) via
// structural templates without including ESPHome headers.
// ==============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

namespace latter_ui {

struct UiColor {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
  uint8_t w{0};

  constexpr UiColor() noexcept = default;
  constexpr UiColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t white = 0) noexcept
      : r(red), g(green), b(blue), w(white) {}
  constexpr explicit UiColor(uint32_t hex_rgb) noexcept
      : r(static_cast<uint8_t>((hex_rgb >> 16) & 0xFFU)),
        g(static_cast<uint8_t>((hex_rgb >> 8) & 0xFFU)),
        b(static_cast<uint8_t>(hex_rgb & 0xFFU)),
        w(0) {}

  template <typename ExtColorT>
    requires(!std::is_same_v<std::remove_cvref_t<ExtColorT>, UiColor> &&
             requires(const ExtColorT &c) {
               static_cast<uint8_t>(c.r);
               static_cast<uint8_t>(c.g);
               static_cast<uint8_t>(c.b);
             })
  constexpr UiColor(const ExtColorT &c) noexcept
      : r(static_cast<uint8_t>(c.r)),
        g(static_cast<uint8_t>(c.g)),
        b(static_cast<uint8_t>(c.b)),
        w([&]() constexpr noexcept -> uint8_t {
          if constexpr (requires { static_cast<uint8_t>(c.w); }) {
            return static_cast<uint8_t>(c.w);
          } else {
            return 0U;
          }
        }()) {}

  template <typename ExtColorT>
    requires(!std::is_same_v<std::remove_cvref_t<ExtColorT>, UiColor> &&
             std::is_constructible_v<ExtColorT, uint8_t, uint8_t, uint8_t, uint8_t> &&
             requires(const ExtColorT &c) { c.r; c.g; c.b; })
  constexpr operator ExtColorT() const noexcept {
    return ExtColorT(r, g, b, w);
  }

  constexpr bool is_on() const noexcept { return (r | g | b | w) != 0U; }

  friend constexpr bool operator==(UiColor a, UiColor b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.w == b.w;
  }
  friend constexpr bool operator!=(UiColor a, UiColor b) noexcept { return !(a == b); }

  static const UiColor BLACK;
  static const UiColor WHITE;
};

inline constexpr UiColor UiColor::BLACK{0, 0, 0, 0};
inline constexpr UiColor UiColor::WHITE{255, 255, 255, 0};
inline constexpr UiColor kColorOff{0, 0, 0, 0};

using Color = UiColor;

// Type-erased mutable color target slot (accepts UiColor* on host and esphome::Color* in YAML).
struct UiThemeSlot {
  void *ptr{nullptr};
  void (*assign_fn)(void *, UiColor) noexcept {nullptr};

  constexpr UiThemeSlot() noexcept = default;
  constexpr UiThemeSlot(std::nullptr_t) noexcept {}

  template <typename ColorT>
    requires requires(ColorT &dst) {
      dst.r = uint8_t{0};
      dst.g = uint8_t{0};
      dst.b = uint8_t{0};
    }
  UiThemeSlot(ColorT *target) noexcept
      : ptr(static_cast<void *>(target)),
        assign_fn(target != nullptr ? +[](void *raw, UiColor c) noexcept {
          auto *dst = static_cast<ColorT *>(raw);
          dst->r = c.r;
          dst->g = c.g;
          dst->b = c.b;
          if constexpr (requires { dst->w = uint8_t{0}; }) {
            dst->w = c.w;
          }
        } : nullptr) {}

  constexpr explicit operator bool() const noexcept { return ptr != nullptr && assign_fn != nullptr; }
  friend constexpr bool operator==(const UiThemeSlot &s, std::nullptr_t) noexcept { return !static_cast<bool>(s); }
  friend constexpr bool operator!=(const UiThemeSlot &s, std::nullptr_t) noexcept { return static_cast<bool>(s); }

  void assign(UiColor c) const noexcept {
    if (ptr != nullptr && assign_fn != nullptr) assign_fn(ptr, c);
  }
};

// Universal 2-Tier Neutral Smoked Glass Material System (D-037, D-041):
//   Clear        -> kChromeGlassTint(22, 27, 36), fixed backdrop-invariant 37.5-45.3% frosted template (96..116/256)
//   ReadableDark -> kChromeModalBg(16, 20, 28), ~81-95% floor (208..244/256) reserved for high-opacity opt-in
enum class GlassVariant : uint8_t {
  Clear = 0,
  ReadableDark = 1,
};

// Shared chrome color tokens (docs/device-ui-design.md).
inline constexpr UiColor kChromeShadow{0, 0, 0};
inline constexpr UiColor kChromeHomePill{175, 190, 210};
inline constexpr UiColor kChromeTrack{30, 38, 50};
inline constexpr UiColor kChromeTrackBorder{50, 62, 78};
inline constexpr UiColor kChromeTrackDim{16, 20, 26};
inline constexpr UiColor kChromeTrackBorderDim{24, 30, 38};
inline constexpr UiColor kChromeBarTrack{16, 20, 27};
inline constexpr UiColor kChromeCardBorder{38, 46, 58};
inline constexpr UiColor kChromePipInactive{95, 112, 134};
inline constexpr UiColor kChromeBackLabel{158, 158, 158};
inline constexpr UiColor kChromeGlassTint{22, 27, 36};
inline constexpr UiColor kChromeGlassSheen{255, 255, 255};
inline constexpr UiColor kChromeCardBgOn{24, 30, 38};
inline constexpr UiColor kChromeCardBgOff{15, 18, 24};
inline constexpr UiColor kChromeCardBrdOn{52, 62, 78};
inline constexpr UiColor kChromeCardBrdOff{34, 40, 50};
inline constexpr UiColor kChromeDangerBg{34, 18, 22};
inline constexpr UiColor kChromeDangerBrd{148, 52, 52};
inline constexpr UiColor kChromeModalBg{16, 20, 28};
inline constexpr UiColor kChromeKeyBg{20, 25, 33};
inline constexpr UiColor kChromeKeyBrd{44, 54, 68};
inline constexpr UiColor kChromeWheelInnerRing{70, 82, 96};
inline constexpr UiColor kChromeWheelOuterRing{42, 50, 62};
inline constexpr UiColor kChromeMuted{140, 152, 168};
inline constexpr UiColor kChromeSubtle{128, 140, 156};
inline constexpr UiColor kChromeCustomSecondary{192, 192, 192};

// Canonical Light Inspector color-temperature slider bounds (Kelvin).
inline constexpr int kLightKelvinMin = 2700;
inline constexpr int kLightKelvinMax = 6500;
inline constexpr UiColor kChromeLightFill{92, 68, 22};
inline constexpr UiColor kChromeCoverFill{34, 68, 96};
inline constexpr UiColor kChromeFanFill{24, 76, 64};
inline constexpr UiColor kChromeFanAccent{48, 196, 156};
inline constexpr UiColor kChromeTeleIce{185, 225, 255};
inline constexpr UiColor kChromeWarmKelvin{255, 168, 72};
inline constexpr UiColor kChromeCoolKelvin{185, 222, 255};
inline constexpr UiColor kChromeDegSilver{204, 214, 228};
inline constexpr UiColor kChromeDayGold{255, 225, 110};
inline constexpr UiColor kChromeRgbRed{255, 95, 95};
inline constexpr UiColor kChromeRgbGreen{90, 225, 135};
inline constexpr UiColor kChromeRgbBlue{95, 165, 255};
inline constexpr UiColor kChromeGridDivider{44, 52, 68};
inline constexpr UiColor kChromeGridBg{14, 18, 24};
inline constexpr UiColor kChromeGridLine{28, 34, 46};
inline constexpr UiColor kChromeCaptionBg{10, 14, 20};
inline constexpr UiColor kChromeDwellTrack{48, 56, 72};
inline constexpr UiColor kChromePlayGlyph{245, 248, 252};
inline constexpr UiColor kChromePauseGlyph{12, 16, 22};

inline UiColor hsv_to_rgb(float h, float s, float v) noexcept {
  h = h - static_cast<float>(static_cast<int>(h));
  if (h < 0.0f) h += 1.0f;
  float r = v, g = v, b = v;
  if (s > 0.0f) {
    const float h6 = h * 6.0f;
    const int sector = static_cast<int>(h6);
    const float f = h6 - static_cast<float>(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (sector % 6) {
      case 0: r = v; g = t; b = p; break;
      case 1: r = q; g = v; b = p; break;
      case 2: r = p; g = v; b = t; break;
      case 3: r = p; g = q; b = v; break;
      case 4: r = t; g = p; b = v; break;
      default: r = v; g = p; b = q; break;
    }
  }
  return {static_cast<uint8_t>(r * 255.0f),
          static_cast<uint8_t>(g * 255.0f),
          static_cast<uint8_t>(b * 255.0f)};
}

inline UiColor lerp_color(UiColor a, UiColor b, float k) noexcept {
  if (k <= 0.0f) return a;
  if (k >= 1.0f) return b;
  return {static_cast<uint8_t>(a.r + (b.r - a.r) * k),
          static_cast<uint8_t>(a.g + (b.g - a.g) * k),
          static_cast<uint8_t>(a.b + (b.b - a.b) * k)};
}

inline UiColor scale_color(UiColor c, float scale) noexcept {
  if (scale <= 0.0f) return kChromeShadow;
  if (scale >= 1.0f) return c;
  return {static_cast<uint8_t>(c.r * scale),
          static_cast<uint8_t>(c.g * scale),
          static_cast<uint8_t>(c.b * scale)};
}

template <typename ColorT>
inline void apply_custom_rgb_harmony(uint32_t rgb, ColorT &out_primary, ColorT &out_secondary) noexcept {
  out_primary = ColorT{static_cast<uint8_t>((rgb >> 16) & 0xFFU),
                       static_cast<uint8_t>((rgb >> 8) & 0xFFU),
                       static_cast<uint8_t>(rgb & 0xFFU)};
  out_secondary = static_cast<ColorT>(kChromeCustomSecondary);
}

inline void apply_custom_rgb_harmony_slots(uint32_t rgb, UiThemeSlot out_primary,
                                           UiThemeSlot out_secondary) noexcept {
  out_primary.assign(UiColor{static_cast<uint8_t>((rgb >> 16) & 0xFFU),
                             static_cast<uint8_t>((rgb >> 8) & 0xFFU),
                             static_cast<uint8_t>(rgb & 0xFFU)});
  out_secondary.assign(kChromeCustomSecondary);
}

inline std::string format_hex_rgb(uint32_t rgb) {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "#%06lX", static_cast<unsigned long>(rgb & 0xFFFFFFUL));
  return std::string(buf);
}

inline float &color_wheel_hue_deg() noexcept {
  static float h = 210.0f;
  return h;
}

inline float &color_wheel_sat() noexcept {
  static float s = 0.78f;
  return s;
}

}  // namespace latter_ui
