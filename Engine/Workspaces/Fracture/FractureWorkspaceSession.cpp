/*============================================================================================================================================
                                                       FRACTUREWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 FractureWorkspaceSession implementation: 6 calibrated presets, instant partition, Jolt simulation lifecycle & diagnostics sampling

#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::Fracture
{

namespace
{

const std::array<FracturePresetEntry, FractureWorkspaceSession::PresetCount> BuiltInPresets {{
    {
        "Concrete Column",
        "Boussinesq + Hertz cone · 3.2 kJ",
        GeometrySpace::Fracture::FractureSolidCategory::Monolith,
        "concrete",
        GeometrySpace::Fracture::FractureSolverMethod::FastRuntimeBoussinesq,
        3200.0, 28u, { 0.05f, -0.48f, 0.25f }, { 0.0f, -1.0f, 0.0f }
    },
    {
        "Baked Fractography",
        "3D Navier-Cauchy + Bouchaud relief",
        GeometrySpace::Fracture::FractureSolidCategory::Monolith,
        "granite",
        GeometrySpace::Fracture::FractureSolverMethod::EngineeringBakedSolid,
        4200.0, 24u, { -0.06f, -0.48f, 0.30f }, { 0.0f, -1.0f, 0.0f }
    },
    {
        "Tempered Curtain",
        "Residual core-skin dicing · 46 kJ/m3",
        GeometrySpace::Fracture::FractureSolidCategory::CurtainPane,
        "tempered",
        GeometrySpace::Fracture::FractureSolverMethod::LefmWavefront,
        480.0, 42u, { 0.0f, -0.032f, 0.08f }, { 0.0f, -1.0f, 0.0f }
    },
    {
        "Spruce Timber Beam",
        "12:1 orthotropic tracheid splinters",
        GeometrySpace::Fracture::FractureSolidCategory::TimberBeam,
        "wood",
        GeometrySpace::Fracture::FractureSolverMethod::GriffithStressBudget,
        2100.0, 22u, { 0.0f, -0.14f, 0.04f }, { 0.0f, -1.0f, 0.0f }
    },
    {
        "Ductile Polymer Cone",
        "ABS yielding · high cohesive bonds",
        GeometrySpace::Fracture::FractureSolidCategory::Cone,
        "plastic",
        GeometrySpace::Fracture::FractureSolverMethod::ClusteredVoronoi,
        2900.0, 20u, { 0.0f, -0.42f, 0.05f }, { 0.0f, -1.0f, 0.0f }
    },
    {
        "Dendritic Boulder",
        "Spall shell + lightning trunk",
        GeometrySpace::Fracture::FractureSolidCategory::Boulder,
        "granite",
        GeometrySpace::Fracture::FractureSolverMethod::DendriticSpallHull,
        4600.0, 28u, { 0.04f, -0.58f, 0.12f }, { 0.0f, -1.0f, 0.0f }
    }
}};

void AppendSample(std::vector<float>& Ring, float Value, size_t MaxCap = 96u)
{
    Ring.push_back(Value);
    if (Ring.size() > MaxCap)
    {
        Ring.erase(Ring.begin(), Ring.begin() + static_cast<ptrdiff_t>(Ring.size() - MaxCap));
    }
}

} // namespace

FractureWorkspaceSession::FractureWorkspaceSession()
{
    DiagnosticsSession.Visible = false;
    ApplyPreset(0u);
}

const std::array<FracturePresetEntry, FractureWorkspaceSession::PresetCount>& FractureWorkspaceSession::InspectPresets() noexcept
{
    return BuiltInPresets;
}

void FractureWorkspaceSession::ApplyPreset(uint32_t PresetIndex) noexcept
{
    ActivePresetIndex = PresetIndex % PresetCount;
    const auto& P     = BuiltInPresets[ActivePresetIndex];
    SolidCategory     = P.Solid;
    MaterialId        = P.MaterialId;
    SolverMethod      = P.Solver;
    ImpactEnergyJ     = P.EnergyJoules;
    FragmentCeiling   = P.Ceiling;
    ImpactPointLocal  = P.ImpactPoint;
    ImpactNormalLocal = P.ImpactNormal;
    RenderOptions.SelectedFragmentIndex = -1;
    RebuildSolidAndPartition();
}

void FractureWorkspaceSession::RebuildSolidAndPartition() noexcept
{
    SourceSolid = GeometrySpace::Fracture::ConvexPartitioning::ConstructSolidPolyhedron(
        SolidCategory, { 1.0f, 1.0f, 1.0f }, Seed);
    const auto Metrics = GeometrySpace::Fracture::ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
    RenderOptions.SolidWorldOffsetM = { 0.0f, 0.0f, std::max(0.45f, -Metrics.BoundsMin.Z + 0.02f) };
    Camera.TargetWorld = { 0.0f, 0.0f, RenderOptions.SolidWorldOffsetM.Z };

    ExecuteFracturePartitionOnly();
    if (ExecutionPhase != ExecutionStripPhase::Edit)
    {
        StartOrResetSimulation(Playing);
    }
}

void FractureWorkspaceSession::ExecuteFracturePartitionOnly() noexcept
{
    GeometrySpace::Fracture::FractureSpecification Spec {};
    Spec.Solver            = SolverMethod;
    Spec.MaterialId        = MaterialId.c_str();
    Spec.Seed              = Seed;
    Spec.Ceiling           = FragmentCeiling;
    Spec.EnergyJoules      = ImpactEnergyJ;
    Spec.MinimumSizeM      = MinimumSizeM;
    Spec.GrainAngleDeg     = GrainAngleDeg;
    Spec.BondStrengthScale = BondStrengthScale;
    Spec.ImpactPoint       = ImpactPointLocal;
    Spec.ImpactNormal      = ImpactNormalLocal;

    PartitionResult = GeometrySpace::Fracture::ConvexPartitioning::ExecuteFracturePartition(SourceSolid, Spec);
    HasFractured    = true;
    ++RevisionCounter;
}

void FractureWorkspaceSession::StartOrResetSimulation(bool AutoPlay) noexcept
{
    if (!HasFractured)
    {
        ExecuteFracturePartitionOnly();
    }

    SimulationSpace::PhysicsSpace::FractureSimulationParameters Params {};
    const auto& Mat = ActiveMaterialSpec();
    Params.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
    Params.GroundPlaneZ       = 0.0f;
    Params.Friction           = Mat.Friction;
    Params.Restitution        = Mat.Restitution;
    Params.EnableBonds        = (ExecutionMode != GeometrySpace::Fracture::FractureExecutionMode::Realtime);
    Params.ApplyImpactImpulse = true;

    (void)SimRunner.LoadPartitionResult(PartitionResult, Params);
    HasSimulatedData = SimRunner.QueryLoaded();
    ElapsedSeconds   = 0.0;
    Playing          = AutoPlay;
    KineticCurveSamples.clear();
    TimelineMarkers.clear();
    TimelineMarkers.push_back({ 0.02f, { 255u, 152u, 72u, 255u }, "Impact" });
    AppendSample(KineticCurveSamples, static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ), 240u);
    ++RevisionCounter;
}

void FractureWorkspaceSession::TransitionExecutionPhase(ExecutionStripPhase NextPhase) noexcept
{
    if (NextPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Edit;
        Playing        = false;
        ElapsedSeconds = 0.0;
        SimRunner.Clear();
        HasSimulatedData = false;
        ++RevisionCounter;
        return;
    }

    if (NextPhase == ExecutionStripPhase::Simulate)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
        StartOrResetSimulation(true);
        return;
    }

    // Play / Pause toggle
    if (ExecutionPhase == ExecutionStripPhase::Edit || !HasSimulatedData)
    {
        ExecutionPhase = ExecutionStripPhase::Play;
        StartOrResetSimulation(true);
    }
    else
    {
        ExecutionPhase = ExecutionStripPhase::Play;
        Playing        = !Playing;
    }
    ++RevisionCounter;
}

void FractureWorkspaceSession::TogglePlayPause() noexcept
{
    if (ExecutionPhase == ExecutionStripPhase::Edit || !HasSimulatedData)
    {
        ExecutionPhase = ExecutionStripPhase::Play;
        StartOrResetSimulation(true);
    }
    else
    {
        Playing = !Playing;
    }
    ++RevisionCounter;
}

void FractureWorkspaceSession::RewindToStart() noexcept
{
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ElapsedSeconds = 0.0;
        ++RevisionCounter;
        return;
    }
    StartOrResetSimulation(false);
}

void FractureWorkspaceSession::StepSingleFrame() noexcept
{
    if (!HasSimulatedData)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
        StartOrResetSimulation(false);
    }
    Playing = false;
    const double Dt = (1.0 / 60.0) * static_cast<double>(TimeScale);
    SimRunner.Step(Dt);
    ElapsedSeconds = std::min(DurationSeconds, ElapsedSeconds + Dt);
    AppendSample(KineticCurveSamples, static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ), 240u);
    ++RevisionCounter;
}

void FractureWorkspaceSession::ScrubToTime(double TargetSeconds) noexcept
{
    const double Clamped = std::clamp(TargetSeconds, 0.0, DurationSeconds);
    if (!HasSimulatedData)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
        StartOrResetSimulation(false);
    }
    else if (Clamped < ElapsedSeconds)
    {
        StartOrResetSimulation(false);
    }

    Playing = false;
    constexpr double FixedStep = 1.0 / 60.0;
    while (ElapsedSeconds + FixedStep * 0.5 < Clamped)
    {
        SimRunner.Step(FixedStep);
        ElapsedSeconds += FixedStep;
        AppendSample(KineticCurveSamples, static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ), 240u);
    }
    ElapsedSeconds = Clamped;
    ++RevisionCounter;
}

void FractureWorkspaceSession::AdvanceFrame(double DeltaSeconds) noexcept
{
    const float RawMs = static_cast<float>(std::clamp(DeltaSeconds * 1000.0, 0.25, 100.0));
    LastFrameTimeMs   = RawMs;
    if (!DiagnosticsSession.Paused)
    {
        AppendSample(FrameTimeSamplesMs, RawMs, 96u);
    }

    if (!Playing || ExecutionPhase == ExecutionStripPhase::Edit)
    {
        return;
    }

    if (!HasSimulatedData)
    {
        StartOrResetSimulation(true);
    }

    const double StepDt = std::clamp(DeltaSeconds, 0.0, 0.05) * static_cast<double>(TimeScale);
    if (StepDt <= 0.0) return;

    SimRunner.Step(StepDt);
    ElapsedSeconds += StepDt;
    AppendSample(KineticCurveSamples, static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ), 240u);
    ++RevisionCounter;

    if (ElapsedSeconds >= DurationSeconds)
    {
        if (LoopEnabled)
        {
            StartOrResetSimulation(true);
        }
        else
        {
            ElapsedSeconds = DurationSeconds;
            Playing        = false;
        }
    }
}

void FractureWorkspaceSession::TriggerViewportImpact(
    Mathematics::Axis3D WorldHitPoint,
    Mathematics::Axis3D WorldHitNormal,
    int32_t             HitFragmentIndex) noexcept
{
    RenderOptions.SelectedFragmentIndex = HitFragmentIndex;
    ImpactPointLocal  = WorldHitPoint - RenderOptions.SolidWorldOffsetM;
    ImpactNormalLocal = Mathematics::NormalizeAxis(WorldHitNormal, { 0.0f, -1.0f, 0.0f });

    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ExecuteFracturePartitionOnly();
    }
    else if (SimRunner.QueryLoaded())
    {
        const auto Impulse = ImpactNormalLocal * static_cast<float>(-std::sqrt(ImpactEnergyJ) * 3.8);
        SimRunner.TriggerImpactImpulse(WorldHitPoint, Impulse, 0.65f);
        ++RevisionCounter;
    }
}

const GeometrySpace::Fracture::FractureMaterialSpec& FractureWorkspaceSession::ActiveMaterialSpec() const noexcept
{
    return GeometrySpace::Fracture::FractureMaterialRepository::LookupMaterial(MaterialId.c_str());
}

GeometrySpace::Fracture::SdfVolumeFootprint FractureWorkspaceSession::QuerySdfFootprint() const noexcept
{
    return GeometrySpace::Fracture::ConvexPartitioning::ComputeSdfVolumeFootprint(
        static_cast<uint32_t>(PartitionResult.Fragments.size()),
        SdfResolution,
        SdfEnabled);
}

uint64_t FractureWorkspaceSession::ComputeViewportRenderSignature() const noexcept
{
    uint64_t H = 14695981039346656037ull;
    auto Mix = [&](uint64_t V) noexcept
    {
        H ^= V;
        H *= 1099511628211ull;
    };
    auto MixFloat = [&](float F) noexcept
    {
        uint32_t Bits = 0u;
        std::memcpy(&Bits, &F, sizeof(Bits));
        Mix(Bits);
    };
    Mix(RevisionCounter);
    Mix(static_cast<uint64_t>(RenderOptions.RenderMode));
    MixFloat(RenderOptions.ExplodeFactor);
    Mix(RenderOptions.ShowBonds ? 1ull : 0ull);
    Mix(RenderOptions.ShowWireframe ? 1ull : 0ull);
    Mix(RenderOptions.ShowWavefronts ? 1ull : 0ull);
    Mix(RenderOptions.ShowImpactMarker ? 1ull : 0ull);
    Mix(RenderOptions.ShowGroundGrid ? 1ull : 0ull);
    Mix(RenderOptions.UseNonPlanarFractography ? 1ull : 0ull);
    Mix(static_cast<uint64_t>(RenderOptions.SelectedFragmentIndex + 10));
    MixFloat(Camera.YawRadians);
    MixFloat(Camera.PitchRadians);
    MixFloat(Camera.DistanceM);
    MixFloat(Camera.TargetWorld.X);
    MixFloat(Camera.TargetWorld.Y);
    MixFloat(Camera.TargetWorld.Z);
    return H;
}

} // namespace Frontier::Workspaces::Fracture
