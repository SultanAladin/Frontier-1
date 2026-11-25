/*============================================================================================================================================
                                                          FLUIDGPUSOLVER.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slices S15-S16) 1:1 3D Eulerian + Sparse Infinite GPU fluid solver uniform ABI & bit-accurate CPU reference mirrors
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L532-L1076 & FluidSolver.js L410-L810

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

[[nodiscard]] float ClampF(const float x, const float lo, const float hi) noexcept { return std::clamp(x, lo, hi); }
[[nodiscard]] float FractF(const float x) noexcept { return x - std::floor(x); }
[[nodiscard]] float LerpF(const float a, const float b, const float t) noexcept { return a + (b - a) * t; }
[[nodiscard]] float StepF(const float edge, const float x) noexcept { return (x < edge) ? 0.0f : 1.0f; }
[[nodiscard]] float SmoothStepF(const float e0, const float e1, const float x) noexcept
{
    const float denom = e1 - e0;
    const float t = (std::fabs(denom) > 1.0e-9f) ? ClampF((x - e0) / denom, 0.0f, 1.0f) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] std::array<float, 4> Lerp4(
    const std::array<float, 4>& a,
    const std::array<float, 4>& b,
    const float t) noexcept
{
    return {
        LerpF(a[0], b[0], t),
        LerpF(a[1], b[1], t),
        LerpF(a[2], b[2], t),
        LerpF(a[3], b[3], t)
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              CPU 3D VOLUME BUFFER
//------------------------------------------------------------------------------------------------------------------------

void FluidVolumeGridCpu::Resize(const uint32_t w, const uint32_t h, const uint32_t d)
{
    Width  = w;
    Height = h;
    Depth  = d;
    Cells.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(d), { 0.0f, 0.0f, 0.0f, 0.0f });
}

void FluidVolumeGridCpu::Clear() noexcept
{
    std::fill(Cells.begin(), Cells.end(), std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
}

size_t FluidVolumeGridCpu::LinearIndex(const int32_t x, const int32_t y, const int32_t z) const noexcept
{
    return (static_cast<size_t>(z) * static_cast<size_t>(Height) + static_cast<size_t>(y)) * static_cast<size_t>(Width)
         + static_cast<size_t>(x);
}

std::array<float, 4> FluidVolumeGridCpu::FetchClamped(const int32_t x, const int32_t y, const int32_t z) const noexcept
{
    if (Cells.empty() || Width == 0u || Height == 0u || Depth == 0u) return { 0.0f, 0.0f, 0.0f, 0.0f };
    const int32_t cx = std::clamp(x, 0, static_cast<int32_t>(Width)  - 1);
    const int32_t cy = std::clamp(y, 0, static_cast<int32_t>(Height) - 1);
    const int32_t cz = std::clamp(z, 0, static_cast<int32_t>(Depth)  - 1);
    return Cells[LinearIndex(cx, cy, cz)];
}

std::array<float, 4> FluidVolumeGridCpu::SampleTrilinear(const float cx, const float cy, const float cz) const noexcept
{
    if (Cells.empty() || Width == 0u || Height == 0u || Depth == 0u) return { 0.0f, 0.0f, 0.0f, 0.0f };
    const float px = ClampF(cx - 0.5f, 0.0f, static_cast<float>(Width)  - 1.0f);
    const float py = ClampF(cy - 0.5f, 0.0f, static_cast<float>(Height) - 1.0f);
    const float pz = ClampF(cz - 0.5f, 0.0f, static_cast<float>(Depth)  - 1.0f);

    const int32_t x0 = static_cast<int32_t>(std::floor(px));
    const int32_t y0 = static_cast<int32_t>(std::floor(py));
    const int32_t z0 = static_cast<int32_t>(std::floor(pz));
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);
    const float fz = pz - static_cast<float>(z0);

    const auto x00 = Lerp4(FetchClamped(x0, y0,     z0),     FetchClamped(x0 + 1, y0,     z0),     fx);
    const auto x10 = Lerp4(FetchClamped(x0, y0 + 1, z0),     FetchClamped(x0 + 1, y0 + 1, z0),     fx);
    const auto x01 = Lerp4(FetchClamped(x0, y0,     z0 + 1), FetchClamped(x0 + 1, y0,     z0 + 1), fx);
    const auto x11 = Lerp4(FetchClamped(x0, y0 + 1, z0 + 1), FetchClamped(x0 + 1, y0 + 1, z0 + 1), fx);
    return Lerp4(Lerp4(x00, x10, fy), Lerp4(x01, x11, fy), fz);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        SLICE S15 UNIFORM BUILDERS & KERNELS
//------------------------------------------------------------------------------------------------------------------------

FluidInjectUniformBlock BuildFluidInjectUniforms(
    const FluidVolumeUniformBlock& volume,
    const uint32_t mode,
    const std::span<const FluidReleaseSourceParameters> sources,
    const float deltaTimeSeconds,
    const float simulationTimeSeconds) noexcept
{
    FluidInjectUniformBlock u {};
    u.Volume = volume;
    uint32_t packed = 0u;
    for (const FluidReleaseSourceParameters& s : sources)
    {
        if (!s.Enabled || packed >= kMaximumReleaseSources) continue;
        if (s.EnvelopeWeight <= 0.0f) continue;
        u.SourcePosRadius[packed]     = { s.PositionMeters[0], s.PositionMeters[1], s.PositionMeters[2], s.RadiusMeters };
        u.SourceVelDensity[packed]    = { s.VelocityMps[0], s.VelocityMps[1], s.VelocityMps[2], s.Density * s.EnvelopeWeight };
        u.SourceTempFuelShape[packed] = { s.Temperature * s.EnvelopeWeight, s.Fuel * s.EnvelopeWeight, s.Jitter, static_cast<float>(s.Shape) };
        u.SourceOrientPulse[packed]   = { s.Orientation[0], s.Orientation[1], s.Orientation[2], s.PulseSeconds };
        ++packed;
    }
    u.InjectTiming = {
        static_cast<float>(mode),
        static_cast<float>(packed),
        deltaTimeSeconds,
        simulationTimeSeconds
    };
    return u;
}

float EvaluateReleaseSourceMaskCpu(
    const FluidInjectUniformBlock& uniforms,
    const int32_t sourceIndex,
    const std::array<float, 3>& worldPos) noexcept
{
    if (sourceIndex < 0 || sourceIndex >= static_cast<int32_t>(kMaximumReleaseSources)) return 0.0f;
    const auto& a = uniforms.SourcePosRadius[static_cast<size_t>(sourceIndex)];
    const auto& c = uniforms.SourceTempFuelShape[static_cast<size_t>(sourceIndex)];
    const auto& d = uniforms.SourceOrientPulse[static_cast<size_t>(sourceIndex)];

    const float cellSize = std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);
    const float rx = worldPos[0] - a[0];
    const float ry = worldPos[1] - a[1];
    const float rz = worldPos[2] - a[2];
    const float radius = std::max(a[3], cellSize);
    const int32_t shape = static_cast<int32_t>(c[3] + 0.5f);
    const float edge = cellSize * 1.5f;
    float mask = 0.0f;

    if (shape == 0)
    {
        const float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
        mask = 1.0f - SmoothStepF(radius - edge, radius, dist);
    }
    else if (shape == 1)
    {
        const float nLen = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float nx = (nLen > 1.0e-5f) ? (d[0] / nLen) : 0.0f;
        const float ny = (nLen > 1.0e-5f) ? (d[1] / nLen) : 1.0f;
        const float nz = (nLen > 1.0e-5f) ? (d[2] / nLen) : 0.0f;
        const float proj = rx * nx + ry * ny + rz * nz;
        const float along = std::fabs(proj);
        const float px = rx - nx * proj, py = ry - ny * proj, pz = rz - nz * proj;
        const float radial = std::sqrt(px * px + py * py + pz * pz);
        mask = (1.0f - SmoothStepF(radius - edge, radius, radial))
             * (1.0f - SmoothStepF(edge * 0.6f, edge * 1.4f, along));
    }
    else
    {
        const float qx = std::fabs(rx) - radius;
        const float qy = std::fabs(ry) - radius;
        const float qz = std::fabs(rz) - radius;
        const float boxDist = std::max({ qx, qy, qz });
        mask = 1.0f - SmoothStepF(-edge, 0.0f, boxDist);
    }

    const float simTime = uniforms.InjectTiming[3];
    const float jitter  = c[2];
    if (jitter > 0.0f)
    {
        const float scale = 2.2f / radius;
        const float n = Frontier::SimulationSpace::ForceFields::ValueNoise({
            worldPos[0] * scale,
            worldPos[1] * scale + simTime * 1.6f,
            worldPos[2] * scale + simTime * 0.9f
        });
        mask *= LerpF(1.0f, SmoothStepF(0.25f, 0.8f, n) * 1.4f, ClampF(jitter, 0.0f, 1.0f));
    }

    const float pulse = d[3];
    if (pulse > 0.0f)
    {
        const float phase = FractF(simTime / pulse);
        mask *= std::exp(-phase * 7.0f) * StepF(phase, 0.45f);
    }

    return ClampF(mask, 0.0f, 1.0f);
}

void RunFluidAdvectCpu(
    const FluidAdvectUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& source,
    FluidVolumeGridCpu& output)
{
    const uint32_t W = source.Width, H = source.Height, D = source.Depth;
    if (output.Width != W || output.Height != H || output.Depth != D) output.Resize(W, H, D);

    const float dt          = uniforms.AdvectParams[0];
    const float dissipation = uniforms.AdvectParams[1];
    const float direction   = uniforms.AdvectParams[2];
    const float invH        = 1.0f / std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                const float cz = static_cast<float>(z) + 0.5f;
                const auto vel = velocity.FetchClamped(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z));
                const float mx = cx - direction * 0.5f * dt * vel[0] * invH;
                const float my = cy - direction * 0.5f * dt * vel[1] * invH;
                const float mz = cz - direction * 0.5f * dt * vel[2] * invH;
                const auto midVel = velocity.SampleTrilinear(mx, my, mz);
                const float ox = cx - direction * dt * midVel[0] * invH;
                const float oy = cy - direction * dt * midVel[1] * invH;
                const float oz = cz - direction * dt * midVel[2] * invH;
                const auto sampled = source.SampleTrilinear(ox, oy, oz);
                output.Cells[output.LinearIndex(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z))] = {
                    sampled[0] * dissipation,
                    sampled[1] * dissipation,
                    sampled[2] * dissipation,
                    sampled[3] * dissipation
                };
            }
        }
    }
}

void RunFluidMacCormackCpu(
    const FluidMacCormackUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& source,
    const FluidVolumeGridCpu& forward,
    const FluidVolumeGridCpu& backward,
    FluidVolumeGridCpu& output)
{
    const uint32_t W = source.Width, H = source.Height, D = source.Depth;
    if (output.Width != W || output.Height != H || output.Depth != D) output.Resize(W, H, D);

    const float dt          = uniforms.MacCormackParams[0];
    const float dissipation = uniforms.MacCormackParams[1];
    const float invH        = 1.0f / std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);
    const float maxX        = static_cast<float>(W) - 1.0f;
    const float maxY        = static_cast<float>(H) - 1.0f;
    const float maxZ        = static_cast<float>(D) - 1.0f;

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                const float cz = static_cast<float>(z) + 0.5f;

                const auto vel = velocity.FetchClamped(ix, iy, iz);
                const auto midVel = velocity.SampleTrilinear(
                    cx - 0.5f * dt * vel[0] * invH,
                    cy - 0.5f * dt * vel[1] * invH,
                    cz - 0.5f * dt * vel[2] * invH);
                const float ox = cx - dt * midVel[0] * invH;
                const float oy = cy - dt * midVel[1] * invH;
                const float oz = cz - dt * midVel[2] * invH;

                const auto fwd = forward.FetchClamped(ix, iy, iz);
                const auto bwd = backward.FetchClamped(ix, iy, iz);
                const auto src = source.FetchClamped(ix, iy, iz);
                std::array<float, 4> corrected {};
                for (size_t c = 0u; c < 4u; ++c)
                {
                    corrected[c] = fwd[c] + 0.5f * (src[c] * dissipation - bwd[c]);
                }

                const int32_t bx = static_cast<int32_t>(std::floor(ClampF(ox - 0.5f, 0.0f, maxX)));
                const int32_t by = static_cast<int32_t>(std::floor(ClampF(oy - 0.5f, 0.0f, maxY)));
                const int32_t bz = static_cast<int32_t>(std::floor(ClampF(oz - 0.5f, 0.0f, maxZ)));
                std::array<float, 4> lo {  1.0e9f,  1.0e9f,  1.0e9f,  1.0e9f };
                std::array<float, 4> hi { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
                for (int32_t dz = 0; dz < 2; ++dz)
                {
                    for (int32_t dy = 0; dy < 2; ++dy)
                    {
                        for (int32_t dx = 0; dx < 2; ++dx)
                        {
                            const auto s = source.FetchClamped(bx + dx, by + dy, bz + dz);
                            for (size_t c = 0u; c < 4u; ++c)
                            {
                                const float sc = s[c] * dissipation;
                                lo[c] = std::min(lo[c], sc);
                                hi[c] = std::max(hi[c], sc);
                            }
                        }
                    }
                }

                bool overshoot = false;
                for (size_t c = 0u; c < 4u; ++c)
                {
                    if (corrected[c] < lo[c] || corrected[c] > hi[c]) { overshoot = true; break; }
                }

                const float speed   = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
                const float courant = speed * dt * invH;
                const float blendW  = 1.0f - SmoothStepF(1.0f, 2.0f, courant);
                std::array<float, 4> result {};
                for (size_t c = 0u; c < 4u; ++c)
                {
                    const float val = overshoot ? fwd[c] : LerpF(fwd[c], corrected[c], blendW);
                    result[c] = ClampF(val, lo[c], hi[c]);
                }
                output.Cells[output.LinearIndex(ix, iy, iz)] = result;
            }
        }
    }
}

void AdvectFluidFieldMacCormackCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& source,
    const float deltaTimeSeconds,
    const float dissipation,
    FluidVolumeGridCpu& scratchForward,
    FluidVolumeGridCpu& scratchBackward,
    FluidVolumeGridCpu& output)
{
    FluidAdvectUniformBlock fwdU { volume, { deltaTimeSeconds, dissipation,  1.0f, 0.0f } };
    RunFluidAdvectCpu(fwdU, velocity, source, scratchForward);

    FluidAdvectUniformBlock bwdU { volume, { deltaTimeSeconds, 1.0f, -1.0f, 0.0f } };
    RunFluidAdvectCpu(bwdU, velocity, scratchForward, scratchBackward);

    FluidMacCormackUniformBlock mcU { volume, { deltaTimeSeconds, dissipation, 0.0f, 0.0f } };
    RunFluidMacCormackCpu(mcU, velocity, source, scratchForward, scratchBackward, output);
}

void RunFluidInjectCpu(
    const FluidInjectUniformBlock& uniforms,
    const FluidVolumeGridCpu& target,
    FluidVolumeGridCpu& output)
{
    const uint32_t W = target.Width, H = target.Height, D = target.Depth;
    if (output.Width != W || output.Height != H || output.Depth != D) output.Resize(W, H, D);

    const int32_t mode  = static_cast<int32_t>(uniforms.InjectTiming[0] + 0.5f);
    const int32_t count = std::min(static_cast<int32_t>(uniforms.InjectTiming[1] + 0.5f), static_cast<int32_t>(kMaximumReleaseSources));
    const float dt      = uniforms.InjectTiming[2];
    const float blend   = std::min(1.0f, dt * 10.0f);

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const auto world = WorldFromFluidCell(uniforms.Volume, {
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f
                });
                auto value = target.FetchClamped(ix, iy, iz);

                for (int32_t i = 0; i < count; ++i)
                {
                    const float mask = EvaluateReleaseSourceMaskCpu(uniforms, i, world);
                    if (mask <= 0.0f) continue;
                    const auto& b = uniforms.SourceVelDensity[static_cast<size_t>(i)];
                    const auto& c = uniforms.SourceTempFuelShape[static_cast<size_t>(i)];
                    if (mode == 0)
                    {
                        if (b[0] * b[0] + b[1] * b[1] + b[2] * b[2] > 0.0f)
                        {
                            const float w = mask * blend;
                            value[0] = LerpF(value[0], b[0], w);
                            value[1] = LerpF(value[1], b[1], w);
                            value[2] = LerpF(value[2], b[2], w);
                        }
                    }
                    else
                    {
                        value[0] = std::min(value[0] + mask * b[3] * dt * 6.0f, std::max(value[0], b[3] * 1.5f));
                        value[1] = LerpF(value[1], std::max(value[1], c[0]), mask * blend);
                        value[2] = std::min(value[2] + mask * c[1] * dt * 6.0f, std::max(value[2], c[1] * 1.5f));
                    }
                }
                output.Cells[output.LinearIndex(ix, iy, iz)] = value;
            }
        }
    }
}

void RunFluidForcesCpu(
    const FluidForcesUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& state,
    const std::span<const Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    FluidVolumeGridCpu& output)
{
    const uint32_t W = velocity.Width, H = velocity.Height, D = velocity.Depth;
    if (output.Width != W || output.Height != H || output.Depth != D) output.Resize(W, H, D);

    const float dt            = uniforms.ForceKinematics[0];
    const float buoyancy      = uniforms.ForceKinematics[1];
    const float gravity       = uniforms.ForceKinematics[2];
    const float ambientTemp   = uniforms.ForceKinematics[3];
    const float forceResponse = uniforms.ForceControl[0];
    const float simTime       = uniforms.ForceControl[1];

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const auto world = WorldFromFluidCell(uniforms.Volume, {
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f
                });
                if (IsSolidAtFluidWorld(uniforms.Volume, world))
                {
                    output.Cells[output.LinearIndex(ix, iy, iz)] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    continue;
                }

                const auto vel = velocity.FetchClamped(ix, iy, iz);
                const auto st  = state.FetchClamped(ix, iy, iz);
                const float temp = st[1] - ambientTemp;
                float ax = 0.0f;
                float ay = buoyancy * temp - gravity * st[0];
                float az = 0.0f;

                if (!forceRecords.empty())
                {
                    const auto gasForce = Frontier::SimulationSpace::ForceFields::EvaluateSaturatedGasForceField(
                        forceRecords,
                        { world[0], world[1], world[2] },
                        { vel[0], vel[1], vel[2] },
                        simTime,
                        forceResponse);
                    ax += gasForce[0];
                    ay += gasForce[1];
                    az += gasForce[2];
                }

                output.Cells[output.LinearIndex(ix, iy, iz)] = {
                    vel[0] + ax * dt,
                    vel[1] + ay * dt,
                    vel[2] + az * dt,
                    0.0f
                };
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        SLICE S16 KERNELS & FULL SOLVER STEP
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] bool IsCellInsideVolume(const FluidVolumeUniformBlock& vol, const int32_t x, const int32_t y, const int32_t z) noexcept
{
    return x >= 0 && y >= 0 && z >= 0
        && x < vol.GridBoundary[0]
        && y < vol.GridBoundary[1]
        && z < vol.GridBoundary[2];
}

[[nodiscard]] bool IsSolidAtCellCpu(const FluidVolumeUniformBlock& vol, const int32_t x, const int32_t y, const int32_t z) noexcept
{
    return IsSolidAtFluidWorld(vol, WorldFromFluidCell(vol, {
        static_cast<float>(x) + 0.5f,
        static_cast<float>(y) + 0.5f,
        static_cast<float>(z) + 0.5f
    }));
}

[[nodiscard]] float EvaluateOutflowSpongeCpu(
    const FluidVolumeUniformBlock& vol,
    const int32_t cx,
    const int32_t cy,
    const int32_t cz) noexcept
{
    if (vol.GridBoundary[3] == 3)
    {
        return 1.0f;
    }
    const int32_t gx = vol.GridBoundary[0];
    const int32_t gy = vol.GridBoundary[1];
    const int32_t gz = vol.GridBoundary[2];
    const float band = std::max(3.0f, static_cast<float>(std::max({ gx, gy, gz })) * 0.07f);
    float keep = 1.0f;

    if (IsOpenFluidFace(vol, { cx, gy, cz })) keep *= SmoothStepF(0.0f, band, static_cast<float>(gy - 1 - cy));
    if (IsOpenFluidFace(vol, { cx, -1, cz })) keep *= SmoothStepF(0.0f, band, static_cast<float>(cy));
    if (IsOpenFluidFace(vol, { gx, cy, cz })) keep *= SmoothStepF(0.0f, band, static_cast<float>(gx - 1 - cx));
    if (IsOpenFluidFace(vol, { -1, cy, cz })) keep *= SmoothStepF(0.0f, band, static_cast<float>(cx));
    if (IsOpenFluidFace(vol, { cx, cy, gz })) keep *= SmoothStepF(0.0f, band, static_cast<float>(gz - 1 - cz));
    if (IsOpenFluidFace(vol, { cx, cy, -1 })) keep *= SmoothStepF(0.0f, band, static_cast<float>(cz));

    return LerpF(0.82f, 1.0f, keep);
}

[[nodiscard]] float SampleVelocityAxisCpu(
    const FluidVolumeUniformBlock& vol,
    const FluidVolumeGridCpu& velocity,
    const int32_t nx,
    const int32_t ny,
    const int32_t nz,
    const int32_t axis,
    const float centreComp) noexcept
{
    if (!IsCellInsideVolume(vol, nx, ny, nz))
    {
        return IsOpenFluidFace(vol, { nx, ny, nz }) ? centreComp : 0.0f;
    }
    if (IsSolidAtCellCpu(vol, nx, ny, nz))
    {
        return 0.0f;
    }
    const auto v = velocity.FetchClamped(nx, ny, nz);
    return (axis == 0) ? v[0] : ((axis == 1) ? v[1] : v[2]);
}

[[nodiscard]] float SamplePressureNeighbourCpu(
    const FluidVolumeUniformBlock& vol,
    const FluidVolumeGridCpu& pressure,
    const int32_t nx,
    const int32_t ny,
    const int32_t nz,
    const float centre) noexcept
{
    if (!IsCellInsideVolume(vol, nx, ny, nz))
    {
        return IsOpenFluidFace(vol, { nx, ny, nz }) ? 0.0f : centre;
    }
    if (IsSolidAtCellCpu(vol, nx, ny, nz))
    {
        return centre;
    }
    return pressure.FetchClamped(nx, ny, nz)[0];
}

[[nodiscard]] float SampleLightNeighbourCpu(
    const FluidVolumeUniformBlock& vol,
    const FluidVolumeGridCpu& pressure,
    const int32_t nx,
    const int32_t ny,
    const int32_t nz,
    const float centre) noexcept
{
    if (!IsCellInsideVolume(vol, nx, ny, nz))
    {
        return 0.0f;
    }
    if (IsSolidAtCellCpu(vol, nx, ny, nz))
    {
        return centre;
    }
    return pressure.FetchClamped(nx, ny, nz)[1];
}

} // namespace

void RunFluidCurlCpu(
    const FluidVorticityUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    FluidVolumeGridCpu& outputCurl)
{
    const uint32_t W = velocity.Width, H = velocity.Height, D = velocity.Depth;
    if (outputCurl.Width != W || outputCurl.Height != H || outputCurl.Depth != D) outputCurl.Resize(W, H, D);
    const float invTwoH = 0.5f / std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const auto xp = velocity.FetchClamped(ix + 1, iy, iz);
                const auto xm = velocity.FetchClamped(ix - 1, iy, iz);
                const auto yp = velocity.FetchClamped(ix, iy + 1, iz);
                const auto ym = velocity.FetchClamped(ix, iy - 1, iz);
                const auto zp = velocity.FetchClamped(ix, iy, iz + 1);
                const auto zm = velocity.FetchClamped(ix, iy, iz - 1);

                const float cx = ((yp[2] - ym[2]) - (zp[1] - zm[1])) * invTwoH;
                const float cy = ((zp[0] - zm[0]) - (xp[2] - xm[2])) * invTwoH;
                const float cz = ((xp[1] - xm[1]) - (yp[0] - ym[0])) * invTwoH;
                const float mag = std::sqrt(cx * cx + cy * cy + cz * cz);
                outputCurl.Cells[outputCurl.LinearIndex(ix, iy, iz)] = { cx, cy, cz, mag };
            }
        }
    }
}

void RunFluidVorticityCpu(
    const FluidVorticityUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& curl,
    FluidVolumeGridCpu& outputVelocity)
{
    const uint32_t W = velocity.Width, H = velocity.Height, D = velocity.Depth;
    if (outputVelocity.Width != W || outputVelocity.Height != H || outputVelocity.Depth != D) outputVelocity.Resize(W, H, D);

    const float h           = std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);
    const float invTwoH     = 0.5f / h;
    const float dt          = uniforms.VorticityControl[0];
    const float confinement = uniforms.VorticityControl[1];

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const auto vel = velocity.FetchClamped(ix, iy, iz);
                const auto ctr = curl.FetchClamped(ix, iy, iz);

                const float ex = (curl.FetchClamped(ix + 1, iy, iz)[3] - curl.FetchClamped(ix - 1, iy, iz)[3]) * invTwoH;
                const float ey = (curl.FetchClamped(ix, iy + 1, iz)[3] - curl.FetchClamped(ix, iy - 1, iz)[3]) * invTwoH;
                const float ez = (curl.FetchClamped(ix, iy, iz + 1)[3] - curl.FetchClamped(ix, iy, iz - 1)[3]) * invTwoH;
                const float mag = std::sqrt(ex * ex + ey * ey + ez * ez);
                const float nx = (mag > 1.0e-5f) ? (ex / mag) : 0.0f;
                const float ny = (mag > 1.0e-5f) ? (ey / mag) : 0.0f;
                const float nz = (mag > 1.0e-5f) ? (ez / mag) : 0.0f;

                const float scale = confinement * h;
                const float fx = scale * (ny * ctr[2] - nz * ctr[1]);
                const float fy = scale * (nz * ctr[0] - nx * ctr[2]);
                const float fz = scale * (nx * ctr[1] - ny * ctr[0]);

                outputVelocity.Cells[outputVelocity.LinearIndex(ix, iy, iz)] = {
                    vel[0] + fx * dt,
                    vel[1] + fy * dt,
                    vel[2] + fz * dt,
                    0.0f
                };
            }
        }
    }
}

void RunFluidThermodynamicsCpu(
    const FluidThermodynamicsUniformBlock& uniforms,
    const FluidVolumeGridCpu& state,
    FluidVolumeGridCpu& outputState)
{
    const uint32_t W = state.Width, H = state.Height, D = state.Depth;
    if (outputState.Width != W || outputState.Height != H || outputState.Depth != D) outputState.Resize(W, H, D);

    const float dt          = uniforms.ThermoControl[0];
    const float cooling     = uniforms.ThermoControl[1];
    const float combustion  = uniforms.ThermoControl[2];
    const float ambientTemp = uniforms.ThermoControl[3];
    constexpr float kIgnition = 0.9f;

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);

                const auto st = state.FetchClamped(ix, iy, iz);
                const float sponge = EvaluateOutflowSpongeCpu(uniforms.Volume, ix, iy, iz);
                float density     = st[0] * sponge;
                float temperature = ambientTemp + (st[1] - ambientTemp) * sponge;
                float fuel        = st[2] * sponge;
                float reaction    = 0.0f;

                if (fuel > 0.0f && temperature > kIgnition && combustion > 0.0f)
                {
                    const float burn = std::min(
                        fuel,
                        combustion * 2.5f * dt * fuel * SmoothStepF(kIgnition, kIgnition + 1.0f, temperature));
                    reaction = burn / std::max(dt, 1.0e-4f);
                    fuel -= burn;
                    temperature += burn * 12.0f;
                    density += burn * 0.55f;
                }

                float excess = temperature - ambientTemp;
                excess = std::max(0.0f, excess - dt * cooling * (0.35f * excess + 0.02f * excess * excess * excess));
                temperature = ambientTemp + excess;

                outputState.Cells[outputState.LinearIndex(ix, iy, iz)] = {
                    std::max(density, 0.0f),
                    temperature,
                    std::max(fuel, 0.0f),
                    reaction
                };
            }
        }
    }
}

void RunFluidDivergenceCpu(
    const FluidDivergenceUniformBlock& uniforms,
    const FluidVolumeGridCpu& velocity,
    const FluidVolumeGridCpu& state,
    FluidVolumeGridCpu& outputDivergence)
{
    const uint32_t W = velocity.Width, H = velocity.Height, D = velocity.Depth;
    if (outputDivergence.Width != W || outputDivergence.Height != H || outputDivergence.Depth != D)
    {
        outputDivergence.Resize(W, H, D);
    }

    const float h             = std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);
    const float expansion     = uniforms.DivergenceControl[0];
    const float fireThreshold = uniforms.DivergenceControl[1];
    const float fireSoftness  = std::max(uniforms.DivergenceControl[2], 0.05f);

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);

                const auto c = velocity.FetchClamped(ix, iy, iz);
                const float xp = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix + 1, iy, iz, 0, c[0]);
                const float xm = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix - 1, iy, iz, 0, c[0]);
                const float yp = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix, iy + 1, iz, 1, c[1]);
                const float ym = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix, iy - 1, iz, 1, c[1]);
                const float zp = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix, iy, iz + 1, 2, c[2]);
                const float zm = SampleVelocityAxisCpu(uniforms.Volume, velocity, ix, iy, iz - 1, 2, c[2]);

                float divergence = 0.5f * ((xp - xm) + (yp - ym) + (zp - zm)) / h;
                const auto st = state.FetchClamped(ix, iy, iz);
                divergence -= expansion * 0.08f * st[3];

                const float flame    = ClampF((st[1] - fireThreshold) / fireSoftness, 0.0f, 1.0f);
                const float presence = SmoothStepF(fireThreshold * 0.7f, fireThreshold * 1.15f, st[1]);
                const float lightSrc = std::min(8.0f, presence * (0.2f + 4.0f * flame * flame));

                outputDivergence.Cells[outputDivergence.LinearIndex(ix, iy, iz)] = { divergence, lightSrc, 0.0f, 0.0f };
            }
        }
    }
}

void RunFluidPressureJacobiCpu(
    const FluidPressureUniformBlock& uniforms,
    const FluidVolumeGridCpu& pressure,
    const FluidVolumeGridCpu& divergence,
    FluidVolumeGridCpu& outputPressure)
{
    const uint32_t W = pressure.Width, H = pressure.Height, D = pressure.Depth;
    if (outputPressure.Width != W || outputPressure.Height != H || outputPressure.Depth != D)
    {
        outputPressure.Resize(W, H, D);
    }

    const float h2     = uniforms.Volume.GridCentreCellSize[3] * uniforms.Volume.GridCentreCellSize[3];
    const float retain = uniforms.PressureControl[0];

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);

                const auto centre = pressure.FetchClamped(ix, iy, iz);
                const auto source = divergence.FetchClamped(ix, iy, iz);

                const float sum = SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix + 1, iy, iz, centre[0])
                                + SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix - 1, iy, iz, centre[0])
                                + SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy + 1, iz, centre[0])
                                + SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy - 1, iz, centre[0])
                                + SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz + 1, centre[0])
                                + SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz - 1, centre[0]);

                const float light = SampleLightNeighbourCpu(uniforms.Volume, pressure, ix + 1, iy, iz, centre[1])
                                  + SampleLightNeighbourCpu(uniforms.Volume, pressure, ix - 1, iy, iz, centre[1])
                                  + SampleLightNeighbourCpu(uniforms.Volume, pressure, ix, iy + 1, iz, centre[1])
                                  + SampleLightNeighbourCpu(uniforms.Volume, pressure, ix, iy - 1, iz, centre[1])
                                  + SampleLightNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz + 1, centre[1])
                                  + SampleLightNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz - 1, centre[1]);

                outputPressure.Cells[outputPressure.LinearIndex(ix, iy, iz)] = {
                    (sum * retain - source[0] * h2) / 6.0f,
                    (light + source[1] * 0.55f) / 6.28f,
                    0.0f,
                    0.0f
                };
            }
        }
    }
}

void RunFluidGradientCpu(
    const FluidGradientShiftUniformBlock& uniforms,
    const FluidVolumeGridCpu& pressure,
    const FluidVolumeGridCpu& velocity,
    FluidVolumeGridCpu& outputVelocity)
{
    const uint32_t W = velocity.Width, H = velocity.Height, D = velocity.Depth;
    if (outputVelocity.Width != W || outputVelocity.Height != H || outputVelocity.Depth != D)
    {
        outputVelocity.Resize(W, H, D);
    }

    const float invTwoH = 0.5f / std::max(uniforms.Volume.GridCentreCellSize[3], 1.0e-5f);

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);

                if (IsSolidAtCellCpu(uniforms.Volume, ix, iy, iz))
                {
                    outputVelocity.Cells[outputVelocity.LinearIndex(ix, iy, iz)] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    continue;
                }

                const float centre = pressure.FetchClamped(ix, iy, iz)[0];
                const float gx = (SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix + 1, iy, iz, centre)
                                - SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix - 1, iy, iz, centre)) * invTwoH;
                const float gy = (SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy + 1, iz, centre)
                                - SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy - 1, iz, centre)) * invTwoH;
                const float gz = (SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz + 1, centre)
                                - SamplePressureNeighbourCpu(uniforms.Volume, pressure, ix, iy, iz - 1, centre)) * invTwoH;

                const auto vel = velocity.FetchClamped(ix, iy, iz);
                float vx = vel[0] - gx;
                float vy = vel[1] - gy;
                float vz = vel[2] - gz;

                if (!IsCellInsideVolume(uniforms.Volume, ix - 1, iy, iz) && !IsOpenFluidFace(uniforms.Volume, { ix - 1, iy, iz })) vx = std::max(vx, 0.0f);
                if (!IsCellInsideVolume(uniforms.Volume, ix + 1, iy, iz) && !IsOpenFluidFace(uniforms.Volume, { ix + 1, iy, iz })) vx = std::min(vx, 0.0f);
                if (!IsCellInsideVolume(uniforms.Volume, ix, iy - 1, iz) && !IsOpenFluidFace(uniforms.Volume, { ix, iy - 1, iz })) vy = std::max(vy, 0.0f);
                if (!IsCellInsideVolume(uniforms.Volume, ix, iy + 1, iz) && !IsOpenFluidFace(uniforms.Volume, { ix, iy + 1, iz })) vy = std::min(vy, 0.0f);
                if (!IsCellInsideVolume(uniforms.Volume, ix, iy, iz - 1) && !IsOpenFluidFace(uniforms.Volume, { ix, iy, iz - 1 })) vz = std::max(vz, 0.0f);
                if (!IsCellInsideVolume(uniforms.Volume, ix, iy, iz + 1) && !IsOpenFluidFace(uniforms.Volume, { ix, iy, iz + 1 })) vz = std::min(vz, 0.0f);

                outputVelocity.Cells[outputVelocity.LinearIndex(ix, iy, iz)] = { vx, vy, vz, 0.0f };
            }
        }
    }
}

void RunFluidShiftCpu(
    const FluidGradientShiftUniformBlock& uniforms,
    const FluidVolumeGridCpu& source,
    FluidVolumeGridCpu& output)
{
    const uint32_t W = source.Width, H = source.Height, D = source.Depth;
    if (output.Width != W || output.Height != H || output.Depth != D) output.Resize(W, H, D);

    const int32_t dx = uniforms.ShiftDelta[0];
    const int32_t dy = uniforms.ShiftDelta[1];
    const int32_t dz = uniforms.ShiftDelta[2];

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const int32_t sx = ix + dx;
                const int32_t sy = iy + dy;
                const int32_t sz = iz + dz;

                output.Cells[output.LinearIndex(ix, iy, iz)] = IsCellInsideVolume(uniforms.Volume, sx, sy, sz)
                    ? source.FetchClamped(sx, sy, sz)
                    : std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f };
            }
        }
    }
}

void RunFluidOccupancyCpu(
    const SparseTileBank& bank,
    const FluidVolumeGridCpu& stateTiles,
    const FluidVolumeGridCpu& velocityTiles,
    const int32_t stride,
    const int32_t band,
    std::vector<float>& outputReadbackRgba)
{
    const auto slots = bank.QuerySlots();
    const int32_t tileCells = static_cast<int32_t>(bank.QueryTileCells());
    const int32_t safeStride = std::max(1, stride);
    const int32_t safeBand   = std::max(1, band);

    outputReadbackRgba.assign(slots.size() * 12u, 0.0f);
    for (size_t s = 0u; s < slots.size(); ++s)
    {
        if (!slots[s].has_value())
        {
            continue;
        }
        float* row = outputReadbackRgba.data() + s * 12u;
        const int32_t zBase = static_cast<int32_t>(s) * tileCells;

        for (int32_t lz = 0; lz < tileCells; lz += safeStride)
        {
            for (int32_t ly = 0; ly < tileCells; ly += safeStride)
            {
                for (int32_t lx = 0; lx < tileCells; lx += safeStride)
                {
                    const float density = stateTiles.FetchClamped(lx, ly, zBase + lz)[0];
                    const auto vel      = velocityTiles.FetchClamped(lx, ly, zBase + lz);
                    const float speed   = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);

                    // Column 0: (densitySum, densityMax, speedMax, residentFlag)
                    row[0] += density;
                    row[1]  = std::max(row[1], density);
                    row[2]  = std::max(row[2], speed);

                    // Column 1: (-X, +X, -Y, +Y) band density
                    if (lx < safeBand)              row[4] += density;
                    if (lx >= tileCells - safeBand) row[5] += density;
                    if (ly < safeBand)              row[6] += density;
                    if (ly >= tileCells - safeBand) row[7] += density;

                    // Column 2: (-Z, +Z, 0, 0) band density
                    if (lz < safeBand)              row[8] += density;
                    if (lz >= tileCells - safeBand) row[9] += density;
                }
            }
        }
        row[3] = 1.0f;
    }
}

void FluidCpuSimulationState::InitializeFixed(
    const std::array<uint32_t, 3>& gridCells,
    const float cellSizeMeters,
    const std::array<float, 3>& centreMeters)
{
    TileBank.ConfigureFixed(gridCells, cellSizeMeters, centreMeters);
    const auto& layout = TileBank.QueryLayout();
    Velocity.Resize(layout.Width, layout.Height, layout.Depth);
    State.Resize(layout.Width, layout.Height, layout.Depth);
    Curl.Resize(layout.Width, layout.Height, layout.Depth);
    Divergence.Resize(layout.Width, layout.Height, layout.Depth);
    Pressure.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchA.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchB.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchC.Resize(layout.Width, layout.Height, layout.Depth);
    WorldTileCells = 0u;
    SimulationTime = 0.0;
    StepCount = 0u;
}

void FluidCpuSimulationState::InitializeWorldGrid(
    const uint32_t tileSpan,
    const uint32_t tileCells,
    const float cellSizeMeters,
    const std::array<float, 3>& targetMeters)
{
    const uint32_t span = std::clamp(tileSpan, 1u, kMaximumGridTiles);
    const uint32_t dim  = std::min(span * tileCells, kMaximumFixedCells);
    const float tileWorld = static_cast<float>(tileCells) * cellSizeMeters;
    const std::array<float, 3> snappedCentre {
        std::round(targetMeters[0] / std::max(tileWorld, 1.0e-5f)) * tileWorld,
        std::round(targetMeters[1] / std::max(tileWorld, 1.0e-5f)) * tileWorld,
        std::round(targetMeters[2] / std::max(tileWorld, 1.0e-5f)) * tileWorld
    };
    TileBank.ConfigureFixed({ dim, dim, dim }, cellSizeMeters, snappedCentre);
    const auto& layout = TileBank.QueryLayout();
    Velocity.Resize(layout.Width, layout.Height, layout.Depth);
    State.Resize(layout.Width, layout.Height, layout.Depth);
    Curl.Resize(layout.Width, layout.Height, layout.Depth);
    Divergence.Resize(layout.Width, layout.Height, layout.Depth);
    Pressure.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchA.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchB.Resize(layout.Width, layout.Height, layout.Depth);
    ScratchC.Resize(layout.Width, layout.Height, layout.Depth);
    WorldTileCells = tileCells;
    SimulationTime = 0.0;
    StepCount = 0u;
}

void StepFluidSimulationCpu(
    FluidCpuSimulationState& sim,
    const FluidStepSettings& settings,
    const std::span<const FluidReleaseSourceParameters> sources,
    const std::span<const Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    const std::span<const FluidColliderSphere> colliders,
    const float deltaTimeSeconds)
{
    const auto volU = sim.TileBank.BuildVolumeUniforms(settings.Boundary, colliders);
    const float simTime = static_cast<float>(sim.SimulationTime);

    // 1. Release source injection into velocity (mode 0) & scalar state (mode 1)
    if (!sources.empty())
    {
        const auto velInjU = BuildFluidInjectUniforms(volU, 0u, sources, deltaTimeSeconds, simTime);
        RunFluidInjectCpu(velInjU, sim.Velocity, sim.ScratchA);
        std::swap(sim.Velocity, sim.ScratchA);

        const auto stateInjU = BuildFluidInjectUniforms(volU, 1u, sources, deltaTimeSeconds, simTime);
        RunFluidInjectCpu(stateInjU, sim.State, sim.ScratchA);
        std::swap(sim.State, sim.ScratchA);
    }

    // 2. Self-advection of velocity & transport of scalar state
    if (settings.Advection == FluidAdvectionOrdinal::MacCormack)
    {
        AdvectFluidFieldMacCormackCpu(volU, sim.Velocity, sim.Velocity, deltaTimeSeconds, settings.VelocityDissipation, sim.ScratchA, sim.ScratchB, sim.ScratchC);
        std::swap(sim.Velocity, sim.ScratchC);

        AdvectFluidFieldMacCormackCpu(volU, sim.Velocity, sim.State, deltaTimeSeconds, settings.Dissipation, sim.ScratchA, sim.ScratchB, sim.ScratchC);
        std::swap(sim.State, sim.ScratchC);
    }
    else
    {
        FluidAdvectUniformBlock vAdvU { volU, { deltaTimeSeconds, settings.VelocityDissipation, 1.0f, 0.0f } };
        RunFluidAdvectCpu(vAdvU, sim.Velocity, sim.Velocity, sim.ScratchA);
        std::swap(sim.Velocity, sim.ScratchA);

        FluidAdvectUniformBlock sAdvU { volU, { deltaTimeSeconds, settings.Dissipation, 1.0f, 0.0f } };
        RunFluidAdvectCpu(sAdvU, sim.Velocity, sim.State, sim.ScratchA);
        std::swap(sim.State, sim.ScratchA);
    }

    // 3. Thermodynamics (outflow sponge + ignition 0.9 combustion + Newtonian/radiative cooling)
    FluidThermodynamicsUniformBlock thermoU {
        volU,
        { deltaTimeSeconds, settings.Cooling, settings.Combustion, settings.AmbientTemperature }
    };
    RunFluidThermodynamicsCpu(thermoU, sim.State, sim.ScratchA);
    std::swap(sim.State, sim.ScratchA);

    // 4. Buoyancy, gravity, velocity-saturated ForceModels, collider masking
    FluidForcesUniformBlock forcesU {
        volU,
        { deltaTimeSeconds, settings.Buoyancy, settings.Gravity, settings.AmbientTemperature },
        { settings.ForceResponse, simTime, static_cast<float>(forceRecords.size()), 0.0f }
    };
    RunFluidForcesCpu(forcesU, sim.Velocity, sim.State, forceRecords, sim.ScratchA);
    std::swap(sim.Velocity, sim.ScratchA);

    // 5. Curl & vorticity confinement
    FluidVorticityUniformBlock vortU { volU, { deltaTimeSeconds, settings.Vorticity, 0.0f, 0.0f } };
    RunFluidCurlCpu(vortU, sim.Velocity, sim.Curl);
    if (settings.Vorticity > 0.0f)
    {
        RunFluidVorticityCpu(vortU, sim.Velocity, sim.Curl, sim.ScratchA);
        std::swap(sim.Velocity, sim.ScratchA);
    }

    // 6. Divergence + flame light source, warm-started Jacobi pressure + screened fire light, gradient projection
    FluidDivergenceUniformBlock divU { volU, { settings.Expansion, settings.FireThreshold, settings.FireSoftness, 0.0f } };
    RunFluidDivergenceCpu(divU, sim.Velocity, sim.State, sim.Divergence);

    FluidPressureUniformBlock pressU { volU, { 0.96f, 0.0f, 0.0f, 0.0f } };
    for (uint32_t iter = 0u; iter < settings.PressureIterations; ++iter)
    {
        pressU.PressureControl[0] = (iter == 0u) ? 0.96f : 1.0f;
        RunFluidPressureJacobiCpu(pressU, sim.Pressure, sim.Divergence, sim.ScratchA);
        std::swap(sim.Pressure, sim.ScratchA);
    }

    FluidGradientShiftUniformBlock gradU { volU, { 0, 0, 0, 0 }, { 2, 3, 0, 0 } };
    RunFluidGradientCpu(gradU, sim.Pressure, sim.Velocity, sim.ScratchA);
    std::swap(sim.Velocity, sim.ScratchA);

    sim.SimulationTime += static_cast<double>(deltaTimeSeconds);
    sim.StepCount += 1u;
}

bool RelocateFluidSimulationCpu(
    FluidCpuSimulationState& sim,
    const std::array<float, 3>& targetMeters,
    const FluidBoundaryOrdinal boundary,
    const std::span<const FluidColliderSphere> colliders)
{
    const float cellSize = std::max(sim.TileBank.QueryCellSizeMeters(), 1.0e-5f);
    const uint32_t tileCells = (sim.WorldTileCells > 0u) ? sim.WorldTileCells : 1u;
    const float tileWorld = static_cast<float>(tileCells) * cellSize;
    const auto oldCentre = sim.TileBank.QueryGridCentre();
    const std::array<float, 3> newCentre {
        std::round(targetMeters[0] / tileWorld) * tileWorld,
        std::round(targetMeters[1] / tileWorld) * tileWorld,
        std::round(targetMeters[2] / tileWorld) * tileWorld
    };
    const std::array<int32_t, 3> shift {
        static_cast<int32_t>(std::lround((newCentre[0] - oldCentre[0]) / cellSize)),
        static_cast<int32_t>(std::lround((newCentre[1] - oldCentre[1]) / cellSize)),
        static_cast<int32_t>(std::lround((newCentre[2] - oldCentre[2]) / cellSize))
    };

    const auto reloc = sim.TileBank.RelocateFixedWindow(shift, newCentre);
    if (!reloc.Moved)
    {
        return false;
    }

    if (reloc.ClearWhole)
    {
        sim.Velocity.Clear();
        sim.State.Clear();
        sim.Pressure.Clear();
        return true;
    }

    const auto volU = sim.TileBank.BuildVolumeUniforms(boundary, colliders);
    FluidGradientShiftUniformBlock shiftU { volU, { reloc.ShiftCells[0], reloc.ShiftCells[1], reloc.ShiftCells[2], 0 }, { 2, 3, 0, 0 } };

    RunFluidShiftCpu(shiftU, sim.Velocity, sim.ScratchA);
    std::swap(sim.Velocity, sim.ScratchA);

    RunFluidShiftCpu(shiftU, sim.State, sim.ScratchA);
    std::swap(sim.State, sim.ScratchA);

    RunFluidShiftCpu(shiftU, sim.Pressure, sim.ScratchA);
    std::swap(sim.Pressure, sim.ScratchA);

    return true;
}

float MeasureMaxFluidDivergenceCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    const uint32_t interiorMargin) noexcept
{
    const float h = std::max(volume.GridCentreCellSize[3], 1.0e-5f);
    float maxDiv = 0.0f;
    for (uint32_t z = interiorMargin; z + interiorMargin < velocity.Depth; ++z)
    {
        for (uint32_t y = interiorMargin; y + interiorMargin < velocity.Height; ++y)
        {
            for (uint32_t x = interiorMargin; x + interiorMargin < velocity.Width; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                if (IsSolidAtCellCpu(volume, ix, iy, iz))
                {
                    continue;
                }
                const auto c = velocity.FetchClamped(ix, iy, iz);
                const float xp = SampleVelocityAxisCpu(volume, velocity, ix + 1, iy, iz, 0, c[0]);
                const float xm = SampleVelocityAxisCpu(volume, velocity, ix - 1, iy, iz, 0, c[0]);
                const float yp = SampleVelocityAxisCpu(volume, velocity, ix, iy + 1, iz, 1, c[1]);
                const float ym = SampleVelocityAxisCpu(volume, velocity, ix, iy - 1, iz, 1, c[1]);
                const float zp = SampleVelocityAxisCpu(volume, velocity, ix, iy, iz + 1, 2, c[2]);
                const float zm = SampleVelocityAxisCpu(volume, velocity, ix, iy, iz - 1, 2, c[2]);
                const float div = std::fabs(0.5f * ((xp - xm) + (yp - ym) + (zp - zm)) / h);
                maxDiv = std::max(maxDiv, div);
            }
        }
    }
    return maxDiv;
}

//------------------------------------------------------------------------------------------------------------------------
//                              REAL ASYNC VULKAN 1.2 FLUID GPU EXECUTOR (MERGE-PREP U03)
//------------------------------------------------------------------------------------------------------------------------

FluidVulkanGpuExecutor::~FluidVulkanGpuExecutor()
{
    Teardown();
}

#if FRONTIER_FLUID_HAS_VULKAN
bool FluidVulkanGpuExecutor::AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept
{
    Interchange_ = interchange;
    if (Interchange_ == nullptr || !Interchange_->Ready())
    {
        GpuLiveReady_    = false;
        Telemetry_.Route = FluidExecutionRoute::HeadlessCpuMirror;
        return false;
    }
    return true;
}
#endif

bool FluidVulkanGpuExecutor::Initialize(
    const std::array<uint32_t, 3>& gridCells,
    float cellSizeMeters,
    const std::array<float, 3>& centreMeters,
    const std::filesystem::path& compiledShaderDirectory)
{
    Teardown();
    GridCells_      = { std::max(4u, gridCells[0]), std::max(4u, gridCells[1]), std::max(4u, gridCells[2]) };
    CellSizeMeters_ = std::max(0.01f, cellSizeMeters);
    CentreMeters_   = centreMeters;
    SimTime_        = 0.0;
    ScratchOccupancy_.reserve(kMaxOccupancyFloat4s);
    ScratchOccupancy_.clear();

    Telemetry_ = {};

#if FRONTIER_FLUID_HAS_VULKAN
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        GpuLiveReady_ = InitializeGpuResources(compiledShaderDirectory);
    }
    (void)OccupancyReadbackRing_.Initialize(kMaxOccupancyFloat4s, GpuLiveReady_ ? Interchange_ : nullptr);
    Telemetry_.Route = GpuLiveReady_ ? FluidExecutionRoute::VulkanComputeGpu : FluidExecutionRoute::HeadlessCpuMirror;
#else
    (void)compiledShaderDirectory;
    GpuLiveReady_ = false;
    (void)OccupancyReadbackRing_.Initialize(kMaxOccupancyFloat4s);
    Telemetry_.Route = FluidExecutionRoute::HeadlessCpuMirror;
#endif

    Initialized_ = true;
    return true;
}

void FluidVulkanGpuExecutor::Teardown() noexcept
{
    OccupancyReadbackRing_.Teardown();
#if FRONTIER_FLUID_HAS_VULKAN
    ReleaseGpuResources();
#endif
    ScratchOccupancy_.clear();
    Telemetry_    = {};
    Initialized_  = false;
    GpuLiveReady_ = false;
}

void FluidVulkanGpuExecutor::ExecuteStep(
    FluidCpuSimulationState& headlessFallbackState,
    const FluidStepSettings& settings,
    std::span<const FluidReleaseSourceParameters> sources,
    std::span<const Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    std::span<const FluidColliderSphere> colliders,
    float deltaTimeSeconds)
{
    if (!Initialized_ || deltaTimeSeconds <= 0.0f) return;

    SimTime_ += static_cast<double>(deltaTimeSeconds);
    const uint32_t nextSlot = (Telemetry_.ActiveRingSlot + 1u) % kTripleBufferSlots;
    Telemetry_.ActiveRingSlot = nextSlot;

#if FRONTIER_FLUID_HAS_VULKAN
    if (GpuLiveReady_ && Interchange_ != nullptr && Interchange_->Ready())
    {
        // Real Async Vulkan 1.2 3D texture compute path: ZERO StepFluidSimulationCpu call, ZERO mid-frame stall!
        DispatchGpuAsync(settings, sources, forceRecords, colliders, deltaTimeSeconds);
        (void)OccupancyReadbackRing_.PollNonBlocking();
        Telemetry_.Route             = FluidExecutionRoute::VulkanComputeGpu;
        Telemetry_.RealGpuDispatched = true;
        Telemetry_.CompletedTicks   += 1ull;
        return;
    }
#endif

    // Strict headless CPU fallback ONLY when VulkanInterchange is unavailable/unready
    StepFluidSimulationCpu(headlessFallbackState, settings, sources, forceRecords, colliders, deltaTimeSeconds);
    Telemetry_.Route             = FluidExecutionRoute::HeadlessCpuMirror;
    Telemetry_.RealGpuDispatched = false;
    Telemetry_.CompletedTicks   += 1ull;

    // 3-slot non-blocking occupancy ring parity in headless mode (1-frame latency, zero allocs)
    ScratchOccupancy_.resize(1u);
    float peakD = 0.0f;
    for (const auto& cell : headlessFallbackState.State.Cells)
    {
        peakD = std::max(peakD, cell[0]);
    }
    ScratchOccupancy_[0] = { peakD, static_cast<float>(headlessFallbackState.StepCount), 0.0f, 1.0f };
    OccupancyReadbackRing_.SubmitHeadlessSnapshot(ScratchOccupancy_, Telemetry_.CompletedTicks);
    if (Telemetry_.CompletedTicks > 1ull)
    {
        (void)OccupancyReadbackRing_.PollNonBlocking(Telemetry_.CompletedTicks - 1ull);
    }
}

bool FluidVulkanGpuExecutor::PollAsyncOccupancyReadback() noexcept
{
#if FRONTIER_FLUID_HAS_VULKAN
    if (GpuLiveReady_)
    {
        RetireCompletedCommandBuffersNonBlocking();
        return OccupancyReadbackRing_.PollNonBlocking();
    }
#endif
    return OccupancyReadbackRing_.PollNonBlocking(Telemetry_.CompletedTicks);
}

#if FRONTIER_FLUID_HAS_VULKAN
void FluidVulkanGpuExecutor::RetireCompletedCommandBuffersNonBlocking() noexcept
{
    if (Interchange_ == nullptr || !Interchange_->Ready()) return;
    if (InitCommand_.CmdBuffer != VK_NULL_HANDLE &&
        Interchange_->QueryTimelineReached(InitCommand_.TimelineSignal))
    {
        Interchange_->FreeCommandBuffer(InitCommand_.CmdBuffer);
        InitCommand_ = {};
    }
    for (auto& pending : InFlightCommands_)
    {
        if (pending.CmdBuffer != VK_NULL_HANDLE &&
            Interchange_->QueryTimelineReached(pending.TimelineSignal))
        {
            Interchange_->FreeCommandBuffer(pending.CmdBuffer);
            pending.CmdBuffer      = VK_NULL_HANDLE;
            pending.TimelineSignal = 0ull;
        }
    }
}

bool FluidVulkanGpuExecutor::InitializeGpuResources(const std::filesystem::path& shaderDir)
{
    using namespace GeometricRaster;
    ReleaseGpuResources();

    if (Interchange_ == nullptr || !Interchange_->Ready()) return false;
    if (!Encoder_.InitializeEncoder(Interchange_, 512u)) return false;

    const auto spvInject = shaderDir / "FluidInjectCompute.comp.spv";
    const auto spvAdvect = shaderDir / "FluidAdvectCompute.comp.spv";
    const auto spvThermo = shaderDir / "FluidThermodynamicsCompute.comp.spv";
    const auto spvForces = shaderDir / "FluidForcesCompute.comp.spv";
    const auto spvCurl   = shaderDir / "FluidCurlCompute.comp.spv";
    const auto spvVort   = shaderDir / "FluidVorticityCompute.comp.spv";
    const auto spvDiv    = shaderDir / "FluidDivergenceCompute.comp.spv";
    const auto spvPress  = shaderDir / "FluidPressureCompute.comp.spv";
    const auto spvGrad   = shaderDir / "FluidGradientCompute.comp.spv";
    if (!std::filesystem::exists(spvInject) || !std::filesystem::exists(spvAdvect) || !std::filesystem::exists(spvGrad))
    {
        Encoder_.TeardownEncoder();
        return false;
    }

    const std::array<VkDescriptorType, 3> injectSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };
    const std::array<VkDescriptorType, 5> advectSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER
    };
    const std::array<VkDescriptorType, 5> forcesSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };
    const std::array<VkDescriptorType, 4> vortSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };
    const std::array<VkDescriptorType, 7> divPressSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    };
    const std::array<VkDescriptorType, 8> gradSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };

    ProgInject_     = Encoder_.RegisterProgram(spvInject, injectSlots, "ComputeEntry");
    ProgAdvect_     = Encoder_.RegisterProgram(spvAdvect, advectSlots, "ComputeEntry");
    ProgThermo_     = Encoder_.RegisterProgram(spvThermo, injectSlots, "ComputeEntry");
    ProgForces_     = Encoder_.RegisterProgram(spvForces, forcesSlots, "ComputeEntry");
    ProgCurl_       = Encoder_.RegisterProgram(spvCurl,   vortSlots,   "CurlComputeEntry");
    ProgVorticity_  = Encoder_.RegisterProgram(spvVort,   vortSlots,   "VorticityComputeEntry");
    ProgDivergence_ = Encoder_.RegisterProgram(spvDiv,    divPressSlots, "DivergenceComputeEntry");
    ProgPressure_   = Encoder_.RegisterProgram(spvPress,  divPressSlots, "PressureComputeEntry");
    ProgGradient_   = Encoder_.RegisterProgram(spvGrad,   gradSlots,   "GradientComputeEntry");

    const std::array<VkDescriptorType, 7> macSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLER
    };
    const std::array<VkDescriptorType, 10> tracerDebrisSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLER,        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const auto spvMac    = shaderDir / "FluidMacCormackCompute.comp.spv";
    const auto spvTracer = shaderDir / "FluidTracerUpdateCompute.comp.spv";
    const auto spvDebris = shaderDir / "FluidDebrisUpdateCompute.comp.spv";
    ProgMacCormack_   = std::filesystem::exists(spvMac)    ? Encoder_.RegisterProgram(spvMac,    macSlots,          "ComputeEntry")             : UINT32_MAX;
    ProgTracerUpdate_ = std::filesystem::exists(spvTracer) ? Encoder_.RegisterProgram(spvTracer, tracerDebrisSlots, "TracerUpdateComputeEntry") : UINT32_MAX;
    ProgDebrisUpdate_ = std::filesystem::exists(spvDebris) ? Encoder_.RegisterProgram(spvDebris, tracerDebrisSlots, "DebrisUpdateComputeEntry") : UINT32_MAX;
    Telemetry_.MacCormackPipelineReady = (ProgMacCormack_ != UINT32_MAX);
    Telemetry_.TracerDebrisReady       = (ProgTracerUpdate_ != UINT32_MAX && ProgDebrisUpdate_ != UINT32_MAX);

    if (ProgInject_ == UINT32_MAX || ProgAdvect_ == UINT32_MAX || ProgGradient_ == UINT32_MAX)
    {
        ReleaseGpuResources();
        return false;
    }

    VkSamplerCreateInfo samplerCi {};
    samplerCi.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCi.magFilter    = VK_FILTER_LINEAR;
    samplerCi.minFilter    = VK_FILTER_LINEAR;
    samplerCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Interchange_->AccessDevice(), &samplerCi, nullptr, &LinearSampler_) != VK_SUCCESS)
    {
        ReleaseGpuResources();
        return false;
    }

    const VkImageUsageFlags volUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto allocVol = [&]() -> DeviceImageResource
    {
        return Interchange_->AllocateImage3D(
            GridCells_[0], GridCells_[1], GridCells_[2],
            VK_FORMAT_R16G16B16A16_SFLOAT, volUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    };

    ImgVelocity_[0] = allocVol(); ImgVelocity_[1] = allocVol();
    ImgState_[0]    = allocVol(); ImgState_[1]    = allocVol();
    ImgPressure_[0] = allocVol(); ImgPressure_[1] = allocVol();
    ImgCurl_        = allocVol();
    ImgDivergence_  = allocVol();
    ImgScratchA_    = allocVol();
    ImgScratchB_    = allocVol();
    ImgDummy2DFloat_ = Interchange_->AllocateImage2D(
        1u, 1u, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    ImgDummy2DInt_   = Interchange_->AllocateImage2D(
        1u, 1u, VK_FORMAT_R32G32B32A32_SINT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    if (ImgVelocity_[0].ImageHandle == VK_NULL_HANDLE || ImgState_[0].ImageHandle == VK_NULL_HANDLE ||
        ImgDummy2DFloat_.ImageHandle == VK_NULL_HANDLE || ImgDummy2DInt_.ImageHandle == VK_NULL_HANDLE)
    {
        ReleaseGpuResources();
        return false;
    }

    // One-shot Vulkan 1.2 layout transition (UNDEFINED -> GENERAL) & zero-clear for all 12 GPU images before first use.
    if (VkCommandBuffer initCmd = Interchange_->BeginSingleTimeCommands(); initCmd != VK_NULL_HANDLE)
    {
        const std::array<VkImage, 12> allImages {
            ImgVelocity_[0].ImageHandle, ImgVelocity_[1].ImageHandle,
            ImgState_[0].ImageHandle,    ImgState_[1].ImageHandle,
            ImgPressure_[0].ImageHandle, ImgPressure_[1].ImageHandle,
            ImgCurl_.ImageHandle,        ImgDivergence_.ImageHandle,
            ImgScratchA_.ImageHandle,    ImgScratchB_.ImageHandle,
            ImgDummy2DFloat_.ImageHandle, ImgDummy2DInt_.ImageHandle
        };

        VkImageSubresourceRange subRange {};
        subRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        subRange.baseMipLevel   = 0u;
        subRange.levelCount     = 1u;
        subRange.baseArrayLayer = 0u;
        subRange.layerCount     = 1u;

        std::array<VkImageMemoryBarrier, 12> barriers {};
        for (size_t i = 0u; i < allImages.size(); ++i)
        {
            barriers[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].srcAccessMask       = 0u;
            barriers[i].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[i].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image               = allImages[i];
            barriers[i].subresourceRange    = subRange;
        }
        vkCmdPipelineBarrier(
            initCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr,
            static_cast<uint32_t>(barriers.size()), barriers.data());

        VkClearColorValue zeroFloat {};
        for (size_t i = 0u; i < 11u; ++i)
        {
            vkCmdClearColorImage(initCmd, allImages[i], VK_IMAGE_LAYOUT_GENERAL, &zeroFloat, 1u, &subRange);
        }
        VkClearColorValue zeroInt {};
        zeroInt.int32[0] = -1;
        zeroInt.int32[1] = -1;
        zeroInt.int32[2] = -1;
        zeroInt.int32[3] = -1;
        vkCmdClearColorImage(initCmd, ImgDummy2DInt_.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, &zeroInt, 1u, &subRange);

        VkMemoryBarrier postClear {};
        postClear.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        postClear.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postClear.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            initCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u, 1u, &postClear, 0u, nullptr, 0u, nullptr);

        InitCommand_.CmdBuffer      = initCmd;
        InitCommand_.TimelineSignal = Interchange_->DispatchCommandBufferAsync(initCmd);
    }

    BufForceRecords_ = Encoder_.AllocateHostWritableBuffer(
        64ull * sizeof(ForceFields::ForceModelGpuRecord), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufOccupancy_    = Encoder_.AllocateDeviceLocalBuffer(
        static_cast<uint64_t>(kMaxOccupancyFloat4s) * 16ull,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    BufTracers_[0]   = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufTracers_[1]   = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufDebrisPos_[0] = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufDebrisPos_[1] = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufDebrisVel_[0] = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufDebrisVel_[1] = Encoder_.AllocateDeviceLocalBuffer(2048ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    for (uint32_t s = 0u; s < kTripleBufferSlots; ++s)
    {
        BufInjectUbo_[s]     = Encoder_.AllocateHostWritableBuffer(sizeof(FluidInjectUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufInjectVelUbo_[s]  = Encoder_.AllocateHostWritableBuffer(sizeof(FluidInjectUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufAdvectUbo_[s]     = Encoder_.AllocateHostWritableBuffer(sizeof(FluidAdvectUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufAdvectBwdUbo_[s]  = Encoder_.AllocateHostWritableBuffer(sizeof(FluidAdvectUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufMacCormackUbo_[s] = Encoder_.AllocateHostWritableBuffer(sizeof(FluidMacCormackUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufThermoUbo_[s]     = Encoder_.AllocateHostWritableBuffer(sizeof(FluidThermodynamicsUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufForcesUbo_[s]     = Encoder_.AllocateHostWritableBuffer(sizeof(FluidForcesUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufVortUbo_[s]       = Encoder_.AllocateHostWritableBuffer(sizeof(FluidVorticityUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufDivUbo_[s]        = Encoder_.AllocateHostWritableBuffer(sizeof(FluidDivergenceUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufPressUbo_[s]      = Encoder_.AllocateHostWritableBuffer(sizeof(FluidPressureUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufGradUbo_[s]       = Encoder_.AllocateHostWritableBuffer(sizeof(FluidGradientShiftUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufTracerUbo_[s]     = Encoder_.AllocateHostWritableBuffer(192ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufDebrisUbo_[s]     = Encoder_.AllocateHostWritableBuffer(512ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    const uint64_t singleVolBytes =
        static_cast<uint64_t>(GridCells_[0]) * GridCells_[1] * GridCells_[2] * 8ull; // RGBA16F = 8B/voxel
    Telemetry_.AllocatedImageBytes = singleVolBytes * 10ull;
    return true;
}

void FluidVulkanGpuExecutor::ReleaseGpuResources() noexcept
{
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        RetireCompletedCommandBuffersNonBlocking();
        uint64_t maxPendingTick = (InitCommand_.CmdBuffer != VK_NULL_HANDLE) ? InitCommand_.TimelineSignal : 0ull;
        for (const auto& pending : InFlightCommands_)
        {
            if (pending.CmdBuffer != VK_NULL_HANDLE && pending.TimelineSignal > maxPendingTick)
            {
                maxPendingTick = pending.TimelineSignal;
            }
        }
        if (maxPendingTick > 0ull)
        {
            // Teardown-only synchronization so in-flight GPU command buffers complete before destroying resources.
            (void)Interchange_->AwaitTimelineTick(maxPendingTick);
        }
        if (InitCommand_.CmdBuffer != VK_NULL_HANDLE)
        {
            Interchange_->FreeCommandBuffer(InitCommand_.CmdBuffer);
            InitCommand_ = {};
        }
        for (auto& pending : InFlightCommands_)
        {
            if (pending.CmdBuffer != VK_NULL_HANDLE)
            {
                Interchange_->FreeCommandBuffer(pending.CmdBuffer);
                pending = {};
            }
        }
        auto relImg = [&](GeometricRaster::DeviceImageResource& img)
        {
            if (img.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(img);
        };
        relImg(ImgVelocity_[0]); relImg(ImgVelocity_[1]);
        relImg(ImgState_[0]);    relImg(ImgState_[1]);
        relImg(ImgPressure_[0]); relImg(ImgPressure_[1]);
        relImg(ImgCurl_); relImg(ImgDivergence_); relImg(ImgScratchA_); relImg(ImgScratchB_);
        relImg(ImgDummy2DFloat_); relImg(ImgDummy2DInt_);

        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        relBuf(BufForceRecords_);
        relBuf(BufOccupancy_);
        relBuf(BufTracers_[0]);   relBuf(BufTracers_[1]);
        relBuf(BufDebrisPos_[0]); relBuf(BufDebrisPos_[1]);
        relBuf(BufDebrisVel_[0]); relBuf(BufDebrisVel_[1]);
        for (uint32_t s = 0u; s < kTripleBufferSlots; ++s)
        {
            relBuf(BufInjectUbo_[s]); relBuf(BufInjectVelUbo_[s]);
            relBuf(BufAdvectUbo_[s]); relBuf(BufAdvectBwdUbo_[s]); relBuf(BufMacCormackUbo_[s]);
            relBuf(BufThermoUbo_[s]); relBuf(BufForcesUbo_[s]); relBuf(BufVortUbo_[s]);
            relBuf(BufDivUbo_[s]);    relBuf(BufPressUbo_[s]);  relBuf(BufGradUbo_[s]);
            relBuf(BufTracerUbo_[s]); relBuf(BufDebrisUbo_[s]);
        }
        if (LinearSampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(Interchange_->AccessDevice(), LinearSampler_, nullptr);
            LinearSampler_ = VK_NULL_HANDLE;
        }
    }
    Encoder_.TeardownEncoder();
    ProgInject_ = ProgAdvect_ = ProgMacCormack_ = ProgThermo_ = ProgForces_ = UINT32_MAX;
    ProgCurl_ = ProgVorticity_ = ProgDivergence_ = ProgPressure_ = ProgGradient_ = UINT32_MAX;
    ProgOccupancy_ = ProgTracerUpdate_ = ProgDebrisUpdate_ = UINT32_MAX;
    GpuLiveReady_ = false;
}

void FluidVulkanGpuExecutor::DispatchGpuAsync(
    const FluidStepSettings& settings,
    std::span<const FluidReleaseSourceParameters> sources,
    std::span<const Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    std::span<const FluidColliderSphere> colliders,
    float deltaTimeSeconds) noexcept
{
    using namespace GeometricRaster;
    RetireCompletedCommandBuffersNonBlocking();

    const uint32_t slot = Telemetry_.ActiveRingSlot;
    // Triple-buffer ring guard: only wait if CPU wrapped 3 full frames ahead of GPU on this slot.
    if (InFlightCommands_[slot].CmdBuffer != VK_NULL_HANDLE)
    {
        if (InFlightCommands_[slot].TimelineSignal > 0ull)
        {
            (void)Interchange_->AwaitTimelineTick(InFlightCommands_[slot].TimelineSignal);
        }
        Interchange_->FreeCommandBuffer(InFlightCommands_[slot].CmdBuffer);
        InFlightCommands_[slot] = {};
    }
    const uint32_t gx = (GridCells_[0] + 7u) / 8u;
    const uint32_t gy = (GridCells_[1] + 7u) / 8u;
    const uint32_t gz = (GridCells_[2] + 3u) / 4u;

    FluidVolumeUniformBlock vol {};
    vol.GridBoundary       = { static_cast<int32_t>(GridCells_[0]), static_cast<int32_t>(GridCells_[1]), static_cast<int32_t>(GridCells_[2]), static_cast<int32_t>(settings.Boundary) };
    vol.GridCentreCellSize = { CentreMeters_[0], CentreMeters_[1], CentreMeters_[2], CellSizeMeters_ };
    vol.LookupOriginCount[3] = static_cast<int32_t>(std::min<size_t>(colliders.size(), 4u));
    for (size_t i = 0u; i < std::min<size_t>(colliders.size(), 4u); ++i)
    {
        vol.Colliders[i] = { colliders[i].CentreMeters[0], colliders[i].CentreMeters[1], colliders[i].CentreMeters[2], colliders[i].RadiusMeters };
    }

    if (!forceRecords.empty() && BufForceRecords_.HostAddress != nullptr)
    {
        const size_t n = std::min<size_t>(forceRecords.size(), 64u);
        std::memcpy(BufForceRecords_.HostAddress, forceRecords.data(), n * sizeof(ForceFields::ForceModelGpuRecord));
    }

    FluidInjectUniformBlock injectStateU {};
    injectStateU.Volume       = vol;
    injectStateU.InjectTiming = { 1.0f, static_cast<float>(std::min<size_t>(sources.size(), 6u)), deltaTimeSeconds, static_cast<float>(SimTime_) };
    for (size_t i = 0u; i < std::min<size_t>(sources.size(), 6u); ++i)
    {
        injectStateU.SourcePosRadius[i]     = { sources[i].PositionMeters[0], sources[i].PositionMeters[1], sources[i].PositionMeters[2], sources[i].RadiusMeters };
        injectStateU.SourceVelDensity[i]    = { sources[i].VelocityMps[0], sources[i].VelocityMps[1], sources[i].VelocityMps[2], sources[i].Density };
        injectStateU.SourceTempFuelShape[i] = { sources[i].Temperature, sources[i].Fuel, sources[i].Jitter, static_cast<float>(sources[i].Shape) };
        injectStateU.SourceOrientPulse[i]   = { sources[i].Orientation[0], sources[i].Orientation[1], sources[i].Orientation[2], sources[i].PulseSeconds };
    }
    if (BufInjectUbo_[slot].HostAddress) std::memcpy(BufInjectUbo_[slot].HostAddress, &injectStateU, sizeof(injectStateU));

    FluidInjectUniformBlock injectVelU = injectStateU;
    injectVelU.InjectTiming[0] = 0.0f; // mode 0 = velocity injection
    if (BufInjectVelUbo_[slot].HostAddress) std::memcpy(BufInjectVelUbo_[slot].HostAddress, &injectVelU, sizeof(injectVelU));

    FluidAdvectUniformBlock advU {};
    advU.Volume       = vol;
    advU.AdvectParams = { deltaTimeSeconds, settings.Dissipation, 1.0f, 0.0f };
    if (BufAdvectUbo_[slot].HostAddress) std::memcpy(BufAdvectUbo_[slot].HostAddress, &advU, sizeof(advU));

    FluidAdvectUniformBlock advBwdU {};
    advBwdU.Volume       = vol;
    advBwdU.AdvectParams = { -deltaTimeSeconds, 1.0f, 1.0f, 0.0f };
    if (BufAdvectBwdUbo_[slot].HostAddress) std::memcpy(BufAdvectBwdUbo_[slot].HostAddress, &advBwdU, sizeof(advBwdU));

    FluidMacCormackUniformBlock macU {};
    macU.Volume           = vol;
    macU.MacCormackParams = { deltaTimeSeconds, settings.Dissipation, 0.0f, 0.0f };
    if (BufMacCormackUbo_[slot].HostAddress) std::memcpy(BufMacCormackUbo_[slot].HostAddress, &macU, sizeof(macU));

    FluidThermodynamicsUniformBlock thermoU {};
    thermoU.Volume        = vol;
    thermoU.ThermoControl = { deltaTimeSeconds, settings.Cooling, settings.Combustion, settings.AmbientTemperature };
    if (BufThermoUbo_[slot].HostAddress) std::memcpy(BufThermoUbo_[slot].HostAddress, &thermoU, sizeof(thermoU));

    FluidForcesUniformBlock forcesU {};
    forcesU.Volume          = vol;
    forcesU.ForceKinematics = { deltaTimeSeconds, settings.Buoyancy, settings.Gravity, settings.AmbientTemperature };
    forcesU.ForceControl    = { settings.ForceResponse, static_cast<float>(SimTime_), static_cast<float>(std::min<size_t>(forceRecords.size(), 64u)), 0.0f };
    if (BufForcesUbo_[slot].HostAddress) std::memcpy(BufForcesUbo_[slot].HostAddress, &forcesU, sizeof(forcesU));

    FluidDivergenceUniformBlock divU {};
    divU.Volume            = vol;
    divU.DivergenceControl = { settings.Expansion, settings.FireThreshold, settings.FireSoftness, 0.0f };
    if (BufDivUbo_[slot].HostAddress) std::memcpy(BufDivUbo_[slot].HostAddress, &divU, sizeof(divU));

    FluidPressureUniformBlock pressU {};
    pressU.Volume          = vol;
    pressU.PressureControl = { 1.0f, 0.0f, 0.0f, 0.0f };
    if (BufPressUbo_[slot].HostAddress) std::memcpy(BufPressUbo_[slot].HostAddress, &pressU, sizeof(pressU));

    FluidGradientShiftUniformBlock gradU {};
    gradU.Volume = vol;
    if (BufGradUbo_[slot].HostAddress) std::memcpy(BufGradUbo_[slot].HostAddress, &gradU, sizeof(gradU));

    VkCommandBuffer cmd = Interchange_->BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return;

    auto emitBarrier = [&]()
    {
        VkMemoryBarrier mb {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0u, 1u, &mb, 0u, nullptr, 0u, nullptr);
    };

    const uint32_t srcIdx = ActivePong_;
    const uint32_t dstIdx = 1u - ActivePong_;
    uint64_t dispatches = 0ull;

    // 1a. Inject State -> State[dstIdx], 1b. Inject Velocity -> Velocity[dstIdx]
    {
        const std::array<ComputeSlotDescriptor, 3> sState {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufInjectUbo_[slot].BufferHandle, sizeof(injectStateU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgInject_, sState, gx, gy, gz)) ++dispatches;

        const std::array<ComputeSlotDescriptor, 3> sVel {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufInjectVelUbo_[slot].BufferHandle, sizeof(injectVelU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgInject_, sVel, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }
    // 2. Advect State[dstIdx] -> State[srcIdx] using Velocity[dstIdx]
    if (UseMacCormack_ && ProgMacCormack_ != UINT32_MAX)
    {
        // Selle et al. (2008) 3-pass MacCormack advection: forward (+dt) -> ScratchA, backward (-dt) -> ScratchB, correct -> State[srcIdx]
        const std::array<ComputeSlotDescriptor, 5> sFwd {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufAdvectUbo_[slot].BufferHandle, sizeof(advU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchA_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE,                  LinearSampler_, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgAdvect_, sFwd, gx, gy, gz)) ++dispatches;
        emitBarrier();

        const std::array<ComputeSlotDescriptor, 5> sBwd {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufAdvectBwdUbo_[slot].BufferHandle, sizeof(advBwdU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchA_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchB_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE,                  LinearSampler_, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgAdvect_, sBwd, gx, gy, gz)) ++dispatches;
        emitBarrier();

        const std::array<ComputeSlotDescriptor, 7> sCor {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufMacCormackUbo_[slot].BufferHandle, sizeof(macU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchA_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchB_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[srcIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE,                  LinearSampler_, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgMacCormack_, sCor, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }
    else
    {
        const std::array<ComputeSlotDescriptor, 5> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufAdvectUbo_[slot].BufferHandle, sizeof(advU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[srcIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE,                  LinearSampler_, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgAdvect_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }
    // 3. Thermodynamics: State[srcIdx] -> State[dstIdx] (final state lands in State[dstIdx])
    {
        const std::array<ComputeSlotDescriptor, 3> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufThermoUbo_[slot].BufferHandle, sizeof(thermoU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgThermo_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }
    // 4. Forces: Velocity[dstIdx] + State[dstIdx] -> Velocity[srcIdx]
    {
        const std::array<ComputeSlotDescriptor, 5> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufForcesUbo_[slot].BufferHandle, sizeof(forcesU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufForceRecords_.BufferHandle,    VK_WHOLE_SIZE },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgForces_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }
    // 5. Divergence: Velocity[srcIdx] + State[dstIdx] -> ImgDivergence_ (distinct read-only ImgScratchA_ on unused slot 5)
    {
        const std::array<ComputeSlotDescriptor, 7> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufDivUbo_[slot].BufferHandle,   sizeof(divU) },
            { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufPressUbo_[slot].BufferHandle, sizeof(pressU) },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgPressure_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgScratchA_.ViewHandle,         VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgDivergence_.ViewHandle,       VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgDivergence_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }

    // 6. Jacobi Pressure iterations
    uint32_t pSrc = srcIdx;
    const uint32_t jacobiIters = std::clamp(settings.PressureIterations, 2u, 48u);
    for (uint32_t iter = 0u; iter < jacobiIters; ++iter)
    {
        const uint32_t pDst = 1u - pSrc;
        const std::array<ComputeSlotDescriptor, 7> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufDivUbo_[slot].BufferHandle,   sizeof(divU) },
            { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufPressUbo_[slot].BufferHandle, sizeof(pressU) },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgState_[dstIdx].ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgPressure_[pSrc].ViewHandle,   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgDivergence_.ViewHandle,       VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgPressure_[pDst].ViewHandle,   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgPressure_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
        pSrc = pDst;
    }

    // 7. Gradient Projection: Pressure[pSrc] + Velocity[srcIdx] -> Velocity[dstIdx] (aligns with State[dstIdx]!)
    {
        const std::array<ComputeSlotDescriptor, 8> s {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufGradUbo_[slot].BufferHandle, sizeof(gradU) },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgPressure_[pSrc].ViewHandle,   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[srcIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgDummy2DFloat_.ViewHandle,     VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgDummy2DFloat_.ViewHandle,     VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgDummy2DInt_.ViewHandle,       VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufOccupancy_.BufferHandle,     VK_WHOLE_SIZE }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgGradient_, s, gx, gy, gz)) ++dispatches;
        emitBarrier();
    }

    if (ProgTracerUpdate_ != UINT32_MAX || ProgDebrisUpdate_ != UINT32_MAX)
    {
        struct alignas(16) TracerUboPod
        {
            FluidVolumeUniformBlock Volume;
            float                   TracerTiming[4];
            int32_t                 TracerDims[4];
        } tUbo { vol, { deltaTimeSeconds, static_cast<float>(SimTime_), 3.0f, Telemetry_.CompletedTicks == 0ull ? 1.0f : 0.0f }, { 64, 32, 2048, 0 } };
        if (BufTracerUbo_[slot].HostAddress != nullptr) std::memcpy(BufTracerUbo_[slot].HostAddress, &tUbo, sizeof(tUbo));
        if (BufDebrisUbo_[slot].HostAddress != nullptr) std::memcpy(BufDebrisUbo_[slot].HostAddress, &vol, sizeof(vol));

        const std::array<ComputeSlotDescriptor, 10> tdSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufTracerUbo_[slot].BufferHandle,  192u },
            { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufDebrisUbo_[slot].BufferHandle,  512u },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVelocity_[dstIdx].ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE, LinearSampler_, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufTracers_[srcIdx].BufferHandle,   VK_WHOLE_SIZE },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufTracers_[dstIdx].BufferHandle,   VK_WHOLE_SIZE },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufDebrisPos_[srcIdx].BufferHandle, VK_WHOLE_SIZE },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufDebrisVel_[srcIdx].BufferHandle, VK_WHOLE_SIZE },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufDebrisPos_[dstIdx].BufferHandle, VK_WHOLE_SIZE },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufDebrisVel_[dstIdx].BufferHandle, VK_WHOLE_SIZE }
        }};
        const uint32_t tdGroups = (2048u + 63u) / 64u;
        if (ProgTracerUpdate_ != UINT32_MAX && Encoder_.EncodeComputeDispatch(cmd, ProgTracerUpdate_, tdSlots, tdGroups)) ++dispatches;
        if (ProgDebrisUpdate_ != UINT32_MAX && Encoder_.EncodeComputeDispatch(cmd, ProgDebrisUpdate_, tdSlots, tdGroups)) ++dispatches;
        emitBarrier();
    }

    ActivePong_ = dstIdx;

    const bool encodedOcc = OccupancyReadbackRing_.EncodeAsyncCopy(cmd, BufOccupancy_.BufferHandle, kMaxOccupancyFloat4s);
    const uint64_t submittedSignal = Interchange_->DispatchCommandBufferAsync(cmd);
    Telemetry_.LastSubmittedTimeline = submittedSignal;
    Telemetry_.TotalDispatches      += dispatches;
    if (encodedOcc)
    {
        OccupancyReadbackRing_.BindActiveSlotTimeline(submittedSignal);
    }

    InFlightCommands_[slot].CmdBuffer      = cmd;
    InFlightCommands_[slot].TimelineSignal = submittedSignal;
}
#endif

FluidGpuVolumeFeedHandle FluidVulkanGpuExecutor::InspectVolumeGpuFeed() const noexcept
{
    FluidGpuVolumeFeedHandle feed {};
    feed.Route                 = Telemetry_.Route;
    feed.GridCells             = GridCells_;
    feed.CentreMeters          = CentreMeters_;
    feed.CellSizeMeters        = CellSizeMeters_;
    feed.LastSubmittedTimeline = Telemetry_.LastSubmittedTimeline;
    feed.GpuResident           = GpuLiveReady_ && Telemetry_.RealGpuDispatched;
#if FRONTIER_FLUID_HAS_VULKAN
    if (GpuLiveReady_)
    {
        feed.VelocityViewHandle = reinterpret_cast<uint64_t>(ImgVelocity_[ActivePong_].ViewHandle);
        feed.StateViewHandle    = reinterpret_cast<uint64_t>(ImgState_[ActivePong_].ViewHandle);
        feed.PressureViewHandle = reinterpret_cast<uint64_t>(ImgPressure_[0].ViewHandle);
        feed.TracerBufferHandle = reinterpret_cast<uint64_t>(BufTracers_[ActivePong_].BufferHandle);
    }
#endif
    return feed;
}

} // namespace Frontier::SimulationSpace::Fluid
