/*============================================================================================================================================
                                                         PATTERNSTAMPSTACKSESSION.CPP
============================================================================================================================================*/
// 🧩 Pattern Studio and Stamp Studio shared document, layer-stack tree, 22-channel PBR bindings, 8 modifiers, and undo/redo history.

#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Frontier::Workspaces
{

namespace
{

constexpr float KappaBezierCircle = 0.55228475f;
constexpr float TwoPiScalar       = 6.28318530718f;

std::filesystem::path ResolveWorkspaceFontPath(std::string_view CandidateOrRelative) noexcept
{
    if (CandidateOrRelative.empty())
    {
        CandidateOrRelative = "EngineContent/FontsArchives/Archivo/Archivo-Bold.ttf";
    }
    std::filesystem::path P(CandidateOrRelative);
    if (std::filesystem::exists(P))
    {
        return P;
    }
#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path FromRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / P;
    if (std::filesystem::exists(FromRoot))
    {
        return FromRoot;
    }
#endif
    return P;
}

std::string FormatSvgHexRgb(ColorRgba Tint) noexcept
{
    char Buf[16];
    std::snprintf(Buf, sizeof(Buf), "#%02x%02x%02x", Tint.Red, Tint.Green, Tint.Blue);
    return std::string(Buf);
}

constexpr PatternStampSurfaceChannelEntry ConstructChannelEntry(PatternStampSurfaceChannelKey Key, float Scalar, ColorRgba Tint) noexcept
{
    PatternStampSurfaceChannelEntry Out {};
    Out.Key           = Key;
    Out.ScalarReading = Scalar;
    Out.Tint          = Tint;
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   STATIC DESCRIPTORS
//------------------------------------------------------------------------------------------------------------------------

const std::vector<PatternStampSurfaceChannelDescriptor>& PatternStampWorkspaceSession::InspectChannelDescriptors() noexcept
{
    static const std::vector<PatternStampSurfaceChannelDescriptor> Descs {
        { PatternStampSurfaceChannelKey::Albedo,       "Albedo",       "Albedo",   "Base Colour",      true,  false, 0.0f,  1.0f, 0.01f, 0.50f, { 239u, 244u, 207u, 255u } },
        { PatternStampSurfaceChannelKey::Roughness,    "Roughness",    "Rough",    "Surface Gloss",    false, false, 0.02f, 1.0f, 0.01f, 0.26f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Metalness,    "Metalness",    "Metal",    "Metallic Reflectance",   false, false, 0.0f,  1.0f, 0.01f, 0.92f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Height,       "Height",       "Height",   "Relief / Bump",    false, false, -1.0f, 1.0f, 0.02f, 0.45f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::NormalStr,    "Normal Str",   "Normal",   "Bump Intensity",   false, true,  0.0f,  2.0f, 0.05f, 0.85f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Occlusion,    "Occlusion",    "AO",       "Ambient Cavity",   false, true,  0.0f,  1.0f, 0.01f, 1.00f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Opacity,      "Opacity",      "Opacity",  "Cutout / Alpha",   false, true,  0.0f,  1.0f, 0.01f, 1.00f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Emissive,     "Emissive",     "Emissive", "Glow Colour",      true,  true,  0.0f,  1.0f, 0.01f, 0.50f, { 255u, 122u, 59u, 255u } },
        { PatternStampSurfaceChannelKey::GlowPower,    "Glow Power",   "Glow",     "Luminance Gain",    false, true,  0.0f,  4.0f, 0.05f, 1.00f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Clearcoat,    "Clearcoat",    "Coat",     "Varnish Coat",     false, true,  0.0f,  1.0f, 0.01f, 0.80f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::CoatRough,    "Coat Rough",   "CoatRgh",  "Varnish Gloss",    false, true,  0.01f, 1.0f, 0.01f, 0.08f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Sheen,        "Sheen",        "Sheen",    "Fabric Velour",    false, true,  0.0f,  1.0f, 0.01f, 0.65f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::SheenRough,   "Sheen Rough",  "SheenRgh", "Microfiber Soft",  false, true,  0.01f, 1.0f, 0.01f, 0.45f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Specular,     "Specular",     "Spec",     "Dielectric F0",    false, true,  0.0f,  1.0f, 0.01f, 1.00f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Anisotropy,   "Anisotropy",   "Aniso",    "Brushed Grain",    false, true,  0.0f,  1.0f, 0.01f, 0.50f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Iridescence,  "Iridescence",  "Irid",     "Thin-Film Pearl",  false, true,  0.0f,  1.0f, 0.01f, 0.70f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::IridFilm,     "Irid Film",    "IridFilm", "Spectrum Shift",   false, true,  0.0f,  1.0f, 0.01f, 0.45f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Transmission, "Transmission", "Trans",    "Glass / Jelly",    false, true,  0.0f,  1.0f, 0.01f, 0.65f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::Subsurface,   "Subsurface",   "Depth",    "Depth / Scatter",  false, true,  0.0f,  1.0f, 0.01f, 0.40f, { 158u, 162u, 170u, 255u } },
        { PatternStampSurfaceChannelKey::SpecColour,   "Spec Colour",  "SpecCol",  "Specular Tint",    true,  true,  0.0f,  1.0f, 0.01f, 0.50f, { 255u, 255u, 255u, 255u } },
        { PatternStampSurfaceChannelKey::SheenColour,  "Sheen Colour", "SheenCol", "Fabric Velour Tint", true, true, 0.0f,  1.0f, 0.01f, 0.50f, { 255u, 255u, 255u, 255u } },
        { PatternStampSurfaceChannelKey::SubsurfTint,  "Subsurf Tint", "SubCol",   "Volume Attenuation", true, true, 0.0f,  1.0f, 0.01f, 0.50f, { 255u, 255u, 255u, 255u } },
    };
    return Descs;
}

const PatternStampSurfaceChannelDescriptor& PatternStampWorkspaceSession::QueryChannelDescriptor(PatternStampSurfaceChannelKey Key) noexcept
{
    const auto& Descs = InspectChannelDescriptors();
    const std::size_t Idx = static_cast<std::size_t>(Key);
    return Idx < Descs.size() ? Descs[Idx] : Descs[0];
}

const std::vector<PatternStampToolDescriptor>& PatternStampWorkspaceSession::InspectToolDescriptors() noexcept
{
    static const std::vector<PatternStampToolDescriptor> Tools {
        { PatternStampToolCategory::Select,        "Select",      "V", true },
        { PatternStampToolCategory::Curve,         "Curve",       "U", true },
        { PatternStampToolCategory::Pen,           "Pen",         "P", false },
        { PatternStampToolCategory::EditPoints,    "Edit Points", "A", false },
        { PatternStampToolCategory::PointGradient, "Point Grad",  "F", true },
        { PatternStampToolCategory::Square,        "Rect",        "R", true },
        { PatternStampToolCategory::Circle,        "Circle",      "O", true },
        { PatternStampToolCategory::Diamond,       "Diamond",     "D", true },
        { PatternStampToolCategory::Triangle,      "Tri",         "T", true },
        { PatternStampToolCategory::Star,          "Star",        "S", false },
        { PatternStampToolCategory::Hexagon,       "Hex",         "G", false },
        { PatternStampToolCategory::Stripe,        "Stripe",      "L", false },
        { PatternStampToolCategory::TextBlock,     "Text",        "X", true },
        { PatternStampToolCategory::HandPan,       "Pan",         "H", true },
    };
    return Tools;
}

const std::vector<const char*>& PatternStampWorkspaceSession::InspectMotifShapeLabels() noexcept
{
    static const std::vector<const char*> Labels {
        "Circle",
        "Ring",
        "Square",
        "Rounded Box",
        "Diamond",
        "Hexagon",
        "Triangle",
        "Star",
        "Cross",
        "Chevron",
        "Stripe / Bar",
        "Arc",
        "Teardrop",
        "Botanical Leaf",
        "5-Petal Flower",
        "Cog / Gear",
        "Line / Stroke",
        "Vector Path",
    };
    return Labels;
}

const std::vector<const char*>& PatternStampWorkspaceSession::InspectModifierLabels() noexcept
{
    static const std::vector<const char*> Labels {
        "Noise Scale",
        "Grid / Hex Array",
        "Radial Array",
        "Mirror Symmetry",
        "Inset / Echo",
        "Scatter & Dropout",
        "Domain Warp",
        "Bevel & Edge Wear",
    };
    return Labels;
}

const std::vector<const char*>& PatternStampWorkspaceSession::InspectFinishLabels() noexcept
{
    static const std::vector<const char*> Labels {
        "Printed Dye",
        "Woven Cotton",
        "Cut-Pile Wool",
        "Glazed Ceramic",
        "Metal Inlay",
        "Embossed Rubber",
    };
    return Labels;
}

const std::vector<const char*>& PatternStampWorkspaceSession::InspectViewChannelLabels() noexcept
{
    static const std::vector<const char*> Labels {
        "Vector SVG",
        "Seamless 2x2",
        "Single PBR Tile",
        "Albedo",
        "Roughness",
        "Metalness",
        "Height",
        "Normal",
        "Occlusion",
        "Opacity",
        "Emissive",
        "Glow Power",
        "Clearcoat",
        "Coat Rough",
        "Sheen",
        "Sheen Rough",
        "Specular",
        "Anisotropy",
        "Iridescence",
        "Irid Film",
        "Transmission",
        "Subsurface",
        "Spec Colour",
        "Sheen Colour",
        "Subsurf Tint",
    };
    return Labels;
}

std::string PatternStampWorkspaceSession::DescribeLayerSubtitle(const PatternStampLayerSpec& Layer) noexcept
{
    if (Layer.Category == PatternStampLayerCategory::Folder)
    {
        return "Folder";
    }
    std::string CategoryTag = "motif";
    if (Layer.Category == PatternStampLayerCategory::Motif)
    {
        const auto& ShapeNames = InspectMotifShapeLabels();
        const std::size_t SIdx = static_cast<std::size_t>(Layer.MotifShape);
        if (SIdx < ShapeNames.size())
        {
            CategoryTag = ShapeNames[SIdx];
            for (char& C : CategoryTag)
            {
                if (C >= 'A' && C <= 'Z')
                {
                    C = static_cast<char>(C + ('a' - 'A'));
                }
            }
        }
    }
    else if (Layer.Category == PatternStampLayerCategory::Typographic)
    {
        CategoryTag = "text";
    }
    else if (Layer.Category == PatternStampLayerCategory::GradientWash)
    {
        CategoryTag = "gradient";
    }

    static constexpr const char* BlendLabels[6] { "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken" };
    if (Layer.BlendMode > 0u && Layer.BlendMode < 6u)
    {
        CategoryTag += " · ";
        CategoryTag += BlendLabels[Layer.BlendMode];
    }

    std::size_t EnabledMods = 0u;
    for (const auto& M : Layer.Modifiers)
    {
        if (M.Enabled)
        {
            ++EnabledMods;
        }
    }
    if (EnabledMods > 0u)
    {
        CategoryTag += " · " + std::to_string(EnabledMods) + (EnabledMods == 1u ? " mod" : " mods");
    }
    return CategoryTag;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SESSION LOGIC
//------------------------------------------------------------------------------------------------------------------------

PatternStampWorkspaceSession::PatternStampWorkspaceSession(PatternStampWorkspaceMode InitialMode) noexcept
{
    ConfigureMode(InitialMode);
}

void PatternStampWorkspaceSession::ConfigureMode(PatternStampWorkspaceMode TargetMode) noexcept
{
    Mode          = TargetMode;
    ResolutionPx  = 1024u;
    ActiveTool    = PatternStampToolCategory::Select;
    ViewChannel   = PatternStampViewChannel::VectorSVG;
    ShowGrid      = true;
    ShowAxes      = true;
    SnapStepPx    = 16;
    ZoomFraction  = 1.0f;
    LayerFilter   = PatternStampLayerFilterMode::AllLayers;
    LayerSearch.clear();
    UndoSnapshots.clear();
    RedoSnapshots.clear();

    BuildPresetDirectory();
    LoadPresetByIndex(0u);
}

void PatternStampWorkspaceSession::BuildPresetDirectory() noexcept
{
    PresetDirectory.clear();
    if (Mode == PatternStampWorkspaceMode::Pattern)
    {
        PresetDirectory.push_back({ "hex-lattice",      "Noise-Scaled Hex Matrix",       "8x8 hex lattice · dual FBM scale",        { 239u, 244u, 207u, 255u }, { 118u, 178u, 216u, 255u } });
        PresetDirectory.push_back({ "kente-weave",      "Gold Foil Kente Weave",         "Interlocking stripe bars · metallic",     { 232u, 194u, 122u, 255u }, { 29u, 33u, 42u, 255u } });
        PresetDirectory.push_back({ "radial-mandala",   "Radial Ceramic Mandala",        "12-fold concentric teardrop lattice",     { 118u, 178u, 216u, 255u }, { 239u, 244u, 207u, 255u } });
        PresetDirectory.push_back({ "perlin-polka",     "Perlin Polka Wave",             "14x14 ceramic dots · Perlin scale-tint",  { 240u, 211u, 140u, 255u }, { 125u, 199u, 176u, 255u } });
        PresetDirectory.push_back({ "ridged-diamonds",  "Ridged Diamond Acoustic Grille","12x12 brick foil diamonds · Ridged FBM",  { 213u, 220u, 230u, 255u }, { 232u, 194u, 122u, 255u } });
        PresetDirectory.push_back({ "voronoi-pebbles",  "Voronoi Organic Pebbles",       "9x9 hex pebbles · scatter + Worley",      { 151u, 143u, 132u, 255u }, { 210u, 195u, 170u, 255u } });
    }
    else
    {
        PresetDirectory.push_back({ "solid-disc",       "Solid Circle Disc",             "Clean cutout circular stamp",             { 234u, 236u, 239u, 255u }, { 22u, 24u, 28u, 255u } });
        PresetDirectory.push_back({ "botanical-seal",   "Botanical Foil Seal",           "Engraved rosette with embossed rim",      { 232u, 194u, 122u, 255u }, { 24u, 27u, 31u, 255u } });
        PresetDirectory.push_back({ "precision-mark",   "Precision Cross Reticle",       "Technical stencil registration crosshair",{ 239u, 244u, 207u, 255u }, { 90u, 118u, 146u, 255u } });
        PresetDirectory.push_back({ "hex-fastener",     "Hexagonal Fastener Head",       "Chamfered metallic bolt & socket ring",   { 196u, 204u, 214u, 255u }, { 64u, 72u, 82u, 255u } });
        PresetDirectory.push_back({ "stencil-serial",   "Industrial Stencil Mark",       "Archivo-Bold typographic serial cutout",  { 240u, 198u, 96u, 255u },  { 28u, 30u, 34u, 255u } });
        PresetDirectory.push_back({ "radial-cog",       "Radial Cog Medallion",          "Precision gear contour + concentric echo",{ 143u, 184u, 222u, 255u }, { 239u, 244u, 207u, 255u } });
    }
}

void PatternStampWorkspaceSession::PopulatePatternHexArrayPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Noise-Scaled Hex Matrix";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 23u, 26u, 31u, 255u };
    Out.GroundTintB          = { 11u, 11u, 11u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    // Bottom layer (index 0): Noise-Scaled Dots
    PatternStampLayerSpec Dots {};
    Dots.Identity      = 1u;
    Dots.Name          = "Noise-Scaled Dots";
    Dots.Category          = PatternStampLayerCategory::Motif;
    Dots.Visible       = true;
    Dots.Opacity       = 1.0f;
    Dots.MotifShape    = PatternStampMotifShape::Hexagon;
    Dots.FillEnabled   = true;
    Dots.FillTint      = { 239u, 244u, 207u, 255u };
    Dots.FillTintB     = { 118u, 178u, 216u, 255u };
    Dots.CenterX       = 256.0f;
    Dots.CenterY       = 256.0f;
    Dots.WidthPx       = 42.0f;
    Dots.HeightPx      = 42.0f;
    Dots.FinishPreset  = PatternStampFinishPreset::PrintedDye;
    Dots.Channels      = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 239u, 244u, 207u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.34f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.12f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.25f, { 158u, 162u, 170u, 255u }),
    };
    Dots.Mask = { false, false, PatternStampMaskMode::Noise, 4.0f, 0.45f, 0.25f, 211 };

    PatternStampModifierSpec HexArr {};
    HexArr.Category       = PatternStampModifierCategory::GridArray;
    HexArr.GridLayout = 2u; // Hex
    HexArr.Columns    = 8;
    HexArr.Rows       = 8;

    PatternStampModifierSpec ScaleMod {};
    ScaleMod.Category           = PatternStampModifierCategory::NoiseScale;
    ScaleMod.NoiseAlgorithm = 0u;
    ScaleMod.ModulateTarget = 0u; // Scale + Color
    ScaleMod.MinScale       = 0.40f;
    ScaleMod.MaxScale       = 1.60f;
    ScaleMod.Frequency      = 3.5f;
    ScaleMod.TargetTintB    = { 118u, 178u, 216u, 255u };
    Dots.Modifiers = { HexArr, ScaleMod };

    // Top layer (index 1, selected by default matching 01-HTML-pattern.png): Fine Accent Rings
    PatternStampLayerSpec Rings {};
    Rings.Identity      = 2u;
    Rings.Name          = "Fine Accent Rings";
    Rings.Category          = PatternStampLayerCategory::Motif;
    Rings.Visible       = true;
    Rings.Opacity       = 0.72f;
    Rings.MotifShape    = PatternStampMotifShape::Ring;
    Rings.FillEnabled   = true;
    Rings.FillTint      = { 239u, 244u, 207u, 255u };
    Rings.FillTintB     = { 90u, 118u, 146u, 255u };
    Rings.CenterX       = 256.0f;
    Rings.CenterY       = 256.0f;
    Rings.WidthPx       = 16.0f;
    Rings.HeightPx      = 16.0f;
    Rings.FinishPreset  = PatternStampFinishPreset::MetalInlay;
    Rings.Channels      = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 239u, 244u, 207u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.26f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.92f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.45f, { 158u, 162u, 170u, 255u }),
    };
    Rings.Mask = { false, false, PatternStampMaskMode::Noise, 4.0f, 0.45f, 0.25f, 211 };

    PatternStampModifierSpec RingGrid {};
    RingGrid.Category       = PatternStampModifierCategory::GridArray;
    RingGrid.GridLayout = 2u;
    RingGrid.Columns    = 8;
    RingGrid.Rows       = 7;

    PatternStampModifierSpec RingNoise {};
    RingNoise.Category           = PatternStampModifierCategory::NoiseScale;
    RingNoise.NoiseAlgorithm = 0u;
    RingNoise.ModulateTarget = 0u;
    RingNoise.MinScale       = 0.40f;
    RingNoise.MaxScale       = 1.60f;
    RingNoise.Frequency      = 3.5f;
    RingNoise.TargetTintB    = { 118u, 178u, 216u, 255u };
    Rings.Modifiers = { RingNoise, RingGrid };

    Out.Layers          = { Dots, Rings };
    Out.SelectedLayerId = Rings.Identity;
}

