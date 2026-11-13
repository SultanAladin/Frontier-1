/*============================================================================================================================================
                                                          UVWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceSession coordinating O(1) GPU selection, Interchange, parallel/GPU UV solvers, .UVSpace & 215 MiB revisions

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace Frontier::Workspaces
{

namespace
{

constexpr uint32_t AuthoringSnapshotMagic   = 0x5556534Eu; // 'UVSN'
constexpr uint32_t AuthoringSnapshotVersion = 1u;

struct UVTriangle2D
{
    uint32_t FaceIndex = 0u;
    float    U0 = 0.0f, V0 = 0.0f;
    float    U1 = 0.0f, V1 = 0.0f;
    float    U2 = 0.0f, V2 = 0.0f;
    float    MinU = 0.0f, MaxU = 0.0f;
    float    MinV = 0.0f, MaxV = 0.0f;
};

[[nodiscard]] bool AxisSeparatesTriangles(
    const UVTriangle2D& A,
    const UVTriangle2D& B,
    float               AxisU,
    float               AxisV,
    float               Epsilon) noexcept
{
    const float LenSq = AxisU * AxisU + AxisV * AxisV;
    if (LenSq <= 1.0e-12f)
    {
        return false;
    }
    const float InvLen = 1.0f / std::sqrt(LenSq);
    const float NU = AxisU * InvLen;
    const float NV = AxisV * InvLen;

    const float PA0 = A.U0 * NU + A.V0 * NV;
    const float PA1 = A.U1 * NU + A.V1 * NV;
    const float PA2 = A.U2 * NU + A.V2 * NV;
    const float PB0 = B.U0 * NU + B.V0 * NV;
    const float PB1 = B.U1 * NU + B.V1 * NV;
    const float PB2 = B.U2 * NU + B.V2 * NV;

    const float MinA = std::min({ PA0, PA1, PA2 });
    const float MaxA = std::max({ PA0, PA1, PA2 });
    const float MinB = std::min({ PB0, PB1, PB2 });
    const float MaxB = std::max({ PB0, PB1, PB2 });
    return (MaxA <= MinB + Epsilon) || (MaxB <= MinA + Epsilon);
}

[[nodiscard]] bool TrianglesOverlap2DSAT(
    const UVTriangle2D& A,
    const UVTriangle2D& B,
    float               Epsilon = 2.5e-4f) noexcept
{
    if (AxisSeparatesTriangles(A, B, -(A.V1 - A.V0), (A.U1 - A.U0), Epsilon) ||
        AxisSeparatesTriangles(A, B, -(A.V2 - A.V1), (A.U2 - A.U1), Epsilon) ||
        AxisSeparatesTriangles(A, B, -(A.V0 - A.V2), (A.U0 - A.U2), Epsilon) ||
        AxisSeparatesTriangles(A, B, -(B.V1 - B.V0), (B.U1 - B.U0), Epsilon) ||
        AxisSeparatesTriangles(A, B, -(B.V2 - B.V1), (B.U2 - B.U1), Epsilon) ||
        AxisSeparatesTriangles(A, B, -(B.V0 - B.V2), (B.U0 - B.U2), Epsilon))
    {
        return false;
    }
    return true;
}

template <typename T>
void AppendPod(std::vector<std::byte>& Buffer, const T& Payload)
{
    const std::size_t Offset = Buffer.size();
    Buffer.resize(Offset + sizeof(T));
    std::memcpy(Buffer.data() + Offset, &Payload, sizeof(T));
}

void AppendString(std::vector<std::byte>& Buffer, std::string_view Text)
{
    const uint32_t Len = static_cast<uint32_t>(Text.size());
    AppendPod(Buffer, Len);
    if (Len > 0u)
    {
        const std::size_t Offset = Buffer.size();
        Buffer.resize(Offset + Len);
        std::memcpy(Buffer.data() + Offset, Text.data(), Len);
    }
}

template <typename T>
[[nodiscard]] bool ReadPod(std::span<const std::byte> Data, std::size_t& Cursor, T& OutValue) noexcept
{
    if (Cursor + sizeof(T) > Data.size())
    {
        return false;
    }
    std::memcpy(&OutValue, Data.data() + Cursor, sizeof(T));
    Cursor += sizeof(T);
    return true;
}

[[nodiscard]] bool ReadString(std::span<const std::byte> Data, std::size_t& Cursor, std::string& OutText)
{
    uint32_t Len = 0u;
    if (!ReadPod(Data, Cursor, Len) || Cursor + Len > Data.size())
    {
        return false;
    }
    OutText.assign(reinterpret_cast<const char*>(Data.data() + Cursor), Len);
    Cursor += Len;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

UVWorkspaceSession::UVWorkspaceSession()
    : ActiveDocument(
          WorkspaceToken{ 0u, 1u, Codecs::SpaceExtensionCategory::UVSpace },
          "UVWorkspaceSession",
          std::filesystem::path("UVWorkspaceSession.UVSpace")),
      RevisionHistory(ActiveDocument.AccessRevisionTracker())
{
    WorkspaceConfig.AccessUVPreferences().InitialViewportLayout = Configuration::UVViewportLayoutMode::Split2DAnd3D;
    WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled    = false;

    auto& Projection = ActiveDocument.AccessViewportProjection();
    Projection.ConfigureSplitMode(Editor::ViewportSplitMode::SplitPlanarAndSpatial);
    Projection.ConfigureSplitRatio(WorkspaceConfig.InspectUVPreferences().SplitRatioNormalized);

    Gizmo3D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    Gizmo3D.ConfigureAnchor({ 0.0f, 0.0f, 0.54f }, 1.12f);

    Gizmo2D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Planar2D);
    Gizmo2D.ConfigureAnchor({ 0.50f, 0.50f, 0.0f }, 0.34f);

    LoadSamplePreset(GeometrySpace::SampleAssetPreset::ControlledIntegrator);
}

bool UVWorkspaceSession::AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer)
{
    Rasterizer = BoundRasterizer;
    if (Rasterizer == nullptr)
    {
        return false;
    }
    Rasterizer->ConfigureLayerSwatches(Asset.Layers.InspectLayers());
    const bool Uploaded = Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    if (Uploaded)
    {
        SynchronizeSelectionToGPU();
        LatestMetrics = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
        LatestMetrics.OverlapFaceCount = static_cast<uint32_t>(CollectOverlappingUVFaces().size());
        FramebuffersDirty = true;
    }
    return Uploaded;
}

bool UVWorkspaceSession::InitializeGPUSolvers(
    GeometricRaster::VulkanInterchange* DeviceInterchange,
    const std::filesystem::path&          CompiledShaderDir)
{
    const bool RelaxReady   = StretchRelaxer.InitializeGPUCompute(DeviceInterchange, CompiledShaderDir);
    const bool SkylineReady = SkylinePacker.InitializeGPUCompute(DeviceInterchange, CompiledShaderDir);
    (void)OpticsEncoder.InitializeOpticsEncoder(DeviceInterchange, CompiledShaderDir);
    return RelaxReady && SkylineReady;
}

void UVWorkspaceSession::TeardownGPUSolvers() noexcept
{
    OpticsEncoder.TeardownOpticsEncoder();
    StretchRelaxer.TeardownGPUCompute();
    SkylinePacker.TeardownGPUCompute();
}

void UVWorkspaceSession::InitializeLoadedAssetLayout()
{
    Instances         = GeometrySpace::GeometryInstanceRegistry{};
    RootInstanceToken = Instances.IssueInstance(Asset.Network.NetworkTitle, 0u);
    (void)Instances.IssueInstance("Primary_Subpart", 0u, { { 0.0f, 0.0f, 0.45f }, {}, { 1.0f, 1.0f, 1.0f } });

    const auto LayerSpan = Asset.Layers.InspectLayers();
    SelectedLayerOrdinal = LayerSpan.empty() ? 0u : LayerSpan.front().LayerOrdinal;
    ActiveUDIMNumber     = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();

    UDIMSettings.clear();
    for (const uint32_t Code : Asset.UDIMTiles)
    {
        UDIMSettings[Code] = UDIMTileSetting{ WorkspaceConfig.InspectUVPreferences().DefaultTileResolutionPx, GeometricRaster::UVBackdropPreset::None };
    }

    SelectionMask.SynchronizeCapacity(Asset.Network);
    SelectionMask.ClearAll();
    SpatialSelectionActive = true;
    if (ActivePreset != GeometrySpace::SampleAssetPreset::ControlledIntegrator)
    {
        const auto& InitialSpan = Asset.Layers.InspectPartitionSpan(Asset.Network, SelectedLayerOrdinal, RootInstanceToken);
        SelectionMask.SelectFacesFromList(InitialSpan.FaceIndices, false);
    }

    RevisionHistory.Reset();
    BaselineUVPairs       = Asset.Network.ExtractParameterizedUVStream();
    BaselineSnapshotBytes = CaptureAuthoringSnapshot();
    RevisionStampCounter  = 1ull;

    Mathematics::Axis3D BoundsMin {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D BoundsMax { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& Vert : Asset.Network.Vertices)
    {
        BoundsMin.X = std::min(BoundsMin.X, Vert.Axes.X);
        BoundsMin.Y = std::min(BoundsMin.Y, Vert.Axes.Y);
        BoundsMin.Z = std::min(BoundsMin.Z, Vert.Axes.Z);
        BoundsMax.X = std::max(BoundsMax.X, Vert.Axes.X);
        BoundsMax.Y = std::max(BoundsMax.Y, Vert.Axes.Y);
        BoundsMax.Z = std::max(BoundsMax.Z, Vert.Axes.Z);
    }

    auto&                    Projection = ActiveDocument.AccessViewportProjection();
    Editor::SpatialCameraRig Rig        = Projection.InspectSpatialCamera();
    if (!Asset.Network.Vertices.empty() && BoundsMin.X <= BoundsMax.X)
    {
        const Mathematics::Axis3D Center {
            0.5f * (BoundsMin.X + BoundsMax.X),
            0.5f * (BoundsMin.Y + BoundsMax.Y),
            0.5f * (BoundsMin.Z + BoundsMax.Z),
        };
        const Mathematics::Axis3D HalfExt {
            0.5f * (BoundsMax.X - BoundsMin.X),
            0.5f * (BoundsMax.Y - BoundsMin.Y),
            0.5f * (BoundsMax.Z - BoundsMin.Z),
        };
        const float Radius = std::max(0.5f, Mathematics::EvaluateLength(HalfExt));
        Rig.TargetPivot    = Center;
        Rig.DistanceMeters = std::clamp(Radius * 2.85f, 3.60f, 16.0f);
        Rig.YawRadians     = 0.88f;
        Rig.PitchRadians   = 0.30f;
    }
    else
    {
        Rig.DistanceMeters = 6.20f;
        Rig.TargetPivot    = { 0.0f, 0.0f, 0.42f };
    }
    Projection.ConfigureSpatialCamera(Rig);
    SpatialCameraComponent.AlignFromSpatialRig(Rig, true);
    SmoothedCameraActive = false;

    const float MaxTileU = static_cast<float>(std::max<std::size_t>(1u, Asset.UDIMTiles.size()));
    Projection.ConfigurePlanarDomainSpan(MaxTileU, 1.0f);

    RenderOptions3D.Camera.DistanceMeters = Rig.DistanceMeters;
    RenderOptions3D.Camera.TargetPivot    = Rig.TargetPivot;
    RenderOptions3D.Camera.YawRadians     = Rig.YawRadians;
    RenderOptions3D.Camera.PitchRadians   = Rig.PitchRadians;
    RenderOptions3D.ActiveLayerOrdinal    = SelectedLayerOrdinal;
    RenderOptions3D.SeamOverlayEnabled    = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    RenderOptions3D.WireframeEnabled      = WorkspaceConfig.InspectUVPreferences().WireframeOverlayEnabled;

    RenderOptions2D.ActiveLayerOrdinal = SelectedLayerOrdinal;
    RenderOptions2D.CheckerEnabled     = WorkspaceConfig.InspectUVPreferences().CheckerOverlayEnabled;
    RenderOptions2D.SeamOverlayEnabled = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    Projection.EvaluateAspectLockedPlanarBounds(640u, 640u, RenderOptions2D.VisibleMinUV, RenderOptions2D.VisibleMaxUV);

    RefreshGizmoAnchorsFromSelection();
    SyncNetworkToGPU();

    char Msg[160]{};
    std::snprintf(
        Msg,
        sizeof(Msg),
        "Loaded %s  |  %u Faces across %zu UDIM Tiles",
        Asset.Network.NetworkTitle.c_str(),
        static_cast<uint32_t>(Asset.Network.Faces.size()),
        Asset.UDIMTiles.size());
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
}

void UVWorkspaceSession::LoadSamplePreset(GeometrySpace::SampleAssetPreset Preset)
{
    ActivePreset = Preset;
    Asset        = GeometrySpace::ConstructSamplePolygonNetwork(Preset);
    InitializeLoadedAssetLayout();
}

bool UVWorkspaceSession::ImportExternalGeometry(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic)
{
    GeometrySpace::ConstructedSampleAsset Imported {};
    if (!Interchange::GeometryInterchange::ImportAssetFromFile(SourcePath, Imported, OutDiagnostic))
    {
        return false;
    }
    Asset = std::move(Imported);
    InitializeLoadedAssetLayout();
    return true;
}

bool UVWorkspaceSession::ExportExternalGeometry(
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic) const
{
    return Interchange::GeometryInterchange::ExportAssetToFile(TargetPath, Asset.Network, &Asset.Layers, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             O(1) LAYER, UDIM & MATCAP OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void UVWorkspaceSession::SelectPriorityLayer(uint16_t LayerOrdinal)
{
    SpatialSelectionActive             = true;
    SelectedLayerOrdinal               = LayerOrdinal;
    RenderOptions3D.ActiveLayerOrdinal = LayerOrdinal;
    RenderOptions2D.ActiveLayerOrdinal = LayerOrdinal;

    const auto& Ref = Asset.Layers.InspectPartitionSpan(Asset.Network, LayerOrdinal, RootInstanceToken);
    SelectionMask.SynchronizeCapacity(Asset.Network);
    SelectionMask.SelectFacesFromList(Ref.FaceIndices, false);
    RefreshGizmoAnchorsFromSelection();
    SynchronizeSelectionToGPU();

    char Msg[160]{};
    std::snprintf(
        Msg,
        sizeof(Msg),
        "Selected Layer %u  |  %zu Faces  |  3D Area %.2f m^2",
        static_cast<unsigned>(LayerOrdinal),
        Ref.FaceIndices.size(),
        Ref.TotalArea3D);
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
}

void UVWorkspaceSession::BindSelectionToPriorityLayer(uint16_t LayerOrdinal)
{
    const auto SelectedFaces = SelectionMask.CollectSelectedFaces();
    if (SelectedFaces.empty())
    {
        return;
    }
    Asset.Layers.AssignFacesToLayer(Asset.Network, SelectedFaces, LayerOrdinal);
    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    RecordUVRevisionStep("BindSelectionToLayer");
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    FramebuffersDirty = true;
}

uint16_t UVWorkspaceSession::CreatePriorityLayer(
    std::string_view      Title,
    Mathematics::Scalar3D LinearTint,
    float                 InitialSharePercent)
{
    const float    Available = Asset.Layers.DeriveRemainderSharePercent();
    const float    Clamped   = std::min(InitialSharePercent, Available);
    const uint16_t NewOrd    = Asset.Layers.ConstructLayer(Title, LinearTint, Clamped);

    // If user currently has faces selected, assign them into the new layer immediately
    const auto SelectedFaces = SelectionMask.CollectSelectedFaces();
    if (!SelectedFaces.empty() && SelectedLayerOrdinal == 0u)
    {
        Asset.Layers.AssignFacesToLayer(Asset.Network, SelectedFaces, NewOrd);
        Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    }
    SelectPriorityLayer(NewOrd);
    RecordUVRevisionStep("CreatePriorityLayer");
    SyncNetworkToGPU();
    return NewOrd;
}

void UVWorkspaceSession::RenameSelectedLayer(std::string_view NewTitle, bool RecordRevision)
{
    if (SelectedLayerOrdinal == 0u || NewTitle.empty())
    {
        return;
    }
    if (auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal))
    {
        if (Layer->LayerTitle == NewTitle)
        {
            return;
        }
        Layer->LayerTitle = std::string(NewTitle);
        if (RecordRevision)
        {
            RecordUVRevisionStep("RenamePriorityLayer");
        }
        FramebuffersDirty = true;
    }
}

uint16_t UVWorkspaceSession::DuplicateSelectedLayer()
{
    if (SelectedLayerOrdinal == 0u)
    {
        return CreatePriorityLayer("Priority Layer", { 0.62f, 0.85f, 0.77f }, 10.0f);
    }
    const auto* Source = Asset.Layers.InspectLayer(SelectedLayerOrdinal);
    if (Source == nullptr)
    {
        return 0u;
    }
    return CreatePriorityLayer(Source->LayerTitle + " Copy", Source->LinearTintRGB, std::min(15.0f, Source->TargetSharePercent));
}

bool UVWorkspaceSession::DeleteSelectedLayer()
{
    if (SelectedLayerOrdinal == 0u)
    {
        return false;
    }
    if (!Asset.Layers.RemoveLayer(Asset.Network, SelectedLayerOrdinal))
    {
        return false;
    }
    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    const auto Remaining = Asset.Layers.InspectLayers();
    SelectPriorityLayer(Remaining.empty() ? 0u : Remaining.front().LayerOrdinal);
    RecordUVRevisionStep("DeletePriorityLayer");
    SyncNetworkToGPU();
    return true;
}

void UVWorkspaceSession::AssignSelectedLayerTint(Mathematics::Scalar3D LinearTint, bool RecordRevision)
{
    AssignLayerTintByOrdinal(SelectedLayerOrdinal, LinearTint, RecordRevision);
}

void UVWorkspaceSession::AssignLayerTintByOrdinal(uint16_t LayerOrdinal, Mathematics::Scalar3D LinearTint, bool RecordRevision)
{
    if (LayerOrdinal == 0u)
    {
        return;
    }
    if (auto* Layer = Asset.Layers.ResolveLayer(LayerOrdinal))
    {
        Layer->LinearTintRGB = LinearTint;
        if (Rasterizer != nullptr)
        {
            Rasterizer->ConfigureLayerSwatches(Asset.Layers.InspectLayers());
        }
        if (RecordRevision)
        {
            RecordUVRevisionStep("TintPriorityLayer");
        }
        FramebuffersDirty = true;
    }
}

void UVWorkspaceSession::AdjustSelectedLayerShare(float RequestedSharePercent, bool RecordRevision)
{
    if (SelectedLayerOrdinal == 0u)
    {
        return;
    }
    auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal);
    if (Layer == nullptr)
    {
        return;
    }

    float OtherSum = 0.0f;
    for (const auto& Entry : Asset.Layers.InspectLayers())
    {
        if (Entry.LayerOrdinal != SelectedLayerOrdinal)
        {
            OtherSum += Entry.TargetSharePercent;
        }
    }
    const float Ceiling       = std::max(0.0f, 100.0f - OtherSum);
    Layer->TargetSharePercent = std::clamp(RequestedSharePercent, 0.0f, Ceiling);
    if (RecordRevision)
    {
        RecordUVRevisionStep("AdjustLayerShare");
    }
    FramebuffersDirty = true;
}

bool UVWorkspaceSession::MoveSelectedLayerToUDIM(uint32_t TargetUDIMNumber)
{
    auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal);
    if (Layer == nullptr)
    {
        return false;
    }
    if (TargetUDIMNumber != 0u && TargetUDIMNumber < 1001u)
    {
        return false;
    }

    Layer->TargetUDIMNumber = TargetUDIMNumber;
    if (TargetUDIMNumber == 0u)
    {
        Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
        RecordUVRevisionStep("SetPriorityLayerAutoUDIM");
        FramebuffersDirty = true;
        return true;
    }
    if (std::find(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end(), TargetUDIMNumber) == Asset.UDIMTiles.end())
    {
        Asset.UDIMTiles.push_back(TargetUDIMNumber);
        std::sort(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end());
    }

    const uint32_t TargetTileIndex = TargetUDIMNumber - 1001u;
    const float TargetOriginU = static_cast<float>(TargetTileIndex % 10u);
    const float TargetOriginV = static_cast<float>(TargetTileIndex / 10u);
    uint32_t MovedIslandCount = 0u;

    for (const UVIslandPartition& Island : SeamCutGenerator::PartitionConnectedUVIslands(Asset.Network))
    {
        if (Island.PriorityLayerOrdinal != SelectedLayerOrdinal || Island.FaceIndices.empty())
        {
            continue;
        }

        const uint32_t CurrentTile = Island.AssignedUDIMNumber >= 1001u ? Island.AssignedUDIMNumber : 1001u;
        const uint32_t CurrentTileIndex = CurrentTile - 1001u;
        const float CurrentOriginU = static_cast<float>(CurrentTileIndex % 10u);
        const float CurrentOriginV = static_cast<float>(CurrentTileIndex / 10u);
        const bool IsOutOfBounds =
            Island.BoundsMinUV.U < CurrentOriginU - 1.0e-4f || Island.BoundsMaxUV.U > CurrentOriginU + 1.0001f ||
            Island.BoundsMinUV.V < CurrentOriginV - 1.0e-4f || Island.BoundsMaxUV.V > CurrentOriginV + 1.0001f;
        const float ShiftU = IsOutOfBounds
            ? TargetOriginU + 0.5f - (Island.BoundsMinUV.U + Island.BoundsMaxUV.U) * 0.5f
            : TargetOriginU - CurrentOriginU;
        const float ShiftV = IsOutOfBounds
            ? TargetOriginV + 0.5f - (Island.BoundsMinUV.V + Island.BoundsMaxUV.V) * 0.5f
            : TargetOriginV - CurrentOriginV;

        if (std::abs(ShiftU) < 1.0e-6f && std::abs(ShiftV) < 1.0e-6f)
        {
            continue;
        }
        for (const uint32_t EdgeIndex : Island.CornerEdgeIndices)
        {
            if (EdgeIndex < Asset.Network.Edges.size())
            {
                auto& UV = Asset.Network.Edges[EdgeIndex].ParameterUV;
                UV.U += ShiftU;
                UV.V += ShiftV;
            }
        }
        ++MovedIslandCount;
    }

    if (MovedIslandCount > 0u)
    {
        ++Asset.Network.UVRevision;
    }
    FinalizeInteractiveUVRevision("MovePriorityLayerToUDIM");
    return true;
}

void UVWorkspaceSession::ApplyMatCapToActiveSelection(uint8_t MatCapSlot, bool RecordRevision)
{
    auto TargetFaces = SelectionMask.CollectSelectedFaces();
    if (TargetFaces.empty())
    {
        TargetFaces.resize(Asset.Network.Faces.size());
        std::iota(TargetFaces.begin(), TargetFaces.end(), 0u);
    }
    for (const uint32_t FaceIdx : TargetFaces)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            Asset.Network.Faces[FaceIdx].Shading.MatCapSlot = static_cast<uint8_t>(MatCapSlot & 15u);
        }
    }
    if (RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::MatCapPreview &&
        RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap)
    {
        RenderOptions3D.DisplayMode = GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap;
    }
    ++Asset.Network.ShadingRevision;
    if (RecordRevision)
    {
        RecordUVRevisionStep("ApplyMatCapPreset");
    }
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    }
    FramebuffersDirty = true;
}

void UVWorkspaceSession::ApplySurfaceAlbedoColour(Mathematics::Scalar3D SrgbColour, bool RecordRevision)
{
    auto ToLinearChannel = [](float Srgb) noexcept -> float
    {
        return std::pow(std::clamp(Srgb, 0.0f, 1.0f), 2.2f);
    };
    const Mathematics::Scalar3D LinearRGB {
        ToLinearChannel(SrgbColour.X),
        ToLinearChannel(SrgbColour.Y),
        ToLinearChannel(SrgbColour.Z),
    };
    auto TargetFaces = SelectionMask.CollectSelectedFaces();
    if (TargetFaces.empty())
    {
        TargetFaces.resize(Asset.Network.Faces.size());
        std::iota(TargetFaces.begin(), TargetFaces.end(), 0u);
    }
    for (const uint32_t FaceIdx : TargetFaces)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            Asset.Network.Faces[FaceIdx].Shading.AlbedoLinearRGB = LinearRGB;
        }
    }
    if (RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::SurfaceMaterial &&
        RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap)
    {
        RenderOptions3D.DisplayMode = GeometricRaster::SurfaceDisplayMode::SurfaceMaterial;
    }
    ++Asset.Network.ShadingRevision;
    if (RecordRevision)
    {
        RecordUVRevisionStep("ApplySurfaceAlbedoColour");
    }
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    }
    FramebuffersDirty = true;
}

void UVWorkspaceSession::ApplyMatCapTintColour(Mathematics::Scalar3D SrgbColour, bool RecordRevision)
{
    auto ToLinearChannel = [](float Srgb) noexcept -> float
    {
        return std::pow(std::clamp(Srgb, 0.0f, 1.0f), 2.2f);
    };
    const Mathematics::Scalar3D LinearRGB {
        ToLinearChannel(SrgbColour.X),
        ToLinearChannel(SrgbColour.Y),
        ToLinearChannel(SrgbColour.Z),
    };
    auto TargetFaces = SelectionMask.CollectSelectedFaces();
    if (TargetFaces.empty())
    {
        TargetFaces.resize(Asset.Network.Faces.size());
        std::iota(TargetFaces.begin(), TargetFaces.end(), 0u);
    }
    for (const uint32_t FaceIdx : TargetFaces)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            Asset.Network.Faces[FaceIdx].Shading.MatCapTintLinearRGB = LinearRGB;
        }
    }
    if (RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::MatCapPreview &&
        RenderOptions3D.DisplayMode != GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap)
    {
        RenderOptions3D.DisplayMode = GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap;
    }
    ++Asset.Network.ShadingRevision;
    if (RecordRevision)
    {
        RecordUVRevisionStep("ApplyMatCapTintColour");
    }
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    }
    FramebuffersDirty = true;
}

void UVWorkspaceSession::ApplySurfaceRoughnessMetallic(
    float Roughness,
    float Metallic,
    float MatCapBlend,
    float Specular,
    bool  RecordRevision)
{
    auto TargetFaces = SelectionMask.CollectSelectedFaces();
    if (TargetFaces.empty())
    {
        TargetFaces.resize(Asset.Network.Faces.size());
        std::iota(TargetFaces.begin(), TargetFaces.end(), 0u);
    }
    for (const uint32_t FaceIdx : TargetFaces)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            auto& Sh       = Asset.Network.Faces[FaceIdx].Shading;
            Sh.Roughness   = std::clamp(Roughness, 0.04f, 1.0f);
            Sh.Metallic    = std::clamp(Metallic, 0.0f, 1.0f);
            Sh.Specular    = std::clamp(Specular, 0.0f, 1.0f);
            Sh.MatCapBlend = std::clamp(MatCapBlend, 0.0f, 1.0f);
        }
    }
    ++Asset.Network.ShadingRevision;
    if (RecordRevision)
    {
        RecordUVRevisionStep("ApplySurfacePBR");
    }
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    }
    FramebuffersDirty = true;
}

uint32_t UVWorkspaceSession::ConstructNextUDIMTile()
{
    uint32_t NextCode = 1001u;
    for (const uint32_t Code : Asset.UDIMTiles)
    {
        NextCode = std::max(NextCode, Code + 1u);
    }
    Asset.UDIMTiles.push_back(NextCode);
    UDIMSettings[NextCode] = UDIMTileSetting{ WorkspaceConfig.InspectUVPreferences().DefaultTileResolutionPx, GeometricRaster::UVBackdropPreset::None };
    ActiveUDIMNumber       = NextCode;
    RecordUVRevisionStep("AddUDIMTile");
    FramebuffersDirty      = true;
    return NextCode;
}

bool UVWorkspaceSession::RemoveUDIMTile(uint32_t UDIMNumber)
{
    if (UDIMNumber == 1001u || Asset.UDIMTiles.size() <= 1u)
    {
        return false;
    }
    const auto It = std::find(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end(), UDIMNumber);
    if (It == Asset.UDIMTiles.end())
    {
        return false;
    }
    Asset.UDIMTiles.erase(It);
    UDIMSettings.erase(UDIMNumber);
    if (ActiveUDIMNumber == UDIMNumber)
    {
        ActiveUDIMNumber = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();
    }
    if (OutlinerUDIMFilter == UDIMNumber)
    {
        OutlinerUDIMFilter = 0u;
    }
    RecordUVRevisionStep("RemoveUDIMTile");
    SyncNetworkToGPU();
    FramebuffersDirty = true;
    return true;
}

std::vector<uint32_t> UVWorkspaceSession::CollectOverlappingUVFaces() const
{
    std::vector<UVTriangle2D> Tris;
    Tris.reserve(Asset.Network.Faces.size() * 2u);

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Asset.Network.Faces.size()); ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Asset.Network.Faces[FaceIdx];
        if (CurrentFace.CornerCount < 3u)
        {
            continue;
        }
        std::vector<Mathematics::UVAxes> LoopUVs;
        LoopUVs.reserve(CurrentFace.CornerCount);
        uint32_t ECur = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount && ECur < Asset.Network.Edges.size(); ++Corner)
        {
            LoopUVs.push_back(Asset.Network.Edges[ECur].ParameterUV);
            ECur = Asset.Network.Edges[ECur].NextEdgeIndex;
        }
        if (LoopUVs.size() < 3u)
        {
            continue;
        }

        for (std::size_t Tri = 1u; Tri + 1u < LoopUVs.size(); ++Tri)
        {
            const auto& A = LoopUVs[0u];
            const auto& B = LoopUVs[Tri];
            const auto& C = LoopUVs[Tri + 1u];
            const float Area2 = std::abs((B.U - A.U) * (C.V - A.V) - (B.V - A.V) * (C.U - A.U));
            if (Area2 <= 1.0e-8f)
            {
                continue;
            }
            UVTriangle2D T {};
            T.FaceIndex = FaceIdx;
            T.U0 = A.U; T.V0 = A.V;
            T.U1 = B.U; T.V1 = B.V;
            T.U2 = C.U; T.V2 = C.V;
            T.MinU = std::min({ A.U, B.U, C.U });
            T.MaxU = std::max({ A.U, B.U, C.U });
            T.MinV = std::min({ A.V, B.V, C.V });
            T.MaxV = std::max({ A.V, B.V, C.V });
            Tris.push_back(T);
        }
    }

    std::sort(Tris.begin(), Tris.end(), [](const UVTriangle2D& L, const UVTriangle2D& R) noexcept
    {
        return L.MinU < R.MinU;
    });

    std::vector<uint8_t> Overlaps(Asset.Network.Faces.size(), 0u);
    constexpr float SweepEps = 2.5e-4f;

    for (std::size_t I = 0u; I < Tris.size(); ++I)
    {
        const UVTriangle2D& A = Tris[I];
        for (std::size_t J = I + 1u; J < Tris.size(); ++J)
        {
            const UVTriangle2D& B = Tris[J];
            if (B.MinU >= A.MaxU - SweepEps)
            {
                break;
            }
            if (A.FaceIndex == B.FaceIndex)
            {
                continue;
            }
            if (B.MinV >= A.MaxV - SweepEps || A.MinV >= B.MaxV - SweepEps)
            {
                continue;
            }
            if (TrianglesOverlap2DSAT(A, B, SweepEps))
            {
                Overlaps[A.FaceIndex] = 1u;
                Overlaps[B.FaceIndex] = 1u;
            }
        }
    }

    std::vector<uint32_t> Result;
    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Overlaps.size()); ++FIdx)
    {
        if (Overlaps[FIdx] != 0u)
        {
            Result.push_back(FIdx);
        }
    }
    return Result;
}

bool UVWorkspaceSession::ApplyLiveUnwrapIfEnabled()
{
    if (!LiveUnwrapEnabled)
    {
        return false;
    }
    bool HasSeamOrPin = false;
    for (const auto& Edge : Asset.Network.Edges)
    {
        if (GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut) ||
            GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock))
        {
            HasSeamOrPin = true;
            break;
        }
    }
    if (!HasSeamOrPin)
    {
        return false;
    }
    const ConformalUnwrapReport Rep = ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, nullptr, 24u);
    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    FramebuffersDirty = true;
    return Rep.IslandsFlattened > 0u;
}

bool UVWorkspaceSession::TryConsumeGlobalHistoryShortcut(
    bool     ShortcutUndo,
    bool     ShortcutRedo,
    bool     TextInputActive,
    uint64_t TickSequence)
{
    if (TextInputActive || (!ShortcutUndo && !ShortcutRedo))
    {
        return false;
    }
    if (TickSequence != UINT64_MAX && TickSequence == LastHistoryTickSequence)
    {
        return false;
    }
    LastHistoryTickSequence = TickSequence;
    if (ShortcutRedo)
    {
        return RedoParameterization();
    }
    if (ShortcutUndo)
    {
        return UndoParameterization();
    }
    return false;
}

UDIMTileSetting& UVWorkspaceSession::AccessUDIMTileSetting(uint32_t UDIMNumber)
{
    auto [It, Inserted] = UDIMSettings.try_emplace(
        UDIMNumber,
        UDIMTileSetting{ WorkspaceConfig.InspectUVPreferences().DefaultTileResolutionPx, GeometricRaster::UVBackdropPreset::None });
    (void)Inserted;
    return It->second;
}

UDIMTileSetting UVWorkspaceSession::QueryUDIMTileSetting(uint32_t UDIMNumber) const noexcept
{
    const auto It = UDIMSettings.find(UDIMNumber);
    if (It != UDIMSettings.end())
    {
        return It->second;
    }
    return UDIMTileSetting{ WorkspaceConfig.InspectUVPreferences().DefaultTileResolutionPx, GeometricRaster::UVBackdropPreset::None };
}

void UVWorkspaceSession::SynchronizeSelectionToGPU()
{
    if (Rasterizer == nullptr)
    {
        return;
    }
    SelectionMask.SynchronizeCapacity(Asset.Network);
    Rasterizer->UploadSelectionBitmaskWords(SelectionMask.InspectPackedFaceWords());
    FramebuffersDirty = true;
}

void UVWorkspaceSession::ClearViewportSelection()
{
    SelectionMask.SynchronizeCapacity(Asset.Network);
    SelectionMask.ClearAll();
    SpatialSelectionActive             = false;
    RenderOptions3D.ActiveLayerOrdinal = UINT16_MAX;
    RenderOptions2D.ActiveLayerOrdinal = UINT16_MAX;
    RenderOptions3D.HoveredFaceIndex   = GeometrySpace::InvalidNetworkIndex;
    RenderOptions2D.HoveredFaceIndex   = GeometrySpace::InvalidNetworkIndex;
    Gizmo2D.AssignHoveredHandle(Editor::GizmoHandleOrdinal::None);
    Gizmo3D.AssignHoveredHandle(Editor::GizmoHandleOrdinal::None);
    SynchronizeSelectionToGPU();
    StatusBannerText  = "Cleared Selection";
    FramebuffersDirty = true;
}

void UVWorkspaceSession::RefreshGizmoAnchorsFromSelection() noexcept
{
    // The spatial manipulator belongs to the object as a whole, not the current face selection.
    if (!Asset.Network.Vertices.empty())
    {
        Mathematics::Axis3D Min { 1.0e9f, 1.0e9f, 1.0e9f };
        Mathematics::Axis3D Max { -1.0e9f, -1.0e9f, -1.0e9f };
        for (const auto& Vertex : Asset.Network.Vertices)
        {
            Min = { std::min(Min.X, Vertex.Axes.X), std::min(Min.Y, Vertex.Axes.Y), std::min(Min.Z, Vertex.Axes.Z) };
            Max = { std::max(Max.X, Vertex.Axes.X), std::max(Max.Y, Vertex.Axes.Y), std::max(Max.Z, Vertex.Axes.Z) };
        }
        Gizmo3D.ConfigureAnchor({ 0.5f * (Min.X + Max.X), 0.5f * (Min.Y + Max.Y), 0.5f * (Min.Z + Max.Z) }, 1.12f);
    }

    const auto Selected = SelectionMask.CollectSelectedFaces();
    if (Selected.empty())
    {
        return;
    }
    Mathematics::UVAxes SumUV {};
    for (const uint32_t FIdx : Selected)
    {
        const auto CU = Asset.Network.EvaluateFaceCentroidUV(FIdx);
        SumUV = { SumUV.U + CU.U, SumUV.V + CU.V };
    }
    const float Inv = 1.0f / static_cast<float>(Selected.size());
    Gizmo2D.ConfigureAnchor({ SumUV.U * Inv, SumUV.V * Inv, 0.0f }, 0.30f);
}

void UVWorkspaceSession::FinalizeInteractiveUVRevision(std::string_view OperationTag)
{
    RecordUVRevisionStep(OperationTag);
    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    FramebuffersDirty = true;
}

void UVWorkspaceSession::FitSelectionOrNetwork() noexcept
{
    auto Selected = SelectionMask.CollectSelectedFaces();
    if (Selected.empty())
    {
        Selected.resize(Asset.Network.Faces.size());
        std::iota(Selected.begin(), Selected.end(), 0u);
    }
    if (Selected.empty())
    {
        return;
    }

    Mathematics::Axis3D Min3D {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D Max3D { -1.0e9f, -1.0e9f, -1.0e9f };
    Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
    Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };

    for (const uint32_t FIdx : Selected)
    {
        if (FIdx >= Asset.Network.Faces.size())
        {
            continue;
        }
        uint32_t Cur = Asset.Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Asset.Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            const auto& V  = Asset.Network.Vertices[Asset.Network.Edges[Cur].OriginVertexIndex].Axes;
            const auto& UV = Asset.Network.Edges[Cur].ParameterUV;
            Min3D = { std::min(Min3D.X, V.X), std::min(Min3D.Y, V.Y), std::min(Min3D.Z, V.Z) };
            Max3D = { std::max(Max3D.X, V.X), std::max(Max3D.Y, V.Y), std::max(Max3D.Z, V.Z) };
            MinUV = { std::min(MinUV.U, UV.U), std::min(MinUV.V, UV.V) };
            MaxUV = { std::max(MaxUV.U, UV.U), std::max(MaxUV.V, UV.V) };
            Cur   = Asset.Network.Edges[Cur].NextEdgeIndex;
        }
    }

    auto& Proj = ActiveDocument.AccessViewportProjection();
    Proj.FitSpatialExtents(Min3D, Max3D);
    Proj.FitPlanarExtents(MinUV, MaxUV);
    FramebuffersDirty = true;
}

void UVWorkspaceSession::FitPlanarSelectionOrNetwork() noexcept
{
    auto Selected = SelectionMask.CollectSelectedFaces();
    if (Selected.empty())
    {
        Selected.resize(Asset.Network.Faces.size());
        std::iota(Selected.begin(), Selected.end(), 0u);
    }
    if (Selected.empty())
    {
        return;
    }

    Mathematics::UVAxes MinUV {  1.0e9f,  1.0e9f };
    Mathematics::UVAxes MaxUV { -1.0e9f, -1.0e9f };
    for (const uint32_t FIdx : Selected)
    {
        if (FIdx >= Asset.Network.Faces.size())
        {
            continue;
        }
        uint32_t Cur = Asset.Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Asset.Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            const auto& UV = Asset.Network.Edges[Cur].ParameterUV;
            MinUV = { std::min(MinUV.U, UV.U), std::min(MinUV.V, UV.V) };
            MaxUV = { std::max(MaxUV.U, UV.U), std::max(MaxUV.V, UV.V) };
            Cur   = Asset.Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ActiveDocument.AccessViewportProjection().FitPlanarExtents(MinUV, MaxUV);
    FramebuffersDirty = true;
}

void UVWorkspaceSession::FitSpatialSelectionOrNetwork() noexcept
{
    auto Selected = SelectionMask.CollectSelectedFaces();
    if (Selected.empty())
    {
        Selected.resize(Asset.Network.Faces.size());
        std::iota(Selected.begin(), Selected.end(), 0u);
    }
    if (Selected.empty())
    {
        return;
    }

    Mathematics::Axis3D Min3D {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D Max3D { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const uint32_t FIdx : Selected)
    {
        if (FIdx >= Asset.Network.Faces.size())
        {
            continue;
        }
        uint32_t Cur = Asset.Network.Faces[FIdx].BoundaryLoopEdgeIndex;
        for (uint16_t C = 0u; C < Asset.Network.Faces[FIdx].CornerCount && Cur != GeometrySpace::InvalidNetworkIndex; ++C)
        {
            const auto& V = Asset.Network.Vertices[Asset.Network.Edges[Cur].OriginVertexIndex].Axes;
            Min3D = { std::min(Min3D.X, V.X), std::min(Min3D.Y, V.Y), std::min(Min3D.Z, V.Z) };
            Max3D = { std::max(Max3D.X, V.X), std::max(Max3D.Y, V.Y), std::max(Max3D.Z, V.Z) };
            Cur   = Asset.Network.Edges[Cur].NextEdgeIndex;
        }
    }
    ActiveDocument.AccessViewportProjection().FitSpatialExtents(Min3D, Max3D);
    FramebuffersDirty = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SOLVER DIRECTIVES, ASYNC THREADING & 215 MIB REVISIONS
//------------------------------------------------------------------------------------------------------------------------

void UVWorkspaceSession::QueueDirective(UVCommandDirective Directive) noexcept
{
    PendingDirective = Directive;
}

UVCommandDirective UVWorkspaceSession::ConsumePendingDirective() noexcept
{
    const UVCommandDirective Current = PendingDirective;
    PendingDirective                 = UVCommandDirective::None;
    return Current;
}

bool UVWorkspaceSession::ExecuteDirective(
    UVCommandDirective            Directive,
    Threading::ExecutionChannels* OptionalChannels)
{
    if (Directive == UVCommandDirective::None)
    {
        return false;
    }

    const auto& Prefs = WorkspaceConfig.InspectUVPreferences();
    char        Msg[192]{};

    switch (Directive)
    {
        case UVCommandDirective::MarkSharpSeams:
        {
            const uint32_t Marked = SeamCutGenerator::MarkDihedralSharpSeams(Asset.Network, SharpAngleThreshold, false);
            WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
            if (LiveUnwrapEnabled && Marked > 0u)
            {
                (void)ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 24u);
            }
            std::snprintf(Msg, sizeof(Msg), "Marked %u Dihedral Seam Edges (>= %.0f deg)", Marked, SharpAngleThreshold);
            RecordUVRevisionStep("MarkSharpSeams");
            break;
        }
        case UVCommandDirective::ResetFaceUVsToTile:
        {
            auto TargetFaces = SelectionMask.CollectSelectedFaces();
            if (TargetFaces.empty())
            {
                TargetFaces.resize(Asset.Network.Faces.size());
                std::iota(TargetFaces.begin(), TargetFaces.end(), 0u);
            }
            const uint32_t TileIdx = (ActiveUDIMNumber >= 1001u) ? (ActiveUDIMNumber - 1001u) : 0u;
            const float TileOriginU = static_cast<float>(TileIdx % 10u);
            const float TileOriginV = static_cast<float>(TileIdx / 10u);
            uint32_t ResetCount = 0u;
            for (const uint32_t FaceIdx : TargetFaces)
            {
                if (FaceIdx >= Asset.Network.Faces.size())
                {
                    continue;
                }
                const auto& F = Asset.Network.Faces[FaceIdx];
                if (F.CornerCount < 3u)
                {
                    continue;
                }
                std::vector<uint32_t> Loop;
                Loop.reserve(F.CornerCount);
                uint32_t E = F.BoundaryLoopEdgeIndex;
                for (uint16_t C = 0u; C < F.CornerCount && E < Asset.Network.Edges.size(); ++C)
                {
                    Loop.push_back(E);
                    E = Asset.Network.Edges[E].NextEdgeIndex;
                }
                if (Loop.size() == 4u)
                {
                    constexpr std::array<Mathematics::UVAxes, 4u> QuadUVs {{
                        { 0.02f, 0.02f }, { 0.98f, 0.02f }, { 0.98f, 0.98f }, { 0.02f, 0.98f },
                    }};
                    for (std::size_t C = 0u; C < 4u; ++C)
                    {
                        Asset.Network.Edges[Loop[C]].ParameterUV = { TileOriginU + QuadUVs[C].U, TileOriginV + QuadUVs[C].V };
                    }
                }
                else if (Loop.size() == 3u)
                {
                    constexpr std::array<Mathematics::UVAxes, 3u> TriUVs {{
                        { 0.02f, 0.02f }, { 0.98f, 0.02f }, { 0.50f, 0.98f },
                    }};
                    for (std::size_t C = 0u; C < 3u; ++C)
                    {
                        Asset.Network.Edges[Loop[C]].ParameterUV = { TileOriginU + TriUVs[C].U, TileOriginV + TriUVs[C].V };
                    }
                }
                else
                {
                    const float Step = 6.28318530718f / static_cast<float>(Loop.size());
                    for (std::size_t C = 0u; C < Loop.size(); ++C)
                    {
                        const float Angle = Step * static_cast<float>(C);
                        Asset.Network.Edges[Loop[C]].ParameterUV = {
                            TileOriginU + 0.50f + 0.46f * std::cos(Angle),
                            TileOriginV + 0.50f + 0.46f * std::sin(Angle),
                        };
                    }
                }
                ++ResetCount;
            }
            ++Asset.Network.UVRevision;
            std::snprintf(Msg, sizeof(Msg), "Reset %u Face UVs to Tile %u", ResetCount, ActiveUDIMNumber);
            RecordUVRevisionStep("ResetFaceUVsToTile");
            break;
        }
        case UVCommandDirective::UnwrapConformal:
        {
            const ConformalUnwrapReport Rep = ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 28u);
            std::snprintf(Msg, sizeof(Msg), "Conformal Unwrap: %u Islands in %.2f ms (0 Flips)", Rep.IslandsFlattened, Rep.ElapsedMilliseconds);
            RecordUVRevisionStep("UnwrapConformal");
            break;
        }
        case UVCommandDirective::FlattenAngleBasedABF:
        {
            const uint32_t Count = UVProjectionSolvers::FlattenAngleBasedABF(Asset.Network, 12u);
            std::snprintf(Msg, sizeof(Msg), "ABF++ Angle Flatten: %u Islands Optimized (0 Flips)", Count);
            RecordUVRevisionStep("FlattenAngleBasedABF");
            break;
        }
        case UVCommandDirective::ProjectSmartUV:
        {
            const uint32_t Clusters = UVProjectionSolvers::ProjectSmartAngleClusters(Asset.Network, 66.0f, 0.02f);
            std::snprintf(Msg, sizeof(Msg), "Smart UV Project: %u Normal Clusters", Clusters);
            RecordUVRevisionStep("ProjectSmartUV");
            break;
        }
        case UVCommandDirective::ProjectCubicBox:
        {
            UVProjectionSolvers::ProjectCubicBox(Asset.Network, SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Applied Triplanar Cubic Box Projection");
            RecordUVRevisionStep("ProjectCubicBox");
            break;
        }
        case UVCommandDirective::ProjectCylindrical:
        {
            UVProjectionSolvers::ProjectCylindrical(Asset.Network, SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Applied Polar Cylindrical Projection");
            RecordUVRevisionStep("ProjectCylindrical");
            break;
        }
        case UVCommandDirective::ProjectSpherical:
        {
            UVProjectionSolvers::ProjectSpherical(Asset.Network, SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Applied Equirectangular Spherical Projection");
            RecordUVRevisionStep("ProjectSpherical");
            break;
        }
        case UVCommandDirective::ProjectCameraView:
        {
            UVProjectionSolvers::ProjectFromCameraView(Asset.Network, ActiveDocument.InspectViewportProjection(), SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Applied Camera View Planar Projection");
            RecordUVRevisionStep("ProjectCameraView");
            break;
        }
        case UVCommandDirective::FollowActiveQuads:
        {
            const uint32_t Count = UVProjectionSolvers::FollowActiveQuads(
                Asset.Network, SelectionMask.ActiveFaceIndex, SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Follow Active Quads: Propagated %u Quads", Count);
            RecordUVRevisionStep("FollowActiveQuads");
            break;
        }
        case UVCommandDirective::MarkSelectedSeams:
        {
            auto Edges = SelectionMask.CollectSelectedEdges();
            if (Edges.empty())
            {
                Edges = GeometrySpace::GeometrySelection::EvaluateBoundaryEdges(Asset.Network, &SelectionMask);
            }
            const uint32_t Marked = UVAuthoringOperations::MarkEdgesAsSeams(Asset.Network, Edges, true);
            WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
            if (LiveUnwrapEnabled && Marked > 0u)
            {
                (void)ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 24u);
            }
            std::snprintf(Msg, sizeof(Msg), "Marked %u Selected Seam Edges", Marked);
            RecordUVRevisionStep("MarkSelectedSeams");
            break;
        }
        case UVCommandDirective::ClearSelectedSeams:
        {
            auto Edges = SelectionMask.CollectSelectedEdges();
            if (Edges.empty())
            {
                Edges = GeometrySpace::GeometrySelection::EvaluateSeamEdges(Asset.Network);
            }
            const uint32_t Welded = UVAuthoringOperations::ClearSeamsWithUVWeld(Asset.Network, Edges);
            if (LiveUnwrapEnabled && Welded > 0u)
            {
                (void)ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 24u);
            }
            std::snprintf(Msg, sizeof(Msg), "Cleared & Welded %u Seam Edges", Welded);
            RecordUVRevisionStep("ClearSelectedSeams");
            break;
        }
        case UVCommandDirective::DeriveSeamsFromIslands:
        {
            const uint32_t Seams = UVAuthoringOperations::DeriveSeamsFromIslands(Asset.Network);
            WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
            std::snprintf(Msg, sizeof(Msg), "Derived %u Seam Edges from UV Islands", Seams);
            RecordUVRevisionStep("DeriveSeamsFromIslands");
            break;
        }
        case UVCommandDirective::PinSelectedUVs:
        {
            const uint32_t Pinned = UVAuthoringOperations::AssignPinLockOnFaces(Asset.Network, SelectionMask.CollectSelectedFaces(), true);
            if (LiveUnwrapEnabled && Pinned > 0u)
            {
                (void)ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 24u);
            }
            std::snprintf(Msg, sizeof(Msg), "Pinned %u UV Corners (EnablePinLock)", Pinned);
            RecordUVRevisionStep("PinSelectedUVs");
            break;
        }
        case UVCommandDirective::UnpinSelectedUVs:
        {
            const uint32_t Unpinned = UVAuthoringOperations::AssignPinLockOnFaces(Asset.Network, SelectionMask.CollectSelectedFaces(), false);
            if (LiveUnwrapEnabled && Unpinned > 0u)
            {
                (void)ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 24u);
            }
            std::snprintf(Msg, sizeof(Msg), "Unpinned %u UV Corners", Unpinned);
            RecordUVRevisionStep("UnpinSelectedUVs");
            break;
        }
        case UVCommandDirective::StitchSelectedSeams:
        {
            auto Edges = SelectionMask.CollectSelectedEdges();
            if (Edges.empty())
            {
                Edges = GeometrySpace::GeometrySelection::EvaluateBoundaryEdges(Asset.Network, &SelectionMask);
            }
            const uint32_t Stitched = UVAuthoringOperations::StitchAcrossSelectedEdges(Asset.Network, Edges);
            std::snprintf(Msg, sizeof(Msg), "Stitched %u Seam Edges Across Islands", Stitched);
            RecordUVRevisionStep("StitchSelectedSeams");
            break;
        }
        case UVCommandDirective::SplitSelectedIsland:
        {
            const uint32_t SplitCount = UVAuthoringOperations::SplitFacesIntoDetachedIsland(Asset.Network, SelectionMask);
            std::snprintf(Msg, sizeof(Msg), "Detached %u Faces into New UV Island", SplitCount);
            RecordUVRevisionStep("SplitSelectedIsland");
            break;
        }
        case UVCommandDirective::RectifySelectedQuads:
        {
            const uint32_t Rectified = UVAuthoringOperations::RectifySelectedQuadFaces(Asset.Network, SelectionMask.CollectSelectedFaces());
            std::snprintf(Msg, sizeof(Msg), "Rectified %u Quad Faces", Rectified);
            RecordUVRevisionStep("RectifySelectedQuads");
            break;
        }
        case UVCommandDirective::RelaxStretchGPU:
        {
            const StretchRelaxReport Rep = StretchRelaxer.RelaxStretch(Asset.Network, RelaxIterations, 0.35f, true);
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Relaxed %u Corners (%u Passes, %s) in %.2f ms",
                Rep.WeldedCornersRelaxed,
                Rep.ExecutedIterations,
                Rep.ExecutedOnVulkanGPU ? "Vulkan .slang" : "CPU",
                Rep.ElapsedMilliseconds);
            RecordUVRevisionStep("RelaxStretchGPU");
            break;
        }
        case UVCommandDirective::PackUVsConcaveSkyline:
        {
            const bool AllowMultiUDIMOverflow =
                Prefs.PackingScope == Configuration::UVPackingScopeMode::AutoSpillOverflowUDIMs;
            const std::vector<uint32_t> ExistingUDIMTiles = Asset.UDIMTiles;
            const std::vector<uint32_t> SelectedFaceIndices = SelectionMask.CollectSelectedFaces();
            LatestPackReport = SkylinePacker.PackConcaveSkyline(
                Asset.Network,
                Asset.Layers,
                Prefs,
                AllowMultiUDIMOverflow,
                OptionalChannels,
                ActiveUDIMNumber,
                Prefs.PackingScope,
                SelectedFaceIndices);
            if (FillPocketsEnabled && Prefs.PackingScope != Configuration::UVPackingScopeMode::SelectedIslandsOnly)
            {
                const float MarginUV = Prefs.IslandMarginPixels / static_cast<float>( std::max(256u, Prefs.DefaultTileResolutionPx));
                (void)UVIslandLayoutOperations::ExpandIntoEmptyPockets(Asset.Network, MarginUV);
            }
            if (Prefs.PackingScope == Configuration::UVPackingScopeMode::SelectedIslandsOnly)
            {
                Asset.UDIMTiles = ExistingUDIMTiles;
                for (const uint32_t TileCode : LatestPackReport.ActiveUDIMNumbers)
                {
                    if (std::find(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end(), TileCode) == Asset.UDIMTiles.end())
                    {
                        Asset.UDIMTiles.push_back(TileCode);
                    }
                }
                std::sort(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end());
            }
            else
            {
                Asset.UDIMTiles = LatestPackReport.ActiveUDIMNumbers;
            }
            if (Asset.UDIMTiles.empty())
            {
                Asset.UDIMTiles.push_back(1001u);
            }
            if (std::find(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end(), ActiveUDIMNumber) == Asset.UDIMTiles.end())
            {
                ActiveUDIMNumber = Asset.UDIMTiles.front();
            }
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Packed %u Islands (%s) | UDIM %u Fill %.1f%% in %.2f ms",
                LatestPackReport.IslandsPacked,
                LatestPackReport.EvaluatedOnVulkanGPU ? "GPU Skyline" : "CPU Skyline",
                ActiveUDIMNumber,
                LatestPackReport.PrimaryTileEfficiency * 100.0f,
                LatestPackReport.ElapsedMilliseconds);
            RecordUVRevisionStep("PackConcaveSkyline");
            break;
        }
        case UVCommandDirective::StackSimilarIslands:
        {
            const uint32_t Stacked = UVIslandLayoutOperations::StackSimilarIslands(Asset.Network);
            std::snprintf(Msg, sizeof(Msg), "Stacked %u Congruent UV Islands", Stacked);
            RecordUVRevisionStep("StackSimilarIslands");
            break;
        }
        case UVCommandDirective::UnstackOverlapping:
        {
            const uint32_t Unstacked = UVIslandLayoutOperations::UnstackOverlappingIslands(Asset.Network);
            std::snprintf(Msg, sizeof(Msg), "Unstacked %u Overlapping UV Islands", Unstacked);
            RecordUVRevisionStep("UnstackOverlapping");
            break;
        }
        case UVCommandDirective::ExpandEmptyPockets:
        {
            const float MarginUV = Prefs.IslandMarginPixels / static_cast<float>(std::max(256u, Prefs.DefaultTileResolutionPx));
            const uint32_t Expanded = UVIslandLayoutOperations::ExpandIntoEmptyPockets(Asset.Network, MarginUV);
            std::snprintf(Msg, sizeof(Msg), "Fill Pockets: Expanded %u UV Islands", Expanded);
            RecordUVRevisionStep("ExpandEmptyPockets");
            break;
        }
        case UVCommandDirective::EqualizeTexelDensity:
        {
            const uint32_t TileRes = QueryUDIMTileSetting(ActiveUDIMNumber).ResolutionPx;
            const TexelDensityReport Rep = TexelDensityEqualizer::EqualizePhysicalTexelDensity(
                Asset.Network, Prefs.TargetTexelDensity, TileRes);
            std::snprintf(Msg, sizeof(Msg), "Equalized %u Islands -> %.0f px/m (@ %u px)", Rep.IslandsEqualized, Rep.AchievedMeanTexelPerM, TileRes);
            RecordUVRevisionStep("EqualizeTexelDensity");
            break;
        }
        case UVCommandDirective::AutoDistributeUDIMs:
        {
            LatestPackReport = SkylinePacker.PackConcaveSkyline(
                Asset.Network,
                Asset.Layers,
                Prefs,
                true,
                OptionalChannels,
                ActiveUDIMNumber,
                Configuration::UVPackingScopeMode::AutoSpillOverflowUDIMs);
            Asset.UDIMTiles  = LatestPackReport.ActiveUDIMNumbers;
            ActiveUDIMNumber = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Distributed %u Islands across %u UDIM Tiles (%.2f ms)",
                LatestPackReport.IslandsPacked,
                LatestPackReport.UDIMTilesOccupied,
                LatestPackReport.ElapsedMilliseconds);
            RecordUVRevisionStep("AutoDistributeUDIMs");
            break;
        }
        case UVCommandDirective::SelectFlippedFaces:
        {
            std::vector<uint32_t> Flipped {};
            for (uint32_t FIdx = 0u; FIdx < Asset.Network.Faces.size(); ++FIdx)
            {
                if (Asset.Network.EvaluateFaceSignedAreaUV(FIdx) <= 0.0f)
                {
                    Flipped.push_back(FIdx);
                }
            }
            SelectionMode = UVSelectionMode::Faces;
            SelectionMask.SelectFacesFromList(Flipped, false);
            SynchronizeSelectionToGPU();
            std::snprintf(Msg, sizeof(Msg), "Selected %zu Flipped-Winding Faces", Flipped.size());
            StatusBannerText = Msg;
            return true;
        }
        case UVCommandDirective::SelectOverlappingFaces:
        {
            const std::vector<uint32_t> Overlapping = CollectOverlappingUVFaces();
            SelectionMode = UVSelectionMode::Faces;
            SelectionMask.SelectFacesFromList(Overlapping, false);
            SpatialSelectionActive = !Overlapping.empty();
            RefreshGizmoAnchorsFromSelection();
            SynchronizeSelectionToGPU();
            LatestMetrics.OverlapFaceCount = static_cast<uint32_t>(Overlapping.size());
            std::snprintf(Msg, sizeof(Msg), "Selected %zu Overlapping UV Faces", Overlapping.size());
            StatusBannerText = Msg;
            FramebuffersDirty = true;
            return true;
        }
        case UVCommandDirective::FitActiveSelection:
        {
            FitSelectionOrNetwork();
            StatusBannerText = "Focused Active Selection in 2D & 3D Viewports";
            return true;
        }
        case UVCommandDirective::ValidateDistortionGPU:
        {
            SyncNetworkToGPU();
            std::snprintf(
                Msg,
                sizeof(Msg),
                "GPU Validated: L2=%.3f | Flips=%u | Overlaps=%u",
                LatestMetrics.MeanL2Stretch,
                LatestMetrics.FlippedFaceCount,
                LatestMetrics.OverlapFaceCount);
            StatusBannerText = Msg;
            return true;
        }
        default:
            return false;
    }

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
    return true;
}

bool UVWorkspaceSession::DispatchDirectiveAsync(
    Threading::WorkspaceThread&   BackgroundSolverThread,
    Threading::ExecutionChannels* OptionalChannels,
    UVCommandDirective            Directive,
    Threading::CompletionCounter* OptionalCompletion)
{
    return BackgroundSolverThread.EnqueueAuthoringTask(
        [this, &BackgroundSolverThread, OptionalChannels, Directive]()
        {
            (void)ExecuteDirective(Directive, OptionalChannels);
            BackgroundSolverThread.PublishSnapshot(
                1u,
                RevisionHistory.QueryBytesUsed(),
                Constants::DefaultGlobalRevisionBudgetBytes,
                StatusBannerText);
        },
        static_cast<uint32_t>(Directive),
        OptionalCompletion);
}

std::vector<std::byte> UVWorkspaceSession::CaptureAuthoringSnapshot() const
{
    std::vector<std::byte> Buffer;
    const auto LayerSpan = Asset.Layers.InspectLayers();

    const std::size_t EstimatedBytes =
        64u +
        Asset.Network.Vertices.size() * sizeof(Mathematics::Axis3D) +
        Asset.Network.Edges.size() * (sizeof(Mathematics::UVAxes) + sizeof(uint8_t)) +
        Asset.Network.Faces.size() * (sizeof(uint16_t) * 2u + sizeof(GeometrySpace::FaceShadingAssignment)) +
        LayerSpan.size() * 64u +
        Asset.UDIMTiles.size() * 16u;
    Buffer.reserve(EstimatedBytes);

    AppendPod(Buffer, AuthoringSnapshotMagic);
    AppendPod(Buffer, AuthoringSnapshotVersion);
    AppendPod(Buffer, static_cast<uint32_t>(Asset.Network.Vertices.size()));
    AppendPod(Buffer, static_cast<uint32_t>(Asset.Network.Edges.size()));
    AppendPod(Buffer, static_cast<uint32_t>(Asset.Network.Faces.size()));
    AppendPod(Buffer, static_cast<uint32_t>(LayerSpan.size()));
    AppendPod(Buffer, static_cast<uint32_t>(Asset.UDIMTiles.size()));
    AppendPod(Buffer, SelectedLayerOrdinal);
    AppendPod(Buffer, Asset.Layers.QueryNextLayerOrdinal());
    AppendPod(Buffer, ActiveUDIMNumber);

    for (const auto& V : Asset.Network.Vertices)
    {
        AppendPod(Buffer, V.Axes);
    }
    for (const auto& E : Asset.Network.Edges)
    {
        AppendPod(Buffer, E.ParameterUV);
        AppendPod(Buffer, static_cast<uint8_t>(E.AttributeMask));
    }
    for (const auto& F : Asset.Network.Faces)
    {
        AppendPod(Buffer, F.PriorityLayerOrdinal);
        AppendPod(Buffer, F.PartGroupOrdinal);
        AppendPod(Buffer, F.Shading);
    }
    for (const auto& Layer : LayerSpan)
    {
        AppendPod(Buffer, Layer.LayerOrdinal);
        AppendPod(Buffer, Layer.TargetSharePercent);
        AppendPod(Buffer, Layer.TargetUDIMNumber);
        AppendPod(Buffer, Layer.LinearTintRGB);
        AppendString(Buffer, Layer.LayerTitle);
    }
    for (const uint32_t Code : Asset.UDIMTiles)
    {
        AppendPod(Buffer, Code);
        const auto Setting = QueryUDIMTileSetting(Code);
        AppendPod(Buffer, Setting.ResolutionPx);
        AppendPod(Buffer, static_cast<uint8_t>(Setting.BackdropPreset));
    }
    return Buffer;
}

bool UVWorkspaceSession::ApplyAuthoringSnapshot(std::span<const std::byte> SnapshotBytes)
{
    std::size_t Cursor = 0u;
    uint32_t Magic = 0u, Version = 0u;
    uint32_t VertCount = 0u, EdgeCount = 0u, FaceCount = 0u, LayerCount = 0u, UDIMCount = 0u;
    uint16_t SavedSelectedLayer = 0u, SavedNextLayerOrd = 1u;
    uint32_t SavedActiveUDIM = 1001u;

    if (!ReadPod(SnapshotBytes, Cursor, Magic) || Magic != AuthoringSnapshotMagic ||
        !ReadPod(SnapshotBytes, Cursor, Version) || Version != AuthoringSnapshotVersion ||
        !ReadPod(SnapshotBytes, Cursor, VertCount) || VertCount != Asset.Network.Vertices.size() ||
        !ReadPod(SnapshotBytes, Cursor, EdgeCount) || EdgeCount != Asset.Network.Edges.size() ||
        !ReadPod(SnapshotBytes, Cursor, FaceCount) || FaceCount != Asset.Network.Faces.size() ||
        !ReadPod(SnapshotBytes, Cursor, LayerCount) ||
        !ReadPod(SnapshotBytes, Cursor, UDIMCount) ||
        !ReadPod(SnapshotBytes, Cursor, SavedSelectedLayer) ||
        !ReadPod(SnapshotBytes, Cursor, SavedNextLayerOrd) ||
        !ReadPod(SnapshotBytes, Cursor, SavedActiveUDIM))
    {
        return false;
    }

    std::vector<Mathematics::Axis3D> SavedVerts(VertCount);
    for (uint32_t I = 0u; I < VertCount; ++I)
    {
        if (!ReadPod(SnapshotBytes, Cursor, SavedVerts[I]))
        {
            return false;
        }
    }

    std::vector<Mathematics::UVAxes> SavedUVs(EdgeCount);
    std::vector<uint8_t> SavedMasks(EdgeCount);
    for (uint32_t I = 0u; I < EdgeCount; ++I)
    {
        if (!ReadPod(SnapshotBytes, Cursor, SavedUVs[I]) || !ReadPod(SnapshotBytes, Cursor, SavedMasks[I]))
        {
            return false;
        }
    }

    std::vector<uint16_t> SavedFaceLayers(FaceCount);
    std::vector<uint16_t> SavedFaceParts(FaceCount);
    std::vector<GeometrySpace::FaceShadingAssignment> SavedShading(FaceCount);
    for (uint32_t I = 0u; I < FaceCount; ++I)
    {
        if (!ReadPod(SnapshotBytes, Cursor, SavedFaceLayers[I]) ||
            !ReadPod(SnapshotBytes, Cursor, SavedFaceParts[I]) ||
            !ReadPod(SnapshotBytes, Cursor, SavedShading[I]))
        {
            return false;
        }
    }

    std::vector<GeometrySpace::PriorityAllocationLayer> SavedLayers(LayerCount);
    for (uint32_t I = 0u; I < LayerCount; ++I)
    {
        if (!ReadPod(SnapshotBytes, Cursor, SavedLayers[I].LayerOrdinal) ||
            !ReadPod(SnapshotBytes, Cursor, SavedLayers[I].TargetSharePercent) ||
            !ReadPod(SnapshotBytes, Cursor, SavedLayers[I].TargetUDIMNumber) ||
            !ReadPod(SnapshotBytes, Cursor, SavedLayers[I].LinearTintRGB) ||
            !ReadString(SnapshotBytes, Cursor, SavedLayers[I].LayerTitle))
        {
            return false;
        }
    }

    std::vector<uint32_t> SavedUDIMTiles(UDIMCount);
    std::unordered_map<uint32_t, UDIMTileSetting> SavedUDIMSettings;
    for (uint32_t I = 0u; I < UDIMCount; ++I)
    {
        uint32_t Code = 1001u;
        uint32_t Res = 2048u;
        uint8_t  Preset = 0u;
        if (!ReadPod(SnapshotBytes, Cursor, Code) || !ReadPod(SnapshotBytes, Cursor, Res) || !ReadPod(SnapshotBytes, Cursor, Preset))
        {
            return false;
        }
        SavedUDIMTiles[I] = Code;
        SavedUDIMSettings[Code] = UDIMTileSetting{ Res, static_cast<GeometricRaster::UVBackdropPreset>(Preset) };
    }

    for (uint32_t I = 0u; I < VertCount; ++I)
    {
        Asset.Network.Vertices[I].Axes = SavedVerts[I];
    }
    for (uint32_t I = 0u; I < EdgeCount; ++I)
    {
        Asset.Network.Edges[I].ParameterUV   = SavedUVs[I];
        Asset.Network.Edges[I].AttributeMask = static_cast<GeometrySpace::EdgeAttributeMask>(SavedMasks[I]);
    }
    for (uint32_t I = 0u; I < FaceCount; ++I)
    {
        Asset.Network.Faces[I].PriorityLayerOrdinal = SavedFaceLayers[I];
        Asset.Network.Faces[I].PartGroupOrdinal     = SavedFaceParts[I];
        Asset.Network.Faces[I].Shading              = SavedShading[I];
    }

    Asset.Layers.RestoreLayers(SavedLayers, SavedNextLayerOrd);
    Asset.UDIMTiles = std::move(SavedUDIMTiles);
    if (Asset.UDIMTiles.empty())
    {
        Asset.UDIMTiles.push_back(1001u);
    }
    UDIMSettings         = std::move(SavedUDIMSettings);
    SelectedLayerOrdinal = SavedSelectedLayer;
    if (SelectedLayerOrdinal != 0u && Asset.Layers.InspectLayer(SelectedLayerOrdinal) == nullptr)
    {
        SelectedLayerOrdinal = SavedLayers.empty() ? 0u : SavedLayers.front().LayerOrdinal;
    }
    ActiveUDIMNumber = SavedActiveUDIM;
    if (std::find(Asset.UDIMTiles.begin(), Asset.UDIMTiles.end(), ActiveUDIMNumber) == Asset.UDIMTiles.end())
    {
        ActiveUDIMNumber = Asset.UDIMTiles.front();
    }
    RenderOptions3D.ActiveLayerOrdinal = SelectedLayerOrdinal;
    RenderOptions2D.ActiveLayerOrdinal = SelectedLayerOrdinal;

    Asset.Network.RecomputeNormals();
    ++Asset.Network.NetworkRevision;
    ++Asset.Network.UVRevision;
    ++Asset.Network.ShadingRevision;
    return true;
}

void UVWorkspaceSession::RecordUVRevisionStep(std::string_view OperationTag)
{
    const std::vector<std::byte> SnapshotBytes = CaptureAuthoringSnapshot();

    // If appending this snapshot would cross the active document budget, advance BaselineSnapshotBytes
    // cleanly to the next folded prefix step BEFORE OperationRecorder drops its ContentRegion bytes.
    const std::size_t RequiredBytes = SnapshotBytes.size() + sizeof(ParametricRecordEntry) + 128u;
    if (RevisionHistory.QueryBytesUsed() + RequiredBytes > RevisionHistory.QueryDocumentBudgetBytes())
    {
        (void)RevisionHistory.PurgeInactiveRedoBranches();
        std::vector<uint32_t> ActiveChain;
        while (RevisionHistory.QueryBytesUsed() + RequiredBytes > RevisionHistory.QueryDocumentBudgetBytes())
        {
            ActiveChain.clear();
            RevisionHistory.InspectHistory().ExtractActiveRootToCursorChain(ActiveChain);
            if (ActiveChain.size() <= 2u)
            {
                break;
            }
            const auto Entries = RevisionHistory.InspectHistory().InspectEntries();
            const auto Records = RevisionHistory.InspectRecorder().InspectRecordEntries();
            const uint32_t FoldStepIdx = ActiveChain[1u];
            if (FoldStepIdx < Entries.size())
            {
                const uint32_t RecIdx = Entries[FoldStepIdx].RecordEntryIndex;
                if (RecIdx < Records.size())
                {
                    const auto& Rec = Records[RecIdx];
                    const auto  Raw = RevisionHistory.InspectRecorder().InspectContentRegion().InspectSpan(Rec.RegionByteOffset, Rec.RegionByteLength);
                    if (!Raw.empty())
                    {
                        BaselineSnapshotBytes.assign(Raw.begin(), Raw.end());
                    }
                }
            }
            (void)RevisionHistory.FoldOldestPrefixSteps(1u);
        }
    }

    const ParametricOperationParameters Params = OperationRecorder::ConstructUVTileTransform(
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) % 10u),
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) / 10u),
        0.0,
        0.0,
        1.0,
        0.0,
        WorkspaceConfig.InspectUVPreferences().TargetTexelDensity,
        OperationTag);

    (void)RevisionHistory.RecordParametricOperation(Params, RevisionStampCounter++, SnapshotBytes);
}

bool UVWorkspaceSession::RestoreUVFromRevisionCursor()
{
    const auto&    History   = RevisionHistory.InspectHistory();
    const uint32_t CursorIdx = History.QueryCursorIndex();
    const uint32_t RootIdx   = History.QueryRootIndex();

    if (CursorIdx == RootIdx)
    {
        if (!BaselineSnapshotBytes.empty())
        {
            if (!ApplyAuthoringSnapshot(BaselineSnapshotBytes))
            {
                return false;
            }
        }
        else if (!Asset.Network.ApplyParameterizedUVStream(BaselineUVPairs))
        {
            return false;
        }
    }
    else
    {
        const auto Entries = History.InspectEntries();
        if (CursorIdx >= Entries.size())
        {
            return false;
        }
        const uint32_t RecIdx  = Entries[CursorIdx].RecordEntryIndex;
        const auto     Records = RevisionHistory.InspectRecorder().InspectRecordEntries();
        if (RecIdx >= Records.size())
        {
            return false;
        }
        const auto& Rec       = Records[RecIdx];
        const auto  RawBytes  = RevisionHistory.InspectRecorder().InspectContentRegion().InspectSpan(Rec.RegionByteOffset, Rec.RegionByteLength);
        if (RawBytes.empty())
        {
            return false;
        }

        uint32_t Magic = 0u;
        if (RawBytes.size() >= sizeof(uint32_t))
        {
            std::memcpy(&Magic, RawBytes.data(), sizeof(uint32_t));
        }

        if (Magic == AuthoringSnapshotMagic)
        {
            if (!ApplyAuthoringSnapshot(RawBytes))
            {
                return false;
            }
        }
        else
        {
            if ((RawBytes.size() % sizeof(float)) != 0u)
            {
                return false;
            }
            const std::span<const float> FloatSpan(
                reinterpret_cast<const float*>(RawBytes.data()),
                RawBytes.size() / sizeof(float));
            if (!Asset.Network.ApplyParameterizedUVStream(FloatSpan))
            {
                return false;
            }
        }
    }

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    FramebuffersDirty = true;
    return true;
}

bool UVWorkspaceSession::UndoParameterization()
{
    if (!RevisionHistory.Undo())
    {
        return false;
    }
    if (RestoreUVFromRevisionCursor())
    {
        StatusBannerText = "Undo";
        return true;
    }
    return false;
}

bool UVWorkspaceSession::RedoParameterization()
{
    if (!RevisionHistory.Redo())
    {
        return false;
    }
    if (RestoreUVFromRevisionCursor())
    {
        StatusBannerText = "Redo";
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           .UVSPACE PERSISTENCE (UVCODEC + REVISION)
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceSession::SaveUVSpaceDocument(
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic) const
{
    Codecs::UVSpaceDocumentModel Model {};
    Model.DocumentTitle        = Asset.Network.NetworkTitle;
    Model.GridColumns          = 10u;
    Model.ReferenceScaleMeters = 1.0;

    const auto& Prefs = WorkspaceConfig.InspectUVPreferences();
    for (const uint32_t UDIMCode : Asset.UDIMTiles)
    {
        const int32_t TileU = static_cast<int32_t>((UDIMCode - 1001u) % 10u);
        const int32_t TileV = static_cast<int32_t>((UDIMCode - 1001u) / 10u);
        Model.Tiles.push_back({
            TileU,
            TileV,
            UDIMCode,
            static_cast<double>(Prefs.TargetTexelDensity),
            static_cast<double>(Prefs.IslandMarginPixels),
        });
    }
    if (Model.Tiles.empty())
    {
        Model.Tiles.push_back({ 0, 0, 1001u, static_cast<double>(Prefs.TargetTexelDensity), static_cast<double>(Prefs.IslandMarginPixels) });
    }

    Model.ParameterizedUVPairs = Asset.Network.ExtractParameterizedUVStream();
    return Codecs::UVCodec::Encoder::EncodeToFile(TargetPath, Model, OutDiagnostic, &RevisionHistory);
}

bool UVWorkspaceSession::LoadUVSpaceDocument(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic)
{
    Codecs::UVSpaceDocumentModel LoadedModel {};
    if (!Codecs::UVCodec::Decoder::DecodeFromFile(SourcePath, LoadedModel, OutDiagnostic, &RevisionHistory))
    {
        return false;
    }

    if (!Asset.Network.ApplyParameterizedUVStream(LoadedModel.ParameterizedUVPairs))
    {
        OutDiagnostic = "ParameterizedUVPairs count does not match active PolygonNetwork::Edges.";
        return false;
    }

    Asset.UDIMTiles.clear();
    for (const auto& Tile : LoadedModel.Tiles)
    {
        Asset.UDIMTiles.push_back(Tile.UDIMNumber);
    }
    if (Asset.UDIMTiles.empty())
    {
        Asset.UDIMTiles.push_back(1001u);
    }
    ActiveUDIMNumber = Asset.UDIMTiles.front();

    BaselineUVPairs = LoadedModel.ParameterizedUVPairs;
    if (RevisionHistory.QueryLiveStepCount() <= 1u)
    {
        BaselineSnapshotBytes = CaptureAuthoringSnapshot();
    }
    else
    {
        BaselineSnapshotBytes.clear();
    }

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    RefreshGizmoAnchorsFromSelection();
    FramebuffersDirty = true;
    return true;
}

void UVWorkspaceSession::SyncNetworkToGPU()
{
    if (Rasterizer != nullptr)
    {
        Rasterizer->ConfigureLayerSwatches(Asset.Layers.InspectLayers());
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
        SynchronizeSelectionToGPU();
        LatestMetrics = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
    }
    LatestMetrics.OverlapFaceCount = static_cast<uint32_t>(CollectOverlappingUVFaces().size());
}

void UVWorkspaceSession::AdvanceWorkspaceCamera(float DeltaSeconds) noexcept
{
    SmoothedCameraActive = true;
    const auto& Rig      = ActiveDocument.InspectViewportProjection().InspectSpatialCamera();
    SpatialCameraComponent.AlignFromSpatialRig(Rig, false);

    const float SafeDt = std::clamp(DeltaSeconds, 1.0f / 500.0f, 0.10f);
    const float Aspect = (Framebuffer3DHeight > 0u)
                             ? (static_cast<float>(Framebuffer3DWidth) / static_cast<float>(Framebuffer3DHeight))
                             : (16.0f / 9.0f);
    const bool PreviouslySettling = SpatialCameraComponent.QuerySettlingMotion();
    SpatialCameraComponent.AdvanceKinematics(SafeDt, Aspect);
    if (PreviouslySettling || SpatialCameraComponent.QuerySettlingMotion())
    {
        FramebuffersDirty = true;
    }
}

void UVWorkspaceSession::RefreshViewportFramebuffers(
    uint32_t Viewport3DW,
    uint32_t Viewport3DH,
    uint32_t Canvas2DW,
    uint32_t Canvas2DH)
{
    if (PendingDirective != UVCommandDirective::None)
    {
        (void)ExecuteDirective(ConsumePendingDirective(), nullptr);
    }

    if (Rasterizer == nullptr)
    {
        return;
    }

    // Keep camera motion at full viewport resolution. Defer only the unchanged UV canvas while
    // navigating, so the preview stays sharp without doing a second synchronous GPU readback.
    const uint32_t Render3DW = Viewport3DW;
    const uint32_t Render3DH = Viewport3DH;
    const bool SpatialViewportValid = Viewport3DW >= 16u && Viewport3DH >= 16u;
    const bool PlanarViewportValid  = Canvas2DW >= 16u && Canvas2DH >= 16u;
    const bool SizeChanged3D = SpatialViewportValid && ((Render3DW != Framebuffer3DWidth) || (Render3DH != Framebuffer3DHeight));
    const bool SizeChanged2D = PlanarViewportValid && ((Canvas2DW != Framebuffer2DWidth) || (Canvas2DH != Framebuffer2DHeight));

    if (!FramebuffersDirty && !SizeChanged3D && !SizeChanged2D &&
        !(DeferredPlanarRefresh && !SpatialNavigationActive))
    {
        return;
    }

    auto&       Projection = ActiveDocument.AccessViewportProjection();
    const auto& Rig        = Projection.InspectSpatialCamera();
    const auto  TileCfg    = QueryUDIMTileSetting(ActiveUDIMNumber);

    if (!SmoothedCameraActive)
    {
        SpatialCameraComponent.AlignFromSpatialRig(Rig, true);
    }
    const auto& SmoothedPosture = SpatialCameraComponent.InspectPosture();
    const auto& CamConfig       = SpatialCameraComponent.InspectConfiguration();

    RenderOptions3D.Camera.TargetPivot            = SmoothedPosture.CurrentPivot;
    RenderOptions3D.Camera.YawRadians             = SmoothedPosture.CurrentYawRadians;
    RenderOptions3D.Camera.PitchRadians           = SmoothedPosture.CurrentPitchRadians;
    RenderOptions3D.Camera.DistanceMeters         = SmoothedPosture.CurrentDistanceMeters;
    RenderOptions3D.Camera.VerticalFovDeg         = SmoothedPosture.VerticalFovDegrees;
    RenderOptions3D.Camera.Basis                  = CamConfig.WorldBasis;
    RenderOptions3D.Camera.OrthographicProjection = (CamConfig.ProjectionMode == Components::CameraProjectionMode::Orthographic);
    RenderOptions3D.SeamOverlayEnabled            = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    RenderOptions3D.WireframeEnabled              = WorkspaceConfig.InspectUVPreferences().WireframeOverlayEnabled;

    RenderOptions2D.CheckerEnabled        = WorkspaceConfig.InspectUVPreferences().CheckerOverlayEnabled;
    RenderOptions2D.SeamOverlayEnabled    = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    RenderOptions2D.BackdropPreset        = TileCfg.BackdropPreset;

    const bool ShowGizmo2D = (SelectionMask.CountSelectedFaces() > 0u) || Gizmo2D.QueryDragging();
    const bool ShowGizmo3D = SpatialSelectionActive || (SelectionMask.CountSelectedFaces() > 0u) || Gizmo3D.QueryDragging();

    if (Viewport3DW >= 16u && Viewport3DH >= 16u)
    {
        if (Rasterizer->Rasterize3DViewport(Render3DW, Render3DH, RenderOptions3D, ShowGizmo3D ? &Gizmo3D : nullptr, &Framebuffer3DRGBA))
        {
            Framebuffer3DWidth  = Render3DW;
            Framebuffer3DHeight = Render3DH;
            (void)OpticsEncoder.ExecuteGPUOptics(Render3DW, Render3DH, CamConfig, {}, Framebuffer3DRGBA);
        }
    }

    if (SpatialNavigationActive)
    {
        DeferredPlanarRefresh = true;
    }
    else if (Canvas2DW >= 16u && Canvas2DH >= 16u)
    {
        const float TileSpanU = static_cast<float>(std::max<std::size_t>(1u, Asset.UDIMTiles.size()));
        Projection.ConfigurePlanarDomainSpan(TileSpanU, 1.0f);
        Projection.EvaluateAspectLockedPlanarBounds(Canvas2DW, Canvas2DH, RenderOptions2D.VisibleMinUV, RenderOptions2D.VisibleMaxUV);

        if (Rasterizer->Rasterize2DUVCanvas(Canvas2DW, Canvas2DH, RenderOptions2D, ShowGizmo2D ? &Gizmo2D : nullptr, &Framebuffer2DRGBA))
        {
            Framebuffer2DWidth  = Canvas2DW;
            Framebuffer2DHeight = Canvas2DH;
            DeferredPlanarRefresh = false;
        }
    }
    else if (!SpatialNavigationActive)
    {
        DeferredPlanarRefresh = false;
    }

    FramebuffersDirty = false;
}

} // namespace Frontier::Workspaces
