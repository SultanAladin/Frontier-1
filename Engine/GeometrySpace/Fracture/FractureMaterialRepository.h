/*============================================================================================================================================
                                                      FRACTUREMATERIALREPOSITORY.H
============================================================================================================================================*/
// 🧩 Physical SI fracture material constants, solid primitives, solver methods, and deterministic PRNG stream

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>

namespace Frontier::GeometrySpace::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FractureMaterialCategory : uint8_t
{
    BulkBrittle  = 0u, // [-] - Isotropic / bedded quasi-brittle solids (concrete, granite)
    Fibrous      = 1u, // [-] - Orthotropic fibrous grain (spruce wood)
    PlateBrittle = 2u, // [-] - Plate-brittle silicates (float glass, tempered glass)
    Ductile      = 3u, // [-] - Tough polymer yielding (ABS polymer)
};

enum class FractureSolidCategory : uint8_t
{
    Monolith     = 0u, // [-] - Tall rectangular architectural column
    CurtainPane  = 1u, // [-] - Thin glass curtain wall panel
    TimberBeam   = 2u, // [-] - Elongated horizontal structural beam
    Cylinder     = 3u, // [-] - Vertical cylindrical pillar
    Cone         = 4u, // [-] - Tapered conical solid
    Boulder      = 5u, // [-] - Faceted polyhedral boulder
    HollowCavity = 6u, // [-] - Heavily faceted cavity specimen
    Cube         = 7u, // [-] - Symmetric reference cube
};

enum class FractureSolverMethod : uint8_t
{
    FastRuntimeBoussinesq = 0u, // [-] - Sub-ms flat-array Boussinesq-Hertz kernel
    EngineeringBakedSolid = 1u, // [-] - 3D Navier-Cauchy lattice + non-planar fractography
    GriffithStressBudget  = 2u, // [-] - Griffith energy-budgeted plane clipping
    LefmWavefront         = 3u, // [-] - Radial star + flexural ring crack wavefront
    ClusteredVoronoi      = 4u, // [-] - Impact-clustered 3D Voronoi cell tessellation
    DendriticSpallHull    = 5u, // [-] - Outer spall hull + lightning interior polylines
};

enum class FractureExecutionMode : uint8_t
{
    Realtime    = 0u, // [-] - Instant full-release fracture (<1 ms partition)
    Baked       = 1u, // [-] - Cached pre-computed engineering fracture archive
    HybridBonds = 2u, // [-] - Cohesive strain-bond clusters with progressive rupture
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FractureMaterialSpec
{
    const char*              Identifier          = "concrete";                              // [-]      - Stable material key
    const char*              Label               = "Reinforced Concrete";                   // [-]      - Full display title
    const char*              ShortLabel          = "Concrete";                              // [-]      - Compact UI label
    FractureMaterialCategory Category            = FractureMaterialCategory::BulkBrittle;   // [-]      - Constitutive mode
    float                    DensityKgPerM3      = 2350.0f;                                 // [kg/m^3] - Bulk mass density
    float                    FractureToughnessGc = 140.0f;                                  // [J/m^2]  - Critical energy release rate
    float                    YoungModulusGPa     = 30.0f;                                   // [GPa]    - Elastic Young's modulus
    float                    PoissonRatio        = 0.20f;                                   // [-]      - Poisson's lateral contraction ratio
    float                    TensileStrengthMPa  = 3.8f;                                    // [MPa]    - Uniaxial tensile failure limit
    float                    WeibullModulus      = 8.0f;                                    // [-]      - Flaw scatter shape parameter m
    float                    StoredEnergyJPerM3  = 0.0f;                                    // [J/m^3]  - Residual tempering strain energy
    float                    HertzConeRad        = 0.95f;                                   // [rad]    - Hertzian cone half-angle
    float                    AnisotropyRatio     = 1.15f;                                   // [-]      - Directional toughness multiplier
    Mathematics::Axis3D      GrainAxis           { 0.0f, 0.0f, 1.0f };                      // [-]      - RHR +Z Up primary anisotropy axis
    float                    BeddingWeakness     = 0.12f;                                   // [0-1]    - Weak cleavage plane attenuation
    float                    RingBias            = 0.24f;                                   // [0-1]    - Concentric ring vs radial probability
    float                    CrackJitter         = 0.32f;                                   // [-]      - Local micro-flaw plane perturbation
    float                    Restitution         = 0.12f;                                   // [0-1]    - Rigid contact bounce factor
    float                    Friction            = 0.80f;                                   // [-]      - Coulomb surface friction
    float                    DustYield           = 2.20f;                                   // [-]      - Debris particulate spawn scale
    Mathematics::Scalar3D    ExteriorLinearRgb   { 0.266f, 0.278f, 0.258f };                // [linear] - Outer surface OpenPBR base color
    Mathematics::Scalar3D    InteriorLinearRgb   { 0.159f, 0.165f, 0.147f };                // [linear] - Fresh cut face OpenPBR base color
    float                    Roughness           = 0.92f;                                   // [0-1]    - OpenPBR specular roughness
    float                    Metalness           = 0.02f;                                   // [0-1]    - OpenPBR metallic weight
    float                    Transmission        = 0.0f;                                    // [0-1]    - OpenPBR glass transmission weight
};

struct EngineeringConstitutiveSpec
{
    float       CompressiveStrengthMPa = 38.0f;  // [MPa] - Unconfined compressive limit
    float       InternalFrictionDeg    = 36.0f;  // [deg] - Mohr-Coulomb friction angle phi
    float       HurstExponent          = 0.78f;  // [-]   - Bouchaud self-affine roughness exponent
    float       AggregateScale         = 6.5f;   // [1/m] - Meso-inclusion spatial frequency
    float       AggregateWeight        = 0.58f;  // [0-1] - Inclusion blend vs fBm roughness
    float       ReliefAmplitudeM       = 0.036f; // [m]   - Peak non-planar cut surface relief
    const char* FractographyLabel      = "";     // [-]   - Descriptive fractographic signature
};

class DeterministicRandomStream
{
public:
    explicit DeterministicRandomStream(uint32_t Seed = 42u) noexcept;
    [[nodiscard]] double NextDouble() noexcept;
    [[nodiscard]] float  NextFloat() noexcept;

private:
    uint32_t Register = 1u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureMaterialRepository
{
public:
    [[nodiscard]] static const FractureMaterialSpec&        LookupMaterial(const char* Identifier) noexcept;
    [[nodiscard]] static const FractureMaterialSpec&        LookupMaterialByIndex(uint32_t Index) noexcept;
    [[nodiscard]] static uint32_t                           QueryMaterialCount() noexcept;
    [[nodiscard]] static const EngineeringConstitutiveSpec& LookupEngineeringSpec(const char* Identifier) noexcept;
    [[nodiscard]] static double                             ComputeRayleighWaveSpeedMps(const FractureMaterialSpec& Spec) noexcept;
};

} // namespace Frontier::GeometrySpace::Fracture