void PatternStampWorkspaceSession::PopulatePatternKenteWeavePreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Gold Foil Kente Weave";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 24u, 26u, 32u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec WarpBars {};
    WarpBars.Identity   = 1u;
    WarpBars.Name       = "Woven Cotton Stripes";
    WarpBars.MotifShape = PatternStampMotifShape::StripeBar;
    WarpBars.FillTint   = { 239u, 244u, 207u, 255u };
    WarpBars.WidthPx    = 44.0f;
    WarpBars.HeightPx   = 480.0f;
    WarpBars.FinishPreset = PatternStampFinishPreset::WovenCotton;
    WarpBars.Channels   = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 239u, 244u, 207u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.84f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.14f, { 158u, 162u, 170u, 255u }),
    };
    PatternStampModifierSpec Grid {};
    Grid.Category    = PatternStampModifierCategory::GridArray;
    Grid.Columns = 6;
    Grid.Rows    = 1;
    WarpBars.Modifiers = { Grid };

    PatternStampLayerSpec FoilDiamonds {};
    FoilDiamonds.Identity   = 2u;
    FoilDiamonds.Name       = "Metal Inlay Diamonds";
    FoilDiamonds.MotifShape = PatternStampMotifShape::Diamond;
    FoilDiamonds.FillTint   = { 232u, 194u, 122u, 255u };
    FoilDiamonds.WidthPx    = 36.0f;
    FoilDiamonds.HeightPx   = 36.0f;
    FoilDiamonds.FinishPreset = PatternStampFinishPreset::MetalInlay;
    FoilDiamonds.Channels   = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 232u, 194u, 122u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.24f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 1.00f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.35f, { 158u, 162u, 170u, 255u }),
    };
    PatternStampModifierSpec GridD {};
    GridD.Category       = PatternStampModifierCategory::GridArray;
    GridD.GridLayout = 1u;
    GridD.Columns    = 6;
    GridD.Rows       = 6;
    FoilDiamonds.Modifiers = { GridD };

    Out.Layers          = { WarpBars, FoilDiamonds };
    Out.SelectedLayerId = FoilDiamonds.Identity;
}

void PatternStampWorkspaceSession::PopulatePatternRadialMandalaPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Radial Ceramic Mandala";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 19u, 22u, 28u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Petals {};
    Petals.Identity   = 1u;
    Petals.Name       = "Cobalt Petals";
    Petals.MotifShape = PatternStampMotifShape::Teardrop;
    Petals.FillTint   = { 118u, 178u, 216u, 255u };
    Petals.WidthPx    = 48.0f;
    Petals.HeightPx   = 96.0f;
    Petals.FinishPreset = PatternStampFinishPreset::GlazedCeramic;
    Petals.Channels   = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 118u, 178u, 216u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.18f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Clearcoat, 0.85f, { 158u, 162u, 170u, 255u }),
    };
    PatternStampModifierSpec Radial {};
    Radial.Category        = PatternStampModifierCategory::RadialArray;
    Radial.RadialCount = 12;
    Radial.RadiusPx    = 140.0f;
    Radial.Rings       = 2;
    Petals.Modifiers = { Radial };

    Out.Layers          = { Petals };
    Out.SelectedLayerId = Petals.Identity;
}

void PatternStampWorkspaceSession::PopulateStampSolidCirclePreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Solid Circle Disc";
    Out.GroundEnabled        = false; // transparent alpha cutout matching 01-HTML-stamp.png
    Out.GroundTintA          = { 23u, 26u, 31u, 255u };
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Disc {};
    Disc.Identity     = 1u;
    Disc.Name         = "Solid Disc";
    Disc.Category         = PatternStampLayerCategory::Motif;
    Disc.Visible      = true;
    Disc.Opacity      = 1.0f;
    Disc.MotifShape   = PatternStampMotifShape::Circle;
    Disc.FillEnabled  = true;
    Disc.FillTint     = { 234u, 236u, 239u, 255u };
    Disc.FillTintB    = { 22u, 24u, 28u, 255u };
    Disc.CenterX      = 256.0f;
    Disc.CenterY      = 256.0f;
    Disc.WidthPx      = 300.0f;
    Disc.HeightPx     = 300.0f;
    Disc.FinishPreset = PatternStampFinishPreset::PrintedDye;
    Disc.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 234u, 236u, 239u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.62f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.00f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.25f, { 158u, 162u, 170u, 255u }),
    };
    Disc.Mask = { false, false, PatternStampMaskMode::Noise, 4.0f, 0.45f, 0.25f, 211 };

    Out.Layers          = { Disc };
    Out.SelectedLayerId = Disc.Identity;
}

void PatternStampWorkspaceSession::PopulateStampBotanicalCrestPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Botanical Foil Seal";
    Out.GroundEnabled        = false;
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Rim {};
    Rim.Identity      = 1u;
    Rim.Name          = "Embossed Ring Rim";
    Rim.MotifShape    = PatternStampMotifShape::Ring;
    Rim.FillTint      = { 232u, 194u, 122u, 255u };
    Rim.WidthPx       = 340.0f;
    Rim.HeightPx      = 340.0f;
    Rim.FinishPreset  = PatternStampFinishPreset::MetalInlay;
    Rim.Channels      = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 232u, 194u, 122u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.24f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 1.00f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.50f, { 158u, 162u, 170u, 255u }),
    };
    PatternStampModifierSpec Wear {};
    Wear.Category           = PatternStampModifierCategory::BevelEdgeWear;
    Wear.ChipWearAmount = 0.28f;
    Rim.Modifiers = { Wear };

    PatternStampLayerSpec Crest {};
    Crest.Identity     = 2u;
    Crest.Name         = "Leaf Crest";
    Crest.MotifShape   = PatternStampMotifShape::FivePetalRosette;
    Crest.FillTint     = { 239u, 244u, 207u, 255u };
    Crest.WidthPx      = 180.0f;
    Crest.HeightPx     = 180.0f;
    Crest.FinishPreset = PatternStampFinishPreset::MetalInlay;
    Crest.Channels     = Rim.Channels;

    Out.Layers          = { Rim, Crest };
    Out.SelectedLayerId = Crest.Identity;
}

