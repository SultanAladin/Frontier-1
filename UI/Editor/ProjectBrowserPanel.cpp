/*==========================================================================================================================================
                                                         PROJECTBROWSERPANEL.CPP
==========================================================================================================================================*/
// 🧩 1:1 native port of the Project browser HTML prototype (Experimental/Project browser/).

#include "ProjectBrowserPanel.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     THEME SURFACES
//------------------------------------------------------------------------------------------------------------------------

ImU32 HexColour(const char *Text) noexcept
{
    auto Nibble = [](char C) -> uint32_t
    {
        if (C >= '0' && C <= '9') return uint32_t(C - '0');
        if (C >= 'a' && C <= 'f') return uint32_t(C - 'a' + 10);
        if (C >= 'A' && C <= 'F') return uint32_t(C - 'A' + 10);
        return 0;
    };
    const size_t Length = std::char_traits<char>::length(Text);
    uint32_t R = 0, G = 0, B = 0, A = 255;
    if (Length == 4) // #rgb
    {
        R = Nibble(Text[1]) * 17; G = Nibble(Text[2]) * 17; B = Nibble(Text[3]) * 17;
    }
    else if (Length == 7 || Length == 9) // #rrggbb[#aa]
    {
        R = Nibble(Text[1]) * 16 + Nibble(Text[2]);
        G = Nibble(Text[3]) * 16 + Nibble(Text[4]);
        B = Nibble(Text[5]) * 16 + Nibble(Text[6]);
        if (Length == 9) A = Nibble(Text[7]) * 16 + Nibble(Text[8]);
    }
    return IM_COL32(R, G, B, A);
}

struct BrowserSurfaces
{
    ImU32 Main, Panel, Card, Nested, Line, Text, Muted, Faint, Control, Selected, Button, ButtonText, Accent, Shadow;
    ImU32 DotProject, DotCad, DotGeometry, Star, StarFill;
};

BrowserSurfaces QueryBrowserSurfaces(const std::string &Theme)
{
    BrowserSurfaces S{};
    if (Theme == "light")
    {
        S.Main = HexColour("#f1f5f9"); S.Panel = HexColour("#ffffff"); S.Card = HexColour("#f8fafc");
        S.Nested = HexColour("#e2e8f0"); S.Line = HexColour("#0f172a14"); S.Text = HexColour("#0f172a");
        S.Muted = HexColour("#64748b"); S.Faint = HexColour("#8995a6"); S.Control = HexColour("#0f172a05");
        S.Selected = HexColour("#0f172a09"); S.Button = HexColour("#171b20"); S.ButtonText = HexColour("#ffffff");
        S.Accent = HexColour("#4d6f65"); S.Shadow = HexColour("#25323f14");
    }
    else if (Theme == "dark")
    {
        S.Main = HexColour("#111111"); S.Panel = HexColour("#1a1a1a"); S.Card = HexColour("#222225");
        S.Nested = HexColour("#2c2c30"); S.Line = HexColour("#ffffff14"); S.Text = HexColour("#ffffffe6");
        S.Muted = HexColour("#ffffff80"); S.Faint = HexColour("#ffffff48"); S.Control = HexColour("#ffffff0d");
        S.Selected = HexColour("#ffffff1a"); S.Button = HexColour("#f1f1ef"); S.ButtonText = HexColour("#151515");
        S.Accent = HexColour("#b7c6bd"); S.Shadow = HexColour("#00000009");
    }
    else // oled
    {
        S.Main = HexColour("#000000"); S.Panel = HexColour("#0a0a0a"); S.Card = HexColour("#141415");
        S.Nested = HexColour("#1c1c1e"); S.Line = HexColour("#ffffff12"); S.Text = HexColour("#fffffff2");
        S.Muted = HexColour("#ffffff70"); S.Faint = HexColour("#ffffff42"); S.Control = HexColour("#ffffff0d");
        S.Selected = HexColour("#ffffff14"); S.Button = HexColour("#f1f1ef"); S.ButtonText = HexColour("#151515");
        S.Accent = HexColour("#b7c6bd"); S.Shadow = HexColour("#00000009");
    }
    S.DotProject = HexColour("#a4bab0"); S.DotCad = HexColour("#9eafc2"); S.DotGeometry = HexColour("#b4a7bd");
    S.Star = HexColour("#e3d9b5"); S.StarFill = HexColour("#e3d9b533");
    return S;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TEXT AND ICONS
//------------------------------------------------------------------------------------------------------------------------

bool BrowserGlyphMissing(ImFont *Font, float Size, const char *Sequence) // UTF-8 sequence absent from the host font?
{
    uint32_t Codepoint = 0;
    const auto Lead = uint8_t(Sequence[0]);
    if ((Lead & 0xE0) == 0xC0 && Sequence[1])
        Codepoint = (uint32_t(Lead & 0x1F) << 6) | uint32_t(uint8_t(Sequence[1]) & 0x3F);
    else if ((Lead & 0xF0) == 0xE0 && Sequence[1] && Sequence[2])
        Codepoint = (uint32_t(Lead & 0x0F) << 12) | (uint32_t(uint8_t(Sequence[1]) & 0x3F) << 6) |
                    uint32_t(uint8_t(Sequence[2]) & 0x3F);
    else return false;
    ImFontBaked *Baked = Font->GetFontBaked(Size);
    return Baked == nullptr || Baked->FindGlyphNoFallback(ImWchar(Codepoint)) == nullptr;
}

std::string SanitiseBrowserText(const std::string &Text, ImFont *Font, float Size) // 🔤 ASCII fallback for bitmap host fonts.
{
    static const std::pair<const char *, const char *> Replacements[] = {
        {"\xE2\x80\xA6", "..."}, // … ellipsis
        {"\xE2\x80\x94", "--"}, // — em dash
        {"\xE2\x80\x93", "-"}, // – en dash
        {"\xC2\xB7", "-"}, // · middot
        {"\xC3\x97", "x"}, // × multiply
    };
    bool Needed = false;
    for (const auto &[Sequence, _] : Replacements)
        if (Text.find(Sequence) != std::string::npos && BrowserGlyphMissing(Font, Size, Sequence)) Needed = true;
    if (!Needed) return Text;
    std::string Out = Text;
    for (const auto &[Sequence, Plain] : Replacements)
    {
        if (!BrowserGlyphMissing(Font, Size, Sequence)) continue;
        size_t At = 0;
        while ((At = Out.find(Sequence, At)) != std::string::npos) { Out.replace(At, 3, Plain); At += 2; }
    }
    return Out;
}

void DrawBrowserText(ImDrawList *Draw, ImFont *Font, float Size, ImVec2 Pos, ImU32 Colour, const std::string &Text,
    float WrapWidth = 0)
{
    if (Text.empty()) return;
    const std::string Safe = SanitiseBrowserText(Text, Font, Size);
    if (WrapWidth > 0)
        Draw->AddText(Font, Size, Pos, Colour, Safe.c_str(), nullptr, WrapWidth);
    else
        Draw->AddText(Font, Size, Pos, Colour, Safe.c_str());
}

ImVec2 MeasureBrowserText(ImFont *Font, float Size, const std::string &Text, float WrapWidth = 0)
{
    if (Text.empty()) return {};
    const std::string Safe = SanitiseBrowserText(Text, Font, Size);
    if (WrapWidth > 0) return Font->CalcTextSizeA(Size, FLT_MAX, WrapWidth, Safe.c_str());
    return Font->CalcTextSizeA(Size, FLT_MAX, 0, Safe.c_str());
}

std::string EllipsizeBrowserText(ImFont *Font, float Size, const std::string &Text, float MaxWidth)
{
    const std::string Safe = SanitiseBrowserText(Text, Font, Size);
    if (Font->CalcTextSizeA(Size, FLT_MAX, 0, Safe.c_str()).x <= MaxWidth) return Safe;
    std::string Out;
    for (size_t End = Safe.size(); End > 0; --End)
    {
        Out = Safe.substr(0, End) + "...";
        if (Font->CalcTextSizeA(Size, FLT_MAX, 0, Out.c_str()).x <= MaxWidth) return Out;
    }
    return "...";
}

enum class BrowserIcon
{
    Folder, Grid, Clock, Star, Arrow, Back, Search, Plus, Cube, File, Close, ChevronLeft, ChevronRight, Minus, External
};

void DrawBrowserIcon(ImDrawList *Draw, BrowserIcon Icon, ImVec2 Min, float Box, ImU32 Colour, bool Fill = false)
{
    const auto P = [Min, Box](float X, float Y) { return ImVec2(Min.x + X / 24 * Box, Min.y + Y / 24 * Box); };
    const float Stroke = std::max(1.0f, 1.5f * Box / 24);
    switch (Icon)
    {
    case BrowserIcon::Folder:
        {
            const ImVec2 Points[] = {P(3, 21), P(3, 7), P(5, 5), P(10, 5), P(12, 8), P(19, 8), P(21, 10), P(21, 21)};
            Draw->AddPolyline(Points, 8, Colour, ImDrawFlags_Closed, Stroke);
        }
        break;
    case BrowserIcon::Grid:
        for (float Y : {4.f, 14.f})
            for (float X : {4.f, 14.f}) Draw->AddRect(P(X, Y), P(X + 6, Y + 6), Colour, Stroke, 0, Stroke);
        break;
    case BrowserIcon::Clock:
        Draw->AddCircle(P(12, 12), 8 / 24 * Box, Colour, 24, Stroke);
        {
            const ImVec2 Points[] = {P(12, 7), P(12, 12), P(15, 14)};
            Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        }
        break;
    case BrowserIcon::Star:
    {
        const ImVec2 Points[] = {P(12, 3), P(14.7f, 8.6f), P(20.9f, 9.5f), P(16.4f, 13.8f), P(17.5f, 20), P(12, 17.1f),
            P(6.5f, 20), P(7.6f, 13.8f), P(3.1f, 9.5f), P(9.3f, 8.6f)};
        if (Fill) Draw->AddConvexPolyFilled(Points, 10, Colour);
        Draw->AddPolyline(Points, 10, Colour, ImDrawFlags_Closed, Stroke);
        break;
    }
    case BrowserIcon::Arrow:
        Draw->AddLine(P(5, 12), P(19, 12), Colour, Stroke);
        {
            const ImVec2 Points[] = {P(13, 6), P(19, 12), P(13, 18)};
            Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        }
        break;
    case BrowserIcon::Back:
        Draw->AddLine(P(19, 12), P(5, 12), Colour, Stroke);
        {
            const ImVec2 Points[] = {P(11, 6), P(5, 12), P(11, 18)};
            Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        }
        break;
    case BrowserIcon::Search:
        Draw->AddCircle(P(10.5f, 10.5f), 6.5f / 24 * Box, Colour, 24, Stroke);
        Draw->AddLine(P(16, 16), P(20, 20), Colour, Stroke);
        break;
    case BrowserIcon::Plus:
        Draw->AddLine(P(12, 5), P(12, 19), Colour, Stroke);
        Draw->AddLine(P(5, 12), P(19, 12), Colour, Stroke);
        break;
    case BrowserIcon::Minus: Draw->AddLine(P(8, 12), P(16, 12), Colour, Stroke); break;
    case BrowserIcon::Cube:
        {
            const ImVec2 Points[] = {P(12, 3), P(21, 8), P(21, 16), P(12, 21), P(3, 16), P(3, 8)};
            Draw->AddPolyline(Points, 6, Colour, ImDrawFlags_Closed, Stroke);
        }
        Draw->AddLine(P(12, 13), P(21, 8), Colour, Stroke);
        Draw->AddLine(P(12, 13), P(3, 8), Colour, Stroke);
        Draw->AddLine(P(12, 13), P(12, 21), Colour, Stroke);
        Draw->AddLine(P(7.5f, 5.5f), P(16.5f, 10.5f), Colour, Stroke);
        break;
    case BrowserIcon::File:
        {
            const ImVec2 Points[] = {P(6, 3), P(14, 3), P(18, 7), P(18, 21), P(6, 21)};
            Draw->AddPolyline(Points, 5, Colour, ImDrawFlags_Closed, Stroke);
        }
        {
            const ImVec2 Points[] = {P(14, 3), P(14, 8), P(18, 8)};
            Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        }
        Draw->AddLine(P(9, 12), P(15, 12), Colour, Stroke);
        Draw->AddLine(P(9, 16), P(15, 16), Colour, Stroke);
        break;
    case BrowserIcon::Close:
        Draw->AddLine(P(6, 6), P(18, 18), Colour, Stroke);
        Draw->AddLine(P(6, 18), P(18, 6), Colour, Stroke);
        break;
    case BrowserIcon::ChevronLeft:
    {
        const ImVec2 Points[] = {P(15, 5), P(8, 12), P(15, 19)};
        Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        break;
    }
    case BrowserIcon::ChevronRight:
    {
        const ImVec2 Points[] = {P(9, 5), P(16, 12), P(9, 19)};
        Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        break;
    }
    case BrowserIcon::External:
        Draw->AddLine(P(7, 17), P(17, 7), Colour, Stroke);
        {
            const ImVec2 Points[] = {P(9, 7), P(17, 7), P(17, 15)};
            Draw->AddPolyline(Points, 3, Colour, 0, Stroke);
        }
        break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FORMATTING
//------------------------------------------------------------------------------------------------------------------------

std::string FormatBrowserCount(uint64_t Value)
{
    std::string Digits = std::to_string(Value), Out;
    size_t Lead = Digits.size() % 3;
    if (Lead == 0 && !Digits.empty()) Lead = 3;
    Out = Digits.substr(0, Lead);
    for (size_t At = Lead; At < Digits.size(); At += 3) { Out += ','; Out += Digits.substr(At, 3); }
    return Out;
}

std::string FormatBrowserNumber(double Value) // 🔢 JSON.stringify number style: 11, 3.92, 2.785.
{
    char Buffer[32]{};
    std::snprintf(Buffer, sizeof Buffer, "%.3f", Value);
    std::string Text = Buffer;
    while (Text.size() > 1 && Text.back() == '0') Text.pop_back();
    if (!Text.empty() && Text.back() == '.') Text.pop_back();
    return Text;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     PANEL STATE
//------------------------------------------------------------------------------------------------------------------------

ProjectBrowserPanel::ProjectBrowserPanel(ProjectPathSelection Selection)
    : PickProjectPath(std::move(Selection)), Entries(ConstructBrowserCatalogue())
{
    ActuatorTriangles = GenerateBrowserActuatorTriangles();
    EonLookup = GenerateBrowserEonLookup();
    std::snprintf(NameText.data(), NameText.size(), "%s", "Untitled");
    std::error_code Failure;
    const auto Current = std::filesystem::current_path(Failure);
    if (!Failure) ConfigureDirectory(Current);
}

const BrowserCatalogueEntry *ProjectBrowserPanel::FindEntry(const std::string &Token) const noexcept
{
    return FindBrowserEntry(Entries, Token);
}

BrowserCatalogueEntry *ProjectBrowserPanel::FindMutable(const std::string &Token) noexcept
{
    for (auto &Entry : Entries)
        if (Entry.Token == Token) return &Entry;
    return nullptr;
}

std::vector<const BrowserCatalogueEntry *> ProjectBrowserPanel::FilterEntries() const
{
    return FilterBrowserCatalogue(Entries, Filter, Search, Prefs.Stars, SortMode == "name");
}

std::vector<std::string> ProjectBrowserPanel::QueryFilteredTitles(
    const std::string &Filter, const std::string &Search, bool SortByName) const
{
    std::vector<std::string> Titles;
    for (const auto *Entry : FilterBrowserCatalogue(Entries, Filter, Search, Prefs.Stars, SortByName))
        Titles.push_back(Entry->Title);
    return Titles;
}

void ProjectBrowserPanel::ConfigurePreferences(const std::filesystem::path &Path)
{
    PrefsPath = Path;
    LoadPreferences();
}

void ProjectBrowserPanel::LoadPreferences()
{
    if (!PrefsPath.empty())
    {
        std::string Diagnostic;
        LoadBrowserPreferences(PrefsPath, Prefs, Diagnostic);
    }
    if (Prefs.Theme != "light" && Prefs.Theme != "dark") Prefs.Theme = "oled";
}

void ProjectBrowserPanel::PersistPreferences()
{
    if (PrefsPath.empty()) return;
    std::string Diagnostic;
    if (!SaveBrowserPreferences(PrefsPath, Prefs, Diagnostic))
        Notify("Browser storage is unavailable. Changes last for this session only.");
}

void ProjectBrowserPanel::Notify(const std::string &Message)
{
    Toast = Message;
    ToastUntil = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

bool ProjectBrowserPanel::OpenSample(const std::string &Token)
{
    const BrowserCatalogueEntry *Entry = FindEntry(Token);
    if (Entry == nullptr || Entry->Imported) return false;
    ActiveToken = Token;
    ActiveRevision = int(Entry->Revisions.size()) - 1;
    PreviewIndex = 0;
    DocumentEnabled = Entry->Category == BrowserFileCategory::Document; // 📄 Documents open on their A4 page.
    Zoom = 100;
    ActiveTab = "details";
    ResetLiveCamera(*Entry);
    LiveChannel = PreviewConfigFor(*Entry, 0, ActiveRevision).Channel;
    LiveSignature.clear();
    CaptureDelay = 2;
    FocusBack = true;
    return true;
}

void ProjectBrowserPanel::CloseFile()
{
    if (ActiveToken.empty()) return;
    ActiveToken.clear();
    FocusBrowse = true;
}

void ProjectBrowserPanel::DebugShow(const std::string &Token, int View, int Revision, bool Document, int ZoomPercent,
    const std::string &Tab, const std::string &Theme, bool Launch, bool ShowCreation)
{
    if (Theme == "light" || Theme == "dark" || Theme == "oled") Prefs.Theme = Theme;
    LaunchOpen = Launch;
    Creating = ShowCreation;
    const BrowserCatalogueEntry *Entry = Token.empty() ? nullptr : FindEntry(Token);
    if (Entry == nullptr)
    {
        ActiveToken.clear();
        return;
    }
    ActiveToken = Token;
    PreviewIndex = Entry->Views.empty() ? 0 : std::max(0, std::min(View, int(Entry->Views.size()) - 1));
    ActiveRevision = Entry->Imported ? -1
                                     : std::max(0, std::min(Revision, int(Entry->Revisions.size()) - 1));
    DocumentEnabled = Document;
    Zoom = std::max(50, std::min(200, ZoomPercent));
    ActiveTab = (Tab == "comments" || Tab == "history") ? Tab : "details";
    ResetLiveCamera(*Entry);
    LiveChannel = Entry->Imported ? 0 : PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision).Channel;
}

void ProjectBrowserPanel::ConfigureRevision(int Index)
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry == nullptr || Entry->Imported || Index < 0 || size_t(Index) >= Entry->Revisions.size()) return;
    ActiveRevision = Index;
    ResetLiveCamera(*Entry);
    LiveChannel = PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision).Channel;
    CaptureDelay = 2;
}

void ProjectBrowserPanel::ResetLiveCamera(const BrowserCatalogueEntry &Entry)
{
    const std::string Title =
        PreviewIndex >= 0 && size_t(PreviewIndex) < Entry.Views.size() ? Entry.Views[size_t(PreviewIndex)] : "";
    LiveCamera = ConfigureBrowserCamera(int(Entry.Category), size_t(std::max(0, PreviewIndex)),
        Entry.Extension == ".sketch" || Title.find("Sketch") != std::string::npos);
    LiveDragging = false;
}

void ProjectBrowserPanel::ConfigureZoom(int Amount)
{
    Zoom = std::max(50, std::min(200, Amount));
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry != nullptr && !Entry->Imported && !DocumentEnabled && ConfigureLivePreview)
    {
        LiveCamera.Distance = (Entry->Category == BrowserFileCategory::Project ? 17 : 10.5) * 100 / Zoom;
        CaptureDelay = 1;
    }
}

