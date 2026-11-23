/*============================================================================================================================================
                                                           CONSTRAINTSOLVER.CPP
============================================================================================================================================*/
// 🧩 Constraint strain rupture, Dock key verification, sweep/spin kinematics, ComponentClass conversion, and reading list lookup

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"
#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

namespace
{

[[nodiscard]] std::string_view TrimAsciiWhitespace(std::string_view InputText) noexcept
{
    while (!InputText.empty() && std::isspace(static_cast<unsigned char>(InputText.front())) != 0)
    {
        InputText.remove_prefix(1u);
    }
    while (!InputText.empty() && std::isspace(static_cast<unsigned char>(InputText.back())) != 0)
    {
        InputText.remove_suffix(1u);
    }
    return InputText;
}

[[nodiscard]] Mathematics::Axis3D ResolveAxisUnit(ComponentClass::ConstraintAxis Axis) noexcept
{
    switch (Axis)
    {
        case ComponentClass::ConstraintAxis::AxisX: return { 1.0f, 0.0f, 0.0f };
        case ComponentClass::ConstraintAxis::AxisY: return { 0.0f, 1.0f, 0.0f };
        case ComponentClass::ConstraintAxis::AxisZ: return { 0.0f, 0.0f, 1.0f };
    }
    return { 0.0f, 1.0f, 0.0f };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            CONSTRAINT LIST READING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstraintListReading::ClearForRewrite() noexcept
{
    // 💡 Preserves heap capacity on recycled bank slots so PhysicsThread publication incurs zero steady-state allocations
    Constraints.clear();
    SlotToDenseIndex.clear();
    StepEvents.clear();
    CompletedStepOrdinal  = 0ull;
    SimulatedSeconds      = 0.0;
    IntactConstraintCount = 0u;
    BrokenConstraintCount = 0u;
}

std::span<const ConstraintReading> ConstraintListReading::InspectConstraints() const noexcept
{
    return Constraints;
}

std::span<const ConstraintTransitionEvent> ConstraintListReading::InspectStepEvents() const noexcept
{
    return StepEvents;
}

const ConstraintReading* ConstraintListReading::ResolveConstraint(ConstraintToken Token) const noexcept
{
    if (!Token.Valid())
    {
        return nullptr;
    }
    const std::size_t ZeroIndex = static_cast<std::size_t>(Token.SlotIndex - 1u);
    if (ZeroIndex >= SlotToDenseIndex.size())
    {
        return nullptr;
    }
    const uint32_t DenseIndex = SlotToDenseIndex[ZeroIndex];
    if (DenseIndex == InvalidDenseIndex || DenseIndex >= Constraints.size())
    {
        return nullptr;
    }
    const ConstraintReading& Candidate = Constraints[DenseIndex];
    if (!Candidate.Resident || Candidate.Token != Token)
    {
        return nullptr;
    }
    return &Candidate;
}

const ConstraintReading* ConstraintListReading::ResolveByComponentTag(uint64_t ComponentInstanceTag) const noexcept
{
    if (ComponentInstanceTag == 0ull)
    {
        return nullptr;
    }
    for (const ConstraintReading& Reading : Constraints)
    {
        if (Reading.Resident && Reading.ComponentInstanceTag == ComponentInstanceTag)
        {
            return &Reading;
        }
    }
    return nullptr;
}

uint32_t ConstraintListReading::QueryConstraintCount() const noexcept
{
    return static_cast<uint32_t>(Constraints.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                               CONSTRAINT SOLVER UTILITIES
//------------------------------------------------------------------------------------------------------------------------

float ConstraintSolver::EvaluateBondStrainRatio(
    float AppliedForceN,
    float AppliedTorqueNm,
    float BreakForceN,
    float BreakTorqueNm) noexcept
{
    float Ratio = 0.0f;
    if (BreakForceN > 1.0e-5f)
    {
        Ratio = std::max(Ratio, AppliedForceN / BreakForceN);
    }
    if (BreakTorqueNm > 1.0e-5f)
    {
        Ratio = std::max(Ratio, AppliedTorqueNm / BreakTorqueNm);
    }
    return Ratio;
}

uint64_t ConstraintSolver::ComputeDockKeyHash(std::string_view KeyText) noexcept
{
    const std::string_view Normalized = TrimAsciiWhitespace(KeyText);
    if (Normalized.empty())
    {
        return 0ull;
    }

    // 📝 Case-insensitive 64-bit FNV-1a fingerprint matching ProjectZeroEditor Dock key normalization
    uint64_t Hash = 14695981039346656037ull;
    for (const char Ch : Normalized)
    {
        const unsigned char Lowered = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(Ch)));
        Hash ^= static_cast<uint64_t>(Lowered);
        Hash *= 1099511628211ull;
    }
    return Hash == 0ull ? 1ull : Hash;
}

bool ConstraintSolver::VerifyDockKeyAccepted(
    std::string_view DockKey,
    std::string_view AttachedTagCsv) noexcept
{
    const uint64_t RequiredHash = ComputeDockKeyHash(DockKey);
    if (RequiredHash == 0ull)
    {
        return true;
    }

    std::size_t Cursor = 0u;
    while (Cursor <= AttachedTagCsv.size())
    {
        const std::size_t CommaPos = AttachedTagCsv.find(',', Cursor);
        const std::string_view Segment = (CommaPos == std::string_view::npos)
            ? AttachedTagCsv.substr(Cursor)
            : AttachedTagCsv.substr(Cursor, CommaPos - Cursor);
        if (ComputeDockKeyHash(Segment) == RequiredHash)
        {
            return true;
        }
        if (CommaPos == std::string_view::npos)
        {
            break;
        }
        Cursor = CommaPos + 1u;
    }
    return false;
}

float ConstraintSolver::EvaluateSweepNormalizedWave(
    double ElapsedSeconds,
    float  CyclePeriodSeconds) noexcept
{
    const double Period = std::max(0.2, static_cast<double>(CyclePeriodSeconds));
    const double Phase  = (2.0 * Constants::Pi * ElapsedSeconds) / Period;
    return static_cast<float>(0.5 - 0.5 * std::cos(Phase));
}

float ConstraintSolver::EvaluateSpinAngularVelocity(
    SpinRateSourceCategory Source,
    float                  TargetRpm,
    float                  LinearSpeedMps,
    float                  RollingRadiusMeters) noexcept
{
    switch (Source)
    {
        case SpinRateSourceCategory::FixedRpm:
            return static_cast<float>((static_cast<double>(TargetRpm) * 2.0 * Constants::Pi) / 60.0);
        case SpinRateSourceCategory::LinearSpeed:
        {
            const float SafeRadius = std::max(0.05f, RollingRadiusMeters);
            return LinearSpeedMps / SafeRadius;
        }
        case SpinRateSourceCategory::Freewheel:
            return 0.0f;
    }
    return 0.0f;
}

ConstraintDescriptor ConstraintSolver::ConstructDescriptorFromComponent(
    const ComponentClass::ConstraintComponentSettings& Settings,
    RigidBodyToken                                     FirstBody,
    RigidBodyToken                                     SecondBody,
    const Mathematics::Axis3D&                         AnchorWorldMeters,
    std::string_view                                   AttachedTagCsv,
    uint64_t                                           ComponentInstanceTag) noexcept
{
    constexpr float DegToRad = static_cast<float>(Constants::Pi / 180.0);

    ConstraintDescriptor Descriptor {};
    Descriptor.FirstBody             = FirstBody;
    Descriptor.SecondBody            = SecondBody;
    Descriptor.AnchorWorldMeters     = AnchorWorldMeters;
    Descriptor.SecondAnchorMeters    = AnchorWorldMeters;
    Descriptor.AxisUnit              = ResolveAxisUnit(Settings.Axis);
    Descriptor.NormalizedPose        = std::clamp(Settings.NormalizedEditPose, 0.0f, 1.0f);
    Descriptor.DisableSelfCollide    = Settings.DisableSelfCollide;
    Descriptor.BreakForceN           = std::max(0.0f, Settings.BreakForceNewtons);
    Descriptor.BreakTorqueNm         = std::max(0.0f, Settings.BreakTorqueNm);
    Descriptor.MaxMotorTorqueOrForce = std::max(1.0f, Settings.MaxMotorTorqueNm);
    Descriptor.SnapRadiusMeters      = std::max(0.05f, Settings.SnapRadiusMeters);
    Descriptor.DockKeyHash           = ComputeDockKeyHash(Settings.DockKey);
    Descriptor.DockKeyMatched        = VerifyDockKeyAccepted(Settings.DockKey, AttachedTagCsv);
    Descriptor.DockInitiallyConnected = Settings.DockConnected;
    Descriptor.ComponentInstanceTag  = ComponentInstanceTag;

    switch (Settings.DriveMode)
    {
        case ComponentClass::ConstraintDriveMode::Hold:      Descriptor.DriveCategory = ConstraintDriveCategory::Hold; break;
        case ComponentClass::ConstraintDriveMode::Steer:     Descriptor.DriveCategory = ConstraintDriveCategory::Steer; break;
        case ComponentClass::ConstraintDriveMode::Sway:      Descriptor.DriveCategory = ConstraintDriveCategory::Sweep; break;
        case ComponentClass::ConstraintDriveMode::Motor:     Descriptor.DriveCategory = ConstraintDriveCategory::MotorPosition; break;
        case ComponentClass::ConstraintDriveMode::Freewheel: Descriptor.DriveCategory = ConstraintDriveCategory::Freewheel; break;
    }

    switch (Settings.Form)
    {
        case ComponentClass::PhysicsConstraintCategory::Fixed:
        {
            Descriptor.Category = ConstraintCategory::Fixed;
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Hinge:
        {
            Descriptor.Category          = ConstraintCategory::Hinge;
            Descriptor.MinLimit          = Settings.MinAngleDegrees * DegToRad;
            Descriptor.MaxLimit          = Settings.MaxAngleDegrees * DegToRad;
            Descriptor.MotorDriveEnabled = (Descriptor.DriveCategory != ConstraintDriveCategory::Freewheel);
            Descriptor.MotorVelocityMode = false;
            Descriptor.TargetMotorCoordinate = Descriptor.MinLimit
                + (Descriptor.MaxLimit - Descriptor.MinLimit) * Descriptor.NormalizedPose;
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Slider:
        {
            Descriptor.Category          = ConstraintCategory::Slider;
            Descriptor.MinLimit          = Settings.MinTravelMeters;
            Descriptor.MaxLimit          = Settings.MaxTravelMeters;
            Descriptor.MotorDriveEnabled = (Descriptor.DriveCategory == ConstraintDriveCategory::Sweep
                || Descriptor.DriveCategory == ConstraintDriveCategory::MotorPosition);
            Descriptor.TargetMotorCoordinate = Descriptor.MinLimit
                + (Descriptor.MaxLimit - Descriptor.MinLimit) * Descriptor.NormalizedPose;
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Ball:
        {
            Descriptor.Category          = ConstraintCategory::SwingTwist;
            Descriptor.ConeHalfAngleRad  = std::clamp(Settings.ConeAngleDegrees * DegToRad, 0.01f, 3.10f);
            Descriptor.TwistHalfAngleRad = std::clamp(Settings.TwistLimitDegrees * DegToRad, 0.01f, 3.14f);
            Descriptor.MotorDriveEnabled = (Descriptor.DriveCategory == ConstraintDriveCategory::Sweep
                || Descriptor.DriveCategory == ConstraintDriveCategory::MotorPosition);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Spring:
        {
            const float Stroke           = std::max(0.01f, Settings.TravelEachWayMeters);
            Descriptor.Category          = ConstraintCategory::Spring;
            Descriptor.MinLimit          = -Stroke;
            Descriptor.MaxLimit          = Stroke;
            Descriptor.SpringFrequencyHz = std::max(0.25f, Settings.NaturalFrequencyHz);
            Descriptor.SpringDampingRatio = std::max(0.05f, Settings.DampingRatioZeta);
            Descriptor.MotorDriveEnabled = true;
            Descriptor.TargetMotorCoordinate = (-Stroke + 2.0f * Stroke * Descriptor.NormalizedPose) * 0.5f;
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Spin:
        {
            Descriptor.Category          = ConstraintCategory::Spin;
            Descriptor.MotorDriveEnabled = (Settings.DriveMode != ComponentClass::ConstraintDriveMode::Freewheel);
            Descriptor.MotorVelocityMode = true;
            Descriptor.SpinSource        = (Settings.DriveMode == ComponentClass::ConstraintDriveMode::Freewheel)
                ? SpinRateSourceCategory::Freewheel
                : SpinRateSourceCategory::FixedRpm;
            Descriptor.TargetMotorCoordinate = EvaluateSpinAngularVelocity(
                Descriptor.SpinSource,
                Settings.TargetRpm,
                0.0f,
                Descriptor.RollingRadiusMeters);
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Tether:
        {
            const float Length            = std::max(0.02f, Settings.RopeLengthMeters);
            Descriptor.Category           = ConstraintCategory::Tether;
            Descriptor.MinLimit           = 0.0f;
            Descriptor.MaxLimit           = Length;
            Descriptor.SecondAnchorMeters = {
                AnchorWorldMeters.X - Descriptor.AxisUnit.X * Length,
                AnchorWorldMeters.Y - Descriptor.AxisUnit.Y * Length,
                AnchorWorldMeters.Z - Descriptor.AxisUnit.Z * Length
            };
            break;
        }
        case ComponentClass::PhysicsConstraintCategory::Dock:
        {
            Descriptor.Category = ConstraintCategory::Dock;
            break;
        }
    }

    return Descriptor;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
