/*============================================================================================================================================
                                                    POLYGONNETWORKDEFORMER.CPP
============================================================================================================================================*/
// 🧩 Phase D09 Bidirectional Frontier PolygonNetwork <-> HighPolySurfaceRest / Live Cage / VAT deformation bridge

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/PolygonNetworkDeformer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  POLYGON NETWORK DEFORMER
//------------------------------------------------------------------------------------------------------------------------

HighPolySurfaceRest PolygonNetworkDeformer::ExtractSurfaceRest(const PolygonNetwork& network)
{
    HighPolySurfaceRest out {};
    const uint32_t vCount = static_cast<uint32_t>(network.Vertices.size());
    if (vCount == 0u)
    {
        return out;
    }

    out.RestPositions.resize(static_cast<size_t>(vCount) * 3u, 0.0f);
    out.RestNormals.resize(static_cast<size_t>(vCount) * 3u, 0.0f);
    out.AlbedoRgb.assign(static_cast<size_t>(vCount) * 3u, 0.12f);
    out.MaterialPbr.assign(static_cast<size_t>(vCount) * 3u, 0.65f);

    for (uint32_t vi = 0u; vi < vCount; ++vi)
    {
        const Vertex& v = network.Vertices[vi];
        const size_t i3 = static_cast<size_t>(vi) * 3u;
        out.RestPositions[i3 + 0u] = v.Axes.X;
        out.RestPositions[i3 + 1u] = v.Axes.Y;
        out.RestPositions[i3 + 2u] = v.Axes.Z;

        const float nl = std::sqrt(v.Normal.X * v.Normal.X + v.Normal.Y * v.Normal.Y + v.Normal.Z * v.Normal.Z);
        if (nl > 1.0e-5f)
        {
            out.RestNormals[i3 + 0u] = v.Normal.X / nl;
            out.RestNormals[i3 + 1u] = v.Normal.Y / nl;
            out.RestNormals[i3 + 2u] = v.Normal.Z / nl;
        }
        else
        {
            out.RestNormals[i3 + 0u] = 0.0f;
            out.RestNormals[i3 + 1u] = 1.0f;
            out.RestNormals[i3 + 2u] = 0.0f;
        }
    }

    // Fan-triangulate each face loop and accumulate per-vertex shading attributes from incident faces
    std::vector<uint32_t> vertexFaceHits(vCount, 0u);
    std::vector<uint32_t> loopVerts;
    loopVerts.reserve(8u);

    for (const Face& face : network.Faces)
    {
        if (face.BoundaryLoopEdgeIndex == InvalidNetworkIndex || face.CornerCount < 3u)
        {
            continue;
        }
        loopVerts.clear();
        uint32_t edgeIdx = face.BoundaryLoopEdgeIndex;
        for (uint32_t guard = 0u; guard < face.CornerCount && edgeIdx < network.Edges.size(); ++guard)
        {
            const DirectedEdge& e = network.Edges[edgeIdx];
            if (e.OriginVertexIndex < vCount)
            {
                loopVerts.push_back(e.OriginVertexIndex);
            }
            edgeIdx = e.NextEdgeIndex;
            if (edgeIdx == face.BoundaryLoopEdgeIndex || edgeIdx == InvalidNetworkIndex)
            {
                break;
            }
        }
        if (loopVerts.size() < 3u)
        {
            continue;
        }

        for (uint32_t vi : loopVerts)
        {
            const size_t i3 = static_cast<size_t>(vi) * 3u;
            if (vertexFaceHits[vi] == 0u)
            {
                out.AlbedoRgb[i3 + 0u]   = face.Shading.AlbedoLinearRGB.X;
                out.AlbedoRgb[i3 + 1u]   = face.Shading.AlbedoLinearRGB.Y;
                out.AlbedoRgb[i3 + 2u]   = face.Shading.AlbedoLinearRGB.Z;
                out.MaterialPbr[i3 + 0u] = face.Shading.Roughness;
                out.MaterialPbr[i3 + 1u] = (face.PartGroupOrdinal == 1u) ? 1.0f : 0.0f;
                out.MaterialPbr[i3 + 2u] = 1.0f;
            }
            ++vertexFaceHits[vi];
        }

        const uint32_t v0 = loopVerts[0];
        for (size_t k = 1u; k + 1u < loopVerts.size(); ++k)
        {
            out.Indices.push_back(v0);
            out.Indices.push_back(loopVerts[k]);
            out.Indices.push_back(loopVerts[k + 1u]);
        }
    }

    return out;
}

