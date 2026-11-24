#include "test_harness.h"

#include <cstring>

namespace latter_test {

namespace {

bool ends_with(std::string_view sv, std::string_view suffix) noexcept {
  return sv.size() >= suffix.size() && sv.substr(sv.size() - suffix.size()) == suffix;
}

size_t count_nonzero_pixels(const std::vector<uint16_t> &px) noexcept {
  size_t cnt = 0;
  for (uint16_t v : px) {
    if (v != 0U) ++cnt;
  }
  return cnt;
}

void test_icon_provider_contract_and_state_resolver() {
  using namespace latter_ui;
  using namespace latter_core;

  // Item 5 Foundation: Provider-agnostic dispatch seam in ui_mdi.h
  LATTER_CHECK(has_icon("mdi:lightbulb"));
  LATTER_CHECK(has_icon("mdi:lightbulb-outline"));
  LATTER_CHECK(!has_icon("mdi:definitely-not-a-real-icon-xyz"));

  int span_count = 0;
  LATTER_CHECK(rasterize_icon_fn("mdi:home", 32, 32, 24,
                                 [&](int x0, int x1, int, uint8_t a) {
                                   if (x1 > x0 && a > 0) ++span_count;
                                 }));
  LATTER_CHECK(span_count > 0);

  auto raw_emit = [](void *u, int x0, int x1, int, uint8_t a) {
    if (x1 > x0 && a > 0) ++(*static_cast<int *>(u));
  };
  int raw_spans = 0;
  LATTER_CHECK(rasterize_icon("mdi:cog", 32, 32, 24, raw_emit, &raw_spans));
  LATTER_CHECK(raw_spans > 0);
  LATTER_CHECK(!rasterize_icon("mdi:nonexistent-icon-xyz", 32, 32, 24, raw_emit, &raw_spans));

  // Item 2: resolve_state_mdi_slug() across all states and edge cases
  char buf[96]{};

  // state == 1 (on/active): upgrades "-outline" -> solid base when pair exists
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb-outline", 1, buf, sizeof(buf)) == "mdi:lightbulb");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:shield-home-outline", 1, buf, sizeof(buf)) == "mdi:shield-home");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", 1, buf, sizeof(buf)) == "mdi:lightbulb");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:fan", 1, buf, sizeof(buf)) == "mdi:fan");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:nonexistent-outline", 1, buf, sizeof(buf)) == "mdi:nonexistent-outline");

  // state == 0 (off/inactive): downgrades solid -> "-outline" when pair exists
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", 0, buf, sizeof(buf)) == "mdi:lightbulb-outline");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:shield-home", 0, buf, sizeof(buf)) == "mdi:shield-home-outline");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb-outline", 0, buf, sizeof(buf)) == "mdi:lightbulb-outline");
  // No outline pair for mdi:fan -> preserved unchanged
  LATTER_CHECK(!has_mdi_icon("mdi:fan-outline"));
  LATTER_CHECK(resolve_state_mdi_slug("mdi:fan", 0, buf, sizeof(buf)) == "mdi:fan");
  // Buffer safety: nullptr or undersized capacity falls back cleanly to original slug
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", 0, nullptr, 0U) == "mdi:lightbulb");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", 0, buf, 10U) == "mdi:lightbulb");

  // state == -1 (stateless/route) and state == 2 (unavailable): unchanged
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", -1, buf, sizeof(buf)) == "mdi:lightbulb");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb-outline", -1, buf, sizeof(buf)) == "mdi:lightbulb-outline");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb", 2, buf, sizeof(buf)) == "mdi:lightbulb");
  LATTER_CHECK(resolve_state_mdi_slug("mdi:lightbulb-outline", 2, buf, sizeof(buf)) == "mdi:lightbulb-outline");

  // Empty slug
  LATTER_CHECK(resolve_state_mdi_slug("", 0, buf, sizeof(buf)).empty());
  LATTER_CHECK(resolve_state_mdi_slug("", 1, buf, sizeof(buf)).empty());
}

