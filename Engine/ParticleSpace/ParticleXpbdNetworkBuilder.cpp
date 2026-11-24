/*============================================================================================================================================
                                                   PARTICLEXPBDNETWORKBUILDER.CPP
============================================================================================================================================*/
// 🧩 Stage F (S26) Conflict-colored zero-atomic Orthotropic XPBD network builder, GPU table packer & CPU reference solver
// Reference: Experimental/ClothEditor/Engine/WebGpuClothAccelerator.js L9-L100, L201-L435, L609-L653 & PatternDraftNetwork.js L75-L120, L431-L530

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleXpbdNetworkBuilder.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace Frontier::ParticleSpace
{

namespace
{

namespace PS = Frontier::SimulationSpace::PhysicsSpace;
namespace FF = Frontier::SimulationSpace::ForceFields;

[[nodiscard]] float Length3(const std::array<float, 3>& v) noexcept
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/// Evaluates unilateral signed distance + outward normal against a single 80-byte RigidColliderProxy (Sphere, OBB, Capsule +Z).
[[nodiscard]] bool QueryRigidProxySdf(
    const PS::RigidColliderProxy& proxy,
    const std::array<float, 3>& worldPos,
    const float contactRadius,
    std::array<float, 3>& outNormal,
    float& outPenetration) noexcept
{
    const PS::RigidOrientationQuaternion quat {
        proxy.OrientationQuat[0], proxy.OrientationQuat[1], proxy.OrientationQuat[2], proxy.OrientationQuat[3]
    };
    const auto rl = PS::RigidBodyDynamics::RotateByConjugateQuaternion(quat, {
        worldPos[0] - proxy.CenterMeters[0],
        worldPos[1] - proxy.CenterMeters[1],
        worldPos[2] - proxy.CenterMeters[2]
    });
    const std::array<float, 3> rel { rl.X, rl.Y, rl.Z };
    std::array<float, 3> localN { 0.0f, 1.0f, 0.0f };
    float pen = 0.0f;
    bool hit = false;

    if (proxy.ShapeCode == 0u)
    {
        const float minD = std::max(proxy.RadiusMeters, 1.0e-4f) + contactRadius;
        const float d2 = rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2];
        if (d2 < minD * minD)
        {
            const float d = std::sqrt(std::max(d2, 1.0e-12f));
            localN = (d > 1.0e-5f)
                ? std::array<float, 3>{ rel[0] / d, rel[1] / d, rel[2] / d }
                : std::array<float, 3>{ 0.0f, 1.0f, 0.0f };
            pen = minD - d;
            hit = true;
        }
    }
    else if (proxy.ShapeCode == 1u)
    {
        const float hx = std::max(proxy.HalfExtentsMeters[0], 1.0e-4f);
        const float hy = std::max(proxy.HalfExtentsMeters[1], 1.0e-4f);
        const float hz = std::max(proxy.HalfExtentsMeters[2], 1.0e-4f);
        const float cx = std::clamp(rel[0], -hx, hx);
        const float cy = std::clamp(rel[1], -hy, hy);
        const float cz = std::clamp(rel[2], -hz, hz);
        const float dx = rel[0] - cx, dy = rel[1] - cy, dz = rel[2] - cz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 1.0e-10f)
        {
            if (d2 < contactRadius * contactRadius)
            {
                const float d = std::sqrt(d2);
                localN = { dx / d, dy / d, dz / d };
                pen = contactRadius - d;
                hit = true;
            }
        }
        else
        {
            const float ex = hx - std::fabs(rel[0]);
            const float ey = hy - std::fabs(rel[1]);
            const float ez = hz - std::fabs(rel[2]);
            if (ex <= ey && ex <= ez)      { localN = { rel[0] >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f }; pen = ex + contactRadius; }
            else if (ey <= ez)             { localN = { 0.0f, rel[1] >= 0.0f ? 1.0f : -1.0f, 0.0f }; pen = ey + contactRadius; }
            else                           { localN = { 0.0f, 0.0f, rel[2] >= 0.0f ? 1.0f : -1.0f }; pen = ez + contactRadius; }
            hit = true;
        }
    }
    else
    {
        const float halfH = std::max(proxy.HalfExtentsMeters[2], 0.0f);
        const float minD  = std::max(proxy.RadiusMeters, 1.0e-4f) + contactRadius;
        const float cz    = std::clamp(rel[2], -halfH, halfH);
        const float dx = rel[0], dy = rel[1], dz = rel[2] - cz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < minD * minD)
        {
            const float d = std::sqrt(std::max(d2, 1.0e-12f));
            localN = (d > 1.0e-5f)
                ? std::array<float, 3>{ dx / d, dy / d, dz / d }
                : std::array<float, 3>{ 0.0f, 0.0f, 1.0f };
            pen = minD - d;
            hit = true;
        }
    }

    if (!hit) return false;
    const auto wn = PS::RigidBodyDynamics::RotateByQuaternion(quat, { localN[0], localN[1], localN[2] });
    outNormal = { wn.X, wn.Y, wn.Z };
    outPenetration = pen;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CONFLICT COLORING (ZERO ATOMICS)
//------------------------------------------------------------------------------------------------------------------------

