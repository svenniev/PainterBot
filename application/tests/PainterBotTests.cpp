#include "algo/VectorAlgorithm.h"
#include "algo/ContourTracer.h"
#include "algo/VectorIndex.h"
#include <array>
#include <utility>
#include "commands/CommandProcessor.h"
#include "io/ImageLoader.h"

#include <wx/filename.h>
#include <wx/image.h>
#include <wx/init.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace painterbot;

void Check(bool condition, const char* description)
{
    if (!condition) {
        throw std::runtime_error(description);
    }
}

template <typename Exception, typename Operation>
void ExpectThrows(Operation operation)
{
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected command failure.");
}

// Independent even-odd containment check against the emitted vector rings.
constexpr bool Contains(const VectorArtwork& art, const Segment& patch, double x, double y, bool pour = false)
{
    bool inside = false;
    for (auto r = patch.firstRing; r < patch.firstRing+patch.ringCount; ++r) {
        if (pour && art.paintThrough[r]) continue;
        const auto end = art.ringOffsets[r+1];
        Vertex a = art.vertices[end-1];
        for (auto i = art.ringOffsets[r]; i < end; ++i) {
            const Vertex b = art.vertices[i];
            if ((a.y2 > y) != (b.y2 > y) && x < a.x2 + (y-a.y2)*(b.x2-a.x2)/(b.y2-a.y2))
                inside = !inside;
            a = b;
        }
    }
    return inside;
}

constexpr bool CheckContours(int width, int height, const std::vector<std::uint16_t>& labels,
                             int expectedPatches = 0, int expectedRings = 0)
{
    Raster source{width,height,{}};
    PaintPlan plan;
    plan.artwork.width = width;
    plan.artwork.height = height;
    detail::ContourTracer(source, labels, plan).Trace();
    detail::IndexFills(plan);
    detail::FindUndercoats(plan);
    const auto& art = plan.artwork;
    if (expectedPatches && plan.segments.size() != expectedPatches) return false;
    if (expectedRings && art.ringOffsets.size() != expectedRings+1) return false;
    double area = 0;
    for (const auto& patch : plan.segments) {
        if (patch.area <= 0) return false;
        area += patch.area;
    }
    if (area != width*height) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int owners = 0;
            for (const auto& patch : plan.segments) {
                if (!Contains(art, patch, 2*x+1, 2*y+1)) continue;
                if (patch.paletteIndex != labels[y*width+x]) return false;
                ++owners;
            }
            if (owners != 1) return false;
            const auto owner = art.PatchAt(x+0.5,y+0.5);
            if (plan.segments[owner].paletteIndex != labels[y*width+x]) return false;
            std::uint32_t painted = Segment::NoUndercoat;
            for (std::uint32_t i = 0; i < plan.segments.size(); ++i) {
                if (Contains(art,plan.segments[i],2*x+1,2*y+1,true)) painted = i;
                const auto base = plan.segments[owner].undercoat;
                const auto expected = owner <= i ? owner : (base <= i ? base : Segment::NoUndercoat);
                if (painted != expected) return false;
            }
            const auto edgeOwner = art.PatchAt(x+0.12,y+0.23);
            if (!Contains(art,plan.segments[edgeOwner],2*x+0.24,2*y+0.46)) return false;
        }
    }
    return true;
}

// These exercise the production tracer during compilation as well as in native tests.
static_assert(CheckContours(3,3,{0,0,0, 0,1,0, 0,0,0}, 2, 3));
static_assert(CheckContours(2,2,{0,1,1,0}, 4, 4));
static_assert(CheckContours(3,2,{0,0,1, 2,0,1}, 3, 3));
static_assert(CheckContours(3,3,{0,0,0, 0,1,0, 0,0,1}, 3));

template<int Mask>
constexpr bool BinaryContourCase = [] {
    std::vector<std::uint16_t> labels;
    for (int i = 0; i < 9; ++i) labels.push_back((Mask >> i) & 1);
    return CheckContours(3, 3, labels);
}();

