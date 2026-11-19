#include "algo/ScanlineAlgorithm.h"
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

void TestSegments()
{
    constexpr Rgb blue{10, 30, 200};
    constexpr Rgb orange{230, 80, 20};
    constexpr Rgb blank{255, 255, 255};
    // The earlier orange corner is isolated from the second orange cell.
    const Raster image{4, 3, {orange, blue, blue, blue,
                              blue, blue, orange, blue,
                              blue, blue, blue, blue}};
    const ScanlineAlgorithm algorithm;
    const auto plan = algorithm.Prepare(image, {16, 12, 2});
    Check(plan.quantized.pixels == image.pixels, "Two-color quantization must preserve the source.");
    Check(plan.segments.size() == 3, "Disconnected islands must be separate segments.");
    Check(plan.segments[0].cellCount == 10, "The wide background must paint first.");
    Check(plan.segments[0].firstStroke == 0 && plan.segments[0].strokeCount == 4,
          "The background must include only its four runs.");
    Check(plan.strokes[1].start.x > plan.strokes[2].start.x, "Odd rows must run from right to left.");
    const auto empty = plan.Preview(0);
    Check(std::all_of(empty.pixels.begin(), empty.pixels.end(), [](Rgb pixel) { return pixel == blank; }),
          "Stroke zero must show blank canvas.");
    const auto background = plan.Preview(4);
    Check(background.pixels[0] == blank && background.pixels[6] == blank && background.pixels[1] == blue,
          "The preview must reveal only cells covered by completed strokes.");
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
    CommandProcessor commands(DecodeReferenceImage, std::make_unique<ScanlineAlgorithm>());
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
    ExpectThrows<std::invalid_argument>([&] { run("prepare 1000 550 33"); });
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
    Check(first != prepared->strokeAtCell.end() && *first == 1 && *last == prepared->strokes.size(),
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
