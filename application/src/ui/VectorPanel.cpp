#include "ui/VectorPanel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <wx/dcbuffer.h>
#include <wx/image.h>
#include <wx/settings.h>

namespace painterbot {
namespace {
constexpr auto Canvas = std::numeric_limits<std::uint32_t>::max();
constexpr auto Outside = Canvas - 1;
} // namespace

VectorPanel::VectorPanel(wxWindow* parent, const wxString& placeholder)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
    , placeholder_(placeholder)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetToolTip("Scroll to zoom, drag to pan, double-click to fit.");
    Bind(wxEVT_PAINT, &VectorPanel::Paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        InvalidateGeometry();
        e.Skip();
    });
    Bind(wxEVT_MOUSEWHEEL, &VectorPanel::Zoom, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        if (plan_ && !HasCapture()) {
            drag_ = e.GetPosition();
            CaptureMouse();
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
        if (!HasCapture() || !e.Dragging()) {
            return;
        }
        const auto pos = e.GetPosition();
        panX_ += pos.x - drag_.x;
        panY_ += pos.y - drag_.y;
        drag_ = pos;
        InvalidateGeometry();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (HasCapture()) {
            ReleaseMouse();
        }
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent&) { });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&) {
        zoom_ = 1;
        panX_ = panY_ = 0;
        InvalidateGeometry();
    });
}

void VectorPanel::InvalidateGeometry()
{
    indexedSize_ = { };
    bitmap_ = wxNullBitmap;
    Refresh(false);
}

void VectorPanel::Zoom(wxMouseEvent& e)
{
    if (!plan_ || !e.GetWheelDelta()) {
        return;
    }
    const auto size = GetClientSize();
    const double next
        = std::clamp(zoom_ * std::pow(1.5, double(e.GetWheelRotation()) / e.GetWheelDelta()), 1.0, 512.0);
    const double ratio = next / zoom_;
    panX_ = (e.GetX() - size.x * 0.5) * (1 - ratio) + panX_ * ratio;
    panY_ = (e.GetY() - size.y * 0.5) * (1 - ratio) + panY_ * ratio;
    zoom_ = next;
    if (zoom_ == 1) {
        panX_ = panY_ = 0;
    }
    InvalidateGeometry();
}

void VectorPanel::SetPlan(std::shared_ptr<const PaintPlan> plan, std::size_t visiblePatches)
{
    if (plan_ == plan && visible_ == visiblePatches) {
        return;
    }
    if (plan_ != plan) {
        plan_ = std::move(plan);
        zoom_ = 1;
        panX_ = panY_ = 0;
        InvalidateGeometry();
    }
    visible_ = visiblePatches;
    bitmap_ = wxNullBitmap;
    Refresh(false);
}

// Scan convert the actual polygon edges at the current transform. Cache four patch IDs
// per screen pixel so seeking only recolors this viewport, even on very large plans.
void VectorPanel::IndexViewport()
{
    const auto size = GetClientSize();
    const auto& art = plan_->artwork;
    const double inset = std::min({ double(FromDIP(8)), size.x * 0.2, size.y * 0.2 });
    const double fit = zoom_
        * std::min(
            (size.x - 2 * inset) / plan_->settings.widthMm, (size.y - 2 * inset) / plan_->settings.heightMm);
    const double cx = size.x * 0.5 + panX_, cy = size.y * 0.5 + panY_;
    const double cw = fit * plan_->settings.widthMm, ch = fit * plan_->settings.heightMm;
    const double scale = std::min(cw / art.width, ch / art.height);
    const double left = cx - scale * art.width * 0.5, top = cy - scale * art.height * 0.5;
    const int stride = 2 * size.x;
    samples_.resize(static_cast<std::size_t>(stride) * 2 * size.y);
    for (int y = 0; y < 2 * size.y; ++y) {
        auto row = samples_.begin() + static_cast<std::size_t>(y) * stride;
        const double screenY = (y + 0.5) * 0.5;
        std::fill_n(row, stride, Outside);
        if (std::abs(screenY - cy) >= ch * 0.5) {
            continue;
        }
        // Convert a continuous screen edge to the first subpixel center on its right.
        const auto column = [stride](double screenX) {
            return static_cast<int>(std::clamp(std::ceil(screenX * 2 - 0.5), 0.0, double(stride)));
        };
        std::fill(row + column(cx - cw * 0.5), row + column(cx + cw * 0.5), Canvas);
        const double sourceY = (screenY - top) / scale;
        if (sourceY < 0 || sourceY >= art.height) {
            continue;
        }
        const int band = static_cast<int>(sourceY * 2);
        const double t = sourceY * 2 - band;
        const auto& edges = art.bands[band];
        const auto edgeX = [&](const FillEdge& e) { return e.X(t); };
        auto edge = std::lower_bound(edges.begin(), edges.end(), -left / scale,
            [&](const FillEdge& e, double bound) { return edgeX(e) < bound; });
        int first = column(left);
        for (; edge != edges.end() && first < stride; ++edge) {
            const int past = column(left + scale * edgeX(*edge));
            std::fill(row + first, row + past, edge->patch);
            first = past;
        }
    }
    indexedSize_ = size;
}

void VectorPanel::ColorViewport()
{
    const int w = indexedSize_.x, h = indexedSize_.y;
    wxImage image(w, h, false);
    auto* dest = image.GetData();
    const auto bg = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
    const Rgb outside { bg.Red(), bg.Green(), bg.Blue() };
    for (int y = 0; y < h; ++y) {
        const auto* row = samples_.data() + static_cast<std::size_t>(y) * 4 * w;
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0;
            for (int offset : { 0, 1, 2 * w, 2 * w + 1 }) {
                const auto id = row[2 * x + offset];
                Rgb c { 255, 255, 255 };
                if (id == Outside) {
                    c = outside;
                } else if (id != Canvas) {
                    const auto visibleId = id < visible_ ? id : plan_->segments[id].undercoat;
                    if (visibleId < visible_) {
                        c = plan_->palette[plan_->segments[visibleId].paletteIndex].preview;
                    }
                }
                r += c.red;
                g += c.green;
                b += c.blue;
            }
            *dest++ = (r + 2) / 4;
            *dest++ = (g + 2) / 4;
            *dest++ = (b + 2) / 4;
        }
    }
    bitmap_ = wxBitmap(image);
}

void VectorPanel::Paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    if (!plan_ || size.x <= 0 || size.y <= 0) {
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
        dc.Clear();
        dc.DrawLabel(placeholder_, wxRect(wxPoint(0, 0), size), wxALIGN_CENTER);
        return;
    }
    if (indexedSize_ != size) {
        IndexViewport();
    }
    if (!bitmap_.IsOk()) {
        ColorViewport();
    }
    dc.DrawBitmap(bitmap_, 0, 0);
}
} // namespace painterbot
