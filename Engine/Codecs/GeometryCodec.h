/*============================================================================================================================================
                                                               GEOMETRYCODEC.H
============================================================================================================================================*/
// 🧩 .GeometrySpace Two-Tier Hybrid encoder & decoder for PolygonNetwork, FacePartitionRegister, ClusterPyramid & WideBoundingTree

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GeometrySpaceDocument
{
    GeometrySpace::PolygonNetwork         Network           {};    // [-]    - Complete half-edge PolygonNetwork (n-gons, seams, pins)
    GeometrySpace::FacePartitionRegister  Partitions        {};    // [-]    - Priority UV allocation layers
    std::vector<uint32_t>                 UDIMTiles         { 1001u }; // [code] - Active UDIM 1001-series tile codes
    SpatialAcceleration::ClusterPyramid   ClusterHierarchy  {};    // [-]    - Optional prebuilt 128-triangle LOD pyramid
    SpatialAcceleration::WideBoundingTree RayAcceleration   {};    // [-]    - Optional prebuilt 128 B 4-wide BVH4
    bool                                  ClustersIncluded  = false; // [-]  - True when ClusterHierarchy is persisted
    bool                                  WideTreeIncluded  = false; // [-]  - True when RayAcceleration is persisted
};

class GeometryEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload ConstructPayload(
        const GeometrySpaceDocument&       Document,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const GeometrySpaceDocument&       Document,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const GeometrySpaceDocument&       Document,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class GeometryDecoder
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload& Payload,
        GeometrySpaceDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   RawContainerBytes,
        GeometrySpaceDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        GeometrySpaceDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

} // namespace Frontier::Codecs
