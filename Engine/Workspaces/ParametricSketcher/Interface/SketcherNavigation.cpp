/*====================================================================================================================================
                                                        SOLIDARCNAVIGATION.CPP
====================================================================================================================================*/
// 🧩 Plasticity viewport navigation on the shared Engine/Render/OrbitCamera (see header)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherNavigation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool NavigateSketcherPointer(OrbitCamera& Camera, const OrbitCameraFrame& Frame, const SketcherNavigationPointer& Pointer)
{
    bool MovedCondition = false;
    if (Pointer.CapturedCondition)
    {
        if (Pointer.MiddleDragCondition)
        {
            RevolveOrbitCamera(Camera, Pointer.DeltaX, Pointer.DeltaY);
            MovedCondition = true;
        }
        else if (Pointer.RightDragCondition)
        {
            PanOrbitCamera(Camera, Frame, Pointer.DeltaX, Pointer.DeltaY);
            MovedCondition = true;
        }
    }
    if (Pointer.HoveredCondition && Pointer.WheelSteps != 0.0f)
    {
        DollyOrbitCamera(Camera, Pointer.WheelSteps);
        MovedCondition = true;
    }
    return MovedCondition;
}

std::optional<OrbitViewPreset> NavigateSketcherKeypad(OrbitCamera& Camera, const SketcherNavigationKeypad& Keypad,
                                                      const OrbitCameraFrame* Frame, bool* MovedOut)
{
    constexpr double HalfPi    = 0.5 * std::numbers::pi;
    constexpr double OrbitStep = std::numbers::pi / 12.0;                                    // [rad] - 15° Blender numpad orbit step
    constexpr double PanPixels = 48.0;                                                       // [px]  - Ctrl + Numpad 2/4/6/8 pan step
    constexpr double SnapTol   = 1.0e-2;
    bool Moved = false;

    if (Keypad.ProjectionPressed)
    {
        Camera.OrthographicEnabled = !Camera.OrthographicEnabled;
        Moved = true;
    }
    if (Keypad.ZoomInPressed)
    {
        DollyOrbitCamera(Camera, 1.5);
        Moved = true;
    }
    if (Keypad.ZoomOutPressed)
    {
        DollyOrbitCamera(Camera, -1.5);
        Moved = true;
    }
    if (Keypad.OrbitLeftPressed || Keypad.OrbitRightPressed || Keypad.OrbitUpPressed || Keypad.OrbitDownPressed)
    {
        const double Horiz = (Keypad.OrbitRightPressed ? 1.0 : 0.0) - (Keypad.OrbitLeftPressed ? 1.0 : 0.0);
        const double Vert  = (Keypad.OrbitUpPressed ? 1.0 : 0.0) - (Keypad.OrbitDownPressed ? 1.0 : 0.0);
        if (Keypad.ControlCondition)
        {
            const OrbitCameraFrame Fallback = Frame ? *Frame : FrameOrbitCamera(Camera, 800.0, 600.0);
            PanOrbitCamera(Camera, Fallback, -Horiz * PanPixels, Vert * PanPixels);
        }
        else
        {
            Camera.Azimuth   = std::remainder(Camera.Azimuth + Horiz * OrbitStep, 2.0 * std::numbers::pi);
            Camera.Elevation = std::clamp(Camera.Elevation + Vert * OrbitStep, -HalfPi, HalfPi);
        }
        Moved = true;
    }
    if (MovedOut) *MovedOut = Moved;

    if (Keypad.FrontPressed) return Keypad.ControlCondition ? OrbitViewPreset::Back : OrbitViewPreset::Front;
    if (Keypad.RightPressed) return Keypad.ControlCondition ? OrbitViewPreset::Left : OrbitViewPreset::Right;
    if (Keypad.TopPressed) return Keypad.ControlCondition ? OrbitViewPreset::Bottom : OrbitViewPreset::Top;
    if (Keypad.IsometricPressed) return OrbitViewPreset::Isometric;
    if (Keypad.OppositePressed)
    {
        if (Camera.Elevation >= HalfPi - SnapTol) return OrbitViewPreset::Bottom;
        if (Camera.Elevation <= -HalfPi + SnapTol) return OrbitViewPreset::Top;
        if (std::abs(Camera.Elevation) <= SnapTol)
        {
            if (std::abs(std::remainder(Camera.Azimuth + HalfPi, 2.0 * std::numbers::pi)) <= SnapTol) return OrbitViewPreset::Back;
            if (std::abs(std::remainder(Camera.Azimuth - HalfPi, 2.0 * std::numbers::pi)) <= SnapTol) return OrbitViewPreset::Front;
            if (std::abs(std::remainder(Camera.Azimuth, 2.0 * std::numbers::pi)) <= SnapTol) return OrbitViewPreset::Left;
            if (std::abs(std::remainder(Camera.Azimuth - std::numbers::pi, 2.0 * std::numbers::pi)) <= SnapTol) return OrbitViewPreset::Right;
        }
        Camera.Azimuth   = std::remainder(Camera.Azimuth + std::numbers::pi, 2.0 * std::numbers::pi);
        Camera.Elevation = -Camera.Elevation;
        if (MovedOut) *MovedOut = true;
    }
    return std::nullopt;
}

} // namespace Frontier::Workspaces::Sketcher
