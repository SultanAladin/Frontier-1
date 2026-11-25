/*============================================================================================================================================
                                                      TETRAHEDRALSOLIDBUILDER.CPP
============================================================================================================================================*/
// 🧩 Phase D01 Conformal 3D hexahedron-to-5-tetrahedron solid builder, Dm^-1 & rest volume V0 precomputation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/TetrahedralSolidBuilder.h"

#include <cmath>
#include <utility>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  DEFORMABLEBODY HELPERS
//------------------------------------------------------------------------------------------------------------------------

int32_t DeformableBody::AppendDistanceConstraint(
    uint32_t a,
    uint32_t b,
    float stiffness,
    float breakRatio,
    bool structural,
    float lengthScale)
{
    const float dx = Positions[b * 3u] - Positions[a * 3u];
    const float dy = Positions[b * 3u + 1u] - Positions[a * 3u + 1u];
    const float dz = Positions[b * 3u + 2u] - Positions[a * 3u + 2u];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz) * lengthScale;
    const int32_t ordinal = static_cast<int32_t>(DistanceConstraints.size());
    DistanceConstraintEntry entry {};
    entry.PointA     = a;
    entry.PointB     = b;
    entry.RestLength = (dist > 1.0e-5f) ? dist : 1.0e-5f;
    entry.Stiffness  = stiffness;
    entry.BreakRatio = breakRatio;
    entry.LiveStrain = 0.0f;
    entry.Structural = structural;
    entry.Severed    = false;
    DistanceConstraints.push_back(entry);
    DistanceDuals.push_back(0.0f);
    return ordinal;
}

void DeformableBody::AppendBendConstraint(uint32_t a, uint32_t b, float stiffness)
{
    const float dx = Positions[b * 3u] - Positions[a * 3u];
    const float dy = Positions[b * 3u + 1u] - Positions[a * 3u + 1u];
    const float dz = Positions[b * 3u + 2u] - Positions[a * 3u + 2u];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    BendConstraintEntry entry {};
    entry.PointA     = a;
    entry.PointB     = b;
    entry.RestLength = (dist > 1.0e-5f) ? dist : 1.0e-5f;
    entry.Stiffness  = stiffness;
    BendConstraints.push_back(entry);
    BendDuals.push_back(0.0f);
}

