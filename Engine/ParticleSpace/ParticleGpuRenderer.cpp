/*============================================================================================================================================
                                                       PARTICLEGPURENDERER.CPP
============================================================================================================================================*/
// 🧩 ParticleSpace GPU sprite/streak uniform builder, draw-batch planner & bit-accurate CPU mirror of ParticleShadingCommon + 18 cutouts
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L753-L966, L1088-L1340 & ParticleSimulation.js L340-L655

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleGpuRenderer.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Frontier::ParticleSpace
{

namespace
{

using Vec3f = std::array<float, 3>;

constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

[[nodiscard]] float ClampF(const float x, const float lo, const float hi) noexcept { return std::clamp(x, lo, hi); }
[[nodiscard]] float FractF(const float x) noexcept { return x - std::floor(x); }
[[nodiscard]] float LerpF(const float a, const float b, const float t) noexcept { return a + (b - a) * t; }
[[nodiscard]] float StepF(const float edge, const float x) noexcept { return (x < edge) ? 0.0f : 1.0f; }
[[nodiscard]] float SmoothStepF(const float edge0, const float edge1, const float x) noexcept
{
    const float denom = edge1 - edge0;
    const float t = (std::fabs(denom) > 1.0e-9f) ? ClampF((x - edge0) / denom, 0.0f, 1.0f) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] Vec3f Add3(const Vec3f& a, const Vec3f& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] Vec3f Mul3(const Vec3f& a, const float s) noexcept { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] Vec3f Lerp3(const Vec3f& a, const Vec3f& b, const float t) noexcept
{
    return { LerpF(a[0], b[0], t), LerpF(a[1], b[1], t), LerpF(a[2], b[2], t) };
}
[[nodiscard]] float Dot3(const Vec3f& a, const Vec3f& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] Vec3f Cross3(const Vec3f& a, const Vec3f& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] float Dist3(const Vec3f& a, const Vec3f& b) noexcept
{
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] std::array<float, 4> ParseHexSwatch(const std::string_view hex) noexcept
{
    std::string_view digits = hex;
    if (!digits.empty() && digits.front() == '#') digits.remove_prefix(1u);
    if (digits.size() != 6u) return { 1.0f, 1.0f, 1.0f, 0.0f };
    char buf[8] {};
    for (size_t i = 0u; i < 6u; ++i) buf[i] = digits[i];
    const unsigned long val = std::strtoul(buf, nullptr, 16);
    return {
        static_cast<float>((val >> 16u) & 255ul) / 255.0f,
        static_cast<float>((val >> 8u) & 255ul) / 255.0f,
        static_cast<float>(val & 255ul) / 255.0f,
        0.0f
    };
}

[[nodiscard]] Vec3f HueToRgb(const float h) noexcept
{
    auto ch = [h](const float offset) noexcept -> float {
        const float raw = offset + h * 6.0f;
        const float k = raw - 6.0f * std::floor(raw / 6.0f);
        return 1.0f - ClampF(std::min(k, 4.0f - k), 0.0f, 1.0f) * 0.85f;
    };
    return { ch(5.0f), ch(3.0f), ch(1.0f) };
}

[[nodiscard]] Vec3f SampleSwatchGradient(const ParticleShadeUniformBlock& shade, const float t) noexcept
{
    const int32_t count = std::max(1, static_cast<int32_t>(shade.ModeRampSwatch[3] + 0.5f));
    if (count == 1) return { shade.SwatchStops[0][0], shade.SwatchStops[0][1], shade.SwatchStops[0][2] };
    const float x = ClampF(t, 0.0f, 1.0f) * static_cast<float>(count - 1);
    const int32_t i = std::min(static_cast<int32_t>(std::floor(x)), count - 2);
    const Vec3f a { shade.SwatchStops[i][0], shade.SwatchStops[i][1], shade.SwatchStops[i][2] };
    const Vec3f b { shade.SwatchStops[i + 1][0], shade.SwatchStops[i + 1][1], shade.SwatchStops[i + 1][2] };
    return Lerp3(a, b, x - static_cast<float>(i));
}

[[nodiscard]] Vec3f SampleSwatchEntry(const ParticleShadeUniformBlock& shade, const float seed) noexcept
{
    const int32_t count = std::max(1, static_cast<int32_t>(shade.ModeRampSwatch[3] + 0.5f));
    const int32_t i = std::clamp(static_cast<int32_t>(std::floor(seed * static_cast<float>(count))), 0, count - 1);
    return { shade.SwatchStops[i][0], shade.SwatchStops[i][1], shade.SwatchStops[i][2] };
}

[[nodiscard]] Vec3f SampleColourGradient(const ParticleShadeUniformBlock& shade, const float t) noexcept
{
    const int32_t rampSource = static_cast<int32_t>(shade.ModeRampSwatch[2] + 0.5f);
    const uint32_t rampMode  = static_cast<uint32_t>(std::max(0, static_cast<int32_t>(shade.ModeRampSwatch[1] + 0.5f)));
    return (rampSource == 1) ? EvaluateAnalyticRampCpu(rampMode, t) : SampleSwatchGradient(shade, t);
}

[[nodiscard]] Vec3f HueRotate(const Vec3f& colour, const float angle) noexcept
{
    constexpr Vec3f k { 0.57735f, 0.57735f, 0.57735f };
    const float c = std::cos(angle), s = std::sin(angle);
    return Add3(Add3(Mul3(colour, c), Mul3(Cross3(k, colour), s)), Mul3(k, Dot3(k, colour) * (1.0f - c)));
}

[[nodiscard]] Vec3f FetchAnchorNormal(std::span<const float> anchorRgba, const int32_t anchorCount, const int32_t index) noexcept
{
    if (anchorCount < 1 || anchorRgba.size() < 8u) return { 0.0f, 1.0f, 0.0f };
    const int32_t slot = std::clamp(index, 0, anchorCount - 1);
    const size_t base = static_cast<size_t>(slot) * 8u;
    if (base + 7u >= anchorRgba.size()) return { 0.0f, 1.0f, 0.0f };
    return { anchorRgba[base + 4u], anchorRgba[base + 5u], anchorRgba[base + 6u] };
}

[[nodiscard]] Vec3f Normalise3(const Vec3f& v) noexcept
{
    const float len = std::sqrt(Dot3(v, v));
    return (len > 1.0e-7f) ? Mul3(v, 1.0f / len) : Vec3f { 0.0f, 1.0f, 0.0f };
}

[[nodiscard]] float Hash31F(const Vec3f& p) noexcept
{
    const float h = std::sin(p[0] * 127.1f + p[1] * 311.7f + p[2] * 74.7f) * 43758.5453123f;
    return FractF(h);
}

void ComputeShapeBasis3(const Vec3f& axis, Vec3f& outT, Vec3f& outB) noexcept
{
    const Vec3f helper = (std::fabs(axis[1]) < 0.95f) ? Vec3f { 0.0f, 1.0f, 0.0f } : Vec3f { 1.0f, 0.0f, 0.0f };
    outT = Normalise3(Cross3(helper, axis));
    outB = Cross3(axis, outT);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               RAMP & UNIFORM BUILDERS
//------------------------------------------------------------------------------------------------------------------------

uint32_t ResolveAnalyticRampOrdinal(const std::string_view rampName) noexcept
{
    if (rampName == "Engineering") return 1u;
    if (rampName == "Plasma")      return 2u;
    if (rampName == "Diverging")   return 3u;
    return 0u; // Turbo
}

ParticleShadeUniformBlock BuildParticleShadeUniforms(
    const ParticleCloudParameters& params,
    const float simulationTimeSeconds,
    const float regionHeightMeters) noexcept
{
    ParticleShadeUniformBlock block {};
    const size_t stopCount = params.ColourStops.empty() ? 1u : std::min<size_t>(params.ColourStops.size(), 8u);
    if (params.ColourStops.empty())
    {
        block.SwatchStops[0] = { 1.0f, 1.0f, 1.0f, 0.0f };
    }
    else
    {
        for (size_t i = 0u; i < stopCount; ++i)
        {
            block.SwatchStops[i] = ParseHexSwatch(params.ColourStops[i]);
        }
    }

    block.ModeRampSwatch = {
        static_cast<float>(params.ColourMode),
        static_cast<float>(ResolveAnalyticRampOrdinal(params.RampName)),
        static_cast<float>(params.RampSource),
        static_cast<float>(stopCount)
    };
    block.ColourJitterCycle = {
        params.HueJitter,
        params.BrightnessJitter,
        params.CycleSpeed,
        params.Brightness
    };
    block.LifeRangeFlicker = {
        std::max(0.05f, params.Lifetime),
        params.Linger,
        params.MaxSpeed,
        params.DisplacementRange
    };
    block.TimingRegionHeight = {
        params.Flicker,
        simulationTimeSeconds,
        std::max(1.0e-3f, regionHeightMeters),
        0.0f
    };
    return block;
}

ParticleSpriteUniformBlock BuildParticleSpriteUniforms(
    const ParticleCloudParameters& params,
    const uint32_t textureSide,
    const uint32_t anchorWidth,
    const uint32_t anchorCount,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters,
    const float haloScale,
    const float haloAlpha,
    const int32_t spriteOverride) noexcept
{
    ParticleSpriteUniformBlock block {};
    block.Shape = BuildParticleShapeUniforms(params, textureSide, anchorWidth, anchorCount);
    block.Shade = BuildParticleShadeUniforms(params, simulationTimeSeconds, regionHeightMeters);
    block.ViewProjection = camera.ViewProjection;

    const float viewW = std::max(1.0f, camera.ViewportWidthPx * camera.PixelRatio);
    const float viewH = std::max(1.0f, camera.ViewportHeightPx * camera.PixelRatio);
    const float focalPixels = viewH / (2.0f * std::tan(std::max(0.05f, camera.VerticalFovRad) * 0.5f));

    block.CameraPositionFocal    = { camera.CameraPosition[0], camera.CameraPosition[1], camera.CameraPosition[2], focalPixels };
    block.CameraForwardViewportX = { camera.CameraForward[0], camera.CameraForward[1], camera.CameraForward[2], viewW };
    block.ViewportYSizeParams    = { viewH, params.SizeStart, params.SizeEnd, params.SizeRandom };
    block.PixelDofParams         = { camera.PixelRatio, static_cast<float>(params.SizeMode), params.FocusDistance, params.Aperture };

    const float spriteVal = (spriteOverride >= 0) ? static_cast<float>(spriteOverride) : static_cast<float>(params.Sprite);
    block.HaloSpriteStyleTrail   = { haloScale, haloAlpha, spriteVal, static_cast<float>(params.Style) };
    return block;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ANALYTIC COLOUR RAMPS (CPU)
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> EvaluateAnalyticRampCpu(const uint32_t rampMode, float t) noexcept
{
    t = ClampF(t, 0.0f, 1.0f);
    if (rampMode == 1u)
    {
        static constexpr std::array<Vec3f, 7u> kStops {{
            { 0.043f, 0.122f, 0.227f }, { 0.071f, 0.318f, 0.541f }, { 0.102f, 0.627f, 0.784f },
            { 0.373f, 0.816f, 0.627f }, { 0.898f, 0.890f, 0.416f }, { 0.949f, 0.545f, 0.176f },
            { 0.851f, 0.157f, 0.110f }
        }};
        const float u = t * 6.0f;
        const int32_t i = std::min(static_cast<int32_t>(std::floor(u)), 5);
        return Lerp3(kStops[i], kStops[i + 1], FractF(u));
    }
    if (rampMode == 2u)
    {
        const Vec3f c0 { 0.0509f, 0.0298f, 0.5279f };
        const Vec3f c1 { 2.1763f, 0.2383f, 0.7539f };
        const Vec3f c2 { -2.6895f, -7.4559f, 3.1107f };
        const Vec3f c3 { 6.1303f, 42.3461f, -28.5188f };
        const Vec3f c4 { -11.1074f, -82.6663f, 60.1398f };
        const Vec3f c5 { 10.0231f, 71.4136f, -54.0721f };
        const Vec3f c6 { -3.6587f, -22.9315f, 18.1919f };
        Vec3f acc = c6;
        acc = Add3(c5, Mul3(acc, t));
        acc = Add3(c4, Mul3(acc, t));
        acc = Add3(c3, Mul3(acc, t));
        acc = Add3(c2, Mul3(acc, t));
        acc = Add3(c1, Mul3(acc, t));
        acc = Add3(c0, Mul3(acc, t));
        return acc;
    }
    if (rampMode == 3u)
    {
        static constexpr std::array<Vec3f, 7u> kStops {{
            { 0.129f, 0.400f, 0.675f }, { 0.404f, 0.663f, 0.812f }, { 0.820f, 0.898f, 0.941f },
            { 0.969f, 0.969f, 0.969f }, { 0.992f, 0.859f, 0.780f }, { 0.937f, 0.541f, 0.384f },
            { 0.698f, 0.094f, 0.169f }
        }};
        const float u = t * 6.0f;
        const int32_t i = std::min(static_cast<int32_t>(std::floor(u)), 5);
        return Lerp3(kStops[i], kStops[i + 1], FractF(u));
    }

    // Turbo (mode 0)
    const float t2 = t * t, t3 = t2 * t, t4 = t2 * t2, t5 = t3 * t2;
    const float r = 0.13572138f + 4.61539260f * t - 42.66032258f * t2 + 132.13108234f * t3 - 152.94239396f * t4 + 59.28637943f * t5;
    const float g = 0.09140261f + 2.19418839f * t +  4.84296658f * t2 -  14.18503333f * t3 +   4.27729857f * t4 +  2.82956604f * t5;
    const float b = 0.10667330f + 12.64194608f * t - 60.58204836f * t2 + 110.36276771f * t3 -  89.90310912f * t4 + 27.34824973f * t5;
    return { r, g, b };
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PER-PARTICLE SHADING (CPU)
//------------------------------------------------------------------------------------------------------------------------

ParticleShadeCpuResult EvaluateParticleShadeCpu(
    const ParticleShapeUniformBlock& shape,
    const ParticleShadeUniformBlock& shade,
    std::span<const float> anchorTexelRgba,
    const int32_t particleIndex,
    const std::array<float, 4>& currentPositionAge,
    const std::array<float, 4>& velocitySeed) noexcept
{
    ParticleShadeCpuResult res {};
    const float simTime = shade.TimingRegionHeight[1];
    const int32_t mode = static_cast<int32_t>(shape.ShapeCounts[3] + 0.5f);
    const int32_t count = std::max(1, static_cast<int32_t>(shape.ShapeCounts[1] + 0.5f));
    const int32_t shapeCode = static_cast<int32_t>(shape.ShapeCounts[0] + 0.5f);
    const int32_t anchorCount = static_cast<int32_t>(shape.AnchorLayout[1] + 0.5f);

    const Vec3f restHome = EvaluateShapePointAtTime(shape, anchorTexelRgba, particleIndex, { 0.0f, 0.0f, 0.0f }, true, simTime);
    res.EffectorWeight = EvaluateEffectorWeight(shape, restHome, simTime);
    const DerezEvaluationReading derez = EvaluateDerezFront(shape, restHome, simTime);
    res.DerezReleased = derez.Released ? 1.0f : 0.0f;

    const float lifetime = shade.LifeRangeFlicker[0];
    const float linger   = shade.LifeRangeFlicker[1];
    const float alive = ((mode == 1 || currentPositionAge[3] < lifetime) && particleIndex < count) ? 1.0f : 0.0f;
    res.Life = (mode == 1) ? 0.5f : ClampF(currentPositionAge[3] / std::max(lifetime, 1.0e-3f), 0.0f, 1.0f);

    const int32_t colourMode = static_cast<int32_t>(shade.ModeRampSwatch[0] + 0.5f);
    const float speed = std::sqrt(
        velocitySeed[0] * velocitySeed[0] + velocitySeed[1] * velocitySeed[1] + velocitySeed[2] * velocitySeed[2]);
    const Vec3f currPos { currentPositionAge[0], currentPositionAge[1], currentPositionAge[2] };
    Vec3f colour {};

    if (colourMode == 1)
    {
        colour = SampleColourGradient(shade, ClampF(speed / std::max(shade.LifeRangeFlicker[2], 1.0e-3f), 0.0f, 1.0f));
    }
    else if (colourMode == 2)
    {
        colour = SampleColourGradient(shade, ClampF(Dist3(currPos, restHome) / std::max(shade.LifeRangeFlicker[3], 1.0e-3f), 0.0f, 1.0f));
    }
    else if (colourMode == 3)
    {
        colour = SampleColourGradient(shade, ClampF(currPos[1] / std::max(shade.TimingRegionHeight[2], 1.0e-3f) + 0.5f, 0.0f, 1.0f));
    }
    else if (colourMode == 4)
    {
        colour = HueToRgb(velocitySeed[3]);
    }
    else if (colourMode == 5)
    {
        colour = SampleSwatchEntry(shade, FractF(velocitySeed[3] * 7.31f + 0.137f));
    }
    else if (colourMode == 6)
    {
        const float t = FractF(simTime * shade.ColourJitterCycle[2] + velocitySeed[3] * 0.35f + static_cast<float>(particleIndex) / static_cast<float>(count) * 0.5f);
        colour = SampleSwatchGradient(shade, std::fabs(t * 2.0f - 1.0f));
    }
    else
    {
        colour = SampleSwatchGradient(shade, res.Life);
    }

    if (shapeCode == 15 && anchorCount > 0)
    {
        const Vec3f fwTint = FetchAnchorNormal(anchorTexelRgba, anchorCount, particleIndex);
        colour = Lerp3(colour, Mul3(fwTint, 1.15f - 0.35f * res.Life), 0.78f);
    }

    const float effColourShift = shape.EffectorMotion[3];
    if (effColourShift > 0.001f && res.EffectorWeight > 0.001f)
    {
        const float shifted = ClampF(res.EffectorWeight * effColourShift, 0.0f, 1.0f);
        colour = Lerp3(colour, SampleSwatchGradient(shade, shifted), shifted);
    }

    const float hueJitter = shade.ColourJitterCycle[0];
    const float brightJitter = shade.ColourJitterCycle[1];
    if (hueJitter > 0.0f)
    {
        colour = HueRotate(colour, (FractF(velocitySeed[3] * 13.7f) - 0.5f) * hueJitter * kTwoPi);
    }
    if (brightJitter > 0.0f)
    {
        colour = Mul3(colour, 1.0f + (FractF(velocitySeed[3] * 29.3f) - 0.5f) * 2.0f * brightJitter);
    }
    colour = {
        std::max(colour[0], 0.0f) * shade.ColourJitterCycle[3],
        std::max(colour[1], 0.0f) * shade.ColourJitterCycle[3],
        std::max(colour[2], 0.0f) * shade.ColourJitterCycle[3]
    };

    const int32_t derezMode = static_cast<int32_t>(shape.EffectorAxisDerez[1] + 0.5f);
    const float derezMix = shape.EffectorAxisDerez[2];
    float derezFade = 1.0f;
    if (mode == 1 && derezMode > 0 && derezMix > 0.001f)
    {
        const float edgeFactor = derez.EdgeBand * shape.DerezFrontParams[2] * derezMix;
        colour[0] += shape.DerezEdgeColourWing[0] * edgeFactor;
        colour[1] += shape.DerezEdgeColourWing[1] * edgeFactor;
        colour[2] += shape.DerezEdgeColourWing[2] * edgeFactor;
        if (derez.Released && derezMode != 2)
        {
            const float releaseLife = ClampF(currentPositionAge[3] / std::max(lifetime, 0.25f), 0.0f, 1.0f);
            if (derezMode == 3) derezFade = 1.0f - SmoothStepF(0.25f, 0.85f, releaseLife);
            else derezFade = 1.0f - SmoothStepF(0.55f, 1.0f, releaseLife);
        }
        else if (derez.Released && derezMode == 2)
        {
            derezFade = 0.55f + 0.45f * derez.EdgeBand;
        }
    }

    const float flickerAmt = shade.TimingRegionHeight[0];
    res.Flicker = (flickerAmt > 0.0f)
        ? LerpF(1.0f, 0.35f + 0.65f * (0.5f + 0.5f * std::sin(simTime * (3.0f + 5.0f * velocitySeed[3]) + velocitySeed[3] * 50.0f)), flickerAmt)
        : 1.0f;

    const float glitchAmt = shape.GlitchParams[0];
    const float glitchRate = shape.GlitchParams[1];
    const float glitchScan = shape.GlitchParams[3];
    if (glitchAmt > 0.001f && glitchScan > 0.001f)
    {
        const float gTick = std::floor(simTime * std::max(glitchRate, 1.0f));
        const float d = (static_cast<float>(particleIndex) * 0.17f) * 12.9898f + gTick * 78.233f;
        const float gHash = FractF(static_cast<float>(std::sin(static_cast<double>(d))) * 43758.5453f);
        res.Flicker *= LerpF(1.0f, StepF(0.18f, gHash) * (0.65f + 0.7f * gHash), ClampF(glitchAmt * glitchScan, 0.0f, 1.0f));
    }

    const float fade = (mode == 1) ? derezFade : (SmoothStepF(0.0f, 0.05f, res.Life) * (1.0f - SmoothStepF(0.75f, 1.0f, res.Life)));
    res.Alpha = alive * fade * res.Flicker;
    res.Linger = (mode == 1)
        ? derezFade
        : ((particleIndex < count) ? ClampF(1.0f - (currentPositionAge[3] - lifetime) / std::max(linger, 1.0e-3f), 0.0f, 1.0f) : 0.0f);

    if (colourMode != 4) colour = Mul3(colour, 0.85f + 0.3f * res.Flicker);
    res.Colour = colour;
    return res;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       18 ANALYTICAL SPRITE CUTOUTS (CPU)
//------------------------------------------------------------------------------------------------------------------------

SpriteCutoutCpuResult EvaluateSpriteCutoutCpu(
    const uint32_t spriteOrdinal,
    const uint32_t styleOrdinal,
    const std::array<float, 4>& inputColour,
    const float soft,
    const float across,
    const std::array<float, 4>& extra,
    const std::array<float, 2>& pointCoord) noexcept
{
    SpriteCutoutCpuResult out {};
    if (inputColour[3] <= 0.0f) { out.Discarded = true; return out; }
    Vec3f rgb { inputColour[0], inputColour[1], inputColour[2] };
    float alpha = inputColour[3];
    const bool butterflyMorph = (static_cast<int32_t>(std::floor(extra[0] + 0.5f)) == 6) && (spriteOrdinal == 5u);

    if (soft > 0.5f)
    {
        const float qx = pointCoord[0] - 0.5f;
        const float qy = pointCoord[1] - 0.5f;
        const float d = std::sqrt(qx * qx + qy * qy) * 2.0f;

        if (spriteOrdinal == 0u)
        {
            if (d > 1.0f) { out.Discarded = true; return out; }
            alpha *= std::exp(-d * d * 4.5f) * (1.0f - SmoothStepF(0.8f, 1.0f, d));
        }
        else if (spriteOrdinal == 1u)
        {
            if (d > 1.0f) { out.Discarded = true; return out; }
            alpha *= 1.0f - SmoothStepF(0.6f, 1.0f, d);
        }
        else if (spriteOrdinal == 2u)
        {
            if (d > 1.0f) { out.Discarded = true; return out; }
            alpha *= 0.22f + 0.78f * SmoothStepF(0.5f, 0.78f, d) * (1.0f - SmoothStepF(0.86f, 1.0f, d));
        }
        else if (spriteOrdinal == 3u)
        {
            const float arms = std::max(std::exp(-std::fabs(qx) * 14.0f), std::exp(-std::fabs(qy) * 14.0f));
            alpha *= std::max(arms * (1.0f - d * 0.9f), std::exp(-d * d * 10.0f));
            if (alpha < 0.01f) { out.Discarded = true; return out; }
        }
        else if (spriteOrdinal == 6u || butterflyMorph)
        {
            const float uvx = qx * 2.0f, uvy = -qy * 2.0f;
            const float wingW = std::max(extra[1], 0.16f);
            const float x = std::fabs(uvx);
            const float xs = x / wingW;
            const float dF = std::hypot((xs - 0.50f) / 0.50f, (uvy - 0.28f) / 0.36f);
            const float dH = std::hypot((xs - 0.40f) / 0.36f, (uvy + 0.28f) / 0.26f);
            const float dMin = std::min(dF, dH);
            const float wing = 1.0f - SmoothStepF(0.84f, 1.0f, dMin);
            const float body = (1.0f - SmoothStepF(0.035f, 0.065f, x)) * StepF(std::fabs(uvy), 0.62f);
            if (wing < 0.01f && body < 0.01f) { out.Discarded = true; return out; }
            const float edge = 0.76f + 0.48f * (1.0f - std::min(dMin, 1.0f));
            rgb = Lerp3(Mul3(rgb, edge), { 0.09f, 0.09f, 0.09f }, body * 0.85f);
            alpha *= std::max(wing, body);
        }
        else if (spriteOrdinal == 5u)
        {
            const float px = qx * 2.15f, py = -qy * 2.15f;
            const float hex = std::max(std::fabs(px) * 0.8660254f + std::fabs(py) * 0.5f, std::fabs(py));
            if (hex > 0.96f) { out.Discarded = true; return out; }
            const float mask = 1.0f - SmoothStepF(0.88f, 0.96f, hex);
            const bool topFace = py > std::fabs(px) * 0.57735f;
            const float shade = topFace ? 1.14f : (px < 0.0f ? 0.80f : 0.54f);
            const float seam = std::min(std::fabs(py - std::fabs(px) * 0.57735f), py < std::fabs(px) * 0.57735f ? std::fabs(px) : 1.0f);
            const float rim = SmoothStepF(0.80f, 0.90f, hex) * 0.28f + (1.0f - SmoothStepF(0.0f, 0.06f, seam)) * 0.22f;
            rgb = Mul3(rgb, shade + rim);
            alpha *= mask;
        }
        else if (spriteOrdinal == 7u)
        {
            const float px = std::fabs(qx * 2.1f), py = std::fabs(qy * 2.1f);
            const float hex = std::max(px * 0.8660254f + py * 0.5f, py);
            if (hex > 0.98f) { out.Discarded = true; return out; }
            const float rim = SmoothStepF(0.70f, 0.88f, hex) * (1.0f - SmoothStepF(0.90f, 0.98f, hex));
            const float core = (1.0f - SmoothStepF(0.0f, 0.88f, hex)) * 0.34f;
            rgb = Mul3(rgb, 0.65f + 1.15f * rim);
            alpha *= ClampF(core + rim * 1.15f, 0.0f, 1.0f);
        }
        else if (spriteOrdinal == 8u)
        {
            const float uvx = qx * 2.0f, uvy = qy * 2.0f;
            if (extra[3] > 0.5f)
            {
                if (std::fabs(uvy) > 0.24f || std::fabs(uvx) > 0.96f) { out.Discarded = true; return out; }
                const float scan = 0.7f + 0.3f * std::sin(uvx * 18.0f + extra[2] * 40.0f);
                alpha *= (1.0f - SmoothStepF(0.14f, 0.24f, std::fabs(uvy))) * scan;
            }
            else
            {
                const float gx = std::floor((qx + 0.5f) * 5.0f);
                const float gy = std::floor((qy + 0.5f) * 5.0f);
                const float symX = std::min(gx, 4.0f - gx);
                const float seedOff = std::floor(extra[2] * 31.0f);
                const float dv = (symX + seedOff) * 12.9898f + (gy + seedOff) * 78.233f;
                const float cellHash = FractF(static_cast<float>(std::sin(static_cast<double>(dv))) * 43758.5453f);
                const float lx = FractF((qx + 0.5f) * 5.0f) - 0.5f;
                const float ly = FractF((qy + 0.5f) * 5.0f) - 0.5f;
                const float box = StepF(std::max(std::fabs(lx), std::fabs(ly)), 0.38f);
                const float on = StepF(0.36f, cellHash) * box;
                if (on < 0.01f) { out.Discarded = true; return out; }
                alpha *= on * (0.82f + 0.18f * std::sin(qy * 36.0f));
            }
        }
        else if (spriteOrdinal == 9u)
        {
            const float uvx = qx * 2.0f, uvy = qy * 2.0f;
            const float r = std::hypot(uvx, uvy);
            if (r > 0.98f) { out.Discarded = true; return out; }
            const float a = std::atan2(uvy, uvx) + kPi;
            const float arm = std::pow(std::fabs(std::cos(a * 3.0f)), r > 0.16f ? 22.0f : 2.0f);
            const float branch = std::pow(std::fabs(std::cos(a * 3.0f + 1.05f)), 26.0f) * SmoothStepF(0.26f, 0.52f, r) * (1.0f - SmoothStepF(0.62f, 0.94f, r));
            const float hub = 1.0f - SmoothStepF(0.06f, 0.20f, r);
            const float arms = std::max(arm * (1.0f - SmoothStepF(0.72f, 0.98f, r)), branch * 0.82f);
            const float mask = std::max(std::max(arms, hub), 0.0f);
            if (mask < 0.012f) { out.Discarded = true; return out; }
            rgb = Lerp3(rgb, Lerp3(rgb, { 1.0f, 1.0f, 1.0f }, 0.35f), hub);
            alpha *= ClampF(mask, 0.0f, 1.0f) * (0.72f + 0.55f * mask);
        }
        else if (spriteOrdinal == 10u)
        {
            const float uvx = qx * 2.0f, uvy = -qy * 2.0f;
            const float t = ClampF((uvy + 0.85f) / 1.75f, 0.0f, 1.0f);
            const float halfW = 1.43f * std::sqrt(t) * std::pow(std::max(0.0f, 1.0f - t), 1.35f);
            const float acrossVal = std::fabs(uvx) - halfW;
            const float inY = SmoothStepF(-0.90f, -0.82f, uvy) * (1.0f - SmoothStepF(0.86f, 0.94f, uvy));
            const float mask = (1.0f - SmoothStepF(-0.04f, 0.04f, acrossVal)) * inY;
            if (mask < 0.01f || halfW < 0.004f) { out.Discarded = true; return out; }
            const float rim = SmoothStepF(0.45f, 0.96f, std::fabs(uvx) / std::max(halfW, 0.01f));
            const float bead = 1.0f - SmoothStepF(0.0f, 0.28f, std::hypot(uvx + 0.14f, uvy + 0.32f));
            const float coreShade = 0.76f + 0.26f * (1.0f - t) + 0.28f * rim;
            rgb = Lerp3(Mul3(rgb, coreShade), { 1.0f, 1.0f, 1.0f }, bead * 0.65f);
            alpha *= mask;
        }
        else if (spriteOrdinal == 11u)
        {
            const float uvx = qx * 2.0f, uvy = -qy * 2.0f;
            const float width = (1.0f - uvy * uvy) * 0.62f;
            const float serration = 0.055f * std::sin(uvy * 26.0f);
            const float acrossVal = std::fabs(uvx) - width - serration * StepF(0.0f, width);
            const float mask = (1.0f - SmoothStepF(-0.03f, 0.05f, acrossVal)) * StepF(std::fabs(uvy), 1.0f);
            if (mask < 0.01f) { out.Discarded = true; return out; }
            const float rib = 1.0f - SmoothStepF(0.0f, 0.05f + std::fabs(uvy) * 0.03f, std::fabs(uvx));
            const Vec3f bladeCol = Mul3(rgb, 0.72f + 0.42f * (1.0f - std::fabs(uvx) / std::max(width, 1.0e-3f)));
            const Vec3f ribCol   = { rgb[0] * 1.5f + 0.10f, rgb[1] * 1.5f + 0.10f, rgb[2] * 1.5f + 0.10f };
            rgb = Lerp3(bladeCol, ribCol, rib * 0.7f);
            alpha *= mask;
        }
        else if (spriteOrdinal == 12u)
        {
            const float uvx = qx * 2.0f, uvy = -qy * 2.0f;
            const float lobe = std::hypot(uvx / 0.66f, (uvy - 0.02f) / 0.90f);
            const float notch = std::hypot(uvx / 0.16f, (uvy - 0.96f) / 0.22f);
            const float mask = (1.0f - SmoothStepF(0.86f, 1.0f, lobe)) * SmoothStepF(0.30f, 0.62f, notch + 0.34f);
            if (mask < 0.01f) { out.Discarded = true; return out; }
            const float vein = 1.0f - SmoothStepF(0.0f, 0.09f, std::fabs(uvx + std::sin(uvy * 3.0f) * 0.05f));
            const Vec3f veinCol { rgb[0] * 1.35f + 0.06f, rgb[1] * 1.35f + 0.06f, rgb[2] * 1.35f + 0.06f };
            rgb = Lerp3(Mul3(rgb, 0.90f), veinCol, vein * 0.6f);
            alpha *= mask;
        }
        else if (spriteOrdinal == 13u)
        {
            const float px = qx * 2.0f, py = -qy * 2.0f;
            const float hex = std::max(std::fabs(px) * 0.8660254f + std::fabs(py) * 0.5f, std::fabs(py));
            const float taper = 1.0f - SmoothStepF(0.55f, 1.0f, std::fabs(py)) * 0.22f;
            if (hex > 0.96f * taper) { out.Discarded = true; return out; }
            const float mask = 1.0f - SmoothStepF(0.88f, 0.96f * taper, hex);
            const float faceA = SmoothStepF(0.0f, 0.55f, px + 0.30f);
            const float faceB = SmoothStepF(0.0f, 0.55f, -px + 0.30f);
            const float faceC = SmoothStepF(0.10f, 0.35f, py);
            const float shade = 0.62f + 0.62f * faceA + 0.28f * faceB + 0.34f * faceC;
            const float edge = SmoothStepF(0.78f, 0.92f, hex) * 0.55f;
            rgb = Mul3(rgb, shade + edge);
            alpha *= mask;
        }
        else if (spriteOrdinal == 14u)
        {
            const float uvx = qx * 2.0f, uvy = qy * 2.0f;
            const float r = std::hypot(uvx, uvy);
            if (r > 1.0f) { out.Discarded = true; return out; }
            const float tR = (r - 0.78f) / 0.10f;
            const float tI = (r - 0.46f) / 0.20f;
            const float ring = std::exp(-(tR * tR));
            const float inner = std::exp(-(tI * tI)) * 0.28f;
            const float mask = ring + inner;
            if (mask < 0.012f) { out.Discarded = true; return out; }
            rgb = Lerp3(Mul3(rgb, 0.65f), { 1.0f, 1.0f, 1.0f }, ring * 0.55f);
            alpha *= ClampF(mask, 0.0f, 1.0f);
        }
        else if (spriteOrdinal == 15u)
        {
            const float uvx = qx * 2.0f, uvy = qy * 2.0f;
            const float r = std::hypot(uvx, uvy);
            if (r > 1.0f) { out.Discarded = true; return out; }
            const float a = std::atan2(uvy, uvx);
            if (styleOrdinal == 1u)
            {
                const float pulse = FractF(r * 3.0f - extra[2] * 2.4f);
                const float tw = (pulse - 0.14f) / 0.10f;
                const float wave = std::exp(-(tw * tw));
                const float crossLines = (1.0f - SmoothStepF(0.015f, 0.04f, std::fabs(uvx))) * 0.25f
                                       + (1.0f - SmoothStepF(0.015f, 0.04f, std::fabs(uvy))) * 0.25f;
                const float rimNotch = StepF(0.84f, r) * StepF(0.86f, std::fabs(std::cos(a * 12.0f))) * 0.55f;
                const float mask = std::max(wave * (1.0f - r * 0.35f), std::max(crossLines * StepF(0.12f, r), rimNotch));
                if (mask < 0.012f) { out.Discarded = true; return out; }
                rgb = Lerp3(rgb, { 0.45f, 1.0f, 0.90f }, wave * 0.65f);
                alpha *= ClampF(mask, 0.0f, 1.0f) * (1.0f - SmoothStepF(0.92f, 1.0f, r));
            }
            else
            {
                const float sweep = extra[2] * kTwoPi;
                const float rawWrap = a - sweep + 12.5663706f;
                const float wrapped = rawWrap - kTwoPi * std::floor(rawWrap / kTwoPi);
                const float wake = SmoothStepF(0.0f, 1.9f, wrapped) * StepF(wrapped, 2.6f);
                const float tl = wrapped / 0.14f;
                const float leading = std::exp(-(tl * tl));
                const float rings = 0.22f * SmoothStepF(0.38f, 0.5f, std::fabs(FractF(r * 3.0f) - 0.5f));
                const float dAx = uvx - 0.42f, dAy = uvy - 0.36f;
                const float dBx = uvx + 0.52f, dBy = uvy + 0.22f;
                const float blipA = std::exp(-(dAx * dAx + dAy * dAy) * 180.0f);
                const float blipB = std::exp(-(dBx * dBx + dBy * dBy) * 180.0f);
                const float blips = (blipA + blipB) * (0.45f + 0.55f * wake);
                const float mask = std::max(std::max(leading, wake * 0.42f), blips) + rings * StepF(0.18f, r);
                if (mask < 0.01f) { out.Discarded = true; return out; }
                rgb = Lerp3(rgb, { 0.65f, 1.0f, 0.85f }, std::max(leading * 0.55f, blips * 0.85f));
                alpha *= ClampF(mask, 0.0f, 1.0f) * (1.0f - SmoothStepF(0.92f, 1.0f, r));
            }
        }
        else if (spriteOrdinal == 16u)
        {
            const float uvx = qx * 2.0f, uvy = -qy * 2.0f;
            const float ribbonA = std::sin(uvx * 5.0f + extra[2] * 4.0f) * 0.30f;
            const float ribbonB = std::sin(uvx * 9.0f - extra[2] * 6.5f) * 0.16f;
            const float band = 1.0f - SmoothStepF(0.10f, 0.62f, std::fabs(uvy - ribbonA - ribbonB));
            const float verticalFade = SmoothStepF(-1.0f, 0.55f, uvy) * (1.0f - SmoothStepF(0.72f, 1.0f, uvy));
            const float mask = band * verticalFade;
            if (mask < 0.012f) { out.Discarded = true; return out; }
            const Vec3f aurora = Lerp3({ 0.22f, 0.95f, 0.72f }, { 0.55f, 0.42f, 1.0f }, SmoothStepF(-0.4f, 0.8f, uvy));
            rgb = Lerp3(rgb, aurora, 0.45f);
            alpha *= mask * 0.92f;
        }
        else if (spriteOrdinal == 17u)
        {
            const float uvx = qx * 2.0f, uvy = qy * 2.0f;
            const float r = std::hypot(uvx, uvy);
            if (r > 1.0f) { out.Discarded = true; return out; }
            const float rim = SmoothStepF(0.72f, 0.82f, r) * (1.0f - SmoothStepF(0.88f, 0.96f, r));
            const float milled = 0.5f + 0.5f * std::sin(std::atan2(uvy, uvx) * 44.0f);
            const float boss = 1.0f - SmoothStepF(0.16f, 0.34f, r);
            const float face = 1.0f - SmoothStepF(0.94f, 1.0f, r);
            const float relief = 0.86f + 0.24f * SmoothStepF(0.18f, 0.66f, r) + 0.26f * boss;
            const float hi = rim * milled * 0.42f + boss * 0.22f;
            rgb = { rgb[0] * relief + hi, rgb[1] * relief + hi, rgb[2] * relief + hi };
            alpha *= face;
        }
    }
    else if (soft < -0.5f)
    {
        alpha *= 1.0f - across * across;
    }

    out.Rgb = rgb;
    out.Alpha = alpha;
    out.Discarded = false;
    return out;
}

void ComputeSpriteDofMetricsCpu(
    const ParticleSpriteUniformBlock& uniforms,
    const float baseSize,
    const float depthMeters,
    float& outPixelDiameter,
    float& outAlphaMultiplier) noexcept
{
    const int32_t sizeMode = static_cast<int32_t>(uniforms.PixelDofParams[1] + 0.5f);
    float pixels = (sizeMode == 0)
        ? (baseSize * uniforms.CameraPositionFocal[3] / std::max(depthMeters, 0.05f))
        : (baseSize * uniforms.PixelDofParams[0]);

    const float focusDist = uniforms.PixelDofParams[2];
    const float aperture  = uniforms.PixelDofParams[3];
    const float coc = (focusDist > 0.0f)
        ? ClampF(std::fabs(depthMeters - focusDist) / std::max(focusDist, 0.1f) * aperture * 3.0f, 0.0f, 1.0f)
        : 0.0f;
    const float blurGrow = 1.0f + coc * 6.0f;
    const float haloScale = uniforms.HaloSpriteStyleTrail[0];
    const float haloAlpha = uniforms.HaloSpriteStyleTrail[1];
    pixels *= blurGrow * haloScale;
    outPixelDiameter = ClampF(pixels, 1.0f, 256.0f);
    outAlphaMultiplier = (depthMeters <= 0.05f) ? 0.0f : (haloAlpha / (blurGrow * blurGrow * 0.6f + 0.4f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SPRITE DRAW-BATCH PLANNER
//------------------------------------------------------------------------------------------------------------------------

std::vector<ParticleSpriteDrawCall> ParticleGpuRenderer::BuildSpriteRenderCalls(
    std::span<const ParticleCloudNode> clouds,
    const ParticleGpuSimulation& simulation,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters) const
{
    // Reference: ParticleSimulation.js L557-L643
    std::vector<ParticleSpriteDrawCall> calls;
    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Visible || !node.Enabled || node.Halted) continue;
        const ParticleCloudStateRecord* rec = simulation.FindCloudRecord(node.NodeId);
        if (!rec || rec->ActiveCount == 0u) continue;

        const ParticleCloudParameters& p = node.Evaluated;
        const uint32_t strandMode = static_cast<uint32_t>(p.StrandMode);
        if (strandMode >= 1u && strandMode <= 8u && p.CarrierMode != ParticleCarrierOrdinal::Object3D) continue;
        if (p.CarrierMode == ParticleCarrierOrdinal::Object3D) continue;
        if (p.Trail == ParticleTrailModeOrdinal::Only) continue;

        const uint32_t count = std::min(p.Count, rec->TextureSide * rec->TextureSide);
        const ParticleSpriteUniformBlock mainBlock = BuildParticleSpriteUniforms(
            p, rec->TextureSide, rec->AnchorTexels.Width, rec->AnchorTexels.Count,
            camera, simulationTimeSeconds, regionHeightMeters, 1.0f, 1.0f, -1);

        if (p.Style == ParticleRenderStyleOrdinal::Streak)
        {
            calls.push_back(ParticleSpriteDrawCall {
                node.NodeId,
                ParticleDrawTopologyOrdinal::StreakLines,
                p.Blend,
                count * 2u,
                false,
                mainBlock
            });
        }

        calls.push_back(ParticleSpriteDrawCall {
            node.NodeId,
            ParticleDrawTopologyOrdinal::SpriteQuads,
            p.Blend,
            count * 6u,
            false,
            mainBlock
        });

        if (p.Glow > 0.0f)
        {
            const ParticleSpriteUniformBlock haloBlock = BuildParticleSpriteUniforms(
                p, rec->TextureSide, rec->AnchorTexels.Width, rec->AnchorTexels.Count,
                camera, simulationTimeSeconds, regionHeightMeters, 3.2f, 0.16f * p.Glow, 0);
            calls.push_back(ParticleSpriteDrawCall {
                node.NodeId,
                ParticleDrawTopologyOrdinal::SpriteQuads,
                ParticleBlendOrdinal::Additive,
                count * 6u,
                true,
                haloBlock
            });
        }
    }
    return calls;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        RIBBON UNIFORMS, SPLINES & PLANNER
//------------------------------------------------------------------------------------------------------------------------

ParticleRibbonUniformBlock BuildParticleRibbonUniforms(
    const ParticleCloudParameters& params,
    const ParticleCloudStateRecord& record,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters,
    const float haloScale,
    const float haloAlpha) noexcept
{
    ParticleRibbonUniformBlock block {};
    block.Shape = BuildParticleShapeUniforms(params, record.TextureSide, record.AnchorTexels.Width, record.AnchorTexels.Count);
    block.Shade = BuildParticleShadeUniforms(params, simulationTimeSeconds, regionHeightMeters);
    block.ViewProjection = camera.ViewProjection;

    const float viewW = std::max(1.0f, camera.ViewportWidthPx * camera.PixelRatio);
    const float viewH = std::max(1.0f, camera.ViewportHeightPx * camera.PixelRatio);
    const float focalPixels = viewH / (2.0f * std::tan(std::max(0.05f, camera.VerticalFovRad) * 0.5f));
    const uint32_t stride = std::max(1u, static_cast<uint32_t>(std::lround(1.0 / std::max(0.01, static_cast<double>(params.TrailRatio)))));
    const uint32_t subdivisions = std::clamp(params.TrailSubdivisions, 1u, 4u);
    const float width = params.TrailWidth * (params.SizeMode == ParticleSizeModeOrdinal::Pixels ? camera.PixelRatio : 1.0f);

    block.CameraPositionFocal    = { camera.CameraPosition[0], camera.CameraPosition[1], camera.CameraPosition[2], focalPixels };
    block.CameraForwardViewportX = { camera.CameraForward[0], camera.CameraForward[1], camera.CameraForward[2], viewW };
    block.ViewportYTrailLayout   = { viewH, static_cast<float>(std::max(2u, record.TrailLength)), static_cast<float>(record.TrailHead), static_cast<float>(subdivisions) };
    block.RibbonWidthTaper       = { static_cast<float>(stride), width, static_cast<float>(params.SizeMode), params.TrailTaper };
    block.RibbonFadeColourHalo   = { params.TrailFade, static_cast<float>(params.TrailColour), haloScale, haloAlpha };
    return block;
}

std::array<float, 3> EvaluateCatmullRomCpu(
    const std::array<float, 3>& p0,
    const std::array<float, 3>& p1,
    const std::array<float, 3>& p2,
    const std::array<float, 3>& p3,
    const float u) noexcept
{
    const float u2 = u * u;
    const float u3 = u2 * u;
    std::array<float, 3> out {};
    for (size_t k = 0u; k < 3u; ++k)
    {
        out[k] = 0.5f * (
            (2.0f * p1[k])
            + (-p0[k] + p2[k]) * u
            + (2.0f * p0[k] - 5.0f * p1[k] + 4.0f * p2[k] - p3[k]) * u2
            + (-p0[k] + 3.0f * p1[k] - 3.0f * p2[k] + p3[k]) * u3);
    }
    return out;
}

std::array<float, 3> SampleTrailRingPointCpu(
    const ParticleCloudStateRecord& record,
    const uint32_t particleIndex,
    const int32_t k) noexcept
{
    if (particleIndex >= record.ReadStates.size()) return { 0.0f, 0.0f, 0.0f };
    if (k <= 0 || record.TrailLength < 2u || record.TrailSamples.empty())
    {
        const auto& live = record.ReadStates[particleIndex].PositionAge;
        return { live[0], live[1], live[2] };
    }
    const int32_t len = static_cast<int32_t>(record.TrailLength);
    const int32_t offset = static_cast<int32_t>(record.TrailHead) - (k - 1);
    const int32_t slot = ((offset % len) + len) % len;
    const size_t idx = static_cast<size_t>(particleIndex) * static_cast<size_t>(record.TrailLength) + static_cast<size_t>(slot);
    if (idx >= record.TrailSamples.size()) return { 0.0f, 0.0f, 0.0f };
    const auto& pt = record.TrailSamples[idx];
    return { pt[0], pt[1], pt[2] };
}

std::vector<ParticleRibbonDrawCall> ParticleGpuRenderer::BuildRibbonRenderCalls(
    std::span<const ParticleCloudNode> clouds,
    const ParticleGpuSimulation& simulation,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters) const
{
    // Reference: ParticleSimulation.js L484-L543 & L585-L586
    std::vector<ParticleRibbonDrawCall> calls;
    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Visible || !node.Enabled || node.Halted) continue;
        const ParticleCloudParameters& p = node.Evaluated;
        if (p.Trail == ParticleTrailModeOrdinal::Off) continue;
        const uint32_t strandMode = static_cast<uint32_t>(p.StrandMode);
        if (strandMode >= 1u && strandMode <= 8u && p.CarrierMode != ParticleCarrierOrdinal::Object3D) continue;

        const ParticleCloudStateRecord* rec = simulation.FindCloudRecord(node.NodeId);
        if (!rec || rec->TrailLength < 2u || rec->ActiveCount == 0u) continue;

        const uint32_t stride = std::max(1u, static_cast<uint32_t>(std::lround(1.0 / std::max(0.01, static_cast<double>(p.TrailRatio)))));
        const uint32_t count = std::min(p.Count, rec->TextureSide * rec->TextureSide);
        const uint32_t trailed = (count + stride - 1u) / stride;
        const uint32_t subdivisions = std::clamp(p.TrailSubdivisions, 1u, 4u);
        const uint32_t vertices = trailed * rec->TrailLength * subdivisions * 6u;
        if (vertices == 0u) continue;

        if (p.Glow > 0.0f)
        {
            const ParticleRibbonUniformBlock haloBlock = BuildParticleRibbonUniforms(
                p, *rec, camera, simulationTimeSeconds, regionHeightMeters, 3.0f, 0.12f * p.Glow);
            calls.push_back(ParticleRibbonDrawCall {
                node.NodeId,
                ParticleBlendOrdinal::Additive,
                vertices,
                true,
                haloBlock
            });
        }

        const ParticleRibbonUniformBlock mainBlock = BuildParticleRibbonUniforms(
            p, *rec, camera, simulationTimeSeconds, regionHeightMeters, 1.0f, 1.0f);
        calls.push_back(ParticleRibbonDrawCall {
            node.NodeId,
            p.Blend,
            vertices,
            false,
            mainBlock
        });
    }
    return calls;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CARRIER SOLID & BACKING SURFACE (S12)
//------------------------------------------------------------------------------------------------------------------------

ParticleCarrierSolidUniformBlock BuildParticleCarrierSolidUniforms(
    const ParticleCloudParameters& params,
    const ParticleCloudStateRecord& record,
    const uint32_t solidVertexCount,
    const uint32_t solidTriangleCount,
    const uint32_t carrierStride,
    const bool isBacking,
    const bool isWire,
    const uint32_t backingMode,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters,
    const std::array<float, 3>& backingColour) noexcept
{
    // Reference: ParticleSimulation.js L376-L426
    ParticleCarrierSolidUniformBlock block {};
    block.Shape = BuildParticleShapeUniforms(params, record.TextureSide, record.AnchorTexels.Width, record.AnchorTexels.Count);
    block.Shade = BuildParticleShadeUniforms(params, simulationTimeSeconds, regionHeightMeters);
    block.ViewProjection = camera.ViewProjection;

    const float viewH = std::max(1.0f, camera.ViewportHeightPx * camera.PixelRatio);
    const float focalPixels = viewH / (2.0f * std::tan(std::max(0.05f, camera.VerticalFovRad) * 0.5f));
    const float yawRad = params.SolidYawDegrees * (kPi / 180.0f);

    block.CameraPositionFocal     = { camera.CameraPosition[0], camera.CameraPosition[1], camera.CameraPosition[2], focalPixels };
    block.CameraForwardPixelRatio = { camera.CameraForward[0], camera.CameraForward[1], camera.CameraForward[2], camera.PixelRatio };
    block.SolidCountsStride       = {
        static_cast<float>(std::max(3u, solidVertexCount)),
        static_cast<float>(std::max(1u, solidTriangleCount)),
        static_cast<float>(std::max(1u, carrierStride)),
        isWire ? 1.0f : 0.0f
    };
    block.BackingParams = {
        isBacking ? 1.0f : 0.0f,
        std::max(0.05f, params.Radius),
        yawRad,
        static_cast<float>(backingMode)
    };
    block.SizeParams = {
        params.SizeStart,
        params.SizeEnd,
        params.SizeRandom,
        static_cast<float>(params.SizeMode)
    };
    block.SpinTiltRoughMetal = {
        params.SolidSpin,
        params.SolidTilt,
        params.SolidRoughness,
        params.SolidMetallic
    };
    block.BackingColourGlow = {
        backingColour[0],
        backingColour[1],
        backingColour[2],
        params.SolidGlow
    };
    return block;
}

CarrierSolidVertexCpuResult EvaluateCarrierSolidVertexCpu(
    const ParticleCarrierSolidUniformBlock& uniforms,
    const std::span<const ParticleStateGpuRecord> states,
    const std::span<const float> anchorTexelRgba,
    const std::span<const float> solidTexelRgba,
    const uint32_t vertexId) noexcept
{
    // Reference: ParticleShaders.js L1375-L1470 (ObjectRenderVertex)
    CarrierSolidVertexCpuResult out {};
    const int32_t triangles     = std::max(1, static_cast<int32_t>(uniforms.SolidCountsStride[1] + 0.5f));
    const int32_t vertices      = std::max(3, static_cast<int32_t>(uniforms.SolidCountsStride[0] + 0.5f));
    const int32_t carrierStride = std::max(1, static_cast<int32_t>(uniforms.SolidCountsStride[2] + 0.5f));
    const int32_t wireMode      = static_cast<int32_t>(uniforms.SolidCountsStride[3] + 0.5f);
    const int32_t isBacking     = static_cast<int32_t>(uniforms.BackingParams[0] + 0.5f);

    const int32_t stride        = (wireMode == 1) ? (triangles * 6) : vertices;
    const int32_t instanceIndex = static_cast<int32_t>(vertexId) / stride;
    const int32_t particle      = (isBacking == 1) ? 0 : (instanceIndex * carrierStride);
    const int32_t slot          = static_cast<int32_t>(vertexId) - instanceIndex * stride;

    int32_t vertexIndex = slot;
    if (wireMode == 1)
    {
        const int32_t triIndex = slot / 6;
        const int32_t edge     = slot - triIndex * 6;
        int32_t corner         = (edge + 1) / 2;
        if (corner > 2) corner = 0;
        vertexIndex = triIndex * 3 + corner;
    }

    const size_t texBase = static_cast<size_t>(std::max(0, vertexIndex)) * 8u;
    Vec3f local  { 0.0f, 0.0f, 0.0f };
    Vec3f normal { 0.0f, 1.0f, 0.0f };
    float ridge  = 0.0f;
    if (texBase + 7u < solidTexelRgba.size())
    {
        local  = { solidTexelRgba[texBase + 0u], solidTexelRgba[texBase + 1u], solidTexelRgba[texBase + 2u] };
        normal = { solidTexelRgba[texBase + 3u], solidTexelRgba[texBase + 4u], solidTexelRgba[texBase + 5u] };
        ridge  = solidTexelRgba[texBase + 7u];
    }

    const int32_t side = std::max(1, static_cast<int32_t>(uniforms.Shape.AnchorLayout[2] + 0.5f));
    const int32_t maxIndex = std::min(side * side - 1, static_cast<int32_t>(states.size()) - 1);
    const int32_t safeParticle = (maxIndex >= 0) ? std::clamp(particle, 0, maxIndex) : 0;
    const std::array<float, 4> current  = (maxIndex >= 0) ? states[static_cast<size_t>(safeParticle)].PositionAge  : std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f };
    const std::array<float, 4> velocity = (maxIndex >= 0) ? states[static_cast<size_t>(safeParticle)].VelocitySeed : std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f };

    const ParticleShadeCpuResult shaded = EvaluateParticleShadeCpu(
        uniforms.Shape, uniforms.Shade, anchorTexelRgba, safeParticle, current, velocity);

    Vec3f base = (isBacking == 1)
        ? Vec3f { uniforms.Shape.OriginRadius[0], uniforms.Shape.OriginRadius[1], uniforms.Shape.OriginRadius[2] }
        : Vec3f { current[0], current[1], current[2] };

    const float glitchAmount     = uniforms.Shape.GlitchParams[0];
    const float glitchRate       = uniforms.Shape.GlitchParams[1];
    const float glitchStepJitter = uniforms.Shape.GlitchParams[2];
    const int32_t derezMode      = static_cast<int32_t>(uniforms.Shape.EffectorAxisDerez[1] + 0.5f);
    const float derezMix         = uniforms.Shape.EffectorAxisDerez[2];
    const float simTime          = uniforms.Shade.TimingRegionHeight[1];
    if (isBacking == 0 && ((glitchAmount > 0.001f && glitchStepJitter > 0.001f) || (derezMode == 5 && shaded.DerezReleased > 0.5f)))
    {
        const float rate = std::max(glitchRate, 1.0f);
        const float stepTick = std::floor(simTime * rate);
        const Vec3f jSeed { static_cast<float>(safeParticle) * 0.13f + stepTick, velocity[3] * 31.7f + stepTick, stepTick * 0.73f };
        const float gate = StepF(0.42f - glitchAmount * 0.35f, Hash31F(Add3(jSeed, { 4.1f, 4.1f, 4.1f })));
        const float amp = glitchAmount * glitchStepJitter * gate
            + ((derezMode == 5 && shaded.DerezReleased > 0.5f) ? (0.09f * derezMix) : 0.0f);
        base[0] += (Hash31F(jSeed) - 0.5f) * 2.0f * amp;
        base[1] += (Hash31F(Add3(jSeed, { 7.3f, 7.3f, 7.3f })) - 0.5f) * 2.0f * amp;
        base[2] += (Hash31F(Add3(jSeed, { 13.9f, 13.9f, 13.9f })) - 0.5f) * 2.0f * amp;
    }

    const float sizeSeed   = FractF(velocity[3] * 41.7f);
    const float sizeRandom = std::exp2((sizeSeed - 0.5f) * 3.0f * uniforms.SizeParams[2]);
    float radius = (isBacking == 1)
        ? uniforms.BackingParams[1]
        : (LerpF(uniforms.SizeParams[0], uniforms.SizeParams[1], shaded.Life) * sizeRandom);

    const int32_t effectorMode       = static_cast<int32_t>(uniforms.Shape.MocapKickEffector[1] + 0.5f);
    const float   effectorStrength   = uniforms.Shape.MocapKickEffector[2];
    const float   effectorScaleBoost = uniforms.Shape.EffectorMotion[2];
    if (isBacking == 0 && effectorMode > 0 && effectorStrength > 0.0001f && std::fabs(effectorScaleBoost) > 0.0001f)
    {
        radius *= std::max(0.08f, 1.0f + shaded.EffectorWeight * effectorScaleBoost);
    }

    const Vec3f camPos { uniforms.CameraPositionFocal[0], uniforms.CameraPositionFocal[1], uniforms.CameraPositionFocal[2] };
    const Vec3f camFwd { uniforms.CameraForwardPixelRatio[0], uniforms.CameraForwardPixelRatio[1], uniforms.CameraForwardPixelRatio[2] };
    const float depth = Dot3({ base[0] - camPos[0], base[1] - camPos[1], base[2] - camPos[2] }, camFwd);
    const int32_t sizeMode = static_cast<int32_t>(uniforms.SizeParams[3] + 0.5f);
    if (isBacking == 0 && sizeMode == 1)
    {
        radius = radius * uniforms.CameraForwardPixelRatio[3] * std::max(depth, 0.05f) / std::max(uniforms.CameraPositionFocal[3], 1.0f);
    }

    Vec3f spun {};
    Vec3f spunNormal {};
    if (isBacking == 1)
    {
        const float yaw = uniforms.BackingParams[2];
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const Vec3f yawed       { local[0] * cy + local[2] * sy, local[1], -local[0] * sy + local[2] * cy };
        const Vec3f yawedNormal { normal[0] * cy + normal[2] * sy, normal[1], -normal[0] * sy + normal[2] * cy };
        const Vec3f axis { uniforms.Shape.AxisFill[0], uniforms.Shape.AxisFill[1], uniforms.Shape.AxisFill[2] };
        Vec3f tAxis {}, bAxis {};
        ComputeShapeBasis3(axis, tAxis, bAxis);
        spun = Add3(Add3(Mul3(tAxis, yawed[0]), Mul3(axis, yawed[1])), Mul3(bAxis, yawed[2]));
        spunNormal = Normalise3(Add3(Add3(Mul3(tAxis, yawedNormal[0]), Mul3(axis, yawedNormal[1])), Mul3(bAxis, yawedNormal[2])));
    }
    else
    {
        const float angle = uniforms.SpinTiltRoughMetal[0] * simTime + velocity[3] * kTwoPi;
        const float ca = std::cos(angle), sa = std::sin(angle);
        const float tilt = uniforms.SpinTiltRoughMetal[1] * (FractF(velocity[3] * 3.7f) - 0.5f) * 1.6f;
        const float ct = std::cos(tilt), st = std::sin(tilt);
        Vec3f rPos  { local[0] * ca + local[2] * sa, local[1], -local[0] * sa + local[2] * ca };
        Vec3f rNorm { normal[0] * ca + normal[2] * sa, normal[1], -normal[0] * sa + normal[2] * ca };
        spun       = { rPos[0], rPos[1] * ct - rPos[2] * st, rPos[1] * st + rPos[2] * ct };
        spunNormal = { rNorm[0], rNorm[1] * ct - rNorm[2] * st, rNorm[1] * st + rNorm[2] * ct };
    }

    out.WorldPos    = Add3(base, Mul3(spun, radius));
    out.WorldNormal = spunNormal;
    out.LocalPos    = local;
    out.Colour      = shaded.Colour;
    out.Alpha       = (isBacking == 1) ? 1.0f : shaded.Alpha;
    out.Ridge       = ridge;
    out.Culled      = (out.Alpha <= 0.002f);
    return out;
}

CarrierSolidFragmentCpuResult EvaluateCarrierSolidFragmentCpu(
    const ParticleCarrierSolidUniformBlock& uniforms,
    const CarrierSolidVertexCpuResult& vertex) noexcept
{
    // Reference: ParticleShaders.js L1471-L1551 (ObjectFragment)
    CarrierSolidFragmentCpuResult out {};
    if (vertex.Alpha <= 0.002f)
    {
        out.Discarded = true;
        return out;
    }

    Vec3f N = Normalise3(vertex.WorldNormal);
    const Vec3f camPos { uniforms.CameraPositionFocal[0], uniforms.CameraPositionFocal[1], uniforms.CameraPositionFocal[2] };
    const Vec3f V = Normalise3({ camPos[0] - vertex.WorldPos[0], camPos[1] - vertex.WorldPos[1], camPos[2] - vertex.WorldPos[2] });
    if (Dot3(N, V) < 0.0f) N = Mul3(N, -1.0f);

    const Vec3f keyLight = Normalise3({ 0.42f, 0.82f, 0.46f });
    const Vec3f rimLight = Normalise3({ -0.55f, 0.28f, -0.62f });
    const float fresnel  = std::pow(1.0f - ClampF(Dot3(N, V), 0.0f, 1.0f), 4.0f);

    const int32_t backingMode = static_cast<int32_t>(uniforms.BackingParams[3] + 0.5f);
    const int32_t wireMode    = static_cast<int32_t>(uniforms.SolidCountsStride[3] + 0.5f);
    const Vec3f   backingCol  { uniforms.BackingColourGlow[0], uniforms.BackingColourGlow[1], uniforms.BackingColourGlow[2] };
    const float   solidGlow   = uniforms.BackingColourGlow[3];
    Vec3f rgb {};

    if (backingMode == 0)
    {
        const float roughness     = ClampF(uniforms.SpinTiltRoughMetal[2], 0.02f, 1.0f);
        const float metallic      = ClampF(uniforms.SpinTiltRoughMetal[3], 0.0f, 1.0f);
        const float specPower     = LerpF(96.0f, 12.0f, roughness);
        const Vec3f halfKey       = Normalise3(Add3(keyLight, V));
        const Vec3f halfRim       = Normalise3(Add3(rimLight, V));
        const float diffuse       = std::max(Dot3(N, keyLight), 0.0f) * 0.72f + std::max(Dot3(N, rimLight), 0.0f) * 0.24f + 0.10f;
        const float specular      = std::pow(std::max(Dot3(N, halfKey), 0.0f), specPower)
                                  + std::pow(std::max(Dot3(N, halfRim), 0.0f), specPower * 0.55f) * 0.45f;
        const float metallicBoost = LerpF(0.35f, 1.15f, metallic);
        const float luma          = Dot3(vertex.Colour, { 0.30f, 0.59f, 0.11f });
        const Vec3f albedo        = Lerp3(vertex.Colour, { luma, luma, luma }, metallic * 0.55f);
        const float specScalar    = specular * metallicBoost * LerpF(0.55f, 1.0f, metallic);
        rgb = Add3(Mul3(albedo, diffuse * (1.0f - metallic * 0.35f)), { specScalar, specScalar, specScalar });
        rgb = Add3(rgb, Mul3(vertex.Colour, fresnel * (0.24f + solidGlow)));
        rgb = Add3(rgb, Mul3(vertex.Colour, vertex.Ridge * 0.22f));
    }
    else if (backingMode == 1)
    {
        const Vec3f base  { 0.022f, 0.026f, 0.036f };
        const Vec3f boxA  = Normalise3({ -0.35f, 0.92f, 0.28f });
        const Vec3f boxB  = Normalise3({ 0.48f, 0.74f, -0.44f });
        const float softA = std::pow(std::max(Dot3(N, Normalise3(Add3(boxA, V))), 0.0f), 120.0f);
        const float softB = std::pow(std::max(Dot3(N, Normalise3(Add3(boxB, V))), 0.0f), 84.0f);
        const float edge  = std::pow(1.0f - ClampF(Dot3(N, V), 0.0f, 1.0f), 4.5f);
        rgb = Add3(base, Mul3(backingCol, softA * 1.5f + softB * 0.9f + edge * 0.42f));
        rgb = Add3(rgb, Mul3(vertex.Colour, 0.06f));
    }
    else if (backingMode == 2)
    {
        const float brushed = 0.5f + 0.5f * std::sin(vertex.LocalPos[1] * 210.0f);
        const float horizon = SmoothStepF(0.15f, 0.9f, std::fabs(N[1]));
        const float spec    = std::pow(std::max(Dot3(N, Normalise3(Add3(keyLight, V))), 0.0f), 64.0f);
        const Vec3f chrome  = Mul3(Lerp3({ 0.30f, 0.33f, 0.38f }, { 0.86f, 0.90f, 0.96f }, horizon), 0.55f + 0.45f * brushed);
        rgb = Add3(Add3(chrome, { spec * 0.9f, spec * 0.9f, spec * 0.9f }), Mul3(backingCol, fresnel * 0.35f));
    }
    else
    {
        const float factor = (0.45f + 1.35f * fresnel) * ((wireMode == 1) ? 1.15f : 0.85f) * (0.85f + 0.3f * vertex.Ridge);
        rgb = Mul3(backingCol, factor);
    }

    float alpha = vertex.Alpha;
    if (backingMode == 3) alpha *= 0.55f + 0.45f * fresnel;
    out.Rgb = rgb;
    out.Alpha = alpha;
    out.Discarded = false;
    return out;
}

bool RequiresParticleDepthBuffer(const std::span<const ParticleCloudNode> clouds) noexcept
{
    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Visible || !node.Enabled || node.Halted) continue;
        const ParticleCloudParameters& p = node.Evaluated;
        if (p.CarrierMode == ParticleCarrierOrdinal::Object3D
            || p.BackingSurface == ParticleBackingSurfaceOrdinal::ObsidianGlossy
            || p.BackingSurface == ParticleBackingSurfaceOrdinal::BrushedChrome)
        {
            return true;
        }
    }
    return false;
}

std::vector<ParticleCarrierSolidDrawCall> ParticleGpuRenderer::BuildCarrierSolidRenderCalls(
    const std::span<const ParticleCloudNode> clouds,
    const ParticleGpuSimulation& simulation,
    const CarrierSolidRegistry& solidRegistry,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const float regionHeightMeters,
    const std::array<float, 3>& backingColour) const
{
    // Reference: ParticleSimulation.js L376-L446, L507-L513, L577-L592
    std::vector<ParticleCarrierSolidDrawCall> calls;

    auto emitCall = [&](
        const ParticleCloudNode& node,
        const ParticleCloudStateRecord& rec,
        const bool isBacking,
        const bool isWire,
        const uint32_t backingMode)
    {
        const ParticleCloudParameters& p = node.Evaluated;
        const std::string& solidName = isBacking ? p.SpawnSolid : p.CarrierSolid;
        const std::string& customId  = isBacking ? p.CustomSolidId : (!p.CarrierCustomId.empty() ? p.CarrierCustomId : p.CustomSolidId);
        const CarrierSolidRecord& solid = solidRegistry.ResolveSolid(solidName, customId);
        if (solid.Triangles == 0u || solid.VertexCount == 0u) return;

        const uint32_t stride = isWire ? (solid.Triangles * 6u) : solid.VertexCount;
        const uint32_t rawCount = isBacking ? 1u : std::min(p.Count, rec.TextureSide * rec.TextureSide);
        uint32_t carrierStride = 1u;
        if (!isBacking && stride > 0u && static_cast<uint64_t>(rawCount) * static_cast<uint64_t>(stride) > kMaximumCarrierVertices)
        {
            const uint64_t num = static_cast<uint64_t>(rawCount) * static_cast<uint64_t>(stride);
            carrierStride = static_cast<uint32_t>(std::max<uint64_t>(1u, (num + kMaximumCarrierVertices - 1u) / kMaximumCarrierVertices));
        }
        const uint32_t drawnCount  = isBacking ? 1u : ((rawCount + carrierStride - 1u) / carrierStride);
        const uint32_t vertexCount = drawnCount * stride;
        if (vertexCount == 0u) return;

        const bool solidDepth = (!isWire) && (isBacking
            ? (backingMode == 1u || backingMode == 2u)
            : (p.Blend == ParticleBlendOrdinal::Alpha));

        ParticleBlendOrdinal blend = p.Blend;
        if (solidDepth)          blend = ParticleBlendOrdinal::Alpha;
        else if (isBacking && isWire) blend = ParticleBlendOrdinal::Additive;

        const ParticleCarrierSolidUniformBlock uniforms = BuildParticleCarrierSolidUniforms(
            p, rec, solid.VertexCount, solid.Triangles, carrierStride,
            isBacking, isWire, backingMode, camera, simulationTimeSeconds, regionHeightMeters, backingColour);

        calls.push_back(ParticleCarrierSolidDrawCall {
            node.NodeId,
            solidName,
            customId,
            isWire ? ParticleSolidTopologyOrdinal::LineList : ParticleSolidTopologyOrdinal::TriangleList,
            blend,
            solidDepth,
            solidDepth,
            isBacking,
            backingMode,
            carrierStride,
            drawnCount,
            vertexCount,
            uniforms
        });
    };

    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Visible || !node.Enabled || node.Halted) continue;
        const ParticleCloudStateRecord* rec = simulation.FindCloudRecord(node.NodeId);
        if (!rec || rec->ActiveCount == 0u) continue;

        const uint32_t backingMode = static_cast<uint32_t>(node.Evaluated.BackingSurface);
        if (backingMode > 0u)
        {
            emitCall(node, *rec, true, backingMode == 3u, backingMode);
        }

        if (node.Evaluated.CarrierMode == ParticleCarrierOrdinal::Object3D)
        {
            emitCall(node, *rec, false, false, 0u);
        }
    }

    return calls;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  FLUID-COUPLED STRANDS, WEBS, FLORA & ARCS (S13)
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] float StrandHashF(const float n, const float salt) noexcept
{
    return FractF(std::sin(n * 127.1f + salt * 311.7f) * 43758.5453123f);
}

void ComputeStrandBasis3(const Vec3f& axis, Vec3f& outR, Vec3f& outU, Vec3f& outF) noexcept
{
    const float len = std::sqrt(Dot3(axis, axis));
    outU = (len > 1.0e-4f) ? Mul3(axis, 1.0f / len) : Vec3f { 0.0f, 1.0f, 0.0f };
    const Vec3f ref = (std::fabs(outU[2]) < 0.92f) ? Vec3f { 0.0f, 0.0f, 1.0f } : Vec3f { 1.0f, 0.0f, 0.0f };
    outR = Normalise3(Cross3(outU, ref));
    outF = Cross3(outR, outU);
}

[[nodiscard]] std::array<float, 2> ComputeOrbAnchor(const int32_t idx, const float R) noexcept
{
    const float a = static_cast<float>(idx % 6) * 1.04719755f + 0.28f + (StrandHashF(static_cast<float>(idx), 2.0f) - 0.5f) * 0.14f;
    const float rr = R * (0.94f + 0.14f * StrandHashF(static_cast<float>(idx), 3.0f));
    return { std::cos(a) * rr, std::sin(a) * rr };
}

} // namespace

