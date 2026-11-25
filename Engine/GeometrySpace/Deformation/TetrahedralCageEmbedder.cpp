/*============================================================================================================================================
                                                     TETRAHEDRALCAGEEMBEDDER.CPP
============================================================================================================================================*/
// 🧩 Phase D04 Exact 3D tetrahedral barycentric cage embedder & Nanson cofactor normal skinning (3D FEM -> shared high-poly surface)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 TETRAHEDRAL CAGE EMBEDDER
//------------------------------------------------------------------------------------------------------------------------

CageEmbeddedSurface TetrahedralCageEmbedder::EmbedIntoFemSolid(
    const SimulationSpace::Deformable::DeformableBody& body,
    const HighPolySurfaceRest& restSurface)
{
    CageEmbeddedSurface out {};
    out.Mode        = CageEmbedMode::Tetrahedral3D;
    out.RestSurface = restSurface;

    const uint32_t V = restSurface.VertexCount();
    out.GpuVertices.resize(V);
    out.CageIndices.assign(static_cast<size_t>(V) * 4u, 0u);
    out.CageWeights.assign(static_cast<size_t>(V) * 4u, 0.0f);
    out.ExtraParams.assign(static_cast<size_t>(V) * 4u, 0.0f);

    if (V == 0u || body.TetCount == 0u)
    {
        return out;
    }

    const float* pos     = body.RestPositions.data();
    const uint32_t* tIdx = body.TetIndices.data();
    const float* Q       = body.TetInverseRestDm.data();

    const bool hasTireLayout = !body.FemTires.empty();
    const uint32_t S = hasTireLayout ? body.FemTires[0].SectorCount : 1u;
    const uint32_t P = hasTireLayout ? body.FemTires[0].ProfileCount : 2u;
    const uint32_t tetsPerSector = hasTireLayout ? ((P - 1u) * 5u) : body.TetCount;
    float hx = 0.0f;
    float hy = 0.0f;
    if (hasTireLayout)
    {
        const uint32_t base = body.FemTires[0].StartPointIndex;
        const uint32_t cagePts = S * P * 2u;
        double sumX = 0.0, sumY = 0.0;
        for (uint32_t k = 0u; k < cagePts; ++k)
        {
            const size_t i3 = static_cast<size_t>(base + k) * 3u;
            sumX += pos[i3 + 0u];
            sumY += pos[i3 + 1u];
        }
        hx = static_cast<float>(sumX / static_cast<double>(cagePts));
        hy = static_cast<float>(sumY / static_cast<double>(cagePts));
    }

    float maxRestErr = 0.0f;
    float maxAbsW    = 0.0f;

    for (uint32_t vi = 0u; vi < V; ++vi)
    {
        const size_t v3 = static_cast<size_t>(vi) * 3u;
        const float px = restSurface.RestPositions[v3];
        const float py = restSurface.RestPositions[v3 + 1u];
        const float pz = restSurface.RestPositions[v3 + 2u];

        uint32_t bestT = 0u;
        float bestScore = -1.0e9f;
        float bw0 = 1.0f, bw1 = 0.0f, bw2 = 0.0f, bw3 = 0.0f;

        const auto testTet = [&](uint32_t t) -> bool
        {
            const uint32_t i0 = tIdx[t * 4u];
            const float dx = px - pos[i0 * 3u];
            const float dy = py - pos[i0 * 3u + 1u];
            const float dz = pz - pos[i0 * 3u + 2u];
            const size_t o = static_cast<size_t>(t) * 9u;
            const float w1 = Q[o + 0u] * dx + Q[o + 1u] * dy + Q[o + 2u] * dz;
            const float w2 = Q[o + 3u] * dx + Q[o + 4u] * dy + Q[o + 5u] * dz;
            const float w3 = Q[o + 6u] * dx + Q[o + 7u] * dy + Q[o + 8u] * dz;
            const float w0 = 1.0f - w1 - w2 - w3;
            const float minW = std::min({ w0, w1, w2, w3 });
            const float maxW = std::max({ w0, w1, w2, w3 });
            const float score = minW - std::max(0.0f, maxW - 1.0f) * 0.25f;
            if (score > bestScore)
            {
                bestScore = score;
                bestT = t;
                bw0 = w0; bw1 = w1; bw2 = w2; bw3 = w3;
            }
            return minW >= -1.0e-4f;
        };

        if (hasTireLayout)
        {
            float ang = std::atan2(py - hy, px - hx);
            if (ang < 0.0f) ang += kTwoPi;
            const uint32_t si0 = static_cast<uint32_t>(std::floor((ang / kTwoPi) * static_cast<float>(S))) % S;
            bool inside = false;
            for (int32_t ds = -1; ds <= 1 && !inside; ++ds)
            {
                const uint32_t si = static_cast<uint32_t>((static_cast<int32_t>(si0) + ds + static_cast<int32_t>(S)) % static_cast<int32_t>(S));
                const uint32_t tStart = si * tetsPerSector;
                const uint32_t tEnd   = std::min(body.TetCount, tStart + tetsPerSector);
                for (uint32_t t = tStart; t < tEnd; ++t)
                {
                    if (testTet(t))
                    {
                        inside = true;
                        break;
                    }
                }
            }
        }
        else
        {
            for (uint32_t t = 0u; t < body.TetCount; ++t)
            {
                if (testTet(t)) break;
            }
        }

        const uint32_t i0 = tIdx[bestT * 4u + 0u];
        const uint32_t i1 = tIdx[bestT * 4u + 1u];
        const uint32_t i2 = tIdx[bestT * 4u + 2u];
        const uint32_t i3 = tIdx[bestT * 4u + 3u];

        const size_t o4 = static_cast<size_t>(vi) * 4u;
        out.CageIndices[o4 + 0u] = i0; out.CageIndices[o4 + 1u] = i1;
        out.CageIndices[o4 + 2u] = i2; out.CageIndices[o4 + 3u] = i3;
        out.CageWeights[o4 + 0u] = bw0; out.CageWeights[o4 + 1u] = bw1;
        out.CageWeights[o4 + 2u] = bw2; out.CageWeights[o4 + 3u] = bw3;
        out.ExtraParams[o4 + 0u] = static_cast<float>(bestT);

        CageBoundVertex& gv = out.GpuVertices[vi];
        gv.RestNormal  = { restSurface.RestNormals[v3], restSurface.RestNormals[v3 + 1u], restSurface.RestNormals[v3 + 2u] };
        gv.Albedo      = { restSurface.AlbedoRgb[v3], restSurface.AlbedoRgb[v3 + 1u], restSurface.AlbedoRgb[v3 + 2u] };
        gv.MaterialPbr = { restSurface.MaterialPbr[v3], restSurface.MaterialPbr[v3 + 1u], restSurface.MaterialPbr[v3 + 2u] };
        gv.CageIndices = { static_cast<float>(i0), static_cast<float>(i1), static_cast<float>(i2), static_cast<float>(i3) };
        gv.CageWeights = { bw0, bw1, bw2, bw3 };
        gv.ExtraParams = { static_cast<float>(bestT), 0.0f, 0.0f, 0.0f };

        const float rx = bw0 * pos[i0 * 3u]      + bw1 * pos[i1 * 3u]      + bw2 * pos[i2 * 3u]      + bw3 * pos[i3 * 3u];
        const float ry = bw0 * pos[i0 * 3u + 1u] + bw1 * pos[i1 * 3u + 1u] + bw2 * pos[i2 * 3u + 1u] + bw3 * pos[i3 * 3u + 1u];
        const float rz = bw0 * pos[i0 * 3u + 2u] + bw1 * pos[i1 * 3u + 2u] + bw2 * pos[i2 * 3u + 2u] + bw3 * pos[i3 * 3u + 2u];
        const float err = std::sqrt((rx - px) * (rx - px) + (ry - py) * (ry - py) + (rz - pz) * (rz - pz));
        maxRestErr = std::max(maxRestErr, err);
        maxAbsW    = std::max({ maxAbsW, std::fabs(bw0), std::fabs(bw1), std::fabs(bw2), std::fabs(bw3) });
    }

    out.MaxRestError = maxRestErr;
    out.MaxAbsWeight = maxAbsW;
    return out;
}

