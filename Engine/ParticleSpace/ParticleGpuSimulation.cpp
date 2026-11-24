/*============================================================================================================================================
                                                      PARTICLEGPUSIMULATION.CPP
============================================================================================================================================*/
// 🧩 Core ParticleSpace GPU uniform layout, anchor table builder, per-cloud state manager & bit-accurate CPU mirror of ParticleUpdateCompute.slang
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L9-L752 & ParticleSimulation.js L1-L340

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
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

[[nodiscard]] float FractF(const float x) noexcept { return x - std::floor(x); }
[[nodiscard]] float ClampF(const float x, const float lo, const float hi) noexcept { return std::clamp(x, lo, hi); }
[[nodiscard]] float LerpF(const float a, const float b, const float t) noexcept { return a + (b - a) * t; }
[[nodiscard]] float SmoothStepF(const float edge0, const float edge1, const float x) noexcept
{
    const float denom = edge1 - edge0;
    const float t = (std::fabs(denom) > 1.0e-9f) ? ClampF((x - edge0) / denom, 0.0f, 1.0f) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] Vec3f Add3(const Vec3f& a, const Vec3f& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] Vec3f Sub3(const Vec3f& a, const Vec3f& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
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
[[nodiscard]] float Len3(const Vec3f& a) noexcept { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
[[nodiscard]] Vec3f Normalize3(const Vec3f& a) noexcept
{
    const float len = Len3(a);
    return (len > 1.0e-12f) ? Vec3f { a[0] / len, a[1] / len, a[2] / len } : Vec3f { 0.0f, 0.0f, 0.0f };
}
[[nodiscard]] Vec3f SafeAxis3(const Vec3f& a) noexcept
{
    const float len = Len3(a);
    return (len > 1.0e-6f) ? Vec3f { a[0] / len, a[1] / len, a[2] / len } : Vec3f { 0.0f, 1.0f, 0.0f };
}

[[nodiscard]] Vec3f ParseHexRgbSrgb(const std::string_view hex) noexcept
{
    std::string_view digits = hex;
    if (!digits.empty() && digits.front() == '#') digits.remove_prefix(1u);
    if (digits.size() != 6u) return { 0.0f, 240.0f / 255.0f, 1.0f };
    char buf[8] {};
    for (size_t i = 0u; i < 6u; ++i) buf[i] = digits[i];
    const unsigned long val = std::strtoul(buf, nullptr, 16);
    return {
        static_cast<float>((val >> 16u) & 255ul) / 255.0f,
        static_cast<float>((val >> 8u) & 255ul) / 255.0f,
        static_cast<float>(val & 255ul) / 255.0f
    };
}

void ComputeShapeBasis(const Vec3f& axis, Vec3f& t, Vec3f& b) noexcept
{
    const Vec3f helper = (std::fabs(axis[1]) < 0.95f) ? Vec3f { 0.0f, 1.0f, 0.0f } : Vec3f { 1.0f, 0.0f, 0.0f };
    t = Normalize3(Cross3(helper, axis));
    b = Cross3(axis, t);
}

[[nodiscard]] Vec3f BoxShellLocal(const int32_t index, const float count) noexcept
{
    const int32_t g = std::max(1, static_cast<int32_t>(std::floor(std::sqrt(std::max(count, 6.0f) / 6.0f))));
    const int32_t cells = g * g;
    const int32_t face = index - (index / 6) * 6;
    const int32_t cell = (index / 6) % cells;
    const int32_t cx = cell - (cell / g) * g;
    const int32_t cy = cell / g;
    const float cu = (static_cast<float>(cx) + 0.5f) / static_cast<float>(g) * 2.0f - 1.0f;
    const float cv = (static_cast<float>(cy) + 0.5f) / static_cast<float>(g) * 2.0f - 1.0f;
    const int32_t ax = face / 2;
    const float sg = ((face - ax * 2) == 0) ? 1.0f : -1.0f;
    if (ax == 0) return { sg, cu, cv };
    if (ax == 1) return { cv, sg, cu };
    return { cu, cv, sg };
}

[[nodiscard]] Vec3f FetchAnchorPosition(
    std::span<const float> anchorRgba,
    const int32_t anchorCount,
    const int32_t index,
    Vec3f& outNormal) noexcept
{
    if (anchorCount < 1 || anchorRgba.size() < 8u)
    {
        outNormal = { 0.0f, 1.0f, 0.0f };
        return { 0.0f, 0.0f, 0.0f };
    }
    const int32_t slot = std::clamp(index, 0, anchorCount - 1);
    const size_t base = static_cast<size_t>(slot) * 8u;
    if (base + 7u >= anchorRgba.size())
    {
        outNormal = { 0.0f, 1.0f, 0.0f };
        return { 0.0f, 0.0f, 0.0f };
    }
    outNormal = { anchorRgba[base + 4u], anchorRgba[base + 5u], anchorRgba[base + 6u] };
    return { anchorRgba[base + 0u], anchorRgba[base + 1u], anchorRgba[base + 2u] };
}

void MocapBoneEndpoints(const int32_t bone, const float phase, const int32_t clip, Vec3f& a, Vec3f& b, float& rad) noexcept
{
    const float s = std::sin(phase), c = std::cos(phase);
    const float s2 = std::sin(phase * 2.0f);
    Vec3f pelvis { 0.0f, 0.05f, 0.0f };
    Vec3f chest  { 0.0f, 0.52f, 0.0f };
    Vec3f neck   { 0.0f, 0.68f, 0.0f };
    Vec3f head   { 0.0f, 0.92f, 0.0f };
    Vec3f shL { -0.22f, 0.62f, 0.0f },  shR { 0.22f, 0.62f, 0.0f };
    Vec3f elL { -0.36f, 0.34f, 0.04f }, elR { 0.36f, 0.34f, 0.04f };
    Vec3f wrL { -0.44f, 0.08f, 0.10f }, wrR { 0.44f, 0.08f, 0.10f };
    Vec3f hpL { -0.12f, 0.02f, 0.0f },  hpR { 0.12f, 0.02f, 0.0f };
    Vec3f knL { -0.14f, -0.44f, 0.03f }, knR { 0.14f, -0.44f, 0.03f };
    Vec3f anL { -0.15f, -0.88f, 0.0f }, anR { 0.15f, -0.88f, 0.0f };

    if (clip == 1)
    {
        const float bob = std::fabs(s) * 0.07f;
        pelvis = { s * 0.03f, 0.04f + bob, 0.0f };
        chest  = { 0.0f, 0.50f + bob, 0.12f };
        neck   = { 0.0f, 0.66f + bob, 0.16f };
        head   = { 0.0f, 0.89f + bob, 0.19f };
        shL = Add3(chest, { -0.21f, 0.10f, -s * 0.06f });
        shR = Add3(chest, {  0.21f, 0.10f,  s * 0.06f });
        elL = Add3(shL, { -0.06f, -0.24f, -s * 0.22f - 0.04f });
        wrL = Add3(elL, {  0.04f,  0.06f + std::max(0.0f, -s) * 0.12f, -s * 0.18f + 0.22f });
        elR = Add3(shR, {  0.06f, -0.24f,  s * 0.22f - 0.04f });
        wrR = Add3(elR, { -0.04f,  0.06f + std::max(0.0f,  s) * 0.12f,  s * 0.18f + 0.22f });
        hpL = Add3(pelvis, { -0.12f, -0.02f,  s * 0.04f });
        hpR = Add3(pelvis, {  0.12f, -0.02f, -s * 0.04f });
        const float liftL = std::max(0.0f, s), liftR = std::max(0.0f, -s);
        knL = Add3(hpL, { -0.01f, -0.38f + liftL * 0.24f,  s * 0.30f + 0.06f });
        anL = Add3(knL, {  0.0f,  -0.40f + liftL * 0.14f,  s * 0.22f - 0.18f });
        knR = Add3(hpR, {  0.01f, -0.38f + liftR * 0.24f, -s * 0.30f + 0.06f });
        anR = Add3(knR, {  0.0f,  -0.40f + liftR * 0.14f, -s * 0.22f - 0.18f });
    }
    else if (clip == 2)
    {
        const float bounce = -std::fabs(s2) * 0.08f;
        const float sway = s * 0.16f;
        pelvis = { sway, 0.04f + bounce, 0.0f };
        chest  = { sway * 0.55f, 0.50f + bounce, c * 0.05f };
        neck   = Add3(chest, { -sway * 0.15f, 0.16f, 0.02f });
        head   = Add3(neck, { s2 * 0.05f, 0.23f, 0.02f });
        shL = Add3(chest, { -0.22f, 0.10f + s * 0.05f, 0.0f });
        shR = Add3(chest, {  0.22f, 0.10f - s * 0.05f, 0.0f });
        elL = Add3(shL, { -0.20f, 0.06f + s * 0.22f, 0.12f });
        wrL = Add3(elL, { -0.10f + c * 0.12f, 0.20f + s * 0.22f, 0.14f });
        elR = Add3(shR, {  0.20f, 0.06f - s * 0.22f, 0.12f });
        wrR = Add3(elR, {  0.10f - c * 0.12f, 0.20f - s * 0.22f, 0.14f });
        hpL = Add3(pelvis, { -0.12f, -0.02f, 0.0f });
        hpR = Add3(pelvis, {  0.12f, -0.02f, 0.0f });
        knL = Add3(hpL, { -0.08f + std::max(0.0f, -s) * 0.12f, -0.40f, 0.10f + std::max(0.0f, s) * 0.14f });
        anL = { -0.22f + sway * 0.4f, -0.88f + std::max(0.0f, s) * 0.16f, s * 0.14f };
        knR = Add3(hpR, {  0.08f - std::max(0.0f,  s) * 0.12f, -0.40f, 0.10f + std::max(0.0f, -s) * 0.14f });
        anR = {  0.22f + sway * 0.4f, -0.88f + std::max(0.0f, -s) * 0.16f, -s * 0.14f };
    }
    else if (clip == 3)
    {
        const float strike = std::pow(std::max(0.0f, s), 2.0f);
        const float kick = std::pow(std::max(0.0f, -s), 2.0f);
        const float yaw = c * 0.45f;
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        pelvis = { 0.0f, -0.02f, 0.0f };
        chest  = { sy * 0.08f, 0.46f, cy * 0.06f };
        neck   = Add3(chest, { 0.0f, 0.16f, 0.02f });
        head   = Add3(neck, { 0.0f, 0.22f, 0.04f });
        shL = Add3(chest, { -0.21f * cy, 0.10f, -0.21f * sy });
        shR = Add3(chest, {  0.21f * cy, 0.10f,  0.21f * sy });
        elL = Add3(shL, { -0.12f, -0.16f, 0.18f + strike * 0.18f });
        wrL = Add3(elL, {  0.06f,  0.04f, 0.20f + strike * 0.36f });
        elR = Add3(shR, {  0.14f, -0.18f, 0.12f });
        wrR = Add3(elR, { -0.08f,  0.12f, 0.18f });
        hpL = Add3(pelvis, { -0.13f, -0.02f, -0.05f });
        hpR = Add3(pelvis, {  0.13f, -0.02f,  0.05f });
        knL = Add3(hpL, { -0.08f, -0.42f, -0.08f });
        anL = { -0.24f, -0.88f, -0.14f };
        knR = Add3(hpR, { 0.08f, -0.36f + kick * 0.42f, 0.16f + kick * 0.22f });
        anR = Add3(knR, { 0.08f * kick, -0.44f + kick * 0.48f, 0.10f + kick * 0.38f });
    }
    else if (clip == 4)
    {
        const float hover = std::sin(phase * 0.7f) * 0.14f;
        pelvis = { 0.0f, 0.12f + hover, 0.0f };
        chest  = { 0.0f, 0.58f + hover, -0.06f };
        neck   = Add3(chest, { 0.0f, 0.16f, -0.03f });
        head   = Add3(neck, { 0.0f, 0.22f, -0.04f });
        shL = Add3(chest, { -0.22f, 0.09f, 0.0f });
        shR = Add3(chest, {  0.22f, 0.09f, 0.0f });
        elL = Add3(shL, { -0.22f,  0.02f + s * 0.08f, 0.08f });
        wrL = Add3(elL, { -0.18f, -0.06f + c * 0.10f, 0.12f });
        elR = Add3(shR, {  0.22f,  0.02f - s * 0.08f, 0.08f });
        wrR = Add3(elR, {  0.18f, -0.06f - c * 0.10f, 0.12f });
        hpL = Add3(pelvis, { -0.12f, -0.03f, 0.0f });
        hpR = Add3(pelvis, {  0.12f, -0.03f, 0.0f });
        knL = Add3(hpL, { -0.06f, -0.38f,  0.14f + s * 0.06f });
        anL = Add3(knL, { -0.03f, -0.38f, -0.12f });
        knR = Add3(hpR, {  0.06f, -0.34f,  0.22f - s * 0.06f });
        anR = Add3(knR, {  0.03f, -0.38f, -0.10f });
    }

    if (bone == 0)      { a = neck;   b = head;  rad = 0.105f; }
    else if (bone == 1) { a = pelvis; b = chest; rad = 0.155f; }
    else if (bone == 2) { a = hpL;    b = hpR;   rad = 0.125f; }
    else if (bone == 3) { a = shL;    b = elL;   rad = 0.055f; }
    else if (bone == 4) { a = elL;    b = wrL;   rad = 0.044f; }
    else if (bone == 5) { a = shR;    b = elR;   rad = 0.055f; }
    else if (bone == 6) { a = elR;    b = wrR;   rad = 0.044f; }
    else if (bone == 7) { a = hpL;    b = knL;   rad = 0.075f; }
    else if (bone == 8) { a = knL;    b = anL;   rad = 0.054f; }
    else if (bone == 9) { a = hpR;    b = knR;   rad = 0.075f; }
    else                { a = knR;    b = anR;   rad = 0.054f; }
}

[[nodiscard]] Vec3f MocapSamplePoint(
    const ParticleShapeUniformBlock& shape,
    const int32_t index,
    const Vec3f& rnd,
    const bool lattice,
    const float phase,
    const int32_t clip) noexcept
{
    const int32_t slot = index - (index / 16) * 16;
    const int32_t bone = slot < 2 ? 0 : (slot < 5 ? 1 : (slot == 5 ? 2 : (slot == 6 ? 3 : (slot == 7 ? 4 : (slot == 8 ? 5 : (slot == 9 ? 6 : (slot < 12 ? 7 : (slot == 12 ? 8 : (slot < 15 ? 9 : 10)))))))));
    Vec3f a {}, b {};
    float rad = 0.054f;
    MocapBoneEndpoints(bone, phase, clip, a, b, rad);
    const float along = lattice ? FractF(static_cast<float>(index) * 0.6180339f + static_cast<float>(bone) * 0.13f) : rnd[0];
    const float ang = lattice ? (static_cast<float>(index) * 2.39996323f) : (rnd[1] * kTwoPi);
    const float shell = LerpF(1.0f, std::sqrt(lattice ? FractF(static_cast<float>(index) * 0.381966f) : rnd[2]), shape.AxisFill[3]);
    const Vec3f seg = Sub3(b, a);
    const Vec3f dir = (Len3(seg) > 1.0e-4f) ? Normalize3(seg) : Vec3f { 0.0f, 1.0f, 0.0f };
    const Vec3f helper = (std::fabs(dir[1]) < 0.92f) ? Vec3f { 0.0f, 1.0f, 0.0f } : Vec3f { 1.0f, 0.0f, 0.0f };
    const Vec3f u = Normalize3(Cross3(helper, dir));
    const Vec3f v = Cross3(dir, u);
    Vec3f local = Add3(Lerp3(a, b, along), Mul3(Add3(Mul3(u, std::cos(ang)), Mul3(v, std::sin(ang))), rad * shell));
    if (bone == 0)
    {
        const Vec3f hc = Lerp3(a, b, 0.58f);
        const float hz = along * 2.0f - 1.0f;
        const float hr = std::sqrt(std::max(0.0f, 1.0f - hz * hz));
        local = Add3(hc, Mul3({ std::cos(ang) * hr, hz, std::sin(ang) * hr }, rad * 1.08f * shell));
    }
    const float figureScale = std::max(0.25f, shape.OriginRadius[3] * shape.MocapParams[3]);
    return Add3({ shape.OriginRadius[0], shape.OriginRadius[1], shape.OriginRadius[2] }, Mul3(local, figureScale));
}

[[nodiscard]] Vec3f EvaluateMocapPose(
    const ParticleShapeUniformBlock& shape,
    const int32_t index,
    const Vec3f& rnd,
    const bool lattice,
    const float timeSeconds,
    Vec3f& outLimbVelocity) noexcept
{
    outLimbVelocity = { 0.0f, 0.0f, 0.0f };
    const int32_t clip = static_cast<int32_t>(shape.MocapParams[0] + 0.5f);
    const float mocapAmp = shape.MocapParams[1];
    const float mocapSpeed = shape.MocapParams[2];
    const float phase = timeSeconds * mocapSpeed * 4.2f;
    const Vec3f rest = MocapSamplePoint(shape, index, rnd, lattice, 0.0f, 0);
    if (clip <= 0 || mocapAmp <= 0.0001f) return rest;
    const Vec3f posed = MocapSamplePoint(shape, index, rnd, lattice, phase, clip);
    const Vec3f prev  = MocapSamplePoint(shape, index, rnd, lattice, phase - 0.12f * std::max(mocapSpeed, 0.1f), clip);
    const float amp = ClampF(mocapAmp, 0.0f, 2.0f);
    outLimbVelocity = Mul3(Sub3(posed, prev), 8.0f * amp * shape.MocapKickEffector[0]);
    return Lerp3(rest, posed, amp);
}

[[nodiscard]] Vec3f RandomDirection(const Vec3f& seed) noexcept
{
    using SimulationSpace::ForceFields::Hash31;
    const float z = Hash31(seed) * 2.0f - 1.0f;
    const float a = Hash31({ seed[2] + 5.1f, seed[0] + 5.1f, seed[1] + 5.1f }) * kTwoPi;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return { r * std::cos(a), z, r * std::sin(a) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             UNIFORM CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

ParticleShapeUniformBlock BuildParticleShapeUniforms(
    const ParticleCloudParameters& params,
    const uint32_t textureSide,
    const uint32_t anchorWidth,
    const uint32_t anchorCount) noexcept
{
    ParticleShapeUniformBlock block {};
    const Vec3f axis = SafeAxis3(params.Direction);
    const uint32_t clampedCount = std::min(params.Count, textureSide * textureSide);
    const Vec3f edgeRgb = ParseHexRgbSrgb(params.DerezEdgeColour);

    block.OriginRadius        = { params.Position[0], params.Position[1], params.Position[2], std::max(0.01f, params.Radius) };
    block.AxisFill            = { axis[0], axis[1], axis[2], params.Fill };
    block.ShapeCounts         = { static_cast<float>(params.Shape), static_cast<float>(clampedCount), params.ShapeDetail, static_cast<float>(params.Mode) };
    block.AnchorLayout        = { static_cast<float>(std::max(1u, anchorWidth)), static_cast<float>(anchorCount), static_cast<float>(std::max(1u, textureSide)), 0.0f };
    block.MocapParams         = { static_cast<float>(params.MocapClip), params.MocapAmplitude, params.MocapSpeed, params.MocapScale };
    block.MocapKickEffector   = { params.MocapTrailKick, static_cast<float>(params.EffectorMode), params.EffectorStrength, params.EffectorFrequency };
    block.EffectorMotion      = { params.EffectorSpeed, params.EffectorPush, params.EffectorScaleBoost, params.EffectorColourShift };
    block.EffectorAxisDerez   = { static_cast<float>(params.EffectorAxis), static_cast<float>(params.DerezMode), params.DerezMix, params.DerezProgress };
    block.DerezFrontParams    = { params.DerezAutoSpeed, params.DerezEdgeWidth, params.DerezEdgeGlow, params.DerezShatter };
    block.DerezEdgeColourWing = { edgeRgb[0], edgeRgb[1], edgeRgb[2], params.SwarmWingRate };
    block.GlitchParams        = { params.GlitchAmount, params.GlitchRate, params.GlitchStepJitter, params.GlitchScanlines };
    return block;
}

ParticleUpdateUniformBlock BuildParticleUpdateUniforms(
    const ParticleCloudNode& node,
    const uint32_t textureSide,
    const uint32_t anchorWidth,
    const uint32_t anchorCount,
    const ParticleSimulationRegion& region,
    const float simulationTimeSeconds,
    const float deltaTimeSeconds,
    const bool resetFlag,
    const uint32_t forceRecordCount,
    const bool hasGasVolume) noexcept
{
    const ParticleCloudParameters& p = node.Evaluated;
    ParticleUpdateUniformBlock block {};
    block.Shape = BuildParticleShapeUniforms(p, textureSide, anchorWidth, anchorCount);

    SimulationSpace::ForceFields::TimingEnvelopeParameters timing {};
    timing.StartTimeSeconds = p.StartTime;
    timing.DurationSeconds  = p.Duration;
    timing.FadeSeconds      = p.Fade;
    timing.RepeatSeconds    = p.Repeat;
    const float envelope = static_cast<float>(
        SimulationSpace::ForceFields::EvaluateNodeTimingWeight(node.Tracks, timing, static_cast<double>(simulationTimeSeconds)));

    block.GridCentreHalfX   = { region.CentreMeters[0], region.CentreMeters[1], region.CentreMeters[2], region.HalfExtentMeters[0] };
    block.GridHalfYZTimeDt  = { region.HalfExtentMeters[1], region.HalfExtentMeters[2], simulationTimeSeconds, deltaTimeSeconds };
    block.SpawnKinematics   = { envelope, p.Rate, std::max(0.05f, p.Lifetime), p.Speed };
    block.LaunchForces      = { p.Spread, p.Radial, p.Gravity, p.Drag };
    block.CouplingNoise     = { p.Noise, p.FieldInfluence, p.GasInfluence, p.NoiseScale };
    block.SpringBounce      = { p.Spring, p.Damping, p.Bounce, p.NoiseSpeed };
    block.DissolveSwarm     = { p.Dissolve, p.Linger, p.SwarmStrength, p.SwarmCohesion };
    block.SwarmControlFlags = { p.SwarmOrbit, resetFlag ? 1.0f : 0.0f, static_cast<float>(forceRecordCount), hasGasVolume ? 1.0f : 0.0f };
    return block;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           STATELESS CPU SHADER MIRROR
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> EvaluateShapePointAtTime(
    const ParticleShapeUniformBlock& shape,
    std::span<const float> anchorTexelRgba,
    const int32_t index,
    const std::array<float, 3>& rnd,
    const bool lattice,
    const float timeSeconds,
    std::array<float, 3>* outShapeVelocity) noexcept
{
    using SimulationSpace::ForceFields::Hash31;
    Vec3f shapeVel { 0.0f, 0.0f, 0.0f };
    const Vec3f origin { shape.OriginRadius[0], shape.OriginRadius[1], shape.OriginRadius[2] };
    const float radius = shape.OriginRadius[3];
    const Vec3f axis { shape.AxisFill[0], shape.AxisFill[1], shape.AxisFill[2] };
    const float fill = shape.AxisFill[3];
    const int32_t shapeCode = static_cast<int32_t>(shape.ShapeCounts[0] + 0.5f);
    const int32_t countInt  = std::max(1, static_cast<int32_t>(shape.ShapeCounts[1] + 0.5f));
    const float shapeDetail = shape.ShapeCounts[2];
    const int32_t anchorCount = static_cast<int32_t>(shape.AnchorLayout[1] + 0.5f);

    Vec3f t {}, b {};
    ComputeShapeBasis(axis, t, b);
    const float count = static_cast<float>(countInt);
    const float fi = static_cast<float>(index);

    if (shapeCode == 9)
    {
        const Vec3f posed = EvaluateMocapPose(shape, index, rnd, lattice, timeSeconds, shapeVel);
        if (outShapeVelocity) *outShapeVelocity = shapeVel;
        return posed;
    }

    Vec3f basePos = origin;
    if (shapeCode == 1)
    {
        if (lattice)
        {
            const float golden = 2.39996323f;
            const float y = 1.0f - (fi + 0.5f) / count * 2.0f;
            const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float a = golden * fi;
            const float shell = LerpF(1.0f, std::pow(Hash31({ fi, 3.1f, 7.7f }), 1.0f / 3.0f), fill);
            basePos = Add3(origin, Mul3({ std::cos(a) * r, y, std::sin(a) * r }, radius * shell));
        }
        else
        {
            const Vec3f d = Normalize3({ rnd[0] * 2.0f - 1.0f + 1.0e-4f, rnd[1] * 2.0f - 1.0f + 1.0e-4f, rnd[2] * 2.0f - 1.0f + 1.0e-4f });
            const float s = std::pow(Hash31({ rnd[2] + 2.7f, rnd[0] + 2.7f, rnd[1] + 2.7f }), 1.0f / 3.0f);
            basePos = Add3(origin, Mul3(d, radius * s));
        }
    }
    else if (shapeCode == 2 || shapeCode == 6)
    {
        Vec3f cell {};
        if (lattice || shapeCode == 6)
        {
            const float side = std::ceil(std::pow(count, 1.0f / 3.0f));
            const float x = fi - side * std::floor(fi / side);
            const float yRaw = std::floor(fi / side);
            const float y = yRaw - side * std::floor(yRaw / side);
            const float z = std::floor(fi / (side * side));
            cell = { (x + 0.5f) / side * 2.0f - 1.0f, (y + 0.5f) / side * 2.0f - 1.0f, (z + 0.5f) / side * 2.0f - 1.0f };
        }
        else
        {
            cell = { rnd[0] * 2.0f - 1.0f, rnd[1] * 2.0f - 1.0f, rnd[2] * 2.0f - 1.0f };
        }
        basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, cell[0]), Mul3(axis, cell[1])), Mul3(b, cell[2])), radius));
    }
    else if (shapeCode == 3)
    {
        const float a = lattice ? (2.39996323f * fi) : (rnd[0] * kTwoPi);
        const float r = lattice ? std::sqrt((fi + 0.5f) / count) : std::sqrt(rnd[1]);
        basePos = Add3(origin, Mul3(Add3(Mul3(t, std::cos(a)), Mul3(b, std::sin(a))), r * radius));
    }
    else if (shapeCode == 4)
    {
        const float a = lattice ? (fi / count * kTwoPi) : (rnd[0] * kTwoPi);
        basePos = Add3(origin, Mul3(Add3(Mul3(t, std::cos(a)), Mul3(b, std::sin(a))), radius));
    }
    else if (shapeCode == 5)
    {
        const float s = lattice ? ((fi + 0.5f) / count * 2.0f - 1.0f) : (rnd[0] * 2.0f - 1.0f);
        basePos = Add3(origin, Mul3(axis, s * radius));
    }
    else if (shapeCode == 7)
    {
        const float u = lattice ? ((fi + 0.5f) / count * kTwoPi) : (rnd[0] * kTwoPi);
        const float v = lattice ? (FractF(fi * 0.6180339887f) * kTwoPi) : (rnd[1] * kTwoPi);
        const float tube = radius * LerpF(0.28f, 0.44f, ClampF(fill, 0.0f, 1.0f));
        const float shell = lattice ? 1.0f : LerpF(1.0f, std::sqrt(rnd[2]), fill);
        const Vec3f radial = Add3(Mul3(t, std::cos(u)), Mul3(b, std::sin(u)));
        basePos = Add3(Add3(origin, Mul3(radial, radius + std::cos(v) * tube * shell)), Mul3(axis, std::sin(v) * tube * shell));
    }
    else if (shapeCode == 8)
    {
        const Vec3f cell = lattice ? BoxShellLocal(index, count) : BoxShellLocal(static_cast<int32_t>(rnd[0] * count), count);
        basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, cell[0]), Mul3(axis, cell[1])), Mul3(b, cell[2])), radius));
    }
    else if (shapeCode == 10)
    {
        const float y = lattice ? (1.0f - (fi + 0.5f) / count) : rnd[0];
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float a = lattice ? (2.39996323f * fi) : (rnd[1] * kTwoPi);
        const float shell = LerpF(1.0f, std::pow(lattice ? FractF(fi * 0.381966f) : rnd[2], 1.0f / 3.0f), fill);
        basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, std::cos(a) * r), Mul3(axis, y)), Mul3(b, std::sin(a) * r)), radius * shell));
    }
    else if (shapeCode == 11)
    {
        Vec3f normal {};
        const Vec3f local = FetchAnchorPosition(anchorTexelRgba, anchorCount, index, normal);
        const float jitter = fill * Hash31({ rnd[2] + 3.3f, rnd[0] + 3.3f, rnd[1] + 3.3f }) * 0.35f;
        const Vec3f outward = Add3(local, Mul3(normal, jitter));
        basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, outward[0]), Mul3(axis, outward[1])), Mul3(b, outward[2])), radius));
    }
    else if (shapeCode == 12)
    {
        const float detail = (shapeDetail > 0.5f) ? shapeDetail : 8.0f;
        const int32_t spokes = static_cast<int32_t>(ClampF(detail, 4.0f, 24.0f));
        const int32_t rings  = static_cast<int32_t>(ClampF(detail * 1.75f, 3.0f, 34.0f));
        const int32_t spokeCount = spokes * (rings + 1);
        const bool onSpoke = (index % 4) < 2;
        float angle = 0.0f, rVal = 0.0f;
        if (onSpoke)
        {
            angle = static_cast<float>(index % spokes) / static_cast<float>(spokes) * kTwoPi;
            const float stepFrac = lattice ? FractF(static_cast<float>(index / spokes) * 0.6180339887f) : rnd[1];
            rVal = (static_cast<float>(index / std::max(spokeCount, 1)) + stepFrac) / static_cast<float>(rings + 1);
            rVal = ClampF(rVal, 0.06f, 1.0f);
        }
        else
        {
            const float ring = static_cast<float>((index / std::max(spokes, 1)) % std::max(rings, 1));
            angle = static_cast<float>(index % std::max(spokes, 1)) / static_cast<float>(spokes) * kTwoPi + (lattice ? 0.0f : rnd[0] * 0.12f);
            rVal = (ring + 0.5f) / static_cast<float>(rings);
        }
        const float sag = fill * 0.42f * std::sin(kPi * ClampF(rVal, 0.0f, 1.0f));
        basePos = Sub3(Add3(origin, Mul3(Add3(Mul3(t, std::cos(angle) * rVal), Mul3(b, std::sin(angle) * rVal)), radius)), Mul3(axis, sag * radius));
    }
    else if (shapeCode == 13)
    {
        const float detail = (shapeDetail > 0.5f) ? shapeDetail : 6.0f;
        const float folds = std::max(2.0f, detail);
        const float side = std::max(2.0f, std::floor(std::sqrt(count)));
        const float fx = fi - side * std::floor(fi / side);
        const float fy = std::floor(fi / side);
        const float u = (fx + 0.5f) / side * 2.0f - 1.0f;
        const float v = std::min((fy + 0.5f) / side, 1.0f);
        const float sway = std::sin(u * kPi * folds) * (0.10f + v * 0.22f) * (0.4f + fill);
        const float scallop = std::sin(u * kPi * folds * 0.5f) * 0.06f * v;
        basePos = Add3(Add3(Add3(origin, Mul3(t, u * radius)), Mul3(axis, (0.5f - v) * radius * 1.6f)), Mul3(b, (sway + scallop) * radius));
    }
    else if (shapeCode == 14)
    {
        const float blades = ClampF(shapeDetail > 0.5f ? shapeDetail : 26.0f, 3.0f, 512.0f);
        const int32_t perBlade = std::max(1, static_cast<int32_t>(std::ceil(count / blades)));
        const int32_t blade = index / perBlade;
        const float h = static_cast<float>(index - blade * perBlade) / static_cast<float>(perBlade);
        const float rootAngle = Hash31({ static_cast<float>(blade), 1.7f, 9.3f }) * kTwoPi;
        const float rootRadius = std::sqrt(Hash31({ static_cast<float>(blade), 4.1f, 2.7f })) * radius;
        const float height = LerpF(0.55f, 1.65f, Hash31({ static_cast<float>(blade), 7.7f, 5.1f })) * radius;
        const float bend = (0.35f + fill) * std::pow(h, 1.6f) * radius;
        const float flutter = std::sin(h * 3.0f + rootAngle * 3.0f + timeSeconds * 1.4f) * 0.06f * h * radius;
        const Vec3f root = Add3(Mul3(t, std::cos(rootAngle) * rootRadius), Mul3(b, std::sin(rootAngle) * rootRadius));
        basePos = Add3(Add3(Add3(origin, root), Mul3(axis, h * height)), Mul3(b, bend + flutter));
    }
    else if (shapeCode == 15)
    {
        if (anchorCount > 0)
        {
            Vec3f fwTint {};
            const Vec3f target = FetchAnchorPosition(anchorTexelRgba, anchorCount, index, fwTint);
            basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, target[0]), Mul3(axis, target[1])), Mul3(b, target[2])), radius));
        }
        else
        {
            const float detail = (shapeDetail > 0.5f) ? shapeDetail : 6.0f;
            const float lobes = std::max(2.0f, std::floor(detail));
            const float y = lattice ? (1.0f - (fi + 0.5f) / count * 2.0f) : (rnd[0] * 2.0f - 1.0f);
            const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float a = lattice ? (2.39996323f * fi) : (rnd[1] * kTwoPi);
            const float lobe = 0.78f + 0.22f * std::fabs(std::cos(a * lobes * 0.5f));
            const float shell = LerpF(lobe, std::pow(std::max(rnd[2], 1.0e-4f), 1.0f / 3.0f), ClampF(fill, 0.0f, 1.0f));
            basePos = Add3(origin, Mul3(Add3(Add3(Mul3(t, std::cos(a) * r), Mul3(axis, y)), Mul3(b, std::sin(a) * r)), radius * shell));
        }
    }

    const int32_t mocapClip = static_cast<int32_t>(shape.MocapParams[0] + 0.5f);
    const float mocapAmp = shape.MocapParams[1];
    if (mocapClip > 0 && mocapAmp > 0.001f)
    {
        Vec3f limbVel {};
        const Vec3f fig = EvaluateMocapPose(shape, index, rnd, lattice, timeSeconds, limbVel);
        shapeVel = limbVel;
        basePos = Lerp3(basePos, fig, ClampF(mocapAmp, 0.0f, 1.0f));
    }

    if (outShapeVelocity) *outShapeVelocity = shapeVel;
    return basePos;
}