ParticleStrandUniformBlock BuildParticleStrandUniforms(
    const ParticleCloudParameters& params,
    const ParticleSimulationRegion& region,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const uint32_t forceRecordCount,
    const bool hasGasVolume) noexcept
{
    // Reference: ParticleSimulation.js L449-L505
    ParticleStrandUniformBlock block {};
    block.ViewProjection = camera.ViewProjection;

    const size_t stopCount = params.ColourStops.empty() ? 1u : std::min<size_t>(params.ColourStops.size(), 8u);
    if (params.ColourStops.empty())
    {
        block.SwatchStops[0] = { 1.0f, 1.0f, 1.0f, 0.0f };
    }
    else
    {
        for (size_t i = 0u; i < stopCount; ++i)
        {
            block.SwatchStops[i] = ParseHexSwatch(params.ColourStops[i]);
        }
    }

    const Vec3f axis = Normalise3(params.Direction);
    const float viewW = std::max(1.0f, camera.ViewportWidthPx * camera.PixelRatio);
    const float viewH = std::max(1.0f, camera.ViewportHeightPx * camera.PixelRatio);
    const float focalPixels = viewH / (2.0f * std::tan(std::max(0.05f, camera.VerticalFovRad) * 0.5f));

    const uint32_t mode      = static_cast<uint32_t>(params.StrandMode);
    const uint32_t variant   = static_cast<uint32_t>(params.StrandVariant);
    const uint32_t baseCount = std::clamp(params.StrandCount, 6u, 192u);
    const uint32_t detail    = std::clamp(params.StrandDetail, 4u, 28u);
    uint32_t totalStrands = baseCount;
    if (mode == 1u)
    {
        const uint32_t sp = std::min(baseCount, 36u);
        totalStrands = std::min(640u, 12u + sp + sp * std::min(detail, 20u));
    }
    else if (mode == 2u)
    {
        totalStrands = std::min(480u, baseCount * 2u);
    }
    else if (mode == 5u || mode == 8u)
    {
        totalStrands = std::min(320u, baseCount * 2u);
    }
    const uint32_t segments = (mode >= 9u) ? std::clamp(detail, 14u, 24u) : 14u;

    Frontier::SimulationSpace::ForceFields::TimingEnvelopeParameters timing {};
    timing.StartTimeSeconds = params.StartTime;
    timing.DurationSeconds  = params.Duration;
    timing.FadeSeconds      = params.Fade;
    timing.RepeatSeconds    = params.Repeat;
    const float envelope = static_cast<float>(
        Frontier::SimulationSpace::ForceFields::EvaluateNodeTimingWeight(
            std::span<const Frontier::SimulationSpace::ForceFields::KeyTrack> {},
            timing,
            static_cast<double>(simulationTimeSeconds)));

    block.CameraPositionFocal          = { camera.CameraPosition[0], camera.CameraPosition[1], camera.CameraPosition[2], focalPixels };
    block.CameraForwardViewportX       = { camera.CameraForward[0], camera.CameraForward[1], camera.CameraForward[2], viewW };
    block.OriginRadius                 = { params.Position[0], params.Position[1], params.Position[2], std::max(0.1f, params.Radius) };
    block.AxisTime                     = { axis[0], axis[1], axis[2], simulationTimeSeconds };
    block.GridCentreHalfX              = { region.CentreMeters[0], region.CentreMeters[1], region.CentreMeters[2], region.HalfExtentMeters[0] };
    block.GridHalfYZForceGas           = { region.HalfExtentMeters[1], region.HalfExtentMeters[2], static_cast<float>(forceRecordCount), hasGasVolume ? 1.0f : 0.0f };
    block.ModeVariantCounts            = { static_cast<float>(mode), static_cast<float>(variant), static_cast<float>(baseCount), static_cast<float>(totalStrands) };
    block.SegmentsDetailViewportSwatch = { static_cast<float>(segments), static_cast<float>(detail), viewH, static_cast<float>(stopCount) };
    block.SagThicknessSwaySpan         = { params.StrandSag, std::max(0.004f, params.StrandThickness), params.StrandSway, std::max(0.3f, params.StrandSpan) };
    block.GlowBrightnessEnvelope       = { params.StrandGlow, params.Brightness, envelope, 0.0f };
    return block;
}

