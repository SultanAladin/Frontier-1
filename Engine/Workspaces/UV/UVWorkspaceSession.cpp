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
    const bool Uploaded = Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    if (Uploaded)
    {
        SynchronizeSelectionToGPU();
        LatestMetrics     = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
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
    if (ActivePreset != GeometrySpace::SampleAssetPreset::ControlledIntegrator)
    {
        const auto& InitialSpan = Asset.Layers.InspectPartitionSpan(Asset.Network, SelectedLayerOrdinal, RootInstanceToken);
        SelectionMask.SelectFacesFromList(InitialSpan.FaceIndices, false);
    }

    RevisionHistory.Reset();
    BaselineUVPairs      = Asset.Network.ExtractParameterizedUVStream();
    RevisionStampCounter = 1ull;

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
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
        SynchronizeSelectionToGPU();
    }
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
    return NewOrd;
}

void UVWorkspaceSession::RenameSelectedLayer(std::string_view NewTitle)
{
    if (SelectedLayerOrdinal == 0u || NewTitle.empty())
    {
        return;
    }
    if (auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal))
    {
        Layer->LayerTitle = std::string(NewTitle);
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
    SyncNetworkToGPU();
    return true;
}

void UVWorkspaceSession::AssignSelectedLayerTint(Mathematics::Scalar3D LinearTint) noexcept
{
    if (SelectedLayerOrdinal == 0u)
    {
        return;
    }
    if (auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal))
    {
        Layer->LinearTintRGB = LinearTint;
        FramebuffersDirty    = true;
    }
}

void UVWorkspaceSession::AdjustSelectedLayerShare(float RequestedSharePercent) noexcept
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
    FramebuffersDirty         = true;
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
        FinalizeInteractiveUVRevision("Move priority layer to UDIM");
    }
    else
    {
        Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
        FramebuffersDirty = true;
    }
    return true;
}

void UVWorkspaceSession::ApplyMatCapToActiveSelection(uint8_t MatCapSlot)
{
    auto TargetFaces = SelectionMask.CollectSelectedFaces();
    if (TargetFaces.empty())
    {
        const auto& Ref = Asset.Layers.InspectPartitionSpan(Asset.Network, SelectedLayerOrdinal, RootInstanceToken);
        TargetFaces     = Ref.FaceIndices;
    }
    for (const uint32_t FaceIdx : TargetFaces)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            Asset.Network.Faces[FaceIdx].Shading.MatCapSlot = static_cast<uint8_t>(MatCapSlot & 15u);
        }
    }
    ++Asset.Network.ShadingRevision;
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
    float Specular)
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
    FramebuffersDirty      = true;
    return NextCode;
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
    if (SelectionMode == UVSelectionMode::Layers)
    {
        (void)Rasterizer->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::SelectByLayer, SelectedLayerOrdinal, false);
    }
    else
    {
        Rasterizer->UploadSelectionBitmaskWords(SelectionMask.InspectPackedFaceWords());
    }
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
            std::snprintf(Msg, sizeof(Msg), "Marked %u Dihedral Seam Edges (>= %.0f deg)", Marked, SharpAngleThreshold);
            RecordUVRevisionStep("MarkSharpSeams");
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
            std::snprintf(Msg, sizeof(Msg), "Cleared & Welded %u Seam Edges", Welded);
            RecordUVRevisionStep("ClearSelectedSeams");
            break;
        }
        case UVCommandDirective::DeriveSeamsFromIslands:
        {
            const uint32_t Seams = UVAuthoringOperations::DeriveSeamsFromIslands(Asset.Network);
            std::snprintf(Msg, sizeof(Msg), "Derived %u Seam Edges from UV Islands", Seams);
            RecordUVRevisionStep("DeriveSeamsFromIslands");
            break;
        }
        case UVCommandDirective::PinSelectedUVs:
        {
            const uint32_t Pinned = UVAuthoringOperations::AssignPinLockOnFaces(Asset.Network, SelectionMask.CollectSelectedFaces(), true);
            std::snprintf(Msg, sizeof(Msg), "Pinned %u UV Corners (EnablePinLock)", Pinned);
            RecordUVRevisionStep("PinSelectedUVs");
            break;
        }
        case UVCommandDirective::UnpinSelectedUVs:
        {
            const uint32_t Unpinned = UVAuthoringOperations::AssignPinLockOnFaces(Asset.Network, SelectionMask.CollectSelectedFaces(), false);
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
            SyncNetworkToGPU();
            std::snprintf(Msg, sizeof(Msg), "GPU Distortion Inspection: %u Overlapping Faces", LatestMetrics.OverlapFaceCount);
            StatusBannerText = Msg;
            return true;
        }
        case UVCommandDirective::FitActiveSelection:
        {
            FitSelectionOrNetwork();
            StatusBannerText = "Framed Active Selection in 2D & 3D Viewports";
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

void UVWorkspaceSession::RecordUVRevisionStep(std::string_view OperationTag)
{
    const std::vector<float> UVStream = Asset.Network.ExtractParameterizedUVStream();
    const std::span<const std::byte> BytePayload(
        reinterpret_cast<const std::byte*>(UVStream.data()),
        UVStream.size() * sizeof(float));

    const ParametricOperationParameters Params = OperationRecorder::ConstructUVTileTransform(
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) % 10u),
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) / 10u),
        0.0,
        0.0,
        1.0,
        0.0,
        WorkspaceConfig.InspectUVPreferences().TargetTexelDensity,
        OperationTag);

    (void)RevisionHistory.RecordParametricOperation(Params, RevisionStampCounter++, BytePayload);
}

bool UVWorkspaceSession::RestoreUVFromRevisionCursor()
{
    const auto&    History   = RevisionHistory.InspectHistory();
    const uint32_t CursorIdx = History.QueryCursorIndex();
    const uint32_t RootIdx   = History.QueryRootIndex();

    if (CursorIdx == RootIdx)
    {
        if (!Asset.Network.ApplyParameterizedUVStream(BaselineUVPairs))
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
        const auto  RawSlice  = RevisionHistory.InspectRecorder().InspectContentRegion().InspectSpan(Rec.RegionByteOffset, Rec.RegionByteLength);
        if (RawSlice.empty() || (RawSlice.size() % sizeof(float)) != 0u)
        {
            return false;
        }
        const std::span<const float> FloatSpan(
            reinterpret_cast<const float*>(RawSlice.data()),
            RawSlice.size() / sizeof(float));
        if (!Asset.Network.ApplyParameterizedUVStream(FloatSpan))
        {
            return false;
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
    return RestoreUVFromRevisionCursor();
}

bool UVWorkspaceSession::RedoParameterization()
{
    if (!RevisionHistory.Redo())
    {
        return false;
    }
    return RestoreUVFromRevisionCursor();
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
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
        SynchronizeSelectionToGPU();
        LatestMetrics = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
    }
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

    if (Viewport3DW >= 16u && Viewport3DH >= 16u)
    {
        if (Rasterizer->Rasterize3DViewport(Render3DW, Render3DH, RenderOptions3D, &Gizmo3D, &Framebuffer3DRGBA))
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