void ProjectBrowserPanel::ToggleStar(const std::string &Token)
{
    if (Prefs.Stars.count(Token)) Prefs.Stars.erase(Token);
    else Prefs.Stars.insert(Token);
    PersistPreferences();
}

BrowserPreviewConfiguration ProjectBrowserPanel::PreviewConfigFor(
    const BrowserCatalogueEntry &Entry, size_t View, int Revision) const
{
    return ConfigureBrowserPreview(Entry, View, size_t(std::max(0, Revision)));
}

std::string ProjectBrowserPanel::SnapshotKeyFor(
    const BrowserCatalogueEntry &Entry, size_t View, int Revision) const
{
    return BrowserSnapshotKey(Entry.Token, View, size_t(std::max(0, Revision)));
}

const ProjectBrowserPanel::CachedPreviewTriangles &ProjectBrowserPanel::PreviewTrianglesFor(
    const BrowserCatalogueEntry &Entry, int Revision)
{
    const std::string Key = Entry.Token + ":" + std::to_string(std::max(0, Revision));
    const auto Cached = PreviewCache.find(Key);
    if (Cached != PreviewCache.end()) return Cached->second;
    CachedPreviewTriangles Built;
    Built.Triangles = GenerateBrowserPreviewTriangles(ActuatorTriangles, int(Entry.Category), std::max(0, Revision));
    Built.Statistics = QueryBrowserPreviewStatistics(Built.Triangles);
    Built.Uv = GenerateBrowserCylindricalUv(Built.Triangles);
    return PreviewCache.emplace(Key, std::move(Built)).first->second;
}

void ProjectBrowserPanel::CaptureNow()
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry == nullptr || Entry->Imported || DocumentEnabled || !CaptureLiveThumbnail) return;
    const size_t View = size_t(std::max(0, PreviewIndex));
    CaptureLiveThumbnail(SnapshotKeyFor(*Entry, View, ActiveRevision), PreviewConfigFor(*Entry, View, ActiveRevision),
        LiveCamera, LiveChannel);
}

size_t ProjectBrowserPanel::AttachPaths(const std::vector<std::filesystem::path> &Paths, bool AutoOpen)
{
    size_t Attached = 0;
    size_t Scanned = 0;
    for (const auto &Path : Paths)
    {
        if (Scanned++ >= 200) break; // 📎 The prototype slices every picker payload to 200 files.
        const std::u8string RawName = Path.filename().u8string();
        const std::string Name(RawName.begin(), RawName.end());
        const size_t Dot = Name.find_last_of('.');
        const std::string Extension = FoldBrowserText(Dot == std::string::npos ? "" : Name.substr(Dot));
        bool Supported = false;
        const BrowserFileCategory Category = ClassifyBrowserImport(Extension, Supported);
        if (!Supported) continue;
        std::error_code Status;
        const uint64_t Bytes = std::filesystem::is_regular_file(Path, Status) && !Status
                                   ? std::filesystem::file_size(Path, Status)
                                   : 0;
        if (!Status && Bytes > 100ull * 1024 * 1024)
        {
            Notify("Files over 100 MiB are not loaded by this design prototype.");
            continue;
        }
        const bool IsImage = QueryBrowserImageExtension(Extension), IsMarkdown = Extension == ".md";
        if (IsMarkdown && !Status && Bytes > 262144)
        {
            Notify("Markdown preview is limited to 256 KiB.");
            continue;
        }
        BrowserCatalogueEntry Entry;
        Entry.Token = "local-" + std::to_string(AttachSerial++) + "-" + std::to_string(Attached);
        Entry.Title = Dot == std::string::npos ? Name : Name.substr(0, Dot);
        Entry.Extension = Extension;
        Entry.Category = Category;
        Entry.Description = "A local file selected for a read-only browser preview.";
        Entry.Art = Category == BrowserFileCategory::Cad ? "Assembly"
                                                        : Category == BrowserFileCategory::Project ? "Valley" : "Geometry";
        Entry.Context = "Local file · read-only";
        Entry.Opened = "Just now";
        const std::u8string RawPath = Path.u8string();
        Entry.Path.assign(RawPath.begin(), RawPath.end()); // 📂 Native dialogs return real paths.
        Entry.Views = {Category == BrowserFileCategory::Project ? "Project preview"
                                                                : Category == BrowserFileCategory::Cad ? "CAD preview"
                                                                                                      : Category == BrowserFileCategory::Document ? "Document" : "Geometry preview"};
        Entry.Imported = true;
        Entry.ByteCount = Bytes;
        if (IsImage)
        {
            std::ifstream Input(Path, std::ios::binary);
            Entry.ImageBytes.assign(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
            Entry.Views = {"Image"};
        }
        if (IsMarkdown)
        {
            std::ifstream Input(Path, std::ios::binary);
            const std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
            Entry.Markdown = Text.substr(0, 262144);
        }
        Entries.insert(Entries.begin(), Entry);
        ++Attached;
    }
    Filter = "all";
    Search.clear();
    SearchText[0] = '\0';
    if (Attached == 1 && AutoOpen)
    {
        const BrowserCatalogueEntry &Entry = Entries.front();
        ActiveToken = Entry.Token;
        ActiveRevision = int(Entry.Revisions.size()) - 1;
        PreviewIndex = 0;
        DocumentEnabled = Entry.Category == BrowserFileCategory::Document;
        Zoom = 100;
        ActiveTab = "details";
        FocusBack = true;
    }
    Notify(Attached ? std::to_string(Attached) + " local file" + (Attached > 1 ? "s" : "") +
                              " attached. Native project decoding is only connected for .projectspace files."
                          : "No supported files found. Choose .project, .assembly, .sketch, geometry, Markdown or image files.");
    return Attached;
}

void ProjectBrowserPanel::OpenInFrontier()
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry == nullptr) return;
    // 🚀 Real native hand-off for attached project containers; the launch dialog previews every other target.
    if (Entry->Imported && Entry->Extension == ".projectspace")
    {
        namespace fs = std::filesystem;
        std::error_code Status;
        const bool Exists = fs::is_regular_file(fs::path(Entry->Path), Status);
        if (!Status && Exists && SelectProject(fs::path(Entry->Path)))
        {
            if (RequestOpenProject()) return;
            Diagnostic.clear();
        }
    }
    LaunchOpen = true;
}

void ProjectBrowserPanel::SubmitComment()
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    const std::string Text = CommentText.data();
    const size_t First = Text.find_first_not_of(" \t\r\n"), Last = Text.find_last_not_of(" \t\r\n");
    if (Entry == nullptr || First == std::string::npos) return;
    Prefs.Notes[Entry->Token].push_back({"You", "Y", Text.substr(First, Last - First + 1).substr(0, 2000), "Just now"});
    CommentText[0] = '\0';
    PersistPreferences();
    Notify("Note saved in this browser. It has not been written to a project file.");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     REAL PROJECT CONTAINERS
//------------------------------------------------------------------------------------------------------------------------

bool ProjectBrowserPanel::ConfigureDirectory(const std::filesystem::path &Requested)
{
    if (!Directory.ConfigureRoot(Requested))
    {
        Diagnostic = Directory.QueryDiagnostic();
        return false;
    }
    const auto Text = EncodeContentPath(Directory.QueryDirectory());
    std::snprintf(DirectoryText.data(), DirectoryText.size(), "%s", Text.c_str());
    Diagnostic.clear();
    return true;
}

bool ProjectBrowserPanel::SelectProject(const std::filesystem::path &Path)
{
    ProjectSpaceDocument Candidate;
    if (!ProjectSpaceCodec::Decode(Path, Candidate, Diagnostic)) return false;
    SelectedPath = Path;
    SelectedProject = std::move(Candidate);
    Diagnostic.clear();
    return true;
}

bool ProjectBrowserPanel::CreateProject(const std::filesystem::path &Destination, const std::string &Name)
{
    const auto Template = ResolveContentDirectory("EngineContent/Defaults") / "BlankProject/Blank.projectspace";
    if (!ProjectSpaceCodec::CreateBlank(Template, Destination, Name, Diagnostic)) return false;
    const auto Folder = Destination.has_parent_path() ? Destination.parent_path() : std::filesystem::path(".");
    (void)ConfigureDirectory(Folder);
    return SelectProject(Destination);
}

bool ProjectBrowserPanel::RequestOpenProject()
{
    if (SelectedPath.empty())
    {
        Diagnostic = "Select a project first.";
        return false;
    }
    // 📝 Revalidate immediately before hand-off; selection may outlive a file edit or removal.
    if (!SelectProject(SelectedPath)) return false;
    LaunchPath = SelectedPath;
    return true;
}

void ProjectBrowserPanel::ResetSelection() noexcept
{
    SelectedPath.clear();
    Diagnostic.clear();
    ActiveToken.clear();
    LaunchOpen = false;
    Creating = false;
    Toast.clear();
}

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     WIDGETS
//------------------------------------------------------------------------------------------------------------------------

struct BrowserFrame
{
    ImDrawList *Draw = nullptr;
    ImFont *Font = nullptr;
    float Scale = 1;
    BrowserSurfaces S{};
    float Width = 0;
    bool Narrow = false; // ≤700px: compact rails, wrapped headers.
};

bool PlaceBrowserHit(ImVec2 Min, ImVec2 Size, const char *Id)
{
    ImGui::SetCursorPos(ImVec2(Min.x - ImGui::GetWindowPos().x + ImGui::GetScrollX(),
        Min.y - ImGui::GetWindowPos().y + ImGui::GetScrollY()));
    ImGui::PushID(Id);
    const bool Clicked = ImGui::InvisibleButton("##hit", Size);
    ImGui::PopID();
    return Clicked;
}

float DrawBrowserParagraph(ImDrawList *Draw, ImFont *Font, float Size, ImVec2 Pos, ImU32 Colour,
    const std::string &Text, float WrapWidth, float LineHeight) // 📝 Greedy wrap with exact line rhythm.
{
    const std::string Safe = SanitiseBrowserText(Text, Font, Size);
    float Y = Pos.y;
    size_t Begin = 0;
    const auto Flush = [&](const std::string &Line)
    {
        if (Draw != nullptr && !Line.empty())
            Draw->AddText(Font, Size, ImVec2(Pos.x, Y), Colour, Line.c_str());
        Y += LineHeight;
    };
    while (Begin <= Safe.size())
    {
        const size_t Break = Safe.find('\n', Begin);
        const std::string Line = Safe.substr(Begin, Break == std::string::npos ? Break : Break - Begin);
        if (WrapWidth <= 0 || Font->CalcTextSizeA(Size, FLT_MAX, 0, Line.c_str()).x <= WrapWidth) Flush(Line);
        else
        {
            size_t Word = 0;
            std::string Row;
            while (Word < Line.size())
            {
                const size_t End = Line.find(' ', Word);
                const std::string Token = Line.substr(Word, End == std::string::npos ? End : End - Word);
                const std::string Trial = Row.empty() ? Token : Row + " " + Token;
                if (Font->CalcTextSizeA(Size, FLT_MAX, 0, Trial.c_str()).x <= WrapWidth) Row = Trial;
                else
                {
                    if (!Row.empty()) Flush(Row);
                    Row = Token;
                }
                Word = End == std::string::npos ? Line.size() : End + 1;
            }
            Flush(Row);
        }
        if (Break == std::string::npos) break;
        Begin = Break + 1;
    }
    return Y - Pos.y;
}

void DrawBrowserSpaced(ImDrawList *Draw, ImFont *Font, float Size, ImVec2 Pos, ImU32 Colour, const std::string &Text,
    float Tracking) // 🔤 Letter-spaced labels (eyebrows, rail titles, tags).
{
    const std::string Safe = SanitiseBrowserText(Text, Font, Size);
    float X = Pos.x;
    char Buffer[8]{};
    for (size_t At = 0; At < Safe.size();)
    {
        size_t Length = 1;
        const auto Lead = uint8_t(Safe[At]);
        if ((Lead & 0xE0) == 0xC0) Length = 2;
        else if ((Lead & 0xF0) == 0xE0) Length = 3;
        else if ((Lead & 0xF8) == 0xF0) Length = 4;
        Length = std::min(Length, Safe.size() - At);
        std::copy_n(Safe.c_str() + At, Length, Buffer);
        Buffer[Length] = '\0';
        Draw->AddText(Font, Size, ImVec2(X, Pos.y), Colour, Buffer);
        X += Font->CalcTextSizeA(Size, FLT_MAX, 0, Buffer).x + Tracking;
        At += Length;
    }
}

bool DrawBrowserPrimary(const char *Id, ImVec2 Min, const std::string &Label, BrowserIcon Icon, bool HasIcon,
    const BrowserFrame &Frame, float FontSize, float PadX, float Height, float IconBox = 14)
{
    const float Gap = 10 * Frame.Scale;
    const float TextWidth = MeasureBrowserText(Frame.Font, FontSize, Label).x;
    const float Width = PadX * 2 + TextWidth + (HasIcon ? IconBox * Frame.Scale + Gap : 0);
    const ImVec2 Size(Width, Height);
    const bool Clicked = PlaceBrowserHit(Min, Size, Id);
    const bool Hovered = ImGui::IsItemHovered(), Active = ImGui::IsItemActive();
    ImU32 Fill = Frame.S.Button;
    if (Hovered || Active) // 🌗 :hover keeps the fill at 85% opacity.
        Fill = IM_COL32((Fill >> 0) & 255, (Fill >> 8) & 255, (Fill >> 16) & 255, 217);
    const float Lift = Active ? 1 * Frame.Scale : 0; // 🖱️ :active translates 1px down.
    Frame.Draw->AddRectFilled(ImVec2(Min.x, Min.y + Lift), ImVec2(Min.x + Width, Min.y + Height + Lift), Fill,
        Height / 2);
    float X = Min.x + PadX;
    const float CentreY = Min.y + Lift + (Height - IconBox * Frame.Scale) / 2;
    if (HasIcon)
    {
        DrawBrowserIcon(Frame.Draw, Icon, ImVec2(X, CentreY), IconBox * Frame.Scale, Frame.S.ButtonText);
        X += IconBox * Frame.Scale + Gap;
    }
    DrawBrowserText(Frame.Draw, Frame.Font, FontSize, ImVec2(X, Min.y + Lift + (Height - FontSize) / 2),
        Frame.S.ButtonText, Label);
    return Clicked;
}

bool DrawBrowserIconButton(const char *Id, ImVec2 Min, float Box, BrowserIcon Icon, const BrowserFrame &Frame,
    bool Bordered = true, ImU32 Colour = 0, float IconBox = 16, bool Enabled = true)
{
    if (Colour == 0) Colour = Frame.S.Text;
    const bool Clicked = Enabled ? PlaceBrowserHit(Min, ImVec2(Box, Box), Id) : false;
    const bool Hovered = Enabled && ImGui::IsItemHovered();
    if (Bordered)
    {
        if (Hovered) Frame.Draw->AddRectFilled(Min, ImVec2(Min.x + Box, Min.y + Box), Frame.S.Selected, 7 * Frame.Scale);
        Frame.Draw->AddRect(Min, ImVec2(Min.x + Box, Min.y + Box), Hovered ? Frame.S.Faint : Frame.S.Line,
            7 * Frame.Scale, 0, std::max(1.f, Frame.Scale));
    }
    else if (Hovered)
        Frame.Draw->AddRectFilled(Min, ImVec2(Min.x + Box, Min.y + Box), Frame.S.Selected, 7 * Frame.Scale);
    const float Glyph = IconBox * Frame.Scale;
    DrawBrowserIcon(Frame.Draw, Icon, ImVec2(Min.x + (Box - Glyph) / 2, Min.y + (Box - Glyph) / 2), Glyph,
        Enabled ? Colour : Frame.S.Faint);
    return Clicked;
}

bool DrawBrowserCollapseToggle(
    const char *Id, ImVec2 Min, const BrowserFrame &Frame, bool Collapsed, bool LeftSide)
{
    const float Width = 27 * Frame.Scale, Height = 29 * Frame.Scale;
    const bool Clicked = PlaceBrowserHit(Min, ImVec2(Width, Height), Id);
    if (ImGui::IsItemHovered())
        Frame.Draw->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + Height), Frame.S.Selected, 6 * Frame.Scale);
    const BrowserIcon Glyph = LeftSide == Collapsed ? BrowserIcon::ChevronRight : BrowserIcon::ChevronLeft;
    DrawBrowserIcon(Frame.Draw, Glyph, ImVec2(Min.x + 5 * Frame.Scale, Min.y + 5 * Frame.Scale), 19 * Frame.Scale,
        Frame.S.Muted);
    return Clicked;
}

bool DrawBrowserTextButton(const char *Id, ImVec2 Min, const std::string &Label, const BrowserFrame &Frame,
    float FontSize, BrowserIcon Icon = BrowserIcon::External, bool HasIcon = false, float IconGap = 0)
{
    const float TextWidth = MeasureBrowserText(Frame.Font, FontSize, Label).x;
    const float Glyph = FontSize * 1.1f;
    const float Width = TextWidth + (HasIcon ? IconGap + Glyph : 0);
    const bool Clicked = PlaceBrowserHit(Min, ImVec2(Width, FontSize * 1.4f), Id);
    DrawBrowserText(Frame.Draw, Frame.Font, FontSize, Min, ImGui::IsItemHovered() ? Frame.S.Text : Frame.S.Muted, Label);
    if (HasIcon)
        DrawBrowserIcon(Frame.Draw, Icon, ImVec2(Min.x + TextWidth + IconGap, Min.y - 1 * Frame.Scale), Glyph,
            ImGui::IsItemHovered() ? Frame.S.Text : Frame.S.Muted);
    return Clicked;
}

void DrawBrowserCrossline(ImVec2 Min, float Width, const BrowserFrame &Frame)
{
    Frame.Draw->AddLine(ImVec2(Min.x, Min.y), ImVec2(Min.x + Width, Min.y), Frame.S.Line, std::max(1.f, Frame.Scale));
    DrawBrowserText(Frame.Draw, Frame.Font, 9 * Frame.Scale, ImVec2(Min.x - 3 * Frame.Scale, Min.y - 7 * Frame.Scale),
        Frame.S.Faint, "+");
    const float Plus = MeasureBrowserText(Frame.Font, 9 * Frame.Scale, "+").x;
    DrawBrowserText(Frame.Draw, Frame.Font, 9 * Frame.Scale,
        ImVec2(Min.x + Width + 3 * Frame.Scale - Plus, Min.y - 7 * Frame.Scale), Frame.S.Faint, "+");
}

float DrawBrowserChip(ImVec2 Min, const std::string &Label, const BrowserFrame &Frame, float FontSize = 8)
{
    const float Size = FontSize * Frame.Scale;
    const float Width = MeasureBrowserText(Frame.Font, Size, Label).x + 14 * Frame.Scale;
    const float Height = Size + 8 * Frame.Scale;
    Frame.Draw->AddRect(Min, ImVec2(Min.x + Width, Min.y + Height), Frame.S.Line, 4 * Frame.Scale, 0,
        std::max(1.f, Frame.Scale));
    DrawBrowserText(Frame.Draw, Frame.Font, Size, ImVec2(Min.x + 7 * Frame.Scale, Min.y + 4 * Frame.Scale),
        Frame.S.Faint, Label);
    return Width;
}

