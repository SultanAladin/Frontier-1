/*============================================================================================================================================
                                                           COORDINATESPACE.H
============================================================================================================================================*/
// 🧩 Configurable Right-Handed World Basis (RightHandedZUp default / RightHandedYUp), det=+1 boundary conversion & isolated Vulkan NDC clip

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WorldCoordinateBasis : uint8_t
{
    RightHandedZUp = 0u, // [-] - Engine Default: +X Right, +Y Forward, +Z Up  (Right x Forward = +Z, det = +1)
    RightHandedYUp = 1u, // [-] - Optional:       +X Right, +Y Up,      -Z Fwd (Right x Up      = +Z, det = +1)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ClipTransform4x4
{
    float Rows[4][4]{}; // [-] - Row-major 4x4 projective transform matching .slang -matrix-layout-row-major
};

//------------------------------------------------------------------------------------------------------------------------
//                                            ACTIVE ENGINE WORLD BASIS REGISTRY
//------------------------------------------------------------------------------------------------------------------------

namespace CoordinateBasisDetail
{

inline std::atomic<uint8_t> ActiveBasisStorage{ static_cast<uint8_t>(WorldCoordinateBasis::RightHandedZUp) };

} // namespace CoordinateBasisDetail

inline void AssignActiveWorldBasis(WorldCoordinateBasis Basis) noexcept
{
    CoordinateBasisDetail::ActiveBasisStorage.store(static_cast<uint8_t>(Basis), std::memory_order_release);
}

[[nodiscard]] inline WorldCoordinateBasis QueryActiveWorldBasis() noexcept
{
    const uint8_t Raw = CoordinateBasisDetail::ActiveBasisStorage.load(std::memory_order_acquire);
    return (Raw == static_cast<uint8_t>(WorldCoordinateBasis::RightHandedYUp))
               ? WorldCoordinateBasis::RightHandedYUp
               : WorldCoordinateBasis::RightHandedZUp;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     DET = +1 ORTHONORMAL RIGHT-HANDED AXIAL QUERIES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr Axis3D QueryWorldRightAxis(WorldCoordinateBasis /*Basis*/) noexcept
{
    return { 1.0f, 0.0f, 0.0f };
}

[[nodiscard]] constexpr Axis3D QueryWorldUpAxis(WorldCoordinateBasis Basis) noexcept
{
    return (Basis == WorldCoordinateBasis::RightHandedYUp)
               ? Axis3D{ 0.0f, 1.0f, 0.0f }
               : Axis3D{ 0.0f, 0.0f, 1.0f };
}

[[nodiscard]] constexpr Axis3D QueryWorldForwardAxis(WorldCoordinateBasis Basis) noexcept
{
    return (Basis == WorldCoordinateBasis::RightHandedYUp)
               ? Axis3D{ 0.0f, 0.0f, -1.0f }
               : Axis3D{ 0.0f, 1.0f, 0.0f };
}

// Orthonormal rotation (det = +1, zero reflection, preserves CCW triangle winding):
// RightHandedYUp (x, y, z) -> RightHandedZUp (x, -z, y)  [R_x(+90 deg)]
// RightHandedZUp (X, Y, Z) -> RightHandedYUp (X, Z, -Y)  [R_x(-90 deg)]
[[nodiscard]] constexpr Axis3D ConvertVectorBetweenBases(
    Axis3D               Vector,
    WorldCoordinateBasis SourceBasis,
    WorldCoordinateBasis TargetBasis) noexcept
{
    if (SourceBasis == TargetBasis)
    {
        return Vector;
    }
    if (SourceBasis == WorldCoordinateBasis::RightHandedYUp &&
        TargetBasis == WorldCoordinateBasis::RightHandedZUp)
    {
        return { Vector.X, -Vector.Z, Vector.Y };
    }
    return { Vector.X, Vector.Z, -Vector.Y };
}

// Evaluates camera eye offset from target pivot for yaw/pitch angles in the requested right-handed world basis.
// Under ConvertVectorBetweenBases(RightHandedYUp -> RightHandedZUp), both bases produce bit-identical camera-space coordinates.
[[nodiscard]] inline Axis3D EvaluateOrbitOffsetFromAngles(
    float                DistanceMeters,
    float                YawRadians,
    float                PitchRadians,
    WorldCoordinateBasis Basis) noexcept
{
    const float CosP = std::cos(PitchRadians);
    const float SinP = std::sin(PitchRadians);
    const float CosY = std::cos(YawRadians);
    const float SinY = std::sin(YawRadians);

    if (Basis == WorldCoordinateBasis::RightHandedYUp)
    {
        return {
            DistanceMeters * CosP * SinY,
            DistanceMeters * SinP,
            DistanceMeters * CosP * CosY,
        };
    }
    return {
        DistanceMeters * CosP * SinY,
        -DistanceMeters * CosP * CosY,
        DistanceMeters * SinP,
    };
}

inline void EvaluateOrthonormalCameraBasis(
    Axis3D               EyePosition,
    Axis3D               TargetPivot,
    WorldCoordinateBasis Basis,
    Axis3D&              OutRight,
    Axis3D&              OutUp,
    Axis3D&              OutForward) noexcept
{
    const Axis3D FallbackForward = QueryWorldForwardAxis(Basis);
    OutForward = NormalizeAxis(TargetPivot - EyePosition, FallbackForward);

    const Axis3D PrimaryUp   = QueryWorldUpAxis(Basis);
    const Axis3D SecondaryUp = (Basis == WorldCoordinateBasis::RightHandedYUp)
                                   ? Axis3D{ 0.0f, 0.0f, -1.0f }
                                   : Axis3D{ 0.0f, 1.0f, 0.0f };
    const Axis3D EffectiveUp = (std::abs(EvaluateDot(OutForward, PrimaryUp)) < 0.985f) ? PrimaryUp : SecondaryUp;

    OutRight = NormalizeAxis(EvaluateCross(OutForward, EffectiveUp), { 1.0f, 0.0f, 0.0f });
    OutUp    = NormalizeAxis(EvaluateCross(OutRight, OutForward), EffectiveUp);
}

//------------------------------------------------------------------------------------------------------------------------
//                                   ISOLATED VULKAN NDC CLIP PROJECTION (Y-DOWN, Z IN [0, 1])
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline ClipTransform4x4 ConstructVulkanPerspectiveViewClip(
    Axis3D Eye,
    Axis3D Right,
    Axis3D Up,
    Axis3D Forward,
    float  VerticalFovDeg,
    float  AspectRatio,
    float  NearPlane,
    float  FarPlane) noexcept
{
    const float TanHalfFov = std::tan(std::clamp(VerticalFovDeg, 5.0f, 165.0f) * Constants::DegToRadFloat * 0.5f);
    const float SafeAspect = std::max(AspectRatio, 1.0e-4f);
    const float ScaleX     = 1.0f / std::max(SafeAspect * TanHalfFov, 1.0e-5f);
    const float ScaleY     = -1.0f / std::max(TanHalfFov, 1.0e-5f); // Isolated Vulkan top-down clip inversion
    const float SafeNear   = std::max(0.005f, NearPlane);
    const float SafeFar    = std::max(SafeNear + 1.0f, FarPlane);
    const float ZRange     = SafeFar / (SafeNear - SafeFar);

    const float View[4][4] = {
        { Right.X,    Right.Y,    Right.Z,    -EvaluateDot(Right, Eye)   },
        { Up.X,       Up.Y,       Up.Z,       -EvaluateDot(Up, Eye)      },
        { -Forward.X, -Forward.Y, -Forward.Z,  EvaluateDot(Forward, Eye) },
        { 0.0f,       0.0f,       0.0f,        1.0f                      },
    };
    const float Proj[4][4] = {
        { ScaleX, 0.0f,   0.0f,   0.0f              },
        { 0.0f,   ScaleY, 0.0f,   0.0f              },
        { 0.0f,   0.0f,   ZRange, SafeNear * ZRange },
        { 0.0f,   0.0f,  -1.0f,   0.0f              },
    };

    ClipTransform4x4 Result{};
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            Result.Rows[Row][Col] =
                Proj[Row][0] * View[0][Col] +
                Proj[Row][1] * View[1][Col] +
                Proj[Row][2] * View[2][Col] +
                Proj[Row][3] * View[3][Col];
        }
    }
    return Result;
}

