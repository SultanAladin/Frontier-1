/*============================================================================================================================================
                                                              PARTICLEBUFFERS.H
============================================================================================================================================*/
// 🧩 16-byte-aligned GPU & CPU particle simulation buffer layouts (80B ParticleSlot, 16B StrandVertex, 32B Anchor, Uniforms)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Canonical 80-byte per-particle simulation record (mirrors Engine/Shaders/Particles/ParticleBuffers.slang).
struct alignas(16) ParticleSlot
{
    float    PositionMeters[3]   = { 0.0f, 0.0f, 0.0f };       // [m]   - World-space position
    float    AgeSeconds          = 0.0f;                       // [s]   - Elapsed age since spawn (<0 = inactive)
    float    VelocityMps[3]      = { 0.0f, 0.0f, 0.0f };       // [m/s] - World-space linear velocity
    float    LifespanSeconds     = 0.0f;                       // [s]   - Assigned lifetime duration
    float    SizeMeters          = 0.05f;                      // [m]   - Rendered radius / half-extent
    float    SeedScalar          = 0.0f;                       // [0-1] - Deterministic per-particle random seed
    float    SpinRadians         = 0.0f;                       // [rad] - Orientation / roll angle
    float    AuxScalar           = 0.0f;                       // [-]   - Auxiliary channel (species / curl phase / effector)
    float    OriginAnchor[3]     = { 0.0f, 0.0f, 0.0f };       // [m]   - Rest anchor or release origin position
    uint32_t PackedTraits        = 0u;                         // [bits]- Packed rig index, alive bit, strand/species tags
    float    ColourLinear[4]     = { 1.0f, 1.0f, 1.0f, 1.0f }; // [lin] - Evaluated scene-linear RGBA colour
};

static_assert(sizeof(ParticleSlot) == 80u, "ParticleSlot must remain exactly 80 bytes for GPU SSBO stride parity.");
static_assert(alignof(ParticleSlot) == 16u, "ParticleSlot must remain 16-byte aligned.");

/// Compact 16-byte vertex inside a 20-vertex continuous ribbon/strand history buffer.
struct alignas(16) StrandVertex
{
    float PositionMeters[3] = { 0.0f, 0.0f, 0.0f }; // [m]   - World-space spine vertex
    float NormalisedSpanU   = 0.0f;                 // [0-1] - Parametric arc position from head (0) to tail (1)
};

static_assert(sizeof(StrandVertex) == 16u, "StrandVertex must remain exactly 16 bytes.");

/// Pre-sampled 32-byte surface or firework anchor point uploaded to GPU anchor storage buffers.
struct alignas(16) ParticleAnchorSample
{
    float    PositionMeters[3] = { 0.0f, 0.0f, 0.0f }; // [m]   - Sampled anchor position in local object space
    float    CurvatureWeight   = 0.5f;                 // [0-1] - Ridge/crease curvature weight or burst shell radius
    float    NormalUnit[3]     = { 0.0f, 1.0f, 0.0f }; // [-]   - Outward unit normal or outward shell direction
    uint32_t GroupIndex        = 0u;                   // [idx] - Sub-part / shell / glyph index
};

static_assert(sizeof(ParticleAnchorSample) == 32u, "ParticleAnchorSample must remain exactly 32 bytes.");

/// 240-byte global simulation uniform block shared across release, advect, and rasterization shaders.
struct alignas(16) ParticleGlobalUniform
{
    float    ElapsedSeconds         = 0.0f;                 // [s]     - Total elapsed simulation clock
    float    DeltaSeconds           = 1.0f / 60.0f;         // [s]     - Current fixed step duration
    uint32_t StepCounter            = 0u;                   // [steps] - Monotonic simulation step counter
    uint32_t ActiveRigCount         = 0u;                   // [count] - Active particle rig count
    float    AmbientWindMps[3]      = { 2.2f, 0.0f, 0.8f }; // [m/s]   - Global ambient wind vector
    float    GravityMps2            = -9.81f;               // [m/s^2] - Standard gravitational acceleration
    float    CameraPosition[3]      = { 0.0f, 3.0f, 9.0f }; // [m]     - Active camera world position
    float    TimeScale              = 1.0f;                 // [x]     - Playback rate multiplier
    float    ColliderBallCenter[3]  = { 0.0f, 1.4f, 0.0f }; // [m]     - Interactive sphere collider centre
    float    ColliderBallRadius     = 0.55f;                // [m]     - Interactive sphere collider radius
    float    ColliderBallVelocity[3]= { 0.0f, 0.0f, 0.0f }; // [m/s]   - Interactive sphere collider velocity
    uint32_t ColliderActive         = 0u;                   // [0/1]   - Non-zero when interactive sphere collider is enabled
    uint32_t ActiveForceCount       = 0u;                   // [count] - Bound Vector Field Force sources
    uint32_t LatticeActive          = 0u;                   // [0/1]   - Non-zero when FieldLatticeCascade is bound
    uint32_t RandomSeed             = 1337u;                // [seed]  - Global deterministic seed
    uint32_t ReservedWord           = 0u;                   // [-]     - 16B alignment padding
    float    ViewProjection[16]     = {};                   // [-]     - Column-major camera ViewProjection matrix
    float    CameraRight[4]         = { 1.0f, 0.0f, 0.0f, 0.0f }; // [-] - Camera right basis vector
    float    CameraUp[4]            = { 0.0f, 1.0f, 0.0f, 0.0f }; // [-] - Camera up basis vector
};

static_assert(sizeof(ParticleGlobalUniform) == 192u, "ParticleGlobalUniform layout verified at 192 bytes.");

