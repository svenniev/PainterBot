#pragma once

#include "viewmodel/viewmodel.h"
#include <wx/bitmap.h>
#include <wx/panel.h>

namespace painterbot {

class VectorPanel final : public wxPanel {
public:
    VectorPanel(wxWindow* parent, const wxString& placeholder);
    void SetPlan(std::shared_ptr<const PaintPlan> plan, std::size_t visiblePatches);

private:
    void Paint(wxPaintEvent&);
    void IndexViewport();
    void ColorViewport();
    void InvalidateGeometry();
    void Zoom(wxMouseEvent&);

    wxString placeholder_;
    std::shared_ptr<const PaintPlan> plan_;
    std::size_t visible_ { };
    double zoom_ { 1 }, panX_ { }, panY_ { };
    wxPoint drag_;
    wxSize indexedSize_;
    std::vector<std::uint32_t> samples_;
    wxBitmap bitmap_;
};
} // namespace painterbot
