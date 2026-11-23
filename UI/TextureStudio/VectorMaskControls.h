/*============================================================================================================================================
                                                       VECTORMASKCONTROLS.H
============================================================================================================================================*/
// 🧩 Shared icon-only Boolean commands and editable GPU-mask descriptors for native Pattern and Stamp inspectors.

#pragma once
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/Interface/PixelSpace.h"
#include <optional>

namespace Frontier
{
std::optional<uint32_t> ConstructVectorBooleanControls(bool Enabled);
bool ConstructVectorAuthoringMaskControls(VectorAuthoringLayer &Layer, const std::array<float, 4> &SourceBounds);
}
