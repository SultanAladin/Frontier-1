// 🧩 Scene records for the ported environment: Sun, atmosphere, clouds, fog and wind settings.

#include "Engine/Scene/EnvironmentRecords.h"

#include <cmath>
#include <utility>

namespace Frontier
{

namespace
{
constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;   // [rad/deg]
} // namespace

SunRecord::SunRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "sun";
    Description.Group       = "Environment";
    Description.DisplayName = "Sun";
    Description.Category    = "Directional light";
    Description.Glyph       = "NativeSun";
    Description.Colour      = "#e8c27a";
    Description.Description = "Light, direction & atmosphere";
    Description.Enabled     = &Placement.Enabled;
    Description.ScalarFields = {
        {"Elevation", "deg", -18.0, 90.0, 0.1, 1, &Placement.ElevationDegrees},
        {"Azimuth", "deg", 0.0, 360.0, 0.1, 1, &Placement.AzimuthDegrees},
        {"Solar illuminance", "x", 0.0, 40.0, 0.01, 2, &Placement.SolarIlluminance}};
    Description.Switches = {{"Sun disc", &Placement.DiscEnabled}};
    RegisterEditorDescription(std::move(Description));
#endif
}

SpatialVector SunRecord::QuerySunDirection() const noexcept
{
    const double Elevation = Placement.ElevationDegrees * DegreesToRadians;   // [rad]
    const double Azimuth   = Placement.AzimuthDegrees * DegreesToRadians;     // [rad]
    const double CosElevation = std::cos(Elevation);
    // 📝 SunDirectionLocal frame: X east, Y north, Z up; azimuth is clockwise from north.
    return {CosElevation * std::sin(Azimuth), CosElevation * std::cos(Azimuth), std::sin(Elevation)};
}

AtmosphereRecord::AtmosphereRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "atmosphere";
    Description.Group       = "Environment";
    Description.DisplayName = "Atmosphere";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeAtmosphere";
    Description.Colour      = "#99aafa";
    Description.Description = "Rayleigh, Mie and ozone column";
    Description.Enabled     = &Enabled;
    Description.ScalarFields = {
        {"Rayleigh scale height", "km", 1.0, 20.0, 0.1, 1, &Parameters.RayleighScaleHeightKm},
        {"Mie scattering", "1/km", 0.0, 0.02, 0.0001, 4, &Parameters.MieScattering},
        {"Mie scale height", "km", 0.1, 5.0, 0.05, 2, &Parameters.MieScaleHeightKm},
        {"Mie phase g", "-", -0.9, 0.95, 0.01, 2, &Parameters.MiePhaseG},
        {"Ground albedo", "-", 0.0, 1.0, 0.01, 2, &Parameters.GroundAlbedo}};
    RegisterEditorDescription(std::move(Description));
#endif
}

CloudRecord::CloudRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "clouds";
    Description.Group       = "Environment";
    Description.DisplayName = "Clouds";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeClouds";
    Description.Colour      = "#99aafa";
    Description.Description = "Seeded density slab, sun in-scattering and self-shadowing";
    Description.Enabled     = &Enabled;
    // 📝 Bounds are the pinned HTML CloudRanges (6afb6f0), mapped onto the Engine slab: base and
    //    thickness replace its base/top pair, and anisotropy maps to the Henyey-Greenstein g.
    Description.ScalarFields = {
        {"Coverage", "-", 0.0, 1.0, 0.01, 2, &Parameters.Coverage},
        {"Density", "x", 0.0, 4.0, 0.01, 2, &Parameters.DensityScale},
        {"Base", "m", 100.0, 20000.0, 10.0, 0, &Parameters.BaseHeightM},
        {"Extinction", "1/m", 0.0, 0.05, 0.0005, 4, &Parameters.ExtinctionPerM},
        {"Anisotropy", "-", -0.8, 0.8, 0.01, 2, &Parameters.PhaseG}};
    RegisterEditorDescription(std::move(Description));
#endif
}

FogRecord::FogRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "fog";
    Description.Group       = "Environment";
    Description.DisplayName = "Fog";
    Description.Category    = "Environment";
    Description.Glyph       = "NativeFog";
    Description.Colour      = "#99aafa";
    Description.Description = "Exponential height fog with cloud-shadowed single scattering";
    Description.Enabled     = &Enabled;
    Description.ScalarFields = {
        {"Density", "1/m", 0.0, 0.01, 0.0001, 4, &Parameters.DensityAtBase},
        {"Base height", "m", 0.0, 500.0, 1.0, 0, &Parameters.BaseHeightM},
        {"Falloff height", "m", 1.0, 500.0, 1.0, 0, &Parameters.FalloffHeightM},
        {"Scatter", "-", 0.0, 1.0, 0.01, 2, &ScatteringAlbedo}};
    RegisterEditorDescription(std::move(Description));
#endif
}

WindRecord::WindRecord() : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE)
    RecordEditorDescription Description;
    Description.StableId    = "wind";
    Description.Group       = "Forcefield";
    Description.DisplayName = "Wind";
    Description.Category    = "ForceField";
    Description.Glyph       = "Wind";
    Description.Colour      = "#a7f3d0";
    Description.Description = "Shared weather force field";
    Description.Enabled     = &Enabled;
    Description.ScalarFields = {
        {"Speed", "m/s", 0.0, 40.0, 0.1, 1, &SpeedMps},
        {"Bearing", "deg", 0.0, 360.0, 1.0, 0, &BearingDegrees},
        {"Gust", "-", 0.0, 1.0, 0.01, 2, &Gust}};
    RegisterEditorDescription(std::move(Description));
#endif
}

} // namespace Frontier