ConflictColorBatchTable ComputeConflictColorBatches(
    const uint32_t itemCount,
    const uint32_t itemArity,
    const std::span<const uint32_t> flatVertices,
    const uint32_t totalPointCount)
{
    ConflictColorBatchTable table {};
    if (itemCount == 0u || itemArity == 0u)
    {
        table.Offsets = { 0u };
        return table;
    }

    // Track used colors per vertex with a dynamic bitmask (64-bit fast path + overflow set)
    std::vector<uint64_t> vertexMask64(totalPointCount, 0ull);
    std::vector<std::vector<uint32_t>> vertexExtraColors(totalPointCount);
    std::vector<uint32_t> itemColors(itemCount, 0u);
    uint32_t maxColor = 0u;

    for (uint32_t item = 0u; item < itemCount; ++item)
    {
        const size_t base = static_cast<size_t>(item) * static_cast<size_t>(itemArity);
        uint64_t forbidden64 = 0ull;
        for (uint32_t a = 0u; a < itemArity; ++a)
        {
            const uint32_t v = flatVertices[base + a];
            if (v < totalPointCount) forbidden64 |= vertexMask64[v];
        }

        uint32_t chosen = 0u;
        if (forbidden64 != ~0ull)
        {
            while (((forbidden64 >> chosen) & 1ull) != 0ull) ++chosen;
        }
        else
        {
            chosen = 64u;
            while (true)
            {
                bool conflict = false;
                for (uint32_t a = 0u; a < itemArity; ++a)
                {
                    const uint32_t v = flatVertices[base + a];
                    if (v >= totalPointCount) continue;
                    const auto& ex = vertexExtraColors[v];
                    if (std::find(ex.begin(), ex.end(), chosen) != ex.end())
                    {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) break;
                ++chosen;
            }
        }

        itemColors[item] = chosen;
        maxColor = std::max(maxColor, chosen);
        for (uint32_t a = 0u; a < itemArity; ++a)
        {
            const uint32_t v = flatVertices[base + a];
            if (v >= totalPointCount) continue;
            if (chosen < 64u) vertexMask64[v] |= (1ull << chosen);
            else vertexExtraColors[v].push_back(chosen);
        }
    }

    table.ColorCount = maxColor + 1u;
    std::vector<uint32_t> counts(table.ColorCount, 0u);
    for (uint32_t c : itemColors) ++counts[c];

    table.Offsets.resize(table.ColorCount + 1u, 0u);
    for (uint32_t c = 0u; c < table.ColorCount; ++c)
    {
        table.Offsets[c + 1u] = table.Offsets[c] + counts[c];
    }

    std::vector<uint32_t> cursor = table.Offsets;
    table.OrderedItems.resize(itemCount, 0u);
    for (uint32_t item = 0u; item < itemCount; ++item)
    {
        const uint32_t c = itemColors[item];
        table.OrderedItems[cursor[c]++] = item;
    }
    return table;
}

bool VerifyConflictColorTable(
    const ConflictColorBatchTable& batches,
    const uint32_t itemArity,
    const std::span<const uint32_t> flatVertices) noexcept
{
    if (batches.Offsets.size() != static_cast<size_t>(batches.ColorCount) + 1u) return false;
    for (uint32_t c = 0u; c < batches.ColorCount; ++c)
    {
        std::unordered_set<uint32_t> seen;
        for (uint32_t k = batches.Offsets[c]; k < batches.Offsets[c + 1u]; ++k)
        {
            const uint32_t item = batches.OrderedItems[k];
            const size_t base = static_cast<size_t>(item) * static_cast<size_t>(itemArity);
            for (uint32_t a = 0u; a < itemArity; ++a)
            {
                const uint32_t v = flatVertices[base + a];
                if (!seen.insert(v).second) return false;
            }
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TOPOLOGY BUILDERS
//------------------------------------------------------------------------------------------------------------------------

XpbdConstraintNetwork BuildOrthotropicClothGridNetwork(
    const uint32_t cols,
    const uint32_t rows,
    const float widthMeters,
    const float heightMeters,
    const float arealDensityKgM2,
    const bool pinTopRow,
    const std::array<float, 3>& originMeters)
{
    XpbdConstraintNetwork net {};
    const uint32_t nx = std::max(2u, cols);
    const uint32_t ny = std::max(2u, rows);
    net.PointCount = nx * ny;
    net.RestPositions.resize(net.PointCount);
    net.MaterialUV.resize(net.PointCount);
    net.PointRestArea.assign(net.PointCount, 0.0f);

    for (uint32_t r = 0u; r < ny; ++r)
    {
        const float vf = static_cast<float>(r) / static_cast<float>(ny - 1u);
        for (uint32_t c = 0u; c < nx; ++c)
        {
            const float uf = static_cast<float>(c) / static_cast<float>(nx - 1u);
            const uint32_t idx = r * nx + c;
            const float uMeters = (uf - 0.5f) * widthMeters;
            const float vMeters = -vf * heightMeters;
            net.MaterialUV[idx]    = { uMeters, vMeters };
            net.RestPositions[idx] = { originMeters[0] + uMeters, originMeters[1] + vMeters, originMeters[2] };
        }
    }

    // Alternating diagonal triangulation (zero grid directional bias, matching PatternDraftNetwork.js)
    for (uint32_t r = 0u; r < ny - 1u; ++r)
    {
        for (uint32_t c = 0u; c < nx - 1u; ++c)
        {
            const uint32_t i00 = r * nx + c;
            const uint32_t i10 = r * nx + (c + 1u);
            const uint32_t i01 = (r + 1u) * nx + c;
            const uint32_t i11 = (r + 1u) * nx + (c + 1u);
            if (((r + c) & 1u) == 0u)
            {
                net.FaceIndices.insert(net.FaceIndices.end(), { i00, i10, i11, i00, i11, i01 });
            }
            else
            {
                net.FaceIndices.insert(net.FaceIndices.end(), { i00, i10, i01, i10, i11, i01 });
            }
        }
    }

    net.FaceCount = static_cast<uint32_t>(net.FaceIndices.size() / 3u);
    net.FaceRestArea.resize(net.FaceCount, 0.0f);
    net.FaceDmInv.resize(static_cast<size_t>(net.FaceCount) * 4u, 0.0f);

    // Exact PatternDraftNetwork.js L436-L457 Dm^-1 and triangle rest area
    for (uint32_t f = 0u; f < net.FaceCount; ++f)
    {
        const uint32_t a = net.FaceIndices[f * 3u + 0u];
        const uint32_t b = net.FaceIndices[f * 3u + 1u];
        const uint32_t c = net.FaceIndices[f * 3u + 2u];
        const float u0 = net.MaterialUV[a][0], v0 = net.MaterialUV[a][1];
        const float u1 = net.MaterialUV[b][0], v1 = net.MaterialUV[b][1];
        const float u2 = net.MaterialUV[c][0], v2 = net.MaterialUV[c][1];

        const float du1 = u1 - u0, dv1 = v1 - v0;
        const float du2 = u2 - u0, dv2 = v2 - v0;
        const float det = du1 * dv2 - du2 * dv1;
        const float area = std::max(1.0e-8f, 0.5f * std::fabs(det));
        const float invDet = (std::fabs(det) > 1.0e-12f) ? (1.0f / det) : 0.0f;

        net.FaceRestArea[f]          = area;
        net.FaceDmInv[f * 4u + 0u]   =  dv2 * invDet;
        net.FaceDmInv[f * 4u + 1u]   = -du2 * invDet;
        net.FaceDmInv[f * 4u + 2u]   = -dv1 * invDet;
        net.FaceDmInv[f * 4u + 3u]   =  du1 * invDet;

        const float third = area / 3.0f;
        net.PointRestArea[a] += third;
        net.PointRestArea[b] += third;
        net.PointRestArea[c] += third;
    }

    // Mass distribution from areal density; pin top row if requested
    net.InverseMass.resize(net.PointCount, 0.0f);
    for (uint32_t i = 0u; i < net.PointCount; ++i)
    {
        const bool pinned = pinTopRow && (i < nx);
        const float mass = std::max(1.0e-5f, net.PointRestArea[i] * arealDensityKgM2);
        net.InverseMass[i] = pinned ? 0.0f : (1.0f / mass);
    }

    // Exact PatternDraftNetwork.js L460-L526 interior hinge stencils
    struct EdgeAdjacency
    {
        uint32_t V0 = 0u, V1 = 0u, OppA = 0u, FaceA = 0u;
        int32_t  OppB = -1, FaceB = -1;
    };
    std::map<uint64_t, EdgeAdjacency> edgeMap;
    for (uint32_t f = 0u; f < net.FaceCount; ++f)
    {
        const uint32_t a = net.FaceIndices[f * 3u + 0u];
        const uint32_t b = net.FaceIndices[f * 3u + 1u];
        const uint32_t c = net.FaceIndices[f * 3u + 2u];
        const std::array<std::array<uint32_t, 3>, 3> tris {{ { a, b, c }, { b, c, a }, { c, a, b } }};
        for (const auto& t : tris)
        {
            const uint32_t lo = std::min(t[0], t[1]);
            const uint32_t hi = std::max(t[0], t[1]);
            const uint64_t key = (static_cast<uint64_t>(lo) << 32u) | static_cast<uint64_t>(hi);
            auto it = edgeMap.find(key);
            if (it == edgeMap.end())
            {
                edgeMap.emplace(key, EdgeAdjacency{ lo, hi, t[2], f, -1, -1 });
            }
            else
            {
                it->second.OppB  = static_cast<int32_t>(t[2]);
                it->second.FaceB = static_cast<int32_t>(f);
            }
        }
    }

    for (const auto& [key, entry] : edgeMap)
    {
        if (entry.OppB < 0) continue;
        const uint32_t v0 = entry.V0, v1 = entry.V1, oppA = entry.OppA;
        const uint32_t oppB = static_cast<uint32_t>(entry.OppB);
        const float u0x = net.MaterialUV[v0][0], u0y = net.MaterialUV[v0][1];
        const float u1x = net.MaterialUV[v1][0], u1y = net.MaterialUV[v1][1];
        const float ex = u1x - u0x, ey = u1y - u0y;
        const float L2 = ex * ex + ey * ey;
        const float L  = std::sqrt(L2);
        if (L < 1.0e-5f) continue;

        const float u2x = net.MaterialUV[oppA][0], u2y = net.MaterialUV[oppA][1];
        const float u3x = net.MaterialUV[oppB][0], u3y = net.MaterialUV[oppB][1];
        const float h2  = std::max(1.0e-4f, (2.0f * net.FaceRestArea[entry.FaceA]) / L);
        const float h3  = std::max(1.0e-4f, (2.0f * net.FaceRestArea[static_cast<size_t>(entry.FaceB)]) / L);
        const float s2  = ((u2x - u0x) * ex + (u2y - u0y) * ey) / L2;
        const float s3  = ((u3x - u0x) * ex + (u3y - u0y) * ey) / L2;

        const float denom = h2 + h3;
        const float c2 = 2.0f / (h2 * denom);
        const float c3 = 2.0f / (h3 * denom);
        const float c0 = -c2 * (1.0f - s2) - c3 * (1.0f - s3);
        const float c1 = -c2 * s2 - c3 * s3;

        net.HingeIndices.insert(net.HingeIndices.end(), { v0, v1, oppA, oppB });
        net.HingeWeights.insert(net.HingeWeights.end(), { c0, c1, c2, c3 });
        net.HingeArea.push_back((net.FaceRestArea[entry.FaceA] + net.FaceRestArea[static_cast<size_t>(entry.FaceB)]) / 3.0f);
    }
    net.HingeCount = static_cast<uint32_t>(net.HingeIndices.size() / 4u);

    net.FaceBatches     = ComputeConflictColorBatches(net.FaceCount, 3u, net.FaceIndices, net.PointCount);
    net.HingeBatches    = ComputeConflictColorBatches(net.HingeCount, 4u, net.HingeIndices, net.PointCount);
    net.DistanceBatches = { 0u, { 0u }, {} };
    net.SeamBatches     = { 0u, { 0u }, {} };
    return net;
}

XpbdConstraintNetwork BuildStrandWebNetwork(
    const uint32_t radialSpokes,
    const uint32_t concentricRings,
    const float radiusMeters,
    const float edgeStiffness,
    const float breakRatio,
    const bool pinOuterRim,
    const std::array<float, 3>& centerMeters)
{
    XpbdConstraintNetwork net {};
    const uint32_t spokes = std::max(4u, radialSpokes);
    const uint32_t rings  = std::max(2u, concentricRings);
    // Point 0 = center origin node, then rings * spokes nodes
    net.PointCount = 1u + rings * spokes;
    net.RestPositions.resize(net.PointCount);
    net.MaterialUV.resize(net.PointCount);
    net.InverseMass.assign(net.PointCount, 1.0f / 0.015f);
    net.PointRestArea.assign(net.PointCount, 0.01f);

    net.RestPositions[0] = centerMeters;
    net.MaterialUV[0]    = { 0.0f, 0.0f };

    constexpr float kTwoPi = 6.28318530717958647692f;
    for (uint32_t r = 1u; r <= rings; ++r)
    {
        const float rad = radiusMeters * (static_cast<float>(r) / static_cast<float>(rings));
        for (uint32_t s = 0u; s < spokes; ++s)
        {
            const float ang = kTwoPi * (static_cast<float>(s) / static_cast<float>(spokes));
            const uint32_t idx = 1u + (r - 1u) * spokes + s;
            const float ox = std::cos(ang) * rad;
            const float oy = std::sin(ang) * rad;
            net.MaterialUV[idx]    = { ox, oy };
            net.RestPositions[idx] = { centerMeters[0] + ox, centerMeters[1] + oy, centerMeters[2] };
            if (pinOuterRim && r == rings)
            {
                net.InverseMass[idx] = 0.0f;
            }
        }
    }

    auto addEdge = [&](const uint32_t a, const uint32_t b, const float stiff) {
        const auto& pa = net.RestPositions[a];
        const auto& pb = net.RestPositions[b];
        const float len = Length3({ pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2] });
        net.DistanceIndices.push_back(a);
        net.DistanceIndices.push_back(b);
        net.DistanceRestLength.push_back(len);
        net.DistanceStiffness.push_back(stiff);
        net.DistanceBreakRatio.push_back(breakRatio);
    };

    // Radial spokes + concentric rings
    for (uint32_t s = 0u; s < spokes; ++s)
    {
        addEdge(0u, 1u + s, edgeStiffness);
        for (uint32_t r = 1u; r < rings; ++r)
        {
            const uint32_t a = 1u + (r - 1u) * spokes + s;
            const uint32_t b = 1u + r * spokes + s;
            addEdge(a, b, edgeStiffness);
        }
    }
    for (uint32_t r = 1u; r <= rings; ++r)
    {
        for (uint32_t s = 0u; s < spokes; ++s)
        {
            const uint32_t a = 1u + (r - 1u) * spokes + s;
            const uint32_t b = 1u + (r - 1u) * spokes + ((s + 1u) % spokes);
            addEdge(a, b, edgeStiffness * 0.85f);
        }
    }

    net.DistanceCount   = static_cast<uint32_t>(net.DistanceIndices.size() / 2u);
    net.DistanceBatches = ComputeConflictColorBatches(net.DistanceCount, 2u, net.DistanceIndices, net.PointCount);
    net.FaceBatches     = { 0u, { 0u }, {} };
    net.HingeBatches    = { 0u, { 0u }, {} };
    net.SeamBatches     = { 0u, { 0u }, {} };
    return net;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             GPU TABLE PACKER
//------------------------------------------------------------------------------------------------------------------------

XpbdPackedGpuTables PackXpbdGpuTables(const XpbdConstraintNetwork& net)
{
    XpbdPackedGpuTables out {};
    uint32_t uCur = XpbdPackedGpuTables::kHeaderWords;
    const uint32_t offFBat = uCur; uCur += net.FaceCount;
    const uint32_t offHBat = uCur; uCur += net.HingeCount;
    const uint32_t offSBat = uCur; uCur += net.SeamCount;
    const uint32_t offDBat = uCur; uCur += net.DistanceCount;
    const uint32_t offFIdx = uCur; uCur += net.FaceCount * 3u;
    const uint32_t offHIdx = uCur; uCur += net.HingeCount * 4u;
    const uint32_t offSIdx = uCur; uCur += net.SeamCount * 2u;
    const uint32_t offDIdx = uCur; uCur += net.DistanceCount * 2u;

    uint32_t fCur = 0u;
    const uint32_t offDmIn  = fCur; fCur += net.FaceCount * 4u;
    const uint32_t offFAre  = fCur; fCur += net.FaceCount;
    const uint32_t offHW    = fCur; fCur += net.HingeCount * 4u;
    const uint32_t offHAre  = fCur; fCur += net.HingeCount;
    const uint32_t offSGap0 = fCur; fCur += net.SeamCount;
    const uint32_t offDLen  = fCur; fCur += net.DistanceCount;
    const uint32_t offDStf  = fCur; fCur += net.DistanceCount;
    const uint32_t offDBrk  = fCur; fCur += net.DistanceCount;

    out.U32Topology.assign(std::max(32u, uCur), 0u);
    out.U32Topology[0]  = 0u;       // reserved pointBatch offset
    out.U32Topology[1]  = offFBat;
    out.U32Topology[2]  = offHBat;
    out.U32Topology[3]  = offSBat;
    out.U32Topology[4]  = offFIdx;
    out.U32Topology[5]  = offHIdx;
    out.U32Topology[6]  = offSIdx;
    out.U32Topology[7]  = offDBat;
    out.U32Topology[8]  = offDIdx;
    out.U32Topology[13] = offDmIn;
    out.U32Topology[14] = offFAre;
    out.U32Topology[15] = offHW;
    out.U32Topology[16] = offHAre;
    out.U32Topology[17] = offSGap0;
    out.U32Topology[18] = net.FaceCount;
    out.U32Topology[19] = net.HingeCount;
    out.U32Topology[20] = net.SeamCount;
    out.U32Topology[21] = net.DistanceCount;
    out.U32Topology[22] = offDLen;
    out.U32Topology[23] = offDStf;

    auto copyU32 = [&](const std::vector<uint32_t>& src, const uint32_t dstOff) {
        if (!src.empty()) std::copy(src.begin(), src.end(), out.U32Topology.begin() + dstOff);
    };
    copyU32(net.FaceBatches.OrderedItems,     offFBat);
    copyU32(net.HingeBatches.OrderedItems,    offHBat);
    copyU32(net.SeamBatches.OrderedItems,     offSBat);
    copyU32(net.DistanceBatches.OrderedItems, offDBat);
    copyU32(net.FaceIndices,                  offFIdx);
    copyU32(net.HingeIndices,                 offHIdx);
    copyU32(net.SeamIndices,                  offSIdx);
    copyU32(net.DistanceIndices,              offDIdx);

    out.F32Rest.assign(std::max(16u, fCur), 0.0f);
    auto copyF32 = [&](const std::vector<float>& src, const uint32_t dstOff) {
        if (!src.empty()) std::copy(src.begin(), src.end(), out.F32Rest.begin() + dstOff);
    };
    copyF32(net.FaceDmInv,          offDmIn);
    copyF32(net.FaceRestArea,       offFAre);
    copyF32(net.HingeWeights,       offHW);
    copyF32(net.HingeArea,          offHAre);
    copyF32(net.SeamInitialGap,     offSGap0);
    copyF32(net.DistanceRestLength, offDLen);
    copyF32(net.DistanceStiffness,  offDStf);
    copyF32(net.DistanceBreakRatio, offDBrk);

    out.HingeDualOffset    = 3u * net.FaceCount;
    out.SeamDualOffset     = out.HingeDualOffset + net.HingeCount;
    out.DistanceDualOffset = out.SeamDualOffset + net.SeamCount;
    out.TotalDuals         = std::max(1u, out.DistanceDualOffset + net.DistanceCount);
    return out;
}

void InitializeXpbdSimulationState(const XpbdConstraintNetwork& network, XpbdSimulationState& state)
{
    state.Positions     = network.RestPositions;
    state.PrevPositions = network.RestPositions;
    state.Velocities.assign(network.PointCount, { 0.0f, 0.0f, 0.0f });
    const uint32_t totalDuals = 3u * network.FaceCount + network.HingeCount + network.SeamCount + network.DistanceCount;
    state.DualLambdas.assign(std::max(1u, totalDuals), 0.0f);
    state.BrokenEdges.assign(network.DistanceCount, 0u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CONFLICT-COLORED ORTHOTROPIC XPBD CPU SOLVER
//------------------------------------------------------------------------------------------------------------------------

void StepParticleXpbdCpu(
    const XpbdConstraintNetwork& net,
    const XpbdSimulationSettings& cfg,
    XpbdSimulationState& state,
    const float frameDeltaTimeSeconds,
    const float simulationTimeSeconds,
    const std::span<const FF::ForceModelGpuRecord> forceRecords,
    const std::span<const PS::RigidColliderProxy> rigidProxies)
{
    const uint32_t substeps   = std::max(1u, cfg.Substeps);
    const uint32_t iterations = std::max(1u, cfg.Iterations);
    const float dt     = frameDeltaTimeSeconds / static_cast<float>(substeps);
    const float invDt  = 1.0f / std::max(dt, 1.0e-6f);
    const float invDt2 = invDt * invDt;

    const uint32_t hingeDualOff = 3u * net.FaceCount;
    const uint32_t seamDualOff  = hingeDualOff + net.HingeCount;
    const uint32_t distDualOff  = seamDualOff + net.SeamCount;

    for (uint32_t sub = 0u; sub < substeps; ++sub)
    {
        const float subTime = simulationTimeSeconds + static_cast<float>(sub) * dt;

        // 1. Predict Inertia (matches WebGpuClothAccelerator.js::predictInertia L201-L234)
        for (uint32_t i = 0u; i < net.PointCount; ++i)
        {
            const auto p = state.Positions[i];
            state.PrevPositions[i] = p;
            const float w = net.InverseMass[i];
            if (w <= 0.0f)
            {
                state.Velocities[i] = { 0.0f, 0.0f, 0.0f };
                continue;
            }

            std::array<float, 3> v {
                state.Velocities[i][0] * cfg.Damping,
                state.Velocities[i][1] * cfg.Damping + cfg.GravityY * dt,
                state.Velocities[i][2] * cfg.Damping
            };

            if (cfg.WindEnabled)
            {
                const float aero = cfg.AeroDragRate * dt;
                v[0] += (cfg.WindVelocity[0] - v[0]) * aero;
                v[1] += (cfg.WindVelocity[1] - v[1]) * aero;
                v[2] += (cfg.WindVelocity[2] - v[2]) * aero;
            }

            if (!forceRecords.empty())
            {
                const auto fAccel = FF::EvaluateTotalForceField(
                    forceRecords, p, v, subTime,
                    static_cast<uint32_t>(FF::ForceTargetChannelBits::Particle));
                v[0] += fAccel[0] * dt;
                v[1] += fAccel[1] * dt;
                v[2] += fAccel[2] * dt;
            }

            state.Velocities[i] = v;
            state.Positions[i]  = { p[0] + v[0] * dt, p[1] + v[1] * dt, p[2] + v[2] * dt };
        }

        // 2. Reset Persistent XPBD Dual Multipliers (matches resetXpbdDuals)
        std::fill(state.DualLambdas.begin(), state.DualLambdas.end(), 0.0f);

        // 3. Gauss-Seidel iterations over conflict-colored zero-atomic batches
        for (uint32_t it = 0u; it < iterations; ++it)
        {
            // A. Seam stitch constraints
            const float seamATilde = invDt2 / 1400.0f;
            for (uint32_t c = 0u; c < net.SeamBatches.ColorCount; ++c)
            {
                for (uint32_t k = net.SeamBatches.Offsets[c]; k < net.SeamBatches.Offsets[c + 1u]; ++k)
                {
                    const uint32_t s = net.SeamBatches.OrderedItems[k];
                    const uint32_t a = net.SeamIndices[s * 2u + 0u];
                    const uint32_t b = net.SeamIndices[s * 2u + 1u];
                    const float wa = net.InverseMass[a], wb = net.InverseMass[b];
                    const float wSum = wa + wb;
                    if (wSum <= 0.0f) continue;

                    const float dx = state.Positions[a][0] - state.Positions[b][0];
                    const float dy = state.Positions[a][1] - state.Positions[b][1];
                    const float dz = state.Positions[a][2] - state.Positions[b][2];
                    const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < 1.0e-8f) continue;

                    const float target = (1.0f - cfg.ZipAlpha) * std::min(net.SeamInitialGap[s], d);
                    const float C      = d - target;
                    const uint32_t dIdx = seamDualOff + s;
                    const float dLam   = (-C - seamATilde * state.DualLambdas[dIdx]) / (wSum + seamATilde);
                    state.DualLambdas[dIdx] += dLam;
                    const float scale = dLam / d;
                    if (wa > 0.0f)
                    {
                        state.Positions[a][0] += wa * dx * scale;
                        state.Positions[a][1] += wa * dy * scale;
                        state.Positions[a][2] += wa * dz * scale;
                    }
                    if (wb > 0.0f)
                    {
                        state.Positions[b][0] -= wb * dx * scale;
                        state.Positions[b][1] -= wb * dy * scale;
                        state.Positions[b][2] -= wb * dz * scale;
                    }
                }
            }

            // B. Distance / Strand / Web constraints (with optional tearing)
            for (uint32_t c = 0u; c < net.DistanceBatches.ColorCount; ++c)
            {
                for (uint32_t k = net.DistanceBatches.Offsets[c]; k < net.DistanceBatches.Offsets[c + 1u]; ++k)
                {
                    const uint32_t e = net.DistanceBatches.OrderedItems[k];
                    if (state.BrokenEdges[e] != 0u) continue;
                    const uint32_t a = net.DistanceIndices[e * 2u + 0u];
                    const uint32_t b = net.DistanceIndices[e * 2u + 1u];
                    const float wa = net.InverseMass[a], wb = net.InverseMass[b];
                    const float wSum = wa + wb;
                    if (wSum <= 0.0f) continue;

                    const float dx = state.Positions[a][0] - state.Positions[b][0];
                    const float dy = state.Positions[a][1] - state.Positions[b][1];
                    const float dz = state.Positions[a][2] - state.Positions[b][2];
                    const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < 1.0e-8f) continue;

                    const float restLen = net.DistanceRestLength[e];
                    const float brk     = net.DistanceBreakRatio[e];
                    if (brk > 1.0f && d > restLen * brk)
                    {
                        state.BrokenEdges[e] = 1u;
                        continue;
                    }

                    const float C      = d - restLen;
                    const float aTilde = invDt2 / std::max(1.0e-4f, net.DistanceStiffness[e]);
                    const uint32_t dIdx = distDualOff + e;
                    const float dLam   = (-C - aTilde * state.DualLambdas[dIdx]) / (wSum + aTilde);
                    state.DualLambdas[dIdx] += dLam;
                    const float scale = dLam / d;
                    if (wa > 0.0f)
                    {
                        state.Positions[a][0] += wa * dx * scale;
                        state.Positions[a][1] += wa * dy * scale;
                        state.Positions[a][2] += wa * dz * scale;
                    }
                    if (wb > 0.0f)
                    {
                        state.Positions[b][0] -= wb * dx * scale;
                        state.Positions[b][1] -= wb * dy * scale;
                        state.Positions[b][2] -= wb * dz * scale;
                    }
                }
            }

            // C. Orthotropic Triangle Membrane (Warp + Weft + Shear — exact WebGpuClothAccelerator.js L284-L387)
            for (uint32_t c = 0u; c < net.FaceBatches.ColorCount; ++c)
            {
                for (uint32_t k = net.FaceBatches.Offsets[c]; k < net.FaceBatches.Offsets[c + 1u]; ++k)
                {
                    const uint32_t f  = net.FaceBatches.OrderedItems[k];
                    const uint32_t ia = net.FaceIndices[f * 3u + 0u];
                    const uint32_t ib = net.FaceIndices[f * 3u + 1u];
                    const uint32_t ic = net.FaceIndices[f * 3u + 2u];
                    const float wa = net.InverseMass[ia], wb = net.InverseMass[ib], wc = net.InverseMass[ic];
                    if (wa + wb + wc <= 0.0f) continue;

                    const float q00 = net.FaceDmInv[f * 4u + 0u], q01 = net.FaceDmInv[f * 4u + 1u];
                    const float q10 = net.FaceDmInv[f * 4u + 2u], q11 = net.FaceDmInv[f * 4u + 3u];
                    const float area = net.FaceRestArea[f];
                    const float cuA = -(q00 + q10), cuB = q00, cuC = q10;
                    const float cvA = -(q01 + q11), cvB = q01, cvC = q11;
                    const uint32_t d3 = f * 3u;

                    auto pA = state.Positions[ia];
                    auto pB = state.Positions[ib];
                    auto pC = state.Positions[ic];

                    // 1. Warp
                    {
                        const std::array<float, 3> fu {
                            (pB[0] - pA[0]) * q00 + (pC[0] - pA[0]) * q10,
                            (pB[1] - pA[1]) * q00 + (pC[1] - pA[1]) * q10,
                            (pB[2] - pA[2]) * q00 + (pC[2] - pA[2]) * q10
                        };
                        const float lu = Length3(fu);
                        if (lu > 1.0e-6f)
                        {
                            const float wG = wa * cuA * cuA + wb * cuB * cuB + wc * cuC * cuC;
                            if (wG > 1.0e-12f)
                            {
                                const float epsU = lu - 1.0f;
                                float d2Psi = cfg.WarpModulus * area;
                                if (epsU < 0.0f) d2Psi *= cfg.CompRatio;
                                else if (epsU > cfg.WarpCrimp)
                                {
                                    const float ov = epsU - cfg.WarpCrimp;
                                    d2Psi += 3.0f * cfg.WallModulus * area * ov * ov;
                                }
                                const float aTilde = invDt2 / std::max(1.0e-4f, d2Psi);
                                const float dLam = (-epsU - aTilde * state.DualLambdas[d3]) / (wG + aTilde);
                                state.DualLambdas[d3] += dLam;
                                const float invLu = 1.0f / lu;
                                const float nx = fu[0] * invLu, ny = fu[1] * invLu, nz = fu[2] * invLu;
                                pA[0] += wa * cuA * dLam * nx; pA[1] += wa * cuA * dLam * ny; pA[2] += wa * cuA * dLam * nz;
                                pB[0] += wb * cuB * dLam * nx; pB[1] += wb * cuB * dLam * ny; pB[2] += wb * cuB * dLam * nz;
                                pC[0] += wc * cuC * dLam * nx; pC[1] += wc * cuC * dLam * ny; pC[2] += wc * cuC * dLam * nz;
                            }
                        }
                    }

                    // 2. Weft
                    {
                        const std::array<float, 3> fv {
                            (pB[0] - pA[0]) * q01 + (pC[0] - pA[0]) * q11,
                            (pB[1] - pA[1]) * q01 + (pC[1] - pA[1]) * q11,
                            (pB[2] - pA[2]) * q01 + (pC[2] - pA[2]) * q11
                        };
                        const float lv = Length3(fv);
                        if (lv > 1.0e-6f)
                        {
                            const float wG = wa * cvA * cvA + wb * cvB * cvB + wc * cvC * cvC;
                            if (wG > 1.0e-12f)
                            {
                                const float epsV = lv - 1.0f;
                                float d2Psi = cfg.WeftModulus * area;
                                if (epsV < 0.0f) d2Psi *= cfg.CompRatio;
                                else if (epsV > cfg.WeftCrimp)
                                {
                                    const float ov = epsV - cfg.WeftCrimp;
                                    d2Psi += 3.0f * cfg.WallModulus * area * ov * ov;
                                }
                                const float aTilde = invDt2 / std::max(1.0e-4f, d2Psi);
                                const float dLam = (-epsV - aTilde * state.DualLambdas[d3 + 1u]) / (wG + aTilde);
                                state.DualLambdas[d3 + 1u] += dLam;
                                const float invLv = 1.0f / lv;
                                const float nx = fv[0] * invLv, ny = fv[1] * invLv, nz = fv[2] * invLv;
                                pA[0] += wa * cvA * dLam * nx; pA[1] += wa * cvA * dLam * ny; pA[2] += wa * cvA * dLam * nz;
                                pB[0] += wb * cvB * dLam * nx; pB[1] += wb * cvB * dLam * ny; pB[2] += wb * cvB * dLam * nz;
                                pC[0] += wc * cvC * dLam * nx; pC[1] += wc * cvC * dLam * ny; pC[2] += wc * cvC * dLam * nz;
                            }
                        }
                    }

                    // 3. Shear (weave locking angle)
                    {
                        const std::array<float, 3> fu2 {
                            (pB[0] - pA[0]) * q00 + (pC[0] - pA[0]) * q10,
                            (pB[1] - pA[1]) * q00 + (pC[1] - pA[1]) * q10,
                            (pB[2] - pA[2]) * q00 + (pC[2] - pA[2]) * q10
                        };
                        const std::array<float, 3> fv2 {
                            (pB[0] - pA[0]) * q01 + (pC[0] - pA[0]) * q11,
                            (pB[1] - pA[1]) * q01 + (pC[1] - pA[1]) * q11,
                            (pB[2] - pA[2]) * q01 + (pC[2] - pA[2]) * q11
                        };
                        const float lu2 = Length3(fu2), lv2 = Length3(fv2);
                        if (lu2 > 1.0e-6f && lv2 > 1.0e-6f)
                        {
                            const float invN = 1.0f / (lu2 * lv2);
                            const std::array<float, 3> ga {
                                (cuA * fv2[0] + cvA * fu2[0]) * invN,
                                (cuA * fv2[1] + cvA * fu2[1]) * invN,
                                (cuA * fv2[2] + cvA * fu2[2]) * invN
                            };
                            const std::array<float, 3> gb {
                                (cuB * fv2[0] + cvB * fu2[0]) * invN,
                                (cuB * fv2[1] + cvB * fu2[1]) * invN,
                                (cuB * fv2[2] + cvB * fu2[2]) * invN
                            };
                            const std::array<float, 3> gc {
                                (cuC * fv2[0] + cvC * fu2[0]) * invN,
                                (cuC * fv2[1] + cvC * fu2[1]) * invN,
                                (cuC * fv2[2] + cvC * fu2[2]) * invN
                            };
                            const float wG = wa * (ga[0] * ga[0] + ga[1] * ga[1] + ga[2] * ga[2])
                                           + wb * (gb[0] * gb[0] + gb[1] * gb[1] + gb[2] * gb[2])
                                           + wc * (gc[0] * gc[0] + gc[1] * gc[1] + gc[2] * gc[2]);
                            if (wG > 1.0e-12f)
                            {
                                const float cs = (fu2[0] * fv2[0] + fu2[1] * fv2[1] + fu2[2] * fv2[2]) * invN;
                                float kS = cfg.ShearModulus * area;
                                if (std::fabs(cs) > cfg.MaxShearS)
                                {
                                    const float ov = std::fabs(cs) - cfg.MaxShearS;
                                    kS += cfg.WallModulus * area * 0.35f * (1.0f + 18.0f * ov * ov);
                                }
                                const float aTilde = invDt2 / std::max(1.0e-4f, kS);
                                const float dLam = (-cs - aTilde * state.DualLambdas[d3 + 2u]) / (wG + aTilde);
                                state.DualLambdas[d3 + 2u] += dLam;
                                pA[0] += wa * dLam * ga[0]; pA[1] += wa * dLam * ga[1]; pA[2] += wa * dLam * ga[2];
                                pB[0] += wb * dLam * gb[0]; pB[1] += wb * dLam * gb[1]; pB[2] += wb * dLam * gb[2];
                                pC[0] += wc * dLam * gc[0]; pC[1] += wc * dLam * gc[1]; pC[2] += wc * dLam * gc[2];
                            }
                        }
                    }

                    state.Positions[ia] = pA;
                    state.Positions[ib] = pB;
                    state.Positions[ic] = pC;
                }
            }

            // D. Quadratic Bending Hinge Batch (exact WebGpuClothAccelerator.js L390-L433)
            for (uint32_t c = 0u; c < net.HingeBatches.ColorCount; ++c)
            {
                for (uint32_t k = net.HingeBatches.Offsets[c]; k < net.HingeBatches.Offsets[c + 1u]; ++k)
                {
                    const uint32_t hi = net.HingeBatches.OrderedItems[k];
                    const uint32_t hb = hi * 4u;
                    const uint32_t i0 = net.HingeIndices[hb + 0u], i1 = net.HingeIndices[hb + 1u];
                    const uint32_t i2 = net.HingeIndices[hb + 2u], i3 = net.HingeIndices[hb + 3u];
                    const float w0 = net.InverseMass[i0], w1 = net.InverseMass[i1];
                    const float w2 = net.InverseMass[i2], w3 = net.InverseMass[i3];
                    const float c0 = net.HingeWeights[hb + 0u], c1 = net.HingeWeights[hb + 1u];
                    const float c2 = net.HingeWeights[hb + 2u], c3 = net.HingeWeights[hb + 3u];
                    const float denomBase = w0 * c0 * c0 + w1 * c1 * c1 + w2 * c2 * c2 + w3 * c3 * c3;
                    if (denomBase <= 1.0e-12f) continue;

                    const auto& x0 = state.Positions[i0];
                    const auto& x1 = state.Positions[i1];
                    const auto& x2 = state.Positions[i2];
                    const auto& x3 = state.Positions[i3];
                    const std::array<float, 3> bVec {
                        c0 * x0[0] + c1 * x1[0] + c2 * x2[0] + c3 * x3[0],
                        c0 * x0[1] + c1 * x1[1] + c2 * x2[1] + c3 * x3[1],
                        c0 * x0[2] + c1 * x1[2] + c2 * x2[2] + c3 * x3[2]
                    };
                    const float bLen = Length3(bVec);
                    if (bLen < 1.0e-9f) continue;

                    const float aTilde = invDt2 / std::max(1.0e-5f, cfg.BendStiffness * net.HingeArea[hi]);
                    const uint32_t dIdx = hingeDualOff + hi;
                    const float dLam = (-bLen - aTilde * state.DualLambdas[dIdx]) / (denomBase + aTilde);
                    state.DualLambdas[dIdx] += dLam;
                    const float s = dLam / bLen;
                    const float nx = bVec[0] * s, ny = bVec[1] * s, nz = bVec[2] * s;

                    if (w0 > 0.0f) { state.Positions[i0][0] += w0 * c0 * nx; state.Positions[i0][1] += w0 * c0 * ny; state.Positions[i0][2] += w0 * c0 * nz; }
                    if (w1 > 0.0f) { state.Positions[i1][0] += w1 * c1 * nx; state.Positions[i1][1] += w1 * c1 * ny; state.Positions[i1][2] += w1 * c1 * nz; }
                    if (w2 > 0.0f) { state.Positions[i2][0] += w2 * c2 * nx; state.Positions[i2][1] += w2 * c2 * ny; state.Positions[i2][2] += w2 * c2 * nz; }
                    if (w3 > 0.0f) { state.Positions[i3][0] += w3 * c3 * nx; state.Positions[i3][1] += w3 * c3 * ny; state.Positions[i3][2] += w3 * c3 * nz; }
                }
            }

            // E. Unilateral Contact Projection against Jolt RigidColliderProxy inside substep loop
            if (!rigidProxies.empty())
            {
                for (uint32_t i = 0u; i < net.PointCount; ++i)
                {
                    if (net.InverseMass[i] <= 0.0f) continue;
                    for (const auto& proxy : rigidProxies)
                    {
                        std::array<float, 3> n {};
                        float pen = 0.0f;
                        if (QueryRigidProxySdf(proxy, state.Positions[i], cfg.ContactRadius, n, pen))
                        {
                            state.Positions[i][0] += n[0] * pen;
                            state.Positions[i][1] += n[1] * pen;
                            state.Positions[i][2] += n[2] * pen;
                        }
                    }
                }
            }
        }

        // 4. Finalize Substep (exact WebGpuClothAccelerator.js::finalizeSubstep L609-L653 + RigidColliderProxy friction)
        for (uint32_t i = 0u; i < net.PointCount; ++i)
        {
            const auto pPre = state.Positions[i];
            auto p = pPre;
            if (p[1] < cfg.GroundY) p[1] = cfg.GroundY;

            if (net.InverseMass[i] <= 0.0f)
            {
                state.Positions[i]  = p;
                state.Velocities[i] = { 0.0f, 0.0f, 0.0f };
                continue;
            }

            const auto& xPrev = state.PrevPositions[i];
            for (const auto& proxy : rigidProxies)
            {
                std::array<float, 3> n {};
                float pen = 0.0f;
                if (QueryRigidProxySdf(proxy, p, cfg.ContactRadius, n, pen))
                {
                    p[0] += n[0] * pen; p[1] += n[1] * pen; p[2] += n[2] * pen;
                    const float dx = p[0] - xPrev[0], dy = p[1] - xPrev[1], dz = p[2] - xPrev[2];
                    const float dn = dx * n[0] + dy * n[1] + dz * n[2];
                    const float tx = dx - dn * n[0], ty = dy - dn * n[1], tz = dz - dn * n[2];
                    const float tLen = std::sqrt(tx * tx + ty * ty + tz * tz);
                    if (tLen > 1.0e-7f)
                    {
                        const float slip = std::min(1.0f, (cfg.FrictionMu * pen) / tLen);
                        p[0] -= tx * slip; p[1] -= ty * slip; p[2] -= tz * slip;
                    }
                }
            }
            state.Positions[i] = p;

            std::array<float, 3> v {
                (p[0] - xPrev[0]) * invDt,
                (p[1] - xPrev[1]) * invDt,
                (p[2] - xPrev[2]) * invDt
            };
            const std::array<float, 3> colDelta { p[0] - pPre[0], p[1] - pPre[1], p[2] - pPre[2] };
            const float cLen = Length3(colDelta);
            if (cLen > 1.0e-6f)
            {
                const float invC = 1.0f / cLen;
                const float cnx = colDelta[0] * invC, cny = colDelta[1] * invC, cnz = colDelta[2] * invC;
                const float vn = v[0] * cnx + v[1] * cny + v[2] * cnz;
                const float sv = std::min(vn, cLen * invDt);
                if (sv > 0.0f)
                {
                    v[0] -= cnx * sv; v[1] -= cny * sv; v[2] -= cnz * sv;
                }
            }
            const float spd = Length3(v);
            if (spd > cfg.MaxSpeed)
            {
                const float s = cfg.MaxSpeed / spd;
                v[0] *= s; v[1] *= s; v[2] *= s;
            }
            state.Velocities[i] = v;
        }
    }
}

} // namespace Frontier::ParticleSpace
