/*============================================================================================================================================
                                                         PATTERNWORKSPACEPANEL.CPP
============================================================================================================================================*/
// 🧩 Port the approved Pattern layout to native controls; retain actual GPU canvas images and atomic source edits across dock presentations.

#include "Engine/ContentInterchange/PatternLibraryPresets.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/ThumbnailAtlasCache.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "PatternWorkspacePanel.h"
#include "VectorMaskControls.h"
#include "WorkspaceZoom.h"
#include <toml++/toml.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <numeric>
#include <cctype>

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

ImVec4 Spectrum(uint32_t Colour)
{
    return {float((Colour >> 16) & 255) / 255, float((Colour >> 8) & 255) / 255, float(Colour & 255) / 255, 1};
}

void Caption(
    ImVec2      Position,
    const char *Text,
    float       Size,
    uint32_t    Colour,
    void       *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont(), Size * 1.302f, Position, ImGui::GetColorU32(Spectrum(Colour)), Text);
}

// 📝 Rounded, padded modal chrome for the close/unsaved-changes prompt. ConstructInterface zeroes
// WindowRounding/WindowPadding for the whole docked workspace, so this overlay needs its own scope
// or it inherits flush, square edges instead of looking like a deliberate dialog.
struct PatternModalScope
{
    explicit PatternModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~PatternModalScope()
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

}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT AND PRESENTATION LIFETIME
//------------------------------------------------------------------------------------------------------------------------

PatternWorkspacePanel::PatternWorkspacePanel()
{
    VectorAuthoringDocument Blank;
    Blank.Name = "Untitled pattern";
    ConfigureDocument(Blank);
    Controls.RegisterExtent = [&](std::string_view CaptionText, PlaneExtent Extent) { ControlExtents[std::string(CaptionText)] = Extent; };
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Pattern panels");
    Panels.ConfigureWorkspaceInsets(43, 0);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"pattern-library", "Pattern library", [&]() { ConstructLibrary(); }});
    Panels.Register({"pattern-layers", "Layers", [&]() { ConstructLayers(); }});
    Panels.Register({"pattern-canvas", "2D viewport", [&]() { ConstructCanvas(); }});
    Panels.Register({"pattern-inspector", "Inspector", [&]() { ConstructInspector(); }});
    (void)Panels.ConfigureStackedPanels("pattern-library", "pattern-layers", 32.f);
    Panels.ConfigureColumns({{"pattern-layers", 278.f / 1436}, {"pattern-canvas", 820.f / 1436},
                             {"pattern-inspector", 338.f / 1436}});
    ArraySpecification.Columns = 3;
    ArraySpecification.StepX = 80;
    ArraySpecification.Radius = 120;
    ArraySpecification.Rings = 2;
    ArraySpecification.RadiusStep = 60;
    ArraySpecification.EndScale = 50;
}

void PatternWorkspacePanel::ConfigureInterfaceScale(float Replacement)
{
    Scale = std::isfinite(Replacement) ? std::clamp(Replacement, .75f, 1.5f) : 1;
    Controls.Scale = Scale;
}

void PatternWorkspacePanel::ConfigureTypeface(void *Regular, void *Light)
{
    RegularTypeface = Regular;
    LightTypeface = Light;
}

void PatternWorkspacePanel::ConfigureDocument(VectorAuthoringDocument Document)
{
    if (Document.Stamp) throw std::runtime_error("Open a native Pattern document, not a Stamp document.");
    CancelPen();
    History.Reset(std::move(Document));
    SavedDocument = History.QueryDocument();
    Selection.clear();
    UndoSelections.clear(); RedoSelections.clear();
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    MaskSelection = 0; PointToken = 0; ActiveAnchor = SIZE_MAX; SelectedAnchors.clear(); AutomaticPen = false;
    Diagnostic.clear();
}

