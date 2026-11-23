//============================================================================================================================================
//                                                     GEOMETRYINSTANCECOMPONENT.H
//============================================================================================================================================

#pragma once

#include "Foundation/Identity.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct GeometryInstanceComponent
{
    GeometryIdentity Geometry = {};
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
};

struct MaterialAssignmentComponent
{
    std::vector<std::uint32_t> MaterialBySlot = {};
};

struct SourceRecordComponent
{
    std::uint64_t ContentHash = 0u;
    std::uint32_t FormatIdentity = 0u;
    bool UnitAssumed = false;
};

} // namespace Slate