[[nodiscard]] inline ClipTransform4x4 ConstructVulkanOrthographicViewClip(
    Axis3D Eye,
    Axis3D Right,
    Axis3D Up,
    Axis3D Forward,
    float  VerticalSpanMeters,
    float  AspectRatio,
    float  NearPlane,
    float  FarPlane) noexcept
{
    const float HalfHeight = std::max(0.05f, 0.5f * VerticalSpanMeters);
    const float HalfWidth  = std::max(0.05f, HalfHeight * std::max(AspectRatio, 1.0e-4f));
    const float ScaleX     = 1.0f / HalfWidth;
    const float ScaleY     = -1.0f / HalfHeight; // Isolated Vulkan top-down clip inversion
    const float SafeNear   = NearPlane;
    const float SafeFar    = std::max(SafeNear + 1.0f, FarPlane);
    const float InvDepth   = 1.0f / (SafeNear - SafeFar);

    const float View[4][4] = {
        { Right.X,    Right.Y,    Right.Z,    -EvaluateDot(Right, Eye)   },
        { Up.X,       Up.Y,       Up.Z,       -EvaluateDot(Up, Eye)      },
        { -Forward.X, -Forward.Y, -Forward.Z,  EvaluateDot(Forward, Eye) },
        { 0.0f,       0.0f,       0.0f,        1.0f                      },
    };
    const float Ortho[4][4] = {
        { ScaleX, 0.0f,   0.0f,      0.0f                },
        { 0.0f,   ScaleY, 0.0f,      0.0f                },
        { 0.0f,   0.0f,   InvDepth,  SafeNear * InvDepth },
        { 0.0f,   0.0f,   0.0f,      1.0f                },
    };

    ClipTransform4x4 Result{};
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            Result.Rows[Row][Col] =
                Ortho[Row][0] * View[0][Col] +
                Ortho[Row][1] * View[1][Col] +
                Ortho[Row][2] * View[2][Col] +
                Ortho[Row][3] * View[3][Col];
        }
    }
    return Result;
}

} // namespace Frontier::Mathematics
