#pragma once

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
inline constexpr int MaxPaletteColors = 32;

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
    int paletteColors{12};
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

struct Segment {
    std::size_t firstStroke{};
    std::size_t strokeCount{};
    std::size_t cellCount{};
};

struct PaintPlan {
    PaintSettings settings;
    PointMm imageOrigin;
    Raster quantized;
    std::vector<PaletteColor> palette;
    std::vector<Segment> segments;
    std::vector<Stroke> strokes;
    std::vector<std::uint32_t> strokeAtCell;  // One-based stroke number for each brush cell.

    [[nodiscard]] Raster Preview(std::size_t strokeCount) const;
};

struct Document {
    std::shared_ptr<const ReferenceImage> reference;
    PaintSettings settings;
    std::shared_ptr<const PaintPlan> plan;
    std::size_t visibleStrokes{};
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
