/*============================================================================================================================================
                                                     PARAMETRICPOLYGONBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParametricPolygonBridge: PolygonComplex / ModelPartition -> Frontier::GeometrySpace::PolygonNetwork

#include "Engine/GeometrySpace/Parametric/ParametricPolygonBridge.h"

#include <algorithm>
#include <array>

namespace Frontier::GeometrySpace::Parametric
{

bool ConvertPolygonComplexToNetwork(
    const PolygonComplex& Complex,
    std::string_view      NetworkTitle,
    PolygonNetwork&       OutNetwork)
{
    OutNetwork.Reset();
    OutNetwork.NetworkTitle = NetworkTitle.empty() ? std::string("ParametricBody") : std::string(NetworkTitle);

    if (Complex.Positions.empty() || Complex.Triangles.empty())
    {
        return false;
    }

    OutNetwork.ReserveStorage(
        static_cast<uint32_t>(Complex.Positions.size()),
        static_cast<uint32_t>(Complex.Triangles.size() * 3u),
        static_cast<uint32_t>(Complex.Triangles.size()));

    for (const Vector3& Pos : Complex.Positions)
    {
        (void)OutNetwork.AppendVertex({
            static_cast<float>(Pos.X),
            static_cast<float>(Pos.Y),
            static_cast<float>(Pos.Z),
        });
    }

    FaceShadingAssignment DefaultShading {};
    DefaultShading.AlbedoLinearRGB = { 0.48f, 0.54f, 0.62f };
    DefaultShading.Metallic        = 0.25f;
    DefaultShading.Roughness       = 0.34f;
    DefaultShading.MatCapBlend     = 0.72f;

    const bool HasCharts = Complex.TriangleCharts.size() == Complex.Triangles.size();
    const bool HasFaces  = Complex.TriangleFaces.size() == Complex.Triangles.size();

    for (std::size_t TriIdx = 0u; TriIdx < Complex.Triangles.size(); ++TriIdx)
    {
        const auto& Tri = Complex.Triangles[TriIdx];
        const std::array<uint32_t, 3> LoopIndices {{
            static_cast<uint32_t>(Tri[0]),
            static_cast<uint32_t>(Tri[1]),
            static_cast<uint32_t>(Tri[2]),
        }};

        std::array<Mathematics::UVAxes, 3> CornerUVs {};
        if (HasCharts)
        {
            for (std::size_t C = 0u; C < 3u; ++C)
            {
                CornerUVs[C] = Mathematics::UVAxes {
                    static_cast<float>(Complex.TriangleCharts[TriIdx][C].X),
                    static_cast<float>(Complex.TriangleCharts[TriIdx][C].Y),
                };
            }
        }

        const uint16_t FaceOrdinal = HasFaces
            ? static_cast<uint16_t>(std::max(0, Complex.TriangleFaces[TriIdx]) + 1)
            : 1u;

        (void)OutNetwork.AppendFace(LoopIndices, CornerUVs, DefaultShading, FaceOrdinal, 1u);
    }

    OutNetwork.LinkOppositeEdges(false);
    OutNetwork.RecomputeNormals();
    OutNetwork.MarkDihedralSharpSeams(32.0f, false);
    return OutNetwork.VerifyNetworkIntegrity();
}

bool TessellateBodyToPolygonNetwork(
    const ModelPartition&       Partition,
    BodyToken                   Body,
    const TessellationSettings& Settings,
    std::string_view            NetworkTitle,
    PolygonNetwork&             OutNetwork,
    std::string*                OutRefusal)
{
    const TessellationOutcome Outcome = TessellateBody(Partition, Body, Settings);
    if (!Outcome.Refusal.empty())
    {
        if (OutRefusal != nullptr)
        {
            *OutRefusal = Outcome.Refusal;
        }
        return false;
    }
    return ConvertPolygonComplexToNetwork(Outcome.Complex, NetworkTitle, OutNetwork);
}

} // namespace Frontier::GeometrySpace::Parametric