/// 256-byte per-rig simulation & shading uniform record.
struct alignas(16) ParticleRigUniform
{
    uint32_t SlotOffset           = 0u;                      // [idx]   - Base index into global ParticleSlot buffer
    uint32_t SlotCapacity         = 2048u;                   // [count] - Maximum slots owned by this rig
    uint32_t ReleaseOriginCode    = 0u;                      // [enum]  - ReleaseOriginCategory code
    uint32_t VisualCarrierCode    = 0u;                      // [enum]  - VisualCarrierCategory code
    uint32_t SpriteCutoutCode     = 0u;                      // [0..19] - Procedural sprite cutout index
    uint32_t ScalarChannelCode    = 0u;                      // [0..14] - ParticleScalarChannel code
    uint32_t OpticalBlendCode     = 0u;                      // [0/1]   - 0=Additive, 1=PremultipliedAlpha
    uint32_t BokehApertureCode    = 0u;                      // [0..3]  - Optical bokeh aperture style
    float    OriginCenter[3]      = { 0.0f, 1.0f, 0.0f };    // [m]     - Release origin centre
    float    OriginRadius         = 0.25f;                   // [m]     - Sphere/disc radius
    float    OriginHalfExtents[3] = { 1.0f, 1.0f, 1.0f };    // [m]     - Box / surface half-extents
    float    ReleaseRatePerSec    = 240.0f;                  // [1/s]   - Continuous spawn rate
    float    LaunchDirection[3]   = { 0.0f, 1.0f, 0.0f };    // [-]     - Unit launch axis
    float    SpreadRadians        = 0.5f;                    // [rad]   - Cone spread half-angle
    float    SpeedMinMps          = 1.0f;                    // [m/s]   - Minimum launch speed
    float    SpeedMaxMps          = 3.0f;                    // [m/s]   - Maximum launch speed
    float    LifeMinSeconds       = 1.0f;                    // [s]     - Minimum particle lifespan
    float    LifeMaxSeconds       = 2.0f;                    // [s]     - Maximum particle lifespan
    float    SizeBirthMeters      = 0.05f;                   // [m]     - Birth radius
    float    SizeDeathMeters      = 0.05f;                   // [m]     - Death radius
    float    SizeMultiplier       = 1.0f;                    // [x]     - Global size scale
    float    StreakThinness       = 1.0f;                    // [x]     - Streak width ratio
    float    GravityScale         = 1.0f;                    // [x]     - Gravity multiplier
    float    AerodynamicDrag      = 0.2f;                    // [1/s]   - Velocity damping
    float    WindCoupling         = 0.0f;                    // [1/s]   - Relaxation rate into ambient/force flow
    float    BuoyancyMps2         = 0.0f;                    // [m/s^2] - Upward thermal acceleration
    float    FloorBounce          = 0.0f;                    // [0-1]   - Restitution against floor plane
    float    FlutterAmplitude     = 0.0f;                    // [m/s]   - Lateral aerodynamic leaf flutter
    float    CurlFrequency        = 0.85f;                   // [1/m]   - 3D curl spatial frequency
    float    CurlAmplitude        = 2.2f;                    // [m/s]   - 3D curl velocity drive
    float    CurlEvolutionRate    = 0.24f;                   // [1/s]   - Curl temporal phase speed
    float    ManifoldRadius       = 1.75f;                   // [m]     - Spherical curl manifold radius
    float    ColourRangeMin       = 0.0f;                    // [-]     - Ramp input domain lower bound
    float    ColourRangeMax       = 1.0f;                    // [-]     - Ramp input domain upper bound
    float    ColourRepeat         = 1.0f;                    // [x]     - Ramp repeat cycles
    float    ColourPhase          = 0.0f;                    // [0-1]   - Ramp phase offset
    uint32_t ColourWrapCode       = 0u;                      // [0..2]  - Clamp / Repeat / Mirror
    uint32_t ColourCurveCode      = 0u;                      // [0..2]  - Linear / Smooth / Stepped
    uint32_t StrandCount          = 0u;                      // [count] - Active strand ribbon count
    uint32_t StrandSegments       = 20u;                     // [count] - Vertices per strand ribbon
    float    PrimaryTintLinear[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // [lin] - Fast fallback birth colour
    float    MidTintLinear[4]     = { 0.8f, 0.9f, 1.0f, 0.6f }; // [lin] - Fast fallback mid colour
    float    EndTintLinear[4]     = { 0.4f, 0.6f, 1.0f, 0.0f }; // [lin] - Fast fallback death colour
    uint32_t DynamicsBitmask      = 0u;                      // [bits]  - Active modular dynamics flags
    uint32_t ReceiverChannelMask  = 0x7Fu;                   // [bits]  - ForceFields receiver channel bitmask
    uint32_t AnchorBufferOffset   = 0u;                      // [idx]   - Start index in anchor buffer
    uint32_t AnchorBufferCount    = 0u;                      // [count] - Sampled anchor count for this rig
};

static_assert(sizeof(ParticleRigUniform) == 256u, "ParticleRigUniform must remain exactly 256 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                             PACKED TRAIT BIT HELPERS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t ParticleTraitAliveBit   = (1u << 0u);
constexpr uint32_t ParticleTraitPinnedBit  = (1u << 1u);
constexpr uint32_t ParticleTraitStrandBit  = (1u << 2u);
constexpr uint32_t ParticleTraitTornBit    = (1u << 3u);

[[nodiscard]] constexpr bool IsParticleSlotAlive(const ParticleSlot& Slot) noexcept
{
    return (Slot.PackedTraits & ParticleTraitAliveBit) != 0u
        && Slot.LifespanSeconds > 0.0f
        && Slot.AgeSeconds >= 0.0f
        && Slot.AgeSeconds < Slot.LifespanSeconds;
}

} // namespace Frontier::ParticleSpace