float BrowserComboHeight(const BrowserFrame &Frame)
{
    // 📐 Native closed-combo frame: app font + the shared theme's vertical frame padding.
    return ImGui::GetFontSize() + 16 * Frame.Scale;
}
float BrowserComboWidth(const BrowserFrame &Frame, const std::vector<std::string> &Options)
{
    float TextW = 0;
    for (const auto &Option : Options) TextW = std::max(TextW, ImGui::CalcTextSize(Option.c_str()).x);
    return TextW + 24 * Frame.Scale + BrowserComboHeight(Frame);
}
int DrawBrowserSelect(const char *Id, ImVec2 Min, float Width, const std::vector<std::string> &Options, int Current,
    const BrowserFrame &Frame, float FontSize, float PadLeft, float PadRight, float Radius, ImU32 Fill,
    float PadY, bool Disabled = false)
{
    // 📝 Native reusable combo: the bespoke hit-test + manual popup never opened reliably.
    // Closed-frame metrics now come from the shared ControlKit theme; keep the signature stable.
    (void)FontSize;
    (void)PadLeft;
    (void)PadRight;
    (void)Radius;
    (void)Fill;
    (void)PadY;
    ImGui::SetCursorPos(ImVec2(Min.x - ImGui::GetWindowPos().x + ImGui::GetScrollX(),
        Min.y - ImGui::GetWindowPos().y + ImGui::GetScrollY()));
    ImGui::PushID(Id);
    ImGui::SetNextItemWidth(Width);
    const std::string Label = Current >= 0 && size_t(Current) < Options.size() ? Options[size_t(Current)] : "";
    const std::string Preview = EllipsizeBrowserText(ImGui::GetFont(), ImGui::GetFontSize(), Label,
        Width - 24 * Frame.Scale - BrowserComboHeight(Frame));
    int Next = Current;
    ImGui::BeginDisabled(Disabled);
    if (DropdownControlSurface::BeginSelection("##select", Preview.c_str(), Frame.Scale))
    {
        for (size_t Index = 0; Index < Options.size(); ++Index)
            if (DropdownControlSurface::Option(Options[Index].c_str(), int(Index) == Current, Frame.Scale))
            {
                Next = int(Index);
                ImGui::CloseCurrentPopup();
            }
        DropdownControlSurface::EndSelection();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return Next;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THUMBNAIL TILES
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructThumbnailTile(ImVec2 Min, ImVec2 Size, const BrowserCatalogueEntry &Entry,
    size_t View, int Revision, BrowserThumbFit Fit, float Scale, float Rounding, ImDrawFlags RoundFlags, float Zoom)
{
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    ImFont *Font = ImGui::GetFont();
    const auto Pending = [&](const char *Label)
    {
        Draw->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + Size.y), IM_COL32(0, 0, 0, 255), Rounding,
            RoundFlags);
        const ImVec2 Text = MeasureBrowserText(Font, 9 * Scale, Label);
        DrawBrowserText(Draw, Font, 9 * Scale, ImVec2(Min.x + (Size.x - Text.x) / 2, Min.y + (Size.y - Text.y) / 2),
            HexColour("#aaaaaa"), Label);
    };
    const auto Blit = [&](const BrowserPreviewImage &Image)
    {
        float U0 = Image.U0, V0 = Image.V0, U1 = Image.U1, V1 = Image.V1;
        const float ZoomInset = Zoom > 1 ? (1 - 1 / Zoom) / 2 : 0; // 🔍 :hover scales the artwork.
        const float SpanU = U1 - U0, SpanV = V1 - V0;
        U0 += SpanU * ZoomInset;
        U1 -= SpanU * ZoomInset;
        V0 += SpanV * ZoomInset;
        V1 -= SpanV * ZoomInset;
        const float TileAspect = Size.x / std::max(1.f, Size.y);
        const float CaptureAspect = Image.Width / std::max(1.f, Image.Height);
        if (Fit == BrowserThumbFit::Cover) // 🖼️ object-fit:cover crops the 1.56 capture into the tile box.
        {
            if (TileAspect > CaptureAspect)
            {
                const float Keep = CaptureAspect / TileAspect, Span = V1 - V0;
                V0 += Span * (1 - Keep) / 2;
                V1 -= Span * (1 - Keep) / 2;
            }
            else
            {
                const float Keep = TileAspect / CaptureAspect, Span = U1 - U0;
                U0 += Span * (1 - Keep) / 2;
                U1 -= Span * (1 - Keep) / 2;
            }
            Draw->AddImageRounded((ImTextureID)Image.Texture, Min, ImVec2(Min.x + Size.x, Min.y + Size.y),
                ImVec2(U0, V0), ImVec2(U1, V1), IM_COL32(255, 255, 255, 255), Rounding, RoundFlags);
        }
        else // 🖼️ object-fit:contain letterboxes on black.
        {
            Draw->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + Size.y), IM_COL32(0, 0, 0, 255), Rounding,
                RoundFlags);
            float W = Size.x, H = Size.y;
            if (TileAspect > CaptureAspect) W = H * CaptureAspect;
            else H = W / CaptureAspect;
            const ImVec2 At(Min.x + (Size.x - W) / 2, Min.y + (Size.y - H) / 2);
            Draw->AddImage((ImTextureID)Image.Texture, At, ImVec2(At.x + W, At.y + H), ImVec2(U0, V0), ImVec2(U1, V1));
        }
    };
    if (Entry.Imported)
    {
        if (!Entry.ImageBytes.empty() && QueryImportedImage)
        {
            const BrowserPreviewImage Image = QueryImportedImage(Entry.Token, Entry.ImageBytes);
            if (Image.Texture)
            {
                Blit(Image);
                return;
            }
            if (!Image.Failed)
            {
                Pending("Rendering preview…");
                return;
            }
        }
        Draw->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + Size.y), S.Card, Rounding, RoundFlags);
        const float Glyph = 18 * Scale;
        DrawBrowserIcon(Draw, BrowserIcon::File, ImVec2(Min.x + (Size.x - Glyph) / 2, Min.y + (Size.y - Glyph) / 2),
            Glyph, S.Faint);
        return;
    }
    if (View < Entry.Views.size() && Entry.Views[View] == "UV layout") // 🗺️ UV tiles are canvas-drawn in HTML too.
    {
        Draw->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + Size.y), HexColour("#0a1211"));
        const ImU32 Grid = HexColour("#30443b");
        for (int Index = 0; Index <= 10; ++Index)
        {
            Draw->AddLine(ImVec2(Min.x + Size.x * Index / 10, Min.y),
                ImVec2(Min.x + Size.x * Index / 10, Min.y + Size.y), Grid, 1);
            Draw->AddLine(ImVec2(Min.x, Min.y + Size.y * Index / 10),
                ImVec2(Min.x + Size.x, Min.y + Size.y * Index / 10), Grid, 1);
        }
        const CachedPreviewTriangles &Preview = PreviewTrianglesFor(Entry, Revision);
        const std::vector<BrowserUvSegment> Segments = GenerateBrowserCylindricalUv(Preview.Triangles);
        const ImU32 Ink = HexColour("#b2c8bb70");
        const float PadX = 20.0f / 600 * Size.x, PadY = 20.0f / 380 * Size.y;
        for (const auto &Segment : Segments)
        {
            const ImVec2 A(Min.x + PadX + float(Segment.Ends[0][0]) * (Size.x - 2 * PadX),
                Min.y + PadY + float(Segment.Ends[0][1]) * (Size.y - 2 * PadY));
            const ImVec2 B(Min.x + PadX + float(Segment.Ends[1][0]) * (Size.x - 2 * PadX),
                Min.y + PadY + float(Segment.Ends[1][1]) * (Size.y - 2 * PadY));
            Draw->AddLine(A, B, Ink, 1);
        }
        return;
    }
    if (!QueryThumbnailImage)
    {
        Pending("Renderer unavailable");
        return;
    }
    const BrowserPreviewImage Image = QueryThumbnailImage(SnapshotKeyFor(Entry, View, Revision));
    if (!Image.Texture)
    {
        Pending("Rendering preview…");
        return;
    }
    Blit(Image);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     LIBRARY VIEW
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ActivateToken(const std::string &Token)
{
    const BrowserCatalogueEntry *Entry = FindEntry(Token);
    if (Entry == nullptr) return;
    if (!Entry->Imported)
    {
        OpenSample(Token);
        return;
    }
    ActiveToken = Token;
    ActiveRevision = -1;
    PreviewIndex = 0;
    DocumentEnabled = Entry->Category == BrowserFileCategory::Document;
    Zoom = 100;
    ActiveTab = "details";
    FocusBack = true;
}

bool ProjectBrowserPanel::DrawLibraryRail(ImVec2 Origin, float Width, float Height, float HeaderH, float Scale)
{
    BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, QueryBrowserSurfaces(Prefs.Theme), 0,
        false};
    ImDrawList *Draw = Frame.Draw;
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), Frame.S.Panel);
    Draw->AddLine(ImVec2(Origin.x + Width - 1, Origin.y), ImVec2(Origin.x + Width - 1, Origin.y + Height),
        Frame.S.Line, std::max(1.f, Scale));
    Draw->AddLine(ImVec2(Origin.x, Origin.y + HeaderH - 1), ImVec2(Origin.x + Width, Origin.y + HeaderH - 1),
        Frame.S.Line, std::max(1.f, Scale));
    const float HeaderCentre = Origin.y + (HeaderH - 29 * Scale) / 2;
    if (LeftCollapsed)
    {
        if (DrawBrowserCollapseToggle("lib-expand", ImVec2(Origin.x + (Width - 27 * Scale) / 2, HeaderCentre), Frame,
                true, true))
        {
            LeftCollapsed = false;
            return true;
        }
        return false;
    }
    DrawBrowserText(Draw, Frame.Font, 12 * Scale, ImVec2(Origin.x + 11 * Scale, Origin.y + (HeaderH - 12 * Scale) / 2),
        Frame.S.Text, "Files");
    if (DrawBrowserCollapseToggle("lib-collapse", ImVec2(Origin.x + Width - 11 * Scale - 27 * Scale, HeaderCentre),
            Frame, false, true))
        LeftCollapsed = true;
    return false;
}

