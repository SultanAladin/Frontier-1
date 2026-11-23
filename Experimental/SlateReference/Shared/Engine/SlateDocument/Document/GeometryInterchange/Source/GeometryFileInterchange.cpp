//============================================================================================================================================
//                                                    GEOMETRYFILEINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 Host-authorized source draining before document-safe format and topology intake.

#include "SlateDocument/Document/GeometryInterchange/Api/GeometryFileInterchange.h"

#include "SlateDocument/Format/GeometryFormatExchange/Api/GeometryFormatExchange.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace Slate
{

namespace
{

constexpr std::uint64_t MaximumSourceExtent = 1024ull * 1024ull * 1024ull; // [B] - protects the interactive host

}

Deliver<GeometryAssetView> GeometryFileInterchange::Import(const std::string& OriginPath,
                                                            const std::string& Naming,
                                                            GeometryInterchange& Geometry,
                                                            IntakeIndex& Intake) const
{
    if (OriginPath.empty() || Naming.empty())
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::ContentUnsupported, "a geometry transfer requires both a source path and a name" });
    }

    std::error_code Error;
    const std::filesystem::path Selected(OriginPath);
    if (!std::filesystem::is_regular_file(Selected, Error) || Error)
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::HostDenied, "the selected geometry source is not a readable regular file" });
    }

    const std::uintmax_t Extent = std::filesystem::file_size(Selected, Error);
    if (Error)
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::HostDenied, "the selected geometry source extent could not be read" });
    }
    if (Extent == 0u || Extent > MaximumSourceExtent || Extent > std::numeric_limits<std::size_t>::max())
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::ExtentExhausted, "the selected geometry source has an unsupported byte extent" });
    }

    std::ifstream Source(Selected, std::ios::binary);
    if (!Source)
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::HostDenied, "the selected geometry source could not be opened" });
    }

    std::vector<std::uint8_t> Stream(static_cast<std::size_t>(Extent));
    Source.read(reinterpret_cast<char*>(Stream.data()), static_cast<std::streamsize>(Stream.size()));
    if (!Source || static_cast<std::size_t>(Source.gcount()) != Stream.size())
    {
        return Deliver<GeometryAssetView>::Refuse(
            { RefusalReason::HostDenied, "the selected geometry source could not be drained completely" });
    }

    GeometryFormatExchange Formats;
    const Deliver<DecodedTopology> Decoded = Formats.Decode(Stream, OriginPath);
    if (!Decoded.Resolved)
        return Deliver<GeometryAssetView>::Refuse(Decoded.Error);

    const Deliver<GeometryIdentity> Accepted = Geometry.AcceptDecoded(Decoded.Resolve(), Naming, Intake);
    if (!Accepted.Resolved)
        return Deliver<GeometryAssetView>::Refuse(Accepted.Error);

    return Geometry.Resolve(Accepted.Resolve());
}

}   // namespace Slate
