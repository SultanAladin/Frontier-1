// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#include "Engine/Interface/SceneDirectoryPanel.h"

#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{

namespace
{

// Directory parenting is cycle-free by construction (ConfigureParent refuses cycles); the cap only guards
// against corrupted state turning a projection into unbounded recursion.
constexpr uint32_t MaximumDepthTier = 128;   // [tiers]

void AppendSubtree(const SceneDirectory& Directory, RecordToken Token, uint32_t DepthTier,
                   const std::unordered_map<uint64_t, std::vector<RecordToken>>& Children,
                   const std::unordered_set<uint64_t>& CollapsedTokens,
                   std::vector<SceneDirectoryRow>& Out)
{
    SceneDirectoryRow Row;
    Row.Token          = Token;
    Row.Name           = Directory.QueryName(Token);
    Row.Classification = Directory.QueryClassification(Token);
#if defined(FRONTIER_EDITOR_PROFILE)
    if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Token))
    {
        Row.Group    = Description->Group;
        Row.Colour   = Description->Colour;
        Row.StableId = Description->StableId;
        if (Description->DisplayName[0] != '\0' && Directory.QueryName(Token).find(" (spawned)") == std::string::npos)
        {
            // Directory names win once configured; the description name is the construction default.
        }
    }
#endif
    Row.DepthTier      = DepthTier;
    Row.ChildCount     = Directory.QueryChildCount(Token);
    Row.Visible        = Directory.QueryVisible(Token);
    Row.Collapsed      = CollapsedTokens.find(Token.Value) != CollapsedTokens.end();
    Out.push_back(Row);

    if (DepthTier >= MaximumDepthTier)
    {
        return;
    }
    const auto It = Children.find(Token.Value);
    if (It != Children.end())
    {
        for (const RecordToken Child : It->second)
        {
            AppendSubtree(Directory, Child, DepthTier + 1, Children, CollapsedTokens, Out);
        }
    }
}

} // namespace

void SceneDirectoryPanel::Synchronize(const SceneDirectory& Directory)
{
    std::vector<RecordToken> Order;
    Directory.CopyRegistrationOrder(Order);
    Total = static_cast<uint32_t>(Order.size());

    std::unordered_map<uint64_t, std::vector<RecordToken>> Children;
    std::vector<RecordToken>                               Roots;
    for (const RecordToken Token : Order)
    {
        const RecordToken Parent = Directory.QueryParent(Token);
        if (Parent.IsValid() && Directory.QueryRegistered(Parent))
        {
            Children[Parent.Value].push_back(Token);
        }
        else
        {
            Roots.push_back(Token);
        }
    }

    for (auto It = CollapsedTokens.begin(); It != CollapsedTokens.end();)
    {
        It = Directory.QueryRegistered(RecordToken{*It}) ? std::next(It) : CollapsedTokens.erase(It);
    }

    Rows.clear();
    for (const RecordToken Root : Roots)
    {
        AppendSubtree(Directory, Root, 0, Children, CollapsedTokens, Rows);
    }

    Tabs.clear();
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
    {
        Tabs.push_back(SceneClassificationTab{static_cast<RecordClassification>(Index), 0});
    }
    for (const SceneDirectoryRow& Row : Rows)
    {
        Tabs[static_cast<size_t>(Row.Classification)].Count += 1;
    }

    Inspector = SceneInspectorSummary{};
    if (Selected.IsValid() && Directory.QueryRegistered(Selected))
    {
        Inspector.HasSelection   = true;
        Inspector.Token          = Selected;
        Inspector.Name           = Directory.QueryName(Selected);
        Inspector.Classification = Directory.QueryClassification(Selected);
        Inspector.ChildCount     = Directory.QueryChildCount(Selected);
        const RecordToken Parent = Directory.QueryParent(Selected);
        Inspector.ParentName     = Parent.IsValid() ? Directory.QueryName(Parent) : std::string{};
        for (const SceneDirectoryRow& Row : Rows)
        {
            if (Row.Token == Selected)
            {
                Inspector.DepthTier = Row.DepthTier;
                break;
            }
        }
    }
    else
    {
        Selected = RecordToken{};
    }

    // 📝 HTML group sections (EditorShell.js GroupOrder at 6afb6f0). Rows without an editor
    //    description fall back to their classification's group so development builds still group.
    static const char* const GroupOrder[] = {"Render", "Environment", "Forcefield", "Objects",
                                             "Vehicle", "Effects", "Destruction", "Level"};
    const auto FallbackGroup = [](RecordClassification Classification) {
        switch (Classification)
        {
            case RecordClassification::Illumination:
            case RecordClassification::Camera:
            case RecordClassification::Interface:    return "Render";
            case RecordClassification::Environment:  return "Environment";
            case RecordClassification::Physics:      return "Forcefield";
            case RecordClassification::Vehicle:
            case RecordClassification::Tyre:         return "Vehicle";
            case RecordClassification::Content:      return "Level";
            case RecordClassification::Geometry:
            case RecordClassification::Unclassified:
            case RecordClassification::Count:        break;
        }
        return "Objects";
    };
    Groups.clear();
    EnabledCounts = SceneDirectoryEnabledCounts{};
    for (SceneDirectoryRow& Row : Rows)
    {
        if (Row.Group.empty())
        {
            Row.Group = FallbackGroup(Row.Classification);
        }
        bool Enabled = Row.Visible;
#if defined(FRONTIER_EDITOR_PROFILE)
        if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Row.Token))
        {
            Enabled = Description->Enabled == nullptr || *Description->Enabled;
        }
