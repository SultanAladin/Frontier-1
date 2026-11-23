//============================================================================================================================================
//                                                   GEOMETRYFORMATEXCHANGE.CPP
//============================================================================================================================================

#include "SlateDocument/Format/GeometryFormatExchange/Api/GeometryFormatExchange.h"

namespace Slate
{

GeometryFormatCapability GeometryFormatExchange::Capability(const std::string& OriginPath) const
{
    GeometryFormatCapability Reported;
    Reported.Subject = ClassifyContent(OriginPath);
    if (Reported.Subject == GeometryContentSubject::Wavefront)
    {
        Reported.ImportSupported = true;
        Reported.ExportSupported = false;
        Reported.PolygonFacesRetained = true;
        Reported.NamedObjectsAndGroupsRetained = true;
        Reported.MaterialAssignmentsRetained = true;
        // This stream-only overload cannot safely follow an OBJ's external mtllib path.
        Reported.MaterialDefinitionsRetained = false;
    }
    return Reported;
}

Deliver<DecodedTopology> GeometryFormatExchange::Decode(const std::vector<std::uint8_t>& Stream,
                                                        const std::string& OriginPath) const
{
    const GeometryFormatCapability Supported = Capability(OriginPath);
    if (!Supported.ImportSupported)
    {
        return Deliver<DecodedTopology>::Refuse(
            { RefusalReason::ContentUnsupported, "no geometry import codec accepts this format" });
    }
    return Translate(Stream, OriginPath);
}

} // namespace Slate
