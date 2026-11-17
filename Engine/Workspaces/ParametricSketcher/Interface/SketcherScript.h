/*====================================================================================================================================
                                                           SOLIDARCSCRIPT.H
====================================================================================================================================*/
// 🧩 Scripted pointer / keyboard input for headless workspace captures, with expectations on the resulting state

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketcherStepKind : uint8_t
{
    Idle    = 0,                                                                             // One frame without input
    Move    = 1,                                                                             // Pointer to Target
    Button  = 2,                                                                             // Mouse button down / up
    Key     = 3,                                                                             // Key down / up (with modifiers)
    Text    = 4,                                                                             // Characters typed
    Wheel   = 5,                                                                             // Vertical wheel steps
    Capture = 6,                                                                             // Write PNG + JSON after this frame
    Expect  = 7,                                                                             // Compare a state field
    Setting = 8,                                                                             // Workspace setting (snap grid on / off)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Pointer target, resolved when the step runs: pixels, @Widget (last frame's rectangle centre), world X Y Z
//    (projected through the current camera into the viewport) or viewport U V (fractions of the viewport).
struct SketcherScriptTarget
{
    std::string Widget   = {};                                                               // [-]   - Registered widget key
    bool        World    = false;                                                            // [-]   - Coordinates are a world point
    bool        Fraction = false;                                                            // [-]   - Coordinates are viewport fractions
    double      Values[3] = {};                                                              // [px | len | -] - By form
    float       OffsetX  = 0.0f;                                                             // [px]  - Added after resolution
    float       OffsetY  = 0.0f;                                                             // [px]  - Added after resolution
};

struct SketcherScriptStep
{
    SketcherStepKind     Kind      = SketcherStepKind::Idle;                                 // [-] - What happens this frame
    SketcherScriptTarget Target    = {};                                                     // [-] - Move
    int                  Button    = 0;                                                      // [-] - ImGuiMouseButton
    bool                 Down      = false;                                                  // [-] - Button / key state
    ImGuiKey             Key       = ImGuiKey_None;                                          // [-] - Key
    bool                 Control   = false;                                                  // [-] - Modifier held with Key / Button
    bool                 Shift     = false;                                                  // [-] - Modifier held with Key / Button
    bool                 Alternate = false;                                                  // [-] - Alt held with Key / Button
    std::string          Text      = {};                                                     // [-] - Characters, capture name, expectation
    float                Wheel     = 0.0f;                                                   // [-] - Wheel steps
    bool                 Anchoring = false;                                                  // [-] - Move: remember the resolved point (drag start)
    bool                 Anchored  = false;                                                  // [-] - Move: offsets apply to the remembered point
    int                  Line      = 0;                                                      // [-] - Script line
};

struct SketcherExpectation
{
    int         Line     = 0;                                                                // [-] - Script line
    std::string Text     = {};                                                               // [-] - "selected-edges == 1"
    std::string Observed = {};                                                               // [-] - Actual value
    bool        Passed   = false;                                                            // [-] - Outcome
};

struct SketcherScript
{
    std::vector<SketcherScriptStep>  Steps        = {};                                      // [-] - One per frame
    size_t                           Cursor       = 0;                                       // [-] - Next step
    ImVec2                           Anchor       = {};                                      // [px] - Drag start (resolved once)
    std::vector<SketcherExpectation> Expectations = {};                                      // [-] - Results so far
    std::string                      Refusal      = {};                                      // [-] - Parse failure
    int                              RevealFrames = 0;                                       // [-] - Frames spent scrolling the next target into view
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SketcherScript ParseSketcherScript(const std::string& Text);

// 💡 Runs the next step: queues its ImGui input events before ImGui::NewFrame. Returns the capture name when this
//    frame must be written (empty otherwise). Expectations are evaluated against the state as it stands.
std::string AdvanceSketcherScript(SketcherScript& Script, SketcherWorkspaceState& State);

[[nodiscard]] bool FinishedSketcherScript(const SketcherScript& Script);

} // namespace Frontier::Workspaces::Sketcher
