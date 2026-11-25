/*============================================================================================================================================
                                                     SURFACEQUADCAGEEMBEDDER.CPP
============================================================================================================================================*/
// 🧩 Phase D04 Bilinear quad-cage + signed normal extrusion embedder (Pneumatic XPBD shell -> shared high-poly surface)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                SURFACE QUAD CAGE EMBEDDER
//------------------------------------------------------------------------------------------------------------------------

CageEmbeddedSurface SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(
    const SimulationSpace::Deformable::DeformableBody& body,
    const HighPolySurfaceRest& restSurface)
{
    CageEmbeddedSurface out {};
    out.Mode        = CageEmbedMode::SurfaceQuad2D;
    out.RestSurface = restSurface;

    const uint32_t V = restSurface.VertexCount();
    out.GpuVertices.resize(V);
    out.CageIndices.assign(static_cast<size_t>(V) * 4u, 0u);
    out.CageWeights.assign(static_cast<size_t>(V) * 4u, 0.0f);
    out.ExtraParams.assign(static_cast<size_t>(V) * 4u, 0.0f);

    if (V == 0u || body.PneumaticTires.empty())
    {
        return out;
    }

    const SimulationSpace::Deformable::PneumaticTireCarcass& ty = body.PneumaticTires[0];
    const uint32_t S    = ty.SectorCount;
    const uint32_t P    = ty.ProfileCount;
    const uint32_t base = ty.StartPointIndex;
    const float* pos    = body.RestPositions.data();

    // Derive exact rest axle center from RestPositions (invariant even if live ty.AxleY has already displaced)
    double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
    const uint32_t cagePts = S * P;
    for (uint32_t k = 0u; k < cagePts; ++k)
    {
        const size_t i3 = static_cast<size_t>(base + k) * 3u;
        sumX += pos[i3 + 0u];
        sumY += pos[i3 + 1u];
        sumZ += pos[i3 + 2u];
    }
    const float hx = static_cast<float>(sumX / static_cast<double>(cagePts));
    const float hy = static_cast<float>(sumY / static_cast<double>(cagePts));
    const float hz = static_cast<float>(sumZ / static_cast<double>(cagePts));

    float maxRestErr = 0.0f;
    float maxAbsW    = 0.0f;

    for (uint32_t vi = 0u; vi < V; ++vi)
    {
        const size_t v3 = static_cast<size_t>(vi) * 3u;
        const float px = restSurface.RestPositions[v3];
        const float py = restSurface.RestPositions[v3 + 1u];
        const float pz = restSurface.RestPositions[v3 + 2u];
        const float rx = px - hx, ry = py - hy, rz = pz - hz;
        const float rV = std::hypot(rx, ry);

        float ang = std::atan2(ry, rx);
        if (ang < 0.0f) ang += kTwoPi;
        const float sf    = (ang / kTwoPi) * static_cast<float>(S);
        const uint32_t si = static_cast<uint32_t>(std::floor(sf)) % S;
        const uint32_t sNext = (si + 1u) % S;
        const float u     = sf - std::floor(sf);

        uint32_t bestPi = 0u;
        float bestV     = 0.0f;
        float bestD2    = 1.0e9f;
        for (uint32_t pi = 0u; pi + 1u < P; ++pi)
        {
            const float r0 = ty.ProfileStations[pi][0], z0 = ty.ProfileStations[pi][1];
            const float r1 = ty.ProfileStations[pi + 1u][0], z1 = ty.ProfileStations[pi + 1u][1];
            const float dr = r1 - r0, dz = z1 - z0;
            const float len2 = std::max(1.0e-8f, dr * dr + dz * dz);
            const float tv   = std::clamp(((rV - r0) * dr + (rz - z0) * dz) / len2, 0.0f, 1.0f);
            const float cr   = r0 + dr * tv, cz = z0 + dz * tv;
            const float d2   = (rV - cr) * (rV - cr) + (rz - cz) * (rz - cz);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                bestPi = pi;
                bestV  = tv;
            }
        }

        const uint32_t i00 = base + si * P + bestPi;
        const uint32_t i10 = base + sNext * P + bestPi;
        const uint32_t i11 = base + sNext * P + bestPi + 1u;
        const uint32_t i01 = base + si * P + bestPi + 1u;
        const float w00 = (1.0f - u) * (1.0f - bestV);
        const float w10 = u * (1.0f - bestV);
        const float w11 = u * bestV;
        const float w01 = (1.0f - u) * bestV;

        const float q00x = pos[i00 * 3u], q00y = pos[i00 * 3u + 1u], q00z = pos[i00 * 3u + 2u];
        const float q10x = pos[i10 * 3u], q10y = pos[i10 * 3u + 1u], q10z = pos[i10 * 3u + 2u];
        const float q11x = pos[i11 * 3u], q11y = pos[i11 * 3u + 1u], q11z = pos[i11 * 3u + 2u];
        const float q01x = pos[i01 * 3u], q01y = pos[i01 * 3u + 1u], q01z = pos[i01 * 3u + 2u];

        const float bx = w00 * q00x + w10 * q10x + w11 * q11x + w01 * q01x;
        const float by = w00 * q00y + w10 * q10y + w11 * q11y + w01 * q01y;
        const float bz = w00 * q00z + w10 * q10z + w11 * q11z + w01 * q01z;

        const float eUx = 0.5f * ((q10x - q00x) + (q11x - q01x));
        const float eUy = 0.5f * ((q10y - q00y) + (q11y - q01y));
        const float eUz = 0.5f * ((q10z - q00z) + (q11z - q01z));
        const float eVx = 0.5f * ((q01x - q00x) + (q11x - q10x));
        const float eVy = 0.5f * ((q01y - q00y) + (q11y - q10y));
        const float eVz = 0.5f * ((q01z - q00z) + (q11z - q10z));

        const float tl = std::max(1.0e-6f, std::sqrt(eUx * eUx + eUy * eUy + eUz * eUz));
        const float Tx = eUx / tl, Ty = eUy / tl, Tz = eUz / tl;
        float Nx = eUy * eVz - eUz * eVy, Ny = eUz * eVx - eUx * eVz, Nz = eUx * eVy - eUy * eVx;
        const float nl = std::max(1.0e-6f, std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz));
        Nx /= nl; Ny /= nl; Nz /= nl;
        const float Bx = Ny * Tz - Nz * Ty, By = Nz * Tx - Nx * Tz, Bz = Nx * Ty - Ny * Tx;

        const float hOff = (px - bx) * Nx + (py - by) * Ny + (pz - bz) * Nz;
        const float rnx  = restSurface.RestNormals[v3];
        const float rny  = restSurface.RestNormals[v3 + 1u];
        const float rnz  = restSurface.RestNormals[v3 + 2u];

        const float nT = rnx * Tx + rny * Ty + rnz * Tz;
        const float nB = rnx * Bx + rny * By + rnz * Bz;
        const float nN = rnx * Nx + rny * Ny + rnz * Nz;

        const size_t o4 = static_cast<size_t>(vi) * 4u;
        out.CageIndices[o4 + 0u] = i00; out.CageIndices[o4 + 1u] = i10;
        out.CageIndices[o4 + 2u] = i11; out.CageIndices[o4 + 3u] = i01;
        out.CageWeights[o4 + 0u] = w00; out.CageWeights[o4 + 1u] = w10;
        out.CageWeights[o4 + 2u] = w11; out.CageWeights[o4 + 3u] = w01;
        out.ExtraParams[o4 + 0u] = nT;  out.ExtraParams[o4 + 1u] = nB;
        out.ExtraParams[o4 + 2u] = nN;  out.ExtraParams[o4 + 3u] = hOff;

        CageBoundVertex& gv = out.GpuVertices[vi];
        gv.RestNormal  = { rnx, rny, rnz };
        gv.Albedo      = { restSurface.AlbedoRgb[v3], restSurface.AlbedoRgb[v3 + 1u], restSurface.AlbedoRgb[v3 + 2u] };
        gv.MaterialPbr = { restSurface.MaterialPbr[v3], restSurface.MaterialPbr[v3 + 1u], restSurface.MaterialPbr[v3 + 2u] };
        gv.CageIndices = { static_cast<float>(i00), static_cast<float>(i10), static_cast<float>(i11), static_cast<float>(i01) };
        gv.CageWeights = { w00, w10, w11, w01 };
        gv.ExtraParams = { nT, nB, nN, hOff };

        const float rxP = bx + Nx * hOff, ryP = by + Ny * hOff, rzP = bz + Nz * hOff;
        const float err = std::sqrt((rxP - px) * (rxP - px) + (ryP - py) * (ryP - py) + (rzP - pz) * (rzP - pz));
        maxRestErr = std::max(maxRestErr, err);
        maxAbsW    = std::max({ maxAbsW, w00, w10, w11, w01 });
    }

    out.MaxRestError = maxRestErr;
    out.MaxAbsWeight = maxAbsW;
    return out;
}

