#include "ui_widgets.h"

#include <cmath>
#include <cstring>

namespace latter_ui {
namespace ui {

namespace {

PinPadModalState g_pin_pad{};

struct HsvUnitRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const HsvUnitRgb *hsv_unit_lut_72() {
  static HsvUnitRgb lut[72]{};
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 72; i++) {
      const esphome::Color c = hsv_to_rgb(static_cast<float>(i * 5) / 360.0f, 1.0f, 1.0f);
      lut[i] = {c.r, c.g, c.b};
    }
    ready = true;
  }
  return lut;
}

}  // namespace

esphome::Color chrome_card_bg_on() { return esphome::Color(24, 30, 38); }
esphome::Color chrome_card_bg_off() { return esphome::Color(15, 18, 24); }
esphome::Color chrome_card_brd_on() { return esphome::Color(52, 62, 78); }
esphome::Color chrome_card_brd_off() { return esphome::Color(34, 40, 50); }
esphome::Color chrome_danger_bg() { return esphome::Color(34, 18, 22); }
esphome::Color chrome_modal_bg() { return esphome::Color(18, 22, 29); }
esphome::Color chrome_key_bg() { return esphome::Color(20, 25, 33); }
esphome::Color chrome_key_brd() { return esphome::Color(44, 54, 68); }
esphome::Color chrome_wheel_inner_ring() { return esphome::Color(70, 82, 96); }
esphome::Color chrome_wheel_outer_ring() { return esphome::Color(42, 50, 62); }
esphome::Color chrome_remote_pad_bg() { return esphome::Color(20, 25, 34); }
esphome::Color chrome_remote_pad_brd() { return esphome::Color(54, 66, 84); }
esphome::Color chrome_remote_ok_bg() { return esphome::Color(30, 38, 52); }
esphome::Color chrome_remote_pwr_bg() { return esphome::Color(42, 18, 22); }
esphome::Color chrome_remote_pwr_brd() { return esphome::Color(148, 52, 52); }

esphome::Color hsv_wheel_sector_color(int sector_idx_72, float sat) {
  int idx = sector_idx_72 % 72;
  if (idx < 0) idx += 72;
  const HsvUnitRgb u = hsv_unit_lut_72()[idx];
  const float s = std::max(0.0f, std::min(1.0f, sat));
  const float inv = 1.0f - s;
  return esphome::Color(
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.r) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.g) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.b) * s));
}

bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat) {
  const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
                 : ((SCREEN_MIN_DIM < 380) ? (static_cast<float>(SCREEN_MIN_DIM) / 420.0f) : 1.0f);
  const float dx = static_cast<float>(tx - cx);
  const float dy = static_cast<float>(ty - cy);
  const float dist_466 = std::sqrt(dx * dx + dy * dy) / ws;
  const float min_r = static_cast<float>(inner_r_466) - 12.0f;
  const float max_r = static_cast<float>(outer_r_466) + 14.0f;
  if (dist_466 < min_r || dist_466 > max_r) return false;

  float hue = std::atan2(dy, dx) * (180.0f / PI_F);
  if (hue < 0.0f) hue += 360.0f;
  const float span = std::max(1.0f, static_cast<float>(outer_r_466 - inner_r_466));
  const float norm = (dist_466 - static_cast<float>(inner_r_466)) / span;
  out_hue_deg = hue;
  out_sat = std::max(0.35f, std::min(1.0f, 0.35f + norm * 0.65f));
  return true;
}

PinPadModalState &pin_pad_state() {
  return g_pin_pad;
}

void pin_pad_scrub() {
  volatile char *p = g_pin_pad.digits;
  for (size_t i = 0; i < sizeof(g_pin_pad.digits); i++) {
    p[i] = '\0';
  }
  g_pin_pad.len = 0;
  g_pin_pad.open = false;
  g_pin_pad.target = PinPadTargetKind::NONE;
  g_pin_pad.error_msg[0] = '\0';
}

void pin_pad_open(PinPadTargetKind kind, const char *prompt_label) {
  pin_pad_scrub();
  g_pin_pad.open = true;
  g_pin_pad.target = kind;
  if (prompt_label != nullptr) {
    std::strncpy(g_pin_pad.prompt, prompt_label, sizeof(g_pin_pad.prompt) - 1);
    g_pin_pad.prompt[sizeof(g_pin_pad.prompt) - 1] = '\0';
  } else {
    g_pin_pad.prompt[0] = '\0';
  }
}

bool pin_pad_press_digit(uint8_t digit) {
  if (digit > 9 || g_pin_pad.len >= 8) return false;
  g_pin_pad.digits[g_pin_pad.len++] = static_cast<char>('0' + digit);
  g_pin_pad.digits[g_pin_pad.len] = '\0';
  g_pin_pad.error_msg[0] = '\0';
  return true;
}

void pin_pad_backspace() {
  if (g_pin_pad.len > 0) {
    g_pin_pad.digits[--g_pin_pad.len] = '\0';
  }
  g_pin_pad.error_msg[0] = '\0';
}

namespace {

bool is_sensitive_perimeter_action_impl(const std::string &domain_or_entity,
                                        const std::string &action, int level) {
  if (action == "unlock" || action == "disarm" || action == "unlock_door" || action == "open_cover") {
    return true;
  }
  if (domain_or_entity == "lock" && action == "open") {
    return true;
  }
  if (domain_or_entity == "whole_home.garage" && action == "turn_on") {
    return true;
  }
  if (domain_or_entity.rfind("cover.", 0) == 0 &&
      (domain_or_entity.find("garage") != std::string::npos ||
       domain_or_entity.find("gate") != std::string::npos) &&
      (action == "turn_on" || action == "open" || action == "toggle" ||
       (action == "set_level" && level > 0))) {
    return true;
  }
  return false;
}

}  // namespace

bool is_perimeter_action_blocked(const std::string &policy, const std::string &domain_or_entity,
                                 const std::string &action, int level) {
  if (policy != "block") return false;
  return is_sensitive_perimeter_action_impl(domain_or_entity, action, level);
}

bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level, bool alarm_has_code) {
  if (!is_sensitive_perimeter_action_impl(domain_or_entity, action, level)) return false;
  if (policy == "allow" || policy == "block") return false;
  if (policy == "require_pin") return true;
  // "auto" or unset: stationary displays and armed alarm codes require PIN; roaming displays allow.
#if LATTER_IS_STATIONARY
  (void)alarm_has_code;
  return true;
#else
  return alarm_has_code;
#endif
}

}  // namespace ui
}  // namespace latter_ui
