/*============================================================================================================================================
                                               SIMULATIONCOORDINATOREXTENSION.CPP
============================================================================================================================================*/
// 🧩 Phase D22 Unified multi-domain Frontier SimulationCoordinatorExtension implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION COORDINATOR EXTENSION
//------------------------------------------------------------------------------------------------------------------------

SimulationCoordinatorExtension::SimulationCoordinatorExtension(SimulationCoordinatorParameters initialParameters) noexcept
    : Parameters_(std::move(initialParameters))
    , Deformables_(Parameters_.DeformableParameters)
{
}

SimulationCoordinatorExtension::~SimulationCoordinatorExtension() noexcept
{
    Finalize();
}

bool SimulationCoordinatorExtension::Initialize() noexcept
{
    if (Initialized_)
    {
        return true;
    }

    try
    {
        ForceRegister_.Clear();
        RigidFeedback_.clear();

        if (Parameters_.EnableFluidSolver)
        {
            FluidSim_.InitializeFixed(
                Parameters_.FluidGridCells,
                Parameters_.FluidCellSizeMeters,
                Parameters_.FluidCentreMeters);
            (void)FluidGpuExecutor_.Initialize(
                Parameters_.FluidGridCells,
                Parameters_.FluidCellSizeMeters,
                Parameters_.FluidCentreMeters,
                Parameters_.CompiledShaderDirectory);
        }

        if (Parameters_.EnableParticleSolver)
        {
            ParticleSim_.ClearAll();
            (void)ParticleGpuExecutor_.Initialize(4096u, Parameters_.CompiledShaderDirectory);
        }

        RigidFeedback_.reserve(128u);

        if (Parameters_.EnableDeformableSolver)
        {
            if (!Deformables_.Initialize())
            {
                return false;
            }
            Deformables_.AssignCameraWorldPosition(CameraWorldPos_);
        }

        StepAccumulator_ = 0.0;
        Telemetry_       = {};
        Suspended_       = false;
        Initialized_     = true;
        return true;
    }
    catch (...)
    {
        Finalize();
        return false;
    }
}

