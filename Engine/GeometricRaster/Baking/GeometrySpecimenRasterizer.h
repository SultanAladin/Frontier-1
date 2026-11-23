/*============================================================================================================================================
                                                        GEOMETRYSPECIMENRASTERIZER.H
============================================================================================================================================*/
// 🧩 Calibrated 3-point OpenPBR studio thumbnail raytracer baking transparent RGBA8 silhouettes with tight world-AABB auto-fit

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.h"

#include <cstdint>
#include <string_view>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class GeometrySpecimenArchetype : uint8_t
{
    Box                   = 0u,  // [-] - Neutral calibration box primitive
    Sphere                = 1u,  // [-] - Metallic/satin shader sphere primitive
    Cylinder              = 2u,  // [-] - Analytical capped cylinder primitive
    Cone                  = 3u,  // [-] - Safety traffic cone with reflective collars
    Torus                 = 4u,  // [-] - Metallic bronze torus ring primitive
    CylinderCone          = 5u,  // [-] - Hazard-striped cylindrical bollard
    CalibrationBevelBox   = 6u,  // [-] - Precision chamfered calibration block
    ControlledIntegrator  = 7u,  // [-] - Precision integrator sphere on stepped plinth
    AeroCoupeGT           = 8u,  // [-] - Sculpted multi-part aerodynamic speedform coupe
    TacticalCrate         = 9u,  // [-] - Reinforced crate with corner ribs
    ArmoredBust           = 10u, // [-] - Sculpted armored torso & visor helm on plinth
    UDIMArchKit           = 11u, // [-] - Architectural stepped plinth, columns & lintel
    ShaderBallSolid       = 12u, // [-] - Slotted outer shell + metallic core + pedestal ring
    ModularHullAssembly   = 13u, // [-] - Multi-part aerospace hull fuselage & swept fins
    EditorCameraRig       = 14u, // [-] - Digital cinema camera rig with matte box & EVF
    EngineCamera          = 15u, // [-] - 35mm cinema camera with twin coaxial magazines
    TripodBroadcastCamera = 16u, // [-] - Cinema camera mounted on 3-leg carbon tripod
    StudioSoftboxLight    = 17u, // [-] - Studio softbox luminaire with diffuser & yoke
    DownlightNarrowIES    = 18u, // [-] - Architectural recessed/pendant IES luminaire
    TurbineWindFan        = 19u, // [-] - Cowled studio turbine fan with pitched rotor blades
    TyreStack             = 20u, // [-] - Racing slick tyre stack with cobalt center-lock hub
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GeometrySpecimenDescriptor
{
    GeometrySpecimenArchetype Archetype             = GeometrySpecimenArchetype::Box; // [-] - Analytical primitive or prop preset
    Mathematics::LinearRGB    PrimaryTint           { 0.72f, 0.72f, 0.70f };          // [-] - Primary linear tint override
    bool                      UseTintOverride       = false;                          // [-] - True to tint primary parts with PrimaryTint
    bool                      TransparentBackground = true;                           // [-] - True for 100% transparent Alpha = 0 studio cutout
    uint32_t                  ResolutionPixels      = 128u;                           // [px]- Output square RGBA8 resolution
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve a ContentBrowser or Outliner entry title into its closest 3D specimen archetype
[[nodiscard]] GeometrySpecimenArchetype ResolveGeometrySpecimenArchetype(std::string_view DisplayTitle) noexcept;

// 📝 Bake a calibrated 3-point OpenPBR studio thumbnail with tight AABB camera framing & silhouette auto-fit
[[nodiscard]] MaterialSpace::SpecimenPreviewImage RasterizeGeometrySpecimenPreview(
    const GeometrySpecimenDescriptor& Descriptor);

// 📝 Convenience helper baking directly from entry display title and sRGB tint
[[nodiscard]] MaterialSpace::SpecimenPreviewImage RasterizeGeometrySpecimenByTitle(
    std::string_view DisplayTitle,
    ColorRgba        SrgbTint,
    uint32_t         ResolutionPixels      = 128u,
    bool             TransparentBackground = true);

} // namespace Frontier::GeometricRaster