StrandEndpointsCpuResult EvaluateStrandEndpointsCpu(
    const ParticleStrandUniformBlock& uniforms,
    const int32_t slot) noexcept
{
    // Reference: ParticleShaders.js L1624-L1852 (EvalStrandEndpoints)
    Vec3f rAxis {}, uVec {}, fAxis {};
    ComputeStrandBasis3({ uniforms.AxisTime[0], uniforms.AxisTime[1], uniforms.AxisTime[2] }, rAxis, uVec, fAxis);
    (void)fAxis;

    const float sd          = static_cast<float>(slot) * 37.0f + 13.0f;
    const Vec3f origin      { uniforms.OriginRadius[0], uniforms.OriginRadius[1], uniforms.OriginRadius[2] };
    const float R           = std::max(0.45f, uniforms.OriginRadius[3]);
    const float span        = std::max(0.35f, uniforms.SagThicknessSwaySpan[3]);
    const float baseSag     = uniforms.SagThicknessSwaySpan[0];
    const float simTime     = uniforms.AxisTime[3];
    const int32_t mode      = static_cast<int32_t>(uniforms.ModeVariantCounts[0] + 0.5f);
    const int32_t variant   = static_cast<int32_t>(uniforms.ModeVariantCounts[1] + 0.5f);
    const int32_t count     = static_cast<int32_t>(uniforms.ModeVariantCounts[2] + 0.5f);
    const int32_t total     = static_cast<int32_t>(uniforms.ModeVariantCounts[3] + 0.5f);
    const int32_t detail    = static_cast<int32_t>(uniforms.SegmentsDetailViewportSwatch[1] + 0.5f);

    StrandEndpointsCpuResult o {};
    o.PointA     = origin;
    o.PointB     = Add3(origin, Mul3(uVec, 0.5f));
    o.Sag        = baseSag;
    o.WidthScale = 1.0f;
    o.PinB       = 0.0f;
    o.StyleTag   = 0.0f;
    o.ColourT    = StrandHashF(sd, 9.0f);
    o.Visible    = 1.0f;

    if (mode == 1)
    {
        const int32_t spokes = std::clamp(count, 8, 36);
        const int32_t rings  = std::clamp(detail, 6, 24);
        const Vec3f hub = Add3(origin, Mul3(uVec, -0.04f * R));
        o.PinB = 1.0f;
        o.StyleTag = 1.0f;
        if (slot < 6)
        {
            const auto la = ComputeOrbAnchor(slot, R);
            const auto lb = ComputeOrbAnchor((slot + 1) % 6, R);
            o.PointA = Add3(origin, Add3(Mul3(rAxis, la[0]), Mul3(uVec, la[1])));
            o.PointB = Add3(origin, Add3(Mul3(rAxis, lb[0]), Mul3(uVec, lb[1])));
            o.Sag = baseSag * 0.22f;
            o.WidthScale = 0.65f;
            o.ColourT = 0.15f;
        }
        else if (slot < 12)
        {
            const auto la = ComputeOrbAnchor(slot - 6, R);
            o.PointA = Add3(origin, Add3(Mul3(rAxis, la[0]), Mul3(uVec, la[1])));
            o.PointB = Add3(origin, Add3(Mul3(rAxis, la[0] * 1.24f), Mul3(uVec, la[1] * 1.24f)));
            o.Sag = baseSag * 0.08f;
            o.WidthScale = 0.58f;
            o.ColourT = 0.1f;
        }
        else if (slot < 12 + spokes)
        {
            const int32_t si = slot - 12;
            const float th = (static_cast<float>(si) / static_cast<float>(spokes)) * kTwoPi + (StrandHashF(static_cast<float>(si), 1.0f) - 0.5f) * 0.03f;
            o.PointA = hub;
            o.PointB = Add3(origin, Mul3(Add3(Mul3(rAxis, std::cos(th)), Mul3(uVec, std::sin(th))), R * 0.95f));
            o.Sag = baseSag * 0.12f;
            o.WidthScale = 0.52f;
            o.ColourT = 0.3f;
        }
        else
        {
            const int32_t spIdx = slot - 12 - spokes;
            const int32_t totalSpiral = spokes * rings;
            if (spIdx >= totalSpiral) { o.Visible = 0.0f; return o; }
            const int32_t s0 = spIdx % spokes;
            const int32_t s1 = (spIdx + 1) % spokes;
            const float f0 = static_cast<float>(spIdx) / static_cast<float>(std::max(totalSpiral, 1));
            const float f1 = static_cast<float>(spIdx + 1) / static_cast<float>(std::max(totalSpiral, 1));
            const float r0 = LerpF(0.14f, 0.92f, std::pow(f0, 0.92f)) * R;
            const float r1 = LerpF(0.14f, 0.92f, std::pow(f1, 0.92f)) * R;
            const float th0 = (static_cast<float>(s0) / static_cast<float>(spokes)) * kTwoPi + (StrandHashF(static_cast<float>(s0), 1.0f) - 0.5f) * 0.03f;
            const float th1 = (static_cast<float>(s1) / static_cast<float>(spokes)) * kTwoPi + (StrandHashF(static_cast<float>(s1), 1.0f) - 0.5f) * 0.03f;
            o.PointA = Add3(hub, Mul3(Add3(Mul3(rAxis, std::cos(th0)), Mul3(uVec, std::sin(th0))), r0));
            o.PointB = Add3(hub, Mul3(Add3(Mul3(rAxis, std::cos(th1)), Mul3(uVec, std::sin(th1))), r1));
            o.Sag = baseSag * (0.14f + 0.32f * f0) * ClampF(Dist3(o.PointA, o.PointB) / std::max(0.3f, R * 0.35f), 0.3f, 1.2f);
            o.WidthScale = 0.42f;
            o.ColourT = LerpF(0.45f, 0.95f, f0);
            o.StyleTag = 1.5f;
        }
    }
    else if (mode == 2)
    {
        const int32_t nChords = std::max(12, total / 3);
        const int32_t nTangle = nChords + std::max(12, (total * 4) / 10);
        o.StyleTag = 2.0f;
        if (slot < nChords)
        {
            const int32_t pocket = slot % 3;
            const float ph = static_cast<float>(pocket) * 2.0943951f;
            const Vec3f dirA { std::cos(ph), 0.35f + 0.65f * StrandHashF(sd, 1.0f), std::sin(ph) };
            const Vec3f dirB { std::cos(ph + 1.35f + 0.5f * StrandHashF(sd, 2.0f)), -0.35f + 1.15f * StrandHashF(sd, 3.0f), std::sin(ph + 1.35f) };
            o.PointA = Add3(origin, Mul3(dirA, R * LerpF(0.55f, 1.15f, StrandHashF(sd, 4.0f))));
            o.PointB = Add3(origin, Mul3(dirB, R * LerpF(0.55f, 1.15f, StrandHashF(sd, 5.0f))));
            o.Sag = baseSag * (0.45f + 0.65f * StrandHashF(sd, 6.0f));
            o.PinB = 1.0f;
            o.WidthScale = 0.50f;
            o.ColourT = 0.25f;
        }
        else if (slot < nTangle)
        {
            const float a0 = StrandHashF(sd, 1.0f) * kTwoPi;
            const float a1 = a0 + LerpF(0.35f, 1.45f, StrandHashF(sd, 2.0f));
            const float r0 = R * LerpF(0.20f, 0.95f, StrandHashF(sd, 3.0f));
            const float r1 = R * LerpF(0.20f, 0.95f, StrandHashF(sd, 4.0f));
            o.PointA = Add3(origin, { std::cos(a0) * r0, (StrandHashF(sd, 5.0f) - 0.35f) * span * 0.8f, std::sin(a0) * r0 });
            o.PointB = Add3(origin, { std::cos(a1) * r1, (StrandHashF(sd, 6.0f) - 0.35f) * span * 0.8f, std::sin(a1) * r1 });
            o.Sag = baseSag * (0.35f + 0.75f * StrandHashF(sd, 7.0f));
            o.PinB = 1.0f;
            o.WidthScale = 0.38f;
            o.ColourT = 0.58f;
        }
        else
        {
            const float a0 = StrandHashF(sd, 1.0f) * kTwoPi;
            const float r0 = R * LerpF(0.18f, 0.85f, StrandHashF(sd, 2.0f));
            o.PointA = Add3(origin, { std::cos(a0) * r0, (StrandHashF(sd, 3.0f) - 0.1f) * span * 0.7f, std::sin(a0) * r0 });
            const float dLen = span * LerpF(0.32f, 0.82f, StrandHashF(sd, 4.0f));
            o.PointB = Add3(o.PointA, { (StrandHashF(sd, 5.0f) - 0.5f) * 0.22f, -dLen, (StrandHashF(sd, 6.0f) - 0.5f) * 0.22f });
            o.Sag = 0.04f;
            o.PinB = 0.0f;
            o.WidthScale = 0.34f;
            o.ColourT = 0.85f;
        }
    }
    else if (mode == 3)
    {
        const int32_t tier = slot % 3;
        const float lane = static_cast<float>(slot / 3) / std::max(1.0f, static_cast<float>(std::max(total / 3, 1) - 1));
        const float yTier = span * (0.38f + 0.26f * static_cast<float>(tier)) - span * 0.25f;
        const float zOff = LerpF(-R, R, lane) + (StrandHashF(sd, 1.0f) - 0.5f) * 0.22f;
        const float xJit = (StrandHashF(sd, 2.0f) - 0.5f) * 0.18f;
        o.PointA = Add3(origin, { -span * 0.95f + xJit, yTier, zOff });
        o.PointB = Add3(origin, {  span * 0.95f - xJit, yTier + (StrandHashF(sd, 3.0f) - 0.5f) * 0.22f, zOff + (StrandHashF(sd, 4.0f) - 0.5f) * 0.18f });
        o.Sag = baseSag * (0.72f + 0.65f * StrandHashF(sd, 5.0f));
        o.PinB = 1.0f;
        o.WidthScale = (variant == 1) ? 1.55f : 1.0f;
        o.StyleTag   = (variant == 1) ? 3.5f  : 3.0f;
        o.ColourT = static_cast<float>(tier) * 0.42f;
    }
    else if (mode == 4)
    {
        const float uX = StrandHashF(sd, 1.0f) * 2.0f - 1.0f;
        const float uZ = StrandHashF(sd, 2.0f) * 2.0f - 1.0f;
        const float yTop = origin[1] + span * (0.55f + 0.22f * StrandHashF(sd, 3.0f));
        const float vLen = span * LerpF(0.62f, 1.25f, StrandHashF(sd, 4.0f));
        o.PointA = { origin[0] + uX * R, yTop, origin[2] + uZ * R };
        o.PointB = Add3(o.PointA, { (StrandHashF(sd, 5.0f) - 0.5f) * 0.30f, -vLen, (StrandHashF(sd, 6.0f) - 0.5f) * 0.30f });
        o.Sag = 0.05f;
        o.PinB = 0.0f;
        o.WidthScale = (variant == 1) ? 2.15f : 1.05f;
        o.StyleTag   = (variant == 1) ? 4.5f  : 4.0f;
        o.ColourT = StrandHashF(sd, 7.0f);
    }
    else if (mode == 5)
    {
        const int32_t clump = slot % 6;
        const float cSd = static_cast<float>(clump) * 23.0f + 7.0f;
        const float cx = (StrandHashF(cSd, 1.0f) * 2.0f - 1.0f) * R * 0.72f;
        const float cz = (StrandHashF(cSd, 2.0f) * 2.0f - 1.0f) * R * 0.72f;
        const float ang = StrandHashF(sd, 1.0f) * kTwoPi;
        const float rOff = std::sqrt(StrandHashF(sd, 2.0f)) * R * 0.38f;
        const float hReed = span * LerpF(0.72f, 1.22f, StrandHashF(sd, 3.0f));
        const float archOut = (0.24f + 0.26f * StrandHashF(sd, 4.0f)) * hReed;
        o.PointA = { origin[0] + cx + std::cos(ang) * rOff, origin[1] - span * 0.45f, origin[2] + cz + std::sin(ang) * rOff };
        o.PointB = Add3(o.PointA, { std::cos(ang) * archOut, hReed, std::sin(ang) * archOut });
        o.Sag = -std::max(0.08f, baseSag * 0.42f) * hReed;
        o.PinB = 0.0f;
        o.WidthScale = (slot % 3 == 0) ? 1.55f : 0.95f;
        o.StyleTag   = (slot % 3 == 0) ? 5.5f  : 5.0f;
        o.ColourT = StrandHashF(sd, 5.0f);
    }
    else if (mode == 6)
    {
        const int32_t tier = slot % 3;
        const float tierFrac = static_cast<float>(tier) * 0.5f;
        const float az = static_cast<float>(slot) * 2.3999632f + (StrandHashF(sd, 1.0f) - 0.5f) * 0.16f;
        const float fLen = std::max(0.65f, R * 1.15f) * LerpF(0.54f, 1.08f, tierFrac) * LerpF(0.88f, 1.14f, StrandHashF(sd, 2.0f));
        const float tipY = span * LerpF(0.88f, 0.32f, tierFrac) * LerpF(0.88f, 1.14f, StrandHashF(sd, 3.0f));
        o.PointA = { origin[0] + std::cos(az) * 0.08f, origin[1] - span * 0.38f, origin[2] + std::sin(az) * 0.08f };
        o.PointB = Add3(o.PointA, { std::cos(az) * fLen, tipY, std::sin(az) * fLen });
        o.Sag = -std::max(0.18f, baseSag) * LerpF(0.72f, 1.22f, tierFrac);
        o.PinB = 0.0f;
        o.WidthScale = LerpF(3.2f, 4.8f, tierFrac);
        o.StyleTag = 6.0f;
        o.ColourT = tierFrac;
    }
    else if (mode == 7)
    {
        const float gx = (StrandHashF(sd, 1.0f) * 2.0f - 1.0f) * R;
        const float gz = (StrandHashF(sd, 2.0f) * 2.0f - 1.0f) * R;
        const float kelpH = span * LerpF(0.68f, 1.25f, StrandHashF(sd, 3.0f));
        const float leanX = (StrandHashF(sd, 4.0f) - 0.5f) * 0.35f;
        const float leanZ = (StrandHashF(sd, 5.0f) - 0.5f) * 0.35f;
        o.PointA = { origin[0] + gx, origin[1] - span * 0.52f, origin[2] + gz };
        o.PointB = Add3(o.PointA, { leanX, kelpH, leanZ });
        o.Sag = -baseSag * 0.18f;
        o.PinB = 0.0f;
        o.WidthScale = 2.65f;
        o.StyleTag = 7.0f;
        o.ColourT = 0.2f + 0.65f * StrandHashF(sd, 6.0f);
    }
    else if (mode == 8)
    {
        const int32_t cluster = slot % 3;
        const float cPhi = static_cast<float>(cluster) * 2.0943951f + 0.4f;
        const float cRad = (cluster == 0) ? 0.0f : (0.55f * R);
        const Vec3f mound { origin[0] + std::cos(cPhi) * cRad, origin[1] - span * 0.35f, origin[2] + std::sin(cPhi) * cRad };
        const float ringR = LerpF(0.05f, ((cluster == 0) ? 0.38f : 0.28f) * R, std::sqrt(StrandHashF(sd, 1.0f)));
        const float az = static_cast<float>(slot) * 2.3999632f + StrandHashF(sd, 2.0f) * 0.38f;
        const float tLen = span * LerpF(0.55f, 1.05f, StrandHashF(sd, 3.0f));
        const float flare = ringR * 1.45f + 0.42f * tLen;
        o.PointA = Add3(mound, { std::cos(az) * ringR, 0.06f + (0.28f - std::min(ringR, 0.25f)) * 0.45f, std::sin(az) * ringR });
        o.PointB = Add3(mound, { std::cos(az) * flare, tLen, std::sin(az) * flare });
        o.Sag = -std::max(0.14f, baseSag * 0.62f);
        o.PinB = 0.0f;
        o.WidthScale = 1.65f;
        o.StyleTag = 8.0f;
        o.ColourT = 0.55f + 0.45f * StrandHashF(sd, 4.0f);
    }
    else if (mode == 9)
    {
        const float halfSpan = span * 0.85f;
        const Vec3f poleA = Add3(origin, Mul3(rAxis, -halfSpan));
        const Vec3f poleB = Add3(origin, Mul3(rAxis,  halfSpan));
        const float spread = (variant == 1) ? 0.42f : 0.26f;
        const Vec3f offA = Mul3({ StrandHashF(sd, 1.0f) - 0.5f, StrandHashF(sd, 2.0f) - 0.5f, StrandHashF(sd, 3.0f) - 0.5f }, spread);
        const Vec3f offB = Mul3({ StrandHashF(sd, 4.0f) - 0.5f, StrandHashF(sd, 5.0f) - 0.5f, StrandHashF(sd, 6.0f) - 0.5f }, spread);
        o.PointA = Add3(poleA, offA);
        o.PointB = Add3(poleB, offB);
        if (slot > 2 && variant == 0)
        {
            const float bf = LerpF(0.22f, 0.78f, StrandHashF(sd, 7.0f));
            o.PointA = Lerp3(poleA, poleB, bf);
            const Vec3f dir = Normalise3({ StrandHashF(sd, 8.0f) - 0.5f, StrandHashF(sd, 9.0f) - 0.35f, StrandHashF(sd, 10.0f) - 0.5f });
            o.PointB = Add3(o.PointA, Mul3(dir, span * LerpF(0.28f, 0.65f, StrandHashF(sd, 11.0f))));
        }
        o.Sag = -baseSag * (0.25f + 0.55f * (StrandHashF(sd, 12.0f) - 0.5f));
        o.PinB = 1.0f;
        o.WidthScale = (slot < 2) ? 1.45f : ((variant == 1) ? 0.72f : 0.65f);
        o.StyleTag = 9.0f;
        o.ColourT = (variant == 1) ? 0.78f : 0.22f;
    }
    else if (mode == 10)
    {
        const float tick = std::floor(simTime * 10.0f);
        const float a = static_cast<float>(slot) / static_cast<float>(std::max(total, 1)) * kTwoPi + StrandHashF(static_cast<float>(slot) + tick, 1.0f) * 0.45f;
        const float ringY = (StrandHashF(static_cast<float>(slot) + tick, 2.0f) - 0.5f) * span * 0.75f;
        o.PointA = Add3(origin, { std::cos(a) * 0.14f, std::sin(a * 2.0f) * 0.08f, std::sin(a) * 0.14f });
        o.PointB = Add3(origin, { std::cos(a) * R, ringY, std::sin(a) * R });
        o.Sag = -baseSag * 0.25f;
        o.PinB = 1.0f;
        o.WidthScale = 1.05f;
        o.StyleTag = 9.0f;
        o.ColourT = FractF(static_cast<float>(slot) * 0.17f);
    }
    else if (mode == 11)
    {
        const int32_t nNodes = std::max(8, detail);
        const float tick = std::floor(simTime * 8.0f);
        const int32_t aIdx = slot % nNodes;
        const int32_t bIdx = (aIdx + 1 + static_cast<int32_t>(StrandHashF(static_cast<float>(slot) + tick * 0.5f, 1.0f) * static_cast<float>(std::max(nNodes / 2, 1)))) % nNodes;
        const float sa = static_cast<float>(aIdx) * 19.0f + 3.0f;
        const float sb = static_cast<float>(bIdx) * 19.0f + 3.0f;
        o.PointA = Add3(origin, Mul3({ StrandHashF(sa, 1.0f) - 0.5f, StrandHashF(sa, 2.0f) - 0.5f, StrandHashF(sa, 3.0f) - 0.5f }, 2.0f * R));
        o.PointB = Add3(origin, Mul3({ StrandHashF(sb, 1.0f) - 0.5f, StrandHashF(sb, 2.0f) - 0.5f, StrandHashF(sb, 3.0f) - 0.5f }, 2.0f * R));
        o.Sag = 0.0f;
        o.PinB = 1.0f;
        o.WidthScale = 0.92f;
        o.StyleTag = 9.0f;
        o.ColourT = FractF(static_cast<float>(aIdx) * 0.23f);
    }

    return o;
}