void PatternStampWorkspaceSession::PopulateStampPrecisionReticlePreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Precision Cross Reticle";
    Out.GroundEnabled        = false;
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Reticle {};
    Reticle.Identity     = 1u;
    Reticle.Name         = "Crosshair Mark";
    Reticle.MotifShape   = PatternStampMotifShape::Cross;
    Reticle.FillTint     = { 239u, 244u, 207u, 255u };
    Reticle.WidthPx      = 240.0f;
    Reticle.HeightPx     = 240.0f;
    Reticle.FinishPreset = PatternStampFinishPreset::PrintedDye;
    Reticle.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 239u, 244u, 207u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.55f, { 158u, 162u, 170u, 255u }),
    };

    Out.Layers          = { Reticle };
    Out.SelectedLayerId = Reticle.Identity;
}

void PatternStampWorkspaceSession::PopulatePatternPerlinPolkaPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Perlin Polka Wave";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 19u, 23u, 29u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Dots {};
    Dots.Identity     = 1u;
    Dots.Name         = "Perlin Dots";
    Dots.MotifShape   = PatternStampMotifShape::Circle;
    Dots.FillTint     = { 240u, 211u, 140u, 255u };
    Dots.FillTintB    = { 125u, 199u, 176u, 255u };
    Dots.WidthPx      = 34.0f;
    Dots.HeightPx     = 34.0f;
    Dots.FinishPreset = PatternStampFinishPreset::GlazedCeramic;
    Dots.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 240u, 211u, 140u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.22f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.10f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.32f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampModifierSpec Grid {};
    Grid.Category   = PatternStampModifierCategory::GridArray;
    Grid.GridLayout = 0u;
    Grid.Columns    = 14;
    Grid.Rows       = 14;

    PatternStampModifierSpec Noise {};
    Noise.Category       = PatternStampModifierCategory::NoiseScale;
    Noise.NoiseAlgorithm = 1u; // Perlin
    Noise.ModulateTarget = 0u; // Scale + Color
    Noise.MinScale       = 0.18f;
    Noise.MaxScale       = 1.72f;
    Noise.Frequency      = 4.0f;
    Noise.TargetTintB    = { 228u, 123u, 100u, 255u };

    Dots.Modifiers      = { Grid, Noise };
    Out.Layers          = { Dots };
    Out.SelectedLayerId = Dots.Identity;
}

void PatternStampWorkspaceSession::PopulatePatternRidgedDiamondsPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Ridged Diamond Acoustic Grille";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 16u, 18u, 21u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Diamonds {};
    Diamonds.Identity     = 1u;
    Diamonds.Name         = "Diamond Perforation";
    Diamonds.MotifShape   = PatternStampMotifShape::Diamond;
    Diamonds.FillTint     = { 213u, 220u, 230u, 255u };
    Diamonds.FillTintB    = { 232u, 194u, 122u, 255u };
    Diamonds.WidthPx      = 38.0f;
    Diamonds.HeightPx     = 38.0f;
    Diamonds.FinishPreset = PatternStampFinishPreset::MetalInlay;
    Diamonds.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 213u, 220u, 230u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.28f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.92f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.45f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampModifierSpec Brick {};
    Brick.Category   = PatternStampModifierCategory::GridArray;
    Brick.GridLayout = 1u; // Brick
    Brick.Columns    = 12;
    Brick.Rows       = 12;

    PatternStampModifierSpec Ridged {};
    Ridged.Category       = PatternStampModifierCategory::NoiseScale;
    Ridged.NoiseAlgorithm = 3u; // Ridged
    Ridged.ModulateTarget = 0u;
    Ridged.MinScale       = 0.25f;
    Ridged.MaxScale       = 1.65f;
    Ridged.Frequency      = 3.0f;
    Ridged.TargetTintB    = { 232u, 194u, 122u, 255u };

    Diamonds.Modifiers  = { Brick, Ridged };
    Out.Layers          = { Diamonds };
    Out.SelectedLayerId = Diamonds.Identity;
}

void PatternStampWorkspaceSession::PopulatePatternVoronoiPebblesPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Voronoi Organic Pebbles";
    Out.GroundEnabled        = true;
    Out.GroundTintA          = { 35u, 33u, 30u, 255u };
    Out.ShowGhostTileBorders = true;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Pebbles {};
    Pebbles.Identity     = 1u;
    Pebbles.Name         = "Rounded Pebbles";
    Pebbles.MotifShape   = PatternStampMotifShape::RoundedBox;
    Pebbles.FillTint     = { 151u, 143u, 132u, 255u };
    Pebbles.FillTintB    = { 210u, 195u, 170u, 255u };
    Pebbles.WidthPx      = 46.0f;
    Pebbles.HeightPx     = 40.0f;
    Pebbles.RotationDeg  = 15.0f;
    Pebbles.FinishPreset = PatternStampFinishPreset::GlazedCeramic;
    Pebbles.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 151u, 143u, 132u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.65f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.55f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampModifierSpec HexGrid {};
    HexGrid.Category   = PatternStampModifierCategory::GridArray;
    HexGrid.GridLayout = 2u; // Hex
    HexGrid.Columns    = 9;
    HexGrid.Rows       = 9;

    PatternStampModifierSpec Scatter {};
    Scatter.Category     = PatternStampModifierCategory::ScatterDropout;
    Scatter.PosJitterPx  = 12.0f;
    Scatter.RotJitterDeg = 35.0f;
    Scatter.Dropout      = 0.06f;

    PatternStampModifierSpec Worley {};
    Worley.Category       = PatternStampModifierCategory::NoiseScale;
    Worley.NoiseAlgorithm = 2u; // Worley
    Worley.ModulateTarget = 0u;
    Worley.MinScale       = 0.45f;
    Worley.MaxScale       = 1.45f;
    Worley.Frequency      = 3.0f;
    Worley.TargetTintB    = { 223u, 211u, 191u, 255u };

    Pebbles.Modifiers   = { HexGrid, Scatter, Worley };
    Out.Layers          = { Pebbles };
    Out.SelectedLayerId = Pebbles.Identity;
}

void PatternStampWorkspaceSession::PopulateStampHexFastenerPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Hexagonal Fastener Head";
    Out.GroundEnabled        = false;
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec HexHead {};
    HexHead.Identity     = 1u;
    HexHead.Name         = "Chamfered Hex Flange";
    HexHead.MotifShape   = PatternStampMotifShape::Hexagon;
    HexHead.FillTint     = { 196u, 204u, 214u, 255u };
    HexHead.WidthPx      = 330.0f;
    HexHead.HeightPx     = 330.0f;
    HexHead.FinishPreset = PatternStampFinishPreset::MetalInlay;
    HexHead.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 196u, 204u, 214u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.28f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.96f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.58f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampLayerSpec Socket {};
    Socket.Identity     = 2u;
    Socket.Name         = "Recessed Socket Ring";
    Socket.MotifShape   = PatternStampMotifShape::Ring;
    Socket.FillTint     = { 68u, 76u, 88u, 255u };
    Socket.WidthPx      = 176.0f;
    Socket.HeightPx     = 176.0f;
    Socket.FinishPreset = PatternStampFinishPreset::MetalInlay;
    Socket.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 68u, 76u, 88u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.44f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.88f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,   -0.35f, { 158u, 162u, 170u, 255u }),
    };

    Out.Layers          = { HexHead, Socket };
    Out.SelectedLayerId = Socket.Identity;
}

void PatternStampWorkspaceSession::PopulateStampIndustrialStencilPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Industrial Stencil Mark";
    Out.GroundEnabled        = false;
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec BorderBar {};
    BorderBar.Identity      = 1u;
    BorderBar.Name          = "Caution Frame Box";
    BorderBar.MotifShape    = PatternStampMotifShape::RoundedBox;
    BorderBar.FillEnabled   = false;
    BorderBar.StrokeWidthPx = 14.0f;
    BorderBar.StrokeTint    = { 240u, 198u, 96u, 255u };
    BorderBar.WidthPx       = 390.0f;
    BorderBar.HeightPx      = 186.0f;
    BorderBar.FinishPreset  = PatternStampFinishPreset::PrintedDye;
    BorderBar.Channels      = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 240u, 198u, 96u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.58f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampLayerSpec SerialText {};
    SerialText.Identity     = 2u;
    SerialText.Name         = "Serial Stencil Text";
    SerialText.Category     = PatternStampLayerCategory::Typographic;
    SerialText.TextContent  = "FR-04";
    SerialText.FontSizePx   = 92.0f;
    SerialText.FillTint     = { 240u, 198u, 96u, 255u };
    SerialText.WidthPx      = 320.0f;
    SerialText.HeightPx     = 110.0f;
    SerialText.FinishPreset = PatternStampFinishPreset::PrintedDye;
    SerialText.Channels     = BorderBar.Channels;

    Out.Layers          = { BorderBar, SerialText };
    Out.SelectedLayerId = SerialText.Identity;
}

void PatternStampWorkspaceSession::PopulateStampRadialCogPreset(PatternStampDocumentSnapshot& Out) noexcept
{
    Out = {};
    Out.DocumentName         = "Radial Cog Medallion";
    Out.GroundEnabled        = false;
    Out.ShowGhostTileBorders = false;
    Out.NextIdentitySeed     = 20u;

    PatternStampLayerSpec Gear {};
    Gear.Identity     = 1u;
    Gear.Name         = "Precision Gear Contour";
    Gear.MotifShape   = PatternStampMotifShape::CogGear;
    Gear.FillTint     = { 143u, 184u, 222u, 255u };
    Gear.WidthPx      = 336.0f;
    Gear.HeightPx     = 336.0f;
    Gear.FinishPreset = PatternStampFinishPreset::MetalInlay;
    Gear.Channels     = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  { 143u, 184u, 222u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.25f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.92f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.48f, { 158u, 162u, 170u, 255u }),
    };

    PatternStampModifierSpec Echo {};
    Echo.Category    = PatternStampModifierCategory::InsetEcho;
    Echo.EchoCopies  = 3;
    Echo.InsetStepPx = 32.0f;
    Echo.AltTint     = { 239u, 244u, 207u, 255u };
    Gear.Modifiers   = { Echo };

    Out.Layers          = { Gear };
    Out.SelectedLayerId = Gear.Identity;
}

void PatternStampWorkspaceSession::LoadPresetByIndex(std::size_t Index) noexcept
{
    if (PresetDirectory.empty())
    {
        return;
    }
    ActivePresetIndex = Index % PresetDirectory.size();
    UndoSnapshots.clear();
    RedoSnapshots.clear();

    if (Mode == PatternStampWorkspaceMode::Pattern)
    {
        switch (ActivePresetIndex)
        {
            case 0u: PopulatePatternHexArrayPreset(Document);        break;
            case 1u: PopulatePatternKenteWeavePreset(Document);      break;
            case 2u: PopulatePatternRadialMandalaPreset(Document);   break;
            case 3u: PopulatePatternPerlinPolkaPreset(Document);     break;
            case 4u: PopulatePatternRidgedDiamondsPreset(Document);  break;
            default: PopulatePatternVoronoiPebblesPreset(Document);  break;
        }
        StatusNotice = "Non-destructive Modifier Stack";
    }
    else
    {
        switch (ActivePresetIndex)
        {
            case 0u: PopulateStampSolidCirclePreset(Document);       break;
            case 1u: PopulateStampBotanicalCrestPreset(Document);    break;
            case 2u: PopulateStampPrecisionReticlePreset(Document);  break;
            case 3u: PopulateStampHexFastenerPreset(Document);       break;
            case 4u: PopulateStampIndustrialStencilPreset(Document); break;
            default: PopulateStampRadialCogPreset(Document);         break;
        }
        StatusNotice = "Non-destructive Modifier Stack";
    }
    for (auto& L : Document.Layers)
    {
        InitializeDefaultPointFieldPins(L);
    }
    BumpRevision();
    RecomputeSummaryMetrics();
}

const std::vector<PatternStampPresetDescriptor>& PatternStampWorkspaceSession::QueryPresetDirectory() const noexcept
{
    return PresetDirectory;
}

PatternStampLayerSpec* PatternStampWorkspaceSession::FindLayerById(uint32_t Identity) noexcept
{
    for (auto& L : Document.Layers)
    {
        if (L.Identity == Identity)
        {
            return &L;
        }
    }
    return nullptr;
}

