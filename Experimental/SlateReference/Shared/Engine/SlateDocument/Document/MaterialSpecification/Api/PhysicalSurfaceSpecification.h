//============================================================================================================================================
//                                                 PHYSICALSURFACESPECIFICATION.H
//============================================================================================================================================
// 🧩 Bounded renderer-facing physical material closure; compatible features compose without exclusive models.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"

#include <cstdint>

namespace Slate
{

enum class SurfaceClosureSelection : std::uint32_t
{
    StandardSurface   = 0u,
    SubsurfaceSurface = 1u,
    ThinTransmission  = 2u,
    VolumeTransmission = 3u,
    Unlit             = 4u,
    Matcap            = 5u
};

enum class PhysicalFeature : std::uint32_t
{
    EonDiffuse       = 1u << 0u,
    Anisotropy       = 1u << 1u,
    SecondSpecular   = 1u << 2u,
    Coat             = 1u << 3u,
    Fuzz             = 1u << 4u,
    ThinFilm         = 1u << 5u
};

enum class CoverageSelection : std::uint32_t { Opaque, Cutout, Blended };
enum class WallSelection : std::uint32_t { Thick, ThinWalled };
enum class InterfaceParameterization : std::uint32_t { RefractionRatio, DirectF0 };

struct PhysicalSurfaceDeclaration
{
    SurfaceClosureSelection  Closure = SurfaceClosureSelection::StandardSurface;
    std::uint32_t            Features = 0u;
    CoverageSelection        Coverage = CoverageSelection::Opaque;
    WallSelection            Wall = WallSelection::Thick;
    InterfaceParameterization Interface = InterfaceParameterization::RefractionRatio;
    bool                     TwoSided = false;
};

/// 🧩 Immutable constants the initial renderer can consume without sampling inactive channels.
struct CompiledPhysicalSurface
{
    PhysicalSurfaceDeclaration Declaration = {};
    ColourSpecification Albedo = { 0.8, 0.8, 0.8, WorkingSpaceIdentity };
    ColourSpecification NormalIncidenceReflectance = { 0.04, 0.04, 0.04, WorkingSpaceIdentity };
    double Metallic = 0.0;
    double Roughness = 0.5;
    double Opacity = 1.0;
    double Transmission = 0.0;
    double RefractionRatio = 1.5;
    std::uint32_t ActiveChannelMask = 0u;
};

/// 🧩 Compiles declared constants and feature choices while retaining the document material as authority.
class PhysicalSurfaceExchange
{
public:
    Deliver<CompiledPhysicalSurface> Compile(const MaterialSpecification& Material,
                                             const PhysicalSurfaceDeclaration& Declaration) const;
};

} // namespace Slate
