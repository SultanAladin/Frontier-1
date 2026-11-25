/*============================================================================================================================================
                                                       NEOHOOKEANTETSOLVER.CPP
============================================================================================================================================*/
// 🧩 Phase D02 Macklin & Müller (2021) Stable Neo-Hookean 3D Volumetric FEM Tetrahedron Solver + Nanson Cofactor Normal Matrix

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/NeoHookeanTetSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   NEO-HOOKEAN TET SOLVER
//------------------------------------------------------------------------------------------------------------------------

void NeoHookeanTetSolver::ProjectSingleTetrahedron(
    DeformableBody& body,
    uint32_t t,
    float invH2,
    float mu,
    float lambda,
    float gamma)
{
    const uint32_t* idx = body.TetIndices.data();
    const size_t t4 = static_cast<size_t>(t) * 4u;
    const uint32_t i0 = idx[t4 + 0u];
    const uint32_t i1 = idx[t4 + 1u];
    const uint32_t i2 = idx[t4 + 2u];
    const uint32_t i3 = idx[t4 + 3u];

    const int32_t grab = body.GrabbedPointIndex;
    const float w0 = (static_cast<int32_t>(i0) == grab) ? 0.0f : body.InverseMass[i0];
    const float w1 = (static_cast<int32_t>(i1) == grab) ? 0.0f : body.InverseMass[i1];
    const float w2 = (static_cast<int32_t>(i2) == grab) ? 0.0f : body.InverseMass[i2];
    const float w3 = (static_cast<int32_t>(i3) == grab) ? 0.0f : body.InverseMass[i3];
    if (w0 + w1 + w2 + w3 <= 0.0f)
    {
        return;
    }

    const float vol = body.TetRestVolume[t];
    if (vol <= 1.0e-12f)
    {
        return;
    }

    float* pos = body.Positions.data();
    const float* Q = body.TetInverseRestDm.data();
    const size_t p0 = static_cast<size_t>(i0) * 3u;
    const size_t p1 = static_cast<size_t>(i1) * 3u;
    const size_t p2 = static_cast<size_t>(i2) * 3u;
    const size_t p3 = static_cast<size_t>(i3) * 3u;

    const float x0 = pos[p0], y0 = pos[p0 + 1u], z0 = pos[p0 + 2u];
    const float d00 = pos[p1] - x0, d10 = pos[p1 + 1u] - y0, d20 = pos[p1 + 2u] - z0;
    const float d01 = pos[p2] - x0, d11 = pos[p2 + 1u] - y0, d21 = pos[p2 + 2u] - z0;
    const float d02 = pos[p3] - x0, d12 = pos[p3 + 1u] - y0, d22 = pos[p3 + 2u] - z0;

    const size_t o = static_cast<size_t>(t) * 9u;
    const float q00 = Q[o + 0u], q01 = Q[o + 1u], q02 = Q[o + 2u];
    const float q10 = Q[o + 3u], q11 = Q[o + 4u], q12 = Q[o + 5u];
    const float q20 = Q[o + 6u], q21 = Q[o + 7u], q22 = Q[o + 8u];

    // F = Ds * Q (columns f0, f1, f2)
    const float f00 = d00 * q00 + d01 * q10 + d02 * q20;
    const float f10 = d10 * q00 + d11 * q10 + d12 * q20;
    const float f20 = d20 * q00 + d21 * q10 + d22 * q20;

    const float f01 = d00 * q01 + d01 * q11 + d02 * q21;
    const float f11 = d10 * q01 + d11 * q11 + d12 * q21;
    const float f21 = d20 * q01 + d21 * q11 + d22 * q21;

    const float f02 = d00 * q02 + d01 * q12 + d02 * q22;
    const float f12 = d10 * q02 + d11 * q12 + d12 * q22;
    const float f22 = d20 * q02 + d21 * q12 + d22 * q22;

    // --- (A) Deviatoric Shear Elasticity: C_D = sqrt(tr(F^T F)) ---
    const float ic = f00 * f00 + f10 * f10 + f20 * f20
                   + f01 * f01 + f11 * f11 + f21 * f21
                   + f02 * f02 + f12 * f12 + f22 * f22;
    const float cD = std::sqrt(std::max(1.0e-10f, ic));
    const float invCD = 1.0f / cD;

    const float g1x = (f00 * q00 + f01 * q01 + f02 * q02) * invCD;
    const float g1y = (f10 * q00 + f11 * q01 + f12 * q02) * invCD;
    const float g1z = (f20 * q00 + f21 * q01 + f22 * q02) * invCD;

    const float g2x = (f00 * q10 + f01 * q11 + f02 * q12) * invCD;
    const float g2y = (f10 * q10 + f11 * q11 + f12 * q12) * invCD;
    const float g2z = (f20 * q10 + f21 * q11 + f22 * q12) * invCD;

    const float g3x = (f00 * q20 + f01 * q21 + f02 * q22) * invCD;
    const float g3y = (f10 * q20 + f11 * q21 + f12 * q22) * invCD;
    const float g3z = (f20 * q20 + f21 * q21 + f22 * q22) * invCD;

    const float g0x = -(g1x + g2x + g3x);
    const float g0y = -(g1y + g2y + g3y);
    const float g0z = -(g1z + g2z + g3z);

    const float denomD = w0 * (g0x * g0x + g0y * g0y + g0z * g0z)
                       + w1 * (g1x * g1x + g1y * g1y + g1z * g1z)
                       + w2 * (g2x * g2x + g2y * g2y + g2z * g2z)
                       + w3 * (g3x * g3x + g3y * g3y + g3z * g3z)
                       + invH2 / (mu * vol);

    if (denomD > 1.0e-12f)
    {
        const float dlD = -cD / denomD;
        if (w0 > 0.0f) { pos[p0] += dlD * w0 * g0x; pos[p0 + 1u] += dlD * w0 * g0y; pos[p0 + 2u] += dlD * w0 * g0z; }
        if (w1 > 0.0f) { pos[p1] += dlD * w1 * g1x; pos[p1 + 1u] += dlD * w1 * g1y; pos[p1 + 2u] += dlD * w1 * g1z; }
        if (w2 > 0.0f) { pos[p2] += dlD * w2 * g2x; pos[p2 + 1u] += dlD * w2 * g2y; pos[p2 + 2u] += dlD * w2 * g2z; }
        if (w3 > 0.0f) { pos[p3] += dlD * w3 * g3x; pos[p3 + 1u] += dlD * w3 * g3y; pos[p3 + 2u] += dlD * w3 * g3z; }
    }

    // --- (B) Hydrostatic Volume Preservation: C_H = det(F) - (1 + mu/lambda) ---
    const float ux0 = pos[p0], uy0 = pos[p0 + 1u], uz0 = pos[p0 + 2u];
    const float e00 = pos[p1] - ux0, e10 = pos[p1 + 1u] - uy0, e20 = pos[p1 + 2u] - uz0;
    const float e01 = pos[p2] - ux0, e11 = pos[p2 + 1u] - uy0, e21 = pos[p2 + 2u] - uz0;
    const float e02 = pos[p3] - ux0, e12 = pos[p3 + 1u] - uy0, e22 = pos[p3 + 2u] - uz0;

    const float h00 = e00 * q00 + e01 * q10 + e02 * q20;
    const float h10 = e10 * q00 + e11 * q10 + e12 * q20;
    const float h20 = e20 * q00 + e21 * q10 + e22 * q20;

    const float h01 = e00 * q01 + e01 * q11 + e02 * q21;
    const float h11 = e10 * q01 + e11 * q11 + e12 * q21;
    const float h21 = e20 * q01 + e21 * q11 + e22 * q21;

    const float h02 = e00 * q02 + e01 * q12 + e02 * q22;
    const float h12 = e10 * q02 + e11 * q12 + e12 * q22;
    const float h22 = e20 * q02 + e21 * q12 + e22 * q22;

    // Cofactor columns of F: cf0 = f1 x f2, cf1 = f2 x f0, cf2 = f0 x f1 (Nanson normal matrix!)
    const float c00 = h11 * h22 - h21 * h12, c10 = h21 * h02 - h01 * h22, c20 = h01 * h12 - h11 * h02;
    const float c01 = h12 * h20 - h22 * h10, c11 = h22 * h00 - h02 * h20, c21 = h02 * h10 - h12 * h00;
    const float c02 = h10 * h21 - h20 * h11, c12 = h20 * h01 - h00 * h21, c22 = h00 * h11 - h10 * h01;

    float* tc = body.TetCofactorNormals.data();
    tc[o + 0u] = c00; tc[o + 1u] = c10; tc[o + 2u] = c20;
    tc[o + 3u] = c01; tc[o + 4u] = c11; tc[o + 5u] = c21;
    tc[o + 6u] = c02; tc[o + 7u] = c12; tc[o + 8u] = c22;

    const float J  = h00 * c00 + h10 * c10 + h20 * c20;
    const float cH = J - gamma;

    // Live 3D Von Mises + volumetric strain magnitude
    body.TetStrain[t] = std::fabs(cD - 1.7320508f) + std::fabs(J - 1.0f) * 2.5f;

    // Gradients of det(F): J_H = cof(F) * Q^T
    const float j1x = c00 * q00 + c01 * q01 + c02 * q02;
    const float j1y = c10 * q00 + c11 * q01 + c12 * q02;
    const float j1z = c20 * q00 + c21 * q01 + c22 * q02;

    const float j2x = c00 * q10 + c01 * q11 + c02 * q12;
    const float j2y = c10 * q10 + c11 * q11 + c12 * q12;
    const float j2z = c20 * q10 + c21 * q11 + c22 * q12;

    const float j3x = c00 * q20 + c01 * q21 + c02 * q22;
    const float j3y = c10 * q20 + c11 * q21 + c12 * q22;
    const float j3z = c20 * q20 + c21 * q21 + c22 * q22;

    const float j0x = -(j1x + j2x + j3x);
    const float j0y = -(j1y + j2y + j3y);
    const float j0z = -(j1z + j2z + j3z);

    const float denomH = w0 * (j0x * j0x + j0y * j0y + j0z * j0z)
                       + w1 * (j1x * j1x + j1y * j1y + j1z * j1z)
                       + w2 * (j2x * j2x + j2y * j2y + j2z * j2z)
                       + w3 * (j3x * j3x + j3y * j3y + j3z * j3z)
                       + invH2 / (lambda * vol);

    if (denomH > 1.0e-12f)
    {
        const float dlH = -cH / denomH;
        if (w0 > 0.0f) { pos[p0] += dlH * w0 * j0x; pos[p0 + 1u] += dlH * w0 * j0y; pos[p0 + 2u] += dlH * w0 * j0z; }
        if (w1 > 0.0f) { pos[p1] += dlH * w1 * j1x; pos[p1 + 1u] += dlH * w1 * j1y; pos[p1 + 2u] += dlH * w1 * j1z; }
        if (w2 > 0.0f) { pos[p2] += dlH * w2 * j2x; pos[p2 + 1u] += dlH * w2 * j2y; pos[p2 + 2u] += dlH * w2 * j2z; }
        if (w3 > 0.0f) { pos[p3] += dlH * w3 * j3x; pos[p3 + 1u] += dlH * w3 * j3y; pos[p3 + 2u] += dlH * w3 * j3z; }
    }
}

