/*============================================================================================================================================
                                                  FLUIDSIMULATIONPOSTOVERLAY.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Screen-space simulation overlay packing & CPU mirror of SimulationPostOverlay.slang
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js PostOverlayFragment (L1758-L1909) & VolumeRenderer.js L72-L176

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidSimulationPostOverlay.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

[[nodiscard]] float ClampF(const float v, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(hi, v));
}

[[nodiscard]] float LerpF(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] float FractF(const float x) noexcept
{
    return x - std::floor(x);
}

[[nodiscard]] float SmoothStepF(const float e0, const float e1, const float x) noexcept
{
    const float d = e1 - e0;
    const float t = (std::fabs(d) > 1.0e-9f) ? ClampF((x - e0) / d, 0.0f, 1.0f) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float Hash21Overlay(const float px, const float py) noexcept
{
    return FractF(std::sin(px * 127.1f + py * 311.7f) * 43758.5453123f);
}

[[nodiscard]] float SmoothNoise2DOverlay(const float px, const float py) noexcept
{
    const float ix = std::floor(px);
    const float iy = std::floor(py);
    const float fx = px - ix;
    const float fy = py - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = LerpF(Hash21Overlay(ix, iy),        Hash21Overlay(ix + 1.0f, iy),        ux);
    const float b = LerpF(Hash21Overlay(ix, iy + 1.0f), Hash21Overlay(ix + 1.0f, iy + 1.0f), ux);
    return LerpF(a, b, uy);
}

[[nodiscard]] std::array<float, 4> SampleBilinearRgba(
    const std::span<const float> img,
    const uint32_t width,
    const uint32_t height,
    const float u,
    const float v) noexcept
{
    const float fx = ClampF(u * static_cast<float>(width) - 0.5f, 0.0f, static_cast<float>(width - 1u));
    const float fy = ClampF(v * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    const uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, height - 1u);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    auto fetch = [&](const uint32_t x, const uint32_t y, const size_t c) -> float {
        return img[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u + c];
    };
    std::array<float, 4> out {};
    for (size_t c = 0u; c < 4u; ++c)
    {
        const float top = LerpF(fetch(x0, y0, c), fetch(x1, y0, c), tx);
        const float bot = LerpF(fetch(x0, y1, c), fetch(x1, y1, c), tx);
        out[c] = LerpF(top, bot, ty);
    }
    return out;
}

} // namespace

bool HasActiveSimulationPostOverlay(
    const FluidShimmerInput& shimmer,
    const std::span<const ParticleOverlayEffectInput> particleEffects) noexcept
{
    if (shimmer.Strength > 0.0005f) return true;
    for (const auto& node : particleEffects)
    {
        if (!node.Visible || !node.Enabled) continue;
        if (node.DistortionMode != DistortionLensModeOrdinal::Off && node.DistortionStrength > 0.0005f) return true;
        if (node.GlitchAmount > 0.001f && (node.GlitchScanlines > 0.001f || node.GlitchChromatic > 0.001f)) return true;
    }
    return false;
}

ProjectedScreenSphere ProjectWorldSphereToScreenUv(
    const std::array<float, 3>& worldPos,
    const float worldRadius,
    const FluidRenderCamera& camera) noexcept
{
    const auto& m = camera.ViewProjection;
    const float x = worldPos[0], y = worldPos[1], z = worldPos[2];
    const float cx = m[0] * x + m[4] * y + m[8]  * z + m[12];
    const float cy = m[1] * x + m[5] * y + m[9]  * z + m[13];
    const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (cw <= 0.05f) return {};

    const float uvX = (cx / cw) * 0.5f + 0.5f;
    const float uvY = (cy / cw) * 0.5f + 0.5f;
    const float dx = worldPos[0] - camera.PositionMeters[0];
    const float dy = worldPos[1] - camera.PositionMeters[1];
    const float dz = worldPos[2] - camera.PositionMeters[2];
    const float dist = std::max(0.25f, std::sqrt(dx * dx + dy * dy + dz * dz));
    const float screenRadius = ClampF(worldRadius / (2.0f * dist * std::tan(camera.FovYRadians * 0.5f)), 0.03f, 1.2f);
    return { uvX, uvY, screenRadius, true };
}

SimulationPostOverlayUniformBlock BuildSimulationPostOverlayUniforms(
    const FluidShimmerInput& shimmer,
    const std::span<const ParticleOverlayEffectInput> particleEffects,
    const FluidRenderCamera& camera,
    const float simulationTimeSeconds) noexcept
{
    SimulationPostOverlayUniformBlock u {};
    u.ViewportTime = {
        static_cast<float>(std::max(1u, camera.ViewportWidth)),
        static_cast<float>(std::max(1u, camera.ViewportHeight)),
        simulationTimeSeconds,
        0.0f
    };

    if (shimmer.Strength > 0.0005f)
    {
        const auto proj = ProjectWorldSphereToScreenUv(shimmer.WorldCentreMeters, shimmer.WorldRadiusMeters * 1.15f, camera);
        u.ShimmerArea = proj.Valid
            ? std::array<float, 4>{ proj.UvX, proj.UvY, proj.ScreenRadius, shimmer.Strength }
            : std::array<float, 4>{ 0.5f, 0.5f, 0.45f, shimmer.Strength };
        u.ShimmerParams = { shimmer.Frequency, shimmer.Speed, shimmer.Chromatic, 0.0f };
    }
    else
    {
        u.ShimmerArea   = { 0.5f, 0.5f, 0.35f, 0.0f };
        u.ShimmerParams = { 14.0f, 2.2f, 0.0f, 0.0f };
    }

    int32_t lensCount   = 0;
    int32_t glitchCount = 0;
    for (const auto& node : particleEffects)
    {
        if (!node.Visible || !node.Enabled) continue;
        const auto proj = ProjectWorldSphereToScreenUv(node.PositionMeters, std::max(0.25f, node.RadiusMeters * 1.35f), camera);
        if (!proj.Valid) continue;

        const uint32_t dMode = static_cast<uint32_t>(node.DistortionMode);
        if (dMode > 0u && node.DistortionStrength > 0.0005f && lensCount < static_cast<int32_t>(kMaximumOverlayEffects))
        {
            const float scale   = std::min(1.6f, proj.ScreenRadius * 3.2f);
            const float lensRad = std::max(0.03f, node.DistortionRadius * scale);
            const float horizon = node.DistortionHorizon * scale;
            u.LensA[static_cast<size_t>(lensCount)] = { proj.UvX, proj.UvY, node.DistortionStrength, lensRad };
            u.LensB[static_cast<size_t>(lensCount)] = { static_cast<float>(dMode), horizon, node.DistortionChromatic, 0.0f };
            ++lensCount;
        }
        if (node.GlitchAmount > 0.001f
            && (node.GlitchScanlines > 0.001f || node.GlitchChromatic > 0.001f)
            && glitchCount < static_cast<int32_t>(kMaximumOverlayEffects))
        {
            u.GlitchA[static_cast<size_t>(glitchCount)] = { proj.UvX, proj.UvY, std::max(0.08f, proj.ScreenRadius * 1.25f), node.GlitchAmount };
            u.GlitchB[static_cast<size_t>(glitchCount)] = { node.GlitchRate, node.GlitchScanlines, node.GlitchChromatic, 0.0f };
            ++glitchCount;
        }
    }

    u.EffectCounts = { lensCount, glitchCount, 0, 0 };
    return u;
}

void ApplySimulationPostOverlayCpu(
    const SimulationPostOverlayUniformBlock& u,
    const uint32_t width,
    const uint32_t height,
    const std::span<const float> sceneRgba32f,
    std::vector<float>& outRgba32f)
{
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    outRgba32f.resize(pixelCount * 4u);
    if (width == 0u || height == 0u || sceneRgba32f.size() < pixelCount * 4u) return;

    const float vpW     = std::max(static_cast<float>(width), 1.0f);
    const float vpH     = std::max(static_cast<float>(height), 1.0f);
    const float aspect  = vpW / vpH;
    const float simTime = u.ViewportTime[2];
    const int32_t lensCount   = std::min(u.EffectCounts[0], static_cast<int32_t>(kMaximumOverlayEffects));
    const int32_t glitchCount = std::min(u.EffectCounts[1], static_cast<int32_t>(kMaximumOverlayEffects));

    for (uint32_t py = 0u; py < height; ++py)
    {
        const float fragY = static_cast<float>(py) + 0.5f;
        const float uvY   = fragY / vpH;
        for (uint32_t px = 0u; px < width; ++px)
        {
            const float fragX = static_cast<float>(px) + 0.5f;
            const float uvX   = fragX / vpW;

            float srcX = uvX;
            float srcY = uvY;
            float insideHorizon = 0.0f;
            float photonRing    = 0.0f;
            float chromaSpread  = 0.0f;
            float chromaDirX    = uvX - 0.5f;
            float chromaDirY    = uvY - 0.5f;

            // 1. Distortion & Gravitational Lensing (Layer 218)
            for (int32_t k = 0; k < lensCount; ++k)
            {
                const float cx         = u.LensA[static_cast<size_t>(k)][0];
                const float cy         = u.LensA[static_cast<size_t>(k)][1];
                const float strength   = u.LensA[static_cast<size_t>(k)][2];
                const float rad        = std::max(u.LensA[static_cast<size_t>(k)][3], 0.02f);
                const int32_t mode     = static_cast<int32_t>(std::floor(u.LensB[static_cast<size_t>(k)][0] + 0.5f));
                const float horizon    = u.LensB[static_cast<size_t>(k)][1];
                const float lensChroma = u.LensB[static_cast<size_t>(k)][2];

                const float dx  = (uvX - cx) * aspect;
                const float dy  = (uvY - cy);
                const float r   = std::sqrt(dx * dx + dy * dy);
                const float dirX = (r > 1.0e-5f) ? (dx / r) : 1.0f;
                const float dirY = (r > 1.0e-5f) ? (dy / r) : 0.0f;

                if (mode == 1)
                {
                    const float einstein = rad * (0.22f + 0.65f * strength);
                    const float rs       = r - (einstein * einstein) / std::max(r, 1.0e-4f);
                    const float taper    = 1.0f - SmoothStepF(rad * 0.65f, rad * 1.45f, r);
                    const float rr       = LerpF(r, rs, taper);
                    srcX += (cx + dirX * rr / aspect - uvX);
                    srcY += (cy + dirY * rr - uvY);
                    if (horizon > 0.001f)
                    {
                        insideHorizon = std::max(insideHorizon, 1.0f - SmoothStepF(horizon * 0.88f, horizon, r));
                        const float q = (r - 1.28f * horizon) / std::max(0.14f * horizon, 1.0e-4f);
                        photonRing += std::exp(-q * q) * (0.55f + 0.85f * strength);
                    }
                    chromaSpread = std::max(chromaSpread, lensChroma * taper * 0.014f);
                    if (taper > 0.01f) { chromaDirX = dirX; chromaDirY = dirY; }
                }
                else if (mode == 2)
                {
                    const float fall = 1.0f - SmoothStepF(rad * 0.35f, rad * 1.15f, r);
                    const float wave = std::sin(r / rad * 22.0f - simTime * 6.5f);
                    srcX += (dirX / aspect) * (wave * 0.024f * strength * fall);
                    srcY += dirY * (wave * 0.024f * strength * fall);
                    chromaSpread = std::max(chromaSpread, lensChroma * fall * std::fabs(wave) * 0.012f);
                    if (fall > 0.01f) { chromaDirX = dirX; chromaDirY = dirY; }
                }
                else if (mode == 3)
                {
                    const float fall = 1.0f - SmoothStepF(rad * 0.25f, rad, r);
                    const float npx = uvX * 12.0f + simTime * 0.8f;
                    const float npy = uvY * 12.0f - simTime * 1.4f;
                    const float wx = SmoothNoise2DOverlay(npx, npy) - 0.5f;
                    const float wy = SmoothNoise2DOverlay(npx + 5.2f, npy + 1.7f) - 0.5f;
                    srcX += wx * (0.032f * strength * fall);
                    srcY += wy * (0.032f * strength * fall);
                    chromaSpread = std::max(chromaSpread, lensChroma * fall * 0.010f);
                }
            }

            // 2. Gas Volume Heat Shimmer (Layer 109)
            if (u.ShimmerArea[3] > 0.0005f)
            {
                const float sdx   = (uvX - u.ShimmerArea[0]) * aspect;
                const float sdy   = (uvY - u.ShimmerArea[1]) * 0.72f;
                const float sr    = std::sqrt(sdx * sdx + sdy * sdy) / std::max(u.ShimmerArea[2], 0.02f);
                const float sFall = 1.0f - SmoothStepF(0.45f, 1.05f, sr);
                if (sFall > 0.001f)
                {
                    const float freq = u.ShimmerParams[0];
                    const float rise = simTime * u.ShimmerParams[1];
                    const float hpx  = uvX * freq;
                    const float hpy  = uvY * freq - rise * 1.65f;
                    const float n1x  = SmoothNoise2DOverlay(hpx, hpy) - 0.5f;
                    const float n1y  = SmoothNoise2DOverlay(hpx + 5.2f, hpy + 1.3f) - 0.5f;
                    const float n2x  = SmoothNoise2DOverlay(hpx * 2.15f + 1.7f, hpy * 2.15f - rise * 0.9f) - 0.5f;
                    const float n2y  = SmoothNoise2DOverlay(hpx * 2.15f + 8.4f, hpy * 2.15f + 3.1f) - 0.5f;
                    const float amp  = 0.026f * u.ShimmerArea[3] * sFall;
                    srcX += (n1x * 0.68f + n2x * 0.32f) * amp;
                    srcY += (n1y * 0.68f + n2y * 0.32f) * amp;
                    chromaSpread = std::max(chromaSpread, u.ShimmerParams[2] * sFall * u.ShimmerArea[3] * 0.009f);
                }
            }

            // 3. Localized Screen-Space Glitch (Layer 217)
            float scanMod = 1.0f;
            for (int32_t g = 0; g < glitchCount; ++g)
            {
                const float gdx  = (uvX - u.GlitchA[static_cast<size_t>(g)][0]) * aspect;
                const float gdy  = (uvY - u.GlitchA[static_cast<size_t>(g)][1]);
                const float gd   = std::sqrt(gdx * gdx + gdy * gdy) / std::max(u.GlitchA[static_cast<size_t>(g)][2], 0.02f);
                const float mask = 1.0f - SmoothStepF(0.50f, 1.05f, gd);
                const float amt  = u.GlitchA[static_cast<size_t>(g)][3] * mask;
                if (amt > 0.001f)
                {
                    const float stepT = std::floor(simTime * std::max(u.GlitchB[static_cast<size_t>(g)][0], 1.0f));
                    const float band  = std::floor(uvY * 36.0f);
                    const float gate  = (Hash21Overlay(band, stepT + static_cast<float>(g) * 19.0f) >= (0.74f - amt * 0.50f)) ? 1.0f : 0.0f;
                    const float shift = (Hash21Overlay(band + 17.0f, stepT * 1.3f) - 0.5f) * 0.06f * amt * u.GlitchB[static_cast<size_t>(g)][1] * gate;
                    srcX += shift;
                    scanMod *= LerpF(1.0f, 0.84f + 0.16f * std::sin(fragY * 1.8f), ClampF(amt * u.GlitchB[static_cast<size_t>(g)][1], 0.0f, 1.0f));
                    chromaSpread = std::max(chromaSpread, amt * u.GlitchB[static_cast<size_t>(g)][2] * 0.014f);
                    chromaDirX = 1.0f;
                    chromaDirY = 0.0f;
                }
            }

            const float cSrcX = ClampF(srcX, 0.001f, 0.999f);
            const float cSrcY = ClampF(srcY, 0.001f, 0.999f);
            const auto sampled = SampleBilinearRgba(sceneRgba32f, width, height, cSrcX, cSrcY);
            float colR = sampled[0], colG = sampled[1], colB = sampled[2];

            if (chromaSpread > 0.0002f)
            {
                const float ox = chromaDirX + 1.0e-5f;
                const float oy = chromaDirY;
                const float olen = std::max(std::sqrt(ox * ox + oy * oy), 1.0e-6f);
                const float offX = (ox / olen) * chromaSpread;
                const float offY = (oy / olen) * chromaSpread;
                colR = SampleBilinearRgba(sceneRgba32f, width, height, ClampF(cSrcX + offX, 0.001f, 0.999f), ClampF(cSrcY + offY, 0.001f, 0.999f))[0];
                colB = SampleBilinearRgba(sceneRgba32f, width, height, ClampF(cSrcX - offX, 0.001f, 0.999f), ClampF(cSrcY - offY, 0.001f, 0.999f))[2];
            }

            const float dim = (1.0f - insideHorizon) * scanMod;
            colR = colR * dim + 1.00f * photonRing;
            colG = colG * dim + 0.86f * photonRing;
            colB = colB * dim + 0.62f * photonRing;

            const size_t idx = (static_cast<size_t>(py) * static_cast<size_t>(width) + static_cast<size_t>(px)) * 4u;
            outRgba32f[idx + 0u] = colR;
            outRgba32f[idx + 1u] = colG;
            outRgba32f[idx + 2u] = colB;
            outRgba32f[idx + 3u] = sampled[3];
        }
    }
}

} // namespace Frontier::SimulationSpace::Fluid
