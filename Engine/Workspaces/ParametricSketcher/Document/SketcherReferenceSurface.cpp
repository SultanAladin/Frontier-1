/*====================================================================================================================================
                                                   SKETCHERREFERENCESURFACE.CPP
====================================================================================================================================*/
// 🧩 BVH builder, O(log N) ray-triangle intersection, and exact closest-point projection for high-poly reference surfaces

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherReferenceSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Frontier::Workspaces::Sketcher
{

using GeometrySpace::Parametric::Cross;
using GeometrySpace::Parametric::Dot;
using GeometrySpace::Parametric::Vector3;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t LeafTriangleCap = 6u;                                                     // Max triangles per BVH leaf
constexpr float    BoundInfinity   = 1.0e30f;

struct TriangleBounds
{
    float MinX = BoundInfinity, MinY = BoundInfinity, MinZ = BoundInfinity;
    float MaxX = -BoundInfinity, MaxY = -BoundInfinity, MaxZ = -BoundInfinity;
    float CenterX = 0.0f, CenterY = 0.0f, CenterZ = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 NormalizeSafe(const Vector3& V) noexcept
{
    const double LengthSq = Dot(V, V);
    if (LengthSq <= 1.0e-24) return { 0.0, 0.0, 1.0 };
    const double Inv = 1.0 / std::sqrt(LengthSq);
    return { V.X * Inv, V.Y * Inv, V.Z * Inv };
}

[[nodiscard]] bool IntersectBoxSlab(
    const SketcherReferenceBvhCell& Cell,
    const double                    Origin[3],
    const double                    InvDir[3],
    double                          MaxT,
    double&                         OutNear) noexcept
{
    double TMin = 0.0;
    double TMax = MaxT;

    const double Tx0 = (double(Cell.MinX) - Origin[0]) * InvDir[0];
    const double Tx1 = (double(Cell.MaxX) - Origin[0]) * InvDir[0];
    TMin = std::max(TMin, std::min(Tx0, Tx1));
    TMax = std::min(TMax, std::max(Tx0, Tx1));
    if (TMax < TMin) return false;

    const double Ty0 = (double(Cell.MinY) - Origin[1]) * InvDir[1];
    const double Ty1 = (double(Cell.MaxY) - Origin[1]) * InvDir[1];
    TMin = std::max(TMin, std::min(Ty0, Ty1));
    TMax = std::min(TMax, std::max(Ty0, Ty1));
    if (TMax < TMin) return false;

    const double Tz0 = (double(Cell.MinZ) - Origin[2]) * InvDir[2];
    const double Tz1 = (double(Cell.MaxZ) - Origin[2]) * InvDir[2];
    TMin = std::max(TMin, std::min(Tz0, Tz1));
    TMax = std::min(TMax, std::max(Tz0, Tz1));
    if (TMax < TMin) return false;

    OutNear = TMin;
    return true;
}

[[nodiscard]] double DistanceSquaredToBox(const SketcherReferenceBvhCell& Cell, const Vector3& Query) noexcept
{
    double Sum = 0.0;
    if (Query.X < double(Cell.MinX))      { const double D = double(Cell.MinX) - Query.X; Sum += D * D; }
    else if (Query.X > double(Cell.MaxX)) { const double D = Query.X - double(Cell.MaxX); Sum += D * D; }
    if (Query.Y < double(Cell.MinY))      { const double D = double(Cell.MinY) - Query.Y; Sum += D * D; }
    else if (Query.Y > double(Cell.MaxY)) { const double D = Query.Y - double(Cell.MaxY); Sum += D * D; }
    if (Query.Z < double(Cell.MinZ))      { const double D = double(Cell.MinZ) - Query.Z; Sum += D * D; }
    else if (Query.Z > double(Cell.MaxZ)) { const double D = Query.Z - double(Cell.MaxZ); Sum += D * D; }
    return Sum;
}

// Ericson Real-Time Collision Detection §5.1.5 closest point on triangle ABC with barycentric coordinates (U, V, W).
void ClosestPointOnTriangle(
    const Vector3& P,
    const Vector3& A,
    const Vector3& B,
    const Vector3& C,
    Vector3&       OutPoint,
    double&        OutU,
    double&        OutV,
    double&        OutW) noexcept
{
    const Vector3 Ab = B - A;
    const Vector3 Ac = C - A;
    const Vector3 Ap = P - A;
    const double  D1 = Dot(Ab, Ap);
    const double  D2 = Dot(Ac, Ap);
    if (D1 <= 0.0 && D2 <= 0.0)
    {
        OutPoint = A; OutU = 1.0; OutV = 0.0; OutW = 0.0;
        return;
    }

    const Vector3 Bp = P - B;
    const double  D3 = Dot(Ab, Bp);
    const double  D4 = Dot(Ac, Bp);
    if (D3 >= 0.0 && D4 <= D3)
    {
        OutPoint = B; OutU = 0.0; OutV = 1.0; OutW = 0.0;
        return;
    }

    const double Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        const double V = D1 / (D1 - D3);
        OutPoint = A + Ab * V; OutU = 1.0 - V; OutV = V; OutW = 0.0;
        return;
    }

    const Vector3 Cp = P - C;
    const double  D5 = Dot(Ab, Cp);
    const double  D6 = Dot(Ac, Cp);
    if (D6 >= 0.0 && D5 <= D6)
    {
        OutPoint = C; OutU = 0.0; OutV = 0.0; OutW = 1.0;
        return;
    }

    const double Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        const double W = D2 / (D2 - D6);
        OutPoint = A + Ac * W; OutU = 1.0 - W; OutV = 0.0; OutW = W;
        return;
    }

    const double Va = D3 * D6 - D5 * D4;
    if (Va <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        const double W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        OutPoint = B + (C - B) * W; OutU = 0.0; OutV = 1.0 - W; OutW = W;
        return;
    }

    const double Denom = 1.0 / (Va + Vb + Vc);
    const double V     = Vb * Denom;
    const double W     = Vc * Denom;
    OutU     = 1.0 - V - W;
    OutV     = V;
    OutW     = W;
    OutPoint = A + Ab * V + Ac * W;
}