PolygonNetwork PolygonNetworkDeformer::BuildPolygonNetworkFromSurface(
    const HighPolySurfaceRest& surface,
    std::string networkTitle)
{
    PolygonNetwork net {};
    net.NetworkTitle = std::move(networkTitle);

    const uint32_t vCount = surface.VertexCount();
    const uint32_t tCount = surface.TriangleCount();
    if (vCount == 0u || tCount == 0u)
    {
        return net;
    }

    net.ReserveStorage(vCount, tCount * 3u, tCount);
    for (uint32_t vi = 0u; vi < vCount; ++vi)
    {
        const size_t i3 = static_cast<size_t>(vi) * 3u;
        const uint32_t idx = net.AppendVertex({
            surface.RestPositions[i3 + 0u],
            surface.RestPositions[i3 + 1u],
            surface.RestPositions[i3 + 2u]
        });
        if (i3 + 2u < surface.RestNormals.size())
        {
            net.Vertices[idx].Normal = {
                surface.RestNormals[i3 + 0u],
                surface.RestNormals[i3 + 1u],
                surface.RestNormals[i3 + 2u]
            };
        }
    }

    const bool hasAlbedo = surface.AlbedoRgb.size() >= static_cast<size_t>(vCount) * 3u;
    const bool hasPbr    = surface.MaterialPbr.size() >= static_cast<size_t>(vCount) * 3u;

    for (uint32_t ti = 0u; ti < tCount; ++ti)
    {
        const uint32_t i0 = surface.Indices[static_cast<size_t>(ti) * 3u + 0u];
        const uint32_t i1 = surface.Indices[static_cast<size_t>(ti) * 3u + 1u];
        const uint32_t i2 = surface.Indices[static_cast<size_t>(ti) * 3u + 2u];
        if (i0 >= vCount || i1 >= vCount || i2 >= vCount || i0 == i1 || i1 == i2 || i2 == i0)
        {
            continue;
        }

        FaceShadingAssignment shading {};
        uint16_t partGroup = 0u;
        if (hasAlbedo)
        {
            const size_t a0 = static_cast<size_t>(i0) * 3u;
            const size_t a1 = static_cast<size_t>(i1) * 3u;
            const size_t a2 = static_cast<size_t>(i2) * 3u;
            shading.AlbedoLinearRGB = {
                (surface.AlbedoRgb[a0 + 0u] + surface.AlbedoRgb[a1 + 0u] + surface.AlbedoRgb[a2 + 0u]) / 3.0f,
                (surface.AlbedoRgb[a0 + 1u] + surface.AlbedoRgb[a1 + 1u] + surface.AlbedoRgb[a2 + 1u]) / 3.0f,
                (surface.AlbedoRgb[a0 + 2u] + surface.AlbedoRgb[a1 + 2u] + surface.AlbedoRgb[a2 + 2u]) / 3.0f
            };
        }
        if (hasPbr)
        {
            const size_t p0 = static_cast<size_t>(i0) * 3u;
            shading.Roughness = surface.MaterialPbr[p0 + 0u];
            shading.Metallic  = 0.04f;
            partGroup         = (surface.MaterialPbr[p0 + 1u] > 0.5f) ? 1u : 0u;
        }

        const uint32_t tri[3] = { i0, i1, i2 };
        (void)net.AppendFace(tri, {}, shading, 0u, partGroup);
    }

    net.LinkOppositeEdges(false);
    // Preserve exact sculpted normals from HighPolySurfaceRest when supplied
    if (surface.RestNormals.size() >= static_cast<size_t>(vCount) * 3u)
    {
        for (uint32_t vi = 0u; vi < vCount; ++vi)
        {
            const size_t i3 = static_cast<size_t>(vi) * 3u;
            net.Vertices[vi].Normal = {
                surface.RestNormals[i3 + 0u],
                surface.RestNormals[i3 + 1u],
                surface.RestNormals[i3 + 2u]
            };
        }
    }
    else
    {
        net.RecomputeNormals();
    }
    return net;
}

bool PolygonNetworkDeformer::ApplyDeformedSample(
    PolygonNetwork& targetNetwork,
    const DeformedSurfaceSample& deformedSample,
    bool recomputeFaceNormals) noexcept
{
    const size_t vCount = targetNetwork.Vertices.size();
    if (vCount == 0u || deformedSample.Positions.size() < vCount * 3u)
    {
        return false;
    }

    const bool hasNormals = deformedSample.Normals.size() >= vCount * 3u;
    for (size_t vi = 0u; vi < vCount; ++vi)
    {
        const size_t i3 = vi * 3u;
        targetNetwork.Vertices[vi].Axes = {
            deformedSample.Positions[i3 + 0u],
            deformedSample.Positions[i3 + 1u],
            deformedSample.Positions[i3 + 2u]
        };
        if (hasNormals)
        {
            targetNetwork.Vertices[vi].Normal = {
                deformedSample.Normals[i3 + 0u],
                deformedSample.Normals[i3 + 1u],
                deformedSample.Normals[i3 + 2u]
            };
        }
    }

    if (recomputeFaceNormals)
    {
        for (Face& face : targetNetwork.Faces)
        {
            if (face.BoundaryLoopEdgeIndex == InvalidNetworkIndex || face.CornerCount < 3u)
            {
                continue;
            }
            const uint32_t e0 = face.BoundaryLoopEdgeIndex;
            const uint32_t e1 = targetNetwork.Edges[e0].NextEdgeIndex;
            const uint32_t e2 = targetNetwork.Edges[e1].NextEdgeIndex;
            if (e1 == InvalidNetworkIndex || e2 == InvalidNetworkIndex) continue;

            const auto& p0 = targetNetwork.Vertices[targetNetwork.Edges[e0].OriginVertexIndex].Axes;
            const auto& p1 = targetNetwork.Vertices[targetNetwork.Edges[e1].OriginVertexIndex].Axes;
            const auto& p2 = targetNetwork.Vertices[targetNetwork.Edges[e2].OriginVertexIndex].Axes;
            const float ax = p1.X - p0.X, ay = p1.Y - p0.Y, az = p1.Z - p0.Z;
            const float bx = p2.X - p0.X, by = p2.Y - p0.Y, bz = p2.Z - p0.Z;
            const float nx = ay * bz - az * by;
            const float ny = az * bx - ax * bz;
            const float nz = ax * by - ay * bx;
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl > 1.0e-6f)
            {
                face.GeometricNormal = { nx / nl, ny / nl, nz / nl };
            }
        }
    }

    ++targetNetwork.NetworkRevision;
    return true;
}

} // namespace Frontier::GeometrySpace::Deformation
