/*============================================================================================================================================
                                                      DIAGNOSTICINFOGRAPHICCANVAS.H
============================================================================================================================================*/
// 🧩 Reusable tactile schematic infographic canvas (Optical Bokeh Aperture selector, Wind Rose compass, Force Field reach & Release Cone diagrams)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InfographicArchetype : uint8_t
{
    BokehApertureSelector = 0u, // [-] - 4-card interactive Optical Bokeh Aperture chooser (Mixed, Sharp, Creamy, Rim Ring)
    WindRoseCompass       = 1u, // [-] - 360-degree directional wind bearing & velocity compass
    ForceFieldReach       = 2u, // [-] - Concentric attenuation envelope & directional vector streamlines
    ReleaseConeEnvelope   = 3u, // [-] - Particle launch spread envelope & ballistic arcs
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DiagnosticInfographicSpec
{
    InfographicArchetype Archetype       = InfographicArchetype::BokehApertureSelector;
    const char*          BadgeLeft       = "OPTICAL BOKEH APERTURE";
    const char*          BadgeRight      = "SCALE 1.00x";
    const char*          FooterHint      = "Click any aperture lens to switch bokeh head style live across all ribbons";
    uint32_t             ActiveOptionIdx = 0u;    // [idx] - Active lens option (0=Mixed, 1=Sharp, 2=Creamy, 3=Rim Ring)
    float                PrimaryAngleDeg = 68.0f; // [deg] - Bearing angle for WindRose / ForceField schematics
    float                PrimaryScalar   = 2.2f;  // [-]   - Wind m/s or force strength magnitude
    float                SecondaryScalar = 1.0f;  // [-]   - Secondary scale or attenuation ratio
    double               AnimationTimeSec= 0.0;   // [s]   - Subtle live schematic animation phase
};

struct DiagnosticInfographicResult
{
    bool     OptionChanged   = false; // [-]   - True when user clicked one of the interactive schematic options
    uint32_t SelectedOption  = 0u;    // [idx] - Newly clicked option index
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class DiagnosticInfographicCanvas
{
public:
    static constexpr float RecommendedHeightPx = 196.0f; // [px] - Canonical inspector infographic card height

    [[nodiscard]] static DiagnosticInfographicResult Construct(
        PixelSpace&                      Surface,
        Rectangle2D                      Bounds,
        const DiagnosticInfographicSpec& Spec,
        const ControlPointer&            Pointer,
        float                            Opacity = 1.0f) noexcept;
};

} // namespace Frontier
