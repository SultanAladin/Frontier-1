/*============================================================================================================================================
                                                       FLUIDVOLUMERENDERER.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Volumetric raymarcher uniform packing & CPU reference mirror of FluidVolumeRaymarch.slang
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js SceneFragment (L1129-L1384) & VolumeRenderer.js L260-L310

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidVolumeRenderer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

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

[[nodiscard]] float Dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] std::array<float, 3> Cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

[[nodiscard]] std::array<float, 3> Normalize3(const std::array<float, 3>& v) noexcept
{
    const float len = std::sqrt(Dot3(v, v));
    if (len <= 1.0e-8f) return { 0.0f, 1.0f, 0.0f };
    return { v[0] / len, v[1] / len, v[2] / len };
}

[[nodiscard]] std::array<float, 3> Lerp3(const std::array<float, 3>& a, const std::array<float, 3>& b, const float t) noexcept
{
    return { LerpF(a[0], b[0], t), LerpF(a[1], b[1], t), LerpF(a[2], b[2], t) };
}

[[nodiscard]] std::array<float, 4> MulMat4Vec4ColMajor(const std::array<float, 16>& m, const std::array<float, 4>& v) noexcept
{
    return {
        m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12] * v[3],
        m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13] * v[3],
        m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3],
        m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3]
    };
}

[[nodiscard]] bool InvertMat4ColMajor(const std::array<float, 16>& m, std::array<float, 16>& invOut) noexcept
{
    double inv[16];
    const auto d = [&](const size_t i) -> double { return static_cast<double>(m[i]); };

    inv[0]  =  d(5)*d(10)*d(15) - d(5)*d(11)*d(14) - d(9)*d(6)*d(15) + d(9)*d(7)*d(14) + d(13)*d(6)*d(11) - d(13)*d(7)*d(10);
    inv[4]  = -d(4)*d(10)*d(15) + d(4)*d(11)*d(14) + d(8)*d(6)*d(15) - d(8)*d(7)*d(14) - d(12)*d(6)*d(11) + d(12)*d(7)*d(10);
    inv[8]  =  d(4)*d(9)*d(15)  - d(4)*d(11)*d(13) - d(8)*d(5)*d(15) + d(8)*d(7)*d(13) + d(12)*d(5)*d(11) - d(12)*d(7)*d(9);
    inv[12] = -d(4)*d(9)*d(14)  + d(4)*d(10)*d(13) + d(8)*d(5)*d(14) - d(8)*d(6)*d(13) - d(12)*d(5)*d(10) + d(12)*d(6)*d(9);

    inv[1]  = -d(1)*d(10)*d(15) + d(1)*d(11)*d(14) + d(9)*d(2)*d(15) - d(9)*d(3)*d(14) - d(13)*d(2)*d(11) + d(13)*d(3)*d(10);
    inv[5]  =  d(0)*d(10)*d(15) - d(0)*d(11)*d(14) - d(8)*d(2)*d(15) + d(8)*d(3)*d(14) + d(12)*d(2)*d(11) - d(12)*d(3)*d(10);
    inv[9]  = -d(0)*d(9)*d(15)  + d(0)*d(11)*d(13) + d(8)*d(1)*d(15) - d(8)*d(3)*d(13) - d(12)*d(1)*d(11) + d(12)*d(3)*d(9);
    inv[13] =  d(0)*d(9)*d(14)  - d(0)*d(10)*d(13) - d(8)*d(1)*d(14) + d(8)*d(2)*d(13) + d(12)*d(1)*d(10) - d(12)*d(2)*d(9);

    inv[2]  =  d(1)*d(6)*d(15) - d(1)*d(7)*d(14) - d(5)*d(2)*d(15) + d(5)*d(3)*d(14) + d(13)*d(2)*d(7) - d(13)*d(3)*d(6);
    inv[6]  = -d(0)*d(6)*d(15) + d(0)*d(7)*d(14) + d(4)*d(2)*d(15) - d(4)*d(3)*d(14) - d(12)*d(2)*d(7) + d(12)*d(3)*d(6);
    inv[10] =  d(0)*d(5)*d(15) - d(0)*d(7)*d(13) - d(4)*d(1)*d(15) + d(4)*d(3)*d(13) + d(12)*d(1)*d(7) - d(12)*d(3)*d(5);
    inv[14] = -d(0)*d(5)*d(14) + d(0)*d(6)*d(13) + d(4)*d(1)*d(14) - d(4)*d(2)*d(13) - d(12)*d(1)*d(6) + d(12)*d(2)*d(5);

    inv[3]  = -d(1)*d(6)*d(11) + d(1)*d(7)*d(10) + d(5)*d(2)*d(11) - d(5)*d(3)*d(10) - d(9)*d(2)*d(7) + d(9)*d(3)*d(6);
    inv[7]  =  d(0)*d(6)*d(11) - d(0)*d(7)*d(10) - d(4)*d(2)*d(11) + d(4)*d(3)*d(10) + d(8)*d(2)*d(7) - d(8)*d(3)*d(6);
    inv[11] = -d(0)*d(5)*d(11) + d(0)*d(7)*d(9)  + d(4)*d(1)*d(11) - d(4)*d(3)*d(9)  - d(8)*d(1)*d(7) + d(8)*d(3)*d(5);
    inv[15] =  d(0)*d(5)*d(10) - d(0)*d(6)*d(9)  - d(4)*d(1)*d(10) + d(4)*d(2)*d(9)  + d(8)*d(1)*d(6) - d(8)*d(2)*d(5);

    const double det = d(0)*inv[0] + d(1)*inv[4] + d(2)*inv[8] + d(3)*inv[12];
    if (std::fabs(det) < 1.0e-12) return false;
    const double invDet = 1.0 / det;
    for (size_t i = 0u; i < 16u; ++i) invOut[i] = static_cast<float>(inv[i] * invDet);
    return true;
}

[[nodiscard]] std::array<float, 2> IntersectBoxCpu(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& dir,
    const std::array<float, 3>& boxMin,
    const std::array<float, 3>& boxMax) noexcept
{
    auto safeInv = [](const float v) -> float {
        return (std::fabs(v) > 1.0e-9f) ? (1.0f / v) : ((v >= 0.0f) ? 1.0e9f : -1.0e9f);
    };
    const std::array<float, 3> inv { safeInv(dir[0]), safeInv(dir[1]), safeInv(dir[2]) };
    const float t0x = (boxMin[0] - origin[0]) * inv[0], t1x = (boxMax[0] - origin[0]) * inv[0];
    const float t0y = (boxMin[1] - origin[1]) * inv[1], t1y = (boxMax[1] - origin[1]) * inv[1];
    const float t0z = (boxMin[2] - origin[2]) * inv[2], t1z = (boxMax[2] - origin[2]) * inv[2];

    const float tmin = std::max({ std::min(t0x, t1x), std::min(t0y, t1y), std::min(t0z, t1z) });
    const float tmax = std::min({ std::max(t0x, t1x), std::max(t0y, t1y), std::max(t0z, t1z) });
    return { tmin, tmax };
}

[[nodiscard]] float IntersectSphereCpu(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& dir,
    const std::array<float, 3>& centre,
    const float radius) noexcept
{
    const std::array<float, 3> oc { origin[0] - centre[0], origin[1] - centre[1], origin[2] - centre[2] };
    const float b = Dot3(oc, dir);
    const float c = Dot3(oc, oc) - radius * radius;
    const float h = b * b - c;
    if (h < 0.0f) return -1.0f;
    return -b - std::sqrt(h);
}

[[nodiscard]] float HenyeyGreensteinCpu(const float cosTheta, const float g) noexcept
{
    const float g2 = g * g;
    const float denom = std::max(1.0e-4f, std::pow(std::max(1.0e-5f, 1.0f + g2 - 2.0f * g * cosTheta), 1.5f));
    return (1.0f - g2) / (4.0f * kPi * denom);
}

[[nodiscard]] float StableJitterCpu(const float px, const float py) noexcept
{
    return FractF(52.9829189f * FractF(px * 0.06711056f + py * 0.00583715f));
}

} // namespace

FluidRenderCamera BuildFluidPerspectiveCamera(
    const std::array<float, 3>& eyeMeters,
    const std::array<float, 3>& targetMeters,
    const float fovYRadians,
    const uint32_t width,
    const uint32_t height,
    const float nearPlane,
    const float farPlane) noexcept
{
    FluidRenderCamera cam {};
    cam.PositionMeters = eyeMeters;
    cam.FovYRadians    = fovYRadians;
    cam.ViewportWidth  = std::max(1u, width);
    cam.ViewportHeight = std::max(1u, height);

    const std::array<float, 3> fwd = Normalize3({
        targetMeters[0] - eyeMeters[0],
        targetMeters[1] - eyeMeters[1],
        targetMeters[2] - eyeMeters[2]
    });
    const std::array<float, 3> worldUp { 0.0f, 1.0f, 0.0f };
    const std::array<float, 3> right = Normalize3(Cross3(fwd, worldUp));
    const std::array<float, 3> up    = Cross3(right, fwd);

    // Standard right-handed OpenGL/WebGL look-at (column-major)
    std::array<float, 16> view {};
    view[0]  = right[0]; view[4]  = right[1]; view[8]  = right[2]; view[12] = -Dot3(right, eyeMeters);
    view[1]  = up[0];    view[5]  = up[1];    view[9]  = up[2];    view[13] = -Dot3(up, eyeMeters);
    view[2]  = -fwd[0];  view[6]  = -fwd[1];  view[10] = -fwd[2];  view[14] =  Dot3(fwd, eyeMeters);
    view[3]  = 0.0f;     view[7]  = 0.0f;     view[11] = 0.0f;     view[15] =  1.0f;

    const float aspect = static_cast<float>(cam.ViewportWidth) / static_cast<float>(cam.ViewportHeight);
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    std::array<float, 16> proj {};
    proj[0]  = f / aspect;
    proj[5]  = f;
    proj[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    proj[11] = -1.0f;
    proj[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);

    // ViewProjection = proj * view (column-major)
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += proj[k * 4 + row] * view[col * 4 + k];
            cam.ViewProjection[col * 4 + row] = sum;
        }
    }
    (void)InvertMat4ColMajor(cam.ViewProjection, cam.InverseViewProjection);
    return cam;
}

FluidVolumeRaymarchUniformBlock BuildFluidVolumeRaymarchUniforms(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeRenderSettings& settings,
    const FluidRenderCamera& camera,
    const bool isSparse,
    const uint32_t tickCounter) noexcept
{
    FluidVolumeRaymarchUniformBlock u {};
    u.Volume                = volume;
    u.InverseViewProjection = camera.InverseViewProjection;

    const float halfY = static_cast<float>(volume.GridBoundary[1]) * volume.GridCentreCellSize[3] * 0.5f;
    const float groundHeight = isSparse ? -0.02f : (volume.GridCentreCellSize[1] - halfY - 0.02f);
    const auto lightDir = Normalize3(settings.LightDirection);

    u.CameraPositionFireLight = { camera.PositionMeters[0], camera.PositionMeters[1], camera.PositionMeters[2], settings.FireLight };
    u.ViewportGroundExposure  = { static_cast<float>(camera.ViewportWidth), static_cast<float>(camera.ViewportHeight), groundHeight, settings.Exposure };
    u.LightDirectionIntensity = { lightDir[0], lightDir[1], lightDir[2], settings.LightIntensity };
    u.LightColourAnisotropy   = { settings.LightColour[0], settings.LightColour[1], settings.LightColour[2], std::min(0.95f, settings.Anisotropy) };
    u.AlbedoDensityScale      = { settings.Albedo[0], settings.Albedo[1], settings.Albedo[2], settings.DensityScale };
    u.AmbientColourAbsorption = { settings.AmbientColour[0], settings.AmbientColour[1], settings.AmbientColour[2], settings.Absorption };
    u.FireEmberIntensity      = { settings.FireEmber[0], settings.FireEmber[1], settings.FireEmber[2], settings.FireIntensity };
    u.FireFlameThreshold      = { settings.FireFlame[0], settings.FireFlame[1], settings.FireFlame[2], settings.FireThreshold };
    u.FireCoreSoftness        = { settings.FireCore[0], settings.FireCore[1], settings.FireCore[2], settings.FireSoftness };
    u.FireOpacitySteps        = {
        settings.FireOpacity,
        static_cast<float>(std::max(1, settings.Steps)),
        static_cast<float>(std::clamp(settings.ShadowSteps, 0, 16)),
        settings.Visible ? 1.0f : 0.0f
    };
    u.FlagsSelection = {
        settings.Transparent ? 1 : 0,
        settings.VoxelPreview ? 1 : 0,
        static_cast<int32_t>(tickCounter & 1023u),
        settings.SelectedSphere
    };
    return u;
}

std::array<float, 3> EvaluateFireRadianceCpu(
    const FluidVolumeRaymarchUniformBlock& u,
    const float temperature) noexcept
{
    const float threshold = u.FireFlameThreshold[3];
    const float softness  = std::max(u.FireCoreSoftness[3], 0.05f);
    const float t = ClampF((temperature - threshold) / softness, 0.0f, 1.0f);
    const std::array<float, 3> ember { u.FireEmberIntensity[0], u.FireEmberIntensity[1], u.FireEmberIntensity[2] };
    const std::array<float, 3> flame { u.FireFlameThreshold[0], u.FireFlameThreshold[1], u.FireFlameThreshold[2] };
    const std::array<float, 3> core  { u.FireCoreSoftness[0],   u.FireCoreSoftness[1],   u.FireCoreSoftness[2]   };
    const auto col = (t < 0.5f) ? Lerp3(ember, flame, t * 2.0f) : Lerp3(flame, core, (t - 0.5f) * 2.0f);
    const float gain = 0.2f + 4.0f * t * t;
    return { col[0] * gain, col[1] * gain, col[2] * gain };
}

float EvaluateFirePresenceCpu(
    const FluidVolumeRaymarchUniformBlock& u,
    const float temperature) noexcept
{
    const float threshold = u.FireFlameThreshold[3];
    return SmoothStepF(threshold * 0.7f, threshold * 1.15f, temperature);
}

float EvaluateSmokePhaseCpu(const float cosTheta, const float g) noexcept
{
    return LerpF(HenyeyGreensteinCpu(cosTheta, -0.25f), HenyeyGreensteinCpu(cosTheta, g), 0.72f) * 4.0f * kPi;
}

std::array<float, 3> EvaluateAcesFilmCpu(const std::array<float, 3>& x) noexcept
{
    auto curve = [](const float v) -> float {
        const float num = v * (2.51f * v + 0.03f);
        const float den = v * (2.43f * v + 0.59f) + 0.14f;
        return ClampF(num / std::max(den, 1.0e-6f), 0.0f, 1.0f);
    };
    return { curve(x[0]), curve(x[1]), curve(x[2]) };
}

float EvaluateSunOpticalDepthCpu(
    const FluidVolumeRaymarchUniformBlock& u,
    const FluidVolumeGridCpu& state,
    const std::array<float, 3>& worldPos,
    const std::array<float, 3>& boxMin,
    const std::array<float, 3>& boxMax) noexcept
{
    const int32_t shadowSteps = static_cast<int32_t>(u.FireOpacitySteps[2]);
    if (shadowSteps <= 0) return 0.0f;

    const std::array<float, 3> lightDir { u.LightDirectionIntensity[0], u.LightDirectionIntensity[1], u.LightDirectionIntensity[2] };
    const auto span = IntersectBoxCpu(worldPos, lightDir, boxMin, boxMax);
    const float start  = std::max(span[0], 0.0f);
    const float length = span[1] - start;
    if (length <= 0.0f) return 0.0f;

    constexpr float kGrowth = 1.35f;
    float stepLen = length * (kGrowth - 1.0f) / (std::pow(kGrowth, static_cast<float>(shadowSteps)) - 1.0f);
    float t = start;
    float optical = 0.0f;
    for (int32_t i = 0; i < 16 && i < shadowSteps; ++i)
    {
        const std::array<float, 3> p {
            worldPos[0] + lightDir[0] * (t + 0.5f * stepLen),
            worldPos[1] + lightDir[1] * (t + 0.5f * stepLen),
            worldPos[2] + lightDir[2] * (t + 0.5f * stepLen)
        };
        const auto cell = FluidCellFromWorld(u.Volume, p);
        optical += state.SampleTrilinear(cell[0], cell[1], cell[2])[0] * stepLen;
        t += stepLen;
        stepLen *= kGrowth;
    }
    return optical * u.AlbedoDensityScale[3] * u.AmbientColourAbsorption[3];
}

std::array<float, 4> RenderFluidVolumePixelCpu(
    const FluidVolumeRaymarchUniformBlock& u,
    const FluidVolumeGridCpu& state,
    const FluidVolumeGridCpu& pressure,
    const float pixelX,
    const float pixelY) noexcept
{
    const float vpW = std::max(u.ViewportGroundExposure[0], 1.0f);
    const float vpH = std::max(u.ViewportGroundExposure[1], 1.0f);
    const float ndcX = (pixelX / vpW) * 2.0f - 1.0f;
    const float ndcY = (pixelY / vpH) * 2.0f - 1.0f;

    const std::array<float, 3> origin { u.CameraPositionFireLight[0], u.CameraPositionFireLight[1], u.CameraPositionFireLight[2] };
    const auto farPt = MulMat4Vec4ColMajor(u.InverseViewProjection, { ndcX, ndcY, 1.0f, 1.0f });
    const float invW = (std::fabs(farPt[3]) > 1.0e-8f) ? (1.0f / farPt[3]) : 1.0f;
    const std::array<float, 3> direction = Normalize3({
        farPt[0] * invW - origin[0],
        farPt[1] * invW - origin[1],
        farPt[2] * invW - origin[2]
    });

    const bool transparent   = (u.FlagsSelection[0] == 1);
    const bool voxelPreview  = (u.FlagsSelection[1] == 1);
    const int32_t selSphere  = u.FlagsSelection[3];
    const bool showVolume    = (static_cast<int32_t>(u.FireOpacitySteps[3]) == 1);

    const float horizon = ClampF(direction[1] * 0.5f + 0.5f, 0.0f, 1.0f);
    std::array<float, 3> colour = transparent
        ? std::array<float, 3>{ 0.0f, 0.0f, 0.0f }
        : Lerp3({ 0.055f, 0.058f, 0.068f }, { 0.028f, 0.029f, 0.034f }, std::pow(horizon, 0.6f));

    const std::array<float, 3> gridCentre { u.Volume.GridCentreCellSize[0], u.Volume.GridCentreCellSize[1], u.Volume.GridCentreCellSize[2] };
    const float cellSize = u.Volume.GridCentreCellSize[3];
    const std::array<float, 3> halfExt {
        static_cast<float>(u.Volume.GridBoundary[0]) * cellSize * 0.5f,
        static_cast<float>(u.Volume.GridBoundary[1]) * cellSize * 0.5f,
        static_cast<float>(u.Volume.GridBoundary[2]) * cellSize * 0.5f
    };
    const std::array<float, 3> boxMin { gridCentre[0] - halfExt[0], gridCentre[1] - halfExt[1], gridCentre[2] - halfExt[2] };
    const std::array<float, 3> boxMax { gridCentre[0] + halfExt[0], gridCentre[1] + halfExt[1], gridCentre[2] + halfExt[2] };
    const std::array<float, 3> fireGlow = Lerp3(
        { u.FireEmberIntensity[0], u.FireEmberIntensity[1], u.FireEmberIntensity[2] },
        { u.FireFlameThreshold[0], u.FireFlameThreshold[1], u.FireFlameThreshold[2] },
        0.6f);
    const float fireLight     = u.CameraPositionFireLight[3];
    const float fireIntensity = u.FireEmberIntensity[3];
    const float groundHeight  = u.ViewportGroundExposure[2];

    float sceneDepth = 1.0e9f;
    if (direction[1] < -1.0e-4f && !transparent)
    {
        const float t = (groundHeight - origin[1]) / direction[1];
        const std::array<float, 3> hit { origin[0] + direction[0] * t, origin[1] + direction[1] * t, origin[2] + direction[2] * t };
        const float fade = std::exp(-std::sqrt(hit[0] * hit[0] + hit[2] * hit[2]) * 0.09f);
        const std::array<float, 3> ground { 0.075f, 0.078f, 0.088f };
        colour = {
            ground[0] * fade + colour[0] * (1.0f - fade),
            ground[1] * fade + colour[1] * (1.0f - fade),
            ground[2] * fade + colour[2] * (1.0f - fade)
        };
        if (showVolume)
        {
            const float shade = std::exp(-EvaluateSunOpticalDepthCpu(u, state, hit, boxMin, boxMax));
            const float shadowFactor = LerpF(0.4f, 1.0f, shade);
            colour[0] *= shadowFactor; colour[1] *= shadowFactor; colour[2] *= shadowFactor;

            const auto nearCell = FluidCellFromWorld(u.Volume, { hit[0], hit[1] + cellSize * 1.5f, hit[2] });
            if (nearCell[0] >= 0.0f && nearCell[1] >= 0.0f && nearCell[2] >= 0.0f
                && nearCell[0] <= static_cast<float>(u.Volume.GridBoundary[0])
                && nearCell[1] <= static_cast<float>(u.Volume.GridBoundary[1])
                && nearCell[2] <= static_cast<float>(u.Volume.GridBoundary[2]))
            {
                const float fl = pressure.SampleTrilinear(nearCell[0], nearCell[1], nearCell[2])[1]
                               * fireLight * fireIntensity * 0.08f * fade;
                colour[0] += fireGlow[0] * fl;
                colour[1] += fireGlow[1] * fl;
                colour[2] += fireGlow[2] * fl;
            }
        }
        sceneDepth = t;
    }

    const int32_t colliderCount = std::min(u.Volume.LookupOriginCount[3], static_cast<int32_t>(kMaximumFluidColliders));
    const std::array<float, 3> lightDir { u.LightDirectionIntensity[0], u.LightDirectionIntensity[1], u.LightDirectionIntensity[2] };
    for (int32_t i = 0; i < colliderCount && !transparent; ++i)
    {
        const std::array<float, 3> sc { u.Volume.Colliders[i][0], u.Volume.Colliders[i][1], u.Volume.Colliders[i][2] };
        const float sr = u.Volume.Colliders[i][3];
        const float t = IntersectSphereCpu(origin, direction, sc, sr);
        if (t > 0.0f && t < sceneDepth)
        {
            const std::array<float, 3> hit { origin[0] + direction[0] * t, origin[1] + direction[1] * t, origin[2] + direction[2] * t };
            const auto n = Normalize3({ hit[0] - sc[0], hit[1] - sc[1], hit[2] - sc[2] });
            const float diffuse = std::max(Dot3(n, lightDir), 0.0f);
            const float rim = std::pow(1.0f - std::max(Dot3(n, { -direction[0], -direction[1], -direction[2] }), 0.0f), 3.0f);
            const std::array<float, 3> baseCol = (i == selSphere)
                ? std::array<float, 3>{ 0.86f, 0.72f, 0.36f }
                : std::array<float, 3>{ 0.42f, 0.44f, 0.50f };
            const float lit = 0.18f + 0.82f * diffuse;
            colour = { baseCol[0] * lit + rim * 0.25f, baseCol[1] * lit + rim * 0.25f, baseCol[2] * lit + rim * 0.25f };
            sceneDepth = t;
        }
    }

    float coverage = 0.0f;
    if (showVolume)
    {
        const auto span = IntersectBoxCpu(origin, direction, boxMin, boxMax);
        const float entry = std::max(span[0], 0.0f);
        const float exitDist = std::min(span[1], sceneDepth);
        if (exitDist > entry)
        {
            const int32_t steps = std::max(1, static_cast<int32_t>(u.FireOpacitySteps[1]));
            const float stepLen = (exitDist - entry) / static_cast<float>(steps);
            const float jitter  = StableJitterCpu(pixelX, pixelY);
            float transmittance = 1.0f;
            std::array<float, 3> accumulated { 0.0f, 0.0f, 0.0f };

            const float cosTheta = Dot3(direction, lightDir);
            const float g = u.LightColourAnisotropy[3];
            const float phase0 = EvaluateSmokePhaseCpu(cosTheta, g);
            const float phase1 = EvaluateSmokePhaseCpu(cosTheta, g * 0.5f);
            const float phase2 = EvaluateSmokePhaseCpu(cosTheta, g * 0.25f);
            const std::array<float, 3> sunLight {
                u.LightColourAnisotropy[0] * u.LightDirectionIntensity[3],
                u.LightColourAnisotropy[1] * u.LightDirectionIntensity[3],
                u.LightColourAnisotropy[2] * u.LightDirectionIntensity[3]
            };
            const std::array<float, 3> albedo  { u.AlbedoDensityScale[0], u.AlbedoDensityScale[1], u.AlbedoDensityScale[2] };
            const float densityScale           = u.AlbedoDensityScale[3];
            const std::array<float, 3> ambient { u.AmbientColourAbsorption[0], u.AmbientColourAbsorption[1], u.AmbientColourAbsorption[2] };
            const float absorption             = u.AmbientColourAbsorption[3];
            const float fireOpacity            = u.FireOpacitySteps[0];

            for (int32_t i = 0; i < 256 && i < steps && transmittance >= 0.005f; ++i)
            {
                const float dist = entry + (static_cast<float>(i) + jitter) * stepLen;
                const std::array<float, 3> p {
                    origin[0] + direction[0] * dist,
                    origin[1] + direction[1] * dist,
                    origin[2] + direction[2] * dist
                };
                const auto coord = FluidCellFromWorld(u.Volume, p);
                const auto st = voxelPreview
                    ? state.FetchClamped(
                        static_cast<int32_t>(std::floor(coord[0])),
                        static_cast<int32_t>(std::floor(coord[1])),
                        static_cast<int32_t>(std::floor(coord[2])))
                    : state.SampleTrilinear(coord[0], coord[1], coord[2]);

                const float density = st[0] * densityScale;
                const float temperature = st[1];
                const float fire = EvaluateFirePresenceCpu(u, temperature);
                if (density < 0.002f && fire < 0.002f) continue;

                const auto rad = EvaluateFireRadianceCpu(u, temperature);
                const std::array<float, 3> emission {
                    rad[0] * fireIntensity * fire,
                    rad[1] * fireIntensity * fire,
                    rad[2] * fireIntensity * fire
                };
                const float sigmaSmoke = density * absorption;
                const float sigmaFire  = fire * fireOpacity;
                const float sigma      = sigmaSmoke + sigmaFire;

                std::array<float, 3> scattering { 0.0f, 0.0f, 0.0f };
                if (sigmaSmoke > 0.0f)
                {
                    const float tau = EvaluateSunOpticalDepthCpu(u, state, p, boxMin, boxMax);
                    const float sun = (phase0 * std::exp(-tau)
                                     + 0.5f * phase1 * std::exp(-tau * 0.5f)
                                     + 0.25f * phase2 * std::exp(-tau * 0.25f)) / 1.75f;
                    const float powder = 1.0f - 0.45f * std::exp(-sigmaSmoke * 2.2f);
                    const float above = state.SampleTrilinear(coord[0], coord[1] + 2.0f, coord[2])[0] * 2.0f
                                      + state.SampleTrilinear(coord[0], coord[1] + 6.0f, coord[2])[0] * 4.0f;
                    const float sky = std::exp(-0.6f * above * cellSize * densityScale * absorption);
                    const float fl = pressure.SampleTrilinear(coord[0], coord[1], coord[2])[1]
                                   * fireLight * fireIntensity * 0.35f;
                    for (size_t c = 0u; c < 3u; ++c)
                    {
                        scattering[c] = albedo[c] * (ambient[c] * sky + sunLight[c] * sun * powder + fireGlow[c] * fl);
                    }
                }

                const float stepTrans = std::exp(-sigma * stepLen);
                const float scale = (sigma > 1.0e-4f) ? ((1.0f - stepTrans) / sigma) : stepLen;
                for (size_t c = 0u; c < 3u; ++c)
                {
                    const float src = scattering[c] * sigmaSmoke + emission[c] * (1.0f + sigmaFire);
                    accumulated[c] += transmittance * src * scale;
                }
                transmittance *= stepTrans;
            }
            for (size_t c = 0u; c < 3u; ++c)
            {
                colour[c] = colour[c] * transmittance + accumulated[c];
            }
            coverage = 1.0f - transmittance;
        }
    }

    const float expScale = u.ViewportGroundExposure[3];
    colour = EvaluateAcesFilmCpu({ colour[0] * expScale, colour[1] * expScale, colour[2] * expScale });
    constexpr float kInvGamma = 1.0f / 2.2f;
    colour = {
        std::pow(std::max(0.0f, colour[0]), kInvGamma),
        std::pow(std::max(0.0f, colour[1]), kInvGamma),
        std::pow(std::max(0.0f, colour[2]), kInvGamma)
    };
    const float alpha = transparent ? std::max(coverage, std::max({ colour[0], colour[1], colour[2] })) : 1.0f;
    return { colour[0], colour[1], colour[2], alpha };
}

void RenderFluidVolumeImageCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    const FluidVolumeGridCpu& state,
    const FluidVolumeGridCpu& pressure,
    const uint32_t width,
    const uint32_t height,
    std::vector<float>& outRgba32f,
    std::vector<uint8_t>* outRgba8)
{
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    outRgba32f.resize(pixelCount * 4u);
    if (outRgba8 != nullptr) outRgba8->resize(pixelCount * 4u);

    for (uint32_t y = 0u; y < height; ++y)
    {
        for (uint32_t x = 0u; x < width; ++x)
        {
            const auto rgba = RenderFluidVolumePixelCpu(
                uniforms,
                state,
                pressure,
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 0.5f);
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            for (size_t c = 0u; c < 4u; ++c)
            {
                outRgba32f[idx + c] = rgba[c];
                if (outRgba8 != nullptr)
                {
                    (*outRgba8)[idx + c] = static_cast<uint8_t>(std::lround(ClampF(rgba[c], 0.0f, 1.0f) * 255.0f));
                }
            }
        }
    }
}

} // namespace Frontier::SimulationSpace::Fluid