const PatternStampLayerSpec* PatternStampWorkspaceSession::FindLayerById(uint32_t Identity) const noexcept
{
    for (const auto& L : Document.Layers)
    {
        if (L.Identity == Identity)
        {
            return &L;
        }
    }
    return nullptr;
}

PatternStampLayerSpec* PatternStampWorkspaceSession::InspectSelectedLayer() noexcept
{
    if (PatternStampLayerSpec* Found = FindLayerById(Document.SelectedLayerId))
    {
        return Found;
    }
    return Document.Layers.empty() ? nullptr : &Document.Layers.back();
}

const PatternStampLayerSpec* PatternStampWorkspaceSession::InspectSelectedLayer() const noexcept
{
    if (const PatternStampLayerSpec* Found = FindLayerById(Document.SelectedLayerId))
    {
        return Found;
    }
    return Document.Layers.empty() ? nullptr : &Document.Layers.back();
}

bool PatternStampWorkspaceSession::QueryEffectiveVisible(uint32_t Identity) const noexcept
{
    uint32_t CurId = Identity;
    for (int Hop = 0; Hop < 8; ++Hop)
    {
        const PatternStampLayerSpec* Cur = FindLayerById(CurId);
        if (Cur == nullptr || !Cur->Visible)
        {
            return false;
        }
        if (Cur->EnclosingFolderId == 0u)
        {
            return true;
        }
        CurId = Cur->EnclosingFolderId;
    }
    return true;
}

uint32_t PatternStampWorkspaceSession::QueryLayerDepth(uint32_t Identity) const noexcept
{
    uint32_t Depth = 0u;
    uint32_t CurId = Identity;
    for (int Hop = 0; Hop < 4; ++Hop)
    {
        const PatternStampLayerSpec* Cur = FindLayerById(CurId);
        if (Cur == nullptr || Cur->EnclosingFolderId == 0u || FindLayerById(Cur->EnclosingFolderId) == nullptr)
        {
            break;
        }
        ++Depth;
        CurId = Cur->EnclosingFolderId;
    }
    return Depth;
}

bool PatternStampWorkspaceSession::QueryUnderCollapsedFolder(uint32_t Identity) const noexcept
{
    uint32_t CurId = Identity;
    for (int Hop = 0; Hop < 8; ++Hop)
    {
        const PatternStampLayerSpec* Cur = FindLayerById(CurId);
        if (Cur == nullptr || Cur->EnclosingFolderId == 0u)
        {
            return false;
        }
        const PatternStampLayerSpec* Folder = FindLayerById(Cur->EnclosingFolderId);
        if (Folder == nullptr)
        {
            return false;
        }
        if (Folder->CollapsedFolder)
        {
            return true;
        }
        CurId = Folder->Identity;
    }
    return false;
}

std::size_t PatternStampWorkspaceSession::CountVisibleLayers() const noexcept
{
    std::size_t Count = 0u;
    for (const auto& L : Document.Layers)
    {
        if (QueryEffectiveVisible(L.Identity))
        {
            ++Count;
        }
    }
    return Count;
}

std::size_t PatternStampWorkspaceSession::CountEnabledModifiers() const noexcept
{
    std::size_t Count = 0u;
    for (const auto& L : Document.Layers)
    {
        for (const auto& M : L.Modifiers)
        {
            if (M.Enabled)
            {
                ++Count;
            }
        }
    }
    return Count;
}

std::size_t PatternStampWorkspaceSession::CountFolderImmediateMembers(uint32_t FolderIdentity) const noexcept
{
    std::size_t Count = 0u;
    for (const auto& L : Document.Layers)
    {
        if (L.EnclosingFolderId == FolderIdentity)
        {
            ++Count;
        }
    }
    return Count;
}

uint64_t PatternStampWorkspaceSession::AllocateSessionSerial() noexcept
{
    static std::atomic<uint64_t> GlobalSerialCounter { 1ull };
    return GlobalSerialCounter.fetch_add(1ull, std::memory_order_relaxed);
}

void PatternStampWorkspaceSession::SelectLayer(uint32_t Identity) noexcept
{
    if (FindLayerById(Identity) != nullptr)
    {
        Document.SelectedLayerId = Identity;
    }
}

void PatternStampWorkspaceSession::AddLayer(PatternStampLayerCategory Category) noexcept
{
    if (Document.Layers.size() >= MaxLayerLimit)
    {
        return;
    }
    PushUndoSnapshot();

    PatternStampLayerSpec Next {};
    Next.Identity   = Document.NextIdentitySeed++;
    Next.Category       = Category;
    Next.Visible    = true;
    Next.Opacity    = 1.0f;
    Next.CenterX    = 256.0f;
    Next.CenterY    = 256.0f;
    Next.WidthPx    = 128.0f;
    Next.HeightPx   = 128.0f;
    Next.FillEnabled = true;
    Next.FillTint   = (Mode == PatternStampWorkspaceMode::Pattern)
        ? ColorRgba{ 239u, 244u, 207u, 255u }
        : ColorRgba{ 232u, 194u, 122u, 255u };
    Next.FillTintB  = { 90u, 118u, 146u, 255u };

    if (Category == PatternStampLayerCategory::Typographic)
    {
        Next.Name       = "Decal Typography";
        Next.TextContent = "FRONTIER";
        Next.FontSizePx = 54.0f;
    }
    else if (Category == PatternStampLayerCategory::GradientWash)
    {
        Next.Name       = "Gradient Wash";
        Next.FillSource = PatternStampFillSourceMode::LinearGradient;
        Next.WidthPx    = 512.0f;
        Next.HeightPx   = 512.0f;
    }
    else if (Category == PatternStampLayerCategory::Folder)
    {
        Next.Name       = "Group Folder";
        Next.FillEnabled = false;
    }
    else
    {
        Next.Name       = "Motif Shape";
        Next.MotifShape = PatternStampMotifShape::Circle;
    }

    if (Category != PatternStampLayerCategory::Folder)
    {
        Next.Channels = {
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  Next.FillTint),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.34f, { 158u, 162u, 170u, 255u }),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.12f, { 158u, 162u, 170u, 255u }),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.25f, { 158u, 162u, 170u, 255u }),
        };
        Next.Mask = { false, false, PatternStampMaskMode::Noise, 4.0f, 0.45f, 0.25f, 211 };
        InitializeDefaultPointFieldPins(Next);
        SeedMotifCubicSubpathsIfEmpty(Next);
    }

    Document.Layers.push_back(std::move(Next));
    Document.SelectedLayerId = Document.Layers.back().Identity;
    BumpRevision();
    RecomputeSummaryMetrics();
}

bool PatternStampWorkspaceSession::DuplicateSelectedLayer() noexcept
{
    const PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr || Document.Layers.size() >= MaxLayerLimit)
    {
        return false;
    }
    PushUndoSnapshot();

    PatternStampLayerSpec Copy = *Sel;
    Copy.Identity         = Document.NextIdentitySeed++;
    Copy.Name             = Sel->Name + " Copy";
    if (Copy.Category != PatternStampLayerCategory::Folder)
    {
        Copy.CenterX = std::min(512.0f, Copy.CenterX + 18.0f);
        Copy.CenterY = std::min(512.0f, Copy.CenterY + 18.0f);
    }

    const uint32_t OriginalId = Sel->Identity;
    auto It = std::find_if(Document.Layers.begin(), Document.Layers.end(), [OriginalId](const PatternStampLayerSpec& L) { return L.Identity == OriginalId; });
    if (It != Document.Layers.end())
    {
        ++It;
    }
    Document.Layers.insert(It, Copy);
    Document.SelectedLayerId = Copy.Identity;
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::DeleteSelectedLayer() noexcept
{
    if (Document.Layers.size() <= 1u)
    {
        return false;
    }
    const PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr)
    {
        return false;
    }
    PushUndoSnapshot();

    const uint32_t DelId = Sel->Identity;
    for (auto& L : Document.Layers)
    {
        if (L.EnclosingFolderId == DelId)
        {
            L.EnclosingFolderId = 0u;
        }
    }
    Document.Layers.erase(
        std::remove_if(Document.Layers.begin(), Document.Layers.end(), [DelId](const PatternStampLayerSpec& L) { return L.Identity == DelId; }),
        Document.Layers.end());
    Document.SelectedLayerId = Document.Layers.empty() ? 0u : Document.Layers.back().Identity;
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::MoveLayerStep(uint32_t Identity, int32_t DeltaUpInStack) noexcept
{
    auto It = std::find_if(Document.Layers.begin(), Document.Layers.end(), [Identity](const PatternStampLayerSpec& L) { return L.Identity == Identity; });
    if (It == Document.Layers.end())
    {
        return false;
    }
    const std::ptrdiff_t Idx  = std::distance(Document.Layers.begin(), It);
    const std::ptrdiff_t Next = Idx + (DeltaUpInStack > 0 ? 1 : -1);
    if (Next < 0 || static_cast<std::size_t>(Next) >= Document.Layers.size())
    {
        return false;
    }
    PushUndoSnapshot();
    std::swap(Document.Layers[static_cast<std::size_t>(Idx)], Document.Layers[static_cast<std::size_t>(Next)]);
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::ReorderLayerRelative(uint32_t SourceId, uint32_t TargetId, bool DropAboveVisual) noexcept
{
    if (SourceId == TargetId)
    {
        return false;
    }
    auto SrcIt = std::find_if(Document.Layers.begin(), Document.Layers.end(), [SourceId](const PatternStampLayerSpec& L) { return L.Identity == SourceId; });
    if (SrcIt == Document.Layers.end())
    {
        return false;
    }
    auto TgtCheck = std::find_if(Document.Layers.begin(), Document.Layers.end(), [TargetId](const PatternStampLayerSpec& L) { return L.Identity == TargetId; });
    if (TgtCheck == Document.Layers.end())
    {
        return false;
    }
    PushUndoSnapshot();

    PatternStampLayerSpec Moved = *SrcIt;
    Document.Layers.erase(SrcIt);
    auto TgtIt = std::find_if(Document.Layers.begin(), Document.Layers.end(), [TargetId](const PatternStampLayerSpec& L) { return L.Identity == TargetId; });
    if (TgtIt == Document.Layers.end())
    {
        Document.Layers.push_back(std::move(Moved));
    }
    else
    {
        if (DropAboveVisual)
        {
            ++TgtIt;
        }
        Document.Layers.insert(TgtIt, std::move(Moved));
    }
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

void PatternStampWorkspaceSession::ToggleLayerVisible(uint32_t Identity) noexcept
{
    if (PatternStampLayerSpec* L = FindLayerById(Identity))
    {
        PushUndoSnapshot();
        L->Visible = !L->Visible;
        BumpRevision();
        RecomputeSummaryMetrics();
    }
}

void PatternStampWorkspaceSession::ToggleLayerLocked(uint32_t Identity) noexcept
{
    if (PatternStampLayerSpec* L = FindLayerById(Identity))
    {
        L->Locked = !L->Locked;
    }
}

void PatternStampWorkspaceSession::ToggleLayerExpandedDrawer(uint32_t Identity) noexcept
{
    if (PatternStampLayerSpec* L = FindLayerById(Identity))
    {
        L->ExpandedInStack = !L->ExpandedInStack;
    }
}

void PatternStampWorkspaceSession::ToggleFolderCollapsed(uint32_t Identity) noexcept
{
    if (PatternStampLayerSpec* L = FindLayerById(Identity))
    {
        if (L->Category == PatternStampLayerCategory::Folder)
        {
            L->CollapsedFolder = !L->CollapsedFolder;
            BumpRevision();
        }
    }
}

void PatternStampWorkspaceSession::ToggleSelectedLayerMask() noexcept
{
    if (PatternStampLayerSpec* Sel = InspectSelectedLayer())
    {
        if (Sel->Category != PatternStampLayerCategory::Folder)
        {
            PushUndoSnapshot();
            Sel->Mask.Enabled = !Sel->Mask.Enabled;
            BumpRevision();
            RecomputeSummaryMetrics();
        }
    }
}

std::vector<PatternStampSurfaceChannelKey> PatternStampWorkspaceSession::QueryAvailableChannelsForSelectedLayer() const noexcept
{
    std::vector<PatternStampSurfaceChannelKey> Avail;
    const PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr || Sel->Category == PatternStampLayerCategory::Folder)
    {
        return Avail;
    }
    for (const auto& Desc : InspectChannelDescriptors())
    {
        const bool Present = std::any_of(Sel->Channels.begin(), Sel->Channels.end(), [&Desc](const PatternStampSurfaceChannelEntry& B) { return B.Key == Desc.Key; });
        if (!Present)
        {
            Avail.push_back(Desc.Key);
        }
    }
    return Avail;
}

bool PatternStampWorkspaceSession::AddChannelToSelectedLayer(PatternStampSurfaceChannelKey ChannelKey) noexcept
{
    PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr || Sel->Category == PatternStampLayerCategory::Folder)
    {
        return false;
    }
    const bool Exists = std::any_of(Sel->Channels.begin(), Sel->Channels.end(), [ChannelKey](const PatternStampSurfaceChannelEntry& B) { return B.Key == ChannelKey; });
    if (Exists)
    {
        return false;
    }
    PushUndoSnapshot();
    const auto& Desc = QueryChannelDescriptor(ChannelKey);
    Sel->Channels.push_back(ConstructChannelEntry(ChannelKey, Desc.DefaultScalar, Desc.DefaultTint));
    BumpRevision();
    return true;
}