[[nodiscard]] bool PartVisibleCondition(const SketcherReferenceSurface& Surface, uint32_t PartIndex) noexcept
{
    if (Surface.Parts.empty()) return true;
    return PartIndex < Surface.Parts.size() ? Surface.Parts[PartIndex].VisibleCondition : true;
}

void BuildCellRecursive(
    SketcherReferenceSurfaceBvh&        Bvh,
    const std::vector<TriangleBounds>&  Bounds,
    uint32_t                            CellIndex,
    uint32_t                            First,
    uint32_t                            Count)
{
    float MinX = BoundInfinity, MinY = BoundInfinity, MinZ = BoundInfinity;
    float MaxX = -BoundInfinity, MaxY = -BoundInfinity, MaxZ = -BoundInfinity;
    float CMinX = BoundInfinity, CMinY = BoundInfinity, CMinZ = BoundInfinity;
    float CMaxX = -BoundInfinity, CMaxY = -BoundInfinity, CMaxZ = -BoundInfinity;

    for (uint32_t Offset = 0u; Offset < Count; ++Offset)
    {
        const TriangleBounds& Tb = Bounds[Bvh.TriangleIndices[First + Offset]];
        MinX  = std::min(MinX, Tb.MinX);  MinY  = std::min(MinY, Tb.MinY);  MinZ  = std::min(MinZ, Tb.MinZ);
        MaxX  = std::max(MaxX, Tb.MaxX);  MaxY  = std::max(MaxY, Tb.MaxY);  MaxZ  = std::max(MaxZ, Tb.MaxZ);
        CMinX = std::min(CMinX, Tb.CenterX); CMinY = std::min(CMinY, Tb.CenterY); CMinZ = std::min(CMinZ, Tb.CenterZ);
        CMaxX = std::max(CMaxX, Tb.CenterX); CMaxY = std::max(CMaxY, Tb.CenterY); CMaxZ = std::max(CMaxZ, Tb.CenterZ);
    }

    Bvh.Cells[CellIndex].MinX = MinX;
    Bvh.Cells[CellIndex].MinY = MinY;
    Bvh.Cells[CellIndex].MinZ = MinZ;
    Bvh.Cells[CellIndex].MaxX = MaxX;
    Bvh.Cells[CellIndex].MaxY = MaxY;
    Bvh.Cells[CellIndex].MaxZ = MaxZ;

    if (Count <= LeafTriangleCap)
    {
        Bvh.Cells[CellIndex].LeftOrFirst = First;
        Bvh.Cells[CellIndex].Count       = Count;
        return;
    }

    const float ExtentX = CMaxX - CMinX;
    const float ExtentY = CMaxY - CMinY;
    const float ExtentZ = CMaxZ - CMinZ;
    int Axis = 0;
    if (ExtentY > ExtentX && ExtentY >= ExtentZ) Axis = 1;
    else if (ExtentZ > ExtentX && ExtentZ > ExtentY) Axis = 2;

    const float Span = (Axis == 0) ? ExtentX : (Axis == 1) ? ExtentY : ExtentZ;
    if (Span <= 1.0e-9f)
    {
        Bvh.Cells[CellIndex].LeftOrFirst = First;
        Bvh.Cells[CellIndex].Count       = Count;
        return;
    }

    const uint32_t Mid = First + (Count >> 1u);
    auto BeginIt = Bvh.TriangleIndices.begin() + First;
    auto MidIt   = Bvh.TriangleIndices.begin() + Mid;
    auto EndIt   = Bvh.TriangleIndices.begin() + (First + Count);
    std::nth_element(BeginIt, MidIt, EndIt, [&](uint32_t LeftTri, uint32_t RightTri)
    {
        if (Axis == 0) return Bounds[LeftTri].CenterX < Bounds[RightTri].CenterX;
        if (Axis == 1) return Bounds[LeftTri].CenterY < Bounds[RightTri].CenterY;
        return Bounds[LeftTri].CenterZ < Bounds[RightTri].CenterZ;
    });

    const uint32_t LeftChild  = static_cast<uint32_t>(Bvh.Cells.size());
    Bvh.Cells.emplace_back();
    Bvh.Cells.emplace_back();
    Bvh.Cells[CellIndex].LeftOrFirst = LeftChild;
    Bvh.Cells[CellIndex].Count       = 0u;

    BuildCellRecursive(Bvh, Bounds, LeftChild, First, Mid - First);
    BuildCellRecursive(Bvh, Bounds, LeftChild + 1u, Mid, (First + Count) - Mid);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherReferenceSurfaceBvh BuildReferenceSurfaceBvh(const std::vector<SketcherReferenceCorner>& Corners)
{
    SketcherReferenceSurfaceBvh Bvh;
    const uint32_t TriangleCount = static_cast<uint32_t>(Corners.size() / 3u);
    if (TriangleCount == 0u) return Bvh;

    std::vector<TriangleBounds> Bounds(TriangleCount);
    Bvh.TriangleIndices.resize(TriangleCount);
    std::iota(Bvh.TriangleIndices.begin(), Bvh.TriangleIndices.end(), 0u);

    for (uint32_t Tri = 0u; Tri < TriangleCount; ++Tri)
    {
        const SketcherReferenceCorner& C0 = Corners[size_t(Tri) * 3u + 0u];
        const SketcherReferenceCorner& C1 = Corners[size_t(Tri) * 3u + 1u];
        const SketcherReferenceCorner& C2 = Corners[size_t(Tri) * 3u + 2u];
        TriangleBounds& Tb = Bounds[Tri];
        Tb.MinX    = std::min({ C0.Position[0], C1.Position[0], C2.Position[0] });
        Tb.MinY    = std::min({ C0.Position[1], C1.Position[1], C2.Position[1] });
        Tb.MinZ    = std::min({ C0.Position[2], C1.Position[2], C2.Position[2] });
        Tb.MaxX    = std::max({ C0.Position[0], C1.Position[0], C2.Position[0] });
        Tb.MaxY    = std::max({ C0.Position[1], C1.Position[1], C2.Position[1] });
        Tb.MaxZ    = std::max({ C0.Position[2], C1.Position[2], C2.Position[2] });
        Tb.CenterX = (C0.Position[0] + C1.Position[0] + C2.Position[0]) * (1.0f / 3.0f);
        Tb.CenterY = (C0.Position[1] + C1.Position[1] + C2.Position[1]) * (1.0f / 3.0f);
        Tb.CenterZ = (C0.Position[2] + C1.Position[2] + C2.Position[2]) * (1.0f / 3.0f);
    }

    Bvh.Cells.reserve(size_t(TriangleCount) * 2u);
    Bvh.Cells.emplace_back();
    BuildCellRecursive(Bvh, Bounds, 0u, 0u, TriangleCount);
    return Bvh;
}

std::optional<SketcherReferenceHit> IntersectReferenceSurface(
    const SketcherReferenceSurface&  Surface,
    uint32_t                         SurfaceIndex,
    const Vector3&                   RayOrigin,
    const Vector3&                   RayDirection,
    double                           MaxDistance,
    const std::array<double, 4>*     SectionPlane)
{
    if (!Surface.VisibleCondition || !Surface.Geometry || Surface.Geometry->Bvh.Cells.empty()) return std::nullopt;

    const auto& Corners = Surface.Geometry->Corners;
    const auto& Bvh     = Surface.Geometry->Bvh;
    const double Origin[3] = { RayOrigin.X, RayOrigin.Y, RayOrigin.Z };
    const double InvDir[3] = {
        std::abs(RayDirection.X) > 1.0e-18 ? 1.0 / RayDirection.X : (RayDirection.X >= 0.0 ? 1.0e18 : -1.0e18),
        std::abs(RayDirection.Y) > 1.0e-18 ? 1.0 / RayDirection.Y : (RayDirection.Y >= 0.0 ? 1.0e18 : -1.0e18),
        std::abs(RayDirection.Z) > 1.0e-18 ? 1.0 / RayDirection.Z : (RayDirection.Z >= 0.0 ? 1.0e18 : -1.0e18),
    };

    double RootNear = 0.0;
    if (!IntersectBoxSlab(Bvh.Cells[0], Origin, InvDir, MaxDistance, RootNear)) return std::nullopt;

    double BestT = MaxDistance;
    std::optional<SketcherReferenceHit> BestHit;

    uint32_t Stack[96];
    int Top = 0;
    Stack[Top++] = 0u;

    while (Top > 0)
    {
        const SketcherReferenceBvhCell& Cell = Bvh.Cells[Stack[--Top]];
        if (Cell.Count > 0u)
        {
            for (uint32_t Offset = 0u; Offset < Cell.Count; ++Offset)
            {
                const uint32_t TriIndex = Bvh.TriangleIndices[Cell.LeftOrFirst + Offset];
                const SketcherReferenceCorner& C0 = Corners[size_t(TriIndex) * 3u + 0u];
                if (!PartVisibleCondition(Surface, C0.PartIndex)) continue;

                const SketcherReferenceCorner& C1 = Corners[size_t(TriIndex) * 3u + 1u];
                const SketcherReferenceCorner& C2 = Corners[size_t(TriIndex) * 3u + 2u];

                const Vector3 V0{ C0.Position[0], C0.Position[1], C0.Position[2] };
                const Vector3 V1{ C1.Position[0], C1.Position[1], C1.Position[2] };
                const Vector3 V2{ C2.Position[0], C2.Position[1], C2.Position[2] };

                const Vector3 Edge1 = V1 - V0;
                const Vector3 Edge2 = V2 - V0;
                const Vector3 Pvec  = Cross(RayDirection, Edge2);
                const double  Det   = Dot(Edge1, Pvec);
                if (std::abs(Det) < 1.0e-14) continue;

                const double  InvDet = 1.0 / Det;
                const Vector3 Tvec   = RayOrigin - V0;
                const double  U      = Dot(Tvec, Pvec) * InvDet;
                if (U < -1.0e-7 || U > 1.0 + 1.0e-7) continue;

                const Vector3 Qvec = Cross(Tvec, Edge1);
                const double  V    = Dot(RayDirection, Qvec) * InvDet;
                if (V < -1.0e-7 || U + V > 1.0 + 1.0e-7) continue;

                const double T = Dot(Edge2, Qvec) * InvDet;
                if (T <= 1.0e-7 || T >= BestT) continue;

                const Vector3 HitPos = RayOrigin + RayDirection * T;
                if (SectionPlane != nullptr)
                {
                    const auto& Pl = *SectionPlane;
                    if (Pl[0] * HitPos.X + Pl[1] * HitPos.Y + Pl[2] * HitPos.Z + Pl[3] < 0.0) continue;
                }

                const double W = std::max(0.0, 1.0 - U - V);
                Vector3 Normal = NormalizeSafe({
                    W * double(C0.Normal[0]) + U * double(C1.Normal[0]) + V * double(C2.Normal[0]),
                    W * double(C0.Normal[1]) + U * double(C1.Normal[1]) + V * double(C2.Normal[1]),
                    W * double(C0.Normal[2]) + U * double(C1.Normal[2]) + V * double(C2.Normal[2]),
                });
                if (Dot(Normal, RayDirection) > 0.0) Normal = Normal * -1.0;

                BestT   = T;
                BestHit = SketcherReferenceHit{ HitPos, Normal, T, SurfaceIndex, C0.PartIndex, TriIndex };
            }
        }
        else
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = LeftIdx + 1u;
            double NearLeft = 0.0, NearRight = 0.0;
            const bool HitLeft  = IntersectBoxSlab(Bvh.Cells[LeftIdx], Origin, InvDir, BestT, NearLeft);
            const bool HitRight = IntersectBoxSlab(Bvh.Cells[RightIdx], Origin, InvDir, BestT, NearRight);
            if (HitLeft && HitRight)
            {
                if (NearLeft <= NearRight)
                {
                    if (Top < 94) { Stack[Top++] = RightIdx; Stack[Top++] = LeftIdx; }
                }
                else
                {
                    if (Top < 94) { Stack[Top++] = LeftIdx; Stack[Top++] = RightIdx; }
                }
            }
            else if (HitLeft)  { if (Top < 95) Stack[Top++] = LeftIdx; }
            else if (HitRight) { if (Top < 95) Stack[Top++] = RightIdx; }
        }
    }

    return BestHit;
}