bool PatternWorkspacePanel::ConfigurePresetDirectory(const std::filesystem::path &Directory)
{
    // The shipped library carries a Library.toml sidecar (families, groups, palette
    // variants); decoding stays lazy per gallery page. Plain folders of TOMLs keep
    // working as single-palette families for local experiments.
    std::vector<PatternLibraryFamilyEntry> Replacement;
    std::vector<std::string> Groups, Palettes;
    std::string DefaultPalette = "Indigo";
    std::filesystem::path Resolved;
    try
    {
        for (const auto &Candidate : {Directory / "Library.toml", Directory / "starters" / "Library.toml"})
        {
            if (Directory.empty() || !std::filesystem::is_regular_file(Candidate)) continue;
            const auto Table = toml::parse_file(Candidate.string());
            if (std::string(Table["format"].value_or("")) != "frontier.pattern_library" || Table["version"].value_or(0) != 1)
                throw std::runtime_error("Unsupported pattern library sidecar.");
            DefaultPalette = Table["default_palette"].value_or("Indigo");
            if (const auto *Array = Table["palettes"].as_array())
                for (const auto &Item : *Array)
                    if (const auto Value = Item.value<std::string>()) Palettes.push_back(*Value);
            if (const auto *Array = Table["groups"].as_array())
                for (const auto &Item : *Array)
                    if (const auto Value = Item.value<std::string>()) Groups.push_back(*Value);
            const auto *Families = Table["families"].as_array();
            if (!Families) throw std::runtime_error("Pattern library sidecar holds no families.");
            for (const auto &Item : *Families)
            {
                const auto *Family = Item.as_table();
                if (!Family) throw std::runtime_error("Pattern library family is malformed.");
                PatternLibraryFamilyEntry Entry;
                Entry.Name = (*Family)["name"].value_or("");
                Entry.Group = (*Family)["group"].value_or("");
                Entry.Description = (*Family)["description"].value_or("");
                Entry.Study = (*Family)["study"].value_or("");
                const auto *Files = (*Family)["files"].as_table();
                if (Entry.Name.empty() || !Files || Files->empty()) throw std::runtime_error("Pattern library family is malformed.");
                for (const auto &[Palette, File] : *Files)
                    if (const auto Value = File.value<std::string>()) Entry.Files[std::string(Palette.str())] = *Value;
                if (Entry.Files.empty()) throw std::runtime_error("Pattern library family holds no palette files.");
                Replacement.push_back(std::move(Entry));
            }
            Resolved = Candidate.parent_path();
            break;
        }
        if (Replacement.empty() && !Directory.empty() && std::filesystem::is_directory(Directory))
        {
            std::vector<std::filesystem::path> Files;
            for (const auto &Entry : std::filesystem::directory_iterator(Directory))
                if (Entry.is_regular_file() && Entry.path().extension() == ".toml") Files.push_back(Entry.path());
            std::sort(Files.begin(), Files.end());
            for (const auto &File : Files)
            {
                PatternLibraryFamilyEntry Entry;
                Entry.Name = File.stem().string();
                Entry.Files[DefaultPalette] = File.filename().string();
                Replacement.push_back(std::move(Entry));
            }
            Resolved = Directory;
        }
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
    if (Replacement.empty()) return false;
    LibraryFamilies = std::move(Replacement);
    LibraryGroups = std::move(Groups);
    LibraryPalettes = std::move(Palettes);
    LibraryDefaultPalette = std::move(DefaultPalette);
    LibraryDirectory = Resolved;
    LibraryGroup = "All patterns";
    LibraryPalette.clear();
    LibrarySearchText[0] = 0;
    LibraryPage = 0;
    LibrarySelected.clear();
    PresetCache.clear();
    PresetCacheOrder.clear();
    PatternRasterCache.clear();
    PatternRasterOrder.clear();
    PatternAtlasKey.clear();
    PatternAtlasPixels.clear();
    PatternAtlasBaked.clear();
    return true;
}

namespace
{
void FlattenThumbnailCubic(std::array<float, 2> A, std::array<float, 2> B, std::array<float, 2> C, std::array<float, 2> D,
    std::vector<std::array<float, 2>> &Points, float Tolerance, int Depth = 0)
{
    const float Dx = D[0] - A[0], Dy = D[1] - A[1];
    const float Length = std::hypot(Dx, Dy);
    const float Deviation = Length > 1e-6f
        ? std::max(std::abs((B[0] - A[0]) * Dy - (B[1] - A[1]) * Dx), std::abs((C[0] - A[0]) * Dy - (C[1] - A[1]) * Dx)) / Length
        : 0;
    if (Depth >= 6 || Deviation <= Tolerance) { Points.push_back(D); return; }
    const auto Mid = [](const std::array<float, 2> &P, const std::array<float, 2> &Q) { return std::array<float, 2>{(P[0] + Q[0]) * .5f, (P[1] + Q[1]) * .5f}; };
    const auto AB = Mid(A, B), BC = Mid(B, C), CD = Mid(C, D);
    const auto ABC = Mid(AB, BC), BCD = Mid(BC, CD);
    const auto ABCD = Mid(ABC, BCD);
    FlattenThumbnailCubic(A, AB, ABC, ABCD, Points, Tolerance, Depth + 1);
    FlattenThumbnailCubic(ABCD, BCD, CD, D, Points, Tolerance, Depth + 1);
}
void SimplifyThumbnailRange(const std::vector<std::array<float, 2>> &Points, size_t First, size_t Last, float Tolerance, std::vector<char> &Keep)
{
    float Farthest = 0;
    size_t Split = First;
    const float Dx = Points[Last][0] - Points[First][0], Dy = Points[Last][1] - Points[First][1];
    const float Length = std::hypot(Dx, Dy);
    for (size_t Index = First + 1; Index < Last; ++Index)
    {
        const float Distance = Length > 1e-6f
            ? std::abs((Points[Index][0] - Points[First][0]) * Dy - (Points[Index][1] - Points[First][1]) * Dx) / Length
            : 0;
        if (Distance > Farthest) { Farthest = Distance; Split = Index; }
    }
    if (Farthest <= Tolerance || Split == First) return;
    Keep[Split] = 1;
    SimplifyThumbnailRange(Points, First, Split, Tolerance, Keep);
    SimplifyThumbnailRange(Points, Split, Last, Tolerance, Keep);
}
void SimplifyThumbnailContour(std::vector<std::array<float, 2>> &Points, float Tolerance)
{
    if (Points.size() < 3) return;
    std::vector<char> Keep(Points.size(), 0);
    Keep.front() = Keep.back() = 1;
    // Closed loops have coincident endpoints, which would degenerate the opening
    // chord and collapse the contour; split at the vertex farthest from the seam.
    if (std::hypot(Points.front()[0] - Points.back()[0], Points.front()[1] - Points.back()[1]) <= Tolerance)
    {
        float Farthest = 0;
        size_t Split = 0;
        for (size_t Index = 1; Index + 1 < Points.size(); ++Index)
        {
            const float Distance = std::hypot(Points[Index][0] - Points.front()[0], Points[Index][1] - Points.front()[1]);
            if (Distance > Farthest) { Farthest = Distance; Split = Index; }
        }
        if (Split > 0)
        {
            Keep[Split] = 1;
            SimplifyThumbnailRange(Points, 0, Split, Tolerance, Keep);
            SimplifyThumbnailRange(Points, Split, Points.size() - 1, Tolerance, Keep);
        }
    }
    else SimplifyThumbnailRange(Points, 0, Points.size() - 1, Tolerance, Keep);
    std::vector<std::array<float, 2>> Reduced;
    for (size_t Index = 0; Index < Points.size(); ++Index)
        if (Keep[Index]) Reduced.push_back(Points[Index]);
    Points = std::move(Reduced);
}
std::vector<std::array<float, 2>> TriangulateThumbnailLoop(std::vector<std::array<float, 2>> Loop)
{
    std::vector<std::array<float, 2>> Triangles;
    std::vector<std::array<float, 2>> Clean;
    for (const auto &Point : Loop)
        if (Clean.empty() || std::hypot(Point[0] - Clean.back()[0], Point[1] - Clean.back()[1]) > 1e-4f) Clean.push_back(Point);
    while (Clean.size() > 1 && std::hypot(Clean.front()[0] - Clean.back()[0], Clean.front()[1] - Clean.back()[1]) <= 1e-4f) Clean.pop_back();
    Loop = std::move(Clean);
    if (Loop.size() < 3) return Triangles;
    float Area = 0;
    for (size_t Index = 0; Index < Loop.size(); ++Index)
    {
        const auto &P = Loop[Index], &Q = Loop[(Index + 1) % Loop.size()];
        Area += (Q[0] - P[0]) * (Q[1] + P[1]);
    }
    std::vector<size_t> Remaining(Loop.size());
    std::iota(Remaining.begin(), Remaining.end(), 0);
    if (Area > 0) std::reverse(Remaining.begin(), Remaining.end());
    const auto Cross = [&](size_t A, size_t B, size_t C)
    { return (Loop[B][0] - Loop[A][0]) * (Loop[C][1] - Loop[A][1]) - (Loop[B][1] - Loop[A][1]) * (Loop[C][0] - Loop[A][0]); };
    const auto Inside = [&](size_t A, size_t B, size_t C, size_t P)
    {
        const float U = Cross(P, A, B), V = Cross(P, B, C), W = Cross(P, C, A);
        return U >= -1e-6f && V >= -1e-6f && W >= -1e-6f;
    };
    for (size_t Guard = 0; Remaining.size() > 3 && Guard < Loop.size() * Loop.size(); ++Guard)
    {
        bool Clipped = false;
        for (size_t Slot = 0; Slot < Remaining.size(); ++Slot)
        {
            const size_t A = Remaining[(Slot + Remaining.size() - 1) % Remaining.size()];
            const size_t B = Remaining[Slot];
            const size_t C = Remaining[(Slot + 1) % Remaining.size()];
            if (Cross(A, B, C) <= 1e-6f) continue;
            bool Occupied = false;
            for (size_t Other : Remaining)
                if (Other != A && Other != B && Other != C && Inside(A, B, C, Other)) { Occupied = true; break; }
            if (Occupied) continue;
            Triangles.push_back(Loop[A]); Triangles.push_back(Loop[B]); Triangles.push_back(Loop[C]);
            Remaining.erase(Remaining.begin() + Slot);
            Clipped = true;
            break;
        }
        if (!Clipped) break;
    }
    if (Remaining.size() == 3)
    {
        Triangles.push_back(Loop[Remaining[0]]); Triangles.push_back(Loop[Remaining[1]]); Triangles.push_back(Loop[Remaining[2]]);
    }
    return Triangles;
}
}

std::vector<size_t> PatternWorkspacePanel::QueryLibraryMatches() const
{
    std::string Query = LibrarySearchText.data();
    const auto Trimmed = Query.find_last_not_of(" \t\n\r");
    Query = Trimmed == std::string::npos ? "" : Query.substr(0, Trimmed + 1);
    const auto Lower = [](std::string Text)
    {
        for (char &Byte : Text) Byte = char(std::tolower((unsigned char)Byte));
        return Text;
    };
    Query = Lower(Query);
    std::vector<size_t> Matches;
    for (size_t Index = 0; Index < LibraryFamilies.size(); ++Index)
    {
        const auto &Family = LibraryFamilies[Index];
        if (LibraryGroup != "All patterns" && Family.Group != LibraryGroup) continue;
        if (!Query.empty() && Lower(Family.Name + " " + Family.Group + " " + Family.Description + " " + Family.Study).find(Query) == std::string::npos)
            continue;
        Matches.push_back(Index);
    }
    return Matches;
}

void PatternWorkspacePanel::ConfigureLibraryFilter(const std::string &Group, const std::string &Palette, bool ClearSearch)
{
    LibraryGroup = Group;
    if (!Palette.empty()) LibraryPalette = Palette;
    if (ClearSearch) LibrarySearchText[0] = 0;
    LibraryPage = 0;
}

void PatternWorkspacePanel::ConfigureLibrarySearch(const std::string &Text)
{
    const size_t Count = std::min(Text.size(), LibrarySearchText.size() - 1);
    std::copy_n(Text.data(), Count, LibrarySearchText.data());
    LibrarySearchText[Count] = 0;
    LibraryPage = 0;
}

const PatternWorkspacePanel::PatternPresetCacheEntry &PatternWorkspacePanel::QueryPresetEntry(const PatternLibraryFamilyEntry &Family)
{
    const std::string Palette = LibraryPalette.empty() ? LibraryDefaultPalette : LibraryPalette;
    auto File = Family.Files.find(Palette);
    if (File == Family.Files.end()) File = Family.Files.begin();
    const std::string Key = File->first + "\n" + File->second;
    const auto Cached = PresetCache.find(Key);
    if (Cached != PresetCache.end()) return Cached->second;
    PatternPresetCacheEntry Entry;
    try
    {
        std::ifstream Input(LibraryDirectory / File->second, std::ios::binary | std::ios::ate);
        const auto Bytes = Input.tellg();
        if (!Input || Bytes < 0 || Bytes > 4000000) throw std::runtime_error("Preset file is unreadable.");
        std::string Source(size_t(Bytes), '\0');
        Input.seekg(0);
        if (!Input.read(Source.data(), Bytes)) throw std::runtime_error("Preset file is truncated.");
        Entry.Document = DecodeVectorAuthoring(Source);
        if (Entry.Document.Stamp || Entry.Document.Layers.empty()) throw std::runtime_error("Preset holds no artwork.");
        Entry.Shapes = TessellatePresetDocument(Entry.Document);
    }
    catch (const std::exception &Failure) { Entry.Failed = true; Entry.Document = VectorAuthoringDocument{}; Diagnostic = Failure.what(); }
    while (PresetCacheOrder.size() >= 256)
    {
        PresetCache.erase(PresetCacheOrder.front());
        PresetCacheOrder.erase(PresetCacheOrder.begin());
    }
    PresetCacheOrder.push_back(Key);
    return PresetCache.emplace(Key, std::move(Entry)).first->second;
}

const std::vector<uint8_t> &PatternWorkspacePanel::QueryPatternRaster(const PatternLibraryFamilyEntry &Family, const PatternPresetCacheEntry &Entry)
{
    const std::string Palette = LibraryPalette.empty() ? LibraryDefaultPalette : LibraryPalette;
    auto File = Family.Files.find(Palette);
    if (File == Family.Files.end()) File = Family.Files.begin();
    const std::string Key = File->first + "\n" + File->second;
    const auto Cached = PatternRasterCache.find(Key);
    if (Cached != PatternRasterCache.end()) return Cached->second;
    // Baked background matches the card thumbnail well so empty tiles blend in.
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(24) << 16 | uint32_t(23) << 8 | uint32_t(20);
    std::vector<uint8_t> Pixels = RasterizePatternThumbnailSvg(Entry.Document, PatternAtlasTile, CardWell);
    while (PatternRasterOrder.size() >= 256)
    {
        PatternRasterCache.erase(PatternRasterOrder.front());
        PatternRasterOrder.erase(PatternRasterOrder.begin());
    }
    PatternRasterOrder.push_back(Key);
    return PatternRasterCache.emplace(Key, std::move(Pixels)).first->second;
}

uint64_t PatternWorkspacePanel::ComputePatternAtlasDigest(const std::string &Palette) const
{
    // 📝 Folds every source file this bake actually reads (per-family, per-palette SVG) plus the
    // layout constants into one digest, so any content edit, palette switch, family add/remove or
    // tile/column layout change naturally invalidates the on-disk cache instead of serving stale pixels.
    uint64_t Digest = 14695981039346656037ull;
    Digest = CombineHash(Digest, HashText(LibraryDirectory.generic_string()));
    Digest = CombineHash(Digest, HashText(Palette));
    Digest = CombineHash(Digest, uint64_t(PatternAtlasTile));
    Digest = CombineHash(Digest, uint64_t(PatternAtlasColumns));
    Digest = CombineHash(Digest, uint64_t(LibraryFamilies.size()));
    for (const auto &Family : LibraryFamilies)
    {
        auto File = Family.Files.find(Palette);
        if (File == Family.Files.end()) File = Family.Files.begin();
        if (File == Family.Files.end()) continue;
        Digest = CombineHash(Digest, HashText(Family.Name));
        Digest = CombineHash(Digest, HashFileContents(LibraryDirectory / File->second));
    }
    return Digest;
}
std::filesystem::path PatternWorkspacePanel::QueryPatternAtlasCacheFile(uint64_t Digest) const
{
    char Hex[17]{};
    std::snprintf(Hex, sizeof(Hex), "%016llx", static_cast<unsigned long long>(Digest));
    return QueryFrontierCacheDirectory() / "PatternAtlas" / (std::string(Hex) + ".atlas");
}
void PatternWorkspacePanel::RefreshPatternAtlas()
{
    // 📝 One library-wide atlas with stable family slots: page flips, searches and filters only
    // re-point card UVs. Each family bakes once per palette key; the revision (and GPU upload)
    // advances only when newly baked tiles join the atlas.
    const std::string Palette = LibraryPalette.empty() ? LibraryDefaultPalette : LibraryPalette;
    const std::string Key = Palette;
    const bool KeyChanged = Key != PatternAtlasKey;
    if (KeyChanged)
    {
        PatternAtlasKey = Key;
        PatternAtlasBaked.assign(LibraryFamilies.size(), 0);
        PatternAtlasPixels.clear();
        PatternAtlasCacheLoaded = false;
    }
    if (LibraryFamilies.empty()) return;
    if (PatternAtlasBaked.size() != LibraryFamilies.size())
    {
        PatternAtlasBaked.assign(LibraryFamilies.size(), 0);
        PatternAtlasPixels.clear();
        PatternAtlasCacheLoaded = false;
    }
    bool BakedNew = PatternAtlasPixels.empty();
    // 📝 Disk cache: on the first bake attempt for this (palette, library-content) identity, try to
    // load a previously baked atlas before touching ThorVG at all. A hit skips every per-family SVG
    // render outright; a miss (or any format/digest mismatch) silently falls through to the normal
    // live bake below and the result is written back for next launch once it completes.
    if (BakedNew && !PatternAtlasCacheLoaded)
    {
        PatternAtlasContentDigest = ComputePatternAtlasDigest(Palette);
        ThumbnailAtlasCacheRecord Cached;
        if (LoadThumbnailAtlasCache(QueryPatternAtlasCacheFile(PatternAtlasContentDigest), PatternAtlasContentDigest,
            PatternAtlasExtent, PatternAtlasExtent, Cached))
        {
            PatternAtlasPixels = std::move(Cached.Rgba);
            PatternAtlasBaked.assign(LibraryFamilies.size(), 1);
            PatternAtlasCacheLoaded = true;
            ++PatternAtlasRevision;
            return;
        }
    }
    for (size_t Family = 0; Family < LibraryFamilies.size() && Family < PatternAtlasCapacity; ++Family)
    {
        if (PatternAtlasBaked[Family]) continue;
        const auto &Entry = QueryPresetEntry(LibraryFamilies[Family]);
        if (!Entry.Failed && !Entry.Shapes.empty()) (void)QueryPatternRaster(LibraryFamilies[Family], Entry);
        PatternAtlasBaked[Family] = 1;
        BakedNew = true;
    }
    if (!BakedNew) return;
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(24) << 16 | uint32_t(23) << 8 | uint32_t(20);
    const std::vector<uint8_t> Blank = RasterizePatternThumbnailSvg(VectorAuthoringDocument{}, PatternAtlasTile, CardWell);
    std::vector<std::vector<uint8_t>> Tiles;
    const size_t Slotted = std::min(LibraryFamilies.size(), PatternAtlasCapacity);
    Tiles.reserve(Slotted);
    for (size_t Family = 0; Family < Slotted; ++Family)
    {
        if (!PatternAtlasBaked[Family]) { Tiles.push_back(Blank); continue; }
        const auto &Entry = QueryPresetEntry(LibraryFamilies[Family]);
        Tiles.push_back(!Entry.Failed && !Entry.Shapes.empty() ? QueryPatternRaster(LibraryFamilies[Family], Entry) : Blank);
    }
    // 📝 A blit failure (bounded-grid rejection, or genuine allocation exhaustion under memory
    // pressure) must never propagate into the render loop uncaught — that previously surfaced as a
    // fatal "bad allocation" dialog on shutdown unwind. Keep whatever atlas was last valid and
    // simply retry on the next bake-triggering change instead of tearing the application down.
    try
    {
        PatternAtlasPixels = BlitPatternThumbnailAtlas(Tiles, PatternAtlasTile, PatternAtlasColumns, CardWell);
        ++PatternAtlasRevision;
    }
    catch (const std::exception &Failure)
    {
        Frontier::DiagnosticJournal::Append("PatternAtlasBlitError", Failure.what());
        return;
    }
    // 📝 Full bake completed (this codepath never partially bakes across calls): persist it so the
    // next launch of this exact library+palette identity hits the cache load above instead.
    const bool AllBaked = std::all_of(PatternAtlasBaked.begin(), PatternAtlasBaked.end(), [](uint8_t Flag) { return Flag != 0; });
    if (AllBaked)
    {
        SaveThumbnailAtlasCache(QueryPatternAtlasCacheFile(PatternAtlasContentDigest), PatternAtlasContentDigest,
            PatternAtlasExtent, PatternAtlasExtent, PatternAtlasPixels);
        PruneThumbnailAtlasCacheDirectory(QueryFrontierCacheDirectory() / "PatternAtlas");
        PatternAtlasCacheLoaded = true;
    }
}

uint64_t PatternWorkspacePanel::QueryPatternAtlasRevision()
{
    RefreshPatternAtlas();
    return PatternAtlasRevision;
}

const std::vector<uint8_t> &PatternWorkspacePanel::QueryPatternAtlasPixels()
{
    RefreshPatternAtlas();
    return PatternAtlasPixels;
}

void PatternWorkspacePanel::RefreshLayerAtlas()
{
    // 📝 One small atlas carries every row's own preview thumbnail for the document that is
    // currently open; it rebakes whenever the document's edit revision moves (add/remove/reorder/
    // redraw a layer all count), exactly like the library atlas rebakes per newly-cached family.
    const uint64_t CurrentRevision = QueryRevision();
    if (CurrentRevision == LayerAtlasBakedRevision) return;
    LayerAtlasBakedRevision = CurrentRevision;
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { LayerAtlasPixels.clear(); return; }
    // 📝 Same reasoning as Stamp's own RefreshLayerAtlas: a layer's authored fill is routinely dark
    // (ink/outline colours), which can render at near-zero contrast against the library grid's dark
    // "empty tile" well — a seemingly missing row preview that actually rasterized fine, just
    // invisibly. Match the real 2D viewport's own board colour (Spectrum(0xede7d7), see
    // ConstructCanvas) instead, so a row's thumbnail always matches what the canvas itself shows.
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(215) << 16 | uint32_t(231) << 8 | uint32_t(237);
    const std::vector<uint8_t> Blank = RasterizePatternThumbnailSvg(VectorAuthoringDocument{}, LayerAtlasTile, CardWell);
    const size_t Slotted = std::min(Document.Layers.size(), LayerAtlasCapacity);
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> FreshCache;
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const uint32_t Token = Document.Layers[Slot].Token;
        // 📝 Only this layer's own cropped SVG text changes when this layer itself is edited; a
        // sibling layer untouched by the edit encodes byte-identical text, so its previous raster
        // is reused instead of paying a full ThorVG parse+raster again for every other layer.
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
        // 📝 A layer with no renderable geometry (e.g. an empty retained-vector shell) must not
        // sink the whole atlas bake; it simply keeps the baked background like an empty gallery slot.
        catch (const std::exception &) { Tiles.push_back(Blank); }
    }
    LayerThumbnailCache = std::move(FreshCache);
    // 📝 Same reasoning as RefreshPatternAtlas: never let a blit failure (bounded-grid rejection or
    // allocation exhaustion) escape uncaught — reordering/adding/removing layers bumps the document
    // revision every time, which re-enters this exact call every frame until it bakes cleanly again.
    try { LayerAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LayerAtlasTile, LayerAtlasColumns, CardWell); ++LayerAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("PatternLayerAtlasBlitError", Failure.what()); }
}

uint64_t PatternWorkspacePanel::QueryLayerAtlasRevision()
{
    RefreshLayerAtlas();
    return LayerAtlasRevision;
}

