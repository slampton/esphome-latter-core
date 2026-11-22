#include "ui_pages.h"

namespace latter_ui {

// ==============================================================================
// Top-Level Active Page Dispatcher (All 14 PageId Routes)
// ==============================================================================

void render_active_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const int pg = runtime_state().nav.gui_page;
  if (pg != PageId::HOME) {
    home_activity_card_shown() = false;
    home_activity_rendered_rows() = -1;
  }
  switch (pg) {
    case PageId::CLOCK:
      render_clock_page(it, ctx);
      break;
    case PageId::HOME:
      render_home_page(it, ctx);
      break;
    case PageId::MUSIC:
      render_music_page(it, ctx);
      break;
    case PageId::GAMES:
      render_games_page(it, ctx);
      break;
    case PageId::SYSTEM:
      render_system_page(it, ctx);
      break;
    case PageId::WEATHER:
      render_weather_page(it, ctx);
      break;
    case PageId::AREA:
      render_area_control_page(it, ctx);
      break;
    case PageId::REMOTE:
      render_tv_remote_page(it, ctx);
      break;
    case PageId::TOOLS:
      render_tools_page(it, ctx);
      break;
    case PageId::HOME_HUB:
      render_home_hub_page(it, ctx);
      break;
    case PageId::TIME:
      render_time_page(it, ctx);
      break;
    case PageId::PINNED:
      render_pinned_page(it, ctx);
      break;
    case PageId::CALENDAR:
      render_calendar_page(it, ctx);
      break;
    case PageId::CAMERAS:
      render_cameras_page(it, ctx);
      break;
    default:
      render_home_page(it, ctx);
      break;
  }
}

}  // namespace latter_ui