void SimulationCoordinatorExtension::Tick(double deltaSeconds) noexcept
{
    // 0.00 ms fast-skip for idle / Edit-mode frames
    if (!Initialized_ || Suspended_ || !ShouldUpdate_ || deltaSeconds <= 0.0)
    {
        return;
    }

    const double clampedDt = std::clamp(deltaSeconds, 0.0, 0.25);
    const double fixedDt   = 1.0 / static_cast<double>(std::max(15.0f, Parameters_.FixedStepHz));
    StepAccumulator_ += clampedDt;

    uint32_t executedSubsteps = 0u;
    const uint32_t maxSteps   = std::max(1u, Parameters_.MaxSubstepsPerTick);

    try
    {
        while (StepAccumulator_ >= fixedDt && executedSubsteps < maxSteps)
        {
            const float dtF  = static_cast<float>(fixedDt);
            const float simT = static_cast<float>(Telemetry_.AccumulatedSeconds);

            // 1. Upstream ForceFields: evaluate KeyTracks & publish wait-free ForceModelGpuRecord span
            Telemetry_.ActiveForceCount = ForceRegister_.PublishEvaluatedReading(
                Telemetry_.AccumulatedSeconds, Telemetry_.CompletedTicks);
            const auto forceLease = ForceRegister_.AcquirePublishedReading();
            const std::span<const ForceFields::ForceModelGpuRecord> activeForces = forceLease->ActiveSpan();

            // 2. 3D Eulerian Fluid Solver (Real Async Vulkan 1.2 Compute on GPU, headless CPU fallback otherwise)
            if (Parameters_.EnableFluidSolver)
            {
                FluidGpuExecutor_.ExecuteStep(
                    FluidSim_,
                    Parameters_.FluidSettings,
                    FluidSources_,
                    activeForces,
                    FluidColliders_,
                    dtF);
                Telemetry_.FluidStepCount = FluidGpuExecutor_.InspectTelemetry().CompletedTicks;

                if (!FluidGpuExecutor_.InspectTelemetry().RealGpuDispatched)
                {
                    float peakD = 0.0f;
                    for (const auto& cell : FluidSim_.State.Cells)
                    {
                        peakD = std::max(peakD, cell[0]);
                    }
                    Telemetry_.FluidPeakDensity = peakD;
                }
                else
                {
                    const auto occ = FluidGpuExecutor_.InspectCompletedOccupancy();
                    Telemetry_.FluidPeakDensity = occ.empty() ? 0.0f : occ[0][0];
                }
            }

            // 3. ParticleSpace coupled to BOTH ForceFields & Fluid (zero-allocation GasVelocitySamplerView!)
            if (Parameters_.EnableParticleSolver && !ParticleClouds_.empty())
            {
                ParticleSpace::ParticleSimulationRegion region {};
                region.CentreMeters = Parameters_.FluidCentreMeters;
                region.HalfExtentMeters = {
                    0.5f * static_cast<float>(Parameters_.FluidGridCells[0]) * Parameters_.FluidCellSizeMeters,
                    0.5f * static_cast<float>(Parameters_.FluidGridCells[1]) * Parameters_.FluidCellSizeMeters,
                    0.5f * static_cast<float>(Parameters_.FluidGridCells[2]) * Parameters_.FluidCellSizeMeters
                };

                ParticleSpace::GasVelocitySamplerView gasView {};
                if (Parameters_.EnableFluidSolver)
                {
                    gasView.Callback    = &SimulationCoordinatorExtension::SampleFluidVelocityThunk;
                    gasView.UserContext = this;
                    const auto fluidFeed = FluidGpuExecutor_.InspectVolumeGpuFeed();
                    ParticleGpuExecutor_.BindExternalFluidVelocityView(
                        fluidFeed.VelocityViewHandle, fluidFeed.GpuResident);
                }
                else
                {
                    ParticleGpuExecutor_.BindExternalFluidVelocityView(0ull, false);
                }

                ParticleGpuExecutor_.ExecuteStep(
                    ParticleSim_,
                    ParticleClouds_,
                    SolidRegistry_,
                    region,
                    simT,
                    dtF,
                    activeForces,
                    gasView,
                    RigidProxies_.empty() ? nullptr : RigidProxies_.data(),
                    static_cast<uint32_t>(RigidProxies_.size()),
                    static_cast<uint32_t>(sizeof(PhysicsSpace::RigidColliderProxy)));

                Telemetry_.ActiveParticleCount = ParticleGpuExecutor_.InspectTelemetry().RealGpuDispatched
                    ? ParticleClouds_[0].Evaluated.Count
                    : ParticleSim_.QueryStatistics().ActiveParticleCount;
            }

            // 4. Deformables (world .SoftBody instances + 4-wheel vehicle) coupled to shared ForceFields & Jolt RigidColliderProxies
            RigidFeedback_.clear();
            uint32_t contactCount = 0u;
            if (Parameters_.EnableDeformableSolver && Deformables_.QueryInitialized())
            {
                Deformable::DeformableExternalCoupling coupling {};
                coupling.ForceRecords = activeForces;
                coupling.RigidProxies = RigidProxies_;

                Deformables_.AssignCameraWorldPosition(CameraWorldPos_);
                Deformables_.AssignExternalCoupling(&coupling);
                Deformables_.Tick(fixedDt);
                Deformables_.AssignExternalCoupling(nullptr);

                const auto& defTel = Deformables_.InspectTelemetry();
                Telemetry_.ActiveDeformableCount = defTel.LastWorldSummary.TotalInstances;
                Telemetry_.LastVehicleSummary    = defTel.LastVehicleSummary;

                // Accumulate two-way deformable -> Jolt rigid impulse feedback for PhysicsThread
                if (!RigidProxies_.empty())
                {
                    for (const auto& inst : Deformables_.InspectWorldRegistry().EnumerateInstances())
                    {
                        if (inst.ActivityTier != Deformable::DeformableActivityTier::Asleep)
                        {
                            contactCount += Deformable::VehicleReadingConverter::AccumulateRigidContactImpulses(
                                inst.Body, RigidProxies_, Parameters_.DeformableImpulseGain, RigidFeedback_);
                        }
                    }
                }
            }
            Telemetry_.DeformableRigidContacts = contactCount;

            Telemetry_.AccumulatedSeconds += fixedDt;
            Telemetry_.CompletedTicks     += 1ull;
            StepAccumulator_              -= fixedDt;
            ++executedSubsteps;
        }
    }
    catch (...)
    {
        // Maintain noexcept lifecycle guarantee
    }
}

void SimulationCoordinatorExtension::Suspend() noexcept
{
    if (Initialized_)
    {
        Suspended_ = true;
        Deformables_.Suspend();
    }
}

void SimulationCoordinatorExtension::Resume() noexcept
{
    if (Initialized_)
    {
        Suspended_ = false;
        Deformables_.Resume();
    }
}

void SimulationCoordinatorExtension::Finalize() noexcept
{
    Deformables_.Finalize();
    FluidGpuExecutor_.Teardown();
    ParticleGpuExecutor_.Teardown();
    ParticleSim_.ClearAll();
    ForceRegister_.Clear();
    FluidSources_.clear();
    ParticleClouds_.clear();
    RigidProxies_.clear();
    FluidColliders_.clear();
    RigidFeedback_.clear();
    StepAccumulator_ = 0.0;
    Suspended_       = false;
    Initialized_     = false;
}

std::array<float, 3> SimulationCoordinatorExtension::SampleFluidVelocityThunk(
    const void* userContext,
    const std::array<float, 3>& worldPosition) noexcept
{
    return static_cast<const SimulationCoordinatorExtension*>(userContext)->SampleFluidVelocityWorld(worldPosition);
}

