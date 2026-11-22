#include "ui_pages.h"

#include <algorithm>
#include <cmath>

namespace latter_ui {
namespace {

UiColor get_va_radiation_color(bool is_solaris, float t) {
  t = t - std::floor(t);
  if (is_solaris) {
    const UiColor c0(35, 210, 110), c1(255, 220, 30), c2(250, 100, 15);
    if (t < 0.333f) return lerp_color(c0, c1, t / 0.333f);
    if (t < 0.667f) return lerp_color(c1, c2, (t - 0.333f) / 0.334f);
    return lerp_color(c2, c0, (t - 0.667f) / 0.333f);
  }
  const UiColor n0(0, 229, 255), n1(138, 43, 226), n2(224, 64, 251), n3(0, 229, 255);
  if (t < 0.333f) return lerp_color(n0, n1, t / 0.333f);
  if (t < 0.667f) return lerp_color(n1, n2, (t - 0.333f) / 0.334f);
  return lerp_color(UiColor(224, 64, 255), n3, (t - 0.667f) / 0.333f);
}

}  // namespace

void render_voice_assistant_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                    const SystemTakeoverState &st) {
  (void)ctx;
  const int cx = st.cx, cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const bool is_speaking = st.va_tts_received || st.va_status_text == "Responding..." || st.va_status_text == "Speaking...";
  const bool is_thinking = !is_speaking && (st.va_status_text == "Thinking..." || st.va_status_text == "Processing...");
  const bool va_vad_active = st.va_vad_active;
  const UiStrView profile_opt = st.va_visual_profile_opt;
  const UiColor col_white = st.col_white, col_solaris_gold = st.col_solaris_gold;
  frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::VA_CANCEL_BTN, kHitTap);

  const bool is_solaris = (profile_opt == "Solaris"), is_light_bar = (profile_opt == "Light Bar");
  const bool is_dot_wave = (profile_opt == "Dot Wave"), is_rings = (profile_opt == "Rings");

  static float smooth_vad = 0.0f, smooth_think = 0.0f, smooth_speak = 0.0f, spin_blend = 0.0f;
  static uint32_t think_hold_until_ms = 0U, speak_start_ms = 0U, last_va_frame_ms = 0U;
  static bool prev_speaking = false;
  static int prev_va_stage = -1;

  const int cur_va_stage = is_speaking ? 2 : (is_thinking ? 1 : 0);
  const bool gap_reset = (last_va_frame_ms == 0U || now_ms < last_va_frame_ms || (now_ms - last_va_frame_ms) > 300U);
  if (gap_reset || (prev_va_stage >= 1 && cur_va_stage == 0)) {
    smooth_vad = smooth_think = smooth_speak = spin_blend = 0.0f;
    think_hold_until_ms = speak_start_ms = 0U;
    prev_speaking = false;
  }
  const uint32_t raw_dt_ms = (!gap_reset && now_ms >= last_va_frame_ms) ? (now_ms - last_va_frame_ms) : 60U;
  last_va_frame_ms = now_ms;
  prev_va_stage = cur_va_stage;
  if (is_speaking && !prev_speaking) speak_start_ms = now_ms;
  prev_speaking = is_speaking;

  const float dt_sec = std::clamp(static_cast<float>(raw_dt_ms) * 0.001f, 0.005f, 0.250f);
  const float env_alpha = 1.0f - std::exp(-dt_sec * (1.0f / 0.24f));
  smooth_vad += ((va_vad_active ? 1.0f : 0.0f) - smooth_vad) * env_alpha;
  smooth_think += ((is_thinking ? 1.0f : 0.0f) - smooth_think) * env_alpha;
  smooth_speak += ((is_speaking ? 1.0f : 0.0f) - smooth_speak) * env_alpha;

  if (is_light_bar) {
    const UiColor c_bar_blue(0, 52, 235), c_bar_deep(0, 22, 115), c_bar_cyan(0, 229, 255), c_bar_ice(215, 252, 255);
    const float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;
    const float syl_env = is_speaking ? ((0.55f + 0.25f * fast_sin(t_sec * 9.5f) + 0.20f * fast_cos(t_sec * 15.3f)) * smooth_speak) : 0.0f;
    const int max_ring_r = SCREEN_MIN_R - 3, base_ring_r = max_ring_r - 14;
    const int inner_glow_r = (base_ring_r - 6) - static_cast<int>(6.0f * smooth_vad + 8.0f * syl_env);
    for (int r = inner_glow_r; r <= max_ring_r; r++) {
      UiColor rc = (r < base_ring_r)
                       ? scale_color(c_bar_deep, std::pow(static_cast<float>(r - inner_glow_r) / static_cast<float>(base_ring_r - inner_glow_r), 2.0f) * 0.85f)
                       : ((r <= max_ring_r - 3) ? c_bar_blue : c_bar_deep);
      fast_circle(it, cx, cy, r, rc);
    }
    const float head_f = is_thinking ? std::fmod(t_sec * 52.0f, 60.0f)
                       : (is_speaking ? std::fmod(t_sec * 34.0f, 60.0f) : std::fmod(t_sec * (24.0f + 14.0f * smooth_vad), 60.0f));
    const float head2_f = std::fmod(head_f + 30.0f, 60.0f);
    for (int i = 0; i < 60; i++) {
      const int next_i = (i + 1) % 60;
      float cyan_w = 0.0f, d1 = head_f - static_cast<float>(i), d2 = head2_f - static_cast<float>(i);
      if (d1 < 0.0f) d1 += 60.0f;
      if (d2 < 0.0f) d2 += 60.0f;
      if (is_speaking) {
        const float tail_len = 13.0f + 10.0f * syl_env;
        if (d1 < tail_len) cyan_w = std::fmax(cyan_w, 1.0f - (d1 / tail_len));
        if (d2 < tail_len) cyan_w = std::fmax(cyan_w, 1.0f - (d2 / tail_len));
      } else if (is_thinking) {
        if (d1 < 14.0f) cyan_w = 1.0f - (d1 * (1.0f / 14.0f));
        else if (d2 < 14.0f) cyan_w = 1.0f - (d2 * (1.0f / 14.0f));
      } else {
        const float tail_len = 14.0f + 10.0f * smooth_vad;
        if (d1 < tail_len) cyan_w = 1.0f - (d1 / tail_len);
        if (smooth_vad > 0.10f && d2 < tail_len * 0.75f) cyan_w = std::fmax(cyan_w, (1.0f - (d2 / (tail_len * 0.75f))) * smooth_vad);
      }
      if (cyan_w > 0.05f) {
        const UiColor seg_c = (cyan_w > 0.74f) ? lerp_color(c_bar_cyan, c_bar_ice, (cyan_w - 0.74f) * 3.8f)
                                               : lerp_color(c_bar_blue, c_bar_cyan, cyan_w * 1.35f);
        const float sx0 = DIAL_SIN[i], cy0 = -DIAL_COS[i], sx1 = DIAL_SIN[next_i], cy1 = -DIAL_COS[next_i];
        const int r_in_c = (base_ring_r - 1) - static_cast<int>(cyan_w * (4.0f + 5.0f * (smooth_vad + syl_env)));
        for (int r = r_in_c; r <= max_ring_r - 2; r += 2) {
          const float rf = static_cast<float>(r);
          const int x0 = cx + static_cast<int>(rf * sx0), y0 = cy + static_cast<int>(rf * cy0);
          const int x1 = cx + static_cast<int>(rf * sx1), y1 = cy + static_cast<int>(rf * cy1);
          fast_line(it, x0, y0, x1, y1, seg_c);
          fast_line(it, x0, y0 + 1, x1, y1 + 1, seg_c);
        }
      }
    }
    return;
  }

  const float va_scale = static_cast<float>(SCREEN_MIN_DIM) / 466.0f;

  // Profile 2: "Dot Wave"
  if (is_dot_wave) {
    const UiColor c_halo_outer(32, 58, 108), c_halo_inner(110, 168, 255);
    const int cap_r = scale_min(12);
    static float g_phase = 0.0f;
    if (gap_reset) g_phase = 0.0f;
    if (is_thinking) think_hold_until_ms = now_ms + 950U;
    const bool show_thinking_spin = is_thinking || (think_hold_until_ms > 0U && now_ms < think_hold_until_ms);
    spin_blend += ((show_thinking_spin ? 1.0f : 0.0f) - spin_blend) * (1.0f - std::exp(-dt_sec * (1.0f / 0.18f)));

    auto draw_glowing_capsule = [&](int bx, int by, int r, int half_h) {
      if (half_h <= 1) {
        fast_filled_circle(it, bx, by, r + 4, c_halo_outer);
        fast_filled_circle(it, bx, by, r + 2, c_halo_inner);
        fast_filled_circle(it, bx, by, r, col_white);
      } else {
        const int y_top = by - half_h, y_bot = by + half_h, h_span = 2 * half_h;
        fast_filled_circle(it, bx, y_top, r + 3, c_halo_outer);
        fast_filled_circle(it, bx, y_bot, r + 3, c_halo_outer);
        fill_rect_row_major(it, bx - (r + 3), y_top, 2 * (r + 3) + 1, h_span, c_halo_outer);
        fast_filled_circle(it, bx, y_top, r + 1, c_halo_inner);
        fast_filled_circle(it, bx, y_bot, r + 1, c_halo_inner);
        fill_rect_row_major(it, bx - (r + 1), y_top, 2 * (r + 1) + 1, h_span, c_halo_inner);
        fast_filled_circle(it, bx, y_top, r, col_white);
        fast_filled_circle(it, bx, y_bot, r, col_white);
        fill_rect_row_major(it, bx - r, y_top, 2 * r + 1, h_span, col_white);
      }
    };

    if (show_thinking_spin || spin_blend > 0.15f) {
      g_phase = std::fmod(g_phase + dt_sec * 4.2f, 200.0f * PI_F);
      const float orbit_r = 40.0f * va_scale * spin_blend;
      static constexpr float LINE_X[3] = {-50.0f, 0.0f, 50.0f};
      for (int d = 0; d < 3; d++) {
        const float ang = g_phase + static_cast<float>(d) * (2.0f * PI_F / 3.0f);
        if (spin_blend > 0.65f) {
          for (int tail = 3; tail >= 1; tail--) {
            float ts = 0.0f, tc = 1.0f;
            fast_sincos(ang - static_cast<float>(tail) * 0.18f, ts, tc);
            fast_filled_circle(it, cx + static_cast<int>(orbit_r * tc), cy + static_cast<int>(orbit_r * ts),
                               std::max(2, scale_min(11 - tail * 2)),
                               scale_color(c_halo_inner, static_cast<float>(4 - tail) * 0.22f * spin_blend));
          }
        }
        float s = 0.0f, c = 1.0f;
        fast_sincos(ang, s, c);
        draw_glowing_capsule(cx + static_cast<int>((1.0f - spin_blend) * LINE_X[d] * va_scale + orbit_r * c),
                             cy + static_cast<int>(orbit_r * s), cap_r, 0);
      }
      return;
    }

    const int dot_off_x[3] = {-scale_min(50), 0, scale_min(50)};
    if (is_speaking) {
      g_phase = std::fmod(g_phase + dt_sec * 5.6f, 200.0f * PI_F);
      const float syl_env = (0.50f + 0.30f * fast_sin(g_phase * 0.9f) + 0.20f * fast_cos(g_phase * 1.5f)) * smooth_speak;
      static constexpr float BAR_MULT[3] = {0.95f, 1.25f, 1.08f};
      static constexpr float BAR_PHASE[3] = {0.0f, 1.4f, 2.8f};
      static constexpr float BAR_MAX_H[3] = {22.0f, 36.0f, 22.0f};
      for (int d = 0; d < 3; d++) {
        const float w = 0.50f + 0.50f * fast_sin(g_phase * BAR_MULT[d] + BAR_PHASE[d]);
        draw_glowing_capsule(cx + dot_off_x[d], cy, cap_r,
                             static_cast<int>(w * BAR_MAX_H[d] * va_scale * (0.35f + 0.65f * syl_env)));
      }
      return;
    }

    g_phase = std::fmod(g_phase + dt_sec * (2.9f + 1.3f * smooth_vad), 200.0f * PI_F);
    const float wave_amp = (14.0f + 12.0f * smooth_vad) * va_scale;
    for (int d = 0; d < 3; d++) {
      const float ph = g_phase - static_cast<float>(d) * 2.094395f;
      const float wave_y = 0.82f * fast_sin(ph) - 0.28f * fast_cos(2.0f * ph);
      draw_glowing_capsule(cx + dot_off_x[d], cy + static_cast<int>(wave_y * wave_amp), cap_r, 0);
    }
    return;
  }

  // Profile 3: "Rings"
  if (is_rings) {
    const UiColor c_cyan_bright(28, 214, 255), c_cyan_ice(195, 248, 255);
    const UiColor c_cyan_mid(16, 128, 165), c_cyan_dim(8, 62, 84), c_cyan_glow(5, 34, 48);
    static uint32_t last_j_ms = 0U;
    static float j_angle = 0.0f;
    const float dt_j = (last_j_ms > 0U && now_ms >= last_j_ms && (now_ms - last_j_ms) <= 250U)
                           ? (static_cast<float>(now_ms - last_j_ms) * 0.001f) : 0.045f;
    last_j_ms = now_ms;

    const float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;
    const float syl_env = is_speaking
        ? ((0.50f + 0.30f * fast_sin(t_sec * 10.5f) + 0.20f * fast_cos(t_sec * 16.8f)) * smooth_speak) : 0.0f;
    const float energy = std::fmin(1.0f, smooth_vad * 0.85f + syl_env);
    const float rot_speed = is_thinking ? 1.85f : (is_speaking ? 0.85f : (0.48f + 0.45f * smooth_vad));
    j_angle = std::fmod(j_angle + dt_j * rot_speed, 2.0f * PI_F);

    auto draw_hud_arc = [&](float r_in, float r_out, float ang_start, float ang_span, UiColor col, int step_deg = 3) {
      const int steps = std::max(2, static_cast<int>((ang_span * (180.0f / PI_F)) / static_cast<float>(step_deg)));
      const float d_ang = ang_span / static_cast<float>(steps);
      for (int s = 0; s < steps; s++) {
        const float a0 = ang_start + static_cast<float>(s) * d_ang;
        float s0 = 0.0f, c0 = 1.0f, s1 = 0.0f, c1 = 1.0f;
        fast_sincos(a0, s0, c0);
        fast_sincos(a0 + d_ang, s1, c1);
        for (float r = r_in * va_scale; r <= r_out * va_scale; r += 1.5f) {
          fast_line(it, cx + static_cast<int>(r * s0), cy - static_cast<int>(r * c0),
                    cx + static_cast<int>(r * s1), cy - static_cast<int>(r * c1), col);
        }
      }
    };

    const float a_cw1 = j_angle;
    fast_circle(it, cx, cy, static_cast<int>(198.0f * va_scale), c_cyan_glow);
    fast_circle(it, cx, cy, static_cast<int>(188.0f * va_scale), c_cyan_mid);
    fast_circle(it, cx, cy, static_cast<int>(182.0f * va_scale), c_cyan_dim);
    draw_hud_arc(189.0f, 195.0f, a_cw1 - 0.18f, 0.36f, c_cyan_bright);
    draw_hud_arc(189.0f, 194.0f, a_cw1 + 1.75f, 1.05f, c_cyan_bright);
    draw_hud_arc(189.0f, 195.0f, a_cw1 + 2.92f, 0.42f, c_cyan_ice);
    draw_hud_arc(189.0f, 194.0f, a_cw1 + 5.25f, 0.32f, c_cyan_bright);
    for (int n = 0; n < 3; n++) draw_hud_arc(189.0f, 194.0f, a_cw1 + 0.52f + static_cast<float>(n) * 0.13f, 0.08f, c_cyan_bright, 2);
    draw_hud_arc(173.0f, 177.0f, a_cw1 + 3.45f, 0.85f, c_cyan_bright);
    for (int i = 0; i < 60; i += 2) {
      if ((i >= 8 && i <= 22) || (i >= 34 && i <= 48)) {
        float ds = 0.0f, dc = 1.0f;
        fast_sincos(a_cw1 + static_cast<float>(i) * (2.0f * PI_F / 60.0f), ds, dc);
        fast_filled_circle(it, cx + static_cast<int>(175.0f * va_scale * ds),
                           cy - static_cast<int>(175.0f * va_scale * dc), (i % 6 == 0) ? 2 : 1, c_cyan_mid);
      }
    }

    const float a_ccw2 = -j_angle * 1.15f;
    fast_circle(it, cx, cy, scale_min(160), c_cyan_dim);
    fast_circle(it, cx, cy, scale_min(134), c_cyan_dim);
    draw_hud_arc(155.0f, 160.0f, a_ccw2 - 0.35f, 0.55f, c_cyan_bright);
    draw_hud_arc(146.0f, 148.0f, a_ccw2 - 0.65f, 2.25f, c_cyan_mid);

    for (int b = 0; b < 14; b++) {
      float bs = 0.0f, bc = 1.0f;
      fast_sincos(a_ccw2 - 0.10f + static_cast<float>(b) * 0.125f, bs, bc);
      const float px = bc * va_scale, py = bs * va_scale, r_in_b = 120.0f * va_scale;
      const float r_out_b = (129.0f + energy * 6.0f * (0.5f + 0.5f * DIAL_SIN[(b * 5 + static_cast<int>(now_ms / 40U)) % 60])) * va_scale;
      const int x0 = cx + static_cast<int>(r_in_b * bs - 2.5f * px), y0 = cy - static_cast<int>(r_in_b * bc - 2.5f * py);
      const int x1 = cx + static_cast<int>(r_out_b * bs - 2.5f * px), y1 = cy - static_cast<int>(r_out_b * bc - 2.5f * py);
      const int x2 = cx + static_cast<int>(r_out_b * bs + 2.5f * px), y2 = cy - static_cast<int>(r_out_b * bc + 2.5f * py);
      const int x3 = cx + static_cast<int>(r_in_b * bs + 2.5f * px), y3 = cy - static_cast<int>(r_in_b * bc + 2.5f * py);
      const UiColor cell_c = (is_thinking ? (((static_cast<int>(now_ms / 45U) - b) & 3) == 0) : (energy > 0.15f && (b % 2 == 0)))
                                 ? c_cyan_ice : c_cyan_bright;
      fast_line(it, x0, y0, x1, y1, cell_c);
      fast_line(it, x1, y1, x2, y2, cell_c);
      fast_line(it, x2, y2, x3, y3, cell_c);
      fast_line(it, x3, y3, x0, y0, cell_c);
    }

    for (int d = 0; d < 12; d++) {
      float ds = 0.0f, dc = 1.0f;
      fast_sincos(a_ccw2 + 0.55f + static_cast<float>(d) * 0.13f, ds, dc);
      fast_filled_circle(it, cx + static_cast<int>(140.0f * va_scale * ds), cy - static_cast<int>(140.0f * va_scale * dc),
                         (d >= 3 && d <= 8) ? 3 : 2, (d % 3 == 0 && energy > 0.2f) ? c_cyan_ice : c_cyan_bright);
      if (d >= 4 && d <= 9) {
        fast_filled_circle(it, cx + static_cast<int>(151.0f * va_scale * ds), cy - static_cast<int>(151.0f * va_scale * dc), 2, c_cyan_mid);
      }
    }
    for (int k = 0; k < 13; k++) {
      float ks = 0.0f, kc = 1.0f;
      fast_sincos(a_ccw2 + 2.85f + static_cast<float>(k) * 0.095f, ks, kc);
      fast_line(it, cx + static_cast<int>(122.0f * va_scale * ks), cy - static_cast<int>(122.0f * va_scale * kc),
                cx + static_cast<int>(132.0f * va_scale * ks), cy - static_cast<int>(132.0f * va_scale * kc), c_cyan_bright);
    }

    const float a_cw3 = j_angle * 0.85f;
    fast_circle(it, cx, cy, scale_min(102), c_cyan_mid);
    for (int i = 0; i < 60; i++) {
      float ts = 0.0f, tc = 1.0f;
      fast_sincos(a_cw3 + static_cast<float>(i) * (2.0f * PI_F / 60.0f), ts, tc);
      const float r0 = 102.0f * va_scale, r1 = ((i % 5 == 0) ? 108.0f : 105.0f) * va_scale;
      fast_line(it, cx + static_cast<int>(r0 * ts), cy - static_cast<int>(r0 * tc),
                cx + static_cast<int>(r1 * ts), cy - static_cast<int>(r1 * tc), (i % 5 == 0) ? c_cyan_bright : c_cyan_dim);
    }
    draw_hud_arc(97.0f, 101.0f, a_cw3 + 0.85f, 1.10f, c_cyan_bright);
    draw_hud_arc(97.0f, 101.0f, a_cw3 + 3.90f, 0.95f, c_cyan_bright);

    fast_circle(it, cx, cy, scale_min(82), c_cyan_mid);
    fast_circle(it, cx, cy, scale_min(74), c_cyan_bright);
    fast_circle(it, cx, cy, scale_min(73), c_cyan_mid);
    if (energy > 0.08f || is_thinking) {
      fast_circle(it, cx, cy, scale_min(64 - static_cast<int>(energy * 5.0f)), scale_color(c_cyan_bright, 0.45f + 0.55f * energy));
    }
    draw_hud_arc(74.0f, 79.0f, 0.35f, 1.35f, (energy > 0.3f) ? c_cyan_ice : c_cyan_bright);
    draw_hud_arc(74.0f, 79.0f, 2.35f, 1.15f, (energy > 0.3f) ? c_cyan_ice : c_cyan_bright);
    draw_hud_arc(74.0f, 79.0f, 4.25f, 1.25f, c_cyan_bright);

    const int r_ret_out = scale_min(88), r_ret_mid = scale_min(82), r_ret_in = scale_min(68), ret_tick = scale_min(4);
    fast_line(it, cx, cy - r_ret_out, cx, cy - r_ret_in, c_cyan_ice);
    fast_line(it, cx - ret_tick, cy - r_ret_mid, cx + ret_tick, cy - r_ret_mid, c_cyan_bright);
    fast_line(it, cx, cy + r_ret_in, cx, cy + r_ret_out, c_cyan_ice);
    fast_line(it, cx - ret_tick, cy + r_ret_mid, cx + ret_tick, cy + r_ret_mid, c_cyan_bright);
    fast_line(it, cx - r_ret_out, cy, cx - r_ret_in, cy, c_cyan_ice);
    fast_line(it, cx - r_ret_mid, cy - ret_tick, cx - r_ret_mid, cy + ret_tick, c_cyan_bright);
    fast_line(it, cx + r_ret_in, cy, cx + r_ret_out, cy, c_cyan_ice);
    fast_line(it, cx + r_ret_mid, cy - ret_tick, cx + r_ret_mid, cy + ret_tick, c_cyan_bright);
    return;
  }

  // Profile 4: "Nebula"
  if (!is_solaris) {
    const UiColor c_elec_cyan(0, 229, 255), c_deep_azure(28, 110, 225);
    const UiColor c_royal_purple(124, 42, 208), c_amethyst(168, 72, 242);
    const UiColor c_neon_magenta(255, 45, 155), c_pink_hi(255, 130, 205), c_pink_spec(255, 185, 232);
    const float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;

    auto render_nebula_purple_orb = [&](int core_r, float energy_boost) {
      const float shift = std::fmin(1.0f, (0.50f + 0.50f * fast_sin(t_sec * 1.50f)) * 0.65f + energy_boost * 0.35f);
      const UiColor outer_col = lerp_color(UiColor(68, 20, 130), UiColor(126, 36, 212), shift * 0.85f);
      const UiColor mid_col = lerp_color(UiColor(126, 36, 212), UiColor(176, 52, 234), 0.35f + 0.65f * shift);
      const UiColor inner_col = lerp_color(UiColor(176, 52, 234), UiColor(234, 58, 174), 0.25f + 0.65f * shift);
      for (int b = 6; b >= 1; b--) {
        const float k = static_cast<float>(7 - b) * (1.0f / 7.0f);
        fast_circle(it, cx, cy, core_r + b, scale_color(outer_col, k * k * 0.76f));
      }
      const float inv_r = 1.0f / static_cast<float>(core_r > 1 ? core_r : 1);
      for (int r = core_r; r >= 1; r -= 2) {
        const float u = 1.0f - static_cast<float>(r) * inv_r;
        const float s = std::clamp(u * u * (3.0f - 2.0f * u) + 0.14f * fast_sin(u * 4.2f - t_sec * 2.3f), 0.0f, 1.0f);
        fast_filled_circle(it, cx, cy, r,
                           (s < 0.55f) ? lerp_color(outer_col, mid_col, s * (1.0f / 0.55f))
                                       : lerp_color(mid_col, inner_col, (s - 0.55f) * (1.0f / 0.45f)));
      }
    };

    auto draw_silk_ribbon_3d = [&](float base_r, float a2, float a3, float a5, int ph2, int ph3, int ph5,
                                   float rx, float ry, float rz, UiColor col_core, UiColor col_aura, bool bloom) {
      const Mat3 rot = Mat3::from_euler(rx, ry, rz);
      int prev_x = 0, prev_y = 0, first_x = 0, first_y = 0;
      int prev_ix = 0, prev_iy = 0, first_ix = 0, first_iy = 0;
      int prev_ox = 0, prev_oy = 0, first_ox = 0, first_oy = 0;
      float prev_z = 0.0f;
      for (int i = 0; i < 60; i += 2) {
        const float r = base_r + a2 * DIAL_SIN[(i * 2 + ph2) % 60] + a3 * DIAL_COS[(i * 3 + ph3) % 60];
        const float z_local = a5 * DIAL_SIN[(i * 5 + ph5) % 60] * 3.2f, ux = DIAL_SIN[i], uy = -DIAL_COS[i];
        const Vec3 v_mid = rot.transform({r * ux, r * uy, z_local});
        const ProjectedPoint p = project_3d(v_mid, cx, cy, 260.0f, 320.0f);
        const ProjectedPoint pi = project_3d(rot.transform({(r - 2.2f) * ux, (r - 2.2f) * uy, z_local}), cx, cy, 260.0f, 320.0f);
        const ProjectedPoint po = project_3d(rot.transform({(r + 2.2f) * ux, (r + 2.2f) * uy, z_local}), cx, cy, 260.0f, 320.0f);
        if (i == 0) {
          first_x = p.x; first_y = p.y; first_ix = pi.x; first_iy = pi.y; first_ox = po.x; first_oy = po.y;
        } else {
          const float depth_k = std::clamp(0.92f - (0.5f * (v_mid.z + prev_z) / (base_r + 15.0f)) * 0.22f, 0.72f, 1.0f);
          const UiColor c_halo = scale_color(col_aura, depth_k * 0.78f);
          if (bloom) {
            fast_line(it, prev_ix, prev_iy, pi.x, pi.y, c_halo);
            fast_line(it, prev_ox, prev_oy, po.x, po.y, c_halo);
          }
          draw_glow_line(it, prev_x, prev_y, p.x, p.y, scale_color(col_core, depth_k), c_halo);
        }
        prev_x = p.x; prev_y = p.y; prev_ix = pi.x; prev_iy = pi.y; prev_ox = po.x; prev_oy = po.y; prev_z = v_mid.z;
      }
      if (bloom) {
        fast_line(it, prev_ix, prev_iy, first_ix, first_iy, scale_color(col_aura, 0.75f));
        fast_line(it, prev_ox, prev_oy, first_ox, first_oy, scale_color(col_aura, 0.75f));
      }
      draw_glow_line(it, prev_x, prev_y, first_x, first_y, col_core, scale_color(col_aura, 0.80f));
    };

    if (is_speaking) {
      const float syl_env = std::fmax(0.18f, (0.55f + 0.25f * fast_sin(t_sec * 11.5f) + 0.20f * fast_cos(t_sec * 19.3f)) * smooth_speak);
      const uint32_t speak_elapsed = now_ms - speak_start_ms;
      for (int w = 0; w < 5; w++) {
        const uint32_t launch_offset = static_cast<uint32_t>(w) * 380U;
        if (speak_elapsed < launch_offset) continue;
        const float progress = static_cast<float>((speak_elapsed - launch_offset) % 1900U) * (1.0f / 1900.0f);
        const int r_center = static_cast<int>((38.0f + progress * 180.0f) * va_scale);
        const int band_w = std::max(3, ((w % 2 == 0) ? 4 : 3) + static_cast<int>((13.0f * progress * (1.15f - 0.25f * progress)) * (0.80f + 0.20f * syl_env)));
        const int half_w = band_w >> 1;
        const float wave_color_phase = static_cast<float>(w) * 0.20f + progress * 0.34f + static_cast<float>(now_ms % 4000U) * 0.00025f;
        const float radial_env = (1.0f - progress * 0.62f) * smooth_speak, inv_half = 1.0f / static_cast<float>(half_w + 1);
        for (int d = -half_w; d <= half_w; d += 2) {
          const int r_curr = r_center + d;
          if (r_curr < scale_min(34) || r_curr > scale_min(226)) continue;
          const float edge_u = static_cast<float>(d < 0 ? -d : d) * inv_half, bell = 1.0f - edge_u * edge_u;
          const float pix_fade = radial_env * (0.22f + 0.78f * ((progress > 0.35f) ? (bell * bell) : bell));
          const UiColor base_col = scale_color(get_va_radiation_color(false, wave_color_phase + static_cast<float>(d) * 0.016f), pix_fade);
          fast_circle(it, cx, cy, r_curr, (edge_u < 0.28f) ? lerp_color(base_col, scale_color(c_pink_hi, pix_fade), 0.32f * (1.0f - progress * 0.5f)) : base_col);
        }
      }
      render_nebula_purple_orb(scale_min(25 + static_cast<int>(5.0f * syl_env)), syl_env);
      return;
    }

    if (is_thinking) {
      const int tick = static_cast<int>(now_ms / 22U);
      const float pulse = fast_sin(static_cast<float>(now_ms % 650U) * (2.0f * PI_F / 650.0f)), vortex_r = 68.0f + 3.0f * pulse;
      fast_circle(it, cx, cy, scale_min(74), UiColor(28, 18, 46));
      fast_circle(it, cx, cy, scale_min(118), UiColor(24, 16, 40));
      draw_silk_ribbon_3d(vortex_r + 3.0f, 6.0f, 5.0f, 3.5f, (tick * 2) % 60, (tick * 3) % 60, tick % 60,
                          0.68f * fast_sin(t_sec * 1.3f), 0.55f * fast_cos(t_sec * 1.1f), t_sec * 0.8f, c_amethyst, c_royal_purple, true);
      draw_silk_ribbon_3d(vortex_r, -5.0f, 6.0f, -3.0f, (tick * 2 + 30) % 60, (tick + 15) % 60, (tick * 2) % 60,
                          -0.62f * fast_cos(t_sec * 1.2f), 0.65f * fast_sin(t_sec * 1.4f), -t_sec * 0.7f, c_pink_hi, c_neon_magenta, true);
      draw_silk_ribbon_3d(vortex_r - 3.0f, 4.0f, -4.0f, 2.5f, (tick + 45) % 60, (tick * 2 + 20) % 60, (tick * 3) % 60,
                          0.48f * fast_cos(t_sec * 1.6f), -0.52f * fast_sin(t_sec * 1.2f), t_sec * 0.9f, c_elec_cyan, c_deep_azure, false);

      const float spin_cw = static_cast<float>(now_ms % 1150U) * (2.0f * PI_F / 1150.0f);
      const Mat3 rot_cw = Mat3::from_euler(0.36f * fast_sin(t_sec * 0.9f), 0.32f * fast_cos(t_sec * 0.8f), 0.0f);
      const Mat3 rot_ccw = Mat3::from_euler(-0.34f * fast_cos(t_sec * 0.85f), 0.36f * fast_sin(t_sec * 0.95f), 0.0f);
      auto draw_orbit_comet = [&](float base_ang, float tail_step, float rad, const Mat3 &rot,
                                  UiColor c_cross, UiColor c_head, UiColor c_spec, int cross_r, int head_r, int spec_r) {
        for (int tail = 4; tail >= 0; tail--) {
          float ts = 0.0f, tc = 1.0f;
          fast_sincos(base_ang + static_cast<float>(tail) * tail_step, ts, tc);
          const ProjectedPoint p = project_3d(rot.transform({rad * tc, rad * ts, 0.0f}), cx, cy, 260.0f, 320.0f);
          if (tail == 0) {
            fast_line(it, p.x - cross_r, p.y, p.x + cross_r, p.y, c_cross);
            fast_line(it, p.x, p.y - cross_r, p.x, p.y + cross_r, c_cross);
            fast_filled_circle(it, p.x, p.y, head_r, c_head);
            fast_filled_circle(it, p.x, p.y, spec_r, c_spec);
          } else {
            fast_filled_circle(it, p.x, p.y, (tail <= 2) ? 2 : 1, scale_color(c_head, static_cast<float>(5 - tail) * 0.18f));
          }
        }
      };
      for (int i = 0; i < 4; i++) {
        draw_orbit_comet(spin_cw + static_cast<float>(i) * (PI_F * 0.5f), -0.12f, 74.0f, rot_cw, c_elec_cyan, c_elec_cyan, c_pink_spec, 5, 3, 1);
        draw_orbit_comet(-spin_cw + (PI_F * 0.25f) + static_cast<float>(i) * (PI_F * 0.5f), 0.12f, 118.0f, rot_ccw, c_amethyst, c_neon_magenta, c_pink_hi, 6, 4, 2);
      }
      render_nebula_purple_orb(scale_min(25 + static_cast<int>(4.0f * pulse)), 0.75f);
      return;
    }

    const float breath = fast_sin(static_cast<float>(now_ms % 2000U) * (2.0f * PI_F / 2000.0f));
    const int tick = static_cast<int>(now_ms / 38U);
    const float base_r = 82.0f + 5.0f * breath + 18.0f * smooth_vad;
    const float a2 = 5.5f + 10.0f * smooth_vad, a3 = 4.2f + 8.0f * smooth_vad, a5 = 2.8f + 5.5f * smooth_vad;
    draw_silk_ribbon_3d(base_r + 5.0f, a2, a3, a5, tick % 60, (tick + 20) % 60, (tick + 40) % 60,
                        0.48f * fast_sin(t_sec * 0.65f), 0.42f * fast_cos(t_sec * 0.55f), t_sec * 0.28f, c_amethyst, c_royal_purple, true);
    draw_silk_ribbon_3d(base_r, -a2, a3 * 0.9f, -a5, (tick + 15) % 60, (tick * 2) % 60, (tick + 10) % 60,
                        -0.50f * fast_cos(t_sec * 0.60f), 0.46f * fast_sin(t_sec * 0.75f), -t_sec * 0.24f, c_pink_hi, c_neon_magenta, true);
    draw_silk_ribbon_3d(base_r - 4.0f, a2 * 0.8f, -a3, a5, (tick + 35) % 60, (tick + 12) % 60, (tick * 2) % 60,
                        0.38f * fast_cos(t_sec * 0.80f), -0.42f * fast_sin(t_sec * 0.62f), t_sec * 0.32f, c_elec_cyan, c_deep_azure, true);
    draw_silk_ribbon_3d(base_r - 8.0f, a2 * 0.45f, a3 * 0.45f, a5 * 0.5f, (tick + 50) % 60, (tick + 30) % 60, tick % 60,
                        0.24f * fast_sin(t_sec * 0.95f), 0.24f * fast_cos(t_sec * 0.95f), -t_sec * 0.18f, c_pink_spec, c_neon_magenta, false);
    render_nebula_purple_orb(scale_min(25 + static_cast<int>(4.0f * breath + 7.0f * smooth_vad)), smooth_vad);
    return;
  }

  // Profile 5: "Solaris"
  const UiColor c_gold = col_solaris_gold, c_amber(255, 105, 15), c_crimson_ember(185, 35, 10);
  const UiColor c_emerald(35, 215, 115), c_warm_white(255, 248, 215);
  const float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;

  auto draw_solaris_core = [&](int core_r) {
    fast_filled_circle(it, cx, cy, core_r + 5, c_crimson_ember);
    fast_filled_circle(it, cx, cy, core_r + 1, c_amber);
    fast_filled_circle(it, cx, cy, core_r - 3, c_gold);
    fast_circle(it, cx, cy, core_r - 5, c_warm_white);
  };
  auto draw_solaris_corona = [&](float r_inner, float base_len, float amp_len, int tick, bool allow_emerald) {
    for (int i = 0; i < 60; i += 2) {
      const float wave = DIAL_SIN[(i * 3 + tick) % 60] * 0.6f + DIAL_COS[(i * 2 + tick) % 60] * 0.4f;
      const float bar_len = std::fmax(6.0f * va_scale, (base_len + amp_len * wave) * va_scale);
      const float ux = DIAL_SIN[i], uy = -DIAL_COS[i];
      const int xm = cx + static_cast<int>((r_inner + bar_len * 0.65f) * ux), ym = cy + static_cast<int>((r_inner + bar_len * 0.65f) * uy);
      fast_line(it, cx + static_cast<int>(r_inner * ux), cy + static_cast<int>(r_inner * uy), xm, ym, c_amber);
      fast_line(it, xm, ym, cx + static_cast<int>((r_inner + bar_len) * ux), cy + static_cast<int>((r_inner + bar_len) * uy),
                (allow_emerald && (i % 4 == 0)) ? c_emerald : c_gold);
    }
  };

  if (is_speaking) {
    const float syl_env = std::fmax(0.15f, (0.55f + 0.25f * fast_sin(t_sec * 11.5f) + 0.20f * fast_cos(t_sec * 19.3f)) * smooth_speak);
    const float breath = fast_sin(static_cast<float>(now_ms % 1800U) * (2.0f * PI_F / 1800.0f));
    const uint32_t speak_elapsed = now_ms - speak_start_ms;
    for (int w = 0; w < 3; w++) {
      const uint32_t launch_offset = static_cast<uint32_t>(w) * 600U;
      if (speak_elapsed < launch_offset) continue;
      const float prog = static_cast<float>((speak_elapsed - launch_offset) % 1800U) * (1.0f / 1800.0f);
      const float fade = fast_sin(prog * PI_F) * (0.45f + 0.45f * syl_env) * smooth_speak;
      const int r_wave = scale_min(82 + static_cast<int>(prog * 132.0f));
      const UiColor ring_c = scale_color(get_va_radiation_color(true, prog + static_cast<float>(w) * 0.33f), fade);
      const UiColor glow_c = scale_color(ring_c, 0.45f);
      fast_circle(it, cx, cy, r_wave - 1, glow_c);
      fast_circle(it, cx, cy, r_wave, ring_c);
      fast_circle(it, cx, cy, r_wave + 1, glow_c);
    }
    draw_solaris_corona((42.0f + 3.0f * breath) * va_scale, 16.0f + 22.0f * syl_env, 6.0f + 10.0f * syl_env, static_cast<int>(now_ms / 35U), true);
    draw_solaris_core(scale_min(27 + static_cast<int>(4.0f * breath + 5.0f * syl_env)));
    return;
  }

  if (is_thinking) {
    const int head_1 = static_cast<int>((now_ms / 18U) % 60U);
    const int head_2 = (60 - static_cast<int>((now_ms / 22U) % 60U)) % 60;
    const int head_3 = static_cast<int>((now_ms / 28U) % 60U);
    const Mat3 rot1 = Mat3::from_euler(0.62f * fast_sin(t_sec * 1.1f), 0.52f * fast_cos(t_sec * 0.9f), 0.0f);
    const Mat3 rot2 = Mat3::from_euler(-0.58f * fast_cos(t_sec * 0.95f), 0.60f * fast_sin(t_sec * 1.15f), 0.0f);
    const Mat3 rot3 = Mat3::from_euler(0.42f * fast_cos(t_sec * 0.75f), -0.48f * fast_sin(t_sec * 0.85f), 0.0f);
    auto draw_3d_meridian = [&](float rad, const Mat3 &rot, UiColor col) {
      int px = 0, py = 0, fx = 0, fy = 0;
      for (int i = 0; i < 60; i += 3) {
        const ProjectedPoint p = project_3d(rot.transform({rad * DIAL_SIN[i], -rad * DIAL_COS[i], 0.0f}), cx, cy, 260.0f, 320.0f);
        if (i == 0) { fx = p.x; fy = p.y; } else fast_line(it, px, py, p.x, p.y, col);
        px = p.x; py = p.y;
      }
      fast_line(it, px, py, fx, fy, col);
    };
    draw_3d_meridian(66.0f, rot1, scale_color(c_amber, 0.34f * smooth_think));
    draw_3d_meridian(94.0f, rot2, scale_color(c_gold, 0.30f * smooth_think));
    draw_3d_meridian(122.0f, rot3, scale_color(c_emerald, 0.30f * smooth_think));

    auto draw_meridian_beads = [&](int base_idx, int dir_step, int max_tail, float rad, const Mat3 &rot,
                                   UiColor c_hd, UiColor c_tl, float fade_k, int r0, int r_far) {
      for (int tail = max_tail; tail >= 0; tail--) {
        const int idx = (base_idx + dir_step * tail * 2 + 60) % 60;
        const ProjectedPoint p = project_3d(rot.transform({rad * DIAL_SIN[idx], -rad * DIAL_COS[idx], 0.0f}), cx, cy, 260.0f, 320.0f);
        fast_filled_circle(it, p.x, p.y, (tail == 0) ? r0 : (tail <= 2 ? 2 : r_far),
                           (tail == 0) ? scale_color(c_hd, smooth_think)
                                       : scale_color(c_tl, static_cast<float>(max_tail + 1 - tail) * fade_k * smooth_think));
      }
    };
    for (int n = 0; n < 3; n++) {
      draw_meridian_beads((head_1 + n * 20) % 60, -1, 5, 66.0f, rot1, c_warm_white, c_gold, 0.16f, 4, 1);
      draw_meridian_beads((head_2 + n * 20 + 10) % 60, 1, 5, 94.0f, rot2, c_warm_white, c_emerald, 0.16f, 4, 1);
      draw_meridian_beads((head_3 + n * 20 + 5) % 60, -1, 4, 122.0f, rot3, c_gold, c_amber, 0.18f, 3, 2);
    }
    draw_solaris_core(scale_min(25 + static_cast<int>(4.0f * fast_sin(static_cast<float>(now_ms % 600U) * (2.0f * PI_F / 600.0f)))));
    return;
  }

  const float breath = fast_sin(static_cast<float>(now_ms % 1800U) * (2.0f * PI_F / 1800.0f));
  draw_solaris_corona((42.0f + 3.0f * breath) * va_scale, 16.0f + 6.0f * breath + 24.0f * smooth_vad,
                      6.0f + 10.0f * smooth_vad, static_cast<int>(now_ms / 45U), smooth_vad > 0.25f);
  draw_solaris_core(scale_min(27 + static_cast<int>(4.0f * breath + 7.0f * smooth_vad)));
}

}  // namespace latter_ui
