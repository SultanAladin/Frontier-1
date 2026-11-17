/*============================================================================================================================================
                                                              FLUIDCODEC.CPP
============================================================================================================================================*/
// 🧩 Pure TOML codec using tomlpp (no JSON): handles native +Z-Up .fluid.toml & legacy Y-Up .gasscene.toml import

#include "Engine/SimulationSpace/FluidSpace/FluidCodec.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Frontier::SimulationSpace::FluidSpace
{

namespace
{

template <typename T>
void ReadOr(const toml::table* Tbl, std::string_view Key, T& OutValue) noexcept
{
    if (Tbl == nullptr) return;
    if (auto Val = (*Tbl)[Key].value<T>(); Val.has_value())
    {
        OutValue = *Val;
    }
}

void ReadFloatOr(const toml::table* Tbl, std::string_view Key, float& OutValue) noexcept
{
    if (Tbl == nullptr) return;
    if (auto Val = (*Tbl)[Key].value<double>(); Val.has_value())
    {
        OutValue = static_cast<float>(*Val);
    }
    else if (auto IVal = (*Tbl)[Key].value<int64_t>(); IVal.has_value())
    {
        OutValue = static_cast<float>(*IVal);
    }
}

void ReadUintOr(const toml::table* Tbl, std::string_view Key, uint32_t& OutValue) noexcept
{
    if (Tbl == nullptr) return;
    if (auto Val = (*Tbl)[Key].value<int64_t>(); Val.has_value() && *Val >= 0)
    {
        OutValue = static_cast<uint32_t>(*Val);
    }
}

[[nodiscard]] toml::array PackVec3(const Mathematics::Axis3D& V)
{
    toml::array A;
    A.push_back(static_cast<double>(V.X));
    A.push_back(static_cast<double>(V.Y));
    A.push_back(static_cast<double>(V.Z));
    return A;
}

[[nodiscard]] Mathematics::Axis3D UnpackVec3(
    const toml::array*         Arr,
    const Mathematics::Axis3D& Fallback,
    bool                       SwizzleYUpToZUp) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u) return Fallback;
    const float X = static_cast<float>((*Arr)[0].value_or(static_cast<double>(Fallback.X)));
    const float B = static_cast<float>((*Arr)[1].value_or(static_cast<double>(Fallback.Y)));
    const float C = static_cast<float>((*Arr)[2].value_or(static_cast<double>(Fallback.Z)));
    return SwizzleYUpToZUp ? Mathematics::Axis3D{ X, C, B } : Mathematics::Axis3D{ X, B, C };
}

} // namespace