void test_all_builtin_route_icons_solid() {
  using namespace latter_ui;
  using namespace latter_core;

  // Verify every canonical route in kShortcutRoutes plus SETTINGS_ROOT resolves to a valid solid icon.
  size_t route_count = 0;
  for (const auto &spec : kShortcutRoutes) {
    const std::string_view slug(ui::route_icon_slug(spec.id));
    LATTER_CHECK(!slug.empty());
    LATTER_CHECK(has_mdi_icon(slug));
    LATTER_CHECK(!ends_with(slug, "-outline"));
    ++route_count;
  }
  LATTER_CHECK(route_count >= 63U);
  {
    const std::string_view settings_slug(ui::route_icon_slug(RouteId::SETTINGS_ROOT));
    LATTER_CHECK(settings_slug == "mdi:cog");
    LATTER_CHECK(has_mdi_icon(settings_slug));
  }

  // Verify exact Studio ROUTE_PREVIEW_ICONS 1:1 solid parity for all unified routes.
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::MUSIC_LIBRARY)) == "mdi:music-box");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::MUSIC_INFO)) == "mdi:information");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::CAMERAS_SETTINGS)) == "mdi:cog");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::APP_TIME)) == "mdi:clock");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::APP_CLOCK)) == "mdi:clock");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::TIME_CLOCK)) == "mdi:clock");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::TIME_STOPWATCH)) == "mdi:timer");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::TOOLS_STOPWATCH)) == "mdi:timer");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::APP_TOOLS)) == "mdi:view-grid");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::SETTINGS_MUSIC)) == "mdi:music-box");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::SETTINGS_HOME)) == "mdi:home");
  LATTER_CHECK(std::string_view(ui::route_icon_slug(RouteId::SETTINGS_INFO)) == "mdi:information");

  // Verify solid fallback icons exist in the baked catalog.
  LATTER_CHECK(has_mdi_icon("mdi:map-marker"));
  LATTER_CHECK(has_mdi_icon("mdi:compass"));
  LATTER_CHECK(has_mdi_icon("mdi:clock"));
  LATTER_CHECK(has_mdi_icon("mdi:timer"));
  LATTER_CHECK(has_mdi_icon("mdi:cog"));
  LATTER_CHECK(has_mdi_icon("mdi:skip-next-circle"));

  // Source-level checks ensuring zero -outline regressions in ui_gfx.h / route helpers
  const std::string gfx_h = read_repo_file("../../include/ui_gfx.h");
  LATTER_CHECK(gfx_h.find("fallback_name = \"mdi:map-marker\"") != std::string::npos);
  LATTER_CHECK(gfx_h.find("mdi:map-marker-outline") == std::string::npos);
}