template<int... Masks>
constexpr bool AllBinaryContours(std::integer_sequence<int, Masks...>)
{
    constexpr std::array<bool, sizeof...(Masks)> results{BinaryContourCase<Masks>...};
    return std::all_of(results.begin(), results.end(), [](bool passed) { return passed; });
}

static_assert(AllBinaryContours(std::make_integer_sequence<int, 512>{}));

void TestVectorDetail()
{
    VectorAlgorithm algorithm;
    // Enumerate every binary 3x3 neighborhood, including self-touching rings and holes.
    for (int mask = 0; mask < 512; ++mask) {
        std::vector<std::uint16_t> labels;
        for (int i = 0; i < 9; ++i) labels.push_back((mask >> i) & 1);
        Check(CheckContours(3,3,labels), "The contour partition lost coverage or changed a source label.");
        Raster input{3,3,{}};
        for (auto label : labels) input.pixels.push_back(label ? Rgb{200,0,0} : Rgb{0,0,0});
        const auto plan = algorithm.Prepare(input, {4,4,2});
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 12; ++x) {
                const double sx = (x+0.27)/4, sy = (y+0.49)/4;
                const auto& patch = plan.segments[plan.artwork.PatchAt(sx,sy)];
                Check(Contains(plan.artwork,patch,2*sx,2*sy), "Vector scan conversion disagrees with polygon winding.");
            }
        }
    }

    Raster image{32,32,{}};
    for (int i = 0; i < 1024; ++i)
        image.pixels.push_back({std::uint8_t(i%32*8), std::uint8_t(i/32*8), 125});
    const auto detailed = algorithm.Prepare(image, {4,4,1024});
    Check(detailed.palette.size() == 1024 && detailed.segments.size() == 1024 && detailed.strokes.size() == 1,
          "All 1024 single-pixel colors must survive on a one-brush-cell canvas.");
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const auto id = detailed.artwork.PatchAt(x+0.5,y+0.5);
            Check(detailed.palette[detailed.segments[id].paletteIndex].preview == image.pixels[y*32+x],
                  "Exact source colors must survive when the palette has room.");
        }
    }
    const auto reduced = algorithm.Prepare(image, {4,4,65});
    Check(reduced.palette.size() > 32 && reduced.palette.size() <= 65, "Reduced palettes must respect limits above 32.");

    Raster stripes{4,1,{{235,0,0},{160,0,0},{95,0,0},{40,0,0}}};
    const auto ordered = algorithm.Prepare(stripes, {16,4,4});
    for (int i = 0; i < 4; ++i) {
        Check(ordered.palette[ordered.segments[i].paletteIndex].preview == stripes.pixels[3-i],
              "Equal-area shades should paint from dimmest to brightest.");
    }
}

void TestSegments()
{
    constexpr Rgb blue{10, 30, 200};
    constexpr Rgb orange{230, 80, 20};
    constexpr Rgb blank{255, 255, 255};
    // The earlier orange corner is isolated from the second orange cell.
    const Raster image{4, 3, {orange, blue, blue, blue,
                              blue, blue, orange, blue,
                              blue, blue, blue, blue}};
    const VectorAlgorithm algorithm;
    const auto plan = algorithm.Prepare(image, {16, 12, 2});
    Check(plan.quantized.pixels == image.pixels, "Two-color quantization must preserve the source.");
    Check(plan.segments.size() == 3, "Disconnected islands must be separate segments.");
    Check(plan.segments[0].cellCount == 10, "The wide background must paint first.");
    Check(plan.segments[0].firstStroke == 0 && plan.segments[0].strokeCount == 3,
          "The background should cross the enclosed island in one run per row.");
    Check(plan.strokes[1].start.x > plan.strokes[1].end.x, "Odd rows must run from right to left.");
    const auto empty = plan.Preview(0);
    Check(std::all_of(empty.pixels.begin(), empty.pixels.end(), [](Rgb pixel) { return pixel == blank; }),
          "Stroke zero must show blank canvas.");
    const auto background = plan.Preview(3);
    Check(background.pixels[0] == blank && background.pixels[6] == blue && background.pixels[1] == blue,
          "The enclosed detail should show its blue undercoat before the orange overpaint.");
    const auto island = plan.artwork.PatchAt(2.5,1.5);
    Check(plan.segments[island].undercoat == 0, "The enclosed orange island should have a blue undercoat.");
    Check(plan.Preview(plan.strokes.size()).pixels == plan.quantized.pixels,
          "The final sequence frame must match the quantized image.");

    const Raster solid{1, 1, {{80, 80, 80}}};
    const auto singleCellPlan = algorithm.Prepare(solid, {4, 4, 32});
    Check(singleCellPlan.palette.size() == 1 && singleCellPlan.segments.size() == 1 && singleCellPlan.strokes.size() == 1
          && singleCellPlan.strokes[0].start == singleCellPlan.strokes[0].end, "A single cell needs one brush dab.");
}

