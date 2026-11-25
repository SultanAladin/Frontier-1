/*============================================================================================================================================
                                                           RUNTIMEWORLDHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone Shipping & Production RuntimeWorldHost with zero Editor/ImGui dependencies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/RuntimeWorldHost.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Frontier::Lifecycle
{

namespace
{

[[nodiscard]] std::string LowercaseAsciiString(std::string_view Input)
{
    std::string Output(Input.size(), '\0');
    for (size_t i = 0u; i < Input.size(); ++i)
    {
        Output[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Input[i])));
    }
    return Output;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RUNTIME WORLD HOST
//------------------------------------------------------------------------------------------------------------------------

RuntimeWorldHost::~RuntimeWorldHost() noexcept
{
    Finalize();
}

bool RuntimeWorldHost::Initialize() noexcept
{
    Active    = true;
    Suspended = false;
    return true;
}

void RuntimeWorldHost::Tick(double Δt) noexcept
{
    if (!Active || Suspended)
    {
        return;
    }

    const float ClampedStep = (Δt > 0.0) ? static_cast<float>(Δt) : 0.0f;
    LastStepReport          = StepRuntimeWorld(ClampedStep);
    ++CompletedStepCount;
}

void RuntimeWorldHost::Suspend() noexcept
{
    Suspended = true;
}

void RuntimeWorldHost::Resume() noexcept
{
    Suspended = false;
}

void RuntimeWorldHost::Finalize() noexcept
{
    ClearWorld();
    Active    = false;
    Suspended = false;
}

void RuntimeWorldHost::ClearWorld() noexcept
{
    ResidentHandles.clear();
    SceneSyncBridge.ClearBindings();
    WorldRegister.ClearAll();
    ActiveScene        = GeometricRaster::WorldSceneSpecification{};
    LastStepReport     = RuntimeWorldStepReport{};
    CompletedStepCount = 0ull;
}

bool RuntimeWorldHost::LoadWorldSpaceFile(
    const std::filesystem::path& WorldSpacePath,
    std::string&                 OutDiagnostic)
{
    Codecs::WorldSpaceDocumentModel Model{};
    if (!Codecs::WorldSpaceDecoder::DecodeFromFile(WorldSpacePath, Model, OutDiagnostic, nullptr))
    {
        return false;
    }

    WorldRegister.ReserveInstanceCapacity(
        static_cast<uint32_t>(Model.GeometryPlacements.size() + Model.MeasuredLuminairePlacements.size() + 16u));

    for (const Codecs::WorldGeometryPlacementEntry& Placement : Model.GeometryPlacements)
    {
        World::SurfaceInstanceParameters Params{};
        Params.GeometryDocumentHash64 = Codecs::InstanceEncoder::ComputeDocumentHash64(Placement.GeometrySpacePath);
        Params.MaterialDocumentHash64 = Codecs::InstanceEncoder::ComputeDocumentHash64(Placement.PrimaryMaterialPath);
        Params.PrimitiveShapeOrdinal  = Placement.PrimitiveShapeOrdinal;
        Params.CastShadows            = Placement.CastShadows;
        Params.VisibilityEnabled      = Placement.VisibilityEnabled;

        World::SurfaceInstance Surface(WorldRegister, Params);
        const World::InstanceToken Token = Surface.QueryToken();
        (void)WorldRegister.AssignWorldTransform(
            Token,
            Placement.CenterWorld.X,
            Placement.CenterWorld.Y,
            Placement.CenterWorld.Z,
            0.0f,
            0.0f,
            Placement.RotationZDegrees,
            Placement.DimensionsMeters.X,
            Placement.DimensionsMeters.Y,
            Placement.DimensionsMeters.Z);

        GeometricRaster::RigidClusterInstance Cluster{};
        Cluster.WorldTranslation    = Placement.CenterWorld;
        Cluster.PreviousTranslation = Placement.CenterWorld;
        Cluster.RotationZDegrees    = Placement.RotationZDegrees;
        Cluster.NonUniformScale     = {
            Placement.DimensionsMeters.X,
            Placement.DimensionsMeters.Y,
            Placement.DimensionsMeters.Z
        };
        Cluster.UniformScale        = (Placement.DimensionsMeters.X + Placement.DimensionsMeters.Y + Placement.DimensionsMeters.Z) / 3.0f;

        const uint32_t ClusterIndex = static_cast<uint32_t>(ActiveScene.ClusterInstances.size());
        ActiveScene.ClusterInstances.push_back(Cluster);
        SceneSyncBridge.BindSurfaceToClusterInstance(Token, ClusterIndex);
        ResidentHandles.push_back(std::move(Surface));
    }

    for (const Codecs::WorldMeasuredLuminairePlacement& Lamp : Model.MeasuredLuminairePlacements)
    {
        World::LuminaireInstanceParameters Params{};
        Params.LuminaireFormOrdinal = 3u;
        Params.PeakLuminanceR       = Lamp.PeakIntensityRgb.R;
        Params.PeakLuminanceG       = Lamp.PeakIntensityRgb.G;
        Params.PeakLuminanceB       = Lamp.PeakIntensityRgb.B;
        Params.RangeMeters          = Lamp.RangeMeters;
        Params.VisibilityEnabled    = true;

        World::LuminaireInstance Luminaire(WorldRegister, Params);
        (void)WorldRegister.AssignWorldTransform(
            Luminaire.QueryToken(),
            Lamp.PositionWorld.X,
            Lamp.PositionWorld.Y,
            Lamp.PositionWorld.Z);
        ResidentHandles.push_back(std::move(Luminaire));
    }

    OutDiagnostic.clear();
    return true;
}

World::InstanceToken RuntimeWorldHost::MountCookedInstanceModel(
    const Codecs::InstanceDocumentModel& DocumentModel)
{
    const bool Visible = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableVisibility);
    const bool CastShadows = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableShadowProjection);
    const bool TickEnabled = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableSimulationTick);

    uint64_t GeometryHash = 0ull;
    uint64_t MaterialHash = 0ull;
    uint64_t ParticleHash = 0ull;
    uint64_t AcousticHash = 0ull;
    for (const Codecs::InstanceSpaceSlotEntry& Slot : DocumentModel.SpaceSlots)
    {
        if (Slot.FormatExtension == ".GeometrySpace")
        {
            GeometryHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".MaterialSpace")
        {
            MaterialHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".ParticleSpace")
        {
            ParticleHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".Acoustic")
        {
            AcousticHash = Slot.DocumentHash64;
        }
    }

    World::CategoryInstanceHandle Mounted{};
    switch (DocumentModel.Category)
    {
        case World::InstanceCategory::SolarBody:
        {
            World::SolarInstanceParameters Params{};
            Params.CycleAnimationEnabled = TickEnabled;
            Params.VisibilityEnabled     = Visible;
            World::SolarInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::LunarBody:
        {
            World::LunarInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::LunarInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::AtmosphereMedium:
        {
            World::AtmosphereInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::AtmosphereInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::CloudDeck:
        {
            World::CloudInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::CloudInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::Luminaire:
        {
            World::LuminaireInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::LuminaireInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::ParticleCloud:
        {
            World::ParticleInstanceParameters Params{};
            Params.ParticleDocumentHash64 = ParticleHash;
            Params.SimulationActive       = Visible && TickEnabled;
            World::ParticleInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::ForceFieldVolume:
        {
            World::ForceFieldInstanceParameters Params{};
            Params.FieldActive = Visible && TickEnabled;
            World::ForceFieldInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::TriggerZone:
        {
            World::TriggerInstanceParameters Params{};
            Params.SensorActive = Visible && TickEnabled;
            World::TriggerInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::AcousticSource:
        {
            World::AcousticInstanceParameters Params{};
            Params.AcousticDocumentHash64 = AcousticHash;
            Params.PlaybackActive         = Visible;
            World::AcousticInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::CameraOptics:
        {
            World::CameraInstanceParameters Params{};
            Params.PrimaryViewActive = Visible;
            World::CameraInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
        case World::InstanceCategory::SurfaceGeometry:
        default:
        {
            World::SurfaceInstanceParameters Params{};
            Params.GeometryDocumentHash64 = GeometryHash;
            Params.MaterialDocumentHash64 = MaterialHash;
            Params.CastShadows            = CastShadows;
            Params.VisibilityEnabled      = Visible;
            World::SurfaceInstance Instance(WorldRegister, Params);
            Mounted = std::move(Instance);
            break;
        }
    }

    if (!Mounted.QueryValid())
    {
        return World::InstanceToken{};
    }

    const World::InstanceToken Token = Mounted.QueryToken();
    (void)WorldRegister.AssignWorldTransform(
        Token,
        DocumentModel.PositionMeters[0],
        DocumentModel.PositionMeters[1],
        DocumentModel.PositionMeters[2],
        DocumentModel.RotationDegrees[0],
        DocumentModel.RotationDegrees[1],
        DocumentModel.RotationDegrees[2],
        DocumentModel.ScaleScalar[0],
        DocumentModel.ScaleScalar[1],
        DocumentModel.ScaleScalar[2]);

    if (DocumentModel.Category == World::InstanceCategory::SurfaceGeometry)
    {
        GeometricRaster::RigidClusterInstance Cluster{};
        Cluster.WorldTranslation    = {
            DocumentModel.PositionMeters[0],
            DocumentModel.PositionMeters[1],
            DocumentModel.PositionMeters[2]
        };
        Cluster.PreviousTranslation = Cluster.WorldTranslation;
        Cluster.RotationZDegrees    = DocumentModel.RotationDegrees[2];
        Cluster.NonUniformScale     = {
            DocumentModel.ScaleScalar[0],
            DocumentModel.ScaleScalar[1],
            DocumentModel.ScaleScalar[2]
        };
        Cluster.UniformScale        =
            (DocumentModel.ScaleScalar[0] + DocumentModel.ScaleScalar[1] + DocumentModel.ScaleScalar[2]) / 3.0f;

        const uint32_t ClusterIndex = static_cast<uint32_t>(ActiveScene.ClusterInstances.size());
        ActiveScene.ClusterInstances.push_back(Cluster);
        SceneSyncBridge.BindSurfaceToClusterInstance(Token, ClusterIndex);
    }

    ResidentHandles.push_back(std::move(Mounted));
    return Token;
}

World::InstanceToken RuntimeWorldHost::MountCookedInstanceFile(
    const std::filesystem::path& InstancePath,
    std::string&                 OutDiagnostic)
{
    Codecs::InstanceDocumentModel Decoded{};
    if (!Codecs::InstanceDecoder::DecodeFromFile(InstancePath, Decoded, OutDiagnostic, nullptr))
    {
        return World::InstanceToken{};
    }

    return MountCookedInstanceModel(Decoded);
}

uint32_t RuntimeWorldHost::MountCookedDirectory(
    const std::filesystem::path& DirectoryPath,
    std::string&                 OutDiagnostic)
{
    std::error_code DirError{};
    if (!std::filesystem::exists(DirectoryPath, DirError) ||
        !std::filesystem::is_directory(DirectoryPath, DirError))
    {
        OutDiagnostic = "Cooked instance directory does not exist.";
        return 0u;
    }

    std::vector<std::filesystem::path> CandidateFiles{};
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(DirectoryPath, DirError))
    {
        if (!Entry.is_regular_file())
        {
            continue;
        }

        const std::string LowerExt = LowercaseAsciiString(Entry.path().extension().string());
        if ((LowerExt == ".instance") || (LowerExt == ".instancespace"))
        {
            CandidateFiles.push_back(Entry.path());
        }
    }

    std::sort(CandidateFiles.begin(), CandidateFiles.end());

    uint32_t MountedCount = 0u;
    for (const std::filesystem::path& FilePath : CandidateFiles)
    {
        std::string FileDiagnostic{};
        if (MountCookedInstanceFile(FilePath, FileDiagnostic).QueryValid())
        {
            ++MountedCount;
        }
    }

    OutDiagnostic.clear();
    return MountedCount;
}

RuntimeWorldStepReport RuntimeWorldHost::StepRuntimeWorld(float Δt) noexcept
{
    RuntimeWorldStepReport Report{};

    // 1. Stream ONLY rebuilt transform slots into WorldSceneSpecification::ClusterInstances before clearing RequireRebuild
    Report.SyncedClusterTransforms = SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, ActiveScene);

    // 2. Execute tight cache-coherent SoA simulation loops (clears RequireRebuild after evaluation)
    Report.LoopMetrics = World::ExecuteWorldSimulationSequence(WorldRegister, Δt, true);

    // 3. Synchronize evaluated Solar, Lunar, Atmosphere & Cloud registers into ActiveScene.Atmosphere
    Report.AtmosphereMutated = World::WorldSceneSyncBridge::SynchronizeAtmosphereToScene(
        WorldRegister,
        Report.LoopMetrics,
        ActiveScene,
        Δt);

    Report.SceneMutated = (Report.SyncedClusterTransforms > 0u) || Report.AtmosphereMutated;
    return Report;
}

} // namespace Frontier::Lifecycle