std::string FluidCodec::SerializeSceneToml(const FluidSceneDocument& Doc)
{
    toml::table Root;
    Root.insert("format", std::string(FormatIdentifier));
    Root.insert("version", NativeVersion);
    Root.insert("up_axis", "Z");
    Root.insert("name", Doc.SceneName);
    Root.insert("preset", Doc.PresetKey);

    toml::table NamesTbl;
    NamesTbl.insert("domain", Doc.Names.Domain);
    NamesTbl.insert("emitter", Doc.Names.Emitter);
    NamesTbl.insert("collider", Doc.Names.Collider);
    NamesTbl.insert("sun", Doc.Names.Sun);
    Root.insert("names", std::move(NamesTbl));

    toml::table CamTbl;
    CamTbl.insert("theta", static_cast<double>(Doc.Camera.ThetaRadians));
    CamTbl.insert("phi", static_cast<double>(Doc.Camera.PhiRadians));
    CamTbl.insert("distance", static_cast<double>(Doc.Camera.DistanceMeters));
    CamTbl.insert("centre", PackVec3(Doc.Camera.CenterWorld));
    Root.insert("camera", std::move(CamTbl));

    toml::table S;
    S.insert("gridResolution", static_cast<int64_t>(Doc.Domain.GridResolution));
    S.insert("pressureIterations", static_cast<int64_t>(Doc.Domain.PressureIterations));
    S.insert("timeScale", static_cast<double>(Doc.Domain.TimeScale));
    S.insert("paused", Doc.Domain.Paused);
    S.insert("macCormackAdvection", Doc.Domain.MacCormackAdvection);
    S.insert("enclosedBox", Doc.Domain.EnclosedBox);
    S.insert("voxelQuantization", static_cast<double>(Doc.Domain.VoxelQuantization));
    S.insert("boundsWidth", static_cast<double>(Doc.Domain.BoundsWidthMeters));
    S.insert("boundsHeight", static_cast<double>(Doc.Domain.BoundsHeightMeters));
    S.insert("dynamicBounds", Doc.Domain.DynamicBounds);
    S.insert("dynamicBoundsMax", static_cast<double>(Doc.Domain.DynamicBoundsMax));
    S.insert("adaptiveCeilingEnabled", Doc.Domain.AdaptiveCeilingEnabled);
    S.insert("maxExplosionCeilingMeters", static_cast<double>(Doc.Domain.MaxExplosionCeilingMeters));
    S.insert("domainOrigin", PackVec3(Doc.Domain.OriginMeters));
    S.insert("worldGridEnabled", Doc.Domain.WorldGridEnabled);
    S.insert("worldGridResolution", static_cast<int64_t>(Doc.Domain.WorldGridResolution));
    S.insert("worldGridSpanMeters", static_cast<double>(Doc.Domain.WorldGridSpanMeters));

    S.insert("emitterEnabled", Doc.Emitter.Enabled);
    S.insert("emitterRate", static_cast<double>(Doc.Emitter.Rate));
    S.insert("emitterRadius", static_cast<double>(Doc.Emitter.RadiusMeters));
    S.insert("emitterHeight", static_cast<double>(Doc.Emitter.HeightMeters));
    S.insert("emitterUpwardVelocity", static_cast<double>(Doc.Emitter.UpwardVelocityMps));
    S.insert("emitterSwirl", static_cast<double>(Doc.Emitter.Swirl));
    S.insert("emitterTemperature", static_cast<double>(Doc.Emitter.Temperature));
    S.insert("emitterFuel", static_cast<double>(Doc.Emitter.Fuel));
    S.insert("emitterSmoke", static_cast<double>(Doc.Emitter.Smoke));

    S.insert("blastCenter", PackVec3(Doc.Blast.CenterWorld));
    S.insert("blastRadius", static_cast<double>(Doc.Blast.RadiusMeters));
    S.insert("blastStrength", static_cast<double>(Doc.Blast.StrengthMps));
    S.insert("blastTemperature", static_cast<double>(Doc.Blast.Temperature));
    S.insert("blastFuel", static_cast<double>(Doc.Blast.Fuel));
    S.insert("blastSmoke", static_cast<double>(Doc.Blast.Smoke));
    S.insert("blastLobes", static_cast<double>(Doc.Blast.Lobes));
    S.insert("blastSeed", static_cast<double>(Doc.Blast.Seed));
    S.insert("triggerForceImpulse", Doc.Blast.TriggerForceImpulse);

    S.insert("vorticityConfinement", static_cast<double>(Doc.Combustion.VorticityConfinement));
    S.insert("buoyancy", static_cast<double>(Doc.Combustion.Buoyancy));
    S.insert("smokeWeight", static_cast<double>(Doc.Combustion.SmokeWeight));
    S.insert("turbulenceStrength", static_cast<double>(Doc.Combustion.TurbulenceStrength));
    S.insert("turbulenceScale", static_cast<double>(Doc.Combustion.TurbulenceScale));
    S.insert("burnRate", static_cast<double>(Doc.Combustion.BurnRate));
    S.insert("burnHeat", static_cast<double>(Doc.Combustion.BurnHeat));
    S.insert("sootGeneration", static_cast<double>(Doc.Combustion.SootGeneration));
    S.insert("combustionExpansion", static_cast<double>(Doc.Combustion.CombustionExpansion));
    S.insert("coolingRate", static_cast<double>(Doc.Combustion.CoolingRate));
    S.insert("smokeDissipation", static_cast<double>(Doc.Combustion.SmokeDissipation));
    S.insert("velocityDamping", static_cast<double>(Doc.Combustion.VelocityDamping));
    S.insert("windX", static_cast<double>(Doc.Combustion.WindVelocityXY.X));
    S.insert("windY", static_cast<double>(Doc.Combustion.WindVelocityXY.Y));
    S.insert("coupleForceFields", Doc.Combustion.CoupleForceFields);

    S.insert("obstacleType", static_cast<int64_t>(static_cast<uint32_t>(Doc.Obstacle.Type)));
    S.insert("obstaclePos", PackVec3(Doc.Obstacle.CenterMeters));
    S.insert("obstacleRadius", static_cast<double>(Doc.Obstacle.RadiusMeters));
    S.insert("colliderAutoMove", Doc.Obstacle.AutoMove);
    S.insert("colliderSpeed", static_cast<double>(Doc.Obstacle.MoveSpeed));
    S.insert("voxelizeSceneGiSdf", Doc.Obstacle.VoxelizeSceneGiSdf);

    S.insert("renderChannel", static_cast<int64_t>(static_cast<uint32_t>(Doc.Shading.RenderChannel)));
    S.insert("colorPalette", static_cast<int64_t>(static_cast<uint32_t>(Doc.Shading.ColorPalette)));
    S.insert("raymarchSteps", static_cast<int64_t>(Doc.Shading.RaymarchSteps));
    S.insert("shadowSteps", static_cast<int64_t>(Doc.Shading.ShadowSteps));
    S.insert("renderScale", static_cast<double>(Doc.Shading.RenderScale));
    S.insert("fireIntensity", static_cast<double>(Doc.Shading.FireIntensity));
    S.insert("bloomIntensity", static_cast<double>(Doc.Shading.BloomIntensity));
    S.insert("godRaysIntensity", static_cast<double>(Doc.Shading.GodRaysIntensity));
    S.insert("shockwaveStrength", static_cast<double>(Doc.Shading.ShockwaveStrength));
    S.insert("temperatureScale", static_cast<double>(Doc.Shading.TemperatureScale));
    S.insert("internalScattering", static_cast<double>(Doc.Shading.InternalScattering));
    S.insert("densityExtinction", static_cast<double>(Doc.Shading.DensityExtinction));
    S.insert("smokeAlbedo", static_cast<double>(Doc.Shading.SmokeAlbedo));
    S.insert("shadowDensity", static_cast<double>(Doc.Shading.ShadowDensity));
    S.insert("phaseAnisotropy", static_cast<double>(Doc.Shading.PhaseAnisotropy));
    S.insert("sunElevation", static_cast<double>(Doc.Shading.SunElevationDegrees));
    S.insert("sunAzimuth", static_cast<double>(Doc.Shading.SunAzimuthDegrees));
    S.insert("sunIntensity", static_cast<double>(Doc.Shading.SunIntensity));
    S.insert("ambientIntensity", static_cast<double>(Doc.Shading.AmbientIntensity));
    S.insert("exposure", static_cast<double>(Doc.Shading.Exposure));
    S.insert("sliceAxis", static_cast<int64_t>(Doc.Shading.SliceAxis));
    S.insert("slicePosition", static_cast<double>(Doc.Shading.SlicePosition));

    S.insert("showEmbers", Doc.Particles.ShowEmbers);
    S.insert("emberCount", static_cast<int64_t>(Doc.Particles.EmberCount));
    S.insert("emberSize", static_cast<double>(Doc.Particles.EmberSize));
    S.insert("emberIntensity", static_cast<double>(Doc.Particles.EmberIntensity));
    S.insert("emberLifetime", static_cast<double>(Doc.Particles.EmberLifetime));
    S.insert("emberAshiness", static_cast<double>(Doc.Particles.EmberAshiness));
    S.insert("shrapnelEnabled", Doc.Particles.ShrapnelEnabled);
    S.insert("advectionCoupling", static_cast<double>(Doc.Particles.AdvectionCoupling));
    S.insert("buoyancyCoupling", static_cast<double>(Doc.Particles.BuoyancyCoupling));

    S.insert("collideWithGiScene", Doc.GiCoupling.CollideWithGiScene);
    S.insert("castGiShadows", Doc.GiCoupling.CastGiShadows);
    S.insert("emitGiLights", Doc.GiCoupling.EmitGiLights);
    S.insert("injectDdgiRadiance", Doc.GiCoupling.InjectDdgiRadiance);
    S.insert("maxEmissiveProxyLights", static_cast<int64_t>(Doc.GiCoupling.MaxEmissiveProxyLights));
    S.insert("emissiveLightIntensity", static_cast<double>(Doc.GiCoupling.EmissiveLightIntensity));
    S.insert("giShadowStrength", static_cast<double>(Doc.GiCoupling.GiShadowStrength));

    S.insert("showBoundingBox", Doc.Overlays.ShowBoundingBox);
    S.insert("showVoxelGridLines", Doc.Overlays.ShowVoxelGridLines);
    S.insert("showActiveVoxelCells", Doc.Overlays.ShowActiveVoxelCells);
    S.insert("showFloorGrid", Doc.Overlays.ShowFloorGrid);
    S.insert("showAtlasMinimap", Doc.Overlays.ShowAtlasMinimap);
    S.insert("atlasMinimapField", static_cast<int64_t>(static_cast<uint32_t>(Doc.Overlays.AtlasMinimapField)));
    S.insert("autoTurntable", Doc.Overlays.AutoTurntable);

    Root.insert("settings", std::move(S));

    std::ostringstream Stream;
    Stream << "# Frontier Fluid Scene (TOML, +Z Up)\n" << Root << "\n";
    return Stream.str();
}

