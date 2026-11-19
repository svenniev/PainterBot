#pragma once

#include "viewmodel/viewmodel.h"
#include <stop_token>

namespace painterbot {
struct ColorTone {
    int family;
    double lightness;
};
ColorTone Tone(Rgb color);
void CheckCanceled(std::stop_token stop);
std::vector<std::uint16_t> QuantizeSource(
    const Raster& source, int limit, std::vector<PaletteColor>& palette, std::stop_token stop);
} // namespace painterbot
