#include "io/ImageLoader.h"

#include <wx/image.h>
#include <wx/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace painterbot {
namespace {

void FlattenTransparency(wxImage& image)
{
    if (image.HasMask() && !image.HasAlpha()) {
        image.InitAlpha();
    }
    if (!image.HasAlpha()) {
        return;
    }
    const auto count = static_cast<std::size_t>(image.GetWidth()) * image.GetHeight();
    const bool hasMask = image.HasMask();
    const Rgb mask{image.GetMaskRed(), image.GetMaskGreen(), image.GetMaskBlue()};
    auto* rgb = image.GetData();
    const auto* alpha = image.GetAlpha();
    for (std::size_t i = 0; i < count; ++i, rgb += 3) {
        const int opacity = hasMask && Rgb{rgb[0], rgb[1], rgb[2]} == mask ? 0 : alpha[i];
        for (int channel = 0; channel < 3; ++channel) {
            rgb[channel] = static_cast<unsigned char>((rgb[channel] * opacity + 255 * (255 - opacity) + 127) / 255);
        }
    }
    image.SetMask(false);
    image.ClearAlpha();
}

Raster ReadRaster(const wxImage& image)
{
    static_assert(sizeof(Rgb) == 3);
    if (!image.IsOk()) {
        throw std::runtime_error("Unable to create a reference image preview.");
    }
    Raster raster{image.GetWidth(), image.GetHeight(), {}};
    raster.pixels.resize(static_cast<std::size_t>(raster.width) * raster.height);
    std::memcpy(raster.pixels.data(), image.GetData(), raster.pixels.size() * sizeof(Rgb));
    return raster;
}

}  // namespace

ReferenceImage DecodeReferenceImage(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    const auto filename = wxString::FromUTF8(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    wxImage image;
    {
        wxLogNull suppressDecoderMessages;
        if (!image.LoadFile(filename, wxBITMAP_TYPE_ANY)) {
            throw std::runtime_error("Unable to open the reference image. Check the file path and image format.");
        }
    }
    FlattenTransparency(image);
    auto source = ReadRaster(image);
    constexpr int maxThumbnailSide = 1000;
    const int longest = std::max(source.width, source.height);
    if (longest > maxThumbnailSide) {
        const double scale = double(maxThumbnailSide) / longest;
        image.Rescale(std::max(1, static_cast<int>(std::lround(source.width * scale))),
                      std::max(1, static_cast<int>(std::lround(source.height * scale))), wxIMAGE_QUALITY_HIGH);
    }
    return {path, std::move(source), ReadRaster(image)};
}

}  // namespace painterbot