bool FluidCodec::DeserializeSceneToml(
    std::string_view    TomlText,
    FluidSceneDocument& OutDocument,
    std::string*        OutError)
{
    toml::table Root;
    try
    {
        Root = toml::parse(TomlText);
    }
    catch (const toml::parse_error& Err)
    {
        if (OutError != nullptr)
        {
            *OutError = std::string(Err.description());
        }
        return false;
    }

    OutDocument = {};
    const int64_t Version = Root["version"].value_or<int64_t>(1);
    std::string   UpAxis  = Root["up_axis"].value_or<std::string>(Version >= 2 ? "Z" : "Y");
    const bool    LegacyYUp = (UpAxis != "Z" && UpAxis != "z");

    if (auto Name = Root["name"].value<std::string>(); Name.has_value())
    {
        OutDocument.SceneName = *Name;
    }
    if (auto Preset = Root["preset"].value<std::string>(); Preset.has_value())
    {
        OutDocument.PresetKey = *Preset;
    }

    if (const auto* NamesTbl = Root["names"].as_table())
    {
        ReadOr(NamesTbl, "domain", OutDocument.Names.Domain);
        ReadOr(NamesTbl, "emitter", OutDocument.Names.Emitter);
        ReadOr(NamesTbl, "collider", OutDocument.Names.Collider);
        ReadOr(NamesTbl, "sun", OutDocument.Names.Sun);
    }

    if (const auto* CamTbl = Root["camera"].as_table())
    {
        ReadFloatOr(CamTbl, "theta", OutDocument.Camera.ThetaRadians);
        ReadFloatOr(CamTbl, "phi", OutDocument.Camera.PhiRadians);
        ReadFloatOr(CamTbl, "distance", OutDocument.Camera.DistanceMeters);
        OutDocument.Camera.CenterWorld = UnpackVec3(
            (*CamTbl)["centre"].as_array(), OutDocument.Camera.CenterWorld, LegacyYUp);
    }

    const auto* S = Root["settings"].as_table();
    if (S == nullptr)
    {
        return true;
    }

    ReadUintOr(S, "gridResolution", OutDocument.Domain.GridResolution);
    OutDocument.Domain.GridResolution = std::clamp(OutDocument.Domain.GridResolution, 16u, 128u);
    ReadUintOr(S, "pressureIterations", OutDocument.Domain.PressureIterations);
    ReadFloatOr(S, "timeScale", OutDocument.Domain.TimeScale);
    ReadOr(S, "paused", OutDocument.Domain.Paused);
    ReadOr(S, "macCormackAdvection", OutDocument.Domain.MacCormackAdvection);
    ReadOr(S, "enclosedBox", OutDocument.Domain.EnclosedBox);
    ReadFloatOr(S, "voxelQuantization", OutDocument.Domain.VoxelQuantization);
    ReadFloatOr(S, "boundsWidth", OutDocument.Domain.BoundsWidthMeters);
    ReadFloatOr(S, "boundsHeight", OutDocument.Domain.BoundsHeightMeters);
    ReadOr(S, "dynamicBounds", OutDocument.Domain.DynamicBounds);
    ReadFloatOr(S, "dynamicBoundsMax", OutDocument.Domain.DynamicBoundsMax);
    ReadOr(S, "adaptiveCeilingEnabled", OutDocument.Domain.AdaptiveCeilingEnabled);
    if (LegacyYUp && OutDocument.Domain.DynamicBounds && !OutDocument.Domain.EnclosedBox &&
        !(*S).contains("adaptiveCeilingEnabled"))
    {
        OutDocument.Domain.AdaptiveCeilingEnabled = true;
    }
    ReadFloatOr(S, "maxExplosionCeilingMeters", OutDocument.Domain.MaxExplosionCeilingMeters);
    if (const auto* Orig = (*S)["domainOrigin"].as_array())
    {
        OutDocument.Domain.OriginMeters = UnpackVec3(Orig, OutDocument.Domain.OriginMeters, LegacyYUp);
    }
    ReadOr(S, "worldGridEnabled", OutDocument.Domain.WorldGridEnabled);
    ReadUintOr(S, "worldGridResolution", OutDocument.Domain.WorldGridResolution);
    ReadFloatOr(S, "worldGridSpanMeters", OutDocument.Domain.WorldGridSpanMeters);

    ReadOr(S, "emitterEnabled", OutDocument.Emitter.Enabled);
    ReadFloatOr(S, "emitterRate", OutDocument.Emitter.Rate);
    ReadFloatOr(S, "emitterRadius", OutDocument.Emitter.RadiusMeters);
    ReadFloatOr(S, "emitterHeight", OutDocument.Emitter.HeightMeters);
    ReadFloatOr(S, "emitterUpwardVelocity", OutDocument.Emitter.UpwardVelocityMps);
    ReadFloatOr(S, "emitterSwirl", OutDocument.Emitter.Swirl);
    ReadFloatOr(S, "emitterTemperature", OutDocument.Emitter.Temperature);
    ReadFloatOr(S, "emitterFuel", OutDocument.Emitter.Fuel);
    ReadFloatOr(S, "emitterSmoke", OutDocument.Emitter.Smoke);

    if (const auto* Bc = (*S)["blastCenter"].as_array())
    {
        OutDocument.Blast.CenterWorld = UnpackVec3(Bc, OutDocument.Blast.CenterWorld, LegacyYUp);
    }
    ReadFloatOr(S, "blastRadius", OutDocument.Blast.RadiusMeters);
    ReadFloatOr(S, "blastStrength", OutDocument.Blast.StrengthMps);
    ReadFloatOr(S, "blastTemperature", OutDocument.Blast.Temperature);
    ReadFloatOr(S, "blastFuel", OutDocument.Blast.Fuel);
    ReadFloatOr(S, "blastSmoke", OutDocument.Blast.Smoke);
    ReadFloatOr(S, "blastLobes", OutDocument.Blast.Lobes);
    ReadFloatOr(S, "blastSeed", OutDocument.Blast.Seed);
    ReadOr(S, "triggerForceImpulse", OutDocument.Blast.TriggerForceImpulse);

    ReadFloatOr(S, "vorticityConfinement", OutDocument.Combustion.VorticityConfinement);
    ReadFloatOr(S, "buoyancy", OutDocument.Combustion.Buoyancy);
    ReadFloatOr(S, "smokeWeight", OutDocument.Combustion.SmokeWeight);
    ReadFloatOr(S, "turbulenceStrength", OutDocument.Combustion.TurbulenceStrength);
    ReadFloatOr(S, "turbulenceScale", OutDocument.Combustion.TurbulenceScale);
    ReadFloatOr(S, "burnRate", OutDocument.Combustion.BurnRate);
    ReadFloatOr(S, "burnHeat", OutDocument.Combustion.BurnHeat);
    ReadFloatOr(S, "sootGeneration", OutDocument.Combustion.SootGeneration);
    ReadFloatOr(S, "combustionExpansion", OutDocument.Combustion.CombustionExpansion);
    ReadFloatOr(S, "coolingRate", OutDocument.Combustion.CoolingRate);
    ReadFloatOr(S, "smokeDissipation", OutDocument.Combustion.SmokeDissipation);
    ReadFloatOr(S, "velocityDamping", OutDocument.Combustion.VelocityDamping);
    ReadFloatOr(S, "windX", OutDocument.Combustion.WindVelocityXY.X);
    if (LegacyYUp)
    {
        ReadFloatOr(S, "windZ", OutDocument.Combustion.WindVelocityXY.Y);
    }
    else
    {
        ReadFloatOr(S, "windY", OutDocument.Combustion.WindVelocityXY.Y);
    }
    ReadOr(S, "coupleForceFields", OutDocument.Combustion.CoupleForceFields);

    {
        uint32_t ObsRaw = static_cast<uint32_t>(OutDocument.Obstacle.Type);
        ReadUintOr(S, "obstacleType", ObsRaw);
        OutDocument.Obstacle.Type = static_cast<FluidObstacleType>(std::min(ObsRaw, 5u));
    }
    if (const auto* Op = (*S)["obstaclePos"].as_array())
    {
        OutDocument.Obstacle.CenterMeters = UnpackVec3(Op, OutDocument.Obstacle.CenterMeters, LegacyYUp);
    }
    else if (LegacyYUp)
    {
        // Legacy v1 stored obstacleX/obstacleY/obstacleZ in normalized UVW [0..1]
        float OX = 0.5f, OY = 0.26f, OZ = 0.5f;
        ReadFloatOr(S, "obstacleX", OX);
        ReadFloatOr(S, "obstacleY", OY);
        ReadFloatOr(S, "obstacleZ", OZ);
        OutDocument.Obstacle.CenterMeters = {
            (OX - 0.5f) * OutDocument.Domain.BoundsWidthMeters,
            (OZ - 0.5f) * OutDocument.Domain.BoundsWidthMeters,
            OutDocument.Domain.OriginMeters.Z + OY * OutDocument.Domain.BoundsHeightMeters,
        };
    }
    ReadFloatOr(S, "obstacleRadius", OutDocument.Obstacle.RadiusMeters);
    ReadOr(S, "colliderAutoMove", OutDocument.Obstacle.AutoMove);
    ReadFloatOr(S, "colliderSpeed", OutDocument.Obstacle.MoveSpeed);
    ReadOr(S, "voxelizeSceneGiSdf", OutDocument.Obstacle.VoxelizeSceneGiSdf);

    {
        uint32_t Rc = 0u, Cp = 0u, Am = 0u;
        ReadUintOr(S, "renderChannel", Rc);
        ReadUintOr(S, "colorPalette", Cp);
        ReadUintOr(S, "atlasMinimapField", Am);
        OutDocument.Shading.RenderChannel     = static_cast<FluidRenderChannel>(std::min(Rc, 8u));
        OutDocument.Shading.ColorPalette      = static_cast<FluidColorPalette>(std::min(Cp, 5u));
        OutDocument.Overlays.AtlasMinimapField = static_cast<FluidAtlasMinimapField>(std::min(Am, 3u));
    }
    ReadUintOr(S, "raymarchSteps", OutDocument.Shading.RaymarchSteps);
    ReadUintOr(S, "shadowSteps", OutDocument.Shading.ShadowSteps);
    ReadFloatOr(S, "renderScale", OutDocument.Shading.RenderScale);
    ReadFloatOr(S, "fireIntensity", OutDocument.Shading.FireIntensity);
    ReadFloatOr(S, "bloomIntensity", OutDocument.Shading.BloomIntensity);
    ReadFloatOr(S, "godRaysIntensity", OutDocument.Shading.GodRaysIntensity);
    ReadFloatOr(S, "shockwaveStrength", OutDocument.Shading.ShockwaveStrength);
    ReadFloatOr(S, "temperatureScale", OutDocument.Shading.TemperatureScale);
    ReadFloatOr(S, "internalScattering", OutDocument.Shading.InternalScattering);
    ReadFloatOr(S, "densityExtinction", OutDocument.Shading.DensityExtinction);
    ReadFloatOr(S, "smokeAlbedo", OutDocument.Shading.SmokeAlbedo);
    ReadFloatOr(S, "shadowDensity", OutDocument.Shading.ShadowDensity);
    ReadFloatOr(S, "phaseAnisotropy", OutDocument.Shading.PhaseAnisotropy);
    ReadFloatOr(S, "sunElevation", OutDocument.Shading.SunElevationDegrees);
    ReadFloatOr(S, "sunAzimuth", OutDocument.Shading.SunAzimuthDegrees);
    ReadFloatOr(S, "sunIntensity", OutDocument.Shading.SunIntensity);
    ReadFloatOr(S, "ambientIntensity", OutDocument.Shading.AmbientIntensity);
    ReadFloatOr(S, "exposure", OutDocument.Shading.Exposure);
    ReadUintOr(S, "sliceAxis", OutDocument.Shading.SliceAxis);
    if (LegacyYUp && (*S)["sliceAxis"].is_integer())
    {
        // Legacy Y=1 (vertical) becomes Z=2, Legacy Z=2 becomes Y=1
        if (OutDocument.Shading.SliceAxis == 1u) OutDocument.Shading.SliceAxis = 2u;
        else if (OutDocument.Shading.SliceAxis == 2u) OutDocument.Shading.SliceAxis = 1u;
    }
    ReadFloatOr(S, "slicePosition", OutDocument.Shading.SlicePosition);

    ReadOr(S, "showEmbers", OutDocument.Particles.ShowEmbers);
    ReadUintOr(S, "emberCount", OutDocument.Particles.EmberCount);
    ReadFloatOr(S, "emberSize", OutDocument.Particles.EmberSize);
    ReadFloatOr(S, "emberIntensity", OutDocument.Particles.EmberIntensity);
    ReadFloatOr(S, "emberLifetime", OutDocument.Particles.EmberLifetime);
    ReadFloatOr(S, "emberAshiness", OutDocument.Particles.EmberAshiness);
    ReadOr(S, "shrapnelEnabled", OutDocument.Particles.ShrapnelEnabled);
    ReadFloatOr(S, "advectionCoupling", OutDocument.Particles.AdvectionCoupling);
    ReadFloatOr(S, "buoyancyCoupling", OutDocument.Particles.BuoyancyCoupling);

    ReadOr(S, "collideWithGiScene", OutDocument.GiCoupling.CollideWithGiScene);
    ReadOr(S, "castGiShadows", OutDocument.GiCoupling.CastGiShadows);
    ReadOr(S, "emitGiLights", OutDocument.GiCoupling.EmitGiLights);
    ReadOr(S, "injectDdgiRadiance", OutDocument.GiCoupling.InjectDdgiRadiance);
    ReadUintOr(S, "maxEmissiveProxyLights", OutDocument.GiCoupling.MaxEmissiveProxyLights);
    ReadFloatOr(S, "emissiveLightIntensity", OutDocument.GiCoupling.EmissiveLightIntensity);
    ReadFloatOr(S, "giShadowStrength", OutDocument.GiCoupling.GiShadowStrength);

    ReadOr(S, "showBoundingBox", OutDocument.Overlays.ShowBoundingBox);
    ReadOr(S, "showVoxelGridLines", OutDocument.Overlays.ShowVoxelGridLines);
    ReadOr(S, "showActiveVoxelCells", OutDocument.Overlays.ShowActiveVoxelCells);
    ReadOr(S, "showFloorGrid", OutDocument.Overlays.ShowFloorGrid);
    ReadOr(S, "showAtlasMinimap", OutDocument.Overlays.ShowAtlasMinimap);
    ReadOr(S, "autoTurntable", OutDocument.Overlays.AutoTurntable);

    return true;
}

bool FluidCodec::LoadSceneFile(
    const std::filesystem::path& FilePath,
    FluidSceneDocument&          OutDocument,
    std::string*                 OutError)
{
    std::ifstream In(FilePath);
    if (!In.is_open())
    {
        if (OutError != nullptr) *OutError = "Could not open fluid TOML file.";
        return false;
    }
    std::ostringstream Ss;
    Ss << In.rdbuf();
    return DeserializeSceneToml(Ss.str(), OutDocument, OutError);
}

bool FluidCodec::SaveSceneFile(
    const std::filesystem::path& FilePath,
    const FluidSceneDocument&    Document,
    std::string*                 OutError)
{
    std::ofstream Out(FilePath);
    if (!Out.is_open())
    {
        if (OutError != nullptr) *OutError = "Could not write fluid TOML file.";
        return false;
    }
    Out << SerializeSceneToml(Document);
    return Out.good();
}

} // namespace Frontier::SimulationSpace::FluidSpace