std::optional<SketcherReferenceHit> IntersectReferenceSurfaces(
    const std::vector<SketcherReferenceSurface>& Surfaces,
    const Vector3&                               RayOrigin,
    const Vector3&                               RayDirection,
    double                                       MaxDistance,
    const std::array<double, 4>*                 SectionPlane)
{
    double BestT = MaxDistance;
    std::optional<SketcherReferenceHit> BestHit;
    for (uint32_t Index = 0u; Index < Surfaces.size(); ++Index)
    {
        const auto Hit = IntersectReferenceSurface(Surfaces[Index], Index, RayOrigin, RayDirection, BestT, SectionPlane);
        if (Hit && Hit->Distance < BestT)
        {
            BestT   = Hit->Distance;
            BestHit = Hit;
        }
    }
    return BestHit;
}

std::optional<SketcherReferenceHit> ProjectOntoReferenceSurface(
    const SketcherReferenceSurface&  Surface,
    uint32_t                         SurfaceIndex,
    const Vector3&                   QueryPoint,
    double                           MaxRadius)
{
    if (!Surface.VisibleCondition || !Surface.Geometry || Surface.Geometry->Bvh.Cells.empty()) return std::nullopt;

    const auto& Corners = Surface.Geometry->Corners;
    const auto& Bvh     = Surface.Geometry->Bvh;
    double BestDistSq   = MaxRadius >= 1.0e14 ? 1.0e28 : MaxRadius * MaxRadius;

    if (DistanceSquaredToBox(Bvh.Cells[0], QueryPoint) > BestDistSq) return std::nullopt;

    std::optional<SketcherReferenceHit> BestHit;
    uint32_t Stack[96];
    int Top = 0;
    Stack[Top++] = 0u;

    while (Top > 0)
    {
        const SketcherReferenceBvhCell& Cell = Bvh.Cells[Stack[--Top]];
        if (DistanceSquaredToBox(Cell, QueryPoint) > BestDistSq) continue;

        if (Cell.Count > 0u)
        {
            for (uint32_t Offset = 0u; Offset < Cell.Count; ++Offset)
            {
                const uint32_t TriIndex = Bvh.TriangleIndices[Cell.LeftOrFirst + Offset];
                const SketcherReferenceCorner& C0 = Corners[size_t(TriIndex) * 3u + 0u];
                if (!PartVisibleCondition(Surface, C0.PartIndex)) continue;

                const SketcherReferenceCorner& C1 = Corners[size_t(TriIndex) * 3u + 1u];
                const SketcherReferenceCorner& C2 = Corners[size_t(TriIndex) * 3u + 2u];
                const Vector3 A{ C0.Position[0], C0.Position[1], C0.Position[2] };
                const Vector3 B{ C1.Position[0], C1.Position[1], C1.Position[2] };
                const Vector3 C{ C2.Position[0], C2.Position[1], C2.Position[2] };

                Vector3 Closest;
                double U = 0.0, V = 0.0, W = 0.0;
                ClosestPointOnTriangle(QueryPoint, A, B, C, Closest, U, V, W);
                const Vector3 Delta = Closest - QueryPoint;
                const double  DistSq = Dot(Delta, Delta);
                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    const Vector3 Normal = NormalizeSafe({
                        U * double(C0.Normal[0]) + V * double(C1.Normal[0]) + W * double(C2.Normal[0]),
                        U * double(C0.Normal[1]) + V * double(C1.Normal[1]) + W * double(C2.Normal[1]),
                        U * double(C0.Normal[2]) + V * double(C1.Normal[2]) + W * double(C2.Normal[2]),
                    });
                    BestHit = SketcherReferenceHit{ Closest, Normal, std::sqrt(DistSq), SurfaceIndex, C0.PartIndex, TriIndex };
                }
            }
        }
        else
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = LeftIdx + 1u;
            const double LeftDistSq  = DistanceSquaredToBox(Bvh.Cells[LeftIdx], QueryPoint);
            const double RightDistSq = DistanceSquaredToBox(Bvh.Cells[RightIdx], QueryPoint);
            if (LeftDistSq <= RightDistSq)
            {
                if (RightDistSq <= BestDistSq && Top < 95) Stack[Top++] = RightIdx;
                if (LeftDistSq <= BestDistSq && Top < 95)  Stack[Top++] = LeftIdx;
            }
            else
            {
                if (LeftDistSq <= BestDistSq && Top < 95)  Stack[Top++] = LeftIdx;
                if (RightDistSq <= BestDistSq && Top < 95) Stack[Top++] = RightIdx;
            }
        }
    }

    return BestHit;
}