bool PatternStampWorkspaceSession::RemoveChannelFromSelectedLayer(PatternStampSurfaceChannelKey ChannelKey) noexcept
{
    PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr)
    {
        return false;
    }
    const std::size_t Before = Sel->Channels.size();
    PushUndoSnapshot();
    Sel->Channels.erase(
        std::remove_if(Sel->Channels.begin(), Sel->Channels.end(), [ChannelKey](const PatternStampSurfaceChannelEntry& B) { return B.Key == ChannelKey; }),
        Sel->Channels.end());
    if (Sel->Channels.size() != Before)
    {
        BumpRevision();
        return true;
    }
    return false;
}

PatternStampSurfaceChannelEntry* PatternStampWorkspaceSession::FindChannelEntry(PatternStampLayerSpec& Layer, PatternStampSurfaceChannelKey ChannelKey) noexcept
{
    for (auto& C : Layer.Channels)
    {
        if (C.Key == ChannelKey)
        {
            return &C;
        }
    }
    return nullptr;
}

void PatternStampWorkspaceSession::ApplyFinishPresetToSelectedLayer(PatternStampFinishPreset Preset) noexcept
{
    PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr)
    {
        return;
    }
    PushUndoSnapshot();
    Sel->FinishPreset = Preset;
    struct FinishTriplet { float R; float M; float H; };
    static constexpr std::array<FinishTriplet, 6> Presets {{
        { 0.62f, 0.00f, 0.02f }, // Printed Dye
        { 0.84f, 0.00f, 0.14f }, // Woven Cotton
        { 0.94f, 0.00f, 0.55f }, // Cut-Pile Wool
        { 0.18f, 0.00f, 0.24f }, // Glazed Ceramic
        { 0.24f, 1.00f, 0.10f }, // Metal Inlay
        { 0.78f, 0.00f, 0.36f }, // Embossed Rubber
    }};
    const std::size_t Idx = static_cast<std::size_t>(Preset);
    if (Idx < Presets.size())
    {
        if (auto* R = FindChannelEntry(*Sel, PatternStampSurfaceChannelKey::Roughness)) R->ScalarReading = Presets[Idx].R;
        if (auto* M = FindChannelEntry(*Sel, PatternStampSurfaceChannelKey::Metalness)) M->ScalarReading = Presets[Idx].M;
        if (auto* H = FindChannelEntry(*Sel, PatternStampSurfaceChannelKey::Height))    H->ScalarReading = Presets[Idx].H;
    }
    BumpRevision();
}