void test_shortcut_record_parsing_and_ui_sig() {
  using namespace latter_ui;
  using namespace latter_core;

  // Legacy 7-column route & entity records default cleanly to GLASS + DEFAULT
  {
    ShortcutItem r7{};
    LATTER_CHECK(parse_shortcut_record("route|app:music||Music||0|-1", r7));
    LATTER_CHECK(r7.kind == ShortcutKind::ROUTE && r7.route_id == RouteId::APP_MUSIC);
    LATTER_CHECK(r7.badge_style == ShortcutBadgeStyle::GLASS);
    LATTER_CHECK(r7.accent == ShortcutAccent::DEFAULT);

    ShortcutItem e7{};
    LATTER_CHECK(parse_shortcut_record("entity|light.desk|toggle|Desk Lamp|mdi:lightbulb|1|1", e7));
    LATTER_CHECK(e7.kind == ShortcutKind::ENTITY && e7.confirm && e7.state == 1);
    LATTER_CHECK(e7.badge_style == ShortcutBadgeStyle::GLASS);
    LATTER_CHECK(e7.accent == ShortcutAccent::DEFAULT);
  }

  // 9-column route records across all badge_style & accent combinations
  struct AccentCase {
    const char *token;
    ShortcutAccent expected;
  };
  static constexpr AccentCase kAccents[] = {
      {"", ShortcutAccent::DEFAULT},
      {"default", ShortcutAccent::DEFAULT},
      {"primary", ShortcutAccent::PRIMARY},
      {"gold", ShortcutAccent::GOLD},
      {"green", ShortcutAccent::GREEN},
      {"red", ShortcutAccent::RED},
      {"orange", ShortcutAccent::ORANGE},
      {"GOLD", ShortcutAccent::GOLD},
      {"unknown_color", ShortcutAccent::DEFAULT},
  };
  for (const auto &tc : kAccents) {
    ShortcutItem r9{};
    const std::string rec = std::string("route|weather:moon||Moon||0|-1|inverted|") + tc.token;
    LATTER_CHECK(parse_shortcut_record(rec, r9));
    LATTER_CHECK(r9.kind == ShortcutKind::ROUTE && r9.route_id == RouteId::WEATHER_MOON);
    LATTER_CHECK(r9.badge_style == ShortcutBadgeStyle::INVERTED);
    LATTER_CHECK(r9.accent == tc.expected);

    ShortcutItem e9{};
    const std::string erec =
        std::string("entity|script.sos|turn_on|Emergency|mdi:bell-ring|1|0|glass|") + tc.token;
    LATTER_CHECK(parse_shortcut_record(erec, e9));
    LATTER_CHECK(e9.kind == ShortcutKind::ENTITY && e9.confirm && e9.state == 0);
    LATTER_CHECK(e9.badge_style == ShortcutBadgeStyle::GLASS);
    LATTER_CHECK(e9.accent == tc.expected);
  }

  // Case-insensitive inverted & entity state = 2 (unavailable)
  {
    ShortcutItem e_inv{};
    LATTER_CHECK(parse_shortcut_record(
        "entity|light.porch|toggle|Porch|mdi:lightbulb|0|2|INVERTED|orange", e_inv));
    LATTER_CHECK(e_inv.state == 2);
    LATTER_CHECK(e_inv.badge_style == ShortcutBadgeStyle::INVERTED);
    LATTER_CHECK(e_inv.accent == ShortcutAccent::ORANGE);
  }

  // compute_shortcuts_ui_sig() must change whenever badge_style or accent mutates
  {
    ShortcutsState st{};
    st.epoch = 10U;
    st.rev = 2U;
    ShortcutItem d0{};
    d0.kind = ShortcutKind::ROUTE;
    d0.route_id = RouteId::APP_MUSIC;
    d0.target = "app:music";
    d0.label = "Music";
    st.dock_items.push_back(d0);

    ShortcutItem p0{};
    p0.kind = ShortcutKind::ENTITY;
    p0.target = "light.desk";
    p0.action = "toggle";
    p0.label = "Desk";
    p0.icon = "mdi:lightbulb";
    p0.state = 1;
    st.pinned_items.push_back(p0);

    const uint32_t sig_base = compute_shortcuts_ui_sig(st);

    st.dock_items[0].badge_style = ShortcutBadgeStyle::INVERTED;
    const uint32_t sig_dock_inv = compute_shortcuts_ui_sig(st);
    LATTER_CHECK(sig_dock_inv != sig_base);

    st.dock_items[0].accent = ShortcutAccent::GOLD;
    const uint32_t sig_dock_gold = compute_shortcuts_ui_sig(st);
    LATTER_CHECK(sig_dock_gold != sig_dock_inv && sig_dock_gold != sig_base);

    st.dock_items[0].badge_style = ShortcutBadgeStyle::GLASS;
    st.dock_items[0].accent = ShortcutAccent::DEFAULT;
    LATTER_CHECK(compute_shortcuts_ui_sig(st) == sig_base);

    st.pinned_items[0].badge_style = ShortcutBadgeStyle::INVERTED;
    const uint32_t sig_pin_inv = compute_shortcuts_ui_sig(st);
    LATTER_CHECK(sig_pin_inv != sig_base);

    st.pinned_items[0].accent = ShortcutAccent::RED;
    const uint32_t sig_pin_red = compute_shortcuts_ui_sig(st);
    LATTER_CHECK(sig_pin_red != sig_pin_inv && sig_pin_red != sig_base);
  }

  // HibernateSnapshot cold-boot slot round-trip preserves badge_style & accent
  {
    ShortcutItem src_route{};
    src_route.kind = ShortcutKind::ROUTE;
    src_route.route_id = RouteId::APP_WEATHER;
    src_route.target = "app:weather";
    src_route.label = "Weather";
    src_route.badge_style = ShortcutBadgeStyle::INVERTED;
    src_route.accent = ShortcutAccent::GOLD;
    DockSnapshotSlot slot_r{};
    pack_snapshot_shortcut_slot(src_route, slot_r);
    ShortcutItem out_r{};
    LATTER_CHECK(unpack_snapshot_shortcut_slot(slot_r, out_r));
    LATTER_CHECK(out_r.badge_style == ShortcutBadgeStyle::INVERTED);
    LATTER_CHECK(out_r.accent == ShortcutAccent::GOLD);
    LATTER_CHECK(!out_r.confirm);

    ShortcutItem src_ent{};
    src_ent.kind = ShortcutKind::ENTITY;
    src_ent.target = "script.goodnight";
    src_ent.action = "turn_on";
    src_ent.label = "Goodnight";
    src_ent.icon = "mdi:weather-night";
    src_ent.confirm = true;
    src_ent.state = 1;
    src_ent.badge_style = ShortcutBadgeStyle::INVERTED;
    src_ent.accent = ShortcutAccent::ORANGE;
    DockSnapshotSlot slot_e{};
    pack_snapshot_shortcut_slot(src_ent, slot_e);
    ShortcutItem out_e{};
    LATTER_CHECK(unpack_snapshot_shortcut_slot(slot_e, out_e));
    LATTER_CHECK(out_e.confirm);
    LATTER_CHECK(out_e.badge_style == ShortcutBadgeStyle::INVERTED);
    LATTER_CHECK(out_e.accent == ShortcutAccent::ORANGE);
    LATTER_CHECK(out_e.state == 1);
  }
}

