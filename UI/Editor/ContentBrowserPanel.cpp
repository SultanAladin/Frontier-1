/*==========================================================================================================================================
                                                         CONTENTBROWSERPANEL.CPP
==========================================================================================================================================*/
// 🧩 Transcribe the Asset Browser reference 1:1 over real, root-confined files without importing scene or painting code.

#include "ContentBrowserPanel.h"
#include "Engine/Interface/IconPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <imgui.h>
#include <optional>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ContentBrowserPanel::ContentBrowserPanel(std::filesystem::path Requested) : Root(std::move(Requested))
{
}

bool ContentBrowserPanel::InitializeContent() noexcept
{
    try
    {
        // An unavailable content root is a visible browser diagnostic, not a failure of the editor host.
        (void)Directory.ConfigureRoot(Root);
        (void)ImportDirectory.ConfigureRoot(Root);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void ContentBrowserPanel::IntegrateContent(double Δτ) noexcept
{
    Expansion.Integrate(Δτ);
    Search.Advance(static_cast<float>(std::min(Δτ, 0.1)));
}

void ContentBrowserPanel::FinalizeContent() noexcept
{
    Search.Active = false;
    PressedControl.clear();
    ScrollDrag = 0;
    Expansion.ConfigureOpen(false);
}

void ContentBrowserPanel::ConfigureOpen(bool Open) noexcept
{
    Expansion.ConfigureOpen(Open);
    if (!Open)
    {
        Search.Active = false;
        CategoryMenu  = false;
        PressedControl.clear();
        ScrollDrag = 0;
        Page       = ContentBrowserPage::Library;
    }
}

PlaneExtent ContentBrowserPanel::PhysicalExtent(PlaneExtent Extent) const noexcept
{
    return {Extent.MinimumX * Scale, Extent.MinimumY * Scale, Extent.MaximumX * Scale, Extent.MaximumY * Scale};
}

PlaneExtent ContentBrowserPanel::QueryEntryExtent(size_t Index) const noexcept
{
    return Index < EntryExtents.size() ? EntryExtents[Index] : PlaneExtent{};
}

PlaneExtent ContentBrowserPanel::QueryImportEntryExtent(size_t Index) const noexcept
{
    return Index < ImportEntryExtents.size() ? ImportEntryExtents[Index] : PlaneExtent{};
}

void ContentBrowserPanel::ResetLocation()
{
    Scroll      = 0;
    AsideOffset = 0;
    PressedControl.clear();
    Search.Active = false;
}

void ContentBrowserPanel::ConfirmImport(const std::filesystem::path &Path)
{
    // 📝 Importing takes a root-confined file into the working selection: the library travels to its folder,
    // drops every filter so the take stays visible, and returns to the library page.
    const auto Folder = Path.parent_path();
    if (!Folder.empty() && Folder != Directory.QueryDirectory())
        (void)Directory.Navigate(Folder);
    Category          = ContentCategory::All;
    Search.Text[0]    = '\0';
    Search.Length     = 0;
    Search.Caret      = 0;
    Search.Active     = false;
    Directory.ConfigureFilter(Category, Search.Text);
    (void)Directory.Select(Path);
    ImportDirectory.ResetSelection();
    Page = ContentBrowserPage::Library;
    ResetLocation();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SEARCH AND KEYS
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::IntegrateSearch()
{
    auto &IO = ImGui::GetIO();
    if (!Search.Active)
        return;
    IO.WantTextInput = true;
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        Search.Cancel();
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        Search.Commit();
    else
    {
        if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_A, false))
            Search.SelectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
            Search.Backspace();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            Search.Delete();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            Search.MoveCaret(-1, IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            Search.MoveCaret(1, IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            Search.MoveHome(IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            Search.MoveEnd(IO.KeyShift);
        if (!IO.KeyCtrl && !IO.KeySuper && !IO.KeyAlt)
            for (const auto Character : IO.InputQueueCharacters)
            {
                if (Character < 32 || Character == 127)
                    continue;
                char UTF8[5]{};
                if (Character < 0x80)
                    UTF8[0] = static_cast<char>(Character);
                else if (Character < 0x800)
                {
                    UTF8[0] = static_cast<char>(0xC0 | (Character >> 6));
                    UTF8[1] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                else if (Character < 0x10000)
                {
                    UTF8[0] = static_cast<char>(0xE0 | (Character >> 12));
                    UTF8[1] = static_cast<char>(0x80 | ((Character >> 6) & 0x3F));
                    UTF8[2] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                else
                {
                    UTF8[0] = static_cast<char>(0xF0 | (Character >> 18));
                    UTF8[1] = static_cast<char>(0x80 | ((Character >> 12) & 0x3F));
                    UTF8[2] = static_cast<char>(0x80 | ((Character >> 6) & 0x3F));
                    UTF8[3] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                Search.Insert(UTF8);
            }
    }
    Directory.ConfigureFilter(Category, Search.Text);
    Scroll = 0;
}

void ContentBrowserPanel::IntegrateKeyboard()
{
    const auto &IO = ImGui::GetIO();
    if (!Focused || Search.Active || IO.WantTextInput)
        return;
    auto &Active = Page == ContentBrowserPage::Import ? ImportDirectory : Directory;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (CategoryMenu)
            CategoryMenu = false;
        else if (Page == ContentBrowserPage::Import)
        {
            Page = ContentBrowserPage::Library;
            ImportDirectory.ResetSelection();
        }
        else if (!HostedPresentation)
            ConfigureOpen(false);
        else
            Focused = false;
        return;
    }
    if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        if (Page == ContentBrowserPage::Library)
            Search.Begin(Search.Text);
        return;
    }
    // 📝 The seek field's own hint: slash focuses it from anywhere in the browser.
    if (Page == ContentBrowserPage::Library && ImGui::IsKeyPressed(ImGuiKey_Slash, false))
    {
        Search.Begin(Search.Text);
        return;
    }
    if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_Home, false) &&
        Page == ContentBrowserPage::Library)
    {
        if (Directory.Navigate(Directory.QueryRoot()))
            ResetLocation();
        return;
    }
    bool Navigated = false;
    if (IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        Navigated = Active.NavigateBack();
    else if (IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        Navigated = Active.NavigateForward();
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        Navigated = Active.NavigateUp();
    else if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        Navigated = Active.Refresh();
    if (Navigated)
    {
        if (Page == ContentBrowserPage::Import)
        {
            ImportOffset = 0;
            ImportDirectory.ResetSelection();
        }
        else
            ResetLocation();
        return;
    }
    if (!HostedPresentation && !Expansion.QueryOpen())
        return;
    const auto &Visible = Active.QueryVisible();
    if (Visible.empty())
        return;
    const auto *Selected = Active.QuerySelection();
    size_t      Ordinal  = 0;
    for (; Ordinal < Visible.size(); ++Ordinal)
        if (&Active.QueryEntries()[Visible[Ordinal]] == Selected)
            break;
    const bool  Importing = Page == ContentBrowserPage::Import;
    const int   Stride    = Importing ? 1 : static_cast<int>(Columns);
    const float RowPitch  = Importing ? 38 : CellHeight;
    int         Step      = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        Step = Stride;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        Step = -Stride;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !IO.KeyAlt)
        Step = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !IO.KeyAlt)
        Step = -1;
    if (Step)
    {
        const auto Next =
            Selected ? std::clamp(static_cast<int>(Ordinal) + Step, 0, static_cast<int>(Visible.size()) - 1) : 0;
        (void)Active.Select(Active.QueryEntries()[Visible[static_cast<size_t>(Next)]].Path);
        // 📝 Scroll-into-view follows the selection on the page that owns it.
        const float RowTop = 16 + static_cast<float>(Next / std::max(Stride, 1)) * (RowPitch + 12);
        if (Importing)
        {
            const float ViewportHeight = (ImportListExtent.MaximumY - ImportListExtent.MinimumY) / Scale;
            if (RowTop < ImportOffset)
                ImportOffset = RowTop;
            else if (RowTop + RowPitch > ImportOffset + ViewportHeight)
                ImportOffset = RowTop + RowPitch - ViewportHeight;
        }
        else
        {
            const float ViewportHeight = (BodyExtent.MaximumY - BodyExtent.MinimumY) / Scale;
            if (RowTop < Scroll)
                Scroll = RowTop;
            else if (RowTop + RowPitch > Scroll + ViewportHeight)
                Scroll = RowTop + RowPitch - ViewportHeight;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && Selected)
    {
        if (Selected->Category == ContentCategory::Directory)
        {
            const auto Path = Selected->Path;
            if (Importing)
            {
                if (ImportDirectory.Navigate(Path))
                {
                    ImportOffset = 0;
                    ImportDirectory.ResetSelection();
                }
            }
            else if (Directory.Navigate(Path))
                ResetLocation();
        }
        else if (Importing)
            ConfirmImport(Selected->Path);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                  REFERENCE TRANSCRIPTION
//------------------------------------------------------------------------------------------------------------------------
// 📝 The library page transcribes AsstbrowsrBasic (via the Slate reference recording) element by element: sources
// aside, seek rail, lattice, inspector, import page, hatch, scrollbars, tooltips. Records stay backed by real
// root-confined files, so the taxonomy is the native category run rather than the reference's seven archives,
// folders remain navigable, and the breadcrumb crumbs the folder trail; everything else — metrics, tokens,
// captions, filtering order, the 48px wheel notch — follows the reference. Runs render without letter tracking,
// which the native text runs do not offer, and card grounds are flat where the reference gradients them.

namespace
{
struct ContentBrowserColours
{
    ColorQuad Ground, Aside, Rail, Stroke, CardUpper, Plate, Medallion, Field, Primary, Secondary;
    ColorQuad Faint, Faintest, Hovered, Taken, EdgeHovered, EdgeTaken, Hatch, Emphatic, EmphaticHovered;
    ColorQuad EmphaticRun, ChipGround, EdgeHolding, MeterQuiet, GripQuiet;
};

ColorQuad Covering(uint32_t Packed) noexcept
{
    return {float((Packed >> 16) & 0xFF) / 255, float((Packed >> 8) & 0xFF) / 255, float(Packed & 0xFF) / 255, 1};
}

ColorQuad Partial(uint32_t Packed, float Alpha) noexcept
{
    auto Quad  = Covering(Packed);
    Quad.Alpha = Alpha;
    return Quad;
}

ContentBrowserColours BrowserColours() noexcept
{
    return {.Ground          = Covering(0x000000),
            .Aside           = Covering(0x0A0A0A),
            .Rail            = Covering(0x0C0C0E),
            .Stroke          = Covering(0x1F1F1F),
            .CardUpper       = Covering(0x131316),
            .Plate           = Covering(0x1A1A1E),
            .Medallion       = Covering(0x17171B),
            .Field           = Covering(0x111111),
            .Primary         = Covering(0xFFFFFF),
            .Secondary       = Covering(0xA3A3A3),
            .Faint           = Covering(0x737373),
            .Faintest        = Covering(0x525252),
            .Hovered         = Partial(0xFFFFFF, .05f),
            .Taken           = Partial(0xFFFFFF, .10f),
            .EdgeHovered     = Partial(0xFFFFFF, .30f),
            .EdgeTaken       = Partial(0xFFFFFF, .60f),
            .Hatch           = Partial(0xFFFFFF, .02f),
            .Emphatic        = Covering(0xFFFFFF),
            .EmphaticHovered = Covering(0xE5E5E5),
            .EmphaticRun     = Covering(0x000000),
            .ChipGround      = Partial(0x000000, .70f),
            .EdgeHolding     = Partial(0xFFFFFF, .40f),
            .MeterQuiet      = Partial(0xFFFFFF, .20f),
            .GripQuiet       = Partial(0xFFFFFF, .15f)};
}

std::string FormatContentOctets(double Octets)
{
    char Sized[32]{};
    if (Octets < 104857.6)
        std::snprintf(Sized, sizeof Sized, "%.1f KB", Octets / 1024);
    else
        std::snprintf(Sized, sizeof Sized, "%.1f MB", Octets / 1048576);
    return Sized;
}

std::string FoldRecordText(std::string Text)
{
    for (auto &Character : Text)
        if (Character >= 'A' && Character <= 'Z')
            Character = static_cast<char>(Character + ('a' - 'A'));
    return Text;
}

uint32_t LatticeColumns(float Width) noexcept
{
    constexpr float CardIdeal = 168, CardGap = 12, CardPad = 16;
    const float     Usable = Width - CardPad * 2;
    if (Usable <= CardIdeal)
        return 1;
    return std::clamp(uint32_t((Usable + CardGap) / (CardIdeal + CardGap)), 1u, 6u);
}

bool IsMaterialRecord(const ContentEntry &Entry)
{
    return Entry.Category != ContentCategory::Directory &&
           FoldRecordText(EncodeContentPath(Entry.Path.extension())) == ".material";
}

std::optional<IconSymbol> RecordIcon(const std::string &Extension)
{
    if (Extension == ".svg")
        return IconSymbol::ContentFileSvg;
    if (Extension == ".png")
        return IconSymbol::ContentFilePng;
    if (Extension == ".jpg" || Extension == ".jpeg")
        return IconSymbol::ContentFileJpg;
    if (Extension == ".json")
        return IconSymbol::ContentFileJson;
    if (Extension == ".ttf")
        return IconSymbol::ContentFileTtf;
    if (Extension == ".otf")
        return IconSymbol::ContentFileOtf;
    if (Extension == ".woff")
        return IconSymbol::ContentFileWoff;
    if (Extension == ".woff2")
        return IconSymbol::ContentFileWoff2;
    if (Extension == ".exr")
        return IconSymbol::ContentFileExr;
    if (Extension == ".obj")
        return IconSymbol::ContentFileObj;
    if (Extension == ".fbx")
        return IconSymbol::ContentFileFbx;
    if (Extension == ".gltf")
        return IconSymbol::ContentFileGltf;
    if (Extension == ".py")
        return IconSymbol::ContentFilePy;
    if (Extension == ".blend")
        return IconSymbol::ContentFileBlend;
    if (Extension == ".zip")
        return IconSymbol::ContentFileZip;
    if (Extension == ".mp3")
        return IconSymbol::ContentFileMp3;
    if (Extension == ".wav")
        return IconSymbol::ContentFileWav;
    if (Extension == ".ogg")
        return IconSymbol::ContentFileOgg;
    if (Extension == ".glsl")
        return IconSymbol::ContentFileGlsl;
    return {};
}

void DrawRecordCrest(PixelSpace &Surface, const PlaneExtent &Box, const ContentEntry &Entry, const ColorQuad &Tint,
                     float Scale)
{
    if (Entry.Category == ContentCategory::Directory)
    {
        ControlKit::GlyphCentred(Surface, Box, Box.Width(), Tint, ControlCentreIconCategory::FolderClosed);
        return;
    }
    const auto Icon = RecordIcon(FoldRecordText(EncodeContentPath(Entry.Path.extension())));
    if (Icon && IconPresentation::Draw(ImGui::GetWindowDrawList(), *Icon,
                                       ImVec2(Box.MinimumX * Scale, Box.MinimumY * Scale), Box.Width() * Scale))
        return;
    ControlKit::GlyphCentred(Surface, Box, Box.Width(), Tint, ControlCentreIconCategory::CircleInfo);
}

void PresentBrowserTooltip(const char *Text, const PlaneExtent &Extent, const ControlPointer &Pointer)
{
    if (Text && ControlKit::Over(Extent, Pointer))
        ImGui::SetTooltip("%s", Text);
}
} // namespace

bool ContentBrowserPanel::ClickedControl(const char *Tag, const PlaneExtent &Extent, const ControlPointer &Pointer)
{
    const bool Hovered = ControlKit::Over(Extent, Pointer);
    if (Hovered && Pointer.Pressed)
        PressedControl = Tag;
    return Hovered && Pointer.Released && PressedControl == Tag;
}

void ContentBrowserPanel::RecordHatch(PixelSpace &Surface, const PlaneExtent &Extent)
{
    static_cast<void>(Surface);
    // 📝 The reference's 45° repeating-linear-gradient: a 1px rule every 7px. One line per rule, clipped to
    // the plate — never one quad per pixel, which tripped the 16-bit index ceiling on a lattice of plates.
    auto *List = ImGui::GetWindowDrawList();
    List->PushClipRect(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale),
                       ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale), true);
    const float Drop = Extent.Height();
    for (float Origin = -Drop; Origin < Extent.Width() + Drop + 7; Origin += 7)
        List->AddLine(ImVec2((Extent.MinimumX + Origin) * Scale, Extent.MinimumY * Scale),
                      ImVec2((Extent.MinimumX + Origin - Drop) * Scale, Extent.MaximumY * Scale),
                      IM_COL32(255, 255, 255, 5), 1);
    List->PopClipRect();
}

void ContentBrowserPanel::RecordScrollbar(PixelSpace &Surface, const PlaneExtent &Extent, uint32_t Target,
                                          float Span, float &Offset, const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    const float Visible = Extent.Height();
    if (Span <= Visible || Visible <= 0)
    {
        Offset = 0;
        return;
    }
    const float Limit = Span - Visible;
    const float Thumb = std::max(Visible * Visible / Span, 28.f);
    const float Travel = Visible - Thumb;
    // 📝 The wheel reaches the run from anywhere over it at 48px a notch, whether or not the bar itself is
    // hovered — the reference scrolls the container and not its scrollbar.
    if (ControlKit::Over(Extent, Pointer) && Pointer.Wheel != 0)
        Offset -= Pointer.Wheel * 48;
    const auto Trough = Spanning(Extent.MaximumX - 6, Extent.MinimumY, 6, Visible);
    if (ControlKit::Over(Trough, Pointer) && Pointer.Pressed && ScrollDrag == 0)
    {
        ScrollDrag        = Target;
        ScrollDragOriginY = Pointer.Y;
        ScrollDragOffset  = Offset;
    }
    if (ScrollDrag == Target && Pointer.Down && Travel > 0)
        Offset = ScrollDragOffset + (Pointer.Y - ScrollDragOriginY) * (Limit / Travel);
    if (Pointer.Released && ScrollDrag == Target)
        ScrollDrag = 0;
    Offset           = std::clamp(Offset, 0.f, Limit);
    const float ThumbY = Extent.MinimumY + (Limit > 0 ? Offset / Limit * Travel : 0);
    Surface.FillRectangle(Spanning(Extent.MaximumX - 3, ThumbY, 3, Thumb),
                          ScrollDrag == Target ? Colours.EdgeHovered : Colours.GripQuiet, 2);
}

void ContentBrowserPanel::RecordSources(PixelSpace &Surface, const PlaneExtent &Extent,
                                       const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Extent, Colours.Aside);
    Surface.FillRectangle(Spanning(Extent.MaximumX - 1, Extent.MinimumY, 1, Extent.Height()), Colours.Stroke);
    ControlKit::TextLeading(Surface, Spanning(Extent.MinimumX + 16, Extent.MinimumY + 16, Extent.Width() - 32, 20),
                            0, Colours.Faint, "SOURCES", 10);
    constexpr float FootY = 69;
    const auto Traversable =
        Spanning(Extent.MinimumX, Extent.MinimumY + 40, Extent.Width(), Extent.Height() - 40 - FootY);
    const auto &Entries = Directory.QueryEntries();
    CategoryExtents.fill({});
    Surface.PushClip(Traversable);
    float Cursor = Traversable.MinimumY - AsideOffset + 8;
    ControlKit::TextLeading(Surface, Spanning(Extent.MinimumX + 16, Cursor + 4, Extent.Width() - 32, 20), 0,
                            Colours.Faint, "PROJECT", 10);
    Cursor += 24;
    const auto CountFor = [&](ContentCategory Category)
    {
        uint32_t Count = 0;
        for (const auto &Entry : Entries)
            if (Category == ContentCategory::All || Entry.Category == Category ||
                (Entry.Category == ContentCategory::Directory && Category != ContentCategory::Directory))
                ++Count;
        return Count;
    };
    const auto SourceRow = [&](const char *Naming, float Step, ContentCategory Category)
    {
        const auto Row = Spanning(Extent.MinimumX + 8 + Step, Cursor, Extent.Width() - 16 - Step, 30);
        const bool Current = Category == this->Category;
        const bool Hovered = ControlKit::Over(Row, Pointer);
        if (Current)
            Surface.FillRectangle(Row, Colours.Taken, 6);
        else if (Hovered)
            Surface.FillRectangle(Row, Colours.Hovered, 6);
        const auto Run = (Current || Hovered) ? Colours.Primary : Colours.Secondary;
        char             Tally[16]{};
        std::snprintf(Tally, sizeof Tally, "%u", CountFor(Category));
        const float TallyX = Surface.MeasureText(Tally, 10).X;
        Surface.PushClip(Spanning(Row.MinimumX + 30, Row.MinimumY, Row.MaximumX - Row.MinimumX - 30, 30));
        ControlKit::TextLeading(Surface, Spanning(Row.MinimumX + 30, Row.MinimumY + 9, Row.Width(), 20), 0, Run,
                                Naming, 12);
        Surface.PopClip();
        ControlKit::TextLeading(Surface, Spanning(Row.MaximumX - TallyX - 8, Row.MinimumY + 10, TallyX + 8, 16), 0,
                                Run, Tally, 10);
        if (Row.MinimumY + 30 > Traversable.MinimumY && Row.MinimumY < Traversable.MaximumY)
            CategoryExtents[size_t(Category)] = PhysicalExtent(Row);
        char Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "source-%u", uint32_t(Category));
        if (ClickedControl(Tag, Row, Pointer))
        {
            this->Category = Category;
            Directory.ConfigureFilter(Category, Search.Text);
            Directory.ResetSelection();
            Scroll = 0;
        }
        Cursor += 32;
    };
    SourceRow("Project Library", 0, ContentCategory::All);
    for (const auto Category : {ContentCategory::Directory, ContentCategory::Typeface, ContentCategory::VectorImage,
                                ContentCategory::RasterImage, ContentCategory::Descriptor, ContentCategory::Other})
        SourceRow(QueryContentCategoryLabel(Category), 24, Category);
    Surface.PopClip();
    RecordScrollbar(Surface, Traversable, 1, Cursor + AsideOffset - Traversable.MinimumY, AsideOffset, Pointer);
    const auto Foot = Spanning(Extent.MinimumX, Extent.MaximumY - FootY, Extent.Width(), FootY);
    Surface.FillRectangle(Spanning(Foot.MinimumX, Foot.MinimumY, Foot.Width(), 1), Colours.Stroke);
    ControlKit::TextLeading(Surface, Spanning(Foot.MinimumX + 16, Foot.MinimumY + 16, 120, 16), 0, Colours.Faint,
                            "Library cache", 10);
    double Known = 0;
    double Shares[7]{};
    for (const auto &Entry : Entries)
        if (Entry.ByteCountKnown)
        {
            Known += double(Entry.ByteCount);
            Shares[size_t(Entry.Category)] += double(Entry.ByteCount);
        }
    const std::string Retained = FormatContentOctets(Known);
    const float       RetainedX = Surface.MeasureText(Retained.c_str(), 10).X;
    ControlKit::TextLeading(Surface, Spanning(Foot.MaximumX - 16 - RetainedX, Foot.MinimumY + 16, RetainedX + 4, 16),
                            0, Colours.Primary, Retained.c_str(), 10);
    const auto Meter = Spanning(Foot.MinimumX + 16, Foot.MinimumY + 38, Foot.Width() - 32, 4);
    Surface.FillRectangle(Meter, Colours.Taken, 2);
    if (Known > 0)
    {
        std::sort(std::begin(Shares), std::end(Shares), std::greater<>{});
        const float First = float(Shares[0] / Known), Second = float(Shares[1] / Known);
        Surface.FillRectangle(Spanning(Meter.MinimumX, Meter.MinimumY, Meter.Width() * First, 4), Colours.Primary,
                              2);
        Surface.FillRectangle(Spanning(Meter.MinimumX + Meter.Width() * First, Meter.MinimumY,
                                       Meter.Width() * Second, 4),
                              Colours.Faint);
        Surface.FillRectangle(Spanning(Meter.MinimumX + Meter.Width() * (First + Second), Meter.MinimumY,
                                       Meter.Width() * (1 - First - Second), 4),
                              Colours.MeterQuiet);
    }
}

void ContentBrowserPanel::RecordSeekRail(PixelSpace &Surface, const PlaneExtent &Extent,
                                        const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Extent, Colours.Aside);
    Surface.FillRectangle(Spanning(Extent.MinimumX, Extent.MaximumY - 1, Extent.Width(), 1), Colours.Stroke);
    // 📝 The seek field yields to the button block: Create/Import stay aboard while the field keeps
    // 120px, and only the arrangement toggle survives below that.
    const bool  Roomy   = Extent.Width() >= 400;
    const float FieldX  = std::min(Extent.Width() - 16 - 16 - (Roomy ? 226 : 44), 448.f);
    const auto  SeekField = Spanning(Extent.MinimumX + 16, Extent.MinimumY + 12, std::max(FieldX, 120.f), 32);
    SearchExtent          = PhysicalExtent(SeekField);
    if (ClickedControl("seek", SeekField, Pointer))
        Search.Begin(Search.Text);
    else if (Pointer.Pressed && Search.Active)
        Search.Commit();
    IntegrateSearch();
    Surface.FillRectangle(SeekField, Colours.Field, 16);
    ControlKit::OutlineRounded(Surface, SeekField, Search.Active ? Colours.EdgeHolding : Colours.Stroke, 16);
    ControlKit::Glyph(Surface, SeekField.MinimumX + 12, SeekField.MinimumY + 9, 14, Colours.Faint,
                      ControlCentreIconCategory::SearchGlass);
    const bool Sought = Search.Length > 0;
    Surface.PushClip(Spanning(SeekField.MinimumX + 34, SeekField.MinimumY, SeekField.Width() - 70, 32));
    ControlKit::TextLeading(Surface, Spanning(SeekField.MinimumX + 34, SeekField.MinimumY + 10, SeekField.Width(),
                                             20),
                            0, Sought ? Colours.Primary : Colours.Faintest,
                            Sought ? Search.Text : "Search assets, formats, tags...", 12);
    Surface.PopClip();
    if (Search.Active)
    {
        const std::string Before(Search.Text, std::min<size_t>(Search.Caret, Search.Length));
        const float       Caret =
            SeekField.MinimumX + 34 + Surface.MeasureText(Before.c_str(), 12).X;
        Surface.FillRectangle(Spanning(Caret, SeekField.MinimumY + 8, 1, 16), Colours.Primary);
    }
    const auto Hint = Spanning(SeekField.MaximumX - 26, SeekField.MinimumY + 8, 18, 16);
    Surface.FillRectangle(Hint, Colours.Hovered, 4);
    ControlKit::OutlineRounded(Surface, Hint, Colours.Stroke, 4);
    ControlKit::TextLeading(Surface, Spanning(Hint.MinimumX + 6, Hint.MinimumY + 2, 12, 12), 0, Colours.Faint, "/",
                            10);
    const auto Import = Spanning(Extent.MaximumX - 16 - 84, Extent.MinimumY + 12, 84, 32);
    const auto Create = Spanning(Import.MinimumX - 8 - 82, Extent.MinimumY + 12, 82, 32);
    const auto Arrangement =
        Spanning((Roomy ? Create.MinimumX - 36 : Extent.MaximumX - 16 - 28), Extent.MinimumY + 12, 28, 32);
    ImportExtent = Roomy ? PhysicalExtent(Import) : PlaneExtent{};
    ViewExtent   = PhysicalExtent(Arrangement);
    const bool ToggleHover = ControlKit::Over(Arrangement, Pointer);
    Surface.FillRectangle(Arrangement, ToggleHover ? Colours.Taken : Colours.Hovered, 6);
    const float GlyphX = Arrangement.MinimumX + 7, GlyphY = Arrangement.MinimumY + 9;
    if (Grid)
        for (int CellY = 0; CellY < 2; ++CellY)
            for (int CellX = 0; CellX < 2; ++CellX)
                Surface.FillRectangle(Spanning(GlyphX + CellX * 7.5f, GlyphY + CellY * 7.5f, 6, 6), Colours.Primary,
                                      1);
    else
        for (int Bar = 0; Bar < 3; ++Bar)
            Surface.FillRectangle(Spanning(GlyphX, GlyphY + Bar * 5, 14, 2.5f), Colours.Primary, 1);
    PresentBrowserTooltip("Switch grid/list arrangement", Arrangement, Pointer);
    if (ClickedControl("arrangement", Arrangement, Pointer))
    {
        Grid   = !Grid;
        Scroll = 0;
    }
    if (!Roomy)
        return;
    const bool CreateHover = ControlKit::Over(Create, Pointer);
    Surface.FillRectangle(Create, CreateHover ? Colours.Taken : Colours.Hovered, 16);
    ControlKit::OutlineRounded(Surface, Create, Colours.Stroke, 16);
    ControlKit::Glyph(Surface, Create.MinimumX + 12, Create.MinimumY + 9, 14,
                      CreateHover ? Colours.Primary : Colours.Secondary, ControlCentreIconCategory::PlusAdd);
    ControlKit::TextLeading(Surface, Spanning(Create.MinimumX + 32, Create.MinimumY + 10, 50, 20), 0,
                            CreateHover ? Colours.Primary : Colours.Secondary, "Create", 12);
    PresentBrowserTooltip("Create a new record", Create, Pointer);
    // 📝 1:1 with the reference: Create records its press and nothing else.
    (void)ClickedControl("create", Create, Pointer);
    const bool ImportHover = ControlKit::Over(Import, Pointer);
    Surface.FillRectangle(Import, ImportHover ? Colours.EmphaticHovered : Colours.Emphatic, 16);
    {
        auto *List = ImGui::GetWindowDrawList();
        const auto At = [&](float Dx, float Dy)
        { return ImVec2((Import.MinimumX + 12 + Dx) * Scale, (Import.MinimumY + 9 + Dy) * Scale); };
        const auto Ink = IM_COL32(0, 0, 0, 255);
        List->AddLine(At(7, 1), At(7, 9), Ink, 1.6f * Scale);
        List->AddLine(At(7, 9), At(3.5f, 5.5f), Ink, 1.6f * Scale);
        List->AddLine(At(7, 9), At(10.5f, 5.5f), Ink, 1.6f * Scale);
        List->AddLine(At(2, 12), At(12, 12), Ink, 1.6f * Scale);
    }
    ControlKit::TextLeading(Surface, Spanning(Import.MinimumX + 32, Import.MinimumY + 10, 52, 20), 0,
                            Colours.EmphaticRun, "Import", 12);
    PresentBrowserTooltip("Import into the library", Import, Pointer);
    if (ClickedControl("import", Import, Pointer))
    {
        Page = ContentBrowserPage::Import;
        ImportDirectory.ResetSelection();
        ImportOffset = 0;
    }
}

void ContentBrowserPanel::RecordLattice(PixelSpace &Surface, const PlaneExtent &Extent,
                                       const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Extent, Colours.Ground);
    const auto Rail = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 41);
    Surface.FillRectangle(Rail, Colours.Rail);
    Surface.FillRectangle(Spanning(Rail.MinimumX, Rail.MaximumY - 1, Rail.Width(), 1), Colours.Stroke);
    // 📝 The reference crumbs its archive trail; the native browser crumbs its folder trail so navigation
    // never loses context — the archive state stays visible on the highlighted sources row.
    NavigationExtents.fill({});
    Surface.PushClip(Rail);
    float Crumb = Rail.MinimumX + 16;
    std::vector<std::pair<std::string, std::filesystem::path>> Segments;
    const std::string RootName = EncodeContentPath(Directory.QueryRoot().filename());
    Segments.emplace_back(RootName.empty() ? "Content" : RootName, Directory.QueryRoot());
    const auto Relative = Directory.QueryDirectory().lexically_relative(Directory.QueryRoot());
    if (Relative != ".")
    {
        auto Folder = Directory.QueryRoot();
        for (const auto &Part : Relative)
        {
            Folder /= Part;
            Segments.emplace_back(EncodeContentPath(Part), Folder);
        }
    }
    for (size_t Index = 0; Index < Segments.size(); ++Index)
    {
        const bool Current = Index + 1 == Segments.size();
        const float Span   = Surface.MeasureText(Segments[Index].first.c_str(), 12).X;
        const auto  Run    = Spanning(Crumb, Rail.MinimumY, Span, 41);
        ControlKit::TextLeading(Surface, Spanning(Crumb, Rail.MinimumY + 14, Span, 20), 0,
                                Current ? Colours.Primary : Colours.Faint, Segments[Index].first.c_str(), 12);
        if (Index == 0)
            NavigationExtents[0] = PhysicalExtent(Run);
        if (Index + 2 == Segments.size())
            NavigationExtents[1] = PhysicalExtent(Run);
        char Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "crumb-%zu", Index);
        if (!Current && ClickedControl(Tag, Run, Pointer))
        {
            if (Directory.Navigate(Segments[Index].second))
                ResetLocation();
        }
        Crumb += Span + 8;
        if (!Current)
        {
            ControlKit::TextLeading(Surface, Spanning(Crumb, Rail.MinimumY + 14, 8, 20), 0, Colours.Faint, "/", 12);
            Crumb += Surface.MeasureText("/", 12).X + 8;
        }
    }
    if (Directory.QueryTruncated() || Directory.QueryOmittedLinks())
    {
        std::string Note;
        if (Directory.QueryTruncated())
            Note += " · showing first 8192";
        if (Directory.QueryOmittedLinks())
            Note += " · " + std::to_string(Directory.QueryOmittedLinks()) + " links omitted";
        ControlKit::TextLeading(Surface, Spanning(Crumb + 8, Rail.MinimumY + 14, Rail.MaximumX - Crumb, 20), 0,
                                Colours.Faintest, Note.c_str(), 10);
    }
    Surface.PopClip();
    const auto Run = Spanning(Extent.MinimumX, Rail.MaximumY, Extent.Width(), Extent.Height() - 41);
    BodyExtent     = PhysicalExtent(Run);
    Surface.PushClip(Run);
    const auto        &Visible = Directory.QueryVisible();
    const uint32_t     Cols = Grid ? LatticeColumns(Run.Width()) : 1;
    Columns                  = Cols;
    const float CardX = Grid ? (Run.Width() - 32 - 12 * float(Cols - 1)) / float(Cols) : Run.Width() - 32;
    const float PlateY = Grid ? CardX * .75f : 0;
    const float CardH  = Grid ? PlateY + 46 : 38;
    CellWidth          = CardX;
    CellHeight         = CardH;
    EntryExtents.assign(Visible.size(), {});
    const auto *Selection = Directory.QuerySelection();
    std::filesystem::path NavigateTo;
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float X = Run.MinimumX + 16 + float(Ordinal % Cols) * (CardX + 12);
        const float Y = Run.MinimumY + 16 + float(Ordinal / Cols) * (CardH + 12) - Scroll;
        if (Y + CardH <= Run.MinimumY || Y >= Run.MaximumY)
            continue;
        const auto  &Entry = Directory.QueryEntries()[Visible[Ordinal]];
        const auto   Card  = Spanning(X, Y, CardX, CardH);
        EntryExtents[Ordinal] = PhysicalExtent(Card);
        const bool Hovered    = ControlKit::Over(Card, Pointer) && ControlKit::Over(Run, Pointer);
        const bool Current    = Selection && Selection->Path == Entry.Path;
        const auto Lifted = Hovered && Grid ? Spanning(Card.MinimumX, Card.MinimumY - 2, CardX, CardH) : Card;
        if (Grid)
        {
            const bool MaterialCard = IsMaterialRecord(Entry);
            const auto Plate = Spanning(Lifted.MinimumX, Lifted.MinimumY, CardX, PlateY);
            if (!MaterialCard)
            {
                Surface.FillRectangle(Lifted, Colours.CardUpper, 12);
                auto *List = ImGui::GetWindowDrawList();
                List->AddRectFilled(ImVec2(Plate.MinimumX * Scale, Plate.MinimumY * Scale),
                                    ImVec2(Plate.MaximumX * Scale, Plate.MaximumY * Scale),
                                    IM_COL32(0x1A, 0x1A, 0x1E, 255), 12 * Scale, ImDrawFlags_RoundCornersTop);
                RecordHatch(Surface, Plate);
                Surface.FillRectangle(Spanning(Plate.MinimumX, Plate.MaximumY - 1, CardX, 1), Colours.Stroke);
            }
            const float CrestSpan = Hovered ? 34 : 32;
            if (MaterialCard)
            {
                const float Radius = Hovered ? 30 : 28;
                ControlKit::FillCircle(Surface, Plate.MinimumX + CardX * .5f, Plate.MinimumY + PlateY * .5f,
                                       Radius, Colours.Medallion);
                ControlKit::OutlineCircle(Surface, Plate.MinimumX + CardX * .5f, Plate.MinimumY + PlateY * .5f,
                                          Radius, Hovered ? Colours.EdgeHovered : Colours.Stroke, 1);
            }
            else
                DrawRecordCrest(Surface,
                                Spanning(Plate.MinimumX + (CardX - CrestSpan) * .5f,
                                         Plate.MinimumY + (PlateY - CrestSpan) * .5f, CrestSpan, CrestSpan),
                                Entry, Hovered ? Colours.Secondary : Colours.Faintest, Scale);
            const std::string Extension = EncodeContentPath(Entry.Path.extension());
            if (!MaterialCard && !Extension.empty())
            {
                std::string Upper;
                for (char Byte : Extension)
                    Upper += static_cast<char>(Byte >= 'a' && Byte <= 'z' ? Byte - 'a' + 'A' : Byte);
                const auto Chip = Spanning(Plate.MinimumX + 8, Plate.MaximumY - 8 - 20,
                                           Surface.MeasureText(Upper.c_str(), 9).X + 24, 20);
                Surface.FillRectangle(Chip, Colours.ChipGround, 6);
                ControlKit::OutlineRounded(Surface, Chip, Colours.Stroke, 6);
                ControlKit::FillCircle(Surface, Chip.MinimumX + 9, Chip.MinimumY + 10, 3, Colours.Secondary);
                ControlKit::TextLeading(Surface, Spanning(Chip.MinimumX + 16, Chip.MinimumY + 5, Chip.Width(), 12),
                                        0, Colours.Secondary, Upper.c_str(), 9);
            }
            Surface.PushClip(Spanning(Lifted.MinimumX + 10, Plate.MaximumY, CardX - 20, CardH));
            ControlKit::TextLeading(Surface, Spanning(Lifted.MinimumX + 10, Plate.MaximumY + 8, CardX - 20, 18),
                                    0, Colours.Primary, Entry.Name.c_str(), 12);
            Surface.PopClip();
            const std::string Sized = Entry.Category == ContentCategory::Directory
                                          ? "Folder"
                                          : FormatContentOctets(Entry.ByteCountKnown ? double(Entry.ByteCount) : 0);
            ControlKit::TextLeading(Surface, Spanning(Lifted.MinimumX + 10, Plate.MaximumY + 26, CardX - 20, 14),
                                    0, Colours.Faint, Sized.c_str(), 10);
            if (!MaterialCard)
                ControlKit::OutlineRounded(Surface, Lifted,
                                           Current ? Colours.EdgeTaken
                                                   : (Hovered ? Colours.EdgeHovered : Colours.Stroke),
                                           12);
        }
        else
        {
            // 📝 The reference's list course reuses the card figure on a zero plate; the native row states
            // the same record honestly — crest, naming and size — instead of transcribing the degenerate
            // overlap.
            if (Current)
                Surface.FillRectangle(Card, Colours.Taken, 6);
            else if (Hovered)
                Surface.FillRectangle(Card, Colours.Hovered, 6);
            DrawRecordCrest(Surface, Spanning(Card.MinimumX + 10, Card.MinimumY + 9, 20, 20), Entry,
                            Current ? Colours.Primary : Colours.Secondary, Scale);
            Surface.PushClip(Spanning(Card.MinimumX + 40, Card.MinimumY, Card.Width() - 140, Card.Height()));
            ControlKit::TextLeading(Surface, Spanning(Card.MinimumX + 40, Card.MinimumY + 12, Card.Width(), 20), 0,
                                    Colours.Primary, Entry.Name.c_str(), 12);
            Surface.PopClip();
            const std::string Sized = Entry.Category == ContentCategory::Directory
                                          ? "Folder"
                                          : FormatContentOctets(Entry.ByteCountKnown ? double(Entry.ByteCount) : 0);
            const float SizedX = Surface.MeasureText(Sized.c_str(), 10).X;
            ControlKit::TextLeading(Surface, Spanning(Card.MaximumX - SizedX - 12, Card.MinimumY + 13, SizedX + 4,
                                                     14),
                                    0, Colours.Faint, Sized.c_str(), 10);
        }
        char Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "entry-%zu", Ordinal);
        if (ClickedControl(Tag, Card, Pointer))
            (void)Directory.Select(Entry.Path);
        if (Hovered && Pointer.Pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            Entry.Category == ContentCategory::Directory)
            NavigateTo = Entry.Path;
    }
    if (Visible.empty())
    {
        const std::string &Diagnostic = Directory.QueryDiagnostic();
        ControlKit::TextLeading(Surface, Spanning(Run.MinimumX + 16, Run.MinimumY + 24, Run.Width() - 32, 20), 0,
                                Colours.Faint,
                                Diagnostic.empty() ? "No record answers this search." : Diagnostic.c_str(), 12);
    }
    Surface.PopClip();
    const uint32_t Courses = Visible.empty() ? 0 : (uint32_t(Visible.size()) + Cols - 1) / Cols;
    RecordScrollbar(Surface, Run, 2, 32 + float(Courses) * (CardH + 12), Scroll, Pointer);
    if (!NavigateTo.empty() && Directory.Navigate(NavigateTo))
        ResetLocation();
}

