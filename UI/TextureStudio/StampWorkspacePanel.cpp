/*============================================================================================================================================
                                                         STAMPWORKSPACEPANEL.CPP
============================================================================================================================================*/
// 🧩 Native Stamp (decal) authoring: the same toolbar/canvas/inspector feel as Pattern, retargeted at one bounded
//    24-layer decal stack (Shape/Text/Vector/Image components) instead of a tileable preset library.

#include "StampWorkspacePanel.h"
#include "VectorMaskControls.h"
#include "PatternThumbnailRaster.h"
#include "WorkspaceZoom.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/ContentInterchange/StampTextOutline.h"
#include "Engine/ContentInterchange/StampVectorImport.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace Frontier
{
namespace
{
constexpr const char *ShapeCaptions[]{"Rectangle", "Ellipse", "Diamond", "Triangle", "Polygon", "Star"};
constexpr const char *ShapeNames[]{"rect", "ellipse", "diamond", "triangle", "polygon", "star"};
constexpr const char *ShapePaths[]{"M4 4H20V20H4Z", "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3Z", "M12 3L21 12L12 21L3 12Z", "M12 3L22 21H2Z", "M7 3H17L22 12L17 21H7L2 12Z", "M12 2L15 8L22 9L17 14L18 22L12 18L6 22L7 14L2 9L9 8Z"};
constexpr const char *SelectPath = "M4 3L11 21L14 14L21 11Z";
constexpr const char *PlusPath = "M12 5V19M5 12H19";
constexpr const char *VisiblePath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z";
constexpr const char *LockPath = "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10";
constexpr const char *TrashPath = "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17";
constexpr const char *CopyPath = "M8 8H21V21H8ZM16 8V3H3V16H8";
constexpr const char *TextIconPath = "M4 5H20M12 5V20M8 20H16";
constexpr const char *VectorIconPath = "M4 19L9 5L13 15L20 4";
constexpr const char *ImageIconPath = "M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15M9 9A1 1 0 1 0 9 9.01";

ImVec4 Spectrum(uint32_t Colour)
{
    return {float((Colour >> 16) & 255) / 255, float((Colour >> 8) & 255) / 255, float(Colour & 255) / 255, 1};
}

void Caption(ImVec2 Position, const char *Text, float Size, uint32_t Colour, void *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont(), Size * 1.302f, Position, ImGui::GetColorU32(Spectrum(Colour)), Text);
}

// 📝 Rounded, padded modal chrome, matching Pattern's own close/unsaved-changes prompt and the
// new Add Text/Vector/Image popups — ConstructInterface zeroes WindowRounding/WindowPadding for the
// whole docked workspace, so a modal needs its own scope or it inherits flush, square edges.
struct StampModalScope
{
    explicit StampModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~StampModalScope()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};

std::string FoldCaption(std::string CaptionText)
{
    for (auto &Letter : CaptionText) if (Letter >= 'A' && Letter <= 'Z') Letter = char(Letter + 32);
    return CaptionText;
}

// 📝 A tiny, self-contained PNG IHDR sniff (independent of MaterialAuthoring's validator) so the Add
// Image popup can size its placeholder rectangle to the pasted image's real aspect ratio before the
// engine's own ValidateVectorAuthoringOrganization pass fully validates/accepts the embedded bytes.
std::optional<std::array<uint32_t, 2>> SniffEmbeddedImageExtent(std::string_view DataUrl)
{
    constexpr std::string_view Prefix = "data:image/png;base64,";
    if (!DataUrl.starts_with(Prefix)) return std::nullopt;
    const std::string_view Encoded = DataUrl.substr(Prefix.size());
    constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<uint8_t, 36> Header{};
    size_t Produced = 0;
    uint32_t Accumulator = 0;
    int Bits = 0;
    for (const char Letter : Encoded.substr(0, 48))
    {
        if (Letter == '=') break;
        const char *Position = std::strchr(Alphabet, Letter);
        if (!Position) return std::nullopt;
        Accumulator = (Accumulator << 6) | uint32_t(Position - Alphabet);
        Bits += 6;
        if (Bits >= 8) { Bits -= 8; if (Produced < Header.size()) Header[Produced++] = uint8_t((Accumulator >> Bits) & 0xff); }
    }
    if (Produced < 24) return std::nullopt;
    constexpr std::array<uint8_t, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(Signature.begin(), Signature.end(), Header.begin())) return std::nullopt;
    const auto ReadExtent = [&Header](size_t Offset)
    { return (uint32_t(Header[Offset]) << 24) | (uint32_t(Header[Offset + 1]) << 16) | (uint32_t(Header[Offset + 2]) << 8) | uint32_t(Header[Offset + 3]); };
    return std::array<uint32_t, 2>{ReadExtent(16), ReadExtent(20)};
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT AND PRESENTATION LIFETIME
//------------------------------------------------------------------------------------------------------------------------

StampWorkspacePanel::StampWorkspacePanel()
{
    VectorAuthoringDocument Blank;
    Blank.Stamp = true;
    Blank.Name = "Untitled stamp";
    ConfigureDocument(Blank);
    Controls.RegisterExtent = [&](std::string_view CaptionText, PlaneExtent Extent) { ControlExtents[std::string(CaptionText)] = Extent; };
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Stamp panels");
    Panels.ConfigureWorkspaceInsets(43, 0);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"stamp-layers", "Layers", [&]() { ConstructLayers(); }});
    Panels.Register({"stamp-canvas", "2D viewport", [&]() { ConstructCanvas(); }});
    Panels.Register({"stamp-inspector", "Inspector", [&]() { ConstructInspector(); }});
    Panels.ConfigureColumns({{"stamp-layers", 278.f / 1436}, {"stamp-canvas", 820.f / 1436},
                             {"stamp-inspector", 338.f / 1436}});
}

void StampWorkspacePanel::ConfigureInterfaceScale(float Replacement)
{
    Scale = std::isfinite(Replacement) ? std::clamp(Replacement, .75f, 1.5f) : 1;
    Controls.Scale = Scale;
}

void StampWorkspacePanel::ConfigureTypeface(void *Regular, void *Light)
{
    RegularTypeface = Regular;
    LightTypeface = Light;
}

void StampWorkspacePanel::ConfigureDocument(VectorAuthoringDocument Document)
{
    if (!Document.Stamp) throw std::runtime_error("Open a native Stamp document, not a Pattern document.");
    History.Reset(std::move(Document));
    SavedDocument = History.QueryDocument();
    Selection.clear();
    UndoSelections.clear(); RedoSelections.clear();
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    MaskSelection = 0;
    Diagnostic.clear();
}

void StampWorkspacePanel::RefreshLayerAtlas()
{
    // 📝 Mirrors Pattern's own per-document layer atlas exactly (same rasterizer, same bake-on-
    // revision-change contract) — Stamp just never bakes a library-wide gallery atlas alongside it.
    const uint64_t CurrentRevision = QueryRevision();
    if (CurrentRevision == LayerAtlasBakedRevision) return;
    LayerAtlasBakedRevision = CurrentRevision;
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { LayerAtlasPixels.clear(); return; }
    // 📝 The dark gallery-card well (near-black) that the library-browsing grid uses is wrong here:
    // a component's authored ink is routinely dark too (the Text tool's own default fill is
    // {.08,.08,.08,1}, ~rgb(20,20,20)), which was rendering at effectively zero contrast against a
    // ~rgb(20,23,24) well — a seemingly "blank"/missing row preview that was actually rasterizing
    // correctly, just invisibly. Match the real 2D viewport's own board colour (Spectrum(0xede7d7),
    // see ConstructCanvas) instead, so a row's thumbnail always matches what the canvas itself shows.
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(215) << 16 | uint32_t(231) << 8 | uint32_t(237);
    const size_t Slotted = std::min(Document.Layers.size(), LayerAtlasCapacity);
    const std::vector<uint8_t> Blank = RasterizePatternThumbnailSvg(VectorAuthoringDocument{}, LayerAtlasTile, CardWell);
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> FreshCache;
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const uint32_t Token = Document.Layers[Slot].Token;
        try
        {
            std::string Markup = EncodePatternLayerThumbnailSvg(Document, Token, CardWell);
            const auto Cached = LayerThumbnailCache.find(Token);
            if (Cached != LayerThumbnailCache.end() && Cached->second.first == Markup)
                Tiles.push_back(Cached->second.second);
            else
                Tiles.push_back(RasterizePatternLayerThumbnail(Document, Token, LayerAtlasTile, CardWell));
            FreshCache.emplace(Token, std::make_pair(std::move(Markup), Tiles.back()));
        }
        catch (const std::exception &) { Tiles.push_back(Blank); }
    }
    LayerThumbnailCache = std::move(FreshCache);
    // 📝 Mirrors Pattern's own guard: a blit failure (bounded-grid rejection or allocation
    // exhaustion) must never propagate uncaught into the render loop — reordering/adding/removing
    // layers bumps the document revision every time, re-entering this call until it bakes cleanly.
    try { LayerAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LayerAtlasTile, LayerAtlasColumns, CardWell); ++LayerAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("StampLayerAtlasBlitError", Failure.what()); }
}

uint64_t StampWorkspacePanel::QueryLayerAtlasRevision()
{
    RefreshLayerAtlas();
    return LayerAtlasRevision;
}

const std::vector<uint8_t> &StampWorkspacePanel::QueryLayerAtlasPixels()
{
    RefreshLayerAtlas();
    return LayerAtlasPixels;
}

const VectorAuthoringDocument &StampWorkspacePanel::QueryDocument() const
{
    return GestureDraft ? *GestureDraft : History.QueryDocument();
}