std::array<float, 3> EvaluateStrandPointCpu(
    const ParticleStrandUniformBlock& uniforms,
    const int32_t slot,
    const StrandEndpointsCpuResult& endpoints,
    const float u,
    const std::array<float, 3>& externalWind) noexcept
{
    // Reference: ParticleShaders.js L1854-L1888 (EvalStrandPoint)
    Vec3f p = Lerp3(endpoints.PointA, endpoints.PointB, u);
    const float archWeight = (endpoints.PinB > 0.5f) ? (4.0f * u * (1.0f - u)) : (u * u);
    if (endpoints.PinB > 0.5f) p[1] -= endpoints.Sag * archWeight;
    else p[1] -= endpoints.Sag * 4.0f * u * (1.0f - u) * 0.65f;

    const float swayMask = (endpoints.PinB > 0.5f) ? (4.0f * u * (1.0f - u)) : std::pow(u, 1.35f);
    const float simTime  = uniforms.AxisTime[3];
    const int32_t mode   = static_cast<int32_t>(uniforms.ModeVariantCounts[0] + 0.5f);
    const float sway     = uniforms.SagThicknessSwaySpan[2];
    const float phase    = static_cast<float>(slot) * 1.37f + u * 2.8f;
    const float waveSpeed = (mode == 7 || mode == 8) ? 1.35f : 2.25f;
    const Vec3f harmonic = Mul3({
        std::sin(simTime * waveSpeed + phase),
        std::cos(simTime * waveSpeed * 0.7f + phase) * 0.28f,
        std::cos(simTime * waveSpeed * 0.9f + phase * 1.2f)
    }, 0.14f * sway);
    p = Add3(p, Mul3(Add3(externalWind, harmonic), swayMask));

    if (endpoints.StyleTag > 8.5f)
    {
        const int32_t variant = static_cast<int32_t>(uniforms.ModeVariantCounts[1] + 0.5f);
        const int32_t detail  = static_cast<int32_t>(uniforms.SegmentsDetailViewportSwatch[1] + 0.5f);
        const float rate      = (variant == 1) ? 26.0f : 15.0f;
        const float tick      = std::floor(simTime * rate);
        const float jitterAmp = (0.12f + 0.015f * static_cast<float>(detail)) * std::max(0.35f, uniforms.SagThicknessSwaySpan[3] * 0.45f);
        const float env       = std::sin(u * kPi);
        const float sA        = static_cast<float>(slot) * 31.0f + std::floor(u * 12.0f) + tick * 7.0f;
        const float sB        = static_cast<float>(slot) * 53.0f + std::floor(u * 24.0f) + tick * 13.0f;
        const Vec3f jA { StrandHashF(sA, 1.0f) - 0.5f, StrandHashF(sA, 2.0f) - 0.5f, StrandHashF(sA, 3.0f) - 0.5f };
        const Vec3f jB { StrandHashF(sB, 4.0f) - 0.5f, StrandHashF(sB, 5.0f) - 0.5f, StrandHashF(sB, 6.0f) - 0.5f };
        p = Add3(p, Mul3(Add3(Mul3(jA, 0.72f), Mul3(jB, 0.28f)), jitterAmp * env));
    }
    return p;
}

