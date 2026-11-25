/*============================================================================================================================================
                                                         CATEGORYINSTANCES.CPP
============================================================================================================================================*/
// 🧩 Implementation of dual-profile RAII category instances & tight SoA main-loop execution passes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/CategoryInstances.h"

#include <cmath>
#include <utility>

namespace Frontier::World
{

namespace
{

[[nodiscard]] float WrapDegrees360(float AngleDegrees) noexcept
{
    float Wrapped = std::fmod(AngleDegrees, 360.0f);
    if (Wrapped < 0.0f)
    {
        Wrapped += 360.0f;
    }
    return Wrapped;
}

[[nodiscard]] uint32_t BuildBaseDirectiveMask(bool VisibilityEnabled, bool SimulationEnabled, bool EditorRegistered) noexcept
{
    InstanceDirectiveMask Mask = InstanceDirectiveMask::None;
    if (VisibilityEnabled)
    {
        Mask = Mask | InstanceDirectiveMask::EnableVisibility;
    }
    if (SimulationEnabled)
    {
        Mask = Mask | InstanceDirectiveMask::EnableSimulationTick;
    }
    if (EditorRegistered)
    {
        Mask = Mask | InstanceDirectiveMask::EnableEditorRegistration;
    }
    return static_cast<uint32_t>(Mask);
}

#if FRONTIER_PROFILE_EDITOR
void RegisterCategoryInEditor(
    WorldInstanceRegister& TargetRegister,
    InstanceToken          Token,
    InstanceToken          EnclosingToken,
    std::string_view       DisplayTitle,
    std::string_view       CategoryLabel,
    std::string_view       Subtitle,
    bool                   VisibilityEnabled,
    bool                   RemovalProtected,
    bool                   DuplicateProtected)
{
    if (EnclosingToken.QueryValid())
    {
        TargetRegister.AttachNestedSlot(EnclosingToken, Token);
    }

    EditorInstanceRegistrationDescriptor Descriptor{};
    Descriptor.Token              = Token;
    Descriptor.EnclosingToken     = EnclosingToken;
    Descriptor.DisplayTitle       = DisplayTitle;
    Descriptor.CategoryLabel      = CategoryLabel;
    Descriptor.Subtitle           = Subtitle;
    Descriptor.CategoryTag        = QueryInstanceCategoryTag(Token.QueryCategory());
    Descriptor.VisibilityEnabled  = VisibilityEnabled;
    Descriptor.RemovalProtected   = RemovalProtected;
    Descriptor.DuplicateProtected = DuplicateProtected;

    TargetRegister.NotifyEditorInstanceRegistered(Descriptor);
}
#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               CATEGORY INSTANCE HANDLE
//------------------------------------------------------------------------------------------------------------------------

CategoryInstanceHandle::~CategoryInstanceHandle() noexcept
{
    Release();
}

CategoryInstanceHandle::CategoryInstanceHandle(CategoryInstanceHandle&& Other) noexcept
    : Register(std::exchange(Other.Register, nullptr))
    , Token(std::exchange(Other.Token, InstanceToken{}))
{
}

CategoryInstanceHandle& CategoryInstanceHandle::operator=(CategoryInstanceHandle&& Other) noexcept
{
    if (this != &Other)
    {
        Release();
        Register = std::exchange(Other.Register, nullptr);
        Token    = std::exchange(Other.Token, InstanceToken{});
    }
    return *this;
}

void CategoryInstanceHandle::Release() noexcept
{
    if ((Register != nullptr) && Token.QueryValid())
    {
        Register->ReclaimInstanceSlot(Token);
    }
    Register = nullptr;
    Token    = InstanceToken{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLAR INSTANCE
//------------------------------------------------------------------------------------------------------------------------

SolarInstance::SolarInstance(
    WorldInstanceRegister&         TargetRegister,
    const SolarInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::SolarBody,
        BuildBaseDirectiveMask(Parameters.VisibilityEnabled, true, false));

    SolarInstanceRegister& Solar = TargetRegister.AccessSolarRegister();
    const uint32_t CategorySlot  = Solar.QueryActiveCount();
    Solar.OwnerTokens.push_back(Token);
    Solar.AzimuthDegrees.push_back(Parameters.AzimuthDegrees);
    Solar.ElevationDegrees.push_back(Parameters.ElevationDegrees);
    Solar.AngularDiameterDegrees.push_back(Parameters.AngularDiameterDegrees);
    Solar.IntensityMultiplier.push_back(Parameters.IntensityMultiplier);
    Solar.DayCycleSpeedDegreesS.push_back(Parameters.DayCycleSpeedDegreesS);
    Solar.CycleAnimationEnabled.push_back(Parameters.CycleAnimationEnabled ? 1u : 0u);
    Solar.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Solar.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
SolarInstance::SolarInstance(
    WorldInstanceRegister&         TargetRegister,
    std::string_view               DisplayTitle,
    const SolarInstanceParameters& Parameters,
    InstanceToken                  EnclosingToken)
    : SolarInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Solar Body",
        "Physical ephemeris & directional irradiance",
        Parameters.VisibilityEnabled,
        true,
        true);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                    LUNAR INSTANCE
//------------------------------------------------------------------------------------------------------------------------

LunarInstance::LunarInstance(
    WorldInstanceRegister&         TargetRegister,
    const LunarInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::LunarBody,
        BuildBaseDirectiveMask(Parameters.VisibilityEnabled, true, false));

    LunarInstanceRegister& Lunar = TargetRegister.AccessLunarRegister();
    const uint32_t CategorySlot  = Lunar.QueryActiveCount();
    Lunar.OwnerTokens.push_back(Token);
    Lunar.MoonSlotOrdinal.push_back(Parameters.MoonSlotOrdinal);
    Lunar.RightAscensionDegrees.push_back(Parameters.RightAscensionDegrees);
    Lunar.DeclinationDegrees.push_back(Parameters.DeclinationDegrees);
    Lunar.AngularDiameterDegrees.push_back(Parameters.AngularDiameterDegrees);
    Lunar.PhaseAngleDegrees.push_back(Parameters.PhaseAngleDegrees);
    Lunar.OrbitalRateDegreesPerS.push_back(Parameters.OrbitalRateDegreesPerS);
    Lunar.AlbedoReflectance.push_back(Parameters.AlbedoReflectance);
    Lunar.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Lunar.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
LunarInstance::LunarInstance(
    WorldInstanceRegister&         TargetRegister,
    std::string_view               DisplayTitle,
    const LunarInstanceParameters& Parameters,
    InstanceToken                  EnclosingToken)
    : LunarInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Lunar Body",
        "Celestial body orbit, phase & disc scattering",
        Parameters.VisibilityEnabled,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                  ATMOSPHERE INSTANCE
//------------------------------------------------------------------------------------------------------------------------

AtmosphereInstance::AtmosphereInstance(
    WorldInstanceRegister&              TargetRegister,
    const AtmosphereInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::AtmosphereMedium,
        BuildBaseDirectiveMask(Parameters.VisibilityEnabled, true, false));

    AtmosphereInstanceRegister& Atmosphere = TargetRegister.AccessAtmosphereRegister();
    const uint32_t CategorySlot            = Atmosphere.QueryActiveCount();
    Atmosphere.OwnerTokens.push_back(Token);
    Atmosphere.RayleighScaleMeters.push_back(Parameters.RayleighScaleMeters);
    Atmosphere.MieScaleMeters.push_back(Parameters.MieScaleMeters);
    Atmosphere.OzoneDensityMultiplier.push_back(Parameters.OzoneDensityMultiplier);
    Atmosphere.ObserverAltitudeMeters.push_back(Parameters.ObserverAltitudeMeters);
    Atmosphere.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Atmosphere.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
AtmosphereInstance::AtmosphereInstance(
    WorldInstanceRegister&              TargetRegister,
    std::string_view                    DisplayTitle,
    const AtmosphereInstanceParameters& Parameters,
    InstanceToken                       EnclosingToken)
    : AtmosphereInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Atmosphere Medium",
        "Rayleigh, Mie & ozone spectral scattering medium",
        Parameters.VisibilityEnabled,
        true,
        true);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOUD INSTANCE
//------------------------------------------------------------------------------------------------------------------------

CloudInstance::CloudInstance(
    WorldInstanceRegister&         TargetRegister,
    const CloudInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::CloudDeck,
        BuildBaseDirectiveMask(Parameters.VisibilityEnabled, true, false));

