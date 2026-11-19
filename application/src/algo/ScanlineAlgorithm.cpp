#include "algo/ScanlineAlgorithm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace painterbot {
namespace {

using Channels = std::array<double, 3>;

void CheckCancellation(std::stop_token stop)
{
    if (stop.stop_requested()) {
        throw std::runtime_error("Preparation canceled.");
    }
}

Channels ToChannels(Rgb color)
{
    return {double(color.red), double(color.green), double(color.blue)};
}

Rgb ToRgb(const Channels& color)
{
    return {static_cast<std::uint8_t>(std::lround(color[0])),
            static_cast<std::uint8_t>(std::lround(color[1])),
            static_cast<std::uint8_t>(std::lround(color[2]))};
}

Channels Average(const Raster& image, double left, double top, double right, double bottom)
{
    Channels sum{};
    const int lastX = std::min(image.width, static_cast<int>(std::ceil(right)));
    const int lastY = std::min(image.height, static_cast<int>(std::ceil(bottom)));
    for (int y = static_cast<int>(top); y < lastY; ++y) {
        const double height = std::min(bottom, y + 1.0) - std::max(top, double(y));
        for (int x = static_cast<int>(left); x < lastX; ++x) {
            const double width = std::min(right, x + 1.0) - std::max(left, double(x));
            const auto pixel = ToChannels(image.pixels[static_cast<std::size_t>(y) * image.width + x]);
            for (int channel = 0; channel < 3; ++channel) {
                sum[channel] += pixel[channel] * width * height;
            }
        }
    }
    for (auto& channel : sum) {
        channel = std::clamp(channel / ((right - left) * (bottom - top)), 0.0, 255.0);
    }
    return sum;
}

std::vector<Channels> SampleImage(const Raster& image, int columns, int rows, std::stop_token stop)
{
    std::vector<Channels> samples;
    samples.reserve(columns * rows);
    const double stepX = double(image.width) / columns;
    const double stepY = double(image.height) / rows;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            CheckCancellation(stop);
            samples.push_back(Average(image, x * stepX, y * stepY, (x + 1) * stepX, (y + 1) * stepY));
        }
    }
    return samples;
}

double Distance(const Channels& a, const Channels& b)
{
    const double red = a[0] - b[0];
    const double green = a[1] - b[1];
    const double blue = a[2] - b[2];
    return 0.30 * red * red + 0.59 * green * green + 0.11 * blue * blue;
}

std::size_t Nearest(const std::vector<Channels>& centers, const Channels& sample)
{
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < centers.size(); ++i) {
        const double distance = Distance(centers[i], sample);
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

std::vector<Channels> SeedCenters(const std::vector<Channels>& samples, int colorLimit)
{
    Channels mean{};
    for (const auto& pixel : samples) {
        for (int channel = 0; channel < 3; ++channel) {
            mean[channel] += pixel[channel] / samples.size();
        }
    }
    const auto first = std::min_element(samples.begin(), samples.end(), [&mean](const auto& a, const auto& b) {
        return Distance(a, mean) < Distance(b, mean);
    });
    std::vector<Channels> centers{*first};
    std::vector<double> nearest(samples.size(), std::numeric_limits<double>::infinity());
    while (centers.size() < static_cast<std::size_t>(colorLimit)) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            nearest[i] = std::min(nearest[i], Distance(samples[i], centers.back()));
        }
        const auto farthest = std::max_element(nearest.begin(), nearest.end());
        if (*farthest < 1.0) {
            break;
        }
        centers.push_back(samples[farthest - nearest.begin()]);
    }
    return centers;
}

std::vector<Rgb> Quantize(const std::vector<Channels>& samples, int colorLimit, std::stop_token stop)
{
    auto centers = SeedCenters(samples, colorLimit);
    for (int iteration = 0; iteration < 10; ++iteration) {
        CheckCancellation(stop);
        std::vector<Channels> sums(centers.size());
        std::vector<std::size_t> counts(centers.size());
        for (const auto& sample : samples) {
            const auto index = Nearest(centers, sample);
            ++counts[index];
            for (int channel = 0; channel < 3; ++channel) {
                sums[index][channel] += sample[channel];
            }
        }
        bool converged = true;
        for (std::size_t i = 0; i < centers.size(); ++i) {
            if (counts[i] == 0) {
                continue;
            }
            for (auto& channel : sums[i]) {
                channel /= counts[i];
            }
            converged &= Distance(centers[i], sums[i]) < 0.01;
            centers[i] = sums[i];
        }
        if (converged) {
            break;
        }
    }

    std::vector<Rgb> colors;
    for (const auto& center : centers) {
        const auto color = ToRgb(center);
        if (std::find(colors.begin(), colors.end(), color) == colors.end()) {
            colors.push_back(color);
        }
    }
    return colors;
}

PaintMix ApproximateMix(Rgb color)
{
    const double high = std::max({color.red, color.green, color.blue});
    const double low = std::min({color.red, color.green, color.blue});
    const double cyan = high - color.red;
    const double magenta = high - color.green;
    const double yellow = high - color.blue;
    const double black = 255.0 - high;
    const double total = cyan + magenta + yellow + black + low;
    return {cyan / total, magenta / total, yellow / total, black / total, low / total};
}

