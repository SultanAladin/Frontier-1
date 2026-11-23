//============================================================================================================================================
//                                                     GEOMETRYFORMATEXCHANGE.H
//============================================================================================================================================
// 🧩 The import/export format boundary for geometry. Codecs translate bytes; geometry ownership remains elsewhere.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/AssetInterchange/Api/AssetInterchange.h"
#include "SlateDocument/Format/GeometryCodec/Api/GeometryCodec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

struct GeometryFormatCapability
{
    GeometryContentSubject Subject = GeometryContentSubject::Unrecognised;
    bool ImportSupported = false;
    bool ExportSupported = false;
    bool PolygonFacesRetained = false;
    bool NamedObjectsAndGroupsRetained = false;
    bool MaterialAssignmentsRetained = false;
    bool MaterialDefinitionsRetained = false;
};

/// 🧩 Dispatches geometry streams to isolated format codecs and reports the exact capability standing.
/// note  It does not register document geometry, derive render triangles, or own codec-vendor objects.
class GeometryFormatExchange
{
public:
    GeometryFormatCapability Capability(const std::string& OriginPath) const;

    /// Decodes one complete stream faithfully. OBJ is the first delivered adapter; glTF/GLB follows.
    Deliver<DecodedTopology> Decode(const std::vector<std::uint8_t>& Stream,
                                    const std::string& OriginPath) const;
};

} // namespace Slate
