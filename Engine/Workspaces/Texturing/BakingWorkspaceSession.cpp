/*============================================================================================================================================
                                                          BAKINGWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of BakingWorkspaceSession: real Vulkan GPU GTX BVH4 + RTX RayQuery baking, study import & PNG/.fvol export.

#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"
#include "Engine/GeometricRaster/Baking/GpuSdfBaker.h"
#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/Baking/TextureBakerRasterizer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace Frontier::GeometricRaster;
using namespace Frontier::GeometricRaster::Baking;

struct BakingGpuEngineStorage
{
    std::unique_ptr<VulkanInterchange> OwnedDevice {};
    VulkanInterchange*                 ActiveDevice = nullptr;
    std::filesystem::path              ShaderDirectory {};
    GpuTextureBaker                    Baker {};
    GpuSdfBaker                        SdfBaker {};
    TextureBakerRasterizer             Rasterizer {};
    PreparedStudyPair                  StudyPair {};
    bool                               StudyUploadedToGpu = false;
    bool                               HasCustomLow  = false;
    bool                               HasCustomHigh = false;
    uint32_t                           ActiveLowTris  = 0u;
    uint32_t                           ActiveHighTris = 0u;

    ~BakingGpuEngineStorage()
    {
        Rasterizer.TeardownRasterizer();
        SdfBaker.TeardownSdfBaker();
        Baker.TeardownBaker();
        if (OwnedDevice != nullptr)
        {
            OwnedDevice->TeardownInterchange();
        }
    }
};

namespace
{

constexpr std::array<BakerTargetDescriptor, BakingWorkspaceSession::TargetCount> BakerTargetEntries {{
    { BakerTargetId::VolumeSdf,          BakerTargetGroup::VolumeDistance, "Volume SDF",            "3D distance grid",     true  },
    { BakerTargetId::TangentNormal,      BakerTargetGroup::Normals,           "Tangent Normal",        "High-to-Low normal",   false },
    { BakerTargetId::WorldNormal,        BakerTargetGroup::Normals,           "World Normal",          "Scene XYZ vector",     false },
    { BakerTargetId::ModelSpaceNormal,   BakerTargetGroup::Normals,           "Object Normal",         "Local XYZ vector",     false },
    { BakerTargetId::BentNormal,         BakerTargetGroup::Normals,           "Bent Normal",           "Average unoccluded ray", false },
    { BakerTargetId::BevelNormal,        BakerTargetGroup::Normals,           "Bevel Normal",          "Rounded edge filter",  false },
    { BakerTargetId::AmbientOcclusion,   BakerTargetGroup::Surface,           "Ambient Occlusion",     "Hemisphere sky AO",    false },
    { BakerTargetId::SecondaryAo,        BakerTargetGroup::Surface,           "Secondary AO",          "Tight contact creases",false },
    { BakerTargetId::Curvature,          BakerTargetGroup::Surface,           "Curvature",             "Ridge & cavity signed",false },
    { BakerTargetId::Convexity,          BakerTargetGroup::Surface,           "Convexity",             "Positive ridges",      false },
    { BakerTargetId::Concavity,          BakerTargetGroup::Surface,           "Concavity",             "Recessed grooves",     false },
    { BakerTargetId::Thickness,          BakerTargetGroup::Surface,           "Thickness",             "Interior ray depth",   false },
    { BakerTargetId::HeightDisplacement, BakerTargetGroup::Surface,           "Height / Displacement", "Cage ray offset",      false },
    { BakerTargetId::TopDownDust,        BakerTargetGroup::DiagnosticMasks,       "Top-Down Dust",         "Directional overlay",  false },
    { BakerTargetId::UvIslandId,         BakerTargetGroup::DiagnosticMasks,       "UV Island ID",          "Chart color index",    false },
    { BakerTargetId::PartMaterialId,     BakerTargetGroup::DiagnosticMasks,       "Part / Material ID",    "Sub-surface ID color", false },
    { BakerTargetId::WorldPosition,      BakerTargetGroup::DiagnosticMasks,       "World Position",        "Normalized XYZ bbox",  false },
    { BakerTargetId::UvWireframe,        BakerTargetGroup::DiagnosticMasks,       "UV Wireframe",          "Edge triangle overlay",false },
}};

constexpr std::array<BakerStudyDescriptor, 5u> BakerStudyEntries {{
    { BakerStudyShape::UtahTeapot, "Utah Teapot (High/Low)", 2928u,  12544u },
    { BakerStudyShape::ShaderBall, "Shader Ball (High/Low)", 2520u,  12928u },
    { BakerStudyShape::ChamferBox, "Chamfer Box (High/Low)",  192u,   9408u },
    { BakerStudyShape::UvSphere,   "UV Sphere (High/Low)",   1280u,   6912u },
    { BakerStudyShape::TorusKnot,  "Torus Knot Study",       1728u,  12096u },
}};

[[nodiscard]] std::filesystem::path DiscoverRepoRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] StudyGeometryCategory ToStudyKind(BakerStudyShape Shape) noexcept
{
    switch (Shape)
    {
        case BakerStudyShape::UtahTeapot: return StudyGeometryCategory::UtahTeapot;
        case BakerStudyShape::ShaderBall: return StudyGeometryCategory::ShaderBall;
        case BakerStudyShape::ChamferBox: return StudyGeometryCategory::ChamferBox;
        case BakerStudyShape::UvSphere:   return StudyGeometryCategory::UvSphere;
        case BakerStudyShape::TorusKnot:  return StudyGeometryCategory::TorusKnot;
        default:                          return StudyGeometryCategory::UtahTeapot;
    }
}

[[nodiscard]] const char* ShortFileSanitizedTarget(BakerTargetId Target) noexcept
{
    switch (Target)
    {
        case BakerTargetId::VolumeSdf:          return "VolumeSdfSlice";
        case BakerTargetId::TangentNormal:      return "TangentNormal";
        case BakerTargetId::WorldNormal:        return "WorldNormal";
        case BakerTargetId::ModelSpaceNormal:   return "ObjectNormal";
        case BakerTargetId::BentNormal:         return "BentNormal";
        case BakerTargetId::BevelNormal:        return "BevelNormal";
        case BakerTargetId::AmbientOcclusion:   return "AmbientOcclusion";
        case BakerTargetId::SecondaryAo:        return "SecondaryAO";
        case BakerTargetId::Curvature:          return "Curvature";
        case BakerTargetId::Convexity:          return "Convexity";
        case BakerTargetId::Concavity:          return "Concavity";
        case BakerTargetId::Thickness:          return "Thickness";
        case BakerTargetId::HeightDisplacement: return "Height";
        case BakerTargetId::TopDownDust:        return "TopDownDust";
        case BakerTargetId::UvIslandId:         return "UvIslandId";
        case BakerTargetId::PartMaterialId:     return "PartMaterialId";
        case BakerTargetId::WorldPosition:      return "WorldPosition";
        case BakerTargetId::UvWireframe:        return "UvWireframe";
        default:                                return "Channel";
    }
}

} // namespace

BakingWorkspaceSession::BakingWorkspaceSession() noexcept
{
    ResetDefaults();
}

BakingWorkspaceSession::~BakingWorkspaceSession() noexcept = default;

void BakingWorkspaceSession::AttachExternalVulkanDevice(
    VulkanInterchange*           ExternalDevice,
    const std::filesystem::path& ShaderDirectory)
{
    if (!GpuStorage)
    {
        GpuStorage = std::make_unique<BakingGpuEngineStorage>();
    }
    GpuStorage->Rasterizer.TeardownRasterizer();
    GpuStorage->SdfBaker.TeardownSdfBaker();
    GpuStorage->Baker.TeardownBaker();
    GpuStorage->OwnedDevice.reset();
    GpuStorage->ActiveDevice      = ExternalDevice;
    GpuStorage->ShaderDirectory   = ShaderDirectory;
    GpuStorage->StudyUploadedToGpu = false;
}

bool BakingWorkspaceSession::EnsureGpuEngineReady()
{
    if (!GpuStorage)
    {
        GpuStorage = std::make_unique<BakingGpuEngineStorage>();
    }

    if (GpuStorage->ActiveDevice == nullptr)
    {
        GpuStorage->OwnedDevice = std::make_unique<VulkanInterchange>();
        std::string Diag;
        if (!GpuStorage->OwnedDevice->InitializeInterchange(DiscoverRepoRoot(), &Diag))
        {
            ProgressStatus      = "Vulkan init failed: " + Diag;
            DeviceIndicatorText = "GPU Unavailable";
            return false;
        }
        GpuStorage->ActiveDevice = GpuStorage->OwnedDevice.get();
    }

    if (!GpuStorage->Baker.Ready())
    {
        if (!GpuStorage->Baker.InitializeBaker(GpuStorage->ActiveDevice, GpuStorage->ShaderDirectory) ||
            !GpuStorage->SdfBaker.InitializeSdfBaker(GpuStorage->ActiveDevice, GpuStorage->ShaderDirectory))
        {
            ProgressStatus      = "Shader compile/load failed";
            DeviceIndicatorText = "Baker Init Error";
            return false;
        }
    }

    if (!GpuStorage->Rasterizer.Ready())
    {
        if (!GpuStorage->Rasterizer.InitializeRasterizer(
                GpuStorage->ActiveDevice,
                &GpuStorage->Baker,
                GpuStorage->ShaderDirectory))
        {
            ProgressStatus = "Preview rasterizer init failed";
            return false;
        }
    }

    if (!GpuStorage->StudyUploadedToGpu)
    {
        if (!GpuStorage->HasCustomLow && !GpuStorage->HasCustomHigh)
        {
            GpuStorage->StudyPair = StudyGeometryBuilder::ConstructStudioStudy(ToStudyKind(Study));
        }
        if (!GpuStorage->Baker.UploadStudyGeometry(GpuStorage->StudyPair))
        {
            ProgressStatus = "Study GPU upload failed";
            return false;
        }
        GpuStorage->ActiveLowTris      = GpuStorage->StudyPair.LowTriangleCount;
        GpuStorage->ActiveHighTris     = GpuStorage->StudyPair.HighTriangleCount;
        GpuStorage->StudyUploadedToGpu = true;

        BakeOrbitCamera Cam {};
        TextureBakerRasterizer::AlignCameraToStudy(GpuStorage->StudyPair, Cam);
        CameraYawRadians     = Cam.YawRadians;
        CameraPitchRadians   = Cam.PitchRadians;
        CameraDistanceMeters = Cam.DistanceMeters;
        CameraPivotX         = Cam.TargetPivot.X;
        CameraPivotY         = Cam.TargetPivot.Y;
        CameraPivotZ         = Cam.TargetPivot.Z;
    }

    const bool PreferCompute = (ComputeDevice == BakerComputeDevice::GtxComputeBvh4);
    const BakeAccelerationBackend ActiveBackend = GpuStorage->Baker.QueryActiveBackend(PreferCompute);
    const std::string_view Adapter = GpuStorage->ActiveDevice->QueryAdapterName();
    const std::string AdapterShort(Adapter.substr(0u, std::min<std::size_t>(Adapter.size(), 22u)));
    DeviceIndicatorText = (ActiveBackend == BakeAccelerationBackend::RtxHardwareRayQuery)
        ? ("RTX RayQuery · " + AdapterShort)
        : ("GTX BVH4 · " + AdapterShort);
    return true;
}

void BakingWorkspaceSession::ResetDefaults() noexcept
{
    Study                  = BakerStudyShape::UtahTeapot;
    ViewportLayout         = BakerViewportLayout::ThreeDOnly;
    ComputeDevice          = BakerComputeDevice::AutoGpu;
    TargetResolutionPx     = 512u;
    NormalConvention       = TangentNormalConvention::OpenGlPlusY;
    AoSampleCount          = 12;
    FrontCageOffsetMeters  = 0.14f;
    RearCageOffsetMeters   = 0.14f;
    AoMaxRayDistanceMeters = 0.45f;
    UvEdgePaddingPx        = 6;
    ShowHighWireOverlay    = false;
    ShowCageOverlay        = false;

    VolumeGridDim          = 36u;
    SignedInteriorDistance = true;
    VolumeBoundsPadding    = 0.16f;
    SectionAxis            = VolumeSectionAxis::AxisZ;
    SectionDepthNormalized = 0.50f;

    InspectedTarget        = BakerTargetId::TangentNormal;
    BakedReadyTargets.fill(false);
    BakingActive           = false;
    VolumeSdfBaked         = false;
    SurfaceBaked           = false;
    BridgedToPaint         = false;
    BakeProgress           = 0.0f;
    ProgressStatus         = "Ready to bake";
    DeviceIndicatorText    = "Vulkan GPU Ready";
    CustomLowAssetLabel.clear();
    CustomHighAssetLabel.clear();
    LastExportedPath.clear();
    PaintTransferSnapshot  = {};

    ResetOrbitCamera();
    ApplyQuickPreset(BakerQuickPreset::PbrPlusVolume);
}

void BakingWorkspaceSession::ResetOrbitCamera() noexcept
{
    if (GpuStorage && GpuStorage->StudyUploadedToGpu)
    {
        BakeOrbitCamera Cam {};
        TextureBakerRasterizer::AlignCameraToStudy(GpuStorage->StudyPair, Cam);
        CameraYawRadians     = Cam.YawRadians;
        CameraPitchRadians   = Cam.PitchRadians;
        CameraDistanceMeters = Cam.DistanceMeters;
        CameraPivotX         = Cam.TargetPivot.X;
        CameraPivotY         = Cam.TargetPivot.Y;
        CameraPivotZ         = Cam.TargetPivot.Z;
    }
    else
    {
        CameraYawRadians     = 0.72f;
        CameraPitchRadians   = 0.36f;
        CameraDistanceMeters = 2.25f;
        CameraPivotX         = 0.0f;
        CameraPivotY         = 0.0f;
        CameraPivotZ         = 0.45f;
    }
}

void BakingWorkspaceSession::OrbitCameraByDelta(float DeltaYawRad, float DeltaPitchRad) noexcept
{
    CameraYawRadians   += DeltaYawRad;
    CameraPitchRadians  = std::clamp(CameraPitchRadians + DeltaPitchRad, -1.42f, 1.42f);
}

void BakingWorkspaceSession::PanCameraByDelta(float RightMeters, float UpMeters) noexcept
{
    const float Cy = std::cos(CameraYawRadians);
    const float Sy = std::sin(CameraYawRadians);
    CameraPivotX += -Sy * RightMeters;
    CameraPivotY +=  Cy * RightMeters;
    CameraPivotZ +=  UpMeters;
}

void BakingWorkspaceSession::ZoomCameraByFactor(float ScaleMultiplier) noexcept
{
    CameraDistanceMeters = std::clamp(CameraDistanceMeters * ScaleMultiplier, 0.35f, 18.0f);
}

void BakingWorkspaceSession::ApplyQuickPreset(BakerQuickPreset Preset) noexcept
{
    SelectedTargets.fill(false);
    if (Preset == BakerQuickPreset::PbrStandard)
    {
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::TangentNormal)]      = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::AmbientOcclusion)]   = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::Curvature)]          = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::HeightDisplacement)] = true;
    }
    else if (Preset == BakerQuickPreset::PbrPlusVolume)
    {
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::VolumeSdf)]          = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::TangentNormal)]      = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::AmbientOcclusion)]   = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::Curvature)]          = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::HeightDisplacement)] = true;
    }
    else if (Preset == BakerQuickPreset::Normals)
    {
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::TangentNormal)] = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::WorldNormal)]   = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::BentNormal)]    = true;
        SelectedTargets[static_cast<std::size_t>(BakerTargetId::BevelNormal)]   = true;
    }
    else
    {
        SelectedTargets.fill(true);
    }
}

void BakingWorkspaceSession::ToggleTargetSelected(BakerTargetId Target) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Target);
    if (Index < SelectedTargets.size())
    {
        SelectedTargets[Index] = !SelectedTargets[Index];
    }
}

void BakingWorkspaceSession::InspectTarget(BakerTargetId Target) noexcept
{
    if (static_cast<std::size_t>(Target) < TargetCount)
    {
        InspectedTarget = Target;
    }
}

void BakingWorkspaceSession::SelectStudy(BakerStudyShape NextStudy) noexcept
{
    if (static_cast<std::size_t>(NextStudy) < BakerStudyEntries.size())
    {
        Study          = NextStudy;
        SurfaceBaked   = false;
        VolumeSdfBaked = false;
        BridgedToPaint = false;
        BakedReadyTargets.fill(false);
        BakeProgress   = 0.0f;
        ProgressStatus = "Ready to bake";
        CustomLowAssetLabel.clear();
        CustomHighAssetLabel.clear();
        if (GpuStorage)
        {
            GpuStorage->HasCustomLow       = false;
            GpuStorage->HasCustomHigh      = false;
            GpuStorage->StudyUploadedToGpu = false;
        }
    }
}

void BakingWorkspaceSession::AssignResolution(uint32_t NextResPx) noexcept
{
    if (NextResPx == 256u || NextResPx == 512u || NextResPx == 1024u || NextResPx == 2048u)
    {
        TargetResolutionPx = NextResPx;
    }
}

void BakingWorkspaceSession::AssignVolumeGridDim(uint32_t NextDim) noexcept
{
    if (NextDim == 24u || NextDim == 36u || NextDim == 48u || NextDim == 64u)
    {
        VolumeGridDim = NextDim;
    }
}

void BakingWorkspaceSession::ExecuteBakeSimulation() noexcept
{
    std::size_t AnySelected = 0u;
    bool        AnySurface  = false;
    bool        WantVolume  = false;
    uint32_t    ActiveMask  = 0u;
    for (std::size_t I = 0u; I < TargetCount; ++I)
    {
        if (!SelectedTargets[I])
        {
            continue;
        }
        ++AnySelected;
        ActiveMask |= (1u << I);
        if (I == static_cast<std::size_t>(BakerTargetId::VolumeSdf))
        {
            WantVolume = true;
        }
        else
        {
            AnySurface = true;
        }
    }
    if (AnySelected == 0u)
    {
        ProgressStatus = "Select at least one bake target";
        BakeProgress   = 0.0f;
        return;
    }

    if (!EnsureGpuEngineReady())
    {
        BakeProgress = 0.0f;
        return;
    }

    BakingActive = true;
    const bool PreferCompute = (ComputeDevice == BakerComputeDevice::GtxComputeBvh4);
    double TotalMs           = 0.0;
    BakeAccelerationBackend BackendUsed = GpuStorage->Baker.QueryActiveBackend(PreferCompute);

    if (AnySurface)
    {
        SurfaceBakeExecutionSpec SurfSpec {};
        SurfSpec.ResolutionPx           = TargetResolutionPx;
        SurfSpec.AoSampleCount          = static_cast<uint32_t>(std::clamp(AoSampleCount, 4, 64));
        SurfSpec.FrontCageMeters        = FrontCageOffsetMeters;
        SurfSpec.RearCageMeters         = RearCageOffsetMeters;
        SurfSpec.AoMaxRayDistanceMeters = AoMaxRayDistanceMeters;
        SurfSpec.TangentSignY           = (NormalConvention == TangentNormalConvention::DirectXMinusY) ? -1.0f : 1.0f;
        SurfSpec.EdgePaddingPx          = static_cast<uint32_t>(std::clamp(UvEdgePaddingPx, 0, 24));
        SurfSpec.ActiveTargetBitmask    = ActiveMask;
        SurfSpec.PreferComputeBvh4      = PreferCompute;

        const BakeCompletionSummary SurfRes = GpuStorage->Baker.ExecuteSurfaceBake(SurfSpec);
        if (!SurfRes.SurfaceSucceeded)
        {
            BakingActive   = false;
            BakeProgress   = 0.0f;
            ProgressStatus = SurfRes.DiagnosticText;
            return;
        }
        SurfaceBaked = true;
        TotalMs     += SurfRes.SurfaceElapsedMs;
        BackendUsed  = SurfRes.ExecutedBackend;
        for (std::size_t I = 1u; I < TargetCount; ++I)
        {
            if (SelectedTargets[I])
            {
                BakedReadyTargets[I] = true;
            }
        }
    }

    if (WantVolume)
    {
        VolumeBakeExecutionSpec VolSpec {};
        VolSpec.GridDim                = VolumeGridDim;
        VolSpec.SignedInteriorDistance = SignedInteriorDistance;
        VolSpec.BoundsPaddingFraction  = VolumeBoundsPadding;
        VolSpec.SectionAxis            = static_cast<uint32_t>(SectionAxis);
        VolSpec.SectionDepthNormalized = SectionDepthNormalized;
        VolSpec.SectionSizePx          = 512u;
        VolSpec.PreferComputeBvh4      = PreferCompute;

        const BakeCompletionSummary VolRes = GpuStorage->SdfBaker.ExecuteVolumeSdfBake(GpuStorage->Baker.InspectAccel(), GpuStorage->Baker.InspectActiveStudy(), VolSpec);
        if (!VolRes.VolumeSucceeded)
        {
            BakingActive   = false;
            BakeProgress   = 0.0f;
            ProgressStatus = VolRes.DiagnosticText;
            return;
        }
        VolumeSdfBaked = true;
        BakedReadyTargets[static_cast<std::size_t>(BakerTargetId::VolumeSdf)] = true;
        TotalMs    += VolRes.VolumeElapsedMs;
        BackendUsed = VolRes.ExecutedBackend;
    }

    if (!SelectedTargets[static_cast<std::size_t>(InspectedTarget)])
    {
        for (std::size_t I = 0u; I < TargetCount; ++I)
        {
            if (SelectedTargets[I])
            {
                InspectedTarget = static_cast<BakerTargetId>(I);
                break;
            }
        }
    }

    BakingActive = false;
    BakeProgress = 1.0f;
    char Buf[112];
    const char* DeviceTag = (BackendUsed == BakeAccelerationBackend::RtxHardwareRayQuery) ? "RTX RayQuery" : "GTX BVH4";
    std::snprintf(Buf, sizeof(Buf), "Done · %s · %zu maps · %.0f ms", DeviceTag, AnySelected, TotalMs);
    ProgressStatus = Buf;
    char BadgeBuf[80];
    std::snprintf(BadgeBuf, sizeof(BadgeBuf), "%s · %.0f ms", DeviceTag, TotalMs);
    DeviceIndicatorText = BadgeBuf;
}

void BakingWorkspaceSession::RefreshSdfSectionOnGpu() noexcept
{
    if (!VolumeSdfBaked || !GpuStorage || !GpuStorage->Baker.Ready())
    {
        return;
    }
    (void)GpuStorage->SdfBaker.UpdateSdfSectionPreview(
        static_cast<uint32_t>(SectionAxis),
        SectionDepthNormalized,
        SignedInteriorDistance,
        512u);
}

void BakingWorkspaceSession::SendBakedTargetsToPaint() noexcept
{
    if (!SurfaceBaked || !GpuStorage || !GpuStorage->Baker.Ready())
    {
        return;
    }
    PaintTransferSnapshot.ResolutionPx       = GpuStorage->Baker.QuerySurfaceResolutionPx();
    PaintTransferSnapshot.TangentNormalRgba8 = GpuStorage->Baker.InspectChannelRgba8(0u);
    PaintTransferSnapshot.AmbientOccRgba8    = GpuStorage->Baker.InspectChannelRgba8(5u);
    PaintTransferSnapshot.CurvatureRgba8     = GpuStorage->Baker.InspectChannelRgba8(7u);
    PaintTransferSnapshot.HeightRgba8        = GpuStorage->Baker.InspectChannelRgba8(11u);
    PaintTransferSnapshot.Ready              = !PaintTransferSnapshot.TangentNormalRgba8.empty();

    BridgedToPaint = true;
    ProgressStatus = "Sent Baked AO / Curvature / Normals to Texture Paint";
}

bool BakingWorkspaceSession::RenderSpatialViewportRgba8(
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    std::vector<uint8_t>& OutRgbaPixels)
{
    if (!EnsureGpuEngineReady())
    {
        return false;
    }
    BakeViewportRenderSpec ViewSpec {};
    ViewSpec.WidthPx               = WidthPx;
    ViewSpec.HeightPx              = HeightPx;
    ViewSpec.InspectedTargetId     = static_cast<uint32_t>(InspectedTarget);
    ViewSpec.SurfaceBaked          = SurfaceBaked;
    ViewSpec.ShowHighWireOverlay   = ShowHighWireOverlay;
    ViewSpec.ShowCageOverlay       = ShowCageOverlay;
    ViewSpec.FrontCageMeters       = FrontCageOffsetMeters;
    ViewSpec.Camera.TargetPivot    = { CameraPivotX, CameraPivotY, CameraPivotZ };
    ViewSpec.Camera.YawRadians     = CameraYawRadians;
    ViewSpec.Camera.PitchRadians   = CameraPitchRadians;
    ViewSpec.Camera.DistanceMeters = CameraDistanceMeters;
    ViewSpec.Camera.VerticalFovDeg = 42.0f;
    ViewSpec.Camera.Basis          = Mathematics::WorldCoordinateBasis::RightHandedZUp;

    if (!GpuStorage->Rasterizer.RenderSpatialViewport(ViewSpec))
    {
        return false;
    }
    OutRgbaPixels = GpuStorage->Rasterizer.InspectSpatialRgba8();
    return !OutRgbaPixels.empty();
}

bool BakingWorkspaceSession::QueryInspectedPlanarRgba8(
    const uint8_t*& OutPixels,
    uint32_t&       OutWidthPx,
    uint32_t&       OutHeightPx) const noexcept
{
    OutPixels   = nullptr;
    OutWidthPx  = 0u;
    OutHeightPx = 0u;
    if (!GpuStorage || !GpuStorage->Baker.Ready())
    {
        return false;
    }
    if (InspectedTarget == BakerTargetId::VolumeSdf)
    {
        const auto& Sec = GpuStorage->SdfBaker.InspectSdfSectionRgba8();
        const uint32_t DimPx = GpuStorage->SdfBaker.QuerySdfSectionSizePx();
        if (DimPx == 0u || Sec.size() < static_cast<std::size_t>(DimPx) * DimPx * 4u)
        {
            return false;
        }
        OutPixels   = Sec.data();
        OutWidthPx  = DimPx;
        OutHeightPx = DimPx;
        return true;
    }

    const uint32_t ChannelSlot = static_cast<uint32_t>(InspectedTarget) - 1u;
    const uint32_t ResPx       = GpuStorage->Baker.QuerySurfaceResolutionPx();
    const auto&    Pixels      = GpuStorage->Baker.InspectChannelRgba8(ChannelSlot);
    if (ResPx == 0u || Pixels.size() < static_cast<std::size_t>(ResPx) * ResPx * 4u)
    {
        return false;
    }
    OutPixels   = Pixels.data();
    OutWidthPx  = ResPx;
    OutHeightPx = ResPx;
    return true;
}

bool BakingWorkspaceSession::LoadCustomStudyAsset(
    const std::filesystem::path& AssetFilePath,
    bool                         ImportAsHighPoly,
    std::string*                 OutDiagnostic)
{
    if (!GpuStorage)
    {
        GpuStorage = std::make_unique<BakingGpuEngineStorage>();
    }
    if (!GpuStorage->HasCustomLow && !GpuStorage->HasCustomHigh && GpuStorage->StudyPair.LowTriangleCount == 0u)
    {
        GpuStorage->StudyPair = StudyGeometryBuilder::ConstructStudioStudy(ToStudyKind(Study));
    }

    GeometrySpace::PolygonNetwork Imported {};
    const bool NormalizePedestal = (!GpuStorage->HasCustomLow && !GpuStorage->HasCustomHigh);
    if (!StudyGeometryBuilder::ImportPolygonNetworkFromFile(AssetFilePath, Imported, NormalizePedestal, OutDiagnostic))
    {
        return false;
    }

    GeometrySpace::PolygonNetwork LowNet  = GpuStorage->StudyPair.LowNetwork;
    GeometrySpace::PolygonNetwork HighNet = GpuStorage->StudyPair.HighNetwork;
    if (ImportAsHighPoly)
    {
        HighNet              = std::move(Imported);
        CustomHighAssetLabel = AssetFilePath.filename().string();
        GpuStorage->HasCustomHigh = true;
    }
    else
    {
        LowNet              = std::move(Imported);
        CustomLowAssetLabel = AssetFilePath.filename().string();
        GpuStorage->HasCustomLow = true;
        if (!GpuStorage->HasCustomHigh)
        {
            HighNet = LowNet;
        }
    }

    const std::string CombinedLabel = !CustomLowAssetLabel.empty()
        ? (CustomLowAssetLabel + " Study")
        : (!CustomHighAssetLabel.empty() ? (CustomHighAssetLabel + " Study") : QueryActiveStudyDescriptor().Label);

    GpuStorage->StudyPair          = StudyGeometryBuilder::PrepareFromPolygonNetworks(std::move(LowNet), std::move(HighNet), CombinedLabel);
    GpuStorage->ActiveLowTris      = GpuStorage->StudyPair.LowTriangleCount;
    GpuStorage->ActiveHighTris     = GpuStorage->StudyPair.HighTriangleCount;
    GpuStorage->StudyUploadedToGpu = false;

    SurfaceBaked   = false;
    VolumeSdfBaked = false;
    BridgedToPaint = false;
    BakedReadyTargets.fill(false);
    BakeProgress   = 0.0f;
    ProgressStatus = "Imported " + AssetFilePath.filename().string();
    return EnsureGpuEngineReady();
}

bool BakingWorkspaceSession::ExportInspectedTargetPng(const std::filesystem::path& ExplicitOutputPath)
{
    if (!GpuStorage || !GpuStorage->Baker.Ready())
    {
        ProgressStatus = "Bake before exporting PNG";
        return false;
    }

    std::filesystem::path OutPath = ExplicitOutputPath;
    if (OutPath.empty())
    {
        const std::filesystem::path ExportDir = DiscoverRepoRoot() / "EngineExports/Baking";
        std::string FileName = std::string("Bake_") + ShortFileSanitizedTarget(InspectedTarget) + ".png";
        OutPath = ExportDir / FileName;
    }

    bool Ok = false;
    if (InspectedTarget == BakerTargetId::VolumeSdf)
    {
        Ok = GpuStorage->SdfBaker.ExportSdfSectionPng(OutPath);
    }
    else
    {
        Ok = GpuStorage->Baker.ExportSurfaceChannelPng(static_cast<uint32_t>(InspectedTarget) - 1u, OutPath);
    }
    if (Ok)
    {
        LastExportedPath = OutPath.string();
        ProgressStatus   = "Exported " + OutPath.filename().string();
    }
    else
    {
        ProgressStatus = "Bake target before exporting PNG";
    }
    return Ok;
}

bool BakingWorkspaceSession::ExportVolumeSdfFvol(const std::filesystem::path& ExplicitOutputPath)
{
    if (!VolumeSdfBaked || !GpuStorage || !GpuStorage->Baker.Ready())
    {
        ProgressStatus = "Bake Volume SDF first";
        return false;
    }

    std::filesystem::path OutPath = ExplicitOutputPath;
    if (OutPath.empty())
    {
        OutPath = DiscoverRepoRoot() / "EngineExports/Baking/BakedVolumeSdf.fvol";
    }
    if (GpuStorage->SdfBaker.ExportVolumeFvol(OutPath))
    {
        LastExportedPath = OutPath.string();
        ProgressStatus   = "Exported " + OutPath.filename().string();
        return true;
    }
    ProgressStatus = "Failed writing .fvol file";
    return false;
}

uint32_t BakingWorkspaceSession::QueryActiveLowTriangles() const noexcept
{
    if (GpuStorage && GpuStorage->ActiveLowTris > 0u)
    {
        return GpuStorage->ActiveLowTris;
    }
    return QueryActiveStudyDescriptor().LowTris;
}

uint32_t BakingWorkspaceSession::QueryActiveHighTriangles() const noexcept
{
    if (GpuStorage && GpuStorage->ActiveHighTris > 0u)
    {
        return GpuStorage->ActiveHighTris;
    }
    return QueryActiveStudyDescriptor().HighTris;
}

std::size_t BakingWorkspaceSession::CountSelectedTargets() const noexcept
{
    std::size_t Total = 0u;
    for (bool Selected : SelectedTargets)
    {
        if (Selected)
        {
            ++Total;
        }
    }
    return Total;
}

bool BakingWorkspaceSession::QueryPresetMatched(BakerQuickPreset Preset) const noexcept
{
    BakingWorkspaceSession InspectSession;
    InspectSession.ApplyQuickPreset(Preset);
    return InspectSession.SelectedTargets == SelectedTargets;
}

bool BakingWorkspaceSession::QueryTargetReady(BakerTargetId Target) const noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Target);
    return (Index < BakedReadyTargets.size()) && BakedReadyTargets[Index];
}

const BakerStudyDescriptor& BakingWorkspaceSession::QueryActiveStudyDescriptor() const noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Study);
    return BakerStudyEntries[Index < BakerStudyEntries.size() ? Index : 0u];
}

const std::array<BakerTargetDescriptor, BakingWorkspaceSession::TargetCount>& BakingWorkspaceSession::QueryTargetRegistry() noexcept
{
    return BakerTargetEntries;
}

const BakerTargetDescriptor& BakingWorkspaceSession::QueryTargetDescriptor(BakerTargetId Target) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Target);
    return BakerTargetEntries[Index < BakerTargetEntries.size() ? Index : 0u];
}

const std::array<BakerStudyDescriptor, 5u>& BakingWorkspaceSession::QueryStudyRegistry() noexcept
{
    return BakerStudyEntries;
}

const char* BakingWorkspaceSession::QueryGroupHeading(BakerTargetGroup Group) noexcept
{
    switch (Group)
    {
        case BakerTargetGroup::VolumeDistance: return "3D Volume & Distance Field";
        case BakerTargetGroup::Normals:           return "Normals & Vectors";
        case BakerTargetGroup::Surface:           return "Surface, Occlusion & Curvature";
        case BakerTargetGroup::DiagnosticMasks:       return "Masks, Height & UV Layout";
        default:                                  return "Bake Targets";
    }
}

} // namespace Frontier::Workspaces
