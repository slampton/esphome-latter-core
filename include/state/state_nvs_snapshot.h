#pragma once

// ==============================================================================
// Latter Core: NVS Preference Handles & Hibernate Cold-Boot Snapshot
// (include/state/state_nvs_snapshot.h)
// ==============================================================================

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#ifdef USE_ESP32
#if __has_include("esphome/core/preferences.h")
#include "esphome/core/preferences.h"
#define LATTER_HAS_ESP_PREFS 1
#endif
#endif

#ifndef LATTER_HAS_ESP_PREFS
#define LATTER_HAS_ESP_PREFS 0
#endif

#include "state/device_runtime_state.h"

namespace latter_core {

// ==============================================================================
// Hibernate Snapshot POD & Cold-Boot Hydration (NVS-backed Frame #1 Seed)
// ==============================================================================
static constexpr uint32_t kHibernateSnapshotMagic   = 0x4C485350U;  // "LHSP"
static constexpr uint16_t kHibernateSnapshotVersion = 3U;
static constexpr uint32_t kHibernateSnapshotHash    = 0x4C484942U;  // "LHIB"
static constexpr size_t kHibernateDockSlots         = 10U;
static constexpr size_t kHibernatePinnedPage1Slots  = 12U;

struct DockSnapshotSlot {
  uint8_t kind = 0;         // 0 = empty, 1 = ROUTE, 2 = ENTITY
  uint8_t confirm = 0;      // 0 = direct, 1 = confirm
  uint8_t route_id = 0;     // RouteId when kind == 1
  int8_t state = -1;        // -1 = neutral/pending (live entity state arrives via HA shortcuts)
  int8_t col = -1;          // Resolved page-0 column (0..3) or -1 = auto-flow
  int8_t row = -1;          // Resolved page-0 row (0..2) or -1 = auto-flow
  uint8_t col_span = 1;     // Column span (1..4)
  uint8_t row_span = 1;     // Row span (1..3)
  char icon_slug[24]{};     // MDI slug without "mdi:" prefix (or empty for default route glyph)
  char label[22]{};         // Display label
  char target[38]{};        // Entity ID when kind == 2 (route token resolved via route_id)
  char action[20]{};        // Explicit HA service action when kind == 2
};

struct HibernateSnapshot {
  uint32_t magic = kHibernateSnapshotMagic;
  uint16_t version = kHibernateSnapshotVersion;
  bool wallpaper_is_day_night_sky = false;
  bool dock_configured = false;
  uint16_t dock_count = 0;
  uint16_t pinned_total = 0;
  DockSnapshotSlot dock_slots[kHibernateDockSlots]{};
  DockSnapshotSlot pinned_page1[kHibernatePinnedPage1Slots]{};
  char pinned_page0_title[28]{};
  char area_name[48]{};
  char area_icon[32]{};
  char posix_tz[48]{};
};

static_assert(std::is_trivially_copyable_v<HibernateSnapshot>,
              "HibernateSnapshot must remain a POD blob for ESPPreferenceObject.");

inline void pack_snapshot_shortcut_slot(const ShortcutItem &src,
                                        DockSnapshotSlot &dst,
                                        int8_t resolved_col = -1,
                                        int8_t resolved_row = -1,
                                        uint8_t resolved_col_span = 1,
                                        uint8_t resolved_row_span = 1) {
  dst = DockSnapshotSlot{};
  dst.kind = (src.kind == ShortcutKind::ENTITY) ? 2U : 1U;
  const uint8_t confirm_bit = (src.kind == ShortcutKind::ENTITY && src.confirm) ? 0x01U : 0x00U;
  const uint8_t inv_bit = (src.badge_style == ShortcutBadgeStyle::INVERTED) ? 0x02U : 0x00U;
  const uint8_t acc_bits = static_cast<uint8_t>((static_cast<uint8_t>(src.accent) & 0x07U) << 4);
  dst.confirm = static_cast<uint8_t>(confirm_bit | inv_bit | acc_bits);
  dst.route_id = static_cast<uint8_t>(src.route_id);
  dst.state = -1;  // Never persist volatile live entity states across cold boots (D-085)
  dst.col = (resolved_col >= 0 && resolved_col < kTileGridMaxCols &&
             resolved_row >= 0 && resolved_row < kTileGridMaxRows)
                ? resolved_col
                : static_cast<int8_t>(-1);
  dst.row = (dst.col >= 0) ? resolved_row : static_cast<int8_t>(-1);
  dst.col_span = static_cast<uint8_t>(std::clamp(static_cast<int>(resolved_col_span), 1, kTileGridMaxCols));
  dst.row_span = static_cast<uint8_t>(std::clamp(static_cast<int>(resolved_row_span), 1, kTileGridMaxRows));
  std::string_view slug(src.icon);
  if (slug.rfind("mdi:", 0) == 0) slug.remove_prefix(4);
  copy_bounded_cstr(dst.icon_slug, slug);
  copy_bounded_cstr(dst.label, src.label);
  if (src.kind == ShortcutKind::ENTITY) {
    copy_bounded_cstr(dst.target, src.target);
    copy_bounded_cstr(dst.action, src.action);
  }
}

inline bool unpack_snapshot_shortcut_slot(const DockSnapshotSlot &src, ShortcutItem &dst) {
  if (src.kind != 1U && src.kind != 2U) return false;
  dst = ShortcutItem{};
  const ShortcutBadgeStyle unpacked_badge =
      ((src.confirm & 0x02U) != 0U) ? ShortcutBadgeStyle::INVERTED : ShortcutBadgeStyle::GLASS;
  const uint8_t raw_acc = static_cast<uint8_t>((src.confirm >> 4) & 0x07U);
  const ShortcutAccent unpacked_accent =
      (raw_acc <= static_cast<uint8_t>(ShortcutAccent::ORANGE))
          ? static_cast<ShortcutAccent>(raw_acc)
          : ShortcutAccent::DEFAULT;

  const char *icon_end = static_cast<const char *>(std::memchr(src.icon_slug, '\0', sizeof(src.icon_slug)));
  const size_t icon_len = (icon_end != nullptr) ? static_cast<size_t>(icon_end - src.icon_slug) : (sizeof(src.icon_slug) - 1U);
  const std::string_view slug_sv(src.icon_slug, icon_len);

  const char *lbl_end = static_cast<const char *>(std::memchr(src.label, '\0', sizeof(src.label)));
  const size_t lbl_len = (lbl_end != nullptr) ? static_cast<size_t>(lbl_end - src.label) : (sizeof(src.label) - 1U);
  const std::string_view lbl_sv(src.label, lbl_len);

  const bool has_placed_pos =
      (src.col >= 0 && src.col < kTileGridMaxCols && src.row >= 0 && src.row < kTileGridMaxRows);
  const int8_t unpacked_page = has_placed_pos ? static_cast<int8_t>(0) : static_cast<int8_t>(-1);
  const int8_t unpacked_col = has_placed_pos ? src.col : static_cast<int8_t>(-1);
  const int8_t unpacked_row = has_placed_pos ? src.row : static_cast<int8_t>(-1);
  const uint8_t unpacked_col_span =
      static_cast<uint8_t>(std::clamp(static_cast<int>(src.col_span), 1, kTileGridMaxCols));
  const uint8_t unpacked_row_span =
      static_cast<uint8_t>(std::clamp(static_cast<int>(src.row_span), 1, kTileGridMaxRows));

  if (src.kind == 1U) {
    const auto rid = static_cast<RouteId>(src.route_id);
    if (rid == RouteId::SETTINGS_ROOT) {
      dst.kind = ShortcutKind::ROUTE;
      dst.route_id = RouteId::SETTINGS_ROOT;
      dst.target = "app:settings";
      dst.label = lbl_sv.empty() ? "Settings" : std::string(lbl_sv);
    } else {
      const auto *spec = shortcut_route_spec(rid);
      if (spec == nullptr) return false;
      dst.kind = ShortcutKind::ROUTE;
      dst.route_id = rid;
      dst.target = spec->token;
      dst.label = lbl_sv.empty() ? std::string(spec->label) : std::string(lbl_sv);
    }
    dst.icon = slug_sv.empty() ? "" : ("mdi:" + std::string(slug_sv));
    dst.confirm = false;
    dst.state = -1;
    dst.badge_style = unpacked_badge;
    dst.accent = unpacked_accent;
    dst.page = unpacked_page;
    dst.col = unpacked_col;
    dst.row = unpacked_row;
    dst.col_span = unpacked_col_span;
    dst.row_span = unpacked_row_span;
    return true;
  }

  const char *tgt_end = static_cast<const char *>(std::memchr(src.target, '\0', sizeof(src.target)));
  const size_t tgt_len = (tgt_end != nullptr) ? static_cast<size_t>(tgt_end - src.target) : (sizeof(src.target) - 1U);
  const std::string_view tgt_sv(src.target, tgt_len);

  const char *act_end = static_cast<const char *>(std::memchr(src.action, '\0', sizeof(src.action)));
  const size_t act_len = (act_end != nullptr) ? static_cast<size_t>(act_end - src.action) : (sizeof(src.action) - 1U);
  const std::string_view act_sv(src.action, act_len);

  if (tgt_sv.empty() || act_sv.empty()) return false;
  dst.kind = ShortcutKind::ENTITY;
  dst.route_id = RouteId::NONE;
  dst.target = std::string(tgt_sv);
  dst.action = std::string(act_sv);
  dst.label = lbl_sv.empty() ? std::string(tgt_sv) : std::string(lbl_sv);
  dst.icon = slug_sv.empty() ? "" : ("mdi:" + std::string(slug_sv));
  dst.confirm = ((src.confirm & 0x01U) != 0U);
  dst.state = -1;  // Neutral/pending until live HA shortcuts arrive (D-085)
  dst.badge_style = unpacked_badge;
  dst.accent = unpacked_accent;
  dst.page = unpacked_page;
  dst.col = unpacked_col;
  dst.row = unpacked_row;
  dst.col_span = unpacked_col_span;
  dst.row_span = unpacked_row_span;
  return true;
}

#if LATTER_HAS_ESP_PREFS
inline esphome::ESPPreferenceObject &hibernate_snapshot_handle() {
  static esphome::ESPPreferenceObject pref;
  static bool inited = false;
  if (!inited && esphome::global_preferences != nullptr) {
    pref = esphome::global_preferences->make_preference<HibernateSnapshot>(kHibernateSnapshotHash);
    inited = true;
  }
  return pref;
}
#else
struct HostHibernateSnapshotStore {
  bool has_saved = false;
  HibernateSnapshot blob{};
};

inline HostHibernateSnapshotStore &host_hibernate_snapshot_store() {
  static HostHibernateSnapshotStore st{};
  return st;
}
#endif

inline std::string &hydrated_posix_tz() {
  static std::string tz;
  return tz;
}

#if !LATTER_HAS_ESP_PREFS
inline void reset_hibernate_snapshot_for_test() {
  host_hibernate_snapshot_store() = HostHibernateSnapshotStore{};
  hydrated_posix_tz().clear();
}
#endif

#if LATTER_HAS_ESP_PREFS
inline void reset_hibernate_snapshot_for_test() {
  hydrated_posix_tz().clear();
}
#endif

inline bool save_hibernate_snapshot(const char *tz_override = nullptr) {
  auto snap_ptr = std::make_unique<HibernateSnapshot>();
  HibernateSnapshot &snap = *snap_ptr;
  snap.magic = kHibernateSnapshotMagic;
  snap.version = kHibernateSnapshotVersion;
  const auto &ac = area_ctx_state();
  const auto &sc = shortcuts_state();

  snap.wallpaper_is_day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
  snap.dock_configured = sc.dock_configured;
  const size_t dcnt = std::min<size_t>(sc.dock_items.size(), kHibernateDockSlots);
  snap.dock_count = static_cast<uint16_t>(dcnt);
  for (size_t i = 0; i < dcnt; ++i) {
    pack_snapshot_shortcut_slot(sc.dock_items[i], snap.dock_slots[i]);
  }
  snap.pinned_total = sc.live_received
                          ? static_cast<uint16_t>(std::min<size_t>(sc.pinned_items.size(), ShortcutsState::kMaxPinnedItems))
                          : sc.cold_pinned_total;
  if (!sc.pinned_items.empty()) {
    const int grid_cols = tile_grid_cols_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    const int grid_rows = tile_grid_rows_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    TileGridPlacement *placements = pinned_grid_placements_scratch();
    int8_t page_sources[kTileGridMaxLogicalPages]{};
    const auto layout_res = resolve_pinned_grid_layout(
        sc, grid_cols, grid_rows, placements, static_cast<int>(ShortcutsState::kMaxPinnedItems),
        page_sources, kTileGridMaxLogicalPages);
    size_t packed = 0U;
    for (int i = 0; i < layout_res.placement_count && packed < kHibernatePinnedPage1Slots; ++i) {
      const auto &pl = placements[static_cast<size_t>(i)];
      if (pl.visual_page != 0) continue;
      if (pl.item_index < 0 || static_cast<size_t>(pl.item_index) >= sc.pinned_items.size()) continue;
      pack_snapshot_shortcut_slot(sc.pinned_items[static_cast<size_t>(pl.item_index)],
                                  snap.pinned_page1[packed++],
                                  pl.col, pl.row, pl.col_span, pl.row_span);
    }
    const std::string_view pg0_title = resolve_pinned_page_title(sc, static_cast<int>(page_sources[0]));
    if (!pg0_title.empty()) {
      copy_bounded_cstr(snap.pinned_page0_title, pg0_title);
    }
  } else {
    const std::string_view pg0_title = resolve_pinned_page_title(sc, 0);
    if (!pg0_title.empty()) {
      copy_bounded_cstr(snap.pinned_page0_title, pg0_title);
    }
  }

  copy_bounded_cstr(snap.area_name, ac.area_name);
  copy_bounded_cstr(snap.area_icon, ac.area_icon);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char *tz_env = (tz_override != nullptr) ? tz_override : std::getenv("TZ");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  if (tz_env != nullptr && tz_env[0] != '\0') {
    copy_bounded_cstr(snap.posix_tz, tz_env);
  }
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  const bool ok = hibernate_snapshot_handle().save(&snap);
  esphome::global_preferences->sync();
  return ok;
#else
  auto &store = host_hibernate_snapshot_store();
  store.blob = snap;
  store.has_saved = true;
  return true;
#endif
}

inline bool load_hibernate_snapshot_if_cold() {
  auto snap_ptr = std::make_unique<HibernateSnapshot>();
  HibernateSnapshot &snap = *snap_ptr;
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  if (!hibernate_snapshot_handle().load(&snap)) return false;
#else
  const auto &store = host_hibernate_snapshot_store();
  if (!store.has_saved) return false;
  snap = store.blob;
#endif
  if (snap.magic != kHibernateSnapshotMagic || snap.version != kHibernateSnapshotVersion) {
    return false;
  }
  snap.pinned_page0_title[sizeof(snap.pinned_page0_title) - 1] = '\0';
  snap.area_name[sizeof(snap.area_name) - 1] = '\0';
  snap.area_icon[sizeof(snap.area_icon) - 1] = '\0';
  snap.posix_tz[sizeof(snap.posix_tz) - 1] = '\0';

  if (snap.posix_tz[0] != '\0') {
    hydrated_posix_tz() = snap.posix_tz;
#if defined(ESP_PLATFORM) && !defined(LATTER_HOST_TEST)
    setenv("TZ", snap.posix_tz, 1);
    tzset();
#endif
  }

  auto &sc = shortcuts_state();
  if (!sc.live_received && sc.epoch == 0U) {
    sc.cold_pinned_total = snap.pinned_total;
    if (snap.dock_configured && sc.dock_items.empty()) {
      sc.dock_configured = true;
      const size_t dcnt = std::min<size_t>(snap.dock_count, kHibernateDockSlots);
      for (size_t i = 0; i < dcnt; ++i) {
        ShortcutItem item{};
        if (unpack_snapshot_shortcut_slot(snap.dock_slots[i], item)) {
          sc.dock_items.push_back(std::move(item));
        }
      }
    }
    if (sc.pinned_items.empty() && snap.pinned_total > 0U) {
      for (size_t i = 0; i < kHibernatePinnedPage1Slots; ++i) {
        ShortcutItem item{};
        if (unpack_snapshot_shortcut_slot(snap.pinned_page1[i], item)) {
          sc.pinned_items.push_back(std::move(item));
        }
      }
    }
    if (sc.pinned_page_titles.empty() && snap.pinned_page0_title[0] != '\0') {
      sc.pinned_page_titles.push_back(PinnedPageTitle{0U, std::string(snap.pinned_page0_title)});
    }
  }

  auto &ac = area_ctx_state();
  if (ac.area_name.empty() && snap.area_name[0] != '\0') {
    ac.area_name = snap.area_name;
    if (runtime_state().area.active_area_name.empty() ||
        runtime_state().area.active_area_name == "Area Control") {
      runtime_state().area.active_area_name = snap.area_name;
    }
  }
  if (ac.area_icon.empty() && snap.area_icon[0] != '\0') {
    ac.area_icon = snap.area_icon;
  }
  if (ac.epoch == 0U && ac.rev == 0U && ac.wallpaper_url.empty() && snap.wallpaper_is_day_night_sky) {
    ac.wallpaper_url = std::string(kDayNightSkyWallpaperUrl);
  }
  return true;
}


}  // namespace latter_core
