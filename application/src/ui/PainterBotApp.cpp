#include "algo/ScanlineAlgorithm.h"
#include "commands/CommandProcessor.h"
#include "io/ImageLoader.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/wx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <utility>

namespace painterbot {
namespace {

enum MenuId { OpenId = wxID_HIGHEST + 1, PrepareId };

struct CanvasLayout {
    double width;
    double height;
    double imageX;
    double imageY;
    double imageWidth;
    double imageHeight;
};

CanvasLayout LayoutFor(const PaintPlan& plan)
{
    return {double(plan.settings.widthMm), double(plan.settings.heightMm),
            plan.imageOrigin.x, plan.imageOrigin.y,
            double(plan.quantized.width * BrushWidthMm), double(plan.quantized.height * BrushWidthMm)};
}

wxBitmap MakeBitmap(const Raster& raster)
{
    static_assert(sizeof(Rgb) == 3);
    wxImage image(raster.width, raster.height, false);
    if (image.IsOk()) {
        std::memcpy(image.GetData(), raster.pixels.data(), raster.pixels.size() * sizeof(Rgb));
    }
    return wxBitmap(image);
}

class ImagePanel final : public wxPanel {
public:
    ImagePanel(wxWindow* parent, const wxString& placeholder)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
        , placeholder_(placeholder)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ImagePanel::Paint, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            Refresh(false);
            event.Skip();
        });
    }

    void SetContent(const wxBitmap& bitmap = {}, CanvasLayout layout = {})
    {
        bitmap_ = bitmap;
        layout_ = layout;
        Refresh(false);
    }

private:
    void Paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
        dc.Clear();
        const wxSize size = GetClientSize();
        if (!bitmap_.IsOk()) {
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            dc.DrawLabel(placeholder_, wxRect(wxPoint(0, 0), size), wxALIGN_CENTER);
            return;
        }

        const double inset = FromDIP(10);
        if (size.x <= 2 * inset || size.y <= 2 * inset) {
            return;
        }
        const double scale = std::min((size.x - 2 * inset) / layout_.width,
                                      (size.y - 2 * inset) / layout_.height);
        const double left = (size.x - layout_.width * scale) / 2;
        const double top = (size.y - layout_.height * scale) / 2;
        std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
        if (graphics) {
            graphics->SetPen(*wxTRANSPARENT_PEN);
            graphics->SetBrush(*wxWHITE_BRUSH);
            graphics->DrawRectangle(left, top, layout_.width * scale, layout_.height * scale);
            graphics->SetInterpolationQuality(bitmap_.GetWidth() <= MaxWidthMm / BrushWidthMm
                ? wxINTERPOLATION_NONE : wxINTERPOLATION_GOOD);
            graphics->DrawBitmap(bitmap_, left + layout_.imageX * scale, top + layout_.imageY * scale,
                                 layout_.imageWidth * scale, layout_.imageHeight * scale);
        }
    }

    wxString placeholder_;
    wxBitmap bitmap_;
    CanvasLayout layout_{};
};

struct Timeline {
    wxSlider* slider{};
    wxStaticText* label{};
};

class MainFrame final : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr, wxID_ANY, "PainterBot Application")
        , commands_(std::make_shared<CommandProcessor>(DecodeReferenceImage, std::make_unique<ScanlineAlgorithm>()))
        , workerTimer_(this)
    {
        SetClientSize(FromDIP(wxSize(1150, 665)));
        SetMinClientSize(FromDIP(wxSize(760, 440)));
        CreateStatusBar();
        CreateMenus();

        notebook_ = new wxNotebook(this, wxID_ANY);
        CreatePreparePage();
        CreatePlotPage();
        Bind(wxEVT_NOTEBOOK_PAGE_CHANGING, [this](wxBookCtrlEvent& event) {
            if (Busy()) {
                event.Veto();
            }
        });
        Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
            if (!Busy()) {
                commands_->Execute(SelectTab{event.GetSelection() == 0 ? Tab::Prepare : Tab::Plot});
            }
        });
        Bind(wxEVT_TIMER, &MainFrame::FinishWorker, this, workerTimer_.GetId());
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (Busy() && event.CanVeto()) {
                event.Veto();
                closeAfterWork_ = true;
                cancellation_.request_stop();
                SetStatusText("Closing...");
            } else {
                cancellation_.request_stop();
                event.Skip();
            }
        });

        UpdateDocument();
        Centre();
    }

    void OpenReference(const wxString& path)
    {
        StartWorker(LoadReference{PathFromUtf8(path.ToStdString(wxConvUTF8))}, "Loading reference image...");
    }