void ContentBrowserPanel::RecordInspector(PixelSpace &Surface, const PlaneExtent &Extent,
                                         const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Extent, Colours.Aside);
    Surface.FillRectangle(Spanning(Extent.MinimumX, Extent.MinimumY, 1, Extent.Height()), Colours.Stroke);
    const auto Tongues = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 48);
    Surface.FillRectangle(Tongues, Colours.Rail);
    Surface.FillRectangle(Spanning(Tongues.MinimumX, Tongues.MaximumY - 1, Tongues.Width(), 1), Colours.Stroke);
    const float                 TongueX = (Tongues.Width() - 12) * .5f;
    const char *const           TongueNaming[2] = {"Details", "Create"};
    const ControlCentreIconCategory TongueCrest[2] = {ControlCentreIconCategory::CircleInfo,
                                                      ControlCentreIconCategory::PlusAdd};
    for (uint32_t Index = 0; Index < 2; ++Index)
    {
        const auto Tongue =
            Spanning(Tongues.MinimumX + 8 + float(Index) * (TongueX + 4), Tongues.MinimumY + 8, TongueX, 32);
        char Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "tongue-%u", Index);
        if (ClickedControl(Tag, Tongue, Pointer))
            InspectorTongue = Index;
        const bool Current = InspectorTongue == Index;
        if (Current)
            Surface.FillRectangle(Tongue, Colours.Taken, 6);
        const auto Run =
            Current ? Colours.Primary : (ControlKit::Over(Tongue, Pointer) ? Colours.Secondary : Colours.Faint);
        const float Titled = Surface.MeasureText(TongueNaming[Index], 12).X;
        const float Origin = Tongue.MinimumX + (TongueX - Titled - 20) * .5f;
        ControlKit::Glyph(Surface, Origin, Tongue.MinimumY + 9, 14, Run, TongueCrest[Index]);
        ControlKit::TextLeading(Surface, Spanning(Origin + 20, Tongue.MinimumY + 10, Titled + 4, 18), 0, Run,
                                TongueNaming[Index], 12);
    }
    const auto Preview = Spanning(Extent.MinimumX, Tongues.MaximumY, Extent.Width(), 192);
    Surface.FillRectangle(Preview, Colours.Ground);
    Surface.FillRectangle(Spanning(Preview.MinimumX, Preview.MaximumY - 1, Preview.Width(), 1), Colours.Stroke);
    const auto *Selected = Directory.QuerySelection();
    if (!Selected)
    {
        const std::string Empty = "No preview available";
        const float Titled = Surface.MeasureText(Empty.c_str(), 12).X;
        ControlKit::TextLeading(Surface,
                                Spanning(Preview.MinimumX + (Preview.Width() - Titled) * .5f,
                                         Preview.MinimumY + 192 * .5f - 8, Titled + 4, 18),
                                0, Colours.Faintest, Empty.c_str(), 12);
        const std::string Nothing = "Nothing selected";
        const float Bare = Surface.MeasureText(Nothing.c_str(), 12).X;
        ControlKit::TextLeading(Surface,
                                Spanning(Extent.MinimumX + (Extent.Width() - Bare) * .5f, Preview.MaximumY + 32,
                                         Bare + 4, 18),
                                0, Colours.Primary, Nothing.c_str(), 12);
        ControlKit::TextLeading(Surface, Spanning(Extent.MinimumX + 24, Preview.MaximumY + 54, Extent.Width(), 18),
                                0, Colours.Faint, "Select an asset to inspect its", 12);
        ControlKit::TextLeading(Surface, Spanning(Extent.MinimumX + 24, Preview.MaximumY + 72, Extent.Width(), 18),
                                0, Colours.Faint, "metadata and import options.", 12);
        return;
    }
    const ContentEntry &Record = *Selected;
    // 📝 The dimensional preview is withheld by instruction, exactly as the reference withholds its viewport:
    // the plate states what it would present rather than pretending to present it.
    RecordHatch(Surface, Preview);
    if (IsMaterialRecord(Record))
    {
        ControlKit::FillCircle(Surface, Preview.MinimumX + Preview.Width() * .5f,
                               Preview.MinimumY + 192 * .5f - 6, 28, Colours.Medallion);
        ControlKit::OutlineCircle(Surface, Preview.MinimumX + Preview.Width() * .5f,
                                  Preview.MinimumY + 192 * .5f - 6, 28, Colours.Stroke, 1);
    }
    else
        DrawRecordCrest(Surface,
                        Spanning(Preview.MinimumX + Preview.Width() * .5f - 28,
                                 Preview.MinimumY + 192 * .5f - 34, 56, 56),
                        Record, Colours.Faintest, Scale);
    ControlKit::TextLeading(Surface, Spanning(Preview.MinimumX + 8, Preview.MinimumY + 8, 120, 14), 0,
                            Colours.Faint, "2D PREVIEW", 9);
    std::string Upper;
    for (char Byte : EncodeContentPath(Record.Path.extension()))
        Upper += static_cast<char>(Byte >= 'a' && Byte <= 'z' ? Byte - 'a' + 'A' : Byte);
    const float ExtensionX = Surface.MeasureText(Upper.c_str(), 9).X;
    ControlKit::TextLeading(Surface, Spanning(Preview.MaximumX - 8 - ExtensionX, Preview.MinimumY + 8,
                                             ExtensionX + 4, 14),
                            0, Colours.Faint, Upper.c_str(), 9);
    const std::string Awaiting = "dimensional preview withheld";
    const float       Withheld = Surface.MeasureText(Awaiting.c_str(), 10).X;
    ControlKit::TextLeading(Surface,
                            Spanning(Preview.MinimumX + (Preview.Width() - Withheld) * .5f, Preview.MaximumY - 24,
                                     Withheld + 4, 16),
                            0, Colours.Faintest, Awaiting.c_str(), 10);
    const auto Crest = Spanning(Extent.MinimumX, Preview.MaximumY, Extent.Width(), 68);
    Surface.FillRectangle(Spanning(Crest.MinimumX, Crest.MaximumY - 1, Crest.Width(), 1), Colours.Stroke);
    const auto Plate = Spanning(Crest.MinimumX + 12, Crest.MinimumY + 12, 40, 40);
    Surface.FillRectangle(Plate, Colours.Medallion, 8);
    ControlKit::OutlineRounded(Surface, Plate, Colours.Stroke, 8);
    DrawRecordCrest(Surface, Spanning(Plate.MinimumX + 10, Plate.MinimumY + 10, 20, 20), Record,
                    Colours.Secondary, Scale);
    Surface.PushClip(Spanning(Plate.MaximumX + 12, Crest.MinimumY, Crest.MaximumX - Plate.MaximumX - 24, 30));
    ControlKit::TextLeading(Surface, Spanning(Plate.MaximumX + 12, Crest.MinimumY + 12, Crest.Width(), 20), 0,
                            Colours.Primary, Record.Name.c_str(), 13);
    Surface.PopClip();
    const char *ChipText = QueryContentCategoryLabel(Record.Category);
    const auto  Chip =
        Spanning(Plate.MaximumX + 12, Crest.MinimumY + 34, Surface.MeasureText(ChipText, 10).X + 12, 20);
    Surface.FillRectangle(Chip, Colours.Hatch, 4);
    ControlKit::OutlineRounded(Surface, Chip, Colours.Stroke, 4);
    ControlKit::TextLeading(Surface, Spanning(Chip.MinimumX + 6, Chip.MinimumY + 5, Chip.Width(), 12), 0,
                            Colours.Secondary, ChipText, 10);
    const auto Properties = Spanning(Extent.MinimumX, Crest.MaximumY, Extent.Width(), 118);
    Surface.FillRectangle(Spanning(Properties.MinimumX, Properties.MaximumY - 1, Properties.Width(), 1),
                          Colours.Stroke);
    ControlKit::TextLeading(Surface, Spanning(Properties.MinimumX + 12, Properties.MinimumY + 12, 160, 16), 0,
                            Colours.Faint, "PROPERTIES", 10);
    float Pair = Properties.MinimumY + 36;
    const auto RecordPair = [&](const char *Naming, const std::string &Reading)
    {
        ControlKit::TextLeading(Surface, Spanning(Properties.MinimumX + 12, Pair, 160, 18), 0, Colours.Faint,
                                Naming, 12);
        const float Span = Surface.MeasureText(Reading.c_str(), 12).X;
        ControlKit::TextLeading(Surface, Spanning(Properties.MaximumX - 12 - Span, Pair, Span + 4, 18), 0,
                                Colours.Primary, Reading.c_str(), 12);
        Pair += 20;
    };
    RecordPair("Format", Record.Category == ContentCategory::Directory ? "Folder" : Upper);
    RecordPair("Category", QueryContentCategoryLabel(Record.Category));
    RecordPair("Size", Record.Category == ContentCategory::Directory
                           ? "—"
                           : FormatContentOctets(Record.ByteCountKnown ? double(Record.ByteCount) : 0));
    const auto Import =
        Spanning(Extent.MinimumX + 12, Extent.MaximumY - 12 - 36, Extent.Width() - 24, 36);
    const bool ImportHover = ControlKit::Over(Import, Pointer);
    Surface.FillRectangle(Import, ImportHover ? Colours.EmphaticHovered : Colours.Emphatic, 6);
    const float Span = Surface.MeasureText("Import", 12).X;
    {
        auto *List = ImGui::GetWindowDrawList();
        const float GlyphX = Import.MinimumX + (Import.Width() - Span - 20) * .5f;
        const auto  At     = [&](float Dx, float Dy)
        { return ImVec2((GlyphX + Dx) * Scale, (Import.MinimumY + 11 + Dy) * Scale); };
        const auto Ink = IM_COL32(0, 0, 0, 255);
        List->AddLine(At(7, 1), At(7, 9), Ink, 1.6f * Scale);
        List->AddLine(At(7, 9), At(3.5f, 5.5f), Ink, 1.6f * Scale);
        List->AddLine(At(7, 9), At(10.5f, 5.5f), Ink, 1.6f * Scale);
        List->AddLine(At(2, 12), At(12, 12), Ink, 1.6f * Scale);
    }
    ControlKit::TextLeading(Surface,
                            Spanning(Import.MinimumX + (Import.Width() - Span - 20) * .5f + 20,
                                     Import.MinimumY + 12, Span + 4, 18),
                            0, Colours.EmphaticRun, "Import", 12);
    PresentBrowserTooltip("Import this record", Import, Pointer);
    if (ClickedControl("inspector-import", Import, Pointer))
    {
        Page = ContentBrowserPage::Import;
        ImportDirectory.ResetSelection();
        ImportOffset = 0;
    }
}

