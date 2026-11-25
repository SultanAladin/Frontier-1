/*============================================================================================================================================
                                                            CURVATUREANALYSIS.CPP
============================================================================================================================================*/
// 🧩 Implementation of Meyer discrete differential curvature and Mikkelsen/Duff continuous corner tangent bases

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometrySpace
{

namespace
{

[[nodiscard]] float SafeCotangent(Mathematics::Axis3D EdgeFromVertexA, Mathematics::Axis3D EdgeFromVertexB) noexcept
{
    const float DotProduct   = Mathematics::EvaluateDot(EdgeFromVertexA, EdgeFromVertexB);
    const float SinMagnitude = Mathematics::EvaluateLength(Mathematics::EvaluateCross(EdgeFromVertexA, EdgeFromVertexB));
    if (SinMagnitude <= 1.0e-8f)
    {
        return 0.0f;
    }
    return std::clamp(DotProduct / SinMagnitude, -1.0e4f, 1.0e4f);
}

[[nodiscard]] float SafeInteriorAngle(Mathematics::Axis3D EdgeFromVertexA, Mathematics::Axis3D EdgeFromVertexB) noexcept
{
    const float LenA = Mathematics::EvaluateLength(EdgeFromVertexA);
    const float LenB = Mathematics::EvaluateLength(EdgeFromVertexB);
    if (LenA <= 1.0e-8f || LenB <= 1.0e-8f)
    {
        return 0.0f;
    }
    const float CosTheta = std::clamp(Mathematics::EvaluateDot(EdgeFromVertexA, EdgeFromVertexB) / (LenA * LenB), -1.0f, 1.0f);
    return std::acos(CosTheta);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           DUFF ET AL. (2017) ORTHONORMAL BASIS
//------------------------------------------------------------------------------------------------------------------------

void ConstructDuffOrthonormalBasis(
    Mathematics::Axis3D  UnitNormal,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept
{
    const Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(UnitNormal, { 0.0f, 1.0f, 0.0f });
    const float SignZ  = std::copysign(1.0f, Normal.Z);
    const float CoeffA = -1.0f / (SignZ + Normal.Z);
    const float CoeffB = Normal.X * Normal.Y * CoeffA;

    OutTangent   = Mathematics::NormalizeAxis({ 1.0f + SignZ * Normal.X * Normal.X * CoeffA, SignZ * CoeffB, -SignZ * Normal.X }, { 1.0f, 0.0f, 0.0f });
    OutBitangent = Mathematics::NormalizeAxis({ CoeffB, SignZ + Normal.Y * Normal.Y * CoeffA, -Normal.Y }, { 0.0f, 0.0f, 1.0f });
}

//------------------------------------------------------------------------------------------------------------------------
//                                       MEYER DISCRETE MEAN & GAUSSIAN CURVATURE
//------------------------------------------------------------------------------------------------------------------------

CurvatureAnalysisSummary EvaluateSurfaceCurvature(const PolygonNetwork& Network)
{
    CurvatureAnalysisSummary Summary {};
    const std::size_t VertexCount = Network.Vertices.size();
    Summary.PerVertexMetrics.resize(VertexCount);
    if (VertexCount == 0u || Network.Faces.empty())
    {
        return Summary;
    }

    std::vector<double>   MixedAreaAccum(VertexCount, 0.0);
    std::vector<double>   AngleSumAccum(VertexCount, 0.0);
    std::vector<uint8_t>  BoundaryVertexMarker(VertexCount, 0u);
    std::vector<Mathematics::Axis3D> LaplaceAccum(VertexCount, { 0.0f, 0.0f, 0.0f });

    for (const DirectedEdge& Edge : Network.Edges)
    {
        if ((Edge.OppositeEdgeIndex == InvalidNetworkIndex ||
             QueryEdgeAttributeEnabled(Edge.AttributeMask, EdgeAttributeMask::EnableBoundaryEdge)) &&
            Edge.OriginVertexIndex < VertexCount)
        {
            BoundaryVertexMarker[Edge.OriginVertexIndex] = 1u;
            if (Edge.NextEdgeIndex < Network.Edges.size())
            {
                const uint32_t DestV = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
                if (DestV < VertexCount)
                {
                    BoundaryVertexMarker[DestV] = 1u;
                }
            }
        }
    }

    // 📝 Fan-triangulate each polygon face and accumulate Meyer mixed Voronoi area, interior angles, and cotangent weights
    for (const Face& CurrentFace : Network.Faces)
    {
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopVertices;
        LoopVertices.reserve(CurrentFace.CornerCount);
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            LoopVertices.push_back(Network.Edges[Walk].OriginVertexIndex);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        for (std::size_t Fan = 1u; Fan + 1u < LoopVertices.size(); ++Fan)
        {
            const std::array<uint32_t, 3u> Tri {{ LoopVertices[0u], LoopVertices[Fan], LoopVertices[Fan + 1u] }};
            if (Tri[0u] >= VertexCount || Tri[1u] >= VertexCount || Tri[2u] >= VertexCount)
            {
                continue;
            }

            const Mathematics::Axis3D Pos0 = Network.Vertices[Tri[0u]].Axes;
            const Mathematics::Axis3D Pos1 = Network.Vertices[Tri[1u]].Axes;
            const Mathematics::Axis3D Pos2 = Network.Vertices[Tri[2u]].Axes;

            const float TriArea = 0.5f * Mathematics::EvaluateLength(Mathematics::EvaluateCross(Pos1 - Pos0, Pos2 - Pos0));
            if (TriArea <= 1.0e-10f)
            {
                continue;
            }

            const float Angle0 = SafeInteriorAngle(Pos1 - Pos0, Pos2 - Pos0);
            const float Angle1 = SafeInteriorAngle(Pos2 - Pos1, Pos0 - Pos1);
            const float Angle2 = SafeInteriorAngle(Pos0 - Pos2, Pos1 - Pos2);

            AngleSumAccum[Tri[0u]] += static_cast<double>(Angle0);
            AngleSumAccum[Tri[1u]] += static_cast<double>(Angle1);
            AngleSumAccum[Tri[2u]] += static_cast<double>(Angle2);

            const float Cot0 = SafeCotangent(Pos1 - Pos0, Pos2 - Pos0);
            const float Cot1 = SafeCotangent(Pos2 - Pos1, Pos0 - Pos1);
            const float Cot2 = SafeCotangent(Pos0 - Pos2, Pos1 - Pos2);

            // 📝 Cotangent Laplace-Beltrami operator: sum_{j} (cot alpha_ij + cot beta_ij) (p_i - p_j)
            LaplaceAccum[Tri[0u]] = LaplaceAccum[Tri[0u]] + (Pos0 - Pos1) * Cot2 + (Pos0 - Pos2) * Cot1;
            LaplaceAccum[Tri[1u]] = LaplaceAccum[Tri[1u]] + (Pos1 - Pos2) * Cot0 + (Pos1 - Pos0) * Cot2;
            LaplaceAccum[Tri[2u]] = LaplaceAccum[Tri[2u]] + (Pos2 - Pos0) * Cot1 + (Pos2 - Pos1) * Cot0;

            const bool Obtuse0 = (Angle0 > 0.5f * Constants::PiFloat);
            const bool Obtuse1 = (Angle1 > 0.5f * Constants::PiFloat);
            const bool Obtuse2 = (Angle2 > 0.5f * Constants::PiFloat);

            if (Obtuse0 || Obtuse1 || Obtuse2)
            {
                MixedAreaAccum[Tri[0u]] += static_cast<double>(Obtuse0 ? (0.5f * TriArea) : (0.25f * TriArea));
                MixedAreaAccum[Tri[1u]] += static_cast<double>(Obtuse1 ? (0.5f * TriArea) : (0.25f * TriArea));
                MixedAreaAccum[Tri[2u]] += static_cast<double>(Obtuse2 ? (0.5f * TriArea) : (0.25f * TriArea));
            }
            else
            {
                const float LenSq01 = Mathematics::EvaluateLengthSquared(Pos1 - Pos0);
                const float LenSq12 = Mathematics::EvaluateLengthSquared(Pos2 - Pos1);
                const float LenSq20 = Mathematics::EvaluateLengthSquared(Pos0 - Pos2);

                MixedAreaAccum[Tri[0u]] += 0.125 * static_cast<double>(LenSq01 * Cot2 + LenSq20 * Cot1);
                MixedAreaAccum[Tri[1u]] += 0.125 * static_cast<double>(LenSq12 * Cot0 + LenSq01 * Cot2);
                MixedAreaAccum[Tri[2u]] += 0.125 * static_cast<double>(LenSq20 * Cot1 + LenSq12 * Cot0);
            }
        }
    }

    double WeightedAbsMeanSum = 0.0;
    double TotalAreaSum       = 0.0;
    double IntegratedGaussian = 0.0;

    for (std::size_t VIdx = 0u; VIdx < VertexCount; ++VIdx)
    {
        VertexCurvatureEntry& Entry = Summary.PerVertexMetrics[VIdx];
        const double SafeArea       = std::max(1.0e-10, MixedAreaAccum[VIdx]);
        Entry.MixedVoronoiAreaSquareMeters = static_cast<float>(MixedAreaAccum[VIdx]);

        if (MixedAreaAccum[VIdx] <= 1.0e-10)
        {
            continue;
        }

        const float InvTwoArea = static_cast<float>(1.0 / (2.0 * SafeArea));
        Entry.MeanCurvatureNormal = LaplaceAccum[VIdx] * InvTwoArea;

        const float UnsignedMean = 0.5f * Mathematics::EvaluateLength(Entry.MeanCurvatureNormal);
        const float NormalSign   = (Mathematics::EvaluateDot(Entry.MeanCurvatureNormal, Network.Vertices[VIdx].Normal) >= 0.0f) ? 1.0f : -1.0f;
        Entry.MeanCurvaturePerMeter = NormalSign * UnsignedMean;

        const double TargetAngle = (BoundaryVertexMarker[VIdx] != 0u) ? Constants::Pi : Constants::Tau;
        const double AngleDeficit = TargetAngle - AngleSumAccum[VIdx];
        Entry.GaussianCurvaturePerSqMeter = static_cast<float>(AngleDeficit / SafeArea);

        const float Discriminant = std::max(
            0.0f,
            Entry.MeanCurvaturePerMeter * Entry.MeanCurvaturePerMeter - Entry.GaussianCurvaturePerSqMeter);
        const float RootDisc = std::sqrt(Discriminant);
        Entry.PrincipalMaximumPerMeter = Entry.MeanCurvaturePerMeter + RootDisc;
        Entry.PrincipalMinimumPerMeter = Entry.MeanCurvaturePerMeter - RootDisc;
        Entry.CavityConcavityFactor    = std::clamp(0.5f + 0.25f * std::tanh(Entry.MeanCurvaturePerMeter), 0.0f, 1.0f);

        TotalAreaSum       += MixedAreaAccum[VIdx];
        WeightedAbsMeanSum += static_cast<double>(UnsignedMean) * MixedAreaAccum[VIdx];
        IntegratedGaussian += AngleDeficit;
    }

    Summary.TotalMixedAreaSquareMeters = static_cast<float>(TotalAreaSum);
    Summary.AreaWeightedMeanCurvature  = (TotalAreaSum > 1.0e-10) ? static_cast<float>(WeightedAbsMeanSum / TotalAreaSum) : 0.0f;
    Summary.IntegratedGaussianRadians  = static_cast<float>(IntegratedGaussian);
    Summary.GaussBonnetEulerEstimate   = static_cast<float>(IntegratedGaussian / Constants::Tau);

    for (const VertexCurvatureEntry& Entry : Summary.PerVertexMetrics)
    {
        Summary.MaximumAbsMeanCurvature = std::max(Summary.MaximumAbsMeanCurvature, std::fabs(Entry.MeanCurvaturePerMeter));
    }

    return Summary;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  MIKKELSEN / DUFF CONTINUOUS CORNER TANGENT BASES
//------------------------------------------------------------------------------------------------------------------------

std::vector<CornerTangentBasis> EvaluateContinuousCornerTangents(const PolygonNetwork& Network)
{
    std::vector<CornerTangentBasis> CornerBases(Network.Edges.size());
    if (Network.Edges.empty() || Network.Faces.empty())
    {
        return CornerBases;
    }

    // 📝 Step 0: Always derive crisp geometric face normals from live vertices (safe even if caller skipped RecomputeNormals())
    std::vector<Mathematics::Axis3D> FaceNormals(Network.Faces.size(), { 0.0f, 1.0f, 0.0f });
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            FaceNormals[FaceIdx] = CurrentFace.GeometricNormal;
            continue;
        }
        const uint32_t E0 = CurrentFace.BoundaryLoopEdgeIndex;
        const uint32_t E1 = (E0 < Network.Edges.size()) ? Network.Edges[E0].NextEdgeIndex : InvalidNetworkIndex;
        if (E0 >= Network.Edges.size() || E1 >= Network.Edges.size())
        {
            FaceNormals[FaceIdx] = CurrentFace.GeometricNormal;
            continue;
        }
        const Mathematics::Axis3D P0 = Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes;
        Mathematics::Axis3D AreaNormal { 0.0f, 0.0f, 0.0f };
        uint32_t WalkEdge = E1;
        for (uint16_t Step = 1u; Step + 1u < CurrentFace.CornerCount && WalkEdge < Network.Edges.size(); ++Step)
        {
            const uint32_t NextWalk = Network.Edges[WalkEdge].NextEdgeIndex;
            if (NextWalk >= Network.Edges.size())
            {
                break;
            }
            const Mathematics::Axis3D P1 = Network.Vertices[Network.Edges[WalkEdge].OriginVertexIndex].Axes;
            const Mathematics::Axis3D P2 = Network.Vertices[Network.Edges[NextWalk].OriginVertexIndex].Axes;
            AreaNormal = AreaNormal + Mathematics::EvaluateCross(P1 - P0, P2 - P0);
            WalkEdge   = NextWalk;
        }
        const Mathematics::Axis3D FallbackNorm =
            (Mathematics::EvaluateLengthSquared(CurrentFace.GeometricNormal) > 1.0e-6f)
                ? CurrentFace.GeometricNormal
                : Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f };
        FaceNormals[FaceIdx] = Mathematics::NormalizeAxis(AreaNormal, FallbackNorm);
    }

    // 📝 Step 1: Compute crease-aware smooth corner normals (hard creases split normal smoothing; smooth seams still share normals!)
    std::vector<Mathematics::Axis3D> CornerNormals(Network.Edges.size(), { 0.0f, 1.0f, 0.0f });
    std::vector<std::vector<uint32_t>> CornersAtVertex(Network.Vertices.size());
    for (uint32_t EdgeIdx = 0u; EdgeIdx < static_cast<uint32_t>(Network.Edges.size()); ++EdgeIdx)
    {
        const uint32_t OrigV = Network.Edges[EdgeIdx].OriginVertexIndex;
        if (OrigV < CornersAtVertex.size() && Network.Edges[EdgeIdx].EnclosingFaceIndex < Network.Faces.size())
        {
            CornersAtVertex[OrigV].push_back(EdgeIdx);
        }
    }

    const float CreaseCosThreshold = std::cos(55.0f * Constants::DegToRadFloat);

    for (uint32_t VIdx = 0u; VIdx < static_cast<uint32_t>(CornersAtVertex.size()); ++VIdx)
    {
        const auto& IncidentCorners = CornersAtVertex[VIdx];
        for (const uint32_t EdgeA : IncidentCorners)
        {
            const uint32_t FaceIdxA = Network.Edges[EdgeA].EnclosingFaceIndex;
            const Mathematics::Axis3D NormA = FaceNormals[FaceIdxA];
            const bool HardIncidentA =
                QueryEdgeAttributeEnabled(Network.Edges[EdgeA].AttributeMask, EdgeAttributeMask::EnableSharpCrease) ||
                (Network.Edges[EdgeA].PreviousEdgeIndex < Network.Edges.size() &&
                 QueryEdgeAttributeEnabled(Network.Edges[Network.Edges[EdgeA].PreviousEdgeIndex].AttributeMask, EdgeAttributeMask::EnableSharpCrease));

            Mathematics::Axis3D BlendedNormal { 0.0f, 0.0f, 0.0f };
            for (const uint32_t EdgeB : IncidentCorners)
            {
                const uint32_t FaceIdxB = Network.Edges[EdgeB].EnclosingFaceIndex;
                const Mathematics::Axis3D NormB = FaceNormals[FaceIdxB];
                const float FaceDot = Mathematics::EvaluateDot(NormA, NormB);
                if (EdgeA != EdgeB && ((HardIncidentA && FaceDot < CreaseCosThreshold) || FaceDot < 0.35f))
                {
                    continue;
                }
                const uint32_t NextB   = Network.Edges[EdgeB].NextEdgeIndex;
                const uint32_t PrevB   = Network.Edges[EdgeB].PreviousEdgeIndex;
                float          Weight  = 1.0f;
                if (NextB < Network.Edges.size() && PrevB < Network.Edges.size())
                {
                    const auto OriginPos = Network.Vertices[VIdx].Axes;
                    const auto EdgeFwd   = Network.Vertices[Network.Edges[NextB].OriginVertexIndex].Axes - OriginPos;
                    const auto EdgeBack  = Network.Vertices[Network.Edges[PrevB].OriginVertexIndex].Axes - OriginPos;
                    Weight               = std::max(1.0e-5f, SafeInteriorAngle(EdgeFwd, EdgeBack));
                }
                BlendedNormal = BlendedNormal + NormB * Weight;
            }
            CornerNormals[EdgeA] = Mathematics::NormalizeAxis(BlendedNormal, NormA);
        }
    }

    // 📝 Step 2: Evaluate per-corner unsmoothed UV-Jacobian tangent & bitangent (Mikkelsen first-order derivatives)
    std::vector<Mathematics::Axis3D> RawTangent(Network.Edges.size(), { 0.0f, 0.0f, 0.0f });
    std::vector<Mathematics::Axis3D> RawBitangent(Network.Edges.size(), { 0.0f, 0.0f, 0.0f });
    std::vector<float>               RawHandedness(Network.Edges.size(), 1.0f);

    for (const Face& CurrentFace : Network.Faces)
    {
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            const DirectedEdge& CurrEdge = Network.Edges[Walk];
            const DirectedEdge& NextEdge = Network.Edges[CurrEdge.NextEdgeIndex];
            const DirectedEdge& PrevEdge = Network.Edges[CurrEdge.PreviousEdgeIndex];

            const Mathematics::Axis3D Pos0 = Network.Vertices[CurrEdge.OriginVertexIndex].Axes;
            const Mathematics::Axis3D Pos1 = Network.Vertices[NextEdge.OriginVertexIndex].Axes;
            const Mathematics::Axis3D Pos2 = Network.Vertices[PrevEdge.OriginVertexIndex].Axes;

            const Mathematics::Axis3D Edge1 = Pos1 - Pos0;
            const Mathematics::Axis3D Edge2 = Pos2 - Pos0;
            const Mathematics::UVAxes dUV1  = NextEdge.ParameterUV - CurrEdge.ParameterUV;
            const Mathematics::UVAxes dUV2  = PrevEdge.ParameterUV - CurrEdge.ParameterUV;

            const float DetUV = dUV1.U * dUV2.V - dUV2.U * dUV1.V;
            const float InteriorWeight = std::max(1.0e-5f, SafeInteriorAngle(Edge1, Edge2));

            if (std::fabs(DetUV) > 1.0e-9f)
            {
                const float InvDet = 1.0f / DetUV;
                const Mathematics::Axis3D UnitT = Mathematics::NormalizeAxis((Edge1 * dUV2.V - Edge2 * dUV1.V) * InvDet, { 1.0f, 0.0f, 0.0f });
                const Mathematics::Axis3D UnitB = Mathematics::NormalizeAxis((Edge2 * dUV1.U - Edge1 * dUV2.U) * InvDet, { 0.0f, 0.0f, 1.0f });
                RawTangent[Walk]    = UnitT * InteriorWeight;
                RawBitangent[Walk]  = UnitB * InteriorWeight;
                RawHandedness[Walk] = (DetUV >= 0.0f) ? 1.0f : -1.0f;
            }
            else
            {
                Mathematics::Axis3D FallbackT {};
                Mathematics::Axis3D FallbackB {};
                ConstructDuffOrthonormalBasis(CornerNormals[Walk], FallbackT, FallbackB);
                RawTangent[Walk]    = FallbackT * InteriorWeight;
                RawBitangent[Walk]  = FallbackB * InteriorWeight;
                RawHandedness[Walk] = 1.0f;
            }

            Walk = CurrEdge.NextEdgeIndex;
        }
    }

    // 📝 Step 3: Weld compatible corners sharing vertex, UV coordinate, normal alignment, and UV handedness (MikkTSpace rule)
    for (uint32_t VIdx = 0u; VIdx < static_cast<uint32_t>(CornersAtVertex.size()); ++VIdx)
    {
        const auto& IncidentCorners = CornersAtVertex[VIdx];
        for (const uint32_t EdgeA : IncidentCorners)
        {
            Mathematics::Axis3D AccumT = RawTangent[EdgeA];
            Mathematics::Axis3D AccumB = RawBitangent[EdgeA];

            for (const uint32_t EdgeB : IncidentCorners)
            {
                if (EdgeA == EdgeB)
                {
                    continue;
                }
                const bool SameUV   = Mathematics::EvaluateUVDistance(Network.Edges[EdgeA].ParameterUV, Network.Edges[EdgeB].ParameterUV) <= 1.0e-4f;
                const bool SameSign = (RawHandedness[EdgeA] * RawHandedness[EdgeB]) > 0.0f;
                const bool SameNorm = Mathematics::EvaluateDot(CornerNormals[EdgeA], CornerNormals[EdgeB]) >= 0.985f;
                if (SameUV && SameSign && SameNorm)
                {
                    AccumT = AccumT + RawTangent[EdgeB];
                    AccumB = AccumB + RawBitangent[EdgeB];
                }
            }

            const Mathematics::Axis3D UnitN = CornerNormals[EdgeA];
            Mathematics::Axis3D       ProjT = AccumT - UnitN * Mathematics::EvaluateDot(UnitN, AccumT);
            if (Mathematics::EvaluateLengthSquared(ProjT) <= 1.0e-10f)
            {
                Mathematics::Axis3D DuffB {};
                ConstructDuffOrthonormalBasis(UnitN, ProjT, DuffB);
            }
            else
            {
                ProjT = Mathematics::NormalizeAxis(ProjT);
            }

            const float Sign = (Mathematics::EvaluateDot(Mathematics::EvaluateCross(UnitN, ProjT), AccumB) < 0.0f) ? -1.0f : 1.0f;
            CornerBases[EdgeA] = CornerTangentBasis{ ProjT, UnitN, Sign };
        }
    }

    return CornerBases;
}

Mathematics::Axis3D ReconstructCornerBitangent(const CornerTangentBasis& Basis) noexcept
{
    return Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Basis.NormalAxis, Basis.TangentAxis) * Basis.HandednessSign, { 0.0f, 0.0f, 1.0f });
}

