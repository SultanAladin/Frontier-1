//============================================================================================================================================
//                                                             SKYDOMEIMAGE.H
//============================================================================================================================================
// 🧩 Generates a viewport's sky image from the CPU atmosphere integrator.
//
// 📝 ⚠️ This lived under `Application/EditorHost/` and was the last definition in that host's second
//    translation unit. Nothing about it is host-specific — it reads an atmosphere and writes pixels — and
//    a proof was already using it from outside the host, which is the clearest sign it never belonged
//    there. It sits beside `AtmosphereIntegrator`, the model it evaluates.
//
//    The three resident surfaces (transmittance, multiple scattering, sky view)
//    are direction-indexed: sampling the sky-view surface at a view direction
//    yields the atmosphere's single + multiple scattered radiance along it. This
//    generator evaluates that model per output texel on a direction-indexed dome —
//    the azimuth across the width, the elevation down the height — and adds the
//    sun's direct disc, exactly as the GPU `SkyViewSurface.slang` entry point does
//    on the device: the same model, one texture the windowed hosts upload through
//    the sampled-image path. The viewport crops the dome to its camera's field of
//    view, so the sun stays in frame at any viewport aspect.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateCompute/Compute/AtmosphereIntegrator/Api/AtmosphereIntegrator.h"
#include "Shared/EnvironmentConfiguration.slang.h"

#include <cstdint>
#include <vector>

namespace Slate
{

/// 🧩 The editor viewport's fixed camera, which the sky image is generated for.
/// tag   guarantee, nonallocating, nonthrowing
struct SkyCamera
{
    double AzimuthDegrees  = 0.0;    // [deg] - the view direction's azimuth; the host turns it toward the sun
    double ElevationDegrees = 15.0;  // [deg] - fixed: the sun visibly rises and sets as the slider moves
    double FieldOfViewDegrees = 60.0; // [deg] - vertical field of view
};

/// 🧩 Evaluates one sky image from the atmosphere and the editor's environment.
/// in    Atmosphere  [-]  the integrator; its medium, sun and camera altitude are declared here
/// in    Environment [-]  the artist's sun/sky/atmosphere ordinates, as the inspector writes them
/// in    Camera      [-]  the viewport's fixed camera
/// in    Width       [px] the output extent; independent of the display
/// in    Height      [px]
/// out   Pixels      [-]  replaced with `Width * Height * 4` RGBA8 bytes, top row first
/// out   Result      [-]  refuses with ContentUnsupported when the integrator declines any declaration
/// note  🔴 The integrator rebuilds its surfaces only when the sun, the camera altitude or the medium
///        moved materially — so a drag that changed only the intensity re-evaluates the image against
///        the standing surfaces, which is exactly the rebuild discipline `28` §4 declares.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<bool> GenerateSkyImage(AtmosphereIntegrator& Atmosphere,
                               const EnvironmentConfiguration& Environment,
                               const SkyCamera& Camera,
                               std::uint32_t Width,
                               std::uint32_t Height,
                               std::vector<std::uint8_t>& Pixels);

} // namespace Slate