void ProjectBrowserPanel::ConstructLibrary(float Width)
{
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const bool Narrow = Width <= 700;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, S, Width, Narrow};
    ImDrawList *Draw = Frame.Draw;
    ImFont *Font = Frame.Font;
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Avail = ImGui::GetContentRegionAvail();
    const float LeftW = (LeftCollapsed ? 44 : (Narrow ? 140 : 204)) * Scale;
    const float RightW = (RightCollapsed ? 44 : (Narrow ? 220 : 302)) * Scale;
    const float CentreW = std::max(0.f, Avail.x - LeftW - RightW);
    const float HeaderH = (Narrow ? 72 : 64) * Scale;
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Avail.x, Origin.y + Avail.y), S.Main);
    if (DrawLibraryRail(Origin, LeftW, Avail.y, HeaderH, Scale) && Width < 700) RightCollapsed = true;
    // 🧭 Library rail filters.
    if (!LeftCollapsed)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * Scale);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, S.Nested);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, S.Muted);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, S.Muted);
        ImGui::SetCursorPos(ImVec2(0, HeaderH));
        ImGui::BeginChild("##librail", ImVec2(LeftW, Avail.y - HeaderH), false,
            ImGuiWindowFlags_NoBackground);
        Frame.Draw = ImGui::GetWindowDrawList();
        const ImVec2 Rail = ImGui::GetWindowPos();
        const float PadX = (Narrow ? 9 : 16) * Scale;
        float Y = Rail.y + (Narrow ? 18 : 27) * Scale;
        const float ContentW = LeftW - PadX * 2;
        const auto RailTitle = [&](const char *Label, float Top)
        {
            Y += Top;
            DrawBrowserSpaced(Frame.Draw, Font, (Narrow ? 8 : 9) * Scale,
                ImVec2(Rail.x + PadX + (Narrow ? 4 : 13) * Scale, Y), S.Faint, Label, 1.5f * Scale);
            Y += (Narrow ? 8 : 9) * Scale + 13 * Scale;
        };
        const auto Nav = [&](const char *Id, const char *Key, const char *Label, BrowserIcon Icon, bool Dot,
                             ImU32 DotColour, const char *Count)
        {
            const float RowH = (Narrow ? 9 * 2 + 14 : 11 * 2 + 18) * Scale;
            const ImVec2 Min(Rail.x + PadX, Y);
            const bool Clicked = PlaceBrowserHit(Min, ImVec2(ContentW, RowH), Id);
            const bool Active = Filter == Key, Hovered = ImGui::IsItemHovered();
            if (Active || Hovered)
            {
                Frame.Draw->AddRectFilled(Min, ImVec2(Min.x + ContentW, Min.y + RowH), S.Selected, 7 * Scale);
                if (Active)
                    Frame.Draw->AddRect(Min, ImVec2(Min.x + ContentW, Min.y + RowH), S.Line, 7 * Scale, 0,
                        std::max(1.f, Scale));
            }
            const float Glyph = (Narrow ? 14 : 18) * Scale;
            const float Pad = (Narrow ? 8 : 12) * Scale;
            if (Dot)
                Frame.Draw->AddRectFilled(ImVec2(Min.x + Pad, Min.y + (RowH - 7 * Scale) / 2),
                    ImVec2(Min.x + Pad + 7 * Scale, Min.y + (RowH + 7 * Scale) / 2), DotColour, 2 * Scale);
            else
                DrawBrowserIcon(Frame.Draw, Icon, ImVec2(Min.x + Pad, Min.y + (RowH - Glyph) / 2), Glyph,
                    Active ? S.Text : S.Muted);
            DrawBrowserText(Frame.Draw, Font, (Narrow ? 10 : 13) * Scale,
                ImVec2(Min.x + Pad + Glyph + 11 * Scale, Min.y + (RowH - (Narrow ? 10 : 13) * Scale) / 2),
                Active ? S.Text : S.Muted, Label);
            if (Count)
                DrawBrowserText(Frame.Draw, Font, 10 * Scale,
                    ImVec2(Min.x + ContentW - Pad - MeasureBrowserText(Font, 10 * Scale, Count).x,
                        Min.y + (RowH - 10 * Scale) / 2),
                    S.Faint, Count);
            Y += RowH + 4 * Scale;
            if (Clicked) Filter = Key;
        };
        RailTitle("WORKSPACE", 0);
        Nav("nav-all", "all", "All files", BrowserIcon::Grid, false, 0, std::to_string(Entries.size()).c_str());
        Nav("nav-recent", "recent", "Recent", BrowserIcon::Clock, false, 0, nullptr);
        Nav("nav-starred", "starred", "Starred", BrowserIcon::Star, false, 0, nullptr);
        RailTitle("FORMATS", 30 * Scale - 4 * Scale);
        Nav("nav-project", "project", "Projects", BrowserIcon::Grid, true, S.DotProject, nullptr);
        Nav("nav-cad", "cad", "CAD", BrowserIcon::Grid, true, S.DotCad, nullptr);
        Nav("nav-geometry", "geometry", "Geometry", BrowserIcon::Grid, true, S.DotGeometry, nullptr);
        const float BlockH = (18 + 14 + 14 + 6 + 36 + 17 + 16) * Scale; // 📌 margin-top:auto pins the block bottom.
        Y = std::max(Y + (Narrow ? 32 : 45) * Scale,
            Rail.y + Avail.y - HeaderH - 22 * Scale - BlockH + (Narrow ? 0 : 0));
        const float BottomX = Rail.x + PadX + (Narrow ? 4 : 12) * Scale;
        DrawBrowserIcon(Frame.Draw, BrowserIcon::Folder, ImVec2(BottomX, Y), 18 * Scale, S.Muted);
        Y += (18 + 14) * Scale;
        DrawBrowserText(Frame.Draw, Font, 11 * Scale, ImVec2(BottomX, Y), S.Text, "Your files. Your folders.");
        Y += 11 * Scale + 6 * Scale;
        Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(BottomX, Y), S.Muted,
            "Browse from any location.\nNo fixed project directory.", ContentW - 12 * Scale, 18 * Scale);
        Y += 17 * Scale;
        if (DrawBrowserTextButton("browse-rail", ImVec2(BottomX, Y), "Browse files", Frame, 11 * Scale,
                BrowserIcon::External, true, 18 * Scale))
            if (PickImportFiles) AttachPaths(PickImportFiles());
            else Notify("File import is not available here.");
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
        Frame.Draw = Draw;
    }
    // 🔍 Library header: title, theme pill, Open from disk.
    const ImVec2 Head(Origin.x + LeftW, Origin.y);
    Draw->AddRectFilled(Head, ImVec2(Head.x + CentreW, Head.y + HeaderH), S.Panel);
    Draw->AddLine(ImVec2(Head.x, Head.y + HeaderH - 1), ImVec2(Head.x + CentreW, Head.y + HeaderH - 1), S.Line,
        std::max(1.f, Scale));
    const std::vector<std::string> Themes = {"OLED", "Light", "Dark"};
    const int ThemeIndex = Prefs.Theme == "light" ? 1 : Prefs.Theme == "dark" ? 2 : 0;
    const float ThemeW = BrowserComboWidth(Frame, Themes);
    const auto ImportFromDisk = [&]
    {
        if (PickImportFiles) AttachPaths(PickImportFiles());
        else Notify("File import is not available here.");
    };
    if (!Narrow)
    {
        DrawBrowserText(Draw, Font, 16 * Scale, ImVec2(Head.x + 18 * Scale, Head.y + (HeaderH - 16 * Scale) / 2),
            S.Text, "Project browser");
        const float ButtonH = 28 * Scale;
        const float ButtonW = MeasureBrowserText(Font, 10 * Scale, "Open from disk").x + 26 * Scale + 14 * Scale + 10 * Scale;
        const ImVec2 ButtonMin(Head.x + CentreW - 18 * Scale - ButtonW, Head.y + (HeaderH - ButtonH) / 2);
        const int Picked = DrawBrowserSelect("theme", ImVec2(ButtonMin.x - 10 * Scale - ThemeW, Head.y + (HeaderH - BrowserComboHeight(Frame)) / 2),
            ThemeW, Themes, ThemeIndex, Frame, (Width <= 1100 ? 9 : 10) * Scale, 11 * Scale, 20 * Scale,
            30 * Scale / 2, IM_COL32(0, 0, 0, 0), 7 * Scale);
        if (Picked != ThemeIndex)
        {
            Prefs.Theme = Picked == 1 ? "light" : Picked == 2 ? "dark" : "oled";
            PersistPreferences();
        }
        if (FocusBrowse) { ImGui::SetCursorPos(ImVec2(ButtonMin.x - Origin.x, ButtonMin.y - Origin.y)); ImGui::SetKeyboardFocusHere(); }
        if (DrawBrowserPrimary("browse", ButtonMin, "Open from disk", BrowserIcon::Folder, true, Frame, 10 * Scale,
                13 * Scale, ButtonH))
            ImportFromDisk();
        FocusBrowse = false;
    }
    else
    {
        DrawBrowserText(Draw, Font, 13 * Scale, ImVec2(Head.x + 9 * Scale, Head.y + 8 * Scale), S.Text,
            "Project browser");
        const float NarrowThemeW = std::min(ThemeW,
            CentreW - MeasureBrowserText(Font, 13 * Scale, "Project browser").x - 30 * Scale);
        const int Picked = DrawBrowserSelect("theme",
            ImVec2(Head.x + CentreW - 9 * Scale - NarrowThemeW,
                Head.y + (HeaderH - BrowserComboHeight(Frame)) / 2),
            NarrowThemeW, Themes, ThemeIndex, Frame, 9 * Scale, 8 * Scale, 12 * Scale, 30 * Scale / 2,
            IM_COL32(0, 0, 0, 0), 5 * Scale);
        if (Picked != ThemeIndex)
        {
            Prefs.Theme = Picked == 1 ? "light" : Picked == 2 ? "dark" : "oled";
            PersistPreferences();
        }
        if (DrawBrowserPrimary("browse", ImVec2(Head.x + 9 * Scale, Head.y + 40 * Scale), "Open from disk",
                BrowserIcon::Folder, true, Frame, 9 * Scale, 9 * Scale, 21 * Scale, 12))
            ImportFromDisk();
    }
    // 🗂️ Cards, empty state, folder access.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * Scale);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, S.Nested);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, S.Muted);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, S.Muted);
    ImGui::SetCursorPos(ImVec2(LeftW, HeaderH));
    ImGui::BeginChild("##libscroll", ImVec2(CentreW, Avail.y - HeaderH), false,
        ImGuiWindowFlags_NoBackground);
    Frame.Draw = ImGui::GetWindowDrawList();
    {
        const ImVec2 Scroll = ImVec2(ImGui::GetWindowPos().x - ImGui::GetScrollX(),
        ImGui::GetWindowPos().y - ImGui::GetScrollY());
        const float Side = Narrow ? 14 * Scale : std::min(48 * Scale, std::max(20 * Scale, Width * 0.03f));
        float Y = Scroll.y + (Narrow ? 20 : 28) * Scale;
        const float InnerW = CentreW - Side * 2;
        const std::vector<const BrowserCatalogueEntry *> Listed = FilterEntries();
        // Section heading + controls.
        const std::string Title = BrowserCollectionTitle(Filter);
        const std::string Count = BrowserCollectionCount(Listed);
        const float TitleW = MeasureBrowserText(Font, 17 * Scale, Title).x;
        const float CountW = MeasureBrowserText(Font, 10 * Scale, Count).x;
        const float SearchW = (Narrow ? InnerW : 130 * Scale + 15 * Scale + 9 * 2 * Scale + 18 * Scale + 9 * Scale + 9 * Scale);
        const std::vector<std::string> SortOptions = {"Last opened", "Name A–Z"};
        const float SortW = BrowserComboWidth(Frame, SortOptions);
        if (!Narrow)
        {
            DrawBrowserText(Frame.Draw, Font, 17 * Scale, ImVec2(Scroll.x + Side, Y), S.Text, Title);
            DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side + TitleW + 14 * Scale, Y + 4 * Scale),
                S.Muted, Count);
            const float ControlsY = Y - 2 * Scale;
            const float SortX = Scroll.x + Side + InnerW - SortW;
            const int Picked = DrawBrowserSelect("sort", ImVec2(SortX, ControlsY + (29 * Scale - BrowserComboHeight(Frame)) / 2), SortW,
                SortOptions, SortMode == "name" ? 1 : 0, Frame, 10 * Scale, 10 * Scale, 24 * Scale,
                7 * Scale, IM_COL32(0, 0, 0, 0), 7 * Scale);
            SortMode = Picked == 1 ? "name" : "recent";
            const float BoxW = SearchW, BoxH = 29 * Scale;
            const ImVec2 Box(SortX - 12 * Scale - BoxW, ControlsY);
            Frame.Draw->AddRectFilled(Box, ImVec2(Box.x + BoxW, Box.y + BoxH), S.Panel, 8 * Scale);
            Frame.Draw->AddRect(Box, ImVec2(Box.x + BoxW, Box.y + BoxH), S.Line, 8 * Scale, 0, std::max(1.f, Scale));
            DrawBrowserIcon(Frame.Draw, BrowserIcon::Search, ImVec2(Box.x + 9 * Scale, Box.y + 7 * Scale),
                15 * Scale, S.Faint);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, S.Text);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, S.Faint);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            const float InputX = Box.x + 9 * Scale + 15 * Scale + 9 * Scale;
            ImGui::SetCursorPos(ImVec2(InputX - Scroll.x + ImGui::GetScrollX(), Box.y + 7 * Scale - Scroll.y + ImGui::GetScrollY()));
            ImGui::SetNextItemWidth(130 * Scale);
            if (FocusSearch) ImGui::SetKeyboardFocusHere();
            ImGui::PushID("search");
            if (ImGui::InputTextWithHint("##input", "Search files…", SearchText.data(), SearchText.size()))
                Search = SearchText.data();
            ImGui::PopID();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            const float KbdW = MeasureBrowserText(Font, 10 * Scale, "/").x + 10 * Scale;
            const ImVec2 Kbd(Box.x + BoxW - 9 * Scale - KbdW, Box.y + 7 * Scale);
            Frame.Draw->AddRect(Kbd, ImVec2(Kbd.x + KbdW, Kbd.y + 15 * Scale), S.Line, 3 * Scale, 0,
                std::max(1.f, Scale));
            DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(Kbd.x + 5 * Scale, Kbd.y + 1 * Scale), S.Faint, "/");
            Y += std::max(29 * Scale, BrowserComboHeight(Frame)) + 23 * Scale;
        }
        else
        {
            DrawBrowserText(Frame.Draw, Font, 17 * Scale, ImVec2(Scroll.x + Side, Y), S.Text, Title);
            DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side + TitleW + 14 * Scale, Y + 4 * Scale),
                S.Muted, Count);
            Y += 24 * Scale + 10 * Scale;
            const float BoxH = 29 * Scale;
            const ImVec2 Box(Scroll.x + Side, Y);
            Frame.Draw->AddRectFilled(Box, ImVec2(Box.x + InnerW, Box.y + BoxH), S.Panel, 8 * Scale);
            Frame.Draw->AddRect(Box, ImVec2(Box.x + InnerW, Box.y + BoxH), S.Line, 8 * Scale, 0,
                std::max(1.f, Scale));
            DrawBrowserIcon(Frame.Draw, BrowserIcon::Search, ImVec2(Box.x + 9 * Scale, Box.y + 7 * Scale),
                15 * Scale, S.Faint);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, S.Text);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, S.Faint);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::SetCursorPos(ImVec2(Box.x + 33 * Scale - Scroll.x + ImGui::GetScrollX(),
                Box.y + 7 * Scale - Scroll.y + ImGui::GetScrollY()));
            ImGui::SetNextItemWidth(InnerW - 33 * Scale - 30 * Scale);
            if (FocusSearch) ImGui::SetKeyboardFocusHere();
            ImGui::PushID("search");
            if (ImGui::InputTextWithHint("##input", "Search files…", SearchText.data(), SearchText.size()))
                Search = SearchText.data();
            ImGui::PopID();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            Y += BoxH + 10 * Scale;
            const int Picked = DrawBrowserSelect("sort", ImVec2(Scroll.x + Side + InnerW - SortW, Y), SortW,
                SortOptions, SortMode == "name" ? 1 : 0, Frame, 10 * Scale, 10 * Scale, 24 * Scale,
                7 * Scale, IM_COL32(0, 0, 0, 0), 7 * Scale);
            SortMode = Picked == 1 ? "name" : "recent";
            Y += BrowserComboHeight(Frame) + 23 * Scale;
        }
        FocusSearch = false;
        const float Gap = Width > 1100 ? 22 * Scale : Width > 540 ? 15 * Scale : 12 * Scale;
        const int Columns = Width >= 1500 ? 3 : Narrow ? 1 : 2;
        const float CardW = (InnerW - Gap * (Columns - 1)) / Columns;
        const float PreviewH = CardW / 1.65f;
        const float TextH = (15 + 17 + 5 + 12 + 14 + 1 + 10 + 11 + 14) * Scale;
        const float CardH = PreviewH + TextH;
        float RowY = Y;
        int Column = 0;
        const auto PlaceCard = [&](const std::function<void(ImVec2)> &Paint)
        {
            const ImVec2 Min(Scroll.x + Side + Column * (CardW + Gap), RowY);
            Paint(Min);
            if (++Column >= Columns)
            {
                Column = 0;
                RowY += CardH + Gap;
            }
        };
        // ➕ Native creation card (the prototype has no creation flow; the native browser keeps Blank Project).
        PlaceCard(
            [&](ImVec2 Min)
            {
                const bool Clicked = PlaceBrowserHit(Min, ImVec2(CardW, CardH), "create-card");
                const bool Hovered = ImGui::IsItemHovered();
                const float Lift = Hovered ? 3 * Scale : 0;
                const ImVec2 Top(Min.x, Min.y - Lift);
                Frame.Draw->AddRectFilled(Top, ImVec2(Top.x + CardW, Top.y + CardH), S.Panel, 12 * Scale);
                Frame.Draw->AddRect(Top, ImVec2(Top.x + CardW, Top.y + CardH), Hovered ? S.Faint : S.Line,
                    12 * Scale, 0, std::max(1.f, Scale));
                Frame.Draw->AddRectFilled(ImVec2(Top.x + 1, Top.y + 1),
                    ImVec2(Top.x + CardW - 1, Top.y + PreviewH), S.Card, 12 * Scale,
                    ImDrawFlags_RoundCornersTop);
                DrawBrowserIcon(Frame.Draw, BrowserIcon::Plus,
                    ImVec2(Top.x + (CardW - 30 * Scale) / 2, Top.y + (PreviewH - 30 * Scale) / 2), 30 * Scale,
                    S.Faint);
                const float TX = Top.x + 16 * Scale;
                float TY = Top.y + PreviewH + 15 * Scale;
                DrawBrowserText(Frame.Draw, Font, 13 * Scale, ImVec2(TX, TY), S.Text, "Blank Project");
                DrawBrowserText(Frame.Draw, Font, 9 * Scale,
                    ImVec2(TX + MeasureBrowserText(Font, 13 * Scale, "Blank Project").x + 8 * Scale, TY + 3 * Scale),
                    S.Faint, ".projectspace");
                TY += 17 * Scale + 5 * Scale;
                DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(TX, TY), S.Muted,
                    "Start from an empty project container");
                TY += 12 * Scale + 14 * Scale;
                Frame.Draw->AddLine(ImVec2(TX, TY), ImVec2(Top.x + CardW - 16 * Scale, TY), S.Line,
                    std::max(1.f, Scale));
                TY += 10 * Scale;
                DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(TX, TY), S.Faint, "Preset");
                if (Clicked) Creating = true;
            });
        for (const BrowserCatalogueEntry *Entry : Listed)
        {
            PlaceCard(
                [&](ImVec2 Min)
                {
                    const std::string HitId = "card-" + Entry->Token;
                    const bool Clicked = PlaceBrowserHit(Min, ImVec2(CardW, CardH), HitId.c_str());
                    const bool Hovered = ImGui::IsItemHovered();
                    const float Lift = Hovered ? 3 * Scale : 0;
                    const ImVec2 Top(Min.x, Min.y - Lift);
                    Frame.Draw->AddRectFilled(Top, ImVec2(Top.x + CardW, Top.y + CardH), S.Panel, 12 * Scale);
                    Frame.Draw->AddRect(Top, ImVec2(Top.x + CardW, Top.y + CardH), Hovered ? S.Faint : S.Line,
                        12 * Scale, 0, std::max(1.f, Scale));
                    ConstructThumbnailTile(ImVec2(Top.x + 1, Top.y + 1), ImVec2(CardW - 2, PreviewH - 1), *Entry,
                        0, 0, BrowserThumbFit::Cover, Scale, 12 * Scale, ImDrawFlags_RoundCornersTop,
                        Hovered ? 1.035f : 1);
                    if (Entry->Imported)
                    {
                        const ImVec2 Tag(Top.x + 12 * Scale, Top.y + PreviewH - 10 * Scale - 14 * Scale);
                        const float TagW =
                            MeasureBrowserText(Font, 8 * Scale, "LOCAL FILE").x + 5 * 8 * 1 * Scale + 12 * Scale;
                        Frame.Draw->AddRectFilled(Tag, ImVec2(Tag.x + TagW, Tag.y + 14 * Scale), HexColour("#00000005"),
                            4 * Scale);
                        DrawBrowserSpaced(Frame.Draw, Font, 8 * Scale,
                            ImVec2(Tag.x + 6 * Scale, Tag.y + 3 * Scale), HexColour("#e9ede9b3"), "LOCAL FILE",
                            1 * Scale);
                    }
                    const float TX = Top.x + 16 * Scale;
                    float TY = Top.y + PreviewH + 15 * Scale;
                    const std::string TitleText =
                        EllipsizeBrowserText(Font, 13 * Scale, Entry->Title, CardW - 32 * Scale - 50 * Scale);
                    DrawBrowserText(Frame.Draw, Font, 13 * Scale, ImVec2(TX, TY), S.Text, TitleText);
                    DrawBrowserText(Frame.Draw, Font, 9 * Scale,
                        ImVec2(TX + MeasureBrowserText(Font, 13 * Scale, TitleText).x + 8 * Scale, TY + 3 * Scale),
                        S.Faint, Entry->Extension);
                    TY += 17 * Scale + 5 * Scale;
                    DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(TX, TY), S.Muted,
                        EllipsizeBrowserText(Font, 10 * Scale, Entry->Context, CardW - 32 * Scale));
                    TY += 12 * Scale + 14 * Scale;
                    Frame.Draw->AddLine(ImVec2(TX, TY), ImVec2(Top.x + CardW - 16 * Scale, TY), S.Line,
                        std::max(1.f, Scale));
                    TY += 10 * Scale;
                    const float Dot = Entry->Category == BrowserFileCategory::Document ? 0 : 5 * Scale;
                    if (Dot > 0)
                        Frame.Draw->AddRectFilled(ImVec2(TX, TY + 3 * Scale), ImVec2(TX + Dot, TY + 3 * Scale + Dot),
                            Entry->Category == BrowserFileCategory::Project ? S.DotProject
                            : Entry->Category == BrowserFileCategory::Cad   ? S.DotCad
                                                                            : S.DotGeometry,
                            2 * Scale);
                    DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(TX + Dot + (Dot > 0 ? 6 : 0) * Scale, TY),
                        S.Faint, BrowserCategoryLabel(Entry->Category));
                    const ImVec2 OpenedSize = MeasureBrowserText(Font, 9 * Scale, Entry->Opened);
                    DrawBrowserText(Frame.Draw, Font, 9 * Scale,
                        ImVec2(Top.x + CardW - 16 * Scale - OpenedSize.x, TY), S.Faint, Entry->Opened);
                    const ImVec2 StarMin(Top.x + CardW - 9 * Scale - 27 * Scale, Top.y + 9 * Scale);
                    Frame.Draw->AddRectFilled(StarMin, ImVec2(StarMin.x + 27 * Scale, StarMin.y + 27 * Scale),
                        HexColour("#00000005"), 7 * Scale);
                    Frame.Draw->AddRect(StarMin, ImVec2(StarMin.x + 27 * Scale, StarMin.y + 27 * Scale),
                        HexColour("#ffffff15"), 7 * Scale, 0, std::max(1.f, Scale));
                    const bool Starred = Prefs.Stars.count(Entry->Token) > 0;
                    const std::string StarId = "star-" + Entry->Token;
                    const bool StarClicked =
                        PlaceBrowserHit(StarMin, ImVec2(27 * Scale, 27 * Scale), StarId.c_str());
                    DrawBrowserIcon(Frame.Draw, BrowserIcon::Star,
                        ImVec2(StarMin.x + 6.5f * Scale, StarMin.y + 6.5f * Scale), 14 * Scale,
                        Starred ? S.Star : HexColour("#ffffff88"), Starred);
                    if (StarClicked) ToggleStar(Entry->Token);
                    else if (Clicked) ActivateToken(Entry->Token);
                });
        }
        if (Column != 0) RowY += CardH + Gap;
        if (Listed.empty())
        {
            float EY = Y + 80 * Scale;
            const float CX = Scroll.x + Side + InnerW / 2;
            DrawBrowserIcon(Frame.Draw, BrowserIcon::Search, ImVec2(CX - 15 * Scale, EY), 30 * Scale, S.Muted);
            EY += 30 * Scale + 15 * Scale;
            const std::string Title2 = "No matching files";
            DrawBrowserText(Frame.Draw, Font, 17 * Scale,
                ImVec2(CX - MeasureBrowserText(Font, 17 * Scale, Title2).x / 2, EY), S.Text, Title2);
            EY += 17 * Scale + 10 * Scale;
            const std::string Hint = "Try another name or choose a different format.";
            DrawBrowserText(Frame.Draw, Font, 10 * Scale,
                ImVec2(CX - MeasureBrowserText(Font, 10 * Scale, Hint).x / 2, EY), S.Muted, Hint);
            EY += 12 * Scale + 20 * Scale;
            const float ButtonW = MeasureBrowserText(Font, 13 * Scale, "Clear search").x + 28 * Scale;
            const ImVec2 BMin(CX - ButtonW / 2, EY);
            const bool Clicked = PlaceBrowserHit(BMin, ImVec2(ButtonW, 36 * Scale), "clear-search");
            const bool Hovered = ImGui::IsItemHovered();
            Frame.Draw->AddRectFilled(BMin, ImVec2(BMin.x + ButtonW, BMin.y + 36 * Scale),
                Hovered ? S.Selected : S.Control, 9 * Scale);
            Frame.Draw->AddRect(BMin, ImVec2(BMin.x + ButtonW, BMin.y + 36 * Scale),
                Hovered ? S.Faint : S.Line, 9 * Scale, 0, std::max(1.f, Scale));
            DrawBrowserText(Frame.Draw, Font, 13 * Scale,
                ImVec2(BMin.x + 14 * Scale, BMin.y + (36 * Scale - 13 * Scale) / 2), S.Text, "Clear search");
            if (Clicked)
            {
                Search.clear();
                SearchText[0] = '\0';
            }
            RowY = EY + 36 * Scale + Gap;
        }
        const float FolderW =
            MeasureBrowserText(Font, 11 * Scale, "Browse a folder").x + 28 * Scale + 18 * Scale + 9 * Scale;
        const ImVec2 FolderMin(Scroll.x + Side + InnerW - FolderW, RowY + 25 * Scale);
        const bool FolderClicked = PlaceBrowserHit(FolderMin, ImVec2(FolderW, 38 * Scale), "browse-folder");
        const bool FolderHovered = ImGui::IsItemHovered();
        if (FolderHovered)
            Frame.Draw->AddRectFilled(FolderMin, ImVec2(FolderMin.x + FolderW, FolderMin.y + 38 * Scale), S.Selected,
                21 * Scale);
        Frame.Draw->AddRect(FolderMin, ImVec2(FolderMin.x + FolderW, FolderMin.y + 38 * Scale),
            FolderHovered ? S.Faint : S.Line, 21 * Scale, 0, std::max(1.f, Scale));
        DrawBrowserIcon(Frame.Draw, BrowserIcon::Folder,
            ImVec2(FolderMin.x + 14 * Scale, FolderMin.y + 10 * Scale), 18 * Scale, S.Text);
        DrawBrowserText(Frame.Draw, Font, 11 * Scale,
            ImVec2(FolderMin.x + 14 * Scale + 18 * Scale + 9 * Scale, FolderMin.y + (38 * Scale - 11 * Scale) / 2),
            S.Text, "Browse a folder");
        if (FolderClicked)
        {
            if (!PickImportFolder) Notify("Folder import is not available here.");
            else
            {
                const std::filesystem::path Folder = PickImportFolder();
                std::vector<std::filesystem::path> Found;
                if (!Folder.empty())
                {
                    std::error_code Status;
                    for (const auto &Item :
                        std::filesystem::recursive_directory_iterator(Folder, Status))
                    {
                        if (Status || Found.size() >= 200) break;
                        if (Item.is_regular_file()) Found.push_back(Item.path());
                    }
                    std::sort(Found.begin(), Found.end());
                }
                if (!Found.empty()) AttachPaths(Found);
                else Notify("No supported files found. Choose .project, .assembly, .sketch, geometry, Markdown or image files.");
            }
        }
        const std::string Note = "Sample files · attached .projectspace files open in the editor";
        DrawBrowserText(Frame.Draw, Font, 9 * Scale,
            ImVec2(Scroll.x + Side + (InnerW - MeasureBrowserText(Font, 9 * Scale, Note).x) / 2,
                FolderMin.y + 38 * Scale + 24 * Scale),
            S.Faint, Note);
        // 📜 Custom drawing never advances the cursor: pin an invisible item at the content bottom
        // so the scrollbar range tracks the cards instead of sticking at zero.
        ImGui::SetCursorPos(ImVec2(1,
        FolderMin.y + (38 + 24 + 14) * Scale - ImGui::GetWindowPos().y + ImGui::GetScrollY()));
        ImGui::Dummy(ImVec2(1, 1));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
    // 📋 Library inspector placeholder.
    const ImVec2 Right(Origin.x + LeftW + CentreW, Origin.y);
    Draw->AddRectFilled(Right, ImVec2(Right.x + RightW, Right.y + Avail.y), S.Panel);
    Draw->AddLine(ImVec2(Right.x, Right.y), ImVec2(Right.x, Right.y + Avail.y), S.Line, std::max(1.f, Scale));
    Draw->AddLine(ImVec2(Right.x, Right.y + HeaderH - 1), ImVec2(Right.x + RightW, Right.y + HeaderH - 1), S.Line,
        std::max(1.f, Scale));
    if (RightCollapsed)
    {
        if (DrawBrowserCollapseToggle("lib-rcollapse", ImVec2(Right.x + (RightW - 27 * Scale) / 2,
                                                 Right.y + (HeaderH - 29 * Scale) / 2),
                Frame, true, false))
        {
            RightCollapsed = false;
            if (Width < 700) LeftCollapsed = true;
        }
        return;
    }
    float TabX = Right.x + 20 * Scale;
    const char *Tabs[] = {"Details", "Comments", "History"};
    for (int Index = 0; Index < 3; ++Index)
    {
        const ImU32 Colour = Index == 0 ? S.Text : S.Muted;
        DrawBrowserText(Draw, Font, 10 * Scale, ImVec2(TabX, Right.y + (HeaderH - 10 * Scale) / 2),
            IM_COL32((Colour >> 0) & 255, (Colour >> 8) & 255, (Colour >> 16) & 255,
                uint32_t(((Colour >> 24) & 255) * 0.65f)),
            Tabs[Index]);
        TabX += MeasureBrowserText(Font, 10 * Scale, Tabs[Index]).x + 16 * Scale;
    }
    if (DrawBrowserCollapseToggle("lib-rcollapse", ImVec2(Right.x + RightW - 13 * Scale - 27 * Scale,
                                                 Right.y + (HeaderH - 29 * Scale) / 2),
            Frame, false, false))
        RightCollapsed = true;
    DrawBrowserIcon(Draw, BrowserIcon::File, ImVec2(Right.x + 24 * Scale, Right.y + HeaderH + 36 * Scale),
        18 * Scale, S.Faint);
    DrawBrowserText(Draw, Font, 11 * Scale, ImVec2(Right.x + 24 * Scale, Right.y + HeaderH + 36 * Scale + 32 * Scale),
        S.Faint, "Select a file to preview it.");
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     READER VIEW
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructUvArt(
    ImVec2 Min, ImVec2 Size, const BrowserCatalogueEntry &Entry, int Revision, float Scale)
{
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + Size.y), HexColour("#0a1211"));
    const ImU32 Grid = HexColour("#30443b");
    for (int Index = 0; Index <= 10; ++Index)
    {
        Draw->AddLine(ImVec2(Min.x + Size.x * Index / 10, Min.y), ImVec2(Min.x + Size.x * Index / 10, Min.y + Size.y),
            Grid, 1);
        Draw->AddLine(ImVec2(Min.x, Min.y + Size.y * Index / 10), ImVec2(Min.x + Size.x, Min.y + Size.y * Index / 10),
            Grid, 1);
    }
    const CachedPreviewTriangles &Preview = PreviewTrianglesFor(Entry, Revision);
    const ImU32 Ink = HexColour("#b2c8bb70");
    const float PadX = 20.0f / 600 * Size.x, PadY = 20.0f / 380 * Size.y;
    for (const auto &Segment : Preview.Uv)
    {
        const ImVec2 A(Min.x + PadX + float(Segment.Ends[0][0]) * (Size.x - 2 * PadX),
            Min.y + PadY + float(Segment.Ends[0][1]) * (Size.y - 2 * PadY));
        const ImVec2 B(Min.x + PadX + float(Segment.Ends[1][0]) * (Size.x - 2 * PadX),
            Min.y + PadY + float(Segment.Ends[1][1]) * (Size.y - 2 * PadY));
        Draw->AddLine(A, B, Ink, 1);
    }
}

