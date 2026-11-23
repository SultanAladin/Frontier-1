/*============================================================================================================================================
                                                        PROJECTINSPECTORFOUNDATION.H
============================================================================================================================================*/
// 🧩 Foundation for Project-Defined User UI Inspectors (<ProjectRoot>/Inspectors/*.Inspector.toml & C++ blueprints)
// Separates shipped Native Editor UI Inspectors (Engine/Editor/Inspectors/) from user-authored Project UI Inspectors.

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

enum class ProjectFieldControl : uint8_t
{
    ScalarSlider = 0u,
    ToggleSwitch = 1u,
    ChoiceGrid   = 2u,
    ColourSwatch = 3u,
    MetadataLine = 4u,
    ActionButton = 5u,
};

struct ProjectFieldDescriptor
{
    std::string              Key             {}; // Unique field identifier within the blueprint
    std::string              Label           {}; // Human-readable control label
    ProjectFieldControl      Control         = ProjectFieldControl::ScalarSlider;
    std::string              UnitText        {}; // Optional unit suffix (e.g. "m", "N", "%")
    std::string              FormatSpec      { "%.2f" };
    std::string              TooltipText     {}; // Optional hover tooltip shown via '?' trigger
    float                    Minimum         = 0.0f;
    float                    Maximum         = 1.0f;
    float                    DefaultScalar   = 0.0f;
    bool                     DefaultToggle   = false;
    uint32_t                 DefaultChoice   = 0u;
    Mathematics::LinearRGB   DefaultColour   { 0.78f, 0.84f, 0.94f };
    std::string              DefaultText     {};
    std::vector<std::string> ChoiceOptions   {};
    uint32_t                 ChoiceColumns   = 2u;
};

struct ProjectSectionDescriptor
{
    std::string                         Heading      {};
    std::string                         RightCaption {};
    std::string                         TooltipText  {};
    std::vector<ProjectFieldDescriptor> Fields       {};
};

struct ProjectPageDescriptor
{
    std::string                           Title    {};
    std::vector<ProjectSectionDescriptor> Sections {};
};

struct ProjectInspectorBlueprint
{
    std::string                        BlueprintId    {}; // Unique blueprint identifier (e.g. "project.vehicle-aero")
    std::string                        TargetDomainTag{}; // Exact match or prefix with trailing '*' (e.g. "project:vehicle-aero")
    std::string                        Subtitle       { "Project custom inspector" };
    bool                               OverrideNative = false; // If true, takes precedence over a shipped native inspector
    std::string                        SourceFilePath {};      // <ProjectRoot>/Inspectors/<Name>.Inspector.toml
    std::vector<std::string>           TagLabels      {};
    std::vector<ProjectPageDescriptor> Pages          {};
};

struct ProjectFieldReading
{
    float                  ScalarReading = 0.0f;
    bool                   ToggleReading = false;
    uint32_t               ChoiceReading = 0u;
    Mathematics::LinearRGB ColourReading { 1.0f, 1.0f, 1.0f };
    std::string            TextReading   {};
    uint32_t               TriggerCount  = 0u;
};

struct ProjectInstanceReadings
{
    std::unordered_map<std::string, ProjectFieldReading> Fields {};
};

// Host managing user-authored Project UI Inspectors loaded from <ProjectRoot>/Inspectors/*.Inspector.toml
class ProjectInspectorHost
{
public:
    ProjectInspectorHost() noexcept = default;

    // Register a blueprint directly from C++ (project module / tests)
    void RegisterBlueprint(ProjectInspectorBlueprint Blueprint);

    // Parse and register a single .Inspector.toml descriptor from text (returns false with OutDiagnostic on parse error)
    [[nodiscard]] bool LoadBlueprintFromTomlText(
        std::string_view TomlText,
        std::string_view VirtualSourcePath,
        std::string*     OutDiagnostic = nullptr);

    // Scan <ProjectDirectory>/Inspectors/*.Inspector.toml (or direct directory) and load all valid user inspectors
    [[nodiscard]] uint32_t ScanProjectDirectory(
        const std::filesystem::path& ProjectDirectory,
        std::vector<std::string>*    OutDiagnostics = nullptr);

    // Remove all loaded project blueprints & per-instance readings
    void ClearAll() noexcept;
    void EraseEntryReadings(OutlinerSlotToken Token) noexcept;

    // Resolve matching user blueprint for an Outliner entry (nullptr if none registered)
    [[nodiscard]] const ProjectInspectorBlueprint* ResolveBlueprint(const OutlinerRecordEntry& Entry) const noexcept;
    [[nodiscard]] std::size_t QueryBlueprintCount() const noexcept { return Blueprints.size(); }

    // Access persisted per-entry multi-instance field readings (auto-seeded from blueprint defaults)
    ProjectInstanceReadings& AccessReadings(OutlinerSlotToken Token, const ProjectInspectorBlueprint& Blueprint);

    // Render any ProjectInspectorBlueprint natively using Layer A InspectorSectionPrimitives (1:1 visual parity + tooltips)
    [[nodiscard]] float Construct(
        PixelSpace&                      Surface,
        float                            LeftX,
        float                            TopY,
        float                            InnerWidth,
        OutlinerRecordEntry&             Entry,
        const ProjectInspectorBlueprint& Blueprint,
        const PaneInputReading&          Input,
        bool&                            OutMutated,
        bool&                            OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    std::vector<ProjectInspectorBlueprint>                    Blueprints   {};
    std::unordered_map<uint64_t, ProjectInstanceReadings>     Readings     {};
    uint32_t                                                  ActivePage   = 0u;
    uint32_t                                                  ActiveDragId = 0u;
};

} // namespace Frontier
