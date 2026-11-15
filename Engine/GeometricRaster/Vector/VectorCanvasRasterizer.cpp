/*============================================================================================================================================
                                                          VECTORCANVASRASTERIZER.CPP
============================================================================================================================================*/
// 🧩 Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer (Modifier Compute -> 2x SSAA Stencil-Cover -> OpenPBR Stack -> Guide + Planar2D Gizmo MRT).

#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr uint32_t MaxGpuLayersPerRaster     = 33u; // Up to 32 vector layers + 1 ground slot
constexpr uint32_t MaxInstancesPerLayer     = 256u;
constexpr uint32_t MaxModifiersPerLayer     = 8u;
constexpr float    CanonicalDocExtentPx     = 512.0f;

constexpr uint64_t AlignUpBytes(uint64_t ByteCount, uint64_t Alignment) noexcept
{
    return (ByteCount + Alignment - 1ull) & ~(Alignment - 1ull);
}

//------------------------------------------------------------------------------------------------------------------------
//                             16-BYTE ALIGNED GPU STRUCTS MATCHING VECTORSHAREDLAYOUTS.SLANG
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) GPUVectorShapeVertex
{
    float    PositionLocalUV[4];
    uint32_t Attributes[4];
};
static_assert(sizeof(GPUVectorShapeVertex) == 32u);

struct alignas(16) GPUVectorLayerUniforms
{
    float    GeometryBounds[4];
    float    StyleParams[4];
    uint32_t FillHeader[4];
    float    FillAux[4];
    float    FillTintA[4];
    float    FillTintB[4];
    float    StrokeTint[4];
    float    GradientStops[8][4];
    float    PinCoords[12][4];
    float    PinColors[12][4];
    float    MaskParams[4];
    float    MaskSeedOptions[4];
    float    PbrScalars0[4];
    float    PbrScalars1[4];
    float    PbrScalars2[4];
    float    PbrScalars3[4];
    float    PbrScalars4[4];
    float    SurfaceAlbedoTint[4];
    float    SurfaceSpecTint[4];
    float    SurfaceSheenTint[4];
    float    SurfaceSubsurfTint[4];
    float    ShapeLocalBounds[4];
};
static_assert(sizeof(GPUVectorLayerUniforms) == 816u);

struct alignas(16) GPUVectorModifierEntry
{
    uint32_t Header[4];
    float    ScalarBlockA[4];
    float    ScalarBlockB[4];
    float    ScalarBlockC[4];
    float    TintBlock[4];
};
static_assert(sizeof(GPUVectorModifierEntry) == 80u);

struct alignas(16) GPUVectorModifierDispatchParams
{
    uint32_t CountsDimensionsPacked[4];
    float    CanvasMetrics[4];
    float    LayerExtentRotation[4];
};
static_assert(sizeof(GPUVectorModifierDispatchParams) == 48u);

struct alignas(16) GPUVectorInstanceEntry
{
    float    TransformRow0[4];
    float    TransformRow1[4];
    float    ColorOpacityPacked[4];
    float    ModParams[4];
    uint32_t PickMeta[4];
};
static_assert(sizeof(GPUVectorInstanceEntry) == 80u);

struct alignas(16) GPUVectorSummaryMetrics
{
    uint32_t InstanceCount;
    uint32_t MinScaleFixed;
    uint32_t MaxScaleFixed;
    uint32_t ReservedMetric;
};
static_assert(sizeof(GPUVectorSummaryMetrics) == 16u);

struct alignas(16) GPUVectorSurfaceEvalParams
{
    uint32_t ViewOptions[4];
    float    CanvasMeta[4];
};
static_assert(sizeof(GPUVectorSurfaceEvalParams) == 32u);

struct alignas(16) GPUVectorOverlayUniforms
{
    float    ViewportCanvasPacked[4];
    float    TileRectPx[4];
    float    SelectedBoundsPx[4];
    float    SelectedRotationSnap[4];
    uint32_t OverlayOptionBits[4];
    uint32_t CountsActivePacked[4];
    float    MarqueeRectPx[4];
    float    KnotPositions[64][4];
    float    KnotInHandles[64][4];
    float    PinPoints[12][4];
    float    PinColors[12][4];
};
static_assert(sizeof(GPUVectorOverlayUniforms) == 2544u);

struct alignas(16) GPUGizmoUniforms
{
    float    ViewProjection[4][4];
    uint32_t HoverSpacePacked[4];
    float    HeadlightDirection[4];
};
static_assert(sizeof(GPUGizmoUniforms) == 96u);

struct VectorRasterPushConstants
{
    uint32_t Indices[4];
};
static_assert(sizeof(VectorRasterPushConstants) == 16u);

constexpr uint64_t LayerUniformSlotBytes    = AlignUpBytes(sizeof(GPUVectorLayerUniforms), 256ull);
constexpr uint64_t ModifierInputSlotBytes   = AlignUpBytes(MaxModifiersPerLayer * sizeof(GPUVectorModifierEntry), 256ull);
constexpr uint64_t ModifierParamsSlotBytes  = AlignUpBytes(sizeof(GPUVectorModifierDispatchParams), 256ull);

void UnpackColorRgba(ColorRgba Tint, float OutRgba[4]) noexcept
{
    OutRgba[0] = static_cast<float>(Tint.Red) / 255.0f;
    OutRgba[1] = static_cast<float>(Tint.Green) / 255.0f;
    OutRgba[2] = static_cast<float>(Tint.Blue) / 255.0f;
    OutRgba[3] = static_cast<float>(Tint.Alpha) / 255.0f;
}

uint32_t PackColorU32(ColorRgba Tint) noexcept
{
    return static_cast<uint32_t>(Tint.Red) |
           (static_cast<uint32_t>(Tint.Green) << 8u) |
           (static_cast<uint32_t>(Tint.Blue) << 16u) |
           (static_cast<uint32_t>(Tint.Alpha) << 24u);
}

std::filesystem::path ResolveWorkspaceFontFile(std::string_view CandidateOrRelative) noexcept
{
    if (CandidateOrRelative.empty())
    {
        CandidateOrRelative = "EngineContent/FontsArchives/Archivo/Archivo-Bold.ttf";
    }
    std::filesystem::path Path(CandidateOrRelative);
    if (std::filesystem::exists(Path))
    {
        return Path;
    }
#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path FromRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / Path;
    if (std::filesystem::exists(FromRoot))
    {
        return FromRoot;
    }
#endif
    return Path;
}

uint32_t ComputeLayerExpectedInstanceBudget(const Workspaces::PatternStampLayerSpec& Layer) noexcept
{
    uint32_t Count = 1u;
    for (const auto& Mod : Layer.Modifiers)
    {
        if (!Mod.Enabled)
        {
            continue;
        }
        if (Mod.Category == Workspaces::PatternStampModifierCategory::GridArray)
        {
            Count *= static_cast<uint32_t>(std::clamp(Mod.Columns, 1, 16) * std::clamp(Mod.Rows, 1, 16));
        }
        else if (Mod.Category == Workspaces::PatternStampModifierCategory::RadialArray)
        {
            Count *= static_cast<uint32_t>(std::clamp(Mod.RadialCount, 1, 32) * std::clamp(Mod.Rings, 1, 8));
        }
        else if (Mod.Category == Workspaces::PatternStampModifierCategory::MirrorSymmetry)
        {
            Count *= (Mod.MirrorAxis == 2u) ? 4u : 2u;
        }
        else if (Mod.Category == Workspaces::PatternStampModifierCategory::InsetEcho)
        {
            Count *= static_cast<uint32_t>(std::clamp(Mod.EchoCopies, 1, 16));
        }
        Count = std::min(Count, MaxInstancesPerLayer);
    }
    return std::clamp(Count, 1u, MaxInstancesPerLayer);
}

void BuildRowMajorTopDownOrtho2D(
    const VectorCanvasLayoutMetrics& Metrics,
    uint32_t                         ViewportWidthPx,
    uint32_t                         ViewportHeightPx,
    float                            OutOrtho[4][4]) noexcept
{
    // TransformGizmo Planar2D places Pivot at (DocX, -DocY, 0) so +Y points upward while doc coordinates are top-down.
    const float W = std::max(1.0f, static_cast<float>(ViewportWidthPx));
    const float H = std::max(1.0f, static_cast<float>(ViewportHeightPx));
    const float Scale = Metrics.TileSpanPx / std::max(1.0f, Metrics.DocExtentPx);

    std::memset(OutOrtho, 0, sizeof(float) * 16u);
    OutOrtho[0][0] =  2.0f * Scale / W;
    OutOrtho[0][3] =  2.0f * (Metrics.TileMinX / W) - 1.0f;
    OutOrtho[1][1] = -2.0f * Scale / H;
    OutOrtho[1][3] =  2.0f * (Metrics.TileMinY / H) - 1.0f;
    OutOrtho[2][2] =  0.1f;
    OutOrtho[2][3] =  0.5f;
    OutOrtho[3][3] =  1.0f;
}

