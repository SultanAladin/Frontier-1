/*============================================================================================================================================
                                                          CONTENTBROWSERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ContentBrowser drawer, pre-baked ThumbnailSheet rendering, SSD/HDD streaming, workspace masks & ThemeRepository styling

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Input/InputExtension.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] float TargetRatioForExtent(ContentShelfExtent Extent) noexcept
{
    switch (Extent)
    {
        case ContentShelfExtent::Closed:     return 0.0f;
        case ContentShelfExtent::SplitHalf:  return 0.48f;
        case ContentShelfExtent::FullHeight: return 0.94f;
    }
    return 0.0f;
}

[[nodiscard]] ColorRgba BlendRgba(ColorQuad Left, ColorQuad Right, float Ratio, float AlphaScale = 1.0f) noexcept
{
    ColorQuad Mixed = ColorQuad::Blend(Left, Right, std::clamp(Ratio, 0.0f, 1.0f));
    Mixed.Alpha     = std::clamp(Mixed.Alpha * AlphaScale, 0.0f, 1.0f);
    return Mixed.ToRgba8();
}

[[nodiscard]] std::pair<std::string, std::string> SplitUppercaseTileHeading(std::string_view Name)
{
    std::string Upper;
    Upper.reserve(Name.size());
    for (const char Ch : Name)
    {
        Upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(Ch))));
    }
    const std::size_t SpacePos = Upper.find(' ');
    if (SpacePos == std::string::npos)
    {
        return { Upper, {} };
    }
    return { Upper.substr(0u, SpacePos), Upper.substr(SpacePos + 1u) };
}

void PopulateEditBufferFromString(TextEditBuffer& Destination, std::string_view SourceText) noexcept
{
    const bool PriorActive    = Destination.Active;
    Destination.Length        = 0u;
    const std::size_t MaxCopy = std::min<std::size_t>(SourceText.size(), TextEditBuffer::Capacity - 1u);
    for (std::size_t Idx = 0u; Idx < MaxCopy; ++Idx)
    {
        Destination.Text[Idx]    = SourceText[Idx];
        Destination.Restore[Idx] = SourceText[Idx];
    }
    Destination.Text[MaxCopy]    = '\0';
    Destination.Restore[MaxCopy] = '\0';
    Destination.Length           = static_cast<uint32_t>(MaxCopy);
    Destination.Caret            = Destination.Length;
    Destination.SelectionAnchor  = Destination.Caret;
    Destination.Active           = PriorActive;
}

bool ConstructCapsuleControl(
    PixelSpace&                       Canvas,
    Rectangle2D                       Bounds,
    const char*                       Label,
    bool                              Selected,
    const ContentBrowserPointerInput& Input,
    const ControlColorSet&            AppearanceColors,
    float                             FontSize      = 10.0f,
    bool                              AccentPrimary = false,
    ControlIcon                       LeadingIcon   = ControlIcon::Count)
{
    const Point2D Cursor{ Input.CursorX, Input.CursorY };
    const bool    Hovered = Bounds.Contains(Cursor);

    ColorRgba Fill   = AppearanceColors.Inset.ToRgba8();
    ColorRgba Border = AppearanceColors.Stroke.ToRgba8();
    ColorRgba Ink    = AppearanceColors.TextDim.ToRgba8();
    ColorRgba IconInk = Ink;

    if (AccentPrimary)
    {
        Fill    = Hovered
                      ? BlendRgba(AppearanceColors.Accent, AppearanceColors.Primary, 0.16f)
                      : AppearanceColors.Accent.ToRgba8();
        Border  = Fill;
        Ink     = AppearanceColors.AccentInk.ToRgba8();
        IconInk = Ink;
    }
    else if (Selected)
    {
        Fill    = BlendRgba(AppearanceColors.Selected, AppearanceColors.Accent, AppearanceColors.LightSurface ? 0.16f : 0.24f);
        Border  = BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Accent, 0.68f);
        Ink     = AppearanceColors.Text.ToRgba8();
        IconInk = BlendRgba(AppearanceColors.Accent, AppearanceColors.Text, 0.28f);
    }
    else if (Hovered)
    {
        Fill    = AppearanceColors.Raised.ToRgba8();
        Border  = AppearanceColors.StrokeStrong.ToRgba8();
        Ink     = AppearanceColors.Text.ToRgba8();
        IconInk = Ink;
    }

    const float Radius = std::min(14.0f, Bounds.Height * 0.5f);
    Canvas.FillRectangle(Bounds, Fill, Radius);
    Canvas.StrokeRectangle(Bounds, Border, Radius, 1.0f);

    const bool LabelNonEmpty = (Label != nullptr && Label[0] != '\0');
    if (LeadingIcon != ControlIcon::Count && !LabelNonEmpty)
    {
        const float IconSize = std::min(14.0f, Bounds.Height - 10.0f);
        VectorIconRepository::ConstructControl(
            Canvas,
            LeadingIcon,
            VectorIconPlacement{
                Bounds.Center().X - IconSize * 0.5f,
                Bounds.Center().Y - IconSize * 0.5f,
                IconSize,
                IconInk,
                1.95f,
            });
    }
    else if (LeadingIcon != ControlIcon::Count && LabelNonEmpty)
    {
        const float IconSize  = 13.0f;
        const float TextWidth = Canvas.MeasureTextWidth(FontSize, Label);
        const float TotalW    = IconSize + 6.0f + TextWidth;
        const float StartX    = Bounds.X + std::max(8.0f, (Bounds.Width - TotalW) * 0.5f);

        VectorIconRepository::ConstructControl(
            Canvas,
            LeadingIcon,
            VectorIconPlacement{
                StartX,
                Bounds.Center().Y - IconSize * 0.5f,
                IconSize,
                IconInk,
                1.90f,
            });
        Canvas.Text(
            { StartX + IconSize + 6.0f, Bounds.Y + (Bounds.Height - FontSize) * 0.5f - 0.5f },
            FontSize,
            Ink,
            Label);
    }
    else if (LabelNonEmpty)
    {
        Canvas.TextCentered(Bounds, FontSize, Ink, Label);
    }

    return Hovered && Input.Pressed;
}

void ConstructSegmentedTrack(
    PixelSpace&            Canvas,
    Rectangle2D            TrackBounds,
    const ControlColorSet& AppearanceColors) noexcept
{
    const ColorRgba TrackFill = AppearanceColors.LightSurface
                                    ? BlendRgba(AppearanceColors.Inset, AppearanceColors.StrokeStrong, 0.48f)
                                    : BlendRgba(AppearanceColors.Field, AppearanceColors.TabBackground, 0.55f);
    const ColorRgba TrackBorder = AppearanceColors.Stroke.ToRgba8();
    const float     Radius      = TrackBounds.Height * 0.5f;
    Canvas.FillRectangle(TrackBounds, TrackFill, Radius);
    Canvas.StrokeRectangle(TrackBounds, TrackBorder, Radius, 1.0f);
}

bool ConstructSegmentedOption(
    PixelSpace&                       Canvas,
    Rectangle2D                       Bounds,
    const char*                       Label,
    bool                              Selected,
    const ContentBrowserPointerInput& Input,
    const ControlColorSet&            AppearanceColors,
    float                             FontSize    = 9.5f,
    ControlIcon                       LeadingIcon = ControlIcon::Count)
{
    const Point2D Cursor{ Input.CursorX, Input.CursorY };
    const bool    Hovered = Bounds.Contains(Cursor);
    const float   Radius  = Bounds.Height * 0.5f;

    ColorRgba Ink     = AppearanceColors.TextDim.ToRgba8();
    ColorRgba IconInk = Ink;

    if (Selected)
    {
        const ColorRgba ActiveFill   = BlendRgba(AppearanceColors.Selected, AppearanceColors.Accent, AppearanceColors.LightSurface ? 0.22f : 0.28f);
        const ColorRgba ActiveBorder = BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Accent, 0.45f);
        Canvas.FillRectangle(Bounds, ActiveFill, Radius);
        Canvas.StrokeRectangle(Bounds, ActiveBorder, Radius, 1.0f);
        Ink     = AppearanceColors.Text.ToRgba8();
        IconInk = BlendRgba(AppearanceColors.Accent, AppearanceColors.Text, 0.28f);
    }
    else if (Hovered)
    {
        Canvas.FillRectangle(Bounds, AppearanceColors.Raised.Faded(0.65f).ToRgba8(), Radius);
        Ink     = AppearanceColors.Text.ToRgba8();
        IconInk = Ink;
    }

    const bool LabelNonEmpty = (Label != nullptr && Label[0] != '\0');
    if (LeadingIcon != ControlIcon::Count && LabelNonEmpty)
    {
        const float IconSize  = 12.5f;
        const float TextWidth = Canvas.MeasureTextWidth(FontSize, Label);
        const float TotalW    = IconSize + 6.0f + TextWidth;
        const float StartX    = Bounds.X + std::max(8.0f, (Bounds.Width - TotalW) * 0.5f);

        VectorIconRepository::ConstructControl(
            Canvas,
            LeadingIcon,
            VectorIconPlacement{
                StartX,
                Bounds.Center().Y - IconSize * 0.5f,
                IconSize,
                IconInk,
                1.90f,
            });
        Canvas.Text(
            { StartX + IconSize + 6.0f, Bounds.Y + (Bounds.Height - FontSize) * 0.5f - 0.5f },
            FontSize,
            Ink,
            Label);
    }
    else if (LabelNonEmpty)
    {
        Canvas.TextCentered(Bounds, FontSize, Ink, Label);
    }

    return Hovered && Input.Pressed;
}

constexpr std::array<ContentCategory, 15> OrderedRailCategories = {{
    ContentCategory::All,
    ContentCategory::Materials,
    ContentCategory::Colours,
    ContentCategory::Geometry,
    ContentCategory::UVLayouts,
    ContentCategory::Textures,
    ContentCategory::Images,
    ContentCategory::Vectors,
    ContentCategory::Fonts,
    ContentCategory::Simulations,
    ContentCategory::Volumetrics,
    ContentCategory::Particles,
    ContentCategory::Photometry,
    ContentCategory::Audio,
    ContentCategory::Shaders,
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTENTBROWSERPANEL IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ContentBrowserPanel::ContentBrowserPanel()
    : ShelfAxis(ShelfSpring.RegisterAxis(0.0, 240.0, 26.0))
{
    (void)StreamChannelThread.Initialize({}, Threading::StorageHardwareTier::AutoDetect, nullptr);
}

ContentBrowserPanel::~ContentBrowserPanel() noexcept
{
    StreamChannelThread.Finalize();
}

void ContentBrowserPanel::ConfigureShelfExtent(ContentShelfExtent RequestedExtent, bool Immediate) noexcept
{
    TargetExtent       = RequestedExtent;
    const double Ratio = static_cast<double>(TargetRatioForExtent(RequestedExtent));
    if (Immediate)
    {
        ShelfSpring.Axis(ShelfAxis).ConfigurePosition(Ratio);
    }
    else
    {
        ShelfSpring.Axis(ShelfAxis).Depart(Ratio);
    }
}

void ContentBrowserPanel::ConfigureWorkspaceDomain(WorkspaceCategory Workspace) noexcept
{
    ActiveWorkspace   = Workspace;
    RepositoryScrollY = 0.0f;

    // 🔴 Snap active category if newly entered workspace blocks previous selection
    if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, Category))
    {
        Category = ContentCategory::All;
    }

    const auto ValidEntries = ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, ContentCategory::All, OriginScope, SearchBuffer);
    if (!ValidEntries.empty() && std::find(ValidEntries.begin(), ValidEntries.end(), SelectedContentIndex) == ValidEntries.end())
    {
        SelectedContentIndex = ValidEntries.front();
    }
}

bool ContentBrowserPanel::ActivateCategory(ContentCategory NextCategory) noexcept
{
    if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, NextCategory))
    {
        StatusMessage = std::string("Blocked in ") + ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) +
                        " workspace: " + ThumbnailSheetRepository::QueryCategoryTitle(NextCategory);
        return false;
    }
    Category          = NextCategory;
    RepositoryScrollY = 0.0f;
    const auto ValidEntries = ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, Category, OriginScope, SearchBuffer);
    if (!ValidEntries.empty())
    {
        SelectedContentIndex = ValidEntries.front();
    }
    return true;
}

void ContentBrowserPanel::ConfigureOriginFilter(ContentOriginFilter NextOrigin) noexcept
{
    OriginScope       = NextOrigin;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureStandardLayout(StandardRepositoryLayout NextLayout) noexcept
{
    StandardLayout = NextLayout;
}

void ContentBrowserPanel::ConfigureColourLayout(ColourRepositoryLayout NextLayout) noexcept
{
    ColourLayout      = NextLayout;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureColourFilter(ColourFilterGroup NextFilter) noexcept
{
    ColourFilter      = NextFilter;
    RepositoryScrollY = 0.0f;
    const auto Filtered = CodexLibrary.QueryFilteredIndices(ColourFilter, ColourSort, SearchBuffer);
    if (!Filtered.empty() && std::find(Filtered.begin(), Filtered.end(), SelectedColourIndex) == Filtered.end())
    {
        SelectedColourIndex = Filtered.front();
    }
}

void ContentBrowserPanel::ConfigureColourSort(ColourSortOrder NextOrder) noexcept
{
    ColourSort = NextOrder;
}

void ContentBrowserPanel::ConfigureSearchQuery(std::string_view SearchText)
{
    SearchBuffer      = std::string(SearchText);
    RepositoryScrollY = 0.0f;
    PopulateEditBufferFromString(SearchEditor, SearchBuffer);
}

void ContentBrowserPanel::ConfigureStorageHardwareTier(
    Threading::StorageHardwareTier RequestedTier,
    const std::filesystem::path&   MountTarget) noexcept
{
    StreamChannelThread.ConfigureHardwareTierOverride(RequestedTier, MountTarget);
}

void ContentBrowserPanel::ConfigureImportDestination(const ImportIntakeConfiguration& Policy)
{
    ThumbnailRepository.ConfigureImportPolicy(Policy);
}

bool ContentBrowserPanel::SelectColourByName(std::string_view AliasName) noexcept
{
    const std::size_t Found = CodexLibrary.FindEntryByName(AliasName);
    if (Found >= CodexLibrary.QueryEntries().size())
    {
        return false;
    }
    SelectedColourIndex = Found;
    return true;
}

ImportIntakeResult ContentBrowserPanel::IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile)
{
    ImportIntakeResult Result = ThumbnailRepository.IntakeExternalAssetDrop(DroppedSourceFile, ActiveWorkspace);
    StatusMessage             = Result.DiagnosticMessage;
    if (!Result.Accepted)
    {
        return Result;
    }

    if (Result.DetectedCategory == ContentCategory::Colours)
    {
        std::string Diagnostic{};
        std::size_t AddedColourIdx = 0u;
        if (ColourCodex::MatchesSwatchExtension(DroppedSourceFile))
        {
            if (CodexLibrary.LoadSingleSwatchFile(DroppedSourceFile, Diagnostic, &AddedColourIdx))
            {
                SelectedColourIndex = AddedColourIdx;
            }
        }
        else
        {
            (void)CodexLibrary.LoadFromTomlFile(DroppedSourceFile, Diagnostic, true);
        }
    }

    OriginScope          = ContentOriginFilter::Imported;
    Category             = Result.DetectedCategory;
    SelectedContentIndex = Result.RepositoryIndex;
    return Result;
}

void ContentBrowserPanel::ClearColourSlots() noexcept
{
    RegisteredSlots.clear();
}

void ContentBrowserPanel::RegisterColourSlot(const ColourSlotRegistration& Registration)
{
    for (ColourSlotRegistration& Existing : RegisteredSlots)
    {
        if (Existing.SlotIdentifier == Registration.SlotIdentifier)
        {
            Existing = Registration;
            return;
        }
    }
    RegisteredSlots.push_back(Registration);
    if (FocusedSlotId.empty())
    {
        FocusedSlotId = Registration.SlotIdentifier;
    }
}

void ContentBrowserPanel::FocusColourSlot(std::string_view SlotIdentifier) noexcept
{
    FocusedSlotId = std::string(SlotIdentifier);
}

void ContentBrowserPanel::AttachOutlinerFallback(OutlinerPanel* TargetOutliner) noexcept
{
    FallbackOutliner = TargetOutliner;
}

void ContentBrowserPanel::AssignContentActivationCallback(std::function<void(const ContentRepositoryEntry&)> Callback)
{
    ContentActivationCallback = std::move(Callback);
}

bool ContentBrowserPanel::ActivateContentEntryAt(std::size_t EntryIndex)
{
    const ContentRepositoryEntry* Entry = ThumbnailRepository.QueryEntryAt(EntryIndex);
    if (Entry == nullptr)
    {
        return false;
    }
    SelectedContentIndex = EntryIndex;
    StatusMessage        = "Opened " + Entry->DisplayTitle + " (" + Entry->ContainerFormat + ")";
    if (ContentActivationCallback)
    {
        ContentActivationCallback(*Entry);
    }
    return true;
}

void ContentBrowserPanel::AttachThemeRepository(const ThemeRepository* SourceTheme) noexcept
{
    AttachedTheme = SourceTheme;
}

ControlColorSet ContentBrowserPanel::ResolveActiveColorSet() const noexcept
{
    if (AttachedTheme != nullptr)
    {
        return ControlAppearance::ConstructColorSet(*AttachedTheme);
    }
    return ControlAppearance::Colors();
}

float ContentBrowserPanel::ResolveCornerRadius() const noexcept
{
    if (AttachedTheme != nullptr)
    {
        return std::clamp(AttachedTheme->QueryCornerRadius(), 6.0f, 22.0f);
    }
    return 15.0f;
}

bool ContentBrowserPanel::ApplySelectedColourToActiveSlot()
{
    return ApplyColourIndexToSlot(SelectedColourIndex, FocusedSlotId);
}

bool ContentBrowserPanel::ApplyColourIndexToSlot(std::size_t ColourIndex, std::string_view TargetSlotIdentifier)
{
    const ColourCodexEntry* Entry = CodexLibrary.QueryEntryAt(ColourIndex);
    if (Entry == nullptr)
    {
        return false;
    }

    ColourSlotRegistration* ChosenSlot = nullptr;
    if (!TargetSlotIdentifier.empty())
    {
        for (ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.SlotIdentifier == TargetSlotIdentifier)
            {
                ChosenSlot = &Slot;
                break;
            }
        }
    }
    if (ChosenSlot == nullptr && !FocusedSlotId.empty())
    {
        for (ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.SlotIdentifier == FocusedSlotId)
            {
                ChosenSlot = &Slot;
                break;
            }
        }
    }
    if (ChosenSlot == nullptr && !RegisteredSlots.empty())
    {
        ChosenSlot = &RegisteredSlots.front();
    }

    if (ChosenSlot != nullptr)
    {
        if (ChosenSlot->TargetColour != nullptr)
        {
            *ChosenSlot->TargetColour = Entry->FillColour;
        }
        if (ChosenSlot->NotifyColourAssigned)
        {
            ChosenSlot->NotifyColourAssigned(Entry->FillColour, *Entry);
        }
        FocusedSlotId = ChosenSlot->SlotIdentifier;
        StatusMessage = "Applied " + Entry->AliasName + " (" + Entry->HexUpper + ") -> " + ChosenSlot->DisplayTitle;
        return true;
    }

    if (FallbackOutliner != nullptr)
    {
        const auto SelectedTokens = FallbackOutliner->QuerySelectedTokens();
        if (!SelectedTokens.empty())
        {
            if (OutlinerRecordEntry* OutlinerEntry = FallbackOutliner->AccessEntry(SelectedTokens.front()))
            {
                OutlinerEntry->FolderTint = Entry->FillColour;
                StatusMessage             = "Applied " + Entry->AliasName + " (" + Entry->HexUpper + ") -> " + OutlinerEntry->Title;
                return true;
            }
        }
    }
    return false;
}

ContentBrowserReading ContentBrowserPanel::InspectReading() const noexcept
{
    uint32_t PermittedCount = 0u;
    for (const ContentCategory Candidate : OrderedRailCategories)
    {
        if (Candidate != ContentCategory::All &&
            ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, Candidate))
        {
            ++PermittedCount;
        }
    }

    ContentBrowserReading Reading{};
    Reading.ShelfExtent            = TargetExtent;
    Reading.ShelfProgress          = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    Reading.ActiveWorkspace        = ActiveWorkspace;
    Reading.ActiveCategory         = Category;
    Reading.ActiveOrigin           = OriginScope;
    Reading.ActiveColourLayout     = ColourLayout;
    Reading.ActiveStandardLayout   = StandardRepositoryLayout(StandardLayout);
    Reading.ActiveColourFilter     = ColourFilter;
    Reading.ActiveColourSort       = ColourSort;
    Reading.ActiveStorageTier      = StreamChannelThread.QueryResolvedTier();
    Reading.ActiveImportMode       = ThumbnailRepository.QueryImportPolicy().Mode;
    Reading.PermittedCategoryCount = PermittedCount;
    Reading.VisibleEntryCount      = LastVisibleCount;
    Reading.ColourFiltersVisible   = (Category == ContentCategory::Colours);
    Reading.DraggingColourActive   = DraggingColour;
    if (const ColourCodexEntry* Selected = CodexLibrary.QueryEntryAt(SelectedColourIndex))
    {
        Reading.SelectedColourAlias = Selected->AliasName;
    }
    Reading.LastStatusMessage = StatusMessage;
    return Reading;
}

bool ContentBrowserPanel::QueryOpen() const noexcept
{
    return TargetExtent != ContentShelfExtent::Closed || ShelfSpring.Axis(ShelfAxis).Current > 0.005;
}

bool ContentBrowserPanel::QueryBlocksUnderlyingWorkspace() const noexcept
{
    return TargetExtent == ContentShelfExtent::FullHeight && !DraggingColour;
}

Rectangle2D ContentBrowserPanel::QueryNotchTabBounds(Rectangle2D ViewportLogicalBounds) const noexcept
{
    const float Progress   = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    const float ShelfH     = std::round(ViewportLogicalBounds.Height * Progress);
    const float DrawerTopY = ViewportLogicalBounds.Bottom() - ShelfH;
    const float NotchW     = 216.0f;
    const float NotchH     = 22.0f;
    return {
        ViewportLogicalBounds.X + (ViewportLogicalBounds.Width - NotchW) * 0.5f,
        std::max(ViewportLogicalBounds.Y, DrawerTopY - NotchH),
        NotchW,
        NotchH,
    };
}

Rectangle2D ContentBrowserPanel::QuerySearchFieldBounds() const noexcept
{
    return LastSearchBounds;
}

Rectangle2D ContentBrowserPanel::QueryRenderedColourCellBounds(std::size_t ColourIndex) const noexcept
{
    for (const RecordedCellHitbox& Hitbox : LastColourHitboxes)
    {
        if (Hitbox.EntryIndex == ColourIndex)
        {
            return Hitbox.Bounds;
        }
    }
    return {};
}

ColourCodex& ContentBrowserPanel::AccessColourCodex() noexcept
{
    return CodexLibrary;
}

const ColourCodex& ContentBrowserPanel::QueryColourCodex() const noexcept
{
    return CodexLibrary;
}

ThumbnailSheetRepository& ContentBrowserPanel::AccessThumbnailRepository() noexcept
{
    return ThumbnailRepository;
}

const ThumbnailSheetRepository& ContentBrowserPanel::QueryThumbnailRepository() const noexcept
{
    return ThumbnailRepository;
}

Threading::AssetStreamThread& ContentBrowserPanel::AccessStreamThread() noexcept
{
    return StreamChannelThread;
}

const Threading::AssetStreamThread& ContentBrowserPanel::QueryStreamThread() const noexcept
{
    return StreamChannelThread;
}

void ContentBrowserPanel::RenderBottomNotchHandle(
    PixelSpace&            Canvas,
    Rectangle2D            NotchBounds,
    bool                   Hovered,
    const ControlColorSet& AppearanceColors) const noexcept
{
    const float Inset = NotchBounds.Width * 0.09f;
    const std::array<Point2D, 4> Contour = {{
        { NotchBounds.X + Inset, NotchBounds.Y },
        { NotchBounds.Right() - Inset, NotchBounds.Y },
        { NotchBounds.Right(), NotchBounds.Bottom() },
        { NotchBounds.X, NotchBounds.Bottom() },
    }};

    const ColorRgba TabFill = Hovered
                                  ? BlendRgba(AppearanceColors.TabBackground, AppearanceColors.Raised, 0.65f)
                                  : BlendRgba(AppearanceColors.TabBackground, AppearanceColors.Panel, 0.35f);
    const ColorRgba TabStroke = Hovered
                                    ? BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Accent, 0.55f)
                                    : AppearanceColors.StrokeStrong.ToRgba8();
    const ColorRgba TabInk = Hovered
                                 ? AppearanceColors.Text.ToRgba8()
                                 : AppearanceColors.TextDim.ToRgba8();

    Canvas.FillPolygon(Contour, TabFill);
    Canvas.StrokePolyline(Contour, TabStroke, 1.0f, true);

    const float TextW = Canvas.MeasureTextWidth(10.0f, "ContentBrowser");
    const float IconS = 11.0f;
    const float StartX = NotchBounds.Center().X - (IconS + 5.0f + TextW) * 0.5f;
    VectorIconRepository::ConstructNavigation(
        Canvas,
        QueryOpen() ? NavigationIcon::ChevronDown : NavigationIcon::ChevronUp,
        VectorIconPlacement{
            StartX,
            NotchBounds.Center().Y - IconS * 0.5f,
            IconS,
            TabInk,
            2.1f,
        });
    Canvas.Text(
        { StartX + IconS + 5.0f, NotchBounds.Y + (NotchBounds.Height - 10.0f) * 0.5f },
        10.0f,
        TabInk,
        "ContentBrowser");
}

void ContentBrowserPanel::RenderColourSlotHighlights(
    PixelSpace&            Canvas,
    Point2D                Cursor,
    const ControlColorSet& AppearanceColors) const noexcept
{
    if (!DraggingColour)
    {
        return;
    }
    const ColorRgba ActiveRing = AppearanceColors.Accent.ToRgba8();
    const ColorRgba IdleRing   = AppearanceColors.Accent.Faded(0.68f).ToRgba8();
    for (const ColourSlotRegistration& Slot : RegisteredSlots)
    {
        if (Slot.ScreenBounds.Width <= 1.0f || Slot.ScreenBounds.Height <= 1.0f)
        {
            continue;
        }
        const bool Hovered = Slot.ScreenBounds.Contains(Cursor);
        Canvas.StrokeRectangle(
            Slot.ScreenBounds.Inset(-3.0f),
            Hovered ? ActiveRing : IdleRing,
            8.0f,
            Hovered ? 2.2f : 1.35f);
    }
}

void ContentBrowserPanel::RenderColoursRepository(
    PixelSpace&                       Canvas,
    Rectangle2D                       RepositoryArea,
    const ContentBrowserPointerInput& PointerInput,
    const ControlColorSet&            AppearanceColors,
    float                             SectionRadius)
{
    LastColourHitboxes.clear();

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
    std::vector<std::size_t> FilteredIndices;
    const auto AllEntries = CodexLibrary.QueryEntries();
    for (const std::size_t Idx : CodexLibrary.QueryFilteredIndices(ColourFilter, ColourSort, SearchBuffer))
    {
        const ColourCodexEntry& Entry = AllEntries[Idx];
        if (OriginScope == ContentOriginFilter::Engine && !Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        // 📝 Project origin searches both Project colours AND EngineContent stock colours
        if (OriginScope == ContentOriginFilter::Imported && Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        FilteredIndices.push_back(Idx);
    }
    LastVisibleCount = static_cast<uint32_t>(FilteredIndices.size());

    if (FilteredIndices.empty())
    {
        Canvas.TextCentered(RepositoryArea, 13.0f, AppearanceColors.TextDim.ToRgba8(), "No .ColourSwatch entries match active filter");
        return;
    }

    if (ColourLayout == ColourRepositoryLayout::VerticalStrips)
    {
        Canvas.FillRectangle(RepositoryArea, AppearanceColors.SectionSurface.ToRgba8(), SectionRadius);
        Canvas.PushClip(RepositoryArea);

        const float StripCount = static_cast<float>(FilteredIndices.size());
        const float StripW     = std::max(44.0f, RepositoryArea.Width / StripCount);

        for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
        {
            const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
            const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
            const float             LeftX    = RepositoryArea.X + std::floor(static_cast<float>(OrderIdx) * StripW);
            const float             RightX   = (OrderIdx + 1u == FilteredIndices.size())
                                                   ? RepositoryArea.Right()
                                                   : (RepositoryArea.X + std::floor(static_cast<float>(OrderIdx + 1u) * StripW));
            const Rectangle2D StripRect{ LeftX, RepositoryArea.Y, std::max(1.0f, RightX - LeftX), RepositoryArea.Height };

            Canvas.FillRectangle(StripRect, Entry.FillColour, 0.0f);
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry.FillColour);

            if (EntryIdx == SelectedColourIndex)
            {
                Canvas.FillCircle({ StripRect.Center().X, StripRect.Y + 16.0f }, 4.0f, Ink.PrimaryInk);
            }

            const Point2D TextExtent = Canvas.MeasureText(14.5f, Entry.HexCompact.c_str());
            const Point2D RotatedAnchor{
                StripRect.X + (StripRect.Width - TextExtent.Y) * 0.5f,
                StripRect.Bottom() - 18.0f,
            };
            Canvas.TextVerticalUp(RotatedAnchor, 14.5f, Ink.PrimaryInk, Entry.HexCompact.c_str());

            LastColourHitboxes.push_back({ EntryIdx, StripRect });

            if (RepositoryArea.Contains(Cursor) && StripRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    SelectedColourIndex = EntryIdx;
                    (void)ApplyColourIndexToSlot(EntryIdx);
                }
                else if (PointerInput.Pressed)
                {
                    SelectedColourIndex = EntryIdx;
                    PotentialColourDrag = true;
                    DraggedColourIndex  = EntryIdx;
                    DragPressOrigin     = Cursor;
                }
            }
        }

        const ColourContrastInk FirstInk = ColourCodex::QueryContrastInk(AllEntries[FilteredIndices.front()].FillColour);
        const char* BannerTitle =
            (ColourFilter == ColourFilterGroup::All) ? "ColourCodex Spectrum" : ColourCodex::QueryFilterGroupTitle(ColourFilter);
        Canvas.Text({ RepositoryArea.X + 24.0f, RepositoryArea.Y + 22.0f }, 14.0f, FirstInk.PrimaryInk, BannerTitle);

        Canvas.PopClip();
        Canvas.StrokeRectangle(RepositoryArea, AppearanceColors.StrokeStrong.ToRgba8(), SectionRadius, 1.0f);
        return;
    }

    if (ColourLayout == ColourRepositoryLayout::HorizontalStrips)
    {
        Canvas.FillRectangle(RepositoryArea, AppearanceColors.SectionSurface.ToRgba8(), SectionRadius);
        Canvas.PushClip(RepositoryArea);

        if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
        {
            RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 36.0f);
        }

        const float BandH     = std::max(40.0f, RepositoryArea.Height / std::min<float>(10.0f, static_cast<float>(FilteredIndices.size())));
        const float MaxScroll = std::max(0.0f, BandH * static_cast<float>(FilteredIndices.size()) - RepositoryArea.Height);
        RepositoryScrollY     = std::min(RepositoryScrollY, MaxScroll);

        for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
        {
            const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
            const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
            const float             BandY    = RepositoryArea.Y + static_cast<float>(OrderIdx) * BandH - RepositoryScrollY;
            const Rectangle2D       BandRect{ RepositoryArea.X, BandY, RepositoryArea.Width, std::ceil(BandH) };
            if (BandRect.Bottom() < RepositoryArea.Y || BandRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }

            Canvas.FillRectangle(BandRect, Entry.FillColour, 0.0f);
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry.FillColour);

            if (EntryIdx == SelectedColourIndex)
            {
                Canvas.FillRectangle({ BandRect.X, BandRect.Y, 5.0f, BandRect.Height }, Ink.PrimaryInk, 0.0f);
            }

            Canvas.Text(
                { BandRect.X + 20.0f, BandRect.Y + (BandRect.Height - 14.0f) * 0.5f },
                13.0f,
                Ink.PrimaryInk,
                Entry.AliasName.c_str());

            const std::string RightSpec = Entry.HexCompact + "   RGB " + ColourCodex::FormatRgbTriplet(Entry.FillColour);
            Canvas.TextRightAligned(
                { BandRect.X, BandRect.Y, BandRect.Width - 20.0f, BandRect.Height },
                12.5f,
                Ink.PrimaryInk,
                RightSpec.c_str());

            LastColourHitboxes.push_back({ EntryIdx, BandRect });

            if (RepositoryArea.Contains(Cursor) && BandRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    SelectedColourIndex = EntryIdx;
                    (void)ApplyColourIndexToSlot(EntryIdx);
                }
                else if (PointerInput.Pressed)
                {
                    SelectedColourIndex = EntryIdx;
                    PotentialColourDrag = true;
                    DraggedColourIndex  = EntryIdx;
                    DragPressOrigin     = Cursor;
                }
            }
        }

        Canvas.PopClip();
        Canvas.StrokeRectangle(RepositoryArea, AppearanceColors.StrokeStrong.ToRgba8(), SectionRadius, 1.0f);
        return;
    }

    // GridCircles: Multi-column flat circular .ColourSwatch cells
    Canvas.PushClip(RepositoryArea);
    if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
    {
        RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 38.0f);
    }

    constexpr float CellGap  = 10.0f;
    constexpr float CellH    = 128.0f;
    const uint32_t  Columns  = std::max(1u, static_cast<uint32_t>((RepositoryArea.Width + CellGap) / 124.0f));
    const float     CellW    = (RepositoryArea.Width - CellGap * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
    const uint32_t  RowCount = static_cast<uint32_t>((FilteredIndices.size() + Columns - 1u) / Columns);
    const float     TotalH   = static_cast<float>(RowCount) * (CellH + CellGap);
    RepositoryScrollY        = std::min(RepositoryScrollY, std::max(0.0f, TotalH - RepositoryArea.Height));

    const ColorRgba IdleFill     = AppearanceColors.SectionSurface.ToRgba8();
    const ColorRgba HoverFill    = BlendRgba(AppearanceColors.SectionSurface, AppearanceColors.Raised, 0.55f);
    const ColorRgba SelectedFill = BlendRgba(AppearanceColors.SectionSubtle, AppearanceColors.Accent, AppearanceColors.LightSurface ? 0.14f : 0.22f);
    const ColorRgba IdleStroke   = AppearanceColors.Stroke.ToRgba8();
    const ColorRgba SelStroke    = BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Accent, 0.72f);
    const ColorRgba TitleInk     = AppearanceColors.Text.ToRgba8();
    const ColorRgba SubInk       = AppearanceColors.TextDim.ToRgba8();
    const ColorRgba RingStroke   = BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Text, 0.22f);

    for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
    {
        const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
        const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
        const uint32_t          Col      = static_cast<uint32_t>(OrderIdx) % Columns;
        const uint32_t          Row      = static_cast<uint32_t>(OrderIdx) / Columns;
        const Rectangle2D       CellRect{
            RepositoryArea.X + static_cast<float>(Col) * (CellW + CellGap),
            RepositoryArea.Y + static_cast<float>(Row) * (CellH + CellGap) - RepositoryScrollY,
            CellW,
            CellH,
        };
        if (CellRect.Bottom() < RepositoryArea.Y || CellRect.Y > RepositoryArea.Bottom())
        {
            continue;
        }

        const bool Hovered  = RepositoryArea.Contains(Cursor) && CellRect.Contains(Cursor);
        const bool Selected = (EntryIdx == SelectedColourIndex);
        Canvas.FillRectangle(CellRect, Selected ? SelectedFill : (Hovered ? HoverFill : IdleFill), SectionRadius);
        Canvas.StrokeRectangle(CellRect, Selected ? SelStroke : IdleStroke, SectionRadius, Selected ? 1.25f : 1.0f);

        const Point2D CircleCenter{ CellRect.Center().X, CellRect.Y + 46.0f };
        Canvas.FillCircle(CircleCenter, 26.0f, Entry.FillColour);
        Canvas.StrokeCircle(CircleCenter, 26.0f, RingStroke, 1.0f);

        Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 84.0f, CellRect.Width - 8.0f, 16.0f }, 10.5f, TitleInk, Entry.AliasName.c_str());
        Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 102.0f, CellRect.Width - 8.0f, 14.0f }, 9.0f, SubInk, Entry.HexUpper.c_str());

        LastColourHitboxes.push_back({ EntryIdx, CellRect });

        if (Hovered)
        {
            if (PointerInput.DoubleClicked)
            {
                SelectedColourIndex = EntryIdx;
                (void)ApplyColourIndexToSlot(EntryIdx);
            }
            else if (PointerInput.Pressed)
            {
                SelectedColourIndex = EntryIdx;
                PotentialColourDrag = true;
                DraggedColourIndex  = EntryIdx;
                DragPressOrigin     = Cursor;
            }
        }
    }
    Canvas.PopClip();
}

void ContentBrowserPanel::RenderNonColoursRepository(
    PixelSpace&                       Canvas,
    Rectangle2D                       RepositoryArea,
    const ContentBrowserPointerInput& PointerInput,
    const ControlColorSet&            AppearanceColors,
    float                             SectionRadius)
{
    LastColourHitboxes.clear();

    const std::vector<std::size_t> Filtered =
        ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, Category, OriginScope, SearchBuffer);
    LastVisibleCount = static_cast<uint32_t>(Filtered.size());

    if (Filtered.empty())
    {
        Canvas.TextCentered(RepositoryArea, 12.5f, AppearanceColors.TextDim.ToRgba8(), "No compatible entries match active search/workspace");
        return;
    }

    // Stream any non-resident ThumbnailSheet tiles via SSD/HDD AssetStreamThread
    ThumbnailRepository.RequestVisibleTileStreaming(Filtered, StreamChannelThread);

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
    Canvas.PushClip(RepositoryArea);
    if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
    {
        RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 38.0f);
    }

    const ColorRgba IdleFill     = AppearanceColors.SectionSurface.ToRgba8();
    const ColorRgba HoverFill    = BlendRgba(AppearanceColors.SectionSurface, AppearanceColors.Raised, 0.55f);
    const ColorRgba SelectedFill = BlendRgba(AppearanceColors.SectionSubtle, AppearanceColors.Accent, AppearanceColors.LightSurface ? 0.14f : 0.22f);
    const ColorRgba IdleStroke   = AppearanceColors.Stroke.ToRgba8();
    const ColorRgba SelStroke    = BlendRgba(AppearanceColors.StrokeStrong, AppearanceColors.Accent, 0.72f);
    const ColorRgba TitleInk     = AppearanceColors.Text.ToRgba8();
    const ColorRgba SubInk       = AppearanceColors.TextDim.ToRgba8();

    if (StandardLayout == StandardRepositoryLayout::List)
    {
        const float MaxScroll = std::max(0.0f, static_cast<float>(Filtered.size()) * 52.0f - RepositoryArea.Height);
        RepositoryScrollY     = std::min(RepositoryScrollY, MaxScroll);

        for (std::size_t OrderIdx = 0u; OrderIdx < Filtered.size(); ++OrderIdx)
        {
            const std::size_t             EntryIdx = Filtered[OrderIdx];
            const ContentRepositoryEntry* Entry    = ThumbnailRepository.QueryEntryAt(EntryIdx);
            if (Entry == nullptr)
            {
                continue;
            }
            const Rectangle2D RowRect{ RepositoryArea.X, RepositoryArea.Y + static_cast<float>(OrderIdx) * 52.0f - RepositoryScrollY, RepositoryArea.Width, 48.0f };
            if (RowRect.Bottom() < RepositoryArea.Y || RowRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }
            const bool Hovered  = RepositoryArea.Contains(Cursor) && RowRect.Contains(Cursor);
            const bool Selected = (SelectedContentIndex == EntryIdx);
            Canvas.FillRectangle(RowRect, Selected ? SelectedFill : (Hovered ? HoverFill : IdleFill), 0.0f);
            Canvas.StrokeRectangle(RowRect, Selected ? SelStroke : IdleStroke, 0.0f, 1.0f);
            if (Selected)
            {
                Canvas.FillRectangle({ RowRect.X, RowRect.Y, 3.0f, RowRect.Height }, AppearanceColors.Accent.ToRgba8(), 0.0f);
            }

            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(EntryIdx);
            const Rectangle2D     IconBox{ RowRect.X + 8.0f, RowRect.Y + 6.0f, 36.0f, 36.0f };
            if (TileUv.Valid)
            {
                Canvas.FillTexturedRectangle(IconBox, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 8.0f);
            }

            const auto  EntryCat    = static_cast<ContentCategory>(Entry->IndexHeader.CategoryCode);
            const auto  EntryOrigin = static_cast<ContentOriginFilter>(Entry->IndexHeader.OriginCode);
            const char* OriginTag   = (EntryOrigin == ContentOriginFilter::Engine) ? "Engine" : ((EntryOrigin == ContentOriginFilter::Imported) ? "Imported" : "Project");
            const std::string MetaLine = Entry->ContainerFormat + "  ·  " + ThumbnailSheetRepository::QueryCategoryTitle(EntryCat) + "  ·  " + OriginTag;

            Canvas.Text({ RowRect.X + 54.0f, RowRect.Y + 10.0f }, 11.5f, TitleInk, Entry->DisplayTitle.c_str());
            Canvas.Text({ RowRect.X + 54.0f, RowRect.Y + 27.0f }, 9.2f, SubInk, MetaLine.c_str());

            if (Hovered && PointerInput.DoubleClicked)
            {
                (void)ActivateContentEntryAt(EntryIdx);
            }
            else if (Hovered && PointerInput.Pressed)
            {
                SelectedContentIndex = EntryIdx;
            }
        }
    }
    else
    {
        constexpr float CellGap  = 10.0f;
        constexpr float CellH    = 134.0f;
        const uint32_t  Columns  = std::max(1u, static_cast<uint32_t>((RepositoryArea.Width + CellGap) / 136.0f));
        const float     CellW    = (RepositoryArea.Width - CellGap * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
        const uint32_t  RowCount = static_cast<uint32_t>((Filtered.size() + Columns - 1u) / Columns);
        RepositoryScrollY        = std::min(RepositoryScrollY, std::max(0.0f, static_cast<float>(RowCount) * (CellH + CellGap) - RepositoryArea.Height));

        for (std::size_t OrderIdx = 0u; OrderIdx < Filtered.size(); ++OrderIdx)
        {
            const std::size_t             EntryIdx = Filtered[OrderIdx];
            const ContentRepositoryEntry* Entry    = ThumbnailRepository.QueryEntryAt(EntryIdx);
            if (Entry == nullptr)
            {
                continue;
            }
            const uint32_t    Col = static_cast<uint32_t>(OrderIdx) % Columns;
            const uint32_t    Row = static_cast<uint32_t>(OrderIdx) / Columns;
            const Rectangle2D CellRect{
                RepositoryArea.X + static_cast<float>(Col) * (CellW + CellGap),
                RepositoryArea.Y + static_cast<float>(Row) * (CellH + CellGap) - RepositoryScrollY,
                CellW,
                CellH,
            };
            if (CellRect.Bottom() < RepositoryArea.Y || CellRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }

            const bool Selected = (SelectedContentIndex == EntryIdx);
            const bool Hovered  = RepositoryArea.Contains(Cursor) && CellRect.Contains(Cursor);
            Canvas.FillRectangle(CellRect, Selected ? SelectedFill : (Hovered ? HoverFill : IdleFill), SectionRadius);
            Canvas.StrokeRectangle(CellRect, Selected ? SelStroke : IdleStroke, SectionRadius, Selected ? 1.25f : 1.0f);

            // Single textured quad sampled directly from pre-baked 1024x1024 ThumbnailSheet page
            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(EntryIdx);
            const Rectangle2D     ThumbRect{ CellRect.Center().X - 31.0f, CellRect.Y + 12.0f, 62.0f, 62.0f };
            if (TileUv.Valid)
            {
                Canvas.FillTexturedRectangle(ThumbRect, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 12.0f);
            }

            Canvas.TextCentered({ CellRect.X + 5.0f, CellRect.Y + 82.0f, CellRect.Width - 10.0f, 16.0f }, 10.0f, TitleInk, Entry->DisplayTitle.c_str());

            const auto EntryCat    = static_cast<ContentCategory>(Entry->IndexHeader.CategoryCode);
            const auto EntryOrigin = static_cast<ContentOriginFilter>(Entry->IndexHeader.OriginCode);
            const char* OriginTag  = (EntryOrigin == ContentOriginFilter::Engine) ? "Engine" : ((EntryOrigin == ContentOriginFilter::Imported) ? "Imported" : "Project");
            const std::string Sub  = std::string(ThumbnailSheetRepository::QueryCategoryTitle(EntryCat)) + " · " + OriginTag;
            Canvas.TextCentered({ CellRect.X + 5.0f, CellRect.Y + 102.0f, CellRect.Width - 10.0f, 14.0f }, 8.8f, SubInk, Sub.c_str());

            if (Hovered && PointerInput.DoubleClicked)
            {
                (void)ActivateContentEntryAt(EntryIdx);
            }
            else if (Hovered && PointerInput.Pressed)
            {
                SelectedContentIndex = EntryIdx;
            }
        }
    }
    Canvas.PopClip();
}

void ContentBrowserPanel::RenderContentDetailsSidebar(
    PixelSpace&                       Canvas,
    Rectangle2D                       SidebarArea,
    const ContentBrowserPointerInput& PointerInput,
    const ControlColorSet&            AppearanceColors,
    float                             SectionRadius)
{
    Canvas.StrokeLine({ SidebarArea.X, SidebarArea.Y }, { SidebarArea.X, SidebarArea.Bottom() }, AppearanceColors.Divider.ToRgba8(), 1.0f);
    const Rectangle2D Inner{ SidebarArea.X + 18.0f, SidebarArea.Y + 6.0f, std::max(80.0f, SidebarArea.Width - 18.0f), SidebarArea.Height - 12.0f };
    Canvas.PushClip(Inner);

    const ColorRgba EyebrowInk = BlendRgba(AppearanceColors.Accent, AppearanceColors.TextDim, 0.48f);
    Canvas.Text({ Inner.X, Inner.Y }, 9.0f, EyebrowInk, "CONTENT DETAILS");

    if (Category == ContentCategory::Colours)
    {
        if (const ColourCodexEntry* Entry = CodexLibrary.QueryEntryAt(SelectedColourIndex))
        {
            const float             TileH  = std::clamp(Inner.Height - 134.0f, 174.0f, 268.0f);
            const float             TileR  = std::min(20.0f, SectionRadius + 4.0f);
            const Rectangle2D       TileRect{ Inner.X, Inner.Y + 22.0f, Inner.Width, TileH };
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry->FillColour);

            Canvas.FillRectangle(TileRect, Entry->FillColour, TileR);
            Canvas.StrokeRectangle(TileRect, Ink.BorderInk, TileR, 1.0f);

            const auto [HeadingLine1, HeadingLine2] = SplitUppercaseTileHeading(Entry->AliasName);
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Y + 20.0f }, 22.0f, Ink.PrimaryInk, HeadingLine1.c_str());
            if (!HeadingLine2.empty())
            {
                Canvas.Text({ TileRect.X + 20.0f, TileRect.Y + 46.0f }, 22.0f, Ink.PrimaryInk, HeadingLine2.c_str());
            }

            const std::string HexLine  = "HEX: " + Entry->HexUpper;
            const std::string RgbLine  = "RGB: " + ColourCodex::FormatRgbTriplet(Entry->FillColour);
            const std::string CmykLine = "CMYK: " + ColourCodex::FormatCmykQuadruplet(Entry->CmykPercent);

            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 68.0f }, 12.0f, Ink.PrimaryInk, HexLine.c_str());
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 48.0f }, 12.0f, Ink.PrimaryInk, RgbLine.c_str());
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 28.0f }, 12.0f, Ink.PrimaryInk, CmykLine.c_str());

            const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
            if (TileRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    (void)ApplyColourIndexToSlot(SelectedColourIndex);
                }
                else if (PointerInput.Pressed)
                {
                    PotentialColourDrag = true;
                    DraggedColourIndex  = SelectedColourIndex;
                    DragPressOrigin     = Cursor;
                }
            }

            std::string TargetLabel = "Active colour slot";
            for (const ColourSlotRegistration& Slot : RegisteredSlots)
            {
                if (Slot.SlotIdentifier == FocusedSlotId)
                {
                    TargetLabel = Slot.DisplayTitle;
                    break;
                }
            }
            if (TargetLabel == "Active colour slot" && !RegisteredSlots.empty())
            {
                TargetLabel = RegisteredSlots.front().DisplayTitle;
            }

            const std::string TargetSummary = "Target: " + TargetLabel + "  ·  .ColourSwatch";
            Canvas.Text({ Inner.X, TileRect.Bottom() + 12.0f }, 9.8f, AppearanceColors.TextDim.ToRgba8(), TargetSummary.c_str());

            const std::string ApplyBtnText = "Apply to " + TargetLabel;
            if (ConstructCapsuleControl(
                    Canvas,
                    { Inner.X, TileRect.Bottom() + 34.0f, Inner.Width, 30.0f },
                    ApplyBtnText.c_str(),
                    false,
                    PointerInput,
                    AppearanceColors,
                    10.5f,
                    true))
            {
                (void)ApplyColourIndexToSlot(SelectedColourIndex);
            }
        }
    }
    else
    {
        if (const ContentRepositoryEntry* Entry = ThumbnailRepository.QueryEntryAt(SelectedContentIndex))
        {
            Canvas.Text({ Inner.X, Inner.Y + 24.0f }, 16.0f, AppearanceColors.Text.ToRgba8(), Entry->DisplayTitle.c_str());

            const Rectangle2D PreviewBox{ Inner.X, Inner.Y + 52.0f, Inner.Width, 148.0f };
            Canvas.FillRectangle(PreviewBox, AppearanceColors.SectionSurface.ToRgba8(), SectionRadius);
            Canvas.StrokeRectangle(PreviewBox, AppearanceColors.Stroke.ToRgba8(), SectionRadius, 1.0f);

            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(SelectedContentIndex);
            if (TileUv.Valid)
            {
                const Rectangle2D BigThumb{ PreviewBox.Center().X - 54.0f, PreviewBox.Center().Y - 54.0f, 108.0f, 108.0f };
                Canvas.FillTexturedRectangle(BigThumb, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 16.0f);
            }

            const auto  EntryCat    = static_cast<ContentCategory>(Entry->IndexHeader.CategoryCode);
            const auto  EntryOrigin = static_cast<ContentOriginFilter>(Entry->IndexHeader.OriginCode);
            const char* OriginName  = (EntryOrigin == ContentOriginFilter::Engine) ? "EngineContent (Read-Only)" : ((EntryOrigin == ContentOriginFilter::Imported) ? "Imported (.Space)" : "Project Content");

            const std::string FormatLine = "Container: " + Entry->ContainerFormat + "  (" + ThumbnailSheetRepository::QueryCategoryTitle(EntryCat) + ")";
            const std::string OriginLine = std::string("Origin: ") + OriginName;
            const std::string SheetLine  = "Sheet Page #" + std::to_string(Entry->IndexHeader.SheetPageOrdinal) +
                                           " · Tile (" + std::to_string(Entry->IndexHeader.TileColumn) + ", " +
                                           std::to_string(Entry->IndexHeader.TileRow) + ") · 64B Index";

            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 14.0f }, 10.0f, BlendRgba(AppearanceColors.Text, AppearanceColors.Accent, 0.25f), FormatLine.c_str());
            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 32.0f }, 9.5f, AppearanceColors.TextDim.ToRgba8(), OriginLine.c_str());
            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 49.0f }, 9.2f, AppearanceColors.TextFaint.ToRgba8(), SheetLine.c_str());

            if (ContentActivationCallback &&
                ConstructCapsuleControl(
                    Canvas,
                    { Inner.X, PreviewBox.Bottom() + 72.0f, Inner.Width, 30.0f },
                    "Open in Workspace",
                    false,
                    PointerInput,
                    AppearanceColors,
                    10.5f,
                    true))
            {
                (void)ActivateContentEntryAt(SelectedContentIndex);
            }
        }
    }

    Canvas.PopClip();
}

void ContentBrowserPanel::RenderWithInput(
    PixelSpace&                       Canvas,
    Rectangle2D                       ViewportLogicalBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds)
{
    const double SafeDt = DeltaSeconds > 0.0 ? DeltaSeconds : (1.0 / 60.0);
    (void)ShelfSpring.Integrate(SafeDt);
    if (SearchEditor.Active)
    {
        SearchEditor.BlinkPhase = std::fmod(SearchEditor.BlinkPhase + static_cast<float>(SafeDt), ControlMetrics::CaretBlinkSeconds);
    }

    const ControlColorSet AppearanceColors = ResolveActiveColorSet();
    const float           SectionRadius    = ResolveCornerRadius();
    const Point2D         Cursor{ PointerInput.CursorX, PointerInput.CursorY };

    if (PotentialColourDrag && PointerInput.HeldDown && !DraggingColour)
    {
        const float Dx = Cursor.X - DragPressOrigin.X;
        const float Dy = Cursor.Y - DragPressOrigin.Y;
        if (std::hypot(Dx, Dy) > 5.0f)
        {
            DraggingColour = true;
            if (TargetExtent == ContentShelfExtent::FullHeight)
            {
                ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
            }
        }
    }

    if (PointerInput.Released)
    {
        if (DraggingColour)
        {
            for (const ColourSlotRegistration& Slot : RegisteredSlots)
            {
                if (Slot.ScreenBounds.Contains(Cursor))
                {
                    (void)ApplyColourIndexToSlot(DraggedColourIndex, Slot.SlotIdentifier);
                    break;
                }
            }
        }
        PotentialColourDrag = false;
        DraggingColour      = false;
    }

    if (PointerInput.Pressed && !QueryBlocksUnderlyingWorkspace())
    {
        for (const ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.ScreenBounds.Contains(Cursor))
            {
                FocusedSlotId = Slot.SlotIdentifier;
                break;
            }
        }
    }

    RenderColourSlotHighlights(Canvas, Cursor, AppearanceColors);

    const float       Progress    = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    const float       DrawerH     = std::round(ViewportLogicalBounds.Height * Progress);
    const Rectangle2D NotchBounds = QueryNotchTabBounds(ViewportLogicalBounds);
    const bool        NotchHover  = NotchBounds.Contains(Cursor);

    if (NotchHover && PointerInput.Pressed)
    {
        ConfigureShelfExtent(
            TargetExtent == ContentShelfExtent::Closed ? ContentShelfExtent::FullHeight : ContentShelfExtent::Closed,
            false);
    }

    RenderBottomNotchHandle(Canvas, NotchBounds, NotchHover, AppearanceColors);

    if (DrawerH <= 2.0f)
    {
        LastSearchBounds = {};
        if (DraggingColour)
        {
            if (const ColourCodexEntry* Dragged = CodexLibrary.QueryEntryAt(DraggedColourIndex))
            {
                Canvas.FillCircle(Cursor, 14.0f, Dragged->FillColour);
                Canvas.StrokeCircle(Cursor, 14.0f, AppearanceColors.Text.ToRgba8(), 2.0f);
            }
        }
        return;
    }

    const Rectangle2D DrawerRect{
        ViewportLogicalBounds.X,
        ViewportLogicalBounds.Bottom() - DrawerH,
        ViewportLogicalBounds.Width,
        DrawerH,
    };

    Canvas.PushClip(DrawerRect);

    ColorQuad DrawerSurface = ColorQuad::Blend(AppearanceColors.Panel, AppearanceColors.TabBackground, AppearanceColors.LightSurface ? 0.08f : 0.18f);
    DrawerSurface.Alpha     = 0.985f;
    Canvas.FillRectangle(DrawerRect, DrawerSurface.ToRgba8(), 0.0f);
    Canvas.StrokeLine({ DrawerRect.X, DrawerRect.Y }, { DrawerRect.Right(), DrawerRect.Y }, AppearanceColors.StrokeStrong.ToRgba8(), 1.0f);

    const float     PadX       = 24.0f;
    float           CurY       = DrawerRect.Y + 14.0f;
    const ColorRgba EyebrowInk = BlendRgba(AppearanceColors.Accent, AppearanceColors.TextDim, 0.45f);

    // 1. Header Bar + Workspace Domain Switcher + SSD/HDD Tier, Import Destination, + Colour, Split, Close
    const std::string Eyebrow = std::string("CONTENT / LIBRARY  ·  ") +
                                ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) + " WORKSPACE";
    Canvas.Text({ DrawerRect.X + PadX, CurY }, 8.5f, EyebrowInk, Eyebrow.c_str());
    Canvas.Text({ DrawerRect.X + PadX, CurY + 12.0f }, 21.0f, AppearanceColors.Text.ToRgba8(), "ContentBrowser");

    constexpr std::array<std::pair<WorkspaceCategory, const char*>, 5> QuickWorkspaces = {{
        { WorkspaceCategory::SceneWorld, "Scene"      },
        { WorkspaceCategory::UV,         "UV"         },
        { WorkspaceCategory::Material,   "Material"   },
        { WorkspaceCategory::Simulation, "Simulation" },
        { WorkspaceCategory::Audio,      "Audio"      },
    }};
    float WsTotalW = 6.0f;
    for (std::size_t Idx = 0u; Idx < QuickWorkspaces.size(); ++Idx)
    {
        WsTotalW += std::max(50.0f, Canvas.MeasureTextWidth(9.2f, QuickWorkspaces[Idx].second) + 18.0f);
        if (Idx + 1u < QuickWorkspaces.size())
        {
            WsTotalW += 3.0f;
        }
    }
    const Rectangle2D WsTrack{ DrawerRect.X + 240.0f, CurY + 4.0f, WsTotalW, 29.0f };
    ConstructSegmentedTrack(Canvas, WsTrack, AppearanceColors);
    float WsX = WsTrack.X + 3.0f;
    for (const auto& [WsKind, WsLabel] : QuickWorkspaces)
    {
        const float SegmentW = std::max(50.0f, Canvas.MeasureTextWidth(9.2f, WsLabel) + 18.0f);
        if (ConstructSegmentedOption(Canvas, { WsX, WsTrack.Y + 3.0f, SegmentW, 23.0f }, WsLabel, ActiveWorkspace == WsKind, PointerInput, AppearanceColors, 9.2f))
        {
            ConfigureWorkspaceDomain(WsKind);
        }
        WsX += SegmentW + 3.0f;
    }

    float RightBtnX = DrawerRect.Right() - PadX;
    RightBtnX -= 30.0f;
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 30.0f, 28.0f },
            "",
            false,
            PointerInput,
            AppearanceColors,
            10.0f,
            false,
            ControlIcon::CloseCross))
    {
        ConfigureShelfExtent(ContentShelfExtent::Closed, false);
    }

    RightBtnX -= 78.0f;
    const bool InSplit = (TargetExtent == ContentShelfExtent::SplitHalf);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 72.0f, 28.0f },
            InSplit ? "Full" : "Split",
            InSplit,
            PointerInput,
            AppearanceColors,
            9.5f,
            false,
            ControlIcon::LayoutSplit))
    {
        ConfigureShelfExtent(InSplit ? ContentShelfExtent::FullHeight : ContentShelfExtent::SplitHalf, false);
    }

    RightBtnX -= 94.0f;
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 88.0f, 28.0f },
            "Colour",
            false,
            PointerInput,
            AppearanceColors,
            9.5f,
            false,
            ControlIcon::PlusAdd))
    {
        if (ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, ContentCategory::Colours))
        {
            const std::size_t Added = CodexLibrary.AddProjectColour("Project Swatch", "#2454FF", "Project");
            (void)ActivateCategory(ContentCategory::Colours);
            ConfigureColourFilter(ColourFilterGroup::All);
            SelectedColourIndex = Added;
            StatusMessage       = "Created Project Swatch (.ColourSwatch #2454FF)";
        }
    }

    RightBtnX -= 106.0f;
    const bool ScratchMode = (ThumbnailRepository.QueryImportPolicy().Mode == ImportDestinationMode::ScratchTempFolder);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 100.0f, 28.0f },
            ScratchMode ? "Import: Temp" : "Import: Project",
            ScratchMode,
            PointerInput,
            AppearanceColors,
            9.0f))
    {
        ImportIntakeConfiguration NextPolicy = ThumbnailRepository.QueryImportPolicy();
        NextPolicy.Mode = ScratchMode ? ImportDestinationMode::ProjectImportedFolder : ImportDestinationMode::ScratchTempFolder;
        ThumbnailRepository.ConfigureImportPolicy(NextPolicy);
        StatusMessage = ScratchMode
                            ? "Import destination -> Projects/<Project>/Content/Imported/"
                            : "Import destination -> Scratch Temp (Projects/_Scratch/Saved/Imported/)";
    }

    RightBtnX -= 84.0f;
    const bool RotationalActive = (StreamChannelThread.QueryResolvedTier() == Threading::StorageHardwareTier::RotationalSequentialIo);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 78.0f, 28.0f },
            RotationalActive ? "I/O: HDD" : "I/O: SSD",
            RotationalActive,
            PointerInput,
            AppearanceColors,
            9.0f))
    {
        ConfigureStorageHardwareTier(
            RotationalActive ? Threading::StorageHardwareTier::FlashStorageRandomIo
                             : Threading::StorageHardwareTier::RotationalSequentialIo);
    }

    CurY += 44.0f;
    Canvas.StrokeLine({ DrawerRect.X + PadX, CurY }, { DrawerRect.Right() - PadX, CurY }, AppearanceColors.Divider.ToRgba8(), 1.0f);
    CurY += 9.0f;

    // 2. Origin Scope Tabs (Unified Recessed Outer Track: [ All  Project  Imported  Engine ]) + Search + Unified Layout Switcher
    constexpr std::array<std::pair<ContentOriginFilter, const char*>, 4> Origins = {{
        { ContentOriginFilter::All,      "All"      },
        { ContentOriginFilter::Project,  "Project"  },
        { ContentOriginFilter::Imported, "Imported" },
        { ContentOriginFilter::Engine,   "Engine"   },
    }};
    constexpr float OriginSegmentW = 68.0f;
    constexpr float OriginGap      = 3.0f;
    const float     OriginTrackW   = 6.0f + 4.0f * OriginSegmentW + 3.0f * OriginGap;
    const Rectangle2D OriginTrack{ DrawerRect.X + PadX, CurY, OriginTrackW, 30.0f };
    ConstructSegmentedTrack(Canvas, OriginTrack, AppearanceColors);

    float OriginX = OriginTrack.X + 3.0f;
    for (const auto& [ScopeEnum, Label] : Origins)
    {
        if (ConstructSegmentedOption(Canvas, { OriginX, OriginTrack.Y + 3.0f, OriginSegmentW, 24.0f }, Label, OriginScope == ScopeEnum, PointerInput, AppearanceColors, 9.5f))
        {
            ConfigureOriginFilter(ScopeEnum);
        }
        OriginX += OriginSegmentW + OriginGap;
    }
    OriginX = OriginTrack.Right();

    const float LayoutClusterWidth = (Category == ContentCategory::Colours) ? 286.0f : 165.0f;
    const float LayoutStartX       = DrawerRect.Right() - PadX - LayoutClusterWidth;

    // Interactive Search Capsule with leading SearchGlass icon and trailing CloseCross clear button
    const float SearchMinX = OriginX + 12.0f;
    const float SearchMaxW = std::max(140.0f, std::min(380.0f, LayoutStartX - 16.0f - SearchMinX));
    const float SearchX    = std::max(SearchMinX, LayoutStartX - 16.0f - SearchMaxW);
    const Rectangle2D SearchBounds{ SearchX, CurY, SearchMaxW, 30.0f };
    LastSearchBounds = SearchBounds;

    const bool SearchHover   = SearchBounds.Contains(Cursor);
    const Rectangle2D ClearBounds{ SearchBounds.Right() - 26.0f, SearchBounds.Y + 5.0f, 20.0f, 20.0f };
    const bool ClearHover    = (!SearchBuffer.empty() && ClearBounds.Contains(Cursor));

    if (PointerInput.Pressed)
    {
        if (ClearHover)
        {
            ConfigureSearchQuery({});
            SearchEditor.Active = true;
        }
        else if (SearchHover)
        {
            if (!SearchEditor.Active)
            {
                PopulateEditBufferFromString(SearchEditor, SearchBuffer);
                SearchEditor.Active     = true;
                SearchEditor.BlinkPhase = 0.0f;
            }
        }
        else if (SearchEditor.Active)
        {
            SearchEditor.Active = false;
        }
    }

    const ColorRgba SearchFill = BlendRgba(AppearanceColors.Field, AppearanceColors.Inset, AppearanceColors.LightSurface ? 0.35f : 0.55f);
    const ColorRgba SearchBorder = SearchEditor.Active
                                       ? AppearanceColors.Highlight.ToRgba8()
                                       : (SearchHover ? AppearanceColors.StrokeStrong.ToRgba8()
                                                      : BlendRgba(AppearanceColors.Stroke, AppearanceColors.TextDim, 0.28f));
    const ColorRgba SearchIconInk = SearchEditor.Active
                                        ? AppearanceColors.Accent.ToRgba8()
                                        : AppearanceColors.TextDim.ToRgba8();

    Canvas.FillRectangle(SearchBounds, SearchFill, 15.0f);
    Canvas.StrokeRectangle(SearchBounds, SearchBorder, 15.0f, SearchEditor.Active ? 1.35f : 1.0f);

    VectorIconRepository::ConstructControl(
        Canvas,
        ControlIcon::SearchGlass,
        VectorIconPlacement{
            SearchBounds.X + 11.0f,
            SearchBounds.Y + 8.0f,
            14.0f,
            SearchIconInk,
            1.9f,
        });

    Canvas.PushClip({ SearchBounds.X + 30.0f, SearchBounds.Y + 2.0f, std::max(24.0f, SearchBounds.Width - 58.0f), SearchBounds.Height - 4.0f });
    if (SearchBuffer.empty() && !SearchEditor.Active)
    {
        Canvas.Text(
            { SearchBounds.X + 31.0f, SearchBounds.Y + 9.0f },
            10.2f,
            AppearanceColors.TextDim.ToRgba8(),
            "Search ContentBrowser...   Ctrl+F");
    }
    else
    {
        Canvas.Text(
            { SearchBounds.X + 31.0f, SearchBounds.Y + 9.0f },
            10.4f,
            AppearanceColors.Text.ToRgba8(),
            SearchBuffer.c_str());

        if (SearchEditor.Active && SearchEditor.BlinkPhase < ControlMetrics::CaretBlinkSeconds * 0.5f)
        {
            const std::string_view PrefixView(SearchEditor.Text, std::min<std::size_t>(SearchEditor.Caret, SearchEditor.Length));
            const std::string      PrefixCopy(PrefixView);
            const float            CaretX = SearchBounds.X + 31.0f + Canvas.MeasureTextWidth(10.4f, PrefixCopy.c_str());
            Canvas.StrokeLine(
                { CaretX, SearchBounds.Y + 7.0f },
                { CaretX, SearchBounds.Bottom() - 7.0f },
                AppearanceColors.Accent.ToRgba8(),
                1.3f);
        }
    }
    Canvas.PopClip();

    if (!SearchBuffer.empty())
    {
        if (ClearHover)
        {
            Canvas.FillCircle(ClearBounds.Center(), 9.0f, AppearanceColors.Raised.ToRgba8());
        }
        VectorIconRepository::ConstructControl(
            Canvas,
            ControlIcon::CloseCross,
            VectorIconPlacement{
                ClearBounds.X + 4.5f,
                ClearBounds.Y + 4.5f,
                11.0f,
                ClearHover ? AppearanceColors.Text.ToRgba8() : AppearanceColors.TextDim.ToRgba8(),
                1.9f,
            });
    }

    // Layout View Switcher inside unified recessed track with Grid / List / Vertical / Horizontal vector icons
    const Rectangle2D LayoutTrack{ LayoutStartX, CurY, LayoutClusterWidth, 30.0f };
    ConstructSegmentedTrack(Canvas, LayoutTrack, AppearanceColors);

    if (Category == ContentCategory::Colours)
    {
        float LayoutX = LayoutTrack.X + 3.0f;
        if (ConstructSegmentedOption(
                Canvas,
                { LayoutX, LayoutTrack.Y + 3.0f, 76.0f, 24.0f },
                "Grid",
                ColourLayout == ColourRepositoryLayout::GridCircles,
                PointerInput,
                AppearanceColors,
                9.5f,
                ControlIcon::LayoutGrid))
        {
            ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
        }
        LayoutX += 79.0f;
        if (ConstructSegmentedOption(
                Canvas,
                { LayoutX, LayoutTrack.Y + 3.0f, 92.0f, 24.0f },
                "Vertical",
                ColourLayout == ColourRepositoryLayout::VerticalStrips,
                PointerInput,
                AppearanceColors,
                9.5f,
                ControlIcon::LayoutColumns))
        {
            ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
        }
        LayoutX += 95.0f;
        if (ConstructSegmentedOption(
                Canvas,
                { LayoutX, LayoutTrack.Y + 3.0f, 106.0f, 24.0f },
                "Horizontal",
                ColourLayout == ColourRepositoryLayout::HorizontalStrips,
                PointerInput,
                AppearanceColors,
                9.5f,
                ControlIcon::LayoutRows))
        {
            ConfigureColourLayout(ColourRepositoryLayout::HorizontalStrips);
        }
    }
    else
    {
        float LayoutX = LayoutTrack.X + 3.0f;
        if (ConstructSegmentedOption(
                Canvas,
                { LayoutX, LayoutTrack.Y + 3.0f, 78.0f, 24.0f },
                "Grid",
                StandardLayout == StandardRepositoryLayout::Grid,
                PointerInput,
                AppearanceColors,
                9.5f,
                ControlIcon::LayoutGrid))
        {
            ConfigureStandardLayout(StandardRepositoryLayout::Grid);
        }
        LayoutX += 81.0f;
        if (ConstructSegmentedOption(
                Canvas,
                { LayoutX, LayoutTrack.Y + 3.0f, 78.0f, 24.0f },
                "List",
                StandardLayout == StandardRepositoryLayout::List,
                PointerInput,
                AppearanceColors,
                9.5f,
                ControlIcon::LayoutList))
        {
            ConfigureStandardLayout(StandardRepositoryLayout::List);
        }
    }

    CurY += 37.0f;

    // 3. Dedicated Colour Filters Bar (ONLY visible when Colours category is active)
    if (Category == ContentCategory::Colours)
    {
        Canvas.Text({ DrawerRect.X + PadX, CurY + 7.0f }, 9.0f, EyebrowInk, "COLOUR FILTERS");
        float ChipX = DrawerRect.X + PadX + 104.0f;

        constexpr std::array<ColourFilterGroup, 8> FilterOrder = {{
            ColourFilterGroup::All,
            ColourFilterGroup::AugustBeigeAndBrown,
            ColourFilterGroup::RedsAndBerries,
            ColourFilterGroup::Yellows,
            ColourFilterGroup::Greens,
            ColourFilterGroup::Blues,
            ColourFilterGroup::Purples,
            ColourFilterGroup::Neutrals,
        }};

        for (const ColourFilterGroup Group : FilterOrder)
        {
            const char* Title = ColourCodex::QueryFilterGroupTitle(Group);
            const float BtnW  = std::max(54.0f, Canvas.MeasureTextWidth(9.5f, Title) + 22.0f);
            if (ChipX + BtnW > DrawerRect.Right() - PadX - 122.0f)
            {
                break;
            }
            if (ConstructCapsuleControl(Canvas, { ChipX, CurY, BtnW, 25.0f }, Title, ColourFilter == Group, PointerInput, AppearanceColors, 9.5f))
            {
                ConfigureColourFilter(Group);
            }
            ChipX += BtnW + 6.0f;
        }

        const std::string SortCaption = std::string("Sort: ") + ColourCodex::QuerySortOrderTitle(ColourSort);
        if (ConstructCapsuleControl(
                Canvas,
                { DrawerRect.Right() - PadX - 114.0f, CurY, 114.0f, 25.0f },
                SortCaption.c_str(),
                false,
                PointerInput,
                AppearanceColors,
                9.5f))
        {
            const uint8_t NextSort = (static_cast<uint8_t>(ColourSort) + 1u) % 3u;
            ConfigureColourSort(static_cast<ColourSortOrder>(NextSort));
        }

        CurY += 33.0f;
    }

    Canvas.StrokeLine({ DrawerRect.X + PadX, CurY }, { DrawerRect.Right() - PadX, CurY }, AppearanceColors.Divider.ToRgba8(), 1.0f);
    CurY += 10.0f;

    // 4. Three-Column Body: Left Permitted-Only Category Rail | Centre Repository | Right Content Details Tile
    constexpr float FooterH  = 32.0f;
    const float     BodyH    = std::max(60.0f, DrawerRect.Bottom() - FooterH - CurY);
    const float     RailW    = 148.0f;
    const float     DetailsW = 286.0f;
    const float     CentreW  = std::max(160.0f, DrawerRect.Width - PadX * 2.0f - RailW - DetailsW - 32.0f);

    const Rectangle2D RailArea{ DrawerRect.X + PadX, CurY, RailW, BodyH };
    const Rectangle2D CentreArea{ RailArea.Right() + 16.0f, CurY, CentreW, BodyH };
    const Rectangle2D DetailsArea{ CentreArea.Right() + 16.0f, CurY, DetailsW, BodyH };

    const ColorRgba RailActiveFill = BlendRgba(AppearanceColors.Selected, AppearanceColors.Accent, AppearanceColors.LightSurface ? 0.14f : 0.22f);
    const ColorRgba RailHoverFill  = AppearanceColors.Raised.ToRgba8();
    const ColorRgba RailAccentBar  = AppearanceColors.Accent.ToRgba8();
    const ColorRgba RailActiveInk  = AppearanceColors.Text.ToRgba8();
    const ColorRgba RailIdleInk    = AppearanceColors.TextDim.ToRgba8();

    Canvas.PushClip(RailArea);
    float RailY = RailArea.Y + 2.0f;
    for (const ContentCategory CategoryOption : OrderedRailCategories)
    {
        // 🔴 Completely exclude blocked categories in specialized workspaces (e.g. Particles/Simulations/Audio in UV)
        if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, CategoryOption))
        {
            continue;
        }
        if (RailY + 26.0f > RailArea.Bottom())
        {
            break;
        }

        const Rectangle2D CatBtn{ RailArea.X, RailY, RailArea.Width - 1.0f, 26.0f };
        const bool        ActiveCat = (Category == CategoryOption);
        const bool        Hovered   = CatBtn.Contains(Cursor);
        if (ActiveCat || Hovered)
        {
            Canvas.FillRectangle(CatBtn, ActiveCat ? RailActiveFill : RailHoverFill, 0.0f);
        }
        if (ActiveCat)
        {
            Canvas.FillRectangle({ CatBtn.X, CatBtn.Y, 3.0f, CatBtn.Height }, RailAccentBar, 0.0f);
        }
        Canvas.Text(
            { CatBtn.X + 12.0f, CatBtn.Y + 6.5f },
            10.2f,
            ActiveCat ? RailActiveInk : (Hovered ? RailActiveInk : RailIdleInk),
            ThumbnailSheetRepository::QueryCategoryTitle(CategoryOption));

        if (Hovered && PointerInput.Pressed)
        {
            (void)ActivateCategory(CategoryOption);
        }
        RailY += 27.0f;
    }
    Canvas.PopClip();
    Canvas.StrokeLine({ RailArea.Right(), RailArea.Y }, { RailArea.Right(), RailArea.Bottom() }, AppearanceColors.Divider.ToRgba8(), 1.0f);

    if (Category == ContentCategory::Colours)
    {
        RenderColoursRepository(Canvas, CentreArea, PointerInput, AppearanceColors, SectionRadius);
    }
    else
    {
        RenderNonColoursRepository(Canvas, CentreArea, PointerInput, AppearanceColors, SectionRadius);
    }

    RenderContentDetailsSidebar(Canvas, DetailsArea, PointerInput, AppearanceColors, SectionRadius);

    // 5. Footer Status Bar
    const Rectangle2D FooterRect{ DrawerRect.X + PadX, DrawerRect.Bottom() - FooterH, DrawerRect.Width - PadX * 2.0f, FooterH };
    Canvas.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, AppearanceColors.Divider.ToRgba8(), 1.0f);

    const char* DefaultHint =
        (Category == ContentCategory::Colours)
            ? "Drag any .ColourSwatch circle/strip onto a colour slot or double-click to apply"
            : "Pre-baked 1024x1024 ThumbnailSheet + 64B ContentIndex · Drop external files to quarantine & import";
    Canvas.Text(
        { FooterRect.X, FooterRect.Y + 9.0f },
        9.5f,
        AppearanceColors.TextDim.ToRgba8(),
        StatusMessage.empty() ? DefaultHint : StatusMessage.c_str());

    const std::string RightMeta = std::string(Threading::AssetStreamThread::QueryHardwareTierTitle(StreamChannelThread.QueryResolvedTier())) +
                                  " · " + ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) + " Mask";
    Canvas.TextRightAligned(FooterRect, 9.0f, AppearanceColors.TextFaint.ToRgba8(), RightMeta.c_str());

    Canvas.PopClip();

    // Cursor-tracked colour marker while actively dragging onto workspace colour slots
    if (DraggingColour)
    {
        if (const ColourCodexEntry* Dragged = CodexLibrary.QueryEntryAt(DraggedColourIndex))
        {
            Canvas.FillCircle(Cursor, 15.0f, Dragged->FillColour);
            Canvas.StrokeCircle(Cursor, 15.0f, AppearanceColors.Text.ToRgba8(), 2.0f);
        }
    }
}

void ContentBrowserPanel::Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr || HostBoundsPhysical.Width <= 4.0f || HostBoundsPhysical.Height <= 4.0f)
    {
        return;
    }

    const float       SafeScale = std::clamp(ScaleFactor, 0.75f, 2.0f);
    ImDrawList*       FgList    = ImGui::GetForegroundDrawList();
    PixelSpace        Canvas(FgList, { HostBoundsPhysical.X, HostBoundsPhysical.Y }, SafeScale);
    const Rectangle2D LogicalBounds{
        0.0f,
        0.0f,
        HostBoundsPhysical.Width / SafeScale,
        HostBoundsPhysical.Height / SafeScale,
    };

    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    if (QueryOpen())
    {
        if (Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::F, false))
        {
            PopulateEditBufferFromString(SearchEditor, SearchBuffer);
            SearchEditor.Active     = true;
            SearchEditor.BlinkPhase = 0.0f;
            SearchEditor.SelectAll();
        }

        if (SearchEditor.Active)
        {
            bool BufferDirty = false;
            const std::string_view TypedText = Snapshot.QueryTypedUtf8();
            if (!TypedText.empty())
            {
                SearchEditor.Insert(Snapshot.TypedUtf8Buffer.data());
                BufferDirty = true;
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Backspace, true))
            {
                SearchEditor.Backspace();
                BufferDirty = true;
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Delete, true))
            {
                SearchEditor.DeleteForward();
                BufferDirty = true;
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::LeftArrow, true))
            {
                SearchEditor.MoveCaret(-1, Snapshot.ShiftHeld);
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::RightArrow, true))
            {
                SearchEditor.MoveCaret(1, Snapshot.ShiftHeld);
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Home, false))
            {
                SearchEditor.MoveHome(Snapshot.ShiftHeld);
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::End, false))
            {
                SearchEditor.MoveEnd(Snapshot.ShiftHeld);
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Enter, false) ||
                Snapshot.QueryKeyPressed(Input::InputLogicalKey::KeypadEnter, false))
            {
                SearchEditor.Active = false;
            }
            if (Snapshot.QueryKeyPressed(Input::InputLogicalKey::Escape, false))
            {
                ConfigureSearchQuery({});
                SearchEditor.Active = false;
            }
            if (BufferDirty)
            {
                SearchBuffer.assign(SearchEditor.Text, SearchEditor.Length);
                RepositoryScrollY = 0.0f;
            }
        }
    }

    ContentBrowserPointerInput Pointer{};
    Pointer.CursorX       = (Snapshot.PositionX - HostBoundsPhysical.X) / SafeScale;
    Pointer.CursorY       = (Snapshot.PositionY - HostBoundsPhysical.Y) / SafeScale;
    Pointer.Pressed       = Snapshot.QueryButtonPressed(0u);
    Pointer.HeldDown      = Snapshot.QueryButtonDown(0u);
    Pointer.Released      = Snapshot.QueryButtonReleased(0u);
    Pointer.DoubleClicked = Snapshot.QueryButtonDoubleClicked(0u);
    Pointer.WheelDeltaY   = Snapshot.ScrollDeltaY;

    RenderWithInput(Canvas, LogicalBounds, Pointer, DeltaSeconds);
}

} // namespace Frontier
