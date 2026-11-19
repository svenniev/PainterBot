#pragma once

#include <algorithm>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace painterbot {

inline constexpr int MaxWidthMm = 1000;
inline constexpr int MaxHeightMm = 550;
inline constexpr int BrushWidthMm = 4;
inline constexpr int MinPaletteColors = 2;
inline constexpr int MaxPaletteColors = 1024;

struct Rgb {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    bool operator==(const Rgb&) const = default;
};

struct Raster {
    int width{};
    int height{};
    std::vector<Rgb> pixels;

    [[nodiscard]] bool IsValid() const noexcept;
};

struct ReferenceImage {
    std::filesystem::path path;
    Raster source;
    Raster thumbnail;
};

struct PaintSettings {
    int widthMm{600};
    int heightMm{350};
    int paletteColors{64};
    bool operator==(const PaintSettings&) const = default;
};

void Validate(const PaintSettings& settings);

struct PaintMix {
    double cyan{};
    double magenta{};
    double yellow{};
    double black{};
    double white{};
};

struct PaletteColor {
    Rgb preview;
    PaintMix mix;
};

struct PointMm {
    double x{};
    double y{};
    bool operator==(const PointMm&) const = default;
};

struct Stroke {
    PointMm start;
    PointMm end;
    std::size_t paletteIndex{};
    bool operator==(const Stroke&) const = default;
};

// Vertices use half source-pixel units. Successive rings share the same vertex pool.
struct Vertex {
    int x2{}, y2{};
    bool operator==(const Vertex&) const = default;
};

struct FillEdge {
    int xTop2, xBottom2;  // Half-pixel x coordinates at the ends of this band.
    std::uint32_t patch;  // The polygon immediately to the left of this edge.

    [[nodiscard]] constexpr double X(double t) const { return 0.5*(xTop2+t*(xBottom2-xTop2)); }
};

struct VectorArtwork {
    int width{}, height{};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> ringOffsets{0};
    std::vector<double> ringAreas;
    std::vector<bool> paintThrough;
    std::vector<std::vector<FillEdge>> bands;

    [[nodiscard]] constexpr std::uint32_t PatchAt(double x, double y) const
    {
        if (!(x >= 0 && x < width && y >= 0 && y < height)) {
            throw std::out_of_range("Point outside vector artwork.");
        }
        const int band = static_cast<int>(2 * y);
        const double t = 2 * y - band;
        const auto& edges = bands[band];
        const auto edge = std::upper_bound(edges.begin(), edges.end(), x, [t](double query, const FillEdge& e) {
            return query < e.X(t);
        });
        if (edge == edges.end()) {
            throw std::logic_error("Open vector scanline.");
        }
        return edge->patch;
    }
};

struct Segment {
    std::size_t firstStroke{};
    std::size_t strokeCount{};
    std::size_t cellCount{};  // Covered cells in the physical brush simulation.
    std::uint32_t firstRing{}, ringCount{}, paletteIndex{};
    static constexpr std::uint32_t NoUndercoat = UINT32_MAX;
    std::uint32_t undercoat{NoUndercoat};
    double area{};  // Net area in square source pixels, including holes.
};

struct PaintPlan {
    PaintSettings settings;
    PointMm imageOrigin;
    VectorArtwork artwork;
    Raster quantized;  // Fixed-brush simulation at full completion.
    std::vector<PaletteColor> palette;
    std::vector<Segment> segments;
    std::vector<Stroke> strokes;
    std::vector<std::uint32_t> strokeAtCell;  // One-based stroke number for each brush cell.

    std::vector<std::uint32_t> undercoatStrokeAtCell;

    [[nodiscard]] Raster Preview(std::size_t strokeCount) const;
};

struct Document {
    std::shared_ptr<const ReferenceImage> reference;
    PaintSettings settings;
    std::shared_ptr<const PaintPlan> plan;
    std::size_t visibleStrokes{};
    std::size_t visiblePatches{};
};

enum class Tab { Prepare, Plot };

class CommandProcessor;

class ViewModel {
public:
    [[nodiscard]] const Document& ActiveDocument() const noexcept { return document_; }
    [[nodiscard]] Tab SelectedTab() const noexcept { return tab_; }

private:
    friend class CommandProcessor;

    Document document_;
    Tab tab_{Tab::Prepare};
};

}  // namespace painterbot
