/*==========================================================================================================================================
                                                        MATERIALLIBRARYPANEL.CPP
==========================================================================================================================================*/
// 🧩 Native material discovery, selection, edit/apply/revert and explicit disk operations; no simulated renderer thumbnails.

#include "MaterialLibraryPanel.h"
#include "Engine/ContentInterchange/MaterialRecipeProfiles.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <set>
#include <optional>

namespace Frontier
{
namespace
{
ImU32 EncodeMaterialInk(uint32_t Rgb, unsigned Alpha = 255)
{
    return IM_COL32((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}
ColorQuad DecodeMaterialSpectrum(uint32_t Rgb)
{
    return {float((Rgb >> 16) & 255) / 255, float((Rgb >> 8) & 255) / 255, float(Rgb & 255) / 255, 1};
}
// 📝 Rounded, padded tooltip card shared by every material hover hint.
struct MaterialTooltipScope
{
    explicit MaterialTooltipScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 10 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * Scale, 5 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.098f, .098f, .106f, .97f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320 * Scale);
    }
    ~MaterialTooltipScope()
    {
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }
};
// 📝 Rounded, padded modal chrome shared by every blocking dialog (library switch, unsaved-edit
// prompt, create/duplicate destination). ConstructInterface zeroes WindowRounding/WindowPadding for
// the whole docked workspace, so these overlays need their own scope or they inherit flush, square
// edges instead of looking like deliberate dialogs.
struct MaterialModalScope
{
    explicit MaterialModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~MaterialModalScope()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};
void PresentMaterialTooltip(float Scale, const char *Title, const char *Body)
{
    MaterialTooltipScope Scope(Scale);
    if (Title && Title[0]) ImGui::TextUnformatted(Title);
    if (Body && Body[0]) ImGui::TextDisabled("%s", Body);
}
void ConstructMaterialCaption(float X, float Y, float Size, uint32_t Rgb, const char *Caption, ImFont *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? Typeface : ImGui::GetFont(), Size * 1.302f, ImVec2(X, Y - Size * .15f), EncodeMaterialInk(Rgb, unsigned(255 * ImGui::GetStyle().Alpha)), Caption);
}
bool ConstructMaterialPill(const char *Caption, ImVec2 Position, ImVec2 Extent, bool Selected = false)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y),
        EncodeMaterialInk(Selected ? 0x282825 : ImGui::IsItemHovered() ? 0x2e2e2b : 0x181818), Extent.y * .5f);
    Ink->AddRect(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y), EncodeMaterialInk(0xffffff, Selected ? 24 : 8), Extent.y * .5f);
    const char *CaptionEnd = ImGui::FindRenderedTextEnd(Caption);
    const auto CaptionExtent = ImGui::CalcTextSize(Caption, CaptionEnd);
    Ink->AddText(ImVec2(Position.x + (Extent.x - CaptionExtent.x) * .5f, Position.y + (Extent.y - CaptionExtent.y) * .5f),
        EncodeMaterialInk(Selected ? 0xcccdbb : 0x858577), Caption, CaptionEnd);
    return Pressed;
}
float *QueryPaintCoefficient(AutomotivePaint &Paint, std::string_view Key)
{
    if (Key == "flakes") return &Paint.Distribution[0];
    if (Key == "flakeSize") return &Paint.Distribution[1];
    if (Key == "flakeScale") return &Paint.Distribution[2];
    if (Key == "depth") return &Paint.Distribution[3];
    if (Key == "flakeRoughnessMin") return &Paint.Finish[0];
    if (Key == "flakeRoughnessMax") return &Paint.Finish[1];
    if (Key == "flakeMetalnessMin") return &Paint.Finish[2];
    if (Key == "flakeMetalnessMax") return &Paint.Finish[3];
    if (Key == "flakeTilt") return &Paint.Relief[0];
    if (Key == "flakeLayerDepth") return &Paint.Relief[1];
    if (Key == "orangePeel") return &Paint.Relief[2];
    if (Key == "orangePeelScale") return &Paint.Relief[3];
    return nullptr;
}
std::optional<MaterialPoolLocation> QueryRecipePoolLocation(MaterialDefinition &Definition, std::string_view Key)
{
    // 📝 Shared-pool-first: procedural params shadow stratum scalars, so one gesture drives both the recipe
    // sliders and the rebaked channels. Direction aliases collapse onto the stored angle.
    if (auto Pool = QueryProceduralPool(Definition, Key)) return Pool;
    for (const auto &Op : QueryProceduralOps())
        if (const auto Canonical = ResolveProceduralParamKey(Op, Key); Canonical != Key)
            if (auto Pool = QueryProceduralPool(Definition, Canonical)) return Pool;
    return {};
}

std::optional<float> QueryRecipeCoefficient(MaterialDefinition &Definition, std::string_view Key)
{
    if (const auto Pool = QueryRecipePoolLocation(Definition, Key)) return float(Pool->Value);
    const uint32_t Index = Key.starts_with("coat") ? 1 : 0;
    if (Index >= Definition.Scattering.Illumination.Configuration[0]) return {};
    auto &Stratum = Definition.Scattering.Illumination.Strata[Index];
    if (Key == "roughness" || Key == "coatRoughness") return Stratum.AlbedoRoughness[3];
    if (Key == "metalness") return Stratum.Optics[0];
    if (Key == "ior" || Key == "coatIor") return Stratum.Optics[1];
    if (Key == "coat") return Stratum.Optics[2];
    if (Key == "anisotropy") return Definition.Scattering.DirectionalOptics[Index][0];
    if (Key == "specular") return Definition.Scattering.DirectionalOptics[Index][3];
    if (Key == "heightStrength") return Definition.Scattering.StratumNormal[Index][1];
    if (Key == "normalStrength") return Definition.Scattering.StratumNormal[Index][0];
    if (Definition.Scattering.Paint.Configuration[3])
    {
        if (Key == "flakeLayers") return float(Definition.Scattering.Paint.Configuration[1]);
        if (const auto *Coefficient = QueryPaintCoefficient(Definition.Scattering.Paint, Key)) return *Coefficient;
    }
    // 📝 Emission strength is the peak of the authored Radiance spectrum (LED recipes).
    if (Key == "emissionStrength")
    {
        const auto &Radiance = Definition.Scattering.RadianceNormal;
        return std::max({Radiance[0], Radiance[1], Radiance[2]});
    }
    return {};
}
const MaterialCatalogueEntry *QuerySelectedMaterial(const MaterialLibrary &Library)
{
    for (const auto &Entry : Library.QueryEntries())
        if (Entry.RelativePath == Library.QuerySelection()) return &Entry;
    return nullptr;
}
bool ConstructMaterialSelection(const char *Caption, const char *Preview, const char *const *Options,
    uint32_t Count, int &Selection, float Scale)
{
    bool Changed = false;
    ImGui::PushID(Caption);
    if (DropdownControlSurface::BeginSelection("##MaterialSelection", Preview, Scale))
    {
        for (uint32_t Index = 0; Index < Count; ++Index)
            if (DropdownControlSurface::Option(Options[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
        DropdownControlSurface::EndSelection();
    }
    ImGui::PopID();
    return Changed;
}
bool ConstructMaterialToggle(const char *Caption, bool &Enabled, float Scale)
{
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const ImVec2 Extent(Width, 32 * Scale);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    if (Pressed) Enabled = !Enabled;
    const auto Minimum = ImVec2(Origin.x + Width - 46 * Scale, Origin.y + 3 * Scale);
    const auto Maximum = ImVec2(Minimum.x + 46 * Scale, Minimum.y + 26 * Scale);
    auto *Ink = ImGui::GetWindowDrawList();
    const ImU32 Track = ImGui::GetColorU32(Enabled ? ImVec4(.31f,.67f,.48f,1) : ImVec4(.23f,.24f,.25f,1));
    Ink->AddText(ImVec2(Origin.x, Origin.y + 7 * Scale), ImGui::GetColorU32(ImGuiCol_Text), Caption);
    Ink->AddRectFilled(Minimum, Maximum, Track, 13 * Scale);
    const float Centre = Enabled ? Maximum.x - 13 * Scale : Minimum.x + 13 * Scale;
    Ink->AddCircleFilled(ImVec2(Centre, (Minimum.y + Maximum.y) * .5f), 9 * Scale, IM_COL32(245,245,240,255));
    return Pressed;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

MaterialLibraryPanel::MaterialLibraryPanel() : SourceText(1048577, '\0')
{
    if (ImGui::GetCurrentContext()) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Material panels");
    (void)Panels.Register({"MaterialLibrary", "Material library", [this]() { ConstructCatalogue(); }, true, {}, {}});
    (void)Panels.Register({"MaterialViewport", "Viewport", [this]() { ConstructViewport(); }, true, {}, {}});
    (void)Panels.Register({"MaterialInspector", "Inspector", [this]() { ConstructInspector(); }, true, {}, {}});
    Panels.ConfigureWorkspaceInsets(79, 0);
    Panels.ConfigureTabExtent(140, 26);
    (void)Panels.ConfigureColumns({{"MaterialLibrary", .26f}, {"MaterialViewport", .48f}, {"MaterialInspector", .26f}});
    std::snprintf(NameText.data(), NameText.size(), "%s", "Untitled material");
    std::snprintf(DestinationText.data(), DestinationText.size(), "%s", "Untitled.material");
}

void MaterialLibraryPanel::ConfigureInterfaceScale(float Scale) noexcept
{
    if (std::isfinite(Scale)) InterfaceScale = std::clamp(Scale, .75f, 1.5f);
}
float MaterialLibraryPanel::QueryInterfaceScale() const noexcept
{
    return InterfaceScale * ImGui::GetStyle().FontScaleDpi;
}

bool MaterialLibraryPanel::ConfigureDirectory(const std::filesystem::path &Directory)
{
    if (Library.QueryModified() || Library.QueryParkedEdits() || !Library.ConfigureDirectory(Directory)) return false;
    std::snprintf(DirectoryText.data(), DirectoryText.size(), "%s", EncodeContentPath(Library.QueryDirectory()).c_str());
    SynchronizeSource();
    return true;
}

bool MaterialLibraryPanel::SelectMaterial(const std::filesystem::path &RelativePath)
{
    if (!Library.OpenMaterial(RelativePath)) return false;
    // 📝 Restored parked drafts compile into the live preview; disk still needs Save or Revert.
    if (Library.QueryUnapplied()) (void)Library.ApplySource();
    PendingLivePreviewApply = false;
    ProceduralDirty = false;
    // 📝 A new selection restarts the inspector at the top; layer-stack scroll offsets never leak across materials.
    if (const auto *Context = ImGui::GetCurrentContext())
        for (auto *Candidate : Context->Windows)
        {
            const std::string Name(Candidate->Name);
            const size_t Child = Name.rfind("/Material properties_");
            if (Child != std::string::npos && Name.find('/', Child + 1) == std::string::npos) ImGui::SetScrollY(Candidate, 0);
        }
    SynchronizeSource();
    return true;
}

void MaterialLibraryPanel::ReplaceSource(std::string Source)
{
    Library.ReplaceSource(std::move(Source));
    SynchronizeSource();
}

bool MaterialLibraryPanel::RequestClose() noexcept
{
    if (!Library.QueryModified() && !Library.QueryParkedEdits()) return true;
    ClosingRequested = true;
    return false;
}

bool MaterialLibraryPanel::ConsumeClose() noexcept
{
    const bool Authorized = CloseAuthorized;
    CloseAuthorized = false;
    return Authorized;
}

void MaterialLibraryPanel::SynchronizeSource()
{
    std::snprintf(SourceText.data(), SourceText.size(), "%s", Library.QuerySource().c_str());
}

void MaterialLibraryPanel::FinalizeContent() noexcept
{
    ActivatePattern = {};
    ActivateStamp = {};
    Library = MaterialLibrary{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIBRARY AND SOURCE EDITOR
//------------------------------------------------------------------------------------------------------------------------

void MaterialLibraryPanel::ConstructCatalogue()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    if (ConstructMaterialPill("Workspace", At(14, 10), ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), !CollectionVisible)) CollectionVisible = false;
    const auto CollectionCaption = "My collection  " + std::to_string(Favourites.size());
    if (ConstructMaterialPill(CollectionCaption.c_str(), ImVec2(Origin.x + Width * .5f + 3 * Scale, Origin.y + 10 * Scale),
        ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), CollectionVisible)) CollectionVisible = true;
    ConstructMaterialCaption(At(20, 56).x, At(20, 56).y, 18 * Scale, 0xd3d3c8, "Material library", static_cast<ImFont *>(HeadingTypeface));
    ConstructMaterialCaption(At(155, 61).x, At(155, 61).y, 8 * Scale, 0x777766, std::to_string(Library.QueryEntries().size()).c_str());
    if (ConstructMaterialPill("##New material", ImVec2(Origin.x + Width - 40 * Scale, At(0, 52).y), ImVec2(24 * Scale, 24 * Scale))) Creating = true;
    ConstructIcon(IconSymbol::EditorAdd, Origin.x + Width - 35 * Scale, At(0, 57).y, 14 * Scale);
    ImGui::SetCursorScreenPos(At(14, 96));
    ImGui::SetNextItemWidth(Width - 28 * Scale);
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(25.f / 255, 25.f / 255, 25.f / 255, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * Scale, 11.5f * Scale));
    ImGui::InputTextWithHint("##Search", "Search materials...", SearchText.data(), SearchText.size());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ConstructIcon(IconSymbol::EditorSearch, At(27, 108).x, At(27, 108).y, 15 * Scale);
    Library.ConfigureFilter(SearchText.data());
    const char *Categories[]{"All materials", "Paint", "Metal", "Ceramic", "Fabric", "Rubber", "Glass", "Plastic", "Leather", "Clay", "Wax", "Skin", "Paper", "Technical", "Stone", "Wall", "Nature"};
    float X = 14, Y = 144;
    ImGui::PushFont(nullptr, 8 * 1.302f * Scale);
    for (const char *Category : Categories)
    {
        const float Edge = ImGui::CalcTextSize(Category).x / Scale + 16;
        if (X + Edge > Width / Scale - 14) { X = 14; Y += 29; }
        if (ConstructMaterialPill(Category, At(X, Y), ImVec2(Edge * Scale, 24 * Scale), CategoryFilter == Category)) CategoryFilter = Category;
        X += Edge + 5;
    }
    Y += 24 + 8;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddLine(At(0, Y), At(Width / Scale, Y), EncodeMaterialInk(0xffffff, 13));
    ConstructMaterialCaption(At(17, Y + 12).x, At(17, Y + 12).y, 8 * Scale, 0x7e7e70, "THE ESSENTIAL COLLECTION");
    std::vector<size_t> Visible;
    for (size_t Index : Library.QueryVisible())
    {
        const auto &Entry = Library.QueryEntries()[Index];
        if (CategoryFilter != "All materials" && Entry.Category != CategoryFilter) continue;
        if (CollectionVisible && !Favourites.contains(EncodeContentPath(Entry.RelativePath))) continue;
        Visible.push_back(Index);
    }
    ConstructMaterialCaption(Origin.x + Width - 30 * Scale, At(0, Y + 12).y, 8 * Scale, 0x7e7e70, std::to_string(Visible.size()).c_str());
    ImGui::PopFont();
    const float FooterY = Bottom - 71 * Scale;
    ImGui::SetCursorScreenPos(At(16, Y + 31));
    ImGui::BeginChild("Material grid", ImVec2(Width - 32 * Scale, std::max(1.f, FooterY - At(0, Y + 31).y)), ImGuiChildFlags_None);
    const float Gap = 9 * Scale;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - 20 * Scale - Gap) * .5f);
    const float CardHeight = 125 * Scale;
    const auto GridOrigin = ImGui::GetCursorScreenPos();
    const float ClipTop = ImGui::GetWindowPos().y, ClipBottom = ClipTop + ImGui::GetWindowSize().y;
    std::vector<std::filesystem::path> Requests;
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float Top = GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap);
        if (Top + CardHeight >= ClipTop && Top < ClipBottom) Requests.push_back(Library.QueryEntries()[Visible[Ordinal]].RelativePath);
    }
    // 📝 Keep the inspector thumbnail resident even when its selected card scrolls out of view.
    if (!Library.QuerySelection().empty() && std::find(Requests.begin(), Requests.end(), Library.QuerySelection()) == Requests.end())
        Requests.push_back(Library.QuerySelection());
    if (RequestThumbnails) RequestThumbnails(Requests);
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const auto &Entry = Library.QueryEntries()[Visible[Ordinal]];
        const ImVec2 Position(GridOrigin.x + 12 * Scale + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        if (Position.y + CardHeight < ClipTop || Position.y > ClipBottom) continue;
        ImGui::SetCursorScreenPos(Position);
        ImGui::PushID(int(Visible[Ordinal]));
        if (ImGui::InvisibleButton("Material card", ImVec2(CardWidth, CardHeight))) SelectMaterial(Entry.RelativePath);
        const bool Hovered = ImGui::IsItemHovered();
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale);
        const PlaneExtent Card{Position.x / Scale, Position.y / Scale, (Position.x + CardWidth) / Scale, (Position.y + CardHeight) / Scale};
        (void)ControlKit::SectionCard(Surface, Card, 15);
        auto *CardInk = ImGui::GetWindowDrawList();
        if (Hovered) CardInk->AddRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), EncodeMaterialInk(0x707173), 15 * Scale);
        const auto Image = QueryThumbnail ? QueryThumbnail(Entry.RelativePath) : MaterialPreviewImage{};
        CardInk->PushClipRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), true);
        if (Image.Texture)
            CardInk->AddImage(ImTextureRef(Image.Texture), ImVec2(Position.x + (CardWidth - 85 * Scale) * .5f, Position.y),
                ImVec2(Position.x + (CardWidth + 85 * Scale) * .5f, Position.y + 85 * Scale),
                ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 89 * Scale, 9 * Scale, 0xc8c8b6, Entry.Name.c_str());
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 105 * Scale, 7 * Scale, 0x767665, Entry.Category.empty() ? "Layered material" : Entry.Category.c_str());
        if (Library.QuerySelection() == Entry.RelativePath)
        {
            CardInk->AddCircleFilled(ImVec2(Position.x + 14 * Scale, Position.y + 14 * Scale), 6 * Scale, EncodeMaterialInk(0xa5b99c));
            CardInk->AddLine(ImVec2(Position.x + 11 * Scale, Position.y + 14 * Scale), ImVec2(Position.x + 13 * Scale, Position.y + 16 * Scale), EncodeMaterialInk(0x2a3927));
            CardInk->AddLine(ImVec2(Position.x + 13 * Scale, Position.y + 16 * Scale), ImVec2(Position.x + 18 * Scale, Position.y + 11 * Scale), EncodeMaterialInk(0x2a3927));
        }
        if (Library.QueryParked(Entry.RelativePath) || (Library.QuerySelection() == Entry.RelativePath && Library.QueryModified()))
            CardInk->AddCircleFilled(ImVec2(Position.x + CardWidth - 14 * Scale, Position.y + 14 * Scale), 5 * Scale, EncodeMaterialInk(0xd8b25c));
        CardInk->PopClipRect();
        if (Hovered) PresentMaterialTooltip(Scale, Entry.Name.c_str(), Entry.Description.c_str());
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + ((Visible.size() + 1) / 2) * (CardHeight + Gap)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    Ink->AddRectFilled(ImVec2(Origin.x, FooterY), ImVec2(Origin.x + Width, Bottom), EncodeMaterialInk(0x151515));
    ConstructIcon(IconSymbol::Stars, Origin.x + 19 * Scale, FooterY + 20 * Scale, 19 * Scale);
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 20 * Scale, 9 * Scale, 0xaeae9b, "Beautiful by calculation.");
    const auto Footer = std::to_string(Library.QueryEntries().size()) + " presets. Vector + procedural surfaces.";
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 37 * Scale, 7 * Scale, 0x6b6b60, Footer.c_str());
    ImGui::PopFont();
}

