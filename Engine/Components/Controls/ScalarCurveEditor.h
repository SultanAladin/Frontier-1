/*============================================================================================================================================
                                                             SCALARCURVEEDITOR.H
============================================================================================================================================*/
// 🧩 Reusable 0..1 piecewise-smooth scalar spline editor for size-over-life, opacity-over-life, wind falloff & force attenuation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ScalarCurveKnot
{
    float PositionT = 0.0f; // [0-1] - Normalized horizontal domain coordinate
    float Amplitude = 1.0f; // [0-1] - Normalized vertical curve output
};

struct ScalarCurveProfile
{
    std::vector<ScalarCurveKnot> Knots {
        { 0.00f, 0.25f },
        { 0.22f, 1.00f },
        { 0.72f, 0.78f },
        { 1.00f, 0.08f },
    };

    [[nodiscard]] float EvaluateAt(float NormalizedT) const noexcept;
    void                NormalizeKnots() noexcept;
};

struct ScalarCurveEditorSession
{
    std::size_t SelectedKnotIndex = 0u;    // [idx] - Currently highlighted knot
    bool        DraggingKnot      = false; // [-]   - True while dragging selected knot
    Rectangle2D LastPlotBounds    {};      // [px]  - Cached curve canvas bounds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ScalarCurveEditor
{
public:
    static constexpr float RecommendedHeightPx = 136.0f; // [px] - Compact inspector spline height

    [[nodiscard]] static bool Construct(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        const char*               Caption,
        ScalarCurveProfile&       Profile,
        ScalarCurveEditorSession& Session,
        const ControlPointer&     Pointer,
        float                     Opacity = 1.0f) noexcept;
};

} // namespace Frontier