void TetrahedralCageEmbedder::EvaluateLiveSurface(
    const SimulationSpace::Deformable::DeformableBody& body,
    const CageEmbeddedSurface& embedded,
    DeformedSurfaceSample& outSample)
{
    const uint32_t V = embedded.VertexCount();
    outSample.Positions.resize(static_cast<size_t>(V) * 3u);
    outSample.Normals.resize(static_cast<size_t>(V) * 3u);
    outSample.Strain.resize(V);

    const float* pos  = body.Positions.data();
    const float* ns   = body.PointStrain.data();
    const float* tc   = body.TetCofactorNormals.data();
    const float* rNrm = embedded.RestSurface.RestNormals.data();

    for (uint32_t vi = 0u; vi < V; ++vi)
    {
        const size_t o4 = static_cast<size_t>(vi) * 4u;
        const size_t v3 = static_cast<size_t>(vi) * 3u;
        const uint32_t i0 = embedded.CageIndices[o4 + 0u];
        const uint32_t i1 = embedded.CageIndices[o4 + 1u];
        const uint32_t i2 = embedded.CageIndices[o4 + 2u];
        const uint32_t i3 = embedded.CageIndices[o4 + 3u];
        const float w0 = embedded.CageWeights[o4 + 0u];
        const float w1 = embedded.CageWeights[o4 + 1u];
        const float w2 = embedded.CageWeights[o4 + 2u];
        const float w3 = embedded.CageWeights[o4 + 3u];

        outSample.Positions[v3 + 0u] = w0 * pos[i0 * 3u + 0u] + w1 * pos[i1 * 3u + 0u] + w2 * pos[i2 * 3u + 0u] + w3 * pos[i3 * 3u + 0u];
        outSample.Positions[v3 + 1u] = w0 * pos[i0 * 3u + 1u] + w1 * pos[i1 * 3u + 1u] + w2 * pos[i2 * 3u + 1u] + w3 * pos[i3 * 3u + 1u];
        outSample.Positions[v3 + 2u] = w0 * pos[i0 * 3u + 2u] + w1 * pos[i1 * 3u + 2u] + w2 * pos[i2 * 3u + 2u] + w3 * pos[i3 * 3u + 2u];

        outSample.Strain[vi] = std::max(0.0f, w0 * ns[i0] + w1 * ns[i1] + w2 * ns[i2] + w3 * ns[i3]);

        const uint32_t tid = static_cast<uint32_t>(embedded.ExtraParams[o4] + 0.5f);
        const size_t to    = static_cast<size_t>(tid) * 9u;
        const float rx = rNrm[v3], ry = rNrm[v3 + 1u], rz = rNrm[v3 + 2u];
        const float nx = tc[to + 0u] * rx + tc[to + 3u] * ry + tc[to + 6u] * rz;
        const float ny = tc[to + 1u] * rx + tc[to + 4u] * ry + tc[to + 7u] * rz;
        const float nz = tc[to + 2u] * rx + tc[to + 5u] * ry + tc[to + 8u] * rz;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1.0e-5f)
        {
            outSample.Normals[v3 + 0u] = nx / nl;
            outSample.Normals[v3 + 1u] = ny / nl;
            outSample.Normals[v3 + 2u] = nz / nl;
        }
        else
        {
            outSample.Normals[v3 + 0u] = rx;
            outSample.Normals[v3 + 1u] = ry;
            outSample.Normals[v3 + 2u] = rz;
        }
    }
}

} // namespace Frontier::GeometrySpace::Deformation