void EncodeImageBarrier(
    VkCommandBuffer      Cmd,
    VkImage              ImageHandle,
    VkImageAspectFlags   AspectMask,
    VkImageLayout        OldLayout,
    VkImageLayout        NewLayout,
    VkPipelineStageFlags SrcStepMask,
    VkPipelineStageFlags DstStepMask,
    VkAccessFlags        SrcAccess,
    VkAccessFlags        DstAccess) noexcept
{
    VkImageMemoryBarrier Barrier {};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.srcAccessMask                   = SrcAccess;
    Barrier.dstAccessMask                   = DstAccess;
    Barrier.oldLayout                       = OldLayout;
    Barrier.newLayout                       = NewLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = ImageHandle;
    Barrier.subresourceRange.aspectMask     = AspectMask;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = 1u;

    vkCmdPipelineBarrier(Cmd, SrcStepMask, DstStepMask, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
}

void EncodeMemoryBarrier(
    VkCommandBuffer      Cmd,
    VkPipelineStageFlags SrcStepMask,
    VkPipelineStageFlags DstStepMask,
    VkAccessFlags        SrcAccess,
    VkAccessFlags        DstAccess) noexcept
{
    VkMemoryBarrier Barrier {};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = SrcAccess;
    Barrier.dstAccessMask = DstAccess;
    vkCmdPipelineBarrier(Cmd, SrcStepMask, DstStepMask, 0u, 1u, &Barrier, 0u, nullptr, 0u, nullptr);
}

uint32_t EncodeWrapShiftBits(int32_t ShiftX, int32_t ShiftY) noexcept
{
    const uint32_t XBits = (ShiftX > 0) ? 1u : ((ShiftX < 0) ? 3u : 0u);
    const uint32_t YBits = (ShiftY > 0) ? 1u : ((ShiftY < 0) ? 3u : 0u);
    return (XBits << 8u) | (YBits << 10u);
}

void FillLayerUniformStruct(
    const Workspaces::PatternStampLayerSpec& Layer,
    bool                                     PatternMode,
    bool                                     GroundSlotEntry,
    bool                                     GroundEnabled,
    const Workspaces::PatternStampDocumentSnapshot& Doc,
    GPUVectorLayerUniforms&                  OutUniforms) noexcept
{
    std::memset(&OutUniforms, 0, sizeof(OutUniforms));

    if (GroundSlotEntry)
    {
        OutUniforms.FillHeader[0] = static_cast<uint32_t>(Doc.GroundFillSource);
        OutUniforms.FillAux[0]    = Doc.GroundAngleDeg * Constants::DegToRadFloat;
        OutUniforms.FillAux[2]    = GroundEnabled ? 1.0f : 0.0f;
        OutUniforms.FillAux[3]    = 8.0f; // bit 3 = GroundMode
        UnpackColorRgba(Doc.GroundTintA, OutUniforms.FillTintA);
        UnpackColorRgba(Doc.GroundTintB, OutUniforms.FillTintB);
        OutUniforms.PbrScalars4[3] = CanonicalDocExtentPx;
        return;
    }

    OutUniforms.GeometryBounds[0] = Layer.CenterX;
    OutUniforms.GeometryBounds[1] = Layer.CenterY;
    OutUniforms.GeometryBounds[2] = Layer.WidthPx;
    OutUniforms.GeometryBounds[3] = Layer.HeightPx;

    OutUniforms.StyleParams[0] = Layer.RotationDeg * Constants::DegToRadFloat;
    OutUniforms.StyleParams[1] = std::clamp(Layer.Opacity, 0.0f, 1.0f);
    OutUniforms.StyleParams[2] = static_cast<float>(Layer.BlendMode);
    OutUniforms.StyleParams[3] = static_cast<float>(Layer.FinishPreset);

    OutUniforms.FillHeader[0] = static_cast<uint32_t>(Layer.FillSource);
    OutUniforms.FillHeader[1] = static_cast<uint32_t>(std::min<std::size_t>(Layer.FillStops.size(), 8u));
    OutUniforms.FillHeader[2] = static_cast<uint32_t>(Layer.StrokeSource);
    OutUniforms.FillHeader[3] = Layer.Identity;

    OutUniforms.FillAux[0] = Layer.GradientAngleDeg * Constants::DegToRadFloat;
    OutUniforms.FillAux[1] = Layer.StrokeWidthPx;
    OutUniforms.FillAux[2] = Layer.FillEnabled ? 1.0f : 0.0f;
    const uint32_t Flags =
        (Layer.Mask.Enabled ? 1u : 0u) |
        (Layer.Mask.Invert ? 2u : 0u) |
        (PatternMode ? 4u : 0u);
    OutUniforms.FillAux[3] = static_cast<float>(Flags);

    UnpackColorRgba(Layer.FillTint, OutUniforms.FillTintA);
    UnpackColorRgba(Layer.FillTintB, OutUniforms.FillTintB);
    UnpackColorRgba(Layer.StrokeTint, OutUniforms.StrokeTint);

    for (uint32_t I = 0u; I < OutUniforms.FillHeader[1]; ++I)
    {
        UnpackColorRgba(Layer.FillStops[I].Tint, OutUniforms.GradientStops[I]);
        OutUniforms.GradientStops[I][3] = Layer.FillStops[I].Offset;
    }

    for (uint32_t P = 0u; P < 12u; ++P)
    {
        const auto& Pin = Layer.PointFieldPins[P];
        OutUniforms.PinCoords[P][0] = Pin.NormX;
        OutUniforms.PinCoords[P][1] = Pin.NormY;
        OutUniforms.PinCoords[P][2] = Pin.Weight;
        OutUniforms.PinCoords[P][3] = Pin.Active ? 1.0f : 0.0f;
        UnpackColorRgba(Pin.Tint, OutUniforms.PinColors[P]);
    }

    OutUniforms.MaskParams[0]       = static_cast<float>(Layer.Mask.Mode);
    OutUniforms.MaskParams[1]       = Layer.Mask.Frequency;
    OutUniforms.MaskParams[2]       = Layer.Mask.Threshold;
    OutUniforms.MaskParams[3]       = Layer.Mask.Softness;
    OutUniforms.MaskSeedOptions[0] = static_cast<float>(Layer.Mask.Seed);

    float BevelWearAmount = 0.0f;
    for (const auto& Mod : Layer.Modifiers)
    {
        if (Mod.Enabled && Mod.Category == Workspaces::PatternStampModifierCategory::BevelEdgeWear)
        {
            BevelWearAmount = std::max(BevelWearAmount, Mod.ChipWearAmount);
        }
    }
    OutUniforms.MaskSeedOptions[1] = BevelWearAmount;
    OutUniforms.MaskSeedOptions[2] = (Layer.Identity == Doc.SelectedLayerId) ? 1.0f : 0.0f;

    // Default OpenPBR channels before scanning Layer.Channels
    float Roughness   = 0.55f;
    float Metalness   = 0.0f;
    float Height      = 0.25f;
    float NormalStr   = 0.75f;
    float Occlusion   = 1.0f;
    float Emissive    = 0.0f;
    float GlowPower   = 0.0f;
    float Clearcoat   = 0.0f;
    float CoatRough   = 0.08f;
    float Sheen       = 0.0f;
    float SheenRough  = 0.45f;
    float Specular    = 0.50f;
    float Anisotropy  = 0.0f;
    float Iridescence = 0.0f;
    float IridFilm    = 450.0f;
    float Transmission = 0.0f;
    float Subsurface  = 0.0f;
    float OpacitySc   = 1.0f;

    UnpackColorRgba(Layer.FillTint, OutUniforms.SurfaceAlbedoTint);
    OutUniforms.SurfaceSpecTint[0] = 1.0f; OutUniforms.SurfaceSpecTint[1] = 1.0f;
    OutUniforms.SurfaceSpecTint[2] = 1.0f; OutUniforms.SurfaceSpecTint[3] = 1.0f;
    OutUniforms.SurfaceSheenTint[0] = 1.0f; OutUniforms.SurfaceSheenTint[1] = 1.0f;
    OutUniforms.SurfaceSheenTint[2] = 1.0f; OutUniforms.SurfaceSheenTint[3] = 1.0f;
    OutUniforms.SurfaceSubsurfTint[0] = 1.0f; OutUniforms.SurfaceSubsurfTint[1] = 0.85f;
    OutUniforms.SurfaceSubsurfTint[2] = 0.75f; OutUniforms.SurfaceSubsurfTint[3] = 1.0f;

    for (const auto& Ch : Layer.Channels)
    {
        switch (Ch.Key)
        {
            case Workspaces::PatternStampSurfaceChannelKey::Albedo:       UnpackColorRgba(Ch.Tint, OutUniforms.SurfaceAlbedoTint); break;
            case Workspaces::PatternStampSurfaceChannelKey::Roughness:    Roughness    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Metalness:    Metalness    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Height:       Height       = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::NormalStr:    NormalStr    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Occlusion:    Occlusion    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Opacity:      OpacitySc    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Emissive:     Emissive     = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::GlowPower:    GlowPower    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Clearcoat:    Clearcoat    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::CoatRough:    CoatRough    = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Sheen:        Sheen        = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::SheenRough:   SheenRough   = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Specular:     Specular     = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Anisotropy:   Anisotropy   = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Iridescence:  Iridescence  = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::IridFilm:     IridFilm     = Ch.ScalarReading * 1000.0f; break;
            case Workspaces::PatternStampSurfaceChannelKey::Transmission: Transmission = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::Subsurface:   Subsurface   = Ch.ScalarReading; break;
            case Workspaces::PatternStampSurfaceChannelKey::SpecColour:   UnpackColorRgba(Ch.Tint, OutUniforms.SurfaceSpecTint); break;
            case Workspaces::PatternStampSurfaceChannelKey::SheenColour:  UnpackColorRgba(Ch.Tint, OutUniforms.SurfaceSheenTint); break;
            case Workspaces::PatternStampSurfaceChannelKey::SubsurfTint:  UnpackColorRgba(Ch.Tint, OutUniforms.SurfaceSubsurfTint); break;
            default: break;
        }
    }

    OutUniforms.PbrScalars0[0] = Roughness;   OutUniforms.PbrScalars0[1] = Metalness;
    OutUniforms.PbrScalars0[2] = Height;      OutUniforms.PbrScalars0[3] = NormalStr;
    OutUniforms.PbrScalars1[0] = Occlusion;   OutUniforms.PbrScalars1[1] = Emissive;
    OutUniforms.PbrScalars1[2] = GlowPower;   OutUniforms.PbrScalars1[3] = Clearcoat;
    OutUniforms.PbrScalars2[0] = CoatRough;   OutUniforms.PbrScalars2[1] = Sheen;
    OutUniforms.PbrScalars2[2] = SheenRough;  OutUniforms.PbrScalars2[3] = Specular;
    OutUniforms.PbrScalars3[0] = Anisotropy;  OutUniforms.PbrScalars3[1] = Iridescence;
    OutUniforms.PbrScalars3[2] = IridFilm;    OutUniforms.PbrScalars3[3] = Transmission;
    OutUniforms.PbrScalars4[0] = Subsurface;  OutUniforms.PbrScalars4[1] = OpacitySc;
    OutUniforms.PbrScalars4[2] = 0.0f;        OutUniforms.PbrScalars4[3] = CanonicalDocExtentPx;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VectorCanvasRasterizer::~VectorCanvasRasterizer()
{
    TeardownRasterizer();
}

bool VectorCanvasRasterizer::InitializeRasterizer(
    VulkanInterchange*           BoundDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownRasterizer();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    DeviceInterchange = BoundDevice;

    if (!ComputeEncoder.InitializeEncoder(DeviceInterchange, 256u) ||
        !TransferRegister.InitializeRegister(DeviceInterchange, 16ull * 1024ull * 1024ull))
    {
        TeardownRasterizer();
        return false;
    }

    if (!PickQuery.InitializePickQuery(
            DeviceInterchange,
            &ComputeEncoder,
            &TransferRegister,
            CompiledShaderDirectory / "ViewportPickReduction.comp.spv"))
    {
        TeardownRasterizer();
        return false;
    }

    // Select hardware/SwiftShader supported combined depth-stencil attachment format
    DepthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    {
        VkFormatProperties FmtProps {};
        vkGetPhysicalDeviceFormatProperties(DeviceInterchange->AccessPhysicalDevice(), VK_FORMAT_D24_UNORM_S8_UINT, &FmtProps);
        if ((FmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0u)
        {
            DepthStencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
    }

    if (!ConstructFramebufferFormats())
    {
        TeardownRasterizer();
        return false;
    }

    // Compute programs
    const std::array<VkDescriptorType, 4u> ModSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    ModifierExpandComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "VectorModifierExpand.comp.spv", ModSlots);

    const std::array<VkDescriptorType, 10u> StackSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};
    LayerStackComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "VectorLayerStackCompute.comp.spv", StackSlots);

    const std::array<VkDescriptorType, 8u> SurfaceSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};
    PbrSurfaceComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "VectorPbrSurfaceCompute.comp.spv", SurfaceSlots);

    if (ModifierExpandComputeProgram == UINT32_MAX ||
        LayerStackComputeProgram == UINT32_MAX ||
        PbrSurfaceComputeProgram == UINT32_MAX)
    {
        TeardownRasterizer();
        return false;
    }

    // Graphics programs
    if (!ConstructStencilCoverProgram(CompiledShaderDirectory / "VectorStencilFill.vert.spv", CompiledShaderDirectory / "VectorStencilFill.frag.spv", 0u, StencilEvenOddProgram) ||
        !ConstructStencilCoverProgram(CompiledShaderDirectory / "VectorStencilFill.vert.spv", CompiledShaderDirectory / "VectorStencilFill.frag.spv", 1u, StencilNonZeroProgram) ||
        !ConstructStencilCoverProgram(CompiledShaderDirectory / "VectorShapeCover.vert.spv",  CompiledShaderDirectory / "VectorShapeCover.frag.spv",  2u, ShapeFillCoverProgram) ||
        !ConstructStencilCoverProgram(CompiledShaderDirectory / "VectorStrokeCover.vert.spv", CompiledShaderDirectory / "VectorStrokeCover.frag.spv", 3u, ShapeStrokeCoverProgram))
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorType, 4u> OverlaySlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    }};
    const std::array<VkDescriptorType, 2u> GizmoSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    if (!ConstructFinalProgram(CompiledShaderDirectory / "VectorGuideOverlay.vert.spv", CompiledShaderDirectory / "VectorGuideOverlay.frag.spv", OverlaySlots, false, GuideOverlayProgram) ||
        !ConstructFinalProgram(CompiledShaderDirectory / "GizmoRasterization.vert.spv", CompiledShaderDirectory / "GizmoRasterization.frag.spv", GizmoSlots, true, Gizmo2DProgram))
    {
        TeardownRasterizer();
        return false;
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    // Linear clamp sampler for 2x SSAA SurfacePreview read
    VkSamplerCreateInfo SampSpec {};
    SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SampSpec.magFilter    = VK_FILTER_LINEAR;
    SampSpec.minFilter    = VK_FILTER_LINEAR;
    SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.maxLod       = 1.0f;
    if (vkCreateSampler(DeviceHandle, &SampSpec, nullptr, &LinearClampSampler) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    // Graphics descriptor allocator (reset per rasterize call)
    const std::array<VkDescriptorPoolSize, 4u> AllocSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024u },
        { VK_DESCRIPTOR_TYPE_SAMPLER,        128u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  256u },
    }};
    VkDescriptorPoolCreateInfo AllocatorSpec {};
    AllocatorSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    AllocatorSpec.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    AllocatorSpec.maxSets       = 512u;
    AllocatorSpec.poolSizeCount = static_cast<uint32_t>(AllocSizes.size());
    AllocatorSpec.pPoolSizes    = AllocSizes.data();
    if (vkCreateDescriptorPool(DeviceHandle, &AllocatorSpec, nullptr, &GraphicsDescriptorAllocator) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    // Allocate persistent host-writable shader staging buffers
    LayerUniformStaging     = ComputeEncoder.AllocateHostWritableBuffer(MaxGpuLayersPerRaster * LayerUniformSlotBytes,   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    ModifierInputBuffer     = ComputeEncoder.AllocateHostWritableBuffer(MaxGpuLayersPerRaster * ModifierInputSlotBytes,  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ModifierParamsBuffer    = ComputeEncoder.AllocateHostWritableBuffer(MaxGpuLayersPerRaster * ModifierParamsSlotBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    InstanceOutputBuffer    = ComputeEncoder.AllocateHostWritableBuffer(MaxGpuLayersPerRaster * MaxInstancesPerLayer * sizeof(GPUVectorInstanceEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    SummaryMetricsBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUVectorSummaryMetrics),                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    SurfaceEvalParamsBuffer = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUVectorSurfaceEvalParams),             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    OverlayUniformsBuffer   = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUVectorOverlayUniforms),               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    GizmoUniformsBuffer     = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUGizmoUniforms),                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    GizmoVertexBuffer       = ComputeEncoder.AllocateHostWritableBuffer(8192u * sizeof(Editor::GizmoGPUVertex),         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    RegionPickMaskBuffer    = ComputeEncoder.AllocateHostWritableBuffer(256u,                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    if (LayerUniformStaging.BufferHandle == VK_NULL_HANDLE ||
        ModifierInputBuffer.BufferHandle == VK_NULL_HANDLE ||
        InstanceOutputBuffer.BufferHandle == VK_NULL_HANDLE ||
        SummaryMetricsBuffer.BufferHandle == VK_NULL_HANDLE ||
        OverlayUniformsBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownRasterizer();
        return false;
    }

    return true;
}

void VectorCanvasRasterizer::TeardownRasterizer() noexcept
{
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
        vkDeviceWaitIdle(DeviceHandle);

        ReleaseLayerGeometryCache();
        ReleaseLayerScratch();
        ReleaseFinalTarget();

        DeviceInterchange->ReleaseBuffer(LayerUniformStaging);
        DeviceInterchange->ReleaseBuffer(ModifierInputBuffer);
        DeviceInterchange->ReleaseBuffer(ModifierParamsBuffer);
        DeviceInterchange->ReleaseBuffer(InstanceOutputBuffer);
        DeviceInterchange->ReleaseBuffer(SummaryMetricsBuffer);
        DeviceInterchange->ReleaseBuffer(SurfaceEvalParamsBuffer);
        DeviceInterchange->ReleaseBuffer(OverlayUniformsBuffer);
        DeviceInterchange->ReleaseBuffer(GizmoUniformsBuffer);
        DeviceInterchange->ReleaseBuffer(GizmoVertexBuffer);
        DeviceInterchange->ReleaseBuffer(RegionPickMaskBuffer);

        DestroyGraphicsProgram(StencilEvenOddProgram);
        DestroyGraphicsProgram(StencilNonZeroProgram);
        DestroyGraphicsProgram(ShapeFillCoverProgram);
        DestroyGraphicsProgram(ShapeStrokeCoverProgram);
        DestroyGraphicsProgram(GuideOverlayProgram);
        DestroyGraphicsProgram(Gizmo2DProgram);

        if (LinearClampSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(DeviceHandle, LinearClampSampler, nullptr);
            LinearClampSampler = VK_NULL_HANDLE;
        }
        if (GraphicsDescriptorAllocator != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(DeviceHandle, GraphicsDescriptorAllocator, nullptr);
            GraphicsDescriptorAllocator = VK_NULL_HANDLE;
        }
        if (LayerCoverAttachmentFormat != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, LayerCoverAttachmentFormat, nullptr);
            LayerCoverAttachmentFormat = VK_NULL_HANDLE;
        }
        if (LayerCoverContinueFormat != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, LayerCoverContinueFormat, nullptr);
            LayerCoverContinueFormat = VK_NULL_HANDLE;
        }
        if (FinalMRTAttachmentFormat != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, FinalMRTAttachmentFormat, nullptr);
            FinalMRTAttachmentFormat = VK_NULL_HANDLE;
        }
    }

    PickQuery.TeardownPickQuery();
    TransferRegister.TeardownRegister();
    ComputeEncoder.TeardownEncoder();
    CachedColorBytes.clear();
    CachedSessionSerial = 0ull;
    CachedGeometryRevision = 0ull;
    DeviceInterchange = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FRAMEBUFFER & PROGRAM CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

