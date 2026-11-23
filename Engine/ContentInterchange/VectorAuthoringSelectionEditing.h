/*============================================================================================================================================
                                                        VECTORAUTHORINGSELECTIONEDITING.H
============================================================================================================================================*/
// 🧩 Shared atomic source edits for native vector selection, transforms, paint, Boolean expressions and reclamation.

#pragma once
#include "VectorAuthoringEditing.h"

namespace Frontier
{
struct VectorAuthoringSourcePatch
{
    float TranslationX = 0;
    float TranslationY = 0;
    float ScaleX       = 1;
    float ScaleY       = 1;
    float Rotation     = 0; // [rad]
    float ScaleBasis   = 0; // [rad] - Axes in which independent scale factors apply.
    std::optional<std::array<float, 4>> Colour;
    std::optional<std::array<float, 4>> ColourEnd;
    std::optional<std::array<float, 4>> StrokeColour;
    std::optional<float> Opacity;
    std::optional<float> StrokeWidth;
    std::optional<uint32_t> PaintCategory;
    std::optional<bool> FillEnabled;
};
VectorSelectionProgram ConstructVectorSelectionProgram(const VectorAuthoringDocument &Document);

struct VectorSelectionExtent
{
    std::array<float, 4> Bounds;
    float Rotation = 0; // [rad]
};
VectorSelectionExtent QueryVectorSelectionExtent(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
std::vector<uint32_t> QueryVectorSelectionRegion(
    const VectorAuthoringDocument &Document,
    std::array<float, 2>           Start,
    std::array<float, 2>           Finish,
    std::span<const uint32_t>     Existing = {});
VectorAuthoringSourcePatch EvaluateVectorSelectionResize(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<int, 2>             Direction,
    std::array<float, 2>           Pointer,
    bool                          Aspect,
    bool                          Centred);
VectorAuthoringSourcePatch EvaluateVectorSelectionRotation(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<float, 2>           Start,
    std::array<float, 2>           Pointer,
    bool                          Constrained);
std::array<float, 4> QueryVectorAuthoringSelectionBounds(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
void ReclaimVectorAuthoringSources(VectorAuthoringDocument &Document);
void TransformVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringSourcePatch &Patch);
uint32_t ConstructVectorAuthoringPrimitive(VectorAuthoringDocument &Document, VectorCoverageInstruction Source, std::string Caption);
void PartitionVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
VectorAuthoringDocument ExtractVectorAuthoringSelection(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
std::vector<uint32_t> IntegrateVectorAuthoringSelection(
    VectorAuthoringDocument       &Document,
    const VectorAuthoringDocument &Fragment,
    std::array<float, 2>           Displacement = {18, 18});
enum class VectorSelectionOrder
{
    Back, Backward, Forward, Front
};
void ReorderVectorAuthoringSelection(
    VectorAuthoringDocument   &Document,
    std::span<const uint32_t>  Selection,
    VectorSelectionOrder      Order);

uint32_t IntegrateVectorAuthoringBoolean(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, VectorCoverageCategory Category);
}
