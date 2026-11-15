/*============================================================================================================================================
                                                         MATERIALPREVIEWRENDERER.CPP
============================================================================================================================================*/
// 🧩 Implementation of Direct Studio OpenPBR preview renderer (Vulkan .slang GPU path + exact 10-lobe OpenPBR CPU reference, NO GI)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Dynamic/DynamicSurfaceResolver.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

struct alignas(16) GpuPreviewViewportUniform
{
    uint32_t              WidthPx         = 0u;
    uint32_t              HeightPx        = 0u;
    uint32_t              SpecimenMode    = 0u;
    uint32_t              RenderMode      = 0u;
    std::array<float, 4u> CameraOriginFov { 0.0f, 0.25f, 2.65f, 0.42f };
    std::array<float, 4u> CameraTargetPad { 0.0f, -0.04f, 0.0f, 0.0f };
};

[[nodiscard]] Mathematics::Axis3D ComputeCameraOrigin(const PreviewCameraSetup& Cam) noexcept
{
    const float Dist = std::clamp(Cam.OrbitDistanceM, 1.45f, 6.0f);
    const float CosP = std::cos(Cam.OrbitPitchRadians);
    const float SinP = std::sin(Cam.OrbitPitchRadians);
    const float CosY = std::cos(Cam.OrbitYawRadians);
    const float SinY = std::sin(Cam.OrbitYawRadians);
    return { Dist * CosP * SinY, Dist * SinP, Dist * CosP * CosY };
}

[[nodiscard]] float EvaluateSoftboxPanelCpu(
    Mathematics::Axis3D Dir,
    Mathematics::Axis3D CenterDir,
    Mathematics::Axis3D AxisU,
    Mathematics::Axis3D AxisV,
    float               HalfU,
    float               HalfV,
    float               Roughness) noexcept
{
    const float Facing = Mathematics::EvaluateDot(Dir, CenterDir);
    if (Facing <= 0.05f) return 0.0f;
    const Mathematics::Axis3D Proj = Dir * (1.0f / std::max(0.15f, Facing)) - CenterDir;
    const float DU = std::fabs(Mathematics::EvaluateDot(Proj, AxisU));
    const float DV = std::fabs(Mathematics::EvaluateDot(Proj, AxisV));
    const float R2 = std::clamp(Roughness * Roughness, 0.0f, 1.0f);
    const float Blur = 0.035f + (0.58f - 0.035f) * R2;
    const auto Smooth = [](float E0, float E1, float X) noexcept -> float {
        const float T = std::clamp((X - E0) / std::max(1.0e-5f, E1 - E0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    };
    const float MaskU = 1.0f - Smooth(HalfU - Blur * 0.35f, HalfU + Blur, DU);
    const float MaskV = 1.0f - Smooth(HalfV - Blur * 0.35f, HalfV + Blur, DV);
    return (MaskU * MaskV) / (1.0f + 4.5f * R2);
}

[[nodiscard]] Mathematics::LinearRGB SampleCalibratedStudioEnv(Mathematics::Axis3D Dir, float Roughness) noexcept
{
    const Mathematics::Axis3D NDir = Mathematics::NormalizeAxis(Dir, { 0.0f, 1.0f, 0.0f });
    const float UpT = std::clamp(0.5f * (NDir.Y + 1.0f), 0.0f, 1.0f);
    Mathematics::LinearRGB Env {
        0.036f + (0.115f - 0.036f) * UpT,
        0.039f + (0.145f - 0.039f) * UpT,
        0.046f + (0.195f - 0.046f) * UpT
    };

    const Mathematics::Axis3D KeyC = Mathematics::NormalizeAxis({ 0.46f, 0.72f, 0.52f });
    const Mathematics::Axis3D KeyU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 1.0f, 0.0f }, KeyC));
    const Mathematics::Axis3D KeyV = Mathematics::EvaluateCross(KeyC, KeyU);
    const float KeyP = EvaluateSoftboxPanelCpu(NDir, KeyC, KeyU, KeyV, 0.34f, 0.24f, Roughness) * 2.85f;
    Env.R += 1.06f * KeyP; Env.G += 1.00f * KeyP; Env.B += 0.93f * KeyP;

    const Mathematics::Axis3D FillC = Mathematics::NormalizeAxis({ -0.76f, 0.26f, 0.58f });
    const Mathematics::Axis3D FillU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 1.0f, 0.0f }, FillC));
    const Mathematics::Axis3D FillV = Mathematics::EvaluateCross(FillC, FillU);
    const float FillP = EvaluateSoftboxPanelCpu(NDir, FillC, FillU, FillV, 0.18f, 0.42f, Roughness) * 1.65f;
    Env.R += 0.76f * FillP; Env.G += 0.90f * FillP; Env.B += 1.12f * FillP;

    const Mathematics::Axis3D RimC = Mathematics::NormalizeAxis({ -0.18f, 0.70f, -0.69f });
    const Mathematics::Axis3D RimU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 1.0f, 0.0f }, RimC));
    const Mathematics::Axis3D RimV = Mathematics::EvaluateCross(RimC, RimU);
    const float RimP = EvaluateSoftboxPanelCpu(NDir, RimC, RimU, RimV, 0.45f, 0.14f, Roughness) * 1.80f;
    Env.R += 0.84f * RimP; Env.G += 0.93f * RimP; Env.B += 1.08f * RimP;

    return Env;
}