SimulationExecutionRouteSummary SimulationCoordinatorExtension::InspectExecutionRoutes() const noexcept
{
    SimulationExecutionRouteSummary summary {};
    summary.DeformableRoute = Deformables_.InspectVulkanExecutor().InspectTelemetry().Route;
    summary.FluidRoute      = FluidGpuExecutor_.InspectTelemetry().Route;
    summary.ParticleRoute   = ParticleGpuExecutor_.InspectTelemetry().Route;

    const bool defGpu  = Deformables_.InspectVulkanExecutor().InspectTelemetry().RealGpuDispatched ||
                         (Deformables_.InspectTelemetry().LastWorldSummary.GpuDispatchedCount > 0u);
    const bool fldGpu  = FluidGpuExecutor_.InspectTelemetry().RealGpuDispatched;
    const bool prtGpu  = ParticleGpuExecutor_.InspectTelemetry().RealGpuDispatched;
    summary.AnyRealGpuDispatched = defGpu || fldGpu || prtGpu;
    summary.AllRealGpuDispatched = defGpu && fldGpu && prtGpu;
    return summary;
}

#if FRONTIER_FLUID_HAS_VULKAN
bool SimulationCoordinatorExtension::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* interchange,
    const std::filesystem::path& compiledShaderDirectory) noexcept
{
    if (!compiledShaderDirectory.empty())
    {
        Parameters_.CompiledShaderDirectory = compiledShaderDirectory;
    }
    const bool defOk      = Deformables_.AttachVulkanInterchange(interchange, Parameters_.CompiledShaderDirectory);
    const bool fluidOk    = FluidGpuExecutor_.AttachVulkanInterchange(interchange);
    const bool particleOk = ParticleGpuExecutor_.AttachVulkanInterchange(interchange);
    return defOk && fluidOk && particleOk;
}
#endif

void SimulationCoordinatorExtension::AssignCameraWorldPosition(const std::array<float, 3>& cameraPositionMeters) noexcept
{
    CameraWorldPos_ = cameraPositionMeters;
    Deformables_.AssignCameraWorldPosition(cameraPositionMeters);
}

void SimulationCoordinatorExtension::AssignRigidColliderProxies(std::span<const PhysicsSpace::RigidColliderProxy> proxies)
{
    RigidProxies_.assign(proxies.begin(), proxies.end());
    FluidColliders_.clear();
    FluidColliders_.reserve(proxies.size());
    for (const auto& p : proxies)
    {
        Fluid::FluidColliderSphere sphere {};
        sphere.CentreMeters = { p.CenterMeters[0], p.CenterMeters[1], p.CenterMeters[2] };
        sphere.RadiusMeters = std::max(0.1f, p.RadiusMeters);
        FluidColliders_.push_back(sphere);
    }
}

void SimulationCoordinatorExtension::SyncVehicleFromPhysicsReading(
    const PhysicsSpace::VehicleReading& vehicleReading,
    const Deformable::VehicleConversionSettings& conversion) noexcept
{
    const auto chassisInput = Deformable::VehicleReadingConverter::ConvertFromPhysicsReading(vehicleReading, conversion);
    Deformables_.AssignVehicleChassisInput(chassisInput);
}

void SimulationCoordinatorExtension::AddFluidSource(const Fluid::FluidReleaseSourceParameters& source)
{
    FluidSources_.push_back(source);
}

void SimulationCoordinatorExtension::ClearFluidSources() noexcept
{
    FluidSources_.clear();
}

void SimulationCoordinatorExtension::AddParticleCloud(const ParticleSpace::ParticleCloudNode& cloud)
{
    ParticleClouds_.push_back(cloud);
}

void SimulationCoordinatorExtension::ClearParticleClouds() noexcept
{
    ParticleClouds_.clear();
}

std::array<float, 3> SimulationCoordinatorExtension::SampleFluidVelocityWorld(
    const std::array<float, 3>& worldPosition) const noexcept
{
    const auto& vel = FluidSim_.Velocity;
    const uint32_t gx = vel.Width;
    const uint32_t gy = vel.Height;
    const uint32_t gz = vel.Depth;
    if (gx == 0u || gy == 0u || gz == 0u || vel.Cells.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float cs  = std::max(1.0e-4f, Parameters_.FluidCellSizeMeters);
    const float hx  = 0.5f * static_cast<float>(gx) * cs;
    const float hy  = 0.5f * static_cast<float>(gy) * cs;
    const float hz  = 0.5f * static_cast<float>(gz) * cs;

    const float fx = ((worldPosition[0] - (Parameters_.FluidCentreMeters[0] - hx)) / cs) - 0.5f;
    const float fy = ((worldPosition[1] - (Parameters_.FluidCentreMeters[1] - hy)) / cs) - 0.5f;
    const float fz = ((worldPosition[2] - (Parameters_.FluidCentreMeters[2] - hz)) / cs) - 0.5f;

    if (fx < 0.0f || fy < 0.0f || fz < 0.0f ||
        fx >= static_cast<float>(gx - 1u) || fy >= static_cast<float>(gy - 1u) || fz >= static_cast<float>(gz - 1u))
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto sample = vel.SampleTrilinear(fx, fy, fz);
    return { sample[0], sample[1], sample[2] };
}

} // namespace Frontier::SimulationSpace
