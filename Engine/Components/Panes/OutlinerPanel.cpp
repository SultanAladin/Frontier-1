/*============================================================================================================================================
                                                              OUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

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

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool CaseInsensitiveContains(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Haystack.size() < Needle.size())
    {
        return false;
    }

    for (std::size_t Offset = 0u; Offset + Needle.size() <= Haystack.size(); ++Offset)
    {
        bool Match = true;
        for (std::size_t Index = 0u; Index < Needle.size(); ++Index)
        {
            const auto Left  = static_cast<unsigned char>(Haystack[Offset + Index]);
            const auto Right = static_cast<unsigned char>(Needle[Index]);
            if (std::tolower(Left) != std::tolower(Right))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            LIFECYCLE & CONSTRUCT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

OutlinerPanel::OutlinerPanel() noexcept
{
    ConstructTemplates = {
        { "Add Item 1", "Item 1", "Type 1", "Position 0, 0, 0 m", ConstructSlotCategory::CategoryOne  },
        { "Add Item 2", "Item 2", "Type 2", "Position 0, 0, 0 m", ConstructSlotCategory::CategoryTwo },
        { "Add Item 3", "Item 3", "Type 3", "Position 0, 0, 0 m", ConstructSlotCategory::CategoryThree  },
        { "Add Item 4", "Item 4", "Type 4", "Position 0, 0, 0 m", ConstructSlotCategory::CategoryFour },
    };
}

void OutlinerPanel::ConfigureTitleVariant(OutlinerTitleVariant Variant) noexcept
{
    TitleVariant = Variant;
}

void OutlinerPanel::ConfigureConstructTemplates(std::span<const OutlinerConstructTemplate> Templates)
{
    ConstructTemplates.assign(Templates.begin(), Templates.end());
}

void OutlinerPanel::ConfigureFooterReadings(const OutlinerFooterReadings& Readings)
{
    FooterReadings = Readings;
}

const char* OutlinerPanel::QueryHeadingText() const noexcept
{
    return (TitleVariant == OutlinerTitleVariant::LayerStack) ? "LayerStack" : "Outliner";
}

std::string OutlinerPanel::GenerateUniqueTitle(std::string_view StemTitle) const
{
    std::string Candidate(StemTitle.empty() ? "Entry" : StemTitle);
    auto Exists = [&](std::string_view Name) noexcept
    {
        for (const auto& Existing : Entries)
        {
            if (Existing.Title == Name)
            {
                return true;
            }
        }
        return false;
    };

    if (!Exists(Candidate))
    {
        return Candidate;
    }

    for (uint32_t Suffix = 2u; Suffix < 10000u; ++Suffix)
    {
        char Buffer[160]{};
        std::snprintf(Buffer, sizeof(Buffer), "%.*s %u", static_cast<int>(StemTitle.size()), StemTitle.data(), Suffix);
        if (!Exists(Buffer))
        {
            return std::string(Buffer);
        }
    }
    return Candidate;
}

OutlinerSlotToken OutlinerPanel::ConstructFromTemplate(uint32_t TemplateIndex, OutlinerSlotToken ExplicitEnclosure)
{
    if (ConstructTemplates.empty())
    {
        return {};
    }

    const auto& Preset = ConstructTemplates[std::min<std::size_t>(TemplateIndex, ConstructTemplates.size() - 1u)];
    OutlinerSlotToken TargetEnclosure = ExplicitEnclosure;
    if (!TargetEnclosure.Valid())
    {
        const OutlinerSlotToken Primary = QueryPrimarySelection();
        if (const OutlinerRecordEntry* Selected = FindEntry(Primary))
        {
            if (Selected->Category == ConstructSlotCategory::Folder)
            {
                TargetEnclosure = Selected->Token;
            }
            else
            {
                TargetEnclosure = Selected->EnclosingToken;
            }
        }
    }

    OutlinerRecordEntry Created{};
    Created.Token             = OutlinerSlotToken{ NextTokenCounter++ };
    Created.EnclosingToken    = (FindEntry(TargetEnclosure) != nullptr) ? TargetEnclosure : OutlinerSlotToken{};
    Created.Category          = Preset.Category;
    Created.Title             = GenerateUniqueTitle(Preset.DefaultTitle);
    Created.CategoryLabel     = Preset.CategoryTitle;
    Created.Subtitle          = Preset.DefaultDetail;
    Created.VisibilityEnabled = true;
    Created.Expanded          = true;

    if (OutlinerRecordEntry* EnclosureEntry = AccessEntry(Created.EnclosingToken))
    {
        EnclosureEntry->Expanded = true;
    }

    Entries.push_back(std::move(Created));
    const OutlinerSlotToken Issued = Entries.back().Token;
    SelectSingle(Issued);
    return Issued;
}

OutlinerSlotToken OutlinerPanel::ConstructFolder(std::string_view FolderTitle, OutlinerSlotToken ExplicitEnclosure)
{
    OutlinerSlotToken TargetEnclosure = ExplicitEnclosure;
    if (TargetEnclosure.Valid() && FindEntry(TargetEnclosure) == nullptr)
    {
        TargetEnclosure = {};
    }

    OutlinerRecordEntry Created{};
    Created.Token             = OutlinerSlotToken{ NextTokenCounter++ };
    Created.EnclosingToken    = TargetEnclosure;
    Created.Category          = ConstructSlotCategory::Folder;
    Created.Title             = GenerateUniqueTitle(FolderTitle.empty() ? "Folder" : FolderTitle);
    Created.CategoryLabel     = "Collection";
    Created.Subtitle          = "Scene collection";
    Created.FolderTint        = WorkspaceSurfaceColors::ShowcaseSage;
    Created.VisibilityEnabled = true;
    Created.Expanded          = true;

    if (OutlinerRecordEntry* EnclosureEntry = AccessEntry(Created.EnclosingToken))
    {
        EnclosureEntry->Expanded = true;
    }

    Entries.push_back(std::move(Created));
    const OutlinerSlotToken Issued = Entries.back().Token;
    SelectSingle(Issued);
    return Issued;
}

bool OutlinerPanel::DeleteSlot(OutlinerSlotToken Token) noexcept
{
    if (!Token.Valid() || FindEntry(Token) == nullptr)
    {
        return false;
    }

    std::vector<OutlinerSlotToken> Condemned{ Token };
    for (std::size_t Cursor = 0u; Cursor < Condemned.size(); ++Cursor)
    {
        const OutlinerSlotToken Current = Condemned[Cursor];
        for (const auto& Entry : Entries)
        {
            if (Entry.EnclosingToken == Current)
            {
                Condemned.push_back(Entry.Token);
            }
        }
    }

    const auto MatchesCondemned = [&](OutlinerSlotToken Candidate) noexcept
    {
        return std::find(Condemned.begin(), Condemned.end(), Candidate) != Condemned.end();
    };

    Entries.erase(
        std::remove_if(
            Entries.begin(),
            Entries.end(),
            [&](const OutlinerRecordEntry& Entry) noexcept { return MatchesCondemned(Entry.Token); }),
        Entries.end());

    SelectedTokens.erase(
        std::remove_if(SelectedTokens.begin(), SelectedTokens.end(), MatchesCondemned),
        SelectedTokens.end());

    if (MatchesCondemned(SelectionAnchor))
    {
        SelectionAnchor = SelectedTokens.empty() ? OutlinerSlotToken{} : SelectedTokens.front();
    }
    return true;
}

uint32_t OutlinerPanel::DeleteSelection() noexcept
{
    const std::vector<OutlinerSlotToken> Snapshot = SelectedTokens;
    uint32_t                             Removed  = 0u;
    for (const OutlinerSlotToken Token : Snapshot)
    {
        if (DeleteSlot(Token))
        {
            ++Removed;
        }
    }
    return Removed;
}

OutlinerSlotToken OutlinerPanel::DuplicateSlot(OutlinerSlotToken Token)
{
    const OutlinerRecordEntry* Source = FindEntry(Token);
    if (Source == nullptr)
    {
        return {};
    }

    OutlinerRecordEntry Copy = *Source;
    Copy.Token               = OutlinerSlotToken{ NextTokenCounter++ };
    Copy.Title               = GenerateUniqueTitle(Source->Title);
    Entries.push_back(std::move(Copy));

    const OutlinerSlotToken Issued = Entries.back().Token;
    SelectSingle(Issued);
    return Issued;
}

OutlinerSlotToken OutlinerPanel::GroupSelectionIntoFolder(std::string_view FolderTitle)
{
    if (SelectedTokens.empty())
    {
        return ConstructFolder(FolderTitle);
    }

    const std::vector<OutlinerSlotToken> SelectedSnapshot = SelectedTokens;
    OutlinerSlotToken                    SharedEnclosure{};
    if (const OutlinerRecordEntry* First = FindEntry(SelectedSnapshot.front()))
    {
        SharedEnclosure = First->EnclosingToken;
    }

    const OutlinerSlotToken FolderToken = ConstructFolder(FolderTitle, SharedEnclosure);
    for (const OutlinerSlotToken SlotToken : SelectedSnapshot)
    {
        if (SlotToken != FolderToken)
        {
            AttachToEnclosure(SlotToken, FolderToken);
        }
    }
    SelectSingle(FolderToken);
    return FolderToken;
}

void OutlinerPanel::ClearAll() noexcept
{
    Entries.clear();
    SelectedTokens.clear();
    SelectionAnchor    = {};
    ScrollOffsetY      = 0.0f;
    DirectiveSheetOpen   = false;
    DragActive         = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TREE ATTACHMENT, REORDERING & SELECTION
//------------------------------------------------------------------------------------------------------------------------

const OutlinerRecordEntry* OutlinerPanel::FindEntry(OutlinerSlotToken Token) const noexcept
{
    if (!Token.Valid())
    {
        return nullptr;
    }
    for (const auto& Entry : Entries)
    {
        if (Entry.Token == Token)
        {
            return &Entry;
        }
    }
    return nullptr;
}

OutlinerRecordEntry* OutlinerPanel::AccessEntry(OutlinerSlotToken Token) noexcept
{
    if (!Token.Valid())
    {
        return nullptr;
    }
    for (auto& Entry : Entries)
    {
        if (Entry.Token == Token)
        {
            return &Entry;
        }
    }
    return nullptr;
}

bool OutlinerPanel::ContainsNestedSlot(OutlinerSlotToken PotentialEnclosure, OutlinerSlotToken Candidate) const noexcept
{
    if (!PotentialEnclosure.Valid() || !Candidate.Valid())
    {
        return false;
    }

    OutlinerSlotToken Cursor = Candidate;
    uint32_t          Guard  = 0u;
    while (Cursor.Valid() && Guard++ <= Entries.size())
    {
        const OutlinerRecordEntry* Entry = FindEntry(Cursor);
        if (Entry == nullptr)
        {
            return false;
        }
        if (Entry->EnclosingToken == PotentialEnclosure)
        {
            return true;
        }
        Cursor = Entry->EnclosingToken;
    }
    return false;
}

bool OutlinerPanel::AttachToEnclosure(OutlinerSlotToken Token, OutlinerSlotToken NewEnclosure) noexcept
{
    if (!Token.Valid() || Token == NewEnclosure)
    {
        return false;
    }

    OutlinerRecordEntry* Moving = AccessEntry(Token);
    if (Moving == nullptr)
    {
        return false;
    }

    if (NewEnclosure.Valid())
    {
        if (FindEntry(NewEnclosure) == nullptr || ContainsNestedSlot(Token, NewEnclosure))
        {
            return false;
        }
        if (OutlinerRecordEntry* TargetEnclosure = AccessEntry(NewEnclosure))
        {
            TargetEnclosure->Expanded = true;
        }
    }

    Moving->EnclosingToken = NewEnclosure;
    return true;
}

bool OutlinerPanel::RepositionRelativeToTarget(
    OutlinerSlotToken     MovingToken,
    OutlinerSlotToken     TargetToken,
    DropRelativePlacement Placement) noexcept
{
    if (!MovingToken.Valid() || !TargetToken.Valid() || MovingToken == TargetToken)
    {
        return false;
    }

    if (Placement == DropRelativePlacement::InsideTarget)
    {
        return AttachToEnclosure(MovingToken, TargetToken);
    }

    const OutlinerRecordEntry* Target = FindEntry(TargetToken);
    if (Target == nullptr)
    {
        return false;
    }
    if (Target->EnclosingToken.Valid() &&
        (Target->EnclosingToken == MovingToken || ContainsNestedSlot(MovingToken, Target->EnclosingToken)))
    {
        return false;
    }

    auto MovingIterator = std::find_if(
        Entries.begin(),
        Entries.end(),
        [&](const OutlinerRecordEntry& Entry) noexcept { return Entry.Token == MovingToken; });
    if (MovingIterator == Entries.end())
    {
        return false;
    }

    OutlinerRecordEntry Extracted = std::move(*MovingIterator);
    Extracted.EnclosingToken      = Target->EnclosingToken;
    Entries.erase(MovingIterator);

    auto TargetIterator = std::find_if(
        Entries.begin(),
        Entries.end(),
        [&](const OutlinerRecordEntry& Entry) noexcept { return Entry.Token == TargetToken; });
    if (TargetIterator == Entries.end())
    {
        Entries.push_back(std::move(Extracted));
        return true;
    }

    if (Placement == DropRelativePlacement::AfterTarget)
    {
        ++TargetIterator;
    }
    Entries.insert(TargetIterator, std::move(Extracted));
    return true;
}

bool OutlinerPanel::QuerySlotSelected(OutlinerSlotToken Token) const noexcept
{
    return Token.Valid() && std::find(SelectedTokens.begin(), SelectedTokens.end(), Token) != SelectedTokens.end();
}

OutlinerSlotToken OutlinerPanel::QueryPrimarySelection() const noexcept
{
    return SelectedTokens.empty() ? OutlinerSlotToken{} : SelectedTokens.front();
}

void OutlinerPanel::SelectSingle(OutlinerSlotToken Token) noexcept
{
    SelectedTokens.clear();
    if (FindEntry(Token) != nullptr)
    {
        SelectedTokens.push_back(Token);
        SelectionAnchor = Token;
    }
    else
    {
        SelectionAnchor = {};
    }
}

void OutlinerPanel::SelectWithModifiers(OutlinerSlotToken Token, bool ShiftHeld, bool ControlHeld) noexcept
{
    if (FindEntry(Token) == nullptr)
    {
        return;
    }

    if (ShiftHeld && SelectionAnchor.Valid())
    {
        std::vector<OutlinerVisibleRow> VisibleRows{};
        BuildVisibleRows(VisibleRows);

        int32_t AnchorRow = -1;
        int32_t ClickRow  = -1;
        for (std::size_t Index = 0u; Index < VisibleRows.size(); ++Index)
        {
            if (VisibleRows[Index].Token == SelectionAnchor)
            {
                AnchorRow = static_cast<int32_t>(Index);
            }
            if (VisibleRows[Index].Token == Token)
            {
                ClickRow = static_cast<int32_t>(Index);
            }
        }

        if (AnchorRow >= 0 && ClickRow >= 0)
        {
            if (!ControlHeld)
            {
                SelectedTokens.clear();
            }
            const int32_t First = std::min(AnchorRow, ClickRow);
            const int32_t Last  = std::max(AnchorRow, ClickRow);
            for (int32_t Row = First; Row <= Last; ++Row)
            {
                const OutlinerSlotToken RowToken = VisibleRows[static_cast<std::size_t>(Row)].Token;
                if (!QuerySlotSelected(RowToken))
                {
                    SelectedTokens.push_back(RowToken);
                }
            }
            return;
        }
    }

    if (ControlHeld)
    {
        auto Found = std::find(SelectedTokens.begin(), SelectedTokens.end(), Token);
        if (Found != SelectedTokens.end())
        {
            SelectedTokens.erase(Found);
        }
        else
        {
            SelectedTokens.push_back(Token);
            SelectionAnchor = Token;
        }
        return;
    }

    SelectSingle(Token);
}

void OutlinerPanel::ClearSelection() noexcept
{
    SelectedTokens.clear();
    SelectionAnchor = {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                            VISIBILITY, COUNTERS & FILTERING
//------------------------------------------------------------------------------------------------------------------------

bool OutlinerPanel::QueryEffectiveVisibility(OutlinerSlotToken Token) const noexcept
{
    OutlinerSlotToken Cursor = Token;
    uint32_t          Guard  = 0u;
    while (Cursor.Valid() && Guard++ <= Entries.size())
    {
        const OutlinerRecordEntry* Entry = FindEntry(Cursor);
        if (Entry == nullptr)
        {
            return false;
        }
        if (!Entry->VisibilityEnabled)
        {
            return false;
        }
        Cursor = Entry->EnclosingToken;
    }
    return true;
}

uint32_t OutlinerPanel::QueryVisibleCount() const noexcept
{
    uint32_t Count = 0u;
    for (const auto& Entry : Entries)
    {
        if (QueryEffectiveVisibility(Entry.Token))
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t OutlinerPanel::QueryHiddenCount() const noexcept
{
    return QueryTotalCount() - QueryVisibleCount();
}

uint32_t OutlinerPanel::CountDirectNested(OutlinerSlotToken Enclosure) const noexcept
{
    uint32_t Count = 0u;
    for (const auto& Entry : Entries)
    {
        if (Entry.EnclosingToken == Enclosure)
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t OutlinerPanel::CountAllNested(OutlinerSlotToken Enclosure) const noexcept
{
    uint32_t Count = 0u;
    for (const auto& Entry : Entries)
    {
        if (ContainsNestedSlot(Enclosure, Entry.Token))
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t OutlinerPanel::CountNestedFolders(OutlinerSlotToken Enclosure) const noexcept
{
    uint32_t Count = 0u;
    for (const auto& Entry : Entries)
    {
        if (Entry.Category == ConstructSlotCategory::Folder && ContainsNestedSlot(Enclosure, Entry.Token))
        {
            ++Count;
        }
    }
    return Count;
}

void OutlinerPanel::ConfigureSearchQuery(std::string_view Query) noexcept
{
    std::string Temp(Query);
    SearchEditor.Begin(Temp.c_str());
    SearchEditor.Active = false;
}

void OutlinerPanel::ConfigureVisibilityFilter(OutlinerVisibilityFilter Filter) noexcept
{
    VisibilityFilter = Filter;
}

void OutlinerPanel::ConfigureCategoryFilter(OutlinerCategoryFilter Filter) noexcept
{
    CategoryFilter = Filter;
}

void OutlinerPanel::ConfigureFilterStripOpen(bool Open) noexcept
{
    FilterStripOpen = Open;
}

void OutlinerPanel::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = std::max(0.0f, OffsetY);
}

void OutlinerPanel::OpenConstructDropdown() noexcept
{
    ConstructDropdownOpen = true;
    SettingsDropdownOpen  = false;
    DirectiveSheetOpen      = false;
}

void OutlinerPanel::OpenHeaderSettingsDropdown() noexcept
{
    SettingsDropdownOpen  = true;
    ConstructDropdownOpen = false;
    DirectiveSheetOpen      = false;
}

void OutlinerPanel::OpenDirectiveSheetForSlot(OutlinerSlotToken Token, Point2D AnchorPosition) noexcept
{
    if (FindEntry(Token) != nullptr && !QuerySlotSelected(Token))
    {
        SelectSingle(Token);
    }
    DirectiveTargetToken    = Token;
    DirectiveSheetAnchor    = AnchorPosition;
    DirectiveSheetOpen      = true;
    ConstructDropdownOpen = false;
    SettingsDropdownOpen  = false;
}

void OutlinerPanel::CloseOverlays() noexcept
{
    ConstructDropdownOpen = false;
    SettingsDropdownOpen  = false;
    DirectiveSheetOpen      = false;
}

bool OutlinerPanel::MatchesActiveFilters(const OutlinerRecordEntry& Entry) const noexcept
{
    const bool EffectiveVisible = QueryEffectiveVisibility(Entry.Token);
    if (VisibilityFilter == OutlinerVisibilityFilter::VisibleOnly && !EffectiveVisible)
    {
        return false;
    }
    if (VisibilityFilter == OutlinerVisibilityFilter::HiddenOnly && EffectiveVisible)
    {
        return false;
    }

    switch (CategoryFilter)
    {
        case OutlinerCategoryFilter::All:         break;
        case OutlinerCategoryFilter::FoldersOnly: if (Entry.Category != ConstructSlotCategory::Folder) return false; break;
        case OutlinerCategoryFilter::FirstType:   if (Entry.Category != ConstructSlotCategory::CategoryOne) return false; break;
        case OutlinerCategoryFilter::SecondType:  if (Entry.Category != ConstructSlotCategory::CategoryTwo) return false; break;
        case OutlinerCategoryFilter::ThirdType:   if (Entry.Category != ConstructSlotCategory::CategoryThree) return false; break;
        case OutlinerCategoryFilter::FourthType:  if (Entry.Category != ConstructSlotCategory::CategoryFour) return false; break;
        case OutlinerCategoryFilter::WithNotes:   if (Entry.AuthorNotes.empty()) return false; break;
    }

    const std::string_view Query(SearchEditor.Text, SearchEditor.Length);
    if (!Query.empty())
    {
        if (!CaseInsensitiveContains(Entry.Title, Query) &&
            !CaseInsensitiveContains(Entry.CategoryLabel, Query) &&
            !CaseInsensitiveContains(Entry.Subtitle, Query) &&
            !CaseInsensitiveContains(Entry.AuthorNotes, Query))
        {
            return false;
        }
    }

    return true;
}

bool OutlinerPanel::SubtreeMatchesFilters(OutlinerSlotToken Token) const noexcept
{
    const OutlinerRecordEntry* Self = FindEntry(Token);
    if (Self == nullptr)
    {
        return false;
    }
    if (MatchesActiveFilters(*Self))
    {
        return true;
    }

    for (const auto& Candidate : Entries)
    {
        if (Candidate.EnclosingToken == Token && SubtreeMatchesFilters(Candidate.Token))
        {
            return true;
        }
    }
    return false;
}

void OutlinerPanel::AppendVisibleSubtree(
    OutlinerSlotToken                Enclosure,
    uint32_t                         Depth,
    bool                             EnclosingVisible,
    std::vector<OutlinerVisibleRow>& OutRows) const
{
    const bool ForceExpand = (SearchEditor.Length > 0u) ||
                             (VisibilityFilter != OutlinerVisibilityFilter::All) ||
                             (CategoryFilter != OutlinerCategoryFilter::All);

    for (const auto& Entry : Entries)
    {
        if (Entry.EnclosingToken != Enclosure)
        {
            continue;
        }
        if (!SubtreeMatchesFilters(Entry.Token))
        {
            continue;
        }

        const uint32_t DirectNested     = CountDirectNested(Entry.Token);
        const uint32_t AllNested        = CountAllNested(Entry.Token);
        const bool     EffectiveVisible = EnclosingVisible && Entry.VisibilityEnabled;

        OutRows.push_back(OutlinerVisibleRow{
            Entry.Token,
            Depth,
            AllNested,
            DirectNested > 0u,
            EffectiveVisible
        });

        if (DirectNested > 0u && (Entry.Expanded || ForceExpand))
        {
            AppendVisibleSubtree(Entry.Token, Depth + 1u, EffectiveVisible, OutRows);
        }
    }
}

void OutlinerPanel::BuildVisibleRows(std::vector<OutlinerVisibleRow>& OutRows) const
{
    OutRows.clear();
    AppendVisibleSubtree(OutlinerSlotToken{}, 0u, true, OutRows);
}

void OutlinerPanel::ExecuteSettingsDirective(OutlinerSettingsDirective Directive)
{
    LastSettingsDirective = Directive;
    switch (Directive)
    {
        case OutlinerSettingsDirective::ExpandAll:
            for (auto& Entry : Entries)
            {
                Entry.Expanded = true;
            }
            break;
        case OutlinerSettingsDirective::CollapseAll:
            for (auto& Entry : Entries)
            {
                Entry.Expanded = false;
            }
            break;
        case OutlinerSettingsDirective::ShowAllHidden:
            for (auto& Entry : Entries)
            {
                Entry.VisibilityEnabled = true;
            }
            break;
        case OutlinerSettingsDirective::ClearWorkspace:
            ClearAll();
            break;
        case OutlinerSettingsDirective::None:
        case OutlinerSettingsDirective::ImportEntries:
        case OutlinerSettingsDirective::ExportSelection:
            break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            RENDERING & INTERACTIVE HANDLING
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt,
    PixelSpace*             OverlaySurface)
{
    if (Bounds.Width <= 4.0f || Bounds.Height <= 4.0f)
    {
        return;
    }

    PixelSpace& ActiveOverlay = (OverlaySurface != nullptr) ? *OverlaySurface : Surface;
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    OverlayOpenBefore = ConstructDropdownOpen || SettingsDropdownOpen || DirectiveSheetOpen;

    WorkspaceAppearance::ForwardInputToTextBuffer(SearchEditor, Input, Δt);
    if (Input.EscapePressed)
    {
        CloseOverlays();
        DragActive = false;
    }

    const ColorRgba DockFill = WorkspaceAppearance::QueryDockedPaneBackground();
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, DockFill);

    // 1. Heading row (.outliner-heading: 63px height, 14px 22px 12px padding)
    const Rectangle2D HeadingBounds{ Bounds.X, Bounds.Y, Bounds.Width, 63.0f };
    const char*       HeadingTitle = QueryHeadingText();
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Light,
        { HeadingBounds.X + 22.0f, HeadingBounds.Y + 18.0f },
        20.0f,
        WorkspaceSurfaceColors::PrimaryInk,
        HeadingTitle);

    const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 20.0f, HeadingTitle);
    char        CountCaption[96]{};
    std::snprintf(
        CountCaption,
        sizeof(CountCaption),
        "%u %s",
        QueryTotalCount(),
        (QueryTotalCount() == 1u) ? "entry" : "entries");

    const Rectangle2D SettingsButton{ HeadingBounds.Right() - 18.0f - 29.0f, HeadingBounds.Y + 17.0f, 29.0f, 29.0f };
    const Rectangle2D PlusButton{ SettingsButton.X - 6.0f - 29.0f, HeadingBounds.Y + 17.0f, 29.0f, 29.0f };

    const float SubtitleX    = HeadingBounds.X + 22.0f + TitleW + 10.0f;
    const float SubtitleMaxW = std::max(0.0f, PlusButton.X - 8.0f - SubtitleX);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { SubtitleX, HeadingBounds.Y + 25.0f },
        SubtitleMaxW,
        10.0f,
        ColorRgba{ 92, 92, 92, 255 },
        CountCaption);

    // "+" Construct button
    {
        const bool Hovered = PlusButton.Contains(Cursor);
        if (Hovered || ConstructDropdownOpen)
        {
            Surface.FillCircle(PlusButton.Center(), 14.5f, ConstructDropdownOpen ? ColorRgba{ 56, 56, 56, 255 } : ColorRgba{ 38, 38, 38, 255 });
        }
        else
        {
            Surface.FillCircle(PlusButton.Center(), 14.5f, ColorRgba{ 28, 28, 28, 255 });
            Surface.StrokeCircle(PlusButton.Center(), 14.5f, ColorRgba{ 48, 48, 48, 255 }, 1.0f);
        }
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::PlusAdd,
            VectorIconPlacement{
                PlusButton.X + 6.5f,
                PlusButton.Y + 6.5f,
                16.0f,
                (Hovered || ConstructDropdownOpen) ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 186, 186, 186, 255 },
                2.0f });

        ConstructDropdownAnchor = { PlusButton.Right(), PlusButton.Bottom() + 5.0f };
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ConstructDropdownOpen = !ConstructDropdownOpen;
            SettingsDropdownOpen  = false;
            DirectiveSheetOpen      = false;
        }
    }

    // Header Settings button (filter sliders icon)
    {
        const bool Hovered  = SettingsButton.Contains(Cursor);
        const ColorRgba BtnBg = (Hovered || SettingsDropdownOpen)
                                    ? (SettingsDropdownOpen ? ColorRgba{ 56, 56, 56, 255 } : ColorRgba{ 38, 38, 38, 255 })
                                    : DockFill;
        if (Hovered || SettingsDropdownOpen)
        {
            Surface.FillCircle(SettingsButton.Center(), 14.5f, BtnBg);
        }
        WorkspaceAppearance::ConstructFilterSlidersIcon(
            Surface,
            { SettingsButton.X + 6.5f, SettingsButton.Y + 6.5f },
            16.0f,
            (Hovered || SettingsDropdownOpen) ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 122, 122, 122, 255 },
            BtnBg);

        SettingsDropdownAnchor = { SettingsButton.Right(), SettingsButton.Bottom() + 5.0f };
        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            SettingsDropdownOpen  = !SettingsDropdownOpen;
            ConstructDropdownOpen = false;
            DirectiveSheetOpen      = false;
        }
    }

    // 2. Visible / Hidden stats tiles (.stats: 64px height, 20px radius)
    const float       StatsY     = HeadingBounds.Bottom();
    const float       StatsTileW = std::max(40.0f, (Bounds.Width - 28.0f - 8.0f) * 0.5f);
    const Rectangle2D VisibleTile{ Bounds.X + 14.0f, StatsY, StatsTileW, 64.0f };
    const Rectangle2D HiddenTile{ VisibleTile.Right() + 8.0f, StatsY, StatsTileW, 64.0f };

    auto ConstructStatsCounterTile = [&](Rectangle2D Tile, bool VisibleSlot, uint32_t Count, bool ActiveFilter)
    {
        const bool Hovered = !OverlayOpenBefore && Tile.Contains(Cursor);
        Surface.FillRectangle(Tile, Hovered ? ColorRgba{ 31, 31, 31, 255 } : WorkspaceSurfaceColors::ControlWell, 20.0f);
        Surface.StrokeRectangle(
            Tile,
            ActiveFilter ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::ControlWellBorder,
            20.0f,
            1.0f);

        const Point2D DiscCenter{ Tile.X + 23.0f, Tile.Y + 22.0f };
        if (VisibleSlot)
        {
            VisualToken VisibleDisc{};
            VisibleDisc.Shape     = VisualTokenShape::Circle;
            VisibleDisc.FillColor = WorkspaceSurfaceColors::StatusGreen;
            VisibleDisc.Extent    = 22.0f;
            WorkspaceAppearance::ConstructVisualToken(Surface, DiscCenter, VisibleDisc);
            WorkspaceAppearance::ConstructCheckSymbol(Surface, DiscCenter, 10.5f, WorkspaceSurfaceColors::StatusGreenInk, 1.9f);
        }
        else
        {
            VisualToken HiddenDisc{};
            HiddenDisc.Shape     = VisualTokenShape::Circle;
            HiddenDisc.FillColor = ColorRgba{ 36, 36, 36, 255 };
            HiddenDisc.Extent    = 22.0f;
            WorkspaceAppearance::ConstructVisualToken(Surface, DiscCenter, HiddenDisc);
            WorkspaceAppearance::ConstructCautionTriangleSign(Surface, DiscCenter, 11.5f, ColorRgba{ 126, 126, 126, 255 });
        }

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Tile.X + 12.0f, Tile.Bottom() - 21.0f },
            10.0f,
            ColorRgba{ 161, 161, 161, 255 },
            VisibleSlot ? "Visible" : "Hidden");

        char NumberBuffer[32]{};
        std::snprintf(NumberBuffer, sizeof(NumberBuffer), "%u", Count);
        const float NumberW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 28.0f, NumberBuffer);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { Tile.Right() - 14.0f - NumberW, Tile.Bottom() - 36.0f },
            28.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            NumberBuffer);

        return Hovered && Input.PrimaryPointer.Pressed;
    };

    if (ConstructStatsCounterTile(
            VisibleTile,
            true,
            QueryVisibleCount(),
            VisibilityFilter == OutlinerVisibilityFilter::VisibleOnly))
    {
        VisibilityFilter = (VisibilityFilter == OutlinerVisibilityFilter::VisibleOnly)
                               ? OutlinerVisibilityFilter::All
                               : OutlinerVisibilityFilter::VisibleOnly;
    }
    if (ConstructStatsCounterTile(
            HiddenTile,
            false,
            QueryHiddenCount(),
            VisibilityFilter == OutlinerVisibilityFilter::HiddenOnly))
    {
        VisibilityFilter = (VisibilityFilter == OutlinerVisibilityFilter::HiddenOnly)
                               ? OutlinerVisibilityFilter::All
                               : OutlinerVisibilityFilter::HiddenOnly;
    }

    // 3. Search + Reusable Studio Controls Filter dropdown button (.outliner-search: 40px height)
    const float       SearchY      = VisibleTile.Bottom() + 10.0f;
    const float       FilterBtnW   = 108.0f;
    const Rectangle2D SearchBounds{ Bounds.X + 14.0f, SearchY, std::max(60.0f, Bounds.Width - 28.0f - 8.0f - FilterBtnW), 40.0f };
    const Rectangle2D FilterButton{ SearchBounds.Right() + 8.0f, SearchY, FilterBtnW, 40.0f };

    Surface.FillRectangle(SearchBounds, WorkspaceSurfaceColors::ControlWell, 20.0f);
    Surface.StrokeRectangle(
        SearchBounds,
        SearchEditor.Active ? ColorRgba{ 120, 126, 142, 255 } : WorkspaceSurfaceColors::ControlWellBorder,
        20.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::SearchGlass,
        VectorIconPlacement{ SearchBounds.X + 12.0f, SearchBounds.Y + 12.0f, 15.0f, ColorRgba{ 108, 108, 108, 255 }, 1.85f });

    if (!OverlayOpenBefore && Input.PrimaryPointer.Pressed)
    {
        if (SearchBounds.Contains(Cursor))
        {
            if (!SearchEditor.Active)
            {
                SearchEditor.Active = true;
                SearchEditor.Caret  = SearchEditor.Length;
                SearchEditor.SelectionAnchor = SearchEditor.Caret;
            }
        }
        else if (SearchEditor.Active)
        {
            SearchEditor.Finalize();
        }
    }

    if (SearchEditor.Length == 0u && !SearchEditor.Active)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { SearchBounds.X + 34.0f, SearchBounds.Y + 13.0f },
            11.0f,
            ColorRgba{ 105, 105, 105, 255 },
            "Search   Ctrl+F");
    }
    else
    {
        const std::string_view QueryView(SearchEditor.Text, SearchEditor.Length);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { SearchBounds.X + 34.0f, SearchBounds.Y + 13.0f },
            SearchBounds.Width - 60.0f,
            11.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            QueryView);

        if (SearchEditor.Active && SearchEditor.BlinkPhase < ControlMetrics::CaretBlinkSeconds * 0.5f)
        {
            const float CaretX = SearchBounds.X + 34.0f +
                                 std::min(
                                     SearchBounds.Width - 64.0f,
                                     WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, QueryView));
            Surface.StrokeLine({ CaretX, SearchBounds.Y + 11.0f }, { CaretX, SearchBounds.Bottom() - 11.0f }, WorkspaceSurfaceColors::PrimaryInk, 1.2f);
        }

        if (SearchEditor.Length > 0u)
        {
            const Rectangle2D ClearHit{ SearchBounds.Right() - 26.0f, SearchBounds.Y + 10.0f, 20.0f, 20.0f };
            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::CloseCross,
                VectorIconPlacement{ ClearHit.X + 4.0f, ClearHit.Y + 4.0f, 12.0f, ColorRgba{ 132, 132, 132, 255 }, 1.8f });
            if (!OverlayOpenBefore && ClearHit.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                ConfigureSearchQuery("");
            }
        }
    }

    // Filter dropdown trigger (Reusable Studio Controls DropdownSelector style)
    {
        ControlPointer FilterPointer = Input.PrimaryPointer;
        if (OverlayOpenBefore)
        {
            FilterPointer.Enabled = false;
        }
        const ControlContact FilterHit = DropdownSelector::ConstructButton(
            Surface,
            FilterButton,
            "Filter",
            FilterStripOpen,
            FilterPointer,
            1.0f,
            ControlIcon::SlidersQuality);
        if (FilterHit.Pressed)
        {
            FilterStripOpen = !FilterStripOpen;
        }
    }

    float ContentTopY = SearchBounds.Bottom() + 6.0f;

    // Optional Filter Chips strip (.filter-chips)
    if (FilterStripOpen)
    {
        struct FilterChipEntry
        {
            const char*            Label;
            OutlinerCategoryFilter Filter;
        };
        constexpr std::array<FilterChipEntry, 7> Chips = {{
            { "All",     OutlinerCategoryFilter::All         },
            { "Folders", OutlinerCategoryFilter::FoldersOnly },
            { "Type 1",  OutlinerCategoryFilter::FirstType   },
            { "Type 2",  OutlinerCategoryFilter::SecondType  },
            { "Type 3",  OutlinerCategoryFilter::ThirdType   },
            { "Type 4",  OutlinerCategoryFilter::FourthType  },
            { "Notes",   OutlinerCategoryFilter::WithNotes   },
        }};

        float ChipX = Bounds.X + 14.0f;
        float ChipY = ContentTopY + 2.0f;
        for (const auto& Chip : Chips)
        {
            const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Chip.Label) + 18.0f;
            if (ChipX + ChipW > Bounds.Right() - 14.0f && ChipX > Bounds.X + 15.0f)
            {
                ChipX = Bounds.X + 14.0f;
                ChipY += 28.0f;
            }
            const Rectangle2D ChipRect{ ChipX, ChipY, ChipW, 24.0f };
            const bool        Selected = (CategoryFilter == Chip.Filter);
            const bool        Hovered  = !OverlayOpenBefore && ChipRect.Contains(Cursor);

            Surface.FillRectangle(
                ChipRect,
                Selected ? ColorRgba{ 54, 54, 54, 255 } : (Hovered ? ColorRgba{ 36, 36, 36, 255 } : ColorRgba{ 28, 28, 28, 255 }),
                12.0f);
            Surface.StrokeRectangle(
                ChipRect,
                Selected ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba{ 52, 52, 52, 255 },
                12.0f,
                1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { ChipRect.X + 9.0f, ChipRect.Y + 6.0f },
                10.0f,
                Selected ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 165, 165, 165, 255 },
                Chip.Label);

            if (Hovered && Input.PrimaryPointer.Pressed)
            {
                CategoryFilter = Chip.Filter;
            }
            ChipX += ChipW + 5.0f;
        }
        ContentTopY = ChipY + 30.0f;
    }

    // 4. Tree rows region (.outliner-rows) above 45px footer
    const float       FooterHeight = 45.0f;
    const Rectangle2D TreeViewport{
        Bounds.X,
        ContentTopY,
        Bounds.Width,
        std::max(0.0f, Bounds.Bottom() - FooterHeight - ContentTopY)
    };

    std::vector<OutlinerVisibleRow> VisibleRows{};
    BuildVisibleRows(VisibleRows);

    constexpr float RowHeight     = 39.0f;
    constexpr float TopPadding    = 4.0f;
    constexpr float BottomPadding = 10.0f;
    const float     ContentHeight = TopPadding + static_cast<float>(VisibleRows.size()) * RowHeight + BottomPadding;
    const float     MaxScrollY    = std::max(0.0f, ContentHeight - TreeViewport.Height);

    if (!OverlayOpenBefore && TreeViewport.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 34.0f;
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PushClip(TreeViewport);

    if (VisibleRows.empty())
    {
        const char* EmptyHeadline = Entries.empty() ? "No entries yet" : "No matching entries";
        const char* EmptyHint     = Entries.empty() ? "Use + in the header to construct items or folders" : "Adjust search or filter criteria";
        const float HeadW         = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.0f, EmptyHeadline);
        const float HintW         = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, EmptyHint);
        const float CenterY       = TreeViewport.Center().Y;

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TreeViewport.Center().X - HeadW * 0.5f, CenterY - 14.0f },
            12.0f,
            ColorRgba{ 132, 132, 132, 255 },
            EmptyHeadline);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TreeViewport.Center().X - HintW * 0.5f, CenterY + 4.0f },
            10.0f,
            ColorRgba{ 88, 88, 88, 255 },
            EmptyHint);

        if (!OverlayOpenBefore && TreeViewport.Contains(Cursor) && Input.SecondaryPressed)
        {
            OpenDirectiveSheetForSlot(OutlinerSlotToken{}, Cursor);
        }
    }
    else
    {
        HoverDropTarget = {};
        bool AnyRowHit  = false;

        for (std::size_t RowIndex = 0u; RowIndex < VisibleRows.size(); ++RowIndex)
        {
            const OutlinerVisibleRow& VisRow = VisibleRows[RowIndex];
            OutlinerRecordEntry*      Entry  = AccessEntry(VisRow.Token);
            if (Entry == nullptr)
            {
                continue;
            }

            const float RowY = TreeViewport.Y + TopPadding + static_cast<float>(RowIndex) * RowHeight - ScrollOffsetY;
            if (RowY + RowHeight < TreeViewport.Y || RowY > TreeViewport.Bottom())
            {
                continue;
            }

            const Rectangle2D RowRect{ TreeViewport.X + 10.0f, RowY, std::max(40.0f, TreeViewport.Width - 20.0f), RowHeight };
            const Rectangle2D RowBox{ RowRect.X, RowRect.Y + 1.0f, RowRect.Width, RowRect.Height - 2.0f };
            const bool        Selected   = QuerySlotSelected(Entry->Token);
            const bool        RowHovered = !OverlayOpenBefore && TreeViewport.Contains(Cursor) && RowRect.Contains(Cursor);
            const float       RowAlpha   = VisRow.EffectiveVisible ? 1.0f : 0.42f;

            if (RowHovered)
            {
                AnyRowHit = true;
            }

            // Sharp unrounded (0.0f) rectangular entry box across all Outliner entries
            if (Selected)
            {
                Surface.FillRectangle(RowBox, WorkspaceSurfaceColors::RowSelected, 0.0f);
                Surface.StrokeRectangle(RowBox, WorkspaceSurfaceColors::RowSelectedBorder, 0.0f, 1.0f);
                Surface.FillRectangle({ RowBox.X, RowBox.Y, 3.0f, RowBox.Height }, WorkspaceSurfaceColors::SelectionAmber, 0.0f);
            }
            else if (RowHovered)
            {
                Surface.FillRectangle(RowBox, WorkspaceSurfaceColors::RowHover, 0.0f);
                Surface.StrokeRectangle(RowBox, ColorRgba{ 52, 52, 52, 255 }, 0.0f, 1.0f);
            }
            else
            {
                Surface.FillRectangle(RowBox, ColorRgba{ 23, 23, 23, 255 }, 0.0f);
                Surface.StrokeRectangle(RowBox, ColorRgba{ 39, 39, 39, 255 }, 0.0f, 1.0f);
            }

            const float IndentX = RowRect.X + 4.0f + static_cast<float>(VisRow.DepthLevel) * 13.0f;

            // Disclosure chevron
            const Rectangle2D DisclosureRect{ IndentX, RowRect.Y + 10.0f, 15.0f, 19.0f };
            bool              ConsumedDisclosureClick = false;
            if (VisRow.NestedBranchPresent)
            {
                VectorIconRepository::ConstructNavigation(
                    Surface,
                    Entry->Expanded ? NavigationIcon::ChevronDown : NavigationIcon::ChevronRight,
                    VectorIconPlacement{
                        DisclosureRect.X + 2.0f,
                        DisclosureRect.Y + 4.0f,
                        11.0f,
                        ColorRgba{ 130, 130, 130, 255 }.ScaledAlpha(RowAlpha),
                        1.9f });
                if (RowHovered && DisclosureRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
                {
                    Entry->Expanded         = !Entry->Expanded;
                    ConsumedDisclosureClick = true;
                }
            }

            // 24px Showcase folder or category icon
            const Point2D IconOrigin{ DisclosureRect.Right() + 4.0f, RowRect.Y + 7.5f };
            WorkspaceAppearance::ConstructSlotCategoryIcon(
                Surface,
                Entry->Category,
                IconOrigin,
                24.0f,
                Entry->FolderTint,
                RowAlpha);

            // Right-side visibility status indicator & eye toggle
            const Rectangle2D EyeHit{ RowRect.Right() - 24.0f, RowRect.Y + 9.5f, 18.0f, 20.0f };
            const Point2D     StatusCenter{ EyeHit.X - 11.0f, RowRect.Center().Y };
            WorkspaceAppearance::ConstructVisibilityIndicator(Surface, StatusCenter, VisRow.EffectiveVisible, 1.0f);

            bool ConsumedEyeClick = false;
            if (RowHovered || Selected || !Entry->VisibilityEnabled)
            {
                const bool EyeHovered = RowHovered && EyeHit.Contains(Cursor);
                VectorIconRepository::ConstructControl(
                    Surface,
                    Entry->VisibilityEnabled ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                    VectorIconPlacement{
                        EyeHit.X + 2.0f,
                        EyeHit.Y + 3.0f,
                        14.0f,
                        EyeHovered ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 124, 124, 124, 255 },
                        1.8f });
                if (EyeHovered && Input.PrimaryPointer.Pressed)
                {
                    Entry->VisibilityEnabled = !Entry->VisibilityEnabled;
                    ConsumedEyeClick         = true;
                }
            }

            // Two-line row identity (title + subtitle) + optional right-side "locked" tag
            float RightLimitX = StatusCenter.X - 14.0f;
            if (Entry->Locked)
            {
                const char* LockedTag = "locked";
                const float LockedW   = WorkspaceAppearance::MeasureWeightedTextWidth(
                    Surface,
                    FontWeightClass::Regular,
                    9.5f,
                    LockedTag);
                RightLimitX -= (LockedW + 8.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Regular,
                    { RightLimitX + 4.0f, RowRect.Center().Y - 6.0f },
                    9.5f,
                    ColorRgba{ 138, 138, 138, 255 }.ScaledAlpha(RowAlpha),
                    LockedTag);
            }

            const float IdentityX    = IconOrigin.X + 31.0f;
            const float IdentityMaxW = std::max(24.0f, RightLimitX - IdentityX);

            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { IdentityX, RowRect.Y + 7.5f },
                IdentityMaxW,
                12.0f,
                (Selected ? WorkspaceSurfaceColors::PrimaryInk : WorkspaceSurfaceColors::SecondaryInk).ScaledAlpha(RowAlpha),
                Entry->Title);

            char FolderSubBuffer[64]{};
            std::string_view SubText = Entry->Subtitle;
            if (Entry->Category == ConstructSlotCategory::Folder)
            {
                std::snprintf(
                    FolderSubBuffer,
                    sizeof(FolderSubBuffer),
                    "%u %s",
                    VisRow.NestedEntryCount,
                    (VisRow.NestedEntryCount == 1u) ? "entry" : "entries");
                SubText = FolderSubBuffer;
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { IdentityX, RowRect.Y + 23.0f },
                IdentityMaxW,
                8.5f,
                ColorRgba{ 116, 116, 116, 255 }.ScaledAlpha(RowAlpha),
                SubText);

            // Row selection, drag initiation & right-click
            if (RowHovered && !ConsumedDisclosureClick && !ConsumedEyeClick)
            {
                if (Input.PrimaryPointer.Pressed)
                {
                    PressedRowToken = Entry->Token;
                    PressOrigin     = Cursor;
                    SelectWithModifiers(Entry->Token, Input.ShiftHeld, Input.ControlHeld);
                }
                if (Input.SecondaryPressed)
                {
                    OpenDirectiveSheetForSlot(Entry->Token, Cursor);
                }
            }

            // Drag-and-drop hover zone evaluation
            if (DragActive && PressedRowToken.Valid() && PressedRowToken != Entry->Token && RowHovered)
            {
                const float LocalT = (Cursor.Y - RowRect.Y) / RowRect.Height;
                HoverDropTarget    = Entry->Token;
                if (LocalT < 0.28f)
                {
                    HoverDropPlacement = DropRelativePlacement::BeforeTarget;
                    Surface.StrokeLine(
                        { RowRect.X + 6.0f, RowRect.Y },
                        { RowRect.Right() - 6.0f, RowRect.Y },
                        WorkspaceSurfaceColors::SelectionAmber,
                        2.0f);
                    Surface.FillCircle({ RowRect.X + 6.0f, RowRect.Y }, 3.2f, WorkspaceSurfaceColors::SelectionAmber);
                }
                else if (LocalT > 0.72f)
                {
                    HoverDropPlacement = DropRelativePlacement::AfterTarget;
                    Surface.StrokeLine(
                        { RowRect.X + 6.0f, RowRect.Bottom() },
                        { RowRect.Right() - 6.0f, RowRect.Bottom() },
                        WorkspaceSurfaceColors::SelectionAmber,
                        2.0f);
                    Surface.FillCircle({ RowRect.X + 6.0f, RowRect.Bottom() }, 3.2f, WorkspaceSurfaceColors::SelectionAmber);
                }
                else
                {
                    HoverDropPlacement = DropRelativePlacement::InsideTarget;
                    Surface.StrokeRectangle(RowBox, WorkspaceSurfaceColors::SelectionAmber, 0.0f, 1.5f);
                }
            }
        }

        // Drag motion progression & drop completion
        if (PressedRowToken.Valid() && Input.PrimaryPointer.Down)
        {
            const float DeltaX = Cursor.X - PressOrigin.X;
            const float DeltaY = Cursor.Y - PressOrigin.Y;
            if ((DeltaX * DeltaX + DeltaY * DeltaY) > 25.0f)
            {
                DragActive = true;
            }
        }
        if (Input.PrimaryPointer.Released)
        {
            if (DragActive && PressedRowToken.Valid() && HoverDropTarget.Valid())
            {
                RepositionRelativeToTarget(PressedRowToken, HoverDropTarget, HoverDropPlacement);
            }
            DragActive      = false;
            PressedRowToken = {};
        }

        if (!OverlayOpenBefore && !AnyRowHit && TreeViewport.Contains(Cursor))
        {
            if (Input.PrimaryPointer.Pressed && !Input.ShiftHeld && !Input.ControlHeld)
            {
                ClearSelection();
            }
            if (Input.SecondaryPressed)
            {
                OpenDirectiveSheetForSlot(OutlinerSlotToken{}, Cursor);
            }
        }
    }

    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, TreeViewport, ContentHeight, ScrollOffsetY, Cursor);
    Surface.PopClip();

    // 5. workspace Outliner Footer (.outliner-footer: 45px height, #151515)
    const Rectangle2D FooterBounds{ Bounds.X, Bounds.Bottom() - FooterHeight, Bounds.Width, FooterHeight };
    Surface.FillRectangle(FooterBounds, WorkspaceSurfaceColors::FooterBackground);
    Surface.StrokeLine(
        { FooterBounds.X, FooterBounds.Y },
        { FooterBounds.Right(), FooterBounds.Y },
        WorkspaceSurfaceColors::HairlineLight,
        1.0f);

    struct FooterSlotView
    {
        const char*      Caption;
        std::string_view Reading;
        bool             HighlightGreen;
    };
    const std::array<FooterSlotView, 5> FooterSlots = {{
        { "REALTIME", FooterReadings.RealtimeText,  true  },
        { "QUALITY",  FooterReadings.QualityText,   false },
        { "SUN",      FooterReadings.PrimarySlot,   false },
        { "MOONS",    FooterReadings.SecondarySlot, false },
        { "CAM",      FooterReadings.CameraText,    false },
    }};

    float FooterX = FooterBounds.X + 15.0f;
    for (const auto& Slot : FooterSlots)
    {
        if (FooterX > FooterBounds.Right() - 28.0f)
        {
            break;
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { FooterX, FooterBounds.Y + 9.0f },
            7.5f,
            ColorRgba{ 88, 88, 88, 255 },
            Slot.Caption);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { FooterX, FooterBounds.Y + 21.0f },
            10.0f,
            Slot.HighlightGreen ? WorkspaceSurfaceColors::StatusGreen : ColorRgba{ 176, 176, 176, 255 },
            Slot.Reading);

        const float CapW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.5f, Slot.Caption);
        const float ValW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Slot.Reading);
        FooterX += std::max(CapW, ValW) + 14.0f;
    }

    Surface.PopClip();

    // 6. Reusable Studio Controls Dropdown Sheets (+ Construct, Outliner Settings, Right-Click Directives — zero shadow)
    if (ConstructDropdownOpen)
    {
        std::vector<DropdownDirectiveEntry> SheetRows{};
        SheetRows.reserve(ConstructTemplates.size() + 2u);

        DropdownDirectiveEntry HeaderRow{};
        HeaderRow.Label          = "CONSTRUCT";
        HeaderRow.SectionCaption = true;
        SheetRows.push_back(HeaderRow);

        for (const auto& Preset : ConstructTemplates)
        {
            DropdownDirectiveEntry Row{};
            Row.Label       = Preset.DropdownLabel.c_str();
            Row.LeadingTint = WorkspaceAppearance::QueryCategoryAccent(Preset.Category, WorkspaceSurfaceColors::ShowcaseSage);
            SheetRows.push_back(Row);
        }

        DropdownDirectiveEntry FolderRow{};
        FolderRow.Label        = "+ Add Folder";
        FolderRow.ShortcutHint = "Ctrl+G";
        FolderRow.LeadingTint  = WorkspaceSurfaceColors::ShowcaseSage;
        FolderRow.DividerAbove = true;
        SheetRows.push_back(FolderRow);

        const float       SheetW = 216.0f;
        const float       SheetX = std::clamp(ConstructDropdownAnchor.X - SheetW, Bounds.X + 8.0f, std::max(Bounds.X + 8.0f, Bounds.Right() - SheetW - 8.0f));
        const Rectangle2D Sheet  = DropdownSelector::QueryDirectiveSheetBounds({ SheetX, ConstructDropdownAnchor.Y }, SheetW, SheetRows);

        int32_t ChosenRow = -1;
        if (DropdownSelector::ConstructDirectiveSheet(ActiveOverlay, Sheet, SheetRows, Input.PrimaryPointer, ChosenRow).Clicked && ChosenRow >= 1)
        {
            const auto TemplateIdx = static_cast<std::size_t>(ChosenRow - 1);
            if (TemplateIdx < ConstructTemplates.size())
            {
                ConstructFromTemplate(static_cast<uint32_t>(TemplateIdx));
            }
            else
            {
                ConstructFolder("Folder");
            }
            ConstructDropdownOpen = false;
        }
        else if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !Sheet.Contains(Cursor) && !PlusButton.Contains(Cursor))
        {
            ConstructDropdownOpen = false;
        }
    }

    if (SettingsDropdownOpen)
    {
        constexpr std::array<DropdownDirectiveEntry, 7> SettingsRows = {{
            { "OUTLINER SETTINGS",   "",       { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, true  },
            { "Import...",           "Ctrl+I", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false },
            { "Export Selection...", "Ctrl+E", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false },
            { "Expand All",          "",       { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, true,  false },
            { "Collapse All",        "",       { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false },
            { "Show All Hidden",     "",       { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false },
            { "Clear Workspace",     "",       { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, true,  false, false },
        }};
        constexpr std::array<OutlinerSettingsDirective, 7> MappedDirectives = {{
            OutlinerSettingsDirective::None,
            OutlinerSettingsDirective::ImportEntries,
            OutlinerSettingsDirective::ExportSelection,
            OutlinerSettingsDirective::ExpandAll,
            OutlinerSettingsDirective::CollapseAll,
            OutlinerSettingsDirective::ShowAllHidden,
            OutlinerSettingsDirective::ClearWorkspace,
        }};

        const float       SheetW = 222.0f;
        const float       SheetX = std::clamp(SettingsDropdownAnchor.X - SheetW, Bounds.X + 8.0f, std::max(Bounds.X + 8.0f, Bounds.Right() - SheetW - 8.0f));
        const Rectangle2D Sheet  = DropdownSelector::QueryDirectiveSheetBounds({ SheetX, SettingsDropdownAnchor.Y }, SheetW, SettingsRows);

        int32_t ChosenRow = -1;
        if (DropdownSelector::ConstructDirectiveSheet(ActiveOverlay, Sheet, SettingsRows, Input.PrimaryPointer, ChosenRow).Clicked &&
            ChosenRow >= 1 && ChosenRow < static_cast<int32_t>(MappedDirectives.size()))
        {
            ExecuteSettingsDirective(MappedDirectives[static_cast<std::size_t>(ChosenRow)]);
            SettingsDropdownOpen = false;
        }
        else if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !Sheet.Contains(Cursor) && !SettingsButton.Contains(Cursor))
        {
            SettingsDropdownOpen = false;
        }
    }

    if (DirectiveSheetOpen)
    {
        OutlinerRecordEntry* TargetEntry   = AccessEntry(DirectiveTargetToken);
        const bool           TargetPresent = (TargetEntry != nullptr);
        const bool           Nested        = TargetPresent && TargetEntry->EnclosingToken.Valid();

        std::vector<DropdownDirectiveEntry> DirectiveRows{};
        if (TargetPresent)
        {
            DirectiveRows.push_back({ "Inspect", "Enter", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            DirectiveRows.push_back({ TargetEntry->VisibilityEnabled ? "Hide" : "Show", "H", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            DirectiveRows.push_back({ "Duplicate", "Ctrl+D", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            DirectiveRows.push_back({ "Group in Folder", "Ctrl+G", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            if (Nested)
            {
                DirectiveRows.push_back({ "Move to Root", "", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            }
            DirectiveRows.push_back({ "Delete", "Del", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, true, true, false });
        }
        else
        {
            DirectiveRows.push_back({ "Add Item 1", "", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
            DirectiveRows.push_back({ "+ Add Folder", "Ctrl+G", { 0u, 0u, 0u, 0u }, ControlIcon::Count, false, false, false, false });
        }

        const float       SheetW   = 222.0f;
        const Rectangle2D RawSheet = DropdownSelector::QueryDirectiveSheetBounds(DirectiveSheetAnchor, SheetW, DirectiveRows);
        const Rectangle2D Sheet{
            std::clamp(RawSheet.X, Bounds.X + 8.0f, std::max(Bounds.X + 8.0f, Bounds.Right() - RawSheet.Width - 8.0f)),
            std::clamp(RawSheet.Y, Bounds.Y + 8.0f, std::max(Bounds.Y + 8.0f, Bounds.Bottom() - RawSheet.Height - 8.0f)),
            RawSheet.Width,
            RawSheet.Height,
        };

        int32_t ChosenRow = -1;
        if (DropdownSelector::ConstructDirectiveSheet(ActiveOverlay, Sheet, DirectiveRows, Input.PrimaryPointer, ChosenRow).Clicked && ChosenRow >= 0)
        {
            if (TargetPresent)
            {
                if (ChosenRow == 0)
                {
                    SelectSingle(TargetEntry->Token);
                }
                else if (ChosenRow == 1)
                {
                    TargetEntry->VisibilityEnabled = !TargetEntry->VisibilityEnabled;
                }
                else if (ChosenRow == 2)
                {
                    DuplicateSlot(TargetEntry->Token);
                }
                else if (ChosenRow == 3)
                {
                    GroupSelectionIntoFolder("Folder");
                }
                else if (Nested && ChosenRow == 4)
                {
                    AttachToEnclosure(TargetEntry->Token, OutlinerSlotToken{});
                }
                else
                {
                    if (SelectedTokens.size() > 1u && QuerySlotSelected(TargetEntry->Token))
                    {
                        DeleteSelection();
                    }
                    else
                    {
                        DeleteSlot(TargetEntry->Token);
                    }
                }
            }
            else if (ChosenRow == 0)
            {
                ConstructFromTemplate(0u);
            }
            else if (ChosenRow == 1)
            {
                ConstructFolder("Folder");
            }
            DirectiveSheetOpen = false;
        }
        else if (OverlayOpenBefore && Input.PrimaryPointer.Pressed && !Sheet.Contains(Cursor))
        {
            DirectiveSheetOpen = false;
        }
    }
}

void OutlinerPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton("##FrontierOutlinerCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PixelSpace ForegroundSurface(ImGui::GetForegroundDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);

    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt, &ForegroundSurface);
}

} // namespace Frontier
