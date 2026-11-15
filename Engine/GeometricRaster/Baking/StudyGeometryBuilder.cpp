/*============================================================================================================================================
                                                           STUDYGEOMETRYBUILDER.CPP
============================================================================================================================================*/
// 🧩 Implementation of High/Low procedural baking study pairs, Wavefront OBJ/STL import, UV-seam island flood & tangent smoothing.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"

#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/GeometrySpace/Primitives/RevolutionSweepPrimitives.h"
#include "Engine/GeometrySpace/Selection/IslandFloodSelection.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

using namespace Frontier::GeometrySpace;
using namespace Frontier::Mathematics;

namespace
{

constexpr float TwoPiF = 6.283185307179586f;
constexpr float PiF    = 3.141592653589793f;

[[nodiscard]] Axis3D SafeNormalize(Axis3D V, Axis3D DefaultAxis = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float L = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
    return (L > 1.0e-7f) ? Axis3D{ V.X / L, V.Y / L, V.Z / L } : DefaultAxis;
}

void BuildFrisvadOrtho(Axis3D N, Axis3D& OutT, Axis3D& OutB) noexcept
{
    if (N.Z < -0.99999f)
    {
        OutT = { 0.0f, -1.0f, 0.0f };
        OutB = { -1.0f, 0.0f, 0.0f };
        return;
    }
    const float A  = 1.0f / (1.0f + N.Z);
    const float BB = -N.X * N.Y * A;
    OutT = { 1.0f - N.X * N.X * A, BB, -N.X };
    OutB = { BB, 1.0f - N.Y * N.Y * A, -N.Y };
}

void AssignBoxAutoUVsIfDegenerate(PolygonNetwork& Network)
{
    float TotalSignedUV = 0.0f;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        TotalSignedUV += std::fabs(Network.EvaluateFaceSignedAreaUV(FaceIdx));
    }
    if (TotalSignedUV > 1.0e-4f)
    {
        return;
    }

    Axis3D BMin { 1.0e30f, 1.0e30f, 1.0e30f };
    Axis3D BMax { -1.0e30f, -1.0e30f, -1.0e30f };
    for (const Vertex& V : Network.Vertices)
    {
        BMin.X = std::min(BMin.X, V.Axes.X);
        BMin.Y = std::min(BMin.Y, V.Axes.Y);
        BMin.Z = std::min(BMin.Z, V.Axes.Z);
        BMax.X = std::max(BMax.X, V.Axes.X);
        BMax.Y = std::max(BMax.Y, V.Axes.Y);
        BMax.Z = std::max(BMax.Z, V.Axes.Z);
    }
    const float Span = std::max({ 1.0e-4f, BMax.X - BMin.X, BMax.Y - BMin.Y, BMax.Z - BMin.Z });

    // Clean 6-chart cube UV projection packed across a 3x2 grid inside [0.02..0.98]^2
    for (Face& F : Network.Faces)
    {
        if (F.BoundaryLoopEdgeIndex == InvalidNetworkIndex || F.CornerCount < 3u)
        {
            continue;
        }
        const Axis3D GN = SafeNormalize(F.GeometricNormal);
        const float Ax = std::fabs(GN.X);
        const float Ay = std::fabs(GN.Y);
        const float Az = std::fabs(GN.Z);

        uint32_t TileIdx = 0u;
        if (Ax >= Ay && Ax >= Az)      TileIdx = (GN.X >= 0.0f) ? 0u : 1u;
        else if (Ay >= Ax && Ay >= Az) TileIdx = (GN.Y >= 0.0f) ? 2u : 3u;
        else                           TileIdx = (GN.Z >= 0.0f) ? 4u : 5u;

        const float CellU0 = 0.03f + static_cast<float>(TileIdx % 3u) * 0.32f;
        const float CellV0 = 0.03f + static_cast<float>(TileIdx / 3u) * 0.48f;
        const float CellW  = 0.28f;
        const float CellH  = 0.44f;

        uint32_t Walk = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            const Axis3D P = Network.Vertices[Network.Edges[Walk].OriginVertexIndex].Axes;
            const Axis3D Q = { (P.X - BMin.X) / Span, (P.Y - BMin.Y) / Span, (P.Z - BMin.Z) / Span };
            float U = 0.0f;
            float V = 0.0f;
            if (TileIdx <= 1u)      { U = Q.Y; V = Q.Z; }
            else if (TileIdx <= 3u) { U = Q.X; V = Q.Z; }
            else                    { U = Q.X; V = Q.Y; }

            Network.Edges[Walk].ParameterUV = {
                CellU0 + std::clamp(U, 0.0f, 1.0f) * CellW,
                CellV0 + std::clamp(V, 0.0f, 1.0f) * CellH,
            };
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }
    }
    Network.LinkOppositeEdges(true);
}

