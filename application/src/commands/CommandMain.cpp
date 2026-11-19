#include "algo/VectorAlgorithm.h"
#include "commands/CommandProcessor.h"
#include "io/ImageLoader.h"

#include <wx/image.h>
#include <wx/init.h>

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace painterbot;

void PrintStatus(const ViewModel& model)
{
    const auto& document = model.ActiveDocument();
    const auto* plan = document.plan.get();
    std::cout << "tab=" << (model.SelectedTab() == Tab::Prepare ? "prepare" : "plot")
              << " width_mm=" << document.settings.widthMm << " height_mm=" << document.settings.heightMm
              << " color_limit=" << document.settings.paletteColors;
    if (document.reference) {
        std::cout << " image_width=" << document.reference->source.width
                  << " image_height=" << document.reference->source.height;
    }
    if (plan) {
        std::cout << " colors=" << plan->palette.size() << " segments=" << plan->segments.size()
                  << " strokes=" << plan->strokes.size() << " visible=" << document.visibleStrokes
                  << " visible_patches=" << document.visiblePatches;
    }
    std::cout << '\n';
}

void PrintPlan(const ViewModel& model)
{
    const auto& plan = model.ActiveDocument().plan;
    if (!plan) {
        throw std::runtime_error("Prepare a painting before requesting its plan.");
    }
    std::cout << "stroke,segment,x_start_mm,y_start_mm,x_end_mm,y_end_mm,red,green,blue,cyan,magenta,yellow,black,white\n"
              << std::fixed << std::setprecision(6);
    for (std::size_t region = 0; region < plan->segments.size(); ++region) {
        const auto& segment = plan->segments[region];
        for (auto i = segment.firstStroke; i < segment.firstStroke + segment.strokeCount; ++i) {
            const auto& stroke = plan->strokes[i];
            const auto& color = plan->palette[stroke.paletteIndex];
            std::cout << i + 1 << ',' << region + 1 << ','
                      << stroke.start.x << ',' << stroke.start.y << ',' << stroke.end.x << ',' << stroke.end.y << ','
                      << int(color.preview.red) << ',' << int(color.preview.green) << ',' << int(color.preview.blue) << ','
                      << color.mix.cyan << ',' << color.mix.magenta << ',' << color.mix.yellow << ','
                      << color.mix.black << ',' << color.mix.white << '\n';
        }
    }
}

void PrintVectors(const Document& document)
{
    if (!document.plan) throw std::runtime_error("Prepare a painting before requesting its polygons.");
    const auto& plan = *document.plan;
    const auto& art = plan.artwork;
    std::cout << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << plan.settings.widthMm
              << "mm\" height=\"" << plan.settings.heightMm << "mm\">\n"
              << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
              << "<svg width=\"100%\" height=\"100%\" viewBox=\"0 0 "
              << 2*art.width << ' ' << 2*art.height << "\">\n";
    for (std::size_t i = 0; i < document.visiblePatches; ++i) {
        const auto& p = plan.segments[i];
        const auto color = plan.palette[p.paletteIndex].preview;
        std::cout << "<path fill=\"rgb(" << int(color.red) << ',' << int(color.green) << ',' << int(color.blue)
                  << ")\" fill-rule=\"evenodd\" d=\"";
        for (auto r = p.firstRing; r < p.firstRing + p.ringCount; ++r) {
            if (art.paintThrough[r]) continue;
            for (auto j = art.ringOffsets[r]; j < art.ringOffsets[r+1]; ++j) {
                const auto v = art.vertices[j];
                std::cout << (j == art.ringOffsets[r] ? "M" : " ") << v.x2 << ' ' << v.y2;
            }
            std::cout << 'Z';
        }
        std::cout << "\"/>\n";
    }
    std::cout << "</svg>\n</svg>\n";
}

void PrintPreview(const ViewModel& model)
{
    const auto& document = model.ActiveDocument();
    if (!document.plan) {
        throw std::runtime_error("Prepare a painting before requesting its preview.");
    }
    const auto frame = document.plan->Preview(document.visibleStrokes);
    std::cout << "P3\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (const auto pixel : frame.pixels) {
        std::cout << int(pixel.red) << ' ' << int(pixel.green) << ' ' << int(pixel.blue) << '\n';
    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(false);
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "PainterBotCommand reads UTF-8 commands from stdin, one per line.\n"
                     "  load \"PATH\"\n  prepare [WIDTH_MM HEIGHT_MM COLORS]\n  seek STROKE_COUNT\n  seek-patch PATCH_COUNT\n"
                     "  tab prepare|plot\n  undo\n  redo\n  status\n"
                     "  vectors  Print the visible polygon layers as SVG.\n"
                     "  plan     Print the ordered strokes as CSV.\n"
                     "  preview  Print the current brush grid as a PPM image.\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "Usage: PainterBotCommand [--help]\n";
        return 1;
    }

    wxInitializer wx;
    if (!wx) {
        std::cerr << "Unable to initialize wxWidgets image support.\n";
        return 1;
    }
    wxInitAllImageHandlers();
    CommandProcessor processor(DecodeReferenceImage, std::make_unique<VectorAlgorithm>());
    std::string line;
    for (std::size_t number = 1; std::getline(std::cin, line); ++number) {
        if (number == 1 && line.starts_with("\xef\xbb\xbf")) {
            line.erase(0, 3);
        }
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        const std::string_view command(line.data() + first, last - first + 1);
        try {
            if (command == "status") {
                PrintStatus(processor.Model());
            } else if (command == "vectors") {
                PrintVectors(processor.Model().ActiveDocument());
            } else if (command == "plan") {
                PrintPlan(processor.Model());
            } else if (command == "preview") {
                PrintPreview(processor.Model());
            } else {
                processor.Execute(ParseCommand(command));
            }
            std::cout.flush();
        } catch (const std::exception& error) {
            std::cerr << "Line " << number << ": " << error.what() << '\n';
            return 1;
        }
    }
    return std::cin.bad() ? 1 : 0;
}
