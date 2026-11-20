/*============================================================================================================================================
                                                              SKETCHERICONS.H
============================================================================================================================================*/
// 🧩 ThorVG-rasterized SVG icon atlas for Parametric Sketcher (66 domain symbols in EngineContent/Icons/Sketcher/*.svg)

#pragma once

#include <cstdint>
#include <filesystem>

struct ImDrawList;
struct ImVec2;

namespace Frontier
{

enum class IconSymbol : uint16_t
{
#define FRONTIER_ICON(Symbol, File) Symbol,
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherSymbols.inc"
#undef FRONTIER_ICON
    Count
};

enum class IconDomain : uint8_t
{
    Editor   = 0,
    Texture  = 1,
    Fluid    = 2,
    Sketcher = 3,
    SolidArc = 3,
};

struct SketcherIconPresentation
{
    static void Attach(const std::filesystem::path& IconsRoot);
    static bool Draw(
        ImDrawList* List,
        IconSymbol  Symbol,
        ImVec2      Min,
        float       Size,
        float       Opacity = 1.0f,
        IconDomain  Domain  = IconDomain::Sketcher,
        uint32_t    Tint    = 0xFFFFFFFFu);
};

} // namespace Frontier