float EvaluateEffectorWeight(
    const ParticleShapeUniformBlock& shape,
    const std::array<float, 3>& worldPosition,
    const float timeSeconds) noexcept
{
    using SimulationSpace::ForceFields::ValueNoise;
    const int32_t effMode = static_cast<int32_t>(shape.MocapKickEffector[1] + 0.5f);
    const float effStrength = shape.MocapKickEffector[2];
    if (effMode <= 0 || effStrength <= 0.0001f) return 0.0f;

    const float freq = std::max(0.15f, shape.MocapKickEffector[3]);
    const float effSpeed = shape.EffectorMotion[0];
    const Vec3f origin { shape.OriginRadius[0], shape.OriginRadius[1], shape.OriginRadius[2] };
    const float radius = shape.OriginRadius[3];
    const Vec3f dVec = Mul3(Sub3(worldPosition, origin), freq);
    float w = 0.0f;

    if (effMode == 1)
    {
        const float dist = Len3(Sub3(worldPosition, origin));
        const float ph = dist * freq * 3.4f - timeSeconds * effSpeed * 3.1f;
        w = std::pow(std::max(0.0f, 0.5f + 0.5f * std::sin(ph)), 1.35f);
    }
    else if (effMode == 2)
    {
        const float ph0 = timeSeconds * effSpeed * 1.35f;
        const float ph1 = timeSeconds * effSpeed * 1.05f + 2.4f;
        const float orbR = std::max(radius, 0.5f) * 0.78f;
        const Vec3f e0 = Add3(origin, { std::cos(ph0) * orbR, std::sin(ph0 * 1.3f) * orbR * 0.55f, std::sin(ph0) * orbR });
        const Vec3f e1 = Add3(origin, { std::cos(ph1) * orbR * 0.85f, std::cos(ph1 * 0.9f) * orbR * 0.5f, std::sin(ph1 * 1.2f) * orbR * 0.85f });
        const float radEff = std::max(0.45f, 1.15f / std::max(0.35f, freq));
        const float g0 = 1.0f - SmoothStepF(radEff * 0.18f, radEff, Len3(Sub3(worldPosition, e0)));
        const float g1 = 1.0f - SmoothStepF(radEff * 0.18f, radEff, Len3(Sub3(worldPosition, e1)));
        w = std::max(g0, g1);
    }
    else if (effMode == 3)
    {
        const float tShift = timeSeconds * effSpeed;
        const float warp = ValueNoise(Add3(Mul3(dVec, 0.78f), { 0.0f, -tShift * 0.35f, tShift * 0.25f })) * 2.4f;
        const float waveA = 0.5f + 0.5f * std::sin(dVec[0] * 1.35f + dVec[1] * 1.55f - tShift * 1.55f + warp);
        const float blot = ValueNoise(Add3(Mul3(dVec, 1.18f), { tShift * 0.42f, -tShift * 0.28f, 2.3f }));
        w = ClampF(waveA * 0.62f + blot * 0.58f - 0.14f, 0.0f, 1.0f);
    }
    else
    {
        const float tS = timeSeconds * effSpeed * 0.45f;
        const float n0 = ValueNoise(Add3(Mul3(dVec, 1.15f), { tS, -tS * 0.7f, tS * 0.5f }));
        const float n1 = ValueNoise(Sub3(Mul3(dVec, 2.25f), { tS * 1.3f, tS * 0.4f, -tS * 0.8f }));
        w = ClampF((n0 * 0.68f + n1 * 0.32f - 0.26f) * 1.65f, 0.0f, 1.0f);
    }
    return ClampF(w * effStrength, 0.0f, 2.0f);
}

