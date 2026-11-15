/*==========================================================================================================================================
                                                    DYNAMICSURFACERESOLVER.H
==========================================================================================================================================*/
// 🧩 Asset-agnostic CPU reference resolver converting any DynamicMaterialRecipe + DynamicParameterBlock into a full 10-lobe OpenPBR sample

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DynamicSurfaceQuery
{
    Mathematics::Axis3D LocalPosition  { 0.0f, 0.0f, 1.0f }; // [m] - Position in asset local coordinates
    Mathematics::Axis3D WorldNormal    { 0.0f, 0.0f, 1.0f }; // [-] - Geometric unit surface normal
    Mathematics::Axis3D WorldTangent   { 1.0f, 0.0f, 0.0f }; // [-] - Geometric unit tangent
    Mathematics::Axis3D WorldBitangent { 0.0f, 1.0f, 0.0f }; // [-] - Geometric unit bitangent
    Mathematics::Axis3D WorldViewDir   { 0.0f, 0.0f, 1.0f }; // [-] - Unit outgoing direction toward camera
};

struct ResolvedDynamicSurface
{
    SurfaceMaterial        Material             {};                 // [-]     - Resolved 10-lobe OpenPBR 1.1 surface parameters
    Mathematics::Axis3D    ShadingNormal        { 0.0f, 0.0f, 1.0f }; // [-]     - Normal perturbed by procedural relief/waves/beads
    Mathematics::LinearRGB IncandescentRadiance { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Non-thermal / blackbody luminous edge & surface radiance
    float                  SurfaceOpacity       = 1.0f;             // [0-1]   - Alpha opacity for translucent barriers & water
    float                  BacklitCrestScatter  = 0.0f;             // [0-1]   - Translucent ocean wave crest scatter mask
    bool                   CutoutDiscarded      = false;            // [-]     - True when voxel lattice cutout removes fragment
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve any DynamicMaterialRecipe at a surface point into full 10-lobe OpenPBR parameters, perturbed normal, glow & opacity
[[nodiscard]] ResolvedDynamicSurface ResolveDynamicSurface(
    const DynamicMaterialRecipe& Recipe,
    const DynamicParameterBlock& Parameters,
    const DynamicSurfaceQuery&   Query) noexcept;

} // namespace Frontier::MaterialSpace::Dynamic