const std::vector<uint8_t> &PatternWorkspacePanel::QueryLayerAtlasPixels()
{
    RefreshLayerAtlas();
    return LayerAtlasPixels;
}

std::vector<PatternWorkspacePanel::PatternThumbnailShape> PatternWorkspacePanel::TessellatePresetDocument(const VectorAuthoringDocument &Document)
{
    // Paint-order tessellation for gallery cards: layers resolve their Over/boolean
    // composition into contour and primitive shapes, cubic runs flatten, simplify and
    // triangulate once here so frames only replay cached triangles and outlines.
    std::vector<PatternThumbnailShape> Shapes;
    size_t Triangles = 0;
    const auto Pack = [](const std::array<float, 4> &Colour, float Opacity)
    {
        return IM_COL32(int(std::clamp(Colour[0], 0.f, 1.f) * 255), int(std::clamp(Colour[1], 0.f, 1.f) * 255),
            int(std::clamp(Colour[2], 0.f, 1.f) * 255), int(std::clamp(Colour[3] * Opacity, 0.f, 1.f) * 255));
    };
    std::function<void(uint32_t)> Emit = [&](uint32_t Root)
    {
        if (Root >= Document.Instructions.size()) return;
        const auto &Source = Document.Instructions[Root];
        const uint32_t Category = Source.Topology[0];
        if (Category > 6)
        {
            if (Source.Topology[1] < Document.Instructions.size() && Source.Topology[2] < Document.Instructions.size())
            {
                Emit(Source.Topology[1]);
                Emit(Source.Topology[2]);
            }
            return;
        }
        const float Opacity = Source.Transform[2];
        if (!(Opacity > 0) || !std::isfinite(Opacity)) return;
        const float Rotation = Source.Transform[0];
        const float Sine = std::sin(Rotation), Cosine = std::cos(Rotation);
        const auto Place = [&](float LocalX, float LocalY)
        { return std::array<float, 2>{Source.Bounds[0] + Cosine * LocalX - Sine * LocalY, Source.Bounds[1] + Sine * LocalX + Cosine * LocalY}; };
        PatternThumbnailShape Shape;
        Shape.FillColour = Pack(Source.Colour, Opacity);
        Shape.LineColour = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) != 0 ? Pack(Source.StrokeColour, Opacity) : Shape.FillColour;
        Shape.Filled = Source.Paint[3] != 0;
        Shape.Stroked = Source.Transform[1] > 0;
        Shape.StrokeWidth = Source.Transform[1];
        if (Category == 0)
        {
            // 📝 Bounds.zw are full extents; the coverage shader halves them.
            const float HX = Source.Bounds[2] * .5f, HY = Source.Bounds[3] * .5f;
            const std::array<std::array<float, 2>, 4> Corners{Place(-HX, -HY), Place(HX, -HY), Place(HX, HY), Place(-HX, HY)};
            if (Shape.Filled) { Shape.Triangles = {Corners[0], Corners[1], Corners[2], Corners[0], Corners[2], Corners[3]}; Triangles += 2; }
            Shape.Outline = {Corners[0], Corners[1], Corners[2], Corners[3]};
            Shape.ClosedOutline = true;
            Shapes.push_back(std::move(Shape));
            return;
        }
        if (Category == 1)
        {
            std::vector<std::array<float, 2>> Loop;
            for (int Step = 0; Step < 28; ++Step)
            {
                const float Angle = 6.28318530718f * Step / 28;
                Loop.push_back(Place(std::cos(Angle) * Source.Bounds[2] * .5f, std::sin(Angle) * Source.Bounds[3] * .5f));
            }
            if (Shape.Filled)
                for (size_t Corner = 1; Corner + 1 < Loop.size(); ++Corner)
                {
                    Shape.Triangles.push_back(Loop[0]); Shape.Triangles.push_back(Loop[Corner]); Shape.Triangles.push_back(Loop[Corner + 1]);
                }
            Triangles += 26;
            Shape.Outline = std::move(Loop);
            Shape.ClosedOutline = true;
            Shapes.push_back(std::move(Shape));
            return;
        }
        if (Category <= 5)
        {
            uint32_t Count = Category == 2 ? 4 : Category == 3 ? 3 : uint32_t(std::max(Source.Paint[2], 3.f));
            if (Category == 5) Count *= 2;
            std::vector<std::array<float, 2>> Loop;
            for (uint32_t Corner = 0; Corner < Count; ++Corner)
            {
                const float Angle = -1.57079632679f + 6.28318530718f * Corner / Count;
                const float Radius = Category == 5 && Corner % 2 == 1 ? Source.Paint[1] : 1;
                Loop.push_back(Place(std::cos(Angle) * Source.Bounds[2] * .5f * Radius, std::sin(Angle) * Source.Bounds[3] * .5f * Radius));
            }
            if (Shape.Filled) { Shape.Triangles = TriangulateThumbnailLoop(Loop); Triangles += Shape.Triangles.size() / 3; }
            Shape.Outline = std::move(Loop);
            Shape.ClosedOutline = true;
            Shapes.push_back(std::move(Shape));
            return;
        }
        const uint32_t Start = Source.Topology[1], Count = Source.Topology[2];
        if (Count == 0 || uint64_t(Start) + Count > Document.Segments.size()) return;
        const auto CloseEnough = [](const std::array<float, 2> &A, const std::array<float, 2> &B)
        { return std::hypot(A[0] - B[0], A[1] - B[1]) <= 1e-4f; };
        std::vector<std::array<float, 2>> Contour;
        const auto Flush = [&]()
        {
            if (Contour.size() < 2) { Contour.clear(); return; }
            for (auto &Point : Contour) Point = Place(Point[0] * Source.Bounds[2], Point[1] * Source.Bounds[3]);
            SimplifyThumbnailContour(Contour, 2.5f);
            if (Contour.size() < 2) { Contour.clear(); return; }
            PatternThumbnailShape Run = Shape;
            Run.ClosedOutline = std::hypot(Contour.front()[0] - Contour.back()[0], Contour.front()[1] - Contour.back()[1]) <= 1.5f;
            Run.Outline = Contour;
            if (Shape.Filled && Triangles < 250000)
            {
                Run.Triangles = TriangulateThumbnailLoop(Contour);
                Triangles += Run.Triangles.size() / 3;
            }
            Shapes.push_back(std::move(Run));
            Contour.clear();
        };
        for (uint32_t Offset = 0; Offset < Count; ++Offset)
        {
            const auto &Segment = Document.Segments[Start + Offset];
            const std::array<float, 2> A{Segment.StartControl[0], Segment.StartControl[1]};
            const std::array<float, 2> B{Segment.StartControl[2], Segment.StartControl[3]};
            const std::array<float, 2> C{Segment.ControlEnd[0], Segment.ControlEnd[1]};
            const std::array<float, 2> D{Segment.ControlEnd[2], Segment.ControlEnd[3]};
            if (!Contour.empty() && !CloseEnough(Contour.back(), A)) Flush();
            if (Contour.empty()) Contour.push_back(A);
            std::vector<std::array<float, 2>> Flat;
            FlattenThumbnailCubic(A, B, C, D, Flat, 0.002f);
            Contour.insert(Contour.end(), Flat.begin(), Flat.end());
        }
        Flush();
    };
    for (const auto &Layer : Document.Layers)
    {
        if (!Layer.Visible) continue;
        Emit(Layer.Root);
    }
    return Shapes;
}

const VectorAuthoringDocument &PatternWorkspacePanel::QueryDocument() const
{
    if (PenPreview) return *PenPreview;
    return GestureDraft ? *GestureDraft : History.QueryDocument();
}