void TestImageFormats()
{
    struct TemporaryImage {
        wxString filename = wxFileName::CreateTempFileName("painterbot");
        std::filesystem::path path = PathFromUtf8(filename.ToStdString(wxConvUTF8));

        ~TemporaryImage()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } file;
    Check(!file.filename.empty(), "Unable to create a temporary image fixture.");

    wxImage image(2, 1);
    image.SetRGB(0, 0, 255, 0, 0);
    image.SetRGB(1, 0, 0, 0, 255);
    Check(image.SaveFile(file.filename, wxBITMAP_TYPE_BMP), "Unable to create the BMP fixture.");
    const auto decodedBmp = DecodeReferenceImage(file.path);
    Check(decodedBmp.source.pixels == std::vector<Rgb>{{255, 0, 0}, {0, 0, 255}},
          "BMP import must preserve opaque colors.");
    image.InitAlpha();
    image.SetAlpha(0, 0, 0);
    image.SetAlpha(1, 0, 128);
    Check(image.SaveFile(file.filename, wxBITMAP_TYPE_PNG), "Unable to create the PNG fixture.");
    const auto png = DecodeReferenceImage(file.path);
    Check(png.source.pixels == std::vector<Rgb>{{255, 255, 255}, {127, 127, 255}},
          "PNG import must composite transparent pixels onto white canvas.");
}