private:
    void CreateMenus()
    {
        auto* file = new wxMenu;
        file->Append(OpenId, "&Open Reference...\tCtrl+O");
        file->AppendSeparator();
        file->Append(wxID_EXIT, "E&xit");

        auto* edit = new wxMenu;
        edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
        edit->Append(wxID_REDO, "&Redo\tCtrl+Y");

        auto* painting = new wxMenu;
        painting->Append(PrepareId, "&Prepare Painting\tCtrl+Enter");

        auto* help = new wxMenu;
        help->Append(wxID_ABOUT, "&About PainterBot");

        auto* menu = new wxMenuBar;
        menu->Append(file, "&File");
        menu->Append(edit, "&Edit");
        menu->Append(painting, "&Painting");
        menu->Append(help, "&Help");
        SetMenuBar(menu);

        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ChooseReference(); }, OpenId);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RunHistory(Undo{}); }, wxID_UNDO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RunHistory(Redo{}); }, wxID_REDO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { PreparePainting(); }, PrepareId);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            wxMessageBox("PainterBot Application\nPrepare a reference image and preview the brush stroke sequence.",
                         "About PainterBot", wxOK | wxICON_INFORMATION, this);
        }, wxID_ABOUT);
    }

    wxSpinCtrl* AddSetting(wxWindow* page, wxSizer* row, const wxString& label,
                          int min, int max, int value)
    {
        row->Add(new wxStaticText(page, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        auto* input = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(80, -1)),
                                     wxSP_ARROW_KEYS, min, max, value);
        input->SetName(label);
        row->Add(input, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        return input;
    }

    ImagePanel* AddPreview(wxWindow* page, wxSizer* row, const wxString& title, const wxString& placeholder)
    {
        auto* column = new wxBoxSizer(wxVERTICAL);
        column->Add(new wxStaticText(page, wxID_ANY, title), 0, wxBOTTOM, FromDIP(6));
        auto* panel = new ImagePanel(page, placeholder);
        column->Add(panel, 1, wxEXPAND);
        row->Add(column, 1, wxEXPAND);
        return panel;
    }

    Timeline AddTimeline(wxWindow* page, wxSizer* layout)
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(page, wxID_ANY, "Stroke sequence"), 0, wxALIGN_CENTER_VERTICAL);
        auto* slider = new wxSlider(page, wxID_ANY, 0, 0, 1);
        slider->SetName("Stroke sequence");
        row->Add(slider, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
        auto* label = new wxStaticText(page, wxID_ANY, "0 / 0", wxDefaultPosition, FromDIP(wxSize(108, -1)),
                                       wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
        layout->Add(row, 0, wxEXPAND | wxTOP, FromDIP(10));
        slider->Bind(wxEVT_SLIDER, [this, slider](wxCommandEvent&) {
            if (!Busy()) {
                commands_->Execute(SeekPreview{static_cast<std::size_t>(slider->GetValue())});
                RenderProgress();
            }
        });
        return {slider, label};
    }

    void CreatePreparePage()
    {
        auto* page = new wxPanel(notebook_);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* content = new wxBoxSizer(wxVERTICAL);
        auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
        openButton_ = new wxButton(page, wxID_ANY, "Open Reference...");
        openButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseReference(); });
        toolbar->Add(openButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(18));
        const auto settings = commands_->Model().ActiveDocument().settings;
        width_ = AddSetting(page, toolbar, "Width (mm)", BrushWidthMm, MaxWidthMm, settings.widthMm);
        height_ = AddSetting(page, toolbar, "Height (mm)", BrushWidthMm, MaxHeightMm, settings.heightMm);
        colors_ = AddSetting(page, toolbar, "Max colors", MinPaletteColors, MaxPaletteColors, settings.paletteColors);
        prepareButton_ = new wxButton(page, wxID_ANY, "Prepare");
        prepareButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PreparePainting(); });
        toolbar->Add(prepareButton_, 0, wxALIGN_CENTER_VERTICAL);
        content->Add(toolbar, 0, wxEXPAND | wxBOTTOM, FromDIP(14));

        auto* previews = new wxBoxSizer(wxHORIZONTAL);
        reference_ = AddPreview(page, previews, "Reference", "Open a reference image");
        previews->AddSpacer(FromDIP(10));
        quantized_ = AddPreview(page, previews, "Quantized", "Prepare a painting");
        previews->AddSpacer(FromDIP(10));
        preparePreview_ = AddPreview(page, previews, "Painting sequence", "Prepare a painting");
        content->Add(previews, 1, wxEXPAND);
        prepareTimeline_ = AddTimeline(page, content);
        layout->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
        page->SetSizer(layout);
        notebook_->AddPage(page, "Prepare", true);
    }

    void CreatePlotPage()
    {
        auto* page = new wxPanel(notebook_);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* content = new wxBoxSizer(wxVERTICAL);
        plotPreview_ = AddPreview(page, content, "Plot simulation", "Prepare a painting in the Prepare tab");
        plotTimeline_ = AddTimeline(page, content);
        layout->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
        page->SetSizer(layout);
        notebook_->AddPage(page, "Plot");
    }

    [[nodiscard]] bool Busy() const { return worker_.valid(); }

    void ChooseReference()
    {
        if (Busy()) {
            return;
        }
        wxFileDialog picker(this, "Open reference image", {}, {},
            "Image files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.pcx;*.pnm;*.tga)"
            "|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.pcx;*.pnm;*.tga|All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (picker.ShowModal() == wxID_OK) {
            OpenReference(picker.GetPath());
        }
    }

    void PreparePainting()
    {
        if (!Busy() && commands_->Model().ActiveDocument().reference) {
            StartWorker(Prepare{PaintSettings{width_->GetValue(), height_->GetValue(), colors_->GetValue()}},
                        "Preparing brush strokes...");
        }
    }

    void StartWorker(Command command, const wxString& message)
    {
        cancellation_ = std::stop_source{};
        try {
            worker_ = std::async(std::launch::async, [commands = commands_, command = std::move(command),
                                                      stop = cancellation_.get_token()] {
                commands->Execute(command, stop);
            });
        } catch (const std::exception& error) {
            ShowError(error);
            return;
        }
        UpdateControls();
        SetStatusText(message);
        workerTimer_.Start(25);
    }

    void FinishWorker(wxTimerEvent&)
    {
        if (!Busy() || worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        workerTimer_.Stop();
        wxString error;
        try {
            worker_.get();
        } catch (const std::exception& failure) {
            error = wxString::FromUTF8(failure.what());
        }
        if (closeAfterWork_) {
            Close();
            return;
        }
        UpdateDocument();
        if (!error.empty()) {
            wxMessageBox(error, "PainterBot Application", wxOK | wxICON_ERROR, this);
        }
    }

    void ShowError(const std::exception& error)
    {
        wxMessageBox(wxString::FromUTF8(error.what()), "PainterBot Application", wxOK | wxICON_ERROR, this);
    }

    void RunHistory(const Command& command)
    {
        if (!Busy()) {
            commands_->Execute(command);
            UpdateDocument();
        }
    }

    void UpdateControls()
    {
        const bool ready = !Busy();
        const bool hasImage = ready && commands_->Model().ActiveDocument().reference;
        const bool hasPlan = ready && commands_->Model().ActiveDocument().plan;
        openButton_->Enable(ready);
        prepareButton_->Enable(hasImage);
        for (auto* input : {width_, height_, colors_}) {
            input->Enable(ready);
        }
        prepareTimeline_.slider->Enable(hasPlan);
        plotTimeline_.slider->Enable(hasPlan);
        GetMenuBar()->Enable(OpenId, ready);
        GetMenuBar()->Enable(PrepareId, hasImage);
        GetMenuBar()->Enable(wxID_UNDO, ready && commands_->CanUndo());
        GetMenuBar()->Enable(wxID_REDO, ready && commands_->CanRedo());
    }

    void UpdateDocument()
    {
        const auto& document = commands_->Model().ActiveDocument();
        if (displayedSettings_ != document.settings) {
            displayedSettings_ = document.settings;
            width_->SetValue(displayedSettings_.widthMm);
            height_->SetValue(displayedSettings_.heightMm);
            colors_->SetValue(displayedSettings_.paletteColors);
        }
        if (referenceImage_ != document.reference) {
            referenceImage_ = document.reference;
            if (referenceImage_) {
                const auto& image = referenceImage_->thumbnail;
                reference_->SetContent(MakeBitmap(image), {double(image.width), double(image.height), 0, 0,
                                                           double(image.width), double(image.height)});
            } else {
                reference_->SetContent();
            }
        }
        if (displayedPlan_ != document.plan) {
            displayedPlan_ = document.plan;
            if (displayedPlan_) {
                quantized_->SetContent(MakeBitmap(displayedPlan_->quantized), LayoutFor(*displayedPlan_));
            } else {
                quantized_->SetContent();
            }
        }
        RenderProgress();
        UpdateControls();

        if (document.plan) {
            SetStatusText(wxString::Format("%zu colors  |  %zu segments  |  %zu strokes  |  %d mm brush",
                                          document.plan->palette.size(), document.plan->segments.size(),
                                          document.plan->strokes.size(), BrushWidthMm));
        } else if (document.reference) {
            const auto utf8 = document.reference->path.filename().u8string();
            const auto filename = wxString::FromUTF8(reinterpret_cast<const char*>(utf8.data()), utf8.size());
            SetStatusText(filename + wxString::Format("  |  %d x %d px", document.reference->source.width,
                                                      document.reference->source.height));
        } else {
            SetStatusText("Open a reference image to begin.");
        }
    }

    void RenderProgress()
    {
        const auto& document = commands_->Model().ActiveDocument();
        const auto* plan = document.plan.get();
        const int count = plan ? static_cast<int>(plan->strokes.size()) : 0;
        const int visible = static_cast<int>(document.visibleStrokes);
        for (const auto& timeline : {prepareTimeline_, plotTimeline_}) {
            if (timeline.slider->GetMax() != std::max(1, count)) {
                timeline.slider->SetRange(0, std::max(1, count));
            }
            if (timeline.slider->GetValue() != visible) {
                timeline.slider->SetValue(visible);
            }
            timeline.label->SetLabel(wxString::Format("%d / %d", visible, count));
        }
        if (plan) {
            const auto bitmap = MakeBitmap(plan->Preview(visible));
            const auto layout = LayoutFor(*plan);
            preparePreview_->SetContent(bitmap, layout);
            plotPreview_->SetContent(bitmap, layout);
        } else {
            preparePreview_->SetContent();
            plotPreview_->SetContent();
        }
    }

    std::shared_ptr<CommandProcessor> commands_;
    std::stop_source cancellation_;
    std::future<void> worker_;
    wxTimer workerTimer_;
    bool closeAfterWork_{};
    wxNotebook* notebook_{};
    wxButton* openButton_{};
    wxButton* prepareButton_{};
    wxSpinCtrl* width_{};
    wxSpinCtrl* height_{};
    wxSpinCtrl* colors_{};
    ImagePanel* reference_{};
    ImagePanel* quantized_{};
    ImagePanel* preparePreview_{};
    ImagePanel* plotPreview_{};
    Timeline prepareTimeline_;
    Timeline plotTimeline_;
    PaintSettings displayedSettings_;
    std::shared_ptr<const ReferenceImage> referenceImage_;
    std::shared_ptr<const PaintPlan> displayedPlan_;
};

class PainterBotApp final : public wxApp {
public:
    bool OnInit() override
    {
        SetAppDisplayName("PainterBot Application");
        wxInitAllImageHandlers();
        auto* frame = new MainFrame;
        frame->Show();
        if (argc > 1) {
            frame->OpenReference(argv[1]);
        }
        return true;
    }
};

}  // namespace
}  // namespace painterbot

wxIMPLEMENT_APP(painterbot::PainterBotApp);
