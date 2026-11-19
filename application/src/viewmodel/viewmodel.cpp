#include "viewmodel/viewmodel.h"

#include <algorithm>
#include <stdexcept>

namespace painterbot {

bool Raster::IsValid() const noexcept
{
    return width > 0 && height > 0
        && pixels.size() / width == static_cast<std::size_t>(height)
        && pixels.size() % width == 0;
}

void Validate(const PaintSettings& settings)
{
    if (settings.widthMm < BrushWidthMm || settings.widthMm > MaxWidthMm
        || settings.heightMm < BrushWidthMm || settings.heightMm > MaxHeightMm) {
        throw std::invalid_argument("Canvas width must be 4-1000 mm and height must be 4-550 mm.");
    }
    if (settings.paletteColors < MinPaletteColors || settings.paletteColors > MaxPaletteColors) {
        throw std::invalid_argument("Palette size must be 2-1024 colors.");
    }
}

Raster PaintPlan::Preview(std::size_t strokeCount) const
{
    if (strokeCount >= strokes.size()) {
        return quantized;
    }

    Raster result{quantized.width, quantized.height, std::vector<Rgb>(quantized.pixels.size())};
    constexpr Rgb blankCanvas{255, 255, 255};
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        const auto base = undercoatStrokeAtCell[i];
        if (strokeAtCell[i] <= strokeCount) {
            result.pixels[i] = quantized.pixels[i];
        } else if (base != 0 && base <= strokeCount) {
            result.pixels[i] = palette[strokes[base-1].paletteIndex].preview;
        } else {
            result.pixels[i] = blankCanvas;
        }
    }
    return result;
}

}  // namespace painterbot