    CloudInstanceRegister& Cloud = TargetRegister.AccessCloudRegister();
    const uint32_t CategorySlot  = Cloud.QueryActiveCount();
    Cloud.OwnerTokens.push_back(Token);
    Cloud.BaseAltitudeMeters.push_back(Parameters.BaseAltitudeMeters);
    Cloud.ThicknessMeters.push_back(Parameters.ThicknessMeters);
    Cloud.DensityFraction.push_back(Parameters.DensityFraction);
    Cloud.WindSpeedMetersPerS.push_back(Parameters.WindSpeedMetersPerS);
    Cloud.AdvectionOffsetMeters.push_back(0.0f);
    Cloud.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Cloud.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
CloudInstance::CloudInstance(
    WorldInstanceRegister&         TargetRegister,
    std::string_view               DisplayTitle,
    const CloudInstanceParameters& Parameters,
    InstanceToken                  EnclosingToken)
    : CloudInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Cloud Deck",
        "Volumetric cloud altitude, thickness & wind advection",
        Parameters.VisibilityEnabled,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUMINAIRE INSTANCE
//------------------------------------------------------------------------------------------------------------------------

LuminaireInstance::LuminaireInstance(
    WorldInstanceRegister&             TargetRegister,
    const LuminaireInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::Luminaire,
        BuildBaseDirectiveMask(Parameters.VisibilityEnabled, true, false));