StrandFragmentCpuResult EvaluateStrandFragmentCpu(
    const ParticleStrandUniformBlock& uniforms,
    const std::array<float, 3>& inputColour,
    const float u,
    const float across,
    const float tag,
    const float seed,
    const float inputAlpha) noexcept
{
    // Reference: ParticleShaders.js L1948-L2073 (StrandFragment)
    StrandFragmentCpuResult out {};
    if (inputAlpha <= 0.002f) { out.Discarded = true; return out; }

    const float aAbs    = std::fabs(across);
    const float glow    = uniforms.GlowBrightnessEnvelope[0];
    const float simTime = uniforms.AxisTime[3];
    Vec3f rgb   = inputColour;
    float alpha = inputAlpha;

    if (tag < 2.5f)
    {
        const float core = std::exp(-aAbs * aAbs * 3.2f);
        const float dOff = FractF(u * 4.0f + seed) - 0.5f;
        const float dew  = (tag > 1.25f) ? (std::exp(-(dOff * dOff) * 64.0f) * glow) : 0.0f;
        rgb   = Lerp3(Mul3(rgb, 0.95f), { 1.0f, 1.0f, 1.0f }, ClampF(core * 0.55f + dew, 0.0f, 1.0f));
        alpha *= ClampF(core * 0.92f + dew, 0.0f, 1.0f);
    }
    else if (tag < 3.8f)
    {
        const float cyl = std::sqrt(std::max(0.0f, 1.0f - aAbs * aAbs));
        if (tag > 3.25f)
        {
            const float link = std::fabs(std::sin(u * 48.0f));
            const float hole = SmoothStepF(0.28f, 0.45f, aAbs) + SmoothStepF(0.70f, 0.92f, link);
            if (hole < 0.38f) { out.Discarded = true; return out; }
            const float spec = 0.35f * std::pow(cyl, 6.0f) * (0.5f + glow);
            rgb = Add3(Mul3(rgb, 0.45f + 0.75f * cyl * link), { spec, spec, spec });
        }
        else
        {
            const float braid = 0.85f + 0.15f * std::sin(u * 80.0f + across * 8.0f);
            const float spec  = 0.28f * std::pow(cyl, 8.0f);
            rgb = Add3(Mul3(rgb, (0.42f + 0.68f * cyl) * braid), { spec, spec, spec });
        }
        alpha *= 1.0f - SmoothStepF(0.78f, 1.0f, aAbs);
    }
    else if (tag < 4.8f)
    {
        if (tag > 4.25f)
        {
            const float stem = 1.0f - SmoothStepF(0.14f, 0.24f, aAbs);
            const float leafPhase = FractF(u * 14.0f + seed * 3.0f) - 0.5f;
            const float sideAlt = (std::sin(std::floor(u * 14.0f + seed * 3.0f) * 3.14159f + 1.57f) > 0.0f) ? 1.0f : -1.0f;
            const float lpx = (across * sideAlt - 0.48f) / 0.48f;
            const float lpy = leafPhase / 0.34f;
            const float leaf = 1.0f - SmoothStepF(0.75f, 1.0f, std::hypot(lpx, lpy));
            const float mask = std::max(stem, leaf);
            if (mask < 0.05f) { out.Discarded = true; return out; }
            rgb = Lerp3(Mul3(rgb, 0.52f), Mul3(rgb, 0.95f + 0.35f * (1.0f - std::fabs(leafPhase))), leaf);
            alpha *= mask;
        }
        else
        {
            const float cyl = std::sqrt(std::max(0.0f, 1.0f - aAbs * aAbs));
            const float twist = 0.80f + 0.20f * std::sin(u * 65.0f + across * 5.0f);
            rgb = Mul3(rgb, (0.48f + 0.60f * cyl) * twist);
            alpha *= 1.0f - SmoothStepF(0.78f, 1.0f, aAbs);
        }
    }
    else if (tag < 5.8f)
    {
        const float isCattail = (tag > 5.25f && u > 0.70f && u < 0.91f) ? 1.0f : 0.0f;
        const float halfW = (isCattail > 0.5f) ? 0.92f : 0.38f;
        if (aAbs > halfW) { out.Discarded = true; return out; }
        const float ratio = aAbs / halfW;
        const float cyl = std::sqrt(std::max(0.0f, 1.0f - ratio * ratio));
        const Vec3f velvet = Mul3({ 0.30f, 0.14f, 0.06f }, 0.65f + 0.55f * cyl);
        rgb = Lerp3(Mul3(rgb, 0.55f + 0.55f * cyl), velvet, isCattail);
        alpha *= 1.0f - SmoothStepF(halfW * 0.75f, halfW, aAbs);
    }
    else if (tag < 6.5f)
    {
        const float vein = 1.0f - SmoothStepF(0.04f, 0.11f, aAbs);
        const float leaflet = std::sin(u * 46.0f + aAbs * 6.0f);
        const float tipEnv = std::sin(ClampF(u, 0.0f, 1.0f) * kPi);
        const float bladeEdge = (0.35f + 0.65f * SmoothStepF(-0.35f, 0.55f, leaflet)) * std::max(tipEnv, 0.12f);
        const float mask = std::max(vein, 1.0f - SmoothStepF(bladeEdge - 0.08f, bladeEdge + 0.04f, aAbs));
        if (mask < 0.06f) { out.Discarded = true; return out; }
        rgb = Lerp3(Mul3(rgb, 0.72f + 0.35f * (1.0f - aAbs)), Add3(Mul3(rgb, 1.45f), Mul3({ 0.18f, 0.24f, 0.08f }, glow)), vein);
        alpha *= mask;
    }
    else if (tag < 7.5f)
    {
        const float stipe = 1.0f - SmoothStepF(0.10f, 0.19f, aAbs);
        const float ruffle = 0.58f + 0.38f * std::sin(u * 26.0f + seed * 12.0f + simTime * 1.4f) * std::sin(u * 3.14159f);
        const float blade = 1.0f - SmoothStepF(ruffle - 0.10f, ruffle + 0.04f, aAbs);
        const float bOff = FractF(u * 9.0f + seed) - 0.5f;
        const float bladder = std::exp(-(bOff * bOff) * 70.0f) * (1.0f - SmoothStepF(0.12f, 0.30f, aAbs));
        const float mask = std::max(stipe, blade * 0.85f);
        if (mask < 0.06f) { out.Discarded = true; return out; }
        rgb = Lerp3(Mul3(rgb, 0.65f + 0.35f * (1.0f - aAbs)), Mul3({ 0.95f, 0.78f, 0.28f }, 1.0f + glow * 0.4f), ClampF(bladder, 0.0f, 1.0f));
        alpha *= mask * 0.92f;
    }
    else if (tag < 8.5f)
    {
        const float cyl = std::sqrt(std::max(0.0f, 1.0f - aAbs * aAbs));
        const float tipGlow = SmoothStepF(0.62f, 0.98f, u) * (0.65f + glow);
        rgb = Add3(Mul3(rgb, 0.48f + 0.58f * cyl), Mul3(Lerp3(rgb, { 0.75f, 0.96f, 1.0f }, 0.55f), tipGlow));
        alpha *= (1.0f - SmoothStepF(0.72f, 1.0f, aAbs)) * (0.75f + 0.25f * tipGlow);
    }
    else
    {
        const float core = std::exp(-aAbs * aAbs * 14.0f);
        const float corona = std::exp(-aAbs * aAbs * 2.4f) * (0.55f + glow * 0.55f);
        const float flicker = 0.82f + 0.18f * std::sin(simTime * 42.0f + seed * 31.0f + u * 12.0f);
        rgb = Mul3(Add3(Mul3(rgb, corona), Mul3({ 1.0f, 0.98f, 1.0f }, core * 1.65f)), flicker);
        alpha *= ClampF(core + corona * 0.65f, 0.0f, 1.0f);
    }

    if (alpha <= 0.005f) { out.Discarded = true; return out; }
    out.Rgb = rgb;
    out.Alpha = alpha;
    out.Discarded = false;
    return out;
}