bool PatternStampWorkspaceSession::AddModifierToSelectedLayer(PatternStampModifierCategory Category) noexcept
{
    PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr)
    {
        return false;
    }
    PushUndoSnapshot();
    PatternStampModifierSpec Spec {};
    Spec.Category = Category;
    Sel->Modifiers.push_back(Spec);
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::RemoveModifierFromSelectedLayer(std::size_t ModifierIndex) noexcept
{
    PatternStampLayerSpec* Sel = InspectSelectedLayer();
    if (Sel == nullptr || ModifierIndex >= Sel->Modifiers.size())
    {
        return false;
    }
    PushUndoSnapshot();
    Sel->Modifiers.erase(Sel->Modifiers.begin() + static_cast<std::ptrdiff_t>(ModifierIndex));
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

void PatternStampWorkspaceSession::AssignResolution(uint32_t TargetResolutionPx) noexcept
{
    ResolutionPx = std::clamp(TargetResolutionPx, 64u, 4096u);
    StatusNotice = "Resolution set to " + std::to_string(ResolutionPx) + " x " + std::to_string(ResolutionPx) + "px";
    BumpRevision();
}

void PatternStampWorkspaceSession::PushUndoSnapshot() noexcept
{
    UndoSnapshots.push_back(Document);
    if (UndoSnapshots.size() > MaxUndoDepth)
    {
        UndoSnapshots.erase(UndoSnapshots.begin());
    }
    RedoSnapshots.clear();
}

bool PatternStampWorkspaceSession::ApplyUndo() noexcept
{
    if (UndoSnapshots.empty())
    {
        return false;
    }
    RedoSnapshots.push_back(Document);
    Document = std::move(UndoSnapshots.back());
    UndoSnapshots.pop_back();
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::ApplyRedo() noexcept
{
    if (RedoSnapshots.empty())
    {
        return false;
    }
    UndoSnapshots.push_back(Document);
    Document = std::move(RedoSnapshots.back());
    RedoSnapshots.pop_back();
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

void PatternStampWorkspaceSession::InitializeDefaultPointFieldPins(PatternStampLayerSpec& Layer) noexcept
{
    bool AnyActive = false;
    for (const auto& P : Layer.PointFieldPins)
    {
        if (P.Active)
        {
            AnyActive = true;
            break;
        }
    }
    if (AnyActive)
    {
        return;
    }
    Layer.PointFieldPins[0] = { 0.28f, 0.28f, 1.2f, Layer.FillTint, true };
    Layer.PointFieldPins[1] = { 0.72f, 0.28f, 1.0f, Layer.FillTintB, true };
    Layer.PointFieldPins[2] = { 0.72f, 0.72f, 1.1f, ColorRgba{ 232u, 194u, 122u, 255u }, true };
    Layer.PointFieldPins[3] = { 0.28f, 0.72f, 1.0f, ColorRgba{ 44u, 58u, 74u, 255u }, true };
}

void PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(PatternStampLayerSpec& Layer) noexcept
{
    InitializeDefaultPointFieldPins(Layer);
    if (!Layer.CubicSubpaths.empty() || Layer.Category == PatternStampLayerCategory::Folder ||
        Layer.Category == PatternStampLayerCategory::GradientWash)
    {
        return;
    }

    const float HalfW = std::max(4.0f, Layer.WidthPx * 0.5f);
    const float HalfH = std::max(4.0f, Layer.HeightPx * 0.5f);
    const float Smooth = std::clamp(Layer.CurveSmoothness, 0.0f, 1.0f);

    GeometricRaster::VectorCubicSubpath MainSubpath {};
    MainSubpath.ClosedLoop = Layer.ClosedCurveLoop;

    auto AppendSharpPoly = [&](const std::vector<GeometricRaster::VectorVertex2D>& Pts, bool Closed)
    {
        MainSubpath.ClosedLoop = Closed;
        MainSubpath.Knots.reserve(Pts.size());
        for (const auto& Pt : Pts)
        {
            MainSubpath.Knots.push_back({ Pt.X, Pt.Y, Pt.X, Pt.Y, Pt.X, Pt.Y, false });
        }
    };

    switch (Layer.MotifShape)
    {
        case PatternStampMotifShape::Circle:
        case PatternStampMotifShape::Ring:
        {
            const float Kx = HalfW * KappaBezierCircle;
            const float Ky = HalfH * KappaBezierCircle;
            MainSubpath.ClosedLoop = true;
            MainSubpath.Knots = {
                {  0.0f,  -HalfH, -Kx,    -HalfH,  Kx,    -HalfH, true },
                {  HalfW,  0.0f,   HalfW, -Ky,     HalfW,  Ky,    true },
                {  0.0f,   HalfH,  Kx,     HalfH, -Kx,     HalfH, true },
                { -HalfW,  0.0f,  -HalfW,  Ky,    -HalfW, -Ky,    true },
            };
            Layer.CubicSubpaths.push_back(std::move(MainSubpath));
            if (Layer.MotifShape == PatternStampMotifShape::Ring)
            {
                const float InnerScale = 0.58f;
                const float Iw = HalfW * InnerScale;
                const float Ih = HalfH * InnerScale;
                const float Ikx = Iw * KappaBezierCircle;
                const float Iky = Ih * KappaBezierCircle;
                GeometricRaster::VectorCubicSubpath Hole {};
                Hole.ClosedLoop = true;
                Hole.Knots = {
                    {  0.0f, -Ih,  Ikx, -Ih, -Ikx, -Ih, true },
                    { -Iw,    0.0f, -Iw, -Iky, -Iw,  Iky, true },
                    {  0.0f,  Ih,  -Ikx,  Ih,  Ikx,  Ih, true },
                    {  Iw,    0.0f,  Iw,  Iky,  Iw, -Iky, true },
                };
                Layer.CubicSubpaths.push_back(std::move(Hole));
                Layer.EvenOddFillRule = true;
            }
            return;
        }
        case PatternStampMotifShape::RoundedBox:
        {
            const float Rad = std::min(HalfW, HalfH) * (0.18f + 0.30f * Smooth);
            const float C = Rad * KappaBezierCircle;
            MainSubpath.ClosedLoop = true;
            MainSubpath.Knots = {
                { -HalfW + Rad, -HalfH, -HalfW + Rad - C, -HalfH, HalfW - Rad, -HalfH, false },
                {  HalfW - Rad, -HalfH,  HalfW - Rad, -HalfH, HalfW - Rad + C, -HalfH, true },
                {  HalfW, -HalfH + Rad,  HalfW, -HalfH + Rad - C, HalfW, HalfH - Rad, false },
                {  HalfW,  HalfH - Rad,  HalfW,  HalfH - Rad, HalfW, HalfH - Rad + C, true },
                {  HalfW - Rad,  HalfH,  HalfW - Rad + C, HalfH, -HalfW + Rad, HalfH, false },
                { -HalfW + Rad,  HalfH, -HalfW + Rad, HalfH, -HalfW + Rad - C, HalfH, true },
                { -HalfW,  HalfH - Rad, -HalfW,  HalfH - Rad + C, -HalfW, -HalfH + Rad, false },
                { -HalfW, -HalfH + Rad, -HalfW, -HalfH + Rad, -HalfW, -HalfH + Rad - C, true },
            };
            Layer.CubicSubpaths.push_back(std::move(MainSubpath));
            return;
        }
        case PatternStampMotifShape::Diamond:
        {
            AppendSharpPoly({ { 0.0f, -HalfH }, { HalfW, 0.0f }, { 0.0f, HalfH }, { -HalfW, 0.0f } }, true);
            break;
        }
        case PatternStampMotifShape::Hexagon:
        {
            std::vector<GeometricRaster::VectorVertex2D> Pts;
            for (int I = 0; I < 6; ++I)
            {
                const float A = TwoPiScalar * (static_cast<float>(I) / 6.0f) - 1.5707963f;
                Pts.push_back({ std::cos(A) * HalfW, std::sin(A) * HalfH });
            }
            AppendSharpPoly(Pts, true);
            break;
        }
        case PatternStampMotifShape::Triangle:
        {
            AppendSharpPoly({ { 0.0f, -HalfH }, { HalfW * 0.866f, HalfH * 0.75f }, { -HalfW * 0.866f, HalfH * 0.75f } }, true);
            break;
        }
        case PatternStampMotifShape::Star:
        {
            std::vector<GeometricRaster::VectorVertex2D> Pts;
            for (int I = 0; I < 10; ++I)
            {
                const float A = TwoPiScalar * (static_cast<float>(I) / 10.0f) - 1.5707963f;
                const float S = ((I & 1) == 0) ? 1.0f : 0.44f;
                Pts.push_back({ std::cos(A) * HalfW * S, std::sin(A) * HalfH * S });
            }
            AppendSharpPoly(Pts, true);
            break;
        }
        case PatternStampMotifShape::Cross:
        {
            const float ArmW = HalfW * 0.34f;
            const float ArmH = HalfH * 0.34f;
            AppendSharpPoly({
                { -ArmW, -HalfH }, {  ArmW, -HalfH }, {  ArmW, -ArmH }, {  HalfW, -ArmH },
                {  HalfW,  ArmH }, {  ArmW,  ArmH }, {  ArmW,  HalfH }, { -ArmW,  HalfH },
                { -ArmW,  ArmH }, { -HalfW,  ArmH }, { -HalfW, -ArmH }, { -ArmW, -ArmH },
            }, true);
            break;
        }
        case PatternStampMotifShape::Chevron:
        {
            const float Thick = HalfH * 0.42f;
            AppendSharpPoly({
                { -HalfW,  HalfH * 0.2f }, { 0.0f, -HalfH }, { HalfW, HalfH * 0.2f },
                {  HalfW,  HalfH * 0.2f + Thick }, { 0.0f, -HalfH + Thick }, { -HalfW, HalfH * 0.2f + Thick },
            }, true);
            break;
        }
        case PatternStampMotifShape::StripeBar:
        {
            const float BarH = HalfH * 0.28f;
            AppendSharpPoly({ { -HalfW, -BarH }, { HalfW, -BarH }, { HalfW, BarH }, { -HalfW, BarH } }, true);
            break;
        }
        case PatternStampMotifShape::Arc:
        {
            std::vector<GeometricRaster::VectorVertex2D> Pts;
            for (int I = 0; I <= 14; ++I)
            {
                const float A = 3.14159265f * (static_cast<float>(I) / 14.0f) - 3.14159265f;
                Pts.push_back({ std::cos(A) * HalfW, std::sin(A) * HalfH });
            }
            for (int I = 14; I >= 0; --I)
            {
                const float A = 3.14159265f * (static_cast<float>(I) / 14.0f) - 3.14159265f;
                Pts.push_back({ std::cos(A) * HalfW * 0.62f, std::sin(A) * HalfH * 0.62f });
            }
            AppendSharpPoly(Pts, true);
            break;
        }
        case PatternStampMotifShape::Teardrop:
        {
            MainSubpath.ClosedLoop = true;
            MainSubpath.Knots = {
                {  0.0f,        -HalfH,         -HalfW * 0.12f, -HalfH * 0.55f,  HalfW * 0.12f, -HalfH * 0.55f, false },
                {  HalfW * 0.72f, HalfH * 0.25f, HalfW * 0.72f, -HalfH * 0.10f,  HalfW * 0.72f,  HalfH * 0.70f, true },
                {  0.0f,         HalfH,          HalfW * 0.42f,  HalfH,         -HalfW * 0.42f,  HalfH,         true },
                { -HalfW * 0.72f, HalfH * 0.25f,-HalfW * 0.72f,  HalfH * 0.70f, -HalfW * 0.72f, -HalfH * 0.10f, true },
            };
            Layer.CubicSubpaths.push_back(std::move(MainSubpath));
            return;
        }
        case PatternStampMotifShape::Leaf:
        {
            MainSubpath.ClosedLoop = true;
            MainSubpath.Knots = {
                { 0.0f, -HalfH, -HalfW * 0.95f, -HalfH * 0.20f,  HalfW * 0.95f, -HalfH * 0.20f, false },
                { 0.0f,  HalfH,  HalfW * 0.95f,  HalfH * 0.35f, -HalfW * 0.95f,  HalfH * 0.35f, false },
            };
            Layer.CubicSubpaths.push_back(std::move(MainSubpath));
            return;
        }
        case PatternStampMotifShape::FivePetalRosette:
        {
            std::vector<GeometricRaster::VectorVertex2D> Pts;
            for (int I = 0; I < 40; ++I)
            {
                const float A = TwoPiScalar * (static_cast<float>(I) / 40.0f);
                const float R = 0.52f + 0.48f * std::fabs(std::cos(2.5f * A));
                Pts.push_back({ std::cos(A) * HalfW * R, std::sin(A) * HalfH * R });
            }
            AppendSharpPoly(Pts, true);
            break;
        }
        case PatternStampMotifShape::CogGear:
        {
            std::vector<GeometricRaster::VectorVertex2D> Pts;
            const int Teeth = 10;
            for (int I = 0; I < Teeth * 4; ++I)
            {
                const float A = TwoPiScalar * (static_cast<float>(I) / static_cast<float>(Teeth * 4));
                const float R = ((I % 4) < 2) ? 1.0f : 0.72f;
                Pts.push_back({ std::cos(A) * HalfW * R, std::sin(A) * HalfH * R });
            }
            AppendSharpPoly(Pts, true);
            break;
        }
        case PatternStampMotifShape::LineStroke:
        {
            AppendSharpPoly({ { -HalfW, 0.0f }, { HalfW, 0.0f } }, false);
            break;
        }
        case PatternStampMotifShape::Square:
        case PatternStampMotifShape::VectorPath:
        default:
        {
            AppendSharpPoly({ { -HalfW, -HalfH }, { HalfW, -HalfH }, { HalfW, HalfH }, { -HalfW, HalfH } }, true);
            break;
        }
    }

    Layer.CubicSubpaths.push_back(std::move(MainSubpath));
}

bool PatternStampWorkspaceSession::ImportSvgIntoSession(
    std::string_view SvgSourceOrPath,
    bool             SourceIsFilePath,
    float            DropDocX,
    float            DropDocY,
    std::string_view DisplayTitle,
    uint32_t         TargetFolderOrLayerId,
    std::string*     OptionalDiagnostic)
{
    std::string NormalizedSvgStorage;
    std::string_view EffectiveSource = SvgSourceOrPath;
    if (!SourceIsFilePath && !SvgSourceOrPath.empty())
    {
        std::string_view Trimmed = SvgSourceOrPath;
        while (!Trimmed.empty() && (Trimmed.front() == ' ' || Trimmed.front() == '\t' || Trimmed.front() == '\n' || Trimmed.front() == '\r'))
        {
            Trimmed.remove_prefix(1u);
        }
        if (Trimmed.find("<svg") == std::string_view::npos)
        {
            if (Trimmed.find('<') != std::string_view::npos)
            {
                NormalizedSvgStorage =
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">" +
                    std::string(Trimmed) +
                    "</svg>";
            }
            else
            {
                NormalizedSvgStorage =
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\"><path d=\"" +
                    std::string(Trimmed) +
                    "\" fill=\"#EFF4CF\"/></svg>";
            }
            EffectiveSource = NormalizedSvgStorage;
        }
    }

    GeometricRaster::ThorVGVectorFrontend Frontend;
    std::vector<GeometricRaster::ExtractedSvgLayer> Extracted;
    if (!Frontend.ExtractSvgToEditableLayers(EffectiveSource, SourceIsFilePath, 220.0f, DropDocX, DropDocY, Extracted, OptionalDiagnostic))
    {
        return false;
    }
    if (Document.Layers.size() >= MaxLayerLimit)
    {
        return false;
    }

    PushUndoSnapshot();

    uint32_t EnclosingFolderId = 0u;
    if (TargetFolderOrLayerId != 0u)
    {
        if (const auto* TargetL = FindLayerById(TargetFolderOrLayerId))
        {
            EnclosingFolderId = (TargetL->Category == PatternStampLayerCategory::Folder) ? TargetL->Identity : TargetL->EnclosingFolderId;
        }
    }

    if (Extracted.size() > 1u && EnclosingFolderId == 0u && Document.Layers.size() + Extracted.size() + 1u <= MaxLayerLimit)
    {
        PatternStampLayerSpec Folder {};
        Folder.Identity    = Document.NextIdentitySeed++;
        Folder.Name        = DisplayTitle.empty() ? std::string("Imported SVG Group") : std::string(DisplayTitle);
        Folder.Category    = PatternStampLayerCategory::Folder;
        Folder.Visible     = true;
        Folder.FillEnabled = false;
        EnclosingFolderId  = Folder.Identity;
        Document.Layers.push_back(std::move(Folder));
    }

    uint32_t LastCreatedId = 0u;
    const std::size_t RemainingSlots = MaxLayerLimit - Document.Layers.size();
    const std::size_t ImportCount    = std::min(Extracted.size(), RemainingSlots);

    for (std::size_t I = 0u; I < ImportCount; ++I)
    {
        const auto& Src = Extracted[I];
        PatternStampLayerSpec Next {};
        Next.Identity          = Document.NextIdentitySeed++;
        Next.EnclosingFolderId = EnclosingFolderId;
        Next.Name              = (Extracted.size() == 1u && !DisplayTitle.empty()) ? std::string(DisplayTitle) : Src.Title;
        Next.Category          = PatternStampLayerCategory::Motif;
        Next.MotifShape        = PatternStampMotifShape::VectorPath;
        Next.Visible           = true;
        Next.BlendMode         = Src.BlendOrdinal;
        Next.Opacity           = std::clamp(Src.Opacity, 0.0f, 1.0f);
        Next.EvenOddFillRule   = Src.EvenOddFillRule;
        Next.FillEnabled       = Src.FillEnabled;
        Next.FillSource        = (Src.GradientPreset == GeometricRaster::VectorGradientPreset::Linear)
            ? PatternStampFillSourceMode::LinearGradient
            : (Src.GradientPreset == GeometricRaster::VectorGradientPreset::Radial
                ? PatternStampFillSourceMode::RadialGradient
                : PatternStampFillSourceMode::Solid);
        Next.FillTint          = Src.FillTint;
        Next.FillTintB         = Src.FillTintB;
        Next.GradientAngleDeg  = Src.GradientAngleDeg;
        for (const auto& Stop : Src.Stops)
        {
            Next.FillStops.push_back({ Stop.Tint, Stop.Offset });
        }
        Next.StrokeWidthPx     = Src.StrokeEnabled ? Src.StrokeWidthPx : 0.0f;
        Next.StrokeTint        = Src.StrokeTint;
        Next.CenterX           = Src.CenterX;
        Next.CenterY           = Src.CenterY;
        Next.WidthPx           = std::max(8.0f, Src.WidthPx);
        Next.HeightPx          = std::max(8.0f, Src.HeightPx);

        // Convert extracted subpaths into layer-local centered coordinates so Gizmo & modifiers work identically
        Next.CubicSubpaths = Src.Subpaths;
        for (auto& Sub : Next.CubicSubpaths)
        {
            for (auto& Knot : Sub.Knots)
            {
                Knot.AnchorX    -= Next.CenterX;
                Knot.AnchorY    -= Next.CenterY;
                Knot.InHandleX  -= Next.CenterX;
                Knot.InHandleY  -= Next.CenterY;
                Knot.OutHandleX -= Next.CenterX;
                Knot.OutHandleY -= Next.CenterY;
            }
        }
        if (!SourceIsFilePath && I == 0u)
        {
            Next.SvgPathPayload = std::string(SvgSourceOrPath);
        }
        Next.Channels = {
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  Next.FillTint),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.34f, { 158u, 162u, 170u, 255u }),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Metalness, 0.05f, { 158u, 162u, 170u, 255u }),
            ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.25f, { 158u, 162u, 170u, 255u }),
        };
        InitializeDefaultPointFieldPins(Next);
        LastCreatedId = Next.Identity;
        Document.Layers.push_back(std::move(Next));
    }

    if (LastCreatedId != 0u)
    {
        Document.SelectedLayerId = LastCreatedId;
        StatusNotice = "Imported SVG vector layer" + std::string(ImportCount > 1u ? "s (" + std::to_string(ImportCount) + ")" : "");
        BumpRevision();
        RecomputeSummaryMetrics();
        return true;
    }
    return false;
}