void ContentBrowserPanel::RecordImport(PixelSpace &Surface, const PlaneExtent &Extent,
                                      const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Extent, Colours.Ground);
    const auto Top = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 48);
    Surface.FillRectangle(Top, Colours.Aside);
    Surface.FillRectangle(Spanning(Top.MinimumX, Top.MaximumY - 1, Top.Width(), 1), Colours.Stroke);
    const auto Library = Spanning(Extent.MinimumX + 16, Extent.MinimumY + 10, 106, 28);
    const auto Import  = Spanning(Library.MaximumX + 8, Extent.MinimumY + 10, 82, 28);
    ImportBackExtent   = PhysicalExtent(Library);
    if (ClickedControl("import-back", Library, Pointer))
    {
        Page = ContentBrowserPage::Library;
        ImportDirectory.ResetSelection();
    }
    Surface.FillRectangle(Library, ControlKit::Over(Library, Pointer) ? Colours.Hovered : Colours.Plate, 6);
    ControlKit::TextLeading(Surface, Spanning(Library.MinimumX + 12, Library.MinimumY + 8, 90, 18), 0,
                            Colours.Primary, "Asset Browser", 12);
    PresentBrowserTooltip("Return to Asset Browser", Library, Pointer);
    Surface.FillRectangle(Import, Colours.Taken, 6);
    ControlKit::TextLeading(Surface, Spanning(Import.MinimumX + 17, Import.MinimumY + 8, 60, 18), 0,
                            Colours.Primary, "Import", 12);
    // 📝 Below the reference's width the preview yields and the sidebar narrows so the source list
    // keeps room to browse.
    const float SidebarX = Extent.Width() >= 880 ? 240 : 180;
    const float PreviewX = Extent.Width() >= 880 ? 320 : 0;
    const auto  Sidebar  = Spanning(Extent.MinimumX, Top.MaximumY, SidebarX, Extent.Height() - 48);
    const auto  Preview  = Spanning(Extent.MaximumX - PreviewX, Top.MaximumY, PreviewX, Extent.Height() - 48);
    const auto  Main     = Spanning(Sidebar.MaximumX, Top.MaximumY, Preview.MinimumX - Sidebar.MaximumX,
                                    Extent.Height() - 48);
    Surface.FillRectangle(Sidebar, Colours.Aside);
    Surface.FillRectangle(Spanning(Sidebar.MaximumX - 1, Sidebar.MinimumY, 1, Sidebar.Height()), Colours.Stroke);
    ControlKit::TextLeading(Surface, Spanning(Sidebar.MinimumX + 16, Sidebar.MinimumY + 18, 200, 16), 0,
                            Colours.Faint, "QUICK ACCESS", 10);
    if (ImportDirectory.QueryDirectory() == ImportDirectory.QueryRoot())
    {
        ImportPlaces = {ImportDirectory.QueryRoot()};
        for (const auto &Entry : ImportDirectory.QueryEntries())
            if (Entry.Category == ContentCategory::Directory)
                ImportPlaces.push_back(Entry.Path);
    }
    for (size_t Index = 0; Index < ImportPlaces.size(); ++Index)
    {
        const auto Place = Spanning(Sidebar.MinimumX + 8, Sidebar.MinimumY + 40 + Index * 32,
                                    Sidebar.Width() - 16, 28);
        const std::string Naming = EncodeContentPath(ImportPlaces[Index].filename());
        char              Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "place-%zu", Index);
        if (ClickedControl(Tag, Place, Pointer))
        {
            (void)ImportDirectory.Navigate(ImportPlaces[Index]);
            ImportDirectory.ResetSelection();
            ImportOffset = 0;
        }
        if (ControlKit::Over(Place, Pointer))
            Surface.FillRectangle(Place, Colours.Hovered, 6);
        Surface.PushClip(Place);
        ControlKit::TextLeading(Surface, Spanning(Place.MinimumX + 10, Place.MinimumY + 8, Place.Width(), 18), 0,
                                Colours.Secondary, Naming.empty() ? "Content" : Naming.c_str(), 12);
        Surface.PopClip();
        PresentBrowserTooltip(Naming.c_str(), Place, Pointer);
    }
    const std::string Location = EncodeContentPath(
        ImportDirectory.QueryDirectory().lexically_relative(ImportDirectory.QueryRoot()));
    ControlKit::TextLeading(Surface, Spanning(Main.MinimumX + 18, Main.MinimumY + 18, Main.Width() - 36, 18), 0,
                            Colours.Faint, Location == "." ? "Content" : Location.c_str(), 12);
    const auto Rows = Spanning(Main.MinimumX + 12, Main.MinimumY + 42, Main.Width() - 24, Main.Height() - 54);
    ImportListExtent = PhysicalExtent(Rows);
    Surface.PushClip(Rows);
    const auto &Visible = ImportDirectory.QueryVisible();
    ImportEntryExtents.assign(Visible.size(), {});
    const auto *Taken = ImportDirectory.QuerySelection();
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float Y = Rows.MinimumY + Ordinal * 38 - ImportOffset;
        if (Y + 34 <= Rows.MinimumY || Y >= Rows.MaximumY)
            continue;
        const auto  &Entry = ImportDirectory.QueryEntries()[Visible[Ordinal]];
        const auto   Card  = Spanning(Rows.MinimumX + 4, Y, Rows.Width() - 12, 34);
        ImportEntryExtents[Ordinal] = PhysicalExtent(Card);
        const bool Current = Taken && Taken->Path == Entry.Path;
        char       Tag[32]{};
        std::snprintf(Tag, sizeof Tag, "import-%zu", Ordinal);
        if (ClickedControl(Tag, Card, Pointer))
        {
            (void)ImportDirectory.Select(Entry.Path);
            if (Entry.Category == ContentCategory::Directory)
            {
                if (ImportDirectory.Navigate(Entry.Path))
                {
                    ImportDirectory.ResetSelection();
                    ImportOffset = 0;
                }
            }
            else
                ConfirmImport(Entry.Path);
        }
        if (Current || ControlKit::Over(Card, Pointer))
            Surface.FillRectangle(Card, Current ? Colours.Taken : Colours.Hovered, 6);
        Surface.PushClip(Spanning(Card.MinimumX + 12, Card.MinimumY, Card.Width() - 100, 34));
        ControlKit::TextLeading(Surface, Spanning(Card.MinimumX + 12, Card.MinimumY + 9, Card.Width(), 18), 0,
                                Colours.Primary, Entry.Name.c_str(), 12);
        Surface.PopClip();
        const std::string Kind = Entry.Category == ContentCategory::Directory
                                     ? "Folder"
                                     : EncodeContentPath(Entry.Path.extension());
        ControlKit::TextLeading(Surface, Spanning(Card.MaximumX - 78, Card.MinimumY + 9, 70, 16), 0,
                                Colours.Faint, Kind.c_str(), 10);
        PresentBrowserTooltip(Entry.Name.c_str(), Card, Pointer);
    }
    if (Visible.empty())
        ControlKit::TextLeading(Surface, Spanning(Rows.MinimumX + 4, Rows.MinimumY + 8, Rows.Width(), 18), 0,
                                Colours.Faint, "No record answers this search.", 12);
    Surface.PopClip();
    RecordScrollbar(Surface, Rows, 3, float(Visible.size()) * 38, ImportOffset, Pointer);
    if (PreviewX <= 0)
        return;
    Surface.FillRectangle(Preview, Colours.Aside);
    Surface.FillRectangle(Spanning(Preview.MinimumX, Preview.MinimumY, 1, Preview.Height()), Colours.Stroke);
    ControlKit::TextLeading(Surface, Spanning(Preview.MinimumX + 14, Preview.MinimumY + 16, 200, 16), 0,
                            Colours.Faint, "SELECTION", 10);
    const auto *Chosen = ImportDirectory.QuerySelection();
    if (Chosen)
    {
        ControlKit::FillCircle(Surface, Preview.MinimumX + Preview.Width() * .5f, Preview.MinimumY + 108, 54,
                               Colours.Medallion);
        ControlKit::OutlineCircle(Surface, Preview.MinimumX + Preview.Width() * .5f, Preview.MinimumY + 108, 54,
                                  Colours.Stroke, 1);
        Surface.PushClip(Spanning(Preview.MinimumX + 14, Preview.MinimumY + 182, Preview.Width() - 28, 24));
        ControlKit::TextLeading(Surface,
                                Spanning(Preview.MinimumX + 14, Preview.MinimumY + 182, Preview.Width(), 20), 0,
                                Colours.Primary, Chosen->Name.c_str(), 12);
        Surface.PopClip();
        ControlKit::TextLeading(Surface, Spanning(Preview.MinimumX + 14, Preview.MinimumY + 204, 200, 16), 0,
                                Colours.Faint,
                                Chosen->Category == ContentCategory::Directory
                                    ? "Folder"
                                    : EncodeContentPath(Chosen->Path.extension()).c_str(),
                                10);
    }
    else
        ControlKit::TextLeading(Surface, Spanning(Preview.MinimumX + 14, Preview.MinimumY + 56, 280, 18), 0,
                                Colours.Faint, "Select a source to preview.", 12);
}
void ContentBrowserPanel::ConstructLayout(PixelSpace &Surface, const PlaneExtent &Pane,
                                               const ControlPointer &Pointer)
{
    const auto Colours = BrowserColours();
    Surface.FillRectangle(Pane, Colours.Ground);
    float PageTop = Pane.MinimumY;
    if (!HostedPresentation)
    {
        // 📝 Native drawer shell above the reference page: title, maximize, close. The page below stays 1:1.
        const auto Shell = Spanning(Pane.MinimumX, Pane.MinimumY, Pane.Width(), 44);
        Surface.FillRectangle(Shell, Colours.Aside);
        Surface.FillRectangle(Spanning(Shell.MinimumX, Shell.MaximumY - 1, Shell.Width(), 1), Colours.Stroke);
        const float Left = Pane.MinimumX + 12;
        ControlKit::TextLeading(Surface, Spanning(Left, Pane.MinimumY + 8, 220, 30), 0, Colours.Primary,
                                "Content Browser", 14);
        const auto Close = Spanning(Pane.MaximumX - 12 - 28, Pane.MinimumY + 8, 28, 28);
        const auto Size = Spanning(Close.MinimumX - 8 - 118, Pane.MinimumY + 8, 118, 28);
        SizeExtent          = PhysicalExtent(Size);
        const char *Sizing  = Maximized ? "Restore" : "Maximize";
        const bool  SizeHot = ControlKit::Over(Size, Pointer);
        Surface.FillRectangle(Size, SizeHot ? Colours.Taken : Colours.Hovered, 14);
        ControlKit::OutlineRounded(Surface, Size, Colours.Stroke, 14);
        ControlKit::TextCentred(Surface, Size, Colours.Secondary, Sizing, 12);
        if (ClickedControl("size", Size, Pointer))
            Maximized = !Maximized;
        if (ControlKit::Over(Close, Pointer))
            Surface.FillRectangle(Close, Colours.Hovered, 14);
        ControlKit::GlyphCentred(Surface, Close, 14, Colours.Secondary, ControlCentreIconCategory::CloseCross);
        PresentBrowserTooltip("Close the browser", Close, Pointer);
        if (ClickedControl("close", Close, Pointer))
            ConfigureOpen(false);
        HeaderContact = ControlKit::Over(Spanning(Left, Pane.MinimumY, Pane.Width() - 24 - 154, 44), Pointer);
        PageTop += 44;
    }
    const auto Sheet = Spanning(Pane.MinimumX, PageTop, Pane.Width(), Pane.Height() - (PageTop - Pane.MinimumY));
    if (Page == ContentBrowserPage::Import)
    {
        RecordImport(Surface, Sheet, Pointer);
    }
    else if (Sheet.Width() >= 840)
    {
        const auto Aside     = Spanning(Sheet.MinimumX, Sheet.MinimumY, 240, Sheet.Height());
        const auto Inspector = Spanning(Sheet.MaximumX - 320, Sheet.MinimumY, 320, Sheet.Height());
        const auto Middle    = Spanning(Aside.MaximumX, Sheet.MinimumY, Inspector.MinimumX - Aside.MaximumX,
                                        Sheet.Height());
        const auto Rail      = Spanning(Middle.MinimumX, Middle.MinimumY, Middle.Width(), 56);
        const auto Lattice   = Spanning(Middle.MinimumX, Rail.MaximumY, Middle.Width(), Middle.Height() - 56);
        RecordSources(Surface, Aside, Pointer);
        RecordSeekRail(Surface, Rail, Pointer);
        RecordLattice(Surface, Lattice, Pointer);
        RecordInspector(Surface, Inspector, Pointer);
        FilterExtent = {};
    }
    else
    {
        // 📝 Below the reference's width the aside and inspector yield: seek rail, a category dropdown
        // carrying the source rows, the lattice, and a selection strip.
        const auto Rail    = Spanning(Sheet.MinimumX, Sheet.MinimumY, Sheet.Width(), 56);
        const auto Filter  = Spanning(Sheet.MinimumX + 16, Rail.MaximumY + 8, Sheet.Width() - 32, 30);
        const auto Strip   = Spanning(Sheet.MinimumX, Sheet.MaximumY - 58, Sheet.Width(), 58);
        const auto Lattice = Spanning(Sheet.MinimumX, Filter.MaximumY + 8, Sheet.Width(),
                                      Strip.MinimumY - Filter.MaximumY - 8);
        RecordSeekRail(Surface, Rail, Pointer);
        RecordLattice(Surface, Lattice, Pointer);
        FilterExtent = PhysicalExtent(Filter);
        static const char *CategoryNaming[] = {"All files", "Folders",     "Typefaces", "SVG icons",
                                               "Images",    "Descriptors", "Other files"};
        const uint32_t Selected = uint32_t(Category);
        if (ControlKit::Dropdown(Surface, Filter, CategoryNaming[Selected], CategoryMenu, Pointer, 1.0f).Clicked)
            CategoryMenu = !CategoryMenu;
        if (CategoryMenu)
        {
            const auto &Entries = Directory.QueryEntries();
            CategoryExtents.fill({});
            const float MenuY      = Filter.MaximumY + 4;
            const float MenuBottom = MenuY + 7 * 28 + 8;
            Surface.FillRectangle(Spanning(Filter.MinimumX, MenuY, Filter.Width(), MenuBottom - MenuY),
                                  Colours.Aside, 6);
            ControlKit::OutlineRounded(Surface, Spanning(Filter.MinimumX, MenuY, Filter.Width(), MenuBottom - MenuY),
                                       Colours.Stroke, 6);
            for (uint32_t Index = 0; Index < 7; ++Index)
            {
                const auto Option = Spanning(Filter.MinimumX, MenuY + 4 + Index * 28, Filter.Width(), 28);
                const auto Value  = static_cast<ContentCategory>(Index);
                uint32_t Count   = 0;
                for (const auto &Entry : Entries)
                    if (Value == ContentCategory::All || Entry.Category == Value ||
                        (Entry.Category == ContentCategory::Directory && Value != ContentCategory::Directory))
                        ++Count;
                char Caption[64]{};
                std::snprintf(Caption, sizeof Caption, "%s (%u)", CategoryNaming[Index], Count);
                const bool Hovered = ControlKit::Over(Option, Pointer);
                if (Value == Category)
                    Surface.FillRectangle(Option, Colours.Taken, 6);
                else if (Hovered)
                    Surface.FillRectangle(Option, Colours.Hovered, 6);
                ControlKit::TextLeading(Surface, Spanning(Option.MinimumX + 12, Option.MinimumY + 7, 240, 18), 0,
                                        Value == Category ? Colours.Primary : Colours.Secondary, Caption, 12);
                if (Option.MinimumY + 28 > MenuY && Option.MinimumY < MenuBottom)
                    CategoryExtents[Index] = PhysicalExtent(Option);
                char Tag[32]{};
                std::snprintf(Tag, sizeof Tag, "narrow-%u", Index);
                if (ClickedControl(Tag, Option, Pointer))
                {
                    Category     = Value;
                    CategoryMenu = false;
                    Directory.ConfigureFilter(Category, Search.Text);
                    Directory.ResetSelection();
                    Scroll = 0;
                }
            }
        }
        else
            CategoryExtents.fill({});
        Surface.FillRectangle(Strip, Colours.Aside);
        Surface.FillRectangle(Spanning(Strip.MinimumX, Strip.MinimumY, Strip.Width(), 1), Colours.Stroke);
        const auto *Narrowed = Directory.QuerySelection();
        if (Narrowed)
        {
            Surface.PushClip(Spanning(Strip.MinimumX + 16, Strip.MinimumY, Strip.Width() - 32, 32));
            ControlKit::TextLeading(Surface, Spanning(Strip.MinimumX + 16, Strip.MinimumY + 8, Strip.Width(), 20),
                                    0, Colours.Primary, Narrowed->Name.c_str(), 12);
            Surface.PopClip();
            const std::string Detail =
                Narrowed->Category == ContentCategory::Directory
                    ? "Folder"
                    : std::string(QueryContentCategoryLabel(Narrowed->Category)) + " · " +
                          FormatContentOctets(Narrowed->ByteCountKnown ? double(Narrowed->ByteCount) : 0);
            ControlKit::TextLeading(Surface, Spanning(Strip.MinimumX + 16, Strip.MinimumY + 30, Strip.Width(), 18),
                                    0, Colours.Faint, Detail.c_str(), 10);
        }
        else
            ControlKit::TextLeading(Surface, Spanning(Strip.MinimumX + 16, Strip.MinimumY + 20, Strip.Width(), 18),
                                    0, Colours.Faintest, "Nothing selected", 12);
    }
    if (Pointer.Released)
        PressedControl.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRAWER ADAPTATION
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::ConstructHostedInterface(float InterfaceScale, bool Enabled) noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    const auto &IO        = ImGui::GetIO();
    const auto  Position  = ImGui::GetCursorScreenPos();
    const auto  Available = ImGui::GetContentRegionAvail();
    const auto *Viewport  = ImGui::GetWindowViewport();
    Origin                = {Viewport->Pos.x, Viewport->Pos.y};
    Scale                 = std::clamp(InterfaceScale, .75f, 1.5f);
    ControlPointer Pointer;
    Pointer.X        = IO.MousePos.x / Scale;
    Pointer.Y        = IO.MousePos.y / Scale;
    Pointer.Pressed  = ImGui::IsMouseClicked(0);
    Pointer.Released = ImGui::IsMouseReleased(0);
    Pointer.Down     = ImGui::IsMouseDown(0);
    Pointer.Wheel    = IO.MouseWheel;
    Pointer.Enabled =
        Enabled && !IO.AppFocusLost && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    Focused = Enabled && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!Enabled || IO.AppFocusLost)
    {
        Search.Active = CategoryMenu = false;
        PressedControl.clear();
        ScrollDrag = 0;
    }
    HostedPresentation = true;
    PixelSpace Surface;
    if (Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale))
        try
        {
            if (Enabled)
                IntegrateKeyboard();
            ConstructLayout(Surface,
                            Spanning(Position.x / Scale, Position.y / Scale, Available.x / Scale, Available.y / Scale),
                            Pointer);
        }
        catch (const std::exception &Failure)
        {
            Surface.Text(Position.x / Scale, Position.y / Scale, ControlKit::Palette().Danger, Failure.what(), 12);
        }
    HostedPresentation = false;
    ImGui::Dummy(Available);
}