std::vector<ParticleStrandDrawCall> ParticleGpuRenderer::BuildStrandRenderCalls(
    const std::span<const ParticleCloudNode> clouds,
    const ParticleSimulationRegion& region,
    const ParticleCameraView& camera,
    const float simulationTimeSeconds,
    const uint32_t forceRecordCount,
    const bool hasGasVolume) const
{
    // Reference: ParticleSimulation.js L449-L505, L578-L584
    std::vector<ParticleStrandDrawCall> calls;
    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Visible || !node.Enabled || node.Halted) continue;
        const ParticleCloudParameters& p = node.Evaluated;
        const uint32_t mode = static_cast<uint32_t>(p.StrandMode);
        if (mode == 0u) continue;

        const ParticleStrandUniformBlock uniforms = BuildParticleStrandUniforms(
            p, region, camera, simulationTimeSeconds, forceRecordCount, hasGasVolume);
        const uint32_t totalStrands = static_cast<uint32_t>(uniforms.ModeVariantCounts[3] + 0.5f);
        const uint32_t segments     = static_cast<uint32_t>(uniforms.SegmentsDetailViewportSwatch[0] + 0.5f);
        const uint32_t vertices     = totalStrands * segments * 6u;
        if (vertices == 0u) continue;

        const bool additiveStrand = (mode >= 9u) || (p.Blend == ParticleBlendOrdinal::Additive);
        calls.push_back(ParticleStrandDrawCall {
            node.NodeId,
            p.StrandMode,
            p.StrandVariant,
            additiveStrand ? ParticleBlendOrdinal::Additive : ParticleBlendOrdinal::Alpha,
            totalStrands,
            segments,
            vertices,
            uniforms
        });
    }
    return calls;
}

} // namespace Frontier::ParticleSpace
