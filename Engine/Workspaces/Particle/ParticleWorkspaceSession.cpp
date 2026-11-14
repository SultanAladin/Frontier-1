/*============================================================================================================================================
                                                       PARTICLEWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceSession (Studio Default scene, Vector Field Forces coupling, 93-preset Asset Browser metadata & playback)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using SimulationSpace::ForceFields::FlowStreamProfile;
using SimulationSpace::ForceFields::ForceAttenuationMode;
using SimulationSpace::ForceFields::ForceDomainCategory;
using SimulationSpace::ForceFields::ForceDurationMode;
using SimulationSpace::ForceFields::ForceModel;
using SimulationSpace::ForceFields::ForceSourceRecordEntry;
using SimulationSpace::ForceFields::ReachEnvelopeCategory;

[[nodiscard]] ForceSourceRecordEntry MakeForce(
    uint32_t              Token,
    const char*           Label,
    ForceModel            Model,
    ForceDomainCategory   Domain,
    float                 Strength,
    float                 Radius,
    float                 Cx,
    float                 Cy,
    float                 Cz,
    ForceDurationMode     Duration = ForceDurationMode::Continuous)
{
    ForceSourceRecordEntry F {};
    F.UniqueToken   = Token;
    F.Label         = Label;
    F.Model         = Model;
    F.Domain        = Domain;
    F.DurationMode  = Duration;
    F.Strength      = Strength;
    F.RadiusMeters  = Radius;
    F.CentreMeters  = { Cx, Cy, Cz };
    F.EvaluatedCentreMeters = F.CentreMeters;
    F.Enabled       = true;
    F.Visible       = true;
    return F;
}

[[nodiscard]] ColourSpace::ColourSourceProfile MakeBokehSilverCyanRamp()
{
    ColourSpace::ColourSourceProfile Profile {};
    ColourSpace::MultiStopGradientSource Grad {};
    Grad.Stops = {
        { 0.00f, ColourSpace::ConvertHexSrgbToLinear("#4D8898", 1.0f) },
        { 0.48f, ColourSpace::ConvertHexSrgbToLinear("#9CDCE6", 1.0f) },
        { 1.00f, ColourSpace::ConvertHexSrgbToLinear("#F5FBFF", 1.0f) },
    };
    Profile.Source        = std::move(Grad);
    Profile.BlendEquation = ColourSpace::OpticalBlendEquation::Additive;
    return Profile;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                CONSTRUCTION & SEEDING
//------------------------------------------------------------------------------------------------------------------------

ParticleWorkspaceSession::ParticleWorkspaceSession()
{
    (void)ColourCoordinator.Initialize();
    ResetStudioDefaultScene();
}

void ParticleWorkspaceSession::ResetStudioDefaultScene()
{
    SceneTabs = {
        { "studio_default", "Studio Default" },
        { "object_placer",  "3D Object Placer · MoGr..." },
        { "effector_grids", "Effector Grids & Halftones" },
        { "cloth_sails",    "Cloth, Sails, Cables" },
    };
    ActiveSceneIndex = 0u;
    ActiveNavView    = ParticleNavView::Workspace;

    Playing           = true;
    ElapsedSeconds    = 0.166;
    LoopDurationSec   = 30.0;
    StepCounter       = 4u;
    TimeScale         = 1.0f;
    BurstCount        = 0u;
    ParameterRevision = 0u;

    AmbientWind = { true, 2.2f, 68.0f, 0.45f };

    // 6 bounded ForceField sources reusing Frontier::SimulationSpace::ForceFields verbatim
    ForceSources = {
        MakeForce(1u, "Wind Generator East", ForceModel::LinearWind,       ForceDomainCategory::FlowField,   8.5f,  4.2f, -3.2f, 1.8f, 0.0f),
        MakeForce(2u, "Swirl Column",        ForceModel::RotationalColumn, ForceDomainCategory::VectorField, 14.5f, 3.6f,  2.6f, 2.0f, -1.4f),
        MakeForce(3u, "Jet Corridor",        ForceModel::FlowStream,       ForceDomainCategory::FlowField,   11.5f, 2.2f, -1.5f, 2.4f,  2.8f),
        MakeForce(4u, "Impulse Shell",       ForceModel::RadialBurst,      ForceDomainCategory::VectorField, 18.0f, 3.4f,  0.0f, 1.5f,  0.0f, ForceDurationMode::Impulse),
        MakeForce(5u, "Attractor",           ForceModel::Attractor,        ForceDomainCategory::VectorField,  9.5f, 2.8f,  2.8f, 1.6f,  2.1f),
        MakeForce(6u, "Repulsor",            ForceModel::Repulsor,         ForceDomainCategory::VectorField, 11.5f, 2.2f, -2.6f, 1.4f, -2.2f),
    };
    ForceSources[2].StreamProfile = FlowStreamProfile::SlipstreamJet;

    // 6 Studio Default Particle Rigs (total capacity 13,300, total alive 6,512 matching HTML Studio Default!)
    ParticleRigs.clear();
    {
        AuthoredParticleRigUi Pour {};
        Pour.Identifier         = "object_surface_pour_ribbons";
        Pour.DisplayName        = "Object Surface Pour Ribbons";
        Pour.CategoryTitle      = "Silk & Curl";
        Pour.FilterTag          = "Curl";
        Pour.BadgeTypeLine      = "VFX · ADD";
        Pour.ShapeTitle         = "Bokeh Bead";
        Pour.ShapeCaption       = "additive glow";
        Pour.ManifoldTitle      = "Bokeh ribbon";
        Pour.ManifoldCaption    = "1.6 m/s stream";
        Pour.CapacityCount      = 3400u;
        Pour.AliveCount         = 3186u;
        Pour.ReleaseRatePerSec  = 620.0f;
        Pour.OriginCentreMeters = { 0.0f, 2.25f, 0.0f };
        Pour.OriginHalfExtentsM = { 1.65f, 1.65f, 1.65f };
        Pour.LifeMinSeconds     = 3.40f;
        Pour.LifeMaxSeconds     = 5.20f;
        Pour.WindCouplingRate   = 0.25f;
        Pour.SizeAtBirthMeters  = 0.048f;
        Pour.SizeAtDeathMeters  = 0.032f;
        Pour.ColourProfile      = MakeBokehSilverCyanRamp();
        ParticleRigs.push_back(std::move(Pour));

        AuthoredParticleRigUi CurlSilk {};
        CurlSilk.Identifier       = "planar_curl_silk";
        CurlSilk.DisplayName      = "Planar Curl Silk";
        CurlSilk.CategoryTitle    = "Silk & Curl";
        CurlSilk.FilterTag        = "Curl";
        CurlSilk.CapacityCount    = 2400u;
        CurlSilk.AliveCount       = 1120u;
        CurlSilk.ReleaseRatePerSec= 420.0f;
        CurlSilk.ColourProfile    = ColourSpace::ConstructProfileFromRampPreset(*ColourSpace::FindBuiltInColourRampPreset("aurora"));
        ParticleRigs.push_back(std::move(CurlSilk));

        AuthoredParticleRigUi SphericalPlasma {};
        SphericalPlasma.Identifier       = "spherical_curl_plasma";
        SphericalPlasma.DisplayName      = "Spherical Curl Plasma";
        SphericalPlasma.CategoryTitle    = "Silk & Curl";
        SphericalPlasma.FilterTag        = "Curl";
        SphericalPlasma.CapacityCount    = 2200u;
        SphericalPlasma.AliveCount       = 840u;
        SphericalPlasma.ReleaseRatePerSec= 380.0f;
        SphericalPlasma.ColourProfile    = ColourSpace::ConstructProfileFromRampPreset(*ColourSpace::FindBuiltInColourRampPreset("plasma"));
        ParticleRigs.push_back(std::move(SphericalPlasma));

        AuthoredParticleRigUi BokehStreams {};
        BokehStreams.Identifier       = "bokeh_stream_ribbons";
        BokehStreams.DisplayName      = "Bokeh Stream Ribbons";
        BokehStreams.CategoryTitle    = "Silk & Curl";
        BokehStreams.FilterTag        = "Curl";
        BokehStreams.CapacityCount    = 1900u;
        BokehStreams.AliveCount       = 516u;
        BokehStreams.ReleaseRatePerSec= 310.0f;
        BokehStreams.ColourProfile    = ColourSpace::ConstructProfileFromRampPreset(*ColourSpace::FindBuiltInColourRampPreset("ocean"));
        ParticleRigs.push_back(std::move(BokehStreams));

        AuthoredParticleRigUi Sparks {};
        Sparks.Identifier       = "sparks";
        Sparks.DisplayName      = "Sparks";
        Sparks.CategoryTitle    = "Ballistics & Sparks";
        Sparks.FilterTag        = "Sparks";
        Sparks.ShapeTitle       = "Streak";
        Sparks.ManifoldTitle    = "Ballistic arc";
        Sparks.FloorBounce      = true;
        Sparks.FloorBounceLocked= false;
        Sparks.CapacityCount    = 1800u;
        Sparks.AliveCount       = 490u;
        Sparks.ReleaseRatePerSec= 280.0f;
        Sparks.ColourProfile    = ColourSpace::ConstructProfileFromRampPreset(*ColourSpace::FindBuiltInColourRampPreset("thermal"));
        ParticleRigs.push_back(std::move(Sparks));

        AuthoredParticleRigUi Fire {};
        Fire.Identifier       = "fire";
        Fire.DisplayName      = "Fire";
        Fire.CategoryTitle    = "Pyro & Water";
        Fire.FilterTag        = "Pyro";
        Fire.ShapeTitle       = "Soft Glow";
        Fire.ManifoldTitle    = "Buoyant plume";
        Fire.CapacityCount    = 1600u;
        Fire.AliveCount       = 360u;
        Fire.ReleaseRatePerSec= 240.0f;
        Fire.ColourProfile    = ColourSpace::ConstructProfileFromRampPreset(*ColourSpace::FindBuiltInColourRampPreset("sunset"));
        ParticleRigs.push_back(std::move(Fire));
    }

    SelectionKind    = ParticleSelectionCategory::ParticleRig;
    SelectedRigIndex = 0u;
    InspectorTab     = ParticleInspectorTab::Overview;

    // Seed Assets Preset Browser cards matching HTML Studio's exact front-page preset lineup + full 93-active count
    AssetPresets = {
        { "coin_fountain",       "Coin fountain",       "Ballistics & Sparks", "BALLISTICS & SPARKS · COIN · VFX",       "Gold coins fired upward from one point, arcing over and bouncing onto the floor.",           "thermal",  false, false },
        { "magnetic_particles",  "Magnetic particles",  "Ballistics & Sparks", "BALLISTICS & SPARKS · DIPOLE · FIELDS",  "Glowing particles steered by a magnetic dipole following local field lines.",               "sunset",   false, false },
        { "bubbles",             "Bubbles",             "Pyro & Water",        "PYRO & WATER · BUBBLE · WATER",          "Soap bubbles: transparent spheres with fresnel rim rising on buoyancy and wind.",           "ocean",    false, false },
        { "foam",                "Foam",                "Pyro & Water",        "PYRO & WATER · BUBBLE · WATER",          "Dense small bubbles that bunch up and bob near the floor pushed by ambient wind.",         "ocean",    false, false },
        { "sparks",              "Sparks",              "Ballistics & Sparks", "BALLISTICS & SPARKS · STREAK · EFFECTS", "Hot metal sparks: gravity, aerodynamic drag, ground bounce and wind coupling.",            "thermal",  true,  false },
        { "rain_streaks",        "Rain streaks",        "Storms & Weather",    "STORMS & WEATHER · STREAK · WEATHER",    "High-speed rain sheets falling through wind and bursting into ground splash rings.",       "ocean",    false, false },
        { "hail",                "Hail",                "Storms & Weather",    "STORMS & WEATHER · ICE HAIL · WEATHER",  "Translucent faceted ice pellets dropping fast and ricocheting off ground and hulls.",      "ocean",    false, false },
        { "leaves",              "Leaves",              "Wind-Blown",          "WIND-BLOWN · WIND-DRIVEN",               "Leaves released from a canopy; flutter, tumble and follow the active wind field.",         "viridis",  false, false },
        { "paper_debris",        "Paper & debris",      "Wind-Blown",          "WIND-BLOWN · WIND-DRIVEN",               "Light paper sheets with strong wind coupling and aerodynamic flutter sway.",               "grayscale",false, false },
        { "fire",                "Fire",                "Pyro & Water",        "PYRO & WATER · PYRO / GLOW · VFX",       "Buoyant additive flame tongues bent smoothly by the active force field.",                  "thermal",  true,  false },
        { "smoke",               "Smoke",               "Pyro & Water",        "PYRO & WATER · PYRO / GLOW · VFX",       "Alpha-blended soft smoke plume that expands and drifts through the wind lattice.",         "grayscale",false, false },
        { "explosion",           "Explosion",           "Pyro & Water",        "PYRO & WATER · PYRO / GLOW · VFX",       "Spherical additive shockwave burst triggered via the Burst [B] action.",                   "thermal",  false, false },
        { "tornado_debris",      "Tornado debris",      "Storms & Weather",    "STORMS & WEATHER · WIND-DRIVEN",         "Debris lifted in a helical swirl column driven by the ForceField lattice.",                "magma",    false, false },
        { "sandstorm",           "Sandstorm",           "Storms & Weather",    "STORMS & WEATHER · WIND-DRIVEN",         "Dense desert dust swept across the stage under prevailing 8 m/s wind.",                    "thermal",  false, false },
        { "snow",                "Snow",                "Storms & Weather",    "STORMS & WEATHER · SNOWFLAKE · WEATHER", "Six-point crystalline snowflakes drifting and fluttering softly across the stage.",        "ocean",    false, false },
        { "embers",              "Embers",              "Pyro & Water",        "PYRO & WATER · PYRO / GLOW · VFX",       "Glowing embers lifted on hot air; additive, short-lived, bent by wind gusts.",             "sunset",   false, false },
        { "object_pour",         "Object Surface Pour", "Silk & Curl",         "SILK & CURL · BOKEH RIBBON · VFX",       "Luminous bokeh strands pouring seamlessly over an obsidian 3D backing surface.",           "aurora",   true,  false },
        { "aurora_curtain",      "Aurora Curtain",      "Atmosphere & Sky",    "ATMOSPHERE & SKY · SILK · VFX",          "Shimmering geomagnetic aurora curtains undulating in upper-atmosphere currents.",          "aurora",   false, false },
        { "orb_spiderweb",       "Orb Spiderweb",       "Cloth & Rigging",     "CLOTH & RIGGING · XPBD STRANDS",         "Real-time radial and spiral woven silk strands reacting to wind and thrown ball.",         "grayscale",false, false },
        { "plasma_shield",       "Plasma Hex Shield",   "Shields & Tech",      "SHIELDS & TECH · BARRIER · VFX",         "Energy barrier dome with hexagonal interference ripples and impact flares.",               "plasma",   false, false },
    };

    LogRecords = {
        { "21:03:19", LogChannelCategory::Forces,    "ForceFields solver active — 6 bounded sources + Ambient Wind" },
        { "21:03:19", LogChannelCategory::Particles, "Engine ready · Vulkan 1.3 Compute & Software 3D preview — 6 effects · 93 active presets" },
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                CLOCK & INTERACTIONS
//------------------------------------------------------------------------------------------------------------------------

void ParticleWorkspaceSession::StepSimulationClock(double DeltaSeconds) noexcept
{
    if (!Playing || DeltaSeconds <= 0.0)
    {
        return;
    }
    ElapsedSeconds += DeltaSeconds * static_cast<double>(TimeScale);
    if (ElapsedSeconds >= LoopDurationSec)
    {
        ElapsedSeconds = std::fmod(ElapsedSeconds, LoopDurationSec);
    }
    ++StepCounter;
    ++CompletedSamples;
}

void ParticleWorkspaceSession::TriggerBurst() noexcept
{
    ++BurstCount;
    for (AuthoredParticleRigUi& Rig : ParticleRigs)
    {
        if (Rig.Visible)
        {
            Rig.AliveCount = std::min(Rig.CapacityCount, Rig.AliveCount + Rig.CapacityCount / 6u);
        }
    }
    AppendLog(LogChannelCategory::Particles, "Burst [B] impulse dispatched to active particle rigs");
}

void ParticleWorkspaceSession::ResetPlayback() noexcept
{
    ElapsedSeconds = 0.0;
    StepCounter    = 0u;
    AppendLog(LogChannelCategory::Particles, "Simulation clock reset & particle slots re-seeded");
}

AuthoredParticleRigUi* ParticleWorkspaceSession::AccessSelectedRig() noexcept
{
    if (SelectionKind != ParticleSelectionCategory::ParticleRig || SelectedRigIndex >= ParticleRigs.size())
    {
        return nullptr;
    }
    return &ParticleRigs[SelectedRigIndex];
}

const AuthoredParticleRigUi* ParticleWorkspaceSession::QuerySelectedRig() const noexcept
{
    if (SelectionKind != ParticleSelectionCategory::ParticleRig || SelectedRigIndex >= ParticleRigs.size())
    {
        return nullptr;
    }
    return &ParticleRigs[SelectedRigIndex];
}

ForceSourceRecordEntry* ParticleWorkspaceSession::AccessSelectedForce() noexcept
{
    if (SelectionKind != ParticleSelectionCategory::ForceSource || SelectedForceIdx >= ForceSources.size())
    {
        return nullptr;
    }
    return &ForceSources[SelectedForceIdx];
}

const ForceSourceRecordEntry* ParticleWorkspaceSession::QuerySelectedForce() const noexcept
{
    if (SelectionKind != ParticleSelectionCategory::ForceSource || SelectedForceIdx >= ForceSources.size())
    {
        return nullptr;
    }
    return &ForceSources[SelectedForceIdx];
}

uint32_t ParticleWorkspaceSession::CountVisibleEntities() const noexcept
{
    uint32_t Count = 0u;
    for (const auto& F : ForceSources)
    {
        if (F.Visible) ++Count;
    }
    for (const auto& R : ParticleRigs)
    {
        if (R.Visible) ++Count;
    }
    return Count;
}

uint32_t ParticleWorkspaceSession::CountHiddenEntities() const noexcept
{
    uint32_t Count = 0u;
    for (const auto& F : ForceSources)
    {
        if (!F.Visible) ++Count;
    }
    for (const auto& R : ParticleRigs)
    {
        if (!R.Visible) ++Count;
    }
    return Count;
}

uint32_t ParticleWorkspaceSession::CountTotalAliveParticles() const noexcept
{
    uint32_t Sum = 0u;
    for (const auto& R : ParticleRigs)
    {
        if (R.Visible) Sum += R.AliveCount;
    }
    return Sum;
}

uint32_t ParticleWorkspaceSession::CountTotalParticleCapacity() const noexcept
{
    uint32_t Sum = 0u;
    for (const auto& R : ParticleRigs)
    {
        Sum += R.CapacityCount;
    }
    return Sum;
}

void ParticleWorkspaceSession::SelectRig(std::size_t RigIndex) noexcept
{
    if (RigIndex < ParticleRigs.size())
    {
        SelectionKind    = ParticleSelectionCategory::ParticleRig;
        SelectedRigIndex = RigIndex;
    }
}

void ParticleWorkspaceSession::SelectForceSource(std::size_t ForceIndex) noexcept
{
    if (ForceIndex < ForceSources.size())
    {
        SelectionKind    = ParticleSelectionCategory::ForceSource;
        SelectedForceIdx = ForceIndex;
    }
}

void ParticleWorkspaceSession::SelectAmbientWind() noexcept
{
    SelectionKind = ParticleSelectionCategory::AmbientWind;
}

void ParticleWorkspaceSession::SelectForceFieldRoot() noexcept
{
    SelectionKind = ParticleSelectionCategory::ForceFieldRoot;
}

void ParticleWorkspaceSession::AppendLog(LogChannelCategory Channel, std::string_view Message)
{
    char TimeBuf[16];
    const int TotalSec = static_cast<int>(ElapsedSeconds) % 86400;
    std::snprintf(TimeBuf, sizeof(TimeBuf), "21:%02d:%02d", (TotalSec / 60) % 60, TotalSec % 60);
    LogRecords.push_back({ TimeBuf, Channel, std::string(Message) });
    if (LogRecords.size() > 128u)
    {
        LogRecords.erase(LogRecords.begin());
    }
}

void ParticleWorkspaceSession::RecordParameterEdit(std::string_view /*Summary*/) noexcept
{
    ++ParameterRevision;
}

void ParticleWorkspaceSession::AddPresetToScene(std::size_t AssetIndex)
{
    if (AssetIndex >= AssetPresets.size())
    {
        return;
    }
    ParticleAssetPresetCard& Card = AssetPresets[AssetIndex];
    Card.InScene = true;

    AuthoredParticleRigUi Added {};
    Added.Identifier    = Card.Identifier;
    Added.DisplayName   = Card.Title;
    Added.CategoryTitle = Card.CategoryTitle;
    Added.FilterTag     = "Effects";
    Added.CapacityCount = 2400u;
    Added.AliveCount    = 1480u;
    if (const auto* Preset = ColourSpace::FindBuiltInColourRampPreset(Card.RampPresetId))
    {
        Added.ColourProfile = ColourSpace::ConstructProfileFromRampPreset(*Preset);
    }
    ParticleRigs.push_back(std::move(Added));
    SelectRig(ParticleRigs.size() - 1u);
    ActiveNavView = ParticleNavView::Workspace;
    RecordParameterEdit(Card.Title);
    AppendLog(LogChannelCategory::Particles, "Added preset '" + Card.Title + "' to active scene");
}

} // namespace Frontier::Workspaces
