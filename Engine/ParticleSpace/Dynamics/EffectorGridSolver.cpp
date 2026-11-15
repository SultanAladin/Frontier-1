/*============================================================================================================================================
                                                            EFFECTORGRIDSOLVER.CPP
============================================================================================================================================*/
// 🧩 Exact C++ MoGraph & Effector Grid solver (Kinetic Hex Torus Matrix, Obsidian Control Vehicle Halftone & Tiles, Height Cubes, Placeable 3D Effectors)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/EffectorGridSolver.h"

#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float Pi    = 3.14159265358979323846f;
constexpr float TwoPi = 6.28318530717958647692f;
constexpr float DegToRad = Pi / 180.0f;

float ClampF(float X, float Lo, float Hi) noexcept
{
    return (X < Lo) ? Lo : ((X > Hi) ? Hi : X);
}

float MixF(float A, float B, float T) noexcept
{
    return A + (B - A) * T;
}

float SmoothstepF(float E0, float E1, float X) noexcept
{
    const float Denom = E1 - E0;
    const float T = ClampF((X - E0) / (std::fabs(Denom) > 1e-6f ? Denom : 1e-6f), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

float Hash31(float Ix, float Iy, float Iz) noexcept
{
    const float Dot = Ix * 127.1f + Iy * 311.7f + Iz * 74.7f;
    const float S   = std::sin(Dot) * 43758.5453123f;
    return S - std::floor(S);
}

// 1:1 smooth trilinear 3D scalar noise matching HTML shaders.js simVNoise
float SimScalarNoise3D(float Px, float Py, float Pz) noexcept
{
    const float Ix = std::floor(Px), Iy = std::floor(Py), Iz = std::floor(Pz);
    const float Fx = Px - Ix, Fy = Py - Iy, Fz = Pz - Iz;
    const float Ux = Fx * Fx * (3.0f - 2.0f * Fx);
    const float Uy = Fy * Fy * (3.0f - 2.0f * Fy);
    const float Uz = Fz * Fz * (3.0f - 2.0f * Fz);

    const float C000 = Hash31(Ix,        Iy,        Iz);
    const float C100 = Hash31(Ix + 1.0f, Iy,        Iz);
    const float C010 = Hash31(Ix,        Iy + 1.0f, Iz);
    const float C110 = Hash31(Ix + 1.0f, Iy + 1.0f, Iz);
    const float C001 = Hash31(Ix,        Iy,        Iz + 1.0f);
    const float C101 = Hash31(Ix + 1.0f, Iy,        Iz + 1.0f);
    const float C011 = Hash31(Ix,        Iy + 1.0f, Iz + 1.0f);
    const float C111 = Hash31(Ix + 1.0f, Iy + 1.0f, Iz + 1.0f);

    const float X00 = MixF(C000, C100, Ux);
    const float X10 = MixF(C010, C110, Ux);
    const float X01 = MixF(C001, C101, Ux);
    const float X11 = MixF(C011, C111, Ux);
    return MixF(MixF(X00, X10, Uy), MixF(X01, X11, Uy), Uz);
}

// 1:1 signed distance matching HTML shaders.js effShapeSdf
float EvaluateEffectorVolumeSdf(const EffectorVolumeDescriptor& Vol, const float WorldPos[3]) noexcept
{
    const float Dx = WorldPos[0] - Vol.CenterM[0];
    const float Dy = WorldPos[1] - Vol.CenterM[1];
    const float Dz = WorldPos[2] - Vol.CenterM[2];
    const float S  = std::max(Vol.RadiusMeters, 0.05f);

    switch (Vol.Shape)
    {
        case EffectorVolumeShape::Box:
        {
            const float Qx = std::fabs(Dx) - S;
            const float Qy = std::fabs(Dy) - S;
            const float Qz = std::fabs(Dz) - S;
            const float Ox = std::max(Qx, 0.0f);
            const float Oy = std::max(Qy, 0.0f);
            const float Oz = std::max(Qz, 0.0f);
            return std::sqrt(Ox * Ox + Oy * Oy + Oz * Oz) + std::min(std::max(Qx, std::max(Qy, Qz)), 0.0f);
        }
        case EffectorVolumeShape::Torus:
        {
            const float Rxz = std::sqrt(Dx * Dx + Dz * Dz) - S;
            return std::sqrt(Rxz * Rxz + Dy * Dy) - S * 0.32f;
        }
        case EffectorVolumeShape::Disc:
        {
            const float H  = S * 0.12f;
            const float Qx = std::sqrt(Dx * Dx + Dz * Dz) - S;
            const float Qy = std::fabs(Dy) - H;
            const float Ox = std::max(Qx, 0.0f);
            const float Oy = std::max(Qy, 0.0f);
            return std::sqrt(Ox * Ox + Oy * Oy) + std::min(std::max(Qx, Qy), 0.0f);
        }
        case EffectorVolumeShape::Cone:
        {
            const float R  = std::sqrt(Dx * Dx + Dz * Dz);
            const float Hh = ClampF((Dy + S) / std::max(2.0f * S, 1e-4f), 0.0f, 1.0f);
            const float Rad = MixF(S, 0.0f, Hh);
            const float Side = (R - Rad) * 0.86f;
            const float Cap  = std::max(Dy - S, -Dy - S);
            return std::max(Side, Cap);
        }
        case EffectorVolumeShape::Sphere:
        default:
            return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - S;
    }
}

float EvaluateSinglePlaceableWeight(const EffectorVolumeDescriptor& Vol, const float WorldPos[3]) noexcept
{
    if (Vol.RadiusMeters <= 0.01f) return 0.0f;
    const float Sdf = EvaluateEffectorVolumeSdf(Vol, WorldPos);
    return ClampF(1.0f - SmoothstepF(0.0f, std::max(0.35f, Vol.RadiusMeters * 0.55f), Sdf), 0.0f, 1.0f);
}

void RotateAroundY(float V[3], float CosY, float SinY) noexcept
{
    const float X =  V[0] * CosY + V[2] * SinY;
    const float Z = -V[0] * SinY + V[2] * CosY;
    V[0] = X;
    V[2] = Z;
}

struct EvaluatedAnchorPoint
{
    float    WorldPos[3]  = { 0.0f, 0.0f, 0.0f };
    float    UnitNormal[3] = { 0.0f, 1.0f, 0.0f };
    float    LocalElevY   = 0.0f;
    uint32_t MaterialSlot = 6u;
};

EvaluatedAnchorPoint EvaluateGridSurfaceAnchor(
    uint32_t                              SlotIdx,
    uint32_t                              Capacity,
    const EffectorLatticeSpec&            Spec,
    const float                           OriginCenter[3],
    std::span<const ParticleAnchorSample> VehicleOrCustomAnchors,
    float                                 ElapsedSeconds) noexcept
{
    EvaluatedAnchorPoint Out {};
    const uint32_t Cols = std::max(2u, Spec.GridColumns);
    const uint32_t Rows = std::max(2u, Spec.GridRows);
    const float ExtX = std::max(0.35f, Spec.OriginExtentM[0]);
    const float ExtY = std::max(0.35f, Spec.OriginExtentM[1]);
    const float ExtZ = std::max(0.35f, Spec.OriginExtentM[2]);

    switch (Spec.HostSurface)
    {
        case EffectorHostSurfaceCategory::ObsidianSphere:
        {
            const float Nf  = static_cast<float>(std::max(1u, Capacity));
            const float K   = static_cast<float>(SlotIdx) + 0.5f;
            const float Y01 = 1.0f - (2.0f * K) / Nf;
            const float Rxz = std::sqrt(std::max(0.0f, 1.0f - Y01 * Y01));
            const float Phi = static_cast<float>(SlotIdx) * 2.3999632f;
            const float Nx  = std::cos(Phi) * Rxz;
            const float Ny  = Y01;
            const float Nz  = std::sin(Phi) * Rxz;
            const float Rad = std::max(0.35f, ExtX);
            Out.WorldPos[0]   = OriginCenter[0] + Nx * (Rad + 0.008f);
            Out.WorldPos[1]   = OriginCenter[1] + Ny * (Rad + 0.008f);
            Out.WorldPos[2]   = OriginCenter[2] + Nz * (Rad + 0.008f);
            Out.UnitNormal[0] = Nx;
            Out.UnitNormal[1] = Ny;
            Out.UnitNormal[2] = Nz;
            Out.LocalElevY    = Ny;
            break;
        }
        case EffectorHostSurfaceCategory::TorusRing:
        {
            // Staggered brick-offset hexagonal torus parameterization (exact HTML shaders.js surf == 4)
            const uint32_t UCols = std::max(8u, Cols);
            const uint32_t VRows = std::max(6u, (Capacity + UCols - 1u) / UCols);
            const uint32_t Iu    = SlotIdx % UCols;
            const uint32_t Iv    = (SlotIdx / UCols) % VRows;
            const float Revolve  = ElapsedSeconds * 0.22f;
            const float Phi      = (static_cast<float>(Iu) / static_cast<float>(UCols)) * TwoPi
                                   + static_cast<float>(Iv & 1u) * (Pi / static_cast<float>(UCols))
                                   + Revolve;
            const float Theta    = (static_cast<float>(Iv) / static_cast<float>(VRows)) * TwoPi;
            const float Rmaj     = std::max(0.5f, ExtX);
            const float Rmin     = Rmaj * 0.38f;
            const float Rad      = Rmaj + Rmin * std::cos(Theta);

            const float Px = std::cos(Phi) * Rad;
            const float Py = std::sin(Theta) * Rmin;
            const float Pz = std::sin(Phi) * Rad;
            const float Nx = std::cos(Phi) * std::cos(Theta);
            const float Ny = std::sin(Theta);
            const float Nz = std::sin(Phi) * std::cos(Theta);

            Out.WorldPos[0]   = OriginCenter[0] + Px + Nx * 0.008f;
            Out.WorldPos[1]   = OriginCenter[1] + Py + Ny * 0.008f;
            Out.WorldPos[2]   = OriginCenter[2] + Pz + Nz * 0.008f;
            Out.UnitNormal[0] = Nx;
            Out.UnitNormal[1] = Ny;
            Out.UnitNormal[2] = Nz;
            Out.LocalElevY    = Ny;
            break;
        }
        case EffectorHostSurfaceCategory::ControlVehicle:
        {
            const float Scale = std::max(0.20f, Spec.OriginExtentM[0]);
            const float Yaw   = Spec.VehicleYawDegrees * DegToRad;
            const float CosY  = std::cos(Yaw);
            const float SinY  = std::sin(Yaw);

            if (!VehicleOrCustomAnchors.empty())
            {
                const auto& A = VehicleOrCustomAnchors[SlotIdx % VehicleOrCustomAnchors.size()];
                float Lp[3] = { A.PositionMeters[0] * Scale, A.PositionMeters[1] * Scale, A.PositionMeters[2] * Scale };
                float Ln[3] = { A.NormalUnit[0], A.NormalUnit[1], A.NormalUnit[2] };
                RotateAroundY(Lp, CosY, SinY);
                RotateAroundY(Ln, CosY, SinY);
                const float NL = std::sqrt(Ln[0] * Ln[0] + Ln[1] * Ln[1] + Ln[2] * Ln[2]);
                if (NL > 1e-6f) { Ln[0] /= NL; Ln[1] /= NL; Ln[2] /= NL; }
                Out.WorldPos[0]   = OriginCenter[0] + Lp[0] + Ln[0] * 0.010f;
                Out.WorldPos[1]   = OriginCenter[1] + Lp[1] + Ln[1] * 0.010f;
                Out.WorldPos[2]   = OriginCenter[2] + Lp[2] + Ln[2] * 0.010f;
                Out.UnitNormal[0] = Ln[0];
                Out.UnitNormal[1] = Ln[1];
                Out.UnitNormal[2] = Ln[2];
                Out.LocalElevY    = A.CurvatureWeight;
                Out.MaterialSlot  = A.GroupIndex;
            }
            break;
        }
        case EffectorHostSurfaceCategory::CubicVolume:
        {
            const uint32_t Side = std::max(3u, static_cast<uint32_t>(std::round(std::cbrt(static_cast<float>(std::max(1u, Capacity))))));
            const uint32_t Gx   = SlotIdx % Side;
            const uint32_t Gy   = (SlotIdx / Side) % Side;
            const uint32_t Gz   = (SlotIdx / (Side * Side)) % Side;
            const float Fu = (static_cast<float>(Gx) + 0.5f) / static_cast<float>(Side) * 2.0f - 1.0f;
            const float Fv = (static_cast<float>(Gy) + 0.5f) / static_cast<float>(Side) * 2.0f - 1.0f;
            const float Fw = (static_cast<float>(Gz) + 0.5f) / static_cast<float>(Side) * 2.0f - 1.0f;
            Out.WorldPos[0]   = OriginCenter[0] + Fu * ExtX;
            Out.WorldPos[1]   = OriginCenter[1] + Fv * ExtY;
            Out.WorldPos[2]   = OriginCenter[2] + Fw * ExtZ;
            Out.UnitNormal[0] = 0.0f; Out.UnitNormal[1] = 1.0f; Out.UnitNormal[2] = 0.0f;
            Out.LocalElevY    = Fv;
            break;
        }
        case EffectorHostSurfaceCategory::WallPlaneXY:
        {
            const uint32_t Ci = SlotIdx % Cols;
            const uint32_t Ri = (SlotIdx / Cols) % Rows;
            const float U01 = (static_cast<float>(Ci) + 0.5f) / static_cast<float>(Cols) * 2.0f - 1.0f;
            const float V01 = (static_cast<float>(Ri) + 0.5f) / static_cast<float>(Rows) * 2.0f - 1.0f;
            Out.WorldPos[0]   = OriginCenter[0] + U01 * ExtX;
            Out.WorldPos[1]   = OriginCenter[1] + V01 * ExtY;
            Out.WorldPos[2]   = OriginCenter[2];
            Out.UnitNormal[0] = 0.0f; Out.UnitNormal[1] = 0.0f; Out.UnitNormal[2] = 1.0f;
            Out.LocalElevY    = V01;
            break;
        }
        case EffectorHostSurfaceCategory::FloorPlaneXZ:
        default:
        {
            const uint32_t Ci = SlotIdx % Cols;
            const uint32_t Ri = (SlotIdx / Cols) % Rows;
            const float U01 = (static_cast<float>(Ci) + 0.5f) / static_cast<float>(Cols) * 2.0f - 1.0f;
            const float V01 = (static_cast<float>(Ri) + 0.5f) / static_cast<float>(Rows) * 2.0f - 1.0f;
            Out.WorldPos[0]   = OriginCenter[0] + U01 * ExtX;
            Out.WorldPos[1]   = OriginCenter[1];
            Out.WorldPos[2]   = OriginCenter[2] + V01 * ExtZ;
            Out.UnitNormal[0] = 0.0f; Out.UnitNormal[1] = 1.0f; Out.UnitNormal[2] = 0.0f;
            Out.LocalElevY    = 0.0f;
            break;
        }
    }
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateEffectorGridWeight(
    const EffectorLatticeSpec&                                  Spec,
    const float                                                 OriginCenter[3],
    const float                                                 WorldPos[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator*  OptionalForces,
    const InteractiveColliderSpec&                              Collider,
    float                                                       ElapsedSeconds) noexcept
{
    const float Freq     = std::max(0.15f, Spec.WaveFrequencyPerM);
    const float Speed    = std::max(0.0f,  Spec.WaveDriveSpeed);
    const float Contrast = std::max(0.40f, Spec.FalloffContrast);
    const float ExtSpan  = std::max(std::max(Spec.OriginExtentM[0], Spec.OriginExtentM[2]), 1.0f);

    const float Dx = (WorldPos[0] - OriginCenter[0]) * Freq;
    const float Dy = (WorldPos[1] - OriginCenter[1]) * Freq;
    const float Dz = (WorldPos[2] - OriginCenter[2]) * Freq;

    float Weight = 0.0f;
    switch (Spec.Driver)
    {
        case EffectorDriverCategory::FluidSimplexWave:
        {
            const float TShift = ElapsedSeconds * Speed;
            const float Warp   = SimScalarNoise3D(Dx * 0.78f, Dy * 0.78f - TShift * 0.35f, Dz * 0.78f + TShift * 0.25f) * 2.4f;
            const float WaveA  = 0.5f + 0.5f * std::sin(Dx * 1.35f + Dy * 1.55f - TShift * 1.55f + Warp);
            const float Blot   = SimScalarNoise3D(Dx * 1.18f + TShift * 0.42f, Dy * 1.18f - TShift * 0.28f, Dz * 1.18f + 2.3f);
            Weight = ClampF(WaveA * 0.62f + Blot * 0.58f - 0.14f, 0.0f, 1.0f);
            break;
        }
        case EffectorDriverCategory::OrbitingDual:
        {
            const float Ph0  = ElapsedSeconds * Speed * 1.35f;
            const float Ph1  = ElapsedSeconds * Speed * 1.05f + 2.4f;
            const float OrbR = ExtSpan * 0.68f;
            const float E0[3] = {
                OriginCenter[0] + std::cos(Ph0) * OrbR,
                OriginCenter[1] + std::sin(Ph0 * 1.3f) * OrbR * 0.55f,
                OriginCenter[2] + std::sin(Ph0) * OrbR
            };
            const float E1[3] = {
                OriginCenter[0] + std::cos(Ph1) * OrbR * 0.85f,
                OriginCenter[1] + std::cos(Ph1 * 0.9f) * OrbR * 0.50f,
                OriginCenter[2] + std::sin(Ph1 * 1.2f) * OrbR * 0.85f
            };
            const float RadEff = std::max(0.55f, 1.15f / std::max(0.35f, Freq));
            const float D0 = std::sqrt((WorldPos[0] - E0[0]) * (WorldPos[0] - E0[0]) +
                                       (WorldPos[1] - E0[1]) * (WorldPos[1] - E0[1]) +
                                       (WorldPos[2] - E0[2]) * (WorldPos[2] - E0[2]));
            const float D1 = std::sqrt((WorldPos[0] - E1[0]) * (WorldPos[0] - E1[0]) +
                                       (WorldPos[1] - E1[1]) * (WorldPos[1] - E1[1]) +
                                       (WorldPos[2] - E1[2]) * (WorldPos[2] - E1[2]));
            const float G0 = 1.0f - SmoothstepF(RadEff * 0.18f, RadEff, D0);
            const float G1 = 1.0f - SmoothstepF(RadEff * 0.18f, RadEff, D1);
            Weight = std::max(G0, G1);
            break;
        }
        case EffectorDriverCategory::CurlIslands:
        {
            const float Ts = ElapsedSeconds * Speed * 0.45f;
            const float N0 = SimScalarNoise3D(Dx * 1.15f + Ts, Dy * 1.15f - Ts * 0.7f, Dz * 1.15f + Ts * 0.5f);
            const float N1 = SimScalarNoise3D(Dx * 2.25f - Ts * 1.3f, Dy * 2.25f + Ts * 0.4f, Dz * 2.25f - Ts * 0.8f);
            Weight = ClampF((N0 * 0.68f + N1 * 0.32f - 0.26f) * 1.65f, 0.0f, 1.0f);
            break;
        }
        case EffectorDriverCategory::ExpandingPulseRing:
        {
            // Radial ripple across the torus / ControlVehicle silhouette (exact HTML effMode == 3)
            const float Rx = WorldPos[0] - OriginCenter[0];
            const float Ry = WorldPos[1] - OriginCenter[1];
            const float Rz = WorldPos[2] - OriginCenter[2];
            const float Dist = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
            const float Phase = Dist * Freq * 3.6f - ElapsedSeconds * Speed * 3.2f;
            Weight = ClampF(std::pow(std::max(0.0f, 0.5f + 0.5f * std::sin(Phase)), 1.4f), 0.0f, 1.0f);
            break;
        }
        case EffectorDriverCategory::Placeable3DEffectors:
        {
            const float Wa = EvaluateSinglePlaceableWeight(Spec.PrimaryVolume, WorldPos);
            const float Wb = EvaluateSinglePlaceableWeight(Spec.SecondaryVolume, WorldPos);
            Weight = std::max(Wa, Wb);
            break;
        }
        case EffectorDriverCategory::BallForceFieldsOnly:
        default:
            Weight = 0.02f;
            break;
    }

    if (Spec.Driver != EffectorDriverCategory::Placeable3DEffectors && Spec.BlendPlaceable)
    {
        Weight = std::max(Weight, EvaluateSinglePlaceableWeight(Spec.PrimaryVolume, WorldPos));
        Weight = std::max(Weight, EvaluateSinglePlaceableWeight(Spec.SecondaryVolume, WorldPos));
    }

    if (OptionalForces != nullptr)
    {
        const SimulationSpace::ForceFields::SpatialVector3 Pt { WorldPos[0], WorldPos[1], WorldPos[2] };
        const SimulationSpace::ForceFields::SpatialVector3 ZeroV { 0.0f, 0.0f, 0.0f };
        const auto Sample = OptionalForces->QueryForceFieldSample(Pt, ZeroV, static_cast<double>(ElapsedSeconds));
        const float AccMag = std::sqrt(Sample.LinearAcceleration.X * Sample.LinearAcceleration.X +
                                       Sample.LinearAcceleration.Y * Sample.LinearAcceleration.Y +
                                       Sample.LinearAcceleration.Z * Sample.LinearAcceleration.Z);
        const float AdvMag = std::sqrt(Sample.AdvectionVelocity.X * Sample.AdvectionVelocity.X +
                                       Sample.AdvectionVelocity.Y * Sample.AdvectionVelocity.Y +
                                       Sample.AdvectionVelocity.Z * Sample.AdvectionVelocity.Z);
        Weight = std::max(Weight, ClampF(AccMag * 0.14f + AdvMag * 0.12f, 0.0f, 1.0f));
    }

    if (Collider.BallActive && Collider.BallRadiusMeters > 0.04f)
    {
        const float Bx = WorldPos[0] - Collider.BallCenterM[0];
        const float By = WorldPos[1] - Collider.BallCenterM[1];
        const float Bz = WorldPos[2] - Collider.BallCenterM[2];
        const float Bd = std::sqrt(Bx * Bx + By * By + Bz * Bz);
        const float Br = Collider.BallRadiusMeters + 0.85f / std::max(0.4f, Freq);
        const float BBoost = 1.0f - SmoothstepF(Collider.BallRadiusMeters * 0.35f, Br, Bd);
        Weight = std::max(Weight, BBoost);
    }

    return std::pow(ClampF(Weight, 0.0f, 1.0f), Contrast);
}

void StepEffectorGridEnsemble(
    const ParticleRig&                                         Rig,
    const EffectorLatticeSpec&                                 Spec,
    const float                                                OriginCenter[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator& Forces,
    const InteractiveColliderSpec&                             Collider,
    std::span<const ParticleAnchorSample>                      OptionalRigAnchors,
    std::span<ParticleSlot>                                    RigSlots,
    float                                                      ElapsedSeconds) noexcept
{
    const uint32_t Capacity = static_cast<uint32_t>(RigSlots.size());
    if (Capacity == 0u) return;

    // Cache deterministic Controlled Integrator surface anchors when HostSurface == ControlVehicle
    std::vector<ParticleAnchorSample> LocalVehicleAnchors;
    std::span<const ParticleAnchorSample> ActiveAnchors = OptionalRigAnchors;
    if (Spec.HostSurface == EffectorHostSurfaceCategory::ControlVehicle)
    {
        static uint32_t CachedCount = 0u;
        static bool     CachedWheels = true;
        static std::vector<ParticleAnchorSample> CachedVehicleAnchors;
        if (CachedCount != Capacity || CachedWheels != Spec.IncludeVehicleWheels || CachedVehicleAnchors.empty())
        {
            CachedVehicleAnchors = SampleControlVehicleAnchors(Capacity, 1337u, Spec.IncludeVehicleWheels, false);
            CachedCount          = Capacity;
            CachedWheels         = Spec.IncludeVehicleWheels;
        }
        ActiveAnchors = CachedVehicleAnchors;
    }

    const float CellW   = std::max(0.008f, Spec.BaseCellWidthMeters * Rig.Kinematics.SizeMultiplier);
    const float MinFrac = ClampF(Spec.MinScaleFraction, 0.0f, 0.95f);
    const float MaxH    = std::max(0.02f, Spec.MaxExtrusionMeters);
    const uint32_t PrimCode = static_cast<uint32_t>(Spec.PrimitiveShape) & 0xFu;

    for (uint32_t I = 0u; I < Capacity; ++I)
    {
        const EvaluatedAnchorPoint Anchor = EvaluateGridSurfaceAnchor(
            I, Capacity, Spec, OriginCenter, ActiveAnchors, ElapsedSeconds);

        const float W = EvaluateEffectorGridWeight(
            Spec, OriginCenter, Anchor.WorldPos, &Forces, Collider, ElapsedSeconds);

        float LatSize = CellW * MixF(MinFrac, 1.0f + MaxH * 0.45f, W);
        float ExtrudeH = 0.0f;

        switch (Spec.PrimitiveShape)
        {
            case EffectorPrimitiveShape::HeightExtrudedCube:
                LatSize  = CellW;
                ExtrudeH = MixF(CellW * MinFrac, MaxH, W);
                break;

            case EffectorPrimitiveShape::UniformBox:
                ExtrudeH = LatSize;
                break;

            case EffectorPrimitiveShape::HexagonPlate:
                // Kinetic Hex plates: beveled 3D hexagonal prisms rising crisply on ripple crests!
                LatSize  = CellW * MixF(std::max(0.26f, MinFrac * 1.40f), 1.06f, W);
                ExtrudeH = CellW * 0.24f + W * MaxH * 0.56f;
                break;

            case EffectorPrimitiveShape::AutoHalftoneUpperTiles:
            {
                // Lower flanks & fenders = crisp halftone circles; upper hood / cabin / roof = raised bevel tiles!
                const bool IsUpperTile = (Spec.HostSurface == EffectorHostSurfaceCategory::ControlVehicle)
                    ? (Anchor.LocalElevY > 0.04f && Anchor.UnitNormal[1] > 0.36f)
                    : (Anchor.LocalElevY > 0.28f);
                if (IsUpperTile)
                {
                    LatSize  = CellW * MixF(std::max(0.26f, MinFrac * 1.55f), 1.08f, W);
                    ExtrudeH = MixF(CellW * 0.24f, MaxH * 0.52f + CellW * 0.38f, W);
                }
                else
                {
                    LatSize  = CellW * MixF(std::max(0.18f, MinFrac), 1.24f, W);
                    ExtrudeH = 0.0f;
                }
                break;
            }

            case EffectorPrimitiveShape::HalftoneCircle:
            case EffectorPrimitiveShape::FlatSquareTile:
            case EffectorPrimitiveShape::TrianglePlate:
            default:
                ExtrudeH = 0.0f;
                break;
        }

        // Exact 3-stop linear RGB interpolation: ColA (idle obsidian) -> ColC (mid wave) -> ColB (peak flare)
        float R = 0.0f, G = 0.0f, B = 0.0f;
        if (W < 0.5f)
        {
            const float T = W * 2.0f;
            R = MixF(Spec.IdleColorLinear[0], Spec.MidColorLinear[0], T);
            G = MixF(Spec.IdleColorLinear[1], Spec.MidColorLinear[1], T);
            B = MixF(Spec.IdleColorLinear[2], Spec.MidColorLinear[2], T);
        }
        else
        {
            const float T = (W - 0.5f) * 2.0f;
            R = MixF(Spec.MidColorLinear[0], Spec.PeakColorLinear[0], T);
            G = MixF(Spec.MidColorLinear[1], Spec.PeakColorLinear[1], T);
            B = MixF(Spec.MidColorLinear[2], Spec.PeakColorLinear[2], T);
        }
        const float Alpha = MixF(0.62f, 1.0f, SmoothstepF(0.0f, 0.22f, W));

        ParticleSlot& Slot = RigSlots[I];
        Slot.PositionMeters[0] = Anchor.WorldPos[0];
        Slot.PositionMeters[1] = Anchor.WorldPos[1];
        Slot.PositionMeters[2] = Anchor.WorldPos[2];
        Slot.AgeSeconds        = ElapsedSeconds;
        Slot.VelocityMps[0]    = Anchor.UnitNormal[0];
        Slot.VelocityMps[1]    = Anchor.UnitNormal[1];
        Slot.VelocityMps[2]    = Anchor.UnitNormal[2];
        Slot.LifespanSeconds   = 1.0e9f;
        Slot.SizeMeters        = LatSize;
        Slot.SeedScalar        = Anchor.LocalElevY;
        Slot.SpinRadians       = ExtrudeH;
        Slot.AuxScalar         = W;
        Slot.OriginAnchor[0]   = Anchor.WorldPos[0];
        Slot.OriginAnchor[1]   = Anchor.WorldPos[1];
        Slot.OriginAnchor[2]   = Anchor.WorldPos[2];
        Slot.PackedTraits      = 1u | (PrimCode << 8u) | ((Anchor.MaterialSlot & 0xFu) << 12u);
        Slot.ColourLinear[0]   = R;
        Slot.ColourLinear[1]   = G;
        Slot.ColourLinear[2]   = B;
        Slot.ColourLinear[3]   = Alpha;
    }
}

} // namespace Frontier::ParticleSpace
