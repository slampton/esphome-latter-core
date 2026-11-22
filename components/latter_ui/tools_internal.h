#pragma once

#include "ui_pages.h"

namespace latter_ui {

void render_calculator_subpage(Rgb565Surface &it, const UiFrameContext &ctx);
void render_dice_subpage(Rgb565Surface &it, const UiFrameContext &ctx);
void render_coin_flip_subpage(Rgb565Surface &it, const UiFrameContext &ctx);

}  // namespace latter_ui
