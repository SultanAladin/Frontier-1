/*============================================================================================================================================
                                                            CUBICPATHEDITING.H
============================================================================================================================================*/
// 🧩 Reusable world-coordinate Bézier anchors and atomic retained-source edits for native vector authoring.

#pragma once
#include "VectorAuthoringSelectionEditing.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CUBIC SOURCE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct CubicAnchor
{
    std::array<float, 2> Position{};
    std::array<float, 2> Incoming{};
    std::array<float, 2> Outgoing{};
    bool operator==(const CubicAnchor &) const = default;
};
struct CubicPath
{
    std::vector<CubicAnchor> Anchors;
    bool Closed = false;
    uint32_t Winding = 0;
    bool operator==(const CubicPath &) const = default;
};
enum class CubicControl
{
    Anchor, Incoming, Outgoing
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCE AND POINT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 2> ConstrainCubicDirection(std::array<float, 2> Origin, std::array<float, 2> Destination);
void ValidateCubicPath(const CubicPath &Path);
void ReverseCubicPath(CubicPath &Path);
uint32_t IntegrateCubicPaths(
    VectorAuthoringDocument &Document,
    uint32_t                 RetainedToken,
    uint32_t                 ConsumedToken,
    bool                     RetainedStart = false,
    bool                     ConsumedEnd = false);
CubicPath DecodeCubicPath(const VectorAuthoringDocument &Document, uint32_t Token);
CubicPath DerivePrimitiveCubicPath(const VectorAuthoringDocument &Document, uint32_t Token);
uint32_t ConstructCubicPath(
    VectorAuthoringDocument   &Document,
    const CubicPath          &Path,
    VectorCoverageInstruction Style,
    std::string               Caption);
void ReplaceCubicPath(VectorAuthoringDocument &Document, uint32_t Token, const CubicPath &Path);
void TranslateCubicControl(
    CubicPath             &Path,
    size_t                 AnchorIndex,
    CubicControl           Control,
    std::array<float, 2>   Destination,
    bool                   Independent = false);
struct CubicSegmentProjection
{
    size_t SegmentIndex = 0;
    float Parameter = 0;
    float Distance = 0;
};
CubicSegmentProjection ProjectCubicSegment(const CubicPath &Path, std::array<float, 2> Position);
void BendCubicSegment(
    CubicPath             &Path,
    size_t                 SegmentIndex,
    float                  Parameter,
    std::array<float, 2>    Displacement);
void SubdivideCubicSegment(CubicPath &Path, size_t SegmentIndex, float Parameter = .5f);
void PartitionCubicAnchor(CubicPath &Path, size_t AnchorIndex);
void ConfigureCubicTangent(CubicPath &Path, size_t AnchorIndex, bool Smooth);
void TranslateCubicAnchors(CubicPath &Path, const std::vector<size_t> &Indices, std::array<float, 2> Displacement);
void PartitionCubicAnchors(CubicPath &Path, const std::vector<size_t> &Indices);
void ConfigureAutomaticCubicTangents(CubicPath &Path, const std::vector<size_t> &Indices);
}