void MaterialLibraryPanel::SynchronizePreviewCamera()
{
    if (!SmoothedInspectionReady)
    {
        SmoothedInspection = Inspection;
        SmoothedInspectionReady = true;
    }
    else
    {
        SmoothedInspection.Shape = Inspection.Shape;
        SmoothedInspection.Environment = Inspection.Environment;
        SmoothedInspection.Exposure = Inspection.Exposure;
        SmoothedInspection.LightIntensity = Inspection.LightIntensity;
        const float Blend = 1 - std::exp(-std::min(ImGui::GetIO().DeltaTime, .1f) * 8);
        SmoothedInspection.Yaw = std::remainder(SmoothedInspection.Yaw +
                std::remainder(Inspection.Yaw - SmoothedInspection.Yaw, 6.2831853f) * Blend,
            6.2831853f);
        SmoothedInspection.Pitch += (Inspection.Pitch - SmoothedInspection.Pitch) * Blend;
        SmoothedInspection.Zoom += (Inspection.Zoom - SmoothedInspection.Zoom) * Blend;
        SmoothedInspection.PanX += (Inspection.PanX - SmoothedInspection.PanX) * Blend;
        SmoothedInspection.PanY += (Inspection.PanY - SmoothedInspection.PanY) * Blend;
        // 📝 Snap once the lag closes so the preview stops dirtying and settles exactly on the target.
        if (std::abs(std::remainder(Inspection.Yaw - SmoothedInspection.Yaw, 6.2831853f)) < .0002f &&
            std::abs(Inspection.Pitch - SmoothedInspection.Pitch) < .0002f &&
            std::abs(Inspection.Zoom - SmoothedInspection.Zoom) < .0002f &&
            std::abs(Inspection.PanX - SmoothedInspection.PanX) < .0002f &&
            std::abs(Inspection.PanY - SmoothedInspection.PanY) < .0002f)
            SmoothedInspection = Inspection;
    }
    if (!SentInspectionReady || SmoothedInspection != SentInspection)
    {
        SentInspection = SmoothedInspection;
        SentInspectionReady = true;
        if (ConfigurePreview) ConfigurePreview(SmoothedInspection);
    }
}

