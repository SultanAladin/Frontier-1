//============================================================================================================================================
//                                                    EXTRUSIONSPECIFICATION.H
//============================================================================================================================================
// 🧩 The first modelling-operation seam: an exact extrusion request over a planar profile. The request is kept
//    separate from any current polygon-generation route so the CAD kernel can stand beside the existing pipeline.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"
#include "SlateShape/Topology/SolidStructure/Api/SolidStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class ExtrusionDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSpecification = 1u,
    ImplementationAbsent = 2u,
    Produced = 3u
};

struct ExtrusionSpecification
{
    ProfileName SourceProfile = {};
    SpatialDirection Direction = {};
    double Distance = 0.0;
    bool Symmetric = false;
    bool StartCap = true;
    bool EndCap = true;

    bool Declared() const;
};

ExtrusionDisposition EvaluateExtrusion(const ExtrusionSpecification& Declared);

Deliver<SolidStructure> ConstructExtrusion(const ProfileSpecification& SourceProfile,
                                          const std::vector<CurveSpecification>& SourceCurves,
                                          const ExtrusionSpecification& Declared);

} // namespace Slate
