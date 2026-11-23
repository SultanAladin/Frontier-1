//============================================================================================================================================
//                                             GEOMETRYRENDERINGEXCHANGE.H
//============================================================================================================================================
// 🧩 Immutable CPU topology into revision-keyed, disposable rendering connectivity.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/Identity.h"
#include "SlateDocument/Document/GeometryInterchange/Api/GeometryInterchange.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct GeometryRenderingVertex
{
    DocumentPosition Position = {};
    SurfaceDirection Perpendicular = {};
    DomainCoordinate Coordinate = {};
    std::uint32_t SourceVertex = 0u;
    std::uint32_t SourceCorner = 0u;
};

struct GeometryRenderingTriangle
{
    std::uint32_t Corners[3] = {};
    std::uint32_t SourceFace = 0u;
    std::uint32_t MaterialIndex = 0u;
};

struct GeometryRenderingSegment
{
    std::uint32_t Corners[2] = {};
};

/// 🧩 One immutable CPU packet from which disposable GPU buffers can be uploaded.
/// note  Positions remain 64-bit here. A device upload must rebase before narrowing them.
struct GeometryRenderingSnapshot
{
    GeometryIdentity Geometry = {};
    std::uint64_t TopologyRevision = 0u;
    std::vector<GeometryRenderingVertex> Vertices = {};
    std::vector<GeometryRenderingTriangle> Triangles = {};
    std::vector<GeometryRenderingSegment> SourceWire = {};
    std::vector<GeometryRenderingSegment> TriangulatedWire = {};
    std::vector<std::uint32_t> UnpresentedFaces = {};
};

/// 🧩 Builds and retains rendering packets by authoritative geometry identity and topology revision.
/// note  Vulkan buffer allocation follows behind this seam; this increment delivers the immutable upload packet.
class GeometryRenderingExchange
{
public:
    Deliver<GeometryRenderingIdentity> Synchronise(const GeometryAssetView& Geometry);
    Deliver<const GeometryRenderingSnapshot*> Resolve(GeometryRenderingIdentity Subject) const;
    Deliver<bool> Retire(GeometryRenderingIdentity Subject);
    void Reclaim();
    std::uint32_t DeclaredCount() const { return OccupiedCount; }

private:
    struct Entry
    {
        GeometryRenderingSnapshot Snapshot = {};
        std::uint32_t Generation = 1u;
        bool Occupied = false;
    };

    std::vector<Entry> Entries = {};
    std::vector<std::uint32_t> ReleasedSlots = {};
    std::uint32_t OccupiedCount = 0u;
};

} // namespace Slate