void MaterialLibraryPanel::ConstructViewport()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const auto Available = ImGui::GetContentRegionAvail();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilledMultiColor(Origin, ImVec2(Origin.x + Available.x, Origin.y + Available.y),
        EncodeMaterialInk(0x202222), EncodeMaterialInk(0x202222), EncodeMaterialInk(0x303235), EncodeMaterialInk(0x303235));
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    // 📝 Full-bleed cover fit between the header and footer bars; transparent margins crop invisibly.
    const float ViewTop = Origin.y + 42 * Scale, ViewBottom = Origin.y + Available.y - 45 * Scale;
    const float ViewWidth = Available.x, ViewHeight = std::max(1.f, ViewBottom - ViewTop);
    const float Edge = std::max(1.f, std::max(ViewWidth, ViewHeight));
    // 📝 Display-size preview: follow the cover-fit edge in framebuffer pixels (snapped, so a live window
    // resize settles instead of reallocating every frame) rather than upscaling the 512 default.
    const float Pixels = Edge * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t Snapped = std::clamp((uint32_t(Pixels) + 63) / 64 * 64, 512u, 2048u);
    if (ConfigurePreviewExtent && Snapped != SentPreviewExtent)
    {
        SentPreviewExtent = Snapped;
        ConfigurePreviewExtent(Snapped);
    }
    const ImVec2 CoverMinimum(Origin.x + (ViewWidth - Edge) * .5f, ViewTop + (ViewHeight - Edge) * .5f);
    if (Image.Texture)
    {
        Ink->PushClipRect(ImVec2(Origin.x, ViewTop), ImVec2(Origin.x + ViewWidth, ViewBottom), true);
        Ink->AddImage(ImTextureRef(Image.Texture), CoverMinimum, ImVec2(CoverMinimum.x + Edge, CoverMinimum.y + Edge),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        Ink->PopClipRect();
    }
    Ink->AddRectFilledMultiColor(ImVec2(Origin.x, ViewTop), ImVec2(Origin.x + ViewWidth, ViewTop + 130 * Scale),
        EncodeMaterialInk(0x151515), EncodeMaterialInk(0x151515), EncodeMaterialInk(0x151515, 0), EncodeMaterialInk(0x151515, 0));
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Available.x, Origin.y + 42 * Scale), EncodeMaterialInk(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Origin.y + 15 * Scale, 7 * Scale, 0x898a78, "LIVE PREVIEW   PBR");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + std::max(4.f, Available.x - 92 * Scale), Origin.y + 7 * Scale));
    if (ImGui::Button("Fullscreen##MaterialPreview", ImVec2(84 * Scale, 27 * Scale)))
    {
        FullscreenPreview = true;
        ResetMaterialPreviewView(Inspection);
        SynchronizePreviewCamera();
    }
    const auto FullscreenMinimum = ImGui::GetItemRectMin(), FullscreenMaximum = ImGui::GetItemRectMax();
    FullscreenPreviewExtent = {FullscreenMinimum.x, FullscreenMinimum.y, FullscreenMaximum.x, FullscreenMaximum.y};
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Fullscreen preview", "Fitted full-work-area preview. Escape to close.");
    const auto *Entry = QuerySelectedMaterial(Library);
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 68 * Scale, 8 * Scale, 0x92917c,
        Entry && !Entry->Category.empty() ? ("01  /  " + Entry->Category + " COLLECTION").c_str() : "MATERIAL COLLECTION");
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 94 * Scale, 30 * Scale, 0xdcdccd, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    if (Entry) ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 136 * Scale, 9 * Scale, 0x7b7c6d, Entry->Description.c_str());
    const float Bottom = Origin.y + Available.y;
    Ink->AddRectFilled(ImVec2(Origin.x, Bottom - 45 * Scale), ImVec2(Origin.x + Available.x, Bottom), EncodeMaterialInk(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "Drag orbit  /  Shift-MMB pan  /  Wheel zoom");
    if (Available.x > 500 * Scale) ConstructMaterialCaption(Origin.x + Available.x - 75 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "VULKAN");

    const auto ConfigureExtent = [&](size_t Index)
    {
        const auto Minimum = ImGui::GetItemRectMin();
        const auto Maximum = ImGui::GetItemRectMax();
        InspectionExtents[Index] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    ImGui::PushID("MaterialInspection");
    ImGui::BeginDisabled(!ConfigurePreview);
    // 📝 One canvas owns gestures until release; widgets and popup menus cannot also drive the camera.
    InspectionExtents[7] = {};
    const ImVec2 CanvasMinimum(Origin.x, Origin.y + 165 * Scale);
    const ImVec2 CanvasMaximum(Origin.x + Available.x, Bottom - 205 * Scale);
    if (CanvasMaximum.y > CanvasMinimum.y)
    {
        ImGui::SetCursorScreenPos(CanvasMinimum);
        ImGui::InvisibleButton("InspectionCanvas", ImVec2(Available.x, CanvasMaximum.y - CanvasMinimum.y),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        ConfigureExtent(7);
        const auto &Input = ImGui::GetIO();
        const bool Hovered = ImGui::IsItemHovered();
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
        if (ImGui::IsItemActive())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            {
                Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / Edge, -2.f, 2.f);
                Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / Edge, -2.f, 2.f);
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 5.f);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
                Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
            }
        }
        if (Hovered && !Input.WantTextInput)
        {
            if (Input.MouseWheel != 0) Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 5.f);
            if (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsMouseDoubleClicked(0))
                ResetMaterialPreviewView(Inspection);
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Inspection.PanX = std::max(-2.f, Inspection.PanX - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Inspection.PanX = std::min(2.f, Inspection.PanX + .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Inspection.PanY = std::max(-2.f, Inspection.PanY - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Inspection.PanY = std::min(2.f, Inspection.PanY + .05f);
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Scale, 5 * Scale));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 18 * Scale, Bottom - 118 * Scale));
    ImGui::SetNextItemWidth(std::min(148 * Scale, Available.x * .42f));
    const char *ShapeOptions[]{"Shader ball", "Sphere", "Plane", "Cube", "Draped cloth"};
    int Shape = int(Inspection.Shape);
    ImGui::SetNextItemWidth(std::min(148 * Scale, Available.x * .42f));
    if (ConstructMaterialSelection("Preview shape", ShapeOptions[Shape], ShapeOptions, 5, Shape, Scale))
        Inspection.Shape = MaterialPreviewShape(Shape);
    ConfigureExtent(3);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Preview shape", "Change preview object; catalogue thumbnails remain unchanged.");
    ImGui::SameLine();
    // 📝 Environment card (Blender-style): sphere previews of every scanned studio archive over
    // exposure and light intensity, with a folder rescan for importing equirect PNGs. The old
    // standalone exposure card and the tinted analytic presets are retired.
    const auto EnvironmentNames = QueryEnvironmentNames ? QueryEnvironmentNames() : std::vector<std::string>{};
    if (!EnvironmentNames.empty())
        Inspection.Environment = std::min<uint32_t>(Inspection.Environment, uint32_t(EnvironmentNames.size() - 1));
    const std::string EnvironmentCaption = EnvironmentNames.empty() ? std::string("Environment") :
        EnvironmentNames[Inspection.Environment];
    const float EnvironmentButtonWidth = std::min(172 * Scale, Available.x * .44f);
    if (ImGui::Button((EnvironmentCaption + "##PreviewEnvironment").c_str(), ImVec2(EnvironmentButtonWidth, 0)))
        ImGui::OpenPopup("##EnvironmentCard");
    ConfigureExtent(4);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Environment", "Studio lighting, exposure and light intensity.");
    ImGui::SetNextWindowSize(ImVec2(342 * Scale, 0));
    if (DropdownControlSurface::BeginMenu("##EnvironmentCard", Scale))
    {
        EnvironmentPreviewExtents.clear();
        if (EnvironmentNames.empty()) ImGui::TextDisabled("No environment archives found.");
        const float TileExtent = 148 * Scale;
        const float CellHeight = TileExtent + 20 * Scale;
        for (size_t Index = 0; Index < EnvironmentNames.size(); ++Index)
        {
            if (Index % 2) ImGui::SameLine();
            const auto CellOrigin = ImGui::GetCursorScreenPos();
            ImGui::PushID(int(Index));
            const bool Pressed = ImGui::InvisibleButton("##EnvironmentPreview", ImVec2(TileExtent, CellHeight));
            ImGui::PopID();
            EnvironmentPreviewExtents.push_back({CellOrigin.x, CellOrigin.y,
                CellOrigin.x + TileExtent, CellOrigin.y + TileExtent});
            const auto Preview = QueryEnvironmentPreview ? QueryEnvironmentPreview(Index) : MaterialPreviewImage{};
            auto *CardInk = ImGui::GetWindowDrawList();
            const ImVec2 TileMinimum = CellOrigin, TileMaximum(CellOrigin.x + TileExtent, CellOrigin.y + TileExtent);
            if (Preview.Texture)
                CardInk->AddImageRounded(ImTextureRef(Preview.Texture), TileMinimum, TileMaximum,
                    ImVec2(Preview.MinimumU, Preview.MinimumV), ImVec2(Preview.MaximumU, Preview.MaximumV),
                    EncodeMaterialInk(0xffffff), 10 * Scale);
            else
                CardInk->AddRectFilled(TileMinimum, TileMaximum, EncodeMaterialInk(0x232629), 10 * Scale);
            if (uint32_t(Index) == Inspection.Environment)
                CardInk->AddRect(TileMinimum, TileMaximum, EncodeMaterialInk(0x8fd0a8, 255), 10 * Scale, 0, 2 * Scale);
            else if (ImGui::IsItemHovered())
                CardInk->AddRect(TileMinimum, TileMaximum, EncodeMaterialInk(0xffffff, 70), 10 * Scale, 0, 1 * Scale);
            const auto &Name = EnvironmentNames[Index];
            const float CaptionWidth = ImGui::CalcTextSize(Name.c_str()).x;
            ConstructMaterialCaption(CellOrigin.x + (TileExtent - CaptionWidth) * .5f,
                CellOrigin.y + TileExtent + 3 * Scale, 10 * Scale,
                uint32_t(Index) == Inspection.Environment ? 0xe8e4d8 : 0xa3a393, Name.c_str());
            if (Pressed)
            {
                Inspection.Environment = uint32_t(Index);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        const auto ExposureOrigin = ImGui::GetCursorScreenPos();
        const float ExposureWidth = std::max(28.f, ImGui::GetContentRegionAvail().x);
        (void)ConstructProperty("Exposure", .25f, 4.f, Inspection.Exposure, "×", true, .01f);
        InspectionExtents[5] = {ExposureOrigin.x, ExposureOrigin.y + 34 * Scale,
            ExposureOrigin.x + ExposureWidth, ExposureOrigin.y + 58 * Scale};
        (void)ConstructProperty("Light intensity", 0.f, 3.f, Inspection.LightIntensity, "×", false, .01f);
        ImGui::Separator();
        if (ImGui::Button("Import environment…", ImVec2(-1, 0)) && RescanEnvironments) RescanEnvironments();
        ImGui::TextDisabled("Drop equirect PNGs into EngineContent/EnvironmentArchives, then import.");
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + std::max(8.f, (Available.x - 240 * Scale) * .5f), Bottom - 196 * Scale));
    const auto ZoomOrigin = ImGui::GetCursorScreenPos();
    Ink->AddRectFilled(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        EncodeMaterialInk(0x181818), 14 * Scale);
    Ink->AddRect(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        EncodeMaterialInk(0xffffff, 20), 14 * Scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::BeginGroup();
    if (ImGui::Button("-##ZoomOut", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
    ConfigureExtent(0);
    ImGui::SameLine(0, 0);
    const auto ZoomCaption = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##ZoomReset";
    if (ImGui::Button(ZoomCaption.c_str(), ImVec2(76 * Scale, 28 * Scale))) Inspection.Zoom = 1;
    ConfigureExtent(1);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Reset zoom", "Reset zoom to 100%.");
    ImGui::SameLine(0, 0);
    if (ImGui::Button("+##ZoomIn", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
    ConfigureExtent(2);
    ImGui::SameLine();
    if (ImGui::Button("Reset view", ImVec2(90 * Scale, 28 * Scale))) ResetMaterialPreviewView(Inspection);
    ConfigureExtent(6);
    if (ImGui::IsItemHovered()) PresentMaterialTooltip(Scale, "Reset view", "Centre / fit and reset orbit. F or Home over preview.");
    ImGui::EndGroup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    SynchronizePreviewCamera();
    ImGui::PopID();
}

void MaterialLibraryPanel::ConstructFullscreenPreview()
{
    if (!FullscreenPreview) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::SetNextWindowViewport(Viewport->ID);
    ImGui::SetNextWindowBgAlpha(1);
    constexpr ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Fullscreen material preview###MaterialPreviewFullscreen", nullptr, Flags))
    {
        ImGui::End();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) FullscreenPreview = false;
    const ImVec2 Origin = ImGui::GetWindowPos();
    const ImVec2 Size = ImGui::GetWindowSize();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), EncodeMaterialInk(0x17191a));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 28, Origin.y + 20));
    ImGui::TextUnformatted(QuerySelectedMaterial(Library) ? QuerySelectedMaterial(Library)->Name.c_str() : "Material preview");
    ImGui::SameLine();
    ImGui::TextDisabled("PBR · fullscreen fit · Escape to close");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Size.x - 150, Origin.y + 14));
    if (ImGui::Button("Close preview  ×", ImVec2(132, 32))) FullscreenPreview = false;
    const float Edge = std::max(1.f, std::min(Size.x - 64.f, Size.y - 150.f));
    const uint32_t FullscreenSnapped =
        std::clamp((uint32_t(Edge * ImGui::GetIO().DisplayFramebufferScale.x) + 63) / 64 * 64, 512u, 2048u);
    if (ConfigurePreviewExtent && FullscreenSnapped != SentPreviewExtent)
    {
        SentPreviewExtent = FullscreenSnapped;
        ConfigurePreviewExtent(FullscreenSnapped);
    }
    const ImVec2 ImagePosition(Origin.x + (Size.x - Edge) * .5f, Origin.y + (Size.y - Edge) * .5f + 8.f);
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    ImGui::SetCursorScreenPos(ImagePosition);
    ImGui::InvisibleButton("Fullscreen inspection canvas", ImVec2(Edge, Edge),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
    const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
    if (ImGui::IsItemActive())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        {
            Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / Edge, -2.f, 2.f);
            Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / Edge, -2.f, 2.f);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 5.f);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
            Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
        }
    }
    if (Hovered && Input.MouseWheel != 0)
    {
        Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 5.f);
    }
    Ink->PushClipRect(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), true);
    if (Image.Texture)
        Ink->AddImage(ImTextureRef(Image.Texture), ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    else Ink->AddRectFilled(ImagePosition, ImVec2(ImagePosition.x + Edge, ImagePosition.y + Edge), EncodeMaterialInk(0x202222));
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + (Size.x - 300) * .5f, Origin.y + Size.y - 58));
    if (ImGui::Button("−##FullscreenZoomOut", ImVec2(42, 30))) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
    ImGui::SameLine();
    const std::string ZoomLabel = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##FullscreenZoomReset";
    if (ImGui::Button(ZoomLabel.c_str(), ImVec2(80, 30))) Inspection.Zoom = 1;
    ImGui::SameLine();
    if (ImGui::Button("+##FullscreenZoomIn", ImVec2(42, 30))) Inspection.Zoom = std::min(5.f, Inspection.Zoom + .1f);
    ImGui::SameLine();
    if (ImGui::Button("Fit object", ImVec2(92, 30))) ResetMaterialPreviewView(Inspection);
    SynchronizePreviewCamera();
    ImGui::End();
}

void MaterialLibraryPanel::ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb)
{
    if (PresentIcon) (void)PresentIcon(Symbol, {X,Y}, Size, DecodeMaterialSpectrum(Rgb));
}