bool VectorCanvasRasterizer::ConstructFramebufferFormats()
{
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    // 1. Per-layer 2x SSAA stencil-cover framebuffer format
    {
        std::array<VkAttachmentDescription, 3u> Atts {};
        Atts[0].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
        Atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        Atts[1].format         = VK_FORMAT_R32G32B32A32_UINT;
        Atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        Atts[2].format         = DepthStencilFormat;
        Atts[2].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const std::array<VkAttachmentReference, 2u> ColorRefs {{
            { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        }};
        const VkAttachmentReference DepthRef { 2u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription Subpass {};
        Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount    = static_cast<uint32_t>(ColorRefs.size());
        Subpass.pColorAttachments       = ColorRefs.data();
        Subpass.pDepthStencilAttachment = &DepthRef;

        std::array<VkSubpassDependency, 2u> Deps {};
        Deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        Deps[0].dstSubpass    = 0u;
        Deps[0].srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        Deps[1].srcSubpass    = 0u;
        Deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        Deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Deps[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo RpCreate {};
        RpCreate.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RpCreate.attachmentCount = static_cast<uint32_t>(Atts.size());
        RpCreate.pAttachments    = Atts.data();
        RpCreate.subpassCount    = 1u;
        RpCreate.pSubpasses      = &Subpass;
        RpCreate.dependencyCount = static_cast<uint32_t>(Deps.size());
        RpCreate.pDependencies   = Deps.data();

        if (vkCreateRenderPass(DeviceHandle, &RpCreate, nullptr, &LayerCoverAttachmentFormat) != VK_SUCCESS)
        {
            return false;
        }

        // Load-and-continue attachment format separating stencil writes from fill-cover zeroing
        Atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        Atts[0].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        Atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        Atts[1].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        Atts[2].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
        Atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[2].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if (vkCreateRenderPass(DeviceHandle, &RpCreate, nullptr, &LayerCoverContinueFormat) != VK_SUCCESS)
        {
            return false;
        }
    }

    // 2. Final viewport RGBA8 + RGBA32UI Pick + D32 Depth format
    {
        std::array<VkAttachmentDescription, 3u> Atts {};
        Atts[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
        Atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        Atts[1].format         = VK_FORMAT_R32G32B32A32_UINT;
        Atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        Atts[2].format         = VK_FORMAT_D32_SFLOAT;
        Atts[2].samples        = VK_SAMPLE_COUNT_1_BIT;
        Atts[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Atts[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Atts[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Atts[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const std::array<VkAttachmentReference, 2u> ColorRefs {{
            { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        }};
        const VkAttachmentReference DepthRef { 2u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription Subpass {};
        Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount    = static_cast<uint32_t>(ColorRefs.size());
        Subpass.pColorAttachments       = ColorRefs.data();
        Subpass.pDepthStencilAttachment = &DepthRef;

        std::array<VkSubpassDependency, 2u> FinalDeps {};
        FinalDeps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        FinalDeps[0].dstSubpass    = 0u;
        FinalDeps[0].srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        FinalDeps[0].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        FinalDeps[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        FinalDeps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        FinalDeps[1].srcSubpass    = 0u;
        FinalDeps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        FinalDeps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        FinalDeps[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
        FinalDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        FinalDeps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo RpCreate {};
        RpCreate.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RpCreate.attachmentCount = static_cast<uint32_t>(Atts.size());
        RpCreate.pAttachments    = Atts.data();
        RpCreate.subpassCount    = 1u;
        RpCreate.pSubpasses      = &Subpass;
        RpCreate.dependencyCount = static_cast<uint32_t>(FinalDeps.size());
        RpCreate.pDependencies   = FinalDeps.data();

        if (vkCreateRenderPass(DeviceHandle, &RpCreate, nullptr, &FinalMRTAttachmentFormat) != VK_SUCCESS)
        {
            return false;
        }
    }

    return true;
}

bool VectorCanvasRasterizer::ConstructStencilCoverProgram(
    const std::filesystem::path& VertSpirv,
    const std::filesystem::path& FragSpirv,
    uint32_t                     StencilVariant,
    GraphicsProgramBundle&       OutBundle)
{
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    OutBundle.VertBinary = DeviceInterchange->LoadShaderBinary(VertSpirv);
    OutBundle.FragBinary = DeviceInterchange->LoadShaderBinary(FragSpirv);
    if (OutBundle.VertBinary == VK_NULL_HANDLE || OutBundle.FragBinary == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 3u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};
    VkDescriptorSetLayoutCreateInfo LayoutSpec { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0u, 3u, Bindings.data() };
    if (vkCreateDescriptorSetLayout(DeviceHandle, &LayoutSpec, nullptr, &OutBundle.SlotLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPushConstantRange PushRange {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(VectorRasterPushConstants);

    VkPipelineLayoutCreateInfo PipeLayoutSpec {};
    PipeLayoutSpec.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutSpec.setLayoutCount         = 1u;
    PipeLayoutSpec.pSetLayouts            = &OutBundle.SlotLayout;
    PipeLayoutSpec.pushConstantRangeCount = 1u;
    PipeLayoutSpec.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(DeviceHandle, &PipeLayoutSpec, nullptr, &OutBundle.ProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    const char* VertEntryName = (StencilVariant == 3u) ? "StrokeVertexEntry"   : "VertexEntry";
    const char* FragEntryName = (StencilVariant == 3u) ? "StrokeFragmentEntry" : "FragmentEntry";
    std::array<VkPipelineShaderStageCreateInfo, 2u> Stages {};
    Stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT,   OutBundle.VertBinary, VertEntryName, nullptr };
    Stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, OutBundle.FragBinary, FragEntryName, nullptr };

    VkPipelineVertexInputStateCreateInfo   VertIn   { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0u, 0u, nullptr, 0u, nullptr };
    VkPipelineInputAssemblyStateCreateInfo InputAsm { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0u, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo      VpSpec   { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0u, 1u, nullptr, 1u, nullptr };

    VkPipelineRasterizationStateCreateInfo RastSpec {};
    RastSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RastSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RastSpec.cullMode    = VK_CULL_MODE_NONE;
    RastSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RastSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo MsSpec { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0u, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0.0f, nullptr, VK_FALSE, VK_FALSE };

    VkPipelineDepthStencilStateCreateInfo DsSpec {};
    DsSpec.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DsSpec.depthTestEnable  = VK_FALSE;
    DsSpec.depthWriteEnable = VK_FALSE;

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    if (StencilVariant == 0u || StencilVariant == 1u)
    {
        // Stencil write-only pass
        DsSpec.stencilTestEnable = VK_TRUE;
        DsSpec.front.compareOp   = VK_COMPARE_OP_ALWAYS;
        DsSpec.front.compareMask = 0xFFu;
        DsSpec.front.writeMask   = 0xFFu;
        DsSpec.front.reference   = 0u;
        DsSpec.front.failOp      = VK_STENCIL_OP_KEEP;
        DsSpec.front.depthFailOp = VK_STENCIL_OP_KEEP;
        DsSpec.front.passOp      = (StencilVariant == 0u) ? VK_STENCIL_OP_INVERT : VK_STENCIL_OP_INCREMENT_AND_WRAP;
        DsSpec.back              = DsSpec.front;
        if (StencilVariant == 1u)
        {
            DsSpec.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        BlendAtts[0].colorWriteMask = 0u;
        BlendAtts[1].colorWriteMask = 0u;
    }
    else if (StencilVariant == 2u)
    {
        // Fill cover: pass where stencil != 0 and zero stencil back out simultaneously
        DsSpec.stencilTestEnable = VK_TRUE;
        DsSpec.front.compareOp   = VK_COMPARE_OP_NOT_EQUAL;
        DsSpec.front.compareMask = 0xFFu;
        DsSpec.front.writeMask   = 0xFFu;
        DsSpec.front.reference   = 0u;
        DsSpec.front.passOp      = VK_STENCIL_OP_ZERO;
        DsSpec.front.failOp      = VK_STENCIL_OP_ZERO;
        DsSpec.front.depthFailOp = VK_STENCIL_OP_ZERO;
        DsSpec.back              = DsSpec.front;

        BlendAtts[0].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAtts[0].blendEnable         = VK_TRUE;
        BlendAtts[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAtts[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtts[0].colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAtts[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAtts[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtts[0].alphaBlendOp        = VK_BLEND_OP_ADD;

        BlendAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAtts[1].blendEnable    = VK_FALSE;
    }
    else
    {
        // Stroke cover: direct triangle pass without stencil requirement
        DsSpec.stencilTestEnable = VK_FALSE;
        BlendAtts[0].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAtts[0].blendEnable         = VK_TRUE;
        BlendAtts[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAtts[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtts[0].colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAtts[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAtts[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtts[0].alphaBlendOp        = VK_BLEND_OP_ADD;

        BlendAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAtts[1].blendEnable    = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo BlendSpec { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0u, VK_FALSE, VK_LOGIC_OP_COPY, 2u, BlendAtts.data(), { 0, 0, 0, 0 } };
    const std::array<VkDynamicState, 2u> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynSpec { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0u, 2u, DynStates.data() };

    VkGraphicsPipelineCreateInfo PipeSpec {};
    PipeSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeSpec.stageCount          = 2u;
    PipeSpec.pStages             = Stages.data();
    PipeSpec.pVertexInputState   = &VertIn;
    PipeSpec.pInputAssemblyState = &InputAsm;
    PipeSpec.pViewportState      = &VpSpec;
    PipeSpec.pRasterizationState = &RastSpec;
    PipeSpec.pMultisampleState   = &MsSpec;
    PipeSpec.pDepthStencilState  = &DsSpec;
    PipeSpec.pColorBlendState    = &BlendSpec;
    PipeSpec.pDynamicState       = &DynSpec;
    PipeSpec.layout              = OutBundle.ProgramLayout;
    PipeSpec.renderPass          = LayerCoverAttachmentFormat;
    PipeSpec.subpass             = 0u;

    return vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &PipeSpec, nullptr, &OutBundle.ProgramHandle) == VK_SUCCESS;
}

bool VectorCanvasRasterizer::ConstructFinalProgram(
    const std::filesystem::path&      VertSpirv,
    const std::filesystem::path&      FragSpirv,
    std::span<const VkDescriptorType> SlotCategories,
    bool                              EnableAlphaBlend,
    GraphicsProgramBundle&            OutBundle)
{
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    OutBundle.VertBinary = DeviceInterchange->LoadShaderBinary(VertSpirv);
    OutBundle.FragBinary = DeviceInterchange->LoadShaderBinary(FragSpirv);
    if (OutBundle.VertBinary == VK_NULL_HANDLE || OutBundle.FragBinary == VK_NULL_HANDLE)
    {
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Bindings(SlotCategories.size());
    for (std::size_t I = 0u; I < SlotCategories.size(); ++I)
    {
        Bindings[I].binding         = static_cast<uint32_t>(I);
        Bindings[I].descriptorType  = SlotCategories[I];
        Bindings[I].descriptorCount = 1u;
        Bindings[I].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo LayoutSpec { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0u, static_cast<uint32_t>(Bindings.size()), Bindings.data() };
    if (vkCreateDescriptorSetLayout(DeviceHandle, &LayoutSpec, nullptr, &OutBundle.SlotLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo PipeLayoutSpec { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0u, 1u, &OutBundle.SlotLayout, 0u, nullptr };
    if (vkCreatePipelineLayout(DeviceHandle, &PipeLayoutSpec, nullptr, &OutBundle.ProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> Stages {};
    Stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT,   OutBundle.VertBinary, "VertexEntry",   nullptr };
    Stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, OutBundle.FragBinary, "FragmentEntry", nullptr };

    VkPipelineVertexInputStateCreateInfo   VertIn   { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0u, 0u, nullptr, 0u, nullptr };
    VkPipelineInputAssemblyStateCreateInfo InputAsm { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0u, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo      VpSpec   { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0u, 1u, nullptr, 1u, nullptr };

    VkPipelineRasterizationStateCreateInfo RastSpec {};
    RastSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RastSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RastSpec.cullMode    = VK_CULL_MODE_NONE;
    RastSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RastSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo  MsSpec { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0u, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0.0f, nullptr, VK_FALSE, VK_FALSE };
    VkPipelineDepthStencilStateCreateInfo DsSpec { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0u, VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_FALSE, VK_FALSE, {}, {}, 0.0f, 1.0f };

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    BlendAtts[0].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[0].blendEnable         = EnableAlphaBlend ? VK_TRUE : VK_FALSE;
    BlendAtts[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAtts[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtts[0].colorBlendOp        = VK_BLEND_OP_ADD;
    BlendAtts[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    BlendAtts[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtts[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    BlendAtts[1].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[1].blendEnable         = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo BlendSpec { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0u, VK_FALSE, VK_LOGIC_OP_COPY, 2u, BlendAtts.data(), { 0, 0, 0, 0 } };
    const std::array<VkDynamicState, 2u> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynSpec { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0u, 2u, DynStates.data() };

    VkGraphicsPipelineCreateInfo PipeSpec {};
    PipeSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeSpec.stageCount          = 2u;
    PipeSpec.pStages             = Stages.data();
    PipeSpec.pVertexInputState   = &VertIn;
    PipeSpec.pInputAssemblyState = &InputAsm;
    PipeSpec.pViewportState      = &VpSpec;
    PipeSpec.pRasterizationState = &RastSpec;
    PipeSpec.pMultisampleState   = &MsSpec;
    PipeSpec.pDepthStencilState  = &DsSpec;
    PipeSpec.pColorBlendState    = &BlendSpec;
    PipeSpec.pDynamicState       = &DynSpec;
    PipeSpec.layout              = OutBundle.ProgramLayout;
    PipeSpec.renderPass          = FinalMRTAttachmentFormat;
    PipeSpec.subpass             = 0u;

    return vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &PipeSpec, nullptr, &OutBundle.ProgramHandle) == VK_SUCCESS;
}

void VectorCanvasRasterizer::DestroyGraphicsProgram(GraphicsProgramBundle& Bundle) noexcept
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    if (Bundle.ProgramHandle != VK_NULL_HANDLE) { vkDestroyPipeline(DeviceHandle, Bundle.ProgramHandle, nullptr); Bundle.ProgramHandle = VK_NULL_HANDLE; }
    if (Bundle.ProgramLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(DeviceHandle, Bundle.ProgramLayout, nullptr); Bundle.ProgramLayout = VK_NULL_HANDLE; }
    if (Bundle.SlotLayout != VK_NULL_HANDLE)    { vkDestroyDescriptorSetLayout(DeviceHandle, Bundle.SlotLayout, nullptr); Bundle.SlotLayout = VK_NULL_HANDLE; }
    if (Bundle.VertBinary != VK_NULL_HANDLE)    { vkDestroyShaderModule(DeviceHandle, Bundle.VertBinary, nullptr); Bundle.VertBinary = VK_NULL_HANDLE; }
    if (Bundle.FragBinary != VK_NULL_HANDLE)    { vkDestroyShaderModule(DeviceHandle, Bundle.FragBinary, nullptr); Bundle.FragBinary = VK_NULL_HANDLE; }
}

bool VectorCanvasRasterizer::EnsureLayerScratch(uint32_t ExtentPx)
{
    if (ScratchTargets.LayerFramebuffer != VK_NULL_HANDLE && ScratchTargets.ExtentPx == ExtentPx)
    {
        return true;
    }
    ReleaseLayerScratch();

    const VkImageUsageFlags CoverUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    const VkImageUsageFlags StackUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    ScratchTargets.LayerCoverColor   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, CoverUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.LayerCoverPick    = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R32G32B32A32_UINT,   CoverUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.LayerDepthStencil = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, DepthStencilFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    ScratchTargets.StackedColor      = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPbrPackA   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPbrPackB   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPbrPackC   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPbrPackD   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPbrExtra   = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.StackedPick       = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R32G32B32A32_UINT,   StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    ScratchTargets.SurfacePreview    = DeviceInterchange->AllocateImage2D(ExtentPx, ExtentPx, VK_FORMAT_R16G16B16A16_SFLOAT, StackUsage, VK_IMAGE_ASPECT_COLOR_BIT);

    if (ScratchTargets.LayerCoverColor.ViewHandle == VK_NULL_HANDLE ||
        ScratchTargets.LayerCoverPick.ViewHandle == VK_NULL_HANDLE ||
        ScratchTargets.LayerDepthStencil.ViewHandle == VK_NULL_HANDLE ||
        ScratchTargets.StackedColor.ViewHandle == VK_NULL_HANDLE ||
        ScratchTargets.SurfacePreview.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseLayerScratch();
        return false;
    }

    const std::array<VkImageView, 3u> FbAtts {{
        ScratchTargets.LayerCoverColor.ViewHandle,
        ScratchTargets.LayerCoverPick.ViewHandle,
        ScratchTargets.LayerDepthStencil.ViewHandle,
    }};
    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = LayerCoverAttachmentFormat;
    FbSpec.attachmentCount = 3u;
    FbSpec.pAttachments    = FbAtts.data();
    FbSpec.width           = ExtentPx;
    FbSpec.height          = ExtentPx;
    FbSpec.layers          = 1u;

    if (vkCreateFramebuffer(DeviceInterchange->AccessDevice(), &FbSpec, nullptr, &ScratchTargets.LayerFramebuffer) != VK_SUCCESS)
    {
        ReleaseLayerScratch();
        return false;
    }
    ScratchTargets.ExtentPx = ExtentPx;
    return true;
}

void VectorCanvasRasterizer::ReleaseLayerScratch() noexcept
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    if (ScratchTargets.LayerFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(DeviceInterchange->AccessDevice(), ScratchTargets.LayerFramebuffer, nullptr);
        ScratchTargets.LayerFramebuffer = VK_NULL_HANDLE;
    }
    DeviceInterchange->ReleaseImage2D(ScratchTargets.LayerCoverColor);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.LayerCoverPick);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.LayerDepthStencil);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedColor);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPbrPackA);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPbrPackB);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPbrPackC);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPbrPackD);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPbrExtra);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.StackedPick);
    DeviceInterchange->ReleaseImage2D(ScratchTargets.SurfacePreview);
    ScratchTargets.ExtentPx = 0u;
}

bool VectorCanvasRasterizer::EnsureFinalTarget(uint32_t WidthPx, uint32_t HeightPx)
{
    if (FinalTarget.Framebuffer != VK_NULL_HANDLE && FinalTarget.WidthPx == WidthPx && FinalTarget.HeightPx == HeightPx)
    {
        return true;
    }
    ReleaseFinalTarget();

    FinalTarget.ColorImage = DeviceInterchange->AllocateImage2D(
        WidthPx, HeightPx, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    FinalTarget.PickImage = DeviceInterchange->AllocateImage2D(
        WidthPx, HeightPx, VK_FORMAT_R32G32B32A32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    FinalTarget.DepthImage = DeviceInterchange->AllocateImage2D(
        WidthPx, HeightPx, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    if (FinalTarget.ColorImage.ViewHandle == VK_NULL_HANDLE ||
        FinalTarget.PickImage.ViewHandle == VK_NULL_HANDLE ||
        FinalTarget.DepthImage.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseFinalTarget();
        return false;
    }

    const std::array<VkImageView, 3u> FbAtts {{
        FinalTarget.ColorImage.ViewHandle,
        FinalTarget.PickImage.ViewHandle,
        FinalTarget.DepthImage.ViewHandle,
    }};
    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = FinalMRTAttachmentFormat;
    FbSpec.attachmentCount = 3u;
    FbSpec.pAttachments    = FbAtts.data();
    FbSpec.width           = WidthPx;
    FbSpec.height          = HeightPx;
    FbSpec.layers          = 1u;

    if (vkCreateFramebuffer(DeviceInterchange->AccessDevice(), &FbSpec, nullptr, &FinalTarget.Framebuffer) != VK_SUCCESS)
    {
        ReleaseFinalTarget();
        return false;
    }
    FinalTarget.PickLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    FinalTarget.WidthPx    = WidthPx;
    FinalTarget.HeightPx   = HeightPx;
    return true;
}

void VectorCanvasRasterizer::ReleaseFinalTarget() noexcept
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    if (FinalTarget.Framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(DeviceInterchange->AccessDevice(), FinalTarget.Framebuffer, nullptr);
        FinalTarget.Framebuffer = VK_NULL_HANDLE;
    }
    DeviceInterchange->ReleaseImage2D(FinalTarget.ColorImage);
    DeviceInterchange->ReleaseImage2D(FinalTarget.PickImage);
    DeviceInterchange->ReleaseImage2D(FinalTarget.DepthImage);
    FinalTarget.PickLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    FinalTarget.WidthPx    = 0u;
    FinalTarget.HeightPx   = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            THORVG VECTOR GEOMETRY COMPILATION
//------------------------------------------------------------------------------------------------------------------------

void VectorCanvasRasterizer::ReleaseLayerGeometryCache() noexcept
{
    if (DeviceInterchange != nullptr)
    {
        for (auto& Pair : CachedLayerGeometry)
        {
            DeviceInterchange->ReleaseBuffer(Pair.second.VertexBuffer);
        }
    }
    CachedLayerGeometry.clear();
}

void VectorCanvasRasterizer::RefreshLayerGeometryCache(const Workspaces::PatternStampWorkspaceSession& Session)
{
    if (CachedSessionSerial == Session.SessionInstanceSerial &&
        CachedGeometryRevision == Session.GeometryRevision &&
        !CachedLayerGeometry.empty())
    {
        return;
    }

    ReleaseLayerGeometryCache();
    CachedSessionSerial    = Session.SessionInstanceSerial;
    CachedGeometryRevision = Session.GeometryRevision;

    for (const auto& Layer : Session.Document.Layers)
    {
        if (Layer.Category == Workspaces::PatternStampLayerCategory::Folder)
        {
            continue;
        }

        std::vector<ExtractedVectorShape> CompiledShapes;
        bool VertsAreLayerLocal = true;

        if (Layer.Category == Workspaces::PatternStampLayerCategory::GradientWash)
        {
            const float Hw = std::max(8.0f, Layer.WidthPx * 0.5f);
            const float Hh = std::max(8.0f, Layer.HeightPx * 0.5f);
            VectorCubicSubpath Quad {};
            Quad.ClosedLoop = true;
            Quad.Knots = {
                { -Hw, -Hh, -Hw, -Hh, -Hw, -Hh, false },
                {  Hw, -Hh,  Hw, -Hh,  Hw, -Hh, false },
                {  Hw,  Hh,  Hw,  Hh,  Hw,  Hh, false },
                { -Hw,  Hh, -Hw,  Hh, -Hw,  Hh, false },
            };
            ExtractedVectorShape WashShape {};
            ThorVGVectorFrontend::CompileCubicSubpathsToGpuShape(
                std::array<VectorCubicSubpath, 1u>{ Quad },
                false,
                true,
                Layer.FillTint,
                false,
                0.0f,
                Layer.StrokeTint,
                Layer.StrokeCap,
                Layer.StrokeJoin,
                WashShape);
            CompiledShapes.push_back(std::move(WashShape));
        }
        else if (Layer.Category == Workspaces::PatternStampLayerCategory::Typographic)
        {
            const std::filesystem::path FontPath = ResolveWorkspaceFontFile(Layer.FontFilePath);
            std::string Diag;
            (void)VectorFrontend.CompileTextToGpuShapes(
                FontPath,
                FontPath.stem().string(),
                Layer.TextContent.empty() ? std::string_view("FRONTIER") : std::string_view(Layer.TextContent),
                std::max(10.0f, Layer.FontSizePx),
                0.0f,
                0.0f,
                Layer.FillTint,
                Layer.StrokeWidthPx,
                Layer.StrokeTint,
                CompiledShapes,
                nullptr,
                nullptr,
                &Diag);
            VertsAreLayerLocal = true;
        }
        else
        {
            Workspaces::PatternStampLayerSpec SeededLayer = Layer;
            Workspaces::PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(SeededLayer);

            float EffectiveStrokeW = SeededLayer.StrokeWidthPx;
            if (SeededLayer.MotifShape == Workspaces::PatternStampMotifShape::LineStroke && EffectiveStrokeW < 2.0f)
            {
                EffectiveStrokeW = 5.0f;
            }
            const bool StrokeOn = EffectiveStrokeW > 0.05f;
            const bool FillOn   = SeededLayer.FillEnabled && (SeededLayer.MotifShape != Workspaces::PatternStampMotifShape::LineStroke);

            ExtractedVectorShape Shape {};
            ThorVGVectorFrontend::CompileCubicSubpathsToGpuShape(
                SeededLayer.CubicSubpaths,
                SeededLayer.EvenOddFillRule,
                FillOn,
                SeededLayer.FillTint,
                StrokeOn,
                EffectiveStrokeW,
                (SeededLayer.MotifShape == Workspaces::PatternStampMotifShape::LineStroke && SeededLayer.StrokeWidthPx < 0.05f)
                    ? SeededLayer.FillTint
                    : SeededLayer.StrokeTint,
                SeededLayer.StrokeCap,
                SeededLayer.StrokeJoin,
                Shape);
            CompiledShapes.push_back(std::move(Shape));
        }

        CompiledLayerGeometry Geo {};
        std::vector<GPUVectorShapeVertex> Verts;
        const float InvW = 1.0f / std::max(1.0f, Layer.WidthPx);
        const float InvH = 1.0f / std::max(1.0f, Layer.HeightPx);

        for (std::size_t SIdx = 0u; SIdx < CompiledShapes.size(); ++SIdx)
        {
            const auto& Src = CompiledShapes[SIdx];
            CompiledLayerGeometry::ShapeBatchSegment Seg {};
            Seg.EvenOddFillRule = Src.EvenOddFillRule;
            Seg.LocalCoords     = VertsAreLayerLocal;
            Seg.BoundsMinX      = Src.BoundsMinX;
            Seg.BoundsMinY      = Src.BoundsMinY;
            Seg.BoundsMaxX      = Src.BoundsMaxX;
            Seg.BoundsMaxY      = Src.BoundsMaxY;

            Seg.FillFirstVertex = static_cast<uint32_t>(Verts.size());
            Seg.FillVertexCount = static_cast<uint32_t>(Src.FillFanTriangles.size());
            const uint32_t FillPackedTint = PackColorU32(Src.FillTint);
            for (const auto& V : Src.FillFanTriangles)
            {
                GPUVectorShapeVertex GpuV {};
                GpuV.PositionLocalUV[0] = V.X;
                GpuV.PositionLocalUV[1] = V.Y;
                GpuV.PositionLocalUV[2] = V.X * InvW;
                GpuV.PositionLocalUV[3] = V.Y * InvH;
                GpuV.Attributes[0]         = 0u;
                GpuV.Attributes[1]         = static_cast<uint32_t>(SIdx);
                GpuV.Attributes[2]         = VertsAreLayerLocal ? 1u : 0u;
                GpuV.Attributes[3]         = FillPackedTint;
                Verts.push_back(GpuV);
            }

            Seg.StrokeFirstVertex = static_cast<uint32_t>(Verts.size());
            Seg.StrokeVertexCount = static_cast<uint32_t>(Src.StrokeTriangles.size());
            const uint32_t StrokePackedTint = PackColorU32(Src.StrokeTint);
            for (const auto& V : Src.StrokeTriangles)
            {
                GPUVectorShapeVertex GpuV {};
                GpuV.PositionLocalUV[0] = V.X;
                GpuV.PositionLocalUV[1] = V.Y;
                GpuV.PositionLocalUV[2] = V.X * InvW;
                GpuV.PositionLocalUV[3] = V.Y * InvH;
                GpuV.Attributes[0]         = 1u;
                GpuV.Attributes[1]         = static_cast<uint32_t>(SIdx);
                GpuV.Attributes[2]         = VertsAreLayerLocal ? 1u : 0u;
                GpuV.Attributes[3]         = StrokePackedTint;
                Verts.push_back(GpuV);
            }

            Geo.Segments.push_back(Seg);
        }

        if (!Verts.empty())
        {
            Geo.TotalVerts   = static_cast<uint32_t>(Verts.size());
            Geo.VertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(Verts.size() * sizeof(GPUVectorShapeVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ComputeEncoder.UploadSpanToBuffer<GPUVectorShapeVertex>(Geo.VertexBuffer, Verts);
            CachedLayerGeometry.emplace(Layer.Identity, std::move(Geo));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PUBLIC COORDINATE & PICKING HELPERS
//------------------------------------------------------------------------------------------------------------------------

VectorCanvasLayoutMetrics VectorCanvasRasterizer::ComputeTileLayoutMetrics(
    uint32_t                                        WidthPx,
    uint32_t                                        HeightPx,
    const Workspaces::PatternStampWorkspaceSession& Session) noexcept
{
    const float W = std::max(1.0f, static_cast<float>(WidthPx));
    const float H = std::max(1.0f, static_cast<float>(HeightPx));
    const float ReferenceSpan = std::min(W * 0.78f, H * 0.84f);
    const float Zoom     = std::clamp(Session.ZoomFraction, 0.25f, 4.0f);
    const float TileSpan = std::max(32.0f, ReferenceSpan * Zoom);

    VectorCanvasLayoutMetrics Metrics {};
    Metrics.DocExtentPx = CanonicalDocExtentPx;
    Metrics.TileSpanPx  = TileSpan;
    Metrics.TileMinX    = (W - TileSpan) * 0.5f + Session.PanOffsetU;
    Metrics.TileMinY    = (H - TileSpan) * 0.5f + Session.PanOffsetV;
    return Metrics;
}

void VectorCanvasRasterizer::ConvertScreenToDocumentPx(
    const VectorCanvasLayoutMetrics& Metrics,
    float                            ScreenX,
    float                            ScreenY,
    float&                           OutDocX,
    float&                           OutDocY) noexcept
{
    const float Scale = Metrics.DocExtentPx / std::max(1.0f, Metrics.TileSpanPx);
    OutDocX = (ScreenX - Metrics.TileMinX) * Scale;
    OutDocY = (ScreenY - Metrics.TileMinY) * Scale;
}

void VectorCanvasRasterizer::ConvertDocumentToNdc(
    const VectorCanvasLayoutMetrics& Metrics,
    uint32_t                         ViewportWidthPx,
    uint32_t                         ViewportHeightPx,
    float                            DocX,
    float                            DocY,
    float&                           OutNdcX,
    float&                           OutNdcY) noexcept
{
    const float Scale = Metrics.TileSpanPx / std::max(1.0f, Metrics.DocExtentPx);
    const float ScreenX = Metrics.TileMinX + DocX * Scale;
    const float ScreenY = Metrics.TileMinY + DocY * Scale;
    OutNdcX = (ScreenX / std::max(1.0f, static_cast<float>(ViewportWidthPx))) * 2.0f - 1.0f;
    OutNdcY = (ScreenY / std::max(1.0f, static_cast<float>(ViewportHeightPx))) * 2.0f - 1.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MAIN GPU VECTOR RASTERIZATION ENTRY
//------------------------------------------------------------------------------------------------------------------------

bool VectorCanvasRasterizer::RasterizeVectorCanvas(
    uint32_t                                        WidthPx,
    uint32_t                                        HeightPx,
    const Workspaces::PatternStampWorkspaceSession& Session,
    const Editor::TransformGizmo*                   OptionalGizmo2D,
    std::vector<uint8_t>*                           OptionalOutRGBA)
{
    const uint32_t SafeW = std::clamp(WidthPx,  64u, 2048u);
    const uint32_t SafeH = std::clamp(HeightPx, 64u, 2048u);
    const bool HardwareGpu = (DeviceInterchange != nullptr && DeviceInterchange->QueryHardwareAccelerated());
    const uint32_t ScratchExtent = HardwareGpu ? std::clamp(std::max(SafeW, SafeH) * 2u, 512u, 1024u) : 512u;

    std::vector<uint8_t>* DestPtr = OptionalOutRGBA != nullptr ? OptionalOutRGBA : &CachedColorBytes;
    const bool Ok = ExecuteCanvasRasterization(SafeW, SafeH, ScratchExtent, true, Session, OptionalGizmo2D, DestPtr);
    if (Ok && OptionalOutRGBA != nullptr)
    {
        CachedColorBytes = *OptionalOutRGBA;
    }
    return Ok;
}

bool VectorCanvasRasterizer::RenderThumbnailRGBA(
    uint32_t                                        ThumbWidthPx,
    uint32_t                                        ThumbHeightPx,
    const Workspaces::PatternStampWorkspaceSession& Session,
    std::vector<uint8_t>&                           OutRGBA)
{
    const uint32_t SafeW = std::clamp(ThumbWidthPx,  32u, 2048u);
    const uint32_t SafeH = std::clamp(ThumbHeightPx, 32u, 2048u);
    const bool HardwareGpu = (DeviceInterchange != nullptr && DeviceInterchange->QueryHardwareAccelerated());
    const uint32_t ScratchExtent = HardwareGpu
        ? std::clamp(std::max(SafeW, SafeH) * 2u, 256u, 1024u)
        : std::clamp(std::max(SafeW, SafeH), 256u, 512u);
    return ExecuteCanvasRasterization(SafeW, SafeH, ScratchExtent, false, Session, nullptr, &OutRGBA);
}

bool VectorCanvasRasterizer::ExecuteCanvasRasterization(
    uint32_t                                        WidthPx,
    uint32_t                                        HeightPx,
    uint32_t                                        ScratchExtentPx,
    bool                                            IncludeGuidesGizmo,
    const Workspaces::PatternStampWorkspaceSession& Session,
    const Editor::TransformGizmo*                   OptionalGizmo2D,
    std::vector<uint8_t>*                           OutRGBA)
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return false;
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    if (!EnsureLayerScratch(ScratchExtentPx) || !EnsureFinalTarget(WidthPx, HeightPx))
    {
        return false;
    }

    ComputeEncoder.ResetTransientDescriptors();
    vkResetDescriptorPool(DeviceHandle, GraphicsDescriptorAllocator, 0u);

    RefreshLayerGeometryCache(Session);

    // 0. Collect visible non-folder layers in bottom->top stack order
    struct ActiveLayerRenderPlan
    {
        const Workspaces::PatternStampLayerSpec* Layer    = nullptr;
        const CompiledLayerGeometry*             Geometry = nullptr;
        uint32_t                                 SlotIndex = 0u;
        uint32_t                                 ExpectedInstances = 1u;
    };
    std::vector<ActiveLayerRenderPlan> ActiveLayers;
    ActiveLayers.reserve(32u);

    for (const auto& Layer : Session.Document.Layers)
    {
        if (ActiveLayers.size() >= 32u)
        {
            break;
        }
        if (Layer.Category == Workspaces::PatternStampLayerCategory::Folder ||
            !Session.QueryEffectiveVisible(Layer.Identity))
        {
            continue;
        }
        auto GeoIt = CachedLayerGeometry.find(Layer.Identity);
        if (GeoIt == CachedLayerGeometry.end() || GeoIt->second.Segments.empty())
        {
            continue;
        }
        ActiveLayerRenderPlan Plan {};
        Plan.Layer             = &Layer;
        Plan.Geometry          = &GeoIt->second;
        Plan.SlotIndex         = static_cast<uint32_t>(ActiveLayers.size() + 1u);
        Plan.ExpectedInstances = ComputeLayerExpectedInstanceBudget(Layer);
        ActiveLayers.push_back(Plan);
    }

    const bool PatternModeActive = (Session.Mode == Workspaces::PatternStampWorkspaceMode::Pattern);

    // Upload ground uniform (slot 0) + all active layer uniforms & modifier specs
    {
        GPUVectorLayerUniforms GroundUniform {};
        Workspaces::PatternStampLayerSpec GroundUniformLayer {};
        FillLayerUniformStruct(GroundUniformLayer, PatternModeActive, true, Session.Document.GroundEnabled, Session.Document, GroundUniform);
        std::memcpy(static_cast<uint8_t*>(LayerUniformStaging.HostAddress), &GroundUniform, sizeof(GroundUniform));
    }

    for (const auto& Plan : ActiveLayers)
    {
        GPUVectorLayerUniforms LayerUni {};
        FillLayerUniformStruct(*Plan.Layer, PatternModeActive, false, Session.Document.GroundEnabled, Session.Document, LayerUni);
        float BMinX = 1.0e30f, BMinY = 1.0e30f, BMaxX = -1.0e30f, BMaxY = -1.0e30f;
        for (const auto& Seg : Plan.Geometry->Segments)
        {
            if (Seg.BoundsMaxX >= Seg.BoundsMinX && Seg.BoundsMaxY >= Seg.BoundsMinY)
            {
                BMinX = std::min(BMinX, Seg.BoundsMinX);
                BMinY = std::min(BMinY, Seg.BoundsMinY);
                BMaxX = std::max(BMaxX, Seg.BoundsMaxX);
                BMaxY = std::max(BMaxY, Seg.BoundsMaxY);
            }
        }
        if (BMinX <= BMaxX && BMinY <= BMaxY)
        {
            LayerUni.ShapeLocalBounds[0] = BMinX;
            LayerUni.ShapeLocalBounds[1] = BMinY;
            LayerUni.ShapeLocalBounds[2] = BMaxX;
            LayerUni.ShapeLocalBounds[3] = BMaxY;
        }
        std::memcpy(
            static_cast<uint8_t*>(LayerUniformStaging.HostAddress) + Plan.SlotIndex * LayerUniformSlotBytes,
            &LayerUni,
            sizeof(LayerUni));

        std::array<GPUVectorModifierEntry, MaxModifiersPerLayer> ModArray {};
        const uint32_t ModCount = static_cast<uint32_t>(std::min<std::size_t>(Plan.Layer->Modifiers.size(), MaxModifiersPerLayer));
        for (uint32_t M = 0u; M < ModCount; ++M)
        {
            const auto& SrcMod = Plan.Layer->Modifiers[M];
            auto&       DstMod = ModArray[M];
            DstMod.Header[0] = static_cast<uint32_t>(SrcMod.Category);
            DstMod.Header[1] = SrcMod.Enabled ? 1u : 0u;
            switch (SrcMod.Category)
            {
                case Workspaces::PatternStampModifierCategory::NoiseScale:
                    DstMod.Header[2]       = SrcMod.NoiseAlgorithm;
                    DstMod.Header[3]       = SrcMod.ModulateTarget;
                    DstMod.ScalarBlockA[0] = SrcMod.MinScale;
                    DstMod.ScalarBlockB[0] = SrcMod.MaxScale;
                    DstMod.ScalarBlockB[1] = SrcMod.Frequency;
                    UnpackColorRgba(SrcMod.TargetTintB, DstMod.TintBlock);
                    break;
                case Workspaces::PatternStampModifierCategory::GridArray:
                    DstMod.Header[2]       = SrcMod.GridLayout;
                    DstMod.ScalarBlockA[0] = static_cast<float>(SrcMod.Columns);
                    DstMod.ScalarBlockB[0] = static_cast<float>(SrcMod.Rows);
                    break;
                case Workspaces::PatternStampModifierCategory::RadialArray:
                    DstMod.ScalarBlockA[0] = static_cast<float>(SrcMod.RadialCount);
                    DstMod.ScalarBlockB[0] = SrcMod.RadiusPx;
                    DstMod.ScalarBlockC[0] = static_cast<float>(SrcMod.Rings);
                    break;
                case Workspaces::PatternStampModifierCategory::MirrorSymmetry:
                    DstMod.Header[2] = SrcMod.MirrorAxis;
                    break;
                case Workspaces::PatternStampModifierCategory::InsetEcho:
                    DstMod.ScalarBlockA[0] = static_cast<float>(SrcMod.EchoCopies);
                    DstMod.ScalarBlockB[0] = SrcMod.InsetStepPx;
                    UnpackColorRgba(SrcMod.AltTint, DstMod.TintBlock);
                    break;
                case Workspaces::PatternStampModifierCategory::ScatterDropout:
                    DstMod.ScalarBlockA[0] = SrcMod.PosJitterPx;
                    DstMod.ScalarBlockB[0] = SrcMod.RotJitterDeg;
                    DstMod.ScalarBlockC[0] = SrcMod.Dropout;
                    break;
                case Workspaces::PatternStampModifierCategory::DomainWarp:
                    DstMod.Header[2]       = SrcMod.WarpMode;
                    DstMod.ScalarBlockA[0] = SrcMod.WarpAmplitude;
                    break;
                case Workspaces::PatternStampModifierCategory::BevelEdgeWear:
                    DstMod.ScalarBlockA[0] = SrcMod.ChipWearAmount;
                    break;
                default:
                    break;
            }
        }
        std::memcpy(
            static_cast<uint8_t*>(ModifierInputBuffer.HostAddress) + Plan.SlotIndex * ModifierInputSlotBytes,
            ModArray.data(),
            sizeof(ModArray));

        GPUVectorModifierDispatchParams DispParams {};
        DispParams.CountsDimensionsPacked[0]     = ModCount;
        DispParams.CountsDimensionsPacked[1]     = Plan.Layer->Identity;
        DispParams.CountsDimensionsPacked[2]     = Plan.SlotIndex;
        DispParams.CountsDimensionsPacked[3]     = Plan.ExpectedInstances;
        DispParams.CanvasMetrics[0]     = CanonicalDocExtentPx;
        DispParams.CanvasMetrics[1]     = PatternModeActive ? 1.0f : 0.0f;
        DispParams.CanvasMetrics[2]     = Plan.Layer->CenterX;
        DispParams.CanvasMetrics[3]     = Plan.Layer->CenterY;
        DispParams.LayerExtentRotation[0] = Plan.Layer->WidthPx;
        DispParams.LayerExtentRotation[1] = Plan.Layer->HeightPx;
        DispParams.LayerExtentRotation[2] = Plan.Layer->RotationDeg * Constants::DegToRadFloat;
        DispParams.LayerExtentRotation[3] = static_cast<float>(Plan.SlotIndex * MaxInstancesPerLayer);
        std::memcpy(
            static_cast<uint8_t*>(ModifierParamsBuffer.HostAddress) + Plan.SlotIndex * ModifierParamsSlotBytes,
            &DispParams,
            sizeof(DispParams));
    }

    // Reset atomic GPU metrics accumulator
    {
        GPUVectorSummaryMetrics ZeroSummary { 0u, 100000000u, 0u, 0u };
        ComputeEncoder.UploadSpanToBuffer<GPUVectorSummaryMetrics>(SummaryMetricsBuffer, std::array<GPUVectorSummaryMetrics, 1u>{ ZeroSummary });
    }

    // Surface evaluation & overlay uniforms
    VectorCanvasLayoutMetrics TileLayout = ComputeTileLayoutMetrics(WidthPx, HeightPx, Session);
    if (!IncludeGuidesGizmo)
    {
        // Thumbnail occupies full output rectangle cleanly
        TileLayout.TileMinX   = 0.0f;
        TileLayout.TileMinY   = 0.0f;
        TileLayout.TileSpanPx = static_cast<float>(std::min(WidthPx, HeightPx));
    }

    {
        GPUVectorSurfaceEvalParams EvalP {};
        EvalP.ViewOptions[0] = IncludeGuidesGizmo ? static_cast<uint32_t>(Session.ViewChannel) : 0u;
        EvalP.ViewOptions[1] = (Session.Mode == Workspaces::PatternStampWorkspaceMode::Stamp) ? 1u : 0u;
        EvalP.ViewOptions[2] = (IncludeGuidesGizmo && Session.Document.ShowGhostTileBorders) ? 1u : 0u;
        EvalP.ViewOptions[3] = Session.Document.GroundEnabled ? 1u : 0u;
        EvalP.CanvasMeta[0]  = CanonicalDocExtentPx;
        EvalP.CanvasMeta[1]  = 1.0f;
        ComputeEncoder.UploadSpanToBuffer<GPUVectorSurfaceEvalParams>(
            SurfaceEvalParamsBuffer, std::array<GPUVectorSurfaceEvalParams, 1u>{ EvalP });
    }

    {
        GPUVectorOverlayUniforms OvUni {};
        std::memset(&OvUni, 0, sizeof(OvUni));
        OvUni.ViewportCanvasPacked[0] = static_cast<float>(WidthPx);
        OvUni.ViewportCanvasPacked[1] = static_cast<float>(HeightPx);
        OvUni.ViewportCanvasPacked[2] = Session.PanOffsetU;
        OvUni.ViewportCanvasPacked[3] = Session.PanOffsetV;

        OvUni.TileRectPx[0] = TileLayout.TileMinX;
        OvUni.TileRectPx[1] = TileLayout.TileMinY;
        OvUni.TileRectPx[2] = TileLayout.TileSpanPx;
        OvUni.TileRectPx[3] = TileLayout.DocExtentPx;

        if (IncludeGuidesGizmo)
        {
            const uint32_t OvOptionBits =
                (Session.ShowGrid ? 1u : 0u) |
                (Session.ShowAxes ? 2u : 0u) |
                ((PatternModeActive && Session.Document.ShowGhostTileBorders) ? 4u : 0u) |
                (Session.ViewChannel == Workspaces::PatternStampViewChannel::Seamless2x2 ? 8u : 0u) |
                (Session.Mode == Workspaces::PatternStampWorkspaceMode::Stamp ? 16u : 0u);
            OvUni.OverlayOptionBits[0] = OvOptionBits;
            OvUni.OverlayOptionBits[1] = Session.Document.SelectedLayerId;
            OvUni.OverlayOptionBits[2] = static_cast<uint32_t>(Session.ActiveTool);
            OvUni.OverlayOptionBits[3] = Session.HoveredLayerId;

            OvUni.SelectedRotationSnap[1] = static_cast<float>(Session.SnapStepPx);
            OvUni.SelectedRotationSnap[2] = Session.ZoomFraction;

            if (const auto* Sel = Session.InspectSelectedLayer())
            {
                const float RotRad = Sel->RotationDeg * Constants::DegToRadFloat;
                const float CosR   = std::cos(RotRad);
                const float SinR   = std::sin(RotRad);
                OvUni.SelectedBoundsPx[0]   = Sel->CenterX;
                OvUni.SelectedBoundsPx[1]   = Sel->CenterY;
                OvUni.SelectedBoundsPx[2]   = Sel->WidthPx;
                OvUni.SelectedBoundsPx[3]   = Sel->HeightPx;
                OvUni.SelectedRotationSnap[0] = RotRad;

                // Bezier knots rotated into document space for EditPoints / Curve / Pen modes
                uint32_t KnotIdx = 0u;
                for (const auto& Sub : Sel->CubicSubpaths)
                {
                    for (const auto& K : Sub.Knots)
                    {
                        if (KnotIdx >= 64u) break;
                        OvUni.KnotPositions[KnotIdx][0] = Sel->CenterX + (K.AnchorX * CosR - K.AnchorY * SinR);
                        OvUni.KnotPositions[KnotIdx][1] = Sel->CenterY + (K.AnchorX * SinR + K.AnchorY * CosR);
                        OvUni.KnotPositions[KnotIdx][2] = Sel->CenterX + (K.OutHandleX * CosR - K.OutHandleY * SinR);
                        OvUni.KnotPositions[KnotIdx][3] = Sel->CenterY + (K.OutHandleX * SinR + K.OutHandleY * CosR);
                        OvUni.KnotInHandles[KnotIdx][0] = Sel->CenterX + (K.InHandleX * CosR - K.InHandleY * SinR);
                        OvUni.KnotInHandles[KnotIdx][1] = Sel->CenterY + (K.InHandleX * SinR + K.InHandleY * CosR);
                        OvUni.KnotInHandles[KnotIdx][2] = K.Smooth ? 1.0f : 0.0f;
                        ++KnotIdx;
                    }
                }
                OvUni.CountsActivePacked[0] = KnotIdx;
                OvUni.CountsActivePacked[2] = (Session.ActiveKnotIndex >= 0) ? static_cast<uint32_t>(Session.ActiveKnotIndex) : 0xFFFFFFFFu;

                if (Sel->FillSource == Workspaces::PatternStampFillSourceMode::PointField ||
                    Session.ActiveTool == Workspaces::PatternStampToolCategory::PointGradient)
                {
                    OvUni.CountsActivePacked[1] = 12u;
                    OvUni.CountsActivePacked[3] = (Session.ActivePointPinIndex >= 0) ? static_cast<uint32_t>(Session.ActivePointPinIndex) : 0xFFFFFFFFu;
                    for (uint32_t P = 0u; P < 12u; ++P)
                    {
                        const auto& Pin = Sel->PointFieldPins[P];
                        OvUni.PinPoints[P][0] = Pin.NormX;
                        OvUni.PinPoints[P][1] = Pin.NormY;
                        OvUni.PinPoints[P][2] = Pin.Active ? 1.0f : 0.0f;
                        UnpackColorRgba(Pin.Tint, OvUni.PinColors[P]);
                    }
                }
            }

            if (Session.MarqueeActive)
            {
                const float DocToScreenScale = TileLayout.TileSpanPx / std::max(1.0f, TileLayout.DocExtentPx);
                const float MqMinDocX        = std::min(Session.MarqueeStartX, Session.MarqueeEndX);
                const float MqMinDocY        = std::min(Session.MarqueeStartY, Session.MarqueeEndY);
                const float MqMaxDocX        = std::max(Session.MarqueeStartX, Session.MarqueeEndX);
                const float MqMaxDocY        = std::max(Session.MarqueeStartY, Session.MarqueeEndY);
                OvUni.MarqueeRectPx[0]       = TileLayout.TileMinX + MqMinDocX * DocToScreenScale;
                OvUni.MarqueeRectPx[1]       = TileLayout.TileMinY + MqMinDocY * DocToScreenScale;
                OvUni.MarqueeRectPx[2]       = TileLayout.TileMinX + MqMaxDocX * DocToScreenScale;
                OvUni.MarqueeRectPx[3]       = TileLayout.TileMinY + MqMaxDocY * DocToScreenScale;
            }
        }
        ComputeEncoder.UploadSpanToBuffer<GPUVectorOverlayUniforms>(
            OverlayUniformsBuffer, std::array<GPUVectorOverlayUniforms, 1u>{ OvUni });
    }

    uint32_t GizmoVertCount = 0u;
    if (IncludeGuidesGizmo && OptionalGizmo2D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> GizmoVerts = OptionalGizmo2D->BuildTriangleStream();
        GizmoVertCount = static_cast<uint32_t>(GizmoVerts.size());
        if (GizmoVertCount > 0u)
        {
            const uint64_t ReqBytes = GizmoVerts.size() * sizeof(Editor::GizmoGPUVertex);
            if (GizmoVertexBuffer.ByteCapacity < ReqBytes)
            {
                DeviceInterchange->ReleaseBuffer(GizmoVertexBuffer);
                GizmoVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(ReqBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            ComputeEncoder.UploadSpanToBuffer<Editor::GizmoGPUVertex>(GizmoVertexBuffer, GizmoVerts);

            GPUGizmoUniforms GizUni {};
            BuildRowMajorTopDownOrtho2D(TileLayout, WidthPx, HeightPx, GizUni.ViewProjection);
            GizUni.HoverSpacePacked[0]      = static_cast<uint32_t>(OptionalGizmo2D->QueryHoveredHandle());
            GizUni.HoverSpacePacked[1]      = static_cast<uint32_t>(OptionalGizmo2D->QueryActiveHandle());
            GizUni.HoverSpacePacked[2]      = 0u;
            GizUni.HeadlightDirection[0] = 0.35f;
            GizUni.HeadlightDirection[1] = 0.45f;
            GizUni.HeadlightDirection[2] = 0.82f;
            GizUni.HeadlightDirection[3] = 1.0f;
            ComputeEncoder.UploadSpanToBuffer<GPUGizmoUniforms>(
                GizmoUniformsBuffer, std::array<GPUGizmoUniforms, 1u>{ GizUni });
        }
    }

    // Begin command buffer recording
    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();

    // 1. Transition all scratch targets to GENERAL for compute initialization
    for (const auto* Img : {
             &ScratchTargets.LayerCoverColor,
             &ScratchTargets.LayerCoverPick,
             &ScratchTargets.StackedColor,
             &ScratchTargets.StackedPbrPackA,
             &ScratchTargets.StackedPbrPackB,
             &ScratchTargets.StackedPbrPackC,
             &ScratchTargets.StackedPbrPackD,
             &ScratchTargets.StackedPbrExtra,
             &ScratchTargets.StackedPick,
             &ScratchTargets.SurfacePreview })
    {
        EncodeImageBarrier(
            Cmd,
            Img->ImageHandle,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
    }

    const uint32_t GroupsX = (ScratchExtentPx + 7u) / 8u;
    const uint32_t GroupsY = (ScratchExtentPx + 7u) / 8u;

    // 2. Ground fill initialization into StackedColor + OpenPBR buffers (Slot 0)
    {
        const std::array<ComputeSlotDescriptor, 10u> GroundStackSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LayerUniformStaging.BufferHandle, sizeof(GPUVectorLayerUniforms), 0ull },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.LayerCoverColor.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.LayerCoverPick.ViewHandle,  VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedColor.ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackA.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackB.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackC.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackD.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrExtra.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPick.ViewHandle,     VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        }};
        (void)ComputeEncoder.EncodeComputeDispatch(Cmd, LayerStackComputeProgram, GroundStackSlots, GroupsX, GroupsY, 1u);
        EncodeMemoryBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }

    // 3. Per-layer GPU Modifier Expand -> 2x SSAA Stencil-Cover -> Compute Layer Stack
    for (const auto& Plan : ActiveLayers)
    {
        // 3a. Expand layer modifiers into InstanceOutputBuffer[BaseOffset..]
        const std::array<ComputeSlotDescriptor, 4u> ModExpandSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ModifierParamsBuffer.BufferHandle, sizeof(GPUVectorModifierDispatchParams), Plan.SlotIndex * ModifierParamsSlotBytes },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ModifierInputBuffer.BufferHandle,  ModifierInputSlotBytes,                  Plan.SlotIndex * ModifierInputSlotBytes },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InstanceOutputBuffer.BufferHandle, VK_WHOLE_SIZE,                           0ull },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SummaryMetricsBuffer.BufferHandle, VK_WHOLE_SIZE,                           0ull },
        }};
        (void)ComputeEncoder.EncodeComputeDispatch(Cmd, ModifierExpandComputeProgram, ModExpandSlots, 1u, 1u, 1u);
        EncodeMemoryBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        EncodeImageBarrier(
            Cmd,
            ScratchTargets.LayerCoverColor.ImageHandle,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        EncodeImageBarrier(
            Cmd,
            ScratchTargets.LayerCoverPick.ImageHandle,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        EncodeImageBarrier(
            Cmd,
            ScratchTargets.LayerDepthStencil.ImageHandle,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        // Allocate layer graphics descriptor set
        VkDescriptorSet LayerGraphicsSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo LayerAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &StencilEvenOddProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &LayerAlloc, &LayerGraphicsSet) != VK_SUCCESS || LayerGraphicsSet == VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 3u> LayerBufs {{
            { LayerUniformStaging.BufferHandle, Plan.SlotIndex * LayerUniformSlotBytes, sizeof(GPUVectorLayerUniforms) },
            { Plan.Geometry->VertexBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE },
            { InstanceOutputBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE },
        }};
        std::array<VkWriteDescriptorSet, 3u> LayerWrites {};
        for (uint32_t B = 0u; B < 3u; ++B)
        {
            LayerWrites[B].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            LayerWrites[B].dstSet          = LayerGraphicsSet;
            LayerWrites[B].dstBinding      = B;
            LayerWrites[B].descriptorCount = 1u;
            LayerWrites[B].descriptorType  = (B == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            LayerWrites[B].pBufferInfo     = &LayerBufs[B];
        }
        vkUpdateDescriptorSets(DeviceHandle, 3u, LayerWrites.data(), 0u, nullptr);

        // 3b. Stencil-Cover render pass into LayerCoverColor + LayerCoverPick
        std::array<VkClearValue, 3u> LayerClears {};
        LayerClears[0].color           = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        LayerClears[1].color.uint32[0] = 0u;
        LayerClears[1].color.uint32[1] = 0u;
        LayerClears[1].color.uint32[2] = 0u;
        LayerClears[1].color.uint32[3] = 0u;
        LayerClears[2].depthStencil    = { 1.0f, 0u };

        VkRenderPassBeginInfo RpBegin {};
        RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        RpBegin.renderPass        = LayerCoverAttachmentFormat;
        RpBegin.framebuffer       = ScratchTargets.LayerFramebuffer;
        RpBegin.renderArea.extent = { ScratchExtentPx, ScratchExtentPx };
        RpBegin.clearValueCount   = 3u;
        RpBegin.pClearValues      = LayerClears.data();
        vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport ScratchVp { 0.0f, 0.0f, static_cast<float>(ScratchExtentPx), static_cast<float>(ScratchExtentPx), 0.0f, 1.0f };
        VkRect2D   ScratchScissor { { 0, 0 }, { ScratchExtentPx, ScratchExtentPx } };
        vkCmdSetViewport(Cmd, 0u, 1u, &ScratchVp);
        vkCmdSetScissor(Cmd, 0u, 1u, &ScratchScissor);

        // Determine periodic tile wrap shifts needed when Pattern mode crosses tile edge
        std::vector<std::pair<int32_t, int32_t>> WrapOffsets { { 0, 0 } };
        if (PatternModeActive && Plan.Layer->Category != Workspaces::PatternStampLayerCategory::GradientWash)
        {
            bool NeedCardinalWrap = !Plan.Layer->Modifiers.empty();
            if (!NeedCardinalWrap)
            {
                const float HalfMax = 0.5f * std::max(Plan.Layer->WidthPx, Plan.Layer->HeightPx) + 12.0f;
                if (Plan.Layer->CenterX - HalfMax < 0.0f || Plan.Layer->CenterX + HalfMax > CanonicalDocExtentPx ||
                    Plan.Layer->CenterY - HalfMax < 0.0f || Plan.Layer->CenterY + HalfMax > CanonicalDocExtentPx)
                {
                    NeedCardinalWrap = true;
                }
            }
            if (NeedCardinalWrap)
            {
                WrapOffsets.push_back({ -1,  0 });
                WrapOffsets.push_back({  1,  0 });
                WrapOffsets.push_back({  0, -1 });
                WrapOffsets.push_back({  0,  1 });
                WrapOffsets.push_back({ -1, -1 });
                WrapOffsets.push_back({  1, -1 });
                WrapOffsets.push_back({ -1,  1 });
                WrapOffsets.push_back({  1,  1 });
            }
        }

        // All stencil winding, stencil-cover (with passOp/failOp = ZERO), and stroke draws execute in strict
        // Vulkan §26.2/§27.10/§28.1 primitive rasterization order inside a single inline subpass.
        const uint32_t FirstInstOffset = Plan.SlotIndex * MaxInstancesPerLayer;
        for (const auto& Wrap : WrapOffsets)
        {
            const uint32_t WrapMask = EncodeWrapShiftBits(Wrap.first, Wrap.second);
            for (const auto& Seg : Plan.Geometry->Segments)
            {
                for (uint32_t Inst = 0u; Inst < Plan.ExpectedInstances; ++Inst)
                {
                    const uint32_t AbsInst = FirstInstOffset + Inst;

                    if (Seg.FillVertexCount >= 3u)
                    {
                        const GraphicsProgramBundle& StencilProg = Seg.EvenOddFillRule ? StencilEvenOddProgram : StencilNonZeroProgram;
                        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, StencilProg.ProgramHandle);
                        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, StencilProg.ProgramLayout, 0u, 1u, &LayerGraphicsSet, 0u, nullptr);

                        VectorRasterPushConstants StencilPush {{ Plan.SlotIndex, AbsInst, WrapMask, Seg.FillFirstVertex }};
                        vkCmdPushConstants(Cmd, StencilProg.ProgramLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(StencilPush), &StencilPush);
                        vkCmdDraw(Cmd, Seg.FillVertexCount, 1u, 0u, 0u);

                        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ShapeFillCoverProgram.ProgramHandle);
                        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ShapeFillCoverProgram.ProgramLayout, 0u, 1u, &LayerGraphicsSet, 0u, nullptr);
                        VectorRasterPushConstants CoverPush {{ Plan.SlotIndex, AbsInst, WrapMask, 0u }};
                        vkCmdPushConstants(Cmd, ShapeFillCoverProgram.ProgramLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(CoverPush), &CoverPush);
                        vkCmdDraw(Cmd, 3u, 1u, 0u, 0u);
                    }

                    if (Seg.StrokeVertexCount >= 3u)
                    {
                        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ShapeStrokeCoverProgram.ProgramHandle);
                        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ShapeStrokeCoverProgram.ProgramLayout, 0u, 1u, &LayerGraphicsSet, 0u, nullptr);
                        VectorRasterPushConstants StrokePush {{ Plan.SlotIndex, AbsInst, 1u | WrapMask, Seg.StrokeFirstVertex }};
                        vkCmdPushConstants(Cmd, ShapeStrokeCoverProgram.ProgramLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(StrokePush), &StrokePush);
                        vkCmdDraw(Cmd, Seg.StrokeVertexCount, 1u, 0u, 0u);
                    }
                }
            }
        }

        vkCmdEndRenderPass(Cmd);

        EncodeImageBarrier(
            Cmd,
            ScratchTargets.LayerCoverColor.ImageHandle,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        EncodeImageBarrier(
            Cmd,
            ScratchTargets.LayerCoverPick.ImageHandle,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);

        // 3c. Compute composite layer onto StackedColor + 4x OpenPBR + StackedPick
        const std::array<ComputeSlotDescriptor, 10u> LayerStackSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LayerUniformStaging.BufferHandle, sizeof(GPUVectorLayerUniforms), Plan.SlotIndex * LayerUniformSlotBytes },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.LayerCoverColor.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.LayerCoverPick.ViewHandle,  VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedColor.ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackA.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackB.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackC.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackD.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrExtra.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPick.ViewHandle,     VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        }};
        (void)ComputeEncoder.EncodeComputeDispatch(Cmd, LayerStackComputeProgram, LayerStackSlots, GroupsX, GroupsY, 1u);
        EncodeMemoryBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    // 4. OpenPBR Surface / Channel Diagnostic Compute Evaluation -> SurfacePreview
    {
        const std::array<ComputeSlotDescriptor, 8u> EvalSlots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceEvalParamsBuffer.BufferHandle, sizeof(GPUVectorSurfaceEvalParams), 0ull },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedColor.ViewHandle,    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackA.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackB.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackC.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrPackD.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.StackedPbrExtra.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, 0ull, 0ull, ScratchTargets.SurfacePreview.ViewHandle,  VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        }};
        (void)ComputeEncoder.EncodeComputeDispatch(Cmd, PbrSurfaceComputeProgram, EvalSlots, GroupsX, GroupsY, 1u);
    }

    // 5. Transition SurfacePreview & StackedPick for final fragment sampling
    EncodeImageBarrier(
        Cmd, ScratchTargets.SurfacePreview.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    EncodeImageBarrier(
        Cmd, ScratchTargets.StackedPick.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    // 6. Allocate Final MRT descriptor sets (Overlay + Optional Gizmo)
    VkDescriptorSet OverlaySet = VK_NULL_HANDLE;
    {
        VkDescriptorSetAllocateInfo OvAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &GuideOverlayProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &OvAlloc, &OverlaySet) != VK_SUCCESS || OverlaySet == VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
            return false;
        }
        const VkDescriptorBufferInfo OvBuf { OverlayUniformsBuffer.BufferHandle, 0ull, sizeof(GPUVectorOverlayUniforms) };
        const VkDescriptorImageInfo  OvSamp { LinearClampSampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
        const VkDescriptorImageInfo  OvSurf { VK_NULL_HANDLE, ScratchTargets.SurfacePreview.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorImageInfo  OvPick { VK_NULL_HANDLE, ScratchTargets.StackedPick.ViewHandle,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 4u> Writes {};
        Writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, OverlaySet, 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &OvBuf, nullptr };
        Writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, OverlaySet, 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLER, &OvSamp, nullptr, nullptr };
        Writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, OverlaySet, 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &OvSurf, nullptr, nullptr };
        Writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, OverlaySet, 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &OvPick, nullptr, nullptr };
        vkUpdateDescriptorSets(DeviceHandle, 4u, Writes.data(), 0u, nullptr);
    }

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (GizmoVertCount > 0u)
    {
        VkDescriptorSetAllocateInfo GizAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &Gizmo2DProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &GizAlloc, &GizmoSet) != VK_SUCCESS || GizmoSet == VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
            return false;
        }
        const VkDescriptorBufferInfo GizUniSlot  { GizmoUniformsBuffer.BufferHandle, 0ull, sizeof(GPUGizmoUniforms) };
        const VkDescriptorBufferInfo GizVertSlot { GizmoVertexBuffer.BufferHandle,   0ull, VK_WHOLE_SIZE };
        std::array<VkWriteDescriptorSet, 2u> Writes {};
        Writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, GizmoSet, 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &GizUniSlot,  nullptr };
        Writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, GizmoSet, 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &GizVertSlot, nullptr };
        vkUpdateDescriptorSets(DeviceHandle, 2u, Writes.data(), 0u, nullptr);
    }

    // 7. Final MRT RenderPass: 2x SSAA GuideOverlay + Planar2D TransformGizmo
    {
        std::array<VkClearValue, 3u> FinalClears {};
        FinalClears[0].color           = { { 0.062f, 0.066f, 0.078f, 1.0f } };
        FinalClears[1].color.uint32[0] = 0u;
        FinalClears[1].color.uint32[1] = 0u;
        FinalClears[1].color.uint32[2] = 0u;
        FinalClears[1].color.uint32[3] = 0u;
        FinalClears[2].depthStencil    = { 1.0f, 0u };

        VkRenderPassBeginInfo FinalRp {};
        FinalRp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        FinalRp.renderPass        = FinalMRTAttachmentFormat;
        FinalRp.framebuffer       = FinalTarget.Framebuffer;
        FinalRp.renderArea.extent = { WidthPx, HeightPx };
        FinalRp.clearValueCount   = 3u;
        FinalRp.pClearValues      = FinalClears.data();
        vkCmdBeginRenderPass(Cmd, &FinalRp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport FinalVp { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
        VkRect2D   FinalScissor { { 0, 0 }, { WidthPx, HeightPx } };
        vkCmdSetViewport(Cmd, 0u, 1u, &FinalVp);
        vkCmdSetScissor(Cmd, 0u, 1u, &FinalScissor);

        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GuideOverlayProgram.ProgramHandle);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GuideOverlayProgram.ProgramLayout, 0u, 1u, &OverlaySet, 0u, nullptr);
        vkCmdDraw(Cmd, 6u, 1u, 0u, 0u);

        if (GizmoVertCount > 0u && GizmoSet != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Gizmo2DProgram.ProgramHandle);
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Gizmo2DProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
            vkCmdDraw(Cmd, GizmoVertCount, 1u, 0u, 0u);
        }

        vkCmdEndRenderPass(Cmd);
        FinalTarget.PickLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // 8. Encode summary metrics + optional RGBA8 readback into timeline staging ring
    TransferRegister.AdvanceRingSlot();
    ReadbackTicket SummaryTicket = TransferRegister.EncodeBufferReadback(Cmd, SummaryMetricsBuffer.BufferHandle, 0ull, sizeof(GPUVectorSummaryMetrics));
    ReadbackTicket ColorTicket {};
    if (OutRGBA != nullptr)
    {
        ColorTicket = TransferRegister.EncodeImageRegionReadback(
            Cmd,
            FinalTarget.ColorImage.ImageHandle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0,
            0,
            WidthPx,
            HeightPx,
            4u);
    }

    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(SummaryTicket, Signal);
    if (OutRGBA != nullptr)
    {
        TransferRegister.BindTicketTimelineSignal(ColorTicket, Signal);
        OutRGBA->resize(static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
        if (!TransferRegister.AwaitReadbackBytes(ColorTicket, *OutRGBA))
        {
            vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
            return false;
        }
    }
    else if (!DeviceInterchange->AwaitTimelineTick(Signal))
    {
        vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
        return false;
    }

    GPUVectorSummaryMetrics MetricsRead {};
    if (TransferRegister.AwaitReadbackTyped<GPUVectorSummaryMetrics>(SummaryTicket, std::span<GPUVectorSummaryMetrics>(&MetricsRead, 1u)))
    {
        LastMetrics.TotalInstancesEvaluated = MetricsRead.InstanceCount;
        LastMetrics.MinNoiseScale           = (MetricsRead.MinScaleFixed <= MetricsRead.MaxScaleFixed)
            ? static_cast<float>(MetricsRead.MinScaleFixed) / 10000.0f
            : 1.0f;
        LastMetrics.MaxNoiseScale           = (MetricsRead.MaxScaleFixed >= 100u)
            ? static_cast<float>(MetricsRead.MaxScaleFixed) / 10000.0f
            : 1.0f;
    }

    vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  GPU VISIBILITY PICKING
//------------------------------------------------------------------------------------------------------------------------

ViewportPickHit VectorCanvasRasterizer::Pick2DPixel(int32_t CursorX, int32_t CursorY)
{
    if (DeviceInterchange == nullptr || FinalTarget.PickImage.ImageHandle == VK_NULL_HANDLE)
    {
        return {};
    }
    ViewportPickHit Hit = PickQuery.QuerySinglePixel(
        FinalTarget.PickImage.ImageHandle,
        FinalTarget.PickLayout,
        FinalTarget.WidthPx,
        FinalTarget.HeightPx,
        CursorX,
        CursorY);
    FinalTarget.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Hit;
}

RegionPickSummary VectorCanvasRasterizer::Reduce2DRegionPick(
    uint32_t MinX,
    uint32_t MinY,
    uint32_t MaxX,
    uint32_t MaxY)
{
    if (DeviceInterchange == nullptr || FinalTarget.PickImage.ImageHandle == VK_NULL_HANDLE)
    {
        return {};
    }
    RegionPickSummary Summary = PickQuery.ExecuteRegionReduction(
        FinalTarget.PickImage.ImageHandle,
        FinalTarget.PickLayout,
        FinalTarget.WidthPx,
        FinalTarget.HeightPx,
        MinX,
        MinY,
        MaxX,
        MaxY,
        0u,
        0u,
        0u,
        RegionPickMaskBuffer.BufferHandle,
        MaxInstancesPerLayer);
    FinalTarget.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Summary;
}

} // namespace Frontier::GeometricRaster
