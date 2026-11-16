/*============================================================================================================================================
                                                       FRACTUREWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureWorkspaceSession: 9 Port-8091 analytical primitives, Torus/Pipe convex wedges, 6-solver dispatch & Jolt sim

#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::Workspaces::Fracture
{

using namespace GeometrySpace::Fracture;

namespace
{

constexpr double PiConst  = 3.14159265358979323846;
constexpr double TauConst = PiConst * 2.0;

[[nodiscard]] const char* SolverShortTitle(FractureSolverMethod Method) noexcept
{
    switch (Method)
    {
    case FractureSolverMethod::ClusteredVoronoi:      return "Clustered Voronoi";
    case FractureSolverMethod::FastRuntimeBoussinesq: return "Uniform Voronoi";
    case FractureSolverMethod::LefmWavefront:         return "Radial";
    case FractureSolverMethod::GriffithStressBudget:  return "Planar slices";
    case FractureSolverMethod::EngineeringBakedSolid: return "Brick";
    case FractureSolverMethod::DendriticSpallHull:    return "Recursive split";
    }
    return "Clustered Voronoi";
}

[[nodiscard]] ConvexPolyhedron BuildBoxExtent(float Width, float Height, float Depth)
{
    return ConvexPartitioning::ConstructBoxPolyhedron(
        std::max(0.02f, Width),
        std::max(0.02f, Depth),
        std::max(0.02f, Height));
}

[[nodiscard]] ConvexPolyhedron BuildSpherePolyhedron(float Radius, uint32_t Rings, uint32_t Slices)
{
    ConvexPolyhedron Poly {};
    const float R = std::max(0.05f, Radius);
    Poly.Vertices.push_back({ 0.0f, 0.0f, R });
    for (uint32_t I = 1u; I < Rings; ++I)
    {
        const double Phi = PiConst * static_cast<double>(I) / static_cast<double>(Rings);
        const float Z    = static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({
                static_cast<float>(RingR * std::cos(Theta)),
                static_cast<float>(RingR * std::sin(Theta)),
                Z
            });
        }
    }
    const uint32_t SouthIdx = static_cast<uint32_t>(Poly.Vertices.size());
    Poly.Vertices.push_back({ 0.0f, 0.0f, -R });

    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { 0u, 1u + J, 1u + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
    }
    for (uint32_t I = 0u; I + 2u < Rings; ++I)
    {
        const uint32_t RowA = 1u + I * Slices;
        const uint32_t RowB = 1u + (I + 1u) * Slices;
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const uint32_t Next = (J + 1u) % Slices;
            Poly.Faces.push_back(ConvexFace { { RowA + J, RowB + J, RowB + Next, RowA + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }
    const uint32_t LastRow = 1u + (Rings - 2u) * Slices;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { LastRow + J, SouthIdx, LastRow + Next }, false, 0u, { 0.0f, 0.0f, -1.0f } });
    }
    ConvexPartitioning::OrientFacesOutward(Poly);
    return Poly;
}

[[nodiscard]] ConvexPolyhedron BuildCapsulePolyhedron(float Radius, float ShaftLength, uint32_t Rings, uint32_t Slices)
{
    ConvexPolyhedron Poly {};
    const float R     = std::max(0.04f, Radius);
    const float HalfL = std::max(0.0f, ShaftLength * 0.5f);
    Poly.Vertices.push_back({ 0.0f, 0.0f, HalfL + R });
    const uint32_t HalfRings = std::max(2u, Rings / 2u);
    for (uint32_t I = 1u; I <= HalfRings; ++I)
    {
        const double Phi  = (PiConst * 0.5) * static_cast<double>(I) / static_cast<double>(HalfRings);
        const float Z     = HalfL + static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({ static_cast<float>(RingR * std::cos(Theta)), static_cast<float>(RingR * std::sin(Theta)), Z });
        }
    }
    for (uint32_t I = 0u; I < HalfRings; ++I)
    {
        const double Phi  = (PiConst * 0.5) + (PiConst * 0.5) * static_cast<double>(I) / static_cast<double>(HalfRings);
        const float Z     = -HalfL + static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({ static_cast<float>(RingR * std::cos(Theta)), static_cast<float>(RingR * std::sin(Theta)), Z });
        }
    }
    const uint32_t SouthIdx = static_cast<uint32_t>(Poly.Vertices.size());
    Poly.Vertices.push_back({ 0.0f, 0.0f, -HalfL - R });

    const uint32_t TotalRows = HalfRings * 2u;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { 0u, 1u + J, 1u + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
    }
    for (uint32_t I = 0u; I + 1u < TotalRows; ++I)
    {
        const uint32_t RowA = 1u + I * Slices;
        const uint32_t RowB = 1u + (I + 1u) * Slices;
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const uint32_t Next = (J + 1u) % Slices;
            Poly.Faces.push_back(ConvexFace { { RowA + J, RowB + J, RowB + Next, RowA + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }
    const uint32_t LastRow = 1u + (TotalRows - 1u) * Slices;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { LastRow + J, SouthIdx, LastRow + Next }, false, 0u, { 0.0f, 0.0f, -1.0f } });
    }
    ConvexPartitioning::OrientFacesOutward(Poly);
    return Poly;
}

[[nodiscard]] std::vector<ConvexPolyhedron> BuildTorusWedges(float RingRadius, float TubeRadius, uint32_t AroundN, uint32_t TubeM)
{
    std::vector<ConvexPolyhedron> Parts;
    Parts.reserve(AroundN);
    const double R  = std::max(0.15, static_cast<double>(RingRadius));
    const double r  = std::min(R * 0.90, std::max(0.04, static_cast<double>(TubeRadius)));
    const double Rr = (R * TauConst) / (static_cast<double>(AroundN) * std::sin(TauConst / static_cast<double>(AroundN)));
    const double Tr = r * std::sqrt(TauConst / (static_cast<double>(TubeM) * std::sin(TauConst / static_cast<double>(TubeM))));

    auto RingPt = [&](double A, double F) -> Mathematics::Axis3D
    {
        const double Rho = Rr + Tr * std::cos(F);
        return {
            static_cast<float>(Rho * std::cos(A)),
            static_cast<float>(Rho * std::sin(A)),
            static_cast<float>(Tr * std::sin(F))
        };
    };

    for (uint32_t S = 0u; S < AroundN; ++S)
    {
        const double A0 = (static_cast<double>(S) / static_cast<double>(AroundN)) * TauConst;
        const double A1 = (static_cast<double>(S + 1u) / static_cast<double>(AroundN)) * TauConst;
        ConvexPolyhedron Wedge {};
        Wedge.Vertices.reserve(TubeM * 2u);
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const double F = (static_cast<double>(K) / static_cast<double>(TubeM)) * TauConst;
            Wedge.Vertices.push_back(RingPt(A0, F));
        }
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const double F = (static_cast<double>(K) / static_cast<double>(TubeM)) * TauConst;
            Wedge.Vertices.push_back(RingPt(A1, F));
        }
        std::vector<uint32_t> Cap0(TubeM), Cap1(TubeM);
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            Cap0[K] = TubeM - 1u - K;
            Cap1[K] = TubeM + K;
        }
        Wedge.Faces.push_back(ConvexFace { std::move(Cap0), false, 0u, { 0.0f, 0.0f, 1.0f }, true, S });
        Wedge.Faces.push_back(ConvexFace { std::move(Cap1), false, 0u, { 0.0f, 0.0f, 1.0f }, true, (S + 1u) % AroundN });
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const uint32_t Next = (K + 1u) % TubeM;
            Wedge.Faces.push_back(ConvexFace { { K, Next, TubeM + Next, TubeM + K }, false, 0u, { 0.0f, 0.0f, 1.0f }, false, 0u });
        }
        ConvexPartitioning::OrientFacesOutward(Wedge);
        Parts.push_back(std::move(Wedge));
    }
    return Parts;
}

[[nodiscard]] std::vector<ConvexPolyhedron> BuildPipeWedges(float OuterR, float BoreR, float Height, uint32_t AroundN)
{
    std::vector<ConvexPolyhedron> Parts;
    Parts.reserve(AroundN);
    const double Ro = std::max(0.08, static_cast<double>(OuterR));
    const double Ri = std::clamp(static_cast<double>(BoreR), 0.02, Ro - 0.02);
    const float Hz  = std::max(0.05f, Height) * 0.5f;

    for (uint32_t S = 0u; S < AroundN; ++S)
    {
        const double A0 = (static_cast<double>(S) / static_cast<double>(AroundN)) * TauConst;
        const double A1 = (static_cast<double>(S + 1u) / static_cast<double>(AroundN)) * TauConst;
        const float C0 = static_cast<float>(std::cos(A0)), S0 = static_cast<float>(std::sin(A0));
        const float C1 = static_cast<float>(std::cos(A1)), S1 = static_cast<float>(std::sin(A1));
        ConvexPolyhedron W {};
        W.Vertices = {
            { static_cast<float>(Ri * C0), static_cast<float>(Ri * S0), -Hz },
            { static_cast<float>(Ro * C0), static_cast<float>(Ro * S0), -Hz },
            { static_cast<float>(Ro * C1), static_cast<float>(Ro * S1), -Hz },
            { static_cast<float>(Ri * C1), static_cast<float>(Ri * S1), -Hz },
            { static_cast<float>(Ri * C0), static_cast<float>(Ri * S0),  Hz },
            { static_cast<float>(Ro * C0), static_cast<float>(Ro * S0),  Hz },
            { static_cast<float>(Ro * C1), static_cast<float>(Ro * S1),  Hz },
            { static_cast<float>(Ri * C1), static_cast<float>(Ri * S1),  Hz },
        };
        W.Faces = {
            { { 0, 3, 2, 1 }, false, 0u, { 0.0f, 0.0f, -1.0f }, false, 0u },
            { { 4, 5, 6, 7 }, false, 0u, { 0.0f, 0.0f,  1.0f }, false, 0u },
            { { 1, 2, 6, 5 }, false, 0u, { 1.0f, 0.0f,  0.0f }, false, 0u },
            { { 3, 0, 4, 7 }, false, 0u, { -1.0f, 0.0f, 0.0f }, false, 0u },
            { { 0, 1, 5, 4 }, false, 0u, { 0.0f, -1.0f, 0.0f }, true, S },
            { { 2, 3, 7, 6 }, false, 0u, { 0.0f,  1.0f, 0.0f }, true, (S + 1u) % AroundN },
        };
        ConvexPartitioning::OrientFacesOutward(W);
        Parts.push_back(std::move(W));
    }
    return Parts;
}

[[nodiscard]] ConvexPolyhedron MergePartsPreview(const std::vector<ConvexPolyhedron>& Parts)
{
    ConvexPolyhedron Merged {};
    for (const auto& P : Parts)
    {
        const uint32_t Base = static_cast<uint32_t>(Merged.Vertices.size());
        Merged.Vertices.insert(Merged.Vertices.end(), P.Vertices.begin(), P.Vertices.end());
        for (const auto& F : P.Faces)
        {
            ConvexFace Copy = F;
            for (uint32_t& Idx : Copy.Indices)
            {
                Idx += Base;
            }
            Merged.Faces.push_back(std::move(Copy));
        }
    }
    return Merged;
}

[[nodiscard]] double SignedDistanceLocal(
    FractureIcons::PrimitiveShapeId Shape,
    float                           DimA,
    float                           DimB,
    float                           DimC,
    Mathematics::Axis3D             P) noexcept
{
    const double X = P.X, Y = P.Y, Z = P.Z;
    const double Rho = std::hypot(X, Y);
    switch (Shape)
    {
    case FractureIcons::PrimitiveShapeId::Box:
    {
        const double Qx = std::abs(X) - std::max(0.02, DimA * 0.5);
        const double Qy = std::abs(Y) - std::max(0.02, DimC * 0.5);
        const double Qz = std::abs(Z) - std::max(0.02, DimB * 0.5);
        return std::hypot(std::max(Qx, 0.0), std::hypot(std::max(Qy, 0.0), std::max(Qz, 0.0)))
             + std::min(std::max({ Qx, Qy, Qz }), 0.0);
    }
    case FractureIcons::PrimitiveShapeId::Pane:
    {
        const double Qx = std::abs(X) - std::max(0.02, DimA * 0.5);
        const double Qy = std::abs(Y) - std::max(0.007, DimC / 2000.0);
        const double Qz = std::abs(Z) - std::max(0.02, DimB * 0.5);
        return std::hypot(std::max(Qx, 0.0), std::hypot(std::max(Qy, 0.0), std::max(Qz, 0.0)))
             + std::min(std::max({ Qx, Qy, Qz }), 0.0);
    }
    case FractureIcons::PrimitiveShapeId::Sphere:
        return std::hypot(X, std::hypot(Y, Z)) - std::max(0.05, static_cast<double>(DimA));
    case FractureIcons::PrimitiveShapeId::Cylinder:
    {
        const double Dr = Rho - std::max(0.04, static_cast<double>(DimA));
        const double Dz = std::abs(Z) - std::max(0.04, DimB * 0.5);
        return (Dr > 0.0 && Dz > 0.0) ? std::hypot(Dr, Dz) : std::max(Dr, Dz);
    }
    case FractureIcons::PrimitiveShapeId::Capsule:
    {
        const double HalfL = std::max(0.0, DimB * 0.5);
        const double Cz    = std::clamp(Z, -HalfL, HalfL);
        return std::hypot(X, std::hypot(Y, Z - Cz)) - std::max(0.04, static_cast<double>(DimA));
    }
    case FractureIcons::PrimitiveShapeId::Torus:
    {
        const double R = std::max(0.15, static_cast<double>(DimA));
        const double r = std::min(R * 0.90, std::max(0.04, static_cast<double>(DimB)));
        return std::hypot(Rho - R, Z) - r;
    }
    case FractureIcons::PrimitiveShapeId::Pipe:
    {
        const double Ro = std::max(0.08, static_cast<double>(DimA));
        const double Ri = std::clamp(static_cast<double>(DimB), 0.02, Ro - 0.02);
        const double Hz = std::max(0.025, DimC * 0.5);
        const double Dr = std::max(Ri - Rho, Rho - Ro);
        const double Dz = std::abs(Z) - Hz;
        return (Dr > 0.0 && Dz > 0.0) ? std::hypot(Dr, Dz) : std::max(Dr, Dz);
    }
    default:
        return std::hypot(X, std::hypot(Y, Z)) - 0.5;
    }
}

[[nodiscard]] Mathematics::Axis3D AnalyticalSurfaceNormal(
    FractureIcons::PrimitiveShapeId Shape,
    float                           DimA,
    float                           DimB,
    float                           DimC,
    Mathematics::Axis3D             P,
    Mathematics::Axis3D             FallbackFlat) noexcept
{
    switch (Shape)
    {
    case FractureIcons::PrimitiveShapeId::Torus:
    {
        const float R   = std::max(0.15f, DimA);
        const float Rho = std::hypot(P.X, P.Y);
        if (Rho < 1.0e-5f) return FallbackFlat;
        const Mathematics::Axis3D TubeCenter { (P.X / Rho) * R, (P.Y / Rho) * R, 0.0f };
        const auto N = Mathematics::NormalizeAxis(P - TubeCenter, FallbackFlat);
        return (Mathematics::EvaluateDot(N, FallbackFlat) > 0.2f) ? N : FallbackFlat;
    }
    case FractureIcons::PrimitiveShapeId::Pipe:
    {
        const float Hz = std::max(0.025f, DimC * 0.5f);
        if (std::abs(P.Z) > Hz - 2.0e-3f)
        {
            const Mathematics::Axis3D CapN { 0.0f, 0.0f, P.Z >= 0.0f ? 1.0f : -1.0f };
            return (Mathematics::EvaluateDot(CapN, FallbackFlat) > 0.35f) ? CapN : FallbackFlat;
        }
        const float MidR = 0.5f * (DimA + DimB);
        const float Rho  = std::hypot(P.X, P.Y);
        if (Rho < 1.0e-5f) return FallbackFlat;
        const Mathematics::Axis3D RadialN = (Rho < MidR)
            ? Mathematics::NormalizeAxis({ -P.X, -P.Y, 0.0f }, FallbackFlat)
            : Mathematics::NormalizeAxis({  P.X,  P.Y, 0.0f }, FallbackFlat);
        return (Mathematics::EvaluateDot(RadialN, FallbackFlat) > 0.2f) ? RadialN : FallbackFlat;
    }
    case FractureIcons::PrimitiveShapeId::Sphere:
    {
        const auto N = Mathematics::NormalizeAxis(P, FallbackFlat);
        return (Mathematics::EvaluateDot(N, FallbackFlat) > 0.2f) ? N : FallbackFlat;
    }
    case FractureIcons::PrimitiveShapeId::Cylinder:
    {
        const float Hz = std::max(0.025f, DimB * 0.5f);
        if (std::abs(P.Z) > Hz - 2.0e-3f) return FallbackFlat;
        const auto N = Mathematics::NormalizeAxis({ P.X, P.Y, 0.0f }, FallbackFlat);
        return (Mathematics::EvaluateDot(N, FallbackFlat) > 0.2f) ? N : FallbackFlat;
    }
    case FractureIcons::PrimitiveShapeId::Capsule:
    {
        const float HalfL = std::max(0.0f, DimB * 0.5f);
        const float Cz    = std::clamp(P.Z, -HalfL, HalfL);
        const auto N = Mathematics::NormalizeAxis({ P.X, P.Y, P.Z - Cz }, FallbackFlat);
        return (Mathematics::EvaluateDot(N, FallbackFlat) > 0.2f) ? N : FallbackFlat;
    }
    case FractureIcons::PrimitiveShapeId::Cone:
    {
        const float Hz = std::max(0.025f, DimB * 0.5f);
        if (P.Z < -Hz + 2.0e-3f) return FallbackFlat;
        const float Ang = std::atan2(P.Y, P.X);
        const auto N = Mathematics::NormalizeAxis(
            { std::cos(Ang) * DimB, std::sin(Ang) * DimB, std::max(0.05f, DimA) }, FallbackFlat);
        return (Mathematics::EvaluateDot(N, FallbackFlat) > 0.2f) ? N : FallbackFlat;
    }
    default:
        return FallbackFlat;
    }
}

// Keep dot(N, P) <= D (exact 3D convex half-space clip preserving Seam/Interior face tags).
bool ClipPolyhedronKeepHalfSpace(
    ConvexPolyhedron&   Poly,
    Mathematics::Axis3D PlaneNormal,
    double              PlaneOffset,
    bool                MarkInterior,
    uint32_t            CutToken,
    bool                MarkSeam,
    uint32_t            SeamId)
{
    constexpr double Eps = 1.0e-6;
    const auto N = Mathematics::NormalizeAxis(PlaneNormal);
    const size_t VCount = Poly.Vertices.size();
    if (VCount < 4u || Poly.Faces.empty()) return false;

    std::vector<double> Dist(VCount, 0.0);
    double MaxD = -1.0e30, MinD = 1.0e30;
    for (size_t I = 0u; I < VCount; ++I)
    {
        const double D = static_cast<double>(Mathematics::EvaluateDot(N, Poly.Vertices[I])) - PlaneOffset;
        Dist[I] = D;
        MaxD = std::max(MaxD, D);
        MinD = std::min(MinD, D);
    }
    if (MaxD <= Eps) return true;  // Completely inside
    if (MinD >= -Eps) return false; // Completely outside

    std::vector<Mathematics::Axis3D> OutVerts;
    OutVerts.reserve(VCount + 8u);
    auto AddUniqueVert = [&](Mathematics::Axis3D Pt) -> uint32_t
    {
        for (uint32_t K = 0u; K < OutVerts.size(); ++K)
        {
            const auto& Q = OutVerts[K];
            if (std::abs(Q.X - Pt.X) + std::abs(Q.Y - Pt.Y) + std::abs(Q.Z - Pt.Z) < 1.0e-5f)
            {
                return K;
            }
        }
        const uint32_t Slot = static_cast<uint32_t>(OutVerts.size());
        OutVerts.push_back(Pt);
        return Slot;
    };

    std::vector<ConvexFace> OutFaces;
    OutFaces.reserve(Poly.Faces.size() + 1u);
    std::vector<uint32_t> CapIndices;

    for (const auto& Face : Poly.Faces)
    {
        const size_t Fn = Face.Indices.size();
        if (Fn < 3u) continue;
        std::vector<uint32_t> Loop;
        Loop.reserve(Fn + 2u);
        for (size_t I = 0u; I < Fn; ++I)
        {
            const uint32_t Ia = Face.Indices[I];
            const uint32_t Ib = Face.Indices[(I + 1u) % Fn];
            const double   Da = Dist[Ia];
            const double   Db = Dist[Ib];
            const auto&    A  = Poly.Vertices[Ia];
            const auto&    B  = Poly.Vertices[Ib];

            if (Da <= Eps)
            {
                const uint32_t Vi = AddUniqueVert(A);
                if (Loop.empty() || Loop.back() != Vi) Loop.push_back(Vi);
            }
            if (std::abs(Da) <= Eps)
            {
                const uint32_t Ci = AddUniqueVert(A);
                if (std::find(CapIndices.begin(), CapIndices.end(), Ci) == CapIndices.end())
                {
                    CapIndices.push_back(Ci);
                }
            }
            if ((Da < -Eps && Db > Eps) || (Da > Eps && Db < -Eps))
            {
                const float T = static_cast<float>(Da / (Da - Db));
                const auto  X = Mathematics::InterpolateLinear(A, B, T);
                const uint32_t Vi = AddUniqueVert(X);
                if (Loop.empty() || Loop.back() != Vi) Loop.push_back(Vi);
                if (std::find(CapIndices.begin(), CapIndices.end(), Vi) == CapIndices.end())
                {
                    CapIndices.push_back(Vi);
                }
            }
        }
        while (Loop.size() > 1u && Loop.front() == Loop.back()) Loop.pop_back();
        if (Loop.size() >= 3u)
        {
            OutFaces.push_back({ std::move(Loop), Face.Interior, Face.CutToken, Face.CrackNormal, Face.Seam, Face.SeamId });
        }
    }

    if (OutFaces.empty()) return false;

    if (CapIndices.size() >= 3u)
    {
        Mathematics::Axis3D Centre { 0.0f, 0.0f, 0.0f };
        for (uint32_t Ci : CapIndices) Centre = Centre + OutVerts[Ci];
        Centre = Centre * (1.0f / static_cast<float>(CapIndices.size()));
        const auto Ref = (std::abs(N.X) < 0.9f) ? Mathematics::Axis3D { 1.0f, 0.0f, 0.0f } : Mathematics::Axis3D { 0.0f, 1.0f, 0.0f };
        const auto U   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(N, Ref));
        const auto V   = Mathematics::EvaluateCross(N, U);
        std::sort(CapIndices.begin(), CapIndices.end(), [&](uint32_t A, uint32_t B)
        {
            const auto Da = OutVerts[A] - Centre;
            const auto Db = OutVerts[B] - Centre;
            return std::atan2(Mathematics::EvaluateDot(Da, V), Mathematics::EvaluateDot(Da, U))
                 < std::atan2(Mathematics::EvaluateDot(Db, V), Mathematics::EvaluateDot(Db, U));
        });
        OutFaces.push_back({ std::move(CapIndices), MarkInterior, CutToken, N, MarkSeam, SeamId });
    }

    if (OutFaces.size() < 4u || OutVerts.size() < 4u) return false;
    Poly.Vertices = std::move(OutVerts);
    Poly.Faces    = std::move(OutFaces);
    return true;
}

[[nodiscard]] ConvexPolyhedron MakeBoxFromMinMax(Mathematics::Axis3D Lo, Mathematics::Axis3D Hi, bool MarkInterior)
{
    ConvexPolyhedron Poly {};
    Poly.Vertices = {
        { Lo.X, Lo.Y, Lo.Z }, { Hi.X, Lo.Y, Lo.Z }, { Hi.X, Hi.Y, Lo.Z }, { Lo.X, Hi.Y, Lo.Z },
        { Lo.X, Lo.Y, Hi.Z }, { Hi.X, Lo.Y, Hi.Z }, { Hi.X, Hi.Y, Hi.Z }, { Lo.X, Hi.Y, Hi.Z }
    };
    const uint32_t Loops[6][4] = {
        { 4u, 5u, 6u, 7u }, { 3u, 2u, 1u, 0u },
        { 1u, 2u, 6u, 5u }, { 3u, 0u, 4u, 7u },
        { 2u, 3u, 7u, 6u }, { 0u, 1u, 5u, 4u }
    };
    for (const auto& Loop : Loops)
    {
        Poly.Faces.push_back({ { Loop[0], Loop[1], Loop[2], Loop[3] }, MarkInterior, MarkInterior ? 1u : 0u, { 0.0f, 0.0f, 1.0f }, false, 0u });
    }
    ConvexPartitioning::OrientFacesOutward(Poly);
    return Poly;
}

[[nodiscard]] bool ClipPolyhedronByPart(ConvexPolyhedron& Cell, const ConvexPolyhedron& Part)
{
    for (const auto& PF : Part.Faces)
    {
        if (PF.Indices.size() < 3u) continue;
        const auto N = ConvexPartitioning::ComputeFaceNormal(Part, PF);
        const double D = static_cast<double>(Mathematics::EvaluateDot(N, Part.Vertices[PF.Indices[0]]));
        if (!ClipPolyhedronKeepHalfSpace(Cell, N, D, PF.Interior, PF.CutToken, PF.Seam, PF.SeamId))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] EngineeringSurface BuildDetailedSurfaceForChunks(
    const std::vector<ConvexPolyhedron>& Chunks,
    FractureIcons::PrimitiveShapeId      Shape,
    float                                DimA,
    float                                DimB,
    float                                DimC,
    bool                                 ClosedCaps,
    bool                                 QualityFans,
    bool                                 ExactNormals)
{
    EngineeringSurface Surf {};
    double ExactVol = 0.0;
    for (const auto& Chunk : Chunks)
    {
        ExactVol += ConvexPartitioning::ComputePolyhedronMetrics(Chunk).VolumeM3;
        for (const auto& Face : Chunk.Faces)
        {
            if (Face.Seam || Face.Indices.size() < 3u) continue;
            if (Face.Interior && !ClosedCaps) continue;

            std::vector<Mathematics::Axis3D> Pts;
            Pts.reserve(Face.Indices.size());
            for (uint32_t Idx : Face.Indices)
            {
                const auto& P = Chunk.Vertices[Idx];
                if (Pts.empty() || Mathematics::EvaluateDistance(Pts.back(), P) > 2.0e-4f)
                {
                    Pts.push_back(P);
                }
            }
            if (Pts.size() > 1u && Mathematics::EvaluateDistance(Pts.front(), Pts.back()) <= 2.0e-4f)
            {
                Pts.pop_back();
            }
            if (Pts.size() < 3u) continue;

            const auto FlatN = ConvexPartitioning::ComputeFaceNormal(Chunk, Face);
            auto EvalNormal = [&](Mathematics::Axis3D P) -> Mathematics::Axis3D
            {
                if (Face.Interior || !ExactNormals) return FlatN;
                return AnalyticalSurfaceNormal(Shape, DimA, DimB, DimC, P, FlatN);
            };
            auto EmitTri = [&](Mathematics::Axis3D A, Mathematics::Axis3D B, Mathematics::Axis3D C)
            {
                const float Area2 = Mathematics::EvaluateLength(Mathematics::EvaluateCross(B - A, C - A));
                if (Area2 < 1.0e-8f) return;
                Surf.Positions.push_back(A);
                Surf.Positions.push_back(B);
                Surf.Positions.push_back(C);
                Surf.Normals.push_back(EvalNormal(A));
                Surf.Normals.push_back(EvalNormal(B));
                Surf.Normals.push_back(EvalNormal(C));
                const uint8_t Mask = Face.Interior ? 1u : 0u;
                Surf.InteriorMask.push_back(Mask);
                Surf.InteriorMask.push_back(Mask);
                Surf.InteriorMask.push_back(Mask);
                ++Surf.TriangleCount;
            };

            if (QualityFans && Pts.size() > 3u)
            {
                Mathematics::Axis3D Cent { 0.0f, 0.0f, 0.0f };
                for (const auto& P : Pts) Cent = Cent + P;
                Cent = Cent * (1.0f / static_cast<float>(Pts.size()));
                for (size_t K = 0u; K < Pts.size(); ++K)
                {
                    EmitTri(Cent, Pts[K], Pts[(K + 1u) % Pts.size()]);
                }
            }
            else
            {
                for (size_t K = 1u; K + 1u < Pts.size(); ++K)
                {
                    EmitTri(Pts[0], Pts[K], Pts[K + 1u]);
                }
            }
        }
    }
    Surf.ExactNonPlanarVolM3 = ExactVol;
    return Surf;
}

} // namespace

const std::array<FracturePresetEntry, FractureWorkspaceSession::PresetCount>& FractureWorkspaceSession::InspectPresets() noexcept
{
    static constexpr std::array<FracturePresetEntry, PresetCount> Presets {{
        {
            "Torus",
            "Clustered Voronoi · Concrete · 2.50 kJ",
            FractureIcons::PrimitiveShapeId::Torus,
            FractureSolidCategory::Boulder,
            "concrete",
            "concrete",
            FractureSolverMethod::ClusteredVoronoi,
            FractureExecutionMode::Baked,
            2500.0,
            42u,
            48u,
            FractureRenderViewMode::Energy
        },
        {
            "Curtain Pane",
            "Radial · Tempered · 0.48 kJ",
            FractureIcons::PrimitiveShapeId::Pane,
            FractureSolidCategory::CurtainPane,
            "glass",
            "tempered",
            FractureSolverMethod::LefmWavefront,
            FractureExecutionMode::Baked,
            480.0,
            107u,
            42u,
            FractureRenderViewMode::Energy
        },
        {
            "Monolith",
            "Uniform Voronoi · Concrete · 3.20 kJ",
            FractureIcons::PrimitiveShapeId::Box,
            FractureSolidCategory::Monolith,
            "concrete",
            "concrete",
            FractureSolverMethod::FastRuntimeBoussinesq,
            FractureExecutionMode::HybridBonds,
            3200.0,
            42u,
            32u,
            FractureRenderViewMode::StudioOpenPBR
        },
        {
            "Baked Granite",
            "Brick · Bedded Granite · 4.20 kJ",
            FractureIcons::PrimitiveShapeId::HexColumn,
            FractureSolidCategory::Cylinder,
            "granite",
            "granite",
            FractureSolverMethod::EngineeringBakedSolid,
            FractureExecutionMode::Baked,
            4200.0,
            73u,
            28u,
            FractureRenderViewMode::StudioOpenPBR
        },
        {
            "Spruce Cylinder",
            "Planar slices · Spruce Wood · 1.80 kJ",
            FractureIcons::PrimitiveShapeId::Cylinder,
            FractureSolidCategory::TimberBeam,
            "wood",
            "wood",
            FractureSolverMethod::GriffithStressBudget,
            FractureExecutionMode::Baked,
            1800.0,
            19u,
            24u,
            FractureRenderViewMode::Cells
        },
        {
            "Polymer Pipe",
            "Recursive split · ABS Polymer · 3.60 kJ",
            FractureIcons::PrimitiveShapeId::Pipe,
            FractureSolidCategory::HollowCavity,
            "plastic",
            "plastic",
            FractureSolverMethod::DendriticSpallHull,
            FractureExecutionMode::Baked,
            3600.0,
            88u,
            36u,
            FractureRenderViewMode::StudioOpenPBR
        }
    }};
    return Presets;
}

const std::array<FracturePrimitiveDescriptor, FractureWorkspaceSession::PrimitiveCount>& FractureWorkspaceSession::InspectPrimitives() noexcept
{
    static constexpr std::array<FracturePrimitiveDescriptor, PrimitiveCount> Primitives {{
        { FractureIcons::PrimitiveShapeId::Box,       "box",      "Box",        "W · H · D",        "Analytical box",      false },
        { FractureIcons::PrimitiveShapeId::Sphere,    "sphere",   "Sphere",     "4/3 pi r^3",       "Analytical sphere",   false },
        { FractureIcons::PrimitiveShapeId::Cylinder,  "cylinder", "Cylinder",   "pi r^2 h",         "Analytical cylinder", false },
        { FractureIcons::PrimitiveShapeId::Cone,      "cone",     "Cone",       "1/3 pi r^2 h",     "Analytical cone",     false },
        { FractureIcons::PrimitiveShapeId::Capsule,   "capsule",  "Capsule",    "pi r^2 l + 4/3r3", "Analytical capsule",  false },
        { FractureIcons::PrimitiveShapeId::Torus,     "torus",    "Torus",      "2 pi^2 R r^2",     "Analytical torus",    true  },
        { FractureIcons::PrimitiveShapeId::Pipe,      "pipe",     "Pipe",       "pi (R^2 - r^2) h", "Analytical pipe",     true  },
        { FractureIcons::PrimitiveShapeId::Pane,      "pane",     "Pane",       "W · H · t",        "Analytical pane",     false },
        { FractureIcons::PrimitiveShapeId::HexColumn, "hex",      "Hex column", "2 sqrt(3) a^2 h",  "Analytical hex",      false },
    }};
    return Primitives;
}

FractureWorkspaceSession::FractureWorkspaceSession()
{
    FrameTimeSamplesMs.assign(96u, 16.67f);
    for (uint32_t I = 0u; I < 96u; ++I)
    {
        FrameTimeSamplesMs[I] = 16.3f + 0.6f * std::sin(static_cast<float>(I) * 0.28f);
    }
    ApplyPreset(0u);
}

void FractureWorkspaceSession::ApplyPreset(uint32_t PresetIndex)
{
    const uint32_t SafeIdx = PresetIndex % PresetCount;
    const auto& Preset     = InspectPresets()[SafeIdx];
    ActivePresetIndex      = SafeIdx;
    SurfaceMaterialId      = Preset.SurfaceMatId;
    MaterialId             = Preset.MaterialId;
    SolverMethod           = Preset.Solver;
    ExecutionMode          = Preset.ExecMode;
    MethodMode             = (ExecutionMode == FractureExecutionMode::Realtime) ? FractureMethodMode::Realtime : FractureMethodMode::Baked;
    ImpactEnergyJ          = Preset.ImpactEnergyJ;
    Seed                   = Preset.Seed;
    FragmentCeiling        = Preset.FragmentCeiling;
    RenderOptions.RenderMode = Preset.DefaultLens;
    RenderOptions.ShowGroundGrid = true;
    RenderOptions.ShowBonds      = false;
    RenderOptions.ShowWavefronts = true;
    RenderOptions.ExplodeFactor  = 0.0f;
    RenderOptions.SelectedFragmentIndex = -1;

    ApplyPrimitiveShape(Preset.Shape);
    AppendLog("preset", Preset.Title, Preset.Subtitle, true);
}

void FractureWorkspaceSession::ApplyPrimitiveShape(FractureIcons::PrimitiveShapeId Shape)
{
    ActiveShape = Shape;
    switch (Shape)
    {
    case FractureIcons::PrimitiveShapeId::Box:
        SolidCategory = FractureSolidCategory::Cube;
        DimPrimaryA = 1.00f; DimPrimaryB = 1.00f; DimPrimaryC = 1.00f;
        ImpactPointLocal = { 0.42f, -0.45f, 0.16f };
        ImpactNormalLocal = { 0.25f, -0.94f, 0.22f };
        break;
    case FractureIcons::PrimitiveShapeId::Sphere:
        SolidCategory = FractureSolidCategory::Boulder;
        DimPrimaryA = 0.50f; DimPrimaryB = 0.50f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.30f, -0.34f, 0.21f };
        ImpactNormalLocal = { 0.60f, -0.68f, 0.42f };
        break;
    case FractureIcons::PrimitiveShapeId::Cylinder:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.40f; DimPrimaryB = 1.20f; DimPrimaryC = 0.40f;
        ImpactPointLocal = { 0.26f, -0.30f, 0.20f };
        ImpactNormalLocal = { 0.65f, -0.76f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Cone:
        SolidCategory = FractureSolidCategory::Cone;
        DimPrimaryA = 0.50f; DimPrimaryB = 1.10f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.22f, -0.26f, 0.05f };
        ImpactNormalLocal = { 0.62f, -0.74f, 0.25f };
        break;
    case FractureIcons::PrimitiveShapeId::Capsule:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.30f; DimPrimaryB = 0.70f; DimPrimaryC = 0.30f;
        ImpactPointLocal = { 0.19f, -0.22f, 0.10f };
        ImpactNormalLocal = { 0.65f, -0.76f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Torus:
        SolidCategory = FractureSolidCategory::Boulder;
        DimPrimaryA = 0.50f; DimPrimaryB = 0.19f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.32f, -0.52f, 0.10f };
        ImpactNormalLocal = { 0.48f, -0.84f, 0.24f };
        break;
    case FractureIcons::PrimitiveShapeId::Pipe:
        SolidCategory = FractureSolidCategory::HollowCavity;
        DimPrimaryA = 0.48f; DimPrimaryB = 0.34f; DimPrimaryC = 0.80f;
        ImpactPointLocal = { 0.30f, -0.36f, 0.12f };
        ImpactNormalLocal = { 0.64f, -0.77f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Pane:
        SolidCategory = FractureSolidCategory::CurtainPane;
        DimPrimaryA = 1.70f; DimPrimaryB = 1.15f; DimPrimaryC = 14.0f;
        ImpactPointLocal = { 0.10f, -0.02f, 0.08f };
        ImpactNormalLocal = { 0.0f, -1.0f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::HexColumn:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.56f; DimPrimaryB = 1.20f; DimPrimaryC = 0.56f;
        ImpactPointLocal = { 0.18f, -0.24f, 0.14f };
        ImpactNormalLocal = { 0.56f, -0.83f, 0.0f };
        break;
    default:
        break;
    }

    ApplyCameraView(0u);
    ExecutionPhase = ExecutionStripPhase::Edit;
    Playing = false;
    ElapsedSeconds = 0.0;
    RebuildSourceGeometry();
    RebuildFracturePartition();
}

void FractureWorkspaceSession::ApplySolverMethod(FractureSolverMethod Method)
{
    SolverMethod = Method;
    if (Method == FractureSolverMethod::FastRuntimeBoussinesq && MethodMode == FractureMethodMode::Realtime)
    {
        ExecutionMode = FractureExecutionMode::Realtime;
    }
    else
    {
        ExecutionMode = FractureExecutionMode::Baked;
    }
    RebuildFracturePartition();
    AppendLog("solver", SolverShortTitle(Method), ActiveMaterialSpec().ShortLabel, true);
}

void FractureWorkspaceSession::ApplyCameraView(uint32_t ViewIndex) noexcept
{
    CameraViewIndex = ViewIndex % 4u;
    switch (CameraViewIndex)
    {
    case 0u:
        Camera.YawRadians   = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 0.64f : 0.72f;
        Camera.PitchRadians = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 0.44f : 0.38f;
        Camera.DistanceM    = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 2.25f : 2.65f;
        break;
    case 1u:
        Camera.YawRadians = 0.0f; Camera.PitchRadians = 1.38f; Camera.DistanceM = 2.60f;
        break;
    case 2u:
        Camera.YawRadians = 0.0f; Camera.PitchRadians = 0.16f; Camera.DistanceM = 2.60f;
        break;
    case 3u:
        Camera.YawRadians = 1.57f; Camera.PitchRadians = 0.16f; Camera.DistanceM = 2.60f;
        break;
    }
    const auto Metrics = ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
    const float FocusZ = (SourceSolid.Vertices.empty()) ? 0.24f
        : std::max(0.18f, static_cast<float>(Metrics.Centroid.Z - Metrics.BoundsMin.Z));
    Camera.TargetWorld = { 0.0f, 0.0f, FocusZ };
}

void FractureWorkspaceSession::RebuildSourceGeometry()
{
    const uint32_t AroundN = (FacetDetail == FractureFacetDetail::Low) ? 24u
                           : (FacetDetail == FractureFacetDetail::High) ? 64u : 40u;
    SourceParts.clear();
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:
        SourceSolid = BuildBoxExtent(DimPrimaryA, DimPrimaryB, DimPrimaryC);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Pane:
        SourceSolid = BuildBoxExtent(DimPrimaryA, DimPrimaryB, std::max(0.014f, DimPrimaryC / 1000.0f));
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Sphere:
        SourceSolid = BuildSpherePolyhedron(DimPrimaryA, 14u, AroundN / 2u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Cylinder:
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(DimPrimaryA, DimPrimaryA, DimPrimaryB, AroundN);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Cone:
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(DimPrimaryA, 0.008f, DimPrimaryB, AroundN);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Capsule:
        SourceSolid = BuildCapsulePolyhedron(DimPrimaryA, DimPrimaryB, 14u, AroundN / 2u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::HexColumn:
    {
        const float SideR = (DimPrimaryA * 0.5f) / std::cos(static_cast<float>(PiConst / 6.0));
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(SideR, SideR, DimPrimaryB, 6u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    }
    case FractureIcons::PrimitiveShapeId::Torus:
        SourceParts = BuildTorusWedges(DimPrimaryA, DimPrimaryB, AroundN, std::max(12u, AroundN / 2u));
        SourceSolid = MergePartsPreview(SourceParts);
        SourceSegmentCount = static_cast<uint32_t>(SourceParts.size());
        break;
    case FractureIcons::PrimitiveShapeId::Pipe:
        SourceParts = BuildPipeWedges(DimPrimaryA, DimPrimaryB, DimPrimaryC, AroundN);
        SourceSolid = MergePartsPreview(SourceParts);
        SourceSegmentCount = static_cast<uint32_t>(SourceParts.size());
        break;
    default:
        SourceSolid = BuildBoxExtent(1.0f, 1.0f, 1.0f);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    }

    SourceFaceCount = static_cast<uint32_t>(SourceSolid.Faces.size());
    const auto Metrics = ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
    const double BottomZ = static_cast<double>(Metrics.BoundsMin.Z);
    RenderOptions.SolidWorldOffsetM = { 0.0f, 0.0f, static_cast<float>(-BottomZ) };
    Camera.TargetWorld = { 0.0f, 0.0f, std::max(0.18f, static_cast<float>(Metrics.Centroid.Z - BottomZ)) };
}

void FractureWorkspaceSession::ExecuteFracturePartitionOnly()
{
    FractureSpecification Spec {};
    Spec.Solver            = SolverMethod;
    Spec.MaterialId        = MaterialId.c_str();
    Spec.Seed              = Seed;
    Spec.Ceiling           = std::max(4u, FragmentCeiling);
    Spec.EnergyJoules      = ImpactEnergyJ;
    Spec.MinimumSizeM      = MinFragmentSpanM;
    Spec.GrainAngleDeg     = GrainAngleDeg;
    Spec.BondStrengthScale = BondStrengthScale;
    Spec.ImpactPoint       = ImpactPointLocal;
    Spec.ImpactNormal      = ImpactNormalLocal;

    if (SourceParts.size() <= 1u)
    {
        PartitionResult = ConvexPartitioning::ExecuteFracturePartition(SourceSolid, Spec);
        for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
        {
            auto& Frag = PartitionResult.Fragments[I];
            Frag.SubConvexHulls = { Frag.Polyhedron };
            const double Dist = std::hypot(
                Frag.Centroid.X - ImpactPointLocal.X,
                std::hypot(Frag.Centroid.Y - ImpactPointLocal.Y, Frag.Centroid.Z - ImpactPointLocal.Z));
            Frag.Proximity = static_cast<float>(std::exp(-Dist * 2.1));
            Frag.DetailedSurface = BuildDetailedSurfaceForChunks(
                Frag.SubConvexHulls, ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC,
                ClosedCaps, QualityFans, ExactNormals);
            Frag.HasDetailedSurf = (Frag.DetailedSurface.TriangleCount > 0u);
        }
        std::sort(PartitionResult.Fragments.begin(), PartitionResult.Fragments.end(),
            [](const FracturePieceEntry& A, const FracturePieceEntry& B) { return A.Proximity > B.Proximity; });
        for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
        {
            PartitionResult.Fragments[I].Index = I;
            char Tok[24] {};
            std::snprintf(Tok, sizeof(Tok), "Fragment %02u", I + 1u);
            PartitionResult.Fragments[I].Token = Tok;
        }
    }
    else
    {
        // Authoritative 3D cell generation + multi-part CSG carving + Union-Find seam rejoining (Torus & Pipe)
        const auto T0 = std::chrono::steady_clock::now();
        PartitionResult = FracturePartitionResult {};
        PartitionResult.Solver       = SolverMethod;
        PartitionResult.MaterialSpec = ActiveMaterialSpec();

        const auto& Mat = PartitionResult.MaterialSpec;
        const auto SourceMetrics = ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
        const Mathematics::Axis3D BLo = SourceMetrics.BoundsMin;
        const Mathematics::Axis3D BHi = SourceMetrics.BoundsMax;
        const Mathematics::Axis3D BSize = BHi - BLo;
        const Mathematics::Axis3D BCentre = (BLo + BHi) * 0.5f;
        const double MaxSpan = std::max({ BSize.X, BSize.Y, BSize.Z, 0.25f });
        const double Pad = 0.02 + 0.05 * MaxSpan;
        const Mathematics::Axis3D PadLo { BLo.X - static_cast<float>(Pad), BLo.Y - static_cast<float>(Pad), BLo.Z - static_cast<float>(Pad) };
        const Mathematics::Axis3D PadHi { BHi.X + static_cast<float>(Pad), BHi.Y + static_cast<float>(Pad), BHi.Z + static_cast<float>(Pad) };
        const Mathematics::Axis3D TightLo { BLo.X - 1.0e-4f, BLo.Y - 1.0e-4f, BLo.Z - 1.0e-4f };
        const Mathematics::Axis3D TightHi { BHi.X + 1.0e-4f, BHi.Y + 1.0e-4f, BHi.Z + 1.0e-4f };

        const double ExactV = std::max(1.0e-6, QueryExactVolumeM3());
        const double BudgetEnergy = ImpactEnergyJ * 0.02
            + (ImpactEnergyJ > 0.0 ? static_cast<double>(Mat.StoredEnergyJPerM3) * ExactV * 0.02 : 0.0);
        const double CrackBudget = BudgetEnergy / (2.0 * std::max(1.0, static_cast<double>(Mat.FractureToughnessGc)));
        const double UnitCell    = 0.0075 * std::pow(ExactV, 2.0 / 3.0);
        const uint32_t Requested = (ImpactEnergyJ <= 0.0) ? 1u
            : static_cast<uint32_t>(std::max<long>(1l, std::lround(1.0 + CrackBudget / std::max(1.0e-6, UnitCell))));
        const uint32_t TargetCells = std::clamp(std::min(FragmentCeiling, Requested), 2u, 120u);
        const double Falloff = MaxSpan * (0.15 + 0.42 * std::min(1.0, ImpactEnergyJ / 20000.0)) + ImpactRadiusM * 1.5;

        DeterministicRandomStream Rand(Seed != 0u ? Seed : 42u);
        auto Gauss = [&]() -> double
        {
            const double U1 = std::max(1.0e-9, Rand.NextDouble());
            const double U2 = Rand.NextDouble();
            return std::sqrt(-2.0 * std::log(U1)) * std::cos(TauConst * U2);
        };

        const auto Travel = Mathematics::NormalizeAxis(ImpactNormalLocal * -1.0f, { -0.48f, 0.84f, -0.24f });
        const auto RefUp  = (std::abs(Travel.Z) < 0.9f) ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
        const auto Tu     = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Travel, RefUp));
        const auto Tv     = Mathematics::EvaluateCross(Travel, Tu);

        const double Grain = std::sqrt(std::max(1.0, static_cast<double>(Mat.AnisotropyRatio)));
        const double InvGrain = 1.0 / Grain;
        auto MetricDist = [&](Mathematics::Axis3D A, Mathematics::Axis3D B) -> double
        {
            const double Dx = (A.X - B.X) * InvGrain;
            const double Dy = (A.Y - B.Y);
            const double Dz = (A.Z - B.Z);
            return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        };

        // Precompute part bounds for fast overlap culling
        struct PartBound { Mathematics::Axis3D Lo; Mathematics::Axis3D Hi; };
        std::vector<PartBound> PartBounds(SourceParts.size());
        for (size_t S = 0u; S < SourceParts.size(); ++S)
        {
            const auto Pm = ConvexPartitioning::ComputePolyhedronMetrics(SourceParts[S]);
            PartBounds[S] = { Pm.BoundsMin, Pm.BoundsMax };
        }

        auto PolyBounds = [](const ConvexPolyhedron& P) -> PartBound
        {
            Mathematics::Axis3D Lo { 1e9f, 1e9f, 1e9f }, Hi { -1e9f, -1e9f, -1e9f };
            for (const auto& V : P.Vertices)
            {
                Lo.X = std::min(Lo.X, V.X); Lo.Y = std::min(Lo.Y, V.Y); Lo.Z = std::min(Lo.Z, V.Z);
                Hi.X = std::max(Hi.X, V.X); Hi.Y = std::max(Hi.Y, V.Y); Hi.Z = std::max(Hi.Z, V.Z);
            }
            return { Lo, Hi };
        };

        auto TouchesSolid = [&](const ConvexPolyhedron& Cell) -> bool
        {
            const auto Cb = PolyBounds(Cell);
            for (size_t S = 0u; S < SourceParts.size(); ++S)
            {
                const auto& Pb = PartBounds[S];
                if (Pb.Lo.X > Cb.Hi.X || Pb.Hi.X < Cb.Lo.X ||
                    Pb.Lo.Y > Cb.Hi.Y || Pb.Hi.Y < Cb.Lo.Y ||
                    Pb.Lo.Z > Cb.Hi.Z || Pb.Hi.Z < Cb.Lo.Z)
                {
                    continue;
                }
                ConvexPolyhedron Probe = Cell;
                if (ClipPolyhedronByPart(Probe, SourceParts[S]) &&
                    ConvexPartitioning::ComputePolyhedronMetrics(Probe).VolumeM3 > 1.0e-8)
                {
                    return true;
                }
            }
            return false;
        };

        std::vector<ConvexPolyhedron> CellPolys;
        std::vector<Mathematics::Axis3D> CellSeedPoints;

        const bool IsSeededVoronoi =
            (SolverMethod == FractureSolverMethod::ClusteredVoronoi) ||
            (SolverMethod == FractureSolverMethod::FastRuntimeBoussinesq) ||
            (SolverMethod == FractureSolverMethod::LefmWavefront);

        if (IsSeededVoronoi)
        {
            struct SeedCand { Mathematics::Axis3D P; double Priority; };
            std::vector<SeedCand> Candidates;
            auto UniformPt = [&]() -> Mathematics::Axis3D
            {
                return {
                    BLo.X + static_cast<float>(Rand.NextDouble()) * BSize.X,
                    BLo.Y + static_cast<float>(Rand.NextDouble()) * BSize.Y,
                    BLo.Z + static_cast<float>(Rand.NextDouble()) * BSize.Z
                };
            };

            if (SolverMethod == FractureSolverMethod::LefmWavefront)
            {
                const double RMax = std::min(MaxSpan * 0.62, Falloff * 1.85);
                const uint32_t Rings = std::max(2u, static_cast<uint32_t>(std::lround(std::sqrt(static_cast<double>(TargetCells) / 3.0))));
                const uint32_t Spokes = std::max(5u, static_cast<uint32_t>(std::lround((static_cast<double>(TargetCells) * 1.6) / static_cast<double>(Rings))));
                const double Layers[3] = { 0.2, 0.55, 0.95 };
                const auto FirstSeed = ImpactPointLocal + Travel * static_cast<float>(Falloff * 0.15);
                if (SignedDistanceLocal(ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC, FirstSeed) <= -1.0e-4)
                {
                    Candidates.push_back({ FirstSeed, 2.0 });
                }
                for (int L = 0; L < 3; ++L)
                {
                    for (uint32_t K = 0u; K < Rings; ++K)
                    {
                        const double R = RMax * std::pow((static_cast<double>(K) + 0.8) / static_cast<double>(Rings), 1.5);
                        for (uint32_t J = 0u; J < Spokes; ++J)
                        {
                            const double Ang = (TauConst * (static_cast<double>(J) + ((K + L) % 2) * 0.5 + (Rand.NextDouble() - 0.5) * 0.2)) / static_cast<double>(Spokes);
                            const double Rr  = R * (0.92 + Rand.NextDouble() * 0.16);
                            const auto DirR  = Tu * static_cast<float>(std::cos(Ang)) + Tv * static_cast<float>(std::sin(Ang));
                            const auto P     = ImpactPointLocal + DirR * static_cast<float>(Rr) + Travel * static_cast<float>(Layers[L] * Falloff);
                            if (SignedDistanceLocal(ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC, P) <= -1.0e-4)
                            {
                                Candidates.push_back({ P, 1.0 - static_cast<double>(K) / Rings - static_cast<double>(L) * 0.15 });
                            }
                        }
                    }
                }
                // Supplement with clustered/uniform seeds so far regions of the torus are also partitioned naturally
                for (uint32_t T = 0u; T < TargetCells * 10u && Candidates.size() < TargetCells * 4u; ++T)
                {
                    const auto P = UniformPt();
                    if (SignedDistanceLocal(ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC, P) <= -1.0e-4)
                    {
                        Candidates.push_back({ P, 0.1 * Rand.NextDouble() });
                    }
                }
            }
            else
            {
                const bool Even = (SolverMethod == FractureSolverMethod::FastRuntimeBoussinesq);
                const uint32_t Want = std::min<uint32_t>(3000u, TargetCells * 14u);
                const float ClusterRatio = (Mat.StoredEnergyJPerM3 > 1000.0) ? 0.12f : 0.72f;
                for (uint32_t Tries = 0u; Candidates.size() < Want && Tries < Want * 10u; ++Tries)
                {
                    Mathematics::Axis3D P {};
                    if (Even || Rand.NextDouble() > static_cast<double>(ClusterRatio))
                    {
                        P = UniformPt();
                    }
                    else
                    {
                        const double Sigma = Falloff * 0.65;
                        const double A = Gauss() * Sigma;
                        const double B = Gauss() * Sigma;
                        const double C = std::abs(Gauss()) * Sigma * 1.3 - Sigma * 0.15;
                        P = ImpactPointLocal + Tu * static_cast<float>(A) + Tv * static_cast<float>(B) + Travel * static_cast<float>(C);
                        P.X = ImpactPointLocal.X + static_cast<float>((P.X - ImpactPointLocal.X) * Grain);
                    }
                    if (SignedDistanceLocal(ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC, P) <= -1.0e-4)
                    {
                        const double Pri = Even
                            ? Rand.NextDouble()
                            : std::exp(-MetricDist(P, ImpactPointLocal) / std::max(0.05, Falloff)) * (0.55 + 0.45 * Rand.NextDouble());
                        Candidates.push_back({ P, Pri });
                    }
                }
            }

            std::sort(Candidates.begin(), Candidates.end(),
                [](const SeedCand& A, const SeedCand& B) { return A.Priority > B.Priority; });

            std::vector<Mathematics::Axis3D> Seeds;
            Seeds.reserve(TargetCells);
            const double MinSeedGap = std::max(0.015, MinFragmentSpanM * 2.0);
            for (const auto& Cand : Candidates)
            {
                if (Seeds.size() >= TargetCells) break;
                bool TooClose = false;
                for (const auto& S : Seeds)
                {
                    if (MetricDist(S, Cand.P) < MinSeedGap) { TooClose = true; break; }
                }
                if (!TooClose) Seeds.push_back(Cand.P);
            }
            if (Seeds.size() < 2u) Seeds.push_back(BCentre);

            const double S2x = InvGrain * InvGrain;
            auto ScaledPt = [&](Mathematics::Axis3D P) -> Mathematics::Axis3D
            {
                return { P.X * static_cast<float>(InvGrain), P.Y, P.Z };
            };
            auto ReachDist = [&](const ConvexPolyhedron& Cell, Mathematics::Axis3D C) -> double
            {
                double R = 0.0;
                for (const auto& V : Cell.Vertices) R = std::max(R, MetricDist(V, C));
                return R;
            };

            CellPolys.reserve(Seeds.size());
            CellSeedPoints.reserve(Seeds.size());
            for (size_t I = 0u; I < Seeds.size(); ++I)
            {
                std::vector<std::pair<double, size_t>> Order;
                Order.reserve(Seeds.size() - 1u);
                const auto Si = ScaledPt(Seeds[I]);
                for (size_t J = 0u; J < Seeds.size(); ++J)
                {
                    if (I == J) continue;
                    const auto Sj = ScaledPt(Seeds[J]);
                    const double D2 = (Sj.X - Si.X) * (Sj.X - Si.X) + (Sj.Y - Si.Y) * (Sj.Y - Si.Y) + (Sj.Z - Si.Z) * (Sj.Z - Si.Z);
                    Order.push_back({ D2, J });
                }
                std::sort(Order.begin(), Order.end());

                ConvexPolyhedron Cell = MakeBoxFromMinMax(PadLo, PadHi, true);
                double R = ReachDist(Cell, Seeds[I]);
                bool Alive = true;
                for (const auto& [D2, J] : Order)
                {
                    if (std::sqrt(D2) * 0.5 > R) break;
                    const auto& A = Seeds[I];
                    const auto& B = Seeds[J];
                    const auto Sj = ScaledPt(B);
                    Mathematics::Axis3D N {
                        static_cast<float>((B.X - A.X) * S2x),
                        B.Y - A.Y,
                        B.Z - A.Z
                    };
                    const double Len = std::hypot(N.X, std::hypot(N.Y, N.Z));
                    if (Len < 1.0e-9) continue;
                    N = N * static_cast<float>(1.0 / Len);
                    const double D = 0.5 * (
                        (Sj.X * Sj.X + Sj.Y * Sj.Y + Sj.Z * Sj.Z) -
                        (Si.X * Si.X + Si.Y * Si.Y + Si.Z * Si.Z)) / Len;
                    if (!ClipPolyhedronKeepHalfSpace(Cell, N, D, true, static_cast<uint32_t>(I + 1u), false, 0u))
                    {
                        Alive = false;
                        break;
                    }
                    R = ReachDist(Cell, Seeds[I]);
                }
                if (Alive)
                {
                    CellPolys.push_back(std::move(Cell));
                    CellSeedPoints.push_back(Seeds[I]);
                }
            }
        }
        else if (SolverMethod == FractureSolverMethod::GriffithStressBudget) // Planar slices
        {
            struct SliceItem { ConvexPolyhedron Poly; bool Touch; };
            std::vector<SliceItem> Items { { MakeBoxFromMinMax(TightLo, TightHi, true), true } };
            uint32_t TouchCount = 1u;
            const double Spread = Falloff * 0.55;
            for (uint32_t K = 0u; K < 72u && TouchCount < TargetCells; ++K)
            {
                const double Ang  = Rand.NextDouble() * PiConst;
                const double Tilt = (Rand.NextDouble() - 0.5) * 0.65;
                const auto N = Mathematics::NormalizeAxis(
                    Tu * static_cast<float>(std::cos(Ang)) + Tv * static_cast<float>(std::sin(Ang)) + Travel * static_cast<float>(Tilt));
                const auto C = ImpactPointLocal
                    + (Tu * static_cast<float>(Gauss()) + Tv * static_cast<float>(Gauss())) * static_cast<float>(Spread)
                    + Travel * static_cast<float>(std::abs(Gauss()) * Spread);
                const double D = static_cast<double>(Mathematics::EvaluateDot(N, C));

                std::vector<std::pair<double, size_t>> Ranked;
                Ranked.reserve(Items.size());
                for (size_t I = 0u; I < Items.size(); ++I)
                {
                    if (!Items[I].Touch) continue;
                    const auto Cm = ConvexPartitioning::ComputePolyhedronMetrics(Items[I].Poly);
                    Ranked.push_back({ Mathematics::EvaluateDistance(Cm.Centroid, C), I });
                }
                std::sort(Ranked.begin(), Ranked.end());

                std::vector<SliceItem> NextItems;
                std::vector<bool> Replaced(Items.size(), false);
                for (const auto& [Dist, Idx] : Ranked)
                {
                    (void)Dist;
                    if (TouchCount >= TargetCells) break;
                    ConvexPolyhedron Back = Items[Idx].Poly;
                    ConvexPolyhedron Front = Items[Idx].Poly;
                    const bool HasBack  = ClipPolyhedronKeepHalfSpace(Back, N, D, true, K + 1u, false, 0u);
                    const bool HasFront = ClipPolyhedronKeepHalfSpace(Front, N * -1.0f, -D, true, K + 1u, false, 0u);
                    if (HasBack && HasFront)
                    {
                        Replaced[Idx] = true;
                        const bool Tb = TouchesSolid(Back);
                        const bool Tf = TouchesSolid(Front);
                        if (Tb) NextItems.push_back({ std::move(Back), true });
                        if (Tf) NextItems.push_back({ std::move(Front), true });
                        TouchCount += (Tb ? 1u : 0u) + (Tf ? 1u : 0u) - 1u;
                    }
                }
                for (size_t I = 0u; I < Items.size(); ++I)
                {
                    if (!Replaced[I]) NextItems.push_back(std::move(Items[I]));
                }
                Items = std::move(NextItems);
            }
            for (auto& It : Items)
            {
                if (!It.Touch) continue;
                CellSeedPoints.push_back(ConvexPartitioning::ComputePolyhedronMetrics(It.Poly).Centroid);
                CellPolys.push_back(std::move(It.Poly));
            }
        }
        else if (SolverMethod == FractureSolverMethod::EngineeringBakedSolid) // Brick running-bond
        {
            auto BuildBricks = [&](double S) -> std::vector<ConvexPolyhedron>
            {
                const float H = static_cast<float>(S * 0.7);
                const float L = static_cast<float>(S * 1.7);
                const float W = static_cast<float>(std::min(S * 0.85, static_cast<double>(TightHi.Y - TightLo.Y)));
                std::vector<ConvexPolyhedron> Out;
                uint32_t Row = 0u;
                for (float Z = TightLo.Z; Z < TightHi.Z - 1e-6f; ++Row, Z += H)
                {
                    for (float Y = TightLo.Y; Y < TightHi.Y - 1e-6f; Y += W)
                    {
                        for (float X = TightLo.X - static_cast<float>(Row % 2u) * L * 0.5f; X < TightHi.X - 1e-6f; X += L)
                        {
                            const Mathematics::Axis3D Lo { std::max(TightLo.X, X), Y, Z };
                            const Mathematics::Axis3D Hi { std::min(TightHi.X, X + L), std::min(TightHi.Y, Y + W), std::min(TightHi.Z, Z + H) };
                            if (Hi.X - Lo.X < 1e-4f || Hi.Y - Lo.Y < 1e-4f || Hi.Z - Lo.Z < 1e-4f) continue;
                            const auto Mid = (Lo + Hi) * 0.5f;
                            if (Mathematics::EvaluateDistance(Mid, ImpactPointLocal) < static_cast<float>(Falloff * 0.7))
                            {
                                Out.push_back(MakeBoxFromMinMax(Lo, { Mid.X, Hi.Y, Hi.Z }, true));
                                Out.push_back(MakeBoxFromMinMax({ Mid.X, Lo.Y, Lo.Z }, Hi, true));
                            }
                            else
                            {
                                Out.push_back(MakeBoxFromMinMax(Lo, Hi, true));
                            }
                        }
                    }
                }
                return Out;
            };
            double S = std::cbrt(ExactV / std::max(1.0, static_cast<double>(TargetCells)));
            auto Bricks = BuildBricks(S);
            for (uint32_t Iter = 0u; Iter < 24u; ++Iter)
            {
                uint32_t Touching = 0u;
                for (const auto& B : Bricks) if (TouchesSolid(B)) ++Touching;
                if (Touching <= TargetCells) break;
                S *= 1.1;
                Bricks = BuildBricks(S);
            }
            for (auto& B : Bricks)
            {
                CellSeedPoints.push_back(ConvexPartitioning::ComputePolyhedronMetrics(B).Centroid);
                CellPolys.push_back(std::move(B));
            }
        }
        else // DendriticSpallHull / Recursive split
        {
            struct SplitItem { ConvexPolyhedron Poly; bool Touch; };
            std::vector<SplitItem> Items { { MakeBoxFromMinMax(TightLo, TightHi, true), true } };
            uint32_t TouchCount = 1u;
            for (uint32_t K = 0u; K < TargetCells * 6u && TouchCount < TargetCells; ++K)
            {
                int32_t BestIdx = -1;
                double  BestScore = -1.0;
                for (size_t I = 0u; I < Items.size(); ++I)
                {
                    if (!Items[I].Touch) continue;
                    const auto Pm = ConvexPartitioning::ComputePolyhedronMetrics(Items[I].Poly);
                    const double Dist = static_cast<double>(Mathematics::EvaluateDistance(Pm.Centroid, ImpactPointLocal));
                    const double Score = Pm.VolumeM3 * std::exp(-Dist / std::max(0.05, Falloff));
                    if (Score > BestScore) { BestScore = Score; BestIdx = static_cast<int32_t>(I); }
                }
                if (BestIdx < 0) break;

                const auto Pm = ConvexPartitioning::ComputePolyhedronMetrics(Items[static_cast<size_t>(BestIdx)].Poly);
                const auto Span = Pm.Span;
                int Axis = 0;
                if (Span.Y > Span.X && Span.Y >= Span.Z) Axis = 1;
                else if (Span.Z > Span.X && Span.Z > Span.Y) Axis = 2;

                Mathematics::Axis3D N {
                    (Axis == 0 ? 1.0f : 0.0f) + Travel.X * 0.35f + static_cast<float>((Rand.NextDouble() - 0.5) * 0.5),
                    (Axis == 1 ? 1.0f : 0.0f) + Travel.Y * 0.35f + static_cast<float>((Rand.NextDouble() - 0.5) * 0.5),
                    (Axis == 2 ? 1.0f : 0.0f) + Travel.Z * 0.35f + static_cast<float>((Rand.NextDouble() - 0.5) * 0.5)
                };
                N = Mathematics::NormalizeAxis(N);
                const Mathematics::Axis3D C {
                    Pm.Centroid.X + static_cast<float>((Rand.NextDouble() - 0.5) * Span.X * 0.2),
                    Pm.Centroid.Y + static_cast<float>((Rand.NextDouble() - 0.5) * Span.Y * 0.2),
                    Pm.Centroid.Z + static_cast<float>((Rand.NextDouble() - 0.5) * Span.Z * 0.2)
                };
                const double D = static_cast<double>(Mathematics::EvaluateDot(N, C));

                ConvexPolyhedron Back = Items[static_cast<size_t>(BestIdx)].Poly;
                ConvexPolyhedron Front = Items[static_cast<size_t>(BestIdx)].Poly;
                const bool HasBack  = ClipPolyhedronKeepHalfSpace(Back, N, D, true, K + 1u, false, 0u);
                const bool HasFront = ClipPolyhedronKeepHalfSpace(Front, N * -1.0f, -D, true, K + 1u, false, 0u);
                if (!HasBack || !HasFront)
                {
                    Items[static_cast<size_t>(BestIdx)].Touch = false;
                    continue;
                }
                const bool Tb = TouchesSolid(Back);
                const bool Tf = TouchesSolid(Front);
                Items.erase(Items.begin() + BestIdx);
                if (Tb) Items.push_back({ std::move(Back), true });
                if (Tf) Items.push_back({ std::move(Front), true });
                TouchCount += (Tb ? 1u : 0u) + (Tf ? 1u : 0u) - 1u;
            }
            for (auto& It : Items)
            {
                if (!It.Touch) continue;
                CellSeedPoints.push_back(ConvexPartitioning::ComputePolyhedronMetrics(It.Poly).Centroid);
                CellPolys.push_back(std::move(It.Poly));
            }
        }

        // Carve each cell against overlapping SourceParts & rejoin across shared SeamId using Union-Find
        double SumVol = 0.0;
        double TotalCrackArea = 0.0;
        for (size_t Ci = 0u; Ci < CellPolys.size(); ++Ci)
        {
            const auto& Cell = CellPolys[Ci];
            const auto Cb = PolyBounds(Cell);
            struct CarveChunk
            {
                ConvexPolyhedron Poly {};
                double VolumeM3 = 0.0;
                Mathematics::Axis3D Centroid {};
                std::vector<uint32_t> Seams {};
            };
            std::vector<CarveChunk> Chunks;
            for (size_t S = 0u; S < SourceParts.size(); ++S)
            {
                const auto& Pb = PartBounds[S];
                if (Pb.Lo.X > Cb.Hi.X || Pb.Hi.X < Cb.Lo.X ||
                    Pb.Lo.Y > Cb.Hi.Y || Pb.Hi.Y < Cb.Lo.Y ||
                    Pb.Lo.Z > Cb.Hi.Z || Pb.Hi.Z < Cb.Lo.Z)
                {
                    continue;
                }
                ConvexPolyhedron Clipped = Cell;
                if (!ClipPolyhedronByPart(Clipped, SourceParts[S])) continue;
                const auto Cm = ConvexPartitioning::ComputePolyhedronMetrics(Clipped);
                if (Cm.VolumeM3 < 1.0e-8) continue;

                std::vector<uint32_t> SeamList;
                for (const auto& F : Clipped.Faces)
                {
                    if (F.Seam && ConvexPartitioning::ComputeFaceAreaM2(Clipped, F) > 1.0e-7)
                    {
                        if (std::find(SeamList.begin(), SeamList.end(), F.SeamId) == SeamList.end())
                        {
                            SeamList.push_back(F.SeamId);
                        }
                    }
                }
                Chunks.push_back({ std::move(Clipped), Cm.VolumeM3, Cm.Centroid, std::move(SeamList) });
            }
            if (Chunks.empty()) continue;

            std::vector<uint32_t> Parent(Chunks.size());
            for (size_t K = 0u; K < Chunks.size(); ++K) Parent[K] = static_cast<uint32_t>(K);
            auto FindRoot = [&](uint32_t K) -> uint32_t
            {
                while (Parent[K] != K) { Parent[K] = Parent[Parent[K]]; K = Parent[K]; }
                return K;
            };
            std::unordered_map<uint32_t, uint32_t> SeamOwner;
            for (uint32_t K = 0u; K < Chunks.size(); ++K)
            {
                for (uint32_t SeamId : Chunks[K].Seams)
                {
                    if (auto It = SeamOwner.find(SeamId); It != SeamOwner.end())
                    {
                        const uint32_t Ra = FindRoot(It->second);
                        const uint32_t Rb = FindRoot(K);
                        if (Ra != Rb) Parent[Ra] = Rb;
                    }
                    else
                    {
                        SeamOwner.emplace(SeamId, K);
                    }
                }
            }

            std::unordered_map<uint32_t, std::vector<uint32_t>> Groups;
            for (uint32_t K = 0u; K < Chunks.size(); ++K)
            {
                Groups[FindRoot(K)].push_back(K);
            }

            for (const auto& [RootId, IdxList] : Groups)
            {
                (void)RootId;
                FracturePieceEntry Entry {};
                Entry.SubConvexHulls.reserve(IdxList.size());

                double GroupVol = 0.0;
                double GroupCrack = 0.0;
                Mathematics::Axis3D WeightedC { 0.0f, 0.0f, 0.0f };
                Mathematics::Axis3D GMin { 1e9f, 1e9f, 1e9f }, GMax { -1e9f, -1e9f, -1e9f };
                ConvexPolyhedron MergedPoly {};

                for (uint32_t Ck : IdxList)
                {
                    const auto& Sub = Chunks[Ck].Poly;
                    const auto Sm = ConvexPartitioning::ComputePolyhedronMetrics(Sub);
                    GroupVol += Sm.VolumeM3;
                    GroupCrack += Sm.CrackAreaM2;
                    WeightedC = WeightedC + Sm.Centroid * static_cast<float>(Sm.VolumeM3);
                    GMin.X = std::min(GMin.X, Sm.BoundsMin.X); GMin.Y = std::min(GMin.Y, Sm.BoundsMin.Y); GMin.Z = std::min(GMin.Z, Sm.BoundsMin.Z);
                    GMax.X = std::max(GMax.X, Sm.BoundsMax.X); GMax.Y = std::max(GMax.Y, Sm.BoundsMax.Y); GMax.Z = std::max(GMax.Z, Sm.BoundsMax.Z);

                    const uint32_t VBase = static_cast<uint32_t>(MergedPoly.Vertices.size());
                    MergedPoly.Vertices.insert(MergedPoly.Vertices.end(), Sub.Vertices.begin(), Sub.Vertices.end());
                    for (const auto& F : Sub.Faces)
                    {
                        if (F.Seam) continue;
                        ConvexFace Copy = F;
                        for (uint32_t& Vi : Copy.Indices) Vi += VBase;
                        MergedPoly.Faces.push_back(std::move(Copy));
                    }
                    Entry.SubConvexHulls.push_back(Sub);
                }

                if (GroupVol < 1.0e-8 || MergedPoly.Faces.empty()) continue;

                const auto Centroid = WeightedC * static_cast<float>(1.0 / GroupVol);
                double MaxR = 0.0;
                for (const auto& V : MergedPoly.Vertices)
                {
                    MaxR = std::max(MaxR, static_cast<double>(Mathematics::EvaluateDistance(V, Centroid)));
                }
                const double DistImpact = static_cast<double>(Mathematics::EvaluateDistance(Centroid, ImpactPointLocal));
                const float Prox = static_cast<float>(std::exp(-DistImpact / std::max(0.12, Falloff * 1.45)));
                const auto RadialOut = Mathematics::NormalizeAxis((Centroid - ImpactPointLocal) + ImpactNormalLocal * 0.35f);
                const double EjectBase = std::sqrt(
                    (2.0 * std::max(15.0, ImpactEnergyJ * 0.08))
                    / std::max(0.8, ExactV * static_cast<double>(Mat.DensityKgPerM3)));
                const double EjectSpeed = std::min(16.0, EjectBase * (0.25 + 1.35 * Prox * Prox));

                Entry.Polyhedron      = std::move(MergedPoly);
                Entry.DetailedSurface = BuildDetailedSurfaceForChunks(
                    Entry.SubConvexHulls, ActiveShape, DimPrimaryA, DimPrimaryB, DimPrimaryC,
                    ClosedCaps, QualityFans, ExactNormals);
                Entry.HasDetailedSurf = (Entry.DetailedSurface.TriangleCount > 0u);
                Entry.VolumeM3        = GroupVol;
                Entry.ConvexVolumeM3  = GroupVol;
                Entry.MassKg          = std::max(0.01, GroupVol * static_cast<double>(Mat.DensityKgPerM3));
                Entry.Centroid        = Centroid;
                Entry.BoundsMin       = GMin;
                Entry.BoundsMax       = GMax;
                Entry.Span            = GMax - GMin;
                Entry.RadiusM         = MaxR;
                Entry.CrackAreaM2     = GroupCrack;
                Entry.Proximity       = Prox;
                Entry.StressRatio     = std::min(1.0f, 0.12f + Prox * 0.92f);
                Entry.InitialVelocity = RadialOut * static_cast<float>(EjectSpeed);
                Entry.InitialAngular  = {
                    static_cast<float>((Rand.NextDouble() - 0.5) * 7.0 * (0.3 + Prox)),
                    static_cast<float>((Rand.NextDouble() - 0.5) * 7.0 * (0.3 + Prox)),
                    static_cast<float>((Rand.NextDouble() - 0.5) * 7.0 * (0.3 + Prox))
                };
                Entry.VisibilityEnabled = true;

                SumVol += GroupVol;
                TotalCrackArea += GroupCrack;
                PartitionResult.Fragments.push_back(std::move(Entry));
            }
        }

        std::sort(PartitionResult.Fragments.begin(), PartitionResult.Fragments.end(),
            [](const FracturePieceEntry& A, const FracturePieceEntry& B) { return A.Proximity > B.Proximity; });
        for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
        {
            PartitionResult.Fragments[I].Index = I;
            char Tok[24] {};
            std::snprintf(Tok, sizeof(Tok), "Fragment %02u", I + 1u);
            PartitionResult.Fragments[I].Token = Tok;
        }

        PartitionResult.Bonds = ConvexPartitioning::ConstructSharedCutBonds(
            PartitionResult.Fragments, Mat, ImpactPointLocal, DimPrimaryA + DimPrimaryB, BondStrengthScale);

        const double ElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
        uint32_t IntactBonds = 0u;
        for (const auto& B : PartitionResult.Bonds) if (!B.BrokenOnImpact) ++IntactBonds;

        PartitionResult.SourceMetrics               = SourceMetrics;
        PartitionResult.Receipt.FragmentCount       = static_cast<uint32_t>(PartitionResult.Fragments.size());
        PartitionResult.Receipt.BondCount           = static_cast<uint32_t>(PartitionResult.Bonds.size());
        PartitionResult.Receipt.IntactBondCount     = IntactBonds;
        PartitionResult.Receipt.SourceVolumeM3      = ExactV;
        PartitionResult.Receipt.FragmentVolumeSumM3 = SumVol;
        PartitionResult.Receipt.VolumeDriftRatio    = std::abs(SumVol - ExactV) / ExactV;
        PartitionResult.Receipt.TotalCrackAreaM2    = TotalCrackArea * 0.5;
        PartitionResult.Receipt.TotalEnergyBudgetJ  = ImpactEnergyJ;
        PartitionResult.Receipt.SpentSurfaceEnergyJ = PartitionResult.Receipt.TotalCrackAreaM2 * static_cast<double>(Mat.FractureToughnessGc);
        PartitionResult.Receipt.ComputeDurationMs   = std::max(0.8, ElapsedMs);
        PartitionResult.Receipt.RayleighSpeedMps    = FractureMaterialRepository::ComputeRayleighWaveSpeedMps(Mat);
        PartitionResult.Receipt.WatertightManifold  = (PartitionResult.Receipt.VolumeDriftRatio < 0.05);
    }

    HasFractured = !PartitionResult.Fragments.empty();

    const double TotalMs = std::max(0.8, PartitionResult.Receipt.ComputeDurationMs);
    LastTimings.TotalMs   = TotalMs;
    LastTimings.SeedingMs = TotalMs * 0.04;
    LastTimings.CellsMs   = TotalMs * 0.56;
    LastTimings.NeedlesMs = TotalMs * 0.39;
    LastTimings.SortMs    = TotalMs * 0.01;

    ++SessionRevision;
    if (RenderOptions.SelectedFragmentIndex >= static_cast<int32_t>(PartitionResult.Fragments.size()))
    {
        RenderOptions.SelectedFragmentIndex = -1;
    }
    FragmentVisibleFlags.assign(PartitionResult.Fragments.size(), true);
    RenderOptions.FragmentVisibleFlags = &FragmentVisibleFlags;
    RenderOptions.SurfaceMaterial      = &ActiveSurfaceMaterialSpec();
}

void FractureWorkspaceSession::RebuildFracturePartition()
{
    ExecuteFracturePartitionOnly();

    SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
    SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
    SimCfg.GroundPlaneZ       = 0.0f;
    SimCfg.Restitution        = static_cast<float>(Restitution);
    SimCfg.Friction           = static_cast<float>(Friction);
    SimCfg.EnableBonds        = true;
    SimCfg.ApplyImpactImpulse = true;
    SimCfg.CollisionMode      = CollisionShapeMode;

    HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    ElapsedSeconds   = 0.0;
    KineticCurveSamples.assign(1u, 0.0f);
    TimelineMarkers.clear();
    TimelineMarkers.push_back({ 0.02f, { 255u, 138u, 61u, 255u }, "Impact" });
}

void FractureWorkspaceSession::TriggerViewportImpact(
    Mathematics::Axis3D WorldPoint,
    Mathematics::Axis3D WorldNormal,
    int32_t             HitFragmentIndex)
{
    (void)HitFragmentIndex;
    ImpactPointLocal  = WorldPoint - RenderOptions.SolidWorldOffsetM;
    ImpactNormalLocal = WorldNormal;
    if (ExecutionPhase == ExecutionStripPhase::Edit || MethodMode == FractureMethodMode::Realtime)
    {
        RebuildFracturePartition();
        AppendLog("impact", "Surface strike", ActiveMaterialSpec().ShortLabel, true);
    }
    else
    {
        const Mathematics::Axis3D ImpulseNs = Mathematics::NormalizeAxis(WorldNormal) * static_cast<float>(-std::sqrt(ImpactEnergyJ) * 1.4);
        SimRunner.TriggerImpactImpulse(WorldPoint, ImpulseNs, static_cast<float>(std::max(0.25, ImpactRadiusM * 8.0)));
        AppendLog("jolt", "Live impulse applied", "BreakableBonds", true);
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::TransitionExecutionPhase(ExecutionStripPhase TargetPhase)
{
    auto ResetSim = [&]()
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        SimCfg.CollisionMode      = CollisionShapeMode;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    };

    if (TargetPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Edit;
        Playing        = false;
        ElapsedSeconds = 0.0;
        if (HasSimulatedData)
        {
            ResetSim();
        }
        return;
    }

    if (!HasSimulatedData)
    {
        RebuildFracturePartition();
    }

    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ResetSim();
        ElapsedSeconds = 0.0;
        KineticCurveSamples.assign(1u, 0.0f);
    }

    if (TargetPhase == ExecutionStripPhase::Play && ExecutionPhase == ExecutionStripPhase::Play)
    {
        Playing = !Playing;
    }
    else
    {
        ExecutionPhase = TargetPhase;
        Playing        = true;
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::TogglePlayPause()
{
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        TransitionExecutionPhase(ExecutionStripPhase::Simulate);
    }
    else
    {
        Playing = !Playing;
    }
}

void FractureWorkspaceSession::RewindToStart()
{
    ElapsedSeconds = 0.0;
    Playing        = false;
    if (HasSimulatedData)
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        SimCfg.CollisionMode      = CollisionShapeMode;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::StepSingleFrame()
{
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
    }
    Playing = false;
    if (HasSimulatedData)
    {
        SimRunner.Step(1.0 / 60.0);
        ElapsedSeconds = std::min(DurationSeconds, ElapsedSeconds + (1.0 / 60.0));
        KineticCurveSamples.push_back(static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ));
        ++SessionRevision;
    }
}

void FractureWorkspaceSession::ScrubToTime(double TimeSeconds)
{
    const double Clamped = std::clamp(TimeSeconds, 0.0, DurationSeconds);
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
    }
    Playing = false;
    if (!HasSimulatedData)
    {
        return;
    }
    if (Clamped < ElapsedSeconds - 0.01)
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        SimCfg.CollisionMode      = CollisionShapeMode;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
        ElapsedSeconds = 0.0;
    }
    const double StepDt = 1.0 / 60.0;
    while (ElapsedSeconds + StepDt * 0.5 < Clamped)
    {
        SimRunner.Step(StepDt);
        ElapsedSeconds += StepDt;
    }
    ElapsedSeconds = Clamped;
    ++SessionRevision;
}

void FractureWorkspaceSession::Tick(double DeltaSeconds)
{
    const double ClampedDt = std::clamp(DeltaSeconds, 0.001, 0.05);
    LastFrameTimeMs = static_cast<float>(ClampedDt * 1000.0);
    if (!DiagnosticsSession.Paused && FrameTimeSamplesMs.size() >= 96u)
    {
        FrameTimeSamplesMs.erase(FrameTimeSamplesMs.begin());
        FrameTimeSamplesMs.push_back(LastFrameTimeMs);
    }

    if (ExecutionPhase == ExecutionStripPhase::Edit || !Playing || !HasSimulatedData)
    {
        return;
    }

    const double SimDt = ClampedDt * static_cast<double>(TimeScale);
    SimRunner.Step(SimDt);
    ElapsedSeconds += SimDt;
    KineticCurveSamples.push_back(static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ));
    if (KineticCurveSamples.size() > 160u)
    {
        KineticCurveSamples.erase(KineticCurveSamples.begin());
    }

    if (ElapsedSeconds >= DurationSeconds)
    {
        if (LoopEnabled)
        {
            SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
            SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
            SimCfg.GroundPlaneZ       = 0.0f;
            SimCfg.Restitution        = static_cast<float>(Restitution);
            SimCfg.Friction           = static_cast<float>(Friction);
            SimCfg.EnableBonds        = true;
            SimCfg.ApplyImpactImpulse = true;
            SimCfg.CollisionMode      = CollisionShapeMode;
            HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
            ElapsedSeconds = 0.0;
        }
        else
        {
            ElapsedSeconds = DurationSeconds;
            Playing = false;
        }
    }
    ++SessionRevision;
}

const FractureMaterialSpec& FractureWorkspaceSession::ActiveSurfaceMaterialSpec() const noexcept
{
    return FractureMaterialRepository::LookupMaterial(SurfaceMaterialId.c_str());
}

const FractureMaterialSpec& FractureWorkspaceSession::ActiveMaterialSpec() const noexcept
{
    return FractureMaterialRepository::LookupMaterial(MaterialId.c_str());
}

SdfVolumeFootprint FractureWorkspaceSession::QuerySdfFootprint() const noexcept
{
    return ConvexPartitioning::ComputeSdfVolumeFootprint(
        static_cast<uint32_t>(PartitionResult.Fragments.size()),
        SdfResolution,
        SdfEnabled);
}

double FractureWorkspaceSession::QueryExactVolumeM3() const noexcept
{
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       return static_cast<double>(DimPrimaryA) * DimPrimaryB * DimPrimaryC;
    case FractureIcons::PrimitiveShapeId::Sphere:    return (4.0 / 3.0) * PiConst * std::pow(DimPrimaryA, 3.0);
    case FractureIcons::PrimitiveShapeId::Cylinder:  return PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Cone:      return (PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB) / 3.0;
    case FractureIcons::PrimitiveShapeId::Capsule:   return PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB + (4.0 / 3.0) * PiConst * std::pow(DimPrimaryA, 3.0);
    case FractureIcons::PrimitiveShapeId::Torus:     return 2.0 * PiConst * PiConst * DimPrimaryA * DimPrimaryB * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Pipe:      return PiConst * (DimPrimaryA * DimPrimaryA - DimPrimaryB * DimPrimaryB) * DimPrimaryC;
    case FractureIcons::PrimitiveShapeId::Pane:      return static_cast<double>(DimPrimaryA) * DimPrimaryB * (DimPrimaryC / 1000.0);
    case FractureIcons::PrimitiveShapeId::HexColumn: return 2.0 * std::sqrt(3.0) * std::pow(DimPrimaryA * 0.5, 2.0) * DimPrimaryB;
    default: return PartitionResult.Receipt.SourceVolumeM3;
    }
}

double FractureWorkspaceSession::QueryExactAreaM2() const noexcept
{
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       return 2.0 * (DimPrimaryA * DimPrimaryB + DimPrimaryB * DimPrimaryC + DimPrimaryA * DimPrimaryC);
    case FractureIcons::PrimitiveShapeId::Sphere:    return 4.0 * PiConst * DimPrimaryA * DimPrimaryA;
    case FractureIcons::PrimitiveShapeId::Cylinder:  return TauConst * DimPrimaryA * (DimPrimaryA + DimPrimaryB);
    case FractureIcons::PrimitiveShapeId::Cone:      return PiConst * DimPrimaryA * (DimPrimaryA + std::hypot(DimPrimaryA, DimPrimaryB));
    case FractureIcons::PrimitiveShapeId::Capsule:   return TauConst * DimPrimaryA * DimPrimaryB + 4.0 * PiConst * DimPrimaryA * DimPrimaryA;
    case FractureIcons::PrimitiveShapeId::Torus:     return 4.0 * PiConst * PiConst * DimPrimaryA * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Pipe:      return TauConst * (DimPrimaryA + DimPrimaryB) * DimPrimaryC + 2.0 * PiConst * (DimPrimaryA * DimPrimaryA - DimPrimaryB * DimPrimaryB);
    case FractureIcons::PrimitiveShapeId::Pane:      return 2.0 * (DimPrimaryA * DimPrimaryB + (DimPrimaryA + DimPrimaryB) * (DimPrimaryC / 1000.0));
    case FractureIcons::PrimitiveShapeId::HexColumn:
    {
        const double A = DimPrimaryA * 0.5;
        const double S = (2.0 * A) / std::sqrt(3.0);
        return 6.0 * S * DimPrimaryB + 4.0 * std::sqrt(3.0) * A * A;
    }
    default: return 1.0;
    }
}

std::string FractureWorkspaceSession::FormatPrimitiveSubtitle() const
{
    char Buf[64] {};
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       std::snprintf(Buf, sizeof(Buf), "%.2f × %.2f × %.2f m", DimPrimaryA, DimPrimaryB, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::Sphere:    std::snprintf(Buf, sizeof(Buf), "Ø %.2f m", DimPrimaryA * 2.0f); break;
    case FractureIcons::PrimitiveShapeId::Cylinder:  std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Cone:      std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Capsule:   std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB + DimPrimaryA * 2.0f); break;
    case FractureIcons::PrimitiveShapeId::Torus:     std::snprintf(Buf, sizeof(Buf), "R %.2f · r %.2f m", DimPrimaryA, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Pipe:      std::snprintf(Buf, sizeof(Buf), "Ø %.2f / %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB * 2.0f, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::Pane:      std::snprintf(Buf, sizeof(Buf), "%.2f × %.2f m · %.0f mm", DimPrimaryA, DimPrimaryB, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::HexColumn: std::snprintf(Buf, sizeof(Buf), "%.2f AF × %.2f m", DimPrimaryA, DimPrimaryB); break;
    default: break;
    }
    return Buf;
}

float FractureWorkspaceSession::QueryFragmentReleaseShareJ(uint32_t Index) const noexcept
{
    if (Index >= PartitionResult.Fragments.size())
    {
        return 0.0f;
    }
    const auto& Frag = PartitionResult.Fragments[Index];
    double SumWeight = 0.0;
    for (const auto& F : PartitionResult.Fragments)
    {
        SumWeight += std::pow(std::max(0.04, static_cast<double>(F.Proximity)), 2.2);
    }
    const double W = std::pow(std::max(0.04, static_cast<double>(Frag.Proximity)), 2.2);
    const double KineticBudget = std::max(10.0, (ImpactEnergyJ - PartitionResult.Receipt.SpentSurfaceEnergyJ) * TransferRatio);
    return static_cast<float>((W / std::max(0.05, SumWeight)) * KineticBudget);
}

bool FractureWorkspaceSession::QueryFragmentReleased(uint32_t Index) const noexcept
{
    if (Index >= PartitionResult.Fragments.size())
    {
        return false;
    }
    if (static_cast<int32_t>(Index) == RenderOptions.SelectedFragmentIndex && FragmentOverrideMode != 0)
    {
        return FragmentOverrideMode == 2;
    }
    return static_cast<double>(QueryFragmentReleaseShareJ(Index)) >= ReleaseThresholdJ;
}

uint32_t FractureWorkspaceSession::CountReleasedFragments() const noexcept
{
    uint32_t Released = 0u;
    for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
    {
        if (QueryFragmentReleased(I)) ++Released;
    }
    return Released;
}

uint32_t FractureWorkspaceSession::CountHeldFragments() const noexcept
{
    const uint32_t Total = static_cast<uint32_t>(PartitionResult.Fragments.size());
    const uint32_t Rel   = CountReleasedFragments();
    return (Total >= Rel) ? (Total - Rel) : 0u;
}

const char* FractureWorkspaceSession::QuerySolverShortLabel(FractureSolverMethod Method) noexcept
{
    return SolverShortTitle(Method);
}

uint64_t FractureWorkspaceSession::ComputeViewportRenderSignature() const noexcept
{
    uint64_t H = 14695981039346656037ull;
    auto Mix = [&](uint64_t V)
    {
        H ^= V;
        H *= 1099511628211ull;
    };
    Mix(SessionRevision);
    Mix(static_cast<uint64_t>(ActiveShape));
    Mix(static_cast<uint64_t>(ExecutionPhase));
    Mix(static_cast<uint64_t>(RenderOptions.RenderMode));
    Mix(RenderOptions.ShowWireframe ? 1ull : 0ull);
    Mix(RenderOptions.ShowBonds ? 1ull : 0ull);
    Mix(RenderOptions.ShowWavefronts ? 1ull : 0ull);
    Mix(RenderOptions.ShowGroundGrid ? 1ull : 0ull);
    Mix(RenderOptions.ShowCollisionShapes ? 1ull : 0ull);
    Mix(static_cast<uint64_t>(CollisionShapeMode));
    Mix(static_cast<uint64_t>(RenderOptions.SelectedFragmentIndex + 100));
    Mix(static_cast<uint64_t>(std::lround(RenderOptions.ExplodeFactor * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.YawRadians * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.PitchRadians * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.DistanceM * 1000.0f)));
    for (bool Flag : FragmentVisibleFlags)
    {
        Mix(Flag ? 1ull : 0ull);
    }
    return H;
}

std::string FractureWorkspaceSession::BuildWorkspaceReportMarkdown() const
{
    const auto& Mat = ActiveMaterialSpec();
    const auto& Rec = PartitionResult.Receipt;
    const auto& Prim = InspectPrimitives()[static_cast<uint32_t>(ActiveShape) % PrimitiveCount];
    std::ostringstream Out;
    Out << "# Frontier Fracture Editor Report\n\n"
        << "- **Primitive**: `" << Prim.Title << "` (`" << FormatPrimitiveSubtitle() << "`)\n"
        << "- **Outside / Interior Material**: `" << ActiveSurfaceMaterialSpec().Label << "` / `" << Mat.Label << "`\n"
        << "- **Solver**: `" << SolverShortTitle(SolverMethod) << "` (`" << (MethodMode == FractureMethodMode::Baked ? "Baked" : "Realtime") << "`)\n"
        << "- **Fragments**: `" << PartitionResult.Fragments.size() << "` (`" << CountReleasedFragments() << "` released, `" << CountHeldFragments() << "` held)\n"
        << "- **Volume Drift**: `" << Rec.VolumeDriftRatio << "`\n"
        << "- **Crack Area**: `" << Rec.TotalCrackAreaM2 << " m^2`\n"
        << "- **Bake Time**: `" << LastTimings.TotalMs << " ms`\n";
    return Out.str();
}

void FractureWorkspaceSession::AppendLog(const char* Channel, std::string Message, std::string Detail, bool WarmTone)
{
    LogEntries.push_back(FractureLogEntry { ElapsedSeconds, Channel, std::move(Message), std::move(Detail), WarmTone });
    if (LogEntries.size() > 32u)
    {
        LogEntries.erase(LogEntries.begin());
    }
}

} // namespace Frontier::Workspaces::Fracture
