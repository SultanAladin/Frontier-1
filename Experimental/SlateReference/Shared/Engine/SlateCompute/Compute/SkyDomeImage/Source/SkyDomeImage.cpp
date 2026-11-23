//============================================================================================================================================
//                                                            SKYDOMEIMAGE.CPP
//============================================================================================================================================

#include "SlateCompute/Compute/SkyDomeImage/Api/SkyDomeImage.h"

#include "SlateMath/Numeric/QuadratureIntegrator/Api/QuadratureIntegrator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Slate
{

namespace
{

constexpr double HalfTurn  = 3.14159265358979323846;
constexpr double SunAngularRadius = 0.00465;   // [rad] - the sun's apparent radius, the donor's own figure

} // namespace

Deliver<bool> GenerateSkyImage(AtmosphereIntegrator& Atmosphere,
                               const EnvironmentConfiguration& Environment,
                               const SkyCamera& Camera,
                               std::uint32_t Width,
                               std::uint32_t Height,
                               std::vector<std::uint8_t>& Pixels)
{
    if (Width == 0u || Height == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a sky image of zero extent" });

    // 🔴 The medium is Earth's, declared once per generation — cheap and idempotent: the integrator
    //    rebuilds only what changed. The artist's turbidity and density scale the sky's own terms, and
    //    the elevation/azimuth declare the sun's direction (second axis is the local zenith).
    MediumSpecification Earth;
    Earth.RayleighScaleHeight = 8000.0 * std::clamp(Environment.AtmosphereScaleHeight, 0.2, 3.0);
    Earth.MieScaleHeight      = 1200.0 / std::clamp(Environment.AtmosphereDensity, 0.2, 3.0);
    // 📐 The Mie scattering and forward-scattering asymmetry are artist-facing:
    //    density brightens the haze around the sun, asymmetry pulls the haze
    //    into a tighter forward lobe. The defaults keep Earth's look at 1.0 / 0.8.
    Earth.MieScattering       = 3.996e-6 * std::clamp(Environment.MieDensity, 0.0, 4.0);
    Earth.MieExtinction       = 4.400e-6 * std::clamp(Environment.MieDensity, 0.0, 4.0);
    Earth.MieAsymmetry        = std::clamp(Environment.MieAsymmetry, -0.95, 0.95);

    if (!Atmosphere.DeclareMedium(Earth).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sky medium was rejected" });

    const double SunElevation = std::clamp(Environment.SunElevation, -90.0, 90.0) * HalfTurn / 180.0;
    const double SunAzimuth   = Environment.SunAzimuth * HalfTurn / 180.0;
    const double SunCosine    = std::cos(SunElevation);
    const double SunDirectionX = SunCosine * std::sin(SunAzimuth);
    const double SunDirectionY = std::sin(SunElevation);
    const double SunDirectionZ = SunCosine * std::cos(SunAzimuth);

    if (!Atmosphere.DeclareSun(SunDirectionX, SunDirectionY, SunDirectionZ).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sky sun direction was rejected" });

    // 📝 A ground-level camera. The reference camera stands on the surface; the atmosphere's own
    //    thickness and the sky-view surface are both built around the same start radius.
    if (!Atmosphere.DeclareCameraAltitude(1.0).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sky camera altitude was rejected" });

    // 📝 `02` §5's Gauss–Legendre rule, derived on the recurrence — the same rule the ConsoleHost's
    //    atmosphere verification derives before it rebuilds.
    QuadratureRule Rule;
    const std::uint32_t Quality = std::min(Environment.AtmosphereQuality, 3u);
    const std::uint32_t QuadratureOrders[4] = { 8u, 16u, 32u, 48u };
    if (!Rule.Derive(QuadratureOrders[Quality]).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sky rule would not derive" });

    if (!Atmosphere.Rebuild(DeclaredWorkingSpace(), Rule).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the sky surfaces would not rebuild" });

    // 📝 The camera is carried for the viewport's crop; the dome itself is direction-indexed and
    //    camera-independent.
    static_cast<void>(Camera);

    Pixels.resize(static_cast<std::size_t>(Width) * Height * 4u);

    // 📐 The sun's disc test uses the same direct term the GPU shader omits: the sky-view surface
    //    deliberately excludes the illuminant's own flux, so the disc is added here from the
    //    transmittance toward the sun, scaled by the artist's intensity and temperature.
    const double SunTemperature = std::clamp(Environment.SunTemperature, 1000.0, 12000.0);
    const double TemperatureT   = (SunTemperature - 1000.0) / 11000.0;
    double SunRed = 1.0, SunGreen = 1.0, SunBlue = 1.0;
    if (TemperatureT < 0.5)
    {
        SunRed   = 1.0;
        SunGreen = 0.31 + TemperatureT * 2.0 * 0.69;
        SunBlue  = 0.12 + TemperatureT * 2.0 * 0.16;
    }
    else
    {
        const double Upper = (TemperatureT - 0.5) * 2.0;
        SunRed   = 1.0 - Upper * 0.24;
        SunGreen = 1.0 - Upper * 0.12;
        SunBlue  = 0.43 + Upper * 0.57;
    }

    // 🔴 SUN INTENSITY WAS READ, CLAMPED AND THEN NEVER USED. The variable sat here unreferenced, which
    //    `-Wunused-variable` would have said the moment this file was compiled as a unit — but it lived
    //    under `Application/` where the host's warning settings never reached it. The artist's sun
    //    intensity slider moved, the setting was saved to the codex and handed to the renderer at
    //    `EditorHost:1297`, and the sky image behind the geometry ignored it completely. The sky and the
    //    lit scene disagreed about how bright the sun was, which reads as a broken exposure rather than
    //    as a missing multiply.
    // 📝 It scales the direct disc, matching how `SkyIntensity` scales the dome below and how the
    //    renderer takes the same figure as `SunIlluminance`. The clamp is the one already written here.
    const double SunIntensity = std::clamp(Environment.SunIntensity, 0.0, 10.0);
    const double SunHeight = std::sin(SunElevation);
    const double Daylight = std::clamp((SunHeight + 0.105) / 0.14, 0.0, 1.0);
    const double Night = 1.0 - std::clamp((SunHeight + 0.31) / 0.205, 0.0, 1.0);

    double SunTransmitRed = 0.0, SunTransmitGreen = 0.0, SunTransmitBlue = 0.0;
    static_cast<void>(Atmosphere.SampleTransmittance(1.0, std::sin(SunElevation),
                                                     SunTransmitRed, SunTransmitGreen, SunTransmitBlue));

    const double DirectStrength = std::clamp(Environment.SunDiscIntensity, 0.0, 4.0) * Daylight * SunIntensity;
    const double DirectRed   = SunRed   * SunTransmitRed * DirectStrength;
    const double DirectGreen = SunGreen * SunTransmitGreen * DirectStrength;
    const double DirectBlue  = SunBlue  * SunTransmitBlue * DirectStrength;

    for (std::uint32_t Y = 0u; Y < Height; ++Y)
    {
        for (std::uint32_t X = 0u; X < Width; ++X)
        {
            // 📐 The dome, not a pinhole: every texel is a direction, with the azimuth spread across
            //    the width and the elevation down the height (zenith at the top, nadir at the bottom).
            //    The viewport then crops the dome to the camera's field of view — which keeps the sun
            //    in frame at any viewport aspect, where a pinhole image of one direction would crop
            //    the sun out of a narrow viewport.
            const double DirectionAzimuth = (static_cast<double>(X) + 0.5) / static_cast<double>(Width) * 2.0 * HalfTurn
                                          - HalfTurn;
            const double DirectionElevation = (0.5 - (static_cast<double>(Y) + 0.5) / static_cast<double>(Height))
                                            * HalfTurn;

            const double ElevationCosine = std::cos(DirectionElevation);
            const double RayX = ElevationCosine * std::sin(DirectionAzimuth);
            const double RayY = std::sin(DirectionElevation);
            const double RayZ = ElevationCosine * std::cos(DirectionAzimuth);

            double SkyRed = 0.0, SkyGreen = 0.0, SkyBlue = 0.0;
            static_cast<void>(Atmosphere.SampleSkyView(RayX, RayY, RayZ,
                                                       SkyRed, SkyGreen, SkyBlue));
            // 📐 The sun disc: the angular test against the declared sun direction, then the direct
            //    term added on top of the atmosphere's own scattered radiance — all in radiance space,
            //    before the display scale, so the disc saturates to white while the dome stays soft.
            // 🔴 The dot product is against the sun's own direction components, held outside the loop:
            //    a test that mixes the texel's own azimuth and elevation into the sun direction is a
            //    band across the whole sky at the sun's elevation, not a disc.
            const double SunCosineAngle = std::clamp(RayX * SunDirectionX
                                                    + RayY * SunDirectionY
                                                    + RayZ * SunDirectionZ,
                                                    -1.0, 1.0);
            double Red   = SkyRed;
            double Green = SkyGreen;
            double Blue  = SkyBlue;
            // 📐 The disc angle, with a soft edge so a 0.27° sun reads as a sun and not as a single
            //    white texel. The strength is applied AFTER the tone curve, so the disc keeps the
            //    temperature's warm hue instead of saturating to white with the dome.
            const double DiscAngle = std::acos(std::clamp(SunCosineAngle, -1.0, 1.0));
            // 📐 The disc is drawn wider than the 0.27° figure so it reads as a sun rather than a
            //    single white texel. The multiplier is artist-facing; 8 keeps the icon size the editor
            //    shipped with, and a larger or smaller value grows the disc from there.
            const double DiscRadius = SunAngularRadius
                                   * std::clamp(Environment.SunDiscRadius, 1.0, 32.0);
            const double DiscStrength = (DiscAngle < DiscRadius)
                ? (1.0 - DiscAngle / DiscRadius) * (1.0 - DiscAngle / DiscRadius) : 0.0;

            // 📐 The sky-view surface stores the radiance per unit solar flux — the shader's own
            //    declaration: "the illuminant's own flux is not applied here… belongs to whoever reads
            //    ③". The reader is this generator, and the flux is the artist's sun intensity times a
            //    display-scale constant, with the sky's own intensity multiplying on top.
            // 🔴 A Reinhard-style curve instead of a linear clamp: the LUT rises quadratically toward
            //    the horizon, so a linear scale clips the whole lower sky to white before the horizon
            //    line — the tone curve keeps the gradient visible and only the sun disc reaches unity.
            const double SkyScale = std::clamp(Environment.SunIntensity, 0.0, 10.0) * 8.0
                                  * std::clamp(Environment.SkyIntensity, 0.0, 3.0) * Daylight;
            const auto Tone = [&](double Radiant) -> double
            {
                const double Scaled = Radiant * SkyScale;
                return Scaled / (1.0 + Scaled);
            };
            Red   = Tone(Red);
            Green = Tone(Green);
            Blue  = Tone(Blue);

            const double NightZenith = std::clamp(RayY * 0.5 + 0.5, 0.0, 1.0);
            Red   += Night * (0.0012 + NightZenith * 0.0013);
            Green += Night * (0.0018 + NightZenith * 0.0032);
            Blue  += Night * (0.0040 + NightZenith * 0.0080);

            // 📐 The sun's own disc is added after the tone curve, in display space, so it keeps the
            //    temperature-mapped warm hue; the direct term's magnitude is the artist's intensity.
            // 🔴 Clamped at unity: the write is an eight-bit channel, and an unclamped value past one
            //    wraps around to garbage rather than clipping.
            Red   = std::min(Red   + DirectRed   * DiscStrength, 1.0);
            Green = std::min(Green + DirectGreen * DiscStrength, 1.0);
            Blue  = std::min(Blue  + DirectBlue  * DiscStrength, 1.0);

            // 📐 The ground plane: beneath the horizon the sky-view surface holds the horizon's glow
            //    fading to nothing; the editor draws a dark ground there, blended AFTER the tone curve
            //    so the ground's own colour is not scaled with the sky's.
            const double GroundMix = std::clamp(-RayY * 900.0, 0.0, 1.0);
            if (GroundMix > 0.0)
            {
                const double GroundLight = 0.003 + 0.017 * Daylight;
                Red   = Red   * (1.0 - GroundMix) + GroundLight * GroundMix;
                Green = Green * (1.0 - GroundMix) + GroundLight * 1.05 * GroundMix;
                Blue  = Blue  * (1.0 - GroundMix) + GroundLight * 1.15 * GroundMix;
            }

            // 📝 A touch of turbidity warms the dome by pulling it toward the horizon's hue.
            const double Turbidity = std::clamp(Environment.SkyTurbidity, 1.0, 10.0);
            const double Warm = (Turbidity - 1.0) / 9.0 * 0.18 * Daylight;
            Red   = std::min(Red   + Warm * (1.0 - Red),   1.0);
            Green = std::min(Green + Warm * 0.5 * (1.0 - Green), 1.0);
            Blue  = std::min(Blue  - Warm * 0.4 * Blue, 1.0);

            const std::size_t Offset = (static_cast<std::size_t>(Y) * Width + X) * 4u;

            // 🔴 THE LADDER IS 8-BIT QUANTISATION, NOT RESOLUTION. Measured on the
            //    shipped bake, a column through the zenith band steps the blue
            //    channel 98 → 99 → 100 … one unit at a time, holding each value flat
            //    for up to 106 rows. The sky's gradient there is far shallower than
            //    1/255 per row, so rounding snaps a smooth ramp into wide plateaus
            //    with hard edges between them — and no amount of extra texels can
            //    help, because the steps are in the VALUE axis, not the pixel axis.
            //    (Bicubic sampling of the LUT was tried first and moved the longest
            //    flat run from 106 px to 108 px: the interpolation was never the
            //    problem.)
            //
            //    An ordered dither of +-0.5 LSB before rounding trades the plateau
            //    for a fine spatial mix of the two neighbouring levels, which the
            //    eye integrates back into the gradient the float actually held.
            constexpr double Bayer[4][4] =
            {
                {  0.0,  8.0,  2.0, 10.0 },
                { 12.0,  4.0, 14.0,  6.0 },
                {  3.0, 11.0,  1.0,  9.0 },
                { 15.0,  7.0, 13.0,  5.0 }
            };

            const double Threshold = (Bayer[Y & 3u][X & 3u] + 0.5) / 16.0 - 0.5;

            const auto Quantise = [&](double Channel) -> std::uint8_t
            {
                const double Scaled = Channel * 255.0 + Threshold;
                const double Held   = Scaled < 0.0 ? 0.0 : (Scaled > 255.0 ? 255.0 : Scaled);

                return static_cast<std::uint8_t>(Held + 0.5);
            };

            Pixels[Offset + 0u] = Quantise(Red);
            Pixels[Offset + 1u] = Quantise(Green);
            Pixels[Offset + 2u] = Quantise(Blue);
            Pixels[Offset + 3u] = 255u;
        }
    }

    return Deliver<bool>::Result(true);
}

} // namespace Slate
