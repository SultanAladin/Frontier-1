/*============================================================================================================================================
                                                           RELEASEORIGINTYPES.H
============================================================================================================================================*/
// 🧩 Modular typed particle release-origin geometry descriptors (analytic, structural, 17 surface anchors, 10 firework patterns)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <variant>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ReleaseOriginCategory : uint8_t
{
    Point            = 0u,  // [-] - Single spatial origin point
    SphereVolume     = 1u,  // [-] - Uniform or shell spherical volume
    Disc             = 2u,  // [-] - Circular disc normal to LaunchDirection
    BoxVolume        = 3u,  // [-] - Axis-aligned box defined by HalfExtents
    SurfaceLattice   = 4u,  // [-] - Structured MoGraph grid/lattice rows x columns
    DualElectrode    = 5u,  // [-] - Dual-pole dielectric arc / beam terminals
    SpiderwebWeave   = 6u,  // [-] - Radial spoke + capture spiral silk weave
    ClothGrid        = 7u,  // [-] - Hanging / rigged rectangular strand grid
    BotanicalRoot    = 8u,  // [-] - Rooted flora stalks / meadow blades / canopy boughs
    SurfaceAnchor    = 9u,  // [-] - Area-weighted sample on 17 procedural surfaces or custom PolygonNetwork
    FireworkPattern  = 10u  // [-] - 10 pyrotechnic shell / sky-text / shape burst samplers
};

enum class SurfacePresetCategory : uint8_t
{
    FloorMatrixXZ         = 0u,
    WallMatrixXY          = 1u,
    ObsidianSphere        = 2u,
    ObsidianHandOrb       = 3u,
    TorusRing             = 4u,
    CubicVolume           = 5u,
    UtahTeapot            = 6u,
    SportsCarSilhouette   = 7u,
    SculptedBust          = 8u,
    TorusKnot32           = 9u,
    PrecisionGear         = 10u,
    QuartzCrystalCluster  = 11u,
    ArchitecturalArchway  = 12u,
    BevelledMonolith      = 13u,
    FacetedSpire             = 14u,
    StellatedStarPoly        = 15u,
    CustomPolygonNetwork     = 16u,
    ControlVehicleIntegrator = 17u,
    Count                    = 18u
};

enum class SurfaceSamplingStrategy : uint8_t
{
    EvenSurface           = 0u, // [-] - Area-weighted uniform surface distribution
    SurfaceInteriorVolume = 1u, // [-] - Surface shell + stratified interior fill
    RidgeCreaseAccent     = 2u  // [-] - Biased toward high-curvature ridges & creases
};

