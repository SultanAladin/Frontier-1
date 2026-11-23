/*====================================================================================================================================
                                                    ENVIRONMENTRECORDS.H
====================================================================================================================================*/
// 🧩 Scene records for the ported environment: Sun, atmosphere, clouds, fog and wind settings.

#pragma once

#include "RecordEntry.h"

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/FogMedium.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Manual Sun placement in the pinned HTML inspector's units. The renderer converts this to the
//    local frame SunDirectionLocal uses (X east, Y north, Z up); latitude/longitude presets remain
//    available through CelestialModel::SunPosition and are not duplicated here.
struct SunPlacement
{
    double ElevationDegrees = 38.0;   // [deg] - Above the horizon; negative is below it
    double AzimuthDegrees   = 145.0;  // [deg] - Clockwise from north (Noon preset at 6afb6f0)
    double SolarIlluminance = 12.0;   // [-] - Exhibit illumination scale (LightingPresets at 6afb6f0)
    bool   Enabled          = true;   // [-] - Sun row toggle
    bool   DiscEnabled      = true;   // [-] - Finite solar disc in the sky
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENVIRONMENT RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each record's constructor builds its own Outliner/Inspector description in editor builds and
//    registers it through RecordEntry::RegisterEditorDescription. Outside the editor profile the
//    records carry only their renderer settings: no description, no field pointers, no UI types.
class SunRecord final : public RecordEntry
{
  public:
    SunRecord();

    SunPlacement Placement;   // [-] - Live inspector-bound settings

    [[nodiscard]] SpatialVector QuerySunDirection() const noexcept;
};

class AtmosphereRecord final : public RecordEntry
{
  public:
    AtmosphereRecord();

    AtmosphereParameters Parameters;   // [-] - Live inspector-bound settings
    bool                 Enabled = true;   // [-] - Atmosphere row toggle
};

class CloudRecord final : public RecordEntry
{
  public:
    CloudRecord();

    CloudParameters Parameters;   // [-] - Live inspector-bound settings
    bool            Enabled = true;   // [-] - Cloud row toggle
};

class FogRecord final : public RecordEntry
{
  public:
    FogRecord();

    FogParameters Parameters;          // [-] - Live inspector-bound settings
    double        ScatteringAlbedo = 0.5;   // [-] - HTML FogScatter default at 6afb6f0
    bool          Enabled = false;          // [-] - HTML FogEnabled default at 6afb6f0
};

// 📝 Wind is the HTML Forcefield entry: one shared weather field that drives cloud advection.
class WindRecord final : public RecordEntry
{
  public:
    WindRecord();

    double SpeedMps   = 7.0;    // [m/s] - HTML CloudWindSpeed default at 6afb6f0
    double BearingDegrees = 90.0;   // [deg] - HTML CloudWindBearing default at 6afb6f0
    double Gust       = 0.25;   // [-] - HTML WindGust default at 6afb6f0
    bool   Enabled    = true;   // [-] - Wind row toggle
};

} // namespace Frontier