void NeoHookeanTetSolver::ProjectAllTetrahedra(
    DeformableBody& body,
    float invH2,
    const DeformableSimulationConfiguration& configuration,
    bool useConflictBatches)
{
    if (body.TetCount == 0u)
    {
        return;
    }

    const float E      = std::max(0.15f, configuration.FemYoungMpa) * 2800.0f;
    const float nu     = std::clamp(configuration.FemPoissonNu, 0.10f, 0.485f);
    const float mu     = E / (2.0f * (1.0f + nu));
    const float lambda = (E * nu) / ((1.0f + nu) * (1.0f - 2.0f * nu));
    const float gamma  = 1.0f + mu / lambda;

    if (useConflictBatches && body.TetBatches.ColorCount > 0u && body.TetBatches.OrderedOrdinals.size() == body.TetCount)
    {
        for (uint32_t c = 0u; c < body.TetBatches.ColorCount; ++c)
        {
            const uint32_t begin = body.TetBatches.BatchOffsets[c];
            const uint32_t end   = body.TetBatches.BatchOffsets[c + 1u];
            for (uint32_t k = begin; k < end; ++k)
            {
                ProjectSingleTetrahedron(body, body.TetBatches.OrderedOrdinals[k], invH2, mu, lambda, gamma);
            }
        }
    }
    else
    {
        for (uint32_t t = 0u; t < body.TetCount; ++t)
        {
            ProjectSingleTetrahedron(body, t, invH2, mu, lambda, gamma);
        }
    }
}

void NeoHookeanTetSolver::ScatterPointStrain(DeformableBody& body)
{
    if (body.TetCount == 0u)
    {
        return;
    }
    std::fill(body.PointStrain.begin(), body.PointStrain.end(), 0.0f);
    const uint32_t* idx = body.TetIndices.data();
    const float* ts = body.TetStrain.data();
    float* ps = body.PointStrain.data();
    for (uint32_t t = 0u; t < body.TetCount; ++t)
    {
        const float quarter = ts[t] * 0.25f;
        const size_t t4 = static_cast<size_t>(t) * 4u;
        ps[idx[t4 + 0u]] += quarter;
        ps[idx[t4 + 1u]] += quarter;
        ps[idx[t4 + 2u]] += quarter;
        ps[idx[t4 + 3u]] += quarter;
    }
}

} // namespace Frontier::SimulationSpace::Deformable