[[nodiscard]] Mathematics::LinearRGB SampleStudioBackdrop(float U, float V) noexcept
{
    const float CX = U * 2.0f - 1.0f;
    const float CY = V * 2.0f - 1.0f;
    const float Radial = CX * CX + CY * CY;
    const float Grad = std::clamp(1.0f - 0.48f * Radial, 0.0f, 1.0f) * (0.036f + 0.032f * std::clamp(1.0f - V, 0.0f, 1.0f));
    return { Grad, Grad * 1.02f, Grad * 1.08f };
}

[[nodiscard]] uint8_t EncodeAcesSrgb8(float LinearVal) noexcept
{
    const float X = std::max(0.0f, LinearVal * 0.88f);
    const float Mapped = std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    const float Srgb   = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::lround(std::clamp(Srgb, 0.0f, 1.0f) * 255.0f));
}

struct SpecimenRayHit
{
    bool                Hit         = false;
    float               DistanceT   = 1.0e9f;
    Mathematics::Axis3D HitPosition {};
    Mathematics::Axis3D Normal      {};
    float               LatheGroove = 0.0f;
};

[[nodiscard]] SpecimenRayHit IntersectSpecimenGeometry(
    PreviewSpecimenGeometry Specimen,
    Mathematics::Axis3D     Origin,
    Mathematics::Axis3D     RayDir) noexcept
{
    SpecimenRayHit Out {};

    if (Specimen == PreviewSpecimenGeometry::TurntablePlane)
    {
        if (std::fabs(RayDir.Y) > 1.0e-4f)
        {
            const float T = (-0.25f - Origin.Y) / RayDir.Y;
            if (T > 0.05f && T < 12.0f)
            {
                const Mathematics::Axis3D P = Origin + RayDir * T;
                if (std::fabs(P.X) <= 1.25f && std::fabs(P.Z) <= 1.25f)
                {
                    Out.Hit         = true;
                    Out.DistanceT   = T;
                    Out.HitPosition = P;
                    Out.Normal      = { 0.0f, 1.0f, 0.0f };
                }
            }
        }
        return Out;
    }

    if (Specimen == PreviewSpecimenGeometry::RoundedStudioBox)
    {
        // Fast signed-distance sphere-march for beveled calibration cube (half-extent 0.68 m, bevel 0.14 m)
        float T = 0.25f;
        for (int Step = 0; Step < 48; ++Step)
        {
            const Mathematics::Axis3D P = Origin + RayDir * T;
            const float QX = std::fabs(P.X) - 0.56f;
            const float QY = std::fabs(P.Y) - 0.56f;
            const float QZ = std::fabs(P.Z) - 0.56f;
            const float OutX = std::max(QX, 0.0f);
            const float OutY = std::max(QY, 0.0f);
            const float OutZ = std::max(QZ, 0.0f);
            const float Dist = std::sqrt(OutX * OutX + OutY * OutY + OutZ * OutZ) + std::min(std::max({ QX, QY, QZ }), 0.0f) - 0.14f;
            if (Dist < 0.002f)
            {
                Out.Hit = true;
                Out.DistanceT = T;
                Out.HitPosition = P;
                const auto BoxSdf = [](Mathematics::Axis3D V) noexcept -> float {
                    const float BX = std::fabs(V.X) - 0.56f;
                    const float BitangentY = std::fabs(V.Y) - 0.56f;
                    const float BZ = std::fabs(V.Z) - 0.56f;
                    return std::sqrt(std::max(BX, 0.0f) * std::max(BX, 0.0f) +
                                     std::max(BitangentY, 0.0f) * std::max(BitangentY, 0.0f) +
                                     std::max(BZ, 0.0f) * std::max(BZ, 0.0f)) + std::min(std::max({ BX, BitangentY, BZ }), 0.0f) - 0.14f;
                };
                constexpr float E = 0.003f;
                Out.Normal = Mathematics::NormalizeAxis({
                    BoxSdf({ P.X + E, P.Y, P.Z }) - BoxSdf({ P.X - E, P.Y, P.Z }),
                    BoxSdf({ P.X, P.Y + E, P.Z }) - BoxSdf({ P.X, P.Y - E, P.Z }),
                    BoxSdf({ P.X, P.Y, P.Z + E }) - BoxSdf({ P.X, P.Y, P.Z - E })
                }, { 0.0f, 0.0f, 1.0f });
                return Out;
            }
            T += Dist;
            if (T > 6.5f) break;
        }
        return Out;
    }

    if (Specimen == PreviewSpecimenGeometry::DrapedClothSheet)
    {
        // Sculpted draped sheet wave intersection via sphere-march
        float T = 0.25f;
        const auto ClothSdf = [](Mathematics::Axis3D P) noexcept -> float {
            const float FoldZ = 0.16f * std::sin(P.X * 5.2f) * std::cos(P.Y * 2.2f) + 0.08f * std::cos(P.X * 9.5f - P.Y * 3.1f);
            const float Thick = std::fabs(P.Z - FoldZ) - 0.045f;
            const float BoundX = std::fabs(P.X) - 0.78f;
            const float BoundY = std::fabs(P.Y) - 0.82f;
            return std::max({ Thick, BoundX, BoundY });
        };
        for (int Step = 0; Step < 54; ++Step)
        {
            const Mathematics::Axis3D P = Origin + RayDir * T;
            const float Dist = ClothSdf(P);
            if (Dist < 0.0025f)
            {
                Out.Hit = true;
                Out.DistanceT = T;
                Out.HitPosition = P;
                constexpr float E = 0.0035f;
                Out.Normal = Mathematics::NormalizeAxis({
                    ClothSdf({ P.X + E, P.Y, P.Z }) - ClothSdf({ P.X - E, P.Y, P.Z }),
                    ClothSdf({ P.X, P.Y + E, P.Z }) - ClothSdf({ P.X, P.Y - E, P.Z }),
                    ClothSdf({ P.X, P.Y, P.Z + E }) - ClothSdf({ P.X, P.Y, P.Z - E })
                }, { 0.0f, 0.0f, 1.0f });
                return Out;
            }
            T += std::max(0.01f, Dist * 0.65f);
            if (T > 6.5f) break;
        }
        return Out;
    }

    // Default: Calibrated studio shaderball (radius 0.86 m) with dual equatorial lathe grooves
    constexpr float SphereR = 0.86f;
    const float B = Mathematics::EvaluateDot(Origin, RayDir);
    const float C = Mathematics::EvaluateDot(Origin, Origin) - SphereR * SphereR;
    const float Disc = B * B - C;
    if (Disc > 0.0f)
    {
        const float T = -B - std::sqrt(Disc);
        if (T > 0.01f)
        {
            Out.Hit         = true;
            Out.DistanceT   = T;
            Out.HitPosition = Origin + RayDir * T;
            Out.Normal      = Out.HitPosition * (1.0f / SphereR);
            Out.LatheGroove = std::exp(-std::pow((Out.Normal.Y - 0.040f) * 58.0f, 2.0f)) +
                              std::exp(-std::pow((Out.Normal.Y + 0.035f) * 58.0f, 2.0f));
        }
    }
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           LIFECYCLE & VULKAN SETUP
//------------------------------------------------------------------------------------------------------------------------

MaterialPreviewRenderer::~MaterialPreviewRenderer()
{
    TeardownVulkanPreview();
}

bool MaterialPreviewRenderer::InitializeVulkanPreview(
    GeometricRaster::VulkanInterchange& TargetDevice,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    TeardownVulkanPreview();
    if (!TargetDevice.Ready())
    {
        return false;
    }

    const std::filesystem::path SpvPath = CompiledShaderDirectory / "DynamicMaterialResolve.comp.spv";
    if (!std::filesystem::exists(SpvPath))
    {
        return false;
    }

    BoundDevice = &TargetDevice;
    if (!DispatchEncoder.InitializeEncoder(BoundDevice, 32u))
    {
        BoundDevice = nullptr;
        return false;
    }

    const std::array<VkDescriptorType, 3u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    ComputeProgramSlot = DispatchEncoder.RegisterProgram(SpvPath, SlotTypes, "DynamicStudioPreviewCompute");
    if (ComputeProgramSlot == UINT32_MAX)
    {
        TeardownVulkanPreview();
        return false;
    }

    const VkMemoryPropertyFlags HostCoherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    ViewportUniformBuf = BoundDevice->AllocateBuffer(sizeof(GpuPreviewViewportUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostCoherent);
    ParamUniformBuf    = BoundDevice->AllocateBuffer(sizeof(MaterialSpace::Dynamic::DynamicParameterBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostCoherent);
    if (ViewportUniformBuf.BufferHandle == VK_NULL_HANDLE || ParamUniformBuf.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownVulkanPreview();
        return false;
    }

    GpuReady = true;
    return true;
}

void MaterialPreviewRenderer::TeardownVulkanPreview() noexcept
{
    if (BoundDevice != nullptr)
    {
        BoundDevice->ReleaseBuffer(PixelOutputBuf);
        BoundDevice->ReleaseBuffer(ParamUniformBuf);
        BoundDevice->ReleaseBuffer(ViewportUniformBuf);
    }
    DispatchEncoder.TeardownEncoder();
    ComputeProgramSlot = UINT32_MAX;
    BoundDevice        = nullptr;
    GpuReady           = false;
}

void MaterialPreviewRenderer::EnsureOutputCapacity(uint64_t RequiredBytes)
{
    if (BoundDevice == nullptr || (PixelOutputBuf.BufferHandle != VK_NULL_HANDLE && PixelOutputBuf.ByteCapacity >= RequiredBytes))
    {
        return;
    }
    BoundDevice->ReleaseBuffer(PixelOutputBuf);
    const VkMemoryPropertyFlags HostCoherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    PixelOutputBuf = BoundDevice->AllocateBuffer(RequiredBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostCoherent);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           VULKAN GPU DIRECT STUDIO OPENPBR
//------------------------------------------------------------------------------------------------------------------------

bool MaterialPreviewRenderer::RenderPreviewVulkanCompute(
    const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
    PreviewSpecimenGeometry                              Specimen,
    PreviewRenderMode                                    RenderMode,
    const PreviewCameraSetup&                            Camera,
    uint32_t                                             WidthPx,
    uint32_t                                             HeightPx,
    PreviewRenderResult&                                 OutResult)
{
    if (!GpuReady || BoundDevice == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }

    const auto StartTime = std::chrono::steady_clock::now();

    const uint64_t OutputBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    EnsureOutputCapacity(OutputBytes);
    if (PixelOutputBuf.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    const Mathematics::Axis3D CamOrigin = ComputeCameraOrigin(Camera);
    GpuPreviewViewportUniform VpUniform {};
    VpUniform.WidthPx         = WidthPx;
    VpUniform.HeightPx        = HeightPx;
    VpUniform.SpecimenMode    = static_cast<uint32_t>(Specimen);
    VpUniform.RenderMode      = static_cast<uint32_t>(RenderMode);
    VpUniform.CameraOriginFov = { CamOrigin.X, CamOrigin.Y, CamOrigin.Z, Camera.FocalScale };
    VpUniform.CameraTargetPad = { 0.0f, -0.04f, 0.0f, 0.0f };

    std::memcpy(ViewportUniformBuf.HostAddress, &VpUniform, sizeof(VpUniform));
    std::memcpy(ParamUniformBuf.HostAddress, &Parameters, sizeof(Parameters));

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<GeometricRaster::ComputeSlotDescriptor, 3u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ViewportUniformBuf.BufferHandle, sizeof(GpuPreviewViewportUniform), 0ull },
        { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ParamUniformBuf.BufferHandle, sizeof(MaterialSpace::Dynamic::DynamicParameterBlock), 0ull },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PixelOutputBuf.BufferHandle, OutputBytes, 0ull },
    }};

    const uint32_t GroupsX = (WidthPx + 7u) / 8u;
    const uint32_t GroupsY = (HeightPx + 7u) / 8u;
    const bool Dispatched  = DispatchEncoder.EncodeComputeDispatch(Cmd, ComputeProgramSlot, Slots, GroupsX, GroupsY, 1u);
    const uint64_t Tick    = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const bool Completed   = Dispatched && BoundDevice->AwaitTimelineTick(Tick);
    BoundDevice->FreeCommandBuffer(Cmd);

    if (!Completed)
    {
        return false;
    }

    OutResult.WidthPx       = WidthPx;
    OutResult.HeightPx      = HeightPx;
    OutResult.UsedVulkanCompute = true;
    OutResult.DisplayRgba8.resize(static_cast<std::size_t>(OutputBytes));
    std::memcpy(OutResult.DisplayRgba8.data(), PixelOutputBuf.HostAddress, static_cast<std::size_t>(OutputBytes));

    const auto EndTime = std::chrono::steady_clock::now();
    OutResult.RenderMillisec = std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     CPU DIRECT STUDIO OPENPBR REFERENCE (NO GI)
//------------------------------------------------------------------------------------------------------------------------

PreviewRenderResult MaterialPreviewRenderer::RenderPreviewCpuReference(
    const MaterialSpace::Dynamic::DynamicMaterialRecipe& Recipe,
    const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
    PreviewSpecimenGeometry                              Specimen,
    const PreviewCameraSetup&                            Camera,
    uint32_t                                             WidthPx,
    uint32_t                                             HeightPx)
{
    const auto StartTime = std::chrono::steady_clock::now();

    PreviewRenderResult Result {};
    Result.WidthPx       = std::clamp(WidthPx, 32u, 1024u);
    Result.HeightPx      = std::clamp(HeightPx, 32u, 1024u);
    Result.UsedVulkanCompute = false;
    Result.DisplayRgba8.resize(static_cast<std::size_t>(Result.WidthPx) * Result.HeightPx * 4u, 0u);

    const auto& AlbedoSheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto& SheenSheet  = MaterialSpace::InspectGlobalSheenLinearCosineSheet();

    const Mathematics::Axis3D CamOrigin = ComputeCameraOrigin(Camera);
    const Mathematics::Axis3D Target    { 0.0f, -0.04f, 0.0f };
    const Mathematics::Axis3D CamFwd    = Mathematics::NormalizeAxis(Target - CamOrigin);
    const Mathematics::Axis3D CamRight  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(CamFwd, { 0.0f, 1.0f, 0.0f }));
    const Mathematics::Axis3D CamUp     = Mathematics::EvaluateCross(CamRight, CamFwd);

    const float Aspect = static_cast<float>(Result.WidthPx) / static_cast<float>(Result.HeightPx);
    const float InvW   = 1.0f / static_cast<float>(Result.WidthPx);
    const float InvH   = 1.0f / static_cast<float>(Result.HeightPx);

    struct StudioLightPanel
    {
        Mathematics::Axis3D    Direction;
        Mathematics::LinearRGB Radiance;
    };
    const std::array<StudioLightPanel, 3u> Panels {{
        { Mathematics::NormalizeAxis({  0.52f,  0.72f,  0.46f }), { 3.40f, 3.25f, 3.05f } },
        { Mathematics::NormalizeAxis({ -0.78f,  0.34f,  0.52f }), { 0.95f, 1.18f, 1.48f } },
        { Mathematics::NormalizeAxis({ -0.25f,  0.62f, -0.74f }), { 1.85f, 2.02f, 2.30f } },
    }};

    uint32_t CombinedLobeMask = 0u;

    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        const float ScreenV = (static_cast<float>(PY) + 0.5f) * InvH;
        const float SY      = (1.0f - 2.0f * ScreenV) * Camera.FocalScale;

        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const float ScreenU = (static_cast<float>(PX) + 0.5f) * InvW;
            const float SX      = (2.0f * ScreenU - 1.0f) * Camera.FocalScale * Aspect;
            const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(CamFwd + CamRight * SX + CamUp * SY);

            const auto Backdrop = SampleStudioBackdrop(ScreenU, ScreenV);

            // Dark matte pedestal beneath the shaderball
            bool HitPedestal = false;
            Mathematics::LinearRGB PedestalRgb = Backdrop;
            if (Specimen == PreviewSpecimenGeometry::ShaderballPedestal && std::fabs(RayDir.Y) > 1.0e-4f)
            {
                const float TopT = (-0.88f - CamOrigin.Y) / RayDir.Y;
                if (TopT > 0.0f)
                {
                    const Mathematics::Axis3D HP = CamOrigin + RayDir * TopT;
                    const float Rad = std::sqrt(HP.X * HP.X + HP.Z * HP.Z);
                    if (Rad < 0.72f)
                    {
                        HitPedestal = true;
                        const float RimLine = std::clamp((Rad - 0.66f) / 0.05f, 0.0f, 1.0f);
                        const float Shadow  = std::clamp((Rad - 0.32f) / 0.36f, 0.0f, 1.0f);
                        const float FloorLuminance   = 0.034f * (0.45f + 0.55f * Shadow) + 0.05f * RimLine;
                        PedestalRgb = { FloorLuminance, FloorLuminance * 1.04f, FloorLuminance * 1.12f };
                    }
                }
            }

            const std::size_t PixelOffset = (static_cast<std::size_t>(PY) * Result.WidthPx + PX) * 4u;
            const auto Hit = IntersectSpecimenGeometry(Specimen, CamOrigin, RayDir);

            if (!Hit.Hit)
            {
                const auto Bg = HitPedestal ? PedestalRgb : Backdrop;
                Result.DisplayRgba8[PixelOffset + 0u] = EncodeAcesSrgb8(Bg.R);
                Result.DisplayRgba8[PixelOffset + 1u] = EncodeAcesSrgb8(Bg.G);
                Result.DisplayRgba8[PixelOffset + 2u] = EncodeAcesSrgb8(Bg.B);
                Result.DisplayRgba8[PixelOffset + 3u] = 255u;
                continue;
            }

            const Mathematics::Axis3D ViewV = RayDir * -1.0f;
            const Mathematics::Axis3D TangentX = Mathematics::NormalizeAxis({ -Hit.Normal.Z, 0.0f, Hit.Normal.X }, { 1.0f, 0.0f, 0.0f });
            const Mathematics::Axis3D BitangentY = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Hit.Normal, TangentX));

            MaterialSpace::Dynamic::DynamicSurfaceQuery Query {};
            Query.LocalPosition  = Hit.HitPosition;
            Query.WorldNormal    = Hit.Normal;
            Query.WorldTangent   = TangentX;
            Query.WorldBitangent = BitangentY;
            Query.WorldViewDir   = ViewV;

            const auto Sample = MaterialSpace::Dynamic::ResolveDynamicSurface(Recipe, Parameters, Query);
            if (Sample.CutoutDiscarded)
            {
                const auto Bg = HitPedestal ? PedestalRgb : Backdrop;
                Result.DisplayRgba8[PixelOffset + 0u] = EncodeAcesSrgb8(Bg.R);
                Result.DisplayRgba8[PixelOffset + 1u] = EncodeAcesSrgb8(Bg.G);
                Result.DisplayRgba8[PixelOffset + 2u] = EncodeAcesSrgb8(Bg.B);
                Result.DisplayRgba8[PixelOffset + 3u] = 255u;
                continue;
            }

            const float GrooveSign = (Hit.Normal.Y >= 0.0f) ? 1.0f : -1.0f;
            const Mathematics::Axis3D ShadeN = Mathematics::NormalizeAxis(
                Sample.ShadingNormal + BitangentY * (Hit.LatheGroove * 0.18f * GrooveSign),
                Sample.ShadingNormal);
            const Mathematics::Axis3D LocalTx = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(BitangentY, ShadeN), TangentX);
            const Mathematics::Axis3D LocalBitangentY = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(ShadeN, LocalTx), BitangentY);
            const MaterialSpace::Scattering::SurfaceShadingFrame ShadingBasis { LocalTx, LocalBitangentY, ShadeN };
            const Mathematics::Axis3D LocalWo = ShadingBasis.WorldToLocal(ViewV);

            const uint32_t ActiveMask = MaterialSpace::DetermineActiveLobeMask(Sample.Material);
            CombinedLobeMask |= ActiveMask;

            const float SphU = 0.5f + std::atan2(Hit.Normal.X, Hit.Normal.Z) / Constants::TauFloat;
            const float SphV = 0.5f - std::asin(std::clamp(Hit.Normal.Y, -1.0f, 1.0f)) / Constants::PiFloat;
            MaterialSpace::Scattering::TextureFootprintJacobian Footprint {};
            Footprint.SurfaceUV      = { SphU * 2.0f, SphV * 2.0f };
            Footprint.DerivativeDuDx = { InvW * 0.7f, 0.0f };
            Footprint.DerivativeDuDy = { 0.0f, InvH * 0.7f };

            // 1. Direct 3-panel studio lighting through Frontier's FULL 10-lobe OpenPBR 1.1 BSDF!
            Mathematics::LinearRGB DirectSum { 0.0f, 0.0f, 0.0f };
            for (const auto& Light : Panels)
            {
                const Mathematics::Axis3D LocalWi = ShadingBasis.WorldToLocal(Light.Direction);
                const float CosI = std::max(0.0f, LocalWi.Z);
                if (CosI <= 1.0e-5f) continue;
                const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                    Sample.Material, ActiveMask, LocalWo, LocalWi, Footprint, AlbedoSheet, SheenSheet);
                DirectSum.R += Bsdf.R * Light.Radiance.R * CosI;
                DirectSum.G += Bsdf.G * Light.Radiance.G * CosI;
                DirectSum.B += Bsdf.B * Light.Radiance.B * CosI;
            }

            // 2. Calibrated GI-free Studio IBL reflection + soft sky fill
            const float NdotV = std::clamp(Mathematics::EvaluateDot(ShadeN, ViewV), 0.0f, 1.0f);
            const Mathematics::Axis3D ReflDir = Mathematics::NormalizeAxis(ShadeN * (2.0f * NdotV) - ViewV);
            const auto EnvRefl = SampleCalibratedStudioEnv(ReflDir, Sample.Material.Specular.Roughness);
            const auto EnvDiff = SampleCalibratedStudioEnv(ShadeN, 0.92f);
            const float DielF0 = std::pow((Sample.Material.Specular.DielectricIOR - 1.0f) / (Sample.Material.Specular.DielectricIOR + 1.0f), 2.0f);
            const float Pow5   = std::pow(1.0f - NdotV, 5.0f);
            const float Metal  = Sample.Material.Specular.Metalness;
            const Mathematics::LinearRGB F0 {
                DielF0 * (1.0f - Metal) + Sample.Material.Diffuse.BaseColor.R * Metal,
                DielF0 * (1.0f - Metal) + Sample.Material.Diffuse.BaseColor.G * Metal,
                DielF0 * (1.0f - Metal) + Sample.Material.Diffuse.BaseColor.B * Metal
            };
            Mathematics::LinearRGB EnvFres {
                F0.R + (1.0f - F0.R) * Pow5,
                F0.G + (1.0f - F0.G) * Pow5,
                F0.B + (1.0f - F0.B) * Pow5
            };
            if (Sample.Material.ThinFilm.ThinFilmWeight > 0.01f)
            {
                EnvFres = MaterialSpace::Scattering::EvaluateSpectralThinFilmFresnel(
                    NdotV,
                    Sample.Material.ThinFilm.ThinFilmThicknessNanometers,
                    Sample.Material.ThinFilm.ThinFilmIOR,
                    1.0f,
                    EnvFres,
                    Sample.Material.ThinFilm.ThinFilmWeight,
                    false);
            }
            const auto EnvCoat = SampleCalibratedStudioEnv(ReflDir, Sample.Material.Clearcoat.CoatRoughness) *
                (Sample.Material.Clearcoat.CoatWeight * (0.04f + 0.96f * Pow5));
            const float SpecAtt = std::pow(1.0f - Sample.Material.Specular.Roughness, 1.85f);

            Mathematics::LinearRGB FinalRgb {
                DirectSum.R + EnvRefl.R * EnvFres.R * SpecAtt + EnvDiff.R * Sample.Material.Diffuse.BaseColor.R * (1.0f - Metal) * 0.55f + EnvCoat.R + Sample.IncandescentRadiance.R,
                DirectSum.G + EnvRefl.G * EnvFres.G * SpecAtt + EnvDiff.G * Sample.Material.Diffuse.BaseColor.G * (1.0f - Metal) * 0.55f + EnvCoat.G + Sample.IncandescentRadiance.G,
                DirectSum.B + EnvRefl.B * EnvFres.B * SpecAtt + EnvDiff.B * Sample.Material.Diffuse.BaseColor.B * (1.0f - Metal) * 0.55f + EnvCoat.B + Sample.IncandescentRadiance.B
            };

            const auto BehindRgb = HitPedestal ? PedestalRgb : Backdrop;
            if (Recipe.PresetIdentity == MaterialSpace::Dynamic::DynamicPresetIdentity::HexagonalEnergyBarrier)
            {
                const float A = std::clamp(Sample.SurfaceOpacity, 0.05f, 0.96f);
                FinalRgb = {
                    BehindRgb.R * (1.0f - A * 0.72f) + (DirectSum.R + EnvCoat.R) * A + Sample.IncandescentRadiance.R,
                    BehindRgb.G * (1.0f - A * 0.72f) + (DirectSum.G + EnvCoat.G) * A + Sample.IncandescentRadiance.G,
                    BehindRgb.B * (1.0f - A * 0.72f) + (DirectSum.B + EnvCoat.B) * A + Sample.IncandescentRadiance.B
                };
            }
            else if (Recipe.PresetIdentity == MaterialSpace::Dynamic::DynamicPresetIdentity::PhysicalOceanWater)
            {
                const float WaterF0 = 0.02f;
                const float WFres   = WaterF0 + (1.0f - WaterF0) * Pow5;
                const float Depth   = 1.35f + (0.35f - 1.35f) * NdotV;
                const float AttR    = std::exp(-1.65f * Depth);
                const float AttG    = std::exp(-0.55f * Depth);
                const float AttB    = std::exp(-0.38f * Depth);
                const float CScat   = Sample.BacklitCrestScatter;
                const Mathematics::LinearRGB DeepC { Parameters.PrimaryColorRgba[0], Parameters.PrimaryColorRgba[1], Parameters.PrimaryColorRgba[2] };
                const Mathematics::LinearRGB ShalC { Parameters.SecondaryColorRgba[0], Parameters.SecondaryColorRgba[1], Parameters.SecondaryColorRgba[2] };
                const Mathematics::LinearRGB Refract {
                    BehindRgb.R * AttR + (DeepC.R + (ShalC.R - DeepC.R) * CScat) * (1.0f - AttR),
                    BehindRgb.G * AttG + (DeepC.G + (ShalC.G - DeepC.G) * CScat) * (1.0f - AttG),
                    BehindRgb.B * AttB + (DeepC.B + (ShalC.B - DeepC.B) * CScat) * (1.0f - AttB)
                };
                const auto WaterRefl = SampleCalibratedStudioEnv(ReflDir, 0.04f);
                const float MixF = std::clamp(WFres + 0.14f, 0.0f, 1.0f);
                FinalRgb = {
                    Refract.R * (1.0f - MixF) + WaterRefl.R * MixF + DirectSum.R * 0.55f,
                    Refract.G * (1.0f - MixF) + WaterRefl.G * MixF + DirectSum.G * 0.55f,
                    Refract.B * (1.0f - MixF) + WaterRefl.B * MixF + DirectSum.B * 0.55f
                };
            }

            Result.DisplayRgba8[PixelOffset + 0u] = EncodeAcesSrgb8(FinalRgb.R);
            Result.DisplayRgba8[PixelOffset + 1u] = EncodeAcesSrgb8(FinalRgb.G);
            Result.DisplayRgba8[PixelOffset + 2u] = EncodeAcesSrgb8(FinalRgb.B);
            Result.DisplayRgba8[PixelOffset + 3u] = 255u;
        }
    }

    Result.ActiveLobeMask = CombinedLobeMask;
    const auto EndTime = std::chrono::steady_clock::now();
    Result.RenderMillisec = std::chrono::duration<float, std::milli>(EndTime - StartTime).count();
    return Result;
}