DerezEvaluationReading EvaluateDerezFront(
    const ParticleShapeUniformBlock& shape,
    const std::array<float, 3>& restHome,
    const float timeSeconds) noexcept
{
    DerezEvaluationReading res {};
    const int32_t derezMode = static_cast<int32_t>(shape.EffectorAxisDerez[1] + 0.5f);
    const float derezMix = shape.EffectorAxisDerez[2];
    if (derezMode <= 0 || derezMix <= 0.0001f) return res;

    const float span = std::max(shape.OriginRadius[3], 0.15f);
    const Vec3f u {
        ClampF((restHome[0] - shape.OriginRadius[0]) / (2.0f * span) + 0.5f, 0.0f, 1.0f),
        ClampF((restHome[1] - shape.OriginRadius[1]) / (2.0f * span) + 0.5f, 0.0f, 1.0f),
        ClampF((restHome[2] - shape.OriginRadius[2]) / (2.0f * span) + 0.5f, 0.0f, 1.0f)
    };

    if (derezMode == 5)
    {
        const Vec3f cell { std::floor(u[0] * 7.0f), std::floor(u[1] * 5.0f), std::floor(u[2] * 7.0f) };
        const float d = Dot3(cell, { 12.9898f, 78.233f, 37.719f });
        const float h = FractF(static_cast<float>(std::sin(static_cast<double>(d))) * 43758.5453f);
        res.WaveU = ClampF(0.6f * h + 0.4f * u[1], 0.0f, 1.0f);
    }
    else if (derezMode == 3)
    {
        res.WaveU = ClampF(0.5f + 0.3f * std::sin(u[0] * 5.0f + u[1] * 3.0f + 0.7f)
                                + 0.2f * std::sin(u[1] * 7.0f - u[2] * 4.0f + 2.1f), 0.0f, 1.0f);
    }
    else
    {
        res.WaveU = (u[0] + u[1] + u[2]) / 3.0f;
    }

    const float autoSpeed = shape.DerezFrontParams[0];
    const float manualProg = shape.EffectorAxisDerez[3];
    const float rawProg = (autoSpeed > 0.0001f) ? (FractF(timeSeconds * autoSpeed) * 1.32f - 0.12f) : (manualProg * 1.24f - 0.06f);
    res.Progress = rawProg * ClampF(derezMix, 0.0f, 1.0f);
    const float width = std::max(0.02f, shape.DerezFrontParams[1]);
    const float dist = res.Progress - res.WaveU;
    res.EdgeBand = std::exp(-(dist * dist) / std::max(width * width * 0.55f, 1.0e-4f));
    res.Released = (derezMode == 2) ? (res.Progress < res.WaveU) : (res.Progress >= res.WaveU);
    return res;
}

void StepParticleCloudCpu(
    ParticleCloudStateRecord& record,
    const ParticleUpdateUniformBlock& uniforms,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    const GasVelocityQueryFn& gasVelocityQuery)
{
    auto invokeFn = [](const void* ctx, const std::array<float, 3>& p) noexcept -> std::array<float, 3>
    {
        return (*static_cast<const GasVelocityQueryFn*>(ctx))(p);
    };
    GasVelocitySamplerView view {};
    if (gasVelocityQuery)
    {
        view.Callback    = invokeFn;
        view.UserContext = &gasVelocityQuery;
    }
    StepParticleCloudCpu(record, uniforms, forceRecords, view);
}

