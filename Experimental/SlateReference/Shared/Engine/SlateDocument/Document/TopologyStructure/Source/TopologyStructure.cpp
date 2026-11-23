//============================================================================================================================================
//                                                         TOPOLOGYSTRUCTURE.CPP
//============================================================================================================================================
// 🧩 Corner run assembly and the seal that closes it.

#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <atomic>

namespace Slate
{

namespace
{

// 🔴 Revisions are registered from one monotonic sequence for the whole process, never counted per topology. A
//    per-topology count cannot discriminate: a topology is sealed exactly once and nothing unseals it, so every
//    sealed topology would report revision one and every gate comparing two of them would compare one against
//    one. Five gates read this — `38`'s conditioning, `40`'s subdivision, `68`'s partition, `16`'s enrollment and
//    `24`'s content key — and each of them exists to refuse a description of a *different* topology. Registered from
//    a shared sequence, two distinct seals never collide and each of those five refuses as its document says.
// 🧵 Atomic because `34` conditions off the tick and two workers may seal two intakes at once. The ordering
//    between two unrelated seals does not matter; that they receive different ordinals does.
std::atomic<std::uint64_t> SealIssuance { 0u };

std::uint64_t IssueRevision()
{
    return SealIssuance.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  DECLARATIONS AND SEAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TopologyStructure::DeclarePositions(const std::vector<DocumentPosition>& Incoming)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    VertexPositions = Incoming;

    return Deliver<bool>::Result(true);
}

Deliver<bool> TopologyStructure::DeclareFace(const std::vector<std::uint32_t>& CornerVertices)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    if (CornerVertices.size() < 3u)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "fewer than three corners is not a face" });

    for (const std::uint32_t VertexIndex : CornerVertices)
    {
        if (VertexIndex >= VertexPositions.size())
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "a corner addresses a vertex that was not declared" });
        }
    }

    const std::uint32_t FaceIndex = static_cast<std::uint32_t>(FaceFirstCorners.size());

    FaceFirstCorners.push_back(static_cast<std::uint32_t>(CornerVertexIndexs.size()));
    FaceCornerCounts.push_back(static_cast<std::uint32_t>(CornerVertices.size()));

    // 📝 A repeated ordinal within one run is accepted deliberately. `38` §3 enrolls it as degenerate and
    //    excludes it downstream; refusing it here would repair the artist's file, which `50` §2 ① forbids.
    for (const std::uint32_t VertexIndex : CornerVertices)
    {
        CornerVertexIndexs.push_back(VertexIndex);
        CornerFaceIndexs.push_back(FaceIndex);
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> TopologyStructure::DeclareCoordinates(const std::vector<DomainCoordinate>& Incoming)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    if (Incoming.size() != CornerVertexIndexs.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "one coordinate per corner is required" });

    CornerCoordinates = Incoming;

    return Deliver<bool>::Result(true);
}

Deliver<bool> TopologyStructure::DeclarePerpendiculars(const std::vector<SurfaceDirection>& Incoming)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    if (Incoming.size() != VertexPositions.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "one perpendicular per vertex is required" });

    VertexPerpendiculars = Incoming;

    return Deliver<bool>::Result(true);
}

Deliver<bool> TopologyStructure::DeclareTangentBases(const std::vector<TangentBasis>& Incoming)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    if (Incoming.size() != VertexPositions.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "one basis per vertex is required" });

    VertexTangentBases = Incoming;

    return Deliver<bool>::Result(true);
}

Deliver<bool> TopologyStructure::DeclareMaterialRegistration(const std::vector<std::uint32_t>& Incoming)
{
    if (SealDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the topology is sealed" });

    if (Incoming.size() != FaceFirstCorners.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "one enrollment per face is required" });

    FaceMaterialIndexs = Incoming;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TopologyStructure::Seal()
{
    if (SealDeclared)
        return Deliver<bool>::Result(true);

    if (FaceFirstCorners.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "a topology with no face is not texturable" });

    // 📝 One material for the whole owner where the source declared none — `50` §3's last-resort row. It is a
    //    default rather than an assumption, so nothing is reported: the artist assigns materials afterwards.
    if (FaceMaterialIndexs.empty())
        FaceMaterialIndexs.assign(FaceFirstCorners.size(), 0u);

    SealedRevision = IssueRevision();
    SealDeclared   = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

bool          TopologyStructure::Sealed() const      { return SealDeclared;   }
std::uint64_t TopologyStructure::Revision() const    { return SealedRevision; }

std::uint32_t TopologyStructure::VertexCount() const
{
    return static_cast<std::uint32_t>(VertexPositions.size());
}

std::uint32_t TopologyStructure::FaceCount() const
{
    return static_cast<std::uint32_t>(FaceFirstCorners.size());
}

std::uint32_t TopologyStructure::CornerCount() const
{
    return static_cast<std::uint32_t>(CornerVertexIndexs.size());
}

std::uint32_t TopologyStructure::FaceFirstCorner(std::uint32_t FaceIndex) const
{
    return FaceIndex < FaceFirstCorners.size() ? FaceFirstCorners[FaceIndex] : 0u;
}

std::uint32_t TopologyStructure::FaceCornerCount(std::uint32_t FaceIndex) const
{
    return FaceIndex < FaceCornerCounts.size() ? FaceCornerCounts[FaceIndex] : 0u;
}

std::uint32_t TopologyStructure::CornerVertex(std::uint32_t CornerIndex) const
{
    return CornerIndex < CornerVertexIndexs.size() ? CornerVertexIndexs[CornerIndex] : 0u;
}

std::uint32_t TopologyStructure::CornerFace(std::uint32_t CornerIndex) const
{
    return CornerIndex < CornerFaceIndexs.size() ? CornerFaceIndexs[CornerIndex] : 0u;
}

const std::vector<DocumentPosition>& TopologyStructure::Positions() const          { return VertexPositions;      }
const std::vector<DomainCoordinate>& TopologyStructure::Coordinates() const        { return CornerCoordinates;    }
const std::vector<SurfaceDirection>& TopologyStructure::Perpendiculars() const     { return VertexPerpendiculars; }
const std::vector<TangentBasis>&     TopologyStructure::TangentBases() const       { return VertexTangentBases;   }
const std::vector<std::uint32_t>&    TopologyStructure::MaterialRegistration() const { return FaceMaterialIndexs; }

bool TopologyStructure::CoordinatesSupplied() const    { return !CornerCoordinates.empty();    }
bool TopologyStructure::PerpendicularsSupplied() const { return !VertexPerpendiculars.empty(); }
bool TopologyStructure::TangentBasesSupplied() const   { return !VertexTangentBases.empty();   }

}   // namespace Slate
