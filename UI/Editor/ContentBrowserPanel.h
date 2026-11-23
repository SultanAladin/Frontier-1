/*==========================================================================================================================================
                                                          CONTENTBROWSERPANEL.H
==========================================================================================================================================*/
// 🧩 Read-only editor content browsing through the shared directory catalogue and expansion tab.

#pragma once

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/PanelExpansionTab.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CONTENT BROWSER
//------------------------------------------------------------------------------------------------------------------------

class ContentBrowserPanel final : public ActivationSequence
{
  public:
    [[nodiscard]] PlaneExtent QuerySizeExtent() const noexcept
    {
        return SizeExtent;
    }
    explicit ContentBrowserPanel(std::filesystem::path Root = "EngineContent");
    void ConstructInterface(float Scale, bool Enabled) noexcept;
    void SuspendInteraction() noexcept
    {
        Search.Active = CategoryMenu = Focused = false;
        PressedControl.clear();
    }
    void               ConstructHostedInterface(float Scale, bool Enabled) noexcept;
    void               ConfigureOpen(bool Open) noexcept;
    [[nodiscard]] bool QuerySearchActive() const noexcept
    {
        return Search.Active;
    }
    [[nodiscard]] bool QueryGrid() const noexcept
    {
        return Grid;
    }
    [[nodiscard]] ContentDirectory &QueryDirectory() noexcept
    {
        return Directory;
    }
    [[nodiscard]] const ContentDirectory &QueryImportDirectory() const noexcept
    {
        return ImportDirectory;
    }
    [[nodiscard]] const PanelExpansionTab &QueryExpansionTab() const noexcept
    {
        return Expansion;
    }
    [[nodiscard]] PlaneExtent QuerySearchExtent() const noexcept
    {
        return SearchExtent;
    }
    [[nodiscard]] PlaneExtent QueryFilterExtent() const noexcept
    {
        return FilterExtent;
    }
    [[nodiscard]] PlaneExtent QueryViewExtent() const noexcept
    {
        return ViewExtent;
    }
    // 📝 Navigation slots bind the breadcrumb: 0 descends to the content root, 1 ascends to the parent
    // folder; history travels on Alt+Left/Right. Category slots bind the sources rows, with All carried by
    // the Project Library row itself.
    [[nodiscard]] PlaneExtent QueryNavigationExtent(size_t Index) const noexcept
    {
        return Index < NavigationExtents.size() ? NavigationExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryCategoryExtent(size_t Index) const noexcept
    {
        return Index < CategoryExtents.size() ? CategoryExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryEntryExtent(size_t Index) const noexcept;
    [[nodiscard]] PlaneExtent QueryImportEntryExtent(size_t Index) const noexcept;
    [[nodiscard]] bool QueryImportOpen() const noexcept
    {
        return Page == ContentBrowserPage::Import;
    }
    [[nodiscard]] PlaneExtent QueryImportExtent() const noexcept
    {
        return ImportExtent;
    }
    [[nodiscard]] PlaneExtent QueryImportBackExtent() const noexcept
    {
        return ImportBackExtent;
    }
    [[nodiscard]] PlaneExtent QueryBodyExtent() const noexcept
    {
        return BodyExtent;
    }

  protected:
    bool InitializeContent() noexcept override;
    void IntegrateContent(double Δτ) noexcept override;
    void FinalizeContent() noexcept override;

  private:
    enum class ContentBrowserPage : uint32_t
    {
        Library,
        Import
    };
    void ConstructLayout(PixelSpace &Surface, const PlaneExtent &Pane, const ControlPointer &Pointer);
    void RecordSources(PixelSpace &Surface, const PlaneExtent &Extent, const ControlPointer &Pointer);
    void RecordSeekRail(PixelSpace &Surface, const PlaneExtent &Extent, const ControlPointer &Pointer);
    void RecordLattice(PixelSpace &Surface, const PlaneExtent &Extent, const ControlPointer &Pointer);
    void RecordInspector(PixelSpace &Surface, const PlaneExtent &Extent, const ControlPointer &Pointer);
    void RecordImport(PixelSpace &Surface, const PlaneExtent &Extent, const ControlPointer &Pointer);
    void RecordHatch(PixelSpace &Surface, const PlaneExtent &Extent);
    void RecordScrollbar(PixelSpace &Surface, const PlaneExtent &Extent, uint32_t Target, float Span, float &Offset,
                         const ControlPointer &Pointer);
    void IntegrateKeyboard();
    void IntegrateSearch();
    void ResetLocation();
    void ConfirmImport(const std::filesystem::path &Path);
    bool ClickedControl(const char *Tag, const PlaneExtent &Extent, const ControlPointer &Pointer);
    [[nodiscard]] PlaneExtent PhysicalExtent(PlaneExtent Extent) const noexcept;

    std::filesystem::path      Root;
    ContentDirectory           Directory;
    ContentDirectory           ImportDirectory;
    PanelExpansionTab          Expansion{PanelExpansionDirectionCategory::Upward};
    TextEntryState             Search;
    ContentCategory            Category = ContentCategory::All;
    ContentBrowserPage         Page     = ContentBrowserPage::Library;
    PlanePoint                 Origin{};
    float                      Scale              = 1;
    float                      Scroll             = 0;
    float                      AsideOffset        = 0;
    float                      ImportOffset       = 0;
    float                      CellWidth          = 132;
    float                      CellHeight         = 116;
    uint32_t                   Columns            = 1;
    uint32_t                   InspectorTongue    = 0;
    bool                       Grid               = true;
    bool                       Maximized          = false;
    bool                       CategoryMenu       = false;
    bool                       Focused            = false;
    bool                       HeaderContact      = false;
    bool                       HostedPresentation = false;
    uint32_t                   ScrollDrag         = 0;
    float                      ScrollDragOriginY  = 0;
    float                      ScrollDragOffset   = 0;
    std::string                PressedControl;
    std::vector<std::filesystem::path> ImportPlaces;
    PlaneExtent                SearchExtent, ViewExtent, BodyExtent, FilterExtent, SizeExtent, ImportListExtent;
    PlaneExtent                ImportExtent, ImportBackExtent;
    std::array<PlaneExtent, 5> NavigationExtents{};
    std::array<PlaneExtent, 7> CategoryExtents{};
    std::vector<PlaneExtent>   EntryExtents;
    std::vector<PlaneExtent>   ImportEntryExtents;
};
} // namespace Frontier
