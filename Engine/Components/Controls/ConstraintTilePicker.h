/*============================================================================================================================================
                                                          CONSTRAINTTILEPICKER.H
============================================================================================================================================*/
// 🧩 4x2 interactive constraint kind tile picker (Fixed, Hinge, Slider, Ball, Spring, Spin, Tether, Dock) for Details inspector

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ConstraintTilePicker
{
public:
    static constexpr float RecommendedHeightPx = 128.0f; // [px] - 2 rows of 58px tiles + 6px gap

    [[nodiscard]] static bool Construct(
        PixelSpace&                            Surface,
        Rectangle2D                            Bounds,
        ComponentClass::PhysicsConstraintKind& InOutKind,
        const ControlPointer&                  Pointer,
        float                                  Opacity = 1.0f) noexcept;

    [[nodiscard]] static const char* QueryKindLabel(ComponentClass::PhysicsConstraintKind Kind) noexcept;
    [[nodiscard]] static const char* QueryKindSummary(ComponentClass::PhysicsConstraintKind Kind) noexcept;
};

} // namespace Frontier
