#include "algo/VectorAlgorithm.h"
#include "algo/ContourTracer.h"
#include "algo/VectorIndex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace painterbot {
namespace {

void OrderPatches(PaintPlan& plan)
{
    std::vector<ColorTone> tones;
    for (const auto& color : plan.palette) {
        tones.push_back(Tone(color.preview));
    }
    std::stable_sort(plan.segments.begin(), plan.segments.end(), [&](const Segment& a, const Segment& b) {
        // Area octaves give similar-sized shadows and highlights room to change order.
        const int layerA = std::ilogb(a.area), layerB = std::ilogb(b.area);
        if (layerA != layerB) {
            return layerA > layerB;
        }
        const auto ta = tones[a.paletteIndex], tb = tones[b.paletteIndex];
        if (ta.family != tb.family) {
            return ta.family < tb.family;
        }
        if (ta.lightness != tb.lightness) {
            return ta.lightness < tb.lightness;
        }
        return a.area > b.area;
    });
}

struct Run {
    int row, first, last;
};

void PlanBrush(PaintPlan& plan)
{
    auto& grid = plan.quantized;
    const auto& art = plan.artwork;
    const double scale = std::min(double(plan.settings.widthMm / BrushWidthMm) / art.width,
        double(plan.settings.heightMm / BrushWidthMm) / art.height);
    grid.width = std::max(1, static_cast<int>(std::lround(scale * art.width)));
    grid.height = std::max(1, static_cast<int>(std::lround(scale * art.height)));
    plan.imageOrigin = { (plan.settings.widthMm - grid.width * BrushWidthMm) / 2.0,
        (plan.settings.heightMm - grid.height * BrushWidthMm) / 2.0 };
    grid.pixels.resize(grid.width * grid.height);
    plan.strokeAtCell.resize(grid.pixels.size());
    plan.undercoatStrokeAtCell.resize(grid.pixels.size());
    std::vector<std::uint32_t> owners(grid.pixels.size());
    std::vector<std::vector<Run>> runs(plan.segments.size());
    const double mmPerPixel
        = std::min(double(plan.settings.widthMm) / art.width, double(plan.settings.heightMm) / art.height);
    const double left = (plan.settings.widthMm - art.width * mmPerPixel) / 2;
    const double top = (plan.settings.heightMm - art.height * mmPerPixel) / 2;
    for (int y = 0; y < grid.height; ++y) {
        const int step = y % 2 ? -1 : 1;
        const auto addCell = [&](std::uint32_t id, int x) {
            auto& paths = runs[id];
            if (!paths.empty() && paths.back().row == y && paths.back().last + step == x) {
                paths.back().last = x;
            } else {
                paths.push_back({ y, x, x });
            }
        };
        for (int x = step == 1 ? 0 : grid.width - 1; x >= 0 && x < grid.width; x += step) {
            const double sx = (plan.imageOrigin.x + (x + 0.5) * BrushWidthMm - left) / mmPerPixel;
            const double sy = (plan.imageOrigin.y + (y + 0.5) * BrushWidthMm - top) / mmPerPixel;
            const auto id = art.PatchAt(std::clamp(sx, 0.0, std::nextafter(double(art.width), 0.0)),
                std::clamp(sy, 0.0, std::nextafter(double(art.height), 0.0)));
            owners[y * grid.width + x] = id;
            const auto undercoat = plan.segments[id].undercoat;
            if (undercoat != Segment::NoUndercoat) {
                addCell(undercoat, x);
            }
            addCell(id, x);
            ++plan.segments[id].cellCount;
            grid.pixels[y * grid.width + x] = plan.palette[plan.segments[id].paletteIndex].preview;
        }
    }
    for (std::size_t id = 0; id < plan.segments.size(); ++id) {
        auto& patch = plan.segments[id];
        patch.firstStroke = plan.strokes.size();
        patch.strokeCount = runs[id].size();
        for (auto run : runs[id]) {
            const auto center = [&](int x) {
                return PointMm { plan.imageOrigin.x + (x + 0.5) * BrushWidthMm,
                    plan.imageOrigin.y + (run.row + 0.5) * BrushWidthMm };
            };
            plan.strokes.push_back({ center(run.first), center(run.last), patch.paletteIndex });
            for (int x = std::min(run.first, run.last); x <= std::max(run.first, run.last); ++x) {
                const int cell = run.row * grid.width + x;
                auto& stage = owners[cell] == id ? plan.strokeAtCell : plan.undercoatStrokeAtCell;
                stage[cell] = static_cast<std::uint32_t>(plan.strokes.size());
            }
        }
    }
}

} // namespace

PaintPlan VectorAlgorithm::Prepare(const Raster& image, const PaintSettings& settings, std::stop_token stop) const
{
    Validate(settings);
    if (!image.IsValid()) {
        throw std::invalid_argument("A valid reference image is required.");
    }
    if (image.pixels.size() > std::numeric_limits<int>::max()
        || std::max(image.width, image.height) > std::numeric_limits<int>::max() / 2) {
        throw std::length_error("Image exceeds the vector coordinate range.");
    }
    CheckCanceled(stop);
    PaintPlan plan;
    plan.settings = settings;
    plan.artwork.width = image.width;
    plan.artwork.height = image.height;
    {
        const auto labels = QuantizeSource(image, settings.paletteColors, plan.palette, stop);
        detail::ContourTracer(image, labels, plan, &stop).Trace();
    }
    OrderPatches(plan);
    detail::IndexFills(plan, &stop);
    detail::FindUndercoats(plan, &stop);
    PlanBrush(plan);
    CheckCanceled(stop);
    return plan;
}
} // namespace painterbot