bool MaterialLibraryPanel::ConstructProperty(const char *Caption, float Minimum, float Maximum, float &Position,
    const char *Unit, bool Logarithmic, float Step, const char *Unavailable, float RowWidth)
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(28.f, RowWidth >= 0.f ? RowWidth : ImGui::GetContentRegionAvail().x);
    const bool Enabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
    ConstructMaterialCaption(Origin.x, Origin.y + 5 * Scale, 10 * Scale, 0xa3a393, Caption);
    ImGui::PushID(Caption);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 76 * Scale, Origin.y));
    ImGui::SetNextItemWidth(76 * Scale);
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Scale, 5 * Scale));
    const bool Percentage = std::string_view(Unit) == "%";
    const float Multiplier = Percentage ? 100.f : 1.f;
    const char *Format = Percentage ? "%.0f %%" : std::string_view(Unit) == "µm" ? "%.1f µm" :
        std::string_view(Unit) == "×" ? "%.2f ×" : Step >= 1 ? "%.0f" : "%.3f";
    float Display = Position * Multiplier;
    bool Changed = ImGui::InputFloat("##Position", &Display, 0, 0, Format);
    if (Changed && std::isfinite(Display)) Position = std::clamp(Display / Multiplier, Minimum, Maximum);
    else Changed = false;
    bool Hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 34 * Scale));
    ImGui::InvisibleButton("Property slider", ImVec2(Width, 24 * Scale));
    const bool Active = Enabled && ImGui::IsItemActive();
    Hovered = Hovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const auto &IO = ImGui::GetIO();
    ControlPointer Pointer{IO.MousePos.x / Scale, IO.MousePos.y / Scale, Enabled && IO.MouseDown[0],
        Enabled && ImGui::IsItemActivated(), Enabled && ImGui::IsMouseReleased(0), Enabled && (Active || ImGui::IsItemHovered()), IO.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const PlaneExtent Extent{Origin.x / Scale, Origin.y / Scale + 34, (Origin.x + Width) / Scale, Origin.y / Scale + 58};
    const bool LogarithmicRange = Logarithmic && Minimum > 0 && Maximum > Minimum;
    const float Fraction = LogarithmicRange ? std::clamp(std::log(std::max(Position, Minimum) / Minimum) / std::log(Maximum / Minimum), 0.f, 1.f) : Position;
    float Replacement = Fraction;
    (void)ControlKit::Slider(Surface, Extent, LogarithmicRange ? 0.f : Minimum, LogarithmicRange ? 1.f : Maximum,
        Fraction, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 24, 22);
    // 📝 First designer slider wins: the properties panel renders the finish card above every other
    // section, so headless gestures target the control that stays above the fold.
    if (!RoughnessRecorded)
    {
        RoughnessExtent = {Origin.x, Origin.y + 34 * Scale, Origin.x + Width, Origin.y + 58 * Scale};
        RoughnessRecorded = true;
    }
    if (!CoatRoughnessRecorded && InspectorLayerContext == 1)
    {
        CoatRoughnessExtent = {Origin.x, Origin.y + 34 * Scale, Origin.x + Width, Origin.y + 58 * Scale};
        CoatRoughnessRecorded = true;
    }
    if (Active && Replacement != Fraction)
    {
        Position = LogarithmicRange ? Minimum * std::pow(Maximum / Minimum, Replacement) : Replacement;
        Changed = true;
    }
    if (Changed && Step >= 1) Position = std::clamp(std::round(Position / Step) * Step, Minimum, Maximum);
    if (Unavailable && Hovered) PresentMaterialTooltip(Scale, "Unavailable", Unavailable);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 84 * Scale));
    ImGui::Dummy(ImVec2(1, 0));
    return Changed && Enabled;
}