std::optional<SketcherReferenceHit> ProjectOntoReferenceSurfaces(
    const std::vector<SketcherReferenceSurface>& Surfaces,
    const Vector3&                               QueryPoint,
    double                                       MaxRadius)
{
    double BestDist = MaxRadius;
    std::optional<SketcherReferenceHit> BestHit;
    for (uint32_t Index = 0u; Index < Surfaces.size(); ++Index)
    {
        const auto Hit = ProjectOntoReferenceSurface(Surfaces[Index], Index, QueryPoint, BestDist);
        if (Hit && Hit->Distance < BestDist)
        {
            BestDist = Hit->Distance;
            BestHit  = Hit;
        }
    }
    return BestHit;
}

void RescaleReferenceSurface(SketcherReferenceSurface& Surface, double RelativeFactor)
{
    if (!Surface.Geometry || RelativeFactor <= 1.0e-9 || std::abs(RelativeFactor - 1.0) <= 1.0e-12) return;

    auto Rebuilt = std::make_shared<SketcherReferenceGeometry>();
    Rebuilt->Corners        = Surface.Geometry->Corners;
    Rebuilt->CreaseSegments = Surface.Geometry->CreaseSegments;
    const float ScaleF = static_cast<float>(RelativeFactor);
    for (SketcherReferenceCorner& Corner : Rebuilt->Corners)
    {
        Corner.Position[0] *= ScaleF;
        Corner.Position[1] *= ScaleF;
        Corner.Position[2] *= ScaleF;
    }
    for (SketcherReferenceSegment& Segment : Rebuilt->CreaseSegments)
    {
        Segment.Start[0] *= ScaleF; Segment.Start[1] *= ScaleF; Segment.Start[2] *= ScaleF;
        Segment.End[0]   *= ScaleF; Segment.End[1]   *= ScaleF; Segment.End[2]   *= ScaleF;
    }
    Rebuilt->Bvh = BuildReferenceSurfaceBvh(Rebuilt->Corners);
    Surface.Geometry = std::move(Rebuilt);

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Surface.MinBound[Axis] *= RelativeFactor;
        Surface.MaxBound[Axis] *= RelativeFactor;
    }
    for (SketcherReferencePart& Part : Surface.Parts)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Part.MinBound[Axis] *= RelativeFactor;
            Part.MaxBound[Axis] *= RelativeFactor;
        }
    }
    Surface.ScaleFactor *= RelativeFactor;
}

} // namespace Frontier::Workspaces::Sketcher
