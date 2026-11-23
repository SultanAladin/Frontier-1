/*============================================================================================================================================
                                                      INSPECTORVECTORGRAPHICS.H
============================================================================================================================================*/
// 🧩 Verbatim 1:1 pure-vector C++ port of ProjectZeroEditor HTML inspector graphics & SVG glyphs (zero CPU pixel loops, zero vkQueueWaitIdle)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"

#include <cstdint>
#include <span>

namespace Frontier::InspectorVector
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class HtmlGlyph : uint8_t
{
    CelestialOrbit = 0u, // [-] - Sun direction gold orbit ring + planets (Inspectors.jsx)
    BakeSparkle    = 1u, // [-] - F76 Bake icon (sparkle + arrow into frame + inner square)
    LoadUpload     = 2u, // [-] - F76 Load image upload arrow
    Check          = 3u, // [-] - Green Baked check mark
    Rebake         = 4u, // [-] - Circular counter-clockwise rebake arrow
    SaveDownload   = 5u, // [-] - Downward download tray arrow
    CameraIcon     = 6u, // [-] - Linked Editor Camera icon
    Plus           = 7u, // [-] - + Add notes icon
    ChevronRight   = 8u, // [-] - Right navigation chevron >
    Sun            = 9u, // [-] - 8-ray sun icon
    LuminousSurface= 10u,// [-] - Area light luminous panel glyph
    PointLight     = 11u,// [-] - Point emitter bulb/rays glyph
    SpotLight      = 12u,// [-] - Spot cone emitter glyph
    IesLight       = 13u,// [-] - Photometric IES polar web glyph
    TubeLight      = 14u,// [-] - Linear tube emitter glyph
    LedLight       = 15u,// [-] - SMD diode package glyph
    LedStrip       = 16u,// [-] - Linear LED strip dots glyph
    Environment    = 17u,// [-] - Environment hemisphere dome glyph
    ResetArc       = 18u,// [-] - ↺ Reset circular arrow
};

enum class RenderTypeThumbKind : uint8_t
{
    PbrRaster = 0u, // [-] - A · PBR (sky-blue lit sphere)
    SdfConeGi = 1u, // [-] - B · SDF (mint sphere + concentric SDF rings)
    RayTraced = 2u, // [-] - C · Ray traced (lavender sphere + dashed bounce rays)
    MatCap    = 3u, // [-] - D · MatCap (warm clay sphere)
};

