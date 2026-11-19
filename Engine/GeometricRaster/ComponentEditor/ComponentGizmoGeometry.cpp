/*============================================================================================================================================
                                                       COMPONENTGIZMOGEOMETRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of procedural & compiled geometry vertex builders for Component Editor .slang pipelines

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/ComponentEditor/ComponentGizmoGeometry.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Interchange/IndexedTriangleStreamInterchange.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster::ComponentEditor
{

namespace
{

constexpr float PiF  = static_cast<float>(Constants::Pi);
constexpr float TauF = static_cast<float>(Constants::Tau);

[[nodiscard]] std::array<float, 3> SrgbHexToLinear(uint32_t RgbHex) noexcept
{
    auto Chan = [](uint32_t Byte) -> float
    {
        const float S = static_cast<float>(Byte) / 255.0f;
        return (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.4f);
    };
    return {
        Chan((RgbHex >> 16u) & 0xFFu),
        Chan((RgbHex >> 8u)  & 0xFFu),
        Chan(RgbHex & 0xFFu)
    };
}

[[nodiscard]] std::array<float, 3> HslToSrgb01(float H, float S, float L) noexcept
{
    auto Hue2 = [](float P, float Q, float T) -> float
    {
        if (T < 0.0f) T += 1.0f;
        if (T > 1.0f) T -= 1.0f;
        if (T < 1.0f / 6.0f) return P + (Q - P) * 6.0f * T;
        if (T < 0.5f)        return Q;
        if (T < 2.0f / 3.0f) return P + (Q - P) * (2.0f / 3.0f - T) * 6.0f;
        return P;
    };
    const float Q = (L < 0.5f) ? (L * (1.0f + S)) : (L + S - L * S);
    const float P = 2.0f * L - Q;
    return { Hue2(P, Q, H + 1.0f / 3.0f), Hue2(P, Q, H), Hue2(P, Q, H - 1.0f / 3.0f) };
}

[[nodiscard]] Mathematics::Axis3D Cross3(const Mathematics::Axis3D& A, const Mathematics::Axis3D& B) noexcept
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}

[[nodiscard]] float Dot3(const Mathematics::Axis3D& A, const Mathematics::Axis3D& B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] Mathematics::Axis3D Norm3(const Mathematics::Axis3D& V, const Mathematics::Axis3D& Fallback) noexcept
{
    const float Len = std::sqrt(Dot3(V, V));
    if (Len < 1.0e-6f)
    {
        return Fallback;
    }
    const float Inv = 1.0f / Len;
    return { V.X * Inv, V.Y * Inv, V.Z * Inv };
}

void AxisBasisVectors(
    ComponentClass::ConstraintAxis Axis,
    Mathematics::Axis3D&           OutAxis,
    Mathematics::Axis3D&           OutU,
    Mathematics::Axis3D&           OutV) noexcept
{
    switch (Axis)
    {
        case ComponentClass::ConstraintAxis::AxisX:
            OutAxis = { 1.0f, 0.0f, 0.0f };
            OutU    = { 0.0f, 1.0f, 0.0f };
            OutV    = { 0.0f, 0.0f, 1.0f };
            break;
        case ComponentClass::ConstraintAxis::AxisY:
            OutAxis = { 0.0f, 1.0f, 0.0f };
            OutU    = { 1.0f, 0.0f, 0.0f };
            OutV    = { 0.0f, 0.0f, 1.0f };
            break;
        case ComponentClass::ConstraintAxis::AxisZ:
        default:
            OutAxis = { 0.0f, 0.0f, 1.0f };
            OutU    = { 1.0f, 0.0f, 0.0f };
            OutV    = { 0.0f, 1.0f, 0.0f };
            break;
    }
}

[[nodiscard]] Mathematics::Axis3D RotateAroundAxis(
    const Mathematics::Axis3D& Point,
    const Mathematics::Axis3D& UnitAxis,
    float                      AngleRadians) noexcept
{
    const float C  = std::cos(AngleRadians);
    const float S  = std::sin(AngleRadians);
    const float K  = Dot3(UnitAxis, Point) * (1.0f - C);
    const auto  Cr = Cross3(UnitAxis, Point);
    return {
        Point.X * C + Cr.X * S + UnitAxis.X * K,
        Point.Y * C + Cr.Y * S + UnitAxis.Y * K,
        Point.Z * C + Cr.Z * S + UnitAxis.Z * K
    };
}

void AppendTri(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          A,
    const Mathematics::Axis3D&          B,
    const Mathematics::Axis3D&          C,
    const Mathematics::Axis3D&          NA,
    const Mathematics::Axis3D&          NB,
    const Mathematics::Axis3D&          NC,
    std::array<float, 2>                UvA = { 0.0f, 0.0f },
    std::array<float, 2>                UvB = { 0.0f, 0.0f },
    std::array<float, 2>                UvC = { 0.0f, 0.0f },
    std::array<float, 3>                Col = { 1.0f, 1.0f, 1.0f })
{
    Out.push_back({ { A.X, A.Y, A.Z }, { NA.X, NA.Y, NA.Z }, { UvA[0], UvA[1] }, { Col[0], Col[1], Col[2] } });
    Out.push_back({ { B.X, B.Y, B.Z }, { NB.X, NB.Y, NB.Z }, { UvB[0], UvB[1] }, { Col[0], Col[1], Col[2] } });
    Out.push_back({ { C.X, C.Y, C.Z }, { NC.X, NC.Y, NC.Z }, { UvC[0], UvC[1] }, { Col[0], Col[1], Col[2] } });
}

void AppendFlatTri(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          A,
    const Mathematics::Axis3D&          B,
    const Mathematics::Axis3D&          C,
    std::array<float, 2>                UvA = { 0.0f, 0.0f },
    std::array<float, 2>                UvB = { 0.0f, 0.0f },
    std::array<float, 2>                UvC = { 0.0f, 0.0f })
{
    const Mathematics::Axis3D U { B.X - A.X, B.Y - A.Y, B.Z - A.Z };
    const Mathematics::Axis3D V { C.X - A.X, C.Y - A.Y, C.Z - A.Z };
    const Mathematics::Axis3D N = Norm3(Cross3(U, V), { 0.0f, 1.0f, 0.0f });
    AppendTri(Out, A, B, C, N, N, N, UvA, UvB, UvC);
}

void AppendOrientedCylinder(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          Center,
    const Mathematics::Axis3D&          AxisUnit,
    const Mathematics::Axis3D&          U,
    const Mathematics::Axis3D&          V,
    float                               RadiusBottom,
    float                               RadiusTop,
    float                               Length,
    uint32_t                            Sides = 20u)
{
    const float HalfL = Length * 0.5f;
    for (uint32_t I = 0u; I < Sides; ++I)
    {
        const float P0 = (static_cast<float>(I) / static_cast<float>(Sides)) * TauF;
        const float P1 = (static_cast<float>(I + 1u) / static_cast<float>(Sides)) * TauF;
        const Mathematics::Axis3D R0 { U.X * std::cos(P0) + V.X * std::sin(P0), U.Y * std::cos(P0) + V.Y * std::sin(P0), U.Z * std::cos(P0) + V.Z * std::sin(P0) };
        const Mathematics::Axis3D R1 { U.X * std::cos(P1) + V.X * std::sin(P1), U.Y * std::cos(P1) + V.Y * std::sin(P1), U.Z * std::cos(P1) + V.Z * std::sin(P1) };

        auto Pt = [&](const Mathematics::Axis3D& Dir, float Rad, float Along) -> Mathematics::Axis3D
        {
            return {
                Center.X + AxisUnit.X * Along + Dir.X * Rad,
                Center.Y + AxisUnit.Y * Along + Dir.Y * Rad,
                Center.Z + AxisUnit.Z * Along + Dir.Z * Rad
            };
        };

        const auto B0 = Pt(R0, RadiusBottom, -HalfL);
        const auto B1 = Pt(R1, RadiusBottom, -HalfL);
        const auto T0 = Pt(R0, RadiusTop,     HalfL);
        const auto T1 = Pt(R1, RadiusTop,     HalfL);
        AppendTri(Out, B0, B1, T1, R0, R1, R1);
        AppendTri(Out, B0, T1, T0, R0, R1, R0);

        const auto CapBot = Pt({ 0.0f, 0.0f, 0.0f }, 0.0f, -HalfL);
        const auto CapTop = Pt({ 0.0f, 0.0f, 0.0f }, 0.0f,  HalfL);
        const Mathematics::Axis3D NegAxis { -AxisUnit.X, -AxisUnit.Y, -AxisUnit.Z };
        if (RadiusBottom > 1.0e-5f)
        {
            AppendTri(Out, CapBot, B1, B0, NegAxis, NegAxis, NegAxis);
        }
        if (RadiusTop > 1.0e-5f)
        {
            AppendTri(Out, CapTop, T0, T1, AxisUnit, AxisUnit, AxisUnit);
        }
    }
}

void AppendOrientedBox(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          Center,
    const Mathematics::Axis3D&          AxisX,
    const Mathematics::Axis3D&          AxisY,
    const Mathematics::Axis3D&          AxisZ,
    float                               HalfX,
    float                               HalfY,
    float                               HalfZ)
{
    auto Corner = [&](float SX, float SY, float SZ) -> Mathematics::Axis3D
    {
        return {
            Center.X + AxisX.X * (SX * HalfX) + AxisY.X * (SY * HalfY) + AxisZ.X * (SZ * HalfZ),
            Center.Y + AxisX.Y * (SX * HalfX) + AxisY.Y * (SY * HalfY) + AxisZ.Y * (SZ * HalfZ),
            Center.Z + AxisX.Z * (SX * HalfX) + AxisY.Z * (SY * HalfY) + AxisZ.Z * (SZ * HalfZ)
        };
    };
    auto Quad = [&](const Mathematics::Axis3D& A, const Mathematics::Axis3D& B,
                    const Mathematics::Axis3D& C, const Mathematics::Axis3D& D,
                    const Mathematics::Axis3D& N)
    {
        AppendTri(Out, A, B, C, N, N, N, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f });
        AppendTri(Out, A, C, D, N, N, N, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
    };
    const Mathematics::Axis3D NX { -AxisX.X, -AxisX.Y, -AxisX.Z };
    const Mathematics::Axis3D NY { -AxisY.X, -AxisY.Y, -AxisY.Z };
    const Mathematics::Axis3D NZ { -AxisZ.X, -AxisZ.Y, -AxisZ.Z };
    Quad(Corner( 1, -1, -1), Corner( 1,  1, -1), Corner( 1,  1,  1), Corner( 1, -1,  1), AxisX);
    Quad(Corner(-1, -1,  1), Corner(-1,  1,  1), Corner(-1,  1, -1), Corner(-1, -1, -1), NX);
    Quad(Corner(-1,  1, -1), Corner(-1,  1,  1), Corner( 1,  1,  1), Corner( 1,  1, -1), AxisY);
    Quad(Corner(-1, -1,  1), Corner(-1, -1, -1), Corner( 1, -1, -1), Corner( 1, -1,  1), NY);
    Quad(Corner(-1, -1,  1), Corner( 1, -1,  1), Corner( 1,  1,  1), Corner(-1,  1,  1), AxisZ);
    Quad(Corner( 1, -1, -1), Corner(-1, -1, -1), Corner(-1,  1, -1), Corner( 1,  1, -1), NZ);
}

void AppendSphere(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          Center,
    float                               Radius,
    uint32_t                            Stacks = 10u,
    uint32_t                            Slices = 14u)
{
    for (uint32_t I = 0u; I < Stacks; ++I)
    {
        const float T0 = PiF * static_cast<float>(I) / static_cast<float>(Stacks);
        const float T1 = PiF * static_cast<float>(I + 1u) / static_cast<float>(Stacks);
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const float P0 = TauF * static_cast<float>(J) / static_cast<float>(Slices);
            const float P1 = TauF * static_cast<float>(J + 1u) / static_cast<float>(Slices);
            auto Dir = [](float Theta, float Phi) -> Mathematics::Axis3D
            {
                return { std::sin(Theta) * std::cos(Phi), std::cos(Theta), std::sin(Theta) * std::sin(Phi) };
            };
            const auto N00 = Dir(T0, P0), N10 = Dir(T1, P0), N11 = Dir(T1, P1), N01 = Dir(T0, P1);
            auto Pt = [&](const Mathematics::Axis3D& N) -> Mathematics::Axis3D
            {
                return { Center.X + N.X * Radius, Center.Y + N.Y * Radius, Center.Z + N.Z * Radius };
            };
            AppendTri(Out, Pt(N00), Pt(N10), Pt(N11), N00, N10, N11);
            AppendTri(Out, Pt(N00), Pt(N11), Pt(N01), N00, N11, N01);
        }
    }
}

void AppendOrientedTorus(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          Center,
    const Mathematics::Axis3D&          NormalAxis,
    const Mathematics::Axis3D&          U,
    const Mathematics::Axis3D&          V,
    float                               MajorR,
    float                               TubeR,
    uint32_t                            MajSegs = 28u,
    uint32_t                            TubSegs = 8u)
{
    for (uint32_t I = 0u; I < MajSegs; ++I)
    {
        const float A0 = TauF * static_cast<float>(I) / static_cast<float>(MajSegs);
        const float A1 = TauF * static_cast<float>(I + 1u) / static_cast<float>(MajSegs);
        for (uint32_t J = 0u; J < TubSegs; ++J)
        {
            const float B0 = TauF * static_cast<float>(J) / static_cast<float>(TubSegs);
            const float B1 = TauF * static_cast<float>(J + 1u) / static_cast<float>(TubSegs);
            auto Sample = [&](float A, float B, Mathematics::Axis3D& OutN) -> Mathematics::Axis3D
            {
                const float CA = std::cos(A), SA = std::sin(A);
                const float CB = std::cos(B), SB = std::sin(B);
                const Mathematics::Axis3D Radial { U.X * CA + V.X * SA, U.Y * CA + V.Y * SA, U.Z * CA + V.Z * SA };
                OutN = Norm3({ Radial.X * CB + NormalAxis.X * SB, Radial.Y * CB + NormalAxis.Y * SB, Radial.Z * CB + NormalAxis.Z * SB }, NormalAxis);
                return {
                    Center.X + Radial.X * MajorR + OutN.X * TubeR,
                    Center.Y + Radial.Y * MajorR + OutN.Y * TubeR,
                    Center.Z + Radial.Z * MajorR + OutN.Z * TubeR
                };
            };
            Mathematics::Axis3D N00{}, N10{}, N11{}, N01{};
            const auto P00 = Sample(A0, B0, N00);
            const auto P10 = Sample(A1, B0, N10);
            const auto P11 = Sample(A1, B1, N11);
            const auto P01 = Sample(A0, B1, N01);
            AppendTri(Out, P00, P10, P11, N00, N10, N11);
            AppendTri(Out, P00, P11, P01, N00, N11, N01);
        }
    }
}

void AppendTubeStrip(
    std::vector<ComponentShaderVertex>& Out,
    const std::vector<Mathematics::Axis3D>& Path,
    float                                   TubeRadius,
    uint32_t                                Sides = 8u)
{
    if (Path.size() < 2u)
    {
        return;
    }
    std::vector<Mathematics::Axis3D> PrevRing {};
    for (std::size_t I = 0u; I < Path.size(); ++I)
    {
        const auto& P = Path[I];
        const auto& A = Path[I > 0u ? I - 1u : 0u];
        const auto& B = Path[I + 1u < Path.size() ? I + 1u : Path.size() - 1u];
        const Mathematics::Axis3D Tan = Norm3({ B.X - A.X, B.Y - A.Y, B.Z - A.Z }, { 0.0f, 1.0f, 0.0f });
        const Mathematics::Axis3D Ref = (std::abs(Tan.Y) < 0.9f) ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D U   = Norm3(Cross3(Ref, Tan), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D V   = Norm3(Cross3(Tan, U), { 0.0f, 0.0f, 1.0f });

        std::vector<Mathematics::Axis3D> Ring(Sides);
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const float Phi = TauF * static_cast<float>(S) / static_cast<float>(Sides);
            const float C = std::cos(Phi), Sn = std::sin(Phi);
            Ring[S] = {
                P.X + (U.X * C + V.X * Sn) * TubeRadius,
                P.Y + (U.Y * C + V.Y * Sn) * TubeRadius,
                P.Z + (U.Z * C + V.Z * Sn) * TubeRadius
            };
        }
        if (I > 0u)
        {
            for (uint32_t S = 0u; S < Sides; ++S)
            {
                const uint32_t Next = (S + 1u) % Sides;
                AppendFlatTri(Out, PrevRing[S], PrevRing[Next], Ring[Next]);
                AppendFlatTri(Out, PrevRing[S], Ring[Next], Ring[S]);
            }
        }
        PrevRing = std::move(Ring);
    }
}

void AppendAnnularSector(
    std::vector<ComponentShaderVertex>& Out,
    const Mathematics::Axis3D&          U,
    const Mathematics::Axis3D&          V,
    float                               InnerR,
    float                               OuterR,
    float                               StartRad,
    float                               SpanRad,
    uint32_t                            Segments = 48u)
{
    const uint32_t N = std::max<uint32_t>(4u, static_cast<uint32_t>(std::ceil(static_cast<float>(Segments) * std::abs(SpanRad) / TauF)));
    const Mathematics::Axis3D Normal = Norm3(Cross3(U, V), { 0.0f, 1.0f, 0.0f });
    for (uint32_t I = 0u; I < N; ++I)
    {
        const float T0 = static_cast<float>(I) / static_cast<float>(N);
        const float T1 = static_cast<float>(I + 1u) / static_cast<float>(N);
        const float P0 = StartRad + SpanRad * T0;
        const float P1 = StartRad + SpanRad * T1;
        auto Pt = [&](float Phi, float Rad) -> Mathematics::Axis3D
        {
            const float C = std::cos(Phi), S = std::sin(Phi);
            return { Rad * (U.X * C + V.X * S), Rad * (U.Y * C + V.Y * S), Rad * (U.Z * C + V.Z * S) };
        };
        const auto A0 = Pt(P0, InnerR), B0 = Pt(P0, OuterR);
        const auto A1 = Pt(P1, InnerR), B1 = Pt(P1, OuterR);
        AppendTri(Out, A0, B0, B1, Normal, Normal, Normal, { T0, 0.0f }, { T0, 1.0f }, { T1, 1.0f });
        AppendTri(Out, A0, B1, A1, Normal, Normal, Normal, { T0, 0.0f }, { T1, 1.0f }, { T1, 0.0f });
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC BUILDERS
//------------------------------------------------------------------------------------------------------------------------

CompiledGeometryVisualBatch ComponentGizmoGeometry::BuildCompiledAssetBatch(
    const ComponentClass::CompiledGeometryRepositoryEntry& Entry)
{
    CompiledGeometryVisualBatch Batch {};
    const auto& Net = Entry.Asset.Network;
    if (Net.Faces.empty() || Net.Vertices.empty())
    {
        return Batch;
    }

    Batch.BoundsMin = {  1.0e9f,  1.0e9f,  1.0e9f };
    Batch.BoundsMax = { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : Net.Vertices)
    {
        Batch.BoundsMin.X = std::min(Batch.BoundsMin.X, V.Axes.X);
        Batch.BoundsMin.Y = std::min(Batch.BoundsMin.Y, V.Axes.Y);
        Batch.BoundsMin.Z = std::min(Batch.BoundsMin.Z, V.Axes.Z);
        Batch.BoundsMax.X = std::max(Batch.BoundsMax.X, V.Axes.X);
        Batch.BoundsMax.Y = std::max(Batch.BoundsMax.Y, V.Axes.Y);
        Batch.BoundsMax.Z = std::max(Batch.BoundsMax.Z, V.Axes.Z);
    }

    struct RawTri
    {
        uint32_t                           SlotIdentifier;
        GeometrySpace::FaceShadingAssignment Shading;
        std::array<uint32_t, 3>            Verts;
        std::array<uint32_t, 3>            Edges;
        Mathematics::Axis3D                FaceNormal;
        float                              Area;
    };

    std::vector<RawTri> Tris {};
    Tris.reserve(Net.Faces.size());
    std::vector<float> Areas {};
    Areas.reserve(Net.Faces.size());

    bool AnyNonZeroUv = false;
    for (const auto& Face : Net.Faces)
    {
        if (Face.CornerCount < 3u || Face.BoundaryLoopEdgeIndex >= Net.Edges.size())
        {
            continue;
        }
        std::vector<uint32_t> LoopE {};
        uint32_t Cur = Face.BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Face.CornerCount && Cur < Net.Edges.size(); ++C)
        {
            LoopE.push_back(Cur);
            const auto& Edge = Net.Edges[Cur];
            if (std::abs(Edge.ParameterUV.U) > 1.0e-4f || std::abs(Edge.ParameterUV.V) > 1.0e-4f)
            {
                AnyNonZeroUv = true;
            }
            Cur = Edge.NextEdgeIndex;
        }
        if (LoopE.size() < 3u)
        {
            continue;
        }
        for (std::size_t K = 1u; K + 1u < LoopE.size(); ++K)
        {
            const uint32_t E0 = LoopE[0], E1 = LoopE[K], E2 = LoopE[K + 1u];
            const uint32_t V0 = Net.Edges[E0].OriginVertexIndex;
            const uint32_t V1 = Net.Edges[E1].OriginVertexIndex;
            const uint32_t V2 = Net.Edges[E2].OriginVertexIndex;
            if (V0 >= Net.Vertices.size() || V1 >= Net.Vertices.size() || V2 >= Net.Vertices.size())
            {
                continue;
            }
            const auto& P0 = Net.Vertices[V0].Axes;
            const auto& P1 = Net.Vertices[V1].Axes;
            const auto& P2 = Net.Vertices[V2].Axes;
            const Mathematics::Axis3D U { P1.X - P0.X, P1.Y - P0.Y, P1.Z - P0.Z };
            const Mathematics::Axis3D V { P2.X - P0.X, P2.Y - P0.Y, P2.Z - P0.Z };
            const Mathematics::Axis3D Cr = Cross3(U, V);
            const float Area = 0.5f * std::sqrt(Dot3(Cr, Cr)) + 1.0e-12f;
            const uint32_t SlotId = (Face.PartGroupOrdinal != 0u) ? Face.PartGroupOrdinal : Face.PriorityLayerOrdinal;
            Tris.push_back({ SlotId, Face.Shading, { V0, V1, V2 }, { E0, E1, E2 }, Norm3(Cr, { 0.0f, 1.0f, 0.0f }), Area });
            Areas.push_back(Area);
        }
    }

    if (Tris.empty())
    {
        return Batch;
    }

    Batch.ValidUvsPresent   = AnyNonZeroUv;
    Batch.TriangleCount = static_cast<uint32_t>(Tris.size());

    std::vector<float> SortedAreas = Areas;
    std::sort(SortedAreas.begin(), SortedAreas.end());
    const float MedianArea = std::max(SortedAreas[SortedAreas.size() / 2u], 1.0e-10f);

    static constexpr std::array<std::array<float, 3>, 5> DensityRamp {{
        { 0.188f, 0.420f, 0.949f }, // #306bf2 sparse blue
        { 0.161f, 0.780f, 0.761f }, // #29c7c2 cyan
        { 0.349f, 0.820f, 0.361f }, // #59d15c green
        { 0.969f, 0.800f, 0.251f }, // #f7cc40 yellow
        { 0.941f, 0.302f, 0.239f }, // #f04d3d dense red
    }};

    std::vector<std::size_t> Order(Tris.size());
    for (std::size_t I = 0u; I < Tris.size(); ++I)
    {
        Order[I] = I;
    }
    std::stable_sort(Order.begin(), Order.end(), [&](std::size_t A, std::size_t B)
    {
        return Tris[A].SlotIdentifier < Tris[B].SlotIdentifier;
    });

    Batch.FullDetailVertices.reserve(Tris.size() * 3u);
    Batch.DensityVertices.reserve(Tris.size() * 3u);
    Batch.LodVertices.reserve(Tris.size() * 3u);

    uint32_t ActiveSlot   = Tris[Order[0]].SlotIdentifier;
    auto     ActiveShade  = Tris[Order[0]].Shading;
    uint32_t SliceStart   = 0u;
    auto FlushSlice = [&](uint32_t SlotId, const GeometrySpace::FaceShadingAssignment& Shade, uint32_t StartVert, uint32_t EndVert)
    {
        GeometrySlotSlice Slice {};
        Slice.SlotIdentifier  = SlotId;
        Slice.FirstVertex     = StartVert;
        Slice.VertexCount     = EndVert - StartVert;
        Slice.AlbedoLinearRGB = Shade.AlbedoLinearRGB;
        Slice.Roughness       = Shade.Roughness;
        Slice.Metalness       = Shade.Metallic;
        if (const auto* CourseMat = Interchange::IndexedTriangleStreamInterchange::InspectCourseMaterial(SlotId))
        {
            Slice.AlbedoLinearRGB = CourseMat->AlbedoLinearRGB;
            Slice.EmissiveRGB     = CourseMat->EmissiveLinear;
            Slice.Roughness       = CourseMat->Roughness;
            Slice.Metalness       = CourseMat->Metallic;
        }
        Batch.SlotSlices.push_back(Slice);
    };

    for (std::size_t OrdIdx = 0u; OrdIdx < Order.size(); ++OrdIdx)
    {
        const RawTri& T = Tris[Order[OrdIdx]];
        if (T.SlotIdentifier != ActiveSlot)
        {
            FlushSlice(ActiveSlot, ActiveShade, SliceStart, static_cast<uint32_t>(Batch.FullDetailVertices.size()));
            ActiveSlot  = T.SlotIdentifier;
            ActiveShade = T.Shading;
            SliceStart  = static_cast<uint32_t>(Batch.FullDetailVertices.size());
        }

        const float X = std::clamp(0.5f - std::log2(T.Area / MedianArea) / 6.0f, 0.0f, 1.0f);
        const float Scaled = X * static_cast<float>(DensityRamp.size() - 1u);
        const std::size_t Lo = std::min<std::size_t>(DensityRamp.size() - 2u, static_cast<std::size_t>(Scaled));
        const float F = Scaled - static_cast<float>(Lo);
        const std::array<float, 3> DensCol {
            DensityRamp[Lo][0] + (DensityRamp[Lo + 1u][0] - DensityRamp[Lo][0]) * F,
            DensityRamp[Lo][1] + (DensityRamp[Lo + 1u][1] - DensityRamp[Lo][1]) * F,
            DensityRamp[Lo][2] + (DensityRamp[Lo + 1u][2] - DensityRamp[Lo][2]) * F
        };

        const uint32_t ClusterIdx = static_cast<uint32_t>(OrdIdx / 64u);
        const float LodHue = std::fmod(static_cast<float>(ClusterIdx) * 137.508f, 360.0f) / 360.0f;
        const std::array<float, 3> LodCol = HslToSrgb01(LodHue, 0.55f, 0.58f);

        for (int C = 0; C < 3; ++C)
        {
            const auto& Vert = Net.Vertices[T.Verts[C]];
            const auto& Edge = Net.Edges[T.Edges[C]];
            const Mathematics::Axis3D N = (Dot3(Vert.Normal, Vert.Normal) > 1.0e-5f)
                ? Norm3(Vert.Normal, T.FaceNormal)
                : T.FaceNormal;
            const std::array<float, 2> Uv { Edge.ParameterUV.U, Edge.ParameterUV.V };

            Batch.FullDetailVertices.push_back({
                { Vert.Axes.X, Vert.Axes.Y, Vert.Axes.Z },
                { N.X, N.Y, N.Z },
                { Uv[0], Uv[1] },
                { 1.0f, 1.0f, 1.0f }
            });
            Batch.DensityVertices.push_back({
                { Vert.Axes.X, Vert.Axes.Y, Vert.Axes.Z },
                { N.X, N.Y, N.Z },
                { Uv[0], Uv[1] },
                { DensCol[0], DensCol[1], DensCol[2] }
            });
            Batch.LodVertices.push_back({
                { Vert.Axes.X, Vert.Axes.Y, Vert.Axes.Z },
                { T.FaceNormal.X, T.FaceNormal.Y, T.FaceNormal.Z },
                { Uv[0], Uv[1] },
                { LodCol[0], LodCol[1], LodCol[2] }
            });
        }
    }
    FlushSlice(ActiveSlot, ActiveShade, SliceStart, static_cast<uint32_t>(Batch.FullDetailVertices.size()));
    Batch.LodTriangleCount = Batch.TriangleCount;
    return Batch;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::BuildDotFloorQuad(float HalfSpanMeters)
{
    std::vector<ComponentShaderVertex> Out {};
    const Mathematics::Axis3D A { -HalfSpanMeters, 0.0f, -HalfSpanMeters };
    const Mathematics::Axis3D B { -HalfSpanMeters, 0.0f,  HalfSpanMeters };
    const Mathematics::Axis3D C {  HalfSpanMeters, 0.0f,  HalfSpanMeters };
    const Mathematics::Axis3D D {  HalfSpanMeters, 0.0f, -HalfSpanMeters };
    const Mathematics::Axis3D N { 0.0f, 1.0f, 0.0f };
    AppendTri(Out, A, B, C, N, N, N, { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f });
    AppendTri(Out, A, C, D, N, N, N, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f });
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::ConstructUnitBoxHull()
{
    std::vector<ComponentShaderVertex> Out {};
    AppendOrientedBox(Out, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f, 1.0f, 1.0f);
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::ConstructUnitSphereHull(uint32_t Stacks, uint32_t Slices)
{
    std::vector<ComponentShaderVertex> Out {};
    AppendSphere(Out, { 0.0f, 0.0f, 0.0f }, 1.0f, Stacks, Slices);
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::ConstructCapsuleHull(float Radius, float HalfHeight, uint8_t AxisIndex)
{
    std::vector<ComponentShaderVertex> Out {};
    Mathematics::Axis3D Axis{}, U{}, V{};
    const auto Ax = (AxisIndex == 0u) ? ComponentClass::ConstraintAxis::AxisX
        : ((AxisIndex == 2u) ? ComponentClass::ConstraintAxis::AxisZ : ComponentClass::ConstraintAxis::AxisY);
    AxisBasisVectors(Ax, Axis, U, V);
    AppendOrientedCylinder(Out, { 0.0f, 0.0f, 0.0f }, Axis, U, V, Radius, Radius, HalfHeight * 2.0f, 20u);
    AppendSphere(Out, { Axis.X * HalfHeight, Axis.Y * HalfHeight, Axis.Z * HalfHeight }, Radius, 10u, 18u);
    AppendSphere(Out, { -Axis.X * HalfHeight, -Axis.Y * HalfHeight, -Axis.Z * HalfHeight }, Radius, 10u, 18u);
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::BuildSpotBeamCone(float LengthMeters, float RadiusMeters, uint32_t Sides)
{
    std::vector<ComponentShaderVertex> Out {};
    const Mathematics::Axis3D Peak { 0.0f, 0.0f, 0.0f };
    for (uint32_t I = 0u; I < Sides; ++I)
    {
        const float P0 = TauF * static_cast<float>(I) / static_cast<float>(Sides);
        const float P1 = TauF * static_cast<float>(I + 1u) / static_cast<float>(Sides);
        const Mathematics::Axis3D N0 { 0.0f, std::cos(P0), std::sin(P0) };
        const Mathematics::Axis3D N1 { 0.0f, std::cos(P1), std::sin(P1) };
        const Mathematics::Axis3D B0 { LengthMeters, N0.Y * RadiusMeters, N0.Z * RadiusMeters };
        const Mathematics::Axis3D B1 { LengthMeters, N1.Y * RadiusMeters, N1.Z * RadiusMeters };
        AppendTri(Out, Peak, B0, B1, N0, N0, N1);
    }
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::BuildCameraFrustumBeam(
    float LengthMeters,
    float VerticalFovDeg,
    float AspectRatio)
{
    std::vector<ComponentShaderVertex> Out {};
    const float HalfH = std::tan((VerticalFovDeg * PiF / 180.0f) * 0.5f) * LengthMeters;
    const float HalfW = HalfH * std::max(AspectRatio, 0.25f);
    const Mathematics::Axis3D Peak { 0.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D C0 { LengthMeters, -HalfH, -HalfW };
    const Mathematics::Axis3D C1 { LengthMeters,  HalfH, -HalfW };
    const Mathematics::Axis3D C2 { LengthMeters,  HalfH,  HalfW };
    const Mathematics::Axis3D C3 { LengthMeters, -HalfH,  HalfW };
    AppendFlatTri(Out, Peak, C0, C1);
    AppendFlatTri(Out, Peak, C1, C2);
    AppendFlatTri(Out, Peak, C2, C3);
    AppendFlatTri(Out, Peak, C3, C0);
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::BuildSolidArrow(float LengthMeters)
{
    std::vector<ComponentShaderVertex> Out {};
    const float Head = std::min(LengthMeters * 0.28f, 0.25f);
    const float Rad  = std::max(0.012f, LengthMeters * 0.025f);
    const float ShaftLen = std::max(LengthMeters - Head, 0.05f);
    AppendOrientedCylinder(Out, { ShaftLen * 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, Rad, Rad, ShaftLen, 14u);
    AppendOrientedCylinder(Out, { LengthMeters - Head * 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, Rad * 3.0f, 0.0f, Head, 18u);
    return Out;
}

std::vector<ComponentShaderVertex> ComponentGizmoGeometry::BuildOriginRing(float RadiusMeters)
{
    std::vector<ComponentShaderVertex> Out {};
    AppendOrientedTorus(Out, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, RadiusMeters, RadiusMeters * 0.09f, 36u, 8u);
    AppendSphere(Out, { 0.0f, 0.0f, 0.0f }, RadiusMeters * 0.22f, 8u, 12u);
    return Out;
}

std::array<float, 3> ComponentGizmoGeometry::QueryConstraintAccentLinearRgb(
    ComponentClass::PhysicsConstraintCategory Form) noexcept
{
    switch (Form)
    {
        case ComponentClass::PhysicsConstraintCategory::Fixed:  return SrgbHexToLinear(0x8a95a5u);
        case ComponentClass::PhysicsConstraintCategory::Hinge:  return SrgbHexToLinear(0x59b8ffu);
        case ComponentClass::PhysicsConstraintCategory::Slider: return SrgbHexToLinear(0xffd15cu);
        case ComponentClass::PhysicsConstraintCategory::Ball:   return SrgbHexToLinear(0xff8c5au);
        case ComponentClass::PhysicsConstraintCategory::Spring: return SrgbHexToLinear(0x6ae092u);
        case ComponentClass::PhysicsConstraintCategory::Spin:   return SrgbHexToLinear(0xb88cffu);
        case ComponentClass::PhysicsConstraintCategory::Tether: return SrgbHexToLinear(0xff6b8au);
        case ComponentClass::PhysicsConstraintCategory::Dock:   return SrgbHexToLinear(0x4dd6c4u);
    }
    return SrgbHexToLinear(0x8a95a5u);
}

std::vector<ConstraintVisualPiece> ComponentGizmoGeometry::BuildConstraintVisualPieces(
    const ComponentClass::ConstraintComponentSettings& Settings,
    float                                              CurrentCoordinate,
    bool                                               DockIntact,
    bool                                               DockKeyAccepted,
    float                                              TimeSeconds)
{
    std::vector<ConstraintVisualPiece> Pieces {};
    Mathematics::Axis3D Axis{}, U{}, V{};
    AxisBasisVectors(Settings.Axis, Axis, U, V);

    const std::array<float, 3> Accent   = QueryConstraintAccentLinearRgb(Settings.Form);
    const std::array<float, 3> Graphite = SrgbHexToLinear(0x2a2d34u);
    const std::array<float, 3> Steel    = SrgbHexToLinear(0x8d939du);

    auto PushSolid = [&](std::array<float, 3> Albedo, float Metal, float Rough, std::array<float, 3> Emissive) -> ConstraintVisualPiece&
    {
        Pieces.emplace_back();
        auto& P = Pieces.back();
        P.PieceCategory   = ConstraintPieceCategory::ShadedSolid;
        P.AlbedoRgb   = Albedo;
        P.EmissiveRgb = Emissive;
        P.Metalness   = Metal;
        P.Roughness   = Rough;
        return P;
    };

    switch (Settings.Form)
    {
        case ComponentClass::PhysicsConstraintCategory::Fixed:
        {
            auto& PrimaryRoot = PushSolid(Graphite, 0.5f, 0.35f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(PrimaryRoot.Vertices, { 0.0f, 0.0f, 0.0f }, Axis, U, V, 0.056f, 0.050f, 0.018f, 6u);
            auto& Face = PushSolid(Accent, 0.1f, 0.35f, { Accent[0] * 0.9f, Accent[1] * 0.9f, Accent[2] * 0.9f });
            AppendOrientedCylinder(Face.Vertices, { Axis.X * 0.011f, Axis.Y * 0.011f, Axis.Z * 0.011f }, Axis, U, V, 0.030f, 0.030f, 0.004f, 6u);
            auto& Bolts = PushSolid(Steel, 1.0f, 0.2f, { 0.0f, 0.0f, 0.0f });
            for (int I = 0; I < 3; ++I)
            {
                const float Phi = (static_cast<float>(I) / 3.0f) * TauF + PiF / 6.0f;
                const Mathematics::Axis3D Pos {
                    (U.X * std::cos(Phi) + V.X * std::sin(Phi)) * 0.041f + Axis.X * 0.010f,
                    (U.Y * std::cos(Phi) + V.Y * std::sin(Phi)) * 0.041f + Axis.Y * 0.010f,
                    (U.Z * std::cos(Phi) + V.Z * std::sin(Phi)) * 0.041f + Axis.Z * 0.010f
                };
                AppendSphere(Bolts.Vertices, Pos, 0.0065f, 6u, 8u);
            }
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Hinge:
        {
            auto& Pin = PushSolid(Steel, 1.0f, 0.18f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(Pin.Vertices, { 0.0f, 0.0f, 0.0f }, Axis, U, V, 0.009f, 0.009f, 0.16f, 14u);
            auto& Knuckles = PushSolid(Graphite, 0.5f, 0.32f, { 0.0f, 0.0f, 0.0f });
            auto& Caps     = PushSolid(Accent, 0.1f, 0.35f, { Accent[0] * 1.1f, Accent[1] * 1.1f, Accent[2] * 1.1f });
            for (float S : { -1.0f, 1.0f })
            {
                AppendOrientedCylinder(Knuckles.Vertices, { Axis.X * S * 0.045f, Axis.Y * S * 0.045f, Axis.Z * S * 0.045f }, Axis, U, V, 0.020f, 0.020f, 0.035f, 16u);
                AppendSphere(Caps.Vertices, { Axis.X * S * 0.080f, Axis.Y * S * 0.080f, Axis.Z * S * 0.080f }, 0.011f, 8u, 10u);
            }
            const float Lo = std::min(Settings.MinAngleDegrees, Settings.MaxAngleDegrees) * PiF / 180.0f;
            const float Hi = std::max(Settings.MinAngleDegrees, Settings.MaxAngleDegrees) * PiF / 180.0f;
            Pieces.emplace_back();
            auto& Fan = Pieces.back();
            Fan.PieceCategory  = ConstraintPieceCategory::RangeFan;
            Fan.AlbedoRgb  = Accent;
            Fan.NormCursor = (CurrentCoordinate - Lo) / std::max(Hi - Lo, 1.0e-5f);
            AppendAnnularSector(Fan.Vertices, U, V, 0.03f, 0.20f, Lo, std::max(Hi - Lo, 0.02f), 40u);

            const Mathematics::Axis3D RotU = RotateAroundAxis(U, Axis, CurrentCoordinate);
            const Mathematics::Axis3D RotV = Norm3(Cross3(Axis, RotU), V);
            auto& Needle = PushSolid(Accent, 0.1f, 0.35f, { Accent[0] * 0.8f, Accent[1] * 0.8f, Accent[2] * 0.8f });
            AppendOrientedBox(Needle.Vertices, { RotU.X * 0.115f, RotU.Y * 0.115f, RotU.Z * 0.115f }, RotU, Axis, RotV, 0.085f, 0.003f, 0.007f);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Slider:
        {
            const float Lo = std::min(Settings.MinTravelMeters, Settings.MaxTravelMeters);
            const float Hi = std::max(Settings.MinTravelMeters, Settings.MaxTravelMeters);
            const float Span = std::max(Hi - Lo, 0.04f);
            const float Mid  = 0.5f * (Lo + Hi);
            auto& Rail = PushSolid(Steel, 0.9f, 0.22f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(Rail.Vertices, { Axis.X * Mid, Axis.Y * Mid, Axis.Z * Mid }, Axis, U, V, 0.008f, 0.008f, Span, 12u);
            auto& Stops = PushSolid(Graphite, 0.5f, 0.35f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(Stops.Vertices, { Axis.X * Lo, Axis.Y * Lo, Axis.Z * Lo }, Axis, U, V, 0.020f, 0.020f, 0.012f, 14u);
            AppendOrientedCylinder(Stops.Vertices, { Axis.X * Hi, Axis.Y * Hi, Axis.Z * Hi }, Axis, U, V, 0.020f, 0.020f, 0.012f, 14u);
            auto& Carriage = PushSolid(Accent, 0.15f, 0.35f, { Accent[0] * 0.8f, Accent[1] * 0.8f, Accent[2] * 0.8f });
            AppendOrientedBox(Carriage.Vertices, { Axis.X * CurrentCoordinate, Axis.Y * CurrentCoordinate, Axis.Z * CurrentCoordinate }, U, Axis, V, 0.024f, 0.026f, 0.024f);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Ball:
        {
            auto& Socket = PushSolid(Graphite, 0.5f, 0.35f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedTorus(Socket.Vertices, { 0.0f, 0.0f, 0.0f }, Axis, U, V, 0.045f, 0.006f, 24u, 8u);
            auto& Ball = PushSolid(Accent, 0.25f, 0.25f, { Accent[0] * 0.7f, Accent[1] * 0.7f, Accent[2] * 0.7f });
            AppendSphere(Ball.Vertices, { 0.0f, 0.0f, 0.0f }, 0.024f, 10u, 14u);
            const float HalfCone = Settings.ConeAngleDegrees * PiF / 180.0f;
            Pieces.emplace_back();
            auto& Cone = Pieces.back();
            Cone.PieceCategory  = ConstraintPieceCategory::RangeFan;
            Cone.AlbedoRgb  = Accent;
            Cone.NormCursor = 0.5f;
            AppendAnnularSector(Cone.Vertices, Axis, U, 0.025f, 0.16f, -HalfCone, std::max(HalfCone * 2.0f, 0.04f), 32u);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Spring:
        {
            const float RestLen = std::max(Settings.TravelEachWayMeters, 0.08f) * 2.4f;
            const float CurLen  = std::max(RestLen + CurrentCoordinate, 0.03f);
            auto& Caps = PushSolid(Graphite, 0.6f, 0.3f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(Caps.Vertices, { -Axis.X * 0.01f, -Axis.Y * 0.01f, -Axis.Z * 0.01f }, Axis, U, V, 0.036f, 0.036f, 0.014f, 16u);
            AppendOrientedCylinder(Caps.Vertices, {  Axis.X * CurLen,  Axis.Y * CurLen,  Axis.Z * CurLen }, Axis, U, V, 0.036f, 0.036f, 0.014f, 16u);
            auto& Coil = PushSolid(Accent, 0.2f, 0.3f, { Accent[0] * 0.65f, Accent[1] * 0.65f, Accent[2] * 0.65f });
            std::vector<Mathematics::Axis3D> Helix {};
            constexpr uint32_t CoilSegs = 64u;
            Helix.reserve(CoilSegs + 1u);
            for (uint32_t I = 0u; I <= CoilSegs; ++I)
            {
                const float T = static_cast<float>(I) / static_cast<float>(CoilSegs);
                const float Phi = T * 5.0f * TauF;
                const float R = 0.032f;
                Helix.push_back({
                    Axis.X * (T * CurLen) + (U.X * std::cos(Phi) + V.X * std::sin(Phi)) * R,
                    Axis.Y * (T * CurLen) + (U.Y * std::cos(Phi) + V.Y * std::sin(Phi)) * R,
                    Axis.Z * (T * CurLen) + (U.Z * std::cos(Phi) + V.Z * std::sin(Phi)) * R
                });
            }
            AppendTubeStrip(Coil.Vertices, Helix, 0.0055f, 8u);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Spin:
        {
            auto& Collar = PushSolid(Steel, 0.9f, 0.2f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedCylinder(Collar.Vertices, { 0.0f, 0.0f, 0.0f }, Axis, U, V, 0.025f, 0.025f, 0.05f, 16u);
            const float SpinPhase = TimeSeconds * (Settings.TargetRpm * TauF / 60.0f);
            const Mathematics::Axis3D RotU = RotateAroundAxis(U, Axis, SpinPhase);
            const Mathematics::Axis3D RotV = Norm3(Cross3(Axis, RotU), V);
            Pieces.emplace_back();
            auto& Comet = Pieces.back();
            Comet.PieceCategory    = ConstraintPieceCategory::CometBand;
            Comet.AlbedoRgb    = Accent;
            Comet.AlphaOrScale = 0.95f;
            AppendAnnularSector(Comet.Vertices, RotU, RotV, 0.045f, 0.145f, 0.0f, TauF, 48u);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Tether:
        {
            const float RopeLen = std::max(Settings.RopeLengthMeters, 0.12f);
            auto& Anchors = PushSolid(Steel, 0.8f, 0.25f, { 0.0f, 0.0f, 0.0f });
            AppendSphere(Anchors.Vertices, { 0.0f, 0.0f, 0.0f }, 0.016f, 8u, 10u);
            AppendSphere(Anchors.Vertices, { Axis.X * RopeLen, Axis.Y * RopeLen, Axis.Z * RopeLen }, 0.016f, 8u, 10u);
            auto& Rope = PushSolid(Accent, 0.1f, 0.4f, { Accent[0] * 0.7f, Accent[1] * 0.7f, Accent[2] * 0.7f });
            std::vector<Mathematics::Axis3D> Strand {};
            for (uint32_t I = 0u; I <= 12u; ++I)
            {
                const float T = static_cast<float>(I) / 12.0f;
                const float Sag = -std::sin(T * PiF) * 0.04f;
                Strand.push_back({ Axis.X * T * RopeLen, Axis.Y * T * RopeLen + Sag, Axis.Z * T * RopeLen });
            }
            AppendTubeStrip(Rope.Vertices, Strand, 0.006f, 8u);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Dock:
        {
            const std::array<float, 3> DockCol = DockIntact
                ? SrgbHexToLinear(0x4dd6c4u)
                : (DockKeyAccepted ? SrgbHexToLinear(0xffb238u) : SrgbHexToLinear(0xff5a5au));
            auto& Collar = PushSolid(Graphite, 0.6f, 0.3f, { 0.0f, 0.0f, 0.0f });
            AppendOrientedTorus(Collar.Vertices, { 0.0f, 0.0f, 0.0f }, Axis, U, V, 0.070f, 0.008f, 28u, 8u);
            Pieces.emplace_back();
            auto& Iris = Pieces.back();
            Iris.PieceCategory  = ConstraintPieceCategory::DockIrisDisc;
            Iris.AlbedoRgb  = DockCol;
            Iris.NormCursor = DockIntact ? 1.0f : 0.0f;
            const float R = 0.075f;
            const auto P0 = Mathematics::Axis3D{ (-U.X - V.X) * R, (-U.Y - V.Y) * R, (-U.Z - V.Z) * R };
            const auto P1 = Mathematics::Axis3D{ ( U.X - V.X) * R, ( U.Y - V.Y) * R, ( U.Z - V.Z) * R };
            const auto P2 = Mathematics::Axis3D{ ( U.X + V.X) * R, ( U.Y + V.Y) * R, ( U.Z + V.Z) * R };
            const auto P3 = Mathematics::Axis3D{ (-U.X + V.X) * R, (-U.Y + V.Y) * R, (-U.Z + V.Z) * R };
            AppendTri(Iris.Vertices, P0, P1, P2, Axis, Axis, Axis, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f });
            AppendTri(Iris.Vertices, P0, P2, P3, Axis, Axis, Axis, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
            break;
        }
    }
    return Pieces;
}

} // namespace Frontier::GeometricRaster::ComponentEditor
