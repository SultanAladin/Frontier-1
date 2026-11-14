/*============================================================================================================================================
                                                             PARTICLECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of Two-Tier Hybrid .ParticleSpace persistence (PascalCase TOML + 64B-aligned .ColourLut & .SurfaceAnchor sections)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

toml::array PackVec3(const float V[3])
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(V[0]));
    Arr.push_back(static_cast<double>(V[1]));
    Arr.push_back(static_cast<double>(V[2]));
    return Arr;
}

void UnpackVec3(const toml::array* Arr, float Out[3]) noexcept
{
    if (!Arr || Arr->size() < 3u) return;
    for (std::size_t K = 0u; K < 3u; ++K)
    {
        if (const auto V = (*Arr)[K].value<double>())
        {
            Out[K] = static_cast<float>(*V);
        }
    }
}

} // namespace

SpaceContainerPayload ParticleEncoder::BuildPayload(
    const ParticleSpace::ParticleDocument& Document,
    const Workspaces::RevisionTracker*     OptionalRevision)
{
    SpaceContainerPayload Payload {};
    Payload.ExtensionCategory  = SpaceExtensionCategory::ParticleSpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Document.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.ParticleWorkspace";

    toml::table DocTbl {};
    DocTbl.insert("Title",            Document.DocumentTitle);
    DocTbl.insert("CoordinateSystem", Document.CoordinateSystem);
    DocTbl.insert("ActivePresetId",   Document.ActivePresetId);
    DocTbl.insert("Seed",             static_cast<int64_t>(Document.Seed));
    DocTbl.insert("LoopDurationSec",  Document.Clock.LoopDurationSec);
    DocTbl.insert("TimeScale",        static_cast<double>(Document.Clock.TimeScale));
    DocTbl.insert("FixedStepHz",      static_cast<int64_t>(Document.Clock.FixedStepHz));
    Payload.DomainManifest.insert("Document", std::move(DocTbl));

    toml::table WindTbl {};
    WindTbl.insert("Enabled",        Document.AmbientWind.Enabled);
    WindTbl.insert("SpeedMps",       static_cast<double>(Document.AmbientWind.SpeedMps));
    WindTbl.insert("AzimuthDegrees", static_cast<double>(Document.AmbientWind.AzimuthDegrees));
    WindTbl.insert("GustAmplitude",  static_cast<double>(Document.AmbientWind.GustAmplitude));
    Payload.DomainManifest.insert("AmbientWind", std::move(WindTbl));

    toml::table CollTbl {};
    CollTbl.insert("BallActive",       Document.Collider.BallActive);
    CollTbl.insert("BallCenterM",      PackVec3(Document.Collider.BallCenterM));
    CollTbl.insert("BallRadiusMeters", static_cast<double>(Document.Collider.BallRadiusMeters));
    CollTbl.insert("BallRestitution",  static_cast<double>(Document.Collider.BallRestitution));
    Payload.DomainManifest.insert("InteractiveCollider", std::move(CollTbl));

    // Reuse canonical VectorFieldCodec inline serialization directly!
    SerializeInlineForceSourcesToManifest(Document.ForceSources, Document.LinkedVectorFieldAssets, Payload.DomainManifest);

    // Serialize modular ParticleRig entries + bake 64B-aligned .ColourLut sections in Tier 2
    toml::array RigsArr {};
    for (std::size_t I = 0u; I < Document.Rigs.size(); ++I)
    {
        const auto& Rig = Document.Rigs[I];
        toml::table R {};
        R.insert("Identifier",        Rig.Identifier);
        R.insert("DisplayName",       Rig.DisplayName);
        R.insert("CategoryLabel",     Rig.CategoryLabel);
        R.insert("Visible",           Rig.Visible);
        R.insert("SlotCapacity",      static_cast<int64_t>(Rig.SlotCapacity));
        R.insert("ReleaseRatePerSec", static_cast<double>(Rig.ReleaseRatePerSec));
        R.insert("LocalSeed",         static_cast<int64_t>(Rig.LocalSeed));
        R.insert("TintChannel",       std::string(ParticleSpace::QueryScalarChannelKey(Rig.TintDriver.Channel)));

        // Origin sub-table
        toml::table Orig {};
        Orig.insert("Category", static_cast<int64_t>(ParticleSpace::QueryReleaseOriginCategory(Rig.Origin)));
        if (const auto* Surf = std::get_if<ParticleSpace::SurfaceAnchorOrigin>(&Rig.Origin))
        {
            Orig.insert("SurfacePreset",    static_cast<int64_t>(Surf->Preset));
            Orig.insert("SamplingStrategy", static_cast<int64_t>(Surf->SamplingStrategy));
            Orig.insert("CenterMeters",     PackVec3(Surf->CenterMeters));
            Orig.insert("ScaleMeters",      PackVec3(Surf->ScaleMeters));
            Orig.insert("EulerDegrees",     PackVec3(Surf->EulerDegrees));
            if (!Surf->CustomAssetId.empty()) Orig.insert("CustomAssetId", Surf->CustomAssetId);

            // Cache deterministic Tier 2 .SurfaceAnchor binary section for the first 256 anchors
            const auto Anchors = ParticleSpace::SampleProceduralSurfaceAnchors(*Surf, 256u, Rig.LocalSeed);
            BinarySectionBlock AncSec {};
            AncSec.SectionIdentifier = "SurfaceAnchor_" + std::to_string(I);
            AncSec.SubFormatTag      = ".SurfaceAnchor";
            AncSec.PayloadBytes.resize(Anchors.size() * sizeof(ParticleSpace::ParticleAnchorSample));
            if (!Anchors.empty())
            {
                std::memcpy(AncSec.PayloadBytes.data(), Anchors.data(), AncSec.PayloadBytes.size());
            }
            Payload.BinarySections.push_back(std::move(AncSec));
        }
        else if (const auto* Fw = std::get_if<ParticleSpace::FireworkOrigin>(&Rig.Origin))
        {
            Orig.insert("FireworkShape",  static_cast<int64_t>(Fw->Shape));
            Orig.insert("SkyTextMessage", Fw->SkyTextMessage);
            Orig.insert("ShellOrigin",    PackVec3(Fw->ShellOrigin));
            Orig.insert("PatternRadius",  static_cast<double>(Fw->PatternRadius));
        }
        else if (const auto* Pt = std::get_if<ParticleSpace::AnalyticPointOrigin>(&Rig.Origin))
        {
            Orig.insert("CenterMeters", PackVec3(Pt->CenterMeters));
        }
        else if (const auto* Sph = std::get_if<ParticleSpace::AnalyticSphereOrigin>(&Rig.Origin))
        {
            Orig.insert("CenterMeters", PackVec3(Sph->CenterMeters));
            Orig.insert("RadiusMeters", static_cast<double>(Sph->RadiusMeters));
            Orig.insert("ShellOnly",    Sph->ShellOnly);
        }
        else if (const auto* Box = std::get_if<ParticleSpace::AnalyticBoxOrigin>(&Rig.Origin))
        {
            Orig.insert("CenterMeters",      PackVec3(Box->CenterMeters));
            Orig.insert("HalfExtentsMeters", PackVec3(Box->HalfExtentsMeters));
        }
        R.insert("Origin", std::move(Orig));

        // Kinematics sub-table
        toml::table Kin {};
        Kin.insert("LaunchDirection", PackVec3(Rig.Kinematics.LaunchDirection));
        Kin.insert("SpreadRadians",   static_cast<double>(Rig.Kinematics.SpreadRadians));
        Kin.insert("SpeedMinMps",     static_cast<double>(Rig.Kinematics.SpeedMinMps));
        Kin.insert("SpeedMaxMps",     static_cast<double>(Rig.Kinematics.SpeedMaxMps));
        Kin.insert("LifeMinSeconds",  static_cast<double>(Rig.Kinematics.LifeMinSeconds));
        Kin.insert("LifeMaxSeconds",  static_cast<double>(Rig.Kinematics.LifeMaxSeconds));
        Kin.insert("SizeBirthMeters", static_cast<double>(Rig.Kinematics.SizeBirthMeters));
        Kin.insert("SizeDeathMeters", static_cast<double>(Rig.Kinematics.SizeDeathMeters));
        Kin.insert("SizeMultiplier",  static_cast<double>(Rig.Kinematics.SizeMultiplier));
        R.insert("Kinematics", std::move(Kin));

        // Carrier sub-table
        toml::table Car {};
        Car.insert("Category", static_cast<int64_t>(ParticleSpace::QueryVisualCarrierCategory(Rig.Carrier)));
        if (const auto* Spr = std::get_if<ParticleSpace::SpriteBillboardCarrier>(&Rig.Carrier))
        {
            Car.insert("SpriteCutout",   static_cast<int64_t>(Spr->Cutout));
            Car.insert("BokehAperture",  static_cast<int64_t>(Spr->BokehAperture));
            Car.insert("BokehHeadScale", static_cast<double>(Spr->BokehHeadScale));
            Car.insert("StreakThinness", static_cast<double>(Spr->StreakThinness));
        }
        R.insert("Carrier", std::move(Car));

        // Delegate colour profile directly to Engine/Codecs/ColourCodec!
        R.insert("Colour", ColourEncoder::SerializeSourceToManifest(Rig.ColourProfile));

        // Dynamics sub-table
        toml::table Dyn {};
        Dyn.insert("GravityScale",     static_cast<double>(Rig.Dynamics.Ballistic.GravityScale));
        Dyn.insert("AerodynamicDrag",  static_cast<double>(Rig.Dynamics.Ballistic.AerodynamicDrag));
        Dyn.insert("BuoyancyMps2",     static_cast<double>(Rig.Dynamics.Ballistic.BuoyancyMps2));
        Dyn.insert("FloorRestitution", static_cast<double>(Rig.Dynamics.Ballistic.FloorRestitution));
        Dyn.insert("WindCouplingRate", static_cast<double>(Rig.Dynamics.ForceCouple.WindCouplingRate));
        if (Rig.Dynamics.Curl.has_value())
        {
            toml::table Curl {};
            Curl.insert("ManifoldMode",    static_cast<int64_t>(Rig.Dynamics.Curl->ManifoldMode));
            Curl.insert("FrequencyPerM",   static_cast<double>(Rig.Dynamics.Curl->FrequencyPerM));
            Curl.insert("DriveSpeedMps",   static_cast<double>(Rig.Dynamics.Curl->DriveSpeedMps));
            Curl.insert("EvolutionRateHz", static_cast<double>(Rig.Dynamics.Curl->EvolutionRateHz));
            Curl.insert("ShellRadiusM",    static_cast<double>(Rig.Dynamics.Curl->ShellRadiusM));
            Dyn.insert("Curl", std::move(Curl));
        }
        R.insert("Dynamics", std::move(Dyn));

        // Bake 64-sample linear RGBA LUT section in Tier 2 for instant zero-CPU GPU upload
        const ColourSpace::ColourRampLut64 Lut = ColourSpace::PackColourRampLut64(Rig.ColourProfile);
        BinarySectionBlock LutSec {};
        LutSec.SectionIdentifier = "ColourLut_" + std::to_string(I);
        LutSec.SubFormatTag      = ".ColourLut";
        LutSec.PayloadBytes.resize(sizeof(Lut));
        std::memcpy(LutSec.PayloadBytes.data(), &Lut, sizeof(Lut));
        Payload.BinarySections.push_back(std::move(LutSec));

        // Bake deterministic Tier 2 .StrandRest rest-length table whenever structural strands/webs/cloth/flora are active
        if (Rig.Dynamics.ElasticStrand.has_value()
         || Rig.Dynamics.Strand.has_value()
         || Rig.Dynamics.Spiderweb.has_value()
         || std::holds_alternative<ParticleSpace::SpiderwebOrigin>(Rig.Origin)
         || std::holds_alternative<ParticleSpace::ClothGridOrigin>(Rig.Origin)
         || std::holds_alternative<ParticleSpace::BotanicalRootOrigin>(Rig.Origin))
        {
            const float BaseRest = Rig.Dynamics.ElasticStrand.has_value() ? Rig.Dynamics.ElasticStrand->RestSegmentMeters : 0.16f;
            std::vector<float> RestLengths(128u, BaseRest);
            BinarySectionBlock RestSec {};
            RestSec.SectionIdentifier = "StrandRest_" + std::to_string(I);
            RestSec.SubFormatTag      = ".StrandRest";
            RestSec.PayloadBytes.resize(RestLengths.size() * sizeof(float));
            std::memcpy(RestSec.PayloadBytes.data(), RestLengths.data(), RestSec.PayloadBytes.size());
            Payload.BinarySections.push_back(std::move(RestSec));
        }

        RigsArr.push_back(std::move(R));
    }
    Payload.DomainManifest.insert("ParticleRig", std::move(RigsArr));

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool ParticleEncoder::EncodeToFile(
    const std::filesystem::path&           TargetPath,
    const ParticleSpace::ParticleDocument& Document,
    std::string&                           OutDiagnostic,
    const Workspaces::RevisionTracker*     OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Document, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

bool ParticleDecoder::ExtractDocument(
    const SpaceContainerPayload&     Payload,
    ParticleSpace::ParticleDocument& OutDocument,
    std::string&                     OutDiagnostic,
    Workspaces::RevisionTracker*     OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ParticleSpace)
    {
        OutDiagnostic = "ParticleDecoder rejected container whose ExtensionCategory is not ParticleSpace.";
        return false;
    }

    OutDocument = {};
    OutDocument.DocumentTitle = Payload.DocumentTitle;

    if (const auto* DocTbl = Payload.DomainManifest["Document"].as_table())
    {
        if (const auto V = (*DocTbl)["Title"].value<std::string>())            OutDocument.DocumentTitle         = *V;
        if (const auto V = (*DocTbl)["CoordinateSystem"].value<std::string>()) OutDocument.CoordinateSystem      = *V;
        if (const auto V = (*DocTbl)["ActivePresetId"].value<std::string>())   OutDocument.ActivePresetId        = *V;
        if (const auto V = (*DocTbl)["Seed"].value<int64_t>())                 OutDocument.Seed                  = static_cast<uint32_t>(*V);
        if (const auto V = (*DocTbl)["LoopDurationSec"].value<double>())       OutDocument.Clock.LoopDurationSec = *V;
        if (const auto V = (*DocTbl)["TimeScale"].value<double>())             OutDocument.Clock.TimeScale       = static_cast<float>(*V);
        if (const auto V = (*DocTbl)["FixedStepHz"].value<int64_t>())          OutDocument.Clock.FixedStepHz     = static_cast<uint32_t>(*V);
    }

    if (const auto* WindTbl = Payload.DomainManifest["AmbientWind"].as_table())
    {
        if (const auto V = (*WindTbl)["Enabled"].value<bool>())        OutDocument.AmbientWind.Enabled        = *V;
        if (const auto V = (*WindTbl)["SpeedMps"].value<double>())     OutDocument.AmbientWind.SpeedMps       = static_cast<float>(*V);
        if (const auto V = (*WindTbl)["AzimuthDegrees"].value<double>())OutDocument.AmbientWind.AzimuthDegrees= static_cast<float>(*V);
        if (const auto V = (*WindTbl)["GustAmplitude"].value<double>())OutDocument.AmbientWind.GustAmplitude  = static_cast<float>(*V);
    }

    if (const auto* CollTbl = Payload.DomainManifest["InteractiveCollider"].as_table())
    {
        if (const auto V = (*CollTbl)["BallActive"].value<bool>())         OutDocument.Collider.BallActive       = *V;
        UnpackVec3((*CollTbl)["BallCenterM"].as_array(), OutDocument.Collider.BallCenterM);
        if (const auto V = (*CollTbl)["BallRadiusMeters"].value<double>()) OutDocument.Collider.BallRadiusMeters = static_cast<float>(*V);
        if (const auto V = (*CollTbl)["BallRestitution"].value<double>())  OutDocument.Collider.BallRestitution  = static_cast<float>(*V);
    }

    // Reuse canonical VectorFieldCodec inline deserialization directly!
    DeserializeInlineForceSourcesFromManifest(Payload.DomainManifest, OutDocument.ForceSources, OutDocument.LinkedVectorFieldAssets);

    if (const auto* RigsArr = Payload.DomainManifest["ParticleRig"].as_array())
    {
        for (const auto& Entry : *RigsArr)
        {
            const auto* RTbl = Entry.as_table();
            if (!RTbl) continue;
            ParticleSpace::ParticleRig Rig {};
            if (const auto V = (*RTbl)["Identifier"].value<std::string>())        Rig.Identifier        = *V;
            if (const auto V = (*RTbl)["DisplayName"].value<std::string>())       Rig.DisplayName       = *V;
            if (const auto V = (*RTbl)["CategoryLabel"].value<std::string>())     Rig.CategoryLabel     = *V;
            if (const auto V = (*RTbl)["Visible"].value<bool>())                  Rig.Visible           = *V;
            if (const auto V = (*RTbl)["SlotCapacity"].value<int64_t>())          Rig.SlotCapacity      = static_cast<uint32_t>(*V);
            if (const auto V = (*RTbl)["ReleaseRatePerSec"].value<double>())      Rig.ReleaseRatePerSec = static_cast<float>(*V);
            if (const auto V = (*RTbl)["LocalSeed"].value<int64_t>())             Rig.LocalSeed         = static_cast<uint32_t>(*V);
            if (const auto V = (*RTbl)["TintChannel"].value<std::string>())       Rig.TintDriver.Channel = ParticleSpace::ParseScalarChannelKey(*V);

            if (const auto* Orig = (*RTbl)["Origin"].as_table())
            {
                const auto Cat = static_cast<ParticleSpace::ReleaseOriginCategory>((*Orig)["Category"].value_or<int64_t>(0));
                if (Cat == ParticleSpace::ReleaseOriginCategory::SurfaceAnchor)
                {
                    ParticleSpace::SurfaceAnchorOrigin Surf {};
                    Surf.Preset           = static_cast<ParticleSpace::SurfacePresetCategory>((*Orig)["SurfacePreset"].value_or<int64_t>(2));
                    Surf.SamplingStrategy = static_cast<ParticleSpace::SurfaceSamplingStrategy>((*Orig)["SamplingStrategy"].value_or<int64_t>(0));
                    UnpackVec3((*Orig)["CenterMeters"].as_array(), Surf.CenterMeters);
                    UnpackVec3((*Orig)["ScaleMeters"].as_array(),  Surf.ScaleMeters);
                    UnpackVec3((*Orig)["EulerDegrees"].as_array(), Surf.EulerDegrees);
                    if (const auto V = (*Orig)["CustomAssetId"].value<std::string>()) Surf.CustomAssetId = *V;
                    Rig.Origin = std::move(Surf);
                }
                else if (Cat == ParticleSpace::ReleaseOriginCategory::FireworkPattern)
                {
                    ParticleSpace::FireworkOrigin Fw {};
                    Fw.Shape = static_cast<ParticleSpace::FireworkShapeCategory>((*Orig)["FireworkShape"].value_or<int64_t>(0));
                    if (const auto V = (*Orig)["SkyTextMessage"].value<std::string>()) Fw.SkyTextMessage = *V;
                    UnpackVec3((*Orig)["ShellOrigin"].as_array(), Fw.ShellOrigin);
                    if (const auto V = (*Orig)["PatternRadius"].value<double>()) Fw.PatternRadius = static_cast<float>(*V);
                    Rig.Origin = std::move(Fw);
                }
                else if (Cat == ParticleSpace::ReleaseOriginCategory::SphereVolume)
                {
                    ParticleSpace::AnalyticSphereOrigin Sph {};
                    UnpackVec3((*Orig)["CenterMeters"].as_array(), Sph.CenterMeters);
                    if (const auto V = (*Orig)["RadiusMeters"].value<double>()) Sph.RadiusMeters = static_cast<float>(*V);
                    if (const auto V = (*Orig)["ShellOnly"].value<bool>())      Sph.ShellOnly    = *V;
                    Rig.Origin = Sph;
                }
                else if (Cat == ParticleSpace::ReleaseOriginCategory::BoxVolume)
                {
                    ParticleSpace::AnalyticBoxOrigin Box {};
                    UnpackVec3((*Orig)["CenterMeters"].as_array(),      Box.CenterMeters);
                    UnpackVec3((*Orig)["HalfExtentsMeters"].as_array(), Box.HalfExtentsMeters);
                    Rig.Origin = Box;
                }
                else
                {
                    ParticleSpace::AnalyticPointOrigin Pt {};
                    UnpackVec3((*Orig)["CenterMeters"].as_array(), Pt.CenterMeters);
                    Rig.Origin = Pt;
                }
            }

            if (const auto* Kin = (*RTbl)["Kinematics"].as_table())
            {
                UnpackVec3((*Kin)["LaunchDirection"].as_array(), Rig.Kinematics.LaunchDirection);
                if (const auto V = (*Kin)["SpreadRadians"].value<double>())   Rig.Kinematics.SpreadRadians   = static_cast<float>(*V);
                if (const auto V = (*Kin)["SpeedMinMps"].value<double>())     Rig.Kinematics.SpeedMinMps     = static_cast<float>(*V);
                if (const auto V = (*Kin)["SpeedMaxMps"].value<double>())     Rig.Kinematics.SpeedMaxMps     = static_cast<float>(*V);
                if (const auto V = (*Kin)["LifeMinSeconds"].value<double>())  Rig.Kinematics.LifeMinSeconds  = static_cast<float>(*V);
                if (const auto V = (*Kin)["LifeMaxSeconds"].value<double>())  Rig.Kinematics.LifeMaxSeconds  = static_cast<float>(*V);
                if (const auto V = (*Kin)["SizeBirthMeters"].value<double>()) Rig.Kinematics.SizeBirthMeters = static_cast<float>(*V);
                if (const auto V = (*Kin)["SizeDeathMeters"].value<double>()) Rig.Kinematics.SizeDeathMeters = static_cast<float>(*V);
                if (const auto V = (*Kin)["SizeMultiplier"].value<double>())  Rig.Kinematics.SizeMultiplier  = static_cast<float>(*V);
            }

            if (const auto* Car = (*RTbl)["Carrier"].as_table())
            {
                const auto Cat = static_cast<ParticleSpace::VisualCarrierCategory>((*Car)["Category"].value_or<int64_t>(0));
                if (Cat == ParticleSpace::VisualCarrierCategory::SpriteBillboard)
                {
                    ParticleSpace::SpriteBillboardCarrier Spr {};
                    Spr.Cutout         = static_cast<ParticleSpace::SpriteCutoutCategory>((*Car)["SpriteCutout"].value_or<int64_t>(9));
                    Spr.BokehAperture  = static_cast<ParticleSpace::BokehApertureCategory>((*Car)["BokehAperture"].value_or<int64_t>(0));
                    Spr.BokehHeadScale = static_cast<float>((*Car)["BokehHeadScale"].value_or<double>(1.0));
                    Spr.StreakThinness = static_cast<float>((*Car)["StreakThinness"].value_or<double>(1.0));
                    Rig.Carrier        = Spr;
                }
            }

            if (const auto* ColTbl = (*RTbl)["Colour"].as_table())
            {
                std::string ColDiag;
                if (!ColourDecoder::ExtractSourceFromManifest(*ColTbl, Rig.ColourProfile, ColDiag))
                {
                    OutDiagnostic = "Failed to decode ParticleRig colour block: " + ColDiag;
                    return false;
                }
            }

            if (const auto* Dyn = (*RTbl)["Dynamics"].as_table())
            {
                if (const auto V = (*Dyn)["GravityScale"].value<double>())     Rig.Dynamics.Ballistic.GravityScale      = static_cast<float>(*V);
                if (const auto V = (*Dyn)["AerodynamicDrag"].value<double>())  Rig.Dynamics.Ballistic.AerodynamicDrag   = static_cast<float>(*V);
                if (const auto V = (*Dyn)["BuoyancyMps2"].value<double>())     Rig.Dynamics.Ballistic.BuoyancyMps2      = static_cast<float>(*V);
                if (const auto V = (*Dyn)["FloorRestitution"].value<double>()) Rig.Dynamics.Ballistic.FloorRestitution  = static_cast<float>(*V);
                if (const auto V = (*Dyn)["WindCouplingRate"].value<double>()) Rig.Dynamics.ForceCouple.WindCouplingRate= static_cast<float>(*V);
                if (const auto* CurlTbl = (*Dyn)["Curl"].as_table())
                {
                    ParticleSpace::CurlManifoldSpec Curl {};
                    Curl.ManifoldMode    = static_cast<uint8_t>((*CurlTbl)["ManifoldMode"].value_or<int64_t>(1));
                    Curl.FrequencyPerM   = static_cast<float>((*CurlTbl)["FrequencyPerM"].value_or<double>(0.85));
                    Curl.DriveSpeedMps   = static_cast<float>((*CurlTbl)["DriveSpeedMps"].value_or<double>(2.2));
                    Curl.EvolutionRateHz = static_cast<float>((*CurlTbl)["EvolutionRateHz"].value_or<double>(0.24));
                    Curl.ShellRadiusM    = static_cast<float>((*CurlTbl)["ShellRadiusM"].value_or<double>(1.75));
                    Rig.Dynamics.Curl    = Curl;
                }
            }

            OutDocument.Rigs.push_back(std::move(Rig));
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }

    return true;
}

bool ParticleDecoder::DecodeFromFile(
    const std::filesystem::path&     SourcePath,
    ParticleSpace::ParticleDocument& OutDocument,
    std::string&                     OutDiagnostic,
    Workspaces::RevisionTracker*     OptionalOutRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
