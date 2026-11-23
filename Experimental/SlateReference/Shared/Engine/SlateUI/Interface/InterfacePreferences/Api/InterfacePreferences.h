//============================================================================================================================================
//                                                     INTERFACEPREFERENCES.H
//============================================================================================================================================
// 🧩 Typed artist preferences that cross from the Control Centre to their owning interface subsystems.

#pragma once

#include <cstdint>

namespace Slate
{

/// 🧩 Antialiasing applied to interface lines, borders, curves, circles, and filled rounded geometry.
/// note  This does not select viewport antialiasing or rebuild the font atlas; those are separate preferences.
enum class InterfaceAntialiasing : std::uint32_t
{
    Refined = 0u,   // [-] - antialiased lines/fills, texture-assisted lines, tighter curve tolerance
    Basic   = 1u,   // [-] - antialiased lines/fills without texture-assisted line strips
    None    = 2u,   // [-] - no interface geometry antialiasing
    ModeCount = 3u
};

/// 🧩 Font-atlas rasterisation preference. Application is deferred until the font pipeline can rebuild safely.
enum class FontRasterisation : std::uint32_t
{
    Automatic = 0u,
    Greyscale = 1u,
    None      = 2u,
    ModeCount = 3u
};

/// 🧩 Curve-flattening quality for generated SVG geometry; independent of interface backend antialiasing.
enum class VectorTessellation : std::uint32_t
{
    Refined = 0u,
    Balanced = 1u,
    Economy = 2u,
    QualityCount = 3u
};

}   // namespace Slate
