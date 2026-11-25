/*============================================================================================================================================
                                                         AUTOSPACELIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Standalone zero-Editor runtime lifecycle implementation for project AutoSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/AutoSpaceLifecycle.h"

#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Codecs/VolumetricSpaceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace AutoSpace
{

namespace
{

[[nodiscard]] std::filesystem::path LocateFrontierRepositoryRoot(const std::filesystem::path& StartDir)
{
    std::error_code Ec {};
    std::filesystem::path Cursor = StartDir.empty() ? std::filesystem::current_path(Ec) : StartDir;
    for (int Depth = 0; Depth < 8; ++Depth)
    {
        if (std::filesystem::exists(Cursor / "Content" / "Environment" / "Skies" / "ClearSkyDaylight.VolumetricSpace", Ec))
        {
            return Cursor;
        }
        if (!Cursor.has_parent_path() || Cursor.parent_path() == Cursor)
        {
            break;
        }
        Cursor = Cursor.parent_path();
    }
    return std::filesystem::current_path(Ec);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

AutoSpaceLifecycle::~AutoSpaceLifecycle() noexcept
{
    Finalize();
}

bool AutoSpaceLifecycle::Initialize(
    const std::filesystem::path& ProjectRootPath,
    bool                         ForceHeadlessWindow,
    std::string&                 OutDiagnostic)
{
    Finalize();

    Policies = BuildAutoSpacePolicyBundle();
    Metrics  = {};

    std::filesystem::path EffectiveProjectRoot = ProjectRootPath;
    if (EffectiveProjectRoot.empty())
    {
        EffectiveProjectRoot = std::filesystem::path("Projects") / "AutoSpace";
    }
    {
        std::error_code RootEc {};
        if (!std::filesystem::exists(EffectiveProjectRoot / "Content" / "Worlds" / "MainWorld.WorldSpace", RootEc))
        {
#if defined(AUTOSPACE_PROJECT_ROOT_DIR)
            const std::filesystem::path CompiledProjectRoot(AUTOSPACE_PROJECT_ROOT_DIR);
            if (std::filesystem::exists(CompiledProjectRoot / "Content" / "Worlds" / "MainWorld.WorldSpace", RootEc))
            {
                EffectiveProjectRoot = CompiledProjectRoot;
            }
#endif
        }
    }

    if (!RuntimeLoop.BootFromProjectRoot(EffectiveProjectRoot, OutDiagnostic, ForceHeadlessWindow))
    {
        return false;
    }

    Policies.ExecutionPolicy = RuntimeLoop.InspectExecutionPolicy();
    Policies.GameplayPolicy.FixedSimulationHz =
        static_cast<uint32_t>(std::lround(1.0 / std::max(0.001, Policies.ExecutionPolicy.FixedStepSeconds)));
    Frontier::SimulationSpace::Policies::GameplayRulePolicyEngine::ClampToSafeBounds(Policies.GameplayPolicy);

    // Load single-level MainWorld.WorldSpace (blank scene: Sun & Sky only, zero geometry placements)
    const std::filesystem::path StartupWorldPath = EffectiveProjectRoot / "Content" / "Worlds" / "MainWorld.WorldSpace";
    std::string WorldDiag {};
    if (!WorldHost.LoadWorldSpaceFile(StartupWorldPath, WorldDiag))
    {
        OutDiagnostic = "AutoSpaceLifecycle failed to load startup MainWorld.WorldSpace (" +
                        StartupWorldPath.generic_string() + "): " + WorldDiag;
        RuntimeLoop.Finalize();
        return false;
    }

    // Verify Sun + Sky documents referenced by MainWorld.WorldSpace
    Frontier::Codecs::WorldSpaceDocumentModel WorldDoc {};
    if (Frontier::Codecs::WorldSpaceDecoder::DecodeFromFile(StartupWorldPath, WorldDoc, WorldDiag))
    {
        const std::filesystem::path RepoRoot = LocateFrontierRepositoryRoot(EffectiveProjectRoot);
        if (!WorldDoc.EnvironmentVolumetricPath.empty())
        {
            Frontier::Codecs::VolumetricSpaceDocumentModel VolDoc {};
            std::string                                    VolDiag {};
            if (Frontier::Codecs::VolumetricSpaceDecoder::DecodeFromFile(
                    RepoRoot / WorldDoc.EnvironmentVolumetricPath, VolDoc, VolDiag))
            {
                WorldHost.AccessActiveScene().Atmosphere  = VolDoc.Atmosphere;
                WorldHost.AccessActiveScene().SkyRadiance = VolDoc.FallbackSkyRadiance;
                Metrics.SkyAtmosphereEnabled              = VolDoc.Atmosphere.Medium.AtmosphereEnabled;
            }
        }
        if (!WorldDoc.SolarPhotometryPath.empty())
        {
            Frontier::Codecs::PhotometrySpaceDocumentModel SunDoc {};
            std::string                                    SunDiag {};
            if (Frontier::Codecs::PhotometrySpaceDecoder::DecodeFromFile(
                    RepoRoot / WorldDoc.SolarPhotometryPath, SunDoc, SunDiag))
            {
                WorldHost.AccessActiveScene().Atmosphere.Solar = SunDoc.SolarEphemeris;
                WorldHost.AccessActiveScene().Sun              = SunDoc.SolarSource;
                Metrics.SunEnabled                             = SunDoc.SolarEphemeris.SolarEnabled;
            }
        }
        Metrics.LoadedGeometryInstances = static_cast<uint32_t>(WorldDoc.GeometryPlacements.size());
        Metrics.MainWorldLoaded         = true;
    }

    const auto Reading            = RuntimeLoop.InspectReading();
    Metrics.BootLoadedFromDisk    = Reading.BootLoadedFromDisk;
    Metrics.WorkerChannelsRunning = Reading.CadenceMetrics.WorkerChannelsActive;
    Metrics.AssetStreamRunning    = Reading.CadenceMetrics.AssetStreamActive;
    Initialized                   = true;
    OutDiagnostic.clear();
    return true;
}

uint32_t AutoSpaceLifecycle::RunBoundedTicks(uint32_t MaximumOuterTicks, double WallStepSeconds) noexcept
{
    if (!Initialized)
    {
        return 0u;
    }
    return RuntimeLoop.ExecuteBoundedFixedLoop(
        MaximumOuterTicks,
        WallStepSeconds,
        &AutoSpaceLifecycle::RouteFixedStep,
        &AutoSpaceLifecycle::RoutePresentationStep,
        this);
}

bool AutoSpaceLifecycle::VerifyProjectContracts(std::string& OutDiagnostic) const
{
    using namespace Frontier::SimulationSpace::Policies;

    if (!Metrics.MainWorldLoaded)
    {
        OutDiagnostic = "AutoSpace MainWorld.WorldSpace was not loaded.";
        return false;
    }
    if (!Metrics.SunEnabled || !Metrics.SkyAtmosphereEnabled)
    {
        OutDiagnostic = "AutoSpace MainWorld.WorldSpace must have both Sun and Sky enabled.";
        return false;
    }
    if (Metrics.LoadedGeometryInstances != 0u)
    {
        OutDiagnostic = "AutoSpace blank world must contain zero geometry placements (not even a ground plane).";
        return false;
    }

    std::array<std::byte, GameplayRulePolicy::WirePayloadBytes> RuleWire{};
    if (!GameplayRulePolicyEngine::EncodePolicyWireBytes(Policies.GameplayPolicy, RuleWire))
    {
        OutDiagnostic = "AutoSpace GameplayRulePolicy wire encode failed.";
        return false;
    }
    GameplayRulePolicy DecodedRule{};
    if (!GameplayRulePolicyEngine::DecodePolicyWireBytes(RuleWire, DecodedRule, OutDiagnostic))
    {
        return false;
    }
    if (DecodedRule.FixedSimulationHz != Policies.GameplayPolicy.FixedSimulationHz)
    {
        OutDiagnostic = "AutoSpace GameplayRulePolicy wire FixedSimulationHz mismatch.";
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool AutoSpaceLifecycle::Suspend() noexcept
{
    return Initialized && RuntimeLoop.Suspend();
}

bool AutoSpaceLifecycle::Resume() noexcept
{
    return Initialized && RuntimeLoop.Resume();
}

void AutoSpaceLifecycle::Finalize() noexcept
{
    RuntimeLoop.Finalize();
    WorldHost.ClearWorld();
    Initialized = false;
}

AutoSpaceLoopMetrics AutoSpaceLifecycle::InspectMetrics() const noexcept
{
    return Metrics;
}

void AutoSpaceLifecycle::RouteFixedStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* Payload) noexcept
{
    if (auto* Self = static_cast<AutoSpaceLifecycle*>(Payload))
    {
        Self->ExecuteFixedSimulationStep(FixedDeltaSeconds, FixedStepOrdinal);
    }
}

void AutoSpaceLifecycle::RoutePresentationStep(double InterpolationAlpha, double WallDeltaSeconds, void* Payload) noexcept
{
    if (auto* Self = static_cast<AutoSpaceLifecycle*>(Payload))
    {
        Self->ExecutePresentationStep(InterpolationAlpha, WallDeltaSeconds);
    }
}

void AutoSpaceLifecycle::ExecuteFixedSimulationStep(double FixedDeltaSeconds, uint64_t /*FixedStepOrdinal*/) noexcept
{
    (void)WorldHost.StepRuntimeWorld(FixedDeltaSeconds);
    ++Metrics.TotalFixedStepsExecuted;
    Metrics.SimulatedElapsedSeconds += FixedDeltaSeconds;
}

void AutoSpaceLifecycle::ExecutePresentationStep(double InterpolationAlpha, double /*WallDeltaSeconds*/) noexcept
{
    ++Metrics.PresentationTicksCount;
    Metrics.LastInterpolationAlpha = InterpolationAlpha;
}

} // namespace AutoSpace