bool PatternStampWorkspaceSession::ApplyFontDrop(
    const std::filesystem::path& FontPath,
    std::string_view             TypefaceTitle,
    float                        DropDocX,
    float                        DropDocY,
    uint32_t                     TargetLayerId)
{
    const std::filesystem::path Resolved = ResolveWorkspaceFontPath(FontPath.string());
    if (!GeometricRaster::ThorVGVectorFrontend::RegisterTypefaceFile(Resolved))
    {
        return false;
    }

    PushUndoSnapshot();
    const std::string ResolvedTypefaceTitle = TypefaceTitle.empty() ? Resolved.stem().string() : std::string(TypefaceTitle);

    if (TargetLayerId != 0u)
    {
        if (PatternStampLayerSpec* Target = FindLayerById(TargetLayerId))
        {
            if (Target->Category == PatternStampLayerCategory::Typographic)
            {
                Target->FontFilePath = Resolved.string();
                Target->TypefaceTitle = ResolvedTypefaceTitle;
                StatusNotice          = "Applied font " + ResolvedTypefaceTitle + " to " + Target->Name;
                BumpRevision();
                RecomputeSummaryMetrics();
                return true;
            }
        }
    }

    if (Document.Layers.size() >= MaxLayerLimit)
    {
        return false;
    }

    PatternStampLayerSpec Next {};
    Next.Identity      = Document.NextIdentitySeed++;
    Next.Name          = ResolvedTypefaceTitle + " Type";
    Next.Category      = PatternStampLayerCategory::Typographic;
    Next.Visible       = true;
    Next.Opacity       = 1.0f;
    Next.TextContent   = "FRONTIER";
    Next.FontSizePx    = 64.0f;
    Next.FontFilePath = Resolved.string();
    Next.TypefaceTitle = ResolvedTypefaceTitle;
    Next.FillEnabled     = true;
    Next.FillTint        = { 239u, 244u, 207u, 255u };
    Next.FillTintB       = { 90u, 118u, 146u, 255u };
    Next.CenterX         = std::clamp(DropDocX, 24.0f, 488.0f);
    Next.CenterY         = std::clamp(DropDocY, 24.0f, 488.0f);
    Next.WidthPx         = 220.0f;
    Next.HeightPx        = 76.0f;
    Next.Channels = {
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Albedo,    1.0f,  Next.FillTint),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Roughness, 0.34f, { 158u, 162u, 170u, 255u }),
        ConstructChannelEntry(PatternStampSurfaceChannelKey::Height,    0.30f, { 158u, 162u, 170u, 255u }),
    };
    InitializeDefaultPointFieldPins(Next);

    Document.Layers.push_back(std::move(Next));
    Document.SelectedLayerId = Document.Layers.back().Identity;
    StatusNotice = "Created typographic layer with " + ResolvedTypefaceTitle;
    BumpRevision();
    RecomputeSummaryMetrics();
    return true;
}

bool PatternStampWorkspaceSession::ApplyColourDrop(
    ColorRgba Tint,
    uint32_t  TargetLayerId,
    int32_t   PinIndex,
    bool      StrokeTarget)
{
    PushUndoSnapshot();
    if (TargetLayerId == 0u)
    {
        Document.GroundEnabled = true;
        Document.GroundTintA   = Tint;
        StatusNotice           = "Applied dropped colour to ground fill";
        BumpRevision();
        return true;
    }

    PatternStampLayerSpec* Target = FindLayerById(TargetLayerId);
    if (Target == nullptr)
    {
        return false;
    }
    Document.SelectedLayerId = Target->Identity;
    if (PinIndex >= 0 && static_cast<std::size_t>(PinIndex) < Target->PointFieldPins.size())
    {
        Target->FillSource = PatternStampFillSourceMode::PointField;
        InitializeDefaultPointFieldPins(*Target);
        Target->PointFieldPins[static_cast<std::size_t>(PinIndex)].Active = true;
        Target->PointFieldPins[static_cast<std::size_t>(PinIndex)].Tint   = Tint;
        StatusNotice = "Applied colour to gradient pin #" + std::to_string(PinIndex + 1);
    }
    else if (StrokeTarget)
    {
        Target->StrokeTint = Tint;
        if (Target->StrokeWidthPx <= 0.1f)
        {
            Target->StrokeWidthPx = 4.0f;
        }
        StatusNotice = "Applied colour to " + Target->Name + " stroke";
    }
    else
    {
        Target->FillEnabled = true;
        Target->FillTint    = Tint;
        if (auto* Albedo = FindChannelEntry(*Target, PatternStampSurfaceChannelKey::Albedo))
        {
            Albedo->Tint = Tint;
        }
        StatusNotice = "Applied colour to " + Target->Name;
    }
    BumpRevision();
    return true;
}

std::string PatternStampWorkspaceSession::ExportDocumentToSvgString() const
{
    std::ostringstream Out;
    Out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\" width=\""
        << ResolutionPx << "\" height=\"" << ResolutionPx << "\">\n";
    if (Document.GroundEnabled)
    {
        Out << "  <rect x=\"0\" y=\"0\" width=\"512\" height=\"512\" fill=\""
            << FormatSvgHexRgb(Document.GroundTintA) << "\"/>\n";
    }
    for (const auto& L : Document.Layers)
    {
        if (!QueryEffectiveVisible(L.Identity) || L.Category == PatternStampLayerCategory::Folder)
        {
            continue;
        }
        PatternStampLayerSpec Copy = L;
        SeedMotifCubicSubpathsIfEmpty(Copy);
        if (Copy.CubicSubpaths.empty())
        {
            continue;
        }
        Out << "  <path d=\"";
        for (const auto& Sub : Copy.CubicSubpaths)
        {
            if (Sub.Knots.empty()) continue;
            Out << "M " << (Sub.Knots[0].AnchorX + Copy.CenterX) << " " << (Sub.Knots[0].AnchorY + Copy.CenterY) << " ";
            const std::size_t SegCount = Sub.ClosedLoop ? Sub.Knots.size() : (Sub.Knots.size() - 1u);
            for (std::size_t I = 0u; I < SegCount; ++I)
            {
                const auto& K0 = Sub.Knots[I];
                const auto& K1 = Sub.Knots[(I + 1u) % Sub.Knots.size()];
                Out << "C " << (K0.OutHandleX + Copy.CenterX) << " " << (K0.OutHandleY + Copy.CenterY) << " "
                    << (K1.InHandleX + Copy.CenterX) << " " << (K1.InHandleY + Copy.CenterY) << " "
                    << (K1.AnchorX + Copy.CenterX) << " " << (K1.AnchorY + Copy.CenterY) << " ";
            }
            if (Sub.ClosedLoop) Out << "Z ";
        }
        Out << "\" fill=\"" << (Copy.FillEnabled ? FormatSvgHexRgb(Copy.FillTint) : std::string("none")) << "\"";
        if (Copy.EvenOddFillRule) Out << " fill-rule=\"evenodd\"";
        if (Copy.StrokeWidthPx > 0.1f)
        {
            Out << " stroke=\"" << FormatSvgHexRgb(Copy.StrokeTint) << "\" stroke-width=\"" << Copy.StrokeWidthPx << "\"";
        }
        if (Copy.Opacity < 0.999f)
        {
            Out << " opacity=\"" << Copy.Opacity << "\"";
        }
        Out << "/>\n";
    }
    Out << "</svg>\n";
    return Out.str();
}

Codecs::VectorSpaceDocumentModel PatternStampWorkspaceSession::BuildVectorSpaceModel() const
{
    Codecs::VectorSpaceDocumentModel Model {};
    Model.DocumentTitle            = Document.DocumentName.empty() ? std::string("UntitledVector") : Document.DocumentName;
    Model.DefaultStrokeWidthMm     = 0.35;
    Model.ViewBoxAspectRatio       = 1.0;
    Model.WorkspaceDocumentPresent = true;
    Model.WorkspaceProfile         = (Mode == PatternStampWorkspaceMode::Stamp) ? std::string("Stamp") : std::string("Pattern");
    Model.ResolutionPx             = ResolutionPx;
    Model.GroundEnabled            = Document.GroundEnabled;
    Model.GroundFillSource         = static_cast<uint8_t>(Document.GroundFillSource);
    Model.GroundTintARgba          = { Document.GroundTintA.Red, Document.GroundTintA.Green, Document.GroundTintA.Blue, Document.GroundTintA.Alpha };
    Model.GroundTintBRgba          = { Document.GroundTintB.Red, Document.GroundTintB.Green, Document.GroundTintB.Blue, Document.GroundTintB.Alpha };
    Model.GroundAngleDeg           = Document.GroundAngleDeg;
    Model.GhostTileBordersVisible  = Document.ShowGhostTileBorders;
    Model.EmbeddedSvgXml           = ExportDocumentToSvgString();

    for (const PatternStampLayerSpec& Src : Document.Layers)
    {
        Codecs::VectorWorkspaceLayerEntry Dst {};
        Dst.Identity          = Src.Identity;
        Dst.EnclosingFolderId = Src.EnclosingFolderId;
        Dst.Title             = Src.Name;
        Dst.Category          = static_cast<uint8_t>(Src.Category);
        Dst.Visible           = Src.Visible;
        Dst.Locked            = Src.Locked;
        Dst.ExpandedDrawer    = Src.ExpandedInStack;
        Dst.CollapsedFolder   = Src.CollapsedFolder;
        Dst.BlendMode         = Src.BlendMode;
        Dst.Opacity           = Src.Opacity;
        Dst.MotifShape        = static_cast<uint8_t>(Src.MotifShape);
        Dst.ClosedCurveLoop   = Src.ClosedCurveLoop;
        Dst.CurveSmoothness   = Src.CurveSmoothness;
        Dst.TextContent       = Src.TextContent;
        Dst.FontSizePx        = Src.FontSizePx;
        Dst.FontFilePath     = Src.FontFilePath;
        Dst.TypefaceTitle   = Src.TypefaceTitle;
        Dst.LetterSpacingEm   = Src.LetterSpacingEm;
        Dst.SvgPathSnippet    = Src.SvgPathPayload;
        Dst.FillEnabled       = Src.FillEnabled;
        Dst.FillSource        = static_cast<uint8_t>(Src.FillSource);
        Dst.FillTintRgba      = { Src.FillTint.Red, Src.FillTint.Green, Src.FillTint.Blue, Src.FillTint.Alpha };
        Dst.FillTintBRgba     = { Src.FillTintB.Red, Src.FillTintB.Green, Src.FillTintB.Blue, Src.FillTintB.Alpha };
        Dst.GradientAngleDeg  = Src.GradientAngleDeg;
        for (const auto& Stop : Src.FillStops)
        {
            Dst.FillStops.push_back({ { Stop.Tint.Red, Stop.Tint.Green, Stop.Tint.Blue, Stop.Tint.Alpha }, Stop.Offset });
        }
        Dst.StrokeWidthPx   = Src.StrokeWidthPx;
        Dst.StrokeSource    = static_cast<uint8_t>(Src.StrokeSource);
        Dst.StrokeTintRgba  = { Src.StrokeTint.Red, Src.StrokeTint.Green, Src.StrokeTint.Blue, Src.StrokeTint.Alpha };
        Dst.StrokeCap       = static_cast<uint8_t>(Src.StrokeCap);
        Dst.StrokeJoin      = static_cast<uint8_t>(Src.StrokeJoin);
        Dst.EvenOddFillRule = Src.EvenOddFillRule;
        Dst.CenterX         = Src.CenterX;
        Dst.CenterY         = Src.CenterY;
        Dst.WidthPx         = Src.WidthPx;
        Dst.HeightPx        = Src.HeightPx;
        Dst.RotationDeg     = Src.RotationDeg;
        Dst.FinishPreset    = static_cast<uint8_t>(Src.FinishPreset);

        for (std::size_t P = 0u; P < 12u; ++P)
        {
            const auto& Pin = Src.PointFieldPins[P];
            Dst.PointPins[P] = {
                Pin.Active,
                Pin.NormX,
                Pin.NormY,
                { Pin.Tint.Red, Pin.Tint.Green, Pin.Tint.Blue, Pin.Tint.Alpha },
            };
        }

        for (const auto& Sub : Src.CubicSubpaths)
        {
            Codecs::VectorWorkspaceSubpathEntry SubOut {};
            SubOut.ClosedLoop = Sub.ClosedLoop;
            for (const auto& K : Sub.Knots)
            {
                SubOut.Knots.push_back({ K.AnchorX, K.AnchorY, K.InHandleX, K.InHandleY, K.OutHandleX, K.OutHandleY, K.Smooth });
                Model.ControlPointKnots.push_back(static_cast<double>(Src.CenterX + K.AnchorX) / 512.0);
                Model.ControlPointKnots.push_back(static_cast<double>(Src.CenterY + K.AnchorY) / 512.0);
                Model.ControlPointKnots.push_back(static_cast<double>(K.OutHandleX - K.AnchorX) / 512.0);
                Model.ControlPointKnots.push_back(static_cast<double>(K.OutHandleY - K.AnchorY) / 512.0);
            }
            if (!Sub.Knots.empty())
            {
                Model.Curves.push_back({ Src.Name, Sub.ClosedLoop, static_cast<uint32_t>(Sub.Knots.size()) });
            }
            Dst.Subpaths.push_back(std::move(SubOut));
        }

        for (const auto& Ch : Src.Channels)
        {
            Dst.Channels.push_back({
                static_cast<uint32_t>(Ch.Key),
                Ch.ScalarReading,
                { Ch.Tint.Red, Ch.Tint.Green, Ch.Tint.Blue, Ch.Tint.Alpha },
            });
        }

        Dst.Mask = {
            Src.Mask.Enabled,
            Src.Mask.Invert,
            static_cast<uint8_t>(Src.Mask.Mode),
            Src.Mask.Frequency,
            Src.Mask.Threshold,
            Src.Mask.Softness,
            Src.Mask.Seed,
        };

        for (const auto& M : Src.Modifiers)
        {
            Dst.Modifiers.push_back({
                static_cast<uint8_t>(M.Category),
                M.Enabled,
                M.NoiseAlgorithm,
                M.ModulateTarget,
                M.MinScale,
                M.MaxScale,
                M.Frequency,
                { M.TargetTintB.Red, M.TargetTintB.Green, M.TargetTintB.Blue, M.TargetTintB.Alpha },
                M.GridLayout,
                M.Columns,
                M.Rows,
                M.RadialCount,
                M.RadiusPx,
                M.Rings,
                M.MirrorAxis,
                M.EchoCopies,
                M.InsetStepPx,
                { M.AltTint.Red, M.AltTint.Green, M.AltTint.Blue, M.AltTint.Alpha },
                M.PosJitterPx,
                M.RotJitterDeg,
                M.Dropout,
                M.WarpMode,
                M.WarpAmplitude,
                M.ChipWearAmount,
            });
        }

        Model.WorkspaceLayers.push_back(std::move(Dst));
    }
    return Model;
}