    LuminaireInstanceRegister& Luminaire = TargetRegister.AccessLuminaireRegister();
    const uint32_t CategorySlot          = Luminaire.QueryActiveCount();
    Luminaire.OwnerTokens.push_back(Token);
    Luminaire.LuminaireFormOrdinal.push_back(Parameters.LuminaireFormOrdinal);
    Luminaire.PeakLuminanceR.push_back(Parameters.PeakLuminanceR);
    Luminaire.PeakLuminanceG.push_back(Parameters.PeakLuminanceG);
    Luminaire.PeakLuminanceB.push_back(Parameters.PeakLuminanceB);
    Luminaire.RangeMeters.push_back(Parameters.RangeMeters);
    Luminaire.OuterConeDegrees.push_back(Parameters.OuterConeDegrees);
    Luminaire.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Luminaire.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
LuminaireInstance::LuminaireInstance(
    WorldInstanceRegister&             TargetRegister,
    std::string_view                   DisplayTitle,
    const LuminaireInstanceParameters& Parameters,
    InstanceToken                      EnclosingToken)
    : LuminaireInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Luminaire",
        "Physical photometric light source",
        Parameters.VisibilityEnabled,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                   SURFACE INSTANCE
//------------------------------------------------------------------------------------------------------------------------

SurfaceInstance::SurfaceInstance(
    WorldInstanceRegister&           TargetRegister,
    const SurfaceInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    uint32_t Mask = BuildBaseDirectiveMask(Parameters.VisibilityEnabled, false, false);
    if (Parameters.CastShadows)
    {
        Mask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableShadowProjection);
    }

    Token = TargetRegister.IssueInstanceSlot(
        InstanceCategory::SurfaceGeometry,
        Mask,
        Parameters.GeometryDocumentHash64);