void ContentBrowserPanel::ConstructInterface(float InterfaceScale, bool Enabled) noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    auto      &IO             = ImGui::GetIO();
    const auto ViewportOrigin = ImGui::GetMainViewport()->Pos;
    Origin                    = {ViewportOrigin.x, ViewportOrigin.y};
    Scale                     = InterfaceScale;
    Expansion.ConfigureExtent(IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const float MaximumHeight = std::max(36.0f * Scale, std::min(900.0f * Scale, IO.DisplaySize.y - 86 * Scale));
    Expansion.ConfigureTravelLimit(
        Maximized ? MaximumHeight : std::min(MaximumHeight, std::max(IO.DisplaySize.y * 0.60f, 440.0f * Scale)));
    const bool Visible = Expansion.QueryHeight() > 0.5f || Expansion.QueryOpen();
    if (!Enabled || IO.AppFocusLost)
    {
        Search.Active = false;
        CategoryMenu  = false;
        PressedControl.clear();
        ScrollDrag = 0;
        Focused = false;
        if (Expansion.QueryDragging())
            Expansion.IntegratePointer(-1e10f, -1e10f, false, false, false, IO.DeltaTime, false);
    }
    const bool Interactive = Enabled && !IO.AppFocusLost;
    ImGui::SetNextWindowPos(ViewportOrigin);
    ImGui::SetNextWindowSize(IO.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Content browser overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const auto     Pane = Spanning(Origin.X / Scale, (Origin.Y + IO.DisplaySize.y - Expansion.QueryHeight()) / Scale,
                                   IO.DisplaySize.x / Scale, Expansion.QueryHeight() / Scale);
    ControlPointer Pointer;
    Pointer.X           = IO.MousePos.x / Scale;
    Pointer.Y           = IO.MousePos.y / Scale;
    Pointer.Pressed     = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    Pointer.Down        = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    Pointer.Released    = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    Pointer.Wheel       = IO.MouseWheel;
    Pointer.Enabled     = Interactive && Expansion.QueryOpen() && !Expansion.QueryDragging();
    const bool OverPane = Pane.Encloses(Pointer.X, Pointer.Y);
    const bool OverTab  = Expansion.QueryTabExtent().Encloses(IO.MousePos.x - Origin.X, IO.MousePos.y - Origin.Y);
    if (Interactive && Pointer.Pressed)
        Focused = OverPane || OverTab;
    HeaderContact = false;
    if (Visible)
    {
        try
        {
            if (Interactive)
                IntegrateKeyboard();
            ConstructLayout(Surface, Pane, Pointer);
        }
        catch (const std::exception &Failure)
        {
            Surface.FillRectangle(Pane, ControlKit::Palette().Field);
            ControlKit::TextLeading(Surface, Pane, 12, ControlKit::Palette().Danger, Failure.what(), 12);
        }
    }
    Expansion.ConstructLayout(Surface, "Content Browser", Origin);
    if (Interactive)
    {
        Expansion.IntegratePointer(IO.MousePos.x - Origin.X, IO.MousePos.y - Origin.Y, Pointer.Pressed && !CategoryMenu,
                                   Pointer.Down, Pointer.Released, IO.DeltaTime, HeaderContact);
        if (OverPane || OverTab || Expansion.QueryDragging())
            ImGui::SetNextFrameWantCaptureMouse(true);
    }
    if (!Expansion.QueryOpen())
    {
        Search.Active = false;
        CategoryMenu  = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Frontier