void DeformableBody::AppendPostureConstraint(uint32_t pointIndex, float stiffness)
{
    PostureConstraintEntry entry {};
    entry.PointIndex = pointIndex;
    entry.TargetX    = Positions[pointIndex * 3u];
    entry.TargetY    = Positions[pointIndex * 3u + 1u];
    entry.TargetZ    = Positions[pointIndex * 3u + 2u];
    entry.Stiffness  = stiffness;
    PostureConstraints.push_back(entry);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TETRAHEDRAL BUILDER
//------------------------------------------------------------------------------------------------------------------------

bool TetrahedralSolidBuilder::AppendTetrahedron(
    DeformableBody& body,
    uint32_t i0,
    uint32_t i1,
    uint32_t i2,
    uint32_t i3)
{
    const float* p = body.RestPositions.data();
    const float x0 = p[i0 * 3u], y0 = p[i0 * 3u + 1u], z0 = p[i0 * 3u + 2u];
    const float ax = p[i1 * 3u] - x0, ay = p[i1 * 3u + 1u] - y0, az = p[i1 * 3u + 2u] - z0;
    const float bx = p[i2 * 3u] - x0, by = p[i2 * 3u + 1u] - y0, bz = p[i2 * 3u + 2u] - z0;
    const float cx = p[i3 * 3u] - x0, cy = p[i3 * 3u + 1u] - y0, cz = p[i3 * 3u + 2u] - z0;

    const float det = ax * (by * cz - bz * cy)
                    - bx * (ay * cz - az * cy)
                    + cx * (ay * bz - az * by);
    if (std::fabs(det) < 1.0e-10f)
    {
        return false;
    }

    if (det < 0.0f)
    {
        std::swap(i2, i3);
    }

    body.TetIndices.push_back(i0);
    body.TetIndices.push_back(i1);
    body.TetIndices.push_back(i2);
    body.TetIndices.push_back(i3);
    body.TetCount = static_cast<uint32_t>(body.TetIndices.size() / 4u);
    return true;
}

uint32_t TetrahedralSolidBuilder::AppendHexCellAsFiveTets(
    DeformableBody& body,
    const std::array<uint32_t, 8>& hexCorners,
    uint32_t parityBit)
{
    // Corner ordering: [v000, v100, v110, v010, v001, v101, v111, v011]
    const uint32_t v000 = hexCorners[0];
    const uint32_t v100 = hexCorners[1];
    const uint32_t v110 = hexCorners[2];
    const uint32_t v010 = hexCorners[3];
    const uint32_t v001 = hexCorners[4];
    const uint32_t v101 = hexCorners[5];
    const uint32_t v111 = hexCorners[6];
    const uint32_t v011 = hexCorners[7];

    uint32_t added = 0u;
    if ((parityBit & 1u) == 0u)
    {
        added += AppendTetrahedron(body, v000, v100, v110, v101) ? 1u : 0u;
        added += AppendTetrahedron(body, v000, v110, v010, v011) ? 1u : 0u;
        added += AppendTetrahedron(body, v000, v001, v101, v011) ? 1u : 0u;
        added += AppendTetrahedron(body, v111, v101, v011, v110) ? 1u : 0u;
        added += AppendTetrahedron(body, v000, v110, v101, v011) ? 1u : 0u;
    }
    else
    {
        added += AppendTetrahedron(body, v100, v110, v010, v111) ? 1u : 0u;
        added += AppendTetrahedron(body, v100, v010, v000, v001) ? 1u : 0u;
        added += AppendTetrahedron(body, v100, v101, v111, v001) ? 1u : 0u;
        added += AppendTetrahedron(body, v011, v111, v001, v010) ? 1u : 0u;
        added += AppendTetrahedron(body, v100, v010, v111, v001) ? 1u : 0u;
    }
    return added;
}

uint32_t TetrahedralSolidBuilder::PrecomputeRestMatrices(DeformableBody& body)
{
    const uint32_t tetCount = static_cast<uint32_t>(body.TetIndices.size() / 4u);
    body.TetCount = tetCount;
    body.TetInverseRestDm.assign(static_cast<size_t>(tetCount) * 9u, 0.0f);
    body.TetCofactorNormals.assign(static_cast<size_t>(tetCount) * 9u, 0.0f);
    body.TetRestVolume.assign(tetCount, 0.0f);
    body.TetStrain.assign(tetCount, 0.0f);

    uint32_t degenerateCount = 0u;
    const float* p = body.RestPositions.data();

    for (uint32_t t = 0u; t < tetCount; ++t)
    {
        const size_t o = static_cast<size_t>(t) * 9u;
        body.TetCofactorNormals[o + 0u] = 1.0f;
        body.TetCofactorNormals[o + 4u] = 1.0f;
        body.TetCofactorNormals[o + 8u] = 1.0f;

        const uint32_t i0 = body.TetIndices[t * 4u + 0u];
        const uint32_t i1 = body.TetIndices[t * 4u + 1u];
        const uint32_t i2 = body.TetIndices[t * 4u + 2u];
        const uint32_t i3 = body.TetIndices[t * 4u + 3u];

        const float x0 = p[i0 * 3u], y0 = p[i0 * 3u + 1u], z0 = p[i0 * 3u + 2u];
        // Columns of Dm: c0 = p1 - p0, c1 = p2 - p0, c2 = p3 - p0
        const float a00 = p[i1 * 3u] - x0, a10 = p[i1 * 3u + 1u] - y0, a20 = p[i1 * 3u + 2u] - z0;
        const float a01 = p[i2 * 3u] - x0, a11 = p[i2 * 3u + 1u] - y0, a21 = p[i2 * 3u + 2u] - z0;
        const float a02 = p[i3 * 3u] - x0, a12 = p[i3 * 3u + 1u] - y0, a22 = p[i3 * 3u + 2u] - z0;

        const float det = a00 * (a11 * a22 - a12 * a21)
                        - a01 * (a10 * a22 - a12 * a20)
                        + a02 * (a10 * a21 - a11 * a20);
        if (std::fabs(det) < 1.0e-10f)
        {
            ++degenerateCount;
            continue;
        }

        body.TetRestVolume[t] = std::fabs(det) / 6.0f;
        const float invD = 1.0f / det;

        // Row-major entries of Q = Dm^-1: q[row * 3 + col]
        body.TetInverseRestDm[o + 0u] = (a11 * a22 - a12 * a21) * invD;
        body.TetInverseRestDm[o + 1u] = (a02 * a21 - a01 * a22) * invD;
        body.TetInverseRestDm[o + 2u] = (a01 * a12 - a02 * a11) * invD;
        body.TetInverseRestDm[o + 3u] = (a12 * a20 - a10 * a22) * invD;
        body.TetInverseRestDm[o + 4u] = (a00 * a22 - a02 * a20) * invD;
        body.TetInverseRestDm[o + 5u] = (a02 * a10 - a00 * a12) * invD;
        body.TetInverseRestDm[o + 6u] = (a10 * a21 - a11 * a20) * invD;
        body.TetInverseRestDm[o + 7u] = (a01 * a20 - a00 * a21) * invD;
        body.TetInverseRestDm[o + 8u] = (a00 * a11 - a01 * a10) * invD;
    }

    return degenerateCount;
}

} // namespace Frontier::SimulationSpace::Deformable