void test_outline_solid_helpers_and_5geom_shortcut_rendering() {
  using namespace latter_ui;
  using namespace latter_core;

  HostSurfaceFixture fix{};

  // 1. Verify draw_icon_lightbulb(is_on=true vs false) renders distinct solid vs outline glyphs
  {
    fix.fill(0U);
    ui::draw_icon_lightbulb(fix.surf, fix.ctx.cx, fix.ctx.cy, 20, fix.ctx.col_gold, false, true);
    const std::vector<uint16_t> px_on = fix.pixels;
    const size_t cnt_on = count_nonzero_pixels(px_on);

    fix.fill(0U);
    ui::draw_icon_lightbulb(fix.surf, fix.ctx.cx, fix.ctx.cy, 20, fix.ctx.col_gold, false, false);
    const size_t cnt_off = count_nonzero_pixels(fix.pixels);

    LATTER_CHECK(cnt_on > 0U && cnt_off > 0U);
    LATTER_CHECK(px_on != fix.pixels);
    LATTER_CHECK(cnt_on > cnt_off);  // Solid silhouette fills more pixels than outline
  }

  // 2. Verify draw_icon_shield_home(is_armed=true vs false) renders distinct solid vs outline glyphs
  {
    fix.fill(0U);
    ui::draw_icon_shield_home(fix.surf, fix.ctx.cx, fix.ctx.cy, 20, fix.ctx.col_red, false, true);
    const std::vector<uint16_t> px_armed = fix.pixels;
    const size_t cnt_armed = count_nonzero_pixels(px_armed);

    fix.fill(0U);
    ui::draw_icon_shield_home(fix.surf, fix.ctx.cx, fix.ctx.cy, 20, fix.ctx.col_red, false, false);
    const size_t cnt_disarmed = count_nonzero_pixels(fix.pixels);

    LATTER_CHECK(cnt_armed > 0U && cnt_disarmed > 0U);
    LATTER_CHECK(px_armed != fix.pixels);
    LATTER_CHECK(cnt_armed > cnt_disarmed);
  }

  // 3. Render Home Dock across all 5 geometries with GLASS, INVERTED, accents, and On/Off state feedback
  {
    reset_runtime_state();
    reset_all_v3_rev_guards();
    home_dock_anim().snap(0);
    auto &rs = runtime_state();
    auto &sc = shortcuts_state();
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = HomeSubpage::MAIN;
    rs.nav.home_dock_page = 0;

    sc = ShortcutsState{};
    sc.dock_configured = true;
    sc.live_received = true;

    ShortcutItem d0{};
    LATTER_CHECK(parse_shortcut_record("route|app:music||Music||0|-1|glass|default", d0));
    ShortcutItem d1{};
    LATTER_CHECK(parse_shortcut_record("route|app:tools||Tools||0|-1|glass|gold", d1));
    ShortcutItem d2{};
    LATTER_CHECK(parse_shortcut_record("entity|light.desk|toggle|Desk|mdi:lightbulb-outline|0|1|inverted|primary", d2));
    ShortcutItem d3{};
    LATTER_CHECK(parse_shortcut_record("entity|script.sos|turn_on|Alert|mdi:bell-ring|1|0|inverted|red", d3));
    ShortcutItem d4{};
    LATTER_CHECK(parse_shortcut_record("entity|light.porch|toggle|Porch|mdi:lightbulb|0|2|inverted|green", d4));
    sc.dock_items = {d0, d1, d2, d3, d4};

    fix.ctx.home_weather_bg = false;
    fix.ctx.home_wake_greeting = false;

    // Frame A: inverted + accents at rest
    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    const std::vector<uint16_t> home_inv_px = fix.pixels;
    LATTER_CHECK(count_nonzero_pixels(home_inv_px) > 100U);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::DOCK_SLOT_BASE + 0) != nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::DOCK_SLOT_BASE + 3) != nullptr);

    // Frame B: flip dock slot 2 from INVERTED to GLASS (state=1) -> pixels must differ
    sc.dock_items[2].badge_style = ShortcutBadgeStyle::GLASS;
    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    const std::vector<uint16_t> home_glass_on_px = fix.pixels;
    LATTER_CHECK(home_glass_on_px != home_inv_px);

    // Frame B2: flip dock slot 2 state 1 -> 0 (solid -> outline + off tint) -> pixels must differ
    sc.dock_items[2].state = 0;
    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(fix.pixels != home_glass_on_px);

    // Frame C: mid-slide dock animation (alpha < 0.99f) with inverted orb must render cleanly
    sc.dock_items[2].badge_style = ShortcutBadgeStyle::INVERTED;
    sc.dock_items[2].state = 1;
    rs.nav.home_dock_page = 1;
    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(home_dock_anim().is_animating());
    fix.ctx.now_ms += 80U;
    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(count_nonzero_pixels(fix.pixels) > 100U);
  }

  // 4. Render Pinned 2x3 Grid across all 5 geometries with GLASS, INVERTED, accents, and On/Off state
  {
    reset_runtime_state();
    auto &rs = runtime_state();
    auto &sc = shortcuts_state();
    rs.nav.gui_page = PageId::PINNED;
    rs.nav.pinned_menu_page = 0;

    sc = ShortcutsState{};
    sc.live_received = true;
    const char *const kPinRecs[6] = {
        "route|app:music||Music||0|-1|glass|default",
        "route|weather:sun||Sun||0|-1|glass|gold",
        "entity|light.lamp|toggle|Lamp On|mdi:lightbulb-outline|0|1|inverted|default",
        "entity|light.hall|toggle|Hall Off|mdi:lightbulb|0|0|inverted|primary",
        "entity|script.sos|turn_on|Emergency|mdi:bell-ring|1|-1|inverted|red",
        "entity|switch.shed|toggle|Shed|mdi:power|0|2|inverted|orange",
    };
    for (const char *rec : kPinRecs) {
      ShortcutItem item{};
      LATTER_CHECK(parse_shortcut_record(rec, item));
      sc.pinned_items.push_back(std::move(item));
    }

    fix.fill(0U);
    frame_hits().clear();
    render_pinned_page(fix.surf, fix.ctx);
    const std::vector<uint16_t> pin_inv_px = fix.pixels;
    LATTER_CHECK(count_nonzero_pixels(pin_inv_px) > 200U);
    // Slots 0..4 are interactive; slot 5 is unavailable (disabled -> no hit registered)
    for (uint16_t s = 0; s < 5; ++s) {
      LATTER_CHECK(frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::PINNED_CARD_BASE + s)) != nullptr);
    }
    LATTER_CHECK(frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::PINNED_CARD_BASE + 5)) == nullptr);

    // Switch all tiles to GLASS/DEFAULT and verify visual output changes
    for (auto &it : sc.pinned_items) {
      it.badge_style = ShortcutBadgeStyle::GLASS;
      it.accent = ShortcutAccent::DEFAULT;
    }
    fix.fill(0U);
    frame_hits().clear();
    render_pinned_page(fix.surf, fix.ctx);
    LATTER_CHECK(fix.pixels != pin_inv_px);
  }

  // 5. Render Area Control light card (On vs Off) and Home Hub Alarm (Armed vs Disarmed)
  {
    reset_runtime_state();
    auto &rs = runtime_state();
    auto &ac = area_ctx_state();
    ac.area_id = "office";
    ac.area_name = "Office";
    ac.available_domains = "light";
    rs.nav.gui_page = PageId::AREA;
    rs.nav.area_subpage = AreaSubpage::OVERVIEW;
    rs.area.domain_filter = "light";
    auto *slots = rs.area_entity_slots();
    slots[0] = BrowseSlotItem{"Desk Lamp", "light.desk", "light", true, 80, "", false};

    fix.fill(0U);
    frame_hits().clear();
    render_area_control_page(fix.surf, fix.ctx);
    const std::vector<uint16_t> area_on = fix.pixels;

    slots[0].active = false;
    slots[0].level = 0;
    fix.fill(0U);
    frame_hits().clear();
    render_area_control_page(fix.surf, fix.ctx);
    LATTER_CHECK(fix.pixels != area_on);

    ac.epoch = 1U;
    ac.home_categories = "alarm";
    ac.alarm_entity = "alarm_control_panel.home";
    ac.alarm_caps = kAlarmCapDefault;
    ac.alarm_state = "armed_away";
    rs.nav.gui_page = PageId::HOME_HUB;
    home_hub_tab() = HomeHubTab::ALARM;
    fix.fill(0U);
    frame_hits().clear();
    render_home_hub_page(fix.surf, fix.ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::ALARM_MODE_BASE + 0) != nullptr);
    const std::vector<uint16_t> hub_armed = fix.pixels;

    ac.alarm_state = "disarmed";
    fix.fill(0U);
    frame_hits().clear();
    render_home_hub_page(fix.surf, fix.ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::ALARM_MODE_BASE + 1) != nullptr);
    LATTER_CHECK(fix.pixels != hub_armed);
  }

  home_dock_anim().reset();
  reset_runtime_state();
  reset_all_v3_rev_guards();
}

}  // namespace

void run_mdi_plus_tests() {
  test_icon_provider_contract_and_state_resolver();
  test_all_builtin_route_icons_solid();
  test_shortcut_record_parsing_and_ui_sig();
  test_outline_solid_helpers_and_5geom_shortcut_rendering();
}

}  // namespace latter_test