#endif
        if (Enabled) { EnabledCounts.Enabled += 1; } else { EnabledCounts.Disabled += 1; }
        SceneDirectoryGroupSection* Section = nullptr;
        for (SceneDirectoryGroupSection& Candidate : Groups)
        {
            if (Candidate.Group == Row.Group) { Section = &Candidate; break; }
        }
        if (Section == nullptr)
        {
            Groups.push_back(SceneDirectoryGroupSection{Row.Group, 0, {}});
            Section = &Groups.back();
        }
        Section->Count += 1;
        Section->Rows.push_back(Row);
    }
    std::stable_sort(Groups.begin(), Groups.end(), [&](const SceneDirectoryGroupSection& A,
                                                       const SceneDirectoryGroupSection& B) {
        const auto Rank = [&](const std::string& Group) {
            for (size_t Index = 0; Index < std::size(GroupOrder); ++Index)
                if (Group == GroupOrder[Index]) return Index;
            return std::size(GroupOrder);
        };
        return Rank(A.Group) < Rank(B.Group);
    });

    InspectorModel = SceneInspectorModel{};
#if defined(FRONTIER_EDITOR_PROFILE)
    if (Selected.IsValid())
    {
        if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Selected))
        {
            InspectorModel.HasSelection = true;
            InspectorModel.Group        = Description->Group;
            InspectorModel.Category     = Description->Category;
            InspectorModel.Name         = Directory.QueryName(Selected);
            const std::string DefaultName = std::string(RecordClassificationName(Inspector.Classification)) +
                                            " " + std::to_string(Selected.Value);
            if (InspectorModel.Name == DefaultName)
            {
                InspectorModel.Name = Description->DisplayName;   // Unconfigured name: use the record's own
            }
            InspectorModel.Description  = Description->Description;
            InspectorModel.HasEnabled   = Description->Enabled != nullptr;
            InspectorModel.Enabled      = Description->Enabled == nullptr || *Description->Enabled;
            InspectorModel.ScalarFields = Description->ScalarFields;
            InspectorModel.Switches     = Description->Switches;
        }
    }
#endif

    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureClassificationFilter(std::optional<RecordClassification> Filter)
{
    ClassificationFilter = Filter;
    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureTextFilter(std::string Text)
{
    TextFilter = std::move(Text);
    std::transform(TextFilter.begin(), TextFilter.end(), TextFilter.begin(),
                   [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureCollapsed(RecordToken Token, bool Collapsed)
{
    if (Collapsed)
    {
        CollapsedTokens.insert(Token.Value);
    }
    else
    {
        CollapsedTokens.erase(Token.Value);
    }
    for (SceneDirectoryRow& Row : Rows)
    {
        if (Row.Token == Token)
        {
            Row.Collapsed = Collapsed;
        }
    }
    RebuildVisible();
}

bool SceneDirectoryPanel::QueryCollapsed(RecordToken Token) const
{
    return CollapsedTokens.find(Token.Value) != CollapsedTokens.end();
}

void SceneDirectoryPanel::RequestSpawn(RecordClassification Classification)
{
    PendingSpawn = Classification;
}

std::optional<RecordClassification> SceneDirectoryPanel::TakeSpawnRequest()
{
    std::optional<RecordClassification> Request = PendingSpawn;
    PendingSpawn.reset();
    return Request;
}

void SceneDirectoryPanel::Select(RecordToken Token)
{
    Selected = Token;
}

void SceneDirectoryPanel::ClearSelection()
{
    Selected  = RecordToken{};
    Inspector = SceneInspectorSummary{};
}

void SceneDirectoryPanel::RebuildVisible()
{
    Visible.clear();
    const bool Searching = !TextFilter.empty();
    bool       Folding   = false;   // Inside a collapsed subtree (suppressed while searching)
    uint32_t   FoldDepth = 0;       // [tiers]
    for (const SceneDirectoryRow& Row : Rows)
    {
        const bool ClassOk = !ClassificationFilter.has_value() || Row.Classification == *ClassificationFilter;
        if (Searching)
        {
            std::string Lower = Row.Name;
            std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                           [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
            if (ClassOk && Lower.find(TextFilter) != std::string::npos)
            {
                Visible.push_back(Row);   // Search reveals through folds
            }
            continue;
        }
        if (Folding && Row.DepthTier > FoldDepth)
        {
            continue;
        }
        Folding = false;
        if (Row.Collapsed)
        {
            Folding   = true;
            FoldDepth = Row.DepthTier;
        }
        if (ClassOk)
        {
            Visible.push_back(Row);
        }
    }
}

} // namespace Frontier