    SurfaceInstanceRegister& Surface = TargetRegister.AccessSurfaceRegister();
    const uint32_t CategorySlot      = Surface.QueryActiveCount();
    Surface.OwnerTokens.push_back(Token);
    Surface.GeometryDocumentHash64.push_back(Parameters.GeometryDocumentHash64);
    Surface.MaterialDocumentHash64.push_back(Parameters.MaterialDocumentHash64);
    Surface.PrimitiveShapeOrdinal.push_back(Parameters.PrimitiveShapeOrdinal);
    Surface.CastShadows.push_back(Parameters.CastShadows ? 1u : 0u);
    Surface.VisibilityEnabled.push_back(Parameters.VisibilityEnabled ? 1u : 0u);
    Surface.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
SurfaceInstance::SurfaceInstance(
    WorldInstanceRegister&           TargetRegister,
    std::string_view                 DisplayTitle,
    const SurfaceInstanceParameters& Parameters,
    InstanceToken                    EnclosingToken)
    : SurfaceInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Surface Geometry",
        "Placed geometry & material surface participant",
        Parameters.VisibilityEnabled,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE INSTANCE
//------------------------------------------------------------------------------------------------------------------------

ParticleInstance::ParticleInstance(
    WorldInstanceRegister&            TargetRegister,
    const ParticleInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::ParticleCloud,
        BuildBaseDirectiveMask(Parameters.SimulationActive, Parameters.SimulationActive, false),
        Parameters.ParticleDocumentHash64);

    ParticleInstanceRegister& Particle = TargetRegister.AccessParticleRegister();
    const uint32_t CategorySlot        = Particle.QueryActiveCount();
    Particle.OwnerTokens.push_back(Token);
    Particle.ParticleDocumentHash64.push_back(Parameters.ParticleDocumentHash64);
    Particle.MaximumParticleCount.push_back(Parameters.MaximumParticleCount);
    Particle.ReleaseRatePerSecond.push_back(Parameters.ReleaseRatePerSecond);
    Particle.AccumulatedClockSeconds.push_back(0.0f);
    Particle.SimulationActive.push_back(Parameters.SimulationActive ? 1u : 0u);
    Particle.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
ParticleInstance::ParticleInstance(
    WorldInstanceRegister&            TargetRegister,
    std::string_view                  DisplayTitle,
    const ParticleInstanceParameters& Parameters,
    InstanceToken                     EnclosingToken)
    : ParticleInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Particle Cloud",
        "GPU-resident particulate simulation cloud",
        Parameters.SimulationActive,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                 FORCE FIELD INSTANCE
//------------------------------------------------------------------------------------------------------------------------

ForceFieldInstance::ForceFieldInstance(
    WorldInstanceRegister&              TargetRegister,
    const ForceFieldInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::ForceFieldVolume,
        BuildBaseDirectiveMask(Parameters.FieldActive, Parameters.FieldActive, false));

    ForceFieldInstanceRegister& ForceField = TargetRegister.AccessForceFieldRegister();
    const uint32_t CategorySlot            = ForceField.QueryActiveCount();
    ForceField.OwnerTokens.push_back(Token);
    ForceField.ForceModelOrdinal.push_back(Parameters.ForceModelOrdinal);
    ForceField.StrengthMetersPerS2.push_back(Parameters.StrengthMetersPerS2);
    ForceField.InfluenceRadiusMeters.push_back(Parameters.InfluenceRadiusMeters);
    ForceField.DirectionAxisX.push_back(Parameters.DirectionAxisX);
    ForceField.DirectionAxisY.push_back(Parameters.DirectionAxisY);
    ForceField.DirectionAxisZ.push_back(Parameters.DirectionAxisZ);
    ForceField.FieldActive.push_back(Parameters.FieldActive ? 1u : 0u);
    ForceField.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
ForceFieldInstance::ForceFieldInstance(
    WorldInstanceRegister&              TargetRegister,
    std::string_view                    DisplayTitle,
    const ForceFieldInstanceParameters& Parameters,
    InstanceToken                       EnclosingToken)
    : ForceFieldInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Force Field",
        "Analytical or vector-grid spatial force volume",
        Parameters.FieldActive,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                   TRIGGER INSTANCE
//------------------------------------------------------------------------------------------------------------------------

TriggerInstance::TriggerInstance(
    WorldInstanceRegister&           TargetRegister,
    const TriggerInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::TriggerZone,
        BuildBaseDirectiveMask(Parameters.SensorActive, Parameters.SensorActive, false));

