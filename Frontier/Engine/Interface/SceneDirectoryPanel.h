/*==========================================================================================================================================
                                                        SCENE DIRECTORY PANEL
                                     ------------------------------------------------------------------
        Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.
==========================================================================================================================================*/

// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#pragma once

#include "Engine/Scene/RecordClassification.h"
#if defined(FRONTIER_EDITOR_PROFILE)
    #include "Engine/Scene/RecordEditorDescription.h"
#endif
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Frontier
{

class SceneDirectory;

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SceneDirectoryRow
{
    RecordToken          Token;                                            // [-] - Record identity
    std::string          Name;                                             // [text] - Display name (configured or default)
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             DepthTier      = 0;                               // [tiers] - Nesting depth; 0 is a root row
    uint32_t             ChildCount     = 0;                               // [records] - Direct children in the directory
    bool                 Visible        = true;                            // [-] - Directory eye toggle (dimmed when off)
    bool                 Collapsed      = false;                           // [-] - Panel-side: subtree folded away
    std::string          Group;                                            // [text] - HTML outliner group (editor profile; classification fallback otherwise)
    std::string          Colour;                                           // [text] - Row accent "#rrggbb" from the record's editor description
    std::string          StableId;                                         // [text] - Pinned HTML entity id from the record's editor description
};

struct SceneClassificationTab
{
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             Count          = 0;   // [records] - Live records in this classification
};

// 📝 One outliner section in the pinned HTML group order (EditorShell.js GroupOrder at 6afb6f0).
//    Groups carry the HTML spelling; classification tabs remain available separately below.
struct SceneDirectoryGroupSection
{
    std::string              Group;      // [text] - Render, Environment, Forcefield, Objects, ...
    uint32_t                 Count = 0;  // [records] - Members before the text filter
    std::vector<SceneDirectoryRow> Rows; // [-] - Visible members in registration order
};

// 📝 Enabled/disabled tiles above the HTML outliner tree: a record counts as enabled when its own
//    editor description reports an enabled setting, and as enabled by default when it has none.
struct SceneDirectoryEnabledCounts
{
    uint32_t Enabled  = 0;   // [records]
    uint32_t Disabled = 0;   // [records]
};

// 📝 The inspector the selected record built for itself (editor profile). Scalar and switch entries
//    point into the record's live settings through its description, so an edit edits the scene.
struct SceneInspectorModel
{
    bool        HasSelection = false;
    bool        HasEnabled   = false;   // [-] - The record exposes an enabled setting
    bool        Enabled      = true;    // [-] - Current enabled setting
    std::string Group;                  // [text] - Crumb group
    std::string Category;               // [text] - Eyebrow type
    std::string Name;                   // [text] - Object header
    std::string Description;            // [text] - Header description

#if defined(FRONTIER_EDITOR_PROFILE)
    std::vector<RecordEditorScalarField> ScalarFields;   // [-] - Live-bound sliders, in display order
    std::vector<RecordEditorSwitch>      Switches;       // [-] - Live-bound switches, in display order
#endif
};

// What the inspector shows for the current selection ("empty inspector" = HasSelection false).
struct SceneInspectorSummary
{
    bool                 HasSelection   = false;
    RecordToken          Token;
    std::string          Name;                                                // [text]
    RecordClassification Classification = RecordClassification::Unclassified;
    std::string          ParentName;                                          // [text] - Empty for a root record
    uint32_t             ChildCount     = 0;                                  // [records]
    uint32_t             DepthTier      = 0;                                  // [tiers]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE DIRECTORY PANEL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same split as the Log Stream: this portable view-model owns projection, filtering and selection; the native
//    drawing (dock panel + inspector) joins it at the SDK phase against the upstream kit. Synchronize() snapshots
//    the directory into a flattened tree: roots in registration order, children directly beneath their parent in
//    registration order. In Shipping nothing ever registers, so every projection stays empty by construction.
class SceneDirectoryPanel
{
  public:
    void Synchronize(const SceneDirectory& Directory);

    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryRows() const { return Rows; }       // Flattened tree, unfiltered
    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryVisible() const { return Visible; } // After the classification filter
    [[nodiscard]] const std::vector<SceneClassificationTab>& QueryTabs() const { return Tabs; }       // Every classification, with live counts
    [[nodiscard]] uint32_t                                   QueryTotal() const { return Total; }     // [records]
    [[nodiscard]] const SceneInspectorSummary&               QueryInspector() const { return Inspector; }
    [[nodiscard]] const std::vector<SceneDirectoryGroupSection>& QueryGroups() const { return Groups; }
    [[nodiscard]] const SceneInspectorModel&                 QueryInspectorModel() const { return InspectorModel; }
    [[nodiscard]] SceneDirectoryEnabledCounts                QueryEnabledCounts() const { return EnabledCounts; }

    void ConfigureClassificationFilter(std::optional<RecordClassification> Filter);   // nullopt = All
    [[nodiscard]] std::optional<RecordClassification> QueryClassificationFilter() const { return ClassificationFilter; }

    void ConfigureTextFilter(std::string Text);   // Case-insensitive substring over row names; empty clears.
    [[nodiscard]] const std::string& QueryTextFilter() const { return TextFilter; }

    void ConfigureCollapsed(RecordToken Token, bool Collapsed);
    [[nodiscard]] bool QueryCollapsed(RecordToken Token) const;

    // Spawn flow: the presentation's add menu records a request; the host services it (records are engine
    // objects — the panel never invents them) and the new record appears by construction.
    void RequestSpawn(RecordClassification Classification);
    [[nodiscard]] std::optional<RecordClassification> TakeSpawnRequest();

    // Native drawing (interface-SDK builds; an empty stub without the SDK, so portable builds keep linking).
    // The host calls Synchronize() each frame first, then services TakeSpawnRequest().
    void ConstructInterface();

    void Select(RecordToken Token);   // Unknown/stale tokens clear the selection on the next Synchronize
    void ClearSelection();
    [[nodiscard]] RecordToken QuerySelected() const { return Selected; }

  private:
    void RebuildVisible();

    std::vector<SceneDirectoryRow>      Rows;
    std::vector<SceneDirectoryGroupSection> Groups;
    SceneDirectoryEnabledCounts         EnabledCounts;
    SceneInspectorModel                 InspectorModel;
    std::vector<SceneDirectoryRow>      Visible;
    std::vector<SceneClassificationTab> Tabs;
    uint32_t                            Total = 0;   // [records]
    std::optional<RecordClassification> ClassificationFilter;
    std::string                         TextFilter;            // [text] - Lower-cased on configure
    std::unordered_set<uint64_t>        CollapsedTokens;       // [-] - Panel-side fold state
    std::optional<RecordClassification> PendingSpawn;
    RecordToken                         Selected;
    SceneInspectorSummary               Inspector;
};

} // namespace Frontier
