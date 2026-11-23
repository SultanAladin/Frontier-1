/*==========================================================================================================================================
                                                         MATERIALLIBRARYPANEL.H
==========================================================================================================================================*/
// 🧩 Share the native material catalogue and source editor between standalone Texture Studio and a future Frontier workspace.

#pragma once
#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>
#include <map>
#include <set>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL LIBRARY PANEL
//------------------------------------------------------------------------------------------------------------------------

struct MaterialPreviewImage
{
    uint64_t Texture = 0;
    float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
};

class MaterialLibraryPanel final : public ActivationSequence
{
  public:
    MaterialLibraryPanel();
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool SelectMaterial(const std::filesystem::path &RelativePath);
    void ConstructInterface();
    void ReplaceSource(std::string Source);
    void ConfigureInterfaceScale(float Scale) noexcept;
    void ConfigureTypeface(void *Typeface, void *Light = nullptr) noexcept { ReferenceTypeface = Typeface; HeadingTypeface = Light; }
    void ConfigureSourceView(bool Visible) noexcept { SourceVisible = Visible; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    std::function<MaterialPreviewImage(const std::filesystem::path &)> QueryThumbnail;
    std::function<MaterialPreviewImage()> QueryPreview;
    std::function<PlanePoint(PlanePoint)> EvaluateNavigationDelta;
    std::function<void(const MaterialPreviewConfiguration &)> ConfigurePreview;
    std::function<void(uint32_t)> ConfigurePreviewExtent;
    // 📝 Environment archive catalogue for the viewport card: display names in catalogue order,
    // one sphere-preview tile per entry, and a folder rescan (the import path).
    std::function<std::vector<std::string>()> QueryEnvironmentNames;
    std::function<MaterialPreviewImage(size_t)> QueryEnvironmentPreview;
    std::function<void()> RescanEnvironments;
    // 📝 Last rendered environment tile extents (popup open), for headless pointer gestures.
    [[nodiscard]] PlaneExtent QueryEnvironmentPreviewExtent(size_t Index) const noexcept
    {
        return Index < EnvironmentPreviewExtents.size() ? EnvironmentPreviewExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] MaterialPreviewConfiguration QueryPreviewConfiguration() const noexcept { return Inspection; }
    [[nodiscard]] PlaneExtent QueryPreviewControlExtent(size_t Index) const noexcept
    {
        return Index < InspectionExtents.size() ? InspectionExtents[Index] : PlaneExtent{};
    }
    std::function<bool(IconSymbol, PlanePoint, float, ColorQuad)> PresentIcon;
    std::function<std::string(const std::filesystem::path &)> QueryThumbnailDiagnostic;
    std::function<void()> InvalidateThumbnails;
    // 📝 Single-tile rebake request, issued only after the material is saved to disk.
    std::function<void(const std::filesystem::path &)> InvalidateThumbnail;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    std::function<void(const std::vector<std::filesystem::path> &)> RequestThumbnails;
    bool RequestClose() noexcept;
    bool ConsumeClose() noexcept;
    [[nodiscard]] PlaneExtent QueryApplyExtent() const noexcept { return ApplyExtent; }
    [[nodiscard]] PlaneExtent QueryFullscreenPreviewExtent() const noexcept { return FullscreenPreviewExtent; }
    [[nodiscard]] bool QueryFullscreenPreview() const noexcept { return FullscreenPreview; }
    [[nodiscard]] PlaneExtent QuerySaveExtent() const noexcept { return SaveExtent; }
    [[nodiscard]] PlaneExtent QueryRoughnessExtent() const noexcept { return RoughnessExtent; }
    // 📝 First designer slider of the coat layer (layer 2), recorded beside the base-layer one so
    // headless gestures can drive a coat control without assuming card geometry.
    [[nodiscard]] PlaneExtent QueryCoatRoughnessExtent() const noexcept { return CoatRoughnessExtent; }
    // 📝 Optional scratch overlay toggle; recorded from the properties card so headless gestures can
    // drive the same opt-in the user clicks.
    [[nodiscard]] PlaneExtent QueryScratchExtent() const noexcept { return ScratchExtent; }
    [[nodiscard]] PlaneExtent QueryRevertExtent() const noexcept { return RevertExtent; }
    [[nodiscard]] MaterialLibrary &QueryLibrary() noexcept { return Library; }

  private:
    std::array<PlaneExtent, 8> InspectionExtents{};
    std::vector<PlaneExtent> EnvironmentPreviewExtents;
    MaterialPreviewConfiguration Inspection;
    // 📝 Orbit lag: Inspection is the input target; SmoothedInspection eases toward it every frame so the
    // camera glides instead of snapping. Discrete state (shape, environment, exposure) always applies instantly.
    MaterialPreviewConfiguration SmoothedInspection, SentInspection;
    bool SmoothedInspectionReady = false, SentInspectionReady = false;
    void SynchronizePreviewCamera();
    MaterialLibrary Library;
    PanelDockspace Panels;
    bool SourceVisible = false;
    bool CollectionVisible = false;
    bool FullscreenPreview = false;
    bool PendingLivePreviewApply = false, ProceduralDirty = false, RoughnessRecorded = false;
    bool CoatRoughnessRecorded = false;
    uint32_t InspectorLayerContext = 0;
    // 📝 Which Palette[] slot the flake colour ramp editor is currently showing in its selected-stop
    // (hex + remove) row; clamped to the live palette count every frame, so a stale index from a
    // previously open material (or one invalidated by a remove) never reads/writes out of bounds.
    uint32_t FlakeColourSelection = 0;
    uint32_t SentPreviewExtent = 0;
    std::string CategoryFilter = "All materials";
    std::set<std::string> Favourites;
    std::set<std::string> FoldedInspectorCards;
    float InterfaceScale = 1;
    void *ReferenceTypeface = nullptr;
    void *HeadingTypeface = nullptr;
    [[nodiscard]] float QueryInterfaceScale() const noexcept;
    bool DirectoryRequested = false;
    PlaneExtent RoughnessExtent, ScratchExtent;
    PlaneExtent CoatRoughnessExtent;
    void ConstructInspector();
    void ConstructViewport();
    void ConstructFullscreenPreview();
    // 📝 RowWidth defaults to the surrounding content region (correct for the free-floating popup
    // sliders), but every inspector card caller passes its own inset row width explicitly so the
    // right-aligned value readout lands inside the card border instead of the outer panel/scrollbar edge.
    bool ConstructProperty(const char *Caption, float Minimum, float Maximum, float &Position, const char *Unit = "%", bool Logarithmic = false, float Step = 0, const char *Unavailable = nullptr, float RowWidth = -1.f);
    void ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb = 0xa3a393);
    void ConstructActions();
    std::array<char,4096> DirectoryText{}, DestinationText{};
    std::array<char,257> SearchText{}, NameText{};
    std::array<char,32> ColourText{};
    std::vector<char> SourceText;
    PlaneExtent ApplyExtent, SaveExtent, RevertExtent, FullscreenPreviewExtent;
    bool ClosingRequested = false, CloseAuthorized = false;
    bool Creating = false;
    bool Duplicating = false;
    void SynchronizeSource();
    void ConstructCatalogue();
    void ConstructSourceEditor();
    void ConstructCreation();
    void FinalizeContent() noexcept override;
};
} // namespace Frontier