void AppendSculptedBox(
    PolygonNetwork& Target,
    Axis3D          Center,
    Scalar3D        Half,
    uint32_t        SubDivPerSide,
    bool            AppendBevelDetails,
    uint16_t        PartBaseOrdinal)
{
    // 6-island cube pack (3x2 cells inside [0.02..0.98]) so each face is an independent seam-bounded UV chart
    struct CubeSide
    {
        Axis3D Normal {};
        Axis3D Right  {};
        Axis3D Up     {};
        float  CellU0 = 0.0f;
        float  CellV0 = 0.0f;
    };
    const CubeSide Sides[6] = {
        { {  1.0f,  0.0f,  0.0f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, 0.03f, 0.03f },
        { { -1.0f,  0.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, 0.36f, 0.03f },
        { {  0.0f,  1.0f,  0.0f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, 0.69f, 0.03f },
        { {  0.0f, -1.0f,  0.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f }, 0.03f, 0.52f },
        { {  0.0f,  0.0f,  1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f }, 0.36f, 0.52f },
        { {  0.0f,  0.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f,-1.0f, 0.0f }, 0.69f, 0.52f },
    };

    const float CellW = 0.28f;
    const float CellH = 0.44f;
    const uint32_t N = std::max(2u, SubDivPerSide);

    for (uint32_t S = 0u; S < 6u; ++S)
    {
        const CubeSide& Side = Sides[S];
        std::vector<uint32_t> GridVerts((N + 1u) * (N + 1u), 0u);
        std::vector<UVAxes>   GridUVs((N + 1u) * (N + 1u));

        for (uint32_t Iy = 0u; Iy <= N; ++Iy)
        {
            const float V = static_cast<float>(Iy) / static_cast<float>(N);
            const float Ly = V * 2.0f - 1.0f;
            for (uint32_t Ix = 0u; Ix <= N; ++Ix)
            {
                const float U = static_cast<float>(Ix) / static_cast<float>(N);
                const float Lx = U * 2.0f - 1.0f;

                Axis3D Local = {
                    Side.Normal.X * Half.X + Side.Right.X * Lx * Half.X + Side.Up.X * Ly * Half.Y,
                    Side.Normal.Y * Half.Y + Side.Right.Y * Lx * Half.X + Side.Up.Y * Ly * Half.Y,
                    Side.Normal.Z * Half.Z + Side.Right.Z * Lx * Half.X + Side.Up.Z * Ly * Half.Z,
                };
                // Smooth box edge rounding for bevel / sculpted detail
                const float MaxC = std::max({ std::fabs(Lx), std::fabs(Ly) });
                float Disp = 0.0f;
                if (AppendBevelDetails)
                {
                    // Soft chamfer taper near box perimeter + recessed central technical panel + 4 corner bolt bosses
                    const float EdgeBlend = std::clamp((MaxC - 0.80f) / 0.20f, 0.0f, 1.0f);
                    Disp -= EdgeBlend * EdgeBlend * 0.075f;

                    const float RCenter = std::sqrt(Lx * Lx + Ly * Ly);
                    const float Ring    = std::exp(-std::pow((RCenter - 0.42f) / 0.07f, 2.0f));
                    Disp -= Ring * 0.036f;

                    if (RCenter < 0.24f)
                    {
                        Disp += 0.024f * (1.0f - (RCenter / 0.24f) * (RCenter / 0.24f));
                    }

                    // 4 corner bolt domes
                    for (int Bx = -1; Bx <= 1; Bx += 2)
                    {
                        for (int By = -1; By <= 1; By += 2)
                        {
                            const float Dx = Lx - 0.65f * static_cast<float>(Bx);
                            const float Dy = Ly - 0.65f * static_cast<float>(By);
                            const float DB = std::sqrt(Dx * Dx + Dy * Dy);
                            if (DB < 0.095f)
                            {
                                Disp += 0.030f * std::cos((DB / 0.095f) * (PiF * 0.5f));
                            }
                        }
                    }

                    // Slight cross groove lines
                    const float CrossMin = std::min(std::fabs(Lx), std::fabs(Ly));
                    if (RCenter > 0.24f && RCenter < 0.72f && CrossMin < 0.035f)
                    {
                        Disp -= 0.014f * std::cos((CrossMin / 0.035f) * (PiF * 0.5f));
                    }
                }
                else
                {
                    const float EdgeBlend = std::clamp((MaxC - 0.88f) / 0.12f, 0.0f, 1.0f);
                    Disp -= EdgeBlend * 0.03f;
                }

                const Axis3D WorldP = {
                    Center.X + Local.X + Side.Normal.X * Disp,
                    Center.Y + Local.Y + Side.Normal.Y * Disp,
                    Center.Z + Local.Z + Side.Normal.Z * Disp,
                };
                GridVerts[Iy * (N + 1u) + Ix] = Target.AppendVertex(WorldP);
                GridUVs[Iy * (N + 1u) + Ix]   = { Side.CellU0 + U * CellW, Side.CellV0 + V * CellH };
            }
        }

        const uint16_t SidePart = static_cast<uint16_t>(PartBaseOrdinal + (S / 2u));
        for (uint32_t Iy = 0u; Iy < N; ++Iy)
        {
            for (uint32_t Ix = 0u; Ix < N; ++Ix)
            {
                const uint32_t V00 = GridVerts[Iy * (N + 1u) + Ix];
                const uint32_t V10 = GridVerts[Iy * (N + 1u) + Ix + 1u];
                const uint32_t V11 = GridVerts[(Iy + 1u) * (N + 1u) + Ix + 1u];
                const uint32_t V01 = GridVerts[(Iy + 1u) * (N + 1u) + Ix];
                const uint32_t Quad[4] = { V00, V10, V11, V01 };
                const UVAxes   UVs[4]  = {
                    GridUVs[Iy * (N + 1u) + Ix],
                    GridUVs[Iy * (N + 1u) + Ix + 1u],
                    GridUVs[(Iy + 1u) * (N + 1u) + Ix + 1u],
                    GridUVs[(Iy + 1u) * (N + 1u) + Ix],
                };
                (void)Target.AppendFace(Quad, UVs, {}, static_cast<uint16_t>(S), SidePart);
            }
        }
    }
}

void AppendSculptedUvSphere(
    PolygonNetwork& Target,
    Axis3D          Center,
    float           Radius,
    uint32_t        LonSegs,
    uint32_t        LatRings,
    UVPatchBounds   UVRegion,
    bool            SculptDetails,
    uint16_t        PartOrdinal)
{
    std::vector<uint32_t> Verts((LatRings + 1u) * (LonSegs + 1u), 0u);
    std::vector<UVAxes>   UVs((LatRings + 1u) * (LonSegs + 1u));

    for (uint32_t Lat = 0u; Lat <= LatRings; ++Lat)
    {
        const float V     = static_cast<float>(Lat) / static_cast<float>(LatRings);
        const float Theta = (1.0f - V) * PiF; // V=1 North pole (+Z), V=0 South pole (-Z)
        const float SinT  = std::sin(Theta);
        const float CosT  = std::cos(Theta);

        for (uint32_t Lon = 0u; Lon <= LonSegs; ++Lon)
        {
            const float U   = static_cast<float>(Lon) / static_cast<float>(LonSegs);
            const float Phi = U * TwoPiF;
            const float CosP = std::cos(Phi);
            const float SinP = std::sin(Phi);

            float R = Radius;
            if (SculptDetails)
            {
                // Equatorial trench + 6 vertical ribs + polar rings + spherical dimples
                const float Eq = std::exp(-std::pow((V - 0.5f) / 0.045f, 2.0f));
                const float Ribs = std::pow(std::max(0.0f, std::cos(Phi * 6.0f)), 4.0f) * std::sin(Theta);
                const float Bands = 0.015f * std::sin(Theta * 10.0f);
                const float Dimple = 0.020f * std::cos(Phi * 12.0f) * std::cos((V - 0.5f) * 16.0f) * SinT;
                R += (-0.048f * Eq + 0.032f * Ribs + Bands + Dimple) * Radius;
            }

            const Axis3D P = {
                Center.X + R * SinT * CosP,
                Center.Y + R * SinT * SinP,
                Center.Z + R * CosT,
            };
            Verts[Lat * (LonSegs + 1u) + Lon] = Target.AppendVertex(P);
            UVs[Lat * (LonSegs + 1u) + Lon]   = {
                UVRegion.Minimum.U + U * (UVRegion.Maximum.U - UVRegion.Minimum.U),
                UVRegion.Minimum.V + V * (UVRegion.Maximum.V - UVRegion.Minimum.V),
            };
        }
    }

    for (uint32_t Lat = 0u; Lat < LatRings; ++Lat)
    {
        for (uint32_t Lon = 0u; Lon < LonSegs; ++Lon)
        {
            const uint32_t I00 = Lat * (LonSegs + 1u) + Lon;
            const uint32_t I10 = I00 + 1u;
            const uint32_t I01 = (Lat + 1u) * (LonSegs + 1u) + Lon;
            const uint32_t I11 = I01 + 1u;
            const uint32_t Quad[4] = { Verts[I00], Verts[I10], Verts[I11], Verts[I01] };
            const UVAxes   QVis[4] = { UVs[I00], UVs[I10], UVs[I11], UVs[I01] };
            (void)Target.AppendFace(Quad, QVis, {}, PartOrdinal, PartOrdinal);
        }
    }
}

void AppendUtahTeapotStudy(PolygonNetwork& Target, bool HighDetail)
{
    const uint32_t BodyRadial = HighDetail ? 72u : 28u;
    const uint32_t TubeSegs   = HighDetail ? 28u : 12u;

    // 1. Body lathe profile (+Z up, unit pedestal resting cleanly at Z=0..0.92)
    std::vector<LatheProfileWaypoint> BodyProfile {
        { 0.000f, 0.000f, true },
        { 0.340f, 0.000f, true },
        { 0.365f, 0.045f, false },
        { 0.440f, 0.145f, false },
        { 0.560f, 0.305f, false },
        { 0.595f, 0.445f, false },
        { 0.535f, 0.590f, false },
        { 0.385f, 0.700f, false },
        { 0.340f, 0.745f, true },
        { 0.382f, 0.780f, true },
        { 0.345f, 0.790f, false },
    };
    if (HighDetail)
    {
        // High adds fine waist flute + rim collar + doubled axial density
        std::vector<LatheProfileWaypoint> Dense;
        for (std::size_t I = 0u; I + 1u < BodyProfile.size(); ++I)
        {
            const auto& A = BodyProfile[I];
            const auto& B = BodyProfile[I + 1u];
            Dense.push_back(A);
            for (int Sub = 1; Sub <= 2; ++Sub)
            {
                const float T = static_cast<float>(Sub) / 3.0f;
                float R = A.RadialOffsetMeters + (B.RadialOffsetMeters - A.RadialOffsetMeters) * T;
                const float Z = A.AxialHeightMeters + (B.AxialHeightMeters - A.AxialHeightMeters) * T;
                // Add crisp high-frequency ornamental grooves near belly & shoulder
                if (Z > 0.25f && Z < 0.65f)
                {
                    R += 0.016f * std::sin((Z - 0.25f) * 58.0f);
                }
                Dense.push_back({ R, Z, false });
            }
        }
        Dense.push_back(BodyProfile.back());
        BodyProfile = std::move(Dense);
    }

    AppendLatheRevolutionPrimitive(
        Target,
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        BodyProfile,
        BodyRadial,
        TwoPiF,
        false,
        { { 0.03f, 0.03f }, { 0.52f, 0.54f } },
        {},
        0u,
        0u);

    // 2. Lid + Finial knob (Part 3 — intentional narrow collar seam between rim and lid!)
    std::vector<LatheProfileWaypoint> LidProfile {
        { 0.338f, 0.750f, true },
        { 0.335f, 0.785f, false },
        { 0.255f, 0.840f, false },
        { 0.125f, 0.875f, false },
        { 0.048f, 0.892f, true },
        { 0.088f, 0.940f, false },
        { 0.068f, 0.978f, false },
        { 0.000f, 0.992f, false },
    };
    if (HighDetail)
    {
        for (auto& W : LidProfile)
        {
            if (W.RadialOffsetMeters > 0.10f && W.RadialOffsetMeters < 0.32f)
            {
                W.AxialHeightMeters += 0.012f;
            }
        }
    }
    AppendLatheRevolutionPrimitive(
        Target,
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        LidProfile,
        HighDetail ? 64u : 24u,
        TwoPiF,
        false,
        { { 0.56f, 0.03f }, { 0.97f, 0.44f } },
        {},
        3u,
        3u);

    // 3. Spout along +X (Part 1): smoothly embedded inside belly at x=0.38 so joint has zero protruding crease
    std::vector<Axis2D> SpoutSection;
    for (uint32_t I = 0u; I < TubeSegs; ++I)
    {
        const float Ang = TwoPiF * static_cast<float>(I) / static_cast<float>(TubeSegs);
        float Mod = 1.0f;
        if (HighDetail)
        {
            Mod += 0.045f * std::cos(Ang * 6.0f);
        }
        SpoutSection.push_back({ std::cos(Ang) * 0.095f * Mod, std::sin(Ang) * 0.095f * Mod });
    }
    const std::vector<SweepSpineWaypoint> SpoutSpine {
        { { 0.34f, 0.0f, 0.29f }, 0.96f, 0.0f },
        { { 0.46f, 0.0f, 0.30f }, 0.98f, 0.0f },
        { { 0.59f, 0.0f, 0.34f }, 0.88f, 0.0f },
        { { 0.70f, 0.0f, 0.42f }, 0.75f, 0.0f },
        { { 0.79f, 0.0f, 0.54f }, 0.64f, 0.0f },
        { { 0.86f, 0.0f, 0.67f }, 0.54f, 0.0f },
        { { 0.94f, 0.0f, 0.77f }, 0.46f, 0.0f },
    };
    AppendProfileSweepPrimitive(
        Target,
        SpoutSection,
        SpoutSpine,
        false,
        false, // Intentional open spout tip (classic non-watertight Utah Teapot stress case!)
        { { 0.03f, 0.58f }, { 0.48f, 0.97f } },
        {},
        1u,
        1u);

    // 4. D-Handle along -X (Part 2): smoothly anchored inside -X wall
    std::vector<Axis2D> HandleSection;
    for (uint32_t I = 0u; I < TubeSegs; ++I)
    {
        const float Ang = TwoPiF * static_cast<float>(I) / static_cast<float>(TubeSegs);
        float Ridge = HighDetail ? (1.0f + 0.08f * std::cos(Ang * 2.0f)) : 1.0f;
        HandleSection.push_back({ std::cos(Ang) * 0.064f * Ridge, std::sin(Ang) * 0.048f });
    }
    const std::vector<SweepSpineWaypoint> HandleSpine {
        { { -0.40f, 0.0f, 0.60f }, 0.95f, 0.0f },
        { { -0.58f, 0.0f, 0.65f }, 0.95f, 0.0f },
        { { -0.78f, 0.0f, 0.64f }, 0.95f, 0.0f },
        { { -0.90f, 0.0f, 0.49f }, 0.91f, 0.0f },
        { { -0.86f, 0.0f, 0.31f }, 0.88f, 0.0f },
        { { -0.66f, 0.0f, 0.21f }, 0.92f, 0.0f },
        { { -0.38f, 0.0f, 0.22f }, 1.00f, 0.0f },
    };
    AppendProfileSweepPrimitive(
        Target,
        HandleSection,
        HandleSpine,
        false,
        true,
        { { 0.56f, 0.48f }, { 0.97f, 0.97f } },
        {},
        2u,
        2u);
}

void AppendShaderBallStudy(PolygonNetwork& Target, bool HighDetail)
{
    // 1. Cutaway outer spherical shell (255 deg lathe with open studio preview window) — Part 0
    std::vector<LatheProfileWaypoint> ShellProfile {
        { 0.35f, 0.14f, true },
        { 0.52f, 0.24f, false },
        { 0.62f, 0.48f, false },
        { 0.56f, 0.76f, false },
        { 0.32f, 0.94f, false },
        { 0.00f, 1.00f, false },
    };
    if (HighDetail)
    {
        std::vector<LatheProfileWaypoint> HighShell;
        for (std::size_t I = 0u; I + 1u < ShellProfile.size(); ++I)
        {
            const auto& A = ShellProfile[I];
            const auto& B = ShellProfile[I + 1u];
            HighShell.push_back(A);
            for (int S = 1; S <= 3; ++S)
            {
                const float T = static_cast<float>(S) / 4.0f;
                float R = A.RadialOffsetMeters + (B.RadialOffsetMeters - A.RadialOffsetMeters) * T;
                const float Z = A.AxialHeightMeters + (B.AxialHeightMeters - A.AxialHeightMeters) * T;
                R += 0.018f * std::sin(Z * 42.0f);
                HighShell.push_back({ R, Z, false });
            }
        }
        HighShell.push_back(ShellProfile.back());
        ShellProfile = std::move(HighShell);
    }

    AppendLatheRevolutionPrimitive(
        Target,
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        ShellProfile,
        HighDetail ? 64u : 28u,
        4.45f,
        true,
        { { 0.03f, 0.03f }, { 0.52f, 0.52f } },
        {},
        0u,
        0u);

    // 2. Inner contrasting core sphere nested inside cutaway cavity — Part 1
    AppendSculptedUvSphere(
        Target,
        { 0.0f, 0.0f, 0.52f },
        0.31f,
        HighDetail ? 44u : 20u,
        HighDetail ? 26u : 12u,
        { { 0.56f, 0.03f }, { 0.97f, 0.44f } },
        HighDetail,
        1u);

    // 3. Sculpted studio collar torus & circular pedestal plinth — Parts 2 & 3
    AppendTorusRingPrimitive(
        Target,
        { 0.0f, 0.0f, 0.14f },
        0.48f,
        HighDetail ? 0.088f : 0.072f,
        HighDetail ? 64u : 24u,
        HighDetail ? 24u : 12u,
        { { 0.03f, 0.56f }, { 0.48f, 0.97f } },
        {},
        2u,
        2u);

    AppendCylinderFrustumPrimitive(
        Target,
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.12f },
        0.64f,
        0.56f,
        HighDetail ? 64u : 24u,
        { { 0.54f, 0.52f }, { 0.97f, 0.97f } },
        {},
        3u,
        3u);
}

void AppendTorusKnotStudy(PolygonNetwork& Target, bool HighDetail)
{
    const uint32_t SpineSamples = HighDetail ? 216u : 72u;
    const uint32_t RadialSegs   = HighDetail ? 28u  : 12u;

    std::vector<Axis2D> Section;
    for (uint32_t J = 0u; J < RadialSegs; ++J)
    {
        const float Ang = TwoPiF * static_cast<float>(J) / static_cast<float>(RadialSegs);
        float R = 0.155f;
        if (HighDetail)
        {
            R *= 1.0f + 0.135f * std::cos(Ang * 5.0f);
        }
        Section.push_back({ std::cos(Ang) * R, std::sin(Ang) * R });
    }

    std::vector<SweepSpineWaypoint> Spine;
    Spine.reserve(SpineSamples);
    for (uint32_t I = 0u; I < SpineSamples; ++I)
    {
        const float T = TwoPiF * static_cast<float>(I) / static_cast<float>(SpineSamples);
        const float P = 2.0f * T;
        const float Q = 3.0f * T;
        const float R = 0.52f + 0.22f * std::cos(Q);
        const Axis3D Pos {
            R * std::cos(P),
            R * std::sin(P),
            0.46f + 0.22f * std::sin(Q),
        };
        const float Scale = HighDetail ? (1.0f + 0.08f * std::sin(T * 12.0f)) : 1.0f;
        Spine.push_back({ Pos, Scale, T * 3.0f });
    }

    AppendProfileSweepPrimitive(
        Target,
        Section,
        Spine,
        true,
        false,
        { { 0.03f, 0.03f }, { 0.97f, 0.97f } },
        {},
        0u,
        0u);
}

void NormalizeNetworkToStudioPedestal(PolygonNetwork& Network)
{
    if (Network.Vertices.empty())
    {
        return;
    }
    Axis3D BMin { 1.0e30f, 1.0e30f, 1.0e30f };
    Axis3D BMax { -1.0e30f, -1.0e30f, -1.0e30f };
    for (const Vertex& V : Network.Vertices)
    {
        BMin.X = std::min(BMin.X, V.Axes.X);
        BMin.Y = std::min(BMin.Y, V.Axes.Y);
        BMin.Z = std::min(BMin.Z, V.Axes.Z);
        BMax.X = std::max(BMax.X, V.Axes.X);
        BMax.Y = std::max(BMax.Y, V.Axes.Y);
        BMax.Z = std::max(BMax.Z, V.Axes.Z);
    }
    const Axis3D Center {
        0.5f * (BMin.X + BMax.X),
        0.5f * (BMin.Y + BMax.Y),
        BMin.Z,
    };
    const float MaxExtent = std::max({ 1.0e-4f, BMax.X - BMin.X, BMax.Y - BMin.Y, BMax.Z - BMin.Z });
    const float Scale     = 1.45f / MaxExtent;
    for (Vertex& V : Network.Vertices)
    {
        V.Axes = {
            (V.Axes.X - Center.X) * Scale,
            (V.Axes.Y - Center.Y) * Scale,
            (V.Axes.Z - Center.Z) * Scale,
        };
    }
}

[[nodiscard]] bool ParseWavefrontObj(
    const std::filesystem::path& FilePath,
    PolygonNetwork&              OutNetwork,
    std::string*                 OutDiag)
{
    std::ifstream File(FilePath);
    if (!File.is_open())
    {
        if (OutDiag != nullptr) *OutDiag = "Unable to open OBJ file: " + FilePath.string();
        return false;
    }

    OutNetwork.Reset();
    OutNetwork.NetworkTitle = FilePath.stem().string();

    std::vector<Axis3D> TempPositions;
    std::vector<UVAxes> TempUVs;
    std::unordered_map<std::string, uint16_t> PartIndexByName;
    uint16_t ActivePartOrdinal = 0u;

    std::string Line;
    while (std::getline(File, Line))
    {
        if (Line.empty() || Line[0] == '#') continue;
        std::istringstream Iss(Line);
        std::string Tag;
        Iss >> Tag;
        if (Tag == "v")
        {
            float X = 0.0f, Y = 0.0f, Z = 0.0f;
            Iss >> X >> Y >> Z;
            // Convert standard Y-up OBJ convention -> Frontier RHR +Z Up (X, -Z, Y)
            TempPositions.push_back({ X, -Z, Y });
        }
        else if (Tag == "vt")
        {
            float U = 0.0f, V = 0.0f;
            Iss >> U >> V;
            TempUVs.push_back({ U, V });
        }
        else if (Tag == "g" || Tag == "o" || Tag == "usemtl")
        {
            std::string Name;
            if (Iss >> Name)
            {
                auto It = PartIndexByName.find(Name);
                if (It == PartIndexByName.end())
                {
                    const uint16_t NextPart = static_cast<uint16_t>(PartIndexByName.size());
                    PartIndexByName.emplace(Name, NextPart);
                    ActivePartOrdinal = NextPart;
                }
                else
                {
                    ActivePartOrdinal = It->second;
                }
            }
        }
        else if (Tag == "f")
        {
            std::vector<uint32_t> FaceVerts;
            std::vector<UVAxes>   FaceUVs;
            std::string Token;
            while (Iss >> Token)
            {
                int PIdx = 0;
                int TIdx = 0;
                const std::size_t S1 = Token.find('/');
                if (S1 == std::string::npos)
                {
                    PIdx = std::atoi(Token.c_str());
                }
                else
                {
                    PIdx = std::atoi(Token.substr(0, S1).c_str());
                    const std::size_t S2 = Token.find('/', S1 + 1u);
                    const std::string VtStr = (S2 == std::string::npos)
                        ? Token.substr(S1 + 1u)
                        : Token.substr(S1 + 1u, S2 - S1 - 1u);
                    if (!VtStr.empty())
                    {
                        TIdx = std::atoi(VtStr.c_str());
                    }
                }
                const int ResolvedP = (PIdx < 0)
                    ? static_cast<int>(TempPositions.size()) + PIdx
                    : (PIdx - 1);
                if (ResolvedP < 0 || ResolvedP >= static_cast<int>(TempPositions.size()))
                {
                    continue;
                }
                while (OutNetwork.Vertices.size() < TempPositions.size())
                {
                    (void)OutNetwork.AppendVertex(TempPositions[OutNetwork.Vertices.size()]);
                }
                FaceVerts.push_back(static_cast<uint32_t>(ResolvedP));
                UVAxes CornerUV { 0.5f, 0.5f };
                if (TIdx != 0)
                {
                    const int ResolvedT = (TIdx < 0) ? (static_cast<int>(TempUVs.size()) + TIdx) : (TIdx - 1);
                    if (ResolvedT >= 0 && ResolvedT < static_cast<int>(TempUVs.size()))
                    {
                        CornerUV = TempUVs[static_cast<std::size_t>(ResolvedT)];
                    }
                }
                FaceUVs.push_back(CornerUV);
            }
            if (FaceVerts.size() >= 3u)
            {
                (void)OutNetwork.AppendFace(FaceVerts, FaceUVs, {}, ActivePartOrdinal, ActivePartOrdinal);
            }
        }
    }

    if (OutNetwork.Faces.empty())
    {
        if (OutDiag != nullptr) *OutDiag = "OBJ contained zero polygon faces: " + FilePath.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseStlFile(
    const std::filesystem::path& FilePath,
    PolygonNetwork&              OutNetwork,
    std::string*                 OutDiag)
{
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        if (OutDiag != nullptr) *OutDiag = "Unable to open STL file: " + FilePath.string();
        return false;
    }
    const std::streamsize ByteLen = File.tellg();
    File.seekg(0, std::ios::beg);
    if (ByteLen < 84)
    {
        if (OutDiag != nullptr) *OutDiag = "STL file truncated: " + FilePath.string();
        return false;
    }

    std::vector<uint8_t> Raw(static_cast<std::size_t>(ByteLen));
    if (!File.read(reinterpret_cast<char*>(Raw.data()), ByteLen))
    {
        return false;
    }

    OutNetwork.Reset();
    OutNetwork.NetworkTitle = FilePath.stem().string();

    uint32_t TriCount = 0u;
    std::memcpy(&TriCount, Raw.data() + 80u, sizeof(uint32_t));
    const std::size_t ExpectedBinaryBytes = 84ull + static_cast<std::size_t>(TriCount) * 50ull;

    std::unordered_map<uint64_t, uint32_t> WeldedVerts;
    const auto GetOrInsertVertex = [&](Axis3D P) -> uint32_t
    {
        const int64_t Qx = static_cast<int64_t>(std::llround(P.X * 10000.0));
        const int64_t Qy = static_cast<int64_t>(std::llround(P.Y * 10000.0));
        const int64_t Qz = static_cast<int64_t>(std::llround(P.Z * 10000.0));
        const uint64_t Key = static_cast<uint64_t>(Qx * 73856093LL) ^ static_cast<uint64_t>(Qy * 19349663LL) ^ static_cast<uint64_t>(Qz * 83492791LL);
        auto It = WeldedVerts.find(Key);
        if (It != WeldedVerts.end())
        {
            const Axis3D Existing = OutNetwork.Vertices[It->second].Axes;
            if (std::fabs(Existing.X - P.X) < 1.0e-3f && std::fabs(Existing.Y - P.Y) < 1.0e-3f && std::fabs(Existing.Z - P.Z) < 1.0e-3f)
            {
                return It->second;
            }
        }
        const uint32_t Idx = OutNetwork.AppendVertex(P);
        WeldedVerts.emplace(Key, Idx);
        return Idx;
    };

    if (Raw.size() >= ExpectedBinaryBytes && TriCount > 0u)
    {
        for (uint32_t T = 0u; T < TriCount; ++T)
        {
            const uint8_t* TriPtr = Raw.data() + 84ull + static_cast<std::size_t>(T) * 50ull;
            float Coords[9] {};
            std::memcpy(Coords, TriPtr + 12u, 36u);
            const uint32_t V0 = GetOrInsertVertex({ Coords[0], Coords[1], Coords[2] });
            const uint32_t V1 = GetOrInsertVertex({ Coords[3], Coords[4], Coords[5] });
            const uint32_t V2 = GetOrInsertVertex({ Coords[6], Coords[7], Coords[8] });
            const uint32_t TriIdx[3] = { V0, V1, V2 };
            (void)OutNetwork.AppendFace(TriIdx);
        }
    }
    else
    {
        std::string Text(reinterpret_cast<const char*>(Raw.data()), Raw.size());
        std::istringstream Iss(Text);
        std::string Word;
        std::vector<uint32_t> TriVerts;
        while (Iss >> Word)
        {
            if (Word == "vertex")
            {
                float X = 0.0f, Y = 0.0f, Z = 0.0f;
                Iss >> X >> Y >> Z;
                TriVerts.push_back(GetOrInsertVertex({ X, Y, Z }));
                if (TriVerts.size() == 3u)
                {
                    (void)OutNetwork.AppendFace(TriVerts);
                    TriVerts.clear();
                }
            }
        }
    }

    if (OutNetwork.Faces.empty())
    {
        if (OutDiag != nullptr) *OutDiag = "STL contained zero triangles: " + FilePath.string();
        return false;
    }
    return true;
}

struct TriangulatedCorner
{
    uint32_t VertexIndex = 0u;
    UVAxes   Uv          {};
};

void ExtractNetworkTriangulation(
    const PolygonNetwork&              Network,
    const std::vector<uint32_t>&       FaceToIsland,
    std::vector<GpuLowSurfaceVertex>&  OutGpuVerts,
    std::vector<GpuBakeTriangle>*      OutRichTris,
    std::vector<SpatialAcceleration::RayTrianglePrimitive>* OutBvhPrims,
    std::vector<Axis3D>*               OutFlatPositions,
    uint32_t&                          OutTriCount)
{
    // Compute MikkTSpace-style area/angle weighted vertex tangent & bitangent sums keyed per vertex
    const std::size_t VertexCount = Network.Vertices.size();
    std::vector<Axis3D> AccumTangent(VertexCount, Axis3D{ 0.0f, 0.0f, 0.0f });
    std::vector<Axis3D> AccumBitangent(VertexCount, Axis3D{ 0.0f, 0.0f, 0.0f });

    struct RawTriangle
    {
        uint32_t V[3]          {};
        UVAxes   UV[3]         {};
        uint16_t PartOrdinal   = 0u;
        uint32_t IslandOrdinal = 0u;
        uint16_t MatOrdinal    = 0u;
    };
    std::vector<RawTriangle> Tris;
    Tris.reserve(Network.Faces.size() * 2u);

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const Face& F = Network.Faces[FaceIdx];
        if (F.BoundaryLoopEdgeIndex == InvalidNetworkIndex || F.CornerCount < 3u)
        {
            continue;
        }
        std::vector<TriangulatedCorner> Loop;
        Loop.reserve(F.CornerCount);
        uint32_t Walk = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            const DirectedEdge& E = Network.Edges[Walk];
            Loop.push_back({ E.OriginVertexIndex, E.ParameterUV });
            Walk = E.NextEdgeIndex;
        }
        const uint32_t Island = (FaceIdx < FaceToIsland.size()) ? FaceToIsland[FaceIdx] : 0u;
        for (std::size_t Fan = 1u; Fan + 1u < Loop.size(); ++Fan)
        {
            RawTriangle RT {};
            RT.V[0] = Loop[0u].VertexIndex;
            RT.V[1] = Loop[Fan].VertexIndex;
            RT.V[2] = Loop[Fan + 1u].VertexIndex;
            RT.UV[0] = Loop[0u].Uv;
            RT.UV[1] = Loop[Fan].Uv;
            RT.UV[2] = Loop[Fan + 1u].Uv;
            RT.PartOrdinal   = F.PartGroupOrdinal;
            RT.IslandOrdinal = Island;
            RT.MatOrdinal    = F.PriorityLayerOrdinal;
            Tris.push_back(RT);

            const Axis3D P0 = Network.Vertices[RT.V[0]].Axes;
            const Axis3D P1 = Network.Vertices[RT.V[1]].Axes;
            const Axis3D P2 = Network.Vertices[RT.V[2]].Axes;
            const Axis3D E1 = P1 - P0;
            const Axis3D E2 = P2 - P0;
            const float Du1 = RT.UV[1].U - RT.UV[0].U;
            const float Dv1 = RT.UV[1].V - RT.UV[0].V;
            const float Du2 = RT.UV[2].U - RT.UV[0].U;
            const float Dv2 = RT.UV[2].V - RT.UV[0].V;
            const float Det = Du1 * Dv2 - Du2 * Dv1;
            if (std::fabs(Det) > 1.0e-10f)
            {
                const float Inv = 1.0f / Det;
                const Axis3D Tang {
                    (E1.X * Dv2 - E2.X * Dv1) * Inv,
                    (E1.Y * Dv2 - E2.Y * Dv1) * Inv,
                    (E1.Z * Dv2 - E2.Z * Dv1) * Inv,
                };
                const Axis3D Bitan {
                    (E2.X * Du1 - E1.X * Du2) * Inv,
                    (E2.Y * Du1 - E1.Y * Du2) * Inv,
                    (E2.Z * Du1 - E1.Z * Du2) * Inv,
                };
                for (int K = 0; K < 3; ++K)
                {
                    AccumTangent[RT.V[K]]   = AccumTangent[RT.V[K]] + Tang;
                    AccumBitangent[RT.V[K]] = AccumBitangent[RT.V[K]] + Bitan;
                }
            }
        }
    }

    OutTriCount = static_cast<uint32_t>(Tris.size());
    OutGpuVerts.reserve(Tris.size() * 3u);
    if (OutRichTris != nullptr) OutRichTris->reserve(Tris.size());
    if (OutBvhPrims != nullptr) OutBvhPrims->reserve(Tris.size());
    if (OutFlatPositions != nullptr) OutFlatPositions->reserve(Tris.size() * 3u);

    const float BaryCoords[3][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } };

    for (uint32_t TriIdx = 0u; TriIdx < static_cast<uint32_t>(Tris.size()); ++TriIdx)
    {
        const RawTriangle& RT = Tris[TriIdx];
        const Axis3D P0 = Network.Vertices[RT.V[0]].Axes;
        const Axis3D P1 = Network.Vertices[RT.V[1]].Axes;
        const Axis3D P2 = Network.Vertices[RT.V[2]].Axes;
        const Axis3D N0 = SafeNormalize(Network.Vertices[RT.V[0]].Normal);
        const Axis3D N1 = SafeNormalize(Network.Vertices[RT.V[1]].Normal);
        const Axis3D N2 = SafeNormalize(Network.Vertices[RT.V[2]].Normal);
        const Axis3D RawCross = EvaluateCross(P1 - P0, P2 - P0);
        const float  CrossLen = std::sqrt(RawCross.X * RawCross.X + RawCross.Y * RawCross.Y + RawCross.Z * RawCross.Z);
        const Axis3D GeomN = (CrossLen > 1.0e-10f)
            ? Axis3D{ RawCross.X / CrossLen, RawCross.Y / CrossLen, RawCross.Z / CrossLen }
            : N0;

        for (int C = 0; C < 3; ++C)
        {
            const uint32_t VIdx = RT.V[C];
            const Axis3D   Pos  = Network.Vertices[VIdx].Axes;
            const Axis3D   VN   = SafeNormalize(Network.Vertices[VIdx].Normal, GeomN);

            // Gram-Schmidt orthonormalization per vertex
            Axis3D FallbackT {};
            Axis3D FallbackB {};
            BuildFrisvadOrtho(VN, FallbackT, FallbackB);

            const Axis3D RawT = AccumTangent[VIdx];
            const float  NdotT = EvaluateDot(VN, RawT);
            const Axis3D OrthoT = SafeNormalize({ RawT.X - VN.X * NdotT, RawT.Y - VN.Y * NdotT, RawT.Z - VN.Z * NdotT }, FallbackT);
            const Axis3D OrthoB = SafeNormalize(EvaluateCross(VN, OrthoT), FallbackB);
            const float  Handed = (EvaluateDot(EvaluateCross(VN, OrthoT), AccumBitangent[VIdx]) < 0.0f) ? -1.0f : 1.0f;

            GpuLowSurfaceVertex GV {};
            GV.Position[0]     = Pos.X;
            GV.Position[1]     = Pos.Y;
            GV.Position[2]     = Pos.Z;
            GV.PartOrdinal     = static_cast<float>(RT.PartOrdinal);
            GV.SmoothNormal[0] = VN.X;
            GV.SmoothNormal[1] = VN.Y;
            GV.SmoothNormal[2] = VN.Z;
            GV.IslandOrdinal   = static_cast<float>(RT.IslandOrdinal);
            GV.Tangent[0]      = OrthoT.X;
            GV.Tangent[1]      = OrthoT.Y;
            GV.Tangent[2]      = OrthoT.Z;
            GV.Handedness      = Handed;
            GV.Bitangent[0]    = OrthoB.X;
            GV.Bitangent[1]    = OrthoB.Y;
            GV.Bitangent[2]    = OrthoB.Z;
            GV.UvCoordU        = RT.UV[C].U;
            GV.UvCoordV        = RT.UV[C].V;
            GV.BarycentricX    = BaryCoords[C][0];
            GV.BarycentricY    = BaryCoords[C][1];
            GV.WireMask        = 1.0f;
            OutGpuVerts.push_back(GV);
        }

        if (OutRichTris != nullptr)
        {
            GpuBakeTriangle GT {};
            GT.CornerA[0]         = P0.X; GT.CornerA[1] = P0.Y; GT.CornerA[2] = P0.Z;
            GT.CornerB[0]         = P1.X; GT.CornerB[1] = P1.Y; GT.CornerB[2] = P1.Z;
            GT.CornerC[0]         = P2.X; GT.CornerC[1] = P2.Y; GT.CornerC[2] = P2.Z;
            GT.PartOrdinal        = RT.PartOrdinal;
            GT.IslandOrdinal      = RT.IslandOrdinal;
            GT.MaterialOrdinal    = RT.MatOrdinal;
            GT.NormalA[0]         = N0.X; GT.NormalA[1] = N0.Y; GT.NormalA[2] = N0.Z;
            GT.AreaWeight         = CrossLen * 0.5f;
            GT.NormalB[0]         = N1.X; GT.NormalB[1] = N1.Y; GT.NormalB[2] = N1.Z;
            GT.NormalC[0]         = N2.X; GT.NormalC[1] = N2.Y; GT.NormalC[2] = N2.Z;
            GT.GeometricNormal[0] = GeomN.X; GT.GeometricNormal[1] = GeomN.Y; GT.GeometricNormal[2] = GeomN.Z;
            OutRichTris->push_back(GT);
        }

        if (OutBvhPrims != nullptr)
        {
            SpatialAcceleration::RayTrianglePrimitive BP {};
            BP.Corner0          = P0;
            BP.Edge01           = P1 - P0;
            BP.Edge02           = P2 - P0;
            BP.SourceFaceIndex  = TriIdx; // Exact [0..HighTriangleCount-1] alignment with GpuBakeTriangle & RTX BLAS!
            BP.MaterialOrdinal  = RT.PartOrdinal;
            BP.OpaqueMask       = 1u;
            BP.PrimitiveOrdinal = TriIdx;
            OutBvhPrims->push_back(BP);
        }

        if (OutFlatPositions != nullptr)
        {
            OutFlatPositions->push_back(P0);
            OutFlatPositions->push_back(P1);
            OutFlatPositions->push_back(P2);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             PUBLIC BUILDER IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

PreparedStudyPair StudyGeometryBuilder::ConstructStudioStudy(StudyGeometryCategory Kind)
{
    PolygonNetwork LowNet {};
    PolygonNetwork HighNet {};
    std::string Label {};

    switch (Kind)
    {
        case StudyGeometryCategory::UtahTeapot:
            Label = "Utah Teapot (High/Low)";
            AppendUtahTeapotStudy(LowNet, false);
            AppendUtahTeapotStudy(HighNet, true);
            break;

        case StudyGeometryCategory::ShaderBall:
            Label = "Shader Ball (High/Low)";
            AppendShaderBallStudy(LowNet, false);
            AppendShaderBallStudy(HighNet, true);
            break;

        case StudyGeometryCategory::ChamferBox:
            Label = "Chamfer Box (High/Low)";
            AppendSculptedBox(LowNet,  { 0.0f, 0.0f, 0.55f }, { 0.55f, 0.55f, 0.55f }, 4u, false, 0u);
            AppendSculptedBox(HighNet, { 0.0f, 0.0f, 0.55f }, { 0.55f, 0.55f, 0.55f }, 28u, true, 0u);
            break;

        case StudyGeometryCategory::UvSphere:
            Label = "UV Sphere (High/Low)";
            AppendSculptedUvSphere(LowNet,  { 0.0f, 0.0f, 0.62f }, 0.62f, 32u, 20u, { { 0.02f, 0.02f }, { 0.98f, 0.98f } }, false, 0u);
            AppendSculptedUvSphere(HighNet, { 0.0f, 0.0f, 0.62f }, 0.62f, 72u, 48u, { { 0.02f, 0.02f }, { 0.98f, 0.98f } }, true,  0u);
            break;

        case StudyGeometryCategory::TorusKnot:
            Label = "Torus Knot Study";
            AppendTorusKnotStudy(LowNet, false);
            AppendTorusKnotStudy(HighNet, true);
            break;
    }

    LowNet.NetworkTitle  = Label + " [Low]";
    HighNet.NetworkTitle = Label + " [High]";
    return PrepareFromPolygonNetworks(std::move(LowNet), std::move(HighNet), std::move(Label));
}

bool StudyGeometryBuilder::ImportPolygonNetworkFromFile(
    const std::filesystem::path&   FilePath,
    PolygonNetwork&                OutNetwork,
    bool                           NormalizeUnitPedestal,
    std::string*                   OutDiagnostic)
{
    std::string Ext = FilePath.extension().string();
    std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });

    bool Loaded = false;
    if (Ext == ".obj")
    {
        Loaded = ParseWavefrontObj(FilePath, OutNetwork, OutDiagnostic);
    }
    else if (Ext == ".stl")
    {
        Loaded = ParseStlFile(FilePath, OutNetwork, OutDiagnostic);
    }
    else
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "Unsupported study format (use .obj or .stl): " + FilePath.string();
        return false;
    }

    if (!Loaded)
    {
        return false;
    }

    if (NormalizeUnitPedestal)
    {
        NormalizeNetworkToStudioPedestal(OutNetwork);
    }
    OutNetwork.RecomputeNormals();
    OutNetwork.LinkOppositeEdges(true);
    AssignBoxAutoUVsIfDegenerate(OutNetwork);
    OutNetwork.RecomputeNormals();
    return true;
}