PlaneExtent StampWorkspacePanel::QueryControlExtent(const std::string &CaptionText) const
{
    const auto Position = ControlExtents.find(CaptionText);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

PlaneExtent StampWorkspacePanel::QueryPanelExtent(const std::string &CaptionText) const
{
    const auto Position = PanelExtents.find(CaptionText);
    return Position == PanelExtents.end() ? PlaneExtent{} : Position->second;
}

void StampWorkspacePanel::CapturePanel(const char *CaptionText)
{
    const auto Position = ImGui::GetWindowPos(), Extent = ImGui::GetWindowSize();
    PanelExtents[CaptionText] = {Position.x, Position.y, Position.x + Extent.x, Position.y + Extent.y};
}

void StampWorkspacePanel::Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens)
{
    const auto PreviousSelection = GestureSource ? GestureSelection : Selection;
    if (History.Finalize(std::move(Document)))
    {
        UndoSelections.push_back(PreviousSelection);
        if (UndoSelections.size() > 80) UndoSelections.erase(UndoSelections.begin());
        RedoSelections.clear();
    }
    ++GestureRevision;
    Selection = std::move(Tokens);
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(History.QueryDocument().Layers.begin(), History.QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    if (std::find(Selection.begin(), Selection.end(), MaskSelection) == Selection.end()) MaskSelection = 0;
    Diagnostic.clear();
}

void StampWorkspacePanel::EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit)
{
    try
    {
        auto Document = History.QueryDocument();
        Edit(Document);
        Finalize(std::move(Document), Selection);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::TransferSelection(bool Remove)
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture and select components before copying.");
        auto Fragment = ExtractVectorAuthoringSelection(History.QueryDocument(), Selection);
        if (Remove)
        {
            auto Document = History.QueryDocument();
            PartitionVectorAuthoringSelection(Document, Selection);
            Finalize(std::move(Document), {});
        }
        SelectionClipboard = std::move(Fragment);
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::InsertSelection(bool InPlace)
{
    try
    {
        if (Gesture != 0 || MaskSelection || !SelectionClipboard) throw std::runtime_error("Copy components before pasting; finish any active gesture first.");
        auto Document = History.QueryDocument();
        auto Tokens = IntegrateVectorAuthoringSelection(Document, *SelectionClipboard,
            InPlace ? std::array<float, 2>{0, 0} : std::array<float, 2>{18, 18});
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ReplicateSelection()
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture before duplicating a component.");
        auto Document = History.QueryDocument();
        TransformVectorAuthoringSelection(Document, Selection, {});
        const auto Fragment = ExtractVectorAuthoringSelection(Document, Selection);
        auto Tokens = IntegrateVectorAuthoringSelection(Document, Fragment);
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::SelectEditableVectors()
{
    Selection.clear(); MaskSelection = 0;
    for (const auto &Layer : History.QueryDocument().Layers)
        if (QueryVectorAuthoringVisibility(History.QueryDocument(), Layer) && !QueryVectorAuthoringProtection(History.QueryDocument(), Layer))
            Selection.push_back(Layer.Token);
}

void StampWorkspacePanel::ApplySourcePatch(const VectorAuthoringSourcePatch &Patch)
{
    EvaluateEdit([&](auto &Document) { TransformVectorAuthoringSelection(Document, Selection, Patch); });
}

void StampWorkspacePanel::RestoreHistory(bool Redo)
{
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    auto &Source = Redo ? RedoSelections : UndoSelections;
    auto &Destination = Redo ? UndoSelections : RedoSelections;
    if (History.RestoreHistory(Redo) && !Source.empty())
    {
        ++GestureRevision;
        Destination.push_back(Selection);
        Selection = std::move(Source.back());
        Source.pop_back();
        Diagnostic.clear();
    }
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(QueryDocument().Layers.begin(), QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    MaskSelection = 0;
}

void StampWorkspacePanel::Select(uint32_t Token, bool Extend)
{
    if (!Extend) Selection.clear();
    const auto Position = std::find(Selection.begin(), Selection.end(), Token);
    if (Extend && Position != Selection.end()) Selection.erase(Position);
    else Selection.push_back(Token);
    MaskSelection = 0;
}

void StampWorkspacePanel::ConstructPrimitive(uint32_t Category, const char *CaptionText)
{
    try
    {
        auto Document = History.QueryDocument();
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {256, 256, 160, 160};
        Source.Colour = {.8627f, .4627f, .3529f, 1};
        Source.ColourEnd = {.35f, .55f, .45f, 1};
        Source.StrokeColour = {.23f, .24f, .28f, 1};
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        if (Document.Layers.empty()) Document.Name = "Custom stamp";
        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, CaptionText == std::string_view(ShapeCaptions[Category]) ? ShapeNames[Category] : CaptionText);
        Finalize(std::move(Document), {Token});
        SearchText[0] = 0; Filter = 0;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

bool StampWorkspacePanel::Save()
{
    if (!SaveSource) return false;
    try
    {
        if (!SaveSource(EncodeVectorAuthoring(QueryDocument()))) return false;
        SavedDocument = QueryDocument();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool StampWorkspacePanel::RequestClose()
{
    if (GestureSource) { Selection = GestureSelection; GestureSource.reset(); GestureDraft.reset(); Gesture = 0; }
    if (!QueryModified()) return true;
    CloseRequested = true;
    return false;
}

bool StampWorkspacePanel::ConsumeMaterialApplyRequest() noexcept
{
    const bool Requested = ApplyToMaterialRequested;
    ApplyToMaterialRequested = false;
    return Requested;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER AND ADD-COMPONENT MENU
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * Scale));
    ImGui::Begin("Stamp header", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 16 * Scale), QueryDocument().Name.c_str(), 10 * Scale, 0xc5c5b8);
    Caption(ImVec2(Origin.x + 22 * Scale + ImGui::CalcTextSize(QueryDocument().Name.c_str()).x, Origin.y + 19 * Scale), ".stamp", 7 * Scale, 0x67675b);
    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230 * Scale, Width - 620 * Scale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = Index == 0 || Index == 1 || Index == 3;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * Scale));
        ImGui::BeginDisabled(!Available || (Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern));
        if (Controls.Button(Workspaces[Index], Widths[Index] * Scale, Index == 3, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
        }
        Controls.Tooltip(Workspaces[Index], Available ? "Switch workspace without losing your document." : "This native workspace is not hosted yet.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * Scale;
    }
    const auto HeaderButton = [&](const char *Label, float X, const char *Path, bool Enabled)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width + X * Scale, Origin.y + 9 * Scale));
        ImGui::BeginDisabled(!Enabled);
        const bool Pressed = Controls.Button(Label, 28 * Scale, false, Path);
        Controls.Tooltip(Label, Label);
        ImGui::EndDisabled();
        return Pressed;
    };
    if (HeaderButton("Undo", -308, "M3 10L7 6L11 10M7 6V15A5 5 0 0 0 17 15", History.QueryUndoAvailable())) RestoreHistory(false);
    if (HeaderButton("Redo", -272, "M21 10L17 6L13 10M17 6V15A5 5 0 0 1 7 15", History.QueryRedoAvailable())) RestoreHistory(true);
    if (HeaderButton("Save document", -236, "M12 3V16M7 11L12 16L17 11M4 17V21H20V17", bool(SaveSource))) Save();
    if (HeaderButton("Document actions", -200, "M4 12H5M11 12H12M18 12H19", true)) ImGui::OpenPopup("Stamp document actions");
    if (DropdownControlSurface::BeginMenu("Stamp document actions", Scale))
    {
        if (DropdownControlSurface::Option("New stamp", false, Scale))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before creating a new stamp.";
            else { VectorAuthoringDocument Blank; Blank.Stamp = true; Blank.Name = "Untitled stamp"; ConfigureDocument(Blank); }
        }
        if (DropdownControlSurface::Option("Open native stamp", false, Scale, bool(LoadSource)))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before opening another.";
            else try { if (const auto Source = LoadSource()) ConfigureDocument(DecodeVectorAuthoring(*Source)); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        const auto EditOption = [&](const char *CaptionText, bool Enabled)
        {
            const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale, Enabled);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[std::string("Document ") + CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
            return Activated;
        };
        const bool Idle = Gesture == 0 && !MaskSelection;
        const bool Selected = Idle && !Selection.empty();
        bool Editable = Selected;
        for (const auto &Layer : History.QueryDocument().Layers)
            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end()
                && (QueryVectorAuthoringProtection(History.QueryDocument(), Layer) || !QueryVectorAuthoringVisibility(History.QueryDocument(), Layer))) Editable = false;
        if (EditOption("Copy selection", Selected)) TransferSelection(false);
        if (EditOption("Cut selection", Editable)) TransferSelection(true);
        if (EditOption("Paste selection", Idle && bool(SelectionClipboard))) InsertSelection(false);
        if (EditOption("Paste in place", Idle && bool(SelectionClipboard))) InsertSelection(true);
        if (EditOption("Duplicate selection", Editable)) ReplicateSelection();
        if (EditOption("Select all editable", Idle)) SelectEditableVectors();
        const char *OrderCaptions[]{"Send to back", "Send backward", "Bring forward", "Bring to front"};
        for (uint32_t Index = 0; Index < 4; ++Index)
            if (EditOption(OrderCaptions[Index], Editable))
                EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder(Index)); });
        ImGui::TextDisabled("Clipboard: %zu components (editor session)", SelectionClipboard ? SelectionClipboard->Layers.size() : size_t(0));
        ImGui::TextDisabled("Native authoring uses TOML, not HTML JSON.");
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 168 * Scale, Origin.y + 9 * Scale));
    if (Controls.Button("Apply to material", 116 * Scale)) ApplyToMaterialRequested = true;
    Controls.Tooltip(
        "Apply to material",
        "Bake the decal (shape/text/vector + channel compositing) onto the selected material preview. "
        "Stamp edits never touch materials until applied.");
    if (HeaderButton("Close stamp studio", -44, "M6 6L18 18M6 18L18 6", bool(ActivateMaterial)))
        if (RequestClose()) ActivateMaterial();
    Controls.ControlHeight = 30;
    ImGui::End();
}

void StampWorkspacePanel::ConstructAddComponentMenu()
{
    const auto &Snapshot = QueryDocument();
    const bool AtCap = Snapshot.Layers.size() >= 24;
    ImGui::TextDisabled("Shapes");
    for (uint32_t Category = 0; Category < 6; ++Category)
    {
        ImGui::BeginDisabled(AtCap);
        if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { ConstructPrimitive(Category, ShapeCaptions[Category]); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    }
    ImGui::Separator();
    ImGui::TextDisabled("Decal components");
    const auto DecalOption = [&](const char *CaptionText, const char *PopupKey)
    {
        ImGui::BeginDisabled(AtCap);
        const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale);
        ImGui::EndDisabled();
        if (Activated)
        {
            ComponentEditToken = 0;
            ComponentTextBuffer[0] = 0;
            ComponentSvgBuffer.clear();
            ComponentImageBuffer.clear();
            ImGui::CloseCurrentPopup();
            // 📝 Do not OpenPopup(PopupKey) here: this lambda runs inside the "Add a stamp component"
            // dropdown's own popup window, a different ID-stack scope than where the modal is polled.
            // Defer to the stable scope in ConstructLayers() instead (see PendingComponentPopup).
            PendingComponentPopup = PopupKey;
        }
    };
    DecalOption("Add vector (SVG)...", "Add stamp vector");
    DecalOption("Add image...", "Add stamp image");
    ImGui::TextDisabled("Use the Text tool in the canvas toolbar to draw and write a text component.");
    if (AtCap) ImGui::TextWrapped("Stamp decals are capped at 24 components (Pattern allows 64).");
}