void MaterialLibraryPanel::ConstructActions()
{
    const auto Extent = []()
    {
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        return PlaneExtent{Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    ImGui::BeginDisabled(!Library.QueryUnapplied());
    if (ImGui::Button("Apply")) (void)Library.ApplySource();
    ImGui::EndDisabled();
    ApplyExtent = Extent();
    ImGui::SameLine();
    if (ImGui::Button("Revert") && Library.RevertSource())
    {
        PendingLivePreviewApply = false;
        ProceduralDirty = false;
        SynchronizeSource();
    }
    RevertExtent = Extent();
    ImGui::SameLine();
    // 📝 Saving is the only edit-path event that re-renders a catalogue tile (baked atlas contract).
    if (ImGui::Button("Save") && Library.SaveMaterial() && InvalidateThumbnail)
        InvalidateThumbnail(Library.QuerySelection());
    SaveExtent = Extent();
    if (ImGui::Button("Duplicate")) Duplicating = true;
    if (Library.QueryModified()) ImGui::TextWrapped("Unsaved edits. Save or Revert; drafts survive switching materials.");
    else if (Library.QueryParkedEdits()) ImGui::TextWrapped("%u other material(s) hold parked edits.", (unsigned)Library.QueryParkedCount());
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
}

void MaterialLibraryPanel::ConstructInspector()
{
    if (Library.QuerySelection().empty()) { ImGui::TextUnformatted("Select a material"); return; }
    if (SourceVisible) { ConstructSourceEditor(); return; }
    RoughnessRecorded = false;
    CoatRoughnessRecorded = false;
    InspectorLayerContext = 0;
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    const auto *Entry = QuerySelectedMaterial(Library);
    ConstructMaterialCaption(At(20, 19).x, At(20, 19).y, 18 * Scale, 0xd3d3c8, "Material inspector", static_cast<ImFont *>(HeadingTypeface));
    const auto Image = QueryThumbnail ? QueryThumbnail(Library.QuerySelection()) : MaterialPreviewImage{};
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(20, 65), At(69, 114), EncodeMaterialInk(0x22221d), 16 * Scale);
    if (Image.Texture) Ink->AddImage(ImTextureRef(Image.Texture), At(22, 67), At(67, 112),
        ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    ConstructMaterialCaption(At(80, 71).x, At(80, 71).y, 18 * Scale, 0xdadacc, Entry ? Entry->Name.c_str() : "Material", static_cast<ImFont *>(HeadingTypeface));
    ConstructMaterialCaption(At(90, 100).x, At(90, 100).y, 7 * Scale, 0x777766, Entry ? Entry->Label.c_str() : "LAYERED MATERIAL");
    ImGui::PushFont(nullptr, 11 * 1.302f * Scale);
    const auto Selection = EncodeContentPath(Library.QuerySelection());
    if (ConstructMaterialPill("##Favourite", ImVec2(Origin.x + Width - 40 * Scale, At(0, 76).y), ImVec2(27 * Scale, 27 * Scale)))
    {
        if (!Favourites.erase(Selection)) Favourites.insert(Selection);
    }
    ConstructIcon(IconSymbol::EditorFavourite, Origin.x + Width - 34 * Scale, At(0, 82).y, 15 * Scale, Favourites.contains(Selection) ? 0xc5d2aa : 0x858577);
    ImGui::PopFont();
    // 📝 Properties and layers share one scroll: recipe cards first, then the layer stack with per-layer
    // cards, channel routing, and procedural controls. No mode toggle — the whole material stays visible.
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    ConstructMaterialCaption(At(21, 147).x, At(21, 147).y, 9 * Scale, 0x8b8d79, "PROPERTIES & LAYERS");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(At(17, 191));
    ImGui::BeginChild("Material properties", ImVec2(Width - 34 * Scale, std::max(40.f, Bottom - At(0, 191).y - 94 * Scale)));
    try
    {
        auto Definition = DecodeSurfaceMaterial(Library.QuerySource());
        bool Changed = false, Rebaked = false;
        {
            // 📝 The scroll child already insets 17*Scale from the panel frame, but the cards themselves
            // used to span that child edge-to-edge, so they sat flush against both the panel border and
            // the scrollbar with no visible gap. A small symmetric inset gives every card real breathing
            // room from both edges.
            const float CardInset = 8 * Scale;
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + CardInset, ImGui::GetCursorScreenPos().y));
            const auto Start = ImGui::GetCursorScreenPos();
            const float CardWidth = std::max(110.f, ImGui::GetContentRegionAvail().x - CardInset);
            ConstructMaterialCaption(Start.x + 3 * Scale, Start.y + 8 * Scale, 7 * Scale, 0x8b8d79, "MATERIAL-AWARE CONTROLS");
            ConstructMaterialCaption(Start.x + 3 * Scale, Start.y + 37 * Scale, 17 * Scale, 0xbabbab,
                Entry && !Entry->RecipeTitle.empty() ? Entry->RecipeTitle.c_str() : "Layered material", static_cast<ImFont *>(HeadingTypeface));
            const char *Description = Entry && !Entry->RecipeCaption.empty() ? Entry->RecipeCaption.c_str() : "Physical surface parameters and independent clearcoat.";
            const float DescriptionSize = 9 * 1.302f * Scale;
            const float DescriptionHeight = ImGui::GetFont()->CalcTextSizeA(DescriptionSize, FLT_MAX, CardWidth - 6 * Scale, Description).y;
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), DescriptionSize, ImVec2(Start.x + 3 * Scale, Start.y + 67 * Scale),
                EncodeMaterialInk(0x868676), Description, nullptr, CardWidth - 6 * Scale);
            float CardY = Start.y + std::max(104.f * Scale, 82.f * Scale + DescriptionHeight);
            // 📝 Properties view (the HTML designer panel). The layer stack is what the code does; the
            // properties are the editable values projected into it, so the stratum/channel routing cards
            // are replaced by the recipe's own grouped controls. Section order mirrors RecipeInspector:
            // colour & finish, optional overlays, construction, then surface detail.
            auto *Profile = Entry ? QueryMaterialRecipeProfile(Entry->Recipe.RecipeId) : nullptr;
            // 📝 The scratch overlay is a property of the saved material, not of the profile: the staged
            // density on the scratch channel is the single source of truth, so the toggle round-trips
            // through the .material source with no separate flag to keep in sync.
            bool ScratchesEnabled = false;
            for (const auto &StratumChannels : Definition.StratumChannels)
                for (const auto &Channel : StratumChannels)
                    if (!Channel.Op.empty())
                        if (const auto Density = Channel.Params.find("scratchDensity"); Density != Channel.Params.end())
                            ScratchesEnabled = ScratchesEnabled || Density->second > 0;
            const auto Controls = Entry ? QueryMaterialRecipeControls(*Entry, ScratchesEnabled)
                : std::vector<MaterialRecipeControl>{};
            // 📝 Finish colours come from the profile (label + address), not from the catalogue, so the
            // native panel names each swatch exactly like the HTML.
            std::vector<std::pair<std::string, std::string>> Colours;
            if (Profile) Colours = Profile->Colours;
            // 📝 Data-driven, not per-profile: any recipe that bundles a "sheen" target (every fabric and
            // leather sheen slider does) gets a live "Sheen colour" swatch, so the fuzz rim tint is always
            // reachable without hand-listing every weave/leather profile here.
            const bool SupportsSheen = std::any_of(Controls.begin(), Controls.end(), [](const MaterialRecipeControl &Control)
            {
                return Control.Key == "sheen" || std::any_of(Control.Targets.begin(), Control.Targets.end(),
                    [](const MaterialRecipeTarget &Target) { return Target.Key == "sheen"; });
            });
            if (SupportsSheen) Colours.emplace_back("sheenColor", "Sheen colour");
            std::vector<std::string> Groups{"finish"};
            for (const auto &Control : Controls)
                if (std::find(Groups.begin(), Groups.end(), Control.Group) == Groups.end()) Groups.push_back(Control.Group);
            // 📝 The scratch card must exist while the overlay is off, otherwise its toggle is
            // unreachable and the optional cuts can never be turned on.
            if (Profile && Profile->SupportsMetalScratches &&
                std::find(Groups.begin(), Groups.end(), "scratches") == Groups.end())
                Groups.push_back("scratches");
            auto RenderColourField = [&](const std::string &Key, const std::string &Label)
            {
                ImGui::PushID(Key.c_str());
                ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 4 * Scale, 10 * Scale,
                    0xa3a393, Label.c_str());
                ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 22 * Scale));
                ImGui::SetNextItemWidth(CardWidth - 34 * Scale);
                // 📝 Three colour homes: the live fabric sheen tint (fuzz rim, read/written straight to the
                // stratum), a procedural channel that owns the key as a text param (led/fruit tint, sheet
                // colour) that edits the live uniform, and everything else which edits the stratum albedo.
                const bool SheenField = Key == "sheenColor";
                const MaterialProceduralChannel *TextOwner = nullptr;
                if (!SheenField)
                    for (const auto &StratumChannels : Definition.StratumChannels)
                        for (const auto &Channel : StratumChannels)
                            if (!Channel.Op.empty() && Channel.Texts.contains(Key)) TextOwner = &Channel;
                float Spectrum[3]{1, 1, 1};
                if (SheenField)
                {
                    auto &Sheen = Definition.Scattering.Illumination.Strata[InspectorLayerContext].Sheen;
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = Sheen[Channel] <= .0031308f ? 12.92f * Sheen[Channel] :
                            1.055f * std::pow(Sheen[Channel], 1.f / 2.4f) - .055f;
                }
                else if (TextOwner)
                {
                    std::string_view Hex = TextOwner->Texts.at(Key);
                    if (Hex.size() == 7 && Hex[0] == '#')
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Spectrum[Channel] = float(std::strtoul(std::string(Hex.substr(1 + Channel * 2, 2)).c_str(), nullptr, 16)) / 255.f;
                }
                else
                {
                    // 📝 Stratum albedo is the authored colour for every value-only family.
                    auto &Albedo = Definition.Scattering.Illumination.Strata[0].AlbedoRoughness;
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = Albedo[Channel] <= .0031308f ? 12.92f * Albedo[Channel] :
                            1.055f * std::pow(Albedo[Channel], 1.f / 2.4f) - .055f;
                }
                if (ImGui::ColorEdit3("##Colour", Spectrum, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoLabel |
                    ImGuiColorEditFlags_NoTooltip))
                {
                    bool Written = false;
                    if (SheenField)
                    {
                        std::array<float, 3> Linear{};
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Linear[Channel] = Spectrum[Channel] <= .04045f ? Spectrum[Channel] / 12.92f :
                                std::pow((Spectrum[Channel] + .055f) / 1.055f, 2.4f);
                        Written = Library.ConfigureStratumColour(InspectorLayerContext, Linear, "SheenColor");
                    }
                    else if (TextOwner)
                    {
                        char Encoded[8];
                        std::snprintf(Encoded, sizeof Encoded, "#%02x%02x%02x",
                            unsigned(std::lround(std::clamp(Spectrum[0], 0.f, 1.f) * 255.f)),
                            unsigned(std::lround(std::clamp(Spectrum[1], 0.f, 1.f) * 255.f)),
                            unsigned(std::lround(std::clamp(Spectrum[2], 0.f, 1.f) * 255.f)));
                        // 📝 Locate the owning stratum so the pool edit reaches the right channel.
                        for (uint32_t Stratum = 0; Stratum < Definition.Scattering.Illumination.Configuration[0] && !Written; ++Stratum)
                            for (const auto &Channel : Definition.StratumChannels[Stratum])
                                if (!Channel.Op.empty() && Channel.Texts.contains(Key))
                                    Written = Library.ConfigureProceduralText(Stratum, Channel.Name, Key, Encoded);
                        if (Written) ProceduralDirty = true;
                    }
                    else
                    {
                        std::array<float, 3> Linear{};
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Linear[Channel] = Spectrum[Channel] <= .04045f ? Spectrum[Channel] / 12.92f :
                                std::pow((Spectrum[Channel] + .055f) / 1.055f, 2.4f);
                        Written = Library.ConfigureStratumColour(0, Linear);
                    }
                    Changed = Written || Changed;
                }
                ImGui::PopID();
            };
            auto RenderControl = [&](const MaterialRecipeControl &Control)
            {
                float Position = Control.Position;
                // 📝 Direct controls carry no mapped targets — the control key IS the coefficient.
                // Requiring targets here disabled every direct slider (detail scale, orange-peel
                // scale, texture scale, …) behind a false "not ported yet" tooltip.
                bool Supported = Control.Direct
                    ? QueryRecipeCoefficient(Definition, Control.Key).has_value()
                    : !Control.Targets.empty();
                for (const auto &Target : Control.Targets) Supported = QueryRecipeCoefficient(Definition, Target.Key).has_value() && Supported;
                if (Supported && Control.Direct)
                    Position = *QueryRecipeCoefficient(Definition, Control.Key);
                else if (Supported)
                {
                    const auto &Target = Control.Targets.front();
                    const float Coefficient = *QueryRecipeCoefficient(Definition, Target.Key);
                    Position = std::abs(Target.Maximum - Target.Minimum) > .000001f ?
                        std::clamp((Coefficient - Target.Minimum) / (Target.Maximum - Target.Minimum), 0.f, 1.f) : Control.Position;
                }
                else if (Control.Direct) Position = QueryRecipeCoefficient(Definition, Control.Key).value_or(Control.Position);
                // 📝 Extending controls grow their domain to whatever the material currently holds, so a
                // saved value above the shipped range still displays and edits instead of clamping.
                float NativeMinimum = Control.Minimum, NativeMaximum = Control.Maximum;
                if (Control.Extend)
                {
                    const float Held = QueryRecipeCoefficient(Definition, Control.Key).value_or(Control.Minimum);
                    NativeMaximum = std::max(NativeMaximum, Held);
                    if (Held > 0) NativeMinimum = std::min(NativeMinimum, Held);
                }
                bool RoughnessLimited = false;
                for (const auto &Target : Control.Targets)
                    if (Target.Key == "roughness" || Target.Key == "coatRoughness")
                    {
                        if (Control.Direct) NativeMinimum = std::max(NativeMinimum, .045f);
                        else if (std::abs(Target.Maximum - Target.Minimum) > .000001f)
                        {
                            const float Boundary = (.045f - Target.Minimum) / (Target.Maximum - Target.Minimum);
                            if (Target.Maximum > Target.Minimum) NativeMinimum = std::max(NativeMinimum, Boundary);
                            else NativeMaximum = std::min(NativeMaximum, Boundary);
                        }
                        RoughnessLimited = NativeMinimum != Control.Minimum || NativeMaximum != Control.Maximum;
                    }
                Supported = Supported && NativeMinimum <= NativeMaximum;
                ImGui::BeginDisabled(!Supported);
                const char *Unavailable = !Supported ? "This recipe parameter is retained, but no native parameter is bound to it yet." :
                    RoughnessLimited ? "The native preview supports roughness of 0.045 or higher; this control is bounded to valid physical values." : nullptr;
                if (ConstructProperty(Control.Caption.c_str(), NativeMinimum, NativeMaximum, Position,
                    Control.Direct ? Control.Unit.c_str() : "%", Control.Logarithmic, Control.Step, Unavailable, CardWidth - 34 * Scale))
                {
                    bool PaintChanged = false;
                    const auto ApplyTarget = [&](const std::string &Key, float Coefficient)
                    {
                        if (Key == "emissionStrength")
                        {
                            Changed = Library.ConfigureEmissionStrength(Coefficient) || Changed;
                            return;
                        }
                        if (Definition.Scattering.Paint.Configuration[3] && Key == "flakeLayers")
                        {
                            Definition.Scattering.Paint.Configuration[1] = uint32_t(std::clamp(std::round(Coefficient), 1.f, 4.f));
                            PaintChanged = true;
                            return;
                        }
                        if (Definition.Scattering.Paint.Configuration[3] && QueryPaintCoefficient(Definition.Scattering.Paint, Key))
                        {
                            *QueryPaintCoefficient(Definition.Scattering.Paint, Key) = Coefficient;
                            PaintChanged = true;
                            return;
                        }
                        if (const auto Pool = QueryRecipePoolLocation(Definition, Key))
                        {
                            // 📝 Recipe sliders drive the shared pool, so every linked channel rebakes from
                            // one gesture. The roughness ceiling scalar follows the pool to preserve the
                            // ORM scale.
                            Changed = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, Key, Coefficient) || Changed;
                            if (Key == "roughness")
                            {
                                float Lift = .3f;
                                for (const auto &Entry : Definition.StratumChannels[Pool->Stratum])
                                    if (Entry.Name == Pool->Channel) Lift = QueryProceduralOpCeilingLift(Entry.Op);
                                Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "Roughness", std::min(1.f, Coefficient + Lift)) || Changed;
                            }
                            // 📝 The weave bake only shades the static albedo nap; the fuzz rim visible under
                            // the key light and the baked environment comes from the live sheen BRDF, so the
                            // same "sheen"/"sheenRoughness" recipe sliders also drive the stratum directly.
                            if (Key == "sheenRoughness")
                                Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "SheenRoughness", Coefficient) || Changed;
                            if (Key == "sheen")
                                Changed = Library.ConfigureStratumCoefficient(Pool->Stratum, "SheenIntensity", Coefficient) || Changed;
                            ProceduralDirty = true;
                            return;
                        }
                        const uint32_t Index = Key.starts_with("coat") ? 1 : 0;
                        const char *Field = Key == "roughness" || Key == "coatRoughness" ? "Roughness" :
                            Key == "metalness" ? "Metallic" : Key == "ior" || Key == "coatIor" ? "Ior" :
                            Key == "coat" ? "Coverage" : Key == "specular" ? "Specular" :
                            Key == "heightStrength" ? "HeightStrength" :
                            Key == "normalStrength" ? "NormalStrength" : "Anisotropy";
                        Changed = Library.ConfigureStratumCoefficient(Index, Field,
                            std::string_view(Field) == "Roughness" ? std::max(.045f, Coefficient) : Coefficient) || Changed;
                    };
                    if (Control.Direct)
                        ApplyTarget(Control.Key, Position);
                    else
                        for (const auto &Target : Control.Targets)
                        {
                            const float Coefficient = Target.Minimum + Position * (Target.Maximum - Target.Minimum);
                            ApplyTarget(Target.Key, Target.Integer ? std::round(Coefficient) : Coefficient);
                        }
                    if (PaintChanged) Changed = Library.ConfigurePaint(Definition.Scattering.Paint) || Changed;
                }
                ImGui::EndDisabled();
                if (!Control.Hint.empty() && Supported)
                {
                    ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 2 * Scale, 8 * Scale,
                        0x7b7b6b, Control.Hint.c_str());
                    ImGui::Dummy(ImVec2(1, 14 * Scale));
                }
            };
            // 📝 Layer categorization: every recipe control is projected onto the stratum its
            // coefficient actually edits (shared pool first, then the paint block, then stratum
            // scalars), so the inspector reads as the physical layer stack — base slab with its
            // flakes/grains under the coat with its thickness/roughness/IOR — instead of mixing
            // strata inside one finish card. Recipe groups survive as cards within each layer.
            const uint32_t LayerCount = std::clamp<uint32_t>(Definition.Scattering.Illumination.Configuration[0], 1, 8);
            const auto QueryKeyStratum = [&](const std::string &Key) -> uint32_t
            {
                if (const auto Pool = QueryRecipePoolLocation(Definition, Key)) return Pool->Stratum;
                if (Definition.Scattering.Paint.Configuration[3] &&
                    (Key == "flakeLayers" || QueryPaintCoefficient(Definition.Scattering.Paint, Key))) return 0;
                return Key.starts_with("coat") ? 1u : 0u;
            };
            std::vector<uint32_t> ControlStrata;
            std::set<std::pair<uint32_t, std::string>> CoveredScalars;
            const auto MarkCovered = [&](const std::string &Key)
            {
                if (Key == "roughness") CoveredScalars.emplace(QueryKeyStratum(Key), "Roughness");
                else if (Key == "coatRoughness") CoveredScalars.emplace(1, "Roughness");
                else if (Key == "metalness") CoveredScalars.emplace(0, "Metallic");
                else if (Key == "ior") CoveredScalars.emplace(0, "Ior");
                else if (Key == "coatIor") CoveredScalars.emplace(1, "Ior");
                else if (Key == "coat") CoveredScalars.emplace(1, "Coverage");
            };
            for (const auto &Control : Controls)
            {
                uint32_t Stratum = LayerCount - 1;
                bool Assigned = false;
                const auto Consider = [&](const std::string &Key)
                {
                    Stratum = std::min(Stratum, std::min(QueryKeyStratum(Key), LayerCount - 1));
                    Assigned = true;
                    MarkCovered(Key);
                };
                if (Control.Direct) Consider(Control.Key);
                for (const auto &Target : Control.Targets) Consider(Target.Key);
                ControlStrata.push_back(Assigned ? Stratum : 0);
            }
            std::vector<uint32_t> ColourStrata;
            for (const auto &[Key, Label] : Colours)
            {
                uint32_t Stratum = 0;
                for (uint32_t Index = 0; Index < LayerCount; ++Index)
                    for (const auto &Channel : Definition.StratumChannels[Index])
                        if (!Channel.Op.empty() && Channel.Texts.contains(Key)) Stratum = Index;
                ColourStrata.push_back(Stratum);
            }
            const auto ScratchPool = QueryRecipePoolLocation(Definition, "scratchDensity");
            const uint32_t ScratchLayer = std::min(ScratchPool ? ScratchPool->Stratum : 0u, LayerCount - 1);
            // 📝 Stratum scalars no recipe control covers stay editable on their own layer card,
            // so every layer's roughness/metalness/IOR/coverage (and dielectric thickness) is
            // reachable even when the HTML recipe never exposed it.
            struct LayerScalar { const char *Caption; const char *Field; float Minimum, Maximum; float *Value; };
            std::vector<std::vector<LayerScalar>> ScalarsByLayer(LayerCount);
            if (Profile)
                for (uint32_t Index = 0; Index < LayerCount; ++Index)
                {
                    auto &Stratum = Definition.Scattering.Illumination.Strata[Index];
                    auto &Scalars = ScalarsByLayer[Index];
                    if (!CoveredScalars.contains({Index, "Roughness"}))
                        Scalars.push_back({"Roughness", "Roughness", .045f, 1.f, &Stratum.AlbedoRoughness[3]});
                    if (!CoveredScalars.contains({Index, "Metallic"}))
                        Scalars.push_back({"Metallic", "Metallic", 0.f, 1.f, &Stratum.Optics[0]});
                    if (!CoveredScalars.contains({Index, "Ior"}))
                        Scalars.push_back({"IOR", "Ior", 1.f, 2.5f, &Stratum.Optics[1]});
                    if (Index > 0 && !CoveredScalars.contains({Index, "Coverage"}))
                        Scalars.push_back({"Coverage", "Coverage", 0.f, 1.f, &Stratum.Optics[2]});
                    if (Stratum.Classification[0] == 1)
                        Scalars.push_back({"Thickness", "Thickness", 0.f, 4.f, &Stratum.AbsorptionThickness[3]});
                }
            struct InspectorCardSlot { uint32_t Layer; std::string Group; bool FirstOfLayer; };
            std::vector<InspectorCardSlot> CardSlots;
            for (uint32_t Layer = 0; Layer < LayerCount; ++Layer)
            {
                bool FirstOfLayer = true;
                const auto AppendSlot = [&](const std::string &GroupName)
                {
                    CardSlots.push_back({Layer, GroupName, FirstOfLayer});
                    FirstOfLayer = false;
                };
                for (const auto &Group : Groups)
                {
                    bool HasControls = false;
                    for (size_t Index = 0; Index < Controls.size(); ++Index)
                        HasControls = HasControls || (Controls[Index].Group == Group && ControlStrata[Index] == Layer);
                    bool HasColours = false;
                    if (Group == "finish")
                        for (const uint32_t Stratum : ColourStrata) HasColours = HasColours || Stratum == Layer;
                    if (HasControls || HasColours || (Group == "finish" && !Profile) ||
                        (Group == "scratches" && Layer == ScratchLayer))
                        AppendSlot(Group);
                }
                if (!ScalarsByLayer[Layer].empty()) AppendSlot("scalars");
            }
            for (const auto &CardSlot : CardSlots)
            {
                const uint32_t Layer = CardSlot.Layer;
                const std::string &Group = CardSlot.Group;
                const std::string CardKey = "L" + std::to_string(Layer) + "|" + Group;
                if (CardSlot.FirstOfLayer)
                {
                    const char *Subtitle = Layer == 0 ? "Base" : Layer == LayerCount - 1 ? "Coat" : "Interlayer";
                    const std::string LayerCaption = "LAYER " + std::to_string(Layer + 1);
                    ConstructMaterialCaption(Start.x + 3 * Scale, CardY + 2 * Scale, 10 * Scale, 0xd8d8c8,
                        LayerCaption.c_str(), static_cast<ImFont *>(HeadingTypeface));
                    ConstructMaterialCaption(Start.x + 3 * Scale + ImGui::CalcTextSize(LayerCaption.c_str()).x +
                        16 * Scale, CardY + 3 * Scale, 8 * Scale, 0x8b8d79, Subtitle);
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(Start.x + 3 * Scale, CardY + 22 * Scale),
                        ImVec2(Start.x + CardWidth - 3 * Scale, CardY + 22 * Scale), EncodeMaterialInk(0xffffff, 26));
                    CardY += 30 * Scale;
                    ImGui::SetCursorScreenPos(ImVec2(Start.x, CardY));
                    ImGui::Dummy(ImVec2(CardWidth, 1));
                }
                std::vector<const MaterialRecipeControl *> GroupControls;
                for (size_t Index = 0; Index < Controls.size(); ++Index)
                    if (Controls[Index].Group == Group && ControlStrata[Index] == Layer)
                        GroupControls.push_back(&Controls[Index]);
                std::vector<std::pair<std::string, std::string>> LayerColours;
                for (size_t Index = 0; Index < Colours.size(); ++Index)
                    if (ColourStrata[Index] == Layer) LayerColours.push_back(Colours[Index]);
                const auto &LayerScalars = ScalarsByLayer[Layer];
                const bool ScalarsCard = Group == "scalars";
                const bool Finish = Group == "finish";
                const bool Flakes = Group == "flakes" && Definition.Scattering.Paint.Configuration[3];
                const bool Scratches = Group == "scratches";
                const uint32_t PaletteColumns = uint32_t(std::max(1.f, std::floor((CardWidth / Scale - 28) / 29)));
                // 📝 Full colour-ramp editor: the swatch grid always reserves one extra tile for the "+"
                // add button (unless already at the 12-stop cap), Ramp mode additionally shows a gradient
                // preview bar above the grid and a "Stop position" slider below (counted into Rows, not
                // ControlsTop, so it reuses the same 84px row rhythm as every other paint control).
                const uint32_t PaletteStopCount = Flakes ? std::clamp(Definition.Scattering.Paint.Configuration[3],
                    1u, uint32_t(Definition.Scattering.Paint.Palette.size())) : 0;
                const uint32_t PaletteTileCount = Flakes ? std::min(PaletteStopCount + (PaletteStopCount < 12 ? 1u : 0u), 12u) : 0;
                const uint32_t PaletteRows = Flakes ? (PaletteTileCount + PaletteColumns - 1) / std::max(1u, PaletteColumns) : 1;
                const bool FlakeRampMode = Flakes && Definition.Scattering.Paint.Configuration[2] == 1;
                const float ControlsTop = Finish ? (LayerColours.empty() ? 62 : 62 + float(LayerColours.size()) * 46) :
                    Flakes ? (22 + 30 + (FlakeRampMode ? 30.f : 0.f) + float(PaletteRows) * 29 + 10 + 34) : 62;
                // 📝 Card height must match what Finish actually renders: a profiled recipe only draws its
                // own GroupControls (no fallback), while an unprofiled material falls back to the 6 generic
                // sliders (Roughness/Metallic/Anisotropy/Specular/Normal/Height strength). The previous fixed
                // "+3" padded profiled cards with dead space and undercounted the unprofiled fallback, which
                // let the next card's background start before this card's controls actually ended.
                const float Rows = ScalarsCard ? float(LayerScalars.size()) :
                    Finish ? float(GroupControls.empty() ? (Profile ? 0 : 6) : GroupControls.size()) :
                    float(GroupControls.size()) + (FlakeRampMode ? 1.f : 0.f);
                const bool Collapsed = FoldedInspectorCards.contains(CardKey);
                const float CardHeight = (Collapsed ? 52.f : ControlsTop + Rows * 84 + 48) * Scale;
                PixelSpace Surface;
                (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale);
                const auto SavedPalette = ControlKit::Palette();
                auto CardPalette = SavedPalette;
                CardPalette.Inset = DecodeMaterialSpectrum(0x222222);
                ControlKit::ConfigurePalette(CardPalette);
                (void)ControlKit::SectionCard(Surface, {Start.x / Scale, CardY / Scale, (Start.x + CardWidth) / Scale, (CardY + CardHeight) / Scale}, 23);
                ControlKit::ConfigurePalette(SavedPalette);
                // 📝 The "LAYER N" heading only draws once, above the first card of that layer, so once a
                // material has several cards per layer (finish + detail + construction + scalars, …) the
                // later cards in the run carry no visible layer identity. Repeat a small tag on every card's
                // header, right-aligned before the fold chevron, so the layer a card belongs to is never
                // ambiguous while scrolling.
                {
                    const std::string LayerTag = "LAYER " + std::to_string(Layer + 1);
                    const float LayerTagSize = 8 * Scale;
                    const float LayerTagWidth = ImGui::GetFont()->CalcTextSizeA(LayerTagSize * 1.302f, FLT_MAX, 0, LayerTag.c_str()).x;
                    ConstructMaterialCaption(Start.x + CardWidth - 46 * Scale - LayerTagWidth, CardY + 21 * Scale,
                        LayerTagSize, 0x6b6d5f, LayerTag.c_str());
                }
                ConstructIcon(Finish ? IconSymbol::EditorSliders : Flakes ? IconSymbol::TextureFlakes :
                    Scratches ? IconSymbol::EditorTexture : ScalarsCard ? IconSymbol::EditorSliders : IconSymbol::TextureClearcoat,
                    Start.x + 17 * Scale, CardY + 18 * Scale, 16 * Scale);
                ConstructMaterialCaption(Start.x + 42 * Scale, CardY + 20 * Scale, 11 * Scale, 0xcacabd,
                    Finish ? (Entry && Entry->Category == "Paint" ? "Paint color & finish" : "Color & finish") :
                    Flakes ? "Metallic flakes" : Scratches ? (Profile && Profile->SupportsMetalScratches ? "Metal scratches" : "Scratches") :
                    ScalarsCard ? "Layer properties" :
                    Group == "detail" ? "Surface detail" : Group == "construction" ? "Construction" :
                    Group == "grains" ? "Sand grains" : Group == "strata" ? "Sediment bands" : Group.c_str(),
                    static_cast<ImFont *>(HeadingTypeface));
                ImGui::PushID(CardKey.c_str());
                ImGui::SetCursorScreenPos(ImVec2(Start.x + 6 * Scale, CardY + 6 * Scale));
                if (ImGui::InvisibleButton("Fold inspector card", ImVec2(CardWidth - 12 * Scale, 39 * Scale)))
                {
                    if (!FoldedInspectorCards.erase(CardKey)) FoldedInspectorCards.insert(CardKey);
                }
                ConstructIcon(Collapsed ? IconSymbol::EditorChevronRight : IconSymbol::EditorChevronDown,
                    Start.x + CardWidth - 31 * Scale, CardY + 20 * Scale, 12 * Scale, 0x858577);
                if (Collapsed)
                {
                    ImGui::PopID();
                    CardY += CardHeight + 15 * Scale;
                    ImGui::SetCursorScreenPos(ImVec2(Start.x, CardY));
                    ImGui::Dummy(ImVec2(CardWidth, 1));
                    continue;
                }
                InspectorLayerContext = Layer;
                ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + 52 * Scale));
                if (Finish)
                {
                    for (const auto &[Key, Label] : LayerColours) RenderColourField(Key, Label);
                    for (const auto *Control : GroupControls) RenderControl(*Control);
                    // 📝 Materials with no ported recipe profile (a fresh or unrecognised material) keep
                    // the generic structured scalars, so their physical coefficients stay editable.
                    if (!Profile)
                    {
                        // 📝 No recipe profile: the generic structured scalars render per layer, so
                        // upper strata stay editable instead of being stranded behind stratum 0.
                        auto &Stratum = Definition.Scattering.Illumination.Strata[Layer];
                        if (ConstructProperty("Roughness", .045f, 1.f, Stratum.AlbedoRoughness[3], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "Roughness", Stratum.AlbedoRoughness[3]) || Changed;
                        if (ConstructProperty("Metallic", 0.f, 1.f, Stratum.Optics[0], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "Metallic", Stratum.Optics[0]) || Changed;
                        if (ConstructProperty("Anisotropy", -.9f, .9f, Definition.Scattering.DirectionalOptics[Layer][0], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "Anisotropy", Definition.Scattering.DirectionalOptics[Layer][0]) || Changed;
                        if (ConstructProperty("Specular", 0.f, 1.f, Definition.Scattering.DirectionalOptics[Layer][3], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "Specular", Definition.Scattering.DirectionalOptics[Layer][3]) || Changed;
                        if (ConstructProperty("Normal strength", 0.f, 4.f, Definition.Scattering.StratumNormal[Layer][0], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "NormalStrength", Definition.Scattering.StratumNormal[Layer][0]) || Changed;
                        if (ConstructProperty("Height strength", 0.f, 4.f, Definition.Scattering.StratumNormal[Layer][1], "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, "HeightStrength", Definition.Scattering.StratumNormal[Layer][1]) || Changed;
                    }
                    ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 4 * Scale, 8 * Scale,
                        0x7b7b6b, "Material-specific ranges. Physical values stay in balance.");
                }
                else if (Scratches)
                {
                    // 📝 Toggle, not a checkbox: the overlay is opt-in and the base finish stays unchanged.
                    bool Enabled = ScratchesEnabled;
                    const auto ToggleOrigin = ImGui::GetCursorScreenPos();
                    ScratchExtent = {ToggleOrigin.x, ToggleOrigin.y, ToggleOrigin.x + 132 * Scale,
                        ToggleOrigin.y + 30 * Scale};
                    if (ConstructMaterialToggle("Enable scratches", Enabled, Scale))
                    {
                    {
                        ScratchesEnabled = Enabled;
                        bool Written = false;
                        if (Enabled)
                        {
                            // 📝 Value-only metals carry no channel yet, so the overlay attaches the
                            // scratch field to the relief and ORM slots; the Albedo scalar stays the
                            // authored reflectance. Seeded params come from the recipe, which stages the
                            // Alloy scratch defaults (density 6) for exactly this purpose.
                            const bool HasField = std::any_of(Definition.StratumChannels[0].begin(), Definition.StratumChannels[0].end(),
                                [](const auto &Channel) { return !Channel.Op.empty() && Channel.Name != "Albedo"; });
                            if (!HasField)
                            {
                                Written = Library.ConfigureStratumChannel(0, "Normal", "scratchedMetal") || Written;
                                Written = Library.ConfigureStratumChannel(0, "Orm", "scratchedMetal") || Written;
                                // 📝 Re-read after attaching: the pool lookup needs the fresh channel block,
                                // and the recipe stages the Alloy scratch defaults for exactly this purpose.
                                Definition = DecodeSurfaceMaterial(Library.QuerySource());
                            }
                            const auto Pool = QueryRecipePoolLocation(Definition, "scratchDensity");
                            if (Pool)
                            {
                                const auto Staged = Entry ? Entry->Recipe.Scalars.find("scratchDensity") : Entry->Recipe.Scalars.end();
                                Written = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, "scratchDensity",
                                    Staged != Entry->Recipe.Scalars.end() ? Staged->second : 6.0) || Written;
                            }
                        }
                        else if (const auto Pool = QueryRecipePoolLocation(Definition, "scratchDensity"))
                            Written = Library.ConfigureProceduralParam(Pool->Stratum, Pool->Channel, "scratchDensity", 0.0) || Written;
                        Changed = Written || Changed;
                        if (Written) ProceduralDirty = true;
                    }
                    }
                    ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 6 * Scale, 8 * Scale,
                        0x7b7b6b, "Optional surface cuts. The base finish stays unchanged when off.");
                    if (ScratchesEnabled)
                        for (const auto *Control : GroupControls) RenderControl(*Control);
                }
                else if (ScalarsCard)
                {
                    for (auto &Scalar : ScalarsByLayer[Layer])
                        if (ConstructProperty(Scalar.Caption, Scalar.Minimum, Scalar.Maximum, *Scalar.Value, "%", false, 0, nullptr, CardWidth - 34 * Scale))
                            Changed = Library.ConfigureStratumCoefficient(Layer, Scalar.Field, *Scalar.Value) || Changed;
                    ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 4 * Scale, 8 * Scale,
                        0x7b7b6b, "Physical coefficients of this layer with no recipe control.");
                }
                else
                {
                    if (Flakes)
                    {
                        // 📝 Flake colours are a genuine stop-based ramp/palette editor (add and remove
                        // stops, drag a blended gradient preview, pick which of Single/Ramp/Palette mode
                        // the flake particles sample - Configuration[2]), not just a fixed-size row of
                        // swatches: a single static row could never express "two colours blended" or
                        // "five colours drawn at random per particle" as distinct, editable concepts.
                        auto &Paint = Definition.Scattering.Paint;
                        const uint32_t PaletteCount = std::clamp(Paint.Configuration[3], 1u, uint32_t(Paint.Palette.size()));
                        FlakeColourSelection = std::min(FlakeColourSelection, PaletteCount - 1);
                        const auto ToSrgb = [](float Linear) { return Linear <= .0031308f ? 12.92f * Linear :
                            1.055f * std::pow(std::max(Linear, 0.f), 1.f / 2.4f) - .055f; };
                        const auto ToLinear = [](float Srgb) { return Srgb <= .04045f ? Srgb / 12.92f :
                            std::pow((Srgb + .055f) / 1.055f, 2.4f); };
                        const auto StopInk = [&](uint32_t Index)
                        {
                            const auto &Stop = Paint.Palette[Index];
                            return IM_COL32(uint8_t(std::clamp(ToSrgb(Stop[0]), 0.f, 1.f) * 255 + .5f),
                                uint8_t(std::clamp(ToSrgb(Stop[1]), 0.f, 1.f) * 255 + .5f),
                                uint8_t(std::clamp(ToSrgb(Stop[2]), 0.f, 1.f) * 255 + .5f), 255);
                        };
                        // 📝 Shared by the "+" add control regardless of mode: find the single largest gap
                        // between consecutive stop positions (treating 0 and 1 as implicit bounds) and insert
                        // a new stop at its midpoint, in sorted order - this keeps Palette[] monotonically
                        // ordered by position at all times, which Ramp mode's own validation requires, so
                        // switching into Ramp mode later never trips on stops collected while browsing
                        // Palette or Single mode.
                        const auto AddStop = [&]()
                        {
                            if (PaletteCount >= 12) return;
                            float Previous = 0.f, BestGap = 0.f, BestPosition = 1.f;
                            for (uint32_t Index = 0; Index <= PaletteCount; ++Index)
                            {
                                const float Current = Index < PaletteCount ? Paint.Palette[Index][3] : 1.f;
                                if (Current - Previous > BestGap) { BestGap = Current - Previous; BestPosition = (Current + Previous) * .5f; }
                                Previous = Current;
                            }
                            uint32_t InsertAt = PaletteCount;
                            for (uint32_t Index = 0; Index < PaletteCount; ++Index)
                                if (Paint.Palette[Index][3] > BestPosition) { InsertAt = Index; break; }
                            for (uint32_t Index = PaletteCount; Index > InsertAt; --Index) Paint.Palette[Index] = Paint.Palette[Index - 1];
                            Paint.Palette[InsertAt] = {.26f, .26f, .26f, BestPosition};
                            Paint.Configuration[3] = PaletteCount + 1;
                            FlakeColourSelection = InsertAt;
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        };
                        const auto RemoveStop = [&]()
                        {
                            if (PaletteCount <= 1) return;
                            const uint32_t Remove = std::min(FlakeColourSelection, PaletteCount - 1);
                            for (uint32_t Index = Remove; Index + 1 < PaletteCount; ++Index) Paint.Palette[Index] = Paint.Palette[Index + 1];
                            Paint.Palette[PaletteCount - 1] = {};
                            Paint.Configuration[3] = PaletteCount - 1;
                            FlakeColourSelection = Remove > 0 ? Remove - 1 : 0;
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        };
                        const auto CursorTop = ImGui::GetCursorScreenPos().y;
                        ConstructMaterialCaption(Start.x + 17 * Scale, CursorTop + 4 * Scale, 10 * Scale, 0xa3a393, "Flake colors");
                        char CountLabel[8]; std::snprintf(CountLabel, sizeof(CountLabel), "%u/12", PaletteCount);
                        ConstructMaterialCaption(Start.x + CardWidth - 44 * Scale, CursorTop + 4 * Scale, 8 * Scale, 0x777766, CountLabel);
                        ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CursorTop + 22 * Scale));
                        // 📝 Mode is independent of the stored stop count, exactly like the HTML reference:
                        // switching modes never discards stops, it only changes how the engine samples them
                        // (Palette = one at random per particle, Ramp = blended by position, Single = stop 0).
                        const std::array<std::pair<const char *, uint32_t>, 3> Modes{{{"Palette", 0}, {"Ramp", 1}, {"Single", 2}}};
                        const float ModeWidth = (CardWidth - 34 * Scale - 8 * Scale) / 3;
                        for (size_t Index = 0; Index < Modes.size(); ++Index)
                        {
                            if (Index) ImGui::SameLine(0, 4 * Scale);
                            const bool Active = Paint.Configuration[2] == Modes[Index].second;
                            ImGui::PushStyleColor(ImGuiCol_Button, EncodeMaterialInk(Active ? 0x3a3e30 : 0x1d1d1b, 255));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EncodeMaterialInk(Active ? 0x454a37 : 0x27271f, 255));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, EncodeMaterialInk(Active ? 0x454a37 : 0x27271f, 255));
                            ImGui::PushStyleColor(ImGuiCol_Text, EncodeMaterialInk(Active ? 0xd9dcc4 : 0x8d8d7f, 255));
                            if (ImGui::Button((std::string(Modes[Index].first) + "##FlakeColourMode").c_str(), ImVec2(ModeWidth, 22 * Scale)) && !Active)
                            {
                                Paint.Configuration[2] = Modes[Index].second;
                                Changed = Library.ConfigurePaint(Paint) || Changed;
                            }
                            ImGui::PopStyleColor(4);
                        }
                        ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 30 * Scale));
                        if (FlakeRampMode)
                        {
                            // 📝 Blended gradient preview: one filled segment per pair of consecutive stops
                            // (already position-sorted - see AddStop), plus a small marker per stop so a click
                            // anywhere on the bar selects the nearest stop for the hex/position row below.
                            const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
                            const float BarWidth = CardWidth - 34 * Scale, BarHeight = 18 * Scale;
                            auto *Ink = ImGui::GetWindowDrawList();
                            Ink->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + BarWidth, BarOrigin.y + BarHeight), EncodeMaterialInk(0x101010), 4 * Scale);
                            for (uint32_t Index = 0; Index + 1 < PaletteCount; ++Index)
                            {
                                const float X0 = BarOrigin.x + Paint.Palette[Index][3] * BarWidth, X1 = BarOrigin.x + Paint.Palette[Index + 1][3] * BarWidth;
                                Ink->AddRectFilledMultiColor(ImVec2(X0, BarOrigin.y), ImVec2(X1, BarOrigin.y + BarHeight),
                                    StopInk(Index), StopInk(Index + 1), StopInk(Index + 1), StopInk(Index));
                            }
                            if (PaletteCount == 1)
                                Ink->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + BarWidth, BarOrigin.y + BarHeight), StopInk(0), 4 * Scale);
                            Ink->AddRect(BarOrigin, ImVec2(BarOrigin.x + BarWidth, BarOrigin.y + BarHeight), EncodeMaterialInk(0xffffff, 26), 4 * Scale);
                            for (uint32_t Index = 0; Index < PaletteCount; ++Index)
                            {
                                const float MarkerX = BarOrigin.x + Paint.Palette[Index][3] * BarWidth;
                                ImGui::SetCursorScreenPos(ImVec2(MarkerX - 6 * Scale, BarOrigin.y + BarHeight + 2 * Scale));
                                ImGui::PushID(int(Index) + 100);
                                if (ImGui::InvisibleButton("##RampStop", ImVec2(12 * Scale, 8 * Scale))) FlakeColourSelection = Index;
                                ImGui::PopID();
                                const bool Selected = Index == FlakeColourSelection;
                                Ink->AddTriangleFilled(ImVec2(MarkerX, BarOrigin.y + BarHeight + 2 * Scale),
                                    ImVec2(MarkerX - 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale),
                                    ImVec2(MarkerX + 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale),
                                    EncodeMaterialInk(Selected ? 0xd9dcc4 : 0x6c6c60, 255));
                            }
                            ImGui::SetCursorScreenPos(ImVec2(BarOrigin.x, BarOrigin.y + BarHeight + 12 * Scale));
                        }
                        const float SwatchPitch = 29 * Scale;
                        const ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
                        for (uint32_t Index = 0; Index < PaletteCount; ++Index)
                        {
                            const uint32_t Column = Index % std::max(1u, PaletteColumns), Row = Index / std::max(1u, PaletteColumns);
                            ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + Column * SwatchPitch, RowOrigin.y + Row * SwatchPitch));
                            ImGui::PushID(int(Index));
                            auto &Stop = Paint.Palette[Index];
                            float Spectrum[3]{ToSrgb(Stop[0]), ToSrgb(Stop[1]), ToSrgb(Stop[2])};
                            const bool Selected = Index == FlakeColourSelection;
                            if (Selected)
                            {
                                const auto SwatchOrigin = ImGui::GetCursorScreenPos();
                                ImGui::GetWindowDrawList()->AddRect(ImVec2(SwatchOrigin.x - 2 * Scale, SwatchOrigin.y - 2 * Scale),
                                    ImVec2(SwatchOrigin.x + 26 * Scale, SwatchOrigin.y + 26 * Scale), EncodeMaterialInk(0xd9dcc4, 220), 3 * Scale, 0, 2 * Scale);
                            }
                            if (ImGui::ColorEdit3("##FlakeColour", Spectrum, ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoTooltip))
                            {
                                for (size_t Channel = 0; Channel < 3; ++Channel) Stop[Channel] = ToLinear(Spectrum[Channel]);
                                Changed = Library.ConfigurePaint(Paint) || Changed;
                            }
                            if (ImGui::IsItemClicked() || ImGui::IsItemActivated()) FlakeColourSelection = Index;
                            ImGui::PopID();
                        }
                        {
                            const uint32_t AddColumn = PaletteCount % std::max(1u, PaletteColumns), AddRow = PaletteCount / std::max(1u, PaletteColumns);
                            if (PaletteCount < 12)
                            {
                                ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + AddColumn * SwatchPitch, RowOrigin.y + AddRow * SwatchPitch));
                                ImGui::PushStyleColor(ImGuiCol_Button, EncodeMaterialInk(0x1d1d1b, 255));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EncodeMaterialInk(0x27271f, 255));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, EncodeMaterialInk(0x27271f, 255));
                                if (ImGui::Button("+##AddFlakeColour", ImVec2(24 * Scale, 24 * Scale))) AddStop();
                                ImGui::PopStyleColor(3);
                            }
                        }
                        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + PaletteRows * SwatchPitch + 10 * Scale));
                        // 📝 Selected-stop row: a real editable swatch + hex field (click the swatch for the
                        // full picker popup, or type a hex code directly) for whichever swatch/marker was
                        // last clicked - exactly the same ColorEdit3(DisplayHex) pattern the main Paint
                        // colour field above already uses - plus the only way to shrink the ramp back down
                        // (disabled at the 1-stop floor so the flake tint can never be configured into an
                        // empty, unsampleable palette). A plain text caption here (the previous approach)
                        // only ever displayed the hex value; it could never actually be edited.
                        auto &Selected = Paint.Palette[FlakeColourSelection];
                        float SelectedSpectrum[3]{ToSrgb(Selected[0]), ToSrgb(Selected[1]), ToSrgb(Selected[2])};
                        const ImVec2 RowY = ImGui::GetCursorScreenPos();
                        ImGui::SetNextItemWidth(CardWidth - 34 * Scale - 70 * Scale);
                        ImGui::PushID("FlakeSelectedColour");
                        if (ImGui::ColorEdit3("##FlakeSelectedColour", SelectedSpectrum,
                            ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoTooltip))
                        {
                            for (size_t Channel = 0; Channel < 3; ++Channel) Selected[Channel] = ToLinear(SelectedSpectrum[Channel]);
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        }
                        ImGui::PopID();
                        ImGui::SetCursorScreenPos(ImVec2(RowY.x + CardWidth - 34 * Scale - 60 * Scale, RowY.y));
                        ImGui::BeginDisabled(PaletteCount <= 1);
                        if (ImGui::Button("Remove##FlakeColour", ImVec2(60 * Scale, 24 * Scale))) RemoveStop();
                        ImGui::EndDisabled();
                        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowY.y + 30 * Scale));
                    }
                    for (const auto *Control : GroupControls) RenderControl(*Control);
                    if (FlakeRampMode)
                    {
                        // 📝 Counted as the extra Rows entry added above, so it inherits the same 84px rhythm
                        // (label + numeric field + track) as every other paint slider in this card.
                        auto &Paint = Definition.Scattering.Paint;
                        const uint32_t Selected = std::min(FlakeColourSelection, Paint.Configuration[3] - 1);
                        const float Minimum = Selected > 0 ? Paint.Palette[Selected - 1][3] : 0.f;
                        const float Maximum = Selected + 1 < Paint.Configuration[3] ? Paint.Palette[Selected + 1][3] : 1.f;
                        float Position = Paint.Palette[Selected][3];
                        if (ConstructProperty("Stop position", Minimum, Maximum, Position, "%", false, 0, nullptr, CardWidth - 34 * Scale))
                        {
                            Paint.Palette[Selected][3] = Position;
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        }
                    }
                    if (Flakes)
                    {
                        ConstructMaterialCaption(Start.x + 17 * Scale, ImGui::GetCursorScreenPos().y + 4 * Scale, 8 * Scale,
                            0x7b7b6b, "Object-space particles. Filtered on the GPU.");
                    }
                }
                ImGui::PopID();
                CardY += CardHeight + 15 * Scale;
                ImGui::SetCursorScreenPos(ImVec2(Start.x, CardY));
                ImGui::Dummy(ImVec2(CardWidth, 1));
            }
        }
        InspectorLayerContext = 0;
        if (Changed)
        {
            SynchronizeSource();
            PendingLivePreviewApply = true;
        }
        // 📝 Procedural gestures rebake once at committed-cache resolution when the gesture ends; the
        // rebake applies itself, so the pending structured apply is consumed with it. Disk persistence
        // stays an explicit Save.
        if (ProceduralDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ProceduralDirty = false;
            if (Library.RebakeProcedurals(512))
            {
                PendingLivePreviewApply = false;
                Rebaked = true;
            }
        }
        // 📝 Structured controls apply to the live preview when the gesture ends; disk persistence remains an explicit Save.
        if (PendingLivePreviewApply && !Rebaked && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (Library.ApplySource()) PendingLivePreviewApply = false;
        }
    }
    catch (const std::exception &Failure) { ImGui::TextWrapped("%s", Failure.what()); }
    ImGui::EndChild();
    if (Library.QueryModified() || !Library.QueryDiagnostic().empty())
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 17 * Scale, Bottom - 88 * Scale));
        ConstructActions();
        ImGui::SameLine();
        if (ImGui::Button("Source")) SourceVisible = true;
    }
    else
    {
        ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
        if (ConstructMaterialPill("+  Save as preset", ImVec2(Origin.x + 17 * Scale, Bottom - 63 * Scale), ImVec2(Width - 34 * Scale, 34 * Scale))) Duplicating = true;
        ImGui::PopFont();
    }
}

