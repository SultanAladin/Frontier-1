/*============================================================================================================================================
                                                             POLYGONNETWORK.H
============================================================================================================================================*/
// 🧩 Half-edge PolygonNetwork storing contiguous Vertices, DirectedEdges, Faces, per-corner UVAxes, and per-face Material/MatCap assignments

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t InvalidNetworkIndex = 0xFFFFFFFFu; // [idx] - Unlinked vertex, half-edge, or face sentinel

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EdgeAttributeMask : uint8_t
{
    None               = 0u,      // [-] - Smooth interior shared edge
    EnableSeamCut      = 1u << 0, // [-] - UV seam split across OppositeEdge
    EnableSharpCrease  = 1u << 1, // [-] - Hard shading normal crease
    EnablePinLock      = 1u << 2, // [-] - Origin UV corner locked during flatten/relax
    EnableBoundaryEdge = 1u << 3, // [-] - Open geometric border with no OppositeEdge
};

[[nodiscard]] constexpr EdgeAttributeMask operator|(EdgeAttributeMask Left, EdgeAttributeMask Right) noexcept
{
    return static_cast<EdgeAttributeMask>(static_cast<uint8_t>(Left) | static_cast<uint8_t>(Right));
}

[[nodiscard]] constexpr EdgeAttributeMask operator&(EdgeAttributeMask Left, EdgeAttributeMask Right) noexcept
{
    return static_cast<EdgeAttributeMask>(static_cast<uint8_t>(Left) & static_cast<uint8_t>(Right));
}

[[nodiscard]] constexpr EdgeAttributeMask operator~(EdgeAttributeMask Operand) noexcept
{
    return static_cast<EdgeAttributeMask>(~static_cast<uint8_t>(Operand));
}