enum class LightBlueprintKind : uint8_t
{
    Area        = 0u, // [-] - APERTURE / FRONT ELEVATION dotted emitter grid + W/H callouts
    Point       = 1u, // [-] - Concentric radial falloff rings + central emitter
    Spot        = 2u, // [-] - Inner/outer cone beam profile + aperture
    Ies         = 3u, // [-] - Polar candela distribution web + lobe
    Tube        = 4u, // [-] - Linear capsule elevation + length/diameter callouts
    Led         = 5u, // [-] - SMD package + Lambertian hemisphere lobe
    LedStrip    = 6u, // [-] - Linear ribbon with pitched LED nodes
    Environment = 7u, // [-] - Sky zenith / horizon / ground hemisphere dome
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SunOrbitGizmoState
{
    bool    Dragging      = false;
    Point2D DragOrigin    {};
    float   StartAzimuth  = 0.0f;
    float   StartElevation = 0.0f;
};

struct TransformTableDragState
{
    uint32_t ActiveCellId = 0u;
    float    DragOriginX  = 0.0f;
    float    StartValue   = 0.0f;
};

struct TransformTableRow
{
    const char* Title    = "";
    const char* Sublabel = "";
    float       X        = 0.0f;
    float       Y        = 0.0f;
    float       Z        = 0.0f;
    float       MinValue = -200.0f;
    float       MaxValue =  200.0f;
    float       Step     = 0.05f;
    int32_t     Decimals = 2;
};

struct LightBlueprintParams
{
    LightBlueprintKind     Kind           = LightBlueprintKind::Area;
    Mathematics::LinearRGB TintRgb        { 1.0f, 0.945f, 0.84f }; // [lin] - Emission color tint
    float                  WidthMeters    = 2.0f;                  // [m]   - Width / length / reach
    float                  HeightMeters   = 1.0f;                  // [m]   - Height / diameter / radius
    float                  SpreadDegrees  = 120.0f;                // [deg] - Beam spread / outer cone
    float                  InnerDegrees   = 22.0f;                 // [deg] - Inner hotspot angle
    float                  FalloffPower   = 2.0f;                  // [-]   - Attenuation exponent / pitch
    uint32_t               CountOrPreset  = 0u;                    // [idx] - Diode count / IES preset
    bool                   TwoSided       = false;                 // [-]   - Double-sided emission flag
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void ConstructHtmlGlyph(
    PixelSpace& Surface,
    HtmlGlyph   Glyph,
    Point2D     Center,
    float       SizePx,
    ColorRgba   Ink) noexcept;

// Exact HTML property card header with optional left vector glyph
void ConstructSectionSurfaceWithIcon(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    HtmlGlyph   Icon,
    ColorRgba   IconInk,
    const char* Title,
    const char* RightText = nullptr,
    ColorRgba   RightInk  = InspectorMetrics::MutedInk) noexcept;

// Exact HTML two-tone split metric ("2,400" white + ".0" dim + "lm" unit + optional caption)
[[nodiscard]] float ConstructSplitMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* WholePart,
    const char* FractionPart,
    const char* UnitText,
    const char* CaptionText = nullptr,
    float       NumberSize  = 32.0f) noexcept;

// Exact HTML header with top-right "+ Add notes" dashed pill and optional parent-component link card
[[nodiscard]] float ConstructInspectorHeaderEx(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Breadcrumb,
    const char*             Title,
    const char*             Subtitle,
    const char*             LinkedParentTitle,
    const char*             LinkedParentCaption,
    const PaneInputReading& Input,
    bool&                   OutAddNotesClicked,
    bool&                   OutParentLinkClicked) noexcept;

// Verbatim 1:1 port of environment-graphics.jsx::SunGizmo (360x280 polar horizon grid + day/night arcs + interactive drag)
bool ConstructSunOrbitGizmo(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    float                   AzimuthDegrees,
    float                   ElevationDegrees,
    SunOrbitGizmoState&     InOutDragState,
    const PaneInputReading& Input,
    float&                  OutNewAzimuth,
    float&                  OutNewElevation) noexcept;

// Verbatim 1:1 port of environment-graphics.jsx::IlluminanceCurve
void ConstructIlluminanceCurve(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       IntensityMultiplier,
    float       DirectGainMultiplier,
    float       MaxIntensity = 60.0f) noexcept;

// Verbatim 1:1 port of environment-graphics.jsx::ScatteringGraph
void ConstructScatteringGraph(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       RayleighScale,
    float       HazeScale) noexcept;

// Verbatim 1:1 port of environment-graphics.jsx::WindCompass
void ConstructWindCompass(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       BearingDegrees) noexcept;

// Verbatim 1:1 port of CameraMount.jsx::MountPicture (large hero 300x170 or small choice 72x52)
void ConstructCameraMountPicture(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    bool        Floating,
    float       HeightMeters,
    float       TiltDegrees,
    float       RollDegrees,
    bool        SmallThumb) noexcept;

// Verbatim 1:1 port of RenderComponents.jsx::TypeThumb (pure smooth concentric vector sphere + motif)
void ConstructRenderTypeSphere(
    PixelSpace&         Surface,
    Rectangle2D         Bounds,
    RenderTypeThumbKind Kind,
    bool                LargeHero) noexcept;

// Verbatim 1:1 port of RenderStudies.jsx::BudgetBar (#7ab8ff Shadows, #7be0ad GI, #c5a3ff Reflections)
[[nodiscard]] float ConstructRenderBudgetBar(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       Width,
    uint32_t    ShadowSamples,
    uint32_t    GiSamples,
    uint32_t    ReflectionSamples) noexcept;

// Verbatim 1:1 port of RenderStudies.jsx::PenumbraStudy
void ConstructPenumbraStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       Softness01,
    uint32_t    ShadowMapRes,
    uint32_t    RayCount,
    bool        UsesShadowMap) noexcept;

// Verbatim 1:1 port of RenderStudies.jsx::HemisphereStudy
void ConstructHemisphereStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    uint32_t    Mode0Off1Sdf2Bvh,
    uint32_t    ConeOrSampleCount,
    float       FillBoost) noexcept;

// Verbatim 1:1 port of RenderStudies.jsx::BounceStudy
void ConstructBounceStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    bool        TracedReflections,
    uint32_t    BounceCount,
    uint32_t    RayCount) noexcept;

// Verbatim 1:1 port of OpticalGraphic.jsx (camera lens + sensor gate + FOV cone)
void ConstructOpticalSchematic(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       FocalLengthMm,
    float       SensorWidthMm,
    float       AspectRatio,
    float       ApertureFStop) noexcept;

// Verbatim 1:1 port of TransformPanel.jsx (3-column X/Y/Z compact table + reset buttons)
[[nodiscard]] float ConstructTransformTable(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             HeadingTitle,
    const char*             SpaceBadgeText,
    std::span<TransformTableRow> Rows,
    uint32_t                ControlOriginId,
    TransformTableDragState& InOutDragState,
    const PaneInputReading& Input,
    int32_t&                OutResetRowIndex,
    bool&                   OutMutated) noexcept;

// Verbatim 1:1 pure-vector photometric blueprints for all 8 standalone Light Inspectors
void ConstructLightBlueprint(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    const LightBlueprintParams& Params) noexcept;

} // namespace Frontier::InspectorVector