void MaterialLibraryPanel::ConstructSourceEditor()
{
    if (ImGui::Button("Properties")) SourceVisible = false;
    if (Library.QuerySelection().empty())
    {
        ImGui::TextUnformatted("Select a material");
        ImGui::TextWrapped(".material files contain versioned TOML. Texture references use the selected library directory as ContentDirectory.");
        return;
    }
    ImGui::Text("%s%s", EncodeContentPath(Library.QuerySelection()).c_str(), Library.QueryModified() ? " *" : "");
    const auto *Applied = Library.QueryApplied();
    if (Applied)
    {
        ImGui::Text("Applied: %s / %u layers", Applied->Name.c_str(), Applied->Scattering.Illumination.Configuration[0]);

    }
    ImGui::Separator();
    const float Height = std::max(24.f, ImGui::GetContentRegionAvail().y-150*QueryInterfaceScale());
    if (ImGui::InputTextMultiline("##MaterialSource", SourceText.data(), SourceText.size(), ImVec2(-1, Height)))
        Library.ReplaceSource(SourceText.data());
    ConstructActions();
}

void MaterialLibraryPanel::ConstructCreation()
{
    if ((Creating || Duplicating) && !ImGui::IsPopupOpen("Material destination")) ImGui::OpenPopup("Material destination");
    const MaterialModalScope ModalScope(QueryInterfaceScale());
    if (ImGui::BeginPopupModal("Material destination", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Duplicating ? "Duplicate current authored material" : "Create material");
        ImGui::TextWrapped("Destination is relative to ContentDirectory; existing files are never overwritten.");
        ImGui::InputText("Destination .material", DestinationText.data(), DestinationText.size());
        if (!Duplicating) ImGui::InputText("Material name", NameText.data(), NameText.size());
        if (ImGui::Button(Duplicating ? "Create duplicate" : "Create"))
        {
            const auto Destination = DecodeContentPath(DestinationText.data());
            const bool Complete = Duplicating ? Library.DuplicateMaterial(Destination) : Library.CreateMaterial(Destination, NameText.data());
            if (Complete)
            {
                SynchronizeSource();
                Creating = Duplicating = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { Creating = Duplicating = false; ImGui::CloseCurrentPopup(); }
        if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
        ImGui::EndPopup();
    }
}

void MaterialLibraryPanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    const float Scale = QueryInterfaceScale();
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 79 * Scale));
    ImGui::SetNextWindowViewport(Viewport->ID);
    const auto SavedPalette = ControlKit::Palette();
    auto Palette = SavedPalette;
    Palette.Panel = DecodeMaterialSpectrum(0x121212);
    Palette.Inset = DecodeMaterialSpectrum(0x1c1c1a);
    Palette.Stroke = {1, 1, 1, .032f};
    Palette.Text = DecodeMaterialSpectrum(0xdeded5);
    Palette.TextDim = DecodeMaterialSpectrum(0x85857b);
    Palette.TextFaint = DecodeMaterialSpectrum(0x777766);
    Palette.SliderTrack = DecodeMaterialSpectrum(0x171717);
    Palette.SliderFill = DecodeMaterialSpectrum(0x454545);
    Palette.SliderThumb = DecodeMaterialSpectrum(0xd7d7ce);
    ControlKit::ConfigurePalette(Palette);
    const auto Colour = [](ColorQuad Spectrum) { return ImVec4(Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.11f, .11f, .10f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.133f, .133f, .133f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.18f, .18f, .17f, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.035f, .035f, .035f, 1));
    ImGui::PushStyleColor(ImGuiCol_Header, Colour(Palette.Selected));
    for (const auto Slot : {ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected})
        ImGui::PushStyleColor(Slot, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(.035f, .035f, .035f, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(.035f, .035f, .035f, 1));
    const auto SavedColourButtonPosition = ImGui::GetStyle().ColorButtonPosition;
    ImGui::GetStyle().ColorButtonPosition = ImGuiDir_Left;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_DockingSeparatorSize, 2 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * Scale, 8.49f * Scale));
    // 📝 STB pixel height spans ascent/descent; CSS sizes the em. DM Sans hhea spans 1302/1000 em.
    ImGui::PushFont(static_cast<ImFont *>(ReferenceTypeface), 10 * 1.302f * Scale);
    ImGui::Begin("TextureWorkspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    auto *Ink = ImGui::GetWindowDrawList();
    const auto Origin = Viewport->WorkPos;
    const float Width = Viewport->WorkSize.x;
    const auto *Entry = QuerySelectedMaterial(Library);
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), EncodeMaterialInk(0x0b0b0b));
    Ink->AddRectFilled(ImVec2(Origin.x, Origin.y + 43 * Scale), ImVec2(Origin.x + Width, Origin.y + 79 * Scale), EncodeMaterialInk(0x111111));
    ConstructMaterialCaption(Origin.x + 16 * Scale, Origin.y + 17 * Scale, 10 * Scale, 0xcecec2, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    const float NameWidth = ImGui::GetFont()->CalcTextSizeA(10 * 1.302f * Scale, 300 * Scale, 0, Entry ? Entry->Name.c_str() : "Material explorer").x;
    ConstructMaterialCaption(Origin.x + 22 * Scale + NameWidth, Origin.y + 20 * Scale, 7 * Scale, 0x767669, ".material");
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    if (ConstructMaterialPill("Material", ImVec2(Origin.x + Width - 300 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale), true)) SourceVisible = false;
    ImGui::BeginDisabled(!ActivatePattern);
    if (ConstructMaterialPill("Pattern", ImVec2(Origin.x + Width - 374 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivatePattern) ActivatePattern();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!ActivateStamp);
    if (ConstructMaterialPill("Stamp", ImVec2(Origin.x + Width - 448 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivateStamp) ActivateStamp();
    ImGui::EndDisabled();
    if (ConstructMaterialPill("Open Library", ImVec2(Origin.x + Width - 225 * Scale, Origin.y + 8 * Scale), ImVec2(94 * Scale, 27 * Scale))) DirectoryRequested = true;
    if (ConstructMaterialPill("Refresh", ImVec2(Origin.x + Width - 120 * Scale, Origin.y + 8 * Scale), ImVec2(100 * Scale, 27 * Scale)))
    {
        (void)Library.Refresh();
        if (!Library.QueryModified() && !Library.QuerySelection().empty())
        {
            const auto Selection = Library.QuerySelection();
            if (Library.OpenMaterial(Selection)) SynchronizeSource();
        }
        if (InvalidateThumbnails) InvalidateThumbnails();
    }
    ConstructIcon(IconSymbol::EditorMaterial, Origin.x + 17 * Scale, Origin.y + 52 * Scale, 15 * Scale);
    ConstructMaterialCaption(Origin.x + 40 * Scale, Origin.y + 56 * Scale, 9 * Scale, 0x8f8f81, "Automotive essentials     >     Material explorer");
    if (ImGui::BeginPopupContextWindow("Material document operations"))
    {
        if (ImGui::MenuItem("Source")) SourceVisible = true;
        if (ImGui::MenuItem("Properties")) SourceVisible = false;
        if (ImGui::MenuItem("New material")) Creating = true;
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    if (DirectoryRequested) { ImGui::OpenPopup("Open material library"); DirectoryRequested = false; }
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    {
        const MaterialModalScope DirectoryModalScope(Scale);
        if (ImGui::BeginPopupModal("Open material library", nullptr, ImGuiWindowFlags_NoResize))
        {
            const auto Dialogue = ImGui::GetCursorScreenPos();
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 4 * Scale, 7 * Scale, 0x8b8d79, "MATERIAL LIBRARY");
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 26 * Scale, 17 * Scale, 0xdcdccd, "Open material library",
                static_cast<ImFont *>(HeadingTypeface));
            ImGui::Dummy(ImVec2(1, 58 * Scale));
            ImGui::TextWrapped("Switch to a different .material catalogue folder.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##Directory", "Material library folder", DirectoryText.data(), DirectoryText.size());
            if (Library.QueryModified() || Library.QueryParkedEdits())
                ImGui::TextWrapped("Save or revert current edits before switching libraries.");
            if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 2 * ButtonWidth - ButtonGap - 20 * Scale);
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 32 * Scale))) ImGui::CloseCurrentPopup();
            ImGui::SameLine(0, ButtonGap);
            ImGui::BeginDisabled(Library.QueryModified() || Library.QueryParkedEdits());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.29f,.42f,.25f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.35f,.49f,.30f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.24f,.35f,.20f,1));
            if (ImGui::Button("Open", ImVec2(ButtonWidth, 32 * Scale)) && ConfigureDirectory(DecodeContentPath(DirectoryText.data()))) ImGui::CloseCurrentPopup();
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
    ConstructCreation();
    if (ClosingRequested && !ImGui::IsPopupOpen("Unsaved material")) ImGui::OpenPopup("Unsaved material");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    {
        const MaterialModalScope UnsavedModalScope(Scale);
        if (ImGui::BeginPopupModal("Unsaved material", nullptr, ImGuiWindowFlags_NoResize))
        {
            const auto Dialogue = ImGui::GetCursorScreenPos();
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 4 * Scale, 7 * Scale, 0x8b8d79, "UNSAVED MATERIAL");
            ConstructMaterialCaption(Dialogue.x, Dialogue.y + 26 * Scale, 17 * Scale, 0xdcdccd, "Save your changes?",
                static_cast<ImFont *>(HeadingTypeface));
            ImGui::Dummy(ImVec2(1, 58 * Scale));
            const auto *Unsaved = QuerySelectedMaterial(Library);
            ImGui::TextWrapped("\u201c%s\u201d has unsaved edits. Save keeps them; Discard loses them.",
                Unsaved ? Unsaved->Name.c_str() : "This material");
            if (Library.QueryParkedEdits()) ImGui::TextWrapped("%u other material(s) hold parked edits; only the current material is saved here.",
                (unsigned)Library.QueryParkedCount());
            if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 3 * ButtonWidth - 2 * ButtonGap - 16 * Scale);
            if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 32 * Scale))) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0, ButtonGap);
            if (ImGui::Button("Discard", ImVec2(ButtonWidth, 32 * Scale))) CloseAuthorized = true;
            ImGui::SameLine(0, ButtonGap);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.29f,.42f,.25f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.35f,.49f,.30f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.24f,.35f,.20f,1));
            if (ImGui::Button("Save and close", ImVec2(ButtonWidth, 32 * Scale)) && Library.SaveMaterial())
            {
                if (InvalidateThumbnail) InvalidateThumbnail(Library.QuerySelection());
                CloseAuthorized = true;
            }
            ImGui::PopStyleColor(3);
            if (CloseAuthorized) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
    ImGui::End();
    if (!Panels.QuerySeated())
    {
        const float Width = std::max(1.f, Viewport->WorkSize.x), Scale = QueryInterfaceScale();
        const float Left = std::min(.30f, 278 * Scale / Width), Right = std::min(.34f, 338 * Scale / Width);
        (void)Panels.ConfigureColumns({{"MaterialLibrary", Left}, {"MaterialViewport", 1 - Left - Right}, {"MaterialInspector", Right}});
    }
    Panels.ConstructInterface(QueryInterfaceScale());
    ConstructFullscreenPreview();
    ImGui::PopFont();
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(17);
    ControlKit::ConfigurePalette(SavedPalette);
    ImGui::GetStyle().ColorButtonPosition = SavedColourButtonPosition;
}
} // namespace Frontier