void StepParticleCloudCpu(
    ParticleCloudStateRecord& record,
    const ParticleUpdateUniformBlock& uniforms,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    GasVelocitySamplerView gasVelocitySampler)
{
    using SimulationSpace::ForceFields::CurlNoise;
    using SimulationSpace::ForceFields::EvaluateTotalForceField;
    using SimulationSpace::ForceFields::ForceTargetChannelBits;
    using SimulationSpace::ForceFields::Hash31;

    const int32_t side = std::max(1, static_cast<int32_t>(uniforms.Shape.AnchorLayout[2] + 0.5f));
    const uint32_t capacity = static_cast<uint32_t>(side * side);
    if (record.ReadStates.size() != capacity) record.ReadStates.resize(capacity);
    if (record.WriteStates.size() != capacity) record.WriteStates.resize(capacity);
    if (record.PreviousPositions.size() != capacity) record.PreviousPositions.resize(capacity);

    const float simTime = uniforms.GridHalfYZTimeDt[2];
    const float dt      = uniforms.GridHalfYZTimeDt[3];
    const int32_t mode  = static_cast<int32_t>(uniforms.Shape.ShapeCounts[3] + 0.5f);
    const int32_t count = static_cast<int32_t>(uniforms.Shape.ShapeCounts[1] + 0.5f);
    const float radius  = uniforms.Shape.OriginRadius[3];
    const Vec3f origin  { uniforms.Shape.OriginRadius[0], uniforms.Shape.OriginRadius[1], uniforms.Shape.OriginRadius[2] };
    const Vec3f axis    { uniforms.Shape.AxisFill[0], uniforms.Shape.AxisFill[1], uniforms.Shape.AxisFill[2] };
    const float lifetime   = uniforms.SpawnKinematics[2];
    const float linger     = uniforms.DissolveSwarm[1];
    const float dissolve   = uniforms.DissolveSwarm[0];
    const float noiseScale = uniforms.CouplingNoise[3];
    const float noiseSpeed = uniforms.SpringBounce[3];
    const int32_t effMode  = static_cast<int32_t>(uniforms.Shape.MocapKickEffector[1] + 0.5f);
    const float effStrength = uniforms.Shape.MocapKickEffector[2];
    const float effPush    = uniforms.Shape.EffectorMotion[1];
    const int32_t effAxis  = static_cast<int32_t>(uniforms.Shape.EffectorAxisDerez[0] + 0.5f);
    const int32_t anchorCount = static_cast<int32_t>(uniforms.Shape.AnchorLayout[1] + 0.5f);
    const int32_t derezMode = static_cast<int32_t>(uniforms.Shape.EffectorAxisDerez[1] + 0.5f);
    const float derezMix    = uniforms.Shape.EffectorAxisDerez[2];
    const float derezShatter = uniforms.Shape.DerezFrontParams[3];
    const bool resetFlag    = (uniforms.SwarmControlFlags[1] > 0.5f);
    const Vec3f gridCentre  { uniforms.GridCentreHalfX[0], uniforms.GridCentreHalfX[1], uniforms.GridCentreHalfX[2] };
    const Vec3f gridHalf    { uniforms.GridCentreHalfX[3], uniforms.GridHalfYZTimeDt[0], uniforms.GridHalfYZTimeDt[1] };
    const float gravity     = uniforms.LaunchForces[2];
    const float drag        = uniforms.LaunchForces[3];
    const float noiseAmt    = uniforms.CouplingNoise[0];
    const float fieldInf    = uniforms.CouplingNoise[1];
    const float gasInf      = uniforms.CouplingNoise[2];
    const float bounce      = uniforms.SpringBounce[2];
    const uint32_t particleChannel = static_cast<uint32_t>(ForceTargetChannelBits::Particle);
    const std::span<const float> anchorData(record.AnchorTexels.Data);

    for (uint32_t idxU = 0u; idxU < capacity; ++idxU)
    {
        const int32_t index = static_cast<int32_t>(idxU);
        const float px = static_cast<float>(index % side);
        const float py = static_cast<float>(index / side);
        const ParticleStateGpuRecord& prevState = record.ReadStates[idxU];
        record.PreviousPositions[idxU] = prevState.PositionAge;

        const float seed = Hash31({ px, py, 4.2f });
        Vec3f limbVel { 0.0f, 0.0f, 0.0f };
        const Vec3f restHome = EvaluateShapePointAtTime(uniforms.Shape, anchorData, index, { 0.0f, 0.0f, 0.0f }, true, simTime, &limbVel);
        Vec3f home = restHome;

        if (dissolve > 0.0f)
        {
            const Vec3f curl = CurlNoise(Add3(Mul3(home, noiseScale), { simTime * noiseSpeed, 2.7f, -simTime * noiseSpeed * 0.6f }));
            home = Add3(home, Mul3(curl, dissolve));
        }

        if (effMode > 0 && effStrength > 0.0001f && std::fabs(effPush) > 0.0001f)
        {
            const float effW = EvaluateEffectorWeight(uniforms.Shape, restHome, simTime);
            Vec3f outDir {};
            if (effAxis == 1)
            {
                outDir = { 0.0f, 1.0f, 0.0f };
            }
            else if (effAxis == 2 && anchorCount > 0)
            {
                Vec3f aNorm {};
                (void)FetchAnchorPosition(anchorData, anchorCount, index, aNorm);
                Vec3f tA {}, bA {};
                ComputeShapeBasis(axis, tA, bA);
                const Vec3f worldNorm = Add3(Add3(Mul3(tA, aNorm[0]), Mul3(axis, aNorm[1])), Mul3(bA, aNorm[2]));
                outDir = (Len3(worldNorm) > 1.0e-4f) ? Normalize3(worldNorm) : axis;
            }
            else
            {
                outDir = Sub3(restHome, origin);
                outDir = (Len3(outDir) > 1.0e-4f) ? Normalize3(outDir) : axis;
            }
            home = Add3(home, Mul3(outDir, effW * effPush));
        }

        const DerezEvaluationReading derez = EvaluateDerezFront(uniforms.Shape, restHome, simTime);

        if (resetFlag)
        {
            ParticleStateGpuRecord outReset {};
            if (mode == 1)
            {
                Vec3f startPos = home;
                if (derezMode == 2 && derezMix > 0.01f && derez.Released)
                {
                    const Vec3f scatter = Mul3(RandomDirection({ px, py, 9.4f }), radius * (1.2f + 0.9f * seed));
                    startPos = Add3(Add3(origin, scatter), { 0.0f, std::fabs(scatter[1]) * 0.5f, 0.0f });
                }
                outReset.PositionAge  = { startPos[0], startPos[1], startPos[2], 0.0f };
                outReset.VelocitySeed = { 0.0f, 0.0f, 0.0f, seed };
            }
            else
            {
                outReset.PositionAge  = { 0.0f, -1.0e4f, 0.0f, lifetime + 1.0f };
                outReset.VelocitySeed = { 0.0f, 0.0f, 0.0f, seed };
            }
            record.WriteStates[idxU] = outReset;
            record.PreviousPositions[idxU] = outReset.PositionAge;
            continue;
        }

        if (index >= count)
        {
            record.WriteStates[idxU] = ParticleStateGpuRecord {
                { 0.0f, -1.0e4f, 0.0f, lifetime + 1.0f },
                { 0.0f, 0.0f, 0.0f, seed }
            };
            continue;
        }

        Vec3f position { prevState.PositionAge[0], prevState.PositionAge[1], prevState.PositionAge[2] };
        Vec3f velocity { prevState.VelocitySeed[0], prevState.VelocitySeed[1], prevState.VelocitySeed[2] };
        float age = prevState.PositionAge[3];
        const float vSeed = prevState.VelocitySeed[3];

        bool alive = (mode == 1) || (age < lifetime);
        const bool lingering = !alive && (age < lifetime + linger);
        if (lingering) age += dt;

        if (!alive && !lingering)
        {
            const float chance = uniforms.SpawnKinematics[1] * uniforms.SpawnKinematics[0] * dt;
            if (Hash31({ px, py, simTime }) < chance)
            {
                const Vec3f rnd {
                    Hash31({ px, py, simTime + 1.3f }),
                    Hash31({ py, px, simTime + 2.9f }),
                    Hash31({ px + 7.0f, py + 7.0f, simTime + 4.1f })
                };
                Vec3f spawnLimbVel {};
                position = EvaluateShapePointAtTime(uniforms.Shape, anchorData, index, rnd, false, simTime, &spawnLimbVel);
                const Vec3f random = RandomDirection({ px, py, simTime + 8.8f });
                Vec3f outward = Sub3(position, origin);
                outward = (Len3(outward) > 1.0e-4f) ? Normalize3(outward) : random;
                const Vec3f launchMix = Lerp3(axis, outward, uniforms.LaunchForces[1]);
                const Vec3f launch = Normalize3({ launchMix[0] + 1.0e-4f, launchMix[1] + 1.0e-4f, launchMix[2] + 1.0e-4f });
                const Vec3f spreadMix = Lerp3(launch, random, uniforms.LaunchForces[0]);
                const Vec3f dir = Normalize3({ spreadMix[0] + 1.0e-4f, spreadMix[1] + 1.0e-4f, spreadMix[2] + 1.0e-4f });
                const float spd = uniforms.SpawnKinematics[3] * (0.75f + 0.5f * Hash31({ px, py, simTime + 6.6f }));
                velocity = Add3(Mul3(dir, spd), spawnLimbVel);
                age = 0.0f;
                alive = true;
            }
        }

        if (alive)
        {
            if (mode == 1 && derezMode > 0 && derezMix > 0.001f)
            {
                if (!derez.Released)
                {
                    position = Lerp3(position, home, ClampF(dt * 18.0f, 0.0f, 1.0f));
                    velocity = Mul3(velocity, std::exp(-8.0f * dt));
                    age = 0.0f;
                }
                else
                {
                    if (age <= 0.0001f && derezMode != 2)
                    {
                        Vec3f outDir = Sub3(restHome, origin);
                        outDir = (Len3(outDir) > 1.0e-4f) ? Normalize3(outDir) : RandomDirection({ px, py, 3.3f });
                        const Vec3f jitter = Mul3(RandomDirection({ px, py, 7.1f }), 0.55f);
                        const float upLift = (derezMode == 4) ? 1.35f : 0.45f;
                        const Vec3f dir = Normalize3(Add3(Add3(outDir, jitter), { 0.0f, upLift, 0.0f }));
                        velocity = Mul3(dir, derezShatter * (0.65f + 0.7f * seed));
                    }
                    age += dt;
                    Vec3f accel { 0.0f, -gravity, 0.0f };
                    if (derezMode == 4)
                    {
                        accel[1] += 2.6f + 1.2f * std::sin(simTime * 3.5f + seed * 6.283f);
                        accel = Add3(accel, Mul3(CurlNoise(Add3(Mul3(position, 1.1f), { simTime * 0.6f, seed * 3.1f, -simTime * 0.4f })), 3.4f));
                    }
                    else if (derezMode == 2)
                    {
                        const Vec3f orbitTarget = Add3(origin, Mul3(CurlNoise(Add3(Mul3(restHome, 0.9f), { simTime * 0.35f, seed * 4.0f, 0.0f })), radius * 1.6f));
                        accel = Add3(accel, Sub3(Mul3(Sub3(orbitTarget, position), 2.2f), Mul3(velocity, 1.8f)));
                    }
                    else
                    {
                        if (noiseAmt != 0.0f)
                        {
                            const Vec3f curl = CurlNoise(Add3(Mul3(position, noiseScale), { simTime * noiseSpeed, vSeed * 0.7f, -simTime * noiseSpeed * 0.4f }));
                            accel = Add3(accel, Mul3(curl, noiseAmt * 3.0f));
                        }
                        if (fieldInf != 0.0f && !forceRecords.empty())
                        {
                            accel = Add3(accel, Mul3(EvaluateTotalForceField(forceRecords, position, velocity, simTime, particleChannel), fieldInf));
                        }
                    }
                    velocity = Add3(velocity, Mul3(accel, dt));
                    velocity = Mul3(velocity, std::exp(-std::max(drag, 0.35f) * dt));
                    position = Add3(position, Mul3(velocity, dt));
                    const float floorY = gridCentre[1] - gridHalf[1] + 0.01f;
                    if (position[1] < floorY && bounce >= 0.0f)
                    {
                        position[1] = floorY;
                        const Vec3f burst = RandomDirection({ px, py, std::floor(simTime * 4.0f) + 1.7f });
                        velocity[1] = std::fabs(velocity[1]) * std::max(bounce, 0.35f) + derezShatter * 0.22f * FractF(seed * 13.7f);
                        velocity[0] = velocity[0] * 0.65f + burst[0] * (derezShatter * 0.28f);
                        velocity[2] = velocity[2] * 0.65f + burst[2] * (derezShatter * 0.28f);
                    }
                }
                record.WriteStates[idxU] = ParticleStateGpuRecord {
                    { position[0], position[1], position[2], age },
                    { velocity[0], velocity[1], velocity[2], vSeed }
                };
                continue;
            }

            age += dt;
            Vec3f acceleration { 0.0f, -gravity, 0.0f };
            if (fieldInf != 0.0f && !forceRecords.empty())
            {
                acceleration = Add3(acceleration, Mul3(EvaluateTotalForceField(forceRecords, position, velocity, simTime, particleChannel), fieldInf));
            }
            if (noiseAmt != 0.0f)
            {
                const Vec3f curl = CurlNoise(Add3(Mul3(position, noiseScale), { simTime * noiseSpeed, vSeed * 0.7f, -simTime * noiseSpeed * 0.4f }));
                acceleration = Add3(acceleration, Mul3(curl, noiseAmt * 3.0f));
            }
            if (gasInf != 0.0f && static_cast<bool>(gasVelocitySampler))
            {
                const Vec3f gas = gasVelocitySampler.Sample(position);
                acceleration = Add3(acceleration, Mul3(Sub3(gas, velocity), gasInf));
            }
            const float swarmStrength = uniforms.DissolveSwarm[2];
            if (swarmStrength > 0.0001f)
            {
                const float flockGroup = (seed > 0.5f) ? 1.0f : -1.0f;
                const float st = simTime * (0.55f + 0.15f * flockGroup);
                const float span = std::max(radius, 0.8f);
                const Vec3f leader = Add3(origin, {
                    std::sin(st * 1.1f + flockGroup) * span * 1.15f,
                    std::sin(st * 1.7f + flockGroup * 0.6f) * span * 0.45f,
                    std::cos(st * 0.9f - flockGroup * 0.4f) * span * 1.15f
                });
                const Vec3f toLeader = Sub3(leader, position);
                const float distLeader = Len3(toLeader);
                const Vec3f cohesionDir = (distLeader > 1.0e-4f) ? Mul3(toLeader, 1.0f / distLeader) : Vec3f { 0.0f, 1.0f, 0.0f };
                const Vec3f relPos = Add3(Sub3(position, origin), { 1.0e-4f, 0.0f, 1.0e-4f });
                const Vec3f tangent = Normalize3(Cross3({ 0.0f, 1.0f, 0.0f }, relPos));
                const Vec3f flutter = CurlNoise(Add3(Mul3(position, 1.25f), { simTime * 0.8f, seed * 5.3f, -simTime * 0.6f }));
                const float wingBob = std::sin(simTime * std::max(uniforms.Shape.DerezEdgeColourWing[3], 1.0f) + seed * kTwoPi) * 0.35f;
                const Vec3f steer = Add3(
                    Add3(Mul3(cohesionDir, uniforms.DissolveSwarm[3] * std::min(distLeader, 3.0f)),
                         Mul3(tangent, uniforms.SwarmControlFlags[0])),
                    Add3(Mul3(flutter, 1.1f), { 0.0f, wingBob, 0.0f }));
                acceleration = Add3(acceleration, Mul3(steer, swarmStrength));
            }
            if (mode == 1)
            {
                const Vec3f springTerm = Sub3(Mul3(Sub3(home, position), uniforms.SpringBounce[0]), Mul3(velocity, uniforms.SpringBounce[1]));
                acceleration = Add3(acceleration, Add3(springTerm, Mul3(limbVel, 0.65f)));
            }

            velocity = Add3(velocity, Mul3(acceleration, dt));
            velocity = Mul3(velocity, std::exp(-drag * dt));
            position = Add3(position, Mul3(velocity, dt));

            const float floorY = gridCentre[1] - gridHalf[1] + 0.01f;
            if (position[1] < floorY && bounce >= 0.0f)
            {
                position[1] = floorY;
                velocity[1] = std::fabs(velocity[1]) * bounce;
                velocity[0] *= 0.7f;
                velocity[2] *= 0.7f;
            }

            const Vec3f local = Sub3(position, gridCentre);
            if (mode == 0 && (std::fabs(local[0]) > gridHalf[0] * 1.05f
                           || std::fabs(local[2]) > gridHalf[2] * 1.05f
                           || local[1] > gridHalf[1] * 1.2f
                           || local[1] < -gridHalf[1] * 1.5f))
            {
                age = std::max(age, lifetime);
            }
        }

        record.WriteStates[idxU] = ParticleStateGpuRecord {
            { position[0], position[1], position[2], age },
            { velocity[0], velocity[1], velocity[2], vSeed }
        };
    }

    std::swap(record.ReadStates, record.WriteStates);
    record.LastUniformBlock = uniforms;
}

