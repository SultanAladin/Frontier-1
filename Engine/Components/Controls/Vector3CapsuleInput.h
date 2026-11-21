/*============================================================================================================================================
                                                          VECTOR3CAPSULEINPUT.H
============================================================================================================================================*/
// 🧩 RGB X/Y/Z axis-tinted vector capsule input row with scrub-drag, click-type edit, uniform link lock, and reset trigger

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct Vector3CapsuleSession
{
    int            ActiveAxis      = -1; // [-1|0|1|2] - Axis currently scrubbed or edited
    bool           Dragging        = false;
    bool           EditingText     = false;
    bool           UniformLocked   = false;
    float          DragAnchorX     = 0.0f;
    float          DragStartScalar = 0.0f;
    TextEditBuffer EditBuffer      {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class Vector3CapsuleInput
{
public:
    static constexpr float RecommendedHeightPx = 46.0f; // [px] - 14px caption + 28px RGB capsules

    [[nodiscard]] static bool Construct(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const char*             Caption,
        const char*             UnitSuffix,
        float&                  InOutX,
        float&                  InOutY,
        float&                  InOutZ,
        Vector3CapsuleSession&  Session,
        const PaneInputReading& Input,
        bool                    ShowUniformLock = false,
        float                   ScrubSensitivity = 0.01f,
        double                  DeltaSeconds     = 1.0 / 60.0) noexcept;
};

} // namespace Frontier
