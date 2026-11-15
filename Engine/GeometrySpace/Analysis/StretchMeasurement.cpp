/*============================================================================================================================================
                                                            STRETCHMEASUREMENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of exact 2x2 UV-Jacobian singular value decomposition, Sander stretch, and Symmetric Dirichlet metrics

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Analysis/StretchMeasurement.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace
{

StretchMeasurementSummary EvaluateNetworkStretch(
    const PolygonNetwork&     Network,
    std::span<const uint32_t> OptionalSubsetFaces)
{
    StretchMeasurementSummary Summary {};
    Summary.PerFaceMetrics.resize(Network.Faces.size());
    if (Network.Faces.empty())
    {
        return Summary;
    }

    std::vector<uint8_t> IncludedMask;
    if (!OptionalSubsetFaces.empty())
    {
        IncludedMask.assign(Network.Faces.size(), 0u);
        for (const uint32_t FaceIdx : OptionalSubsetFaces)
        {
            if (FaceIdx < IncludedMask.size())
            {
                IncludedMask[FaceIdx] = 1u;
            }
        }
    }

    // 📝 Phase 1: Accumulate total 3D surface area and positive UV area for scale-invariant Sander normalization
    double TotalArea3D = 0.0;
    double TotalAreaUV = 0.0;

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        if (!IncludedMask.empty() && IncludedMask[FaceIdx] == 0u)
        {
            continue;
        }
        const float Area3D   = Network.EvaluateFaceArea3D(FaceIdx);
        const float SignedUV = Network.EvaluateFaceSignedAreaUV(FaceIdx);
        TotalArea3D += static_cast<double>(Area3D);
        if (SignedUV > 1.0e-11f)
        {
            TotalAreaUV += static_cast<double>(SignedUV);
        }
    }

    Summary.TotalSurfaceAreaSquareMeters = static_cast<float>(TotalArea3D);
    Summary.TotalPositiveUVArea          = static_cast<float>(TotalAreaUV);
    const float GlobalScaleFactor = (TotalArea3D > 1.0e-12 && TotalAreaUV > 1.0e-12)
        ? static_cast<float>(std::sqrt(TotalAreaUV / TotalArea3D))
        : 1.0f;

    // 📝 Phase 2: Evaluate exact 2x2 metric tensor singular values across triangulated polygon fans
    double WeightedL2SquaredAccum = 0.0;
    double WeightedDirichletAccum = 0.0;
    double WeightedConformalAccum = 0.0;
    float  WorstNormalizedSigma   = 1.0f;

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        if (!IncludedMask.empty() && IncludedMask[FaceIdx] == 0u)
        {
            continue;
        }

        const Face& CurrentFace = Network.Faces[FaceIdx];
        FaceStretchMetrics& Metrics = Summary.PerFaceMetrics[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        const float SignedFaceUV = Network.EvaluateFaceSignedAreaUV(FaceIdx);
        if (SignedFaceUV < -1.0e-10f)
        {
            Metrics.InvertedUVOrientation = true;
            ++Summary.InvertedUVFaceCount;
        }
        else if (std::fabs(SignedFaceUV) <= 1.0e-10f)
        {
            ++Summary.DegenerateUVFaceCount;
        }

        std::vector<uint32_t>            LoopVertices;
        std::vector<Mathematics::UVAxes> LoopUVs;
        LoopVertices.reserve(CurrentFace.CornerCount);
        LoopUVs.reserve(CurrentFace.CornerCount);

        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            LoopVertices.push_back(Network.Edges[Walk].OriginVertexIndex);
            LoopUVs.push_back(Network.Edges[Walk].ParameterUV);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        double FaceAreaAccum    = 0.0;
        double SigmaMaxAccum    = 0.0;
        double SigmaMinAccum    = 0.0;
        double SanderSqAccum    = 0.0;
        double DirichletAccum   = 0.0;
        double ConformalAccum   = 0.0;
        double AuthalicAccum    = 0.0;

        for (std::size_t Fan = 1u; Fan + 1u < LoopVertices.size(); ++Fan)
        {
            const Mathematics::Axis3D P0 = Network.Vertices[LoopVertices[0u]].Axes;
            const Mathematics::Axis3D P1 = Network.Vertices[LoopVertices[Fan]].Axes;
            const Mathematics::Axis3D P2 = Network.Vertices[LoopVertices[Fan + 1u]].Axes;

            const Mathematics::UVAxes UV0 = LoopUVs[0u];
            const Mathematics::UVAxes UV1 = LoopUVs[Fan];
            const Mathematics::UVAxes UV2 = LoopUVs[Fan + 1u];

            const float TriArea3D = 0.5f * Mathematics::EvaluateLength(Mathematics::EvaluateCross(P1 - P0, P2 - P0));
            if (TriArea3D <= 1.0e-11f)
            {
                continue;
            }

            const float dU1 = UV1.U - UV0.U;
            const float dV1 = UV1.V - UV0.V;
            const float dU2 = UV2.U - UV0.U;
            const float dV2 = UV2.V - UV0.V;
            const float TwiceAreaUV = dU1 * dV2 - dU2 * dV1;

            if (std::fabs(TwiceAreaUV) <= 1.0e-11f)
            {
                FaceAreaAccum  += static_cast<double>(TriArea3D);
                SigmaMaxAccum  += 100.0 * static_cast<double>(TriArea3D);
                SigmaMinAccum  += 0.01  * static_cast<double>(TriArea3D);
                SanderSqAccum  += 100.0 * static_cast<double>(TriArea3D);
                DirichletAccum += 5000.0 * static_cast<double>(TriArea3D);
                ConformalAccum += 100.0 * static_cast<double>(TriArea3D);
                continue;
            }

            // 📝 Sander et al. (2001): Ss = dP/dU, St = dP/dV
            const float InvDet = 1.0f / TwiceAreaUV;
            const Mathematics::Axis3D DerivU = ((P1 - P0) * dV2 - (P2 - P0) * dV1) * InvDet;
            const Mathematics::Axis3D DerivV = ((P2 - P0) * dU1 - (P1 - P0) * dU2) * InvDet;

            const float MetricA = Mathematics::EvaluateDot(DerivU, DerivU);
            const float MetricB = Mathematics::EvaluateDot(DerivU, DerivV);
            const float MetricC = Mathematics::EvaluateDot(DerivV, DerivV);

            const float Discriminant = std::sqrt(std::max(0.0f, (MetricA - MetricC) * (MetricA - MetricC) + 4.0f * MetricB * MetricB));
            const float RawSigmaMax  = std::sqrt(std::max(1.0e-12f, 0.5f * (MetricA + MetricC + Discriminant)));
            const float RawSigmaMin  = std::sqrt(std::max(1.0e-12f, 0.5f * (MetricA + MetricC - Discriminant)));

            // 📝 Normalize by GlobalScaleFactor so an isometric UV chart scores Sigma = 1.0 regardless of world size
            const float NormMax = std::clamp(RawSigmaMax * GlobalScaleFactor, 1.0e-4f, 1.0e4f);
            const float NormMin = std::clamp(RawSigmaMin * GlobalScaleFactor, 1.0e-4f, 1.0e4f);

            const float TriL2Sq      = 0.5f * (NormMax * NormMax + NormMin * NormMin);
            const float TriDirichlet = 0.5f * (NormMax * NormMax + (1.0f / (NormMax * NormMax)) +
                                               NormMin * NormMin + (1.0f / (NormMin * NormMin)));
            const float TriConformal = NormMax / std::max(1.0e-6f, NormMin);
            const float TriAuthalic  = 1.0f / std::max(1.0e-6f, NormMax * NormMin);

            FaceAreaAccum  += static_cast<double>(TriArea3D);
            SigmaMaxAccum  += static_cast<double>(RawSigmaMax)  * static_cast<double>(TriArea3D);
            SigmaMinAccum  += static_cast<double>(RawSigmaMin)  * static_cast<double>(TriArea3D);
            SanderSqAccum  += static_cast<double>(TriL2Sq)      * static_cast<double>(TriArea3D);
            DirichletAccum += static_cast<double>(TriDirichlet) * static_cast<double>(TriArea3D);
            ConformalAccum += static_cast<double>(TriConformal) * static_cast<double>(TriArea3D);
            AuthalicAccum  += static_cast<double>(TriAuthalic)  * static_cast<double>(TriArea3D);

            WorstNormalizedSigma = std::max(WorstNormalizedSigma, std::max(NormMax, 1.0f / NormMin));
        }

        if (FaceAreaAccum > 1.0e-12)
        {
            const double InvFaceArea       = 1.0 / FaceAreaAccum;
            Metrics.SingularMaxSigma         = static_cast<float>(SigmaMaxAccum * InvFaceArea);
            Metrics.SingularMinSigma         = static_cast<float>(SigmaMinAccum * InvFaceArea);
            Metrics.SanderL2Stretch          = static_cast<float>(std::sqrt(std::max(0.0, SanderSqAccum * InvFaceArea)));
            Metrics.SymmetricDirichletEnergy = static_cast<float>(DirichletAccum * InvFaceArea);
            Metrics.QuasiConformalRatio      = static_cast<float>(ConformalAccum * InvFaceArea);
            Metrics.AuthalicAreaRatio        = static_cast<float>(AuthalicAccum  * InvFaceArea);

            WeightedL2SquaredAccum += SanderSqAccum;
            WeightedDirichletAccum += DirichletAccum;
            WeightedConformalAccum += ConformalAccum;
        }
    }

    if (TotalArea3D > 1.0e-12)
    {
        Summary.AreaWeightedL2Stretch        = static_cast<float>(std::sqrt(std::max(0.0, WeightedL2SquaredAccum / TotalArea3D)));
        Summary.MeanSymmetricDirichletEnergy = static_cast<float>(WeightedDirichletAccum / TotalArea3D);
        Summary.MeanQuasiConformalRatio      = static_cast<float>(WeightedConformalAccum / TotalArea3D);
        Summary.WorstFaceLInfinityStretch    = WorstNormalizedSigma;
    }

    return Summary;
}

} // namespace Frontier::GeometrySpace