std::vector<int> AssignColors(PaintPlan& plan, const std::vector<Channels>& samples, std::stop_token stop)
{
    const auto colors = Quantize(samples, plan.settings.paletteColors, stop);
    std::vector<Channels> centers;
    std::transform(colors.begin(), colors.end(), std::back_inserter(centers), ToChannels);
    std::vector<int> paletteIndex(colors.size(), -1);
    std::vector<int> cellColors;
    cellColors.reserve(samples.size());
    plan.quantized.pixels.reserve(samples.size());
    for (const auto& sample : samples) {
        const auto index = Nearest(centers, sample);
        if (paletteIndex[index] < 0) {
            paletteIndex[index] = static_cast<int>(plan.palette.size());
            plan.palette.push_back({colors[index], ApproximateMix(colors[index])});
        }
        cellColors.push_back(paletteIndex[index]);
        plan.quantized.pixels.push_back(colors[index]);
    }
    return cellColors;
}

struct Run {
    int row;
    int start;
    int end;
};

struct Region {
    int paletteIndex;
    std::size_t cellCount;
    bool touchesBorder;
    std::vector<Run> runs;
};

Region FillRegion(int seed, int regionId, const std::vector<int>& colors, std::vector<int>& regionIds,
                  int columns, int rows, std::vector<int>& queue)
{
    Region region{colors[seed], 0, false, {}};
    queue.clear();
    queue.push_back(seed);
    regionIds[seed] = regionId;
    const auto visit = [&](int cell) {
        if (regionIds[cell] < 0 && colors[cell] == region.paletteIndex) {
            regionIds[cell] = regionId;
            queue.push_back(cell);
        }
    };
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const int cell = queue[i];
        const int x = cell % columns;
        const int y = cell / columns;
        region.touchesBorder |= x == 0 || x == columns - 1 || y == 0 || y == rows - 1;
        if (x > 0) {
            visit(cell - 1);
        }
        if (x + 1 < columns) {
            visit(cell + 1);
        }
        if (y > 0) {
            visit(cell - columns);
        }
        if (y + 1 < rows) {
            visit(cell + columns);
        }
    }
    region.cellCount = queue.size();
    return region;
}

std::vector<Region> SegmentImage(const std::vector<int>& colors, int columns, int rows, std::stop_token stop)
{
    std::vector<int> regionIds(colors.size(), -1);
    std::vector<int> queue;
    std::vector<Region> regions;
    for (int cell = 0; cell < static_cast<int>(colors.size()); ++cell) {
        if (regionIds[cell] < 0) {
            CheckCancellation(stop);
            regions.push_back(FillRegion(cell, static_cast<int>(regions.size()), colors, regionIds,
                                         columns, rows, queue));
        }
    }
    for (int y = 0; y < rows; ++y) {
        const int direction = y % 2 == 0 ? 1 : -1;
        const int pastEnd = direction == 1 ? columns : -1;
        int x = direction == 1 ? 0 : columns - 1;
        while (x != pastEnd) {
            const int start = x;
            const int regionId = regionIds[y * columns + x];
            do {
                x += direction;
            } while (x != pastEnd && regionIds[y * columns + x] == regionId);
            regions[regionId].runs.push_back({y, start, x - direction});
        }
    }
    std::stable_sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) {
        if (a.cellCount != b.cellCount) {
            return a.cellCount > b.cellCount;
        }
        return a.touchesBorder && !b.touchesBorder;
    });
    return regions;
}

void AddStrokes(PaintPlan& plan, const std::vector<Region>& regions)
{
    plan.strokeAtCell.resize(plan.quantized.pixels.size());
    for (const auto& region : regions) {
        plan.segments.push_back({plan.strokes.size(), region.runs.size(), region.cellCount});
        for (const auto& run : region.runs) {
            const auto point = [&](int column) {
                return PointMm{plan.imageOrigin.x + (column + 0.5) * BrushWidthMm,
                               plan.imageOrigin.y + (run.row + 0.5) * BrushWidthMm};
            };
            plan.strokes.push_back({point(run.start), point(run.end), static_cast<std::size_t>(region.paletteIndex)});
            const int row = run.row * plan.quantized.width;
            const int left = std::min(run.start, run.end);
            const int right = std::max(run.start, run.end);
            std::fill(plan.strokeAtCell.begin() + row + left, plan.strokeAtCell.begin() + row + right + 1,
                      static_cast<std::uint32_t>(plan.strokes.size()));
        }
    }
}

}  // namespace

PaintPlan ScanlineAlgorithm::Prepare(const Raster& image, const PaintSettings& settings, std::stop_token stop) const
{
    Validate(settings);
    if (!image.IsValid()) {
        throw std::invalid_argument("A valid reference image is required.");
    }
    CheckCancellation(stop);
    const int maxColumns = settings.widthMm / BrushWidthMm;
    const int maxRows = settings.heightMm / BrushWidthMm;
    const double scale = std::min(double(maxColumns) / image.width, double(maxRows) / image.height);
    const int columns = std::clamp(static_cast<int>(std::lround(image.width * scale)), 1, maxColumns);
    const int rows = std::clamp(static_cast<int>(std::lround(image.height * scale)), 1, maxRows);

    PaintPlan plan;
    plan.settings = settings;
    plan.imageOrigin = {(settings.widthMm - columns * BrushWidthMm) / 2.0,
                        (settings.heightMm - rows * BrushWidthMm) / 2.0};
    plan.quantized.width = columns;
    plan.quantized.height = rows;
    const auto samples = SampleImage(image, columns, rows, stop);
    const auto colors = AssignColors(plan, samples, stop);
    AddStrokes(plan, SegmentImage(colors, columns, rows, stop));
    CheckCancellation(stop);
    return plan;
}

}  // namespace painterbot