void SurfaceQuadCageEmbedder::EvaluateLiveSurface(
    const SimulationSpace::Deformable::DeformableBody& body,
    const CageEmbeddedSurface& embedded,
    DeformedSurfaceSample& outSample)
{
    const uint32_t V = embedded.VertexCount();
    outSample.Positions.resize(static_cast<size_t>(V) * 3u);
    outSample.Normals.resize(static_cast<size_t>(V) * 3u);
    outSample.Strain.resize(V);

    const float* pos = body.Positions.data();
    const bool hasTire = !body.PneumaticTires.empty();
    const float hx = hasTire ? body.PneumaticTires[0].AxleX : 0.0f;
    const float hy = hasTire ? body.PneumaticTires[0].AxleY : 0.0f;
    const float outerR = hasTire ? body.PneumaticTires[0].OuterRadius : 1.14f;

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

        const float q00x = pos[i0 * 3u], q00y = pos[i0 * 3u + 1u], q00z = pos[i0 * 3u + 2u];
        const float q10x = pos[i1 * 3u], q10y = pos[i1 * 3u + 1u], q10z = pos[i1 * 3u + 2u];
        const float q11x = pos[i2 * 3u], q11y = pos[i2 * 3u + 1u], q11z = pos[i2 * 3u + 2u];
        const float q01x = pos[i3 * 3u], q01y = pos[i3 * 3u + 1u], q01z = pos[i3 * 3u + 2u];

        const float bx = w0 * q00x + w1 * q10x + w2 * q11x + w3 * q01x;
        const float by = w0 * q00y + w1 * q10y + w2 * q11y + w3 * q01y;
        const float bz = w0 * q00z + w1 * q10z + w2 * q11z + w3 * q01z;

        const float eUx = 0.5f * ((q10x - q00x) + (q11x - q01x));
        const float eUy = 0.5f * ((q10y - q00y) + (q11y - q01y));
        const float eUz = 0.5f * ((q10z - q00z) + (q11z - q01z));
        const float eVx = 0.5f * ((q01x - q00x) + (q11x - q10x));
        const float eVy = 0.5f * ((q01y - q00y) + (q11y - q10y));
        const float eVz = 0.5f * ((q01z - q00z) + (q11z - q10z));

        const float tl = std::max(1.0e-6f, std::sqrt(eUx * eUx + eUy * eUy + eUz * eUz));
        const float Tx = eUx / tl, Ty = eUy / tl, Tz = eUz / tl;
        float Qx = eUy * eVz - eUz * eVy, Qy = eUz * eVx - eUx * eVz, Qz = eUx * eVy - eUy * eVx;
        const float ql = std::max(1.0e-6f, std::sqrt(Qx * Qx + Qy * Qy + Qz * Qz));
        Qx /= ql; Qy /= ql; Qz /= ql;
        const float Bx = Qy * Tz - Qz * Ty, By = Qz * Tx - Qx * Tz, Bz = Qx * Ty - Qy * Tx;

        const float nT = embedded.ExtraParams[o4 + 0u];
        const float nB = embedded.ExtraParams[o4 + 1u];
        const float nN = embedded.ExtraParams[o4 + 2u];
        const float hOff = embedded.ExtraParams[o4 + 3u];

        const float wx = bx + Qx * hOff;
        const float wy = by + Qy * hOff;
        const float wz = bz + Qz * hOff;

        outSample.Positions[v3 + 0u] = wx;
        outSample.Positions[v3 + 1u] = wy;
        outSample.Positions[v3 + 2u] = wz;

        const float nx = Tx * nT + Bx * nB + Qx * nN;
        const float ny = Ty * nT + By * nB + Qy * nN;
        const float nz = Tz * nT + Bz * nB + Qz * nN;
        const float nl = std::max(1.0e-6f, std::sqrt(nx * nx + ny * ny + nz * nz));
        outSample.Normals[v3 + 0u] = nx / nl;
        outSample.Normals[v3 + 1u] = ny / nl;
        outSample.Normals[v3 + 2u] = nz / nl;

        const float dAxle = std::hypot(wx - hx, wy - hy);
        outSample.Strain[vi] = std::max(0.0f, (outerR - dAxle) * 4.5f);
    }
}

} // namespace Frontier::GeometrySpace::Deformation