bool PatternStampWorkspaceSession::LoadFromVectorSpaceModel(const Codecs::VectorSpaceDocumentModel& Model)
{
    if (!Model.WorkspaceDocumentPresent && Model.WorkspaceLayers.empty())
    {
        if (!Model.EmbeddedSvgXml.empty())
        {
            return ImportSvgIntoSession(Model.EmbeddedSvgXml, false, 256.0f, 256.0f, Model.DocumentTitle);
        }
        return false;
    }

    PushUndoSnapshot();
    Mode = (Model.WorkspaceProfile == "Stamp")
        ? PatternStampWorkspaceMode::Stamp
        : PatternStampWorkspaceMode::Pattern;
    ResolutionPx                    = std::clamp(Model.ResolutionPx, 64u, 4096u);
    Document.DocumentName           = Model.DocumentTitle.empty() ? std::string("Imported Vector") : Model.DocumentTitle;
    Document.GroundEnabled          = Model.GroundEnabled;
    Document.GroundFillSource       = static_cast<PatternStampFillSourceMode>(Model.GroundFillSource);
    Document.GroundTintA            = { Model.GroundTintARgba[0], Model.GroundTintARgba[1], Model.GroundTintARgba[2], Model.GroundTintARgba[3] };
    Document.GroundTintB            = { Model.GroundTintBRgba[0], Model.GroundTintBRgba[1], Model.GroundTintBRgba[2], Model.GroundTintBRgba[3] };
    Document.GroundAngleDeg         = Model.GroundAngleDeg;
    Document.ShowGhostTileBorders   = Model.GhostTileBordersVisible;
    Document.Layers.clear();

    uint32_t MaxId = 1u;
    for (const auto& Src : Model.WorkspaceLayers)
    {
        if (Document.Layers.size() >= MaxLayerLimit)
        {
            break;
        }
        PatternStampLayerSpec Dst {};
        Dst.Identity          = std::max(1u, Src.Identity);
        MaxId                 = std::max(MaxId, Dst.Identity);
        Dst.EnclosingFolderId = Src.EnclosingFolderId;
        Dst.Name              = Src.Title;
        Dst.Category          = static_cast<PatternStampLayerCategory>(Src.Category);
        Dst.Visible           = Src.Visible;
        Dst.Locked            = Src.Locked;
        Dst.ExpandedInStack   = Src.ExpandedDrawer;
        Dst.CollapsedFolder   = Src.CollapsedFolder;
        Dst.BlendMode         = Src.BlendMode;
        Dst.Opacity           = Src.Opacity;
        Dst.MotifShape        = static_cast<PatternStampMotifShape>(Src.MotifShape);
        Dst.ClosedCurveLoop   = Src.ClosedCurveLoop;
        Dst.CurveSmoothness   = Src.CurveSmoothness;
        Dst.TextContent       = Src.TextContent;
        Dst.FontSizePx        = Src.FontSizePx;
        Dst.FontFilePath     = Src.FontFilePath;
        Dst.TypefaceTitle   = Src.TypefaceTitle;
        Dst.LetterSpacingEm   = Src.LetterSpacingEm;
        Dst.SvgPathPayload    = Src.SvgPathSnippet;
        Dst.FillEnabled       = Src.FillEnabled;
        Dst.FillSource        = static_cast<PatternStampFillSourceMode>(Src.FillSource);
        Dst.FillTint          = { Src.FillTintRgba[0], Src.FillTintRgba[1], Src.FillTintRgba[2], Src.FillTintRgba[3] };
        Dst.FillTintB         = { Src.FillTintBRgba[0], Src.FillTintBRgba[1], Src.FillTintBRgba[2], Src.FillTintBRgba[3] };
        Dst.GradientAngleDeg  = Src.GradientAngleDeg;
        for (const auto& S : Src.FillStops)
        {
            Dst.FillStops.push_back({ { S.TintRgba[0], S.TintRgba[1], S.TintRgba[2], S.TintRgba[3] }, S.Position });
        }
        Dst.StrokeWidthPx   = Src.StrokeWidthPx;
        Dst.StrokeSource    = static_cast<PatternStampFillSourceMode>(Src.StrokeSource);
        Dst.StrokeTint      = { Src.StrokeTintRgba[0], Src.StrokeTintRgba[1], Src.StrokeTintRgba[2], Src.StrokeTintRgba[3] };
        Dst.StrokeCap       = static_cast<GeometricRaster::VectorStrokeCapStyle>(Src.StrokeCap);
        Dst.StrokeJoin      = static_cast<GeometricRaster::VectorStrokeJoinStyle>(Src.StrokeJoin);
        Dst.EvenOddFillRule = Src.EvenOddFillRule;
        Dst.CenterX         = Src.CenterX;
        Dst.CenterY         = Src.CenterY;
        Dst.WidthPx         = Src.WidthPx;
        Dst.HeightPx        = Src.HeightPx;
        Dst.RotationDeg     = Src.RotationDeg;
        Dst.FinishPreset    = static_cast<PatternStampFinishPreset>(Src.FinishPreset);

        for (std::size_t P = 0u; P < 12u; ++P)
        {
            const auto& Pin = Src.PointPins[P];
            Dst.PointFieldPins[P] = {
                Pin.NormX,
                Pin.NormY,
                1.0f,
                { Pin.TintRgba[0], Pin.TintRgba[1], Pin.TintRgba[2], Pin.TintRgba[3] },
                Pin.Active,
            };
        }
        for (const auto& Sub : Src.Subpaths)
        {
            GeometricRaster::VectorCubicSubpath SubOut {};
            SubOut.ClosedLoop = Sub.ClosedLoop;
            for (const auto& K : Sub.Knots)
            {
                SubOut.Knots.push_back({ K.AnchorX, K.AnchorY, K.InHandleX, K.InHandleY, K.OutHandleX, K.OutHandleY, K.Smooth });
            }
            Dst.CubicSubpaths.push_back(std::move(SubOut));
        }
        for (const auto& Ch : Src.Channels)
        {
            Dst.Channels.push_back({
                static_cast<PatternStampSurfaceChannelKey>(Ch.ChannelKey),
                Ch.ScalarWeight,
                { Ch.TintRgba[0], Ch.TintRgba[1], Ch.TintRgba[2], Ch.TintRgba[3] },
            });
        }
        Dst.Mask = {
            Src.Mask.Enabled,
            Src.Mask.Invert,
            static_cast<PatternStampMaskMode>(Src.Mask.Mode),
            Src.Mask.Frequency,
            Src.Mask.Threshold,
            Src.Mask.Softness,
            Src.Mask.Seed,
        };
        for (const auto& M : Src.Modifiers)
        {
            PatternStampModifierSpec ModOut {};
            ModOut.Category       = static_cast<PatternStampModifierCategory>(M.Category);
            ModOut.Enabled        = M.Enabled;
            ModOut.NoiseAlgorithm = M.NoiseAlgorithm;
            ModOut.ModulateTarget = M.ModulateTarget;
            ModOut.MinScale       = M.MinScale;
            ModOut.MaxScale       = M.MaxScale;
            ModOut.Frequency      = M.Frequency;
            ModOut.TargetTintB    = { M.TargetTintBRgba[0], M.TargetTintBRgba[1], M.TargetTintBRgba[2], M.TargetTintBRgba[3] };
            ModOut.GridLayout     = M.GridLayout;
            ModOut.Columns        = M.Columns;
            ModOut.Rows           = M.Rows;
            ModOut.RadialCount    = M.RadialCount;
            ModOut.RadiusPx       = M.RadiusPx;
            ModOut.Rings          = M.Rings;
            ModOut.MirrorAxis     = M.MirrorAxis;
            ModOut.EchoCopies     = M.EchoCopies;
            ModOut.InsetStepPx    = M.InsetStepPx;
            ModOut.AltTint        = { M.AltTintRgba[0], M.AltTintRgba[1], M.AltTintRgba[2], M.AltTintRgba[3] };
            ModOut.PosJitterPx    = M.PosJitterPx;
            ModOut.RotJitterDeg   = M.RotJitterDeg;
            ModOut.Dropout        = M.Dropout;
            ModOut.WarpMode       = M.WarpMode;
            ModOut.WarpAmplitude  = M.WarpAmplitude;
            ModOut.ChipWearAmount = M.ChipWearAmount;
            Dst.Modifiers.push_back(ModOut);
        }
        SeedMotifCubicSubpathsIfEmpty(Dst);
        Document.Layers.push_back(std::move(Dst));
    }

    Document.NextIdentitySeed = MaxId + 1u;
    Document.SelectedLayerId  = Document.Layers.empty() ? 0u : Document.Layers.back().Identity;
    StatusNotice = "Loaded .VectorSpace document: " + Document.DocumentName;
    BumpRevision();
    RecomputeSummaryMetrics();
    return !Document.Layers.empty();
}

void PatternStampWorkspaceSession::RecomputeSummaryMetrics() noexcept
{
    int32_t TotalInstances = 0;
    float   MinS           = 1.0f;
    float   MaxS           = 1.0f;
    bool    FoundNoise     = false;
    float   AreaSum        = 0.0f;

    for (const auto& L : Document.Layers)
    {
        if (!QueryEffectiveVisible(L.Identity) || L.Category == PatternStampLayerCategory::Folder)
        {
            continue;
        }
        int32_t LayerCopies = 1;
        for (const auto& M : L.Modifiers)
        {
            if (!M.Enabled)
            {
                continue;
            }
            if (M.Category == PatternStampModifierCategory::GridArray)
            {
                LayerCopies *= std::max(1, M.Columns) * std::max(1, M.Rows);
            }
            else if (M.Category == PatternStampModifierCategory::RadialArray)
            {
                LayerCopies *= std::max(1, M.RadialCount) * std::max(1, M.Rings);
            }
            else if (M.Category == PatternStampModifierCategory::NoiseScale)
            {
                MinS       = FoundNoise ? std::min(MinS, M.MinScale) : M.MinScale;
                MaxS       = FoundNoise ? std::max(MaxS, M.MaxScale) : M.MaxScale;
                FoundNoise = true;
            }
        }
        TotalInstances += LayerCopies;

        const float W = std::max(1.0f, L.WidthPx);
        const float H = std::max(1.0f, L.HeightPx);
        float SingleArea = W * H;
        if (L.MotifShape == PatternStampMotifShape::Circle || L.MotifShape == PatternStampMotifShape::Ring)
        {
            SingleArea *= 0.785398f;
        }
        else if (L.MotifShape == PatternStampMotifShape::Hexagon)
        {
            SingleArea *= 0.6495f;
        }
        AreaSum += SingleArea * static_cast<float>(LayerCopies);
    }

    InstancesEvaluated = std::max(0, TotalInstances);
    MinScaleReading    = MinS;
    MaxScaleReading    = MaxS;
    if (Document.GroundEnabled)
    {
        SurfaceFractionPct = 100;
    }
    else
    {
        const float TileArea = 512.0f * 512.0f;
        SurfaceFractionPct   = std::clamp(static_cast<int32_t>(std::lround((AreaSum / TileArea) * 100.0f)), 0, 100);
    }
}

} // namespace Frontier::Workspaces
