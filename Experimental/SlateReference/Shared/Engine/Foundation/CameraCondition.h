//============================================================================================================================================
//                                                          CAMERAGUARANTEE.H
//============================================================================================================================================
// 🧩 Host-translated camera input shared by editor, player and spectator camera components.

#pragma once

namespace Slate
{

/// 🧩 One tick of camera intent. Hosts translate their window, controller, or network input into this neutral guarantee.
/// tag   guarantee, nonallocating, nonthrowing
struct CameraCondition
{
    bool  ForwardHeld  = false;
    bool  BackwardHeld = false;
    bool  LeftHeld     = false;
    bool  RightHeld    = false;
    bool  UpHeld       = false;
    bool  DownHeld     = false;
    bool  LookHeld     = false;
    float LookDeltaX   = 0.0f;
    float LookDeltaY   = 0.0f;
    float SpeedSteps   = 0.0f;   // [notches] - editor fly-speed adjustment while looking
    bool  ShiftHeld    = false;
};

} // namespace Slate