void StepTrailHistoryCpu(
    ParticleCloudStateRecord& record,
    const ParticleTrailUpdateUniformBlock& uniforms)
{
    const uint32_t capacity    = std::max(1u, static_cast<uint32_t>(uniforms.LayoutHeadAdvance[0] + 0.5f));
    const uint32_t trailLength = std::max(2u, static_cast<uint32_t>(uniforms.LayoutHeadAdvance[1] + 0.5f));
    const uint32_t head        = static_cast<uint32_t>(std::max(0.0f, uniforms.LayoutHeadAdvance[2] + 0.5f));
    const bool advance         = (uniforms.LayoutHeadAdvance[3] > 0.5f);
    const bool reset           = (uniforms.ResetLifetimePad[0] > 0.5f);
    const float lifetime       = uniforms.ResetLifetimePad[1];

    const size_t totalSlots = static_cast<size_t>(capacity) * static_cast<size_t>(trailLength);
    if (record.TrailSamples.size() != totalSlots)
    {
        record.TrailSamples.assign(totalSlots, std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
    }
    if (record.TrailScratch.size() != totalSlots)
    {
        record.TrailScratch.resize(totalSlots);
    }

    const uint32_t safeCap = std::min<uint32_t>(capacity, static_cast<uint32_t>(record.ReadStates.size()));
    for (uint32_t p = 0u; p < safeCap; ++p)
    {
        const auto& current  = record.ReadStates[p].PositionAge;
        const auto& previous = (p < record.PreviousPositions.size()) ? record.PreviousPositions[p] : current;
        const float dx = current[0] - previous[0];
        const float dy = current[1] - previous[1];
        const float dz = current[2] - previous[2];
        const bool jumped = (dx * dx + dy * dy + dz * dz) > 4.0f;
        const bool baseRespawn = reset || (current[3] < previous[3]) || jumped;
        const bool frozen = (current[3] >= lifetime) && (lifetime > 0.0f);
        const size_t rowBase = static_cast<size_t>(p) * static_cast<size_t>(trailLength);

        for (uint32_t slot = 0u; slot < trailLength; ++slot)
        {
            const auto& old = record.TrailSamples[rowBase + slot];
            if (baseRespawn || old[3] < 0.5f)
            {
                record.TrailScratch[rowBase + slot] = { current[0], current[1], current[2], 1.0f };
            }
            else if (advance && slot == head && !frozen)
            {
                record.TrailScratch[rowBase + slot] = { current[0], current[1], current[2], 1.0f };
            }
            else
            {
                record.TrailScratch[rowBase + slot] = old;
            }
        }
    }

    std::swap(record.TrailSamples, record.TrailScratch);
    record.LastTrailUniforms = uniforms;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                PARTICLEGPUSIMULATION
//------------------------------------------------------------------------------------------------------------------------

void ParticleGpuSimulation::ClearAll() noexcept
{
    Records.clear();
    Statistics = {};
}

void ParticleGpuSimulation::ResetAll() noexcept
{
    for (ParticleCloudStateRecord& rec : Records)
    {
        rec.NeedsReset = true;
        rec.TrailHead = 0u;
        rec.TrailClock = 0.0f;
        rec.TrailNeedsReset = true;
    }
}

void ParticleGpuSimulation::ResetCloud(const std::string_view nodeId) noexcept
{
    for (ParticleCloudStateRecord& rec : Records)
    {
        if (rec.NodeId == nodeId)
        {
            rec.NeedsReset = true;
            rec.TrailHead = 0u;
            rec.TrailClock = 0.0f;
            rec.TrailNeedsReset = true;
            return;
        }
    }
}

void ParticleGpuSimulation::PruneDeadClouds(std::span<const ParticleCloudNode> liveNodes)
{
    Records.erase(
        std::remove_if(Records.begin(), Records.end(), [&](const ParticleCloudStateRecord& rec) {
            for (const ParticleCloudNode& n : liveNodes)
            {
                if (n.NodeId == rec.NodeId) return false;
            }
            return true;
        }),
        Records.end());
}

void ParticleGpuSimulation::RefreshCloudAnchors(
    ParticleCloudStateRecord& record,
    const ParticleCloudNode& node,
    const CarrierSolidRegistry& solidRegistry,
    const float simulationTimeSeconds)
{
    const ParticleCloudParameters& p = node.Evaluated;
    const bool isFireworkPattern = (p.Shape == ParticleShapeOrdinal::FireworkBurst && p.FireworkPattern != FireworkPatternOrdinal::ClassicBouquet);
    const uint32_t shellIndex = (p.FireworkPattern == FireworkPatternOrdinal::GrandFinale)
        ? static_cast<uint32_t>(std::floor(std::max(0.0f, simulationTimeSeconds) / 3.5f))
        : 0u;

    // Zero-allocation 64-bit FNV-1a anchor signature hash
    auto mixBytes = [](uint64_t& h, const void* ptr, size_t len) noexcept
    {
        const auto* bytes = static_cast<const uint8_t*>(ptr);
        for (size_t i = 0u; i < len; ++i)
        {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    };
    uint64_t wantedHash = 0ull;
    const bool needsAnchors = (p.Shape == ParticleShapeOrdinal::SolidSurface) || isFireworkPattern;
    if (needsAnchors)
    {
        wantedHash = 14695981039346656037ull;
        if (p.Shape == ParticleShapeOrdinal::SolidSurface)
        {
            const uint32_t tag = 1u;
            mixBytes(wantedHash, &tag, sizeof(tag));
            mixBytes(wantedHash, p.SpawnSolid.data(), p.SpawnSolid.size());
            mixBytes(wantedHash, p.CustomSolidId.data(), p.CustomSolidId.size());
            const uint32_t sm = static_cast<uint32_t>(p.SpawnSampleMode);
            mixBytes(wantedHash, &sm, sizeof(sm));
            mixBytes(wantedHash, &p.SolidYawDegrees, sizeof(p.SolidYawDegrees));
            mixBytes(wantedHash, &p.Count, sizeof(p.Count));
        }
        else
        {
            const uint32_t tag = 2u;
            mixBytes(wantedHash, &tag, sizeof(tag));
            const uint32_t pat = static_cast<uint32_t>(p.FireworkPattern);
            mixBytes(wantedHash, &pat, sizeof(pat));
            mixBytes(wantedHash, p.FireworkText.data(), p.FireworkText.size());
            mixBytes(wantedHash, &p.Count, sizeof(p.Count));
            mixBytes(wantedHash, &shellIndex, sizeof(shellIndex));
        }
    }

    if (record.AnchorHash == wantedHash) return;
    record.AnchorHash = wantedHash;
    record.AnchorTexels = AnchorTexelTable {};
    if (!needsAnchors) return;

    if (isFireworkPattern)
    {
        const FireworkAnchorSet sampled = SampleFireworkAnchors(p.FireworkPattern, p.FireworkText, p.Count, shellIndex);
        SolidAnchorSet converted {};
        converted.Count     = sampled.Count;
        converted.Positions = sampled.Positions;
        converted.Normals   = sampled.Normals;
        converted.Ridges    = sampled.Ridges;
        record.AnchorTexels = PackAnchorTexels(converted);
    }
    else
    {
        const CarrierSolidRecord& solid = solidRegistry.ResolveSolid(p.SpawnSolid, p.CustomSolidId);
        const SolidAnchorSet sampled = SampleSolidAnchors(solid, p.Count, p.SpawnSampleMode, static_cast<double>(p.SolidYawDegrees));
        record.AnchorTexels = PackAnchorTexels(sampled);
    }
    if (p.Mode == ParticleSpawnModeOrdinal::Cloner) record.NeedsReset = true;
}

ParticleCloudStateRecord& ParticleGpuSimulation::EnsureCloudRecord(
    const ParticleCloudNode& node,
    const CarrierSolidRegistry& solidRegistry,
    const float simulationTimeSeconds)
{
    const uint32_t side = ComputeParticleTextureSide(node.Evaluated.Count);
    const uint32_t cap  = side * side;

    ParticleCloudStateRecord* found = nullptr;
    for (ParticleCloudStateRecord& rec : Records)
    {
        if (rec.NodeId == node.NodeId)
        {
            found = &rec;
            break;
        }
    }

    if (!found)
    {
        ParticleCloudStateRecord rec {};
        rec.NodeId      = node.NodeId;
        rec.TextureSide = side;
        rec.Capacity    = cap;
        rec.ActiveCount = std::min(node.Evaluated.Count, cap);
        rec.NeedsReset  = true;
        rec.ReadStates.resize(cap);
        rec.WriteStates.resize(cap);
        rec.PreviousPositions.resize(cap);
        Records.push_back(std::move(rec));
        found = &Records.back();
    }
    else if (found->TextureSide != side)
    {
        found->TextureSide = side;
        found->Capacity    = cap;
        found->ReadStates.assign(cap, ParticleStateGpuRecord {});
        found->WriteStates.assign(cap, ParticleStateGpuRecord {});
        found->PreviousPositions.assign(cap, std::array<float, 4> { 0.0f, -1.0e4f, 0.0f, 1.0f });
        found->NeedsReset  = true;
    }

    found->ActiveCount = std::min(node.Evaluated.Count, cap);
    // Zero-allocation 64-bit formation hash (replaces per-tick std::string concatenation)
    const uint64_t formationHash =
        (static_cast<uint64_t>(node.Evaluated.Mode) << 48u) ^
        (static_cast<uint64_t>(node.Evaluated.Shape) << 36u) ^
        (static_cast<uint64_t>(node.Evaluated.Count) << 4u) ^
        (node.Evaluated.DerezMode == ParticleDerezOrdinal::ReverseDerez ? 1ull : 0ull) ^
        0x9E3779B97F4A7C15ull;
    if (found->FormationHash != formationHash)
    {
        found->NeedsReset    = true;
        found->FormationHash = formationHash;
    }

    RefreshCloudAnchors(*found, node, solidRegistry, simulationTimeSeconds);

    // On-demand spline ribbon history ring buffer (ParticleSimulation.js L82-L95)
    const bool wantsTrail = (node.Evaluated.Trail != ParticleTrailModeOrdinal::Off);
    const uint32_t wantedTrailLen = wantsTrail
        ? ComputeEffectiveTrailLength(node.Evaluated.Count, node.Evaluated.TrailLength)
        : 0u;
    if (wantsTrail && (found->TrailLength != wantedTrailLen || found->TrailSamples.size() != static_cast<size_t>(cap) * static_cast<size_t>(wantedTrailLen)))
    {
        found->TrailLength = wantedTrailLen;
        found->TrailHead = 0u;
        found->TrailClock = 0.0f;
        found->TrailNeedsReset = true;
        found->TrailSamples.assign(static_cast<size_t>(cap) * static_cast<size_t>(wantedTrailLen), std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
        found->TrailScratch.resize(found->TrailSamples.size());
    }
    else if (!wantsTrail && found->TrailLength != 0u)
    {
        found->TrailLength = 0u;
        found->TrailHead = 0u;
        found->TrailClock = 0.0f;
        found->TrailNeedsReset = true;
        found->TrailSamples.clear();
        found->TrailScratch.clear();
    }

    return *found;
}

const ParticleCloudStateRecord* ParticleGpuSimulation::FindCloudRecord(const std::string_view nodeId) const noexcept
{
    for (const ParticleCloudStateRecord& rec : Records)
    {
        if (rec.NodeId == nodeId) return &rec;
    }
    return nullptr;
}

void ParticleGpuSimulation::StepCloudsCpu(
    std::span<const ParticleCloudNode> clouds,
    const CarrierSolidRegistry& solidRegistry,
    const ParticleSimulationRegion& region,
    const float simulationTimeSeconds,
    const float deltaTimeSeconds,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    const GasVelocityQueryFn& gasVelocityQuery)
{
    auto invokeFn = [](const void* ctx, const std::array<float, 3>& p) noexcept -> std::array<float, 3>
    {
        return (*static_cast<const GasVelocityQueryFn*>(ctx))(p);
    };
    GasVelocitySamplerView view {};
    if (gasVelocityQuery)
    {
        view.Callback    = invokeFn;
        view.UserContext = &gasVelocityQuery;
    }
    StepCloudsCpu(clouds, solidRegistry, region, simulationTimeSeconds, deltaTimeSeconds, forceRecords, view);
}

void ParticleGpuSimulation::StepCloudsCpu(
    std::span<const ParticleCloudNode> clouds,
    const CarrierSolidRegistry& solidRegistry,
    const ParticleSimulationRegion& region,
    const float simulationTimeSeconds,
    const float deltaTimeSeconds,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    GasVelocitySamplerView gasVelocitySampler)
{
    PruneDeadClouds(clouds);
    Statistics = {};

    for (const ParticleCloudNode& node : clouds)
    {
        if (!node.Enabled || node.Halted) continue;
        ParticleCloudStateRecord& rec = EnsureCloudRecord(node, solidRegistry, simulationTimeSeconds);
        if (deltaTimeSeconds <= 0.0f && !rec.NeedsReset) continue;

        const bool resetNow = rec.NeedsReset;
        const ParticleUpdateUniformBlock uniforms = BuildParticleUpdateUniforms(
            node,
            rec.TextureSide,
            rec.AnchorTexels.Width,
            rec.AnchorTexels.Count,
            region,
            simulationTimeSeconds,
            deltaTimeSeconds,
            resetNow,
            static_cast<uint32_t>(forceRecords.size()),
            static_cast<bool>(gasVelocitySampler));

        StepParticleCloudCpu(rec, uniforms, forceRecords, gasVelocitySampler);
        if (resetNow)
        {
            rec.NeedsReset = false;
            rec.TrailNeedsReset = true;
        }

        if (rec.TrailLength >= 2u)
        {
            rec.TrailClock += deltaTimeSeconds;
            const bool advance = (rec.TrailClock >= node.Evaluated.TrailInterval) || rec.TrailNeedsReset;
            if (advance)
            {
                rec.TrailClock = 0.0f;
                rec.TrailHead = (rec.TrailHead + 1u) % rec.TrailLength;
            }
            ParticleTrailUpdateUniformBlock trailU {};
            trailU.LayoutHeadAdvance = {
                static_cast<float>(rec.Capacity),
                static_cast<float>(rec.TrailLength),
                static_cast<float>(rec.TrailHead),
                advance ? 1.0f : 0.0f
            };
            trailU.ResetLifetimePad = {
                rec.TrailNeedsReset ? 1.0f : 0.0f,
                (node.Evaluated.Mode == ParticleSpawnModeOrdinal::Cloner) ? 0.0f : std::max(0.05f, node.Evaluated.Lifetime),
                0.0f,
                0.0f
            };
            StepTrailHistoryCpu(rec, trailU);
            rec.TrailNeedsReset = false;
        }

        Statistics.ActiveCloudCount    += 1u;
        Statistics.ActiveParticleCount += rec.ActiveCount;
        Statistics.DispatchWorkgroups  += (rec.Capacity + 63u) / 64u;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                             REAL ASYNC VULKAN 1.2 PARTICLE GPU EXECUTOR (MERGE-PREP U03)
//------------------------------------------------------------------------------------------------------------------------

ParticleVulkanGpuExecutor::~ParticleVulkanGpuExecutor()
{
    Teardown();
}

#if FRONTIER_PARTICLE_HAS_VULKAN
bool ParticleVulkanGpuExecutor::AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept
{
    Interchange_ = interchange;
    if (Interchange_ == nullptr || !Interchange_->Ready())
    {
        GpuLiveReady_    = false;
        Telemetry_.Route = ParticleExecutionRoute::HeadlessCpuMirror;
        return false;
    }
    return true;
}
#endif

bool ParticleVulkanGpuExecutor::Initialize(
    uint32_t maxParticleCapacity,
    const std::filesystem::path& compiledShaderDirectory)
{
    Teardown();
    MaxParticleCapacity_ = std::max(64u, maxParticleCapacity);
    ActivePong_          = 0u;
    Telemetry_           = {};

#if FRONTIER_PARTICLE_HAS_VULKAN
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        GpuLiveReady_ = InitializeGpuResources(compiledShaderDirectory);
    }
    (void)ImpulseReadbackRing_.Initialize(kMaxImpulseEntries, GpuLiveReady_ ? Interchange_ : nullptr);
    Telemetry_.Route = GpuLiveReady_ ? ParticleExecutionRoute::VulkanComputeGpu : ParticleExecutionRoute::HeadlessCpuMirror;
#else
    (void)compiledShaderDirectory;
    GpuLiveReady_ = false;
    (void)ImpulseReadbackRing_.Initialize(kMaxImpulseEntries);
    Telemetry_.Route = ParticleExecutionRoute::HeadlessCpuMirror;
#endif

    Initialized_ = true;
    return true;
}

void ParticleVulkanGpuExecutor::Teardown() noexcept
{
    ImpulseReadbackRing_.Teardown();
#if FRONTIER_PARTICLE_HAS_VULKAN
    ReleaseGpuResources();
#endif
    Telemetry_    = {};
    Initialized_  = false;
    GpuLiveReady_ = false;
}

void ParticleVulkanGpuExecutor::ExecuteStep(
    ParticleGpuSimulation& headlessFallbackSim,
    std::span<const ParticleCloudNode> clouds,
    const CarrierSolidRegistry& solidRegistry,
    const ParticleSimulationRegion& region,
    float simulationTimeSeconds,
    float deltaTimeSeconds,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    GasVelocitySamplerView headlessGasSampler,
    const void* rigidProxyBytes,
    uint32_t rigidProxyCount,
    uint32_t rigidProxyStrideBytes)
{
    if (!Initialized_ || clouds.empty() || deltaTimeSeconds <= 0.0f) return;

    Telemetry_.ActiveRingSlot           = (Telemetry_.ActiveRingSlot + 1u) % kTripleBufferSlots;
    Telemetry_.ActiveRigidColliderCount = std::min(rigidProxyCount, kMaxRigidProxies);
    uint32_t totalExpected = 0u;
    for (const auto& c : clouds) totalExpected += c.Evaluated.Count;
    Telemetry_.ActiveParticleCount = std::min(totalExpected, MaxParticleCapacity_);

#if FRONTIER_PARTICLE_HAS_VULKAN
    if (GpuLiveReady_ && Interchange_ != nullptr && Interchange_->Ready())
    {
        // Live Hardware GPU path: ZERO StepCloudsCpu mirror, ZERO mid-frame stalls!
        DispatchGpuAsync(
            clouds, region, simulationTimeSeconds, deltaTimeSeconds, forceRecords,
            rigidProxyBytes, rigidProxyCount, rigidProxyStrideBytes);
        (void)ImpulseReadbackRing_.PollNonBlocking();
        Telemetry_.Route             = ParticleExecutionRoute::VulkanComputeGpu;
        Telemetry_.RealGpuDispatched = true;
        Telemetry_.CompletedTicks   += 1ull;
        return;
    }
#endif

    (void)rigidProxyBytes;
    (void)rigidProxyStrideBytes;

    // Strict headless CPU fallback ONLY when VulkanInterchange is unavailable/unready
    headlessFallbackSim.StepCloudsCpu(
        clouds, solidRegistry, region, simulationTimeSeconds, deltaTimeSeconds, forceRecords, headlessGasSampler);
    Telemetry_.Route             = ParticleExecutionRoute::HeadlessCpuMirror;
    Telemetry_.RealGpuDispatched = false;
    Telemetry_.CompletedTicks   += 1ull;

    // 3-slot non-blocking impulse readback parity in headless mode
    std::array<ParticleRigidImpulseEntry, 0> emptyImpulses {};
    ImpulseReadbackRing_.SubmitHeadlessSnapshot(emptyImpulses, Telemetry_.CompletedTicks);
    if (Telemetry_.CompletedTicks > 1ull)
    {
        (void)ImpulseReadbackRing_.PollNonBlocking(Telemetry_.CompletedTicks - 1ull);
    }
}

ParticleGpuRenderFeedHandle ParticleVulkanGpuExecutor::InspectRenderFeedHandle() const noexcept
{
    ParticleGpuRenderFeedHandle feed {};
    feed.Route                 = Telemetry_.Route;
    feed.ActiveParticleCount   = Telemetry_.ActiveParticleCount;
    feed.MaxParticleCapacity   = MaxParticleCapacity_;
    feed.LastSubmittedTimeline = Telemetry_.LastSubmittedTimeline;
    feed.GpuResident           = GpuLiveReady_ && Telemetry_.RealGpuDispatched;
#if FRONTIER_PARTICLE_HAS_VULKAN
    if (GpuLiveReady_)
    {
        feed.StateBufferHandle   = reinterpret_cast<uint64_t>(BufStates_[ActivePong_].BufferHandle);
        feed.PrevPosBufferHandle = reinterpret_cast<uint64_t>(BufPrevPos_.BufferHandle);
        feed.AnchorBufferHandle  = reinterpret_cast<uint64_t>(BufAnchors_.BufferHandle);
        feed.ImpulseBufferHandle = reinterpret_cast<uint64_t>(BufImpulseAtomic_.BufferHandle);
    }
#endif
    return feed;
}

bool ParticleVulkanGpuExecutor::UploadXpbdNetwork(const ParticleXpbdGpuNetworkUpload& networkUpload)
{
    if (networkUpload.PointCount == 0u || networkUpload.PositionsInvMass4.size() < static_cast<size_t>(networkUpload.PointCount) * 4u)
    {
        return false;
    }

    XpbdPointCount_     = networkUpload.PointCount;
    XpbdFaceCount_      = networkUpload.FaceCount;
    XpbdDistanceCount_  = networkUpload.DistanceCount;
    XpbdSeamCount_      = networkUpload.SeamCount;
    XpbdHingeCount_     = networkUpload.HingeCount;
    XpbdDualFloatCount_ = std::max(1u, networkUpload.DualFloatCount);

#if FRONTIER_PARTICLE_HAS_VULKAN
    if (GpuLiveReady_ && Interchange_ != nullptr && Interchange_->Ready())
    {
        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        relBuf(BufXpbdPosInvMass_); relBuf(BufXpbdPrevPos_); relBuf(BufXpbdVelocities_);
        relBuf(BufXpbdTopology_);   relBuf(BufXpbdRestTables_); relBuf(BufXpbdDuals_);

        const uint64_t ptBytes   = static_cast<uint64_t>(XpbdPointCount_) * 16ull;
        const uint64_t topoBytes = std::max<uint64_t>(64ull, static_cast<uint64_t>(networkUpload.TopologyWords.size()) * 4ull);
        const uint64_t restBytes = std::max<uint64_t>(64ull, static_cast<uint64_t>(networkUpload.RestTables.size()) * 4ull);
        const uint64_t dualBytes = static_cast<uint64_t>(XpbdDualFloatCount_) * 4ull;

        BufXpbdPosInvMass_ = Encoder_.AllocateHostWritableBuffer(ptBytes,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufXpbdPrevPos_    = Encoder_.AllocateHostWritableBuffer(ptBytes,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufXpbdVelocities_ = Encoder_.AllocateHostWritableBuffer(ptBytes,   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufXpbdTopology_   = Encoder_.AllocateHostWritableBuffer(topoBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufXpbdRestTables_ = Encoder_.AllocateHostWritableBuffer(restBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufXpbdDuals_      = Encoder_.AllocateDeviceLocalBuffer(dualBytes,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        if (BufXpbdPosInvMass_.HostAddress != nullptr)
        {
            std::memcpy(BufXpbdPosInvMass_.HostAddress, networkUpload.PositionsInvMass4.data(), ptBytes);
        }
        if (BufXpbdPrevPos_.HostAddress != nullptr)
        {
            std::memcpy(BufXpbdPrevPos_.HostAddress, networkUpload.PositionsInvMass4.data(), ptBytes);
        }
        if (BufXpbdVelocities_.HostAddress != nullptr)
        {
            std::memset(BufXpbdVelocities_.HostAddress, 0, ptBytes);
        }
        if (BufXpbdTopology_.HostAddress != nullptr && !networkUpload.TopologyWords.empty())
        {
            std::memcpy(BufXpbdTopology_.HostAddress, networkUpload.TopologyWords.data(),
                        networkUpload.TopologyWords.size() * sizeof(uint32_t));
        }
        if (BufXpbdRestTables_.HostAddress != nullptr && !networkUpload.RestTables.empty())
        {
            std::memcpy(BufXpbdRestTables_.HostAddress, networkUpload.RestTables.data(),
                        networkUpload.RestTables.size() * sizeof(float));
        }
    }
#endif
    return true;
}

bool ParticleVulkanGpuExecutor::PollAsyncImpulseReadback() noexcept
{
#if FRONTIER_PARTICLE_HAS_VULKAN
    if (GpuLiveReady_)
    {
        RetireCompletedCommandBuffersNonBlocking();
        return ImpulseReadbackRing_.PollNonBlocking();
    }
#endif
    return ImpulseReadbackRing_.PollNonBlocking(Telemetry_.CompletedTicks);
}

#if FRONTIER_PARTICLE_HAS_VULKAN
void ParticleVulkanGpuExecutor::RetireCompletedCommandBuffersNonBlocking() noexcept
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

bool ParticleVulkanGpuExecutor::InitializeGpuResources(const std::filesystem::path& shaderDir)
{
    using namespace GeometricRaster;
    ReleaseGpuResources();

    if (Interchange_ == nullptr || !Interchange_->Ready()) return false;
    if (!Encoder_.InitializeEncoder(Interchange_, 256u)) return false;

    const auto spvUpdate = shaderDir / "ParticleUpdateCompute.comp.spv";
    const auto spvTrail  = shaderDir / "ParticleTrailUpdateCompute.comp.spv";
    if (!std::filesystem::exists(spvUpdate))
    {
        Encoder_.TeardownEncoder();
        return false;
    }

    const std::array<VkDescriptorType, 8> updateSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLER
    };
    const std::array<VkDescriptorType, 5> trailSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };

    ProgUpdate_ = Encoder_.RegisterProgram(spvUpdate, updateSlots, "ComputeEntry");
    ProgTrail_  = std::filesystem::exists(spvTrail)
        ? Encoder_.RegisterProgram(spvTrail, trailSlots, "ComputeEntry")
        : UINT32_MAX;
    if (ProgUpdate_ == UINT32_MAX)
    {
        ReleaseGpuResources();
        return false;
    }

    // Stage F Additive GPU Physics programs (Layer 221 Rigid/Spatial-Hash Collisions + Layer 220 XPBD)
    const std::array<VkDescriptorType, 7> collSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const std::array<VkDescriptorType, 9> hashSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const std::array<VkDescriptorType, 8> xpbdSlots {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    auto tryReg = [&](const char* spvName, std::span<const VkDescriptorType> s, const char* entry) -> uint32_t
    {
        const auto p = shaderDir / spvName;
        return std::filesystem::exists(p) ? Encoder_.RegisterProgram(p, s, entry) : UINT32_MAX;
    };
    ProgCollision_      = tryReg("ParticleCollisionCompute.comp.spv",      collSlots, "ComputeEntry");
    ProgHashClear_      = tryReg("ParticleSpatialHashClear.comp.spv",      hashSlots, "ClearBucketsComputeEntry");
    ProgHashCount_      = tryReg("ParticleSpatialHashCount.comp.spv",      hashSlots, "CountCellsComputeEntry");
    ProgHashScanLocal_  = tryReg("ParticleSpatialHashScanLocal.comp.spv",  hashSlots, "PrefixSumLocalComputeEntry");
    ProgHashScanBlocks_ = tryReg("ParticleSpatialHashScanBlocks.comp.spv", hashSlots, "PrefixSumBlocksComputeEntry");
    ProgHashScanAdd_    = tryReg("ParticleSpatialHashScanAdd.comp.spv",    hashSlots, "PrefixSumAddBlocksComputeEntry");
    ProgHashScatter_    = tryReg("ParticleSpatialHashScatter.comp.spv",    hashSlots, "ScatterIndicesComputeEntry");
    ProgHashResolve_    = tryReg("ParticleSpatialHashResolve.comp.spv",    hashSlots, "ResolveNeighborsComputeEntry");
    ProgXpbdPredict_    = tryReg("ParticleXpbdPredictInertia.comp.spv",    xpbdSlots, "PredictInertiaComputeEntry");
    ProgXpbdResetDual_  = tryReg("ParticleXpbdResetDuals.comp.spv",        xpbdSlots, "ResetDualsComputeEntry");
    ProgXpbdDistance_   = tryReg("ParticleXpbdSolveDistance.comp.spv",     xpbdSlots, "SolveDistanceBatchComputeEntry");
    ProgXpbdSeam_       = tryReg("ParticleXpbdSolveSeam.comp.spv",         xpbdSlots, "SolveSeamBatchComputeEntry");
    ProgXpbdFace_       = tryReg("ParticleXpbdSolveFace.comp.spv",         xpbdSlots, "SolveFaceBatchComputeEntry");
    ProgXpbdHinge_      = tryReg("ParticleXpbdSolveHinge.comp.spv",        xpbdSlots, "SolveHingeBatchComputeEntry");
    ProgXpbdFinalize_   = tryReg("ParticleXpbdFinalizeSubstep.comp.spv",   xpbdSlots, "FinalizeSubstepComputeEntry");

    Telemetry_.CollisionPipelineReady   = (ProgCollision_ != UINT32_MAX);
    Telemetry_.SpatialHashPipelineReady = (ProgHashClear_ != UINT32_MAX && ProgHashCount_ != UINT32_MAX && ProgHashScanLocal_ != UINT32_MAX &&
                                           ProgHashScanBlocks_ != UINT32_MAX && ProgHashScanAdd_ != UINT32_MAX &&
                                           ProgHashScatter_ != UINT32_MAX && ProgHashResolve_ != UINT32_MAX);
    Telemetry_.XpbdPipelineReady        = (ProgXpbdPredict_ != UINT32_MAX && ProgXpbdDistance_ != UINT32_MAX && ProgXpbdFinalize_ != UINT32_MAX);

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

    const uint64_t stateBytes  = static_cast<uint64_t>(MaxParticleCapacity_) * sizeof(ParticleStateGpuRecord);
    const uint64_t bucketBytes = static_cast<uint64_t>(kSpatialHashBuckets) * sizeof(uint32_t);
    const uint64_t pIdxBytes   = static_cast<uint64_t>(MaxParticleCapacity_) * sizeof(uint32_t);
    BufStates_[0]    = Encoder_.AllocateDeviceLocalBuffer(
        stateBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    BufStates_[1]    = Encoder_.AllocateDeviceLocalBuffer(
        stateBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    BufPrevPos_      = Encoder_.AllocateDeviceLocalBuffer(
        static_cast<uint64_t>(MaxParticleCapacity_) * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    BufAnchors_      = Encoder_.AllocateHostWritableBuffer(static_cast<uint64_t>(MaxParticleCapacity_) * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufForces_       = Encoder_.AllocateHostWritableBuffer(64ull * sizeof(SimulationSpace::ForceFields::ForceModelGpuRecord), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufRigidProxies_ = Encoder_.AllocateHostWritableBuffer(static_cast<uint64_t>(kMaxRigidProxies) * 80ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufProxyOmega_   = Encoder_.AllocateHostWritableBuffer(static_cast<uint64_t>(kMaxRigidProxies) * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHullHeaders_  = Encoder_.AllocateHostWritableBuffer(16ull * 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHullVoxels_   = Encoder_.AllocateDeviceLocalBuffer(256ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufImpulseAtomic_= Encoder_.AllocateDeviceLocalBuffer(
        static_cast<uint64_t>(kMaxImpulseEntries) * sizeof(ParticleRigidImpulseEntry),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    BufHashCounts_   = Encoder_.AllocateDeviceLocalBuffer(bucketBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHashOffsets_  = Encoder_.AllocateDeviceLocalBuffer(bucketBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHashBlockSums_= Encoder_.AllocateDeviceLocalBuffer(256ull * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHashWriteOffs_= Encoder_.AllocateDeviceLocalBuffer(bucketBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHashBuckets_  = Encoder_.AllocateDeviceLocalBuffer(pIdxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    BufHashSortedIdx_= Encoder_.AllocateDeviceLocalBuffer(pIdxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ImgFallbackGas_  = Interchange_->AllocateImage3D(
        4u, 4u, 4u, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    for (uint32_t s = 0u; s < kTripleBufferSlots; ++s)
    {
        BufUpdateUbo_[s]    = Encoder_.AllocateHostWritableBuffer(sizeof(ParticleUpdateUniformBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufCollisionUbo_[s] = Encoder_.AllocateHostWritableBuffer(64ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufHashUbo_[s]      = Encoder_.AllocateHostWritableBuffer(64ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        BufXpbdUbo_[s]      = Encoder_.AllocateHostWritableBuffer(128ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    if (BufStates_[0].BufferHandle == VK_NULL_HANDLE || BufStates_[1].BufferHandle == VK_NULL_HANDLE ||
        ImgFallbackGas_.ImageHandle == VK_NULL_HANDLE)
    {
        ReleaseGpuResources();
        return false;
    }

    // One-shot Vulkan 1.2 image layout transition (UNDEFINED -> GENERAL) & zero-clear for ImgFallbackGas_ + state SSBOs.
    if (VkCommandBuffer initCmd = Interchange_->BeginSingleTimeCommands(); initCmd != VK_NULL_HANDLE)
    {
        VkImageSubresourceRange subRange {};
        subRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        subRange.baseMipLevel   = 0u;
        subRange.levelCount     = 1u;
        subRange.baseArrayLayer = 0u;
        subRange.layerCount     = 1u;

        VkImageMemoryBarrier toGeneral {};
        toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.srcAccessMask       = 0u;
        toGeneral.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image               = ImgFallbackGas_.ImageHandle;
        toGeneral.subresourceRange    = subRange;
        vkCmdPipelineBarrier(
            initCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &toGeneral);

        VkClearColorValue zeroColor {};
        vkCmdClearColorImage(initCmd, ImgFallbackGas_.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, &zeroColor, 1u, &subRange);
        vkCmdFillBuffer(initCmd, BufStates_[0].BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        vkCmdFillBuffer(initCmd, BufStates_[1].BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        if (BufPrevPos_.BufferHandle != VK_NULL_HANDLE)
        {
            vkCmdFillBuffer(initCmd, BufPrevPos_.BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        }
        if (BufImpulseAtomic_.BufferHandle != VK_NULL_HANDLE)
        {
            vkCmdFillBuffer(initCmd, BufImpulseAtomic_.BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        }

        VkMemoryBarrier postClear {};
        postClear.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        postClear.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postClear.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            initCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u, 1u, &postClear, 0u, nullptr, 0u, nullptr);

        const uint64_t initTick = Interchange_->DispatchCommandBufferAsync(initCmd);
        InitCommand_.CmdBuffer      = initCmd;
        InitCommand_.TimelineSignal = initTick;
    }

    Telemetry_.AllocatedSsboBytes = stateBytes * 2ull + BufPrevPos_.ByteCapacity + BufAnchors_.ByteCapacity +
                                    BufForces_.ByteCapacity + BufRigidProxies_.ByteCapacity + BufImpulseAtomic_.ByteCapacity +
                                    bucketBytes * 3ull + pIdxBytes * 2ull +
                                    (sizeof(ParticleUpdateUniformBlock) + 128ull) * kTripleBufferSlots;
    return true;
}

void ParticleVulkanGpuExecutor::ReleaseGpuResources() noexcept
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
        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        relBuf(BufStates_[0]); relBuf(BufStates_[1]); relBuf(BufPrevPos_);
        relBuf(BufAnchors_); relBuf(BufForces_); relBuf(BufRigidProxies_);
        relBuf(BufProxyOmega_); relBuf(BufHullHeaders_); relBuf(BufHullVoxels_);
        relBuf(BufImpulseAtomic_);
        relBuf(BufHashCounts_); relBuf(BufHashOffsets_); relBuf(BufHashBlockSums_);
        relBuf(BufHashWriteOffs_); relBuf(BufHashBuckets_); relBuf(BufHashSortedIdx_);
        relBuf(BufXpbdPosInvMass_); relBuf(BufXpbdPrevPos_); relBuf(BufXpbdVelocities_);
        relBuf(BufXpbdTopology_); relBuf(BufXpbdRestTables_); relBuf(BufXpbdDuals_);
        for (auto& u : BufUpdateUbo_) relBuf(u);
        for (auto& u : BufCollisionUbo_) relBuf(u);
        for (auto& u : BufHashUbo_) relBuf(u);
        for (auto& u : BufXpbdUbo_) relBuf(u);
        if (ImgFallbackGas_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ImgFallbackGas_);
        if (LinearSampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(Interchange_->AccessDevice(), LinearSampler_, nullptr);
            LinearSampler_ = VK_NULL_HANDLE;
        }
    }
    Encoder_.TeardownEncoder();
    ProgUpdate_ = ProgTrail_ = ProgCollision_ = UINT32_MAX;
    ProgHashClear_ = ProgHashCount_ = ProgHashScanLocal_ = ProgHashScanBlocks_ = ProgHashScanAdd_ = ProgHashScatter_ = ProgHashResolve_ = UINT32_MAX;
    ProgXpbdPredict_ = ProgXpbdResetDual_ = ProgXpbdDistance_ = ProgXpbdSeam_ = ProgXpbdFace_ = ProgXpbdHinge_ = ProgXpbdFinalize_ = UINT32_MAX;
    Telemetry_.CollisionPipelineReady   = false;
    Telemetry_.SpatialHashPipelineReady = false;
    Telemetry_.XpbdPipelineReady        = false;
    GpuLiveReady_ = false;
}

void ParticleVulkanGpuExecutor::DispatchGpuAsync(
    std::span<const ParticleCloudNode> clouds,
    const ParticleSimulationRegion& region,
    float simulationTimeSeconds,
    float deltaTimeSeconds,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    const void* rigidProxyBytes,
    uint32_t rigidProxyCount,
    uint32_t rigidProxyStrideBytes) noexcept
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
    if (!forceRecords.empty() && BufForces_.HostAddress != nullptr)
    {
        const size_t n = std::min<size_t>(forceRecords.size(), 64u);
        std::memcpy(BufForces_.HostAddress, forceRecords.data(), n * sizeof(SimulationSpace::ForceFields::ForceModelGpuRecord));
    }

    const uint32_t uploadedProxies = (rigidProxyBytes != nullptr && rigidProxyStrideBytes == 80u)
        ? std::min(rigidProxyCount, kMaxRigidProxies)
        : 0u;
    if (uploadedProxies > 0u && BufRigidProxies_.HostAddress != nullptr)
    {
        std::memcpy(BufRigidProxies_.HostAddress, rigidProxyBytes, static_cast<size_t>(uploadedProxies) * 80u);
    }

    const auto& primaryCloud = clouds[0];
    const uint32_t side = ComputeParticleTextureSide(primaryCloud.Evaluated.Count);
    const uint32_t cap  = std::min(side * side, MaxParticleCapacity_);
    const ParticleUpdateUniformBlock uniforms = BuildParticleUpdateUniforms(
        primaryCloud, side, 1u, 0u, region, simulationTimeSeconds, deltaTimeSeconds,
        Telemetry_.CompletedTicks == 0ull, static_cast<uint32_t>(std::min<size_t>(forceRecords.size(), 64u)), false);
    if (BufUpdateUbo_[slot].HostAddress != nullptr)
    {
        std::memcpy(BufUpdateUbo_[slot].HostAddress, &uniforms, sizeof(uniforms));
    }

    VkCommandBuffer cmd = Interchange_->BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return;

    auto emitBarrier = [&]()
    {
        VkMemoryBarrier barrier {};
        barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 1u, &barrier, 0u, nullptr, 0u, nullptr);
    };

    const uint32_t readIdx  = ActivePong_;
    const uint32_t writeIdx = 1u - ActivePong_;
    const VkImageView gasVelView = (ExternalFluidVelocityView_ != 0ull)
        ? reinterpret_cast<VkImageView>(ExternalFluidVelocityView_)
        : ImgFallbackGas_.ViewHandle;
    const bool hasLiveGasVol = (ExternalFluidVelocityView_ != 0ull);
    const ParticleUpdateUniformBlock updatedUniforms = BuildParticleUpdateUniforms(
        primaryCloud, side, 1u, 0u, region, simulationTimeSeconds, deltaTimeSeconds,
        Telemetry_.CompletedTicks == 0ull, static_cast<uint32_t>(std::min<size_t>(forceRecords.size(), 64u)), hasLiveGasVol);
    if (BufUpdateUbo_[slot].HostAddress != nullptr)
    {
        std::memcpy(BufUpdateUbo_[slot].HostAddress, &updatedUniforms, sizeof(updatedUniforms));
    }
    const std::array<ComputeSlotDescriptor, 8> slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufUpdateUbo_[slot].BufferHandle, sizeof(updatedUniforms) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufStates_[readIdx].BufferHandle,  VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufStates_[writeIdx].BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufPrevPos_.BufferHandle,          VK_WHOLE_SIZE },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufAnchors_.BufferHandle,          VK_WHOLE_SIZE },
        { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufForces_.BufferHandle,           VK_WHOLE_SIZE },
        { 6u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, gasVelView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        { 7u, VK_DESCRIPTOR_TYPE_SAMPLER,        VK_NULL_HANDLE, 0ull, 0ull, VK_NULL_HANDLE, LinearSampler_, VK_IMAGE_LAYOUT_GENERAL }
    }};

    const uint32_t groupsX = (cap + 63u) / 64u;
    if (Encoder_.EncodeComputeDispatch(cmd, ProgUpdate_, slots, groupsX))
    {
        Telemetry_.TotalDispatches += 1ull;
    }

    uint32_t liveStateIdx = writeIdx;

    // Layer 221A: GPU Particle <-> Jolt RigidColliderProxy two-way collision + atomic impulse accumulation
    if (AdditiveConfig_.EnableRigidCollisions && ProgCollision_ != UINT32_MAX && uploadedProxies > 0u)
    {
        emitBarrier();
        struct CollisionUboPod
        {
            uint32_t ParticleCount;
            uint32_t ProxyCount;
            uint32_t HullCount;
            uint32_t Flags;
            float    DeltaTime;
            float    ParticleRadius;
            float    Restitution;
            float    Friction;
            float    Pad[8];
        } colUbo { cap, uploadedProxies, 0u, 1u, deltaTimeSeconds,
                   AdditiveConfig_.ParticleRadiusMeters, AdditiveConfig_.Restitution, AdditiveConfig_.Friction, {} };
        if (BufCollisionUbo_[slot].HostAddress != nullptr)
        {
            std::memcpy(BufCollisionUbo_[slot].HostAddress, &colUbo, 64u);
        }
        const std::array<ComputeSlotDescriptor, 7> colSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufCollisionUbo_[slot].BufferHandle, 64u },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufStates_[liveStateIdx].BufferHandle, VK_WHOLE_SIZE },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufRigidProxies_.BufferHandle,         VK_WHOLE_SIZE },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufProxyOmega_.BufferHandle,           VK_WHOLE_SIZE },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHullHeaders_.BufferHandle,          VK_WHOLE_SIZE },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHullVoxels_.BufferHandle,           VK_WHOLE_SIZE },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufImpulseAtomic_.BufferHandle,        VK_WHOLE_SIZE }
        }};
        if (Encoder_.EncodeComputeDispatch(cmd, ProgCollision_, colSlots, groupsX))
        {
            Telemetry_.TotalDispatches           += 1ull;
            Telemetry_.AdditivePhysicsDispatches += 1ull;
        }
    }

    // Layer 221B: 7-stage Blelloch Spatial-Hash GPU Self-Collision
    if (AdditiveConfig_.EnableSpatialHashSelfCollision && Telemetry_.SpatialHashPipelineReady)
    {
        emitBarrier();
        const uint32_t nextIdx = 1u - liveStateIdx;
        struct HashUboPod
        {
            uint32_t ParticleCount;
            uint32_t BucketCount;
            float    CellSize;
            float    ParticleRadius;
            float    Restitution;
            float    Friction;
            float    DeltaTime;
            float    Pad[9];
        } hUbo { cap, kSpatialHashBuckets, std::max(0.05f, AdditiveConfig_.ParticleRadiusMeters * 2.5f),
                 AdditiveConfig_.ParticleRadiusMeters, AdditiveConfig_.Restitution, AdditiveConfig_.Friction,
                 deltaTimeSeconds, {} };
        if (BufHashUbo_[slot].HostAddress != nullptr)
        {
            std::memcpy(BufHashUbo_[slot].HostAddress, &hUbo, 64u);
        }
        const std::array<ComputeSlotDescriptor, 9> hSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufHashUbo_[slot].BufferHandle,      64u },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufStates_[liveStateIdx].BufferHandle, VK_WHOLE_SIZE },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufStates_[nextIdx].BufferHandle,      VK_WHOLE_SIZE },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashCounts_.BufferHandle,           VK_WHOLE_SIZE },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashOffsets_.BufferHandle,          VK_WHOLE_SIZE },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashBlockSums_.BufferHandle,        VK_WHOLE_SIZE },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashWriteOffs_.BufferHandle,        VK_WHOLE_SIZE },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashBuckets_.BufferHandle,          VK_WHOLE_SIZE },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufHashSortedIdx_.BufferHandle,        VK_WHOLE_SIZE }
        }};
        const uint32_t bucketGroups = (kSpatialHashBuckets + 63u) / 64u;
        const uint32_t scanGroups   = (kSpatialHashBuckets + 255u) / 256u;
        auto runHash = [&](uint32_t prog, uint32_t gx)
        {
            if (Encoder_.EncodeComputeDispatch(cmd, prog, hSlots, gx))
            {
                Telemetry_.TotalDispatches           += 1ull;
                Telemetry_.AdditivePhysicsDispatches += 1ull;
            }
            emitBarrier();
        };
        runHash(ProgHashClear_,      bucketGroups);
        runHash(ProgHashCount_,      groupsX);
        runHash(ProgHashScanLocal_,  scanGroups);
        runHash(ProgHashScanBlocks_, 1u);
        runHash(ProgHashScanAdd_,    scanGroups);
        runHash(ProgHashScatter_,    groupsX);
        runHash(ProgHashResolve_,    groupsX);
        liveStateIdx = nextIdx;
    }

    // Layer 220: 7-stage Conflict-Colored GPU Orthotropic XPBD Compute (matches ParticleXpbdCompute.slang 128B UBO)
    if (AdditiveConfig_.EnableXpbdConstraints && Telemetry_.XpbdPipelineReady &&
        XpbdPointCount_ > 0u && BufXpbdPosInvMass_.BufferHandle != VK_NULL_HANDLE)
    {
        emitBarrier();
        const uint32_t substeps = std::clamp(AdditiveConfig_.XpbdSubsteps, 1u, 8u);
        const float subDt = deltaTimeSeconds / static_cast<float>(substeps);
        const float invDt = (subDt > 1.0e-6f) ? (1.0f / subDt) : 0.0f;
        struct alignas(16) XpbdUboPod
        {
            float    TimeControl[4];
            float    GravityDampWind[4];
            float    MembraneModuli[4];
            float    CrimpCompShear[4];
            float    BendFriction[4];
            uint32_t CountsBatch[4];
            uint32_t DualOffsets[4];
            float    WindAeroFlags[4];
        } xUbo {
            { subDt, invDt, invDt * invDt, 1.0e-6f },
            { -9.81f, 0.992f, 0.0f, 0.0f },
            { 1800.0f, 1800.0f, 650.0f, 2400.0f },
            { 0.0f, 0.0f, 0.35f, 0.45f },
            { 0.25f, AdditiveConfig_.Friction, 0.0f, 35.0f },
            { XpbdPointCount_, 0u, XpbdDistanceCount_, uploadedProxies },
            { XpbdFaceCount_, 0u, XpbdHingeCount_, XpbdHingeCount_ + XpbdSeamCount_ },
            { 0.0f, 0.4f, AdditiveConfig_.ParticleRadiusMeters, 0.0f }
        };
        if (BufXpbdUbo_[slot].HostAddress != nullptr)
        {
            std::memcpy(BufXpbdUbo_[slot].HostAddress, &xUbo, sizeof(xUbo));
        }
        const std::array<ComputeSlotDescriptor, 8> xSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufXpbdUbo_[slot].BufferHandle,    sizeof(xUbo) },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdPosInvMass_.BufferHandle,   VK_WHOLE_SIZE },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdPrevPos_.BufferHandle,      VK_WHOLE_SIZE },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdVelocities_.BufferHandle,   VK_WHOLE_SIZE },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdTopology_.BufferHandle,     VK_WHOLE_SIZE },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdRestTables_.BufferHandle,   VK_WHOLE_SIZE },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufXpbdDuals_.BufferHandle,        VK_WHOLE_SIZE },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufRigidProxies_.BufferHandle,     VK_WHOLE_SIZE }
        }};
        const uint32_t ptGroups   = (XpbdPointCount_ + 63u) / 64u;
        const uint32_t dualGroups = (XpbdDualFloatCount_ + 63u) / 64u;
        auto runXpbd = [&](uint32_t prog, uint32_t gx)
        {
            if (prog != UINT32_MAX && gx > 0u && Encoder_.EncodeComputeDispatch(cmd, prog, xSlots, gx))
            {
                Telemetry_.TotalDispatches           += 1ull;
                Telemetry_.AdditivePhysicsDispatches += 1ull;
                emitBarrier();
            }
        };
        for (uint32_t s = 0u; s < substeps; ++s)
        {
            runXpbd(ProgXpbdPredict_,   ptGroups);
            runXpbd(ProgXpbdResetDual_, dualGroups);
            if (XpbdDistanceCount_ > 0u) runXpbd(ProgXpbdDistance_, (XpbdDistanceCount_ + 63u) / 64u);
            if (XpbdSeamCount_ > 0u)     runXpbd(ProgXpbdSeam_,     (XpbdSeamCount_ + 63u) / 64u);
            if (XpbdFaceCount_ > 0u)     runXpbd(ProgXpbdFace_,     (XpbdFaceCount_ + 63u) / 64u);
            if (XpbdHingeCount_ > 0u)    runXpbd(ProgXpbdHinge_,    (XpbdHingeCount_ + 63u) / 64u);
            runXpbd(ProgXpbdFinalize_,  ptGroups);
        }
    }

    ActivePong_ = liveStateIdx;
    emitBarrier();
    const bool encodedReadback = ImpulseReadbackRing_.EncodeAsyncCopy(cmd, BufImpulseAtomic_.BufferHandle, kMaxImpulseEntries);
    const uint64_t submittedSignal = Interchange_->DispatchCommandBufferAsync(cmd);
    Telemetry_.LastSubmittedTimeline = submittedSignal;
    if (encodedReadback)
    {
        ImpulseReadbackRing_.BindActiveSlotTimeline(submittedSignal);
    }

    InFlightCommands_[slot].CmdBuffer      = cmd;
    InFlightCommands_[slot].TimelineSignal = submittedSignal;
}
#endif

} // namespace Frontier::ParticleSpace
