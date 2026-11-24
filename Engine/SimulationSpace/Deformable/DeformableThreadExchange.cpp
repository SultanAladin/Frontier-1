/*============================================================================================================================================
                                                   DEFORMABLETHREADEXCHANGE.CPP
============================================================================================================================================*/
// 🧩 Phase D12 Wait-free cross-thread simulation/render boundary built on Frontier::Threading::ThreadBoundaryExchange

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableThreadExchange.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/PneumaticTireSolver.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <algorithm>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE THREAD EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

DeformableThreadExchange::DeformableThreadExchange(DeformablePresetKind initialPreset)
    : Boundary_(Threading::DefaultBoundaryDirectiveCapacity)
{
    DeformablePresetRegistry::BuildPreset(initialPreset, Body_, Configuration_);
    // Pre-seed all wait-free reading bank slots so live Tick() publishes perform 0 heap allocations.
    for (uint32_t slot = 0u; slot < Threading::DefaultReadingBankSlots; ++slot)
    {
        PublishCurrentReading();
    }
}

bool DeformableThreadExchange::EnqueueDirective(DeformableControlDirective directive) noexcept
{
    return Boundary_.EnqueueDirective(std::move(directive));
}

Threading::ReadingLease<DeformablePublishedReading> DeformableThreadExchange::AcquireReadLease() const noexcept
{
    return Boundary_.AcquireReadLease();
}

uint64_t DeformableThreadExchange::QueryPublishedGeneration() const noexcept
{
    return Boundary_.QueryPublishedGeneration();
}

uint64_t DeformableThreadExchange::QueryDrainedDirectiveCount() const noexcept
{
    return Boundary_.QueryDrainedDirectiveCount();
}

void DeformableThreadExchange::ApplyDirectiveOnOwnerThread(const DeformableControlDirective& directive)
{
    switch (directive.Category)
    {
        case DeformableDirectiveCategory::SwitchPreset:
            DeformablePresetRegistry::BuildPreset(directive.PresetKind, Body_, Configuration_);
            break;
        case DeformableDirectiveCategory::ApplyConfiguration:
            Configuration_ = directive.Configuration;
            break;
        case DeformableDirectiveCategory::TriggerTireDrop:
            PneumaticTireSolver::TriggerDropLift(Body_, directive.DropLiftMeters);
            break;
        case DeformableDirectiveCategory::CutAlongSegment:
            (void)XpbdConstraintSolver::CutConstraintsNearSegment(
                Body_, directive.SegmentA, directive.SegmentB, directive.CutRadius);
            break;
        case DeformableDirectiveCategory::SetGrabTarget:
            Body_.GrabbedPointIndex = directive.GrabPointIndex;
            Body_.GrabTargetMeters  = directive.SegmentA;
            break;
        case DeformableDirectiveCategory::ReleaseGrab:
            Body_.GrabbedPointIndex = -1;
            break;
        case DeformableDirectiveCategory::SetTireDeformMode:
            ActiveTireMode_ = directive.TireMode;
            break;
    }
}

void DeformableThreadExchange::PublishCurrentReading()
{
    (void)Boundary_.PublishReadingInPlace(
        [this](DeformablePublishedReading& dst) noexcept
        {
            dst.StepOrdinal       = StepOrdinal_;
            dst.SimulationSeconds = SimulationSeconds_;
            dst.ActivePreset      = Body_.PresetKind;
            dst.ActiveTireMode    = ActiveTireMode_;
            dst.PointCount        = Body_.PointCount;
            dst.TetCount          = Body_.TetCount;
            dst.SeveredCount      = Body_.SeveredCount;

            if (!Body_.FemTires.empty())
            {
                dst.AxleX        = Body_.FemTires[0].AxleX;
                dst.AxleY        = Body_.FemTires[0].AxleY;
                dst.SpinAngleRad = Body_.FemTires[0].SpinAngleRad;
            }
            else if (!Body_.PneumaticTires.empty())
            {
                dst.AxleX        = Body_.PneumaticTires[0].AxleX;
                dst.AxleY        = Body_.PneumaticTires[0].AxleY;
                dst.SpinAngleRad = Body_.PneumaticTires[0].SpinAngleRad;
            }
            else
            {
                dst.AxleX = dst.AxleY = dst.SpinAngleRad = 0.0f;
            }

            float minX = 1.0e9f, minY = 1.0e9f, minZ = 1.0e9f;
            float maxX = -1.0e9f, maxY = -1.0e9f, maxZ = -1.0e9f;
            float peakStrain = 0.0f;
            for (uint32_t i = 0u; i < Body_.PointCount; ++i)
            {
                const size_t i3 = static_cast<size_t>(i) * 3u;
                const float x = Body_.Positions[i3 + 0u];
                const float y = Body_.Positions[i3 + 1u];
                const float z = Body_.Positions[i3 + 2u];
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
                peakStrain = std::max(peakStrain, Body_.PointStrain[i]);
            }
            dst.PeakStrain = peakStrain;
            if (Body_.PointCount > 0u)
            {
                dst.BoundsMinMeters = { minX, minY, minZ };
                dst.BoundsMaxMeters = { maxX, maxY, maxZ };
            }
            else
            {
                dst.BoundsMinMeters = { 0.0f, 0.0f, 0.0f };
                dst.BoundsMaxMeters = { 0.0f, 0.0f, 0.0f };
            }

            dst.Positions   = Body_.Positions;
            dst.PointStrain = Body_.PointStrain;
        },
        SimulationSeconds_,
        StepOrdinal_);
}

void DeformableThreadExchange::StepAndPublishOnOwnerThread(
    float dtSeconds,
    float windPulse,
    const DeformableExternalCoupling* externalCoupling)
{
    Boundary_.DrainDirectives(
        [this](const DeformableControlDirective& d)
        {
            ApplyDirectiveOnOwnerThread(d);
        });

    XpbdConstraintSolver::Step(
        Body_,
        dtSeconds,
        Configuration_,
        static_cast<float>(SimulationSeconds_),
        windPulse,
        externalCoupling);

    ++StepOrdinal_;
    SimulationSeconds_ += static_cast<double>(dtSeconds);
    PublishCurrentReading();
}

} // namespace Frontier::SimulationSpace::Deformable