[[nodiscard]] constexpr bool QueryEdgeAttributeEnabled(EdgeAttributeMask Mask, EdgeAttributeMask Directive) noexcept
{
    return (static_cast<uint8_t>(Mask) & static_cast<uint8_t>(Directive)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FaceShadingAssignment
{
    Mathematics::Scalar3D AlbedoLinearRGB     { 0.30f, 0.49f, 0.79f }; // [0-1]  - Independent per-face surface Albedo Colour (linear RGB)
    Mathematics::Scalar3D MatCapTintLinearRGB { 1.00f, 1.00f, 1.00f }; // [0-1]  - Independent per-face Studio MatCap tint colour (linear RGB)
    float                 Metallic            = 0.60f;                 // [0-1]  - Metallic response
    float                 Roughness           = 0.24f;                 // [0-1]  - Microfacet roughness
    float                 Specular            = 0.68f;                 // [0-1]  - Dielectric F0 specular response
    float                 MatCapBlend         = 0.62f;                 // [0-1]  - Blend weight in Material+MatCap mode
    uint8_t               MatCapSlot          = 5u;                    // [0-15] - Procedural engineering MatCap slot
};

struct Vertex
{
    Mathematics::Axis3D Axes              {};                  // [m]   - Exact 3D vertex coordinates
    Mathematics::Axis3D Normal            { 0.0f, 1.0f, 0.0f }; // [-]   - Area-weighted smooth vertex normal
    uint32_t            OutgoingEdgeIndex = InvalidNetworkIndex; // [idx] - First DirectedEdge originating at this Vertex
};

struct DirectedEdge
{
    uint32_t            OriginVertexIndex  = InvalidNetworkIndex; // [idx]  - Starting Vertex index
    uint32_t            OppositeEdgeIndex  = InvalidNetworkIndex; // [idx]  - Paired reverse DirectedEdge across shared edge
    uint32_t            NextEdgeIndex      = InvalidNetworkIndex; // [idx]  - Next DirectedEdge around EnclosingFace loop
    uint32_t            PreviousEdgeIndex  = InvalidNetworkIndex; // [idx]  - Previous DirectedEdge around EnclosingFace loop
    uint32_t            EnclosingFaceIndex = InvalidNetworkIndex; // [idx]  - Incident Face bounded by this loop
    Mathematics::UVAxes ParameterUV        {};                    // [uv]   - Per-corner UV coordinate at OriginVertex
    EdgeAttributeMask   AttributeMask      = EdgeAttributeMask::None; // [mask] - Seam, sharp-crease, pin & border directives
};

struct Face
{
    uint32_t              BoundaryLoopEdgeIndex = InvalidNetworkIndex;   // [idx] - Entry DirectedEdge of closed face loop
    Mathematics::Axis3D   GeometricNormal       { 0.0f, 1.0f, 0.0f };    // [-]   - Unit face normal
    FaceShadingAssignment Shading               {};                      // [-]   - Per-face Material & MatCap assignment
    uint16_t              CornerCount           = 0u;                    // [cnt] - Loop valence (3 = triangle, 4 = quad, N = n-gon)
    uint16_t              PriorityLayerOrdinal  = 0u;                    // [idx] - 0 = Unassigned Remainder; 1..N = Layer ordinal
    uint16_t              PartGroupOrdinal      = 0u;                    // [idx] - Logical mechanical part ordinal
};

class PolygonNetwork
{
public:
    std::string               NetworkTitle    { "UntitledNetwork" }; // [-]     - Human-readable asset identifier
    std::vector<Vertex>       Vertices        {};                    // [-]     - Contiguous 3D vertices
    std::vector<DirectedEdge> Edges           {};                    // [-]     - Contiguous directed half-edges
    std::vector<Face>         Faces           {};                    // [-]     - Contiguous polygonal faces
    uint64_t                  NetworkRevision = 1ull;                // [count] - Incremented on vertex/edge/face edits
    uint64_t                  UVRevision      = 1ull;                // [count] - Incremented on ParameterUV / seam / pin edits
    uint64_t                  ShadingRevision = 1ull;                // [count] - Incremented on FaceShadingAssignment edits

    void Reset();
    void ReserveStorage(uint32_t ExpectedVertices, uint32_t ExpectedEdges, uint32_t ExpectedFaces);

    [[nodiscard]] uint32_t AppendVertex(Mathematics::Axis3D Coordinates);

    [[nodiscard]] uint32_t AppendFace(
        std::span<const uint32_t>            VertexLoop,
        std::span<const Mathematics::UVAxes> CornerUVLoop         = {},
        const FaceShadingAssignment&         InitialShading       = {},
        uint16_t                             PriorityLayerOrdinal = 0u,
        uint16_t                             PartGroupOrdinal     = 0u);

    void LinkOppositeEdges(bool MarkUVDiscontinuitiesAsSeams = true);
    void RecomputeNormals();
    void MarkDihedralSharpSeams(float SharpThresholdDegrees, bool MarkAsUVSeam = false);

    void AssignEdgeSeam(uint32_t EdgeIndex, bool SeamEnabled) noexcept;
    void AssignCornerPin(uint32_t EdgeIndex, bool PinEnabled) noexcept;
    void AssignShadingToFaces(std::span<const uint32_t> TargetFaceIndices, const FaceShadingAssignment& Assignment) noexcept;
    void FillShadingAcrossAllFaces(const FaceShadingAssignment& Assignment) noexcept;

    [[nodiscard]] Mathematics::Axis3D EvaluateFaceCentroid3D(uint32_t FaceIndex) const noexcept;
    [[nodiscard]] Mathematics::UVAxes EvaluateFaceCentroidUV(uint32_t FaceIndex) const noexcept;
    [[nodiscard]] float               EvaluateFaceArea3D(uint32_t FaceIndex) const noexcept;
    [[nodiscard]] float               EvaluateFaceSignedAreaUV(uint32_t FaceIndex) const noexcept;
    [[nodiscard]] uint32_t            DeriveFaceUDIMNumber(uint32_t FaceIndex) const noexcept;

    [[nodiscard]] std::vector<float> ExtractParameterizedUVStream() const;
    [[nodiscard]] bool               ApplyParameterizedUVStream(std::span<const float> InterleavedUVPairs) noexcept;
    [[nodiscard]] bool               VerifyNetworkIntegrity() const noexcept;
};

} // namespace Frontier::GeometrySpace
