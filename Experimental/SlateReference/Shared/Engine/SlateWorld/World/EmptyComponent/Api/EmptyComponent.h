//============================================================================================================================================
//                                                         EMPTYCOMPONENT.H
//============================================================================================================================================
// 🧩 A transformable scene anchor with no surface geometry.

#pragma once

#include <cstdint>

namespace Slate
{

enum class EmptyDisplayShape : std::uint32_t
{
    Axes = 0u,
    Cross = 1u,
    Box = 2u,
    Circle = 3u,
    ImagePlaceholder = 4u,
    ShapeCount = 5u
};

struct EmptyComponent
{
    EmptyDisplayShape Shape = EmptyDisplayShape::Axes;
    double DisplaySize = 1.0;
    std::uint32_t ColourRole = 0u;
};

} // namespace Slate
