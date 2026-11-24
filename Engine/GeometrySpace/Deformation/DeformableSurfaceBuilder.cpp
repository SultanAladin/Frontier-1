/*============================================================================================================================================
                                                   DEFORMABLESURFACEBUILDER.CPP
============================================================================================================================================*/
// 🧩 Phase D13 Unified C++20 procedural 3D triangle surface generator for ALL 9 deformable presets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

[[nodiscard]] Vec3 Add(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
[[nodiscard]] Vec3 Sub(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
[[nodiscard]] Vec3 Mul(Vec3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
[[nodiscard]] float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 Cross(Vec3 a, Vec3 b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
[[nodiscard]] float Length(Vec3 a) noexcept { return std::sqrt(Dot(a, a)); }
[[nodiscard]] Vec3 Normalize(Vec3 a, Vec3 fallback = { 0.0f, 1.0f, 0.0f }) noexcept
{
    const float l = Length(a);
    return (l > 1.0e-6f) ? Vec3 { a.x / l, a.y / l, a.z / l } : fallback;
}

uint32_t PushVertex(
    ProceduralRenderSurface& dst,
    Vec3 pos,
    Vec3 nrm,
    const std::array<float, 3>& rgb,
    const std::array<float, 3>& pbr,
    float strain)
{
    const uint32_t idx = dst.VertexCount();
    const Vec3 un = Normalize(nrm);
    dst.Positions.push_back(pos.x);   dst.Positions.push_back(pos.y);   dst.Positions.push_back(pos.z);
    dst.Normals.push_back(un.x);      dst.Normals.push_back(un.y);      dst.Normals.push_back(un.z);
    dst.AlbedoRgb.push_back(rgb[0]);  dst.AlbedoRgb.push_back(rgb[1]);  dst.AlbedoRgb.push_back(rgb[2]);
    dst.MaterialPbr.push_back(pbr[0]);dst.MaterialPbr.push_back(pbr[1]);dst.MaterialPbr.push_back(pbr[2]);
    dst.Strain.push_back(strain);
    return idx;
}

void PushQuad(ProceduralRenderSurface& dst, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    dst.Indices.push_back(a); dst.Indices.push_back(b); dst.Indices.push_back(c);
    dst.Indices.push_back(a); dst.Indices.push_back(c); dst.Indices.push_back(d);
}

[[nodiscard]] Vec3 ReadPoint(const SimulationSpace::Deformable::DeformableBody& body, uint32_t idx) noexcept
{
    const size_t i3 = static_cast<size_t>(idx) * 3u;
    return { body.Positions[i3], body.Positions[i3 + 1u], body.Positions[i3 + 2u] };
}

void AppendEllipsoid(
    ProceduralRenderSurface& dst,
    Vec3 center,
    Vec3 radii,
    uint32_t segU,
    uint32_t segV,
    const std::array<float, 3>& rgb,
    const std::array<float, 3>& pbr,
    float strain)
{
    const uint32_t firstVertex = dst.VertexCount();
    for (uint32_t iv = 0u; iv <= segV; ++iv)
    {
        const float phi = kPi * static_cast<float>(iv) / static_cast<float>(segV);
        const float sp  = std::sin(phi);
        const float cp  = std::cos(phi);
        for (uint32_t iu = 0u; iu <= segU; ++iu)
        {
            const float th = kTwoPi * static_cast<float>(iu) / static_cast<float>(segU);
            const float ct = std::cos(th);
            const float st = std::sin(th);
            const Vec3 dir { sp * ct, cp, sp * st };
            const Vec3 p { center.x + dir.x * radii.x, center.y + dir.y * radii.y, center.z + dir.z * radii.z };
            const Vec3 n { dir.x / std::max(1.0e-4f, radii.x), dir.y / std::max(1.0e-4f, radii.y), dir.z / std::max(1.0e-4f, radii.z) };
            (void)PushVertex(dst, p, n, rgb, pbr, strain);
        }
    }
    const uint32_t stride = segU + 1u;
    for (uint32_t iv = 0u; iv < segV; ++iv)
    {
        for (uint32_t iu = 0u; iu < segU; ++iu)
        {
            const uint32_t a = firstVertex + iv * stride + iu;
            const uint32_t b = a + 1u;
            const uint32_t c = firstVertex + (iv + 1u) * stride + iu + 1u;
            const uint32_t d = firstVertex + (iv + 1u) * stride + iu;
            PushQuad(dst, a, d, c, b);
        }
    }
}

void AppendBox(
    ProceduralRenderSurface& dst,
    Vec3 center,
    Vec3 halfExtents,
    const std::array<float, 3>& rgb,
    const std::array<float, 3>& pbr)
{
    static constexpr Vec3 kFaces[6][5] = {
        { { 1, 0, 0 }, {  1, -1, -1 }, {  1,  1, -1 }, {  1,  1,  1 }, {  1, -1,  1 } },
        { {-1, 0, 0 }, { -1, -1,  1 }, { -1,  1,  1 }, { -1,  1, -1 }, { -1, -1, -1 } },
        { { 0, 1, 0 }, { -1,  1, -1 }, { -1,  1,  1 }, {  1,  1,  1 }, {  1,  1, -1 } },
        { { 0,-1, 0 }, { -1, -1,  1 }, { -1, -1, -1 }, {  1, -1, -1 }, {  1, -1,  1 } },
        { { 0, 0, 1 }, { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 } },
        { { 0, 0,-1 }, {  1, -1, -1 }, { -1, -1, -1 }, { -1,  1, -1 }, {  1,  1, -1 } }
    };
    for (const auto& f : kFaces)
    {
        const Vec3 n = f[0];
        uint32_t v[4];
        for (int k = 0; k < 4; ++k)
        {
            const Vec3 p {
                center.x + f[k + 1].x * halfExtents.x,
                center.y + f[k + 1].y * halfExtents.y,
                center.z + f[k + 1].z * halfExtents.z
            };
            v[k] = PushVertex(dst, p, n, rgb, pbr, 0.0f);
        }
        PushQuad(dst, v[0], v[1], v[2], v[3]);
    }
}

void AppendCylinderBetween(
    ProceduralRenderSurface& dst,
    Vec3 p0,
    Vec3 p1,
    float r0,
    float r1,
    uint32_t sides,
    const std::array<float, 3>& rgb0,
    const std::array<float, 3>& rgb1,
    const std::array<float, 3>& pbr,
    float s0,
    float s1)
{
    const Vec3 axis = Sub(p1, p0);
    if (Length(axis) < 1.0e-5f) return;
    const Vec3 T = Normalize(axis);
    const Vec3 up = (std::fabs(T.y) < 0.9f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
    const Vec3 U = Normalize(Cross(up, T));
    const Vec3 V = Cross(T, U);

    const uint32_t firstVertex = dst.VertexCount();
    for (uint32_t k = 0u; k <= sides; ++k)
    {
        const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(sides);
        const Vec3 radial = Add(Mul(U, std::cos(a)), Mul(V, std::sin(a)));
        (void)PushVertex(dst, Add(p0, Mul(radial, r0)), radial, rgb0, pbr, s0);
        (void)PushVertex(dst, Add(p1, Mul(radial, r1)), radial, rgb1, pbr, s1);
    }
    for (uint32_t k = 0u; k < sides; ++k)
    {
        const uint32_t a = firstVertex + k * 2u;
        const uint32_t b = a + 1u;
        const uint32_t c = a + 3u;
        const uint32_t d = a + 2u;
        PushQuad(dst, a, b, c, d);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  DEFORMABLE SURFACE BUILDER
//------------------------------------------------------------------------------------------------------------------------

void DeformableSurfaceBuilder::AppendSkinnedSurface(
    const HighPolySurfaceRest& restSurface,
    const DeformedSurfaceSample& deformedSample,
    ProceduralRenderSurface& outSurface)
{
    const uint32_t V = restSurface.VertexCount();
    if (V == 0u || deformedSample.Positions.size() < static_cast<size_t>(V) * 3u)
    {
        return;
    }

    const uint32_t firstVertex = outSurface.VertexCount();
    const bool hasNormals = deformedSample.Normals.size() >= static_cast<size_t>(V) * 3u;
    const bool hasStrain  = deformedSample.Strain.size() >= V;
    const bool hasAlbedo  = restSurface.AlbedoRgb.size() >= static_cast<size_t>(V) * 3u;
    const bool hasPbr     = restSurface.MaterialPbr.size() >= static_cast<size_t>(V) * 3u;

    for (uint32_t vi = 0u; vi < V; ++vi)
    {
        const size_t i3 = static_cast<size_t>(vi) * 3u;
        const Vec3 p { deformedSample.Positions[i3], deformedSample.Positions[i3 + 1u], deformedSample.Positions[i3 + 2u] };
        const Vec3 n = hasNormals
            ? Vec3 { deformedSample.Normals[i3], deformedSample.Normals[i3 + 1u], deformedSample.Normals[i3 + 2u] }
            : Vec3 { 0.0f, 1.0f, 0.0f };
        const std::array<float, 3> rgb = hasAlbedo
            ? std::array<float, 3> { restSurface.AlbedoRgb[i3], restSurface.AlbedoRgb[i3 + 1u], restSurface.AlbedoRgb[i3 + 2u] }
            : std::array<float, 3> { 0.11f, 0.115f, 0.125f };
        const std::array<float, 3> pbr = hasPbr
            ? std::array<float, 3> { restSurface.MaterialPbr[i3], 0.0f, 0.04f }
            : std::array<float, 3> { 0.65f, 0.0f, 0.04f };
        const float st = hasStrain ? deformedSample.Strain[vi] : 0.0f;
        (void)PushVertex(outSurface, p, n, rgb, pbr, st);
    }

    for (uint32_t idx : restSurface.Indices)
    {
        outSurface.Indices.push_back(firstVertex + idx);
    }
}

void DeformableSurfaceBuilder::AppendForgedAlloyRim(
    ProceduralRenderSurface& outSurface,
    float axleX,
    float axleY,
    float axleZ,
    float spinAngleRad,
    float rimRadius,
    float beadHalfWidthZ)
{
    const std::array<float, 3> alloySilver { 0.72f, 0.75f, 0.80f };
    const std::array<float, 3> alloyDark   { 0.28f, 0.30f, 0.34f };
    const std::array<float, 3> alloyPbr    { 0.22f, 0.0f, 0.92f };
    const std::array<float, 3> rotorPbr    { 0.36f, 0.0f, 0.85f };

    // Outer barrel ring
    constexpr uint32_t kSegs = 36u;
    const float rOuter = rimRadius * 1.015f;
    const float rInner = rimRadius * 0.88f;
    const uint32_t barrelStart = outSurface.VertexCount();
    for (uint32_t s = 0u; s <= kSegs; ++s)
    {
        const float a = spinAngleRad + kTwoPi * static_cast<float>(s) / static_cast<float>(kSegs);
        const float ca = std::cos(a), sa = std::sin(a);
        const Vec3 nOut { ca, sa, 0.0f };
        const Vec3 nIn  { -ca, -sa, 0.0f };
        (void)PushVertex(outSurface, { axleX + ca * rOuter, axleY + sa * rOuter, axleZ - beadHalfWidthZ }, nOut, alloySilver, alloyPbr, 0.0f);
        (void)PushVertex(outSurface, { axleX + ca * rOuter, axleY + sa * rOuter, axleZ + beadHalfWidthZ }, nOut, alloySilver, alloyPbr, 0.0f);
        (void)PushVertex(outSurface, { axleX + ca * rInner, axleY + sa * rInner, axleZ + beadHalfWidthZ }, nIn,  alloyDark,   alloyPbr, 0.0f);
        (void)PushVertex(outSurface, { axleX + ca * rInner, axleY + sa * rInner, axleZ - beadHalfWidthZ }, nIn,  alloyDark,   alloyPbr, 0.0f);
    }
    for (uint32_t s = 0u; s < kSegs; ++s)
    {
        const uint32_t b0 = barrelStart + s * 4u;
        const uint32_t b1 = barrelStart + (s + 1u) * 4u;
        PushQuad(outSurface, b0 + 0u, b0 + 1u, b1 + 1u, b1 + 0u);
        PushQuad(outSurface, b0 + 2u, b0 + 3u, b1 + 3u, b1 + 2u);
        PushQuad(outSurface, b0 + 1u, b0 + 2u, b1 + 2u, b1 + 1u);
        PushQuad(outSurface, b0 + 3u, b0 + 0u, b1 + 0u, b1 + 3u);
    }

    // Center hub + brake rotor disc
    AppendEllipsoid(outSurface, { axleX, axleY, axleZ }, { rimRadius * 0.24f, rimRadius * 0.24f, beadHalfWidthZ * 0.92f },
                    14u, 8u, alloyDark, rotorPbr, 0.0f);

    // 5 twin-split forged alloy spokes on both faces
    for (uint32_t sp = 0u; sp < 5u; ++sp)
    {
        const float spokeAngle = spinAngleRad + kTwoPi * static_cast<float>(sp) / 5.0f;
        for (int sub = -1; sub <= 1; sub += 2)
        {
            const float a0 = spokeAngle + static_cast<float>(sub) * 0.085f;
            const float a1 = spokeAngle + static_cast<float>(sub) * 0.145f;
            const Vec3 axlePt  { axleX + std::cos(a0) * rimRadius * 0.18f, axleY + std::sin(a0) * rimRadius * 0.18f, axleZ + beadHalfWidthZ * 0.52f };
            const Vec3 lipPt  { axleX + std::cos(a1) * rimRadius * 0.92f, axleY + std::sin(a1) * rimRadius * 0.92f, axleZ + beadHalfWidthZ * 0.68f };
            AppendCylinderBetween(outSurface, axlePt, lipPt, 0.042f, 0.030f, 6u, alloySilver, alloySilver, alloyPbr, 0.0f, 0.0f);

            const Vec3 axleBack { axleX + std::cos(a0) * rimRadius * 0.18f, axleY + std::sin(a0) * rimRadius * 0.18f, axleZ - beadHalfWidthZ * 0.52f };
            const Vec3 lipBack { axleX + std::cos(a1) * rimRadius * 0.92f, axleY + std::sin(a1) * rimRadius * 0.92f, axleZ - beadHalfWidthZ * 0.68f };
            AppendCylinderBetween(outSurface, axleBack, lipBack, 0.042f, 0.030f, 6u, alloySilver, alloySilver, alloyPbr, 0.0f, 0.0f);
        }
    }
}

void DeformableSurfaceBuilder::AppendBodyAttachments(
    const SimulationSpace::Deformable::DeformableBody& body,
    ProceduralRenderSurface& outSurface,
    bool includeStaticProps,
    bool includeColliderSphere)
{
    // 1. Strand Tubes (parallel-transport swept tubes; skips severed edges)
    for (const auto& tube : body.StrandTubes)
    {
        const size_t nPts = tube.PointIndices.size();
        if (nPts < 2u) continue;
        const uint32_t sides = std::max(4u, tube.RingSides);

        for (size_t k = 0u; k + 1u < nPts; ++k)
        {
            if (k < tube.EdgeOrdinals.size())
            {
                const int32_t eIdx = tube.EdgeOrdinals[k];
                if (eIdx >= 0 && static_cast<size_t>(eIdx) < body.DistanceConstraints.size() &&
                    body.DistanceConstraints[static_cast<size_t>(eIdx)].Severed)
                {
                    continue;
                }
            }
            const float t0 = static_cast<float>(k) / static_cast<float>(nPts - 1u);
            const float t1 = static_cast<float>(k + 1u) / static_cast<float>(nPts - 1u);
            const float r0 = tube.RadiusStart + (tube.RadiusEnd - tube.RadiusStart) * t0;
            const float r1 = tube.RadiusStart + (tube.RadiusEnd - tube.RadiusStart) * t1;
            const std::array<float, 3> c0 {
                tube.ColourStart[0] + (tube.ColourEnd[0] - tube.ColourStart[0]) * t0,
                tube.ColourStart[1] + (tube.ColourEnd[1] - tube.ColourStart[1]) * t0,
                tube.ColourStart[2] + (tube.ColourEnd[2] - tube.ColourStart[2]) * t0
            };
            const std::array<float, 3> c1 {
                tube.ColourStart[0] + (tube.ColourEnd[0] - tube.ColourStart[0]) * t1,
                tube.ColourStart[1] + (tube.ColourEnd[1] - tube.ColourStart[1]) * t1,
                tube.ColourStart[2] + (tube.ColourEnd[2] - tube.ColourStart[2]) * t1
            };
            const uint32_t iA = tube.PointIndices[k];
            const uint32_t iB = tube.PointIndices[k + 1u];
            AppendCylinderBetween(
                outSurface, ReadPoint(body, iA), ReadPoint(body, iB),
                r0, r1, sides, c0, c1, tube.MaterialPbr,
                body.PointStrain[iA], body.PointStrain[iB]);
        }
    }

    // 2. Interlocking Metallic Chain Links (alternating 90-degree stadium tori)
    for (const auto& chain : body.ChainStrands)
    {
        const size_t nPts = chain.PointIndices.size();
        for (size_t k = 0u; k + 1u < nPts; ++k)
        {
            if (k < chain.EdgeOrdinals.size())
            {
                const int32_t eIdx = chain.EdgeOrdinals[k];
                if (eIdx >= 0 && static_cast<size_t>(eIdx) < body.DistanceConstraints.size() &&
                    body.DistanceConstraints[static_cast<size_t>(eIdx)].Severed)
                {
                    continue;
                }
            }
            const uint32_t iA = chain.PointIndices[k];
            const uint32_t iB = chain.PointIndices[k + 1u];
            const Vec3 pA = ReadPoint(body, iA);
            const Vec3 pB = ReadPoint(body, iB);
            const Vec3 mid = Mul(Add(pA, pB), 0.5f);
            const Vec3 axis = Sub(pB, pA);
            const float len = Length(axis);
            if (len < 1.0e-5f) continue;

            const Vec3 T = Normalize(axis);
            const Vec3 ref = (std::fabs(T.z) < 0.9f) ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
            Vec3 U = Normalize(Cross(ref, T));
            Vec3 V = Cross(T, U);
            if ((k & 1u) != 0u) std::swap(U, V);

            const float majorA = len * 0.56f;
            const float majorB = chain.LinkRadius * 0.65f;
            const float wireR  = chain.LinkRadius * 0.24f;
            const float st     = 0.5f * (body.PointStrain[iA] + body.PointStrain[iB]);

            constexpr uint32_t kLoopSegs = 12u;
            Vec3 prevPt {};
            for (uint32_t m = 0u; m <= kLoopSegs; ++m)
            {
                const float th = kTwoPi * static_cast<float>(m) / static_cast<float>(kLoopSegs);
                const Vec3 pt = Add(mid, Add(Mul(T, std::cos(th) * majorA), Mul(U, std::sin(th) * majorB)));
                if (m > 0u)
                {
                    AppendCylinderBetween(outSurface, prevPt, pt, wireR, wireR, 5u, chain.Colour, chain.Colour, chain.MaterialPbr, st, st);
                }
                prevPt = pt;
            }
        }
    }

    // 3. Arched Botanical Foliage Blades (ferns, vines, grass, kelp)
    for (const auto& blade : body.FoliageBlades)
    {
        if (blade.PointIndex >= body.PointCount || blade.NextIndex >= body.PointCount) continue;
        const Vec3 rootPos = ReadPoint(body, blade.PointIndex);
        const Vec3 nextP = ReadPoint(body, blade.NextIndex);
        const Vec3 stemT = Normalize(Sub(nextP, rootPos), { 0.0f, 1.0f, 0.0f });
        const Vec3 upRef = (std::fabs(stemT.y) < 0.92f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
        const Vec3 sideA = Normalize(Cross(stemT, upRef));
        const Vec3 sideB = Normalize(Cross(sideA, stemT));

        const float ca = std::cos(blade.AngleOffset);
        const float sa = std::sin(blade.AngleOffset);
        const Vec3 dirOut = Normalize(Add(
            Mul(Add(Mul(sideA, ca), Mul(sideB, sa)), static_cast<float>(blade.LateralSide)),
            Mul(stemT, std::sin(blade.PitchRad))));
        const Vec3 bladeSide = Normalize(Cross(dirOut, sideB), sideA);
        const Vec3 bladeUp   = Normalize(Cross(bladeSide, dirOut), { 0.0f, 1.0f, 0.0f });
        const float st       = body.PointStrain[blade.PointIndex];

        constexpr uint32_t kSegs = 4u;
        const uint32_t vertexStart = outSurface.VertexCount();
        for (uint32_t s = 0u; s <= kSegs; ++s)
        {
            const float u = static_cast<float>(s) / static_cast<float>(kSegs);
            const float width = blade.Width * std::sin(u * kPi) * (1.0f - 0.15f * u);
            const float droop = -blade.ArchFactor * blade.Length * u * u;
            const Vec3 spine = Add(Add(rootPos, Mul(dirOut, blade.Length * u)), Mul(Vec3 { 0.0f, 1.0f, 0.0f }, droop));
            const std::array<float, 3> col {
                blade.ColourBase[0] + (blade.ColourTip[0] - blade.ColourBase[0]) * u,
                blade.ColourBase[1] + (blade.ColourTip[1] - blade.ColourBase[1]) * u,
                blade.ColourBase[2] + (blade.ColourTip[2] - blade.ColourBase[2]) * u
            };
            (void)PushVertex(outSurface, Sub(spine, Mul(bladeSide, width)), bladeUp, col, blade.MaterialPbr, st);
            (void)PushVertex(outSurface, Add(spine, Mul(bladeSide, width)), bladeUp, col, blade.MaterialPbr, st);
        }
        for (uint32_t s = 0u; s < kSegs; ++s)
        {
            const uint32_t a = vertexStart + s * 2u;
            PushQuad(outSurface, a, a + 1u, a + 3u, a + 2u);
        }
    }

    // 4. Point Beads (dewdrops, cattail heads, kelp pneumatocyst gas bladders)
    for (const auto& bead : body.PointBeads)
    {
        if (bead.PointIndex >= body.PointCount) continue;
        const Vec3 p = ReadPoint(body, bead.PointIndex);
        const Vec3 c { p.x + bead.OffsetMeters[0], p.y + bead.OffsetMeters[1], p.z + bead.OffsetMeters[2] };
        const Vec3 r { bead.RadiiMeters[0], bead.RadiiMeters[1], bead.RadiiMeters[2] };
        AppendEllipsoid(outSurface, c, r, 10u, 6u, bead.Colour, bead.MaterialPbr, body.PointStrain[bead.PointIndex]);
    }

    // 5. Tear-Aware Orthotropic Woven Cloth Sheets
    for (const auto& sheet : body.ClothSheets)
    {
        const uint32_t C = sheet.Columns;
        const uint32_t R = sheet.Rows;
        if (C < 2u || R < 2u) continue;

        const uint32_t vertexStart = outSurface.VertexCount();
        std::vector<Vec3> accNormals(static_cast<size_t>(C) * R, { 0.0f, 0.0f, 0.0f });

        auto edgeBroken = [&](const std::vector<int32_t>& edgeIndices, size_t idx) -> bool
        {
            if (idx >= edgeIndices.size()) return false;
            const int32_t e = edgeIndices[idx];
            return (e >= 0 && static_cast<size_t>(e) < body.DistanceConstraints.size() &&
                    body.DistanceConstraints[static_cast<size_t>(e)].Severed);
        };

        std::vector<std::array<uint32_t, 4>> activeQuads;
        activeQuads.reserve(static_cast<size_t>(C - 1u) * (R - 1u));
        for (uint32_t r = 0u; r + 1u < R; ++r)
        {
            for (uint32_t c = 0u; c + 1u < C; ++c)
            {
                const bool topCut   = edgeBroken(sheet.HorizontalEdges, static_cast<size_t>(r) * (C - 1u) + c);
                const bool botCut   = edgeBroken(sheet.HorizontalEdges, static_cast<size_t>(r + 1u) * (C - 1u) + c);
                const bool leftCut  = edgeBroken(sheet.VerticalEdges,   static_cast<size_t>(r) * C + c);
                const bool rightCut = edgeBroken(sheet.VerticalEdges,   static_cast<size_t>(r) * C + c + 1u);
                if (topCut || botCut || leftCut || rightCut) continue;

                const uint32_t q0 = r * C + c;
                const uint32_t q1 = q0 + 1u;
                const uint32_t q2 = (r + 1u) * C + c + 1u;
                const uint32_t q3 = (r + 1u) * C + c;
                activeQuads.push_back({ q0, q1, q2, q3 });

                const Vec3 p0 = ReadPoint(body, sheet.StartPointIndex + q0);
                const Vec3 p1 = ReadPoint(body, sheet.StartPointIndex + q1);
                const Vec3 p2 = ReadPoint(body, sheet.StartPointIndex + q2);
                const Vec3 p3 = ReadPoint(body, sheet.StartPointIndex + q3);
                const Vec3 n  = Cross(Sub(p1, p0), Sub(p3, p0));
                const Vec3 n2 = Cross(Sub(p3, p2), Sub(p1, p2));
                const Vec3 fn = Add(n, n2);
                accNormals[q0] = Add(accNormals[q0], fn);
                accNormals[q1] = Add(accNormals[q1], fn);
                accNormals[q2] = Add(accNormals[q2], fn);
                accNormals[q3] = Add(accNormals[q3], fn);
            }
        }

        for (uint32_t r = 0u; r < R; ++r)
        {
            for (uint32_t c = 0u; c < C; ++c)
            {
                const uint32_t localIdx = r * C + c;
                const uint32_t ptIdx = sheet.StartPointIndex + localIdx;
                const float stripe = (((r / 2u) + (c / 2u)) & 1u) ? 0.28f : 0.0f;
                const float u = static_cast<float>(c) / static_cast<float>(C - 1u);
                const float mixW = std::clamp(u * 0.65f + stripe, 0.0f, 1.0f);
                const std::array<float, 3> col {
                    sheet.ColourA[0] + (sheet.ColourB[0] - sheet.ColourA[0]) * mixW,
                    sheet.ColourA[1] + (sheet.ColourB[1] - sheet.ColourA[1]) * mixW,
                    sheet.ColourA[2] + (sheet.ColourB[2] - sheet.ColourA[2]) * mixW
                };
                (void)PushVertex(outSurface, ReadPoint(body, ptIdx), Normalize(accNormals[localIdx], { 0, 0, 1 }),
                                 col, sheet.MaterialPbr, body.PointStrain[ptIdx]);
            }
        }
        for (const auto& q : activeQuads)
        {
            PushQuad(outSurface, vertexStart + q[0], vertexStart + q[1], vertexStart + q[2], vertexStart + q[3]);
        }
    }

    // 6. Forged Alloy Rims for FEM & Pneumatic Tires
    for (const auto& femTire : body.FemTires)
    {
        AppendForgedAlloyRim(outSurface, femTire.AxleX, femTire.AxleY, femTire.AxleZ,
                             femTire.SpinAngleRad, femTire.RimRadius, femTire.BeadHalfWidthZ);
    }
    for (const auto& pneuTire : body.PneumaticTires)
    {
        AppendForgedAlloyRim(outSurface, pneuTire.AxleX, pneuTire.AxleY, pneuTire.AxleZ,
                             pneuTire.SpinAngleRad, pneuTire.RimRadius, pneuTire.BeadHalfWidthZ);
    }

    // 7. Static Hardware Props
    if (includeStaticProps)
    {
        for (const auto& prop : body.StaticProps)
        {
            AppendBox(outSurface,
                      { prop.PositionMeters[0], prop.PositionMeters[1], prop.PositionMeters[2] },
                      { prop.ScaleMeters[0] * 0.5f, prop.ScaleMeters[1] * 0.5f, prop.ScaleMeters[2] * 0.5f },
                      prop.Colour, prop.MaterialPbr);
        }
    }

    // 8. Interactive Curb / Sphere Collider Visual
    if (includeColliderSphere && body.Collider.Active && body.Collider.RadiusMeters > 0.01f)
    {
        const Vec3 c { body.Collider.CenterMeters[0], body.Collider.CenterMeters[1], body.Collider.CenterMeters[2] };
        const float r = body.Collider.RadiusMeters;
        AppendEllipsoid(outSurface, c, { r, r, r }, 18u, 12u,
                        { 0.90f, 0.44f, 0.14f }, { 0.32f, 0.0f, 0.18f }, 0.0f);
    }
}

} // namespace Frontier::GeometrySpace::Deformation
