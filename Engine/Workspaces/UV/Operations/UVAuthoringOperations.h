/*============================================================================================================================================
                                                         UVAUTHORINGOPERATIONS.H
============================================================================================================================================*/
// 🧩 Direct UV authoring operations: Seam Mark/Clear/Brush, UV Pinning, Edge Stitch, Split Island, Mirror, Align & Rectify Quads

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class UVAlignAxis : uint8_t
{
    HorizontalU = 0u, // [-] - Snap selected UV corners to median U
    VerticalV   = 1u, // [-] - Snap selected UV corners to median V
    MinimumU    = 2u, // [-] - Snap selected UV corners to minimum U
    MaximumU    = 3u, // [-] - Snap selected UV corners to maximum U
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class UVAuthoringOperations
{
public:
    // Seam authoring ('K' brush, Mark Seam, Clear Seam, Clear & Weld, Seams from Islands)
    static uint32_t MarkEdgesAsSeams(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      EdgeIndices,
        bool                           SeamEnabled);

    static uint32_t ClearSeamsWithUVWeld(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      EdgeIndices);

    static uint32_t DeriveSeamsFromIslands(GeometrySpace::PolygonNetwork& Network);

    // Interactive Seam Brush ('K'): marks/toggles single edge or entire Alt+EdgeLoop in one stroke
    static uint32_t ApplySeamBrushStroke(
        GeometrySpace::PolygonNetwork& Network,
        uint32_t                       HoveredEdgeIndex,
        bool                           ClearInsteadOfMark,
        bool                           LoopStroke);

    // UV Pinning ('P' / 'Alt+P')
    static uint32_t AssignPinLockOnFaces(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      FaceIndices,
        bool                           PinEnabled);

    static uint32_t AssignPinLockOnEdges(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      EdgeIndices,
        bool                           PinEnabled);

    static uint32_t AssignPinLockOnVertices(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      VertexIndices,
        bool                           PinEnabled);

    static uint32_t CountPinnedCorners(const GeometrySpace::PolygonNetwork& Network) noexcept;

    // Stitch ('V'):rigidly transforms the opposite UV island to snap & weld across selected seam edges
    static uint32_t StitchAcrossSelectedEdges(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      SeamEdgeIndices);

    // Split Island ('Y'): marks perimeter of selected faces as seams and offsets detached island slightly
    static uint32_t SplitFacesIntoDetachedIsland(
        GeometrySpace::PolygonNetwork&               Network,
        const GeometrySpace::ComponentSelectionMask& Selection,
        Mathematics::UVAxes                          DetachmentOffset = { 0.04f, 0.04f });

    // 2D UV Transforms (Translate, Rotate around pivot, Scale around pivot, Mirror U/V)
    static void TransformSelectedFaceUVs(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      FaceIndices,
        Mathematics::UVAxes            PivotUV,
        Mathematics::UVAxes            TranslationDelta,
        float                          RotationRadians,
        Mathematics::Scalar2D          ScaleFactor);

    static void TransformSelectedCornerUVs(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      CornerEdgeIndices,
        Mathematics::UVAxes            PivotUV,
        Mathematics::UVAxes            TranslationDelta,
        float                          RotationRadians,
        Mathematics::Scalar2D          ScaleFactor);

    [[nodiscard]] static std::vector<uint32_t> CollectTransformableCornerEdges(
        const GeometrySpace::PolygonNetwork&         Network,
        const GeometrySpace::ComponentSelectionMask& Selection);

    static void MirrorSelectedFaceUVs(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      FaceIndices,
        bool                           MirrorHorizontalU);

    // Align / Straighten / Rectify Quads
    static void AlignSelectedEdgeUVs(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      EdgeIndices,
        UVAlignAxis                    AxisMode);

    static void AlignSelectedVertexUVs(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      VertexIndices,
        UVAlignAxis                    AxisMode);

    static uint32_t RectifySelectedQuadFaces(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      FaceIndices);
};

} // namespace Frontier::Workspaces
