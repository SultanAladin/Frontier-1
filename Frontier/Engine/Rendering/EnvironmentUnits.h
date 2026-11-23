/*==========================================================================================================================================
                                                   ENVIRONMENT FRAME DECLARATION
                                     ------------------------------------------------------------------
                 Atmosphere LUT bakes, sky, clouds, fog, precipitation and flare declared as
                      ordered submission units (plan §8.1 groups; Phase 6).
==========================================================================================================================================*/

// 🧩 Environment frame declaration: the environment half of the frame as data.
//
//   AtmosphereTransmittance (LUT bake) → AtmosphereMultiple (LUT) → CelestialSky → NightStars
//     → CloudRecords → CloudShadowProject → CloudTrace → CloudPresent
//     → FogMedium → Precipitation → FlareVisibility → FlareImage
//
// The declaration marks LUTs persistent and imports surface depth. CompleteFrameUnits combines
// these target touches with illumination declarations; it does not execute them or present an editor frame.
// The portable layer derives ordering/aliasing/barriers, and Exhibits/EnvironmentPipeline tests selected models.

#pragma once

#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct EnvironmentFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

[[nodiscard]] EnvironmentFrameDeclaration DeclareEnvironmentFrame(uint32_t Width, uint32_t Height);

} // namespace Frontier