PreviewRenderResult MaterialPreviewRenderer::RenderPreview(
    const MaterialSpace::Dynamic::DynamicMaterialRecipe& Recipe,
    const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
    PreviewSpecimenGeometry                              Specimen,
    PreviewRenderMode                                    RenderMode,
    const PreviewCameraSetup&                            Camera,
    uint32_t                                             WidthPx,
    uint32_t                                             HeightPx)
{
    PreviewRenderResult Result {};
    if (GpuReady && Specimen == PreviewSpecimenGeometry::ShaderballPedestal &&
        RenderPreviewVulkanCompute(Parameters, Specimen, RenderMode, Camera, WidthPx, HeightPx, Result))
    {
        Result.ActiveLobeMask = MaterialSpace::DetermineActiveLobeMask(Recipe.PrimaryFinish);
        return Result;
    }
    return RenderPreviewCpuReference(Recipe, Parameters, Specimen, Camera, WidthPx, HeightPx);
}

bool MaterialPreviewRenderer::QuerySpecimenHitDirection(
    const PreviewCameraSetup& Camera,
    float                     NormalizedX,
    float                     NormalizedY,
    float                     AspectRatio,
    Mathematics::Axis3D&      OutLocalHitDir) noexcept
{
    const Mathematics::Axis3D CamOrigin = ComputeCameraOrigin(Camera);
    const Mathematics::Axis3D Target    { 0.0f, -0.04f, 0.0f };
    const Mathematics::Axis3D CamFwd    = Mathematics::NormalizeAxis(Target - CamOrigin);
    const Mathematics::Axis3D CamRight  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(CamFwd, { 0.0f, 1.0f, 0.0f }));
    const Mathematics::Axis3D CamUp     = Mathematics::EvaluateCross(CamRight, CamFwd);

    const float SX = (2.0f * NormalizedX - 1.0f) * Camera.FocalScale * std::max(0.1f, AspectRatio);
    const float SY = (1.0f - 2.0f * NormalizedY) * Camera.FocalScale;
    const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(CamFwd + CamRight * SX + CamUp * SY);

    const auto Hit = IntersectSpecimenGeometry(PreviewSpecimenGeometry::ShaderballPedestal, CamOrigin, RayDir);
    if (!Hit.Hit)
    {
        return false;
    }
    OutLocalHitDir = Mathematics::NormalizeAxis(Hit.Normal, { 0.0f, 0.0f, 1.0f });
    return true;
}

} // namespace Frontier::Workspaces