// 📝 Finds the largest point size whose word-wrapped glyph-outline layout still fits inside
// {BoxWidth, BoxHeight} (into a throwaway scratch document, never touching the real one) — the
// "fit to box" behaviour an SVG/Illustrator-style text tool gives when you drag out a frame and
// type into it. Text wraps onto as many lines as needed within BoxWidth, so a longer caption grows
// downward with more lines instead of shrinking indefinitely onto one ever-thinner line; the point
// size only shrinks once the wrapped block's own height would overflow the box. Binary-searches the
// real glyph bounds directly (reading the baked instruction's own ink extents, never the selection
// query's visible-canvas-clamped bounds, since this scratch text is never actually placed on a
// canvas) because wrapping makes line count — and so required height — a step function of point
// size, not the simple linear ratio a single probe size could extrapolate from.
float MeasureStampTextFitSize(const std::string &Content, float BoxWidth, float BoxHeight, const std::filesystem::path &ContentRoot)
{
    const std::string TypefacePath = ContentRoot.empty() ? std::string("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf")
        : (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string();
    const float Width = std::max(1.f, BoxWidth), Height = std::max(1.f, BoxHeight);
    const auto Fits = [&](float PointSize)
    {
        VectorAuthoringDocument Scratch; Scratch.Stamp = true;
        const uint32_t Root = AppendStampTextOutline(Scratch, Content, PointSize, {0, 0, 0, 1}, TypefacePath, Width);
        const auto &Instruction = Scratch.Instructions[Root];
        return Instruction.Bounds[2] <= Width && Instruction.Bounds[3] <= Height;
    };
    float Low = 4.f, High = 512.f;
    if (!Fits(Low)) return Low; // Even the smallest permitted size cannot fit; still return something sane.
    for (int Step = 0; Step < 28; ++Step)
    {
        const float Mid = (Low + High) * .5f;
        if (Fits(Mid)) Low = Mid; else High = Mid;
    }
    return std::clamp(Low, 4.f, 512.f);
}

void StampWorkspacePanel::RebakeTextLayer(uint32_t Token, const std::string &Content, float PointSize, float WrapWidth)
{
    if (Content.empty()) { Diagnostic = "Text content cannot be empty."; return; }
    try
    {
        EvaluateEdit([&](auto &Document)
        {
            const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == Token; });
            if (Existing == Document.Layers.end()) throw std::runtime_error("The edited text component no longer exists.");
            const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            const std::array<float, 4> Colour = Document.Instructions[Existing->Root].Colour;
            // 📝 Resolve the glyph-outline typeface off the app's own executable-relative content
            // root when the host configured one, instead of trusting the process's current working
            // directory to happen to be the EngineContent-containing folder.
            const uint32_t NewRoot = ContentRoot.empty() ? AppendStampTextOutline(Document, Content, PointSize, Colour, "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", WrapWidth)
                : AppendStampTextOutline(Document, Content, PointSize, Colour,
                      (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string(), WrapWidth);
            for (auto &Layer : Document.Layers) if (Layer.Token == Token) { Layer.Root = NewRoot; Layer.SourceMarkup = Content; Layer.TextPointSize = PointSize; }
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = OldBounds[0] - NewBounds[0];
            Patch.TranslationY = OldBounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Token}, Patch);
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::CommitTextComposer()
{
    std::string Content = ComponentTextBuffer.data();
    const auto NotSpace = [](unsigned char Character) { return !std::isspace(Character); };
    Content.erase(Content.begin(), std::find_if(Content.begin(), Content.end(), NotSpace));
    Content.erase(std::find_if(Content.rbegin(), Content.rend(), NotSpace).base(), Content.end());
    const uint32_t EditToken = ComponentEditToken;
    const auto Bounds = TextEditBounds;
    Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0;
    if (Content.empty()) { Diagnostic.clear(); return; } // Nothing was written; discard the box silently.
    try
    {
        const float PointSize = MeasureStampTextFitSize(Content, Bounds[2], Bounds[3], ContentRoot);
        if (EditToken) { RebakeTextLayer(EditToken, Content, PointSize, Bounds[2]); return; }
        EvaluateEdit([&](auto &Document)
        {
            if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
            const uint32_t NewRoot = ContentRoot.empty() ? AppendStampTextOutline(Document, Content, PointSize, {.08f, .08f, .08f, 1}, "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", Bounds[2])
                : AppendStampTextOutline(Document, Content, PointSize, {.08f, .08f, .08f, 1},
                      (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string(), Bounds[2]);
            VectorAuthoringLayer Layer;
            Layer.Token = ++Document.TokenClock;
            Layer.Root = NewRoot;
            Layer.Name = Content.size() > 24 ? Content.substr(0, 24) + "..." : Content;
            Layer.ComponentKind = VectorAuthoringComponentKind::Text;
            Layer.SourceMarkup = Content;
            Layer.TextPointSize = PointSize;
            if (Document.Layers.empty()) Document.Name = "Custom stamp";
            Document.Layers.push_back(Layer);
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Layer.Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = Bounds[0] - NewBounds[0];
            Patch.TranslationY = Bounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Layer.Token}, Patch);
            Selection = {Layer.Token};
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ConstructTextComposer(float BoardMinimumX, float BoardMinimumY, float BoardSize)
{
    // 📝 An SVG/Illustrator-style text tool: the box was already drawn (or an existing text
    // component's bounds were loaded) into TextEditBounds, so this just overlays a borderless,
    // auto-focused text field exactly over that box — typing feels like writing directly onto the
    // canvas. Escape cancels; Enter or clicking anywhere else commits (CommitTextComposer bakes the
    // real glyph-outline component, auto-fit to the drawn box, once there is something to bake).
    const ImVec2 Minimum(BoardMinimumX + (TextEditBounds[0] - TextEditBounds[2] * .5f) / 512 * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] - TextEditBounds[3] * .5f) / 512 * BoardSize);
    const ImVec2 Maximum(BoardMinimumX + (TextEditBounds[0] + TextEditBounds[2] * .5f) / 512 * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] + TextEditBounds[3] * .5f) / 512 * BoardSize);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, IM_COL32(255, 255, 255, 240));
    Ink->AddRect(Minimum, Maximum, IM_COL32(134, 164, 112, 255), 0, 0, std::max(1.f, 1.5f * Scale));
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::PushID("StampTextComposer");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.06f, .06f, .05f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    // 📝 Keep the live text already at the size the commit will actually bake at — recomputed (via
    // the same MeasureStampTextFitSize the commit itself calls) only when the composed text changes,
    // never every frame. Without this the field typed at a small, generic caret size and only jumped
    // to the real auto-fit size (often much larger) the instant the box committed.
    const std::string CurrentContent = ComponentTextBuffer.data();
    char BoxKeySuffix[64]{};
    std::snprintf(BoxKeySuffix, sizeof(BoxKeySuffix), "\x01%.3f\x01%.3f", double(TextEditBounds[2]), double(TextEditBounds[3]));
    const std::string CacheKey = CurrentContent + BoxKeySuffix;
    if (CacheKey != TextComposerFitCacheKey)
    {
        TextComposerFitCacheKey = CacheKey;
        try { TextComposerFitPointSize = CurrentContent.empty() ? 0.f : MeasureStampTextFitSize(CurrentContent, TextEditBounds[2], TextEditBounds[3], ContentRoot); }
        catch (const std::exception &) { TextComposerFitPointSize = 0.f; }
    }
    const float LivePixelSize = TextComposerFitPointSize > 0
        ? std::clamp(TextComposerFitPointSize * (BoardSize / 512.f), 8.f, 400.f) : 16.f * Scale;
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface ? RegularTypeface : LightTypeface), LivePixelSize);
    if (TextEditFocusPending) { ImGui::SetKeyboardFocusHere(); TextEditFocusPending = false; }
    // 📝 ImGui's multiline default is backwards for this use case (plain Enter inserts a newline;
    // Ctrl+Enter validates) — CtrlEnterForNewLine flips it so plain Enter commits the component like
    // the spec calls for, and Ctrl+Enter is the escape hatch for an actual multi-line caption.
    const bool EnterCommit = ImGui::InputTextMultiline("##Compose", ComponentTextBuffer.data(), ComponentTextBuffer.size(),
        ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    const bool Deactivated = ImGui::IsItemDeactivated();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0; Diagnostic.clear(); }
    else if (EnterCommit || Deactivated) CommitTextComposer();
}