    TriggerInstanceRegister& Trigger = TargetRegister.AccessTriggerRegister();
    const uint32_t CategorySlot      = Trigger.QueryActiveCount();
    Trigger.OwnerTokens.push_back(Token);
    Trigger.HalfExtentMetersX.push_back(Parameters.HalfExtentMetersX);
    Trigger.HalfExtentMetersY.push_back(Parameters.HalfExtentMetersY);
    Trigger.HalfExtentMetersZ.push_back(Parameters.HalfExtentMetersZ);
    Trigger.OverlapCountReading.push_back(0u);
    Trigger.SensorActive.push_back(Parameters.SensorActive ? 1u : 0u);
    Trigger.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
TriggerInstance::TriggerInstance(
    WorldInstanceRegister&           TargetRegister,
    std::string_view                 DisplayTitle,
    const TriggerInstanceParameters& Parameters,
    InstanceToken                    EnclosingToken)
    : TriggerInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Trigger Zone",
        "Spatial overlap sensor volume",
        Parameters.SensorActive,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                  ACOUSTIC INSTANCE
//------------------------------------------------------------------------------------------------------------------------

AcousticInstance::AcousticInstance(
    WorldInstanceRegister&            TargetRegister,
    const AcousticInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::AcousticSource,
        BuildBaseDirectiveMask(Parameters.PlaybackActive, Parameters.PlaybackActive, false),
        Parameters.AcousticDocumentHash64);

    AcousticInstanceRegister& Acoustic = TargetRegister.AccessAcousticRegister();
    const uint32_t CategorySlot        = Acoustic.QueryActiveCount();
    Acoustic.OwnerTokens.push_back(Token);
    Acoustic.AcousticDocumentHash64.push_back(Parameters.AcousticDocumentHash64);
    Acoustic.GainScalar.push_back(Parameters.GainScalar);
    Acoustic.MaxDistanceMeters.push_back(Parameters.MaxDistanceMeters);
    Acoustic.PlaybackActive.push_back(Parameters.PlaybackActive ? 1u : 0u);
    Acoustic.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
AcousticInstance::AcousticInstance(
    WorldInstanceRegister&            TargetRegister,
    std::string_view                  DisplayTitle,
    const AcousticInstanceParameters& Parameters,
    InstanceToken                     EnclosingToken)
    : AcousticInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Acoustic Source",
        "Spatial audio propagation emitter",
        Parameters.PlaybackActive,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA INSTANCE
//------------------------------------------------------------------------------------------------------------------------

CameraInstance::CameraInstance(
    WorldInstanceRegister&          TargetRegister,
    const CameraInstanceParameters& Parameters)
{
    Register = &TargetRegister;
    Token    = TargetRegister.IssueInstanceSlot(
        InstanceCategory::CameraOptics,
        BuildBaseDirectiveMask(Parameters.PrimaryViewActive, false, false));

    CameraInstanceRegister& Camera = TargetRegister.AccessCameraRegister();
    const uint32_t CategorySlot    = Camera.QueryActiveCount();
    Camera.OwnerTokens.push_back(Token);
    Camera.FocalLengthMillimeters.push_back(Parameters.FocalLengthMillimeters);
    Camera.SensorWidthMillimeters.push_back(Parameters.SensorWidthMillimeters);
    Camera.NearPlaneMeters.push_back(Parameters.NearPlaneMeters);
    Camera.FarPlaneMeters.push_back(Parameters.FarPlaneMeters);
    Camera.PrimaryViewActive.push_back(Parameters.PrimaryViewActive ? 1u : 0u);
    Camera.RequireRebuild = true;

    TargetRegister.BindCategorySlot(Token, CategorySlot);
}

#if FRONTIER_PROFILE_EDITOR
CameraInstance::CameraInstance(
    WorldInstanceRegister&          TargetRegister,
    std::string_view                DisplayTitle,
    const CameraInstanceParameters& Parameters,
    InstanceToken                   EnclosingToken)
    : CameraInstance(TargetRegister, Parameters)
{
    if (InstanceRecordEntry* Entry = TargetRegister.AccessRecordEntry(Token))
    {
        Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableEditorRegistration);
    }

