/*============================================================================================================================================
                                                        TIRESURFACEBUILDER.CPP
============================================================================================================================================*/
// 🧩 Phase D04 Shared 7,552-vertex / 12,544-triangle sculpted automotive sport tire surface generator (used by FEM, XPBD & VAT)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIRE SURFACE BUILDER
//------------------------------------------------------------------------------------------------------------------------

HighPolySurfaceRest TireSurfaceBuilder::ConstructSportTireSurface(
    float hx,
    float hy,
    float hz,
    float rimR,
    float outerR,
    float halfW)
{
    HighPolySurfaceRest out {};
    out.RestPositions.reserve(7600u * 3u);
    out.RestNormals.reserve(7600u * 3u);
    out.AlbedoRgb.reserve(7600u * 3u);
    out.MaterialPbr.reserve(7600u * 3u);
    out.Indices.reserve(12600u * 3u);

    const auto addV = [&](
        float px, float py, float pz,
        float nx, float ny, float nz,
        float cr, float cg, float cb,
        float rough, float isTread, float ao) -> uint32_t
    {
        const uint32_t id = static_cast<uint32_t>(out.RestPositions.size() / 3u);
        const float nl = std::max(1.0e-6f, std::sqrt(nx * nx + ny * ny + nz * nz));
        out.RestPositions.push_back(px);     out.RestPositions.push_back(py);     out.RestPositions.push_back(pz);
        out.RestNormals.push_back(nx / nl);  out.RestNormals.push_back(ny / nl);  out.RestNormals.push_back(nz / nl);
        out.AlbedoRgb.push_back(cr);         out.AlbedoRgb.push_back(cg);         out.AlbedoRgb.push_back(cb);
        out.MaterialPbr.push_back(rough);    out.MaterialPbr.push_back(isTread);  out.MaterialPbr.push_back(ao);
        return id;
    };

    const float H  = outerR - rimR;
    const float gD = 0.022f;

    struct ProfileStation
    {
        float r, z, isTread, ao, rough;
    };

    const std::array<ProfileStation, 18u> halfProf = {{
        { rimR + 0.006f,     halfW * 0.77f,  0.0f, 0.78f, 0.56f },
        { rimR + H * 0.09f,  halfW * 0.88f,  0.0f, 0.92f, 0.44f },
        { rimR + H * 0.16f,  halfW * 0.85f,  0.0f, 0.85f, 0.52f },
        { rimR + H * 0.32f,  halfW * 0.97f,  0.0f, 0.96f, 0.50f },
        { rimR + H * 0.48f,  halfW * 1.02f,  0.0f, 0.98f, 0.48f },
        { rimR + H * 0.54f,  halfW * 1.032f, 0.0f, 1.00f, 0.42f },
        { rimR + H * 0.60f,  halfW * 1.01f,  0.0f, 0.94f, 0.50f },
        { rimR + H * 0.78f,  halfW * 0.94f,  0.0f, 0.94f, 0.56f },
        { outerR - 0.042f,   halfW * 0.84f,  0.0f, 0.92f, 0.62f },
        { outerR - 0.014f,   halfW * 0.76f,  1.0f, 0.98f, 0.72f },
        { outerR - 0.006f,   halfW * 0.56f,  1.0f, 0.98f, 0.74f },
        { outerR - gD,       halfW * 0.52f,  1.0f, 0.42f, 0.84f },
        { outerR - gD,       halfW * 0.45f,  1.0f, 0.42f, 0.84f },
        { outerR - 0.002f,   halfW * 0.41f,  1.0f, 0.98f, 0.74f },
        { outerR - 0.001f,   halfW * 0.21f,  1.0f, 0.98f, 0.74f },
        { outerR - gD,       halfW * 0.17f,  1.0f, 0.40f, 0.84f },
        { outerR - gD,       halfW * 0.10f,  1.0f, 0.40f, 0.84f },
        { outerR,            0.0f,           1.0f, 1.00f, 0.74f }
    }};

    std::vector<ProfileStation> prof;
    prof.reserve(35u);
    for (size_t i = 0u; i + 1u < halfProf.size(); ++i)
    {
        ProfileStation s = halfProf[i];
        s.z = -s.z;
        prof.push_back(s);
    }
    for (size_t i = halfProf.size(); i > 0u; --i)
    {
        prof.push_back(halfProf[i - 1u]);
    }

    const uint32_t P    = static_cast<uint32_t>(prof.size()); // 35
    constexpr uint32_t SECS = 128u;

    std::vector<std::array<float, 2>> profN(P);
    for (uint32_t pi = 0u; pi < P; ++pi)
    {
        const ProfileStation& pPrev = prof[pi > 0u ? pi - 1u : 0u];
        const ProfileStation& pNext = prof[pi + 1u < P ? pi + 1u : P - 1u];
        const float dr = pNext.r - pPrev.r;
        const float dz = pNext.z - pPrev.z;
        float nr = dz, nz = -dr;
        if (nr * (prof[pi].r - rimR) + nz * prof[pi].z < 0.0f)
        {
            nr = -nr;
            nz = -nz;
        }
        const float l = std::max(1.0e-6f, std::hypot(nr, nz));
        profN[pi] = { nr / l, nz / l };
    }

    for (uint32_t si = 0u; si < SECS; ++si)
    {
        const float a  = (static_cast<float>(si) / static_cast<float>(SECS)) * kTwoPi;
        const float ca = std::cos(a), sa = std::sin(a);
        for (uint32_t pi = 0u; pi < P; ++pi)
        {
            const ProfileStation& st = prof[pi];
            const float nr = profN[pi][0], nz = profN[pi][1];
            const float cr = (st.isTread > 0.5f) ? 0.115f : 0.095f;
            const float cg = (st.isTread > 0.5f) ? 0.120f : 0.100f;
            const float cb = (st.isTread > 0.5f) ? 0.132f : 0.112f;
            addV(hx + ca * st.r, hy + sa * st.r, hz + st.z, ca * nr, sa * nr, nz, cr, cg, cb, st.rough, st.isTread, st.ao);
        }
    }

    for (uint32_t si = 0u; si < SECS; ++si)
    {
        const uint32_t sNext = (si + 1u) % SECS;
        for (uint32_t pi = 0u; pi + 1u < P; ++pi)
        {
            const uint32_t i00 = si * P + pi;
            const uint32_t i10 = sNext * P + pi;
            const uint32_t i11 = sNext * P + pi + 1u;
            const uint32_t i01 = si * P + pi + 1u;
            out.Indices.push_back(i00); out.Indices.push_back(i10); out.Indices.push_back(i11);
            out.Indices.push_back(i00); out.Indices.push_back(i11); out.Indices.push_back(i01);
        }
    }

    const auto addLugPrism = [&](float a0, float a1, float z0, float z1, float rBase, float rTop, float skewA,
                                 const std::array<float, 3>& col, float rough)
    {
        const float insetA = (a1 - a0) * 0.16f;
        const float insetZ = (z1 - z0) * 0.12f;
        const float bA[4] = { a0, a1, a1 + skewA, a0 + skewA };
        const float bZ[4] = { z0, z0, z1, z1 };
        const float tA[4] = { a0 + insetA, a1 - insetA, a1 - insetA + skewA, a0 + insetA + skewA };
        const float tZ[4] = { z0 + insetZ, z0 + insetZ, z1 - insetZ, z1 - insetZ };
        uint32_t bIdx[4], tIdx[4];
        for (uint32_t k = 0u; k < 4u; ++k)
        {
            const float cb = std::cos(bA[k]), sb = std::sin(bA[k]);
            const float ct = std::cos(tA[k]), st = std::sin(tA[k]);
            bIdx[k] = addV(hx + cb * rBase, hy + sb * rBase, hz + bZ[k], cb, sb, (k >= 2u ? 0.35f : -0.35f),
                           col[0] * 0.78f, col[1] * 0.78f, col[2] * 0.78f, rough + 0.08f, 1.0f, 0.72f);
            tIdx[k] = addV(hx + ct * rTop, hy + st * rTop, hz + tZ[k], ct, st, 0.0f,
                           col[0], col[1], col[2], rough, 1.0f, 1.0f);
        }
        out.Indices.push_back(tIdx[0]); out.Indices.push_back(tIdx[1]); out.Indices.push_back(tIdx[2]);
        out.Indices.push_back(tIdx[0]); out.Indices.push_back(tIdx[2]); out.Indices.push_back(tIdx[3]);
        for (uint32_t k = 0u; k < 4u; ++k)
        {
            const uint32_t kn = (k + 1u) & 3u;
            out.Indices.push_back(bIdx[k]); out.Indices.push_back(bIdx[kn]); out.Indices.push_back(tIdx[kn]);
            out.Indices.push_back(bIdx[k]); out.Indices.push_back(tIdx[kn]); out.Indices.push_back(tIdx[k]);
        }
    };

    struct LugBand { float z0, z1, rBase, lugH, phase, skew; };
    const std::array<LugBand, 5u> lugBands = {{
        { -halfW * 0.75f, -halfW * 0.54f, outerR - 0.011f, 0.016f, 0.0f,  -0.022f },
        { -halfW * 0.43f, -halfW * 0.19f, outerR - 0.003f, 0.015f, 0.5f,  -0.014f },
        { -halfW * 0.09f,  halfW * 0.09f, outerR - 0.001f, 0.014f, 0.25f,  0.006f },
        {  halfW * 0.19f,  halfW * 0.43f, outerR - 0.003f, 0.015f, 0.5f,   0.014f },
        {  halfW * 0.54f,  halfW * 0.75f, outerR - 0.011f, 0.016f, 0.0f,   0.022f }
    }};
    constexpr uint32_t PITCHES = 64u;
    const float dPitch = kTwoPi / static_cast<float>(PITCHES);
    for (size_t bi = 0u; bi < lugBands.size(); ++bi)
    {
        const LugBand& b = lugBands[bi];
        const float fillFrac = (bi == 2u) ? 0.76f : 0.66f;
        for (uint32_t k = 0u; k < PITCHES; ++k)
        {
            const float aStart = (static_cast<float>(k) + b.phase) * dPitch;
            addLugPrism(aStart, aStart + dPitch * fillFrac, b.z0, b.z1, b.rBase, b.rBase + b.lugH,
                        b.skew, { 0.135f, 0.142f, 0.156f }, 0.72f);
        }
    }

    constexpr uint32_t PAD_COUNT = 32u;
    const float dPad = kTwoPi / static_cast<float>(PAD_COUNT);
    for (float sgn : { -1.0f, 1.0f })
    {
        const float zSurf = sgn * (halfW * 0.95f);
        const float zTop  = sgn * (halfW * 0.995f);
        const float rIn   = rimR + H * 0.64f;
        const float rOut  = outerR - 0.055f;
        for (uint32_t k = 0u; k < PAD_COUNT; ++k)
        {
            const float a0 = static_cast<float>(k) * dPad + 0.02f;
            const float a1 = a0 + dPad * 0.46f;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            const float am = 0.5f * (a0 + a1), cm = std::cos(am), sm = std::sin(am);

            const uint32_t b0 = addV(hx + c0 * rIn,  hy + s0 * rIn,  hz + zSurf, cm * 0.25f, sm * 0.25f, sgn * 0.96f, 0.09f, 0.095f, 0.11f, 0.55f, 0.0f, 0.80f);
            const uint32_t b1 = addV(hx + c1 * rIn,  hy + s1 * rIn,  hz + zSurf, cm * 0.25f, sm * 0.25f, sgn * 0.96f, 0.09f, 0.095f, 0.11f, 0.55f, 0.0f, 0.80f);
            const uint32_t b2 = addV(hx + c1 * rOut, hy + s1 * rOut, hz + zSurf, cm * 0.25f, sm * 0.25f, sgn * 0.96f, 0.09f, 0.095f, 0.11f, 0.55f, 0.0f, 0.80f);
            const uint32_t b3 = addV(hx + c0 * rOut, hy + s0 * rOut, hz + zSurf, cm * 0.25f, sm * 0.25f, sgn * 0.96f, 0.09f, 0.095f, 0.11f, 0.55f, 0.0f, 0.80f);

            const uint32_t t0 = addV(hx + c0 * (rIn + 0.01f),  hy + s0 * (rIn + 0.01f),  hz + zTop, cm * 0.18f, sm * 0.18f, sgn * 0.98f, 0.14f, 0.148f, 0.165f, 0.44f, 0.0f, 1.0f);
            const uint32_t t1 = addV(hx + c1 * (rIn + 0.01f),  hy + s1 * (rIn + 0.01f),  hz + zTop, cm * 0.18f, sm * 0.18f, sgn * 0.98f, 0.14f, 0.148f, 0.165f, 0.44f, 0.0f, 1.0f);
            const uint32_t t2 = addV(hx + c1 * (rOut - 0.01f), hy + s1 * (rOut - 0.01f), hz + zTop, cm * 0.18f, sm * 0.18f, sgn * 0.98f, 0.14f, 0.148f, 0.165f, 0.44f, 0.0f, 1.0f);
            const uint32_t t3 = addV(hx + c0 * (rOut - 0.01f), hy + s0 * (rOut - 0.01f), hz + zTop, cm * 0.18f, sm * 0.18f, sgn * 0.98f, 0.14f, 0.148f, 0.165f, 0.44f, 0.0f, 1.0f);

            out.Indices.push_back(t0); out.Indices.push_back(t1); out.Indices.push_back(t2);
            out.Indices.push_back(t0); out.Indices.push_back(t2); out.Indices.push_back(t3);
            out.Indices.push_back(b0); out.Indices.push_back(b1); out.Indices.push_back(t1);
            out.Indices.push_back(b0); out.Indices.push_back(t1); out.Indices.push_back(t0);
            out.Indices.push_back(b1); out.Indices.push_back(b2); out.Indices.push_back(t2);
            out.Indices.push_back(b1); out.Indices.push_back(t2); out.Indices.push_back(t1);
            out.Indices.push_back(b2); out.Indices.push_back(b3); out.Indices.push_back(t3);
            out.Indices.push_back(b2); out.Indices.push_back(t3); out.Indices.push_back(t2);
            out.Indices.push_back(b3); out.Indices.push_back(b0); out.Indices.push_back(t0);
            out.Indices.push_back(b3); out.Indices.push_back(t0); out.Indices.push_back(t3);
        }
    }

    return out;
}

} // namespace Frontier::GeometrySpace::Deformation