PlaneExtent PatternWorkspacePanel::QueryControlExtent(const std::string &CaptionText) const
{
    const auto Position = ControlExtents.find(CaptionText);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

PlaneExtent PatternWorkspacePanel::QueryPanelExtent(const std::string &CaptionText) const
{
    const auto Position = PanelExtents.find(CaptionText);
    return Position == PanelExtents.end() ? PlaneExtent{} : Position->second;
}

void PatternWorkspacePanel::CapturePanel(const char *CaptionText)
{
    const auto Position = ImGui::GetWindowPos(), Extent = ImGui::GetWindowSize();
    PanelExtents[CaptionText] = {Position.x, Position.y, Position.x + Extent.x, Position.y + Extent.y};
}

void PatternWorkspacePanel::Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens)
{
    if (PenSource) throw std::runtime_error("Finish or cancel the pending pen path before another source edit.");
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

void PatternWorkspacePanel::EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit)
{
    try
    {
        auto Document = History.QueryDocument();
        Edit(Document);
        Finalize(std::move(Document), Selection);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void PatternWorkspacePanel::TransferSelection(bool Remove)
{
    try
    {
        if (PenSource || Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture and select vector layers before copying.");
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

void PatternWorkspacePanel::InsertSelection(bool InPlace)
{
    try
    {
        if (PenSource || Gesture != 0 || MaskSelection || !SelectionClipboard) throw std::runtime_error("Copy vector layers before pasting; finish any active gesture first.");
        auto Document = History.QueryDocument();
        auto Tokens = IntegrateVectorAuthoringSelection(Document, *SelectionClipboard,
            InPlace ? std::array<float, 2>{0, 0} : std::array<float, 2>{18, 18});
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void PatternWorkspacePanel::ReplicateSelection()
{
    try
    {
        if (PenSource || Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture before duplicating vector layers.");
        auto Document = History.QueryDocument();
        TransformVectorAuthoringSelection(Document, Selection, {});
        const auto Fragment = ExtractVectorAuthoringSelection(Document, Selection);
        auto Tokens = IntegrateVectorAuthoringSelection(Document, Fragment);
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void PatternWorkspacePanel::SelectEditableVectors()
{
    Selection.clear(); MaskSelection = 0; PointToken = 0; SelectedAnchors.clear(); ActiveAnchor = SIZE_MAX;
    for (const auto &Layer : History.QueryDocument().Layers)
        if (QueryVectorAuthoringVisibility(History.QueryDocument(), Layer) && !QueryVectorAuthoringProtection(History.QueryDocument(), Layer))
            Selection.push_back(Layer.Token);
}

void PatternWorkspacePanel::ApplySourcePatch(const VectorAuthoringSourcePatch &Patch)
{
    EvaluateEdit([&](auto &Document) { TransformVectorAuthoringSelection(Document, Selection, Patch); });
}

void PatternWorkspacePanel::RestoreHistory(bool Redo)
{
    if (PenSource) { CancelPen(); return; }
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    size_t AnchorCount = 0;
    try { AnchorCount = DecodeCubicPath(History.QueryDocument(), PointToken).Anchors.size(); } catch (const std::exception &) {}
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
    try
    {
        if (DecodeCubicPath(History.QueryDocument(), PointToken).Anchors.size() != AnchorCount)
        { SelectedAnchors.clear(); ActiveAnchor = SIZE_MAX; }
    }
    catch (const std::exception &) { SelectedAnchors.clear(); ActiveAnchor = SIZE_MAX; }
}

void PatternWorkspacePanel::Select(uint32_t Token, bool Extend)
{
    if (Selection != std::vector<uint32_t>{Token} || Extend) { PointToken = 0; ActiveAnchor = SIZE_MAX; SelectedAnchors.clear(); }
    if (!Extend) Selection.clear();
    const auto Position = std::find(Selection.begin(), Selection.end(), Token);
    if (Extend && Position != Selection.end()) Selection.erase(Position);
    else Selection.push_back(Token);
    MaskSelection = 0;
}

void PatternWorkspacePanel::ConstructPrimitive(uint32_t Category, const char *CaptionText)
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
        if (Document.Layers.empty()) Document.Name = "Custom pattern";
        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, CaptionText == std::string_view(ShapeCaptions[Category]) ? ShapeNames[Category] : CaptionText);
        Finalize(std::move(Document), {Token});
        SearchText[0] = 0; Filter = 0;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void PatternWorkspacePanel::ApplyLibraryPreset(size_t Index)
{
    try
    {
        ApplyPresetDocument(ConstructPatternLibraryPreset(Index));
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void PatternWorkspacePanel::ApplyPresetDocument(VectorAuthoringDocument Document)
{
    if (Document.Layers.empty()) throw std::runtime_error("Pattern preset has no motifs.");
    Finalize(std::move(Document), {});
    SearchText[0] = 0; Filter = 0;
}

bool PatternWorkspacePanel::Save()
{
    if (!SaveSource) return false;
    if (PenSource && !FinishPen(false)) return false;
    try
    {
        if (!SaveSource(EncodeVectorAuthoring(QueryDocument()))) return false;
        SavedDocument = QueryDocument();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool PatternWorkspacePanel::RequestClose()
{
    CancelPen();
    if (GestureSource) { Selection = GestureSelection; GestureSource.reset(); GestureDraft.reset(); Gesture = 0; }
    if (!QueryModified()) return true;
    CloseRequested = true;
    return false;
}

bool PatternWorkspacePanel::ConsumeMaterialApplyRequest() noexcept
{
    const bool Requested = ApplyToMaterialRequested;
    ApplyToMaterialRequested = false;
    return Requested;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     APPROVED HEADER AND LAYER PANEL
//------------------------------------------------------------------------------------------------------------------------

void PatternWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * Scale));
    ImGui::Begin("Pattern header", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 16 * Scale), QueryDocument().Name.c_str(), 10 * Scale, 0xc5c5b8);
    Caption(ImVec2(Origin.x + 22 * Scale + ImGui::CalcTextSize(QueryDocument().Name.c_str()).x, Origin.y + 19 * Scale), ".pattern", 7 * Scale, 0x67675b);
    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230 * Scale, Width - 620 * Scale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = Index == 0 || Index == 1 || Index == 3;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * Scale));
        ImGui::BeginDisabled(!Available || (Index == 0 && !ActivateMaterial) || (Index == 3 && !ActivateStamp));
        if (Controls.Button(Workspaces[Index], Widths[Index] * Scale, Index == 1, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 3 && ActivateStamp) ActivateStamp();
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
    if (HeaderButton("Document actions", -200, "M4 12H5M11 12H12M18 12H19", true)) ImGui::OpenPopup("Pattern document actions");
    if (DropdownControlSurface::BeginMenu("Pattern document actions", Scale))
    {
        if (DropdownControlSurface::Option("New pattern", false, Scale))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before creating a new pattern.";
            else { VectorAuthoringDocument Blank; Blank.Name = "Untitled pattern"; ConfigureDocument(Blank); }
        }
        if (DropdownControlSurface::Option("Open native pattern", false, Scale, bool(LoadSource)))
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
        const bool Idle = !PenSource && Gesture == 0 && !MaskSelection;
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
        ImGui::TextDisabled("Clipboard: %zu vectors (editor session)", SelectionClipboard ? SelectionClipboard->Layers.size() : size_t(0));
        ImGui::TextDisabled("Native authoring uses TOML, not HTML JSON.");
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 168 * Scale, Origin.y + 9 * Scale));
    if (Controls.Button("Apply to material", 116 * Scale)) ApplyToMaterialRequested = true;
    Controls.Tooltip(
        "Apply to material",
        "Snapshot the current pattern onto the selected material preview. "
        "Pattern edits never touch materials until applied.");
    if (HeaderButton("Close pattern studio", -44, "M6 6L18 18M6 18L18 6", bool(ActivateMaterial)))
        if (RequestClose()) ActivateMaterial();
    Controls.ControlHeight = 30;
    ImGui::End();
}

void PatternWorkspacePanel::ConstructLibraryCard(const PatternLibraryFamilyEntry &Family, const PatternPresetCacheEntry &Entry, float Width, bool Selected, size_t Slot)
{
    // One card per family, matching the Alloy .pe-starters card: bordered button,
    // 76px cover-fit tile preview, 10px wrapped name, description tooltip.
    if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 10 * 1.302f * Scale);
    const float LineHeight = ImGui::GetTextLineHeight();
    const float ThumbHeight = 76 * Scale;
    const float NameHeight = LineHeight * 3 + 18 * Scale;
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Size(Width, ThumbHeight + NameHeight);
    const ImVec2 Far(Origin.x + Size.x, Origin.y + Size.y);
    ImGui::InvisibleButton(("##LibraryCard-" + Family.Name).c_str(), Size);
    const bool Clicked = ImGui::IsItemClicked(), Hovered = ImGui::IsItemHovered();
    ControlExtents[Family.Name] = {Origin.x, Origin.y, Far.x, Far.y};
    const char *Description = Family.Description.empty() ? Family.Name.c_str() : Family.Description.c_str();
    Controls.Tooltip(Family.Name.c_str(), Description);
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Origin, Far, Selected ? IM_COL32(53, 66, 55, 255) : Hovered ? IM_COL32(52, 61, 56, 255) : IM_COL32(35, 40, 41, 255), 7 * Scale);
    const ImVec2 ThumbFar(Far.x, Origin.y + ThumbHeight);
    ImGui::PushClipRect(Origin, ThumbFar, true);
    Draw->AddRectFilled(Origin, ThumbFar, IM_COL32(20, 23, 24, 255));
    PatternAtlasImage Atlas{};
    if (QueryPatternAtlas) Atlas = QueryPatternAtlas();
    if (Atlas.Texture != 0 && !Entry.Failed && !Entry.Shapes.empty() && Slot < PatternAtlasCapacity)
    {
        // Atlas fast path: one textured quad replaces tens of thousands of
        // replayed triangles. The host returns the slot-0 rectangle; each
        // card offsets it by its stable family slot, then center-crops the
        // square tile to the card rectangle like the vector projection below.
        const float Step = float(PatternAtlasTile + 2) / PatternAtlasExtent;
        const float Span = float(PatternAtlasTile) / PatternAtlasExtent;
        float U0 = Atlas.MinimumU + (Slot % PatternAtlasColumns) * Step;
        float V0 = Atlas.MinimumV + (Slot / PatternAtlasColumns) * Step;
        float U1 = U0 + Span, V1 = V0 + Span;
        const float Aspect = Size.x / ThumbHeight;
        if (Aspect > 1) { const float Crop = (1 - 1 / Aspect) * .5f * (V1 - V0); V0 += Crop; V1 -= Crop; }
        else { const float Crop = (1 - Aspect) * .5f * (U1 - U0); U0 += Crop; U1 -= Crop; }
        Draw->AddImage(ImTextureRef(Atlas.Texture), Origin, ThumbFar, ImVec2(U0, V0), ImVec2(U1, V1));
    }
    else if (!Entry.Failed && !Entry.Shapes.empty())
    {
        const float Unit = std::max(Size.x / 512, ThumbHeight / 512);
        const auto Project = [&](float X, float Y)
        { return ImVec2(Origin.x + Size.x * .5f + (X - 256) * Unit, Origin.y + ThumbHeight * .5f + (Y - 256) * Unit); };
        std::vector<ImVec2> Polyline;
        for (const auto &Shape : Entry.Shapes)
        {
            // Batched triangle submission: dense weave cards replay tens of
            // thousands of cached triangles per frame, so triangles reserve one
            // index run and skip fully off-thumb copies instead of issuing one
            // draw-list call per triangle.
            if (Shape.Filled && Shape.Triangles.size() >= 3)
            {
                const size_t Count = Shape.Triangles.size() / 3;
                Draw->PrimReserve(int(Count * 3), int(Count * 3));
                ImDrawVert *Vertex = Draw->_VtxWritePtr;
                ImDrawIdx *Index = Draw->_IdxWritePtr;
                ImDrawIdx Base = Draw->_VtxCurrentIdx;
                for (size_t Triangle = 0; Triangle < Count; ++Triangle)
                {
                    const ImVec2 A = Project(Shape.Triangles[Triangle * 3][0], Shape.Triangles[Triangle * 3][1]);
                    const ImVec2 B = Project(Shape.Triangles[Triangle * 3 + 1][0], Shape.Triangles[Triangle * 3 + 1][1]);
                    const ImVec2 C = Project(Shape.Triangles[Triangle * 3 + 2][0], Shape.Triangles[Triangle * 3 + 2][1]);
                    if ((A.x < Origin.x && B.x < Origin.x && C.x < Origin.x) || (A.x > Far.x && B.x > Far.x && C.x > Far.x) ||
                        (A.y < Origin.y && B.y < Origin.y && C.y < Origin.y) || (A.y > ThumbFar.y && B.y > ThumbFar.y && C.y > ThumbFar.y))
                        continue;
                    for (const ImVec2 *Point : {&A, &B, &C})
                    {
                        Vertex->pos = *Point;
                        Vertex->uv = Draw->_Data->TexUvWhitePixel;
                        Vertex->col = Shape.FillColour;
                        ++Vertex;
                        *Index++ = Base++;
                    }
                }
                // PrimReserve over-reserves for culled triangles; rewind the
                // pointers and shrink the buffers back to the written vertices.
                const int Unwritten = Draw->_VtxWritePtr + Count * 3 - Vertex;
                Draw->_VtxWritePtr = Vertex;
                Draw->_IdxWritePtr = Index;
                Draw->_VtxCurrentIdx = Base;
                Draw->VtxBuffer.resize(Draw->VtxBuffer.Size - Unwritten);
                Draw->IdxBuffer.resize(Draw->IdxBuffer.Size - Unwritten);
            }
            if (Shape.Stroked && Shape.Outline.size() > 1)
            {
                Polyline.clear();
                for (const auto &Point : Shape.Outline) Polyline.push_back(Project(Point[0], Point[1]));
                Draw->AddPolyline(Polyline.data(), int(Polyline.size()), Shape.LineColour,
                    Shape.ClosedOutline ? ImDrawFlags_Closed : 0, std::max(Shape.StrokeWidth * Unit, 1.f));
            }
        }
    }
    else
    {
        const char *Empty = Entry.Failed ? "Unreadable preset" : "Empty preset";
        Draw->AddText(ImVec2(Origin.x + (Size.x - ImGui::CalcTextSize(Empty).x) * .5f, Origin.y + (ThumbHeight - LineHeight) * .5f),
            IM_COL32(140, 145, 155, 255), Empty);
    }
    ImGui::PopClipRect();
    ImGui::PushClipRect(ImVec2(Origin.x, ThumbFar.y), Far, true);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 9 * Scale, ThumbFar.y + 9 * Scale));
    ImGui::PushTextWrapPos(Far.x - 9 * Scale);
    ImGui::TextUnformatted(Family.Name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopClipRect();
    Draw->AddRect(Origin, Far, Selected ? IM_COL32(179, 200, 166, 255) : Hovered ? IM_COL32(109, 129, 112, 255) : IM_COL32(54, 61, 61, 255),
        7 * Scale, 0, Selected || Hovered ? 2.f : 1.f);
    if (RegularTypeface) ImGui::PopFont();
    // 📝 The wrapped name leaves the cursor mid-card, which collapsed the
    // gallery rows into a diagonal cascade; restore the documented card box
    // so the grid caller can place the next column and row absolutely.
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Far.y));
    if (Clicked && !Entry.Failed)
    {
        try
        {
            ApplyPresetDocument(Entry.Document);
            LibrarySelected = Family.Name;
        }
        catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
    }
}

void PatternWorkspacePanel::ConstructLibrary()
{
    // Pattern library gallery, ported 1:1 from the Alloy Studio library panel:
    // kicker, collection shortcuts, collection filter, search, print palette,
    // result count, two-column starter cards (24 per page), paging, notes.
    CapturePanel("Library");
    if (ImGui::GetContentRegionAvail().y < 35 * Scale) return;
    if (LibraryFamilies.empty())
    {
        for (size_t Index = 0; Index < QueryPatternLibraryPresetCount(); ++Index)
        {
            const char *Caption = QueryPatternLibraryPresetName(Index);
            if (Controls.Button(Caption, ImGui::GetContentRegionAvail().x - 16 * Scale)) ApplyLibraryPreset(Index);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[Caption] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        }
        return;
    }
    const float ContentWidth = ImGui::GetContentRegionAvail().x - 16 * Scale;
    const auto SmallFont = [&](float Pixels)
    {
        if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), Pixels * 1.302f * Scale);
    };
    const auto PopSmallFont = [&]() { if (RegularTypeface) ImGui::PopFont(); };
    ImGui::SetCursorPosX(16 * Scale);
    SmallFont(9);
    ImGui::TextColored(ImVec4(155 / 255.f, 170 / 255.f, 158 / 255.f, 1), "START WITH A STRUCTURE");
    PopSmallFont();
    const auto GalleryButton = [&](const char *Caption, float Width)
    {
        SmallFont(11);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(35 / 255.f, 40 / 255.f, 41 / 255.f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(52 / 255.f, 61 / 255.f, 56 / 255.f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(52 / 255.f, 61 / 255.f, 56 / 255.f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(54 / 255.f, 61 / 255.f, 61 / 255.f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(215 / 255.f, 223 / 255.f, 216 / 255.f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * Scale, 9 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, .5f));
        ImGui::SetCursorPosX(16 * Scale);
        const bool Pressed = ImGui::Button(Caption, ImVec2(Width, 0));
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(5);
        PopSmallFont();
        return Pressed;
    };
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8 * Scale);
    // The packaged DM Sans face carries no U+2197 glyph, so the web shortcut
    // arrows render as a plain ASCII marker with identical behaviour.
    if (GalleryButton("Geometric constructions >", ContentWidth))
        ConfigureLibraryFilter("Geometric constructions", LibraryDefaultPalette, true);
    if (GalleryButton("Stitches \xc2\xb7 one thread color >", ContentWidth))
        ConfigureLibraryFilter("Stitch patterns", "", true);
    SmallFont(11);
    ImGui::SetCursorPosX(16 * Scale);
    ImGui::SetNextItemWidth(ContentWidth);
    if (DropdownControlSurface::BeginSelection("##PatternCollection", LibraryGroup.c_str(), Scale))
    {
        if (DropdownControlSurface::Option("All patterns", LibraryGroup == "All patterns", Scale))
        {
            ConfigureLibraryFilter("All patterns", "", false);
            ImGui::CloseCurrentPopup();
        }
        for (const auto &Group : LibraryGroups)
            if (DropdownControlSurface::Option(Group.c_str(), LibraryGroup == Group, Scale))
            {
                ConfigureLibraryFilter(Group, "", false);
                ImGui::CloseCurrentPopup();
            }
        DropdownControlSurface::EndSelection();
    }
    ImGui::SetCursorPosX(16 * Scale);
    ImGui::SetNextItemWidth(ContentWidth);
    if (ImGui::InputTextWithHint("##LibrarySearch", "Search weaves, stitches, checks\xe2\x80\xa6", LibrarySearchText.data(), LibrarySearchText.size()))
        LibraryPage = 0;
    PopSmallFont();
    const bool ConcealsPalette = LibraryGroup == "Fabric weaves" || LibraryGroup == "Stitch patterns" ||
        LibraryGroup == "Islamic geometry" || LibraryGroup == "African traditional";
    const std::string Palette = LibraryPalette.empty() ? LibraryDefaultPalette : LibraryPalette;
    if (!ConcealsPalette)
    {
        SmallFont(10);
        ImGui::SetCursorPosX(16 * Scale);
        ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1), "Print palette \xc2\xb7 not a separate pattern");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color option for printed basic families only");
        PopSmallFont();
        SmallFont(11);
        ImGui::SetCursorPosX(16 * Scale);
        ImGui::SetNextItemWidth(ContentWidth);
        const std::vector<std::string> Options = LibraryPalettes.empty() ? std::vector<std::string>{LibraryDefaultPalette} : LibraryPalettes;
        if (DropdownControlSurface::BeginSelection("##LibraryPalette", Palette.c_str(), Scale))
        {
            for (const auto &Option : Options)
                if (DropdownControlSurface::Option(Option.c_str(), Palette == Option, Scale))
                {
                    ConfigureLibraryFilter(LibraryGroup, Option, false);
                    ImGui::CloseCurrentPopup();
                }
            DropdownControlSurface::EndSelection();
        }
        PopSmallFont();
    }
    if (LibraryGroup == "Islamic geometry" || LibraryGroup == "African traditional")
    {
        SmallFont(10);
        ImGui::SetCursorPosX(16 * Scale);
        ImGui::PushTextWrapPos(16 * Scale + ContentWidth);
        ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1),
            "Original vector studies inspired by named traditions, not historical reproductions. Edit colours in Design palette.");
        ImGui::PopTextWrapPos();
        PopSmallFont();
    }
    RefreshPatternAtlas();
    const auto Matches = QueryLibraryMatches();
    SmallFont(10);
    ImGui::SetCursorPosX(16 * Scale);
    ImGui::TextColored(ImVec4(178 / 255.f, 199 / 255.f, 167 / 255.f, 1), "%zu results \xc2\xb7 one card per family", Matches.size());
    PopSmallFont();
    const size_t Pages = std::max<size_t>(1, (Matches.size() + 23) / 24);
    const size_t Page = std::min(LibraryPage, Pages - 1);
    const float Gap = 9 * Scale;
    const float CardWidth = (ContentWidth - Gap) * .5f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (15 - 4) * Scale);
    float RowTop = ImGui::GetCursorPosY();
    for (size_t Slot = Page * 24; Slot < std::min(Page * 24 + 24, Matches.size()); ++Slot)
    {
        // 📝 Cards restore the cursor to their own box bottom, so the second
        // column pins to the captured row top instead of SameLine, which
        // would inherit the sibling's bottom edge and shear the grid.
        const bool FirstColumn = (Slot - Page * 24) % 2 == 0;
        if (FirstColumn) { RowTop = ImGui::GetCursorPosY(); ImGui::SetCursorPosX(16 * Scale); }
        else ImGui::SetCursorPos(ImVec2(16 * Scale + CardWidth + Gap, RowTop));
        const auto &Family = LibraryFamilies[Matches[Slot]];
        const auto &Entry = QueryPresetEntry(Family);
        ConstructLibraryCard(Family, Entry, CardWidth, LibrarySelected == Family.Name, Matches[Slot]);
        if (!FirstColumn) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Gap - 4 * Scale);
    }
    if (Matches.empty())
    {
        SmallFont(10);
        ImGui::SetCursorPosX(16 * Scale);
        ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1), "No matches. Try another collection or search.");
        PopSmallFont();
    }
    SmallFont(10);
    SmallFont(11);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * Scale, 5 * Scale));
    const float ArrowWidth = ImGui::CalcTextSize(">").x + 24 * Scale;
    ImGui::PopStyleVar();
    PopSmallFont();
    ImGui::SetCursorPosX(16 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * Scale, 5 * Scale));
    ImGui::BeginDisabled(Page == 0);
    SmallFont(11);
    const bool Previous = ImGui::Button("<", ImVec2(ArrowWidth, 0));
    PopSmallFont();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (Previous && Page > 0) LibraryPage = Page - 1;
    ImGui::SameLine();
    const std::string PageCaption = "Page " + std::to_string(Page + 1) + " / " + std::to_string(Pages);
    const float PageWidth = ImGui::CalcTextSize(PageCaption.c_str()).x;
    ImGui::SetCursorPosX(16 * Scale + (ContentWidth - PageWidth) * .5f);
    ImGui::TextColored(ImVec4(170 / 255.f, 183 / 255.f, 176 / 255.f, 1), "%s", PageCaption.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(16 * Scale + ContentWidth - ArrowWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * Scale, 5 * Scale));
    ImGui::BeginDisabled(Page + 1 >= Pages);
    SmallFont(11);
    const bool Next = ImGui::Button(">", ImVec2(ArrowWidth, 0));
    PopSmallFont();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (Next) LibraryPage = Page + 1;
    ImGui::SetCursorPosX(16 * Scale);
    ImGui::PushTextWrapPos(16 * Scale + ContentWidth);
    ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1),
        "Stitches and weaves use one thread/yarn color. Backgrounds, colors, direction and draft variants do not add library cards. The rejected rug collections have been removed.");
    ImGui::PopTextWrapPos();
    PopSmallFont();
}

