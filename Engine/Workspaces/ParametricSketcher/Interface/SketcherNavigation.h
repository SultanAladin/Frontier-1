/*====================================================================================================================================
                                                         SOLIDARCNAVIGATION.H
====================================================================================================================================*/
// 🧩 Plasticity viewport navigation on the shared Engine/Render/OrbitCamera: mouse gestures and numpad views

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"

#include <optional>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One frame of pointer input over the viewport, in viewport pixels (y down).
struct SketcherNavigationPointer
{
    float DeltaX               = 0.0f;                                                        // [px]  - Pointer motion this frame
    float DeltaY               = 0.0f;                                                        // [px]  - Pointer motion this frame
    float WheelSteps           = 0.0f;                                                        // [-]   - + = away from the user (closer)
    bool  MiddleDragCondition  = false;                                                       // [-]   - Middle button dragging
    bool  RightDragCondition   = false;                                                       // [-]   - Right button dragging past the click tolerance
    bool  CapturedCondition    = false;                                                       // [-]   - Viewport hovered or holding the gesture
    bool  HoveredCondition     = false;                                                       // [-]   - Pointer over the viewport
};

// 📝 Numpad state this frame (Blender / Plasticity viewport keymap: numpad camera navigation stays live outside numeric entry).
struct SketcherNavigationKeypad
{
    bool FrontPressed        = false;                                                         // [-] - Numpad 1 (Ctrl: Back)
    bool RightPressed        = false;                                                         // [-] - Numpad 3 (Ctrl: Left)
    bool TopPressed          = false;                                                         // [-] - Numpad 7 (Ctrl: Bottom)
    bool IsometricPressed    = false;                                                         // [-] - Numpad 0: Isometric 3D view
    bool OppositePressed     = false;                                                         // [-] - Numpad 9: Opposite / 180° flip view
    bool ProjectionPressed   = false;                                                         // [-] - Numpad 5: Perspective ⇄ Orthographic
    bool OrbitLeftPressed    = false;                                                         // [-] - Numpad 4: Orbit left 15° (Ctrl: Pan left)
    bool OrbitRightPressed   = false;                                                         // [-] - Numpad 6: Orbit right 15° (Ctrl: Pan right)
    bool OrbitUpPressed      = false;                                                         // [-] - Numpad 8: Orbit up 15° (Ctrl: Pan up)
    bool OrbitDownPressed    = false;                                                         // [-] - Numpad 2: Orbit down 15° (Ctrl: Pan down)
    bool ZoomInPressed       = false;                                                         // [-] - Numpad +: Dolly closer
    bool ZoomOutPressed      = false;                                                         // [-] - Numpad -: Dolly further
    bool ControlCondition    = false;                                                         // [-] - Ctrl held: opposite preset or pan step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Plasticity orbit-controls: middle drag orbits (azimuth about world z, elevation about camera right), right drag
// pans in the view plane, the wheel dollies. Returns true when the camera moved.
bool NavigateSketcherPointer(OrbitCamera& Camera, const OrbitCameraFrame& Frame, const SketcherNavigationPointer& Pointer);

// Numpad 5 toggles projection; 1 / 3 / 7 (Ctrl: opposite), 0 (Isometric), 9 (180° flip), 2 / 4 / 6 / 8 (15° orbit steps,
// Ctrl: pan), + / - (zoom). Returns a named preset when entered so the caller updates the drawing grid plane.
[[nodiscard]] std::optional<OrbitViewPreset> NavigateSketcherKeypad(OrbitCamera& Camera, const SketcherNavigationKeypad& Keypad,
                                                                    const OrbitCameraFrame* Frame = nullptr, bool* MovedOut = nullptr);

} // namespace Frontier::Workspaces::Sketcher