void StampWorkspacePanel::ConstructVectorPopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp vector", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(ComponentEditToken ? "Replace this component's SVG markup." : "Paste sanitized SVG markup. Every solid-filled shape imports with its own colour and transform; gradients fall back to a neutral grey; scripts and external references are never evaluated.");
        ImGui::Spacing();
        ComponentSvgBuffer.resize(std::max<size_t>(ComponentSvgBuffer.size(), VectorAuthoringSourceMarkupLimit));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampSvgContent", ComponentSvgBuffer.data(), ComponentSvgBuffer.size(), ImVec2(0, 160 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string Markup = ComponentSvgBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                EvaluateEdit([&](auto &Document)
                {
                    std::array<float, 2> AnchorCentre{256, 256};
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited vector component no longer exists.");
                        const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{EditToken});
                        AnchorCentre = {OldBounds[0], OldBounds[1]};
                    }
                    const uint32_t NewRoot = AppendStampVectorImport(Document, Markup);
                    if (EditToken)
                        for (auto &Layer : Document.Layers) if (Layer.Token == EditToken) { Layer.Root = NewRoot; Layer.SourceMarkup = Markup; }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        VectorAuthoringLayer Layer;
                        Layer.Token = ++Document.TokenClock;
                        Layer.Root = NewRoot;
                        Layer.Name = "Imported vector";
                        Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
                        Layer.SourceMarkup = Markup;
                        if (Document.Layers.empty()) Document.Name = "Custom stamp";
                        Document.Layers.push_back(Layer);
                    }
                    ReclaimVectorAuthoringSources(Document);
                    const uint32_t FinalToken = EditToken ? EditToken : Document.Layers.back().Token;
                    const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{FinalToken});
                    VectorAuthoringSourcePatch Patch;
                    Patch.TranslationX = AnchorCentre[0] - NewBounds[0];
                    Patch.TranslationY = AnchorCentre[1] - NewBounds[1];
                    TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{FinalToken}, Patch);
                    Selection = {FinalToken};
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::ConstructImagePopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp image", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(
            "Paste a PNG data URL (data:image/png;base64,...), up to 256\u00d7256 px. The native compute "
            "shader does not sample image pixels yet, so this renders as a placeholder box sized to the "
            "image's own aspect ratio until pixel sampling ships; the embedded bytes still round-trip "
            "through save/load and the HTML Stamp Studio.");
        ImGui::Spacing();
        ComponentImageBuffer.resize(std::max<size_t>(ComponentImageBuffer.size(), 300064));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampImageContent", ComponentImageBuffer.data(), ComponentImageBuffer.size(), ImVec2(0, 100 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string DataUrl = ComponentImageBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                const auto Extent = SniffEmbeddedImageExtent(DataUrl);
                if (!Extent) throw std::runtime_error("That is not a recognizable PNG data URL.");
                const float Aspect = float((*Extent)[0]) / std::max(1.f, float((*Extent)[1]));
                EvaluateEdit([&](auto &Document)
                {
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited image component no longer exists.");
                        auto &Placeholder = Document.Instructions[Existing->Root];
                        const float Height = Placeholder.Bounds[3];
                        Placeholder.Bounds[2] = std::clamp(Height * Aspect, 4.f, 2048.f);
                        Existing->EmbeddedImage = DataUrl;
                    }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        VectorCoverageInstruction Source;
                        Source.Topology[0] = 0;
                        const float Height = 160.f;
                        Source.Bounds = {256, 256, std::clamp(Height * Aspect, 4.f, 2048.f), Height};
                        Source.Colour = {.55f, .55f, .55f, 1};
                        Source.StrokeColour = {.23f, .24f, .28f, 1};
                        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, "Imported image");
                        for (auto &Layer : Document.Layers) if (Layer.Token == Token)
                        {
                            Layer.ComponentKind = VectorAuthoringComponentKind::ImportedImage;
                            Layer.EmbeddedImage = DataUrl;
                        }
                        if (Document.Layers.size() == 1) Document.Name = "Custom stamp";
                        Selection = {Token};
                    }
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructLayers()
{
    CapturePanel("Layers");
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 15 * Scale), "Components", 23 * Scale, 0xd9d9cc, LightTypeface);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * Scale, Origin.y + 14 * Scale));
    const auto &Snapshot = QueryDocument();
    if (Controls.Button("Add stamp component", 28 * Scale, false, PlusPath)) ImGui::OpenPopup("Add a stamp component");
    if (DropdownControlSurface::BeginMenu("Add a stamp component", Scale))
    {
        ConstructAddComponentMenu();
        DropdownControlSurface::EndMenu();
    }
    // 📝 Open the requested add-component popup here, in the same stable ID-stack scope that
    // ConstructVectorPopup/ConstructImagePopup poll with BeginPopupModal below — see
    // PendingComponentPopup for why this cannot happen inside ConstructAddComponentMenu itself.
    if (PendingComponentPopup)
    {
        ImGui::OpenPopup(PendingComponentPopup);
        PendingComponentPopup = nullptr;
    }
    ConstructVectorPopup();
    ConstructImagePopup();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * Scale, Origin.y + 64 * Scale));
    ImGui::BeginChild("Stamp layers body", ImVec2(Width - 16 * Scale, 0), ImGuiChildFlags_None);
    Controls.ControlHeight = 48;
    size_t Visible = 0, Locked = 0;
    for (const auto &Layer : Snapshot.Layers) { if (Layer.Visible) ++Visible; if (Layer.Locked) ++Locked; }
    char CapLabel[32]; std::snprintf(CapLabel, sizeof(CapLabel), "%02zu\n/ 24", Snapshot.Layers.size());
    const size_t Counts[]{Visible, Snapshot.Layers.size() - Visible, Locked};
    const char *Captions[]{"Visible", "Hidden", "Locked"};
    const float StatWidth = (ImGui::GetContentRegionAvail().x - 18 * Scale) / 4;
    Controls.Button(CapLabel, StatWidth, false, {}, true);
    Controls.Tooltip("Component cap", "Stamp decals are capped at 24 components (Pattern allows 64).");
    for (int Index = 0; Index < 3; ++Index)
    {
        ImGui::SameLine(0, 6 * Scale);
        char Label[32]; std::snprintf(Label, sizeof(Label), "%02zu\n%s", Counts[Index], Captions[Index]);
        if (Controls.Button(Label, StatWidth, Filter == Index + 1, {}, true)) Filter = Filter == Index + 1 ? 0 : Index + 1;
    }
    Controls.ControlHeight = 30;
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##FindComponent", "Find a component...", SearchText.data(), SearchText.size());
    ImGui::Dummy(ImVec2(0, 8 * Scale));
    ImGui::TextDisabled("Top components render in front · drag to reorder");
    const char *KindCaptions[]{"", "Text", "Imported vector", "Imported image"};
    for (auto Position = Snapshot.Layers.rbegin(); Position != Snapshot.Layers.rend(); ++Position)
    {
        const auto &Layer = *Position;
        if (Filter == 1 && !Layer.Visible) continue;
        if (Filter == 2 && Layer.Visible) continue;
        if (Filter == 3 && !Layer.Locked) continue;
        if (FoldCaption(Layer.Name).find(FoldCaption(SearchText.data())) == std::string::npos) continue;
        const auto RowOrigin = ImGui::GetCursorScreenPos();
        const auto RowWidth = ImGui::GetContentRegionAvail().x;
        const bool Selected = !MaskSelection && std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end();
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(RowOrigin, ImVec2(RowOrigin.x + RowWidth, RowOrigin.y + 56 * Scale), ImGui::GetColorU32(Spectrum(Selected ? 0x28271f : 0x222220)), 8 * Scale);
        if (Selected) Ink->AddRect(RowOrigin, ImVec2(RowOrigin.x + RowWidth, RowOrigin.y + 56 * Scale), ImGui::GetColorU32(Spectrum(0x5b523b)), 8 * Scale);
        ImGui::PushID(int(Layer.Token));
        Controls.ControlHeight = 48;
        const std::string SelectCaption = "Select:" + std::to_string(Layer.Token);
        if (ImGui::InvisibleButton(SelectCaption.c_str(), ImVec2(std::max(30.f, RowWidth - 66 * Scale), 56 * Scale))) Select(Layer.Token, ImGui::GetIO().KeyShift);
        ControlExtents[SelectCaption] = {RowOrigin.x, RowOrigin.y, RowOrigin.x + RowWidth - 66 * Scale, RowOrigin.y + 56 * Scale};
        const auto &Source = Snapshot.Instructions[Layer.Root];
        Ink->PushClipRect(ImVec2(RowOrigin.x + 32 * Scale, RowOrigin.y), ImVec2(RowOrigin.x + RowWidth - 64 * Scale, RowOrigin.y + 56 * Scale), true);
        Caption(ImVec2(RowOrigin.x + 36 * Scale, RowOrigin.y + 12 * Scale), Layer.Name.c_str(), 11 * Scale, 0xd5d5c4);
        const char *Kind = Layer.ComponentKind != VectorAuthoringComponentKind::Freeform ? KindCaptions[uint32_t(Layer.ComponentKind)]
            : Source.Topology[0] < 6 ? ShapeCaptions[Source.Topology[0]] : "Retained vector";
        Caption(ImVec2(RowOrigin.x + 36 * Scale, RowOrigin.y + 32 * Scale), Kind, 7 * Scale, 0x93937c);
        Ink->PopClipRect();
        // 📝 Row preview: same own-bounds-cropped SVG raster Pattern uses, backed by the GPU atlas
        // once baked; Stamp skips the CPU vector-replay middle tier (atlas bakes within a frame or
        // two of a CPU-side SVG rasterize, so the gap is a brief placeholder, not a real regression).
        {
            // 📝 Square preview (24x24, vertically centred in the 56-tall row) — the box was
            // previously 24 wide by 36 tall, a non-square crop window that distorted every row
            // thumbnail's aspect regardless of what atlas content filled it.
            const ImVec2 ThumbMin(RowOrigin.x + 8 * Scale, RowOrigin.y + 16 * Scale), ThumbMax(RowOrigin.x + 32 * Scale, RowOrigin.y + 40 * Scale);
            StampAtlasImage LayerAtlas{};
            if (QueryLayerThumbnailAtlas) LayerAtlas = QueryLayerThumbnailAtlas();
            const size_t Slot = size_t(&Layer - Snapshot.Layers.data());
            bool ThumbnailDrawn = false;
            if (LayerAtlas.Texture != 0 && Slot < LayerAtlasCapacity && QueryLayerAtlasRevision() > 0)
            {
                const float Step = float(LayerAtlasTile + 2) / LayerAtlasExtent;
                const float Span = float(LayerAtlasTile) / LayerAtlasExtent;
                const float U0 = LayerAtlas.MinimumU + (Slot % LayerAtlasColumns) * Step;
                const float V0 = LayerAtlas.MinimumV + (Slot / LayerAtlasColumns) * Step;
                Ink->AddRectFilled(ThumbMin, ThumbMax, ImGui::GetColorU32(Spectrum(0x161614)), 4 * Scale);
                Ink->AddImageRounded(ImTextureRef(LayerAtlas.Texture), ThumbMin, ThumbMax, ImVec2(U0, V0),
                    ImVec2(U0 + Span, V0 + Span), IM_COL32_WHITE, 4 * Scale);
                ThumbnailDrawn = true;
            }
            if (!ThumbnailDrawn)
                Ink->AddRect(ImVec2(ThumbMin.x + 4 * Scale, ThumbMin.y + 11 * Scale), ImVec2(ThumbMin.x + 18 * Scale, ThumbMin.y + 25 * Scale),
                    ImGui::GetColorU32(Spectrum(0xc7d9b8)), 2 * Scale);
        }
        if (!Layer.Locked && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("FrontierStampComponentLayer", &Layer.Token, sizeof(Layer.Token));
            ImGui::TextUnformatted(Layer.Name.c_str());
            ImGui::EndDragDropSource();
        }
        if (!Layer.Locked && ImGui::BeginDragDropTarget())
        {
            if (const auto *Payload = ImGui::AcceptDragDropPayload("FrontierStampComponentLayer"); Payload && Payload->IsDelivery())
            {
                const auto SourceToken = *static_cast<const uint32_t *>(Payload->Data);
                EvaluateEdit([&](auto &Document)
                {
                    const auto SourcePosition = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Candidate) { return Candidate.Token == SourceToken; });
                    if (SourcePosition == Document.Layers.end() || QueryVectorAuthoringProtection(Document, *SourcePosition)) return;
                    const auto Copy = *SourcePosition;
                    Document.Layers.erase(SourcePosition);
                    const auto Destination = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Candidate) { return Candidate.Token == Layer.Token; });
                    Document.Layers.insert(Destination, Copy);
                });
            }
            ImGui::EndDragDropTarget();
        }
        Controls.ControlHeight = 26;
        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + RowWidth - 60 * Scale, RowOrigin.y + 15 * Scale));
        if (Controls.Button(("Visibility:" + std::to_string(Layer.Token)).c_str(), 25 * Scale, Layer.Visible, VisiblePath)) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Visible = !Candidate.Visible; });
        ImGui::SameLine(0, 2 * Scale);
        if (Controls.Button(("Lock:" + std::to_string(Layer.Token)).c_str(), 25 * Scale, Layer.Locked, LockPath)) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Locked = !Candidate.Locked; });
        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + 62 * Scale));
        ImGui::Dummy(ImVec2(0, 0));
        if (Layer.Mask)
        {
            Controls.ControlHeight = 28;
            if (Controls.Button(("Mask · " + Layer.Name).c_str(), RowWidth - 18 * Scale, MaskSelection == Layer.Token)) { Selection = {Layer.Token}; MaskSelection = Layer.Token; }
        }
        ImGui::PopID();
    }
    Controls.ControlHeight = 30;
    if (Snapshot.Layers.empty()) ImGui::TextWrapped("Add your first component to start a decal.");
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU CANVAS, VIEW NAVIGATION AND GESTURES
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructCanvas()
{
    CapturePanel("Viewport");
    const auto ContentOrigin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const auto At = [&](float X, float Y) { return ImVec2(ContentOrigin.x + X * Scale, ContentOrigin.y + Y * Scale); };
    const float InnerWidth = Width - 48 * Scale;
    ImGui::SetCursorScreenPos(At(24, 17));
    Controls.ControlHeight = 28;
    Controls.Button("Design decal", 76 * Scale, true, {}, true);
    ImGui::SameLine(0, 6 * Scale);
    ImGui::BeginDisabled(); Controls.Button("3D material", 76 * Scale, false, {}, true); Controls.Tooltip("3D material", "Native channel targets and material application are not hosted yet."); ImGui::EndDisabled();
    Caption(At(std::max(200.f, Width / Scale - 212), 26), "Compose one decal. Apply it to a real surface.", 10 * Scale, 0x777766);
    ImGui::GetWindowDrawList()->AddLine(At(24, 60), At(Width / Scale - 24, 60), ImGui::GetColorU32(Spectrum(0x1e1e1e)));
    Caption(At(24, 89), MaskSelection ? "OWNED MASK / 512 UNITS" : "DECAL CANVAS / 512 UNITS", 8 * Scale, 0x8d8d7b);
    auto Name = QueryDocument().Name;
    std::snprintf(NameText.data(), NameText.size(), "%s", Name.c_str());
    ImGui::SetCursorScreenPos(At(24, 105));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushFont(static_cast<ImFont *>(LightTypeface ? LightTypeface : RegularTypeface), 20 * 1.302f * Scale);
    ImGui::SetNextItemWidth(InnerWidth - 150 * Scale);
    if (ImGui::InputText("##StampName", NameText.data(), NameText.size())) EvaluateEdit([&](auto &Document) { Document.Name = NameText.data(); });
    ImGui::PopFont(); ImGui::PopStyleColor();
    Caption(At(Width / Scale - 132, 109), QueryModified() ? "Unsaved changes" : "Document saved", 9 * Scale, 0x969b81);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(24, 161), At(Width / Scale - 24, 211), ImGui::GetColorU32(Spectrum(0x191919)), 8 * Scale);
    ImGui::SetCursorScreenPos(At(32, 170));
    Controls.ControlHeight = 32;
    if (Controls.Button("Select tool (V)", 32 * Scale, Tool == VectorAuthoringTool::Select, SelectPath)) Tool = VectorAuthoringTool::Select;
    Controls.Tooltip("Select", "Select visible GPU contours; Shift-click to extend. Drag once for one undoable source edit.");
    for (uint32_t Category = 0; Category < 2; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        if (Controls.Button(Category ? "Ellipse tool (O)" : "Rectangle tool (R)", 32 * Scale, Tool == (Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle), ShapePaths[Category])) Tool = Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle;
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the canvas to author an editable source shape.");
    }
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Text is a real drag-to-draw tool like Rectangle/Ellipse, not a modal trigger: drag a box on
    // the canvas (a plain click uses a sensible default box), then write directly into it — see the
    // Gesture == 2 (drawing the box) / Gesture == 8 (composing text, ConstructTextComposer) handling
    // further down in this function.
    const bool AtComponentCap = QueryDocument().Layers.size() >= 24;
    ImGui::BeginDisabled(AtComponentCap);
    if (Controls.Button("Text tool (T)", 32 * Scale, Tool == VectorAuthoringTool::Text, TextIconPath)) Tool = VectorAuthoringTool::Text;
    ImGui::EndDisabled();
    Controls.Tooltip("Text", AtComponentCap ? "Stamp decals are capped at 24 components (Pattern allows 64)." : "Drag a box on the canvas and write directly into it. Real glyph-outline geometry, read from the bundled DM Sans font.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pan tool", 32 * Scale, Tool == VectorAuthoringTool::Pan, "M7 12V6Q8 3 9 6V11V4Q11 1 12 4V11V5Q15 2 15 5V12V8Q18 5 18 8V16Q16 23 10 21L3 14Q2 10 5 12L7 14")) Tool = VectorAuthoringTool::Pan;
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("More tools", 78 * Scale)) ImGui::OpenPopup("Stamp more tools");
    if (DropdownControlSurface::BeginMenu("Stamp more tools", Scale))
    {
        for (uint32_t Category = 2; Category < 6; ++Category)
        {
            if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { Tool = VectorAuthoringTool(uint32_t(VectorAuthoringTool::Diamond) + Category - 2); ImGui::CloseCurrentPopup(); }
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        }
        Controls.Toggle("Transparent canvas", TransparentCanvas);
        Controls.Toggle("Show grid", GridEnabled);
        Controls.Toggle("Snap to grid", SnappingEnabled);
        DropdownControlSurface::EndMenu();
    }
    Controls.ControlHeight = 24;
    Ink->AddRectFilled(At(24, 211), At(Width / Scale - 24, 249), ImGui::GetColorU32(Spectrum(0x20241d)));
    Caption(At(36, 226), "VECTOR CANVAS    512 \u00d7 512", 8 * Scale, 0xa5b394);
    ImGui::SetCursorScreenPos(At(Width / Scale - 219, 218));
    if (Controls.Button("Zoom out", 26 * Scale, false, "M5 12H19")) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.f / 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    char ZoomText[32]; std::snprintf(ZoomText, sizeof(ZoomText), "%.0f%%", Zoom * 100);
    Controls.Button(ZoomText, 62 * Scale, false, {}, true);
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Raised from 8x (800%) to 10x (1000%): the bake ceiling both canvases clamp their
    // on-screen device-pixel request to is 4096, and the 360-unit base board at 1000% zoom
    // (3600 device pixels at 1x DPI) still sits comfortably under that, so this stays crisp.
    if (Controls.Button("Zoom in", 26 * Scale, false, PlusPath)) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Fit", 46 * Scale, false, {}, true)) { Zoom = 1; Pan = {}; }
    Controls.ControlHeight = 30;
    const ImVec2 Minimum = At(25, 248), Maximum = At(Width / Scale - 25, 678);
    Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x242c26)));
    const float BoardSize = std::max(32.f, std::min(InnerWidth - 120 * Scale, 360 * Scale)) * Zoom;
    const ImVec2 Centre((Minimum.x + Maximum.x) * .5f + Pan.X, (Minimum.y + Maximum.y) * .5f + Pan.Y);
    const ImVec2 BoardMinimum(Centre.x - BoardSize * .5f, Centre.y - BoardSize * .5f), BoardMaximum(Centre.x + BoardSize * .5f, Centre.y + BoardSize * .5f);
    BoardExtent = {BoardMinimum.x, BoardMinimum.y, BoardMaximum.x, BoardMaximum.y};
    Ink->PushClipRect(Minimum, Maximum, true);
    Ink->AddRectFilled(BoardMinimum, BoardMaximum, ImGui::GetColorU32(Spectrum(0xede7d7)));
    if (TransparentCanvas)
    {
        for (float Y = BoardMinimum.y; Y < BoardMaximum.y; Y += 12 * Scale)
            for (float X = BoardMinimum.x; X < BoardMaximum.x; X += 12 * Scale)
                if ((int((X - BoardMinimum.x) / (12 * Scale)) + int((Y - BoardMinimum.y) / (12 * Scale))) % 2)
                    Ink->AddRectFilled(ImVec2(X, Y), ImVec2(std::min(X + 12 * Scale, BoardMaximum.x), std::min(Y + 12 * Scale, BoardMaximum.y)), ImGui::GetColorU32(Spectrum(0xb5b8ae)));
    }
    Ink->AddRectFilled(Minimum, ImVec2(Maximum.x, Minimum.y + 21 * Scale), ImGui::GetColorU32(Spectrum(0x1c211a)));
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + 23 * Scale, Maximum.y), ImGui::GetColorU32(Spectrum(0x1c211a)));
    for (int Coordinate = -256; Coordinate <= 768; Coordinate += 64)
    {
        const float Horizontal = BoardMinimum.x + float(Coordinate) / 512 * BoardSize;
        const float Vertical = BoardMinimum.y + float(Coordinate) / 512 * BoardSize;
        char Label[16]; std::snprintf(Label, sizeof(Label), "%d", Coordinate);
        if (Horizontal > Minimum.x + 24 * Scale && Horizontal < Maximum.x)
        {
            Ink->AddLine(ImVec2(Horizontal, Minimum.y), ImVec2(Horizontal, Minimum.y + 20 * Scale), IM_COL32(80, 94, 66, 170));
            Caption(ImVec2(Horizontal + 4 * Scale, Minimum.y + 4 * Scale), Label, 7 * Scale, 0x879576);
        }
        if (Vertical > Minimum.y + 22 * Scale && Vertical < Maximum.y)
        {
            Ink->AddLine(ImVec2(Minimum.x, Vertical), ImVec2(Minimum.x + 22 * Scale, Vertical), IM_COL32(80, 94, 66, 170));
            Caption(ImVec2(Minimum.x + 3 * Scale, Vertical + 3 * Scale), Label, 7 * Scale, 0x879576);
        }
    }
    const auto &Snapshot = QueryDocument();
    const uint64_t HistoryRevision = History.QueryRevision();
    if (!CachedCoverageValid || CachedCoverageMask != MaskSelection || CachedHistoryRevision != HistoryRevision ||
        CachedGestureRevision != GestureRevision)
    {
        CachedCoverageProgram = ConstructVectorAuthoringCoverage(Snapshot);
        if (MaskSelection)
        {
            const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == MaskSelection; });
            if (Position != Snapshot.Layers.end() && Position->Mask)
                CachedCoverageProgram = {{*Position->Mask}, Snapshot.Segments, 0};
        }
        CachedCoverageMask = MaskSelection;
        CachedHistoryRevision = HistoryRevision;
        CachedGestureRevision = GestureRevision;
        CachedCoverageValid = true;
    }
    const float DisplayPixels = BoardSize * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t RequestedExtent = uint32_t(std::clamp(std::ceil(DisplayPixels), 1.f, 4096.f));
    const auto Texture = QueryCoverage ? QueryCoverage(CachedCoverageProgram, RequestedExtent) : 0;
    if (Texture) Ink->AddImage(ImTextureRef(Texture), BoardMinimum, BoardMaximum);
    else Caption(ImVec2(BoardMinimum.x + 12 * Scale, BoardMinimum.y + 12 * Scale), "GPU coverage unavailable", 11 * Scale, 0x68695a);
    const bool BakePending = Texture && QueryCoverageReady && !QueryCoverageReady();
    if (BakePending) { if (PendingBakeSince < 0) PendingBakeSince = ImGui::GetTime(); }
    else PendingBakeSince = -1;
    if (BakePending && PendingBakeSince >= 0 && ImGui::GetTime() - PendingBakeSince > 0.15)
    {
        const char *Badge = "Baking decal...";
        const ImVec2 BadgeExtent = ImGui::CalcTextSize(Badge);
        const ImVec2 BadgeMaximum(BoardMaximum.x - 10 * Scale, BoardMinimum.y + 10 * Scale + BadgeExtent.y + 10 * Scale);
        const ImVec2 BadgeMinimum(BadgeMaximum.x - BadgeExtent.x - 20 * Scale, BoardMinimum.y + 10 * Scale);
        Ink->AddRectFilled(BadgeMinimum, BadgeMaximum, IM_COL32(26, 30, 24, 225), 6 * Scale);
        Ink->AddRect(BadgeMinimum, BadgeMaximum, IM_COL32(191, 216, 169, 255), 6 * Scale);
        Ink->AddText(ImVec2(BadgeMinimum.x + 10 * Scale, BadgeMinimum.y + 5 * Scale), IM_COL32(212, 232, 192, 255), Badge);
    }
    if (GridEnabled)
    {
        for (float Coordinate = 0; Coordinate <= 512; Coordinate += 64)
        {
            const float Pixel = Coordinate / 512 * BoardSize;
            Ink->AddLine(ImVec2(BoardMinimum.x + Pixel, BoardMinimum.y), ImVec2(BoardMinimum.x + Pixel, BoardMaximum.y), IM_COL32(75, 90, 70, 65));
            Ink->AddLine(ImVec2(BoardMinimum.x, BoardMinimum.y + Pixel), ImVec2(BoardMaximum.x, BoardMinimum.y + Pixel), IM_COL32(75, 90, 70, 65));
        }
    }
    std::array<int, 2> HoveredResize{};
    bool HoveredRotation = false;
    if (!Selection.empty() && !MaskSelection)
    {
        try
        {
            const auto Extent = QueryVectorSelectionExtent(Snapshot, Selection);
            const auto &Bounds = Extent.Bounds;
            const auto ScreenPoint = [&](float X, float Y)
            {
                const float θ = Extent.Rotation;
                return ImVec2(BoardMinimum.x + (Bounds[0] + X * std::cos(θ) - Y * std::sin(θ)) / 512 * BoardSize,
                              BoardMinimum.y + (Bounds[1] + X * std::sin(θ) + Y * std::cos(θ)) / 512 * BoardSize);
            };
            const std::array<ImVec2, 4> Corners{ScreenPoint(-Bounds[2] * .5f, -Bounds[3] * .5f),
                ScreenPoint(Bounds[2] * .5f, -Bounds[3] * .5f), ScreenPoint(Bounds[2] * .5f, Bounds[3] * .5f),
                ScreenPoint(-Bounds[2] * .5f, Bounds[3] * .5f)};
            Ink->AddPolyline(Corners.data(), 4, IM_COL32(191, 216, 169, 255), ImDrawFlags_Closed, 1.3f * Scale);
            bool Editable = true;
            for (const auto &Layer : Snapshot.Layers)
                if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() &&
                    (QueryVectorAuthoringProtection(Snapshot, Layer) || !QueryVectorAuthoringVisibility(Snapshot, Layer))) Editable = false;
            if (Editable && Tool == VectorAuthoringTool::Select)
            {
                const auto RegisterGrip = [&](const char *Name, ImVec2 Point)
                {
                    const float Radius = 6 * Scale;
                    ControlExtents[Name] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
                    const auto Mouse = ImGui::GetIO().MousePos;
                    return std::abs(Mouse.x - Point.x) <= Radius && std::abs(Mouse.y - Point.y) <= Radius;
                };
                const ImVec2 Rotate = ScreenPoint(0, -Bounds[3] * .5f - 29 * Scale * 512 / BoardSize);
                Ink->AddLine(ScreenPoint(0, -Bounds[3] * .5f), Rotate, IM_COL32(191, 216, 169, 255));
                Ink->AddCircleFilled(Rotate, 4 * Scale, IM_COL32(212, 232, 192, 255));
                Ink->AddCircle(Rotate, 4 * Scale, IM_COL32(40, 56, 43, 255));
                HoveredRotation = RegisterGrip("Rotate selection", Rotate);
                for (int Y = -1; Y <= 1; ++Y) for (int X = -1; X <= 1; ++X)
                {
                    if (!X && !Y) continue;
                    const ImVec2 Point = ScreenPoint(float(X) * Bounds[2] * .5f, float(Y) * Bounds[3] * .5f);
                    Ink->AddRectFilled(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(203, 225, 184, 255), Scale);
                    Ink->AddRect(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(40, 60, 40, 255), Scale);
                    const std::string Name = std::string("Resize:") + (Y < 0 ? "n" : Y > 0 ? "s" : "") + (X < 0 ? "w" : X > 0 ? "e" : "");
                    if (RegisterGrip(Name.c_str(), Point)) HoveredResize = {X, Y};
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (Gesture == 4)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / 512 * BoardSize, BoardMinimum.y + PointerOrigin.Y / 512 * BoardSize);
        const auto Mouse = ImGui::GetIO().MousePos;
        const ImVec2 Lower(std::min(Origin.x, Mouse.x), std::min(Origin.y, Mouse.y));
        const ImVec2 Upper(std::max(Origin.x, Mouse.x), std::max(Origin.y, Mouse.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(191, 216, 169, 24));
        Ink->AddRect(Lower, Upper, IM_COL32(191, 216, 169, 220));
    }
    if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / 512 * BoardSize, BoardMinimum.y + PointerOrigin.Y / 512 * BoardSize);
        const ImVec2 Current(BoardMinimum.x + TextBoxCurrent.X / 512 * BoardSize, BoardMinimum.y + TextBoxCurrent.Y / 512 * BoardSize);
        const ImVec2 Lower(std::min(Origin.x, Current.x), std::min(Origin.y, Current.y));
        const ImVec2 Upper(std::max(Origin.x, Current.x), std::max(Origin.y, Current.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(134, 164, 112, 40));
        Ink->AddRect(Lower, Upper, IM_COL32(134, 164, 112, 230));
    }
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::InvisibleButton("Stamp canvas", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    ControlExtents["Stamp canvas"] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    // 📝 Submitted after the invisible canvas button (not during the earlier Ink-drawing pass) so the
    // composer's real InputTextMultiline sits on top for mouse hit-testing — otherwise the button
    // underneath it would steal clicks meant for placing the text cursor inside the box.
    if (Gesture == 8) ConstructTextComposer(BoardMinimum.x, BoardMinimum.y, BoardSize);
    const auto DocumentPoint = [&](ImVec2 Point, bool Snap = true)
    {
        PlanePoint Position{(Point.x - BoardMinimum.x) / BoardSize * 512, (Point.y - BoardMinimum.y) / BoardSize * 512};
        if (SnappingEnabled && Snap) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        return Position;
    };
    if (Hovered && Input.MouseWheel != 0 && Gesture == 0)
    {
        const float Previous = Zoom;
        Zoom = ApplyWorkspaceWheelZoom(Zoom, Input.MouseWheel);
        Pan.X += (Input.MousePos.x - Centre.x) * (1 - Zoom / Previous);
        Pan.Y += (Input.MousePos.y - Centre.y) * (1 - Zoom / Previous);
    }
    if (Hovered && Tool == VectorAuthoringTool::Select)
    {
        if (HoveredRotation || Gesture == 6) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (HoveredResize[0] || HoveredResize[1]) ImGui::SetMouseCursor(!HoveredResize[0] ? ImGuiMouseCursor_ResizeNS : !HoveredResize[1] ? ImGuiMouseCursor_ResizeEW : HoveredResize[0] == HoveredResize[1] ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
    }
    bool TextReEditEntered = false;
    if (Hovered && Gesture == 0 && Tool == VectorAuthoringTool::Select && ImGui::IsMouseDoubleClicked(0) && QuerySelectionToken)
    {
        // 📝 Double-clicking an existing text component re-enters the same on-canvas composer a
        // freshly drawn box would, seeded with its current content/bounds, instead of only being
        // editable through the Inspector.
        try
        {
            const auto &Source = History.QueryDocument();
            if (const auto Hit = QuerySelectionToken(Source, DocumentPoint(Input.MousePos, false)); Hit && *Hit)
            {
                const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                if (Position != Source.Layers.end() && Position->ComponentKind == VectorAuthoringComponentKind::Text &&
                    !QueryVectorAuthoringProtection(Source, *Position) && QueryVectorAuthoringVisibility(Source, *Position))
                {
                    TextEditBounds = QueryVectorAuthoringSelectionBounds(Source, std::array<uint32_t, 1>{*Hit});
                    ComponentEditToken = *Hit;
                    std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", Position->SourceMarkup.c_str());
                    Selection = {*Hit};
                    Gesture = 8; TextEditFocusPending = true;
                    TextReEditEntered = true;
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (Hovered && Gesture == 0 && !TextReEditEntered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(2)))
    {
        GestureSelection = Selection;
        PointerOrigin = DocumentPoint(Input.MousePos, false);
        if (ImGui::IsMouseClicked(2) || Tool == VectorAuthoringTool::Pan || ImGui::IsKeyDown(ImGuiKey_Space)) { Gesture = 3; PanOrigin = Pan; }
        else if (MaskSelection) { /* Mask descriptor interaction remains in the Inspector. */ }
        else if (HoveredRotation || HoveredResize[0] || HoveredResize[1])
        {
            Gesture = HoveredRotation ? 6 : 5;
            ResizeDirection = HoveredResize;
            GestureSource = History.QueryDocument();
        }
        else if (Tool != VectorAuthoringTool::Select)
        {
            Gesture = 2;
            GestureSource = History.QueryDocument();
            GestureDraft.reset();
            GesturePrimitiveToken = 0;
            TextBoxCurrent = PointerOrigin; // Seeds the Text tool's drawn box so a plain click (no drag) still has a point to measure from.
        }
        else
        {
            SelectionExtend = Input.KeyShift;
            GestureSource = History.QueryDocument();
            SelectionPointer = PointerOrigin; SelectionTravel = 0; SelectionReleased = false;
            Gesture = 7;
        }
    }
    if (Gesture == 7 && (ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()))
    {
        Selection = GestureSelection; GestureSource.reset(); Gesture = 0;
    }
    if (Gesture == 7)
    {
        if (!SelectionReleased)
        {
            SelectionPointer = DocumentPoint(Input.MousePos, false);
            SelectionTravel = std::max(SelectionTravel, std::hypot(SelectionPointer.X - PointerOrigin.X, SelectionPointer.Y - PointerOrigin.Y) * BoardSize / 512);
            SelectionReleased = !ImGui::IsMouseDown(0);
        }
        try
        {
            if (!GestureSource || History.QueryDocument() != *GestureSource)
                throw std::runtime_error("Source changed while GPU selection was pending; click again.");
            if (!QuerySelectionToken) throw std::runtime_error("GPU contour selection is unavailable; use the layer list.");
            if (const auto Hit = QuerySelectionToken(*GestureSource, PointerOrigin))
            {
                if (*Hit)
                {
                    const auto Position = std::find_if(GestureSource->Layers.begin(), GestureSource->Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                    if (Position == GestureSource->Layers.end() || QueryVectorAuthoringProtection(*GestureSource, *Position) || !QueryVectorAuthoringVisibility(*GestureSource, *Position))
                        throw std::runtime_error("GPU selection token no longer authenticates to an editable source.");
                    if (SelectionExtend || std::find(Selection.begin(), Selection.end(), *Hit) == Selection.end()) Select(*Hit, SelectionExtend);
                    Gesture = Selection.empty() ? 0 : 1;
                    if (Selection.empty()) GestureSource.reset();
                }
                else
                {
                    MarqueeAdditive = SelectionExtend;
                    Gesture = 4;
                    if (!MarqueeAdditive) Selection.clear();
                }
                Diagnostic.clear();
            }
        }
        catch (const std::exception &Failure)
        {
            Diagnostic = Failure.what(); GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision;
        }
    }
    if (Gesture == 3 && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(2)))
    {
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        Pan.X += Δ.X; Pan.Y += Δ.Y;
    }
    if ((Gesture == 1 || Gesture == 2 || Gesture == 5 || Gesture == 6) && (ImGui::IsMouseDragging(0, 2) || (Gesture == 1 && SelectionReleased && SelectionTravel >= 2 * Scale)) && GestureSource)
    {
        auto Position = DocumentPoint(Input.MousePos, Gesture != 6);
        if (Gesture == 1 && SelectionReleased)
        {
            Position = SelectionPointer;
            if (SnappingEnabled) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        }
        try
        {
            if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
            {
                // 📝 Text never creates a GestureDraft primitive while dragging; it only tracks the
                // box endpoint for the preview rectangle below and for sizing the composer on release.
                TextBoxCurrent = Position;
            }
            else if (Gesture == 2)
            {
                VectorCoverageInstruction Source;
                Source.Topology[0] = Tool == VectorAuthoringTool::Rectangle ? 0u : Tool == VectorAuthoringTool::Ellipse ? 1u : uint32_t(Tool) - uint32_t(VectorAuthoringTool::Diamond) + 2;
                Source.Bounds = {(Position.X + PointerOrigin.X) * .5f, (Position.Y + PointerOrigin.Y) * .5f,
                    std::max(1.f, std::abs(Position.X - PointerOrigin.X)), std::max(1.f, std::abs(Position.Y - PointerOrigin.Y))};
                Source.Colour = {.8627f, .4627f, .3529f, 1};
                Source.StrokeColour = {.23f, .24f, .28f, 1};
                Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
                if (!GestureDraft)
                {
                    auto Draft = *GestureSource;
                    GesturePrimitiveToken = ConstructVectorAuthoringPrimitive(Draft, Source, ShapeNames[Source.Topology[0]]);
                    GestureDraft = std::move(Draft);
                    Selection = {GesturePrimitiveToken};
                }
                else
                {
                    auto &Draft = *GestureDraft;
                    const auto Layer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Candidate)
                        { return Candidate.Token == GesturePrimitiveToken; });
                    if (Layer == Draft.Layers.end()) throw std::runtime_error("The in-progress shape lost its authenticated layer token.");
                    Draft.Instructions.at(Layer->Root).Bounds = Source.Bounds;
                }
                ++GestureRevision;
            }
            else
            {
                auto Draft = *GestureSource;
                if (Gesture == 1)
                {
                    VectorAuthoringSourcePatch Patch; Patch.TranslationX = Position.X - PointerOrigin.X; Patch.TranslationY = Position.Y - PointerOrigin.Y;
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                else
                {
                    const auto Patch = Gesture == 5
                        ? EvaluateVectorSelectionResize(Draft, Selection, ResizeDirection, {Position.X, Position.Y}, Input.KeyShift, Input.KeyAlt)
                        : EvaluateVectorSelectionRotation(Draft, Selection, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, Input.KeyShift || SnappingEnabled);
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            Diagnostic.clear();
        }
        catch (const std::exception &Failure) { GestureDraft.reset(); GesturePrimitiveToken = 0; ++GestureRevision; Diagnostic = Failure.what(); }
    }
    if ((ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()) && Gesture)
    {
        Selection = GestureSelection; GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    }
    else if (Gesture == 2 && Tool == VectorAuthoringTool::Text && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        // 📝 The box is drawn; hand off to the on-canvas composer (Gesture 8) instead of the generic
        // shape-finalize path below, which only knows how to finalize a GestureDraft primitive.
        constexpr float MinimumBoxExtent = 8.f, DefaultBoxWidth = 220.f, DefaultBoxHeight = 64.f;
        const float Width = std::abs(TextBoxCurrent.X - PointerOrigin.X), Height = std::abs(TextBoxCurrent.Y - PointerOrigin.Y);
        if (Width < MinimumBoxExtent && Height < MinimumBoxExtent)
            TextEditBounds = {PointerOrigin.X, PointerOrigin.Y, DefaultBoxWidth, DefaultBoxHeight};
        else
            TextEditBounds = {(PointerOrigin.X + TextBoxCurrent.X) * .5f, (PointerOrigin.Y + TextBoxCurrent.Y) * .5f,
                std::max(MinimumBoxExtent, Width), std::max(MinimumBoxExtent, Height)};
        ComponentEditToken = 0;
        ComponentTextBuffer[0] = 0;
        GestureSource.reset(); Gesture = 8; TextEditFocusPending = true;
    }
    // 📝 Gesture 7 (marquee) and Gesture 8 (on-canvas text composer) both legitimately persist across
    // many idle-mouse frames after the mouse that opened them is released — Gesture 8 especially, since
    // the whole point is to keep the InputTextMultiline mounted and focused while the user types with the
    // mouse sitting untouched. Excluding only 7 here let this catch-all drag-release cleanup fire on the
    // very next frame after the Text tool handed off to the composer, snapping Gesture back to 0 (with no
    // GestureDraft to finalize) before a single character could be typed — the composer box vanished
    // instantly, nothing reached the layer stack, exactly the reported "Text tool is broken" symptom.
    else if (Gesture && Gesture != 7 && Gesture != 8 && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        if (Gesture == 4 && GestureSource)
        {
            const auto Position = SelectionReleased ? SelectionPointer : DocumentPoint(Input.MousePos, false);
            if (std::hypot(Position.X - PointerOrigin.X, Position.Y - PointerOrigin.Y) * BoardSize / 512 >= 3 * Scale)
                Selection = QueryVectorSelectionRegion(*GestureSource, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, MarqueeAdditive ? std::span<const uint32_t>(GestureSelection) : std::span<const uint32_t>{});
        }
        if (GestureDraft) { auto Document = std::move(*GestureDraft); GestureDraft.reset(); Finalize(std::move(Document), Selection); }
        GestureSource.reset(); Gesture = 0;
    }
    Ink->AddRectFilled(At(24, 678), At(Width / Scale - 24, 711), ImGui::GetColorU32(Spectrum(0x191c16)), 6 * Scale);
    Caption(At(35, 689), "Marquee to select · Shift constrain · Alt centred resize · Space pan", 8 * Scale, 0x909a7c);
    Caption(At(Width / Scale - 114, 689), (std::to_string(Selection.size()) + " selected").c_str(), 8 * Scale, 0xc6d2b5);
    Caption(At(24, 733), "Decal preview on a plain surface", 10 * Scale, 0xc3cbb5);
    const ImVec2 PreviewMinimum = At(24, 753), PreviewMaximum = At(Width / Scale - 24, 885);
    Ink->AddRectFilled(PreviewMinimum, PreviewMaximum, ImGui::GetColorU32(Spectrum(0xede7d7)), 8 * Scale);
    if (Texture && !MaskSelection)
    {
        // 📝 One centred image, not a tiled repeat: a decal is a single applied sticker, not a
        // seamless pattern, so this mirrors how "Apply to material" will actually place it.
        const float Edge = std::min(PreviewMaximum.x - PreviewMinimum.x, PreviewMaximum.y - PreviewMinimum.y) * .72f;
        const ImVec2 Centre((PreviewMinimum.x + PreviewMaximum.x) * .5f, (PreviewMinimum.y + PreviewMaximum.y) * .5f);
        Ink->AddImage(ImTextureRef(Texture), ImVec2(Centre.x - Edge * .5f, Centre.y - Edge * .5f), ImVec2(Centre.x + Edge * .5f, Centre.y + Edge * .5f));
    }
    ImGui::SetCursorScreenPos(PreviewMaximum);
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION-AWARE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructInspector()
{
    CapturePanel("Inspector");
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Position.x + 18 * Scale, Position.y + 30 * Scale));
    ImGui::BeginChild("Stamp inspector scroll", ImVec2(ImGui::GetContentRegionAvail().x - 18 * Scale, 0));
    const auto Snapshot = QueryDocument();
    const auto SelectedTokens = Selection;
    const auto Selected = [&](const auto &Layer) { return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end(); };
    const auto First = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), Selected);
    const bool Protected = std::any_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Selected(Layer) && QueryVectorAuthoringProtection(Snapshot, Layer); });
    if (First == Snapshot.Layers.end())
    {
        Controls.BeginCard("Your next component", true);
        ImGui::TextWrapped("Pick a tool and draw a shape, or add Text/Vector/Image with the + beside Components.");
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    if (!MaskSelection && Selection.size() == 1 && First->ComponentKind != VectorAuthoringComponentKind::Freeform)
    {
        const bool IsText = First->ComponentKind == VectorAuthoringComponentKind::Text;
        const bool IsVector = First->ComponentKind == VectorAuthoringComponentKind::ImportedVector;
        Controls.BeginCard(IsText ? "Text content" : IsVector ? "Imported vector" : "Imported image", true);
        ImGui::BeginDisabled(Protected);
        if (IsText)
        {
            // 📝 Text properties live inline here (content + font size), not behind a modal: editing
            // either commits (rebakes the real glyph outline, anchor held at its current centre) the
            // moment you leave the field, via IsItemDeactivatedAfterEdit — not on every keystroke.
            if (InspectorTextToken != First->Token)
            {
                std::snprintf(InspectorTextBuffer.data(), InspectorTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                InspectorTextToken = First->Token;
            }
            ImGui::TextDisabled("Content");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##StampTextInspectorContent", InspectorTextBuffer.data(), InspectorTextBuffer.size(), ImVec2(0, 56 * Scale));
            const bool ContentCommit = ImGui::IsItemDeactivatedAfterEdit();
            float PointSize = First->TextPointSize;
            Controls.Number("Font size", PointSize, 4, 512);
            const bool SizeCommit = ImGui::IsItemDeactivatedAfterEdit();
            if (ContentCommit || SizeCommit) RebakeTextLayer(First->Token, InspectorTextBuffer.data(), PointSize);
            if (Controls.Button("Edit on canvas...", 160 * Scale))
            {
                TextEditBounds = QueryVectorAuthoringSelectionBounds(Snapshot, std::array<uint32_t, 1>{First->Token});
                ComponentEditToken = First->Token;
                std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                Gesture = 8; TextEditFocusPending = true;
            }
        }
        else if (IsVector)
        {
            ImGui::Text("%zu bytes of SVG markup", First->SourceMarkup.size());
            if (Controls.Button("Replace SVG...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentSvgBuffer = First->SourceMarkup;
                ImGui::OpenPopup("Add stamp vector");
            }
            ImGui::TextWrapped("Gradients fall back to a neutral grey; clip-paths and masks are not ported.");
        }
        else
        {
            ImGui::Text("%zu bytes embedded (PNG data URL)", First->EmbeddedImage.size());
            ImGui::TextWrapped("Pixel sampling is not implemented yet; this renders as a placeholder box.");
            if (Controls.Button("Replace image...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentImageBuffer = First->EmbeddedImage;
                ImGui::OpenPopup("Add stamp image");
            }
        }
        ImGui::EndDisabled();
        Controls.EndCard();
        ConstructVectorPopup();
        ConstructImagePopup();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    }
    if (!MaskSelection)
    {
        Controls.BeginCard(Selection.size() == 1 ? "Selected component" : "Selected components");
        ImGui::BeginDisabled(Protected);
        ImGui::TextUnformatted(Selection.size() == 1 ? "Align to canvas" : "Align selection");
        for (int Axis = 0; Axis < 2; ++Axis)
        {
            for (int Boundary = 0; Boundary < 3; ++Boundary)
            {
                if (Boundary) ImGui::SameLine(0, 6 * Scale);
                const char *Captions[]{"Align left", "Centre horizontally", "Align right", "Align top", "Centre vertically", "Align bottom"};
                if (Controls.Button(Captions[Axis * 3 + Boundary], 30 * Scale, false, Axis == 0 ? "M3 3V21M7 7H16V17H7Z" : "M3 3H21M7 7H17V16H7Z"))
                {
                    const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
                    const float Centre = Axis ? Bounds[1] : Bounds[0], Extent = Axis ? Bounds[3] : Bounds[2];
                    const float Destination = Boundary == 0 ? Extent * .5f : Boundary == 1 ? 256 : 512 - Extent * .5f;
                    VectorAuthoringSourcePatch Patch; if (Axis) Patch.TranslationY = Destination - Centre; else Patch.TranslationX = Destination - Centre;
                    ApplySourcePatch(Patch);
                }
            }
        }
        if (Selection.size() >= 2)
        {
            bool Eligible = !Protected && std::all_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return !Selected(Layer) || !Layer.Mask; });
            if (const auto Operation = ConstructVectorBooleanControls(Eligible))
            {
                try { auto Document = Snapshot; const auto Token = IntegrateVectorAuthoringBoolean(Document, Selection, VectorCoverageCategory(*Operation)); Finalize(std::move(Document), {Token}); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
        }
        ImGui::Spacing();
        if (Controls.Button("Duplicate selection", 30 * Scale, false, CopyPath)) ReplicateSelection();
        Controls.Tooltip("Duplicate", "Retain editable source, mask and channel settings in an independent copy.");
        ImGui::SameLine(0, 6 * Scale);
        if (Controls.Button("Delete selection", 30 * Scale, false, TrashPath)) EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
        Controls.ControlHeight = 48;
        if (Controls.Button(("Surface channels · " + std::to_string(Selection.size()) + " selected").c_str(), ImGui::GetContentRegionAvail().x, ChannelsOpen, {}, true)) ChannelsOpen = !ChannelsOpen;
        Controls.ControlHeight = 30;
        if (ChannelsOpen)
        {
            ImGui::TextWrapped("For a plastic/film sticker over a decal: enable Transmission + Clearcoat + Roughness on the "
                "film layer, and Height/Normal on the decal layer beneath it for embossed text or bump.");
            if (Selection.size() > 1)
                ImGui::TextDisabled("Toggling a channel applies the displayed setting to every selected component.");
            const char *Channels[]{"Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Ambient occlusion", "Transmission", "Clearcoat"};
            ImGui::BeginDisabled(Protected);
            for (size_t Index = 0; Index < std::size(Channels); ++Index)
            {
                bool Enabled = First->SurfaceChannels[Index].Enabled;
                if (Controls.Toggle(Channels[Index], Enabled))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceChannels[Index].Enabled = Enabled;
                    });
                float Strength = First->SurfaceChannels[Index].Strength;
                const std::string StrengthCaption = std::string(Channels[Index]) + " strength";
                const float Maximum = Index == size_t(VectorAuthoringSurfaceChannel::Normal) ? 4.f : 1.f;
                if (Controls.Range(StrengthCaption.c_str(), Strength, 0.f, Maximum))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceChannels[Index].Strength = Strength;
                    });
            }
            ImGui::EndDisabled();
            float SurfaceRelief = First->SurfaceRelief;
            ImGui::BeginDisabled(Protected);
            if (Controls.Range("Surface relief", SurfaceRelief, -1.f, 1.f))
                EvaluateEdit([&](auto &Document)
                {
                    for (auto &Layer : Document.Layers)
                        if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                            Layer.SurfaceRelief = SurfaceRelief;
                });
            ImGui::EndDisabled();
            Controls.Tooltip("Surface relief", "Signed height amplitude used by the Height map and its derived tangent normals.");
        }
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    Controls.BeginCard(MaskSelection ? "Vector mask" : "Selected component", true);
    ImGui::BeginDisabled(Protected);
    Controls.BeginCard("Mask", false, true);
    auto EditedLayer = *First;
    if (!EditedLayer.Mask)
    {
        if (Controls.Button("Add mask")) EvaluateEdit([&](auto &Document)
        {
            for (auto &Layer : Document.Layers) if (Layer.Token == First->Token)
            {
                auto Mask = Snapshot.Instructions[First->Root];
                if (Mask.Topology[0] > 6) { Mask = VectorCoverageInstruction{}; Mask.Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection); }
                Mask.Colour = Mask.ColourEnd = {1, 1, 1, 1}; Layer.Mask = Mask;
            }
        });
    }
    else
    {
        if (Controls.Button("Edit mask")) MaskSelection = First->Token;
        bool Changed = Controls.Toggle("Enable vector mask", EditedLayer.MaskEnabled);
        Changed |= Controls.Toggle("Invert vector mask", EditedLayer.MaskInverted);
        Changed |= Controls.Toggle("Luminance mask", EditedLayer.MaskLuminance);
        int Category = EditedLayer.Mask->Topology[0] == 1 ? 1 : 0;
        const char *Categories[]{"Rectangle", "Ellipse"};
        if (EditedLayer.Mask->Topology[0] <= 1 && Controls.Dropdown("Mask shape", Category, Categories)) { EditedLayer.Mask->Topology[0] = uint32_t(Category); Changed = true; }
        Changed |= Controls.Number("Mask opacity", EditedLayer.Mask->Transform[2], 0, 1);
        Changed |= Controls.Number("Mask X", EditedLayer.Mask->Bounds[0], -512, 1024);
        Changed |= Controls.Number("Mask Y", EditedLayer.Mask->Bounds[1], -512, 1024);
        Changed |= Controls.Number("Mask width", EditedLayer.Mask->Bounds[2], 1, 1024);
        Changed |= Controls.Number("Mask height", EditedLayer.Mask->Bounds[3], 1, 1024);
        if (Changed) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer = EditedLayer; });
        if (Controls.Button("Remove mask")) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Mask.reset(); });
        if (MaskSelection && Controls.Button("Done editing mask")) MaskSelection = 0;
        ImGui::TextDisabled("Paint/image mask authoring is not ported yet.");
    }
    Controls.EndCard();
    if (!MaskSelection)
    {
        Controls.BeginCard("Transform", false, true);
        char LayerName[81]; std::snprintf(LayerName, sizeof(LayerName), "%s", First->Name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Layer name", LayerName, sizeof(LayerName))) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Name = LayerName; });
        const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
        if (ImGui::BeginTable("Transform fields", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
        ImGui::TableSetupColumn("Horizontal", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Vertical", ImGuiTableColumnFlags_WidthStretch, 1);
        for (size_t Index = 0; Index < 4; ++Index)
        {
            if (Index % 2 == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, 80 * Scale);
            ImGui::TableNextColumn();
            float Quantity = Bounds[Index];
            const char *Captions[]{"X", "Y", "Width", "Height"};
            if (Controls.Number(Captions[Index], Quantity, Index < 2 ? -512.f : 1.f, 1024))
            {
                VectorAuthoringSourcePatch Patch;
                if (Index == 0) Patch.TranslationX = Quantity - Bounds[0];
                if (Index == 1) Patch.TranslationY = Quantity - Bounds[1];
                if (Index == 2) Patch.ScaleX = Quantity / std::max(.01f, Bounds[2]);
                if (Index == 3) Patch.ScaleY = Quantity / std::max(.01f, Bounds[3]);
                ApplySourcePatch(Patch);
            }
        }
        ImGui::EndTable();
        }
        const auto &Source = Snapshot.Instructions[First->Root];
        float Rotation = Source.Topology[0] <= 6 ? Source.Transform[0] * 180 / std::numbers::pi_v<float> : 0;
        if (Controls.Number("Rotation", Rotation, -360, 360)) { VectorAuthoringSourcePatch Patch; Patch.Rotation = Rotation * std::numbers::pi_v<float> / 180 - Source.Transform[0]; ApplySourcePatch(Patch); }
        Controls.EndCard();
        Controls.BeginCard("Fill", false, true);
        int Category = int(Source.Transform[3]);
        const char *Sources[]{"Solid colour", "Linear gradient", "Radial gradient"};
        if (Controls.Dropdown("Component fill source", Category, Sources)) { VectorAuthoringSourcePatch Patch; Patch.PaintCategory = uint32_t(Category); ApplySourcePatch(Patch); }
        auto Colour = Source.Colour;
        if (Controls.Colour("Fill colour", Colour)) { VectorAuthoringSourcePatch Patch; Patch.Colour = Colour; ApplySourcePatch(Patch); }
        if (Category)
        {
            auto EndColour = Source.ColourEnd;
            if (Controls.Colour("Gradient end colour", EndColour)) { VectorAuthoringSourcePatch Patch; Patch.ColourEnd = EndColour; ApplySourcePatch(Patch); }
        }
        bool Filled = Source.Paint[3] != 0;
        if (Controls.Toggle("Fill enabled", Filled)) { VectorAuthoringSourcePatch Patch; Patch.FillEnabled = Filled; ApplySourcePatch(Patch); }
        Controls.EndCard();
        Controls.BeginCard("Stroke", false, true);
        float Stroke = Source.Transform[1];
        if (Controls.Number("Stroke width", Stroke, 0, 25)) { VectorAuthoringSourcePatch Patch; Patch.StrokeWidth = Stroke; ApplySourcePatch(Patch); }
        auto StrokeColour = Source.StrokeColour;
        if (Controls.Colour("Stroke colour", StrokeColour)) { VectorAuthoringSourcePatch Patch; Patch.StrokeColour = StrokeColour; ApplySourcePatch(Patch); }
        ImGui::TextWrapped("Fill and stroke colours are independent. Stroke image sources remain deferred.");
        Controls.EndCard();
    }
    ImGui::EndDisabled();
    Controls.EndCard();
    if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED HOSTING AND KEYBOARD INPUT
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructCloseDialogue()
{
    if (CloseRequested && !ImGui::IsPopupOpen("Unsaved stamp")) ImGui::OpenPopup("Unsaved stamp");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Unsaved stamp", nullptr, ImGuiWindowFlags_NoResize))
    {
        const auto Dialogue = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Dialogue.x, Dialogue.y + 4 * Scale), "UNSAVED STAMP", 7 * Scale, 0x8b8d79);
        Caption(ImVec2(Dialogue.x, Dialogue.y + 26 * Scale), "Save this stamp?", 17 * Scale, 0xdcdccd, LightTypeface);
        ImGui::Dummy(ImVec2(1, 58 * Scale));
        const auto &Name = QueryDocument().Name;
        ImGui::TextWrapped("\u201c%s\u201d has unsaved changes. Save keeps them; Discard restores the last save.",
            Name.empty() ? "Untitled stamp" : Name.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 3 * ButtonWidth - 2 * ButtonGap - 16 * Scale);
        if (Controls.Button("Cancel", ButtonWidth)) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (CancelClose) CancelClose(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Discard", ButtonWidth)) { History.Reset(SavedDocument); CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Save and close", ButtonWidth, true) && Save()) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::SuspendContent() noexcept
{
    if (GestureSource) Selection = std::move(GestureSelection);
    GestureSource.reset(); GestureDraft.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
}

void StampWorkspacePanel::FinalizeContent() noexcept
{
    SuspendContent();
    QueryCoverage = {}; QueryCoverageReady = {}; QuerySelectionToken = {}; SaveSource = {}; LoadSource = {}; ActivateMaterial = {}; ActivatePattern = {}; CancelClose = {}; EvaluateNavigationDelta = {};
}

void StampWorkspacePanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    ControlExtents.clear(); PanelExtents.clear();
    const auto SavedPalette = ControlKit::Palette();
    auto Palette = SavedPalette;
    Palette.Panel = {.0706f, .0706f, .0706f, 1};
    Palette.Inset = {.098f, .098f, .098f, 1};
    Palette.Raised = {.133f, .133f, .133f, 1};
    Palette.Selected = {.20f, .208f, .173f, 1};
    Palette.Text = {.808f, .808f, .753f, 1};
    Palette.TextDim = {.56f, .58f, .50f, 1};
    Palette.TextFaint = {.35f, .37f, .30f, 1};
    Palette.Accent = {.39f, .45f, .31f, 1};
    ControlKit::ConfigurePalette(Palette);
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 11 * 1.302f * Scale);
    const auto Colour = [](ColorQuad Tone) { return ImVec4(Tone.Red, Tone.Green, Tone.Blue, Tone.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Colour(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
    for (auto Slot : {ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_DockingEmptyBg}) ImGui::PushStyleColor(Slot, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    for (auto Slot : {ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected}) ImGui::PushStyleColor(Slot, Spectrum(0x101010));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(32, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * Scale, 9 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * Scale, 10 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_DockingSeparatorSize, 2 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4 * Scale);
    const float CloseMinimumSelected = ImGui::GetStyle().TabCloseButtonMinWidthSelected;
    const float CloseMinimumUnselected = ImGui::GetStyle().TabCloseButtonMinWidthUnselected;
    ImGui::GetStyle().TabCloseButtonMinWidthSelected = FLT_MAX;
    ImGui::GetStyle().TabCloseButtonMinWidthUnselected = FLT_MAX;
    ConstructHeader();
    Panels.ConstructInterface(Scale);
    ConstructCloseDialogue();
    const auto &Input = ImGui::GetIO();
    if (Gesture == 0 && !Input.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        const bool Command = Input.KeyCtrl || Input.KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_C)) TransferSelection(false);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_X)) TransferSelection(true);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_V)) InsertSelection(Input.KeyShift);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_D)) ReplicateSelection();
        if (Command && ImGui::IsKeyPressed(ImGuiKey_A)) SelectEditableVectors();
        if (!Command && (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) || ImGui::IsKeyPressed(ImGuiKey_RightBracket)))
        {
            const bool Forward = ImGui::IsKeyPressed(ImGuiKey_RightBracket);
            const auto Order = Input.KeyShift ? (Forward ? VectorSelectionOrder::Front : VectorSelectionOrder::Back)
                : (Forward ? VectorSelectionOrder::Forward : VectorSelectionOrder::Backward);
            if (!Selection.empty() && !MaskSelection) EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, Order); });
        }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) RestoreHistory(Input.KeyShift);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) RestoreHistory(true);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_V)) Tool = VectorAuthoringTool::Select;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_R)) Tool = VectorAuthoringTool::Rectangle;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_O)) Tool = VectorAuthoringTool::Ellipse;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_T)) Tool = VectorAuthoringTool::Text;
        if (ImGui::IsKeyPressed(ImGuiKey_0)) { Pan = {}; Zoom = 1; }
        if (!Selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (MaskSelection)
            {
                EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == MaskSelection && !QueryVectorAuthoringProtection(Document, Layer)) Layer.Mask.reset(); });
                MaskSelection = 0;
            }
            else EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        }
        if (!Selection.empty())
        {
            VectorAuthoringSourcePatch Patch;
            const float Step = Input.KeyShift ? 10.f : 1.f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Patch.TranslationX -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Patch.TranslationX += Step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Patch.TranslationY -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Patch.TranslationY += Step;
            if (Patch.TranslationX || Patch.TranslationY) ApplySourcePatch(Patch);
        }
    }
    ImGui::GetStyle().TabCloseButtonMinWidthSelected = CloseMinimumSelected;
    ImGui::GetStyle().TabCloseButtonMinWidthUnselected = CloseMinimumUnselected;
    ImGui::PopStyleVar(8);
    ImGui::PopStyleColor(22);
    ImGui::PopFont();
    ControlKit::ConfigurePalette(SavedPalette);
}
}