void PatternWorkspacePanel::ConstructLayers()
{
    CapturePanel("Layers");
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 15 * Scale), "Motifs", 23 * Scale, 0xd9d9cc, LightTypeface);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * Scale, Origin.y + 14 * Scale));
    if (Controls.Button("Add pattern layer", 28 * Scale, false, PlusPath)) ImGui::OpenPopup("Add a pattern motif");
    if (DropdownControlSurface::BeginMenu("Add a pattern motif", Scale))
    {
        ImGui::TextDisabled("Shapes");
        for (uint32_t Category = 0; Category < 6; ++Category)
        {
            if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { ConstructPrimitive(Category, ShapeCaptions[Category]); ImGui::CloseCurrentPopup(); }
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        }
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * Scale, Origin.y + 64 * Scale));
    ImGui::BeginChild("Pattern layers body", ImVec2(Width - 16 * Scale, 0), ImGuiChildFlags_None);
    Controls.ControlHeight = 48;
    if (Controls.Button("Groups & folders", ImGui::GetContentRegionAvail().x, GroupsOpen, {}, true)) GroupsOpen = !GroupsOpen;
    Controls.ControlHeight = 30;
    if (GroupsOpen)
    {
        ImGui::BeginDisabled(Selection.empty());
        if (Controls.Button("Group selection")) EvaluateEdit([&](auto &Document)
        {
            if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
            const auto Token = ++Document.TokenClock;
            Document.Enclosures.push_back({Token, 0, "Group"});
            for (auto &Layer : Document.Layers) if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() && !QueryVectorAuthoringProtection(Document, Layer)) Layer.EnclosureToken = Token;
        });
        ImGui::EndDisabled();
        const auto Snapshot = QueryDocument();
        for (const auto &Entry : Snapshot.Enclosures)
        {
            ImGui::PushID(int(Entry.Token));
            ImGui::TextUnformatted(Entry.Name.c_str());
            bool Enabled = Entry.GroupWideEditingEnabled;
            ImGui::BeginDisabled(Entry.FolderEnabled || Entry.LockEnabled);
            if (Controls.Toggle("Group-wide properties", Enabled)) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Enclosures) if (Candidate.Token == Entry.Token) Candidate.GroupWideEditingEnabled = Enabled; });
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    Controls.ControlHeight = 48;
    ImGui::BeginDisabled();
    Controls.Button("Stored groups · 0", ImGui::GetContentRegionAvail().x, false, {}, true);
    Controls.Tooltip("Stored groups", "Native stored definitions and linked-instance editing are not ported yet.");
    ImGui::EndDisabled();
    Controls.ControlHeight = 30;
    const auto Snapshot = QueryDocument();
    size_t Visible = 0, Locked = 0;
    for (const auto &Layer : Snapshot.Layers) { if (Layer.Visible) ++Visible; if (Layer.Locked) ++Locked; }
    const size_t Counts[]{Visible, Snapshot.Layers.size() - Visible, Locked};
    const char *Captions[]{"Visible", "Hidden", "Locked"};
    const float StatWidth = (ImGui::GetContentRegionAvail().x - 12 * Scale) / 3;
    for (int Index = 0; Index < 3; ++Index)
    {
        if (Index) ImGui::SameLine(0, 6 * Scale);
        char Label[32]; std::snprintf(Label, sizeof(Label), "%02zu\n%s", Counts[Index], Captions[Index]);
        Controls.ControlHeight = 64;
        if (Controls.Button(Label, StatWidth, Filter == Index + 1, {}, true)) Filter = Filter == Index + 1 ? 0 : Index + 1;
    }
    Controls.ControlHeight = 30;
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##FindLayer", "Find a layer...", SearchText.data(), SearchText.size());
    ImGui::Dummy(ImVec2(0, 8 * Scale));
    ImGui::TextDisabled("Top layers render in front · drag to reorder");
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
        Caption(ImVec2(RowOrigin.x + 36 * Scale, RowOrigin.y + 32 * Scale), Source.Topology[0] < 6 ? ShapeCaptions[Source.Topology[0]] : "Retained vector", 7 * Scale, 0x93937c);
        Ink->PopClipRect();
        // 📝 Row preview: the layer's own rendered shape, cropped to its own tight bounds — the
        // ported Texture-workspace layer-row thumbnail, and the same own-bounds contract the
        // selection hitbox fix relies on, instead of the old static swatch outline. The GPU atlas
        // is the fast path once baked; otherwise fall back to a CPU vector replay of just this one
        // layer, and only fall back further to a bare outline if that also finds nothing to draw.
        {
            // 📝 Square preview (24x24, vertically centred in the 56-tall row) — the box was
            // previously 24 wide by 36 tall, a non-square crop window that distorted every row
            // thumbnail's aspect regardless of what atlas content filled it.
            const ImVec2 ThumbMin(RowOrigin.x + 8 * Scale, RowOrigin.y + 16 * Scale), ThumbMax(RowOrigin.x + 32 * Scale, RowOrigin.y + 40 * Scale);
            PatternAtlasImage LayerAtlas{};
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
            {
                try
                {
                    VectorAuthoringDocument OneLayer = Snapshot;
                    OneLayer.Layers = {Layer};
                    const auto Shapes = TessellatePresetDocument(OneLayer);
                    if (!Shapes.empty())
                    {
                        const std::array<uint32_t, 1> OneToken{Layer.Token};
                        const auto Extent = QueryVectorSelectionExtent(Snapshot, OneToken);
                        const float Span = std::max({Extent.Bounds[2], Extent.Bounds[3], 1.f}) * 1.25f;
                        const float Unit = (ThumbMax.x - ThumbMin.x) / Span;
                        const ImVec2 Centre((ThumbMin.x + ThumbMax.x) * .5f, (ThumbMin.y + ThumbMax.y) * .5f);
                        const auto Project = [&](float X, float Y)
                        { return ImVec2(Centre.x + (X - Extent.Bounds[0]) * Unit, Centre.y + (Y - Extent.Bounds[1]) * Unit); };
                        Ink->AddRectFilled(ThumbMin, ThumbMax, ImGui::GetColorU32(Spectrum(0x161614)), 4 * Scale);
                        Ink->PushClipRect(ThumbMin, ThumbMax, true);
                        for (const auto &Shape : Shapes)
                        {
                            if (Shape.Filled && Shape.Triangles.size() >= 3)
                                for (size_t Triangle = 0; Triangle < Shape.Triangles.size() / 3; ++Triangle)
                                    Ink->AddTriangleFilled(Project(Shape.Triangles[Triangle * 3][0], Shape.Triangles[Triangle * 3][1]),
                                        Project(Shape.Triangles[Triangle * 3 + 1][0], Shape.Triangles[Triangle * 3 + 1][1]),
                                        Project(Shape.Triangles[Triangle * 3 + 2][0], Shape.Triangles[Triangle * 3 + 2][1]), Shape.FillColour);
                            if (Shape.Stroked && Shape.Outline.size() > 1)
                            {
                                std::vector<ImVec2> Polyline;
                                for (const auto &Point : Shape.Outline) Polyline.push_back(Project(Point[0], Point[1]));
                                Ink->AddPolyline(Polyline.data(), int(Polyline.size()), Shape.LineColour,
                                    Shape.ClosedOutline ? ImDrawFlags_Closed : 0, std::max(Shape.StrokeWidth * Unit, 1.f));
                            }
                        }
                        Ink->PopClipRect();
                        ThumbnailDrawn = true;
                    }
                }
                catch (const std::exception &) { ThumbnailDrawn = false; }
            }
            if (!ThumbnailDrawn)
                Ink->AddRect(ImVec2(ThumbMin.x + 4 * Scale, ThumbMin.y + 11 * Scale), ImVec2(ThumbMin.x + 18 * Scale, ThumbMin.y + 25 * Scale),
                    ImGui::GetColorU32(Spectrum(0xc7d9b8)), 2 * Scale);
        }
        if (!Layer.Locked && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("FrontierVectorAuthoringLayer", &Layer.Token, sizeof(Layer.Token));
            ImGui::TextUnformatted(Layer.Name.c_str());
            ImGui::EndDragDropSource();
        }
        if (!Layer.Locked && ImGui::BeginDragDropTarget())
        {
            if (const auto *Payload = ImGui::AcceptDragDropPayload("FrontierVectorAuthoringLayer"); Payload && Payload->IsDelivery())
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
    if (Snapshot.Layers.empty()) ImGui::TextWrapped("Draw your first shape to start a pattern.");
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU CANVAS, VIEW NAVIGATION AND GESTURES
//------------------------------------------------------------------------------------------------------------------------

void PatternWorkspacePanel::CancelPen()
{
    PenSource.reset(); PenPreview.reset(); PendingPath = {}; PenSelection.clear(); ++GestureRevision;
    PenToken = 0; PenAnchorFloor = 0; PenReversed = false; IndependentPenContinuation = false;
    Diagnostic.clear();
    if (Gesture == 8) Gesture = 0;
}

void PatternWorkspacePanel::ResumePen(uint32_t Token, bool FromStart)
{
    const auto &Source = History.QueryDocument();
    const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Source.Layers.end() || MaskSelection || QueryVectorAuthoringProtection(Source, *Position)
        || !QueryVectorAuthoringVisibility(Source, *Position)) throw std::runtime_error("Select one visible, editable open cubic source to continue.");
    auto Path = DecodeCubicPath(Source, Token);
    if (Path.Closed || Path.Anchors.size() >= 400) throw std::runtime_error("Continue an open path below its 400-anchor budget.");
    if (FromStart) ReverseCubicPath(Path);
    auto &Endpoint = Path.Anchors.back();
    for (size_t Axis = 0; Axis < 2; ++Axis) Endpoint.Outgoing[Axis] = 2 * Endpoint.Position[Axis] - Endpoint.Incoming[Axis];
    CancelPen();
    PenSource = Source;
    PenSelection = Selection;
    PendingPath = std::move(Path);
    PenToken = Token; PenAnchorFloor = PendingPath.Anchors.size(); PenReversed = FromStart;
    Tool = VectorAuthoringTool::Pen;
}

