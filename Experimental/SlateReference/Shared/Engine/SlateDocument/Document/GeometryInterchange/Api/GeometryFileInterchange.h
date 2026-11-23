//============================================================================================================================================
//                                                     GEOMETRYFILEINTERCHANGE.H
//============================================================================================================================================
// 🧩 Path-selected geometry transfer: drain first, decode faithfully, then register authoritative topology.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/GeometryInterchange/Api/GeometryInterchange.h"

#include <string>

namespace Slate
{

/// 🧩 Executes one approved geometry-source selection without giving codecs filesystem authority.
class GeometryFileInterchange
{
public:

    /// 🧩 Drains a selected source, decodes its complete byte stream, then registers it through GeometryInterchange.
    /// note  The format codec only receives the drained bytes and origin spelling. It cannot follow `mtllib` or
    ///       any other secondary path. Earcut and source face/corner retention remain wholly in document intake.
    Deliver<GeometryAssetView> Import(const std::string& OriginPath,
                                      const std::string& Naming,
                                      GeometryInterchange& Geometry,
                                      IntakeIndex& Intake) const;
};

}   // namespace Slate