    RegisterCategoryInEditor(
        TargetRegister,
        Token,
        EnclosingToken,
        DisplayTitle,
        "Camera Optics",
        "Physical lens focal length & sensor optics",
        Parameters.PrimaryViewActive,
        false,
        false);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                           TIGHT MAIN-LOOP EXECUTION PASSES
//------------------------------------------------------------------------------------------------------------------------

uint32_t ExecuteSolarLoop(SolarInstanceRegister& SolarRegister, float Δt) noexcept
{
    const uint32_t Count = SolarRegister.QueryActiveCount();
    if (Count == 0u)
    {
        return 0u;
    }

    bool AnimatedStepRequired = false;
    if (Δt > 0.0f)
    {
        for (uint32_t i = 0u; i < Count; ++i)
        {
            if ((SolarRegister.VisibilityEnabled[i] != 0u) &&
                (SolarRegister.CycleAnimationEnabled[i] != 0u) &&
                (SolarRegister.DayCycleSpeedDegreesS[i] != 0.0f))
            {
                AnimatedStepRequired = true;
                break;
            }
        }
    }

    if (!SolarRegister.RequireRebuild && !AnimatedStepRequired)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (SolarRegister.VisibilityEnabled[i] == 0u)
        {
            continue;
        }

        if ((Δt > 0.0f) && (SolarRegister.CycleAnimationEnabled[i] != 0u))
        {
            SolarRegister.AzimuthDegrees[i] = WrapDegrees360(
                SolarRegister.AzimuthDegrees[i] + SolarRegister.DayCycleSpeedDegreesS[i] * Δt);
        }
        ++EvaluatedCount;
    }

    SolarRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteLunarLoop(LunarInstanceRegister& LunarRegister, float Δt) noexcept
{
    const uint32_t Count = LunarRegister.QueryActiveCount();
    if (Count == 0u)
    {
        return 0u;
    }

    bool AnimatedStepRequired = false;
    if (Δt > 0.0f)
    {
        for (uint32_t i = 0u; i < Count; ++i)
        {
            if ((LunarRegister.VisibilityEnabled[i] != 0u) &&
                (LunarRegister.OrbitalRateDegreesPerS[i] != 0.0f))
            {
                AnimatedStepRequired = true;
                break;
            }
        }
    }

    if (!LunarRegister.RequireRebuild && !AnimatedStepRequired)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (LunarRegister.VisibilityEnabled[i] == 0u)
        {
            continue;
        }

        if ((Δt > 0.0f) && (LunarRegister.OrbitalRateDegreesPerS[i] != 0.0f))
        {
            LunarRegister.RightAscensionDegrees[i] = WrapDegrees360(
                LunarRegister.RightAscensionDegrees[i] + LunarRegister.OrbitalRateDegreesPerS[i] * Δt);
        }
        ++EvaluatedCount;
    }

    LunarRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteAtmosphereLoop(AtmosphereInstanceRegister& AtmosphereRegister, float /*Δt*/) noexcept
{
    const uint32_t Count = AtmosphereRegister.QueryActiveCount();
    if ((Count == 0u) || !AtmosphereRegister.RequireRebuild)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (AtmosphereRegister.VisibilityEnabled[i] != 0u)
        {
            ++EvaluatedCount;
        }
    }

    AtmosphereRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteCloudLoop(CloudInstanceRegister& CloudRegister, float Δt) noexcept
{
    const uint32_t Count = CloudRegister.QueryActiveCount();
    if (Count == 0u)
    {
        return 0u;
    }

    bool AdvectionRequired = false;
    if (Δt > 0.0f)
    {
        for (uint32_t i = 0u; i < Count; ++i)
        {
            if ((CloudRegister.VisibilityEnabled[i] != 0u) && (CloudRegister.WindSpeedMetersPerS[i] != 0.0f))
            {
                AdvectionRequired = true;
                break;
            }
        }
    }

    if (!CloudRegister.RequireRebuild && !AdvectionRequired)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (CloudRegister.VisibilityEnabled[i] == 0u)
        {
            continue;
        }

        if (Δt > 0.0f)
        {
            CloudRegister.AdvectionOffsetMeters[i] += CloudRegister.WindSpeedMetersPerS[i] * Δt;
        }
        ++EvaluatedCount;
    }

    CloudRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteLuminaireLoop(LuminaireInstanceRegister& LuminaireRegister, float /*Δt*/) noexcept
{
    const uint32_t Count = LuminaireRegister.QueryActiveCount();
    if ((Count == 0u) || !LuminaireRegister.RequireRebuild)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (LuminaireRegister.VisibilityEnabled[i] != 0u)
        {
            ++EvaluatedCount;
        }
    }

    LuminaireRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteForceFieldLoop(ForceFieldInstanceRegister& ForceFieldRegister, float /*Δt*/) noexcept
{
    const uint32_t Count = ForceFieldRegister.QueryActiveCount();
    if ((Count == 0u) || !ForceFieldRegister.RequireRebuild)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (ForceFieldRegister.FieldActive[i] != 0u)
        {
            ++EvaluatedCount;
        }
    }

    ForceFieldRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteTriggerLoop(WorldInstanceRegister& WorldRegister, float /*Δt*/) noexcept
{
    TriggerInstanceRegister& Triggers = WorldRegister.AccessTriggerRegister();
    const uint32_t TriggerCount       = Triggers.QueryActiveCount();
    if (TriggerCount == 0u)
    {
        return 0u;
    }

    const WorldTransformStorage& Transforms = WorldRegister.InspectTransforms();
    const uint32_t TransformCount           = Transforms.QueryActiveCount();
    uint32_t EvaluatedCount                 = 0u;

    for (uint32_t t = 0u; t < TriggerCount; ++t)
    {
        if (Triggers.SensorActive[t] == 0u)
        {
            Triggers.OverlapCountReading[t] = 0u;
            continue;
        }

        const InstanceRecordEntry* TriggerEntry = WorldRegister.FindRecordEntry(Triggers.OwnerTokens[t]);
        if ((TriggerEntry == nullptr) || (TriggerEntry->TransformSlot == InvalidInstanceSlot))
        {
            Triggers.OverlapCountReading[t] = 0u;
            continue;
        }

        const uint32_t CenterSlot = TriggerEntry->TransformSlot;
        const float CenterX       = Transforms.PositionMetersX[CenterSlot];
        const float CenterY       = Transforms.PositionMetersY[CenterSlot];
        const float CenterZ       = Transforms.PositionMetersZ[CenterSlot];
        const float HalfX         = Triggers.HalfExtentMetersX[t];
        const float HalfY         = Triggers.HalfExtentMetersY[t];
        const float HalfZ         = Triggers.HalfExtentMetersZ[t];

        uint32_t OverlapCount = 0u;
        for (uint32_t p = 0u; p < TransformCount; ++p)
        {
            if (p == CenterSlot)
            {
                continue;
            }

            if ((std::fabs(Transforms.PositionMetersX[p] - CenterX) <= HalfX) &&
                (std::fabs(Transforms.PositionMetersY[p] - CenterY) <= HalfY) &&
                (std::fabs(Transforms.PositionMetersZ[p] - CenterZ) <= HalfZ))
            {
                ++OverlapCount;
            }
        }

        Triggers.OverlapCountReading[t] = OverlapCount;
        ++EvaluatedCount;
    }

    Triggers.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteParticleLoop(ParticleInstanceRegister& ParticleRegister, float Δt) noexcept
{
    const uint32_t Count = ParticleRegister.QueryActiveCount();
    if ((Count == 0u) || (Δt <= 0.0f && !ParticleRegister.RequireRebuild))
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (ParticleRegister.SimulationActive[i] == 0u)
        {
            continue;
        }

        if (Δt > 0.0f)
        {
            ParticleRegister.AccumulatedClockSeconds[i] += Δt;
        }
        ++EvaluatedCount;
    }

    ParticleRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteAcousticLoop(AcousticInstanceRegister& AcousticRegister, float /*Δt*/) noexcept
{
    const uint32_t Count = AcousticRegister.QueryActiveCount();
    if ((Count == 0u) || !AcousticRegister.RequireRebuild)
    {
        return 0u;
    }

    uint32_t EvaluatedCount = 0u;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if (AcousticRegister.PlaybackActive[i] != 0u)
        {
            ++EvaluatedCount;
        }
    }

    AcousticRegister.RequireRebuild = false;
    return EvaluatedCount;
}

uint32_t ExecuteTransformLoop(WorldInstanceRegister& WorldRegister) noexcept
{
    WorldTransformStorage& Transforms = WorldRegister.AccessTransforms();
    if (!Transforms.RequireRebuild)
    {
        return 0u;
    }

    uint32_t UploadedCount = 0u;
    for (const uint32_t Slot : Transforms.RebuildTransformSlots)
    {
        if (Slot >= Transforms.TransformRequireRebuild.size())
        {
            continue;
        }

        if (Transforms.TransformRequireRebuild[Slot] != 0u)
        {
            Transforms.TransformRequireRebuild[Slot] = 0u;
            if (InstanceRecordEntry* Entry = WorldRegister.AccessRecordEntry(Transforms.OwnerTokens[Slot]))
            {
                Entry->DirectiveMask &= ~static_cast<uint32_t>(InstanceDirectiveMask::EnableTransformRebuild);
            }
            ++UploadedCount;
        }
    }

    Transforms.RebuildTransformSlots.clear();
    Transforms.RequireRebuild = false;
    return UploadedCount;
}

WorldLoopStepMetrics ExecuteWorldSimulationSequence(
    WorldInstanceRegister& WorldRegister,
    float                  Δt,
    bool                   ExecutionEnabled) noexcept
{
    WorldLoopStepMetrics Metrics{};

    // Transform uploads & environment RequireRebuild always remain responsive in Editor
    Metrics.UploadedTransformCount   = ExecuteTransformLoop(WorldRegister);
    Metrics.EvaluatedAtmosphereCount = ExecuteAtmosphereLoop(WorldRegister.AccessAtmosphereRegister(), Δt);
    Metrics.EvaluatedLuminaireCount  = ExecuteLuminaireLoop(WorldRegister.AccessLuminaireRegister(), Δt);

    if (!ExecutionEnabled)
    {
        // Authoring mode: evaluate only RequireRebuild changes with zero time progression
        Metrics.EvaluatedSolarCount      = ExecuteSolarLoop(WorldRegister.AccessSolarRegister(), 0.0f);
        Metrics.EvaluatedLunarCount      = ExecuteLunarLoop(WorldRegister.AccessLunarRegister(), 0.0f);
        Metrics.EvaluatedCloudCount      = ExecuteCloudLoop(WorldRegister.AccessCloudRegister(), 0.0f);
        Metrics.EvaluatedForceFieldCount = ExecuteForceFieldLoop(WorldRegister.AccessForceFieldRegister(), 0.0f);
        Metrics.EvaluatedAcousticCount   = ExecuteAcousticLoop(WorldRegister.AccessAcousticRegister(), 0.0f);
        return Metrics;
    }

    // Active play / simulation mode: linear cache-coherent SoA sweeps
    Metrics.EvaluatedSolarCount      = ExecuteSolarLoop(WorldRegister.AccessSolarRegister(), Δt);
    Metrics.EvaluatedLunarCount      = ExecuteLunarLoop(WorldRegister.AccessLunarRegister(), Δt);
    Metrics.EvaluatedCloudCount      = ExecuteCloudLoop(WorldRegister.AccessCloudRegister(), Δt);
    Metrics.EvaluatedForceFieldCount = ExecuteForceFieldLoop(WorldRegister.AccessForceFieldRegister(), Δt);
    Metrics.EvaluatedTriggerCount    = ExecuteTriggerLoop(WorldRegister, Δt);
    Metrics.EvaluatedParticleCount   = ExecuteParticleLoop(WorldRegister.AccessParticleRegister(), Δt);
    Metrics.EvaluatedAcousticCount   = ExecuteAcousticLoop(WorldRegister.AccessAcousticRegister(), Δt);
    return Metrics;
}

} // namespace Frontier::World