PreparedStudyPair StudyGeometryBuilder::PrepareFromPolygonNetworks(
    PolygonNetwork LowNetwork,
    PolygonNetwork HighNetwork,
    std::string    Label)
{
    PreparedStudyPair Result {};
    Result.StudyDisplayLabel = std::move(Label);

    LowNetwork.LinkOppositeEdges(true);
    LowNetwork.RecomputeNormals();
    AssignBoxAutoUVsIfDegenerate(LowNetwork);

    HighNetwork.LinkOppositeEdges(true);
    HighNetwork.RecomputeNormals();

    // True seam-bounded UV-island connectivity BFS flood-fill via GeometrySpace::ExtractSeamBoundedIslands
    const std::vector<ExtractedUVIsland> LowIslands = ExtractSeamBoundedIslands(LowNetwork);
    std::vector<uint32_t> LowFaceToIsland(LowNetwork.Faces.size(), 0u);
    for (const ExtractedUVIsland& Isl : LowIslands)
    {
        for (uint32_t FIdx : Isl.FaceIndices)
        {
            if (FIdx < LowFaceToIsland.size())
            {
                LowFaceToIsland[FIdx] = Isl.IslandOrdinal;
            }
        }
    }
    Result.UvIslandCount = static_cast<uint32_t>(std::max<std::size_t>(1u, LowIslands.size()));

    std::vector<uint32_t> HighFaceToIsland(HighNetwork.Faces.size(), 0u);
    uint16_t MaxPart = 0u;
    for (const Face& F : LowNetwork.Faces)
    {
        MaxPart = std::max(MaxPart, F.PartGroupOrdinal);
    }
    for (const Face& F : HighNetwork.Faces)
    {
        MaxPart = std::max(MaxPart, F.PartGroupOrdinal);
    }
    Result.PartGroupCount = static_cast<uint32_t>(MaxPart) + 1u;

    ExtractNetworkTriangulation(
        LowNetwork,
        LowFaceToIsland,
        Result.LowSurfaceVertices,
        nullptr,
        nullptr,
        nullptr,
        Result.LowTriangleCount);

    ExtractNetworkTriangulation(
        HighNetwork,
        HighFaceToIsland,
        Result.HighWireVertices,
        &Result.HighTriangles,
        &Result.HighBvhPrimitives,
        &Result.HighPositionsFlat,
        Result.HighTriangleCount);

    Axis3D BMin { 1.0e30f, 1.0e30f, 1.0e30f };
    Axis3D BMax { -1.0e30f, -1.0e30f, -1.0e30f };
    const auto ExpandVerts = [&](const PolygonNetwork& Net)
    {
        for (const Vertex& V : Net.Vertices)
        {
            BMin.X = std::min(BMin.X, V.Axes.X);
            BMin.Y = std::min(BMin.Y, V.Axes.Y);
            BMin.Z = std::min(BMin.Z, V.Axes.Z);
            BMax.X = std::max(BMax.X, V.Axes.X);
            BMax.Y = std::max(BMax.Y, V.Axes.Y);
            BMax.Z = std::max(BMax.Z, V.Axes.Z);
        }
    };
    ExpandVerts(LowNetwork);
    ExpandVerts(HighNetwork);
    if (BMin.X > BMax.X)
    {
        BMin = { -0.5f, -0.5f, 0.0f };
        BMax = {  0.5f,  0.5f, 1.0f };
    }
    Result.BoundsMin        = BMin;
    Result.BoundsMax        = BMax;
    const Axis3D Span       = BMax - BMin;
    Result.BoundsDiagMeters = std::max(0.05f, std::sqrt(Span.X * Span.X + Span.Y * Span.Y + Span.Z * Span.Z));

    Result.LowNetwork  = std::move(LowNetwork);
    Result.HighNetwork = std::move(HighNetwork);
    return Result;
}

} // namespace Frontier::GeometricRaster::Baking