void PatternWorkspacePanel::RefreshPenPreview()
{
    PenPreview.reset();
    ++GestureRevision;
    if (!PenSource || PendingPath.Anchors.size() < 2) return;
    if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !PendingPath.Closed) return;
    auto Draft = *PenSource;
    if (PenToken)
    {
        auto Path = PendingPath;
        if (PenReversed) ReverseCubicPath(Path);
        ReplaceCubicPath(Draft, PenToken, Path);
        PenPreview = std::move(Draft);
        return;
    }
    VectorCoverageInstruction Style;
    Style.Transform[1] = 1.4f;
    Style.Paint[3] = PendingPath.Closed ? 1.f : 0.f;
    auto Path = PendingPath;
    if (AutomaticPen)
    {
        std::vector<size_t> Indices;
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
        ConfigureAutomaticCubicTangents(Path, Indices);
    }
    ConstructCubicPath(Draft, Path, Style, "Pen contour");
    PenPreview = std::move(Draft);
}

bool PatternWorkspacePanel::FinishPen(bool Closed)
{
    if (!PenSource) return true;
    try
    {
        if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !Closed) { CancelPen(); return true; }
        auto Path = PendingPath; Path.Closed = Closed;
        if (AutomaticPen && !PenToken)
        {
            std::vector<size_t> Indices;
            for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
            ConfigureAutomaticCubicTangents(Path, Indices);
        }
        if (PenReversed) ReverseCubicPath(Path);
        auto Draft = *PenSource;
        VectorCoverageInstruction Style;
        Style.Transform[1] = 1.4f;
        uint32_t Token = PenToken;
        if (Token) ReplaceCubicPath(Draft, Token, Path);
        else Token = ConstructCubicPath(Draft, Path, Style, "Pen contour");
        Selection = PenSelection;
        CancelPen();
        Finalize(std::move(Draft), {Token});
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool PatternWorkspacePanel::ConstructPenInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if (Tool != VectorAuthoringTool::Pen && !PenSource) return false;
    const auto &Input = ImGui::GetIO();
    if (PenSource && Tool != VectorAuthoringTool::Pen)
    {
        if (!FinishPen(false)) Tool = VectorAuthoringTool::Pen;
        return true;
    }
    if (PenSource && (Input.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape)))
    {
        CancelPen(); return true;
    }
    if (PenSource && !Input.WantTextInput)
    {
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { Save(); return true; }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { CancelPen(); return true; }
        if (Gesture == 0 && !Input.KeyCtrl)
        {
            std::optional<VectorAuthoringTool> Requested;
            if (ImGui::IsKeyPressed(ImGuiKey_V)) Requested = VectorAuthoringTool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_A)) Requested = VectorAuthoringTool::Points;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) Requested = VectorAuthoringTool::Rectangle;
            if (ImGui::IsKeyPressed(ImGuiKey_O)) Requested = VectorAuthoringTool::Ellipse;
            if (Requested) { if (FinishPen(false)) Tool = *Requested; return true; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) { FinishPen(false); return true; }
        if (Gesture == 0 && ImGui::IsKeyPressed(ImGuiKey_Backspace))
        {
            if (PenToken && PendingPath.Anchors.size() <= PenAnchorFloor) { CancelPen(); return true; }
            PendingPath.Anchors.pop_back();
            if (PendingPath.Anchors.empty()) CancelPen();
            else RefreshPenPreview();
            return true;
        }
    }
    if (MaskSelection) return false;
    const auto DocumentPoint = [&]()
    {
        std::array<float, 2> Coordinate{(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
        if (SnappingEnabled) for (auto &Position : Coordinate) Position = std::round(Position / 16) * 16;
        return Coordinate;
    };
    const auto Project = [&](std::array<float, 2> Coordinate)
    {
        return ImVec2(Board.MinimumX + Coordinate[0] / 512 * Board.Width(), Board.MinimumY + Coordinate[1] / 512 * Board.Height());
    };
    uint32_t ResumeToken = 0;
    bool ResumeStart = false;
    if (!PenSource && Selection.size() == 1)
    {
        const auto &Source = History.QueryDocument();
        const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
        if (Position != Source.Layers.end() && !QueryVectorAuthoringProtection(Source, *Position)
            && QueryVectorAuthoringVisibility(Source, *Position) && Source.Instructions[Position->Root].Topology[0] == 6)
        {
            try
            {
                const auto Path = DecodeCubicPath(Source, Position->Token);
                if (!Path.Closed && Path.Anchors.size() < 400)
                {
                    auto *Ink = ImGui::GetWindowDrawList();
                    Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
                    for (bool FromStart : {true, false})
                    {
                        const auto Endpoint = Project(FromStart ? Path.Anchors.front().Position : Path.Anchors.back().Position);
                        const float Radius = 7 * Scale;
                        ControlExtents[FromStart ? "Resume start endpoint" : "Resume end endpoint"] = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                        Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                        if (std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y) <= Radius)
                        {
                            ResumeToken = Position->Token; ResumeStart = FromStart;
                        }
                    }
                    Ink->PopClipRect();
                }
            }
            catch (const std::exception &) {}
        }
    }
    uint32_t JoinToken = 0;
    bool JoinEnd = false;
    if (PenSource && PenToken && Gesture == 0)
    {
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        float Nearest = 7 * Scale;
        for (auto Layer = PenSource->Layers.rbegin(); Layer != PenSource->Layers.rend(); ++Layer)
        {
            if (Layer->Token == PenToken || !QueryVectorAuthoringVisibility(*PenSource, *Layer)
                || PenSource->Instructions[Layer->Root].Topology[0] != 6) continue;
            CubicPath Path;
            try { Path = DecodeCubicPath(*PenSource, Layer->Token); }
            catch (const std::exception &) { continue; } // Compound contours remain visible but are not endpoint targets.
            if (Path.Closed) continue;
            for (bool End : {false, true})
            {
                const auto Endpoint = Project(End ? Path.Anchors.back().Position : Path.Anchors.front().Position);
                const float Radius = 7 * Scale;
                ControlExtents[std::string(End ? "Join end endpoint:" : "Join start endpoint:") + std::to_string(Layer->Token)]
                    = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                const float Distance = std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y);
                if (Distance < Nearest)
                {
                    Nearest = Distance; JoinToken = Layer->Token; JoinEnd = End;
                }
            }
        }
        Ink->PopClipRect();
    }
    bool Consumed = Gesture == 8;
    try
    {
        if (Gesture == 0 && Hovered && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            Consumed = true;
            if (!PenSource && ResumeToken && !Input.KeyCtrl)
            {
                ResumePen(ResumeToken, ResumeStart);
                IndependentPenContinuation = false;
                PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                    (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
                Gesture = 8;
                return true;
            }
            if (PenSource && !PendingPath.Anchors.empty())
            {
                const auto Last = Project(PendingPath.Anchors.back().Position);
                if (Input.KeyAlt && std::hypot(Input.MousePos.x - Last.x, Input.MousePos.y - Last.y) <= 7 * Scale)
                {
                    if (AutomaticPen && !PenToken) throw std::runtime_error("Disable automatic Pen curves before breaking an outgoing tangent.");
                    // 📝 Keep the incoming curve; only the following segment receives this corner or independent drag.
                    PendingPath.Anchors.back().Outgoing = PendingPath.Anchors.back().Position;
                    IndependentPenContinuation = true;
                    PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                        (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
                    Gesture = 8;
                    RefreshPenPreview();
                    return true;
                }
                const auto First = Project(PendingPath.Anchors.front().Position);
                if (std::hypot(Input.MousePos.x - First.x, Input.MousePos.y - First.y) <= 7 * Scale)
                {
                    if (PendingPath.Anchors.size() < 3) throw std::runtime_error("A closed path needs at least three anchors.");
                    FinishPen(true); return true;
                }
            }
            if (JoinToken && !Input.KeyCtrl)
            {
                // 📝 Build and validate the entire continuation/join before consuming either retained source.
                auto Draft = *PenSource;
                auto Path = PendingPath;
                if (PenReversed) ReverseCubicPath(Path);
                ReplaceCubicPath(Draft, PenToken, Path);
                const auto Token = IntegrateCubicPaths(Draft, PenToken, JoinToken, PenReversed, JoinEnd);
                Selection = PenSelection;
                CancelPen();
                Finalize(std::move(Draft), {Token});
                return true;
            }
            if (PendingPath.Anchors.size() >= 400) throw std::runtime_error("Pen paths support at most 400 anchors.");
            if (!PenSource)
            {
                PenSource = History.QueryDocument(); PenSelection = Selection;
                PendingPath = {};
            }
            auto Position = DocumentPoint();
            if (Input.KeyShift && !PendingPath.Anchors.empty()) Position = ConstrainCubicDirection(PendingPath.Anchors.back().Position, Position);
            IndependentPenContinuation = false;
            PendingPath.Anchors.push_back({Position, Position, Position});
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
            Gesture = 8;
            RefreshPenPreview();
        }
        if (Gesture == 8 && PenSource && ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMousePosValid())
        {
            // 📝 Move only the newly placed anchor, never the retained endpoint being resumed.
            if (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor)
            {
                auto &Anchor = PendingPath.Anchors.back();
                const std::array<float, 2> Displacement{Input.MouseDelta.x / Board.Width() * 512,
                    Input.MouseDelta.y / Board.Height() * 512};
                for (auto *Point : {&Anchor.Position, &Anchor.Incoming, &Anchor.Outgoing})
                    for (size_t Axis = 0; Axis < 2; ++Axis) (*Point)[Axis] += Displacement[Axis];
                PointerOrigin.X += Displacement[0]; PointerOrigin.Y += Displacement[1];
                RefreshPenPreview();
            }
        }
        else
        if (Gesture == 8 && PenSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
        {
            auto &Anchor = PendingPath.Anchors.back();
            auto Position = DocumentPoint();
            if (Input.KeyShift) Position = ConstrainCubicDirection(Anchor.Position, Position);
            Anchor.Outgoing = Position;
            // 📝 The resumed endpoint's incoming control belongs to an existing segment; never mirror over it.
            if (!IndependentPenContinuation && (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor))
                for (size_t Axis = 0; Axis < 2; ++Axis)
                    Anchor.Incoming[Axis] = Input.KeyAlt ? Anchor.Position[Axis] : 2 * Anchor.Position[Axis] - Position[Axis];
            RefreshPenPreview();
        }
        if (PenSource)
        {
            auto *Ink = ImGui::GetWindowDrawList();
            Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
            auto Guides = PendingPath;
            if (AutomaticPen && !PenToken && Guides.Anchors.size() >= 2)
            {
                std::vector<size_t> Indices;
                for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index) Indices.push_back(Index);
                ConfigureAutomaticCubicTangents(Guides, Indices);
            }
            for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index)
            {
                const auto &Anchor = Guides.Anchors[Index];
                const auto Position = Project(Anchor.Position);
                ControlExtents["Pen anchor " + std::to_string(Index)] = {Position.x - 6 * Scale, Position.y - 6 * Scale, Position.x + 6 * Scale, Position.y + 6 * Scale};
                Ink->AddCircle(Position, 4 * Scale, IM_COL32(204, 230, 181, 255));
                if (Index + 1 == PendingPath.Anchors.size())
                {
                    Ink->AddLine(Project(Anchor.Incoming), Project(Anchor.Outgoing), IM_COL32(170, 197, 150, 210));
                    for (auto Coordinate : {Anchor.Incoming, Anchor.Outgoing}) Ink->AddCircleFilled(Project(Coordinate), 3 * Scale, IM_COL32(204, 230, 181, 255));
                }
            }
            Ink->PopClipRect();
        }
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
    return Consumed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETAINED POINT GUIDES AND INPUT
//------------------------------------------------------------------------------------------------------------------------

bool PatternWorkspacePanel::ConstructPointInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if ((Tool != VectorAuthoringTool::Points && Gesture != 9 && Gesture != 10) || MaskSelection || Selection.size() != 1) return false;
    const auto Snapshot = QueryDocument();
    const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
    if (Position == Snapshot.Layers.end() || Snapshot.Instructions[Position->Root].Topology[0] != 6
        || QueryVectorAuthoringProtection(Snapshot, *Position) || !QueryVectorAuthoringVisibility(Snapshot, *Position)) return false;
    if (PointToken != Position->Token) { PointToken = Position->Token; ActiveAnchor = SIZE_MAX; SelectedAnchors.clear(); }
    const auto &Input = ImGui::GetIO();
    try
    {
        const auto Path = DecodeCubicPath(Snapshot, PointToken);
        if (ActiveAnchor >= Path.Anchors.size()) ActiveAnchor = SIZE_MAX;
        std::erase_if(SelectedAnchors, [&](size_t Index) { return Index >= Path.Anchors.size(); });
        const auto Project = [&](std::array<float, 2> Coordinate)
        {
            return ImVec2(Board.MinimumX + Coordinate[0] / 512 * Board.Width(), Board.MinimumY + Coordinate[1] / 512 * Board.Height());
        };
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        size_t Hit = SIZE_MAX;
        CubicControl HitControl = CubicControl::Anchor;
        const auto Grip = [&](size_t Index, CubicControl Control, const char *Caption, std::array<float, 2> Coordinate)
        {
            const auto Point = Project(Coordinate);
            const float Radius = 6 * Scale;
            ControlExtents[std::string(Caption) + std::to_string(Index)] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
            const bool Selected = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Index) != SelectedAnchors.end();
            Ink->AddCircleFilled(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale,
                Selected ? IM_COL32(230, 246, 211, 255) : IM_COL32(130, 153, 117, 255));
            Ink->AddCircle(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale, IM_COL32(45, 65, 40, 255));
            if (std::hypot(Input.MousePos.x - Point.x, Input.MousePos.y - Point.y) <= Radius)
            {
                Hit = Index; HitControl = Control;
            }
        };
        if (ActiveAnchor != SIZE_MAX)
        {
            const auto &Anchor = Path.Anchors[ActiveAnchor];
            for (auto Control : {CubicControl::Incoming, CubicControl::Outgoing})
            {
                if ((!Path.Closed && ActiveAnchor == 0 && Control == CubicControl::Incoming)
                    || (!Path.Closed && ActiveAnchor + 1 == Path.Anchors.size() && Control == CubicControl::Outgoing)) continue;
                const auto Coordinate = Control == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
                Ink->AddLine(Project(Anchor.Position), Project(Coordinate), IM_COL32(170, 197, 150, 210));
                Grip(ActiveAnchor, Control, Control == CubicControl::Incoming ? "Path incoming " : "Path outgoing ", Coordinate);
            }
        }
        // 📝 Anchors take hit precedence over coincident corner tangents.
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Grip(Index, CubicControl::Anchor, "Path anchor ", Path.Anchors[Index].Position);
        Ink->PopClipRect();
        const std::array<float, 2> Pointer{(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
        if (Gesture == 0 && Hovered && Hit == SIZE_MAX && ImGui::IsMousePosValid() && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            const auto Projection = ProjectCubicSegment(Path, Pointer);
            if (Projection.Distance * Board.Width() / 512 <= 6 * Scale && Projection.Parameter >= .02f && Projection.Parameter <= .98f)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(0))
                {
                    SegmentProjection = Projection; SegmentBending = false; PointDragSource = Path;
                    GestureSource = History.QueryDocument(); GestureSelection = Selection;
                    PointerOrigin = {Pointer[0], Pointer[1]}; Gesture = 10;
                }
            }
        }
        if (Gesture == 10 && GestureSource)
        {
            if (std::hypot(Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
                SegmentBending = true;
            if (SegmentBending || ImGui::IsMouseReleased(0))
            {
                auto Edited = PointDragSource;
                if (SegmentBending) BendCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter,
                    {Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y});
                else
                {
                    SubdivideCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter);
                    ActiveAnchor = SegmentProjection.SegmentIndex + 1; SelectedAnchors = {ActiveAnchor};
                }
                auto Draft = *GestureSource; ReplaceCubicPath(Draft, PointToken, Edited); GestureDraft = std::move(Draft); ++GestureRevision;
            }
            return true;
        }
        if (Hovered && Hit != SIZE_MAX) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (Gesture == 0 && Hovered && Hit != SIZE_MAX && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            if (HitControl == CubicControl::Anchor)
            {
                const auto Included = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Hit);
                if (Input.KeyShift && Included != SelectedAnchors.end())
                {
                    SelectedAnchors.erase(Included);
                    ActiveAnchor = SelectedAnchors.empty() ? SIZE_MAX : SelectedAnchors.back();
                    return true;
                }
                if (Included == SelectedAnchors.end())
                {
                    if (!Input.KeyShift) SelectedAnchors.clear();
                    SelectedAnchors.push_back(Hit);
                }
            }
            ActiveAnchor = Hit; ActiveControl = HitControl;
            PointDragSource = Path;
            GestureSource = History.QueryDocument(); GestureSelection = Selection;
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
            Gesture = 9;
        }
        if (Gesture == 9 && GestureSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
        {
            const auto &Anchor = PointDragSource.Anchors.at(ActiveAnchor);
            const auto Origin = ActiveControl == CubicControl::Anchor ? Anchor.Position
                : ActiveControl == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
            std::array<float, 2> Destination{Origin[0] + (Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                Origin[1] + (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y};
            if (SnappingEnabled) for (auto &Coordinate : Destination) Coordinate = std::round(Coordinate / 16) * 16;
            if (Input.KeyShift && ActiveControl != CubicControl::Anchor) Destination = ConstrainCubicDirection(Anchor.Position, Destination);
            auto Edited = PointDragSource;
            if (ActiveControl == CubicControl::Anchor)
                TranslateCubicAnchors(Edited, SelectedAnchors, {Destination[0] - Origin[0], Destination[1] - Origin[1]});
            else TranslateCubicControl(Edited, ActiveAnchor, ActiveControl, Destination, Input.KeyAlt);
            auto Draft = *GestureSource;
            ReplaceCubicPath(Draft, PointToken, Edited);
            GestureDraft = std::move(Draft);
            ++GestureRevision;
        }
        return Gesture == 9;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        if (Gesture == 9 || Gesture == 10) { GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision; }
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     NATIVE CANVAS
//------------------------------------------------------------------------------------------------------------------------

void PatternWorkspacePanel::ConstructCanvas()
{
    CapturePanel("Viewport");
    const auto ContentOrigin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const auto At = [&](float X, float Y) { return ImVec2(ContentOrigin.x + X * Scale, ContentOrigin.y + Y * Scale); };
    const float InnerWidth = Width - 48 * Scale;
    ImGui::SetCursorScreenPos(At(24, 17));
    Controls.ControlHeight = 28;
    Controls.Button("Design tile", 72 * Scale, true, {}, true);
    ImGui::SameLine(0, 6 * Scale);
    ImGui::BeginDisabled(); Controls.Button("3D material", 76 * Scale, false, {}, true); Controls.Tooltip("3D material", "Native channel targets and material application are not hosted yet."); ImGui::EndDisabled();
    Caption(At(std::max(200.f, Width / Scale - 212), 26), "Compose in 2D. See it on a real surface.", 10 * Scale, 0x777766);
    ImGui::GetWindowDrawList()->AddLine(At(24, 60), At(Width / Scale - 24, 60), ImGui::GetColorU32(Spectrum(0x1e1e1e)));
    Caption(At(24, 89), MaskSelection ? "OWNED MASK / 512 UNITS" : "REPEAT TILE / 512 UNITS", 8 * Scale, 0x8d8d7b);
    auto Name = QueryDocument().Name;
    std::snprintf(NameText.data(), NameText.size(), "%s", Name.c_str());
    ImGui::SetCursorScreenPos(At(24, 105));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushFont(static_cast<ImFont *>(LightTypeface ? LightTypeface : RegularTypeface), 20 * 1.302f * Scale);
    ImGui::SetNextItemWidth(InnerWidth - 150 * Scale);
    if (ImGui::InputText("##PatternName", NameText.data(), NameText.size())) EvaluateEdit([&](auto &Document) { Document.Name = NameText.data(); });
    ImGui::PopFont(); ImGui::PopStyleColor();
    Caption(At(Width / Scale - 132, 109), QueryModified() ? "Unsaved changes" : "Document saved", 9 * Scale, 0x969b81);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(24, 161), At(Width / Scale - 24, 211), ImGui::GetColorU32(Spectrum(0x191919)), 8 * Scale);
    ImGui::SetCursorScreenPos(At(32, 170));
    Controls.ControlHeight = 32;
    if (Controls.Button("Select tool (V)", 32 * Scale, Tool == VectorAuthoringTool::Select, SelectPath)) Tool = VectorAuthoringTool::Select;
    Controls.Tooltip("Select", "Select visible GPU contours; Shift-click to extend. Drag once for one undoable source edit.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Point tool (A)", 32 * Scale, Tool == VectorAuthoringTool::Points, "M4 3L11 21L14 14L21 11Z")) Tool = VectorAuthoringTool::Points;
    Controls.Tooltip("Point editing", "Select one cubic path. Click a segment to split; drag it to bend with fixed anchors. Drag anchors/tangents; Alt moves tangents independently. Escape cancels.");
    for (uint32_t Category = 0; Category < 2; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        if (Controls.Button(Category ? "Ellipse tool (O)" : "Rectangle tool (R)", 32 * Scale, Tool == (Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle), ShapePaths[Category])) Tool = Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle;
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the tile to author an editable source shape.");
    }
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pen tool (P)", 32 * Scale, Tool == VectorAuthoringTool::Pen, "M12 3L21 12L15 21L3 9ZM3 9L12 12L15 21M12 12V3")) Tool = VectorAuthoringTool::Pen;
    Controls.Tooltip("Pen", "Click anchors; drag tangents. Hold Space during placement to reposition the new anchor and controls. Alt-click the last pending anchor to break its outgoing tangent; drag independently. Click an open endpoint to continue; while continuing, click another path endpoint to join. Ctrl bypasses endpoint joining or starts a separate path. Shift constrains direction; Alt moves independently. First anchor closes; Enter finishes; Escape cancels.");
    ImGui::SameLine(0, 4 * Scale);
    ImGui::BeginDisabled(); Controls.Button("Sample appearance", 32 * Scale, false, "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"); Controls.Tooltip("Eyedropper", "GPU vector sampling remains to be integrated."); ImGui::EndDisabled();
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pan tool", 32 * Scale, Tool == VectorAuthoringTool::Pan, "M7 12V6Q8 3 9 6V11V4Q11 1 12 4V11V5Q15 2 15 5V12V8Q18 5 18 8V16Q16 23 10 21L3 14Q2 10 5 12L7 14")) Tool = VectorAuthoringTool::Pan;
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("More tools", 78 * Scale)) ImGui::OpenPopup("Pattern more tools");
    if (DropdownControlSurface::BeginMenu("Pattern more tools", Scale))
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
        ImGui::BeginDisabled(bool(PenSource));
        if (Controls.Toggle("Automatic Pen curves", AutomaticPen)) ImGui::CloseCurrentPopup();
        Controls.Tooltip("Automatic Pen curves", "New Pen paths derive tangents from neighbouring anchors. Manual tangent drags are replaced. Existing endpoint continuation stays manual.");
        ImGui::EndDisabled();
        DropdownControlSurface::EndMenu();
    }
    Controls.ControlHeight = 24;
    Ink->AddRectFilled(At(24, 211), At(Width / Scale - 24, 249), ImGui::GetColorU32(Spectrum(0x20241d)));
    Caption(At(36, 226), "VECTOR CANVAS    512 × 512", 8 * Scale, 0xa5b394);
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
    // 📝 Resolution-independence for the live canvas: ask the GPU to bake at the board's actual
    // on-screen device-pixel edge (zoom included) rather than always sampling a fixed-size raster
    // and stretching it — that stretch is what reads as pixelated art when zooming in or out.
    const float DisplayPixels = BoardSize * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t RequestedExtent = uint32_t(std::clamp(std::ceil(DisplayPixels), 1.f, 4096.f));
    const auto Texture = QueryCoverage ? QueryCoverage(CachedCoverageProgram, RequestedExtent) : 0;
    if (Texture) Ink->AddImage(ImTextureRef(Texture), BoardMinimum, BoardMaximum);
    else Caption(ImVec2(BoardMinimum.x + 12 * Scale, BoardMinimum.y + 12 * Scale), "GPU coverage unavailable", 11 * Scale, 0x68695a);
    const bool BakePending = Texture && QueryCoverageReady && !QueryCoverageReady();
    if (BakePending) { if (PendingBakeSince < 0) PendingBakeSince = ImGui::GetTime(); }
    else PendingBakeSince = -1;
    // 📝 GPU compute for a typical edit retires within a frame or two; only show the badge once a
    // bake has stayed pending longer than that, so routine editing stops flashing it continuously.
    if (BakePending && PendingBakeSince >= 0 && ImGui::GetTime() - PendingBakeSince > 0.15)
    {
        const char *Badge = "Baking pattern...";
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
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::InvisibleButton("Pattern canvas", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    ControlExtents["Pattern canvas"] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
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
    const bool PenInput = ConstructPenInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    const bool PointInput = ConstructPointInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    if (!PenInput && !PointInput && Hovered && Gesture == 0 && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(2)))
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
        else if (Tool != VectorAuthoringTool::Select && Tool != VectorAuthoringTool::Points)
        {
            Gesture = 2;
            GestureSource = History.QueryDocument();
            GestureDraft.reset();
            GesturePrimitiveToken = 0;
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
            if (Gesture == 2)
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
    // 📝 Cancel precedes release so Escape and button-up in the same tick cannot finalize a cancelled edit.
    if ((ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()) && Gesture)
    {
        Selection = GestureSelection; GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    }
    else if (Gesture && Gesture != 7 && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
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
    Caption(At(35, 689), Tool == VectorAuthoringTool::Points ? "Shift-click anchors to multi-select · Drag curve to bend · Click curve to split · Space pan" : "Marquee to select · Shift constrain · Alt centred resize · Space pan", 8 * Scale, 0x909a7c);
    Caption(At(Width / Scale - 114, 689), (std::to_string(Selection.size()) + " selected").c_str(), 8 * Scale, 0xc6d2b5);
    Caption(At(24, 733), "Seamless repeat preview", 10 * Scale, 0xc3cbb5);
    const ImVec2 RepeatMinimum = At(24, 753), RepeatMaximum = At(Width / Scale - 24, 885);
    Ink->AddRectFilled(RepeatMinimum, RepeatMaximum, ImGui::GetColorU32(Spectrum(0xede7d7)), 8 * Scale);
    if (Texture && !MaskSelection)
    {
        const float Tile = 120 * Scale;
        Ink->PushClipRect(RepeatMinimum, RepeatMaximum, true);
        for (float X = RepeatMinimum.x; X < RepeatMaximum.x; X += Tile)
            Ink->AddImage(ImTextureRef(Texture), ImVec2(X, RepeatMinimum.y), ImVec2(X + Tile, RepeatMinimum.y + Tile));
        Ink->PopClipRect();
    }
    ImGui::SetCursorScreenPos(RepeatMaximum);
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION-AWARE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void PatternWorkspacePanel::ConstructInspector()
{
    CapturePanel("Inspector");
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Position.x + 18 * Scale, Position.y + 30 * Scale));
    ImGui::BeginChild("Pattern inspector scroll", ImVec2(ImGui::GetContentRegionAvail().x - 18 * Scale, 0));
    if (PenSource)
    {
        Controls.BeginCard("Pending pen path", true);
        ImGui::TextWrapped("Click to place anchors; drag tangents. Ctrl-click a retained endpoint to start a separate path. While continuing a retained path, click another endpoint to join; Ctrl bypasses joining. Joins require adjacent matching opaque strokes, matching group/channels and no owned masks. Hold Space during placement to reposition the new anchor. Alt-click the last anchor to break its outgoing tangent (manual curves). Click the first anchor to close. Enter finishes open; Escape cancels; Backspace removes the last anchor.");
        if (Controls.Button("Finish open path", 200 * Scale)) FinishPen(false);
        if (Controls.Button("Cancel pen path", 200 * Scale)) CancelPen();
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    const auto Snapshot = QueryDocument();
    const auto SelectedTokens = Selection;
    const auto Selected = [&](const auto &Layer) { return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end(); };
    const auto First = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), Selected);
    const bool Protected = std::any_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Selected(Layer) && QueryVectorAuthoringProtection(Snapshot, Layer); });
    if (First == Snapshot.Layers.end())
    {
        Controls.BeginCard("Your next shape", true);
        ImGui::TextWrapped("Pick a tool and draw on the canvas. Add a shape with the + beside Motifs.");
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    if (!MaskSelection && Selection.size() == 2)
    {
        std::vector<uint32_t> Paths;
        for (const auto &Layer : Snapshot.Layers) if (Selected(Layer) && Snapshot.Instructions[Layer.Root].Topology[0] == 6) Paths.push_back(Layer.Token);
        if (Paths.size() == 2)
        {
            Controls.BeginCard("Join open paths", true);
            ImGui::TextWrapped("Retains the lower layer's identity and appearance. Adds a straight bridge, or merges coincident endpoints. Requires adjacent layers with matching opaque solid strokes, group/channel metadata and no owned masks.");
            constexpr const char *Captions[]{"Join end to start", "Join end to end", "Join start to start", "Join start to end"};
            for (int Pair = 0; Pair < 4; ++Pair)
            {
                const bool RetainedStart = Pair >= 2, ConsumedEnd = Pair % 2 != 0;
                std::string Refusal;
                try { auto Candidate = Snapshot; IntegrateCubicPaths(Candidate, Paths[0], Paths[1], RetainedStart, ConsumedEnd); }
                catch (const std::exception &Failure) { Refusal = Failure.what(); }
                ImGui::BeginDisabled(Protected || Gesture != 0 || !Refusal.empty());
                if (Controls.Button(Captions[Pair], 210 * Scale))
                    EvaluateEdit([&](auto &Document) { IntegrateCubicPaths(Document, Paths[0], Paths[1], RetainedStart, ConsumedEnd); });
                Controls.Tooltip(Captions[Pair], Refusal.empty() ? "Join the chosen endpoints in one undoable source operation. The consumed layer is removed; independent aliases are retained." : Refusal.c_str());
                ImGui::EndDisabled();
            }
            Controls.EndCard();
            if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
        }
    }
    if (!MaskSelection && Selection.size() == 1 && Tool == VectorAuthoringTool::Points)
    {
        Controls.BeginCard("Path points", true);
        const auto Category = Snapshot.Instructions[First->Root].Topology[0];
        ImGui::BeginDisabled(Protected || Gesture != 0);
        if (Category < 6)
        {
            ImGui::TextWrapped("Convert this primitive to an editable cubic path. Ellipses use a four-curve approximation.");
            if (Controls.Button("Convert to cubic path", 200 * Scale))
                EvaluateEdit([&](auto &Document) { ReplaceCubicPath(Document, First->Token, DerivePrimitiveCubicPath(Document, First->Token)); });
        }
        else if (Category == 6)
        {
            ImGui::TextWrapped("Shift-click anchors to select or deselect several. Drag or nudge selected anchors together. Automatic tangents use neighbours; Delete removes selected anchors. Escape cancels a drag.");
            ImGui::Text("%zu anchors selected", SelectedAnchors.size());
            if (Controls.Button("Select all anchors", 200 * Scale))
            {
                try
                {
                    const auto Path = DecodeCubicPath(Snapshot, First->Token);
                    PointToken = First->Token; SelectedAnchors.clear();
                    for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) SelectedAnchors.push_back(Index);
                    ActiveAnchor = SelectedAnchors.back();
                }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
            ImGui::BeginDisabled(PointToken != First->Token || SelectedAnchors.empty());
            if (Controls.Button("Automatic selected tangents", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    ConfigureAutomaticCubicTangents(Path, SelectedAnchors);
                    ReplaceCubicPath(Document, First->Token, Path);
                });
            Controls.Tooltip("Automatic selected tangents", "Derive aligned tangents from neighbouring anchor positions. Lengths use one third of each adjacent chord. Only selected anchors change; one undo restores the source.");
            for (bool Smooth : {false, true})
                if (Controls.Button(SelectedAnchors.size() > 1
                    ? (Smooth ? "Smooth selected anchors" : "Corner selected anchors")
                    : (Smooth ? "Smooth active anchor" : "Corner active anchor"), 200 * Scale))
                    EvaluateEdit([&](auto &Document)
                    {
                        auto Path = DecodeCubicPath(Document, First->Token);
                        for (size_t Index : SelectedAnchors) ConfigureCubicTangent(Path, Index, Smooth);
                        ReplaceCubicPath(Document, First->Token, Path);
                    });
            bool SegmentEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                SegmentEligible = ActiveAnchor < Path.Anchors.size() && Path.Anchors.size() < 400
                    && (Path.Closed || ActiveAnchor + 1 < Path.Anchors.size());
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!SegmentEligible);
            if (Controls.Button("Subdivide following segment", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    SubdivideCubicSegment(Path, ActiveAnchor);
                    ReplaceCubicPath(Document, First->Token, Path);
                    ++ActiveAnchor; SelectedAnchors = {ActiveAnchor};
                });
            Controls.Tooltip("Subdivide segment", "Insert an anchor halfway along the following cubic using de Casteljau subdivision. The curve is preserved; one undo restores the source.");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            bool ResumeEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                ResumeEligible = !Path.Closed && Path.Anchors.size() < 400;
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!ResumeEligible);
            for (bool FromStart : {true, false})
                if (Controls.Button(FromStart ? "Resume from start" : "Resume from end", 200 * Scale))
                {
                    try { ResumePen(First->Token, FromStart); }
                    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
                }
            ImGui::EndDisabled();
        }
        else ImGui::TextWrapped("Select an independent source path; aggregate expressions are not flattened for point editing.");
        ImGui::EndDisabled();
        Controls.EndCard();
    }
    if (!MaskSelection)
    {
        Controls.BeginCard(Selection.size() == 1 ? "Selected vector" : "Selected vectors");
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
            ImGui::TextWrapped("Surface contributions are authored per selected shape and sampled by the live material preview.");
            if (Selection.size() > 1)
                ImGui::TextDisabled("Toggling a channel applies the displayed setting to every selected shape.");
            int Structure = int(First->SurfaceStructure);
            ImGui::BeginDisabled(Protected);
            if (ImGui::Combo("Surface construction", &Structure, "Freeform\0Stitch\0Weave\0"))
                EvaluateEdit([&](auto &Document)
                {
                    for (auto &Layer : Document.Layers)
                        if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                        {
                            Layer.SurfaceStructure = VectorAuthoringSurfaceStructure(Structure);
                            const bool StitchGeometry = Layer.TextileGeometry == VectorAuthoringTextileGeometry::NeedleEntry ||
                                Layer.TextileGeometry == VectorAuthoringTextileGeometry::StitchThread ||
                                Layer.TextileGeometry == VectorAuthoringTextileGeometry::UpperThreadPass;
                            const bool WeaveGeometry = Layer.TextileGeometry == VectorAuthoringTextileGeometry::WeaveYarn;
                            if ((Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Freeform && Layer.TextileGeometry != VectorAuthoringTextileGeometry::Unclassified) ||
                                (Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Stitch && WeaveGeometry) ||
                                (Layer.SurfaceStructure == VectorAuthoringSurfaceStructure::Weave && StitchGeometry))
                                Layer.TextileGeometry = VectorAuthoringTextileGeometry::Unclassified;
                            if (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform)
                            {
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Enabled = true;
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled = true;
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = std::max(Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength, 0.01f);
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength = std::max(Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength, 0.01f);
                            }
                        }
                });
            ImGui::EndDisabled();
            int TextileGeometry = int(First->TextileGeometry);
            ImGui::BeginDisabled(Protected);
            if (ImGui::Combo("Textile geometry", &TextileGeometry,
                "Unclassified\0Needle entry (stitch holes)\0Stitch thread path\0Upper thread pass\0Weave yarn\0"))
                EvaluateEdit([&](auto &Document)
                {
                    for (auto &Layer : Document.Layers)
                        if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                        {
                            Layer.TextileGeometry = VectorAuthoringTextileGeometry(TextileGeometry);
                            if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::NeedleEntry ||
                                Layer.TextileGeometry == VectorAuthoringTextileGeometry::StitchThread ||
                                Layer.TextileGeometry == VectorAuthoringTextileGeometry::UpperThreadPass)
                                Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
                            else if (Layer.TextileGeometry == VectorAuthoringTextileGeometry::WeaveYarn)
                                Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
                            if (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform)
                            {
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Enabled = true;
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled = true;
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = std::max(Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength, 0.01f);
                                Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength = std::max(Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength, 0.01f);
                            }
                        }
                });
            ImGui::EndDisabled();
            const char *Channels[]{"Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Ambient occlusion", "Transmission", "Clearcoat"};
            ImGui::BeginDisabled(Protected);
            for (size_t Index = 0; Index < std::size(Channels); ++Index)
            {
                bool Enabled = First->SurfaceChannels[Index].Enabled;
                const bool RequiredForTextile = First->SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform && (Index == size_t(VectorAuthoringSurfaceChannel::Height) || Index == size_t(VectorAuthoringSurfaceChannel::Normal));
                ImGui::BeginDisabled(RequiredForTextile);
                const bool Changed = Controls.Toggle(Channels[Index], Enabled);
                ImGui::EndDisabled();
                if (Changed)
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceChannels[Index].Enabled = Enabled;
                    });
                float Strength = First->SurfaceChannels[Index].Strength;
                const std::string StrengthCaption = std::string(Channels[Index]) + " strength";
                const float Minimum = RequiredForTextile ? 0.01f : 0.f;
                const float Maximum = Index == size_t(VectorAuthoringSurfaceChannel::Normal) ? 4.f : 1.f;
                if (Controls.Range(StrengthCaption.c_str(), Strength, Minimum, Maximum))
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
        Controls.ControlHeight = 48;
        if (Controls.Button("Array / pattern", ImGui::GetContentRegionAvail().x, ArraysOpen, {}, true)) ArraysOpen = !ArraysOpen;
        Controls.ControlHeight = 30;
        if (ArraysOpen && Controls.Array(ArraySpecification, Snapshot.Layers.size(), Selection.size(), 64, Protected))
        {
            try { auto Result = GenerateVectorAuthoringArray(Snapshot, Selection, ArraySpecification); Finalize(std::move(Result.Document), std::move(Result.Selection)); }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    Controls.BeginCard(MaskSelection ? "Vector mask" : "Selected motif", true);
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
        if (Controls.Dropdown("Motif fill source", Category, Sources)) { VectorAuthoringSourcePatch Patch; Patch.PaintCategory = uint32_t(Category); ApplySourcePatch(Patch); }
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

void PatternWorkspacePanel::ConstructCloseDialogue()
{
    if (CloseRequested && !ImGui::IsPopupOpen("Unsaved pattern")) ImGui::OpenPopup("Unsaved pattern");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    const PatternModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Unsaved pattern", nullptr, ImGuiWindowFlags_NoResize))
    {
        const auto Dialogue = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Dialogue.x, Dialogue.y + 4 * Scale), "UNSAVED PATTERN", 7 * Scale, 0x8b8d79);
        Caption(ImVec2(Dialogue.x, Dialogue.y + 26 * Scale), "Save this pattern?", 17 * Scale, 0xdcdccd, LightTypeface);
        ImGui::Dummy(ImVec2(1, 58 * Scale));
        const auto &Name = QueryDocument().Name;
        ImGui::TextWrapped("\u201c%s\u201d has unsaved changes. Save keeps them; Discard restores the last save.",
            Name.empty() ? "Untitled pattern" : Name.c_str());
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

void PatternWorkspacePanel::SuspendContent() noexcept
{
    CancelPen();
    if (GestureSource) Selection = std::move(GestureSelection);
    GestureSource.reset(); GestureDraft.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
}

void PatternWorkspacePanel::FinalizeContent() noexcept
{
    SuspendContent();
    QueryCoverage = {}; QueryCoverageReady = {}; QuerySelectionToken = {}; SaveSource = {}; LoadSource = {}; ActivateMaterial = {}; ActivateStamp = {}; CancelClose = {}; EvaluateNavigationDelta = {};
}

void PatternWorkspacePanel::ConstructInterface()
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
    const bool PendingBeforeInterface = PenSource.has_value();
    ConstructHeader();
    Panels.ConstructInterface(Scale);
    ConstructCloseDialogue();
    const auto &Input = ImGui::GetIO();
    if (Gesture == 0 && !PenSource && !PendingBeforeInterface && !Input.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
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
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_P)) Tool = VectorAuthoringTool::Pen;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_A)) Tool = VectorAuthoringTool::Points;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_V)) Tool = VectorAuthoringTool::Select;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_R)) Tool = VectorAuthoringTool::Rectangle;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_O)) Tool = VectorAuthoringTool::Ellipse;
        if (ImGui::IsKeyPressed(ImGuiKey_0)) { Pan = {}; Zoom = 1; }
        if (!Selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (MaskSelection)
            {
                EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == MaskSelection && !QueryVectorAuthoringProtection(Document, Layer)) Layer.Mask.reset(); });
                MaskSelection = 0;
            }
            else if (Tool == VectorAuthoringTool::Points)
            {
                if (Selection.size() == 1 && PointToken == Selection[0] && ActiveAnchor != SIZE_MAX)
                    EvaluateEdit([&](auto &Document)
                    {
                        auto Path = DecodeCubicPath(Document, PointToken);
                        PartitionCubicAnchors(Path, SelectedAnchors);
                        ReplaceCubicPath(Document, PointToken, Path);
                        ActiveAnchor = SIZE_MAX; SelectedAnchors.clear();
                    });
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
            if (Patch.TranslationX || Patch.TranslationY)
            {
                if (Tool == VectorAuthoringTool::Points)
                {
                    if (Selection.size() == 1 && PointToken == Selection[0] && ActiveAnchor != SIZE_MAX)
                        EvaluateEdit([&](auto &Document)
                        {
                            auto Path = DecodeCubicPath(Document, PointToken);
                            TranslateCubicAnchors(Path, SelectedAnchors, {Patch.TranslationX, Patch.TranslationY});
                            ReplaceCubicPath(Document, PointToken, Path);
                        });
                }
                else ApplySourcePatch(Patch);
            }
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
