/*============================================================================================================================================
                                                       DEFORMABLEEXTENSION.CPP
============================================================================================================================================*/
// 🧩 Phase D19 Official Frontier Lifecycle::SuspendableParticipant runtime extension implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableExtension.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"

#include <algorithm>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   DEFORMABLE EXTENSION
//------------------------------------------------------------------------------------------------------------------------

DeformableExtension::DeformableExtension(DeformableExtensionParameters initialParameters) noexcept
    : Parameters_(std::move(initialParameters))
{
}

DeformableExtension::~DeformableExtension() noexcept
{
    Finalize();
}

bool DeformableExtension::Initialize() noexcept
{
    if (Initialized_)
    {
        return true;
    }

    try
    {
        WorldRegistry_.ClearAll();
        WorldRegistry_.SetCullingPolicy(Parameters_.WorldCullingPolicy);

        HeroExchange_ = std::make_unique<DeformableThreadExchange>(Parameters_.HeroExchangePreset);

        DeformablePresetRegistry::BuildPreset(Parameters_.HeroExchangePreset, HeroGpuBody_, HeroGpuConfig_);
        (void)VulkanExecutor_.InitializeForBody(HeroGpuBody_, HeroGpuConfig_, Parameters_.CompiledShaderDirectory);

        Telemetry_ = {};
        if (Parameters_.EnableVehicleCoupling)
        {
            VehicleInput_ = VehicleDeformableCoupling::BuildCanonicalSportCarInput();
            std::string atlasErr;
            const bool loadedWithAtlas = !Parameters_.ShippedVertexAtlasPath.empty() &&
                VehicleCoupling_.Initialize(Parameters_.VehicleLodPolicy, Parameters_.ShippedVertexAtlasPath, &atlasErr);
            if (!loadedWithAtlas)
            {
                (void)VehicleCoupling_.Initialize(Parameters_.VehicleLodPolicy, {}, nullptr);
            }
            Telemetry_.HasShippedVertexAtlas = VehicleCoupling_.HasShippedVertexAtlas();
        }

        StepAccumulator_ = 0.0;
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

void DeformableExtension::Tick(double deltaSeconds) noexcept
{
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

            if (HeroExchange_)
            {
                HeroExchange_->StepAndPublishOnOwnerThread(dtF, WindPulse_, ExternalCoupling_);
                Telemetry_.HeroPublishedGeneration = HeroExchange_->QueryPublishedGeneration();
            }

            if (VulkanExecutor_.Ready())
            {
                (void)VulkanExecutor_.ExecuteTickInPlace(
                    HeroGpuBody_, dtF, HeroGpuConfig_, simT, WindPulse_, ExternalCoupling_);
            }

            Telemetry_.LastWorldSummary = WorldRegistry_.StepWorld(
                dtF, simT, CameraWorldPos_, WindPulse_, ExternalCoupling_);

            if (Parameters_.EnableVehicleCoupling && VehicleCoupling_.Ready())
            {
                Telemetry_.LastVehicleSummary = VehicleCoupling_.StepVehicle(
                    VehicleInput_, dtF, simT, CameraWorldPos_);
            }

            Telemetry_.AccumulatedSeconds += fixedDt;
            Telemetry_.CompletedTicks     += 1ull;
            StepAccumulator_              -= fixedDt;
            ++executedSubsteps;
        }
    }
    catch (...)
    {
        // Maintain noexcept lifecycle invariant
    }
}

void DeformableExtension::Suspend() noexcept
{
    if (Initialized_)
    {
        Suspended_ = true;
    }
}

void DeformableExtension::Resume() noexcept
{
    if (Initialized_)
    {
        Suspended_ = false;
    }
}

void DeformableExtension::Finalize() noexcept
{
    VulkanExecutor_.Teardown();
    WorldRegistry_.ClearAll();
    HeroExchange_.reset();
    StepAccumulator_ = 0.0;
    Suspended_       = false;
    Initialized_     = false;
}

#if FRONTIER_DEFORMABLE_HAS_VULKAN
bool DeformableExtension::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* interchange,
    const std::filesystem::path& compiledShaderDirectory) noexcept
{
    if (!compiledShaderDirectory.empty())
    {
        Parameters_.CompiledShaderDirectory = compiledShaderDirectory;
    }
    const bool execOk  = VulkanExecutor_.AttachVulkanInterchange(interchange);
    const bool worldOk = WorldRegistry_.AttachVulkanInterchange(interchange, Parameters_.CompiledShaderDirectory);
    if (Initialized_ && execOk && HeroGpuBody_.PointCount > 0u)
    {
        (void)VulkanExecutor_.InitializeForBody(HeroGpuBody_, HeroGpuConfig_, Parameters_.CompiledShaderDirectory);
    }
    return execOk && worldOk;
}
#endif

void DeformableExtension::AssignCameraWorldPosition(const std::array<float, 3>& cameraPositionMeters) noexcept
{
    CameraWorldPos_ = cameraPositionMeters;
}

void DeformableExtension::AssignExternalCoupling(const DeformableExternalCoupling* externalCoupling) noexcept
{
    ExternalCoupling_ = externalCoupling;
}

void DeformableExtension::AssignVehicleChassisInput(const VehicleChassisCouplingInput& chassisInput) noexcept
{
    VehicleInput_ = chassisInput;
}

void DeformableExtension::AssignWindPulse(float windPulse) noexcept
{
    WindPulse_ = windPulse;
}

bool DeformableExtension::EnqueueHeroDirective(DeformableControlDirective directive) noexcept
{
    return HeroExchange_ ? HeroExchange_->EnqueueDirective(std::move(directive)) : false;
}

Threading::ReadingLease<DeformablePublishedReading> DeformableExtension::AcquireHeroReadLease() const noexcept
{
    if (HeroExchange_)
    {
        return HeroExchange_->AcquireReadLease();
    }
    static const Threading::PublishedReadingExchange<DeformablePublishedReading> emptyExchange {};
    return emptyExchange.AcquireReadLease();
}

} // namespace Frontier::SimulationSpace::Deformable
