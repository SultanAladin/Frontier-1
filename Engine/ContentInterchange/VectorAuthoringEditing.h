/*============================================================================================================================================
                                                              VECTORAUTHORINGEDITING.H
============================================================================================================================================*/
// 🧩 Shared source-only arrays, enclosure navigation and selected-channel edits for vector and future texture authoring.

#pragma once
#include "VectorAuthoringDocument.h"
#include "VectorAuthoringArrayPlacement.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct VectorAuthoringArrayResult
{
    VectorAuthoringDocument Document;
    std::vector<uint32_t>  Selection;
};
//------------------------------------------------------------------------------------------------------------------------
//                                                     AUTHORING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool QueryVectorAuthoringEnclosureContains(const VectorAuthoringDocument &Document, uint32_t BoundaryToken, uint32_t EnclosureToken);
void RelocateVectorAuthoringEnclosure(VectorAuthoringDocument &Document, uint32_t EnclosureToken, uint32_t DestinationToken);
void NormalizeVectorAuthoringCaptions(VectorAuthoringDocument &Document);
bool ApplyVectorAuthoringChannelPatch(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringChannelPatch &Patch);
VectorAuthoringArrayResult GenerateVectorAuthoringArray(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringArraySpecification &Specification);
}