void ProjectBrowserPanel::ConstructReader(float Width)
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry == nullptr)
    {
        CloseFile();
        ConstructLibrary(Width);
        return;
    }
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const bool Narrow = Width <= 700;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, S, Width, Narrow};
    ImDrawList *Draw = Frame.Draw;
    ImFont *Font = Frame.Font;
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Avail = ImGui::GetContentRegionAvail();
    const float LeftW = (LeftCollapsed ? 44 : (Narrow ? 140 : 204)) * Scale;
    const float RightW = (RightCollapsed ? 44 : (Narrow ? 220 : 302)) * Scale;
    const float CentreW = std::max(0.f, Avail.x - LeftW - RightW);
    const float HeaderH = (Narrow ? 72 : 64) * Scale;
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + Avail.x, Origin.y + Avail.y), S.Main);
    const std::string RailName = BrowserRailTitle(*Entry);
    // 🧭 Reader rail: version thumbs + reference document.
    Draw->AddRectFilled(Origin, ImVec2(Origin.x + LeftW, Origin.y + Avail.y), S.Panel);
    Draw->AddLine(ImVec2(Origin.x + LeftW - 1, Origin.y), ImVec2(Origin.x + LeftW - 1, Origin.y + Avail.y), S.Line,
        std::max(1.f, Scale));
    Draw->AddLine(ImVec2(Origin.x, Origin.y + HeaderH - 1), ImVec2(Origin.x + LeftW, Origin.y + HeaderH - 1), S.Line,
        std::max(1.f, Scale));
    if (LeftCollapsed)
    {
        if (DrawBrowserCollapseToggle("read-expand", ImVec2(Origin.x + (LeftW - 27 * Scale) / 2,
                                                      Origin.y + (HeaderH - 29 * Scale) / 2),
                Frame, true, true))
        {
            LeftCollapsed = false;
            if (Width < 700) RightCollapsed = true;
        }
    }
    else
    {
        DrawBrowserText(Draw, Font, 11 * Scale, ImVec2(Origin.x + 11 * Scale, Origin.y + (HeaderH - 11 * Scale) / 2),
            S.Text, EllipsizeBrowserText(Font, 11 * Scale, RailName, LeftW - 22 * Scale - 40 * Scale));
        const std::string Count = std::to_string(Entry->Views.size());
        DrawBrowserText(Draw, Font, 9 * Scale,
            ImVec2(Origin.x + 11 * Scale + MeasureBrowserText(Font, 11 * Scale, RailName).x + 3 * Scale,
                Origin.y + (HeaderH - 9 * Scale) / 2),
            S.Faint, Count);
        if (DrawBrowserCollapseToggle("read-collapse", ImVec2(Origin.x + LeftW - 11 * Scale - 27 * Scale,
                                                      Origin.y + (HeaderH - 29 * Scale) / 2),
                Frame, false, true))
            LeftCollapsed = true;
    }
    const auto NavigatePreview = [&](int Index)
    {
        PreviewIndex = Index;
        DocumentEnabled = false;
        Zoom = 100;
        ResetLiveCamera(*Entry);
        LiveChannel = PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision).Channel;
        CaptureDelay = 2;
    };
    if (!LeftCollapsed)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * Scale);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, S.Nested);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, S.Muted);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, S.Muted);
        ImGui::SetCursorPos(ImVec2(0, HeaderH));
        ImGui::BeginChild("##readrail", ImVec2(LeftW, Avail.y - HeaderH), false, ImGuiWindowFlags_NoBackground);
        Frame.Draw = ImGui::GetWindowDrawList();
        const ImVec2 Rail = ImGui::GetWindowPos();
        const float PadX = (Narrow ? 9 : 16) * Scale;
        float Y = Rail.y + (Narrow ? 18 : 27) * Scale;
        const float InnerW = LeftW - PadX * 2;
        Y += 19 * Scale;
        DrawBrowserCrossline(ImVec2(Rail.x + PadX, Y), InnerW, Frame);
        Y += 23 * Scale;
        for (size_t Index = 0; Index < Entry->Views.size(); ++Index)
        {
            const bool Current = !DocumentEnabled && int(Index) == PreviewIndex;
            const float ThumbH = InnerW / 1.38f;
            const ImVec2 Min(Rail.x + PadX, Y);
            const std::string HitId = "thumb-" + std::to_string(Index);
            const bool Clicked = PlaceBrowserHit(Min, ImVec2(InnerW, ThumbH + 8 * Scale + 12 * Scale), HitId.c_str());
            Frame.Draw->AddRect(Min, ImVec2(Min.x + InnerW, Min.y + ThumbH), Current ? S.Text : S.Line,
                6 * Scale, 0, 2 * Scale);
            ConstructThumbnailTile(ImVec2(Min.x + 5 * Scale, Min.y + 5 * Scale),
                ImVec2(InnerW - 10 * Scale, ThumbH - 10 * Scale), *Entry, Index, ActiveRevision,
                BrowserThumbFit::Cover, Scale);
            const std::string Label =
                EllipsizeBrowserText(Font, 10 * Scale, Entry->Views[Index], InnerW);
            DrawBrowserText(Frame.Draw, Font, 10 * Scale,
                ImVec2(Min.x + (InnerW - MeasureBrowserText(Font, 10 * Scale, Label).x) / 2,
                    Min.y + ThumbH + 8 * Scale),
                Current ? S.Text : S.Muted, Label);
            Y += ThumbH + 8 * Scale + 12 * Scale + 21 * Scale;
            if (Clicked) NavigatePreview(int(Index));
        }
        Y += 31 * Scale - 21 * Scale;
        DrawBrowserSpaced(Frame.Draw, Font, 8 * Scale, ImVec2(Rail.x + PadX, Y), S.Faint, "REFERENCE", 1.5f * Scale);
        Y += 8 * Scale + 12 * Scale;
        if (Entry->Category != BrowserFileCategory::Document)
        {
            const ImVec2 Min(Rail.x + PadX, Y);
            const bool Clicked = PlaceBrowserHit(Min, ImVec2(InnerW, 34 * Scale), "doc-link");
            if (ImGui::IsItemHovered())
                Frame.Draw->AddRectFilled(Min, ImVec2(Min.x + InnerW, Min.y + 34 * Scale), S.Selected, 6 * Scale);
            DrawBrowserIcon(Frame.Draw, BrowserIcon::File, ImVec2(Min.x + 6 * Scale, Min.y + 10 * Scale), 14 * Scale,
                S.Muted);
            DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Min.x + 6 * Scale + 14 * Scale + 8 * Scale, Min.y + 9 * Scale),
                S.Text, "Overview.md");
            DrawBrowserText(Frame.Draw, Font, 8 * Scale,
                ImVec2(Min.x + 6 * Scale + 14 * Scale + 8 * Scale, Min.y + 9 * Scale + 11 * Scale), S.Faint,
                Entry->Imported ? "Browser-generated summary" : "Sample document");
            if (Clicked)
            {
                DocumentEnabled = true;
                Zoom = 100;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
        Frame.Draw = Draw;
    }
    // 🎬 Reader header: back, name, version, star, launch, theme.
    const ImVec2 Head(Origin.x + LeftW, Origin.y);
    Draw->AddRectFilled(Head, ImVec2(Head.x + CentreW, Head.y + HeaderH), S.Panel);
    Draw->AddLine(ImVec2(Head.x, Head.y + HeaderH - 1), ImVec2(Head.x + CentreW, Head.y + HeaderH - 1), S.Line,
        std::max(1.f, Scale));
    const std::string FullName = Entry->Title + Entry->Extension;
    std::vector<std::string> RevisionOptions;
    if (Entry->Imported) RevisionOptions = {"Local file"};
    else
        for (size_t Index = 0; Index < Entry->Revisions.size(); ++Index)
            RevisionOptions.push_back(BrowserVersionLabel(Entry->Revisions.size(), Index));
    const float RevisionW =
        std::min(150 * Scale, std::max(112 * Scale,
                                     MeasureBrowserText(Font, 10 * Scale,
                                         Entry->Imported ? "Local file" : "Version 3 · Current")
                                             .x +
                                         41 * Scale));
    const bool IconLaunch = Width <= 1100 || Narrow;
    const float LaunchW = IconLaunch ? 31 * Scale
                                    : MeasureBrowserText(Font, 10 * Scale, "Open in Frontier").x + 24 * Scale + 15 * Scale + 10 * Scale;
    const float LaunchH = IconLaunch ? 31 * Scale : 28 * Scale;
    const float StarBox = Narrow ? 25 * Scale : 30 * Scale;
    const std::vector<std::string> Themes = {"OLED", "Light", "Dark"};
    const int ThemeIndex = Prefs.Theme == "light" ? 1 : Prefs.Theme == "dark" ? 2 : 0;
    const float ThemeW = BrowserComboWidth(Frame, Themes);
    const auto ThemePicker = [&](ImVec2 Min)
    {
        const int Picked = DrawBrowserSelect("theme", Min, ThemeW, Themes, ThemeIndex, Frame, 9 * Scale, 11 * Scale,
            20 * Scale, 30 * Scale / 2, IM_COL32(0, 0, 0, 0), 7 * Scale);
        if (Picked != ThemeIndex)
        {
            Prefs.Theme = Picked == 1 ? "light" : Picked == 2 ? "dark" : "oled";
            PersistPreferences();
        }
    };
    float ActionsW = RevisionW + 7 * Scale + StarBox + 7 * Scale + LaunchW + 9 * Scale + ThemeW;
    if (!Narrow)
    {
        const float BackBox = 30 * Scale;
        if (FocusBack)
        {
            ImGui::SetCursorPos(ImVec2(Head.x + 16 * Scale - Origin.x, Head.y + (HeaderH - BackBox) / 2 - Origin.y));
            ImGui::SetKeyboardFocusHere();
        }
        if (DrawBrowserIconButton("back", ImVec2(Head.x + 16 * Scale, Head.y + (HeaderH - BackBox) / 2), BackBox,
                BrowserIcon::Back, Frame))
            CloseFile();
        FocusBack = false;
        DrawBrowserText(Draw, Font, 11 * Scale,
            ImVec2(Head.x + 16 * Scale + BackBox + 8 * Scale, Head.y + (HeaderH - 11 * Scale) / 2), S.Text,
            EllipsizeBrowserText(Font, 11 * Scale, FullName,
                std::max(20.f, CentreW - 16 * Scale - BackBox - 8 * Scale - 9 * Scale - ActionsW - 16 * Scale)));
        float X = Head.x + CentreW - 16 * Scale - ThemeW;
        ThemePicker(ImVec2(X, Head.y + (HeaderH - BrowserComboHeight(Frame)) / 2));
        X -= 9 * Scale + LaunchW;
        if (IconLaunch)
        {
            if (DrawBrowserIconButton("open", ImVec2(X, Head.y + (HeaderH - LaunchH) / 2), LaunchH,
                    BrowserIcon::Arrow, Frame, false, S.ButtonText))
                OpenInFrontier();
            Draw->AddCircleFilled(ImVec2(X + LaunchH / 2, Head.y + HeaderH / 2), LaunchH / 2, S.Button);
            DrawBrowserIcon(Draw, BrowserIcon::Arrow,
                ImVec2(X + (LaunchH - 15 * Scale) / 2, Head.y + (HeaderH - 15 * Scale) / 2), 15 * Scale,
                S.ButtonText);
        }
        else if (DrawBrowserPrimary("open", ImVec2(X, Head.y + (HeaderH - LaunchH) / 2), "Open in Frontier",
                     BrowserIcon::Arrow, true, Frame, 10 * Scale, 12 * Scale, LaunchH, 15))
            OpenInFrontier();
        X -= 7 * Scale + StarBox;
        const bool Starred = Prefs.Stars.count(Entry->Token) > 0;
        if (DrawBrowserIconButton("star-reader", ImVec2(X, Head.y + (HeaderH - StarBox) / 2), StarBox,
                BrowserIcon::Star, Frame, true, Starred ? S.Accent : S.Text, 16, true))
            ToggleStar(Entry->Token);
        X -= 7 * Scale + RevisionW;
        if (!Entry->Imported)
        {
            const int Picked = DrawBrowserSelect("revision", ImVec2(X, Head.y + (HeaderH - BrowserComboHeight(Frame)) / 2),
                RevisionW, RevisionOptions, ActiveRevision, Frame, 10 * Scale, 13 * Scale, 28 * Scale,
                26 * Scale / 2, S.Selected, 8 * Scale);
            if (Picked != ActiveRevision) ConfigureRevision(Picked);
        }
        else
            DrawBrowserSelect("revision", ImVec2(X, Head.y + (HeaderH - BrowserComboHeight(Frame)) / 2), RevisionW,
                RevisionOptions, 0, Frame, 10 * Scale, 13 * Scale, 28 * Scale, 26 * Scale / 2, S.Selected, 8 * Scale,
                true);
    }
    else
    {
        const float BackBox = 30 * Scale;
        if (DrawBrowserIconButton("back", ImVec2(Head.x + 9 * Scale, Head.y + 6 * Scale), BackBox,
                BrowserIcon::Back, Frame))
            CloseFile();
        DrawBrowserText(Draw, Font, 10 * Scale, ImVec2(Head.x + 9 * Scale + BackBox + 8 * Scale, Head.y + 6 * Scale + (BackBox - 10 * Scale) / 2),
            S.Text, EllipsizeBrowserText(Font, 10 * Scale, FullName, CentreW - 60 * Scale - BackBox));
        float X = Head.x + 9 * Scale;
        const int Picked = Entry->Imported ? 0
                                           : DrawBrowserSelect("revision", ImVec2(X, Head.y + 41 * Scale + (25 * Scale - BrowserComboHeight(Frame)) / 2), RevisionW,
                            RevisionOptions, ActiveRevision, Frame, 9 * Scale, 13 * Scale, 28 * Scale, 26 * Scale / 2,
                            S.Selected, 8 * Scale, Entry->Imported);
        if (!Entry->Imported && Picked != ActiveRevision) ConfigureRevision(Picked);
        X += RevisionW + 5 * Scale;
        const bool Starred = Prefs.Stars.count(Entry->Token) > 0;
        if (DrawBrowserIconButton("star-reader", ImVec2(X, Head.y + 41 * Scale), 25 * Scale, BrowserIcon::Star,
                Frame, true, Starred ? S.Accent : S.Text, 14, true))
            ToggleStar(Entry->Token);
        X += 25 * Scale + 5 * Scale;
        Draw->AddCircleFilled(ImVec2(X + 25 * Scale / 2, Head.y + 41 * Scale + 25 * Scale / 2), 25 * Scale / 2,
            S.Button);
        if (DrawBrowserIconButton("open", ImVec2(X, Head.y + 41 * Scale), 25 * Scale, BrowserIcon::Arrow, Frame,
                false, S.ButtonText, 14, true))
            OpenInFrontier();
        ThemePicker(ImVec2(Head.x + CentreW - 9 * Scale - ThemeW,
            Head.y + 41 * Scale + (25 * Scale - BrowserComboHeight(Frame)) / 2));
    }
    // 🖼️ Viewer scroll: heading, canvas, caption, toolbar, bottom bar.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * Scale);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, S.Nested);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, S.Muted);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, S.Muted);
    ImGui::SetCursorPos(ImVec2(LeftW, HeaderH));
    ImGui::BeginChild("##viewscroll", ImVec2(CentreW, Avail.y - HeaderH), false,
        ImGuiWindowFlags_NoBackground | (WasLiveHovered ? ImGuiWindowFlags_NoScrollWithMouse : 0));
    Frame.Draw = ImGui::GetWindowDrawList();
    {
        const ImVec2 Scroll = ImVec2(ImGui::GetWindowPos().x - ImGui::GetScrollX(),
        ImGui::GetWindowPos().y - ImGui::GetScrollY());
        const float Side = Narrow ? 12 * Scale : std::min(36 * Scale, std::max(18 * Scale, Width * 0.025f));
        float Y = Scroll.y + (Narrow ? 18 : 24) * Scale;
        const float InnerW = CentreW - Side * 2;
        const std::string Title = DocumentEnabled ? (Entry->Category == BrowserFileCategory::Document ? Entry->Title : "Overview.md")
                                                  : (PreviewIndex >= 0 && size_t(PreviewIndex) < Entry->Views.size()
                                                         ? Entry->Views[size_t(PreviewIndex)]
                                                         : "");
        std::string Eyebrow = "DOCUMENT";
        if (!DocumentEnabled)
        {
            std::string Rail = RailName;
            for (auto &Character : Rail) Character = char(std::toupper(uint8_t(Character)));
            const int Version = int(Entry->Revisions.size()) - ActiveRevision;
            char Number[8]{};
            std::snprintf(Number, sizeof Number, "%02d", PreviewIndex + 1);
            Eyebrow = Rail + " " + Number + " · VERSION " + std::to_string(Version);
        }
        std::string Mode = Entry->Imported ? (!Entry->ImageBytes.empty() ? "Local image" : !Entry->Markdown.empty() ? "Local Markdown" : "Native preview not connected")
                                           : "Reference Vulkan";
        if (DocumentEnabled) Mode = "Markdown";
        const bool LiveView = !DocumentEnabled && !Entry->Imported && Title != "UV layout" && Title != "Metadata";
        if (!LiveView) WasLiveHovered = false;
        if (LiveView && !ConfigureLivePreview) Mode = "Renderer unavailable";
        DrawBrowserSpaced(Frame.Draw, Font, 8 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted, Eyebrow, 1.8f * Scale);
        if (!Narrow)
        {
            const ImVec2 ModeSize = MeasureBrowserText(Font, 10 * Scale, Mode);
            DrawBrowserText(Frame.Draw, Font, 10 * Scale,
                ImVec2(Scroll.x + Side + InnerW - ModeSize.x, Y), S.Muted, Mode);
        }
        Y += 8 * Scale + 5 * Scale;
        DrawBrowserText(Frame.Draw, Font, 21 * Scale, ImVec2(Scroll.x + Side, Y), S.Text,
            EllipsizeBrowserText(Font, 21 * Scale, Title, InnerW));
        Y += 21 * Scale + 23 * Scale;
        // Canvas.
        const float MinH = (Narrow ? 280 : 330) * Scale;
        if (DocumentEnabled)
        {
            const std::string Text = !Entry->Markdown.empty() ? Entry->Markdown
                                                              : ConstructBrowserOverview(*Entry, size_t(std::max(0, ActiveRevision)));
            const std::vector<BrowserMarkdownBlock> Blocks = ParseBrowserMarkdown(Text);
            const float Reserved = 300 * Scale;
            const float FitH = std::max(160 * Scale, Avail.y - HeaderH - Reserved);
            const float Fit = std::min(1.f, std::min(InnerW / 794, FitH / 1123)) * Zoom / 100;
            const float PageW = 794 * Fit, Pad = 60 * Fit;
            float PageH = 1123 * Fit;
            float ContentH = 2 * Pad;
            for (const auto &Block : Blocks)
            {
                if (Block.Level == 1) ContentH += 25 * Fit + 20 * Fit;
                else if (Block.Level == 2) ContentH += 25 * Fit + 15 * Fit + 10 * Fit;
                else
                {
                    const std::string Safe = SanitiseBrowserText(Block.Text, Font, 13 * Fit);
                    ContentH += Font->CalcTextSizeA(13 * Fit, FLT_MAX, PageW - 2 * Pad, Safe.c_str()).y + 15 * Fit;
                }
            }
            if (!Entry->Imported) ContentH += (PageW - 2 * Pad) / 1.6f + 20 * Fit;
            PageH = std::max(PageH, ContentH);
            Y += 8 * Scale;
            const ImVec2 Page(Scroll.x + Side + (InnerW - PageW) / 2, Y);
            Frame.Draw->AddRectFilled(Page, ImVec2(Page.x + PageW, Page.y + PageH), S.Card, 2 * Scale);
            Frame.Draw->AddRect(Page, ImVec2(Page.x + PageW, Page.y + PageH), S.Line, 2 * Scale, 0,
                std::max(1.f, Scale));
            float BY = Page.y + Pad;
            for (const auto &Block : Blocks)
            {
                if (Block.Level == 1)
                {
                    DrawBrowserText(Frame.Draw, Font, 25 * Fit, ImVec2(Page.x + Pad, BY), S.Text, Block.Text);
                    BY += 25 * Fit + 20 * Fit;
                }
                else if (Block.Level == 2)
                {
                    BY += 25 * Fit;
                    DrawBrowserText(Frame.Draw, Font, 15 * Fit, ImVec2(Page.x + Pad, BY), S.Text, Block.Text);
                    BY += 15 * Fit + 10 * Fit;
                }
                else
                {
                    BY += DrawBrowserParagraph(Frame.Draw, Font, 13 * Fit, ImVec2(Page.x + Pad, BY), S.Muted,
                        Block.Text, PageW - 2 * Pad, 13 * Fit * 1.85f);
                    BY += 15 * Fit;
                }
            }
            if (!Entry->Imported)
            {
                BY += 20 * Fit - 15 * Fit;
                ConstructThumbnailTile(ImVec2(Page.x + Pad, BY),
                    ImVec2(PageW - 2 * Pad, (PageW - 2 * Pad) / 1.6f), *Entry, 0, ActiveRevision,
                    BrowserThumbFit::Cover, Scale);
            }
            Y += PageH + 24 * Scale;
        }
        else
        {
            const float CanvasH = std::max(MinH, InnerW / 1.46f);
            const ImVec2 Box(Scroll.x + Side, Y);
            Frame.Draw->AddRectFilled(Box, ImVec2(Box.x + InnerW, Box.y + CanvasH), S.Card, 8 * Scale);
            Frame.Draw->AddRect(Box, ImVec2(Box.x + InnerW, Box.y + CanvasH), S.Line, 8 * Scale, 0,
                std::max(1.f, Scale));
            if (Entry->Imported && Entry->Markdown.empty() && Entry->ImageBytes.empty())
            {
                float VY = Box.y + 35 * Scale;
                DrawBrowserIcon(Frame.Draw, BrowserIcon::File, ImVec2(Box.x + 35 * Scale, VY), 18 * Scale, S.Muted);
                VY += 18 * Scale + 12 * Scale;
                DrawBrowserText(Frame.Draw, Font, 18 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Text,
                    EllipsizeBrowserText(Font, 18 * Scale, FullName, InnerW - 70 * Scale));
                VY += 18 * Scale + 20 * Scale;
                VY += DrawBrowserParagraph(Frame.Draw, Font, 13 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Text,
                    "This file was selected from your device. Native decoding is not connected.",
                    InnerW - 70 * Scale, 13 * Scale * 1.5f);
                VY += 18 * Scale;
                DrawBrowserChip(ImVec2(Box.x + 35 * Scale, VY),
                    FormatBrowserCount(Entry->ByteCount) + " bytes · original unchanged", Frame);
            }
            else if (Entry->Imported && !Entry->ImageBytes.empty())
            {
                if (QueryImportedImage)
                {
                    const BrowserPreviewImage Image = QueryImportedImage(Entry->Token, Entry->ImageBytes);
                    if (Image.Failed)
                    {
                        float VY = Box.y + 35 * Scale;
                        DrawBrowserIcon(
                            Frame.Draw, BrowserIcon::File, ImVec2(Box.x + 35 * Scale, VY), 18 * Scale, S.Muted);
                        VY += 18 * Scale + 12 * Scale;
                        DrawBrowserText(Frame.Draw, Font, 18 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Text,
                            EllipsizeBrowserText(Font, 18 * Scale, FullName, InnerW - 70 * Scale));
                        VY += 18 * Scale + 20 * Scale;
                        DrawBrowserParagraph(Frame.Draw, Font, 13 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Text,
                            "This image encoding is not supported by the native decoder; the original file is "
                            "unchanged.",
                            InnerW - 70 * Scale, 13 * Scale * 1.5f);
                    }
                    else if (Image.Texture)
                    {
                        const float ZoomScale = Zoom / 100.f;
                        const float ImageAspect = Image.Width / std::max(1.f, Image.Height);
                        float W = InnerW * ZoomScale, H = W / ImageAspect;
                        if (H > CanvasH * ZoomScale)
                        {
                            H = CanvasH * ZoomScale;
                            W = H * ImageAspect;
                        }
                        const ImVec2 At(Box.x + (InnerW - W) / 2, Box.y + (CanvasH - H) / 2);
                        ImGui::PushClipRect(Box, ImVec2(Box.x + InnerW, Box.y + CanvasH), true);
                        Frame.Draw->AddImage((ImTextureID)Image.Texture, At, ImVec2(At.x + W, At.y + H),
                            ImVec2(Image.U0, Image.V0), ImVec2(Image.U1, Image.V1));
                        ImGui::PopClipRect();
                    }
                }
            }
            else if (Title == "UV layout" && !Entry->Imported)
            {
                constexpr float UvAspect = 600.0f / 380;
                float W = InnerW, H = W / UvAspect;
                if (H > CanvasH)
                {
                    H = CanvasH;
                    W = H * UvAspect;
                }
                ConstructUvArt(ImVec2(Box.x + (InnerW - W) / 2, Box.y + (CanvasH - H) / 2), ImVec2(W, H), *Entry,
                    ActiveRevision, Scale);
            }
            else if (Title == "Metadata" && !Entry->Imported)
            {
                const CachedPreviewTriangles &Preview = PreviewTrianglesFor(*Entry, ActiveRevision);
                float VY = Box.y + 35 * Scale;
                DrawBrowserSpaced(Frame.Draw, Font, 9 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Muted,
                    "RENDERED GEOMETRY · VERSION " + std::to_string(int(Entry->Revisions.size()) - ActiveRevision),
                    1.8f * Scale);
                VY += 9 * Scale + 12 * Scale;
                DrawBrowserText(Frame.Draw, Font, 18 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Text,
                    Entry->Title);
                VY += 18 * Scale + 20 * Scale;
                std::string Json = "{\n  \"Triangles\": " + FormatBrowserCount(Preview.Statistics.Triangles) +
                                   ",\n  \"Extents\": [\n    " +
                                   FormatBrowserNumber(Preview.Statistics.Extents[0]) + ",\n    " +
                                   FormatBrowserNumber(Preview.Statistics.Extents[1]) + ",\n    " +
                                   FormatBrowserNumber(Preview.Statistics.Extents[2]) +
                                   "\n  ],\n  \"SurfaceChannels\": [\n    \"Albedo\",\n    \"Specular\",\n    \"Metallic\",\n    \"Roughness\"\n  ],\n  \"Renderer\": \"Reference Vulkan · EON diffuse / compensated GGX\"\n}";
                DrawBrowserParagraph(Frame.Draw, Font, 11 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Muted, Json,
                    InnerW - 70 * Scale, 22 * Scale);
            }
            else if (LiveView)
            {
                const BrowserPreviewConfiguration Config =
                    PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision);
                const std::string Signature = SnapshotKeyFor(*Entry, size_t(PreviewIndex), ActiveRevision) + ":" +
                                              std::to_string(LiveChannel);
                if (ConfigureLivePreview)
                {
                    ConfigureLivePreview(Config, LiveCamera, LiveChannel, int(InnerW), int(CanvasH));
                    LiveSignature = Signature;
                }
                if (CaptureDelay > 0 && --CaptureDelay == 0) CaptureNow();
                PlaceBrowserHit(Box, ImVec2(InnerW, CanvasH), "live-canvas");
                const bool Hovered = ImGui::IsItemHovered();
                WasLiveHovered = Hovered && LiveView;
                if (Hovered && ConfigureLivePreview)
                {
                    const float Wheel = ImGui::GetIO().MouseWheel;
                    if (Wheel != 0) // 🖱️ deltaY: one wheel notch ≈ 100, scroll-up zooms in.
                    {
                        ZoomBrowserCamera(LiveCamera, -double(Wheel) * 100.0);
                        CaptureDelay = 1;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
                    {
                        ResetLiveCamera(*Entry);
                        LiveChannel = Config.Channel;
                        CaptureDelay = 1;
                    }
                    const bool Panning =
                        ImGui::GetIO().KeyShift || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0);
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0) ||
                        ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))
                    {
                        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
                        OrbitBrowserCamera(LiveCamera, double(Delta.x), double(Delta.y), Panning);
                        LiveDragging = true;
                    }
                    else if (LiveDragging)
                    {
                        LiveDragging = false;
                        CaptureNow();
                    }
                }
                if (!ConfigureLivePreview)
                {
                    float VY = Box.y + 35 * Scale;
                    DrawBrowserText(Frame.Draw, Font, 12 * Scale, ImVec2(Box.x + 35 * Scale, VY), S.Muted,
                        "Rendered preview unavailable");
                    VY += 12 * Scale + 12 * Scale;
                    VY += DrawBrowserParagraph(Frame.Draw, Font, 12 * Scale, ImVec2(Box.x + 35 * Scale, VY),
                        S.Muted, "The native preview renderer is not connected.", InnerW - 70 * Scale,
                        12 * Scale * 1.5f);
                    VY += DrawBrowserParagraph(Frame.Draw, Font, 12 * Scale, ImVec2(Box.x + 35 * Scale, VY),
                        S.Muted, "No illustration has been substituted.", InnerW - 70 * Scale,
                        12 * Scale * 1.5f);
                }
                else if (QueryLiveImage)
                {
                    const BrowserPreviewImage Live = QueryLiveImage();
                    if (Live.Texture)
                    {
                        ImGui::PushClipRect(Box, ImVec2(Box.x + InnerW, Box.y + CanvasH), true);
                        Frame.Draw->AddImage((ImTextureID)Live.Texture, Box,
                            ImVec2(Box.x + InnerW, Box.y + CanvasH), ImVec2(Live.U0, Live.V0),
                            ImVec2(Live.U1, Live.V1));
                        ImGui::PopClipRect();
                    }
                    const CachedPreviewTriangles &Preview = PreviewTrianglesFor(*Entry, ActiveRevision);
                    const float BarH = 32 * Scale;
                    const ImVec2 Bar(Box.x + 12 * Scale, Box.y + CanvasH - 12 * Scale - BarH);
                    const float BarW = InnerW - 24 * Scale;
                    Frame.Draw->AddRectFilled(Bar, ImVec2(Bar.x + BarW, Bar.y + BarH),
                        HexColour("#090909d9"), 7 * Scale);
                    Frame.Draw->AddRect(Bar, ImVec2(Bar.x + BarW, Bar.y + BarH), HexColour("#ffffff20"),
                        7 * Scale, 0, std::max(1.f, Scale));
                    DrawBrowserText(Frame.Draw, Font, 8 * Scale,
                        ImVec2(Bar.x + 10 * Scale, Bar.y + (BarH - 8 * Scale) / 2), HexColour("#bbbbbb"),
                        "View");
                    const std::vector<std::string> Channels = {"Lit · EON / GGX", "Albedo", "Specular",
                        "Metallic", "Roughness", "CAD contours"};
                    const int ChannelIndex = LiveChannel >= 0 && LiveChannel < 6 ? LiveChannel : 0;
                    const float SelectW = std::min(130 * Scale, BrowserComboWidth(Frame, Channels));
                    const int Picked = DrawBrowserSelect("channel",
                        ImVec2(Bar.x + 10 * Scale + MeasureBrowserText(Font, 8 * Scale, "View").x + 6 * Scale,
                            Bar.y + (BarH - BrowserComboHeight(Frame)) / 2),
                        SelectW, Channels, ChannelIndex, Frame, 9 * Scale, 4 * Scale, 14 * Scale, 4 * Scale,
                        HexColour("#111111"), 3 * Scale);
                    if (Picked != ChannelIndex)
                    {
                        LiveChannel = Picked;
                        CaptureDelay = 1;
                        static bool TracedChannel = false;
                        if (!TracedChannel)
                        {
                            TracedChannel = true;
                            DiagnosticJournal::Append(
                                "BrowserFooter", "channel control picked index=" + std::to_string(Picked));
                        }
                    }
                    const float CapW =
                        MeasureBrowserText(Font, 8 * Scale, "Capture thumbnail").x + 16 * Scale;
                    const ImVec2 Cap(Bar.x + 10 * Scale +
                                         MeasureBrowserText(Font, 8 * Scale, "View").x + 6 * Scale + SelectW +
                                         10 * Scale,
                        Bar.y + (BarH - 18 * Scale) / 2);
                    const bool CapClicked = PlaceBrowserHit(Cap, ImVec2(CapW, 18 * Scale), "capture");
                    Frame.Draw->AddRect(Cap, ImVec2(Cap.x + CapW, Cap.y + 18 * Scale), HexColour("#ffffff20"),
                        4 * Scale, 0, std::max(1.f, Scale));
                    DrawBrowserText(Frame.Draw, Font, 8 * Scale,
                        ImVec2(Cap.x + 8 * Scale, Cap.y + (18 * Scale - 8 * Scale) / 2),
                        HexColour("#dddddd"), "Capture thumbnail");
                    if (CapClicked)
                    {
                        CaptureNow();
                        Notify("Thumbnail captured from the current rendered view.");
                        static bool TracedCapture = false;
                        if (!TracedCapture)
                        {
                            TracedCapture = true;
                            DiagnosticJournal::Append("BrowserFooter", "capture control clicked");
                        }
                    }
                    const std::string Tris =
                        FormatBrowserCount(Preview.Statistics.Triangles) + " triangles";
                    const ImVec2 TrisSize = MeasureBrowserText(Font, 8 * Scale, Tris);
                    DrawBrowserText(Frame.Draw, Font, 8 * Scale,
                        ImVec2(Bar.x + BarW - 10 * Scale - TrisSize.x, Bar.y + (BarH - 8 * Scale) / 2),
                        HexColour("#bbbbbb"), Tris);
                }
            }
            Y += CanvasH;
        }
        // Caption + toolbar + bottom bar.
        const std::string Description =
            DocumentEnabled ? "Markdown · read-only preview" : Entry->Description;
        std::string Resolution = Entry->Imported ? "Local file" : "Drag to orbit · scroll to zoom";
        if (DocumentEnabled) Resolution = "A4 · 210 × 297 mm";
        else if (Title == "UV layout") Resolution = "Generated cylindrical UV · actual vertices";
        Y += 13 * Scale;
        DrawBrowserText(Frame.Draw, Font, (Narrow ? 8 : 9) * Scale, ImVec2(Scroll.x + Side, Y), S.Faint,
            EllipsizeBrowserText(Font, 9 * Scale, Description, InnerW / 2));
        const ImVec2 ResSize = MeasureBrowserText(Font, 9 * Scale, Resolution);
        DrawBrowserText(Frame.Draw, Font, (Narrow ? 8 : 9) * Scale,
            ImVec2(Scroll.x + Side + InnerW - ResSize.x, Y), S.Faint, Resolution);
        Y += 9 * Scale + 13 * Scale + 14 * Scale;
        const bool CanStep = !DocumentEnabled && Entry->Views.size() > 1;
        const std::string IndexLabel =
            DocumentEnabled ? "Document" : std::to_string(PreviewIndex + 1) + " / " + std::to_string(Entry->Views.size());
        std::string ZoomLabel = std::to_string(Zoom) + "%";
        if (DocumentEnabled)
        {
            const float Reserved = 300 * Scale;
            const float FitH = std::max(160 * Scale, Avail.y - HeaderH - Reserved);
            const float Fit = std::min(1.f, std::min(InnerW / 794, FitH / 1123)) * Zoom / 100;
            ZoomLabel = std::to_string(int(std::round(Fit * 100))) + "%";
        }
        const float ToolH = 39 * Scale;
        const float ToolW = 10 * Scale + 25 * Scale + 10 * Scale +
                            MeasureBrowserText(Font, 10 * Scale, IndexLabel).x + 10 * Scale + 25 * Scale + 10 * Scale +
                            1 + 8 * Scale + 25 * Scale + 10 * Scale +
                            std::max(35 * Scale, MeasureBrowserText(Font, 10 * Scale, ZoomLabel).x) + 10 * Scale +
                            25 * Scale + 10 * Scale + 1 + 8 * Scale +
                            MeasureBrowserText(Font, 9 * Scale, "Fit to view").x + 10 * Scale;
        float TX = Scroll.x + Side + (InnerW - ToolW) / 2;
        Frame.Draw->AddRectFilled(ImVec2(TX, Y), ImVec2(TX + ToolW, Y + ToolH), S.Panel, 10 * Scale);
        Frame.Draw->AddRect(ImVec2(TX, Y), ImVec2(TX + ToolW, Y + ToolH), S.Line, 10 * Scale, 0,
            std::max(1.f, Scale));
        TX += 10 * Scale;
        const float IconY = Y + (ToolH - 25 * Scale) / 2;
        if (DrawBrowserIconButton("prev", ImVec2(TX, IconY), 25 * Scale, BrowserIcon::Back, Frame, false, 0, 16,
                CanStep))
            NavigatePreview((PreviewIndex + int(Entry->Views.size()) - 1) % int(Entry->Views.size()));
        TX += 25 * Scale + 10 * Scale;
        DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(TX, Y + (ToolH - 10 * Scale) / 2), S.Text, IndexLabel);
        TX += MeasureBrowserText(Font, 10 * Scale, IndexLabel).x + 10 * Scale;
        if (DrawBrowserIconButton("next", ImVec2(TX, IconY), 25 * Scale, BrowserIcon::Arrow, Frame, false, 0, 16,
                CanStep))
            NavigatePreview((PreviewIndex + 1) % int(Entry->Views.size()));
        TX += 25 * Scale + 10 * Scale;
        Frame.Draw->AddLine(ImVec2(TX, Y + (ToolH - 15 * Scale) / 2), ImVec2(TX, Y + (ToolH + 15 * Scale) / 2),
            S.Line, std::max(1.f, Scale));
        TX += 1 + 8 * Scale;
        if (DrawBrowserIconButton("zoom-out", ImVec2(TX, IconY), 25 * Scale, BrowserIcon::Minus, Frame, false, 0,
                14, true))
            ConfigureZoom(Zoom - 25);
        TX += 25 * Scale + 10 * Scale;
        {
            const float LabelW = std::max(35 * Scale, MeasureBrowserText(Font, 10 * Scale, ZoomLabel).x);
            const bool Clicked = PlaceBrowserHit(ImVec2(TX, Y), ImVec2(LabelW, ToolH), "zoom-reset");
            DrawBrowserText(Frame.Draw, Font, 10 * Scale,
                ImVec2(TX + (LabelW - MeasureBrowserText(Font, 10 * Scale, ZoomLabel).x) / 2,
                    Y + (ToolH - 10 * Scale) / 2),
                ImGui::IsItemHovered() ? S.Text : S.Muted, ZoomLabel);
            if (Clicked) ConfigureZoom(100);
            TX += LabelW + 10 * Scale;
        }
        if (DrawBrowserIconButton("zoom-in", ImVec2(TX, IconY), 25 * Scale, BrowserIcon::Plus, Frame, false, 0, 14,
                true))
            ConfigureZoom(Zoom + 25);
        TX += 25 * Scale + 10 * Scale;
        Frame.Draw->AddLine(ImVec2(TX, Y + (ToolH - 15 * Scale) / 2), ImVec2(TX, Y + (ToolH + 15 * Scale) / 2),
            S.Line, std::max(1.f, Scale));
        TX += 1 + 8 * Scale;
        if (DrawBrowserTextButton("fit", ImVec2(TX, Y + (ToolH - 12 * Scale) / 2), "Fit to view", Frame, 9 * Scale))
            ConfigureZoom(100);
        Y += ToolH + 25 * Scale;
        Frame.Draw->AddLine(ImVec2(Scroll.x + Side, Y), ImVec2(Scroll.x + Side + InnerW, Y), S.Line,
            std::max(1.f, Scale));
        Y += 15 * Scale;
        DrawBrowserIcon(Frame.Draw, BrowserIcon::Cube, ImVec2(Scroll.x + Side, Y - 2 * Scale), 14 * Scale, S.Faint);
        const std::string Bottom = Entry->Imported
                                       ? "Your original file is unchanged"
                                       : "Sample · version " + std::to_string(int(Entry->Revisions.size()) - ActiveRevision);
        DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side + 14 * Scale + 9 * Scale, Y), S.Faint,
            Bottom);
        const std::string Right = "No editor opened";
        DrawBrowserText(Frame.Draw, Font, 9 * Scale,
            ImVec2(Scroll.x + Side + InnerW - MeasureBrowserText(Font, 9 * Scale, Right).x, Y), S.Faint, Right);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
    Frame.Draw = Draw;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     READER INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructReaderInspector(
    float Width, ImVec2 Origin, ImVec2 Avail, float HeaderH, float Scale)
{
    const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
    if (Entry == nullptr) return;
    const bool Narrow = Width <= 700;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, S, Width, Narrow};
    ImDrawList *Draw = Frame.Draw;
    ImFont *Font = Frame.Font;
    const float LeftW = (LeftCollapsed ? 44 : (Narrow ? 140 : 204)) * Scale;
    const float RightW = (RightCollapsed ? 44 : (Narrow ? 220 : 302)) * Scale;
    const ImVec2 Right(Origin.x + Avail.x - RightW, Origin.y);
    Draw->AddRectFilled(Right, ImVec2(Right.x + RightW, Right.y + Avail.y), S.Panel);
    Draw->AddLine(ImVec2(Right.x, Right.y), ImVec2(Right.x, Right.y + Avail.y), S.Line, std::max(1.f, Scale));
    Draw->AddLine(ImVec2(Right.x, Right.y + HeaderH - 1), ImVec2(Right.x + RightW, Right.y + HeaderH - 1), S.Line,
        std::max(1.f, Scale));
    if (RightCollapsed)
    {
        if (DrawBrowserCollapseToggle("read-rcollapse", ImVec2(Right.x + (RightW - 27 * Scale) / 2,
                                                      Right.y + (HeaderH - 29 * Scale) / 2),
                Frame, true, false))
        {
            RightCollapsed = false;
            if (Width < 700) LeftCollapsed = true;
        }
        return;
    }
    const float TabSize = (Narrow ? 9 : 10) * Scale;
    float TabX = Right.x + 20 * Scale;
    const char *Tabs[] = {"details", "comments", "history"};
    const char *Labels[] = {"Details", "Comments", "History"};
    size_t NoteCount = Entry->Notes.size();
    const auto Local = Prefs.Notes.find(Entry->Token);
    if (Local != Prefs.Notes.end()) NoteCount += Local->second.size();
    for (int Index = 0; Index < 3; ++Index)
    {
        std::string Label = Labels[Index];
        const bool Selected = ActiveTab == Tabs[Index];
        const float LabelW = MeasureBrowserText(Font, TabSize, Label).x;
        float BadgeW = 0;
        std::string Badge;
        if (Index == 1 && NoteCount > 0)
        {
            Badge = std::to_string(NoteCount);
            BadgeW = 3 * Scale + MeasureBrowserText(Font, 8 * Scale, Badge).x;
        }
        const bool Clicked =
            PlaceBrowserHit(ImVec2(TabX, Right.y), ImVec2(LabelW + BadgeW, HeaderH), Tabs[Index]);
        if (ImGui::IsItemHovered() && !Selected)
            Draw->AddRectFilled(ImVec2(TabX - 4 * Scale, Right.y + (HeaderH - 18 * Scale) / 2),
                ImVec2(TabX + LabelW + BadgeW + 4 * Scale, Right.y + (HeaderH + 18 * Scale) / 2), S.Selected,
                4 * Scale);
        DrawBrowserText(Draw, Font, TabSize, ImVec2(TabX, Right.y + (HeaderH - TabSize) / 2),
            Selected ? S.Text : S.Muted, Label);
        if (!Badge.empty())
            DrawBrowserText(Draw, Font, 8 * Scale, ImVec2(TabX + LabelW + 3 * Scale, Right.y + (HeaderH - 8 * Scale) / 2),
                S.Faint, Badge);
        if (Clicked) ActiveTab = Tabs[Index];
        TabX += LabelW + BadgeW + (Narrow ? 9 : 16) * Scale;
    }
    if (DrawBrowserCollapseToggle("read-rcollapse", ImVec2(Right.x + RightW - 13 * Scale - 27 * Scale,
                                                  Right.y + (HeaderH - 29 * Scale) / 2),
            Frame, false, false))
        RightCollapsed = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * Scale);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, S.Nested);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, S.Muted);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, S.Muted);
    ImGui::SetCursorPos(ImVec2(Avail.x - RightW, HeaderH));
    ImGui::BeginChild("##inspectscroll", ImVec2(RightW, Avail.y - HeaderH), false, ImGuiWindowFlags_NoBackground);
    Frame.Draw = ImGui::GetWindowDrawList();
    {
        const ImVec2 Scroll = ImVec2(ImGui::GetWindowPos().x - ImGui::GetScrollX(),
        ImGui::GetWindowPos().y - ImGui::GetScrollY());
        const float Side = Width <= 1100 ? 18 * Scale : 22 * Scale;
        float Y = Scroll.y + (Width <= 1100 ? 22 : 24) * Scale;
        const float InnerW = RightW - Side * 2;
        const std::string FullName = Entry->Title + Entry->Extension;
        if (ActiveTab == "details")
        {
            const float HeroH = InnerW / 1.9f;
            const ImVec2 Hero(Scroll.x + Side, Y);
            const bool UvHero = !Entry->Imported && PreviewIndex >= 0 &&
                                size_t(PreviewIndex) < Entry->Views.size() &&
                                Entry->Views[size_t(PreviewIndex)] == "UV layout";
            Frame.Draw->AddRectFilled(Hero, ImVec2(Hero.x + InnerW, Hero.y + HeroH), S.Card, 6 * Scale);
            if (UvHero)
                ConstructUvArt(ImVec2(Hero.x + 5 * Scale, Hero.y + 5 * Scale),
                    ImVec2(InnerW - 10 * Scale, HeroH - 10 * Scale), *Entry, ActiveRevision, Scale);
            else
                ConstructThumbnailTile(ImVec2(Hero.x + 1, Hero.y + 1), ImVec2(InnerW - 2, HeroH - 2), *Entry,
                    size_t(std::max(0, PreviewIndex)), ActiveRevision, BrowserThumbFit::Contain, Scale, 6 * Scale,
                    ImDrawFlags_RoundCornersAll);
            Frame.Draw->AddRect(Hero, ImVec2(Hero.x + InnerW, Hero.y + HeroH), S.Line, 6 * Scale, 0,
                std::max(1.f, Scale));
            Y += HeroH + 18 * Scale;
            DrawBrowserText(Frame.Draw, Font, 14 * Scale, ImVec2(Scroll.x + Side, Y), S.Text,
                EllipsizeBrowserText(Font, 14 * Scale, FullName, InnerW));
            Y += 14 * Scale + 6 * Scale;
            Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted,
                Entry->Imported ? "Selected from your device" : "Sample file · local workspace", InnerW,
                19 * Scale);
            Y += 25 * Scale;
            DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side, Y), S.Text, "Description");
            Y += 9 * Scale + 8 * Scale;
            Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted,
                Entry->Description, InnerW, 19 * Scale);
            Y += 20 * Scale;
            DrawBrowserCrossline(ImVec2(Scroll.x + Side, Y), InnerW, Frame);
            Y += 20 * Scale;
            const std::pair<std::string, std::string> Rows[] = {
                {"Workspace", BrowserWorkspaceLabel(Entry->Category)},
                {BrowserCountsLabel(Entry->Category), std::to_string(Entry->Views.size())},
                {"Last opened", Entry->Opened},
                {"Storage", Entry->Imported ? "Local file selection" : "Sample location"},
            };
            for (const auto &[Key, Value] : Rows)
            {
                DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted, Key);
                const ImVec2 ValueSize = MeasureBrowserText(Font, 9 * Scale, Value);
                DrawBrowserText(Frame.Draw, Font, 9 * Scale,
                    ImVec2(Scroll.x + Side + InnerW - ValueSize.x, Y), S.Text, Value);
                Y += 9 * Scale + 11 * Scale;
            }
            Y += 25 * Scale - 11 * Scale;
            DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side, Y), S.Text, "Location");
            Y += 9 * Scale + 8 * Scale;
            const float PathH = DrawBrowserParagraph(nullptr, Font, 9 * Scale, ImVec2(0, 0), S.Muted, Entry->Path,
                                    InnerW - 20 * Scale, 9 * Scale * 1.8f);
            Frame.Draw->AddRectFilled(ImVec2(Scroll.x + Side, Y),
                ImVec2(Scroll.x + Side + InnerW, Y + PathH + 20 * Scale), S.Control, 5 * Scale);
            Frame.Draw->AddRect(ImVec2(Scroll.x + Side, Y), ImVec2(Scroll.x + Side + InnerW, Y + PathH + 20 * Scale),
                S.Line, 5 * Scale, 0, std::max(1.f, Scale));
            DrawBrowserParagraph(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side + 10 * Scale, Y + 10 * Scale),
                S.Muted, Entry->Path, InnerW - 20 * Scale, 9 * Scale * 1.8f);
            Y += PathH + 20 * Scale + 18 * Scale;
            DrawBrowserChip(ImVec2(Scroll.x + Side, Y),
                Entry->Imported ? "Read-only · native decoder pending" : "Sample file · read-only", Frame);
        }
        else if (ActiveTab == "comments")
        {
            struct NoteRow
            {
                std::string Author, Initial, Text, Time;
                bool Sample;
            };
            std::vector<NoteRow> Notes;
            for (const auto &Note : Entry->Notes) Notes.push_back({Note.Author, Note.Initial, Note.Text, Note.Time, true});
            if (Local != Prefs.Notes.end())
                for (const auto &Note : Local->second)
                    Notes.push_back({Note.Author, Note.Initial, Note.Text, Note.Time, false});
            const bool HasSample = std::any_of(Notes.begin(), Notes.end(), [](const NoteRow &Note) { return Note.Sample; });
            DrawBrowserText(Frame.Draw, Font, 8 * Scale, ImVec2(Scroll.x + Side, Y), S.Faint,
                std::string("FILE NOTES · ") + (HasSample ? "SAMPLE THREAD + YOUR LOCAL NOTES" : "BROWSER-LOCAL ONLY"));
            Y += 8 * Scale + 22 * Scale;
            if (Notes.empty())
            {
                Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted,
                    "No notes yet. Add the first thought below.", InnerW, 19 * Scale);
            }
            for (const auto &Note : Notes)
            {
                Frame.Draw->AddRectFilled(ImVec2(Scroll.x + Side, Y),
                    ImVec2(Scroll.x + Side + 25 * Scale, Y + 25 * Scale), S.Nested, 6 * Scale);
                const ImVec2 InitialSize = MeasureBrowserText(Font, 9 * Scale, Note.Initial);
                DrawBrowserText(Frame.Draw, Font, 9 * Scale,
                    ImVec2(Scroll.x + Side + (25 * Scale - InitialSize.x) / 2, Y + (25 * Scale - 9 * Scale) / 2),
                    S.Text, Note.Initial);
                const float NX = Scroll.x + Side + 25 * Scale + 9 * Scale;
                DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(NX, Y),
                    S.Text, Note.Author + (Note.Sample ? " · sample" : " · you"));
                DrawBrowserText(Frame.Draw, Font, 8 * Scale, ImVec2(NX, Y + 10 * Scale), S.Faint, Note.Time);
                Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(NX, Y + 10 * Scale + 8 * Scale + 8 * Scale),
                    S.Muted, Note.Text, InnerW - 25 * Scale - 9 * Scale, 18 * Scale);
                Y += 24 * Scale;
            }
            if (Notes.empty()) Y += 24 * Scale;
            const float FormH = 19 * Scale + 11 * Scale + 10 * Scale + 72 * Scale + 8 * Scale + 24 * Scale + 19 * Scale;
            Frame.Draw->AddRectFilled(ImVec2(Scroll.x, Y), ImVec2(Scroll.x + RightW, Y + FormH), S.Card);
            Frame.Draw->AddLine(ImVec2(Scroll.x, Y), ImVec2(Scroll.x + RightW, Y), S.Line, std::max(1.f, Scale));
            float FY = Y + 19 * Scale;
            DrawBrowserText(Frame.Draw, Font, 9 * Scale, ImVec2(Scroll.x + Side, FY), S.Muted,
                "Add a note to this preview");
            FY += 9 * Scale + 10 * Scale;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, S.Text);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, S.Faint);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::SetCursorPos(ImVec2(Side + ImGui::GetScrollX(), FY - Scroll.y + ImGui::GetScrollY()));
            ImGui::SetNextItemWidth(InnerW);
            ImGui::PushID("comment");
            ImGui::InputTextMultiline("##text", CommentText.data(), CommentText.size(), ImVec2(InnerW, 72 * Scale));
            ImGui::PopID();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            FY += 72 * Scale + 8 * Scale;
            DrawBrowserText(Frame.Draw, Font, 8 * Scale, ImVec2(Scroll.x + Side, FY + 6 * Scale), S.Faint,
                "Saved in this browser only");
            const float SendW = MeasureBrowserText(Font, 9 * Scale, "Send").x + 24 * Scale + 14 * Scale + 10 * Scale;
            if (DrawBrowserPrimary("send", ImVec2(Scroll.x + Side + InnerW - SendW, FY), "Send",
                    BrowserIcon::Arrow, true, Frame, 9 * Scale, 12 * Scale, 24 * Scale, 14))
                SubmitComment();
        }
        else
        {
            DrawBrowserText(Frame.Draw, Font, 8 * Scale, ImVec2(Scroll.x + Side, Y), S.Faint,
                Entry->Imported ? "NO NATIVE HISTORY CONNECTED" : "SAMPLE VERSION HISTORY · READ-ONLY");
            Y += 8 * Scale + 22 * Scale;
            if (Entry->Imported)
            {
                Y += DrawBrowserParagraph(Frame.Draw, Font, 10 * Scale, ImVec2(Scroll.x + Side, Y), S.Muted,
                    "The native history archive has not been read. No revisions have been invented for this file.",
                    InnerW, 19 * Scale);
            }
            else
                for (size_t Index = 0; Index < Entry->Revisions.size(); ++Index)
                {
                    const bool Selected = int(Index) == ActiveRevision;
                    const float ThumbH = (InnerW - 18 * Scale - 10 * Scale) / 1.9f;
                    const float PH = Font->CalcTextSizeA(9 * Scale, FLT_MAX, InnerW - 28 * Scale,
                                         SanitiseBrowserText(Entry->Revisions[Index], Font, 9 * Scale).c_str())
                                         .y;
                    const float RowH = 9 * Scale + 12 * Scale + 5 * Scale + 10 * Scale + 5 * Scale + PH + 10 * Scale +
                                       ThumbH + 20 * Scale;
                    const ImVec2 Min(Scroll.x + Side, Y);
                    const std::string HitId = "rev-" + std::to_string(Index);
                    const bool Clicked = PlaceBrowserHit(Min, ImVec2(InnerW, RowH), HitId.c_str());
                    if (Selected)
                        Frame.Draw->AddRectFilled(ImVec2(Min.x + 1, Min.y),
                            ImVec2(Min.x + InnerW, Min.y + RowH), S.Selected, 6 * Scale,
                            ImDrawFlags_RoundCornersRight);
                    Frame.Draw->AddLine(ImVec2(Min.x, Min.y), ImVec2(Min.x, Min.y + RowH), S.Line,
                        std::max(1.f, Scale));
                    Frame.Draw->AddCircleFilled(ImVec2(Min.x, Min.y + 9 * Scale + 5 * Scale + 3 * Scale), 3.5f * Scale,
                        Index == 0 ? S.Text : S.Muted);
                    if (Selected)
                    {
                        Frame.Draw->AddCircle(ImVec2(Min.x, Min.y + 9 * Scale + 5 * Scale + 3 * Scale),
                            6.5f * Scale, S.Panel, 16, 3 * Scale);
                        Frame.Draw->AddCircleFilled(ImVec2(Min.x, Min.y + 9 * Scale + 5 * Scale + 3 * Scale),
                            3.5f * Scale, S.Text);
                    }
                    const float RX = Min.x + 18 * Scale;
                    float RY = Min.y + 9 * Scale;
                    const std::string Strong = "Version " + std::to_string(Entry->Revisions.size() - Index);
                    DrawBrowserText(Frame.Draw, Font, 10 * Scale, ImVec2(RX, RY), S.Text, Strong);
                    if (Index == 0)
                        DrawBrowserText(Frame.Draw, Font, 6 * Scale,
                            ImVec2(RX + MeasureBrowserText(Font, 10 * Scale, Strong).x + 8 * Scale, RY + 3 * Scale),
                            S.Faint, "CURRENT");
                    RY += 10 * Scale + 5 * Scale;
                    DrawBrowserText(Frame.Draw, Font, 8 * Scale, ImVec2(RX, RY), S.Faint,
                        BrowserRevisionDate(Index) + " · Sample revision");
                    RY += 8 * Scale + 5 * Scale;
                    RY += DrawBrowserParagraph(Frame.Draw, Font, 9 * Scale, ImVec2(RX, RY), S.Text,
                        Entry->Revisions[Index], InnerW - 28 * Scale, 9 * Scale * 1.5f);
                    RY += 10 * Scale;
                    ConstructThumbnailTile(ImVec2(RX, RY), ImVec2(InnerW - 28 * Scale, ThumbH), *Entry, 0,
                        int(Index), BrowserThumbFit::Cover, Scale, 4 * Scale, ImDrawFlags_RoundCornersAll);
                    Frame.Draw->AddRect(ImVec2(RX, RY), ImVec2(RX + InnerW - 28 * Scale, RY + ThumbH), S.Line,
                        4 * Scale, 0, std::max(1.f, Scale));
                    Y += RowH;
                    if (Clicked) ConfigureRevision(int(Index));
                }
            Y += 18 * Scale;
            DrawBrowserChip(ImVec2(Scroll.x + Side, Y),
                "Select a version to review it · restoring disk files is not connected", Frame);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
    (void)LeftW;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DIALOGS AND TOAST
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructLaunchDialog(float Width)
{
    if (LaunchOpen && !ImGui::IsPopupOpen("##launch")) ImGui::OpenPopup("##launch");
    if (!LaunchOpen || !ImGui::IsPopupOpen("##launch")) return;
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 153));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 19 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::max(1.f, Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, S.Panel);
    ImGui::PushStyleColor(ImGuiCol_Border, S.Line);
    const float DialogW = std::min(480 * Scale, Width - 32 * Scale);
    ImGui::SetNextWindowSize(ImVec2(DialogW, 0));
    const ImVec2 Centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(Centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    bool Open = true;
    if (ImGui::BeginPopupModal("##launch", &Open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
    {
        BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, S, Width, false};
        ImDrawList *Draw = Frame.Draw;
        ImFont *Font = Frame.Font;
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Pad = 34 * Scale;
        float Y = Origin.y + Pad;
        const float InnerW = DialogW - Pad * 2;
        const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
        DrawBrowserSpaced(Draw, Font, 9 * Scale, ImVec2(Origin.x + Pad, Y), S.Muted, "NATIVE HAND-OFF PREVIEW",
            1.8f * Scale);
        Y += 9 * Scale + 14 * Scale;
        DrawBrowserText(Draw, Font, 25 * Scale, ImVec2(Origin.x + Pad, Y), S.Text, "Ready for the next step.");
        Y += 25 * Scale + 12 * Scale;
        Y += DrawBrowserParagraph(Draw, Font, 12 * Scale, ImVec2(Origin.x + Pad, Y), S.Muted,
            Entry != nullptr && Entry->Imported ? "This is the selected local file for a future native hand-off."
                                                : "This is a sample file used to review the launch flow.",
            InnerW, 12 * Scale * 1.8f);
        Y += 20 * Scale;
        float HandH = 18 * 2 * Scale + 10 * Scale + 10 * Scale + 8 * 3 * Scale;
        const std::string Target = Entry != nullptr ? BrowserLaunchTarget(Entry->Category) : "Frontier";
        const std::string File = Entry != nullptr ? Entry->Path : "";
        const float FileH = DrawBrowserParagraph(nullptr, Font, 11 * Scale, ImVec2(0, 0), S.Text, File,
            InnerW - 36 * Scale, 11 * Scale * 1.5f);
        HandH += 12 * Scale + FileH;
        Draw->AddRectFilled(ImVec2(Origin.x + Pad, Y), ImVec2(Origin.x + Pad + InnerW, Y + HandH), S.Control,
            9 * Scale);
        Draw->AddRect(ImVec2(Origin.x + Pad, Y), ImVec2(Origin.x + Pad + InnerW, Y + HandH), S.Line, 9 * Scale, 0,
            std::max(1.f, Scale));
        float HY = Y + 18 * Scale;
        DrawBrowserSpaced(Draw, Font, 8 * Scale, ImVec2(Origin.x + Pad + 18 * Scale, HY), S.Faint, "OPEN WITH",
            1 * Scale);
        HY += 8 * Scale + 8 * Scale;
        DrawBrowserText(Draw, Font, 12 * Scale, ImVec2(Origin.x + Pad + 18 * Scale, HY), S.Text, Target);
        HY += 12 * Scale + 10 * Scale + 8 * Scale;
        DrawBrowserSpaced(Draw, Font, 8 * Scale, ImVec2(Origin.x + Pad + 18 * Scale, HY), S.Faint, "FILE", 1 * Scale);
        HY += 8 * Scale + 8 * Scale;
        DrawBrowserParagraph(Draw, Font, 11 * Scale, ImVec2(Origin.x + Pad + 18 * Scale, HY), S.Text, File,
            InnerW - 36 * Scale, 11 * Scale * 1.5f);
        Y += HandH + 12 * Scale;
        Y += DrawBrowserParagraph(Draw, Font, 10 * Scale, ImVec2(Origin.x + Pad, Y), S.Muted,
            "This preview does not launch the editor or decode native project content. Your original file is unchanged.",
            InnerW, 10 * Scale * 1.8f);
        Y += 20 * Scale;
        const float ButtonW = MeasureBrowserText(Font, 13 * Scale, "Back to preview").x + 40 * Scale;
        if (DrawBrowserPrimary("return", ImVec2(Origin.x + Pad, Y), "Back to preview", BrowserIcon::Back, false,
                Frame, 13 * Scale, 20 * Scale, 37 * Scale))
        {
            LaunchOpen = false;
            ImGui::CloseCurrentPopup();
        }
        Y += 37 * Scale + Pad;
        ImGui::Dummy(ImVec2(DialogW, Y - Origin.y));
        if (DrawBrowserIconButton("close-dialog", ImVec2(Origin.x + DialogW - 16 * Scale - 30 * Scale,
                                                     Origin.y + 16 * Scale),
                30 * Scale, BrowserIcon::Close, Frame, true, 0, 16, true))
        {
            LaunchOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (!Open) LaunchOpen = false;
        ImGui::EndPopup();
    }
    else if (!Open) LaunchOpen = false;
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

void ProjectBrowserPanel::ConstructCreationModal()
{
    if (Creating && !ImGui::IsPopupOpen("##create")) ImGui::OpenPopup("##create");
    if (!Creating || !ImGui::IsPopupOpen("##create")) return;
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 153));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 19 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::max(1.f, Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, S.Panel);
    ImGui::PushStyleColor(ImGuiCol_Border, S.Line);
    const float DialogW = std::min(440 * Scale, ImGui::GetMainViewport()->Size.x - 32 * Scale);
    ImGui::SetNextWindowSize(ImVec2(DialogW, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    bool Open = true;
    if (ImGui::BeginPopupModal("##create", &Open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
    {
        BrowserFrame Frame{ImGui::GetWindowDrawList(), ImGui::GetFont(), Scale, S, 0, false};
        ImDrawList *Draw = Frame.Draw;
        ImFont *Font = Frame.Font;
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Pad = 30 * Scale;
        float Y = Origin.y + Pad;
        const float InnerW = DialogW - Pad * 2;
        DrawBrowserText(Draw, Font, 20 * Scale, ImVec2(Origin.x + Pad, Y), S.Text, "New project");
        Y += 20 * Scale + 18 * Scale;
        DrawBrowserText(Draw, Font, 9 * Scale, ImVec2(Origin.x + Pad, Y), S.Muted, "Name");
        Y += 9 * Scale + 8 * Scale;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, S.Control);
        ImGui::PushStyleColor(ImGuiCol_Text, S.Text);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, S.Faint);
        ImGui::PushStyleColor(ImGuiCol_Border, S.Line);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.f, Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * Scale);
        ImGui::SetCursorPos(ImVec2(Pad + ImGui::GetScrollX(), Y - Origin.y + ImGui::GetScrollY()));
        ImGui::SetNextItemWidth(InnerW);
        ImGui::InputTextWithHint("##name", "Untitled project", NameText.data(), NameText.size());
        Y += 30 * Scale + 16 * Scale;
        Draw->AddText(Font, 9 * Scale, ImVec2(Origin.x + Pad, Y), S.Muted, "Destination .projectspace");
        Y += 9 * Scale + 8 * Scale;
        ImGui::SetCursorPos(ImVec2(Pad + ImGui::GetScrollX(), Y - Origin.y + ImGui::GetScrollY()));
        ImGui::SetNextItemWidth(InnerW - 90 * Scale);
        ImGui::InputTextWithHint(
            "##destination", "Choose a destination…", DestinationText.data(), DestinationText.size());
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        const float BrowseW = MeasureBrowserText(Font, 11 * Scale, "Browse").x + 28 * Scale;
        const ImVec2 BrowseMin(Origin.x + Pad + InnerW - BrowseW, Y - 4 * Scale);
        const bool BrowseClicked = PlaceBrowserHit(BrowseMin, ImVec2(BrowseW, 30 * Scale), "create-browse");
        Frame.Draw->AddRect(BrowseMin, ImVec2(BrowseMin.x + BrowseW, BrowseMin.y + 30 * Scale),
            ImGui::IsItemHovered() ? S.Faint : S.Line, 7 * Scale, 0, std::max(1.f, Scale));
        DrawBrowserText(Draw, Font, 11 * Scale, ImVec2(BrowseMin.x + 14 * Scale, BrowseMin.y + 8 * Scale), S.Text,
            "Browse");
        if (BrowseClicked && PickProjectPath)
        {
            const std::filesystem::path Picked = PickProjectPath(true);
            if (!Picked.empty())
                std::snprintf(DestinationText.data(), DestinationText.size(), "%s", EncodeContentPath(Picked).c_str());
        }
        Y += 30 * Scale + 12 * Scale;
        if (!CreateError.empty())
        {
            Y += DrawBrowserParagraph(Draw, Font, 10 * Scale, ImVec2(Origin.x + Pad, Y), HexColour("#d98a8a"),
                CreateError, InnerW, 15 * Scale);
            Y += 8 * Scale;
        }
        const float CreateW = MeasureBrowserText(Font, 12 * Scale, "Create project").x + 40 * Scale;
        const float CancelW = MeasureBrowserText(Font, 12 * Scale, "Cancel").x + 28 * Scale;
        float BX = Origin.x + Pad + InnerW - CreateW;
        if (DrawBrowserPrimary("create-go", ImVec2(BX, Y), "Create project", BrowserIcon::Plus, false, Frame,
                12 * Scale, 20 * Scale, 36 * Scale))
        {
            if (CreateProject(DecodeContentPath(DestinationText.data()), NameText.data()))
            {
                CreateError.clear();
                Creating = false;
                ImGui::CloseCurrentPopup();
                AttachPaths({SelectedPath}, true);
            }
            else CreateError = Diagnostic;
        }
        BX -= 10 * Scale + CancelW;
        const bool CancelClicked = PlaceBrowserHit(ImVec2(BX, Y), ImVec2(CancelW, 36 * Scale), "create-cancel");
        Frame.Draw->AddRect(ImVec2(BX, Y), ImVec2(BX + CancelW, Y + 36 * Scale),
            ImGui::IsItemHovered() ? S.Faint : S.Line, 18 * Scale, 0, std::max(1.f, Scale));
        DrawBrowserText(Draw, Font, 12 * Scale, ImVec2(BX + 14 * Scale, Y + 11 * Scale), S.Text, "Cancel");
        if (CancelClicked)
        {
            Creating = false;
            CreateError.clear();
            ImGui::CloseCurrentPopup();
        }
        Y += 36 * Scale + Pad;
        ImGui::Dummy(ImVec2(DialogW, Y - Origin.y));
        if (!Open)
        {
            Creating = false;
            CreateError.clear();
        }
        ImGui::EndPopup();
    }
    else if (!Open)
    {
        Creating = false;
        CreateError.clear();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

void ProjectBrowserPanel::ConstructToast(ImVec2 Origin, ImVec2 Size)
{
    if (Toast.empty() || std::chrono::steady_clock::now() > ToastUntil) return;
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const BrowserSurfaces S = QueryBrowserSurfaces(Prefs.Theme);
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    ImFont *Font = ImGui::GetFont();
    const float MaxW = std::min(600 * Scale, Size.x * 0.9f) - 40 * Scale;
    const float TextH = DrawBrowserParagraph(nullptr, Font, 11 * Scale, ImVec2(0, 0), S.Text, Toast, MaxW,
        11 * Scale * 1.5f);
    const float BoxW = std::min(MaxW, MeasureBrowserText(Font, 11 * Scale, Toast).x) + 40 * Scale;
    const float BoxH = TextH + 26 * Scale;
    const ImVec2 Min(Origin.x + (Size.x - BoxW) / 2, Origin.y + Size.y - 25 * Scale - BoxH);
    Draw->AddRectFilled(Min, ImVec2(Min.x + BoxW, Min.y + BoxH), S.Nested, 10 * Scale);
    Draw->AddRect(Min, ImVec2(Min.x + BoxW, Min.y + BoxH), S.Line, 10 * Scale, 0, std::max(1.f, Scale));
    DrawBrowserParagraph(Draw, Font, 11 * Scale, ImVec2(Min.x + 20 * Scale, Min.y + 13 * Scale), S.Text, Toast,
        BoxW - 40 * Scale, 11 * Scale * 1.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME DRIVER
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructInterface()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::SetNextWindowViewport(Viewport->ID);
    const auto Colour = [](ColorQuad Quad) { return ImVec4(Quad.Red, Quad.Green, Quad.Blue, Quad.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colour(Colours.Field));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colour(Colours.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(Colours.Text));
    ImGui::PushStyleColor(ImGuiCol_Border, Colour(Colours.StrokeStrong));
    ImGui::PushStyleColor(ImGuiCol_Separator, Colour(Colours.Divider));
    ImGui::PushStyleColor(ImGuiCol_Button, Colour(Colours.Raised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colour(Colours.Selected));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Colour(Colours.Inset));
    ImGui::PushStyleColor(ImGuiCol_Header, Colour(Colours.Selected));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Colour(Colours.Raised));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    constexpr auto Options = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("Project browser", nullptr, Options);
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Avail = ImGui::GetContentRegionAvail();
    const float Width = Avail.x / std::max(0.01f, ImGui::GetStyle().FontScaleDpi);
    if (!ResponsiveConfigured)
    {
        LeftCollapsed = Width < 600;
        RightCollapsed = Width < 1000;
        ResponsiveConfigured = true;
    }
    else
    {
        if (Width < 600) LeftCollapsed = true;
        if (Width < 1000) RightCollapsed = true;
    }
    const bool Typing = ImGui::GetIO().WantTextInput || LaunchOpen || Creating;
    if (!Typing)
    {
        if (ActiveToken.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Slash, false)) FocusSearch = true;
        }
        else
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CloseFile();
            const BrowserCatalogueEntry *Entry = FindEntry(ActiveToken);
            const bool CanStep =
                Entry != nullptr && !DocumentEnabled && Entry->Views.size() > 1 && ImGui::GetIO().WantTextInput == false;
            if (CanStep && Entry != nullptr)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
                {
                    PreviewIndex = (PreviewIndex + 1) % int(Entry->Views.size());
                    DocumentEnabled = false;
                    Zoom = 100;
                    ResetLiveCamera(*Entry);
                    LiveChannel = PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision).Channel;
                    CaptureDelay = 2;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
                {
                    PreviewIndex = (PreviewIndex + int(Entry->Views.size()) - 1) % int(Entry->Views.size());
                    DocumentEnabled = false;
                    Zoom = 100;
                    ResetLiveCamera(*Entry);
                    LiveChannel = PreviewConfigFor(*Entry, size_t(PreviewIndex), ActiveRevision).Channel;
                    CaptureDelay = 2;
                }
            }
        }
    }
    if (ActiveToken.empty()) ConstructLibrary(Width);
    else
    {
        ConstructReader(Width);
        const float Scale = ImGui::GetStyle().FontScaleDpi;
        ConstructReaderInspector(Width, Origin, Avail, (Width <= 700 ? 72 : 64) * Scale, Scale);
    }
    ConstructLaunchDialog(Width);
    ConstructCreationModal();
    ImGui::SetCursorPos(ImVec2(Origin.x - ImGui::GetWindowPos().x, Origin.y - ImGui::GetWindowPos().y));
    ImGui::Dummy(Avail);
    ConstructToast(Origin, Avail);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(10);
}

} // namespace Frontier