void TestSampleAndCommands(const std::filesystem::path& file)
{
    CommandProcessor commands(DecodeReferenceImage, std::make_unique<VectorAlgorithm>());
    auto run = [&](std::string_view command) { commands.Execute(ParseCommand(command)); };
    const auto& document = commands.Model().ActiveDocument();
    ExpectThrows<std::runtime_error>([&] { run("prepare"); });
    ExpectThrows<std::invalid_argument>([&] { run("seek -1"); });
    ExpectThrows<std::invalid_argument>([&] { run("prepare 1000 550 12 extra"); });
    const auto utf8 = file.u8string();
    run("load \"" + std::string(utf8.begin(), utf8.end()) + "\"");
    Check(document.reference && document.reference->source.width == 3440 && document.reference->source.height == 1440,
          "The bundled JPEG must decode.");
    Check(document.reference->thumbnail.width == 1000, "The reference preview must use a bounded thumbnail.");
    ExpectThrows<std::invalid_argument>([&] { run("prepare 1001 550 12"); });
    ExpectThrows<std::invalid_argument>([&] { run("prepare 1000 551 12"); });
    ExpectThrows<std::invalid_argument>([&] { run("prepare 1000 550 1025"); });
    Check(!document.plan && document.settings == PaintSettings{}, "Rejected settings must leave the document intact.");

    run("prepare 1000 550 16");
    const auto prepared = document.plan;
    Check(prepared && prepared->quantized.width == 250 && prepared->quantized.height == 105,
          "The brush grid must fit the reference aspect ratio within the canvas.");
    Check(prepared->palette.size() <= 16, "The plan must respect the requested color limit.");
    Check(prepared->segments.size() > 1 && prepared->strokes.size() > 1, "The sample must produce an ordered plan.");
    for (const auto& entry : prepared->palette) {
        const auto& mix = entry.mix;
        Check(mix.cyan >= 0 && mix.magenta >= 0 && mix.yellow >= 0 && mix.black >= 0 && mix.white >= 0
              && std::abs(mix.cyan + mix.magenta + mix.yellow + mix.black + mix.white - 1) < 1e-9,
              "Each palette color must have a normalized CMYKW mix.");
    }

    const auto [first, last] = std::minmax_element(prepared->strokeAtCell.begin(), prepared->strokeAtCell.end());
    Check(first != prepared->strokeAtCell.end() && *first >= 1 && *last == prepared->strokes.size(),
          "Each brush cell must have a valid stroke number.");
    std::size_t nextStroke = 0;
    std::size_t coveredCells = 0;
    for (const auto& region : prepared->segments) {
        Check(region.firstStroke == nextStroke, "Every segment must describe a contiguous range of strokes.");
        nextStroke += region.strokeCount;
        coveredCells += region.cellCount;
    }
    Check(nextStroke == prepared->strokes.size() && coveredCells == prepared->quantized.pixels.size(),
          "The ordered plan must cover every brush cell exactly once.");
    for (const auto& stroke : prepared->strokes) {
        for (const auto endpoint : {stroke.start, stroke.end}) {
            Check(endpoint.x >= 2 && endpoint.x <= 998 && endpoint.y >= 2 && endpoint.y <= 548,
                  "The brush footprint must remain within the canvas.");
        }
    }

    run("seek-patch 0");
    Check(document.visiblePatches == 0 && document.visibleStrokes == 0, "The first polygon frame must be empty.");
    run("seek-patch 1");
    Check(document.visiblePatches == 1 && document.visibleStrokes == prepared->segments[0].strokeCount,
          "The polygon playhead must advance physical strokes through the same completed patches.");
    ExpectThrows<std::invalid_argument>([&] { run("seek-patch 999999999"); });
    run("tab plot");
    run("seek 1");
    Check(commands.Model().SelectedTab() == Tab::Plot && document.visibleStrokes == 1,
          "The CLI must control the tab and shared playhead.");
    run("undo");
    Check(!document.plan && document.settings == PaintSettings{} && commands.CanRedo(),
          "Undo must restore the previous settings and plan.");
    ExpectThrows<std::runtime_error>([&] { run("load missing-reference.png"); });
    Check(commands.CanRedo(), "Failed loading must preserve redo history.");
    run("redo");
    Check(document.plan == prepared && document.visibleStrokes == prepared->strokes.size(),
          "Redo must restore the computed plan and its completed preview.");

    run("undo");
    run("prepare 1000 550 16");
    Check(document.plan->artwork.vertices == prepared->artwork.vertices && document.plan->artwork.ringOffsets == prepared->artwork.ringOffsets,
          "Repeating preparation must reproduce vector rings.");
    Check(!commands.CanRedo() && document.plan->strokes == prepared->strokes
          && document.plan->quantized.pixels == prepared->quantized.pixels,
          "Preparing again must branch history and reproduce the same plan.");

    std::stop_source canceled;
    canceled.request_stop();
    ExpectThrows<std::runtime_error>([&] { commands.Execute(Prepare{}, canceled.get_token()); });
    run("undo");
    run("undo");
    Check(!document.reference && !commands.CanUndo(), "Undoing load must restore the empty document.");
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        wxInitializer wx;
        Check(wx.IsOk() && argc == 2, "Expected wxWidgets initialization and the sample image argument.");
        wxInitAllImageHandlers();
        TestVectorDetail();
        TestSegments();
        TestImageFormats();
        TestSampleAndCommands(argv[1]);
        std::cout << "PainterBot tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