enum class FireworkShapeCategory : uint8_t
{
    Bouquet     = 0u,
    SkyText     = 1u,
    Heart       = 2u,
    Star        = 3u,
    Smiley      = 4u,
    Saturn      = 5u,
    Crown       = 6u,
    Butterfly   = 7u,
    Spiral      = 8u,
    GrandFinale = 9u,
    Count       = 10u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AnalyticPointOrigin
{
    float CenterMeters[3] = { 0.0f, 0.5f, 0.0f }; // [m] - Point release coordinate
};

struct AnalyticSphereOrigin
{
    float CenterMeters[3] = { 0.0f, 1.5f, 0.0f }; // [m] - Sphere centre
    float RadiusMeters    = 0.5f;                 // [m] - Outer sphere radius
    bool  ShellOnly       = false;                // [-] - True constrains spawn onto outer surface
};

struct AnalyticDiscOrigin
{
    float CenterMeters[3] = { 0.0f, 0.2f, 0.0f }; // [m] - Disc centre
    float NormalAxis[3]   = { 0.0f, 1.0f, 0.0f }; // [-] - Disc plane unit normal
    float RadiusMeters    = 1.2f;                 // [m] - Disc outer radius
};

struct AnalyticBoxOrigin
{
    float CenterMeters[3]      = { 0.0f, 1.5f, 0.0f }; // [m] - Box centre
    float HalfExtentsMeters[3] = { 1.5f, 0.5f, 1.5f }; // [m] - Axis half-extents
};

struct DualElectrodeOrigin
{
    float CathodeMeters[3] = { -1.8f, 1.6f, 0.0f }; // [m] - Start electrode terminal
    float AnodeMeters[3]   = {  1.8f, 1.6f, 0.0f }; // [m] - Target electrode terminal
};

struct SurfaceLatticeOrigin
{
    uint32_t Columns             = 24u;                    // [count] - Lattice horizontal divisions
    uint32_t Rows                = 24u;                    // [count] - Lattice vertical divisions
    float    CenterMeters[3]     = { 0.0f, 0.1f, 0.0f };   // [m]     - Lattice centre
    float    HalfExtentsMeters[3]= { 3.5f, 0.0f, 3.5f };   // [m]     - Lattice span
};

struct SpiderwebOrigin
{
    uint32_t Spokes          = 18u;                    // [count] - Radial anchor spokes
    uint32_t SpiralRings     = 14u;                    // [count] - Capture spiral turns
    float    CenterMeters[3] = { 0.0f, 2.2f, 0.0f };   // [m]     - Hub coordinate
    float    RadiusMeters    = 2.1f;                   // [m]     - Outer anchor frame radius
    bool     MessyCobweb     = false;                  // [-]     - False=Orb-weave, True=Attic cobweb
};

struct ClothGridOrigin
{
    uint32_t Strands         = 28u;                    // [count] - Number of parallel strands / rows
    uint32_t SegmentsPerRow  = 16u;                    // [count] - Subdivided curve vertices per strand
    float    OriginMeters[3] = { 0.0f, 2.4f, 0.0f };   // [m]     - Top anchor bar / mast elevation
    float    SpanMeters      = 3.2f;                   // [m]     - Width or span length
    float    DropMeters      = 2.0f;                   // [m]     - Vertical drape / mast height
};

struct BotanicalRootOrigin
{
    uint32_t RootCount       = 180u;                   // [count] - Plant stalks / fronds / canopy stems
    uint32_t SegmentsPerStem = 12u;                    // [count] - Articulated vertices per stem
    float    CenterMeters[3] = { 0.0f, 0.0f, 0.0f };   // [m]     - Bed centre
    float    PatchRadius     = 3.2f;                   // [m]     - Meadow / seabed patch radius
    float    CanopyHeight    = 2.4f;                   // [m]     - Stalk / tree height
};

struct SurfaceAnchorOrigin
{
    SurfacePresetCategory   Preset           = SurfacePresetCategory::ObsidianSphere; // [-] - One of 17 surface presets
    SurfaceSamplingStrategy SamplingStrategy = SurfaceSamplingStrategy::EvenSurface;  // [-] - Sampling weighting
    float                   CenterMeters[3]  = { 0.0f, 2.25f, 0.0f };                 // [m] - World translation
    float                   ScaleMeters[3]   = { 1.65f, 1.65f, 1.65f };               // [m] - Surface half-extent scale
    float                   EulerDegrees[3]  = { 0.0f, 0.0f, 0.0f };                  // [deg] - Pitch / Yaw / Roll
    std::string             CustomAssetId    {};                                       // [-] - External GeometrySpace/OBJ/STL ID
};

struct FireworkOrigin
{
    FireworkShapeCategory Shape             = FireworkShapeCategory::Bouquet; // [-]   - One of 10 firework shapes
    std::string           SkyTextMessage    { "2026" };                       // [-]   - UTF-8 ASCII sky-writing text
    float                 ShellOrigin[3]    = { 0.0f, 4.2f, 0.0f };           // [m]   - Detonation burst centre
    float                 PatternRadius     = 2.6f;                           // [m]   - Expanded sky pattern radius
};

using ReleaseOriginSpec = std::variant<
    AnalyticPointOrigin,
    AnalyticSphereOrigin,
    AnalyticDiscOrigin,
    AnalyticBoxOrigin,
    DualElectrodeOrigin,
    SurfaceLatticeOrigin,
    SpiderwebOrigin,
    ClothGridOrigin,
    BotanicalRootOrigin,
    SurfaceAnchorOrigin,
    FireworkOrigin>;

[[nodiscard]] constexpr ReleaseOriginCategory QueryReleaseOriginCategory(const ReleaseOriginSpec& Spec) noexcept
{
    return static_cast<ReleaseOriginCategory>(Spec.index());
}

} // namespace Frontier::ParticleSpace