uint32_t PackOctahedralDirection32(Mathematics::Axis3D UnitDirection) noexcept
{
    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(UnitDirection, { 0.0f, 1.0f, 0.0f });
    const float InvL1 = 1.0f / std::max(1.0e-8f, std::fabs(Dir.X) + std::fabs(Dir.Y) + std::fabs(Dir.Z));
    float OctX = Dir.X * InvL1;
    float OctY = Dir.Y * InvL1;
    if (Dir.Z < 0.0f)
    {
        const float FoldX = (1.0f - std::fabs(OctY)) * ((OctX >= 0.0f) ? 1.0f : -1.0f);
        const float FoldY = (1.0f - std::fabs(OctX)) * ((OctY >= 0.0f) ? 1.0f : -1.0f);
        OctX = FoldX;
        OctY = FoldY;
    }

    const auto QuantX = static_cast<int16_t>(std::lround(std::clamp(OctX, -1.0f, 1.0f) * 32767.0f));
    const auto QuantY = static_cast<int16_t>(std::lround(std::clamp(OctY, -1.0f, 1.0f) * 32767.0f));
    return static_cast<uint32_t>(static_cast<uint16_t>(QuantX)) |
           (static_cast<uint32_t>(static_cast<uint16_t>(QuantY)) << 16u);
}

Mathematics::Axis3D UnpackOctahedralDirection32(uint32_t PackedWord) noexcept
{
    const auto QuantX = static_cast<int16_t>(PackedWord & 0xFFFFu);
    const auto QuantY = static_cast<int16_t>((PackedWord >> 16u) & 0xFFFFu);
    float OctX = std::clamp(static_cast<float>(QuantX) / 32767.0f, -1.0f, 1.0f);
    float OctY = std::clamp(static_cast<float>(QuantY) / 32767.0f, -1.0f, 1.0f);
    float OctZ = 1.0f - std::fabs(OctX) - std::fabs(OctY);
    if (OctZ < 0.0f)
    {
        const float OrigX = OctX;
        OctX = (1.0f - std::fabs(OctY)) * ((OrigX >= 0.0f) ? 1.0f : -1.0f);
        OctY = (1.0f - std::fabs(OrigX)) * ((OctY >= 0.0f) ? 1.0f : -1.0f);
    }
    return Mathematics::NormalizeAxis({ OctX, OctY, OctZ }, { 0.0f, 1.0f, 0.0f });
}

} // namespace Frontier::GeometrySpace
