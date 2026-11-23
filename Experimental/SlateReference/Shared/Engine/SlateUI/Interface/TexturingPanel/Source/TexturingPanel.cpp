//============================================================================================================================================
//                                                           TEXTUREPAINTPANEL.CPP
//============================================================================================================================================
// 🧩 The editor's texture-texture layer stack — the LayerstackV1 reference's own
//    header, tools, rows, mask rows, folders and footer inside the workspace
//    leaf, with the selection-driven properties page behind the carousel.
//    See TexturingPanel.h for the flow: a layer row + Tab → channel
//    properties, a mask + Tab → the mask panel, a decal/pattern/generator +
//    Tab → its settings, a folder + Tab → the combined stack properties.
//    No history panel.

#include "SlateUI/Interface/TexturingPanel/Api/TexturingPanel.h"
#include "SlateUI/Interface/TreeMechanics/Api/TreeMechanics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Slate
{

namespace
{

constexpr double HoverOver = 120.0;   // [ms] - the reference's transition-colors duration

/// 🧩 Holds a coordinate between two bounds.
constexpr float Held(float Coordinate, float Minimum, float Maximum)
{
    return (Coordinate < Minimum) ? Minimum : (Coordinate > Maximum) ? Maximum : Coordinate;
}

/// 🧩 The same colour at a declared fraction of its own coverage.
constexpr ThemeToken Faded(ThemeToken Declared, float Fraction)
{
    const float Bounded = Held(Fraction, 0.0f, 1.0f);
    Declared.Opacity    = static_cast<std::uint8_t>(static_cast<float>(Declared.Opacity) * Bounded + 0.5f);
    return Declared;
}

/// 🧩 Whether one run holds another as a case-insensitive subsequence — the reference's own
///    `name.toLowerCase().includes(filterText.toLowerCase())`.
bool RunHolds(const char* Subject, const char* Sought)
{
    if (Sought == nullptr || Sought[0] == '\0')
        return true;

    if (Subject == nullptr)
        return false;

    const auto Lowered = [](char Letter) -> char
    {
        return (Letter >= 'A' && Letter <= 'Z') ? static_cast<char>(Letter - 'A' + 'a') : Letter;
    };

    for (std::uint32_t Departure = 0u; Subject[Departure] != '\0'; ++Departure)
    {
        std::uint32_t Advanced = 0u;

        while (Sought[Advanced] != '\0' &&
               Lowered(Subject[Departure + Advanced]) == Lowered(Sought[Advanced]))
        {
            ++Advanced;
        }

        if (Sought[Advanced] == '\0')
            return true;
    }

    return false;
}

/// 🧩 How many effect names a comma-separated run carries — the "n FX" chip's count.
std::uint32_t EffectCount(const char* Effects)
{
    if (Effects == nullptr || Effects[0] == '\0')
        return 0u;

    std::uint32_t Count = 1u;

    for (std::uint32_t Index = 0u; Effects[Index] != '\0'; ++Index)
    {
        if (Effects[Index] == ',')
            ++Count;
    }

    return Count;
}

/// 🧩 The reference's `COLORS` swatch run, for the layer menu's colour tags.
constexpr std::uint32_t SwatchColours[TexturingContext::TextureSwatchCount] =
{
    0xE5484Du, 0xF76B15u, 0xFFC53Du, 0x46A758u, 0x12A594u,
    0x8AB4D8u, 0x9B8CF0u, 0xE93D82u, 0x8B8D98u, 0xB0E64Cu
};

// 📐 The stack's filter categories — the editor's layer kinds, in the FacetPanel's option order.
const char* const StackFacetOptions[TexturingContext::TextureFacetCount] =
{
    "Brushed", "Fill", "Decal", "Pattern", "Generator", "Adjustment", "Filter", "Folder"
};

const ThemeToken StackFacetColours[TexturingContext::TextureFacetCount] =
{
    Covering(0xF97316u),   // [-] - Covering
    Covering(0x3B82F6u),   // [-] - Fill
    Covering(0xEF4444u),   // [-] - Decal
    Covering(0x10B981u),   // [-] - Pattern
    Covering(0x8B5CF6u),   // [-] - Generator
    Covering(0xEAB308u),   // [-] - Adjustment
    Covering(0x06B6D4u),   // [-] - Filter
    Covering(0x8A8A8Au)    // [-] - Folder
};

// 📐 One tone travelling toward another, for a hover that grows rather than
//    switching. Spelled here because the identical helper in ControlPanel.cpp
//    lives in that translation unit's own anonymous namespace.
constexpr std::uint8_t BlendChannel(std::uint8_t Previous, std::uint8_t Incoming, float Fraction)
{
    return static_cast<std::uint8_t>(static_cast<float>(Previous) +
                                     (static_cast<float>(Incoming) -
                                      static_cast<float>(Previous)) * Fraction + 0.5f);
}

constexpr ThemeToken Blend(ThemeToken Previous, ThemeToken Incoming, float Fraction)
{
    const float Held = (Fraction < 0.0f) ? 0.0f : (Fraction > 1.0f) ? 1.0f : Fraction;

    return ThemeToken{ BlendChannel(Previous.Red,     Incoming.Red,     Held),
                       BlendChannel(Previous.Green,   Incoming.Green,   Held),
                       BlendChannel(Previous.Blue,    Incoming.Blue,    Held),
                       BlendChannel(Previous.Opacity, Incoming.Opacity, Held) };
}

// 📐 The chips region's facets: ONE PER CHANNEL, captioned and coloured from the
//    channel schema rather than from a hand-written list beside it. A chip's
//    swatch is the channel's own hue, which is what makes the chip legible as
//    that channel and not merely as a word.
const char* const* ChannelFacetOptions()
{
    static const char* Captions[TextureChannelLimit] = {};
    static bool Filled = false;

    if (!Filled)
    {
        for (std::uint32_t Each = 0u; Each < TextureChannelLimit; ++Each)
            Captions[Each] = TextureChannelAt(Each).Label;

        Filled = true;
    }

    return Captions;
}

const ThemeToken* ChannelFacetColours()
{
    static ThemeToken Hues[TextureChannelLimit] = {};
    static bool Filled = false;

    if (!Filled)
    {
        for (std::uint32_t Each = 0u; Each < TextureChannelLimit; ++Each)
            Hues[Each] = Covering(TextureChannelAt(Each).Hue);

        Filled = true;
    }

    return Hues;
}

/// 🧩 Whether the search and the layer facets jointly retain one row.
bool RowRetained(const TexturingContext& Applied, const TextureLayerRow& Row)
{
    const bool Searching = Applied.Retention[0] != '\0';

    if (Searching)
    {
        if (!RunHolds(Row.Naming, Applied.Retention) && !RunHolds(Row.Tagged, Applied.Retention))
            return false;
    }

    for (std::uint32_t Facet = 0u; Facet < TexturingContext::TextureFacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return Applied.FacetEnabled[TextureLayerFacetOf(Row.Classified)];
    }

    return true;
}

/// 🧩 Whether the search or any stack facet is active at all.
bool RetentionActive(const TexturingContext& Applied)
{
    if (Applied.Retention[0] != '\0')
        return true;

    for (std::uint32_t Facet = 0u; Facet < TexturingContext::TextureFacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return true;
    }

    return false;
}

/// 🧩 Whether the channel search or any channel facet is active at all.
// 🔴 This asked whether ANY facet stood and, if so, hid every channel outside it.
//    With the chips now being the enabled set, that reading would hide every
//    channel the layer has switched OFF — the artist could never switch one back
//    on, because the card holding its switch would have vanished. The chips
//    region and the search do different work now and are asked separately: the
//    chips say which channels the layer HAS, the search narrows which of them the
//    card lists.
bool ChannelRetentionActive(const TexturingContext& Applied)
{
    return Applied.Retention[0] != '\0';
}

/// 🧩 Whether one row belongs to the solo's set: the solo row, its ancestors and its descendants.
bool RowInSolo(const TexturingContext& Applied, const TextureLayerRow* Rows,
               std::uint32_t RowCount, std::uint32_t Index)
{
    if (Applied.SoloTaken >= RowCount)
        return true;

    if (Index == Applied.SoloTaken)
        return true;

    // 📐 The solo row's ancestors: walk the candidate's enclosure chain toward the root.
    std::uint32_t Walking = Rows[Index].Enclosing;

    while (Walking < RowCount)
    {
        if (Walking == Applied.SoloTaken)
            return true;

        if (Rows[Walking].Depth >= Rows[Index].Depth)
            break;

        Walking = Rows[Walking].Enclosing;
    }

    // 📐 The solo row's descendants: the candidate is inside the solo row's subtree.
    Walking = Index;

    while (Walking < RowCount && Rows[Walking].Depth > Rows[Applied.SoloTaken].Depth)
    {
        if (Walking == Applied.SoloTaken)
            return true;

        Walking = Rows[Walking].Enclosing;
    }

    return false;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CLASSIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

SymbolSubject TextureLayerGlyph(TextureLayerClassification Classified)
{
    // 📐 The reference's own `TYPE` icons, transcribed: brush, drop, sliders, funnel, decal, pattern,
    //    spark, folder.
    switch (Classified)
    {
        case TextureLayerClassification::Brushed:      return SymbolSubject::Bristle;
        case TextureLayerClassification::Fill:       return SymbolSubject::DropletDrop;
        case TextureLayerClassification::Decal:      return SymbolSubject::StencilDecal;
        case TextureLayerClassification::Pattern:    return SymbolSubject::TiledPattern;
        case TextureLayerClassification::Generator:  return SymbolSubject::GeneratorSpark;
        case TextureLayerClassification::Adjustment: return SymbolSubject::AdjustmentSliders;
        case TextureLayerClassification::Filter:     return SymbolSubject::FilterFunnel;
        case TextureLayerClassification::Folder:     return SymbolSubject::FolderClosed;
        case TextureLayerClassification::Material:   return SymbolSubject::MaterialSphere;
        default:                                     return SymbolSubject::LayerMerge;
    }
}

ThemeToken TextureLayerHue(TextureLayerClassification Classified)
{
    switch (Classified)
    {
        case TextureLayerClassification::Brushed:      return Covering(0xF97316u);
        case TextureLayerClassification::Fill:       return Covering(0x3B82F6u);
        case TextureLayerClassification::Decal:      return Covering(0xEF4444u);
        case TextureLayerClassification::Pattern:    return Covering(0x10B981u);
        case TextureLayerClassification::Generator:  return Covering(0x8B5CF6u);
        case TextureLayerClassification::Adjustment: return Covering(0xEAB308u);
        case TextureLayerClassification::Filter:     return Covering(0x06B6D4u);
        case TextureLayerClassification::Folder:     return Covering(0x8A8A8Au);
        case TextureLayerClassification::Material:   return Covering(0xEC4899u);
        default:                                     return Covering(0x8A8A8Au);
    }
}

const char* TextureLayerText(TextureLayerClassification Classified)
{
    switch (Classified)
    {
        case TextureLayerClassification::Brushed:      return "Brushed";
        case TextureLayerClassification::Fill:       return "Fill";
        case TextureLayerClassification::Decal:      return "Decal";
        case TextureLayerClassification::Pattern:    return "Pattern";
        case TextureLayerClassification::Generator:  return "Generator";
        case TextureLayerClassification::Adjustment: return "Adjustment";
        case TextureLayerClassification::Filter:     return "Filter";
        case TextureLayerClassification::Folder:     return "Folder";
        case TextureLayerClassification::Material:   return "Material";
        default:                                     return "Layer";
    }
}

std::uint32_t TextureLayerFacetOf(TextureLayerClassification Classified)
{
    switch (Classified)
    {
        case TextureLayerClassification::Brushed:      return 0u;
        case TextureLayerClassification::Fill:       return 1u;
        case TextureLayerClassification::Decal:      return 2u;
        case TextureLayerClassification::Pattern:    return 3u;
        case TextureLayerClassification::Generator:  return 4u;
        case TextureLayerClassification::Adjustment: return 5u;
        case TextureLayerClassification::Filter:     return 6u;
        case TextureLayerClassification::Folder:     return 7u;
        case TextureLayerClassification::Material:   return 1u;
        default:                                     return 0u;
    }
}

// 📐 CHANNEL_SLOTS from `References/ChannelPropertyPanel.html`, transcribed
//    entry for entry: the group, the swatch, the edit kind, and the span each
//    scalar is authored over. The eight-name run this replaced carried none of
//    it, so every channel drew the same row over the same 0..100.
const TextureChannelSlot& TextureChannelAt(std::uint32_t Index)
{
    using Edit = TextureChannelEdit;

    static const TextureChannelSlot Slots[TextureChannelLimit] =
    {
        { "Base Colour",       "Surface",     "Colour atlas \xC2\xB7 RGB",   "",  0xB87333u, Edit::Colour              },
        { "Metallic",          "Surface",     "Material atlas \xC2\xB7 R",   "",  0x8B5CF6u, Edit::Scalar,  0.0, 1.0   },
        { "Roughness",         "Surface",     "Material atlas \xC2\xB7 G",   "",  0x3B82F6u, Edit::Scalar,  0.0, 1.0   },
        { "Height",            "Surface",     "Material atlas \xC2\xB7 B",   "",  0x8A8A8Au, Edit::Scalar,  0.0, 1.0   },
        { "Normal",            "Surface",     "No storage \xC2\xB7 derived", "",  0x10B981u, Edit::Derived             },
        { "Opacity",           "Surface",     "Material atlas \xC2\xB7 A",   "",  0x94A3B8u, Edit::Scalar,  0.0, 1.0   },

        { "Emissive",          "Radiance",    "Emissive atlas \xC2\xB7 RGB", "",  0xF59E0Bu, Edit::Colour              },
        { "Ambient Occlusion", "Radiance",    "Emissive atlas \xC2\xB7 A",   "",  0x6B7280u, Edit::Scalar,  0.0, 1.0   },

        { "Anisotropy",        "Reflectance", "Reflect atlas \xC2\xB7 R",    "",  0x22D3EEu, Edit::Scalar,  0.0, 1.0   },
        { "Anisotropy Angle",  "Reflectance", "Reflect atlas \xC2\xB7 G",    "\xC2\xB0", 0x0EA5E9u, Edit::Scalar, 0.0, 360.0 },
        { "Clearcoat",         "Reflectance", "Reflect atlas \xC2\xB7 B",    "",  0xE2E8F0u, Edit::Scalar,  0.0, 1.0   },
        { "Refraction Index",  "Reflectance", "Reflect atlas \xC2\xB7 A",    "",  0xA78BFAu, Edit::Scalar,  1.0, 3.0   },

        { "Sheen",             "Scattering",  "Scatter atlas \xC2\xB7 RGB",  "",  0xF472B6u, Edit::Colour              },
        { "Subsurface",        "Scattering",  "Sheen atlas \xC2\xB7 RGB",    "",  0xFB7185u, Edit::Colour              },
    };

    static const TextureChannelSlot Absent;
    return Index < TextureChannelLimit ? Slots[Index] : Absent;
}

// 📐 GENERATOR_CATALOGUE from the reference, transcribed entry for entry with
//    every parameter's own span and default. The port carried none of these:
//    choosing a generator set a name and offered no knobs at all.
const TextureGeneratorEntry& TextureGeneratorAt(std::uint32_t Index)
{
    static const TextureGeneratorEntry Catalogue[TextureGeneratorLimit] =
    {
        { "Curvature",         "Mask", "Convex edge wear", 3u,
          { { "Balance", 0.0, 1.0, 0.50 }, { "Contrast", 0.0, 1.0, 0.70 }, { "Radius", 0.0, 1.0, 0.25 } } },

        { "Ambient Occlusion", "Mask", "Cavity dirt", 2u,
          { { "Spread", 0.0, 1.0, 0.40 }, { "Contrast", 0.0, 1.0, 0.60 } } },

        { "Thickness",         "Mask", "Translucent falloff", 2u,
          { { "Depth", 0.0, 1.0, 0.50 }, { "Contrast", 0.0, 1.0, 0.50 } } },

        { "Position Gradient", "Mask", "World-axis ramp", 2u,
          { { "Origin", 0.0, 1.0, 0.50 }, { "Falloff", 0.0, 1.0, 0.35 } } },

        { "Metal Edge Wear",   "Wear", "Curvature + grunge", 3u,
          { { "Intensity", 0.0, 1.0, 0.60 }, { "Softness", 0.0, 1.0, 0.30 }, { "Grain", 0.0, 1.0, 0.45 } } },

        { "Dirt",              "Wear", "Occlusion-driven", 2u,
          { { "Amount", 0.0, 1.0, 0.50 }, { "Scale", 0.0, 1.0, 0.30 } } },

        { "Water Runoff",      "Wear", "Gravity streaks", 3u,
          { { "Length", 0.0, 1.0, 0.55 }, { "Density", 0.0, 1.0, 0.40 }, { "Gravity", 0.0, 1.0, 0.80 } } },

        { "Perlin Noise", "Procedural", "Fractal value noise", 3u,
          { { "Scale", 0.0, 1.0, 0.40 }, { "Octaves", 0.0, 1.0, 0.50 }, { "Contrast", 0.0, 1.0, 0.50 } } },

        { "Voronoi",      "Procedural", "Cellular partition", 2u,
          { { "Density", 0.0, 1.0, 0.35 }, { "Jitter", 0.0, 1.0, 0.70 } } },

        { "Brushed Metal","Procedural", "Anisotropic streaks", 2u,
          { { "Angle", 0.0, 1.0, 0.00 }, { "Grain", 0.0, 1.0, 0.60 } } },
    };

    static const TextureGeneratorEntry Absent;
    return Index < TextureGeneratorLimit ? Catalogue[Index] : Absent;
}

const char* TextureChannelText(std::uint32_t Index)
{
    return TextureChannelAt(Index).Label;
}

std::uint32_t TextureChannelGroup(std::uint32_t Index)
{
    // 🔴 This used to bracket ordinals — "0 is Base, 6 and up are Output" — so a
    //    channel's group was a property of its position in the list rather than
    //    of the channel. Inserting one entry silently regrouped everything after
    //    it. The group is read from the schema now.
    const char* const Group = TextureChannelAt(Index).Group;

    for (std::uint32_t Each = 0u; Each < TextureChannelGroupCount; ++Each)
        if (std::strcmp(Group, TextureChannelGroupNames[Each]) == 0)
            return Each;

    return 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE SHARED STACK
//------------------------------------------------------------------------------------------------------------------------

void SeedTexturingContextFromRows(TexturingContext& Applied,
                              const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
    {
        const bool Stands = Index < RowCount;
        const TextureLayerRow& Row = Stands ? Rows[Index] : TextureLayerRow{};

        // 📝 The stack page's working copies.
        Applied.LayerOpacity[Index]   = Row.Opacity;
        Applied.LayerBlendTaken[Index] = 0u;

        if (Stands)
        {
            for (std::uint32_t Blend = 0u; Blend < TextureBlendCount; ++Blend)
            {
                if (std::strcmp(Row.Blend, TextureBlendNames[Blend]) == 0)
                {
                    Applied.LayerBlendTaken[Index] = Blend;
                    break;
                }
            }
        }

        Applied.LayerLocked[Index]    = Row.Locked;
        Applied.MaskAttached[Index]   = Row.MaskDeclared;
        Applied.MaskVisible[Index]    = true;
        Applied.LayerTagHue[Index]    = Row.TagHue;
        Applied.LayerExpanded[Index]  = Row.Expanded;
        Applied.LayerCardExpanded[Index] = false;
        for (std::uint32_t Section = 0u; Section < 5u; ++Section)
            Applied.LayerCardSection[Index][Section] = true;
        Applied.LayerResolution[Index] = 2048.0;
        Applied.LayerHeightIntegrated[Index] = false;
        Applied.LayerHeightBlendTaken[Index] = 0u;
        Applied.LayerEffectTaken[Index] = (Row.Effects != nullptr && Row.Effects[0] != '\0') ? 1u : 0u;

        if (Stands && Row.Detail != nullptr)
        {
            double Parsed = 0.0;
            char* ParsedPast = nullptr;
            Parsed = std::strtod(Row.Detail, &ParsedPast);
            if (ParsedPast != Row.Detail && Parsed >= 16.0)
                Applied.LayerResolution[Index] = Parsed;
        }

        Applied.LayerPresent[Index]   = true;
        Applied.ChannelTaken[Index]   = 0u;

        // 📝 The properties page's scratch.
        for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
        {
            Applied.ChannelOn[Index][Channel] = Stands && Channel < 6u;
            Applied.ChannelAmount[Index][Channel] = 100u;
            Applied.ChannelBlendTaken[Index][Channel] = 0u;
        }

        // 🔴 These three were hardcoded to 100 / false / 0 while `TextureLayerRow`
        //    carries `MaskStrength`, `MaskInverted` and `Source` right there. The
        //    seed discarded all three, so a row declaring MaskStrength = 88
        //    rendered 100 and the mask card looked as though it could not read
        //    its own row. This function exists precisely to stop the row and the
        //    working copy drifting apart, and on these fields it was the thing
        //    causing the drift.
        Applied.MaskDensity[Index]       = Stands ? Row.MaskStrength : 100u;
        Applied.MaskInverted[Index]      = Stands && Row.MaskInverted;
        Applied.MaskSourceTaken[Index]   = 0u;

        // 📐 The mask source is a run on the row; the card holds an ordinal into
        //    the roster. Resolve it the same way the blend above is resolved.
        if (Stands && Row.Source != nullptr && Row.Source[0] != '\0')
        {
            for (std::uint32_t Source = 0u; Source < 5u; ++Source)
            {
                if (std::strcmp(Row.Source, TextureMaskSourceNames[Source]) == 0)
                {
                    Applied.MaskSourceTaken[Index] = Source;
                    break;
                }
            }
        }

        // 📐 The mask targets every channel the row declares, which is what the
        //    reference's `mask.channels` holds.
        for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
            Applied.MaskChannel[Index][Channel] = Stands && Channel < 4u;

        Applied.SettingAmount[Index][0]  = 100u;
        Applied.SettingAmount[Index][1]  = 50u;
        Applied.SettingAmount[Index][2]  = 50u;
        Applied.SettingAmount[Index][3]  = 50u;
        Applied.SettingToggle[Index]     = 0u;
        Applied.SettingSelection[Index]     = 0u;
    }
}

void TexturingStack::Seed(const TextureLayerRow* Source, std::uint32_t SourceCount)
{
    Count = (Source != nullptr) ? std::min(SourceCount, TextureLayerLimit) : 0u;
    NextIdentity = 1u;

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        Rows[Index] = Source[Index];
        if (Rows[Index].Identity == 0u)
            Rows[Index].Identity = NextIdentity;
        if (Rows[Index].Identity >= NextIdentity)
            NextIdentity = Rows[Index].Identity + 1u;
        Names[Index][0] = '\0';
    }
}

namespace
{

/// 🧩 Writes one row's name into the stack's own storage and borrows it back.
const char* HoldName(TexturingStack& Stack, std::uint32_t Index, const char* Name)
{
    std::snprintf(Stack.Names[Index], sizeof(Stack.Names[Index]), "%s", Name);
    return Stack.Names[Index];
}

/// 🧩 What a freshly added row stands on, from the reference's `add()`.
TextureLayerRow NewRow(TexturingStack& Stack, std::uint32_t Index,
                       TextureLayerClassification Classified, const char* Name,
                       std::uint32_t Depth, std::uint32_t Enclosing)
{
    TextureLayerRow Row;
    Row.Naming       = HoldName(Stack, Index, Name);
    Row.Classified   = Classified;
    Row.Blend        = (Classified == TextureLayerClassification::Folder) ? "Passthrough" : "Normal";
    Row.Opacity      = 100u;
    Row.TexturingHue     = SwatchColours[Index % TexturingContext::TextureSwatchCount];
    Row.TagHue       = Row.TexturingHue;
    Row.MaskDeclared = false;
    Row.MaskStrength = 100u;
    Row.Detail       = "2048px \u00B7 RGBA 8";
    Row.ChannelCount = 6u;
    Row.Depth        = Depth;
    Row.Enclosing    = Enclosing;
    Row.EnclosedCount = 0u;
    Row.Expanded     = true;
    Row.Tagged       = "";
    Row.Identity     = Stack.NextIdentity++;

    switch (Classified)
    {
        case TextureLayerClassification::Decal:
            Row.Detail   = "Planar \u00B7 100%";
            Row.ChannelCount = 1u;
            break;
        case TextureLayerClassification::Pattern:
            Row.Detail   = "Hex Grid \u00B7 4\u00D74";
            Row.ChannelCount = 2u;
            break;
        case TextureLayerClassification::Folder:
            Row.Detail   = "0 layers";
            Row.ChannelCount = 0u;
            break;
        case TextureLayerClassification::Adjustment:
            Row.ChannelCount = 2u;
            break;
        default:
            break;
    }

    return Row;
}

/// 🧩 The extent of the taken row's whole subtree: the contiguous run of rows nested inside it.
std::uint32_t SubtreePast(const TexturingStack& Stack, std::uint32_t Taken)
{
    if (Taken >= Stack.Count)
        return Taken + 1u;

    const std::uint32_t Floor = Stack.Rows[Taken].Depth;
    std::uint32_t Past = Taken + 1u;

    while (Past < Stack.Count && Stack.Rows[Past].Depth > Floor)
        ++Past;

    return Past;
}

void RebuildTextureHierarchy(TexturingStack& Stack)
{
    std::uint32_t Ancestors[TextureLayerLimit] = {};

    for (std::uint32_t Index = 0u; Index < Stack.Count; ++Index)
    {
        TextureLayerRow& Row = Stack.Rows[Index];
        if (Row.Depth == 0u)
            Row.Enclosing = 0xFFFFFFFFu;
        else
            Row.Enclosing = Ancestors[Row.Depth - 1u];

        Ancestors[Row.Depth] = Index;
        Row.EnclosedCount = 0u;
    }

    for (std::uint32_t Index = 0u; Index < Stack.Count; ++Index)
        if (Stack.Rows[Index].Enclosing < Stack.Count)
            ++Stack.Rows[Stack.Rows[Index].Enclosing].EnclosedCount;
}

}   // namespace

void TexturingStack::ApplyRequest(TexturingContext& Applied)
{
    const std::uint32_t Request = Applied.Structural;
    Applied.Structural = 0u;

    if (Request == static_cast<std::uint32_t>(TexturingRequest::None) || Count == 0u)
        return;

    // ① Write the working copies back into the model so the artist's edits never drift.
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        TextureLayerRow& Row = Rows[Index];
        Row.Opacity       = Applied.LayerOpacity[Index];
        Row.Blend         = TextureBlendNames[Applied.LayerBlendTaken[Index] % TextureBlendCount];
        Row.Locked        = Applied.LayerLocked[Index];
        Row.MaskDeclared  = Applied.MaskAttached[Index];
        Row.TagHue        = Applied.LayerTagHue[Index];
        Row.TexturingHue      = Applied.LayerTagHue[Index];
        Row.Expanded      = Applied.LayerExpanded[Index];
        Row.Selected      = Applied.LayerSelected[Index];
    }

    const std::uint32_t Taken = std::min(Applied.LayerTaken, Count - 1u);

    // ② Apply the structural change — the reference's own operations.
    switch (static_cast<TexturingRequest>(Request))
    {
        case TexturingRequest::Relocate:
        {
            const std::uint32_t Source = Applied.DragSource;
            const std::uint32_t Destination = Applied.DragDestination;
            const std::uint32_t Placement = Applied.DragPlacement;

            if (Source >= Count || Destination >= Count || Source == Destination || Placement == 0u)
                break;

            const std::uint32_t SourcePast = SubtreePast(*this, Source);
            if (Destination >= Source && Destination < SourcePast)
                break;

            if (Placement == 3u && Rows[Destination].Classified != TextureLayerClassification::Folder)
                break;

            const std::uint32_t IncomingDepth = Rows[Destination].Depth + (Placement == 3u ? 1u : 0u);
            const std::int32_t DepthDelta = static_cast<std::int32_t>(IncomingDepth)
                                          - static_cast<std::int32_t>(Rows[Source].Depth);
            const std::int32_t Deepest = static_cast<std::int32_t>(Rows[SourcePast - 1u].Depth) + DepthDelta;
            if (Deepest < 0 || Deepest >= static_cast<std::int32_t>(TextureLayerLimit))
                break;

            const std::uint32_t Span = SourcePast - Source;
            TextureLayerRow Carried[TextureLayerLimit] = {};
            for (std::uint32_t Index = 0u; Index < Span; ++Index)
                Carried[Index] = Rows[Source + Index];

            for (std::uint32_t Index = Source; Index + Span < Count; ++Index)
                Rows[Index] = Rows[Index + Span];

            const std::uint32_t ReducedDestination = Destination > Source
                                                   ? Destination - Span : Destination;
            const std::uint32_t DestinationDepth = Rows[ReducedDestination].Depth;
            std::uint32_t Home = ReducedDestination;

            if (Placement == 1u)
            {
                Home = ReducedDestination;
            }
            else
            {
                Home = ReducedDestination + 1u;
                while (Home < Count - Span && Rows[Home].Depth > DestinationDepth)
                    ++Home;
            }

            for (std::uint32_t Index = Count - Span; Index-- > Home;)
                Rows[Index + Span] = Rows[Index];

            for (std::uint32_t Index = 0u; Index < Span; ++Index)
            {
                Rows[Home + Index] = Carried[Index];
                Rows[Home + Index].Depth = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(Carried[Index].Depth) + DepthDelta);
            }

            RebuildTextureHierarchy(*this);
            Applied.LayerTaken = Home;
            Applied.MaskTaken = false;
            break;
        }
        case TexturingRequest::Delete:
        {
            // 📐 A taken mask deletes the mask only, exactly as the reference's `aDel` branches.
            if (Applied.MaskTaken && Rows[Taken].MaskDeclared)
            {
                Rows[Taken].MaskDeclared = false;
                Applied.MaskTaken        = false;
                break;
            }

            const std::uint32_t Past = SubtreePast(*this, Taken);

            if (Taken + 1u < Count)
            {
                const std::uint32_t Move = Count - Past;

                for (std::uint32_t Index = 0u; Index < Move; ++Index)
                    Rows[Taken + Index] = Rows[Past + Index];

                Count -= (Past - Taken);
            }
            else
            {
                Count = Taken;
            }

            if (Count > 0u)
                Applied.LayerTaken = std::min(Taken, Count - 1u);

            Applied.MaskTaken = false;
            break;
        }
        case TexturingRequest::Duplicate:
        {
            const std::uint32_t Past = SubtreePast(*this, Taken);
            const std::uint32_t Span = Past - Taken;

            if (Count + Span > TextureLayerLimit)
                break;

            // 📝 The copy lands beneath the whole subtree, exactly as the reference's `aDup` inserts
            //    the deep copy at the taken row's position.
            for (std::uint32_t Index = Count; Index-- > Taken + Span;)
                Rows[Index + Span - 1u] = Rows[Index - 1u];

            for (std::uint32_t Index = 0u; Index < Span; ++Index)
            {
                const std::uint32_t At = Taken + Span + Index;
                Rows[At] = Rows[Taken + Index];
                Rows[At].Identity = NextIdentity++;

                if (Index == 0u)
                {
                    char Copied[64] = {};
                    std::snprintf(Copied, sizeof(Copied), "%s copy", Rows[Taken].Naming);
                    Rows[At].Naming = HoldName(*this, At, Copied);
                }
                else if (Rows[Taken + Index].Naming >= Names[Taken + Index] &&
                         Rows[Taken + Index].Naming < Names[Taken + Index] + sizeof(Names[0]))
                {
                    // 📝 A nested row that was itself an inserted name is re-homed so the two copies
                    //    never share the same buffer.
                    std::snprintf(Names[At], sizeof(Names[At]), "%s", Rows[Taken + Index].Naming);
                    Rows[At].Naming = Names[At];
                }
            }

            Count += Span;
            Applied.LayerTaken = Taken + Span;
            Applied.MaskTaken  = false;
            break;
        }
        case TexturingRequest::Group:
        {
            if (Count + 1u > TextureLayerLimit)
                break;

            const std::uint32_t Past = SubtreePast(*this, Taken);
            const std::uint32_t Span = Past - Taken;
            const TextureLayerRow Wrapped = Rows[Taken];

            // 📝 The folder takes the row's place; the subtree shifts one deeper beneath it.
            for (std::uint32_t Index = Count; Index-- > Taken;)
                Rows[Index + 1u] = Rows[Index];

            Rows[Taken] = NewRow(*this, Taken, TextureLayerClassification::Folder, "Group",
                                 Wrapped.Depth, Wrapped.Enclosing);
            Rows[Taken].TexturingHue  = Wrapped.TagHue;
            Rows[Taken].TagHue    = Wrapped.TagHue;
            Rows[Taken].Detail    = "1 layers";
            Rows[Taken].EnclosedCount = 1u;

            for (std::uint32_t Index = 1u; Index <= Span; ++Index)
            {
                Rows[Taken + Index].Depth     = Wrapped.Depth + 1u;
                Rows[Taken + Index].Enclosing = Taken;
            }

            ++Count;
            Applied.LayerTaken = Taken;
            Applied.MaskTaken  = false;
            break;
        }
        case TexturingRequest::MoveUp:
        case TexturingRequest::MoveDown:
        {
            const std::int32_t Direction = (Request == static_cast<std::uint32_t>(TexturingRequest::MoveUp))
                                         ? -1 : 1;
            const std::int32_t Neighbour = static_cast<std::int32_t>(Taken) + Direction;

            if (Neighbour < 0 || Neighbour >= static_cast<std::int32_t>(Count))
                break;

            const TextureLayerRow& Current = Rows[Taken];
            const TextureLayerRow& Nearby  = Rows[static_cast<std::uint32_t>(Neighbour)];

            // 📐 Moving INTO an open folder parks the row as the folder's first (up) or last (down)
            //    child — the reference's `shift()`.
            if (Nearby.Classified == TextureLayerClassification::Folder && Nearby.Expanded &&
                Nearby.Depth + 1u == Current.Depth && Nearby.Enclosing == Current.Enclosing)
            {
                const std::uint32_t Past  = SubtreePast(*this, Taken);
                const std::uint32_t Span  = Past - Taken;
                const std::uint32_t Home  = static_cast<std::uint32_t>(Neighbour) + (Direction < 0 ? 1u : 1u);

                for (std::uint32_t Index = 0u; Index < Span; ++Index)
                {
                    for (std::uint32_t Step = Past; Step-- > Home + Index;)
                        Rows[Step] = Rows[Step - 1u];

                    Rows[Home + Index] = Taken < Home ? Rows[Past - 1u + Index] : Rows[Taken + Index];
                }

                // 🔴 The block move above was not used: the simpler splice below is exact and readable.
                for (std::uint32_t Index = Taken; Index + Span < Past; ++Index)
                    Rows[Index] = Rows[Index + Span];

                // 📐 Re-home the moved subtree under the folder.
                for (std::uint32_t Index = Home; Index < Home + Span; ++Index)
                {
                    Rows[Index].Depth = Current.Depth + (Rows[Index].Depth > Current.Depth ? 1 : 0);
                    Rows[Index].Depth = Nearby.Depth + 1u + (Rows[Index].Depth > Nearby.Depth + 1u ? 1u : 0u);
                }

                Applied.LayerTaken = Home;
                Applied.MaskTaken  = false;
                break;
            }

            // 📐 Otherwise the row swaps with its same-parent neighbour — the reference's `list.splice`.
            if (Current.Depth != Nearby.Depth || Current.Enclosing != Nearby.Enclosing)
                break;

            const std::uint32_t Past = SubtreePast(*this, Taken);
            const std::uint32_t Span = Past - Taken;

            // 📝 The whole subtree moves together, one slot at a time.
            for (std::uint32_t Index = 0u; Index < Span; ++Index)
                std::swap(Rows[Taken + Index], Rows[static_cast<std::uint32_t>(Neighbour) + Index]);

            Applied.LayerTaken = static_cast<std::uint32_t>(Neighbour);
            Applied.MaskTaken  = false;
            break;
        }
        default:
        {
            // 📐 The add family — the reference's `add()`: the new row lands BEFORE the taken row, or
            //    at the top when nothing is taken.
            if (Count + 1u > TextureLayerLimit)
                break;

            TextureLayerClassification Classified = TextureLayerClassification::Brushed;
            const char* Name = "Texture Layer";

            switch (static_cast<TexturingRequest>(Request))
            {
                case TexturingRequest::AddFill:       Classified = TextureLayerClassification::Fill;       Name = "Fill Layer";      break;
                case TexturingRequest::AddAdjustment: Classified = TextureLayerClassification::Adjustment; Name = "Adjustment";     break;
                case TexturingRequest::AddFilter:     Classified = TextureLayerClassification::Filter;     Name = "Filter";          break;
                case TexturingRequest::AddDecal:      Classified = TextureLayerClassification::Decal;      Name = "Decal Layer";     break;
                case TexturingRequest::AddPattern:    Classified = TextureLayerClassification::Pattern;    Name = "Pattern Layer";   break;
                case TexturingRequest::AddFolder:     Classified = TextureLayerClassification::Folder;     Name = "New Folder";      break;
                default:                                                                                                                  break;
            }

            const std::uint32_t Home = (Taken < Count) ? Taken : 0u;

            for (std::uint32_t Index = Count; Index-- > Home;)
                Rows[Index + 1u] = Rows[Index];

            Rows[Home] = NewRow(*this, Home, Classified, Name,
                                Home < Count ? Rows[Home + 1u].Depth : 0u,
                                Home < Count ? Rows[Home + 1u].Enclosing : 0xFFFFFFFFu);

            if (Classified == TextureLayerClassification::Folder)
                Rows[Home].Expanded = true;

            // 📝 The inserted folder holds nothing; an inserted row inside a folder keeps the depth.
            if (Home + 1u < Count + 1u && Home + 1u < Count &&
                Rows[Home + 1u].Depth <= Rows[Home].Depth)
            {
                // nothing to re-home — the new row shares the taken row's level
            }

            ++Count;
            Applied.LayerTaken = Home;
            Applied.MaskTaken  = false;
            break;
        }
    }

    Applied.DragSource = TextureLayerLimit;
    Applied.DragDestination = TextureLayerLimit;
    Applied.DragPlacement = 0u;

    // ③ Re-seed the working copies so every index lines up with the changed row set.
    SeedTexturingContextFromRows(Applied, Rows, Count);
    for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
        Applied.LayerSelected[Index] = Index < Count && Rows[Index].Selected;

    if (Applied.LayerTaken >= Count && Count > 0u)
        Applied.LayerTaken = Count - 1u;
    Applied.LayerSelectionAnchor = Applied.LayerTaken;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TexturingPanel::ConstructTexturingPanel(ControlIndex& IncomingInteraction,
                                           MotionIntegrator& Integrator,
                                           RecordingSurface& IncomingSurface,
                                           const ThemeProfile& Resolved)
{
    if (Interaction != nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the texturing panel is already constructed" });
    }

    Interaction     = &IncomingInteraction;
    Motion     = &Integrator;
    this->Surface = &IncomingSurface;
    Appearance = &Resolved;

    if (!Controls.ConstructControlPanel(IncomingInteraction, IncomingSurface, Resolved).Resolved ||
        !SharedControls.ConstructComponents(IncomingInteraction, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the texturing controls were rejected" });
    }

    if (!StackFacets.ConstructFacetPanel(Integrator, IncomingSurface, Resolved).Resolved ||
        !ChannelFacets.ConstructFacetPanel(Integrator, IncomingSurface, Resolved).Resolved ||
        !MaskFacets.ConstructFacetPanel(Integrator, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the texturing filters were rejected" });
    }

    ControlIdentity* const Every[] =
    {
        &HeaderAdd,
        &ToolFolder, &ToolMask, &ToolCollapse, &SearchField,
        &BlendField, &OpacityRow,
        &BarButtons[0],  &BarButtons[1],  &BarButtons[2],  &BarButtons[3],
        &BarButtons[4],  &BarButtons[5],  &BarButtons[6],  &BarButtons[7],
        &BarButtons[8],  &BarButtons[9],  &BarButtons[10], &BarButtons[11],
        &StackStrip, &PropertyStrip,
        &ExportBack,
        &ExportArrows[0][0], &ExportArrows[0][1], &ExportArrows[1][0], &ExportArrows[1][1],
        &ExportArrows[2][0], &ExportArrows[2][1],
        &ExportCarouselOptions[0][0], &ExportCarouselOptions[0][1], &ExportCarouselOptions[0][2], &ExportCarouselOptions[0][3],
        &ExportCarouselOptions[0][4], &ExportCarouselOptions[0][5], &ExportCarouselOptions[0][6], &ExportCarouselOptions[0][7],
        &ExportCarouselOptions[0][8], &ExportCarouselOptions[0][9],
        &ExportCarouselOptions[1][0], &ExportCarouselOptions[1][1], &ExportCarouselOptions[1][2], &ExportCarouselOptions[1][3],
        &ExportCarouselOptions[1][4], &ExportCarouselOptions[1][5], &ExportCarouselOptions[1][6], &ExportCarouselOptions[1][7],
        &ExportCarouselOptions[1][8], &ExportCarouselOptions[1][9],
        &ExportCarouselOptions[2][0], &ExportCarouselOptions[2][1], &ExportCarouselOptions[2][2], &ExportCarouselOptions[2][3],
        &ExportCarouselOptions[2][4], &ExportCarouselOptions[2][5], &ExportCarouselOptions[2][6], &ExportCarouselOptions[2][7],
        &ExportCarouselOptions[2][8], &ExportCarouselOptions[2][9],
        &ExportFields[0], &ExportFields[1], &ExportFields[2],
        &ExportOptions[0], &ExportOptions[1], &ExportOptions[2],
        &ExportOptions[3], &ExportOptions[4], &ExportOptions[5],
        &MenuAdd, &MenuLayer, &MenuMask, &MenuBlend,
        &MaskRows[0], &MaskRows[1], &MaskRows[2], &MaskRows[3], &MaskRows[4],
        &MaskRows[5], &MaskRows[6], &MaskRows[7], &MaskRows[8],
        &MaskParams[0], &MaskParams[1], &MaskParams[2],
        &DecalRows[0], &DecalRows[1], &DecalRows[2], &DecalRows[3],
        &DecalRows[4], &DecalRows[5], &DecalRows[6], &DecalRows[7],
        &DecalRows[8], &DecalRows[9],
        &FolderRows[0], &FolderRows[1], &FolderRows[2],
        &SettingRows[0], &SettingRows[1], &SettingRows[2], &SettingRows[3]
    };

    for (ControlIdentity* Identity : Every)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        *Identity = Registered.Resolve();
    }

    for (ControlIdentity& Identity : MenuIdentities)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        Identity = Registered.Resolve();
    }

    for (ControlIdentity& Identity : InlineControls)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        Identity = Registered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
    {
        ControlIdentity* const Rows[] =
        {
            &LayerContacts[Index], &LayerChevrons[Index],
            &LayerEyes[Index], &LayerDetails[Index], &LayerMores[Index],
            &MaskContacts[Index], &MaskEyes[Index],
            &MaskDetails[Index], &MaskMores[Index]
        };

        for (ControlIdentity* Identity : Rows)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
            if (!Registered.Resolved)
                return Deliver<bool>::Refuse(Registered.Error);

            *Identity = Registered.Resolve();
        }
    }

    for (std::uint32_t Index = 0u; Index < TextureChannelLimit; ++Index)
    {
        ControlIdentity* const Rows[] =
        {
            &ChannelFolds[Index], &ChannelDots[Index],
            &ChannelBlends[Index], &ChannelOps[Index],
            &ChannelGenerators[Index], &ChannelGenReset[Index], &ChannelGenDrop[Index],
            &ChannelParams[Index][0], &ChannelParams[Index][1], &ChannelParams[Index][2]
        };

        for (ControlIdentity* Identity : Rows)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
            if (!Registered.Resolved)
                return Deliver<bool>::Refuse(Registered.Error);

            *Identity = Registered.Resolve();
        }
    }

    // 📐 The carousel's own travel. Registered here, never mid-tick.
    if (const Deliver<bool> Pages = StackPages.ConstructSlidingPages(Integrator, 0u, 260.0,
                                                                    EaseCurve::Standard);
        !Pages.Resolved)
        return Pages;

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const Deliver<std::uint32_t> Registered = Integrator.RegisterEased(1.0);
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        ExportMotion[Index] = Registered.Resolve();
    }

    Reapply(Resolved);

    return Deliver<bool>::Result(true);
}

void TexturingPanel::Reapply(const ThemeProfile& Resolved)
{
    Appearance = &Resolved;
    Tinted = Resolved.Shell;

    const float Applied = static_cast<float>(Resolved.Measure.DisplayScale)
                        * Resolved.ControlMeasure.ArtistFactor;

    Scaled = ScaleShellLengths(Applied);
}

void TexturingPanel::Reset()
{
    Controls.Reset();
    SharedControls.Reset();
    StackPages.Reset();
    StackFacets.Reset();
    ChannelFacets.Reset();
    MaskFacets.Reset();
    ExportOverflow.Reset();

    Interaction     = nullptr;
    Motion     = nullptr;
    Surface    = nullptr;
    Appearance = nullptr;
    Sampled    = {};
    Tinted     = {};
    Scaled     = {};
    RowTally   = 0u;
    MenuPresented = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE ADVANCE
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t TexturingPanel::PropertyTabCount(const TexturingContext& Applied,
                                                  const TextureLayerRow& Current) const
{
    // 📐 The tabs the selection offers: a folder offers the combined stack page alone; a taken mask
    //    offers the mask page alone; a layer offers Channels, its Mask (when declared), and its
    //    Settings (decal / pattern / generator / the generic layer settings).
    // 🔴 ONE SELECTION SHOWED TWO OR THREE PANELS. A layer offered Channels, Mask and
    //    Settings; a decal offered Decal and Settings. Taking a texture layer is a
    //    request to edit THAT layer's channels, and taking a mask is a request to
    //    edit THAT mask — the strip then asked a second question the selection had
    //    already answered, and the artist had to choose a tab to see the thing they
    //    had just clicked on.
    //
    //    One selection, one panel. The mask is reached by taking the mask ROW, which
    //    the stack already draws beneath its layer; the decal's settings are folded
    //    into the decal card rather than parked behind a tab of their own.
    static_cast<void>(Current);
    static_cast<void>(Applied);

    return 1u;
}

void TexturingPanel::Advance(const PointerCondition& Contact, double Elapsed,
                                TexturingContext& Applied,
                                const TextureLayerRow* Rows, std::uint32_t RowCount,
                                bool TabPressed, const ModifierCondition& Modifiers)
{
    Sampled = Contact;
    Modified = Modifiers;
    Controls.Advance(Contact, Elapsed);
    SharedControls.Sample(Contact);
    StackFacets.Advance(Contact, Elapsed);
    ChannelFacets.Advance(Contact, Elapsed);
    MaskFacets.Advance(Contact, Elapsed);

    // 📝 The search pill's taken state, for the host's typed-run feed.
    Applied.SearchTaken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);

    // 📐 Tab TOGGLES the carousel: from the stack to the properties — landing on the tab the
    //    selection names (Channels for a layer, Mask for a mask, Stack for a folder) — and back.
    //    The property tabs themselves are switched with the strip, never by Tab: one key, one
    //    travel, exactly as the user's flow describes.
    const std::uint32_t PriorPage = Applied.StackPage;
    if (TabPressed && Rows != nullptr && RowCount > 0u &&
        Applied.LayerTaken < RowCount)
    {
        if (Applied.StackPage == 0u)
        {
            Applied.StackPage   = 1u;
            Applied.PropertyTab = Applied.MaskTaken ? 1u : 0u;
        }
        else
        {
            Applied.StackPage = 0u;
        }
    }
    if (Applied.StackPage != PriorPage)
    {
        Interaction->Withdraw();
        Interaction->Abandon();
        Applied.MenuOpen = 0u;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE WHOLE LEAF
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::Record(const PlaneExtent& Extent, TexturingContext& Applied,
                               const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    if (Rows == nullptr)
        RowCount = 0u;

    if (RowCount > TextureLayerLimit)
        RowCount = TextureLayerLimit;

    RowTally = 0u;
    InlineControlsSpent = 0u;

    // 📐 The carousel: a three-page strip, translated by one whole extent. Page 0 the stack, page 1
    //    the selection-driven properties — the same slide the shell's inspector uses.
    //
    // 🔴 SCROLL ① — THE PAGE TRAVEL. This was a hard ternary on `StackPage`, so the
    //    slide the comment describes never happened: the carousel TELEPORTED between
    //    two frames. The user asked for the travel from the stack to a mask's
    //    properties and back to read as movement, and there was none to read. The
    //    page now departs on an eased interpolant and the strip is carried by the
    //    fraction, so the two pages genuinely slide past one another.
    StackPages.Navigate(Applied.StackPage);

    Surface->Confine(Extent);

    const PlaneExtent Leading  = StackPages.Page(Extent, 0u);
    const PlaneExtent Trailing = StackPages.Page(Extent, 1u);
    const PlaneExtent Flatten  = StackPages.Page(Extent, 2u);
    const PointerCondition LivePointer = Sampled;
    const auto SeatPagePointer = [&](std::uint32_t Page)
    {
        Sampled = LivePointer;
        if (StackPages.CurrentPage() != Page)
        {
            Sampled.PositionX = -1000000.0f;
            Sampled.PositionY = -1000000.0f;
            Sampled.ContactHeld = Sampled.ContactPressed = Sampled.ContactReleased = false;
            Sampled.ContactDoublePressed = false;
            Sampled.WheelY = 0.0f;
        }
    };

    if (!Surface->Excluded(Leading))
    {
        SeatPagePointer(0u);
        RecordStackPage(Leading, Applied, Rows, RowCount);
    }

    if (!Surface->Excluded(Trailing))
    {
        SeatPagePointer(1u);
        RecordPropertiesPage(Trailing, Applied, Rows, RowCount);
    }

    if (!Surface->Excluded(Flatten))
    {
        SeatPagePointer(2u);
        RecordFlattenPage(Flatten, Applied);
    }
    Sampled = LivePointer;

    Surface->Release();
    SharedControls.RecordDeferred();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLATTENED EXPORT
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordFlattenPage(const PlaneExtent& Extent, TexturingContext& Applied)
{
    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);
    const float Pad = Scaled.PanePad;
    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, SymbolSubject::LayerMerge, Tinted.Accent,
                     Applied.ExportMode == 0u ? "Export Flattened" : "Export Texture Set",
                     Applied.ExportMode == 0u ? "Non-destructive combined output" : "Channel-preserving output setup");

    const PlaneExtent Back = Spanning(Extent.MinimumX + Pad, Header.MaximumY + Pad, 82.0f, 28.0f);
    const bool OnBack = Back.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (Sampled.ContactPressed && OnBack)
    {
        Interaction->Withdraw();
        Applied.StackPage = 0u;
    }
    Interaction->DeclareHovered(ExportBack, OnBack, HoverOver);
    Surface->Ground(Back, OnBack ? Tinted.TileHovered : Tinted.Tile, 14.0f, CornerAll);
    Surface->Edge(Back, Tinted.HairlineFirm, 1.0f, 14.0f, CornerAll);
    Surface->TextRun(Back.MinimumX + 18.0f, Back.MinimumY + 7.0f, Tinted.Primary, "Back", Scaled.RunSecondary);

    const PlaneExtent ScrollViewport = { Extent.MinimumX, Back.MaximumY + 8.0f,
                                         Extent.MaximumX, Extent.MaximumY };
    constexpr float ExportContentHeight = 570.0f;
    const float PageScroll = ExportOverflow.Advance(Sampled, ScrollViewport, ExportContentHeight);
    Surface->Confine(ScrollViewport);

    static const char* const Formats[] =
        { "PNG", "JPEG", "TGA", "TIFF", "OpenEXR", "HDR", "WebP", "DDS", "KTX2" };
    static const char* const Resolutions[] = { "128", "256", "512", "1K", "2K", "4K", "8K", "16K" };
    static const char* const Presets[] =
        { "Generic PBR", "Unreal ORM", "Unity HDRP", "Unity URP", "glTF 2.0",
          "Godot 4", "Blender", "Maya / Arnold", "V-Ray" };

    const auto RecordRail = [&](std::uint32_t RailIndex, float Y, const char* Caption,
                                const char* const* Options, std::uint32_t OptionCount,
                                std::uint32_t& Taken)
    {
        if (Taken >= OptionCount) Taken = OptionCount - 1u;
        Surface->TextRun(Extent.MinimumX + Pad, Y, Tinted.Primary, Caption, Scaled.RunPrimary);
        Y += 28.0f;
        const PlaneExtent Left = Spanning(Extent.MinimumX + Pad, Y + 13.0f, 38.0f, 38.0f);
        const PlaneExtent Right = Spanning(Extent.MaximumX - Pad - 38.0f, Y + 13.0f, 38.0f, 38.0f);
        const PlaneExtent Rail = { Left.MaximumX + 12.0f, Y, Right.MinimumX - 12.0f, Y + 64.0f };
        const double Fraction = Motion->Eased(ExportMotion[RailIndex]).Current();
        const double Scroll = ExportFrom[RailIndex] +
                              (ExportTarget[RailIndex] - ExportFrom[RailIndex]) * Fraction;

        for (std::uint32_t Side = 0u; Side < 2u; ++Side)
        {
            const PlaneExtent Cell = Side == 0u ? Left : Right;
            const bool On = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Sampled.ContactPressed && On)
            {
                Interaction->Withdraw();
                if (Side == 0u && Taken > 0u) --Taken;
                if (Side == 1u && Taken + 1u < OptionCount) ++Taken;
                ExportFrom[RailIndex] = Scroll;
                ExportTarget[RailIndex] = static_cast<double>(Taken) * 144.0;
                Motion->Eased(ExportMotion[RailIndex]).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
            }
            Interaction->DeclareHovered(ExportArrows[RailIndex][Side], On, HoverOver);
            Surface->Ground(Cell, On ? Tinted.TileHovered : Tinted.Tile, 21.0f, CornerAll);
            Surface->Edge(Cell, Tinted.HairlineFirm, 1.0f, 21.0f, CornerAll);
            Surface->TextRun(Cell.MinimumX + 16.0f, Cell.MinimumY + 11.0f, Tinted.Primary,
                             Side == 0u ? "<" : ">", Scaled.RunPrimary);
        }

        Surface->Confine(Rail);
        for (std::uint32_t Index = 0u; Index < OptionCount; ++Index)
        {
            const PlaneExtent OptionTile = Spanning(Rail.MinimumX + 4.0f + Index * 144.0f - static_cast<float>(Scroll),
                                                Rail.MinimumY, 132.0f, 60.0f);
            const bool On = Rail.Encloses(Sampled.PositionX, Sampled.PositionY) &&
                            OptionTile.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Sampled.ContactPressed && On)
            {
                Interaction->Withdraw();
                Taken = Index;
                ExportFrom[RailIndex] = Scroll;
                ExportTarget[RailIndex] = static_cast<double>(Index) * 144.0;
                Motion->Eased(ExportMotion[RailIndex]).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
            }
            Interaction->DeclareHovered(ExportCarouselOptions[RailIndex][Index], On, HoverOver);
            const bool Selected = Taken == Index;
            Surface->Ground(OptionTile, Selected ? Tinted.RowTaken : (On ? Tinted.TileHovered : Tinted.Tile), 12.0f, CornerAll);
            Surface->Edge(OptionTile, Selected ? Tinted.Accent : Tinted.Hairline, 1.0f, 12.0f, CornerAll);
            Surface->TextRun(OptionTile.MinimumX + 14.0f, OptionTile.MinimumY + 10.0f, Tinted.Primary,
                             Options[Index], Scaled.RunPrimary);
            Surface->TextRun(OptionTile.MinimumX + 14.0f, OptionTile.MinimumY + 36.0f, Tinted.Muted,
                             RailIndex == 0u ? "Image format" :
                             RailIndex == 1u ? "Square output" : "Packing preset", Scaled.RunFine);
        }
        Surface->Release();
    };

    float Y = Back.MaximumY + 22.0f - PageScroll;
    RecordRail(0u, Y, "Output format", Formats, 9u, Applied.ExportFormat);
    Y += 96.0f;
    RecordRail(1u, Y, "Resolution", Resolutions, 8u, Applied.ExportResolution);
    Y += 96.0f;
    RecordRail(2u, Y, "DCC / engine preset", Presets, 9u, Applied.ExportPreset);
    Y += 96.0f;

    const auto Field = [&](std::uint32_t Index, const PlaneExtent& Row, const char* Label,
                           const char* Placeholder, char* Run, std::uint32_t Limit)
    {
        Surface->TextRun(Row.MinimumX, Row.MinimumY + 9.0f, Tinted.Muted, Label, Scaled.RunSecondary);
        EditableTextDeclaration Declared;
        Declared.Placeholder = Placeholder;
        SharedControls.EditableText(ExportFields[Index],
                                    Spanning(Row.MinimumX + 92.0f, Row.MinimumY, Row.Width() - 92.0f, 34.0f),
                                    Declared, Run, Limit);
    };

    const float Half = (Extent.Width() - Pad * 3.0f) * 0.5f;
    Field(0u, Spanning(Extent.MinimumX + Pad, Y, Half, 34.0f), "Name", "Export name",
          Applied.ExportName, 64u);
    Field(1u, Spanning(Extent.MinimumX + Pad * 2.0f + Half, Y, Half, 34.0f), "Tags", "tag, tag",
          Applied.ExportTags, 96u);
    Y += 42.0f;
    Field(2u, Spanning(Extent.MinimumX + Pad, Y, Extent.Width() - Pad * 2.0f, 34.0f),
          "Location", "Project/Textures", Applied.ExportLocation, 96u);
    Y += 48.0f;

    const char* Packing[] =
        { "Separate PBR maps", "AO:R  Rough:G  Metal:B", "Metal:R  AO:G  Detail:B  Smooth:A",
          "Metallic RGB + Smoothness A", "R:AO  G:Rough  B:Metal" };
    const std::uint32_t PackingIndex = Applied.ExportPreset < 5u ? Applied.ExportPreset : 0u;
    Surface->TextRun(Extent.MinimumX + Pad, Y, Tinted.Primary, "Packing", Scaled.RunSecondary);
    Surface->TextRun(Extent.MinimumX + Pad + 92.0f, Y, Tinted.Muted,
                     Packing[PackingIndex], Scaled.RunSecondary);
    Y += 30.0f;

    static const char* const Depths[3] = { "8-bit", "16-bit", "32-bit float" };
    Surface->TextRun(Extent.MinimumX + Pad, Y + 8.0f, Tinted.Muted, "Bit depth", Scaled.RunSecondary);
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent Cell = Spanning(Extent.MinimumX + Pad + 92.0f + Index * 116.0f, Y, 106.0f, 32.0f);
        const bool On = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && On && !Interaction->AnyDisclosed()) Interaction->Grab(ExportOptions[Index], ControlPart::Body);
        if (On && Interaction->Released(ExportOptions[Index])) Applied.ExportBitDepth = Index;
        Interaction->DeclareHovered(ExportOptions[Index], On, HoverOver);
        const bool Taken = Applied.ExportBitDepth == Index;
        Surface->Ground(Cell, Taken ? Tinted.RowTaken : (On ? Tinted.TileHovered : Tinted.Tile), 16.0f, CornerAll);
        Surface->Edge(Cell, Taken ? Tinted.Accent : Tinted.Hairline, 1.0f, 16.0f, CornerAll);
        Surface->TextRun(Cell.MinimumX + 14.0f, Cell.MinimumY + 8.0f, Tinted.Primary, Depths[Index], Scaled.RunFine);
    }
    Y += 42.0f;

    bool* Switches[3] = { &Applied.ExportDirectXNormals, &Applied.ExportDilation, &Applied.ExportBaseColourSrgb };
    const char* SwitchNames[3] = { "DirectX normals", "Infinite dilation", "sRGB base colour" };
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent Cell = Spanning(Extent.MinimumX + Pad + Index * 172.0f, Y, 162.0f, 30.0f);
        const bool On = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && On && !Interaction->AnyDisclosed()) Interaction->Grab(ExportOptions[3u + Index], ControlPart::Body);
        if (On && Interaction->Released(ExportOptions[3u + Index])) *Switches[Index] = !*Switches[Index];
        Interaction->DeclareHovered(ExportOptions[3u + Index], On, HoverOver);
        Surface->Ground(Cell, *Switches[Index] ? Tinted.RowTaken : Tinted.Tile, 15.0f, CornerAll);
        Surface->Edge(Cell, *Switches[Index] ? Tinted.Accent : Tinted.Hairline, 1.0f, 15.0f, CornerAll);
        Surface->TextRun(Cell.MinimumX + 12.0f, Cell.MinimumY + 7.0f, Tinted.Primary, SwitchNames[Index], Scaled.RunFine);
    }

    Surface->TextRun(Extent.MinimumX + Pad, Y + 42.0f, Tinted.Faint,
                     Applied.ExportMode == 0u ? "Non-destructive flattened copy; authored layers remain intact."
                                               : "Texture-set export; authored layers and channels remain intact.",
                     Scaled.RunFine);

    Surface->Release();
    const PlaneExtent Thumb = ExportOverflow.Thumb(ScrollViewport, ExportContentHeight);
    if (Thumb.Height() > 0.0f)
        Surface->Ground(Thumb, Tinted.HairlineFirm, 1.5f, CornerAll);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE STACK PAGE
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                                         const ThemeToken& Hue, const char* Titled,
                                         const char* Secondary)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MaximumY - 1.0f, Extent.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    const float Pad      = Scaled.HeaderPadX;
    const float Medallion = Scaled.MedallionExtent;

    const PlaneExtent Crest = Spanning(Extent.MinimumX + Pad,
                                       Extent.MinimumY + (Extent.Height() - Medallion) * 0.5f,
                                       Medallion, Medallion);

    Surface->Ground(Crest, Hue, 6.0f, CornerAll);

    const float Figure = Medallion * 0.62f;
    Surface->Stroke(Glyph,
                    Spanning(Crest.MinimumX + (Medallion - Figure) * 0.5f,
                             Crest.MinimumY + (Medallion - Figure) * 0.5f, Figure, Figure),
                    Covering(0xFFFFFFu));

    const float Run        = Scaled.RunPrimary;
    const float SecondaryRun = Scaled.RunFine;
    const float PairHeight = Run * 1.30f + SecondaryRun * 1.30f;
    const float PairLead   = Extent.MinimumY + (Extent.Height() - PairHeight) * 0.5f;
    const float RunLead    = Crest.MaximumX + Pad;

    Surface->TextRunTruncated(RunLead, PairLead, Extent.MaximumX - RunLead - Pad,
                              Tinted.Primary, Titled, Run, true);
    Surface->TextRunTruncated(RunLead, PairLead + Run * 1.30f,
                              Extent.MaximumX - RunLead - Pad, Hue, Secondary, SecondaryRun, false);
}

void TexturingPanel::RecordSearchPill(const PlaneExtent& Extent, TexturingContext& Applied)
{
    const bool Hovered = Extent.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Hovered && Sampled.ContactPressed && !Interaction->AnyDisclosed())
        Interaction->Grab(SearchField, ControlPart::Body);

    const bool Taken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);

    // 🔴 A pill: radius = half the height, so both ends are fully rounded.
    const float PillRadius = Extent.Height() * 0.5f;

    Surface->Ground(Extent, Tinted.MenuLower, PillRadius, CornerAll);
    Surface->Edge(Extent, Taken ? Faded(Tinted.Primary, 0.22f) : Tinted.Hairline,
                  1.0f, PillRadius, CornerAll);

    const float GlyphExtent = 13.0f;
    const float GlyphLead   = Extent.MinimumX + 10.0f;
    const float GlyphTop    = Extent.MinimumY + (Extent.Height() - GlyphExtent) * 0.5f;

    Surface->Stroke(SymbolSubject::MagnifierLens,
                    Spanning(GlyphLead, GlyphTop, GlyphExtent, GlyphExtent), Tinted.Faint);

    const float RunLead = GlyphLead + GlyphExtent + 8.0f;
    const float FieldRun = Scaled.RunSecondary;
    const float RunTop   = Extent.MinimumY + (Extent.Height() - FieldRun) * 0.5f;

    const bool Empty = Applied.Retention[0] == '\0';

    Surface->TextRunTruncated(RunLead, RunTop, Extent.MaximumX - RunLead - 8.0f,
                              Empty ? Tinted.Faint : Tinted.Primary,
                              Empty ? "Filter layers\u2026" : Applied.Retention, FieldRun);
}

/// 🧩 One pill item inside an open menu — grab, release and the write it performs.
/// note  📐 The items begin below the menu's title, exactly as the reference's `.pop h6` sits above
///        its buttons.
/// out   Writes  [-]  every item that resolved a release this tick is marked 1
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
void TexturingPanel::RecordMenuOptions(const PlaneExtent& Card, const char* const* Captions,
                                          const SymbolSubject* Glyphs, std::uint32_t OptionCount,
                                          const char* const* Shortcuts, ControlIdentity* Identities,
                                          TexturingContext& Applied, std::uint32_t* Writes,
                                          bool Interactive)
{
    const float Pad = Scaled.PanePad;
    const float RowY = Scaled.LayerToolHeight + 2.0f;
    const float OptionsTop = Card.MinimumY + Pad + 20.0f;

    for (std::uint32_t Index = 0u; Index < OptionCount; ++Index)
    {
        const PlaneExtent Cell = Spanning(Card.MinimumX + Pad,
                                          OptionsTop + RowY * static_cast<float>(Index),
                                          Card.Width() - Pad * 2.0f, RowY);

        const bool Hovered = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Interactive && Hovered && Sampled.ContactPressed)
            Interaction->Grab(Identities[Index], ControlPart::Body);

        if (Interactive && Hovered && Interaction->Released(Identities[Index]))
        {
            if (Writes != nullptr)
                Writes[Index] = 1u;

            Applied.MenuOpen = 0u;
            Interaction->Withdraw();
        }

        Interaction->DeclareHovered(Identities[Index], Hovered, HoverOver);

        if (Hovered)
            Surface->Ground(Cell, Faded(Tinted.Primary, 0.09f), RowY * 0.5f, CornerAll);

        const float GlyphExtent = 14.0f;

        if (Glyphs != nullptr)
        {
            Surface->Stroke(Glyphs[Index],
                            Spanning(Cell.MinimumX + 10.0f,
                                     Cell.MinimumY + (RowY - GlyphExtent) * 0.5f,
                                     GlyphExtent, GlyphExtent),
                            Hovered ? Tinted.Primary : Tinted.Muted);
        }

        const float Run = Scaled.RunSecondary;
        const float TextLead = Cell.MinimumX + (Glyphs != nullptr ? 32.0f : 12.0f);

        Surface->TextRun(TextLead, Cell.MinimumY + (RowY - Run) * 0.5f,
                         Hovered ? Tinted.Primary : Tinted.Muted,
                         Captions[Index], Run);

        if (Shortcuts != nullptr && Shortcuts[Index] != nullptr)
        {
            const float ShortcutRun = Scaled.RunFiner;
            const float Span = Surface->MeasureRun(Shortcuts[Index], ShortcutRun, 0.0f);

            Surface->TextRun(Cell.MaximumX - Pad - Span,
                             Cell.MinimumY + (RowY - ShortcutRun) * 0.5f,
                             Tinted.Faint, Shortcuts[Index], ShortcutRun);
        }
    }
}

void TexturingPanel::RecordStackPage(const PlaneExtent& Extent, TexturingContext& Applied,
                                        const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);

    const float Pad = Scaled.PanePad;

    // ① The tools begin directly beneath the editor's leaf header. The former count band repeated
    //    chrome the leaf already owns and spent a whole row on an unexplained layer/mask tally.
    const float ToolY = Scaled.LayerToolHeight;
    const float ToolBand = ToolY + Scaled.LayerFoldPad * 2.0f;
    const PlaneExtent Tools = Spanning(Extent.MinimumX, Extent.MinimumY,
                                       Extent.Width(), ToolBand);

    RecordStackTools(Tools, Applied);

    // ③ The stack's filter card — the SAME FacetPanel the scene directory carries, with the layer
    //    categories (the user's own requirement: the filters sit on both pages).
    const FacetDeclaration StackFacetCard =
    {
        "Filters", StackFacetOptions, StackFacetColours,
        TexturingContext::TextureFacetCount, 0xFFFFFFFFu
    };

    const float FacetY = StackFacets.MeasureHeight(Extent.Width() - Pad * 2.0f, StackFacetCard,
                                                   Applied.FacetEnabled);

    const PlaneExtent FacetCard = Spanning(Extent.MinimumX + Pad, Tools.MaximumY + Pad,
                                           Extent.Width() - Pad * 2.0f, FacetY);

    Discard(StackFacets.Record(FacetCard, StackFacetCard, Applied.FacetEnabled));

    // ④ 🔴 THE PAGE STRIP IS WITHDRAWN. "Stack | Properties" was asked to be removed
    //    once already and I left it standing. It was a third way to reach the page —
    //    beside Tab and beside taking a row, both of which travel there on their own —
    //    and it spent a whole 31 px band restating navigation the panel already has.
    //    The carousel is unchanged; only this strip is gone.
    const PlaneExtent Strip = Spanning(Extent.MinimumX,
                                       Extent.MaximumY - Scaled.LayerFootCrumb - Scaled.LayerFootProp
                                       - Scaled.LayerFootBar,
                                       Extent.Width(), 0.0f);

    // ⑤ The reference's footer: crumb, blend + opacity, the action bar.
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Strip.MaximumY,
                                        Extent.Width(),
                                        Scaled.LayerFootCrumb + Scaled.LayerFootProp
                                        + Scaled.LayerFootBar);

    RecordStackFooter(Footer, Applied, Rows, RowCount);

    // ⑥ The rows band.
    const PlaneExtent Body = Spanning(Extent.MinimumX + 10.0f, FacetCard.MaximumY + Pad,
                                      Extent.Width() - 20.0f,
                                      Strip.MinimumY - FacetCard.MaximumY - Pad);

    if (Body.MaximumY > Body.MinimumY)
    {
        Surface->Confine(Body);

        const bool Filtering = RetentionActive(Applied);

        // 📐 Measure the stack before drawing it. A folder's contents appear on
        //    disclosure, so the list's height is a function of what is unfolded and
        //    has to be re-measured every tick rather than cached.
        float Content = 0.0f;
        float CardOpen[TextureLayerLimit] = {};

        for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        {
            if (Filtering && !RowRetained(Applied, Rows[Index]))
                continue;

            // 📐 An enclosed row's height is weighted by how far its folder has
            //    opened, so the list's measure follows the fold rather than jumping
            //    the moment the boolean flips.
            const float Folded = EnclosureFraction(Applied, Rows, RowCount, Index);

            if (Folded <= 0.0f)
                continue;

            CardOpen[Index] = Controls.OutlineExpansion(LayerDetails[Index],
                                                          Applied.LayerCardExpanded[Index], true);

            const PlaneExtent Measuring = Spanning(Body.MinimumX, 0.0f, Body.Width(), 0.0f);
            const float CardHeight = RecordInlineLayerCard(Measuring, Applied, Rows[Index],
                                                           Index, false);

            Content += (Scaled.LayerRowY + 2.0f
                     + (Applied.MaskAttached[Index] ? (Scaled.LayerMaskY + 2.0f) : 0.0f)
                     + (CardHeight + 2.0f) * CardOpen[Index]) * Folded;
        }

        const float Rolled = AdvanceListScroll(Applied.StackListShown, Applied.StackListWanted,
                                               Content, Body.Height(), Body);

        float Sweep = Body.MinimumY - Rolled;

        const std::uint32_t PriorDragDestination = Applied.DragDestination;
        const std::uint32_t PriorDragPlacement = Applied.DragPlacement;
        const bool Carrying = Applied.DragSource < RowCount;
        const float DragTravel = Carrying
            ? std::abs(Sampled.PositionY - Applied.DragOriginY) : 0.0f;
        const bool Dragging = Carrying && Sampled.ContactHeld && DragTravel >= 5.0f;
        Applied.DragDestination = TextureLayerLimit;
        Applied.DragPlacement = 0u;

        std::uint32_t Depths[TextureLayerLimit] = {};
        for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
            Depths[Index] = Rows[Index].Depth;

        for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        {
            if (Filtering && !RowRetained(Applied, Rows[Index]))
                continue;

            // 🔴 A FOLDER SNAPPED OPEN AND SHUT. Disclosure was a bare boolean read at
            //    the top of the loop, so a folder's contents appeared and vanished
            //    between two frames with no travel — while the channel cards beside
            //    them animate their fold on OutlineExpansion. The enclosed rows now
            //    ride the same eased fraction: they grow in height and fade in as the
            //    folder opens.
            const float Folded = EnclosureFraction(Applied, Rows, RowCount, Index);

            if (Folded <= 0.001f)
                continue;

            const TextureLayerRow& Current = Rows[Index];

            // 📐 The row's height, plus the attached mask row beneath when one stands.
            const float MaskY = Applied.MaskAttached[Index]
                              ? (Scaled.LayerMaskY + 2.0f) : 0.0f;
            const float RowY = (Scaled.LayerRowY + MaskY) * Folded;

            // 🔴 NESTED ROWS WERE NOT INDENTED. `LayerKidsX` (15 px, the reference's
            //    `.kids>.kin{margin-left:15px}`) has sat in ShellMetric unused since the
            //    port: every row was drawn at Body.MinimumX whatever its depth, so a
            //    folder's contents sat flush with the folder and the tree read as a flat
            //    list. LayerstackV1.html indents each level and draws a hairline down the
            //    children's gutter; both are here now.
            const float Indent = Scaled.LayerKidsX * static_cast<float>(Current.Depth);

            const PlaneExtent Row = Spanning(Body.MinimumX + Indent, Sweep,
                                             Body.Width() - Indent, RowY);

            if (Dragging && Row.Encloses(Sampled.PositionX, Sampled.PositionY) &&
                Body.Encloses(Sampled.PositionX, Sampled.PositionY) &&
                TextureStackPolicy::AllowsPlacement(Applied.DragSource, Index, Depths, RowCount))
            {
                const float Fraction = (Sampled.PositionY - Row.MinimumY) / Row.Height();
                Applied.DragDestination = Index;
                Applied.DragPlacement = (Current.Classified == TextureLayerClassification::Folder &&
                                         Fraction > 0.30f && Fraction < 0.70f)
                                      ? 3u : (Fraction < 0.5f ? 1u : 2u);
            }

            // 📐 The reference's `.kids>.kin::before` — a 1 px rail down the gutter each
            //    nested row sits in, in the enclosing folder's own hue at 0.32.
            for (std::uint32_t Level = 0u; Level < Current.Depth; ++Level)
            {
                const std::uint32_t Owner = Current.Enclosing < RowCount
                                          ? Applied.LayerTagHue[Current.Enclosing] : 0x444444u;

                Surface->Ground(Spanning(Body.MinimumX + Scaled.LayerKidsX
                                         * static_cast<float>(Level) + 6.0f,
                                         Sweep, 1.0f, RowY),
                                Faded(Covering(Owner != 0u ? Owner : 0x444444u), 0.32f),
                                0.0f, CornerNone);
            }

            Sweep += RowY + 2.0f;

            if (Surface->Excluded(Row))
            {
                if (CardOpen[Index] > 0.001f)
                {
                    const PlaneExtent Measuring = Spanning(Row.MinimumX, Sweep, Row.Width(), 0.0f);
                    const float Full = RecordInlineLayerCard(Measuring, Applied, Current, Index, false);
                    const float Open = Full * CardOpen[Index] * Folded;
                    const PlaneExtent Card = Spanning(Row.MinimumX, Sweep, Row.Width(), Open);

                    if (!Surface->Excluded(Card))
                    {
                        Surface->Confine(Card);
                        RecordInlineLayerCard(Spanning(Card.MinimumX, Card.MinimumY,
                                                       Card.Width(), Full),
                                              Applied, Current, Index, true);
                        Surface->Release();
                    }

                    Sweep += Open + 2.0f;
                }

                continue;
            }

            if (RowTally < TextureLayerLimit)
            {
                RowRects[RowTally] = Row;
                ++RowTally;
            }

            RecordStackRow(Row, Applied, Rows, RowCount, Current, Index);

            if (Applied.DragDestination == Index)
            {
                if (Applied.DragPlacement == 3u)
                    Surface->Edge(Row, Tinted.Accent, 2.0f, Scaled.FieldRadius, CornerAll);
                else
                {
                    const float MarkY = Applied.DragPlacement == 1u ? Row.MinimumY : Row.MaximumY - 2.0f;
                    Surface->Ground(Spanning(Row.MinimumX, MarkY, Row.Width(), 2.0f),
                                    Tinted.Accent, 1.0f, CornerAll);
                }
            }

            if (Applied.MaskAttached[Index])
            {
                // 🔴 THE MASK ROW IGNORED BOTH INDENTS. It was placed at
                //    `Body.MinimumX` with the body's full width, so it neither
                //    followed its layer into a folder — a mask on a nested layer sat
                //    flush with the top level while its own layer was inset — nor
                //    carried the reference's own `.attach{padding-left:26px}`, which
                //    is what sets a mask in from the entry it hangs off. It takes the
                //    parent's indent AND the attach inset now, so it always reads as
                //    belonging to the row directly above it.
                const PlaneExtent Mask = Spanning(Row.MinimumX + Scaled.LayerMaskIndent,
                                                  Row.MinimumY + Scaled.LayerRowY,
                                                  Row.Width() - Scaled.LayerMaskIndent,
                                                  Scaled.LayerMaskY);
                RecordMaskRow(Mask, Applied, Rows, RowCount, Current, Index);
            }

            // 📐 Independent of the leftward page carousel: the trailing V discloses this card in the
            //    stack itself. The full body is recorded and clipped to the eased opening, so it reads
            //    as a dropdown rather than as navigation to Channels / Mask / Properties.
            if (CardOpen[Index] > 0.001f)
            {
                const PlaneExtent Measuring = Spanning(Row.MinimumX, Sweep, Row.Width(), 0.0f);
                const float Full = RecordInlineLayerCard(Measuring, Applied, Current, Index, false);
                const float Open = Full * CardOpen[Index] * Folded;
                const PlaneExtent Card = Spanning(Row.MinimumX, Sweep, Row.Width(), Open);

                if (!Surface->Excluded(Card))
                {
                    Surface->Confine(Card);
                    RecordInlineLayerCard(Spanning(Card.MinimumX, Card.MinimumY,
                                                   Card.Width(), Full),
                                          Applied, Current, Index, true);
                    Surface->Release();
                }

                Sweep += Open + 2.0f;
            }
        }

        if (Carrying && !Sampled.ContactHeld)
        {
            if (PriorDragDestination < RowCount && PriorDragPlacement != 0u)
            {
                Applied.DragDestination = PriorDragDestination;
                Applied.DragPlacement = PriorDragPlacement;
                Applied.Structural = static_cast<std::uint32_t>(TexturingRequest::Relocate);
            }
            else
            {
                Applied.DragSource = TextureLayerLimit;
                Applied.DragDestination = TextureLayerLimit;
                Applied.DragPlacement = 0u;
            }
        }

        if (Filtering && Sweep <= Body.MinimumY - Rolled + 0.5f)
        {
            const float Run = Scaled.RunSecondary;
            const char* Prose = "No layers match the search or filters.";

            Surface->TextRun(Body.MinimumX + (Body.Width()
                                              - Surface->MeasureRun(Prose, Run, 0.0f)) * 0.5f,
                             Body.MinimumY + Scaled.PanePad * 2.0f, Tinted.Faint, Prose, Run);
        }

        Surface->Release();

        RecordScrollThumb(Body, Content, Rolled);
    }

    // ⑦ The open menu, recorded last so it draws above the whole page.
    RecordMenu(Extent, Applied, Rows, RowCount);

    // 🔴 The filter card's dropdown is deferred, exactly as the scene directory's is.
    StackFacets.RecordDeferred();
}

// 🧩 How far the folder enclosing one row has opened — 1 for a top-level row, 0
//    for a row inside a shut folder, and the eased fraction in between. Ancestors
//    compound, so a row two folders deep is hidden while EITHER is shut.
float TexturingPanel::EnclosureFraction(const TexturingContext& Applied,
                                           const TextureLayerRow* Rows,
                                           std::uint32_t RowCount, std::uint32_t Index)
{
    if (Rows == nullptr || Index >= RowCount)
        return 1.0f;

    std::uint32_t Parents[TextureLayerLimit] = {};
    float Expansion[TextureLayerLimit] = {};
    for (std::uint32_t Candidate = 0u; Candidate < RowCount; ++Candidate)
    {
        Parents[Candidate] = Rows[Candidate].Enclosing;
        Expansion[Candidate] = Rows[Candidate].EnclosedCount > 0u
                             ? Controls.OutlineExpansion(LayerChevrons[Candidate],
                                                         Applied.LayerExpanded[Candidate], true)
                             : 1.0f;
    }

    return VisibleTree::AncestorOccupancy(Parents, Expansion, RowCount, Index);
}

void TexturingPanel::RecordStackTools(const PlaneExtent& Tools, TexturingContext& Applied)
{
    const float ToolY = Scaled.LayerToolHeight;
    const float Top = Tools.MinimumY + (Tools.Height() - ToolY) * 0.5f;

    // 📐 The search pill fills everything the three tools leave.
    const float IconSpan = ToolY * 3.0f + 10.0f * 2.0f + 6.0f;
    const PlaneExtent Search = Spanning(Tools.MinimumX + Scaled.PanePad, Top,
                                        Tools.Width() - Scaled.PanePad * 2.0f - IconSpan - 1.0f,
                                        ToolY);

    RecordSearchPill(Search, Applied);

    // 📐 The separator and the three tools: folder, mask, collapse.
    const float VSepY = Top + (ToolY - 17.0f) * 0.5f;
    Surface->Ground(Spanning(Search.MaximumX + 6.0f, VSepY, 1.0f, 17.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    const float Gap = 5.0f;
    float Lead = Search.MaximumX + 13.0f;

    struct ToolCell
    {
        ControlIdentity* Target;
        SymbolSubject    Glyph;
        std::uint32_t    Request;
    };

    const ToolCell ToolsDeclared[3] =
    {
        { &ToolFolder,   SymbolSubject::FolderClosed,   static_cast<std::uint32_t>(TexturingRequest::AddFolder) },
        { &ToolMask,     SymbolSubject::HalfMask,       0x80000000u },
        { &ToolCollapse, SymbolSubject::CollapseFold,   0u }
    };

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent Cell = Spanning(Lead, Top, ToolY, ToolY);
        const bool Hovered = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);

        const bool MaskToolOn = (Index == 1u) && Applied.LayerTaken < TextureLayerLimit &&
                                Applied.MaskAttached[Applied.LayerTaken];

        if (Hovered)
            Surface->Ground(Cell, Faded(Tinted.Primary, 0.08f), Scaled.LayerRadius, CornerAll);

        const float Figure = 15.0f;

        Surface->Stroke(ToolsDeclared[Index].Glyph,
                        Spanning(Cell.MinimumX + (ToolY - Figure) * 0.5f,
                                 Cell.MinimumY + (ToolY - Figure) * 0.5f, Figure, Figure),
                        MaskToolOn ? Tinted.Primary
                                   : (Hovered ? Tinted.Primary : Tinted.Muted));

        if (Sampled.ContactPressed && Hovered && !Interaction->AnyDisclosed())
            Interaction->Grab(*ToolsDeclared[Index].Target, ControlPart::Body);

        if (Hovered && Interaction->Released(*ToolsDeclared[Index].Target))
        {
            if (Index == 1u)
            {
                // 📐 The mask tool toggles the taken row's attached mask — the reference's `btnMask`.
                if (Applied.LayerTaken < TextureLayerLimit)
                {
                    const std::uint32_t Taken = Applied.LayerTaken;
                    Applied.MaskAttached[Taken] = !Applied.MaskAttached[Taken];

                    if (Applied.MaskAttached[Taken])
                    {
                        Applied.MaskVisible[Taken] = true;
                        Applied.MaskTaken = true;
                    }
                    else
                    {
                        Applied.MaskTaken = false;
                    }
                }
            }
            else if (Index == 2u)
            {
                // 📐 Collapse / expand all folders — the reference's `btnCollapse`.
                bool AnyOpen = false;

                for (std::uint32_t Row = 0u; Row < TextureLayerLimit; ++Row)
                {
                    if (Applied.LayerExpanded[Row])
                    {
                        AnyOpen = true;
                        break;
                    }
                }

                const bool Open = !AnyOpen;

                for (std::uint32_t Row = 0u; Row < TextureLayerLimit; ++Row)
                    Applied.LayerExpanded[Row] = Open;
            }
            else
            {
                Applied.Structural = ToolsDeclared[Index].Request;
            }
        }

        Lead += ToolY + Gap;
    }
}

void TexturingPanel::RecordStackRow(const PlaneExtent& Row, TexturingContext& Applied,
                                       const TextureLayerRow* Rows, std::uint32_t RowCount,
                                       const TextureLayerRow& Current, std::uint32_t Index)
{
    const bool Taken   = Applied.LayerSelected[Index];
    // 🔴 The layer row's own hover excludes the mask strip beneath it: the row's extent carries the
    //    attached mask row too, and a click there must address the MASK, never the layer — the
    //    reported defect where the mask row could not be taken.
    const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY) &&
                         !(Applied.MaskAttached[Index] &&
                           Sampled.PositionY >= Row.MinimumY + Scaled.LayerRowY);
    const bool Absent  = !Applied.LayerPresent[Index];
    const bool Branch  = Current.EnclosedCount > 0u;
    const bool SoloDim = !RowInSolo(Applied, Rows, RowCount, Index);

    // 📐 The row's geometry: the entry tag, then the chevron, the eye, the thumb, the meta, the
    //    chips and the details + more cells — the reference's `.row` order.
    const float LeadX = Row.MinimumX + Scaled.RowLeadX
                      + static_cast<float>(Current.Depth) * Scaled.RowStepX;

    const float ChevronY = Row.MinimumY + (Scaled.LayerRowY - Scaled.ChevronExtent) * 0.5f;

    const PlaneExtent Chevron = Spanning(LeadX, ChevronY,
                                         Scaled.ChevronExtent, Scaled.ChevronExtent);

    const float EyeExtent = Scaled.LayerToolHeight - 5.0f;
    const float EyeLead = Chevron.MaximumX + (Branch ? 8.0f : 0.0f) + 8.0f;
    const PlaneExtent Eye = Spanning(EyeLead,
                                     Row.MinimumY + (Scaled.LayerRowY - EyeExtent) * 0.5f,
                                     EyeExtent, EyeExtent);

    const float ThumbExtent = Scaled.LayerThumbY;
    const PlaneExtent Thumb = Spanning(Eye.MaximumX + 8.0f,
                                       Row.MinimumY + (Scaled.LayerRowY - ThumbExtent) * 0.5f,
                                       ThumbExtent, ThumbExtent);

    const float MetaLead = Thumb.MaximumX + Scaled.PanePad;
    const float RightGrip = Row.MaximumX - 14.0f;

    const PlaneExtent More = Spanning(RightGrip - EyeExtent,
                                      Row.MinimumY + (Scaled.LayerRowY - EyeExtent) * 0.5f,
                                      EyeExtent, EyeExtent);
    const PlaneExtent Details = Spanning(More.MinimumX - 4.0f - EyeExtent,
                                         More.MinimumY, EyeExtent, EyeExtent);

    const float RowCoverage = (Absent ? 0.34f : 1.0f) * (SoloDim ? 0.30f : 1.0f);

    const bool OnChevron = Branch && Chevron.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool OnEye     = Eye.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool OnDetails = Details.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool OnMore    = More.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Sampled.ContactPressed && !Interaction->AnyDisclosed())
    {
        if (OnChevron)
            Interaction->Grab(LayerChevrons[Index], ControlPart::Chevron);
        else if (OnEye)
            Interaction->Grab(LayerEyes[Index], ControlPart::Body);
        else if (OnDetails)
            Interaction->Grab(LayerDetails[Index], ControlPart::Body);
        else if (OnMore)
            Interaction->Grab(LayerMores[Index], ControlPart::Body);
        else if (Hovered)
        {
            Interaction->Grab(LayerContacts[Index], ControlPart::Body);
            if (!Applied.LayerLocked[Index])
            {
                Applied.DragSource = Index;
                Applied.DragDestination = TextureLayerLimit;
                Applied.DragPlacement = 0u;
                Applied.DragOriginY = Sampled.PositionY;
            }
        }
    }

    if (OnChevron && Interaction->Released(LayerChevrons[Index]))
        Applied.LayerExpanded[Index] = !Applied.LayerExpanded[Index];

    if (OnEye && Interaction->Released(LayerEyes[Index]))
        Applied.LayerPresent[Index] = !Applied.LayerPresent[Index];

    // 📐 The V is disclosure, not navigation. It opens the entry's card down inside the stack while
    //    the page carousel remains where it stands.
    if (OnDetails && Interaction->Released(LayerDetails[Index]))
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken = false;
        const bool Incoming = !Applied.LayerCardExpanded[Index];

        if (Incoming)
            for (std::uint32_t Other = 0u; Other < TextureLayerLimit; ++Other)
                Applied.LayerCardExpanded[Other] = false;

        Applied.LayerCardExpanded[Index] = Incoming;
    }

    // 📐 A double contact on the row is the pointer equivalent of Tab: it selects the layer and sends
    //    the carousel left to Channels / Mask / Properties. This path deliberately excludes every
    //    row action, especially the independent disclosure V above.
    if (Sampled.ContactDoublePressed && Hovered && !OnChevron && !OnEye && !OnDetails && !OnMore)
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken = false;
        Applied.StackPage = 1u;
        Applied.PropertyTab = 0u;
    }

    // 📐 The more button opens the layer menu — the reference's `layerMenu`.
    if (OnMore && Interaction->Released(LayerMores[Index]))
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken  = false;
        Applied.MenuOpen   = 2u;
        Applied.MenuRow    = Index;
        Interaction->Disclose(MenuLayer);
        MenuAnchorExtent = More;
    }

    if (Hovered && !OnChevron && !OnEye && !OnDetails && !OnMore &&
        Interaction->Released(LayerContacts[Index]))
    {
        bool Presented[TextureLayerLimit] = {};
        for (std::uint32_t Candidate = 0u; Candidate < RowCount; ++Candidate)
        {
            Presented[Candidate] = !RetentionActive(Applied) || RowRetained(Applied, Rows[Candidate]);
            std::uint32_t Parent = Rows[Candidate].Enclosing;
            std::uint32_t Guard = 0u;
            while (Presented[Candidate] && Parent < RowCount && Guard++ < TextureLayerLimit)
            {
                Presented[Candidate] = Applied.LayerExpanded[Parent];
                Parent = Rows[Parent].Enclosing;
            }
        }
        SelectionSet::Apply(Applied.LayerSelected, RowCount, Applied.LayerSelectionAnchor,
                            Index, Presented,
                            SelectionGesture{ Modified.Shifted, Modified.Commanded });

        Applied.LayerTaken = SelectionSet::Primary(Applied.LayerSelected, RowCount, Index);
        Applied.MaskTaken  = false;
    }

    Interaction->DeclareHovered(LayerContacts[Index], Hovered, HoverOver);

    // ② The entry tag — the reference's `.tag`: a solid 3 px colour rail, dimmed to
    //    0.3 while the row is withheld (`.entry.off .tag`).
    const std::uint32_t TagHue = Applied.LayerTagHue[Index] != 0u
                               ? Applied.LayerTagHue[Index] : Current.TagHue;

    // 🔴 THE WRONG ENTRY WAS DOTTED. LayerstackV1.html gives every layer entry a
    //    plain `<span class="tag">` and reserves `class="tag dot"` for the ATTACHED
    //    MASK entry alone:
    //
    //        <div class="entry" …><span class="tag"></span>            ← layer, solid
    //        <div class="attach"><div class="entry" …>
    //            <span class="tag dot"></span>                          ← mask, dotted
    //
    //    This dotted the LAYER's rail whenever the layer happened to carry a mask,
    //    so a masked layer lost its solid rail and the stack grew a column of broken
    //    bars the reference does not have. The dotting says "this row is the mask",
    //    not "this row has one" — and the mask row draws its own rail directly
    //    beneath, which is where the reader is meant to see it.
    //
    //    🟡 And my previous pass made it worse rather than better: told the dots
    //       looked wrong, I retuned them to 2 px on / 3 px off without re-reading
    //       the sheet. The rhythm was never the defect — 3 px on / 4 px off is
    //       exactly what `repeating-linear-gradient(180deg, var(--c) 0 3px,
    //       transparent 3px 7px)` states. Guessing at a figure the reference
    //       already gives is how a port drifts.
    Surface->Ground(Spanning(Row.MinimumX, Row.MinimumY, Scaled.LayerTagX, Scaled.LayerRowY),
                    Faded(Covering(TagHue), Absent ? 0.3f : 1.0f), 0.0f, CornerNone);

    // ③ The row ground — the reference's `#0d0d0d` row, its hover, and the selected pose.
    if (Taken)
    {
        Surface->Ground(Spanning(Row.MinimumX + Scaled.LayerTagX, Row.MinimumY,
                                 Row.Width() - Scaled.LayerTagX, Scaled.LayerRowY),
                        Tinted.RowTaken, 0.0f, CornerNone);
        Surface->Edge(Spanning(Row.MinimumX + Scaled.LayerTagX, Row.MinimumY,
                               Row.Width() - Scaled.LayerTagX, Scaled.LayerRowY),
                      Faded(Tinted.Primary, 0.18f), 1.0f, 0.0f, CornerNone);
    }
    else if (Hovered)
    {
        Surface->Ground(Spanning(Row.MinimumX + Scaled.LayerTagX, Row.MinimumY,
                                 Row.Width() - Scaled.LayerTagX, Scaled.LayerRowY),
                        Covering(0x161616u), 0.0f, CornerNone);
    }
    else
    {
        Surface->Ground(Spanning(Row.MinimumX + Scaled.LayerTagX, Row.MinimumY,
                                 Row.Width() - Scaled.LayerTagX, Scaled.LayerRowY),
                        Covering(0x0D0D0Du), 0.0f, CornerNone);
    }

    // ④ The disclosure chevron (folders only — the reference's `.tw.void` is skipped).
    if (Branch)
    {
        const bool Open = Applied.LayerExpanded[Index];

        Surface->Stroke(Open ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                        Chevron, Faded(OnChevron ? Tinted.Primary : Tinted.Faint,
                                       RowCoverage));
    }

    // ⑤ The eye — always standing, exactly as the reference's `.eye` carries its 0.55 opacity.
    if (OnEye)
        Surface->Ground(Eye, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(Absent ? SymbolSubject::EyeClosed : SymbolSubject::EyeOpen, Eye,
                    Faded(OnEye ? Tinted.Primary : Faded(Tinted.Primary, Absent ? 0.4f : 0.55f),
                          RowCoverage));

    // ⑥ The square thumb: checker + folder glyph for folders, the hue wash + type badge for layers.
    const float Checker = ThumbExtent * 0.25f;

    for (std::uint32_t RowStep = 0u; RowStep < 4u; ++RowStep)
    {
        for (std::uint32_t Column = 0u; Column < 4u; ++Column)
        {
            const bool Light = ((RowStep + Column) & 1u) == 0u;

            Surface->Ground(Spanning(Thumb.MinimumX + Checker * static_cast<float>(Column),
                                     Thumb.MinimumY + Checker * static_cast<float>(RowStep),
                                     Checker + 0.5f, Checker + 0.5f),
                            Faded(Covering(Light ? 0x1C1C1Cu : 0x101010u), RowCoverage),
                            0.0f, CornerNone);
        }
    }

    Surface->Edge(Thumb, Faded(Tinted.Primary, 0.15f * RowCoverage), 1.0f, 0.0f, CornerAll);

    if (Current.Classified == TextureLayerClassification::Folder)
    {
        const float Glyph = 16.0f;
        Surface->Stroke(SymbolSubject::FolderClosed,
                        Spanning(Thumb.MinimumX + (ThumbExtent - Glyph) * 0.5f,
                                 Thumb.MinimumY + (ThumbExtent - Glyph) * 0.5f, Glyph, Glyph),
                        Faded(Covering(TagHue), RowCoverage));
    }
    else
    {
        // 📐 The hue wash over the checker, then the badge with the type glyph — the reference's
        //    texture disc + `badge`.
        Surface->Ground(Thumb, Faded(Covering(TagHue), 0.30f * RowCoverage), 0.0f, CornerNone);

        // 🔴 `Thumb.MaximumY + ThumbExtent` added a SIZE to a COORDINATE that was
        //    already the thumbnail's bottom edge, so the type badge was placed a
        //    whole thumbnail below the row it belongs to — the loose icon floating
        //    under the last entry. `Thumb` is an extent, not an origin: its trailing
        //    edge is MaximumY, and the badge is inset up from it like every other
        //    corner-pinned mark in this panel.
        const float BadgeExtent = Scaled.LayerBadgeY;
        const PlaneExtent Badge = Spanning(Thumb.MaximumX - BadgeExtent - 3.0f,
                                           Thumb.MaximumY - BadgeExtent - 3.0f,
                                           BadgeExtent, BadgeExtent);

        Surface->Ground(Badge, Faded(Covering(0x000000u), RowCoverage), Scaled.LayerRadius * 0.5f,
                        CornerAll);
        Surface->Edge(Badge, Faded(Tinted.Primary, 0.18f), 1.0f,
                      Scaled.LayerRadius * 0.5f, CornerAll);

        const float Figure = BadgeExtent * 0.62f;
        Surface->Stroke(TextureLayerGlyph(Current.Classified),
                        Spanning(Badge.MinimumX + (BadgeExtent - Figure) * 0.5f,
                                 Badge.MinimumY + (BadgeExtent - Figure) * 0.5f, Figure, Figure),
                        Faded(Tinted.Muted, RowCoverage));
    }

    // ⑦ The meta: name + the sub run.
    const float NamingRun  = Scaled.RunPrimary;
    const float NamingTop  = Row.MinimumY + (Scaled.LayerRowY * 0.5f - NamingRun * 1.3f) * 0.5f;
    const float NamingLimit = Details.MinimumX - Scaled.PanePad;

    Surface->TextRunTruncated(MetaLead, NamingTop, NamingLimit,
                              Faded(Taken ? Tinted.Primary : Tinted.Muted, RowCoverage),
                              Current.Naming, NamingRun, Taken);

    const float SubRun = Scaled.RunFine;
    const float SubTop = NamingTop + NamingRun * 1.3f;

    char Sub[96] = {};
    const bool IsFolder = Current.Classified == TextureLayerClassification::Folder;

    // 🔴 The sub-line used to restate the blend and the opacity, both of which
    //    the details panel already owns as their own rows, and both of which the
    //    wide columns draw again beside it. Blend appeared twice and opacity
    //    three times on one row.
    //    The stack answers "what is this and what is inside it"; the inspector
    //    answers "how is it set". So the sub-line now carries only what the row
    //    itself cannot: the classification, a folder's tally, and the resolution
    //    or source that identifies the content.
    if (IsFolder)
    {
        std::snprintf(Sub, sizeof(Sub), "%u items", Current.EnclosedCount);
    }
    else if (Current.Source[0] != '\0')
    {
        std::snprintf(Sub, sizeof(Sub), "%s \u00B7 %s",
                      TextureLayerText(Current.Classified), Current.Source);
    }
    else if (Current.Detail[0] != '\0')
    {
        std::snprintf(Sub, sizeof(Sub), "%s \u00B7 %s",
                      TextureLayerText(Current.Classified), Current.Detail);
    }
    else
    {
        std::snprintf(Sub, sizeof(Sub), "%s", TextureLayerText(Current.Classified));
    }

    Surface->TextRunTruncated(MetaLead, SubTop, NamingLimit,
                              Faded(Tinted.Faint, RowCoverage), Sub, SubRun);

    // ⑧ The opacity column.
    // 🔴 The opacity used to appear three times over — in the sub-line, as a
    //    mini-bar, and as a value — and only when WideRows was toggled, so the
    //    one figure an artist reads constantly was the one that could vanish.
    //    A folder has no opacity of its own, so it states none.
    float ChipLimit = Details.MinimumX - 8.0f;

    if (!IsFolder && NamingLimit - MetaLead > 160.0f)
    {
        const std::uint32_t Amount = Applied.LayerOpacity[Index];

        char Value[8] = {};
        std::snprintf(Value, sizeof(Value), "%u%%", Amount);

        const float ValueSpan = Surface->MeasureRun(Value, SubRun, 0.0f);
        const float ValueLead = ChipLimit - ValueSpan;

        Surface->TextRun(ValueLead, Row.MinimumY + (Scaled.LayerRowY - SubRun) * 0.5f,
                         Faded(Taken ? Tinted.Primary : Tinted.Muted, RowCoverage),
                         Value, SubRun, 0.0f, true);

        ChipLimit = ValueLead - 10.0f;

        // 📐 The blend run stays a wide-only column: it is a setting rather than
        //    a reading, and the inspector states it in full.
        if (Applied.WideRows && NamingLimit - MetaLead > 300.0f)
        {
            const char* Blend = TextureBlendNames[Applied.LayerBlendTaken[Index] % TextureBlendCount];
            const float BlendSpan = Surface->MeasureRun(Blend, SubRun, 0.0f);

            Surface->TextRun(ChipLimit - BlendSpan,
                             Row.MinimumY + (Scaled.LayerRowY - SubRun) * 0.5f,
                             Faded(Tinted.Faint, RowCoverage), Blend, SubRun, 0.0f, true);

            ChipLimit = ChipLimit - BlendSpan - 10.0f;
        }
    }

    // ⑨ The chips, right-to-left: CH, FX, MASK, L, 3D — the reference's `.chips`.
    const float ChipRun = Scaled.RunFiner;
    const float ChipH = Scaled.LayerChipY;
    float ChipX = ChipLimit;

    const auto DrawChip = [&](const char* Text, std::uint32_t Background, std::uint32_t Foreground,
                              const ThemeToken& Border) -> float
    {
        const float Span = Surface->MeasureRun(Text, ChipRun, 0.0f) + 14.0f;

        if (ChipX - Span >= MetaLead + 40.0f)
        {
            const PlaneExtent Chip = Spanning(ChipX - Span,
                                              Row.MinimumY + (Scaled.LayerRowY - ChipH) * 0.5f,
                                              Span, ChipH);

            Surface->Ground(Chip, Faded(Covering(Background), RowCoverage), ChipH * 0.5f, CornerAll);
            Surface->Edge(Chip, Faded(Border, RowCoverage), 1.0f, ChipH * 0.5f, CornerAll);
            Surface->TextRun(Chip.MinimumX + 7.0f,
                             Row.MinimumY + (Scaled.LayerRowY - ChipRun) * 0.5f,
                             Faded(Covering(Foreground), RowCoverage), Text, ChipRun, 0.0f, true);

            ChipX -= Span + 4.0f;
        }

        return ChipX;
    };

    char ChannelChip[12] = {};
    std::snprintf(ChannelChip, sizeof(ChannelChip), "%u/8 CH", Current.ChannelCount);

    if (Current.ChannelCount > 0u && !IsFolder)
        DrawChip(ChannelChip, 0x1E1E1Eu, 0x9A9A9Au, Tinted.Hairline);

    if (EffectCount(Current.Effects) > 0u)
    {
        char EffectChip[12] = {};
        std::snprintf(EffectChip, sizeof(EffectChip), "%u FX", EffectCount(Current.Effects));
        DrawChip(EffectChip, 0x22190Eu, 0xFFD9A0u, Covering(0x5A3A1Cu));
    }

    if (Applied.MaskAttached[Index])
        DrawChip("MASK", 0x2A2A2Au, 0xDCDCDCu, Tinted.Hairline);

    if (Applied.LayerLocked[Index])
        DrawChip("L", 0x2C1918u, 0xFF9D96u, Covering(0x4C2524u));

    if (Current.Classified == TextureLayerClassification::Decal)
        DrawChip("3D", 0x1B242Fu, 0xA9D8FFu, Covering(0x38506Au));

    // ⑩ The details and more cells.
    const float CellCoverage = (Hovered || Taken) ? 1.0f : 0.45f;

    if (OnDetails)
        Surface->Ground(Details, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(Applied.LayerCardExpanded[Index]
                    ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                    Spanning(Details.MinimumX + (EyeExtent - 13.0f) * 0.5f,
                             Details.MinimumY + (EyeExtent - 13.0f) * 0.5f, 13.0f, 13.0f),
                    Faded(Faded(Tinted.Faint, CellCoverage), RowCoverage));

    if (OnMore)
        Surface->Ground(More, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(SymbolSubject::EllipsisDots,
                    Spanning(More.MinimumX + (EyeExtent - 13.0f) * 0.5f,
                             More.MinimumY + (EyeExtent - 13.0f) * 0.5f, 13.0f, 13.0f),
                    Faded(Faded(Tinted.Faint, CellCoverage), RowCoverage));
}

ControlIdentity TexturingPanel::NextInlineControl()
{
    return (InlineControlsSpent < 40u) ? InlineControls[InlineControlsSpent++] : ControlIdentity{};
}

float TexturingPanel::RecordInlineLayerCard(const PlaneExtent& Extent,
                                                TexturingContext& Applied,
                                                const TextureLayerRow& Current,
                                                std::uint32_t Index, bool Recording)
{
    const float Pad = Scaled.PanePad;
    const float HeadY = 29.0f;
    const float FieldY = Scaled.ComponentY;
    const float Left = Extent.MinimumX + Pad * 1.5f;
    const float Right = Extent.MaximumX - Pad * 1.5f;
    float Sweep = Extent.MinimumY + 8.0f;

    if (Recording)
    {
        Surface->Ground(Extent, Tinted.MenuLower, Scaled.LayerRadius, CornerAll);
        Surface->Edge(Extent, Tinted.Hairline, 1.0f, Scaled.LayerRadius, CornerAll);
    }

    const auto Section = [&](std::uint32_t SectionIndex, const char* Caption,
                             const char* Summary) -> bool
    {
        const PlaneExtent Head = Spanning(Extent.MinimumX, Sweep, Extent.Width(), HeadY);
        bool& Opened = Applied.LayerCardSection[Index][SectionIndex];

        if (Recording)
        {
            const ControlIdentity Disclosure = NextInlineControl();
            const bool Over = Head.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Sampled.ContactPressed && Over && !Interaction->AnyDisclosed())
                Interaction->Grab(Disclosure, ControlPart::Body);
            if (Over && Interaction->Released(Disclosure))
                Opened = !Opened;
            Interaction->DeclareHovered(Disclosure, Over, HoverOver);

            Surface->Ground(Head, Over ? Tinted.TileHovered : Tinted.Tile,
                            0.0f, CornerNone);
            Surface->Ground(Spanning(Head.MinimumX, Head.MaximumY - 1.0f, Head.Width(), 1.0f),
                            Tinted.Hairline, 0.0f, CornerNone);
            Surface->Stroke(Opened ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                            Spanning(Left, Sweep + (HeadY - 12.0f) * 0.5f, 12.0f, 12.0f),
                            Tinted.Faint);
            Surface->TextRun(Left + 18.0f, Sweep + (HeadY - Scaled.RunSmall) * 0.5f,
                             Tinted.Primary, Caption, Scaled.RunSmall, 0.0f, true);

            if (Summary != nullptr && Summary[0] != '\0')
            {
                const float Wide = Surface->MeasureRun(Summary, Scaled.RunFine, 0.0f);
                Surface->TextRun(Right - Wide, Sweep + (HeadY - Scaled.RunFine) * 0.5f,
                                 Tinted.Faint, Summary, Scaled.RunFine);
            }
        }

        Sweep += HeadY + (Opened ? 4.0f : 2.0f);
        return Opened;
    };

    const auto SkipFields = [&](std::uint32_t Count)
    {
        if (Recording)
            for (std::uint32_t FieldIndex = 0u; FieldIndex < Count; ++FieldIndex)
                (void) NextInlineControl();
    };

    const auto Field = [&]()
    {
        const PlaneExtent Row = Spanning(Left, Sweep, Right - Left, FieldY);
        Sweep += FieldY + 4.0f;
        return Row;
    };

    static const char* const TagNames[] = { "Violet", "Orange", "Green", "Blue", "Rose" };
    static constexpr std::uint32_t TagHues[] =
    {
        0x9B8CF0u, 0xF97316u, 0x84CC16u, 0x38BDF8u, 0xFB7185u
    };
    static const char* const HeightModes[] = { "Normal Map Detail", "Replace", "Add", "Overlay" };
    static const char* const Effects[] = { "None", "Levels", "Blur", "Sharpen", "Edge Wear" };

    const bool InfoOpen = Section(0u, "Info", TextureLayerText(Current.Classified));
    if (InfoOpen)
    {
        const PlaneExtent ResolutionRow = Field();
        if (Recording)
            SharedControls.MagnitudeRow(
                NextInlineControl(), ResolutionRow,
                MagnitudeDeclaration{ "Resolution", "px", 16.0, 16384.0, 0u,
                                      MagnitudeDeclaration::Arrange::Measured },
                Applied.LayerResolution[Index]);

        std::uint32_t TagTaken = 0u;
        for (std::uint32_t Tag = 0u; Tag < 5u; ++Tag)
            if (Applied.LayerTagHue[Index] == TagHues[Tag])
                TagTaken = Tag;

        const PlaneExtent TagRow = Field();
        if (Recording && SharedControls.SelectionField(NextInlineControl(), TagRow,
                                                       SelectionDeclaration{ "Tag Colour", TagNames, 5u },
                                                       TagTaken).ReadingAltered)
            Applied.LayerTagHue[Index] = TagHues[TagTaken];
    }
    else
    {
        SkipFields(2u);
    }

    const bool HeightOpen = Section(1u, "Height to Normal",
                                    Applied.LayerHeightIntegrated[Index] ? "Enabled" : "Off");
    if (HeightOpen)
    {
        const PlaneExtent HeightToggle = Field();
        if (Recording)
            SharedControls.ToggleRow(NextInlineControl(), HeightToggle,
                                     ToggleDeclaration{ "Re-integrate height into normal" },
                                     Applied.LayerHeightIntegrated[Index]);

        const PlaneExtent HeightMode = Field();
        if (Recording)
            SharedControls.SelectionField(NextInlineControl(), HeightMode,
                                          SelectionDeclaration{ "Blend Mode", HeightModes, 4u },
                                          Applied.LayerHeightBlendTaken[Index]);
    }
    else
    {
        SkipFields(2u);
    }

    const bool EffectsOpen = Section(2u, "Effects", Effects[Applied.LayerEffectTaken[Index] % 5u]);
    if (EffectsOpen)
    {
        const PlaneExtent EffectRow = Field();
        if (Recording)
            SharedControls.SelectionField(NextInlineControl(), EffectRow,
                                          SelectionDeclaration{ "Effect", Effects, 5u },
                                          Applied.LayerEffectTaken[Index]);
    }
    else
    {
        SkipFields(1u);
    }

    const bool ColourOpen = Section(3u, "Colour Blending",
                                    TextureBlendNames[Applied.LayerBlendTaken[Index]
                                                      % TextureBlendCount]);
    if (ColourOpen)
    {
        const PlaneExtent BlendRow = Field();
        if (Recording)
            SharedControls.SelectionField(NextInlineControl(), BlendRow,
                                          SelectionDeclaration{ "Blend", TextureBlendNames,
                                                                TextureBlendCount },
                                          Applied.LayerBlendTaken[Index]);

        double Opacity = static_cast<double>(Applied.LayerOpacity[Index]);
        const PlaneExtent OpacityField = Field();
        if (Recording && SharedControls.MagnitudeRow(
                                 NextInlineControl(), OpacityField,
                                 MagnitudeDeclaration{ "Opacity", "%", 0.0, 100.0, 0u,
                                                       MagnitudeDeclaration::Arrange::Measured },
                                 Opacity).ReadingAltered)
            Applied.LayerOpacity[Index] = static_cast<std::uint32_t>(Opacity + 0.5);
    }
    else
    {
        SkipFields(2u);
    }

    char Active[20] = {};
    std::snprintf(Active, sizeof Active, "%u active", Current.ChannelCount);
    const bool ChannelsOpen = Section(4u, "Channel Blending", Active);
    const std::uint32_t ChannelCount = Current.ChannelCount < TextureChannelLimit
                                     ? Current.ChannelCount : TextureChannelLimit;

    if (ChannelsOpen)
    {
        for (std::uint32_t Channel = 0u; Channel < ChannelCount; ++Channel)
        {
            const char* Caption = (Current.Channels[Channel] != nullptr)
                                ? Current.Channels[Channel] : TextureChannelText(Channel);
            const PlaneExtent ChannelBlend = Field();
            if (Recording)
                SharedControls.SelectionField(NextInlineControl(), ChannelBlend,
                                              SelectionDeclaration{ Caption, TextureBlendNames,
                                                                    TextureBlendCount },
                                              Applied.ChannelBlendTaken[Index][Channel]);

            double Amount = static_cast<double>(Applied.ChannelAmount[Index][Channel]);
            const PlaneExtent ChannelAmount = Field();
            if (Recording && SharedControls.MagnitudeRow(
                                     NextInlineControl(), ChannelAmount,
                                     MagnitudeDeclaration{ "Opacity", "%", 0.0, 100.0, 0u,
                                                           MagnitudeDeclaration::Arrange::Measured },
                                     Amount).ReadingAltered)
                Applied.ChannelAmount[Index][Channel] = static_cast<std::uint32_t>(Amount + 0.5);
        }
    }
    else
    {
        SkipFields(ChannelCount * 2u);
    }

    Sweep += 6.0f;
    return Sweep - Extent.MinimumY;
}

void TexturingPanel::RecordMaskRow(const PlaneExtent& Row, TexturingContext& Applied,
                                      const TextureLayerRow* Rows, std::uint32_t RowCount,
                                      const TextureLayerRow& Current, std::uint32_t Index)
{
    const bool Taken   = Applied.LayerTaken == Index && Applied.MaskTaken;
    const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool Absent  = !Applied.MaskVisible[Index];
    const bool SoloDim = !RowInSolo(Applied, Rows, RowCount, Index);

    const float Coverage = (Absent ? 0.34f : 1.0f) * (SoloDim ? 0.30f : 1.0f);

    // 🔴 EVERY DASH ON THIS ROW WAS WHITE. The elbow, the border and the rail were
    //    three different treatments in two different colours: the rail took the
    //    layer's hue while the elbow and the border took flat white at 0.10. A mask
    //    belongs to exactly one layer, and the only thing on the row that said so was
    //    the 3 px rail. All of it carries the owning layer's hue now, so an Edge Wear
    //    mask is Edge Wear's colour on every stroke that outlines it.
    const std::uint32_t OwnerHue = Applied.LayerTagHue[Index] != 0u
                                 ? Applied.LayerTagHue[Index] : Current.TagHue;
    const ThemeToken    Owner    = Covering(OwnerHue);

    // 📐 ONE dash rhythm for the whole OUTLINE. The border ran 10 px on / 4 px off —
    //    a long heavy dash — while the elbow was solid and the rail ran 3/7. Three
    //    treatments for one idea. The elbow and all four border strokes share one
    //    figure now, 2 on / 4 off: finer and tighter than the rail's, because a rail
    //    is read as a column of marks and an outline is read as a line.
    const float DashOn   = Scaled.LayerDashOn;
    const float DashStep = Scaled.LayerDashStep;
    const float DashWeight = 1.0f;

    // 📐 The connector elbow — the reference's `.attach::before/::after`, dashed on
    //    the same beat and in the owner's hue so the join reads as part of the outline.
    const float SpineX = Row.MinimumX - Scaled.LayerMaskIndent + 14.0f;

    for (float Y = Row.MinimumY; Y < Row.MinimumY + 23.0f; Y += DashStep)
    {
        const float Reach = std::min(DashOn, Row.MinimumY + 23.0f - Y);

        if (Reach <= 0.0f)
            break;

        Surface->Ground(Spanning(SpineX, Y, DashWeight, Reach),
                        Faded(Owner, 0.55f * Coverage), 0.0f, CornerNone);
    }

    for (float X = SpineX; X < SpineX + 11.0f; X += DashStep)
    {
        const float Reach = std::min(DashOn, SpineX + 11.0f - X);

        if (Reach <= 0.0f)
            break;

        Surface->Ground(Spanning(X, Row.MinimumY + 23.0f, Reach, DashWeight),
                        Faded(Owner, 0.55f * Coverage), 0.0f, CornerNone);
    }

    // 📐 The mask row ground: near-transparent, dashed border, solid + selected when taken.
    const PlaneExtent Ground = Spanning(Row.MinimumX, Row.MinimumY, Row.Width(), Row.Height());

    if (Taken)
    {
        Surface->Ground(Ground, Tinted.RowTaken, 0.0f, CornerNone);
        Surface->Edge(Ground, Tinted.HairlineFirm, 1.0f, 0.0f, CornerNone);
    }
    else
    {
        Surface->Ground(Ground, Hovered ? Tinted.TileHovered : Tinted.MenuLower, 0.0f, CornerNone);

        // 📐 The dashed border, on the SAME beat as the rail and the elbow and in the
        //    owner's hue. 🔴 The two side edges were drawn SOLID while the top and
        //    bottom were dashed, so three of the four strokes disagreed with each
        //    other; all four are dashed now.
        const ThemeToken Stroke = Faded(Owner, 0.45f * Coverage);

        for (float X = Row.MinimumX; X < Row.MaximumX; X += DashStep)
        {
            const float Reach = std::min(DashOn, Row.MaximumX - X);

            if (Reach <= 0.0f)
                break;

            Surface->Ground(Spanning(X, Row.MinimumY, Reach, DashWeight), Stroke, 0.0f, CornerNone);
            Surface->Ground(Spanning(X, Row.MaximumY - DashWeight, Reach, DashWeight),
                            Stroke, 0.0f, CornerNone);
        }

        for (float Y = Row.MinimumY; Y < Row.MaximumY; Y += DashStep)
        {
            const float Reach = std::min(DashOn, Row.MaximumY - Y);

            if (Reach <= 0.0f)
                break;

            Surface->Ground(Spanning(Row.MinimumX, Y, DashWeight, Reach), Stroke, 0.0f, CornerNone);
            Surface->Ground(Spanning(Row.MaximumX - DashWeight, Y, DashWeight, Reach),
                            Stroke, 0.0f, CornerNone);
        }
    }

    // 📐 The mask entry's colour rail shares the outline's rhythm and one-pixel cross-axis weight.
    //    Its 1 × 2 px marks remain vertical without appearing heavier than the 2 × 1 px border marks.
    for (float Y = Row.MinimumY; Y < Row.MinimumY + Row.Height(); Y += DashStep)
    {
        const float Reach = std::min(DashOn, Row.MinimumY + Row.Height() - Y);

        if (Reach <= 0.0f)
            break;

        Surface->Ground(Spanning(Row.MinimumX, Y, DashWeight, Reach),
                        Faded(Owner, Absent ? 0.3f : 0.85f), 0.0f, CornerNone);
    }

    // 📐 The content: eye, mini thumb, MASK name + sub, chips, details and more — the reference's
    //    `.row.msk`.
    const float Lead = Row.MinimumX + Scaled.LayerMaskIndent;
    const float EyeExtent = Scaled.LayerToolHeight - 5.0f;
    const PlaneExtent Eye = Spanning(Lead,
                                     Row.MinimumY + (Row.Height() - EyeExtent) * 0.5f,
                                     EyeExtent, EyeExtent);

    const float Mini = Scaled.LayerThumbY - 8.0f;
    const PlaneExtent Thumb = Spanning(Eye.MaximumX + 8.0f,
                                       Row.MinimumY + (Row.Height() - Mini) * 0.5f,
                                       Mini, Mini);

    const float MetaLead = Thumb.MaximumX + Scaled.PanePad;
    const float RightGrip = Row.MaximumX - 6.0f;

    const PlaneExtent More = Spanning(RightGrip - EyeExtent,
                                      Row.MinimumY + (Row.Height() - EyeExtent) * 0.5f,
                                      EyeExtent, EyeExtent);
    const PlaneExtent Details = Spanning(More.MinimumX - 4.0f - EyeExtent,
                                         More.MinimumY, EyeExtent, EyeExtent);

    const bool OnEye     = Eye.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool OnDetails = Details.Encloses(Sampled.PositionX, Sampled.PositionY);
    const bool OnMore    = More.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Sampled.ContactPressed && Hovered && !Interaction->AnyDisclosed())
    {
        if (OnEye)
            Interaction->Grab(MaskEyes[Index], ControlPart::Body);
        else if (OnDetails)
            Interaction->Grab(MaskDetails[Index], ControlPart::Body);
        else if (OnMore)
            Interaction->Grab(MaskMores[Index], ControlPart::Body);
        else
            Interaction->Grab(MaskContacts[Index], ControlPart::Body);
    }

    if (OnEye && Interaction->Released(MaskEyes[Index]))
        Applied.MaskVisible[Index] = !Applied.MaskVisible[Index];

    if (OnDetails && Interaction->Released(MaskDetails[Index]))
    {
        Applied.LayerTaken  = Index;
        Applied.MaskTaken   = true;
        Applied.StackPage   = 1u;
        Applied.PropertyTab = 1u;
    }

    if (Sampled.ContactDoublePressed && Hovered && !OnEye && !OnDetails && !OnMore)
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken = true;
        Applied.StackPage = 1u;
        Applied.PropertyTab = 1u;
    }

    if (OnMore && Interaction->Released(MaskMores[Index]))
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken  = true;
        Applied.MenuOpen   = 3u;
        Applied.MenuRow    = Index;
        Interaction->Disclose(MenuMask);
        MenuAnchorExtent = More;
    }

    if (Hovered && !OnEye && !OnDetails && !OnMore && Interaction->Released(MaskContacts[Index]))
    {
        Applied.LayerTaken = Index;
        Applied.MaskTaken  = true;
    }

    Interaction->DeclareHovered(MaskContacts[Index], Hovered, HoverOver);

    if (OnEye)
        Surface->Ground(Eye, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(Absent ? SymbolSubject::EyeClosed : SymbolSubject::EyeOpen, Eye,
                    Faded(OnEye ? Tinted.Primary : Faded(Tinted.Primary, Absent ? 0.4f : 0.55f),
                          Coverage));

    // 📐 The mini thumb: the mask's hue wash with the mask glyph.
    for (std::uint32_t RowStep = 0u; RowStep < 2u; ++RowStep)
    {
        for (std::uint32_t Column = 0u; Column < 2u; ++Column)
        {
            const bool Light = ((RowStep + Column) & 1u) == 0u;

            Surface->Ground(Spanning(Thumb.MinimumX + Mini * 0.5f * static_cast<float>(Column),
                                     Thumb.MinimumY + Mini * 0.5f * static_cast<float>(RowStep),
                                     Mini * 0.5f + 0.5f, Mini * 0.5f + 0.5f),
                            Faded(Covering(Light ? 0x1C1C1Cu : 0x101010u), Coverage),
                            0.0f, CornerNone);
        }
    }

    const float MaskGlyph = Mini * 0.55f;
    Surface->Stroke(SymbolSubject::HalfMask,
                    Spanning(Thumb.MinimumX + (Mini - MaskGlyph) * 0.5f,
                             Thumb.MinimumY + (Mini - MaskGlyph) * 0.5f, MaskGlyph, MaskGlyph),
                    Faded(Covering(0xDCDCDCu), Coverage));

    // 📐 The name and the sub run.
    const float NamingRun = Scaled.RunSmall;
    const float NamingTop = Row.MinimumY + (Row.Height() * 0.5f - NamingRun * 1.3f) * 0.5f;
    const float NamingLimit = Details.MinimumX - Scaled.PanePad;

    // 📝 The mask is an entry name, not an acronym. Natural title case and ordinary tracking keep it
    //    legible beside the compact uppercase capability chips.
    Surface->TextRun(MetaLead, NamingTop,
                     Faded(Taken ? Tinted.Primary : Tinted.Muted, Coverage),
                     "Mask", NamingRun, 0.0f, true);

    const char* Source = Current.Source[0] != '\0'
                       ? Current.Source
                       : TextureMaskSourceNames[Applied.MaskSourceTaken[Index] % 5u];

    char Sub[64] = {};
    std::snprintf(Sub, sizeof(Sub), "%s \u00B7 Gray 8 \u00B7 %u%%%s",
                  Source, Applied.MaskDensity[Index],
                  Applied.MaskInverted[Index] ? " \u00B7 INV" : "");

    Surface->TextRunTruncated(MetaLead, NamingTop + NamingRun * 1.3f, NamingLimit,
                              Faded(Tinted.Faint, Coverage), Sub, Scaled.RunFine);

    // 📐 The chips: CH, FX.
    float ChipX = NamingLimit;
    const float ChipRun = Scaled.RunFiner;
    const float ChipH = Scaled.LayerChipY;

    if (Current.ChannelCount > 0u && Current.ChannelCount < TextureChannelLimit)
    {
        char Chip[12] = {};
        std::snprintf(Chip, sizeof(Chip), "%u CH", Current.ChannelCount);

        const float Span = Surface->MeasureRun(Chip, ChipRun, 0.0f) + 14.0f;
        const PlaneExtent ChipCell = Spanning(ChipX - Span,
                                              Row.MinimumY + (Row.Height() - ChipH) * 0.5f,
                                              Span, ChipH);

        Surface->Ground(ChipCell, Faded(Tinted.Tile, Coverage), ChipH * 0.5f, CornerAll);
        Surface->Edge(ChipCell, Faded(Tinted.Hairline, Coverage), 1.0f, ChipH * 0.5f, CornerAll);
        Surface->TextRun(ChipCell.MinimumX + 7.0f,
                         Row.MinimumY + (Row.Height() - ChipRun) * 0.5f,
                         Faded(Tinted.Muted, Coverage), Chip, ChipRun, 0.0f, true);

        ChipX -= Span + 4.0f;
    }

    if (EffectCount(Current.Effects) > 0u)
    {
        char Chip[12] = {};
        std::snprintf(Chip, sizeof(Chip), "%u FX", EffectCount(Current.Effects));

        const float Span = Surface->MeasureRun(Chip, ChipRun, 0.0f) + 14.0f;
        const PlaneExtent ChipCell = Spanning(ChipX - Span,
                                              Row.MinimumY + (Row.Height() - ChipH) * 0.5f,
                                              Span, ChipH);

        Surface->Ground(ChipCell, Faded(Covering(0x22190Eu), Coverage), ChipH * 0.5f, CornerAll);
        Surface->Edge(ChipCell, Faded(Covering(0x5A3A1Cu), Coverage), 1.0f, ChipH * 0.5f, CornerAll);
        Surface->TextRun(ChipCell.MinimumX + 7.0f,
                         Row.MinimumY + (Row.Height() - ChipRun) * 0.5f,
                         Faded(Covering(0xFFD9A0u), Coverage), Chip, ChipRun, 0.0f, true);
    }

    // 📐 The details and more cells.
    const float CellCoverage = (Hovered || Taken) ? 1.0f : 0.45f;

    if (OnDetails)
        Surface->Ground(Details, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(SymbolSubject::ChevronRight,
                    Spanning(Details.MinimumX + (EyeExtent - 13.0f) * 0.5f,
                             Details.MinimumY + (EyeExtent - 13.0f) * 0.5f, 13.0f, 13.0f),
                    Faded(Faded(Tinted.Faint, CellCoverage), Coverage));

    if (OnMore)
        Surface->Ground(More, Faded(Tinted.Primary, 0.08f), 3.0f, CornerAll);

    Surface->Stroke(SymbolSubject::EllipsisDots,
                    Spanning(More.MinimumX + (EyeExtent - 13.0f) * 0.5f,
                             More.MinimumY + (EyeExtent - 13.0f) * 0.5f, 13.0f, 13.0f),
                    Faded(Faded(Tinted.Faint, CellCoverage), Coverage));
}

void TexturingPanel::RecordBarButton(ControlIdentity Target, const PlaneExtent& Cell,
                                        SymbolSubject Glyph, TexturingContext& Applied,
                                        std::uint32_t Request, bool Dimmed)
{
    const bool Hovered = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);
    const float Coverage = Dimmed ? 0.25f : 1.0f;

    if (Hovered && !Dimmed)
        Surface->Ground(Cell, Faded(Tinted.Primary, 0.08f), Scaled.LayerRadius, CornerAll);

    const float Figure = 15.0f;

    Surface->Stroke(Glyph,
                    Spanning(Cell.MinimumX + (Cell.Width() - Figure) * 0.5f,
                             Cell.MinimumY + (Cell.Height() - Figure) * 0.5f, Figure, Figure),
                    Faded(Hovered ? Tinted.Primary : Tinted.Muted, Coverage));

    if (Sampled.ContactPressed && Hovered && !Dimmed && !Interaction->AnyDisclosed())
        Interaction->Grab(Target, ControlPart::Body);

    if (Hovered && !Dimmed && Interaction->Released(Target))
        Applied.Structural = Request;
}

void TexturingPanel::RecordStackFooter(const PlaneExtent& Footer, TexturingContext& Applied,
                                          const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    const float Pad = Scaled.PanePad;
    const bool Selection = Applied.LayerTaken < RowCount;
    const std::uint32_t Taken = Applied.LayerTaken;

    // ① The crumb — the reference's breadcrumb path.
    const float CrumbRun = Scaled.RunFiner;
    const float CrumbTop = Footer.MinimumY + (Scaled.LayerFootCrumb - CrumbRun) * 0.5f;

    char Crumb[160] = {};
    Crumb[0] = '\0';

    if (Selection)
    {
        // 📐 The path: the ancestors' names, then the taken name — the reference's `ancestors().map`.
        char Path[128] = {};
        std::uint32_t Steps[TextureLayerLimit] = {};
        std::uint32_t StepCount = 0u;
        std::uint32_t Walking = Rows[Taken].Enclosing;

        while (Walking < RowCount && StepCount + 1u < TextureLayerLimit)
        {
            Steps[StepCount++] = Walking;
            Walking = Rows[Walking].Enclosing;
        }

        for (std::uint32_t Step = StepCount; Step-- > 0u;)
        {
            std::snprintf(Path + std::strlen(Path), sizeof(Path) - std::strlen(Path), "%s \u203A ",
                          Rows[Steps[Step]].Naming);
        }

        std::snprintf(Crumb, sizeof(Crumb), "%s%s \u00B7 %s", Path, Rows[Taken].Naming,
                      Applied.MaskTaken
                          ? (Rows[Taken].Source[0] != '\0' ? Rows[Taken].Source
                                                           : "grayscale mask")
                          : TextureLayerText(Rows[Taken].Classified));
    }
    else
    {
        std::snprintf(Crumb, sizeof(Crumb), "no selection");
    }

    Surface->TextRunTruncated(Footer.MinimumX + Pad, CrumbTop,
                              Footer.MaximumX - Pad, Tinted.Faint, Crumb, CrumbRun);

    // ② The blend pill + the opacity slider — the reference's `.prop`.
    const float PropY = Footer.MinimumY + Scaled.LayerFootCrumb;
    const float PropH = Scaled.LayerFootProp;
    const float PillH = Scaled.LayerToolHeight - 1.0f;
    const float PillY = PropY + (PropH - PillH) * 0.5f;

    const bool MaskOn = Selection && Applied.MaskTaken;
    const bool BlendDim = !Selection || MaskOn;

    const float PillW = std::min(Footer.Width() * 0.45f, 190.0f);
    const PlaneExtent Pill = Spanning(Footer.MinimumX + Pad, PillY, PillW, PillH);

    Surface->Ground(Pill, Faded(Tinted.Primary, BlendDim ? 0.03f : 0.06f),
                    PillH * 0.5f, CornerAll);
    Surface->Edge(Pill, Tinted.Hairline, 1.0f, PillH * 0.5f, CornerAll);

    const float PillRun = Scaled.RunSecondary;

    if (Selection)
    {
        const char* BlendText = Applied.MaskTaken
                              ? "Mask density"
                              : TextureBlendNames[Applied.LayerBlendTaken[Taken] % TextureBlendCount];

        Surface->TextRunTruncated(Pill.MinimumX + 13.0f,
                                  Pill.MinimumY + (PillH - PillRun) * 0.5f,
                                  Pill.MaximumX - 26.0f,
                                  BlendDim ? Faded(Tinted.Muted, 0.5f) : Tinted.Primary,
                                  BlendText, PillRun, true);

        const float Chev = 11.0f;
        Surface->Stroke(SymbolSubject::ChevronDown,
                        Spanning(Pill.MaximumX - 18.0f,
                                 Pill.MinimumY + (PillH - Chev) * 0.5f, Chev, Chev),
                        Faded(Tinted.Faint, BlendDim ? 0.4f : 1.0f));
    }

    if (!BlendDim)
    {
        if (Sampled.ContactPressed && Pill.Encloses(Sampled.PositionX, Sampled.PositionY) &&
            !Interaction->AnyDisclosed())
        {
            Interaction->Grab(BlendField, ControlPart::Body);
        }

        if (Pill.Encloses(Sampled.PositionX, Sampled.PositionY) && Interaction->Released(BlendField))
        {
            Interaction->Disclose(MenuBlend);
            Applied.MenuOpen = 4u;
            MenuAnchorExtent = Pill;
        }
    }

    // 📐 The opacity row.
    // 🔴 THIS WAS A FOURTH HAND-ROLLED SLIDER. Two Grounds for the track, two
    //    Medallions for the knob, a hand-written hit test with its own ad-hoc
    //    ±8/±14 px cushion, and a separate value pill — none of it the shared
    //    control, so it had no readout cell, no unit segment, no hover growth and
    //    no keyboard path, and it drifted from every other slider in the editor.
    //    It is MagnitudeRow now, in the Measured arrangement, exactly as the sun
    //    rows are: caption, track, then the reading with its unit.
    const std::uint32_t Amount = Selection
                               ? (MaskOn ? Applied.MaskDensity[Taken]
                                         : Applied.LayerOpacity[Taken])
                               : 100u;

    MagnitudeDeclaration Opacity;
    Opacity.Caption   = MaskOn ? "Density" : "Opacity";
    Opacity.UnitGlyph = "%";
    Opacity.Minimum   = 0.0;
    Opacity.Maximum   = 100.0;
    Opacity.Layout    = MagnitudeDeclaration::Arrange::Measured;

    double Reading = static_cast<double>(Amount);

    const PlaneExtent OpacityExtent = Spanning(Pill.MaximumX + 12.0f, PropY,
                                               Footer.MaximumX - Pad - Pill.MaximumX - 12.0f,
                                               PropH);

    if (OpacityExtent.Width() > 0.0f &&
        SharedControls.MagnitudeRow(OpacityRow, OpacityExtent, Opacity, Reading, false).ReadingAltered
        && Selection)
    {
        const std::uint32_t Written = static_cast<std::uint32_t>(Reading + 0.5);

        if (MaskOn)
            Applied.MaskDensity[Taken] = Written;
        else
            Applied.LayerOpacity[Taken] = Written;
    }

    // ③ The action bar — the reference's `.bar`.
    const float BarY = PropY + Scaled.LayerFootProp;
    const float BarH = Scaled.LayerFootBar;
    const float ButtonY = Scaled.LayerToolHeight;
    const float ButtonTop = BarY + (BarH - ButtonY) * 0.5f;

    const float VSepY = ButtonTop + (ButtonY - 17.0f) * 0.5f;

    float Lead = Footer.MinimumX + Pad;

    // 📐 The Add action, moved down from the withdrawn header. It is the only one of
    //    that header's four buttons that did anything: undo and redo were hardcoded
    //    to the disabled pose, and the expand toggle drove wide columns the row
    //    layout no longer lays out.
    {
        const PlaneExtent AddCell = Spanning(Lead, ButtonTop, ButtonY, ButtonY);
        BarCellTally = 0u;
        BarCells[BarCellTally++] = AddCell;
        const bool OnAdd = AddCell.Encloses(Sampled.PositionX, Sampled.PositionY);

        Surface->Ground(AddCell, OnAdd ? Faded(Tinted.Primary, 0.16f)
                                       : Faded(Tinted.Primary, 0.09f),
                        Scaled.LayerRadius, CornerAll);
        Surface->Edge(AddCell, Tinted.Hairline, 1.0f, Scaled.LayerRadius, CornerAll);

        const float AddFigure = 15.0f;

        Surface->Stroke(SymbolSubject::PlusCross,
                        Spanning(AddCell.MinimumX + (ButtonY - AddFigure) * 0.5f,
                                 AddCell.MinimumY + (ButtonY - AddFigure) * 0.5f,
                                 AddFigure, AddFigure),
                        OnAdd ? Tinted.Primary : Tinted.Muted);

        if (Sampled.ContactPressed && OnAdd && !Interaction->AnyDisclosed())
            Interaction->Grab(HeaderAdd, ControlPart::Body);

        if (OnAdd && Interaction->Released(HeaderAdd))
        {
            Interaction->Disclose(MenuAdd);
            Applied.MenuOpen = 1u;
            MenuAnchorExtent = AddCell;
        }

        Lead = AddCell.MaximumX + 5.0f;

        Surface->Ground(Spanning(Lead + 5.0f, VSepY, 1.0f, 17.0f),
                        Tinted.Hairline, 0.0f, CornerNone);
        Lead += 11.0f;
    }

    struct BarCell
    {
        SymbolSubject   Glyph;
        std::uint32_t   Request;
        bool            Always;
    };

    // 🔴 THE BAR'S FIRST SIX CELLS WERE THE + MENU, SPELLED TWICE. Covering, Fill,
    //    Adjustment, Filter, Decal and Pattern are exactly the six declarations the
    //    Add menu offers — same requests, same glyphs, two places to press for one
    //    delivery, and the menu names them in words while the bar left the artist to
    //    infer six icons. Withdrawn: adding a layer is the + button's job.
    //
    //    📐 Measured before removing them, not assumed — proof/BarMain.cpp presses
    //    every cell through the real pointer path and reports what the row set did:
    //    all thirteen acted, so none of these were decorations. They were duplicates,
    //    which is a different fault and the one worth fixing.
    //
    //    What remains is what the + menu does NOT offer, because it acts on the row
    //    already taken rather than declaring a new one: group it, duplicate it, lock
    //    it, reorder it, delete it.
    const BarCell Bar[6] =
    {
        { SymbolSubject::FolderClosed,      static_cast<std::uint32_t>(TexturingRequest::Group),         false },
        { SymbolSubject::CopyDuplicate,     static_cast<std::uint32_t>(TexturingRequest::Duplicate),     false },
        { SymbolSubject::LockClosed,        0x80000001u,                                                    false },
        { SymbolSubject::ArrowUpLine,       static_cast<std::uint32_t>(TexturingRequest::MoveUp),        false },
        { SymbolSubject::ArrowDownLine,     static_cast<std::uint32_t>(TexturingRequest::MoveDown),      false },
        { SymbolSubject::TrashBin,          static_cast<std::uint32_t>(TexturingRequest::Delete),        false }
    };

    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        // 📐 One separator before the reorder pair and one before the bin, so the
        //    bar reads as three groups: shape it, move it, remove it.
        if (Index == 3u || Index == 5u)
        {
            Surface->Ground(Spanning(Lead + 5.0f, VSepY, 1.0f, 17.0f),
                            Tinted.Hairline, 0.0f, CornerNone);
            Lead += 11.0f;
        }

        const PlaneExtent Cell = Spanning(Lead, ButtonTop, ButtonY, ButtonY);

        if (BarCellTally < 13u)
            BarCells[BarCellTally++] = Cell;

        if (Index == 2u)
        {
            // 📐 The lock toggles the taken row's lock — a working copy, never a structural request.
            const bool Locked = Selection && Applied.LayerLocked[Taken];

            const bool Hovered = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);

            if (Hovered && Selection)
                Surface->Ground(Cell, Faded(Tinted.Primary, 0.08f), Scaled.LayerRadius, CornerAll);

            const float Figure = 15.0f;

            Surface->Stroke(Locked ? SymbolSubject::LockClosed : SymbolSubject::LockOpen,
                            Spanning(Cell.MinimumX + (ButtonY - Figure) * 0.5f,
                                     Cell.MinimumY + (ButtonY - Figure) * 0.5f, Figure, Figure),
                            Faded(Hovered ? Tinted.Primary : Tinted.Muted,
                                  Selection ? 1.0f : 0.25f));

            if (Sampled.ContactPressed && Hovered && Selection && !Interaction->AnyDisclosed())
                Interaction->Grab(BarButtons[Index], ControlPart::Body);

            if (Hovered && Selection && Interaction->Released(BarButtons[Index]))
                Applied.LayerLocked[Taken] = !Applied.LayerLocked[Taken];
        }
        else
        {
            RecordBarButton(BarButtons[Index], Cell, Bar[Index].Glyph, Applied,
                            Bar[Index].Request, !Bar[Index].Always && !Selection);
        }

        Lead += ButtonY + 5.0f;
    }
}

void TexturingPanel::RecordMenu(const PlaneExtent& Extent, TexturingContext& Applied,
                                   const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    if (Applied.MenuOpen != 0u)
        MenuPresented = Applied.MenuOpen;

    const std::uint32_t Open = MenuPresented;

    if (Open == 0u)
        return;

    ControlIdentity MenuTarget = (Open == 1u) ? MenuAdd
                               : (Open == 2u) ? MenuLayer
                               : (Open == 3u) ? MenuMask : MenuBlend;

    // 📐 A menu remains presented while its taken fade returns to zero. This is what gives closing the
    //    same visible travel as opening instead of deleting the card on the withdrawal tick.
    const bool Disclosed = Applied.MenuOpen == Open && Interaction->Disclosed(MenuTarget);
    Interaction->DeclareTaken(MenuTarget, Disclosed, 160.0, EaseCurve::CssEase);
    const float Disclosure = Interaction->TakenFraction(MenuTarget);

    if (!Disclosed)
        Applied.MenuOpen = 0u;

    if (Disclosure <= 0.0f)
    {
        MenuPresented = 0u;
        return;
    }

    const float Pad = Scaled.PanePad;
    const float RowY = Scaled.LayerToolHeight + 2.0f;

    std::uint32_t OptionCount = 0u;
    const char* const* Captions = nullptr;
    const SymbolSubject* Glyphs = nullptr;
    const char* const* Shortcuts = nullptr;
    ControlIdentity* Identities = &MenuIdentities[0];
    const char* Title = "";

    // 📐 The four menus' declared items — the reference's popup content, trimmed to what the editor
    //    owns (no history spine, no colour wheel).
    static const char* const AddCaptions[7] =
    {
        "Brushed layer", "Fill layer", "Adjustment", "Filter",
        "Decal layer \u00B7 3D", "Pattern layer", "Group"
    };
    static const SymbolSubject AddGlyphs[7] =
    {
        SymbolSubject::Bristle, SymbolSubject::DropletDrop,
        SymbolSubject::AdjustmentSliders, SymbolSubject::FilterFunnel,
        SymbolSubject::StencilDecal, SymbolSubject::TiledPattern, SymbolSubject::FolderClosed
    };
    static const char* const AddShortcuts[7] =
    {
        "P", "F", "A", "R", "D", "T", "G"
    };

    static const char* LayerCaptions[7] =
    {
        "Details", "Add mask", "Lock", "Solo", "Duplicate", "Group", "Delete"
    };
    static const SymbolSubject LayerGlyphs[7] =
    {
        SymbolSubject::ChevronRight, SymbolSubject::HalfMask, SymbolSubject::LockClosed,
        SymbolSubject::EyeOpen, SymbolSubject::CopyDuplicate, SymbolSubject::FolderClosed,
        SymbolSubject::TrashBin
    };
    static const char* const LayerShortcuts[7] =
    {
        "Space", "M", "L", "S", "\u2318D", "\u2318G", "\u232B"
    };

    static const char* const MaskCaptions[3] = { "Details", "Invert", "Delete mask" };
    static const SymbolSubject MaskGlyphs[3] =
    {
        SymbolSubject::ChevronRight, SymbolSubject::HalfMask, SymbolSubject::TrashBin
    };

    static const char* const BlendCaptions[TextureBlendCount] =
    {
        "Normal", "Passthrough", "Replace", "Multiply", "Screen", "Overlay",
        "Soft Light", "Hard Light", "Linear Dodge (Add)", "Color Dodge", "Linear Burn",
        "Difference", "Exclusion"
    };

    float CardY = MenuAnchorExtent.MaximumY + 6.0f;
    const float CardW = 206.0f;
    ControlIdentity* SwatchIdentities = &MenuIdentities[14];

    switch (Open)
    {
        case 1u:
            OptionCount = 7u;
            Captions  = AddCaptions;
            Glyphs    = AddGlyphs;
            Shortcuts = AddShortcuts;
            Title     = "Add";
            Identities = &MenuIdentities[0];
            break;
        case 2u:
        {
            OptionCount = 7u;
            Captions  = LayerCaptions;
            Glyphs    = LayerGlyphs;
            Shortcuts = LayerShortcuts;
            Title     = Rows != nullptr && Applied.MenuRow < RowCount
                      ? Rows[Applied.MenuRow].Naming : "Layer";
            Identities = &MenuIdentities[7];

            // 📐 The dynamic captions: mask and lock and solo follow the row's own state.
            LayerCaptions[1] = Applied.MaskAttached[Applied.MenuRow] ? "Remove mask" : "Add mask";
            LayerCaptions[2] = Applied.LayerLocked[Applied.MenuRow]  ? "Unlock"      : "Lock";
            LayerCaptions[3] = Applied.SoloTaken == Applied.MenuRow  ? "Clear solo"  : "Solo";
            break;
        }
        case 3u:
            OptionCount = 3u;
            Captions  = MaskCaptions;
            Glyphs    = MaskGlyphs;
            Shortcuts = nullptr;
            Title     = "Mask";
            Identities = &MenuIdentities[24];
            break;
        default:
            OptionCount = TextureBlendCount;
            Captions  = BlendCaptions;
            Glyphs    = nullptr;
            Shortcuts = nullptr;
            Title     = "Blend mode";
            Identities = &MenuIdentities[27];
            break;
    }

    // 📐 The card hangs right-aligned to its anchor, flipped above when the leaf has no room.
    const float CardH = Pad * 2.0f + 20.0f + RowY * static_cast<float>(OptionCount)
                      + (Open == 2u ? (RowY + 6.0f) : 0.0f);

    float CardX = MenuAnchorExtent.MaximumX - CardW;
    bool OpensAbove = false;

    if (CardY + CardH > Extent.MaximumY - 6.0f)
    {
        CardY = MenuAnchorExtent.MinimumY - CardH - 6.0f;
        OpensAbove = true;
    }

    CardX = Held(CardX, Extent.MinimumX + 6.0f, Extent.MaximumX - CardW - 6.0f);

    const PlaneExtent Card = Spanning(CardX, CardY, CardW, CardH);
    const float RevealedHeight = Card.Height() * Disclosure;
    const PlaneExtent Revealed = OpensAbove
        ? PlaneExtent{ Card.MinimumX, Card.MaximumY - RevealedHeight, Card.MaximumX, Card.MaximumY }
        : PlaneExtent{ Card.MinimumX, Card.MinimumY, Card.MaximumX, Card.MinimumY + RevealedHeight };

    // 📐 A contact that arrived outside the card and its anchor withdraws the menu.
    if (Sampled.ContactPressed && !Card.Encloses(Sampled.PositionX, Sampled.PositionY) &&
        !MenuAnchorExtent.Encloses(Sampled.PositionX, Sampled.PositionY))
    {
        Applied.MenuOpen = 0u;
        Interaction->Withdraw();
    }

    Surface->Ground(Revealed, Tinted.Menu, 16.0f, CornerAll);
    Surface->Edge(Revealed, Tinted.HairlineFirm, 1.0f, 16.0f, CornerAll);
    Surface->Confine(Revealed);

    Surface->TextRunCapitalised(Card.MinimumX + 10.0f, Card.MinimumY + 9.0f,
                                Tinted.Faint, Title, Scaled.RunFiner, 1.0f, true);

    // 📐 The items.
    std::uint32_t WritesLocal[TextureBlendCount] = {};

    RecordMenuOptions(Card, Captions, Glyphs, OptionCount, Shortcuts, Identities,
                      Applied, WritesLocal, Disclosed);

    if (Open == 1u)
    {
        for (std::uint32_t Index = 0u; Index < OptionCount; ++Index)
        {
            if (WritesLocal[Index] != 0u)
                Applied.Structural = static_cast<std::uint32_t>(TexturingRequest::AddTexturing) + Index;
        }
    }
    else if (Open == 2u)
    {
        if (WritesLocal[0] != 0u)
        {
            Applied.StackPage = 1u;
            Applied.PropertyTab = 0u;
        }

        if (WritesLocal[1] != 0u)
        {
            const std::uint32_t Row = Applied.MenuRow;
            Applied.MaskAttached[Row] = !Applied.MaskAttached[Row];

            if (Applied.MaskAttached[Row])
                Applied.MaskVisible[Row] = true;

            Applied.MaskTaken = false;
        }

        if (WritesLocal[2] != 0u)
            Applied.LayerLocked[Applied.MenuRow] = !Applied.LayerLocked[Applied.MenuRow];

        if (WritesLocal[3] != 0u)
            Applied.SoloTaken = (Applied.SoloTaken == Applied.MenuRow)
                              ? 0xFFFFFFFFu : Applied.MenuRow;

        if (WritesLocal[4] != 0u)
            Applied.Structural = static_cast<std::uint32_t>(TexturingRequest::Duplicate);

        if (WritesLocal[5] != 0u)
            Applied.Structural = static_cast<std::uint32_t>(TexturingRequest::Group);

        if (WritesLocal[6] != 0u)
            Applied.Structural = static_cast<std::uint32_t>(TexturingRequest::Delete);
    }
    else if (Open == 3u)
    {
        if (WritesLocal[0] != 0u)
        {
            Applied.StackPage = 1u;
            Applied.PropertyTab = 1u;
        }

        if (WritesLocal[1] != 0u)
            Applied.MaskInverted[Applied.MenuRow] = !Applied.MaskInverted[Applied.MenuRow];

        if (WritesLocal[2] != 0u)
        {
            Applied.MaskAttached[Applied.MenuRow] = false;
            Applied.MaskTaken = false;
        }
    }
    else if (Open == 4u)
    {
        for (std::uint32_t Index = 0u; Index < OptionCount; ++Index)
        {
            if (WritesLocal[Index] != 0u && Applied.LayerTaken < RowCount)
                Applied.LayerBlendTaken[Applied.LayerTaken] = Index;
        }

        // 📐 The taken blend's check — the reference's check mark on the standing option.
        if (Applied.LayerTaken < RowCount)
        {
            const std::uint32_t TakenBlend = Applied.LayerBlendTaken[Applied.LayerTaken]
                                           % TextureBlendCount;
            const float CheckY = Card.MinimumY + Pad + 20.0f
                               + RowY * static_cast<float>(TakenBlend) + (RowY - 12.0f) * 0.5f;

            const float PointsX[3] = { Card.MinimumX + 12.0f, Card.MinimumX + 16.0f,
                                       Card.MinimumX + 23.0f };
            const float PointsY[3] = { CheckY + 6.5f, CheckY + 11.0f, CheckY + 3.5f };

            Surface->Polyline(PointsX, PointsY, 3u, Tinted.Primary, 1.6f);
        }
    }

    // 📐 The layer menu's colour swatches — the reference's `.swatches` row.
    if (Open == 2u)
    {
        const float SwatchY = Card.MinimumY + Pad * 2.0f + 20.0f + RowY * 7.0f + 6.0f;
        const float Swatch = 18.0f;
        const float Gap = (CardW - Pad * 2.0f - Swatch * 10.0f) / 9.0f;

        for (std::uint32_t SwatchIndex = 0u; SwatchIndex < TexturingContext::TextureSwatchCount;
             ++SwatchIndex)
        {
            const float X = Card.MinimumX + Pad + static_cast<float>(SwatchIndex) * (Swatch + Gap);
            const PlaneExtent Cell = Spanning(X, SwatchY, Swatch, Swatch);

            const bool On = Applied.LayerTagHue[Applied.MenuRow] == SwatchColours[SwatchIndex];

            Surface->Ground(Cell, Covering(SwatchColours[SwatchIndex]),
                            Swatch * 0.5f, CornerAll);
            Surface->Edge(Cell, Faded(Tinted.Primary, On ? 1.0f : 0.16f), On ? 2.0f : 1.0f,
                          Swatch * 0.5f, CornerAll);

            const bool Hovered = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);

            if (Disclosed && Hovered && Sampled.ContactPressed)
                Interaction->Grab(SwatchIdentities[SwatchIndex], ControlPart::Body);

            if (Disclosed && Hovered && Interaction->Released(SwatchIdentities[SwatchIndex]))
            {
                Applied.LayerTagHue[Applied.MenuRow] = SwatchColours[SwatchIndex];
                Applied.MenuOpen = 0u;
                Interaction->Withdraw();
            }
        }
    }

    Surface->Release();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROPERTIES PAGE
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordPropertiesPage(const PlaneExtent& Extent, TexturingContext& Applied,
                                             const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);

    if (RowCount == 0u || Applied.LayerTaken >= RowCount)
    {
        const float Run = Scaled.RunSecondary;
        const char* Prose = "Select a layer in the stack, then press Tab.";

        Surface->TextRun(Extent.MinimumX + (Extent.Width()
                                              - Surface->MeasureRun(Prose, Run, 0.0f)) * 0.5f,
                         Extent.MinimumY + Scaled.HeaderHeight, Tinted.Faint, Prose, Run);
        return;
    }

    const TextureLayerRow& Current = Rows[Applied.LayerTaken];
    const ThemeToken Hue = TextureLayerHue(Current.Classified);

    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);

    char Classified[48] = {};
    std::snprintf(Classified, sizeof(Classified), "%s \u00B7 %s",
                  TextureLayerText(Current.Classified),
                  TextureBlendNames[Applied.LayerBlendTaken[Applied.LayerTaken] % TextureBlendCount]);

    RecordLeafHeader(Header, TextureLayerGlyph(Current.Classified), Hue, Current.Naming, Classified);

    // 📐 The property strip names the ONE panel the selection opens. It is a heading
    //    now rather than a chooser, because there is nothing left to choose between.
    const char* TabCaptions[3] = { "Channels", nullptr, nullptr };

    if (Current.Classified == TextureLayerClassification::Folder)
        TabCaptions[0] = "Stack";

    // 🔴 A decal was given the Channels tab and the channels card, so selecting a
    //    decal opened a channel editor for a stencil that has no channels to author.
    if (Current.Classified == TextureLayerClassification::Decal)
        TabCaptions[0] = "Decal";

    if (Applied.MaskTaken)
        TabCaptions[0] = "Mask";

    const std::uint32_t TabCount = PropertyTabCount(Applied, Current);

    if (Applied.PropertyTab >= TabCount)
        Applied.PropertyTab = 0u;

    const TabDeclaration Declared{ TabCaptions, TabCount };
    static_cast<void>(Controls.TabStrip(PropertyStrip,
                                        Spanning(Extent.MinimumX, Header.MaximumY,
                                                 Extent.Width(), Scaled.ComponentY),
                                        Declared, Applied.PropertyTab));

    const PlaneExtent Pages = Spanning(Extent.MinimumX, Header.MaximumY + Scaled.ComponentY,
                                       Extent.Width(),
                                       Extent.MaximumY - Header.MaximumY - Scaled.ComponentY
                                       - Scaled.FooterHeight);

    if (Pages.MaximumY <= Pages.MinimumY)
        return;

    Surface->Confine(Pages);

    // 📐 One selection, one card. The classification and the mask flag decide it
    //    outright; there is no tab left to disagree with them.
    if (Current.Classified == TextureLayerClassification::Folder)
        RecordFolderCard(Pages, Applied, Rows, RowCount);
    else if (Applied.MaskTaken)
        RecordMaskCard(Pages, Applied, Current);
    else if (Current.Classified == TextureLayerClassification::Decal)
        RecordDecalCard(Pages, Applied, Current);
    else
        RecordChannelCard(Pages, Applied, Current);

    Surface->Release();

    // 📐 The footer: the selection's small summary.
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    char Summary[64] = {};
    std::snprintf(Summary, sizeof(Summary), "%u%% \u00B7 %s",
                  Applied.LayerOpacity[Applied.LayerTaken],
                  Applied.MaskTaken ? "mask" : TextureLayerText(Current.Classified));

    const float FootRun = Scaled.RunFine;
    const float FootTop = Footer.MinimumY + (Footer.Height() - FootRun) * 0.5f;

    Surface->Ground(Spanning(Footer.MinimumX + Scaled.HeaderPadX,
                             Footer.MinimumY + (Footer.Height() - Scaled.ChipExtent) * 0.5f,
                             Scaled.ChipExtent, Scaled.ChipExtent), Hue, 2.0f, CornerAll);

    Surface->TextRun(Footer.MinimumX + Scaled.HeaderPadX + Scaled.ChipExtent + Scaled.PanePad,
                     FootTop, Tinted.Muted, Summary, FootRun);

    ChannelFacets.RecordDeferred();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CHANNEL PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordChannelCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                          const TextureLayerRow& /*Current*/)
{
    const float Pad = Scaled.PanePad;

    // 📐 The card's own filter: a search pill filtering the channel names and the channel-group
    //    facets — the SAME filter pair as the stack page and the scene directory.
    const PlaneExtent Search = Spanning(Extent.MinimumX + Pad, Extent.MinimumY + Pad,
                                        Extent.Width() - Pad * 2.0f, Scaled.SearchHeight);

    RecordSearchPill(Search, Applied);

    // 🔴 The chips region is the reference's channel set, not a view filter. Its
    //    enabled array IS `ChannelOn` for the taken layer, so a chip's cross
    //    disables that channel and the add-menu re-admits it — the behaviour the
    //    filter card already implements and this card was not spending.
    //    📐 `baseColour` is the locked ordinal: the reference marks it `.locked`
    //    and refuses to remove it, because a layer with no base colour has
    //    nothing to composite.
    const FacetDeclaration ChannelFacetCard =
    {
        "Channels", ChannelFacetOptions(), ChannelFacetColours(),
        TextureChannelLimit, 0u
    };

    bool* const Enabled = Applied.ChannelOn[Applied.LayerTaken];

    const float FacetY = ChannelFacets.MeasureHeight(Extent.Width() - Pad * 2.0f, ChannelFacetCard,
                                                     Enabled);

    const PlaneExtent FacetCard = Spanning(Extent.MinimumX + Pad, Search.MaximumY + Pad,
                                           Extent.Width() - Pad * 2.0f, FacetY);

    Discard(ChannelFacets.Record(FacetCard, ChannelFacetCard, Enabled));

    const float BodyTop = FacetCard.MaximumY + Pad;

    if (BodyTop >= Extent.MaximumY - Pad)
        return;

    const PlaneExtent Body = Spanning(Extent.MinimumX + Pad, BodyTop,
                                      Extent.Width() - Pad * 2.0f,
                                      Extent.MaximumY - BodyTop - Pad);

    Surface->Confine(Body);

    const bool Filtering = ChannelRetentionActive(Applied);

    // 📐 Measure the list before drawing it, so the scroll knows how far it may
    //    travel. Fourteen unfolded channel cards stand far past any editor leaf.
    float Content = 0.0f;

    for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
    {
        if (!Enabled[Channel])
            continue;

        if (Filtering && !RunHolds(TextureChannelText(Channel), Applied.Retention))
            continue;

        const float Opened = Interaction->TakenFraction(ChannelFolds[Channel]);

        Content += Scaled.LayerHeadHeight * 0.82f
                 + ChannelBodyHeight(Applied, Channel) * Opened
                 + Scaled.LayerRowGap;
    }

    const float Rolled = AdvanceListScroll(Applied.PropertyListShown, Applied.PropertyListWanted,
                                           Content, Body.Height(), Body);

    float Sweep = Body.MinimumY - Rolled;

    for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
    {
        const char* Name = TextureChannelText(Channel);

        // 🔴 The card listed all fourteen channels whatever the layer had
        //    enabled, so the chips region above it and the card list below it
        //    disagreed on every tick — nine chips over fourteen cards. The
        //    reference lists exactly the enabled set:
        //        CHANNEL_ORDER.filter(Key => Layer.Enabled.includes(Key))
        //    A channel is admitted through the add-menu and removed through its
        //    chip, which is the whole point of the chips region being here.
        if (!Enabled[Channel])
            continue;

        if (Filtering && !RunHolds(Name, Applied.Retention))
            continue;

        const float RowY = Scaled.LayerHeadHeight * 0.82f;

        // 📐 The card is a ground, its head, and — while unfolded — its body. The
        //    fold animates on the shared expansion so it opens over 200 ms rather
        //    than appearing between two frames.
        const float Opened = Controls.OutlineExpansion(ChannelFolds[Channel],
                                                       !Applied.ChannelFolded[Channel], true);

        const float BodyY = ChannelBodyHeight(Applied, Channel) * Opened;
        const PlaneExtent Card = Spanning(Body.MinimumX, Sweep, Body.Width(), RowY + BodyY);

        // 📐 A card wholly past either edge is skipped rather than breaking the
        //    loop: with the list scrolled, cards above the viewport still have to
        //    be stepped over to reach the ones below it.
        if (Sweep + RowY + BodyY < Body.MinimumY || Sweep > Body.MaximumY)
        {
            Sweep += RowY + BodyY + Scaled.LayerRowGap;
            continue;
        }

        Surface->Ground(Card, Tinted.Tile, Scaled.LayerRadius, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.LayerRadius, CornerAll);

        const PlaneExtent Row = Spanning(Card.MinimumX + Scaled.PanePad, Sweep,
                                         Card.Width() - Scaled.PanePad * 2.0f, RowY);

        RecordChannelRow(Row, Applied, Channel);

        if (BodyY > 0.5f)
        {
            const PlaneExtent Inner = Spanning(Card.MinimumX + Scaled.PanePad * 1.5f,
                                               Card.MinimumY + RowY,
                                               Card.Width() - Scaled.PanePad * 3.0f, BodyY);

            Surface->Confine(Inner);
            static_cast<void>(RecordChannelBody(Inner, Applied, Channel));
            Surface->Release();
        }

        Sweep += RowY + BodyY + Scaled.LayerRowGap;
    }

    Surface->Release();

    RecordScrollThumb(Body, Content, Rolled);
}

void TexturingPanel::RecordChannelRow(const PlaneExtent& Row, TexturingContext& Applied,
                                         std::uint32_t Channel)
{
    // 🧩 `.chan-head`: chevron, the classification dot, the title, and the folded
    //    summary — the mode the channel is authored in. That is the whole head.
    // 🔴 It used to carry a trailing switch AND the atlas placement run. Both were
    //    wrong here. The switch was a second way to say what the chip's cross
    //    already says, and since the card list is now the enabled set, throwing
    //    the switch removed the very card holding it — a control that deletes
    //    itself on use. The placement belongs to the preview's second line, which
    //    is where the reference states it (`prev-line2 = ATLAS_COMPONENT[Key]`).
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);

    const bool On     = Applied.ChannelOn[Layer][Channel];
    const bool Folded = Applied.ChannelFolded[Channel];

    // ① The head: chevron, swatch, label, and the mode the card is authored in.
    const PlaneExtent Fold = Spanning(Row.MinimumX,
                                      Row.MinimumY + (Row.Height() - Scaled.ChevronExtent) * 0.5f,
                                      Scaled.ChevronExtent, Scaled.ChevronExtent);

    const bool OnFold = Fold.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Sampled.ContactPressed && OnFold && !Interaction->AnyDisclosed())
        Interaction->Grab(ChannelFolds[Channel], ControlPart::Chevron);

    if (OnFold && Interaction->Released(ChannelFolds[Channel]))
        Applied.ChannelFolded[Channel] = !Folded;

    Surface->Stroke(Folded ? SymbolSubject::ChevronRight : SymbolSubject::ChevronDown,
                    Fold, Tinted.Faint);

    // 📐 `.ch-dot`: the classification dot shrinks and dims while folded —
    //    `transform:scale(.72);opacity:.5`. It rides the same fold fraction the
    //    body does, so head and body settle together rather than one snapping.
    const float Opened = Interaction->TakenFraction(ChannelFolds[Channel]);
    const float Swatch = 9.0f * (0.72f + 0.28f * Opened);

    Surface->Medallion(Fold.MaximumX + Scaled.PanePad + 4.5f,
                       Row.MinimumY + Row.Height() * 0.5f, Swatch * 0.5f,
                       Faded(Covering(Slot.Hue), On ? (0.5f + 0.5f * Opened) : 0.35f));

    const float NameRun = Scaled.RunSecondary;

    // 📐 `.ch-src`: the mode is the FOLDED SUMMARY — it is what the head says in
    //    place of the body while shut, so a shut card still states how the
    //    channel is authored. A derived channel says so and has no mode.
    const char* const Modes[3] = { "Value", "Texture", "Generator" };
    const char* Summary = (Slot.Edit == TextureChannelEdit::Derived)
                        ? "Derived" : Modes[Applied.ChannelMode[Layer][Channel] % 3u];

    const float SumRun  = Scaled.RunFiner;
    const float SumSpan = Surface->MeasureRun(Summary, SumRun, 0.06f);

    // 📐 `.chan-panel:not(.collapsed) .ch-src{opacity:.45;transform:translateX(3px)}`
    //    — the tag fades and slides as the card opens, because an open card's
    //    body already states the mode.
    Surface->TextRunCapitalised(Row.MaximumX - SumSpan + 3.0f * Opened,
                                Row.MinimumY + (Row.Height() - SumRun) * 0.5f,
                                Faded(Tinted.Faint, 1.0f - 0.55f * Opened),
                                Summary, SumRun, 0.06f, false);

    // 📐 The title is truncated against the summary tag rather than drawn over it.
    Surface->TextRunTruncated(Fold.MaximumX + Scaled.PanePad * 2.0f + 9.0f,
                              Row.MinimumY + (Row.Height() - NameRun) * 0.5f,
                              Row.MaximumX - SumSpan - Scaled.PanePad,
                              On ? Tinted.Primary : Tinted.Muted, Slot.Label, NameRun, false);
}

// 🧩 `.chan-prev`: the tile, the mode line, and the atlas lane the deposit lands
//    in. This is what tells the artist what the channel will actually write, and
//    the port carried none of it.
float TexturingPanel::RecordChannelPreview(const PlaneExtent& Extent,
                                              const TexturingContext& Applied,
                                              std::uint32_t Channel)
{
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);

    const float Tile = 34.0f;
    const float Y    = Tile + Scaled.PanePad * 0.5f;

    const PlaneExtent Face = Spanning(Extent.MinimumX, Extent.MinimumY + Scaled.PanePad * 0.5f,
                                      Tile, Tile);

    const bool Derived = Slot.Edit == TextureChannelEdit::Derived;
    const std::uint32_t Mode = Derived ? 3u : (Applied.ChannelMode[Layer][Channel] % 3u);

    // 📐 A Value tile shows the value: the colour itself for a colour channel, or
    //    the grey the scalar resolves to. Texture and Generator have nothing
    //    resolved yet, so they keep the reference's chequer ground.
    if (Mode == 0u && Slot.Edit == TextureChannelEdit::Colour)
    {
        Surface->Ground(Face, Covering(Slot.Hue), 7.0f, CornerAll);
    }
    else if (Mode == 0u)
    {
        // 📐 Normalised against the CHANNEL'S OWN span — refraction index starts
        //    at 1 and the angle runs to 360, so a raw reading would clip both.
        const double Span = Slot.Maximum - Slot.Minimum;
        double Fraction = (Span > 0.0)
                        ? (Applied.ChannelReading[Layer][Channel] - Slot.Minimum) / Span : 0.0;

        Fraction = (Fraction < 0.0) ? 0.0 : (Fraction > 1.0) ? 1.0 : Fraction;

        const std::uint32_t Level = static_cast<std::uint32_t>(Fraction * 255.0 + 0.5);

        Surface->Ground(Face, Covering((Level << 16) | (Level << 8) | Level), 7.0f, CornerAll);
    }
    else
    {
        // 📐 The reference's 8 px chequer, which reads as "no resolved sample".
        Surface->Ground(Face, Tinted.Desk, 7.0f, CornerAll);

        for (int Down = 0; Down < 4; ++Down)
        {
            for (int Along = 0; Along < 4; ++Along)
            {
                if (((Down + Along) & 1) != 0)
                    continue;

                Surface->Ground(Spanning(Face.MinimumX + 1.0f + static_cast<float>(Along) * 8.0f,
                                         Face.MinimumY + 1.0f + static_cast<float>(Down) * 8.0f,
                                         8.0f, 8.0f), Covering(0x1A1A1Au), 0.0f, CornerNone);
            }
        }
    }

    Surface->Edge(Face, Tinted.Hairline, 1.0f, 7.0f, CornerAll);

    char Line[48] = {};
    std::snprintf(Line, sizeof(Line), "%s preview",
                  Derived ? "Central differences"
                          : (Mode == 1u ? "Texture" : Mode == 2u ? "Generator" : "Value"));

    const float FirstRun  = Scaled.RunSmall;
    const float SecondRun = Scaled.RunFiner;
    const float Pair      = FirstRun * 1.4f + SecondRun * 1.4f;
    const float PairTop   = Face.MinimumY + (Tile - Pair) * 0.5f;
    const float RunLead   = Face.MaximumX + Scaled.PanePad;

    Surface->TextRunTruncated(RunLead, PairTop, Extent.MaximumX, Tinted.Muted,
                              Line, FirstRun, false);
    Surface->TextRunTruncated(RunLead, PairTop + FirstRun * 1.4f, Extent.MaximumX,
                              Tinted.Faint, Slot.Placement, SecondRun, false);

    return Y + Scaled.PanePad * 0.5f;
}

// 🧩 How tall an unfolded channel card stands, so the fold has a figure to
//    animate toward rather than snapping open.
float TexturingPanel::ChannelBodyHeight(const TexturingContext& Applied,
                                           std::uint32_t Channel) const
{
    // 🔴 This returned one figure whichever mode stood, so a Generator card with
    //    three knobs was given the same room as a bare Value row and its
    //    parameters were clipped away by the confine.
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);
    const float RowY = Appearance->ControlMeasure.FieldHeight + Scaled.PanePad * 0.5f;

    // 📐 The preview closes every body, so its 34 px tile and its two runs are
    //    part of every measurement.
    // 🔴 The measure and the record had drifted: the record now draws a preview
    //    the measure never allowed for, and a body measured short is CLIPPED by
    //    the confine rather than overflowing visibly — the failure is silent.
    const float PreviewY = 34.0f + Scaled.PanePad;

    if (Slot.Edit == TextureChannelEdit::Derived)
        return Scaled.RunFine * 1.6f + Scaled.PanePad + PreviewY + Scaled.PanePad;

    float Height = RowY + PreviewY;   // the source segment and the preview

    switch (Applied.ChannelMode[Layer][Channel] % 3u)
    {
        case 1u:
        {
            // two slot rows, the label between them
            const float SlotY = Scaled.LayerHeadHeight * 0.9f;
            Height += SlotY * 2.0f + Scaled.RunFiner * 1.6f + Scaled.PanePad * 0.5f;
            break;
        }
        case 2u:
        {
            Height += RowY;   // the picker

            const std::uint32_t Standing = Applied.ChannelGenerator[Layer][Channel];
            if (Standing != TexturingContext::AbsentGenerator)
            {
                // the hairline, the note-and-actions header, then one row a knob
                Height += 1.0f + Scaled.PanePad + 20.0f;
                Height += RowY * static_cast<float>(TextureGeneratorAt(Standing).ParamCount);
            }
            break;
        }
        default:
            Height += RowY;   // the colour bar or the amount row
            break;
    }

    return Height + Scaled.PanePad;
}

// 🧩 SCROLL ② — THE LIST TRAVEL. One list's wheel scroll, eased.
// 🔴 Neither the stack list nor the channels list could be scrolled by any means.
//    The stack runs past its viewport as soon as a folder unfolds and the channels
//    page is fourteen cards deep, so in both cases the tail of the list was simply
//    unreachable — not clipped-and-scrollable, but gone.
float TexturingPanel::AdvanceListScroll(float& Shown, float& Wanted, float Content,
                                           float Viewport, const PlaneExtent& Over)
{
    const float Travel = (Content > Viewport) ? (Content - Viewport) : 0.0f;

    if (Travel <= 0.0f)
    {
        // 📐 A list that fits is parked at its head, so that shortening a list can
        //    never leave it scrolled to somewhere that no longer exists.
        Wanted = 0.0f;
        Shown  = 0.0f;
        return 0.0f;
    }

    if (Over.Encloses(Sampled.PositionX, Sampled.PositionY) && Sampled.WheelY != 0.0f)
    {
        // 🔴 A disclosure owned by a panel that previously occupied this leaf can survive the subject
        //    change because all editor panels share the interaction index. Treating ANY disclosure as a
        //    reason to reject the wheel therefore made the Layer Stack permanently unscrollable after a
        //    visit to the viewport or Scene Directory. A wheel over this list owns the gesture: it closes
        //    whichever transient popup remains and scrolls the visible list in the same tick.
        Interaction->Withdraw();
        Wanted -= Sampled.WheelY * 56.0f;
    }

    if (Wanted < 0.0f)     Wanted = 0.0f;
    if (Wanted > Travel)   Wanted = Travel;

    // 📐 The drawn offset chases the wanted one by a fraction of what remains each
    //    tick. That is the lag: the list keeps moving for a few ticks after the
    //    notch rather than snapping to it.
    const float Remaining = Wanted - Shown;

    if (Remaining > 0.35f || Remaining < -0.35f)
        Shown += Remaining * 0.26f;
    else
        Shown = Wanted;

    return Shown;
}

// 🧩 The thumb, drawn only when there is somewhere to travel — it is the only cue
//    that a list continues past its viewport.
void TexturingPanel::RecordScrollThumb(const PlaneExtent& Viewport, float Content, float Offset)
{
    if (Content <= Viewport.Height() || Viewport.Height() <= 0.0f)
        return;

    const float Fraction = Viewport.Height() / Content;
    const float ThumbY   = Viewport.Height() * Fraction;
    const float Reach    = Content - Viewport.Height();
    const float Along    = (Reach > 0.0f) ? (Offset / Reach) : 0.0f;

    Surface->Ground(Spanning(Viewport.MaximumX - 5.0f,
                             Viewport.MinimumY + (Viewport.Height() - ThumbY) * Along,
                             3.0f, ThumbY),
                    Faded(Tinted.Muted, 0.55f), 1.5f, CornerAll);
}

// 🧩 One titled section of a properties page: a ground, a hairline-separated
//    heading, and the body the caller records inside it. Every card on the mask,
//    decal and folder pages is one of these, so they cannot drift apart the way
//    the four hand-rolled headers did.
PlaneExtent TexturingPanel::RecordSectionCard(const PlaneExtent& Extent, const char* Titled,
                                                 float BodyHeight)
{
    const float HeadY = Scaled.ComponentY;
    const PlaneExtent Card = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(),
                                      HeadY + BodyHeight + Scaled.PanePad);

    Surface->Ground(Card, Tinted.Menu, Scaled.CardRadius, CornerAll);
    Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

    Surface->TextRunCapitalised(Card.MinimumX + Scaled.PanePad * 1.5f,
                                Card.MinimumY + (HeadY - Scaled.RunFine) * 0.5f,
                                Tinted.Muted, Titled, Scaled.RunFine, 0.08f, true);

    Surface->Ground(Spanning(Card.MinimumX, Card.MinimumY + HeadY, Card.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    return Spanning(Card.MinimumX + Scaled.PanePad * 1.5f, Card.MinimumY + HeadY + Scaled.PanePad * 0.5f,
                    Card.Width() - Scaled.PanePad * 3.0f, BodyHeight);
}

// 🧩 The reference's `.iconbtn`: a small bordered square holding one figure,
//    hovering to a lifted tile and, when it is destructive, to the danger hue.
bool TexturingPanel::RecordIconAction(ControlIdentity Target, const PlaneExtent& Cell,
                                         SymbolSubject Glyph, bool Destructive)
{
    const bool Over = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Sampled.ContactPressed && Over && !Interaction->AnyDisclosed())
        Interaction->Grab(Target, ControlPart::Body);

    const bool Fired = Over && Interaction->Released(Target);

    Interaction->DeclareHovered(Target, Over, HoverOver);

    const float Lit = Interaction->HoveredFraction(Target);
    const ThemeToken Danger = Covering(0xE05A5Au);

    Surface->Ground(Cell, Blend(Tinted.MenuLower,
                                Destructive ? Faded(Danger, 0.16f) : Tinted.TileHovered, Lit),
                    5.0f, CornerAll);
    Surface->Edge(Cell, Blend(Tinted.Hairline, Destructive ? Danger : Tinted.HairlineFirm, Lit),
                  1.0f, 5.0f, CornerAll);

    const float Figure = Cell.Height() * 0.58f;

    Surface->Stroke(Glyph, Spanning(Cell.MinimumX + (Cell.Width() - Figure) * 0.5f,
                                    Cell.MinimumY + (Cell.Height() - Figure) * 0.5f,
                                    Figure, Figure),
                    Blend(Tinted.Muted, Destructive ? Danger : Tinted.Primary, Lit));

    return Fired;
}

float TexturingPanel::RecordSlotRow(const PlaneExtent& Extent, ThemeToken Tint,
                                       SymbolSubject Glyph, const char* Naming,
                                       const char* Meta, bool Filled)
{
    // 📐 The reference's `.slot`: a square thumbnail, two stacked runs, and the
    //    actions at the trailing edge.
    const float Thumb = Extent.Height() - 8.0f;

    Surface->Ground(Extent, Tinted.MenuLower, Scaled.LayerRadius, CornerAll);
    Surface->Edge(Extent, Tinted.Hairline, 1.0f, Scaled.LayerRadius, CornerAll);

    const PlaneExtent Tile = Spanning(Extent.MinimumX + 4.0f, Extent.MinimumY + 4.0f, Thumb, Thumb);

    Surface->Ground(Tile, Filled ? Faded(Tint, 0.30f) : Tinted.Menu, 3.0f, CornerAll);

    const float Figure = Thumb * 0.55f;
    Surface->Stroke(Glyph, Spanning(Tile.MinimumX + (Thumb - Figure) * 0.5f,
                                    Tile.MinimumY + (Thumb - Figure) * 0.5f, Figure, Figure),
                    Filled ? Tint : Tinted.Faint);

    const float NameRun = Scaled.RunFine;
    const float MetaRun = Scaled.RunFiner;
    const float Pair    = NameRun * 1.35f + MetaRun * 1.35f;
    const float PairTop = Extent.MinimumY + (Extent.Height() - Pair) * 0.5f;
    const float RunLead = Tile.MaximumX + Scaled.PanePad;

    Surface->TextRun(RunLead, PairTop, Filled ? Tinted.Primary : Tinted.Muted, Naming, NameRun);
    Surface->TextRun(RunLead, PairTop + NameRun * 1.35f, Tinted.Faint, Meta, MetaRun);

    return Extent.Height();
}

float TexturingPanel::RecordValueBody(const PlaneExtent& Extent, TexturingContext& Applied,
                                         std::uint32_t Channel)
{
    // 🧩 Value: a colour field for a colour channel, a magnitude row for a scalar
    //    over the channel's OWN span.
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);
    const float RowY = Appearance->ControlMeasure.FieldHeight + Scaled.PanePad * 0.5f;

    if (Slot.Edit == TextureChannelEdit::Colour)
    {
        // 📐 The reference's `.colorbar`: a round chip, the hex, and a caret.
        const PlaneExtent Bar = Spanning(Extent.MinimumX, Extent.MinimumY,
                                         Extent.Width(), RowY - Scaled.PanePad * 0.5f);

        Surface->Ground(Bar, Tinted.MenuLower, Bar.Height() * 0.5f, CornerAll);
        Surface->Edge(Bar, Tinted.Hairline, 1.0f, Bar.Height() * 0.5f, CornerAll);

        const float Chip = Bar.Height() - 8.0f;
        Surface->Medallion(Bar.MinimumX + 4.0f + Chip * 0.5f,
                           Bar.MinimumY + Bar.Height() * 0.5f, Chip * 0.5f, Covering(Slot.Hue));

        char Hex[10] = {};
        std::snprintf(Hex, sizeof(Hex), "#%06X", static_cast<unsigned>(Slot.Hue));

        Surface->TextRun(Bar.MinimumX + 8.0f + Chip,
                         Bar.MinimumY + (Bar.Height() - Scaled.RunFine) * 0.5f,
                         Tinted.Primary, Hex, Scaled.RunFine);

        const float Mark = Scaled.ChevronExtent * 0.7f;
        Surface->Stroke(SymbolSubject::ChevronDown,
                        Spanning(Bar.MaximumX - Mark - 8.0f,
                                 Bar.MinimumY + (Bar.Height() - Mark) * 0.5f, Mark, Mark),
                        Tinted.Faint);

        return RowY;
    }

    MagnitudeDeclaration Amount;
    Amount.Caption   = "Amount";
    Amount.UnitGlyph = Slot.Unit;
    Amount.Minimum   = Slot.Minimum;
    Amount.Maximum   = Slot.Maximum;
    // 📐 The reference's own rule: "A degree span reads as a whole number; a 0..1
    //    span needs two decimals." The angle is the only channel with a unit and
    //    the only one whose span is wide enough to read whole.
    Amount.Decimals  = (Slot.Maximum - Slot.Minimum >= 10.0) ? 0u : 2u;

    double Reading = Applied.ChannelReading[Layer][Channel];

    if (SharedControls.MagnitudeRow(ChannelOps[Channel],
                                    Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), RowY),
                                    Amount, Reading, false).ReadingAltered)
    {
        Applied.ChannelReading[Layer][Channel] = Reading;
    }

    return RowY;
}

float TexturingPanel::RecordTextureBody(const PlaneExtent& Extent, TexturingContext& Applied,
                                           std::uint32_t Channel)
{
    // 🧩 Texture: the textured-stroke slot, then the imported base beneath it.
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);

    const std::uint32_t Strokes = Applied.ChannelStrokes[Layer][Channel];
    const bool Textured  = Strokes > 0u;
    const bool Imported = Applied.ChannelImported[Layer][Channel];

    const float SlotY = Scaled.LayerHeadHeight * 0.9f;
    float Sweep = Extent.MinimumY;

    char TexturingMeta[64] = {};
    if (Textured)
        std::snprintf(TexturingMeta, sizeof(TexturingMeta), "%u strokes \xC2\xB7 2048 \xC3\x97 2048 atlas",
                      static_cast<unsigned>(Strokes));
    else
        std::snprintf(TexturingMeta, sizeof(TexturingMeta), "Atlas allocates on the first stroke");

    Sweep += RecordSlotRow(Spanning(Extent.MinimumX, Sweep, Extent.Width(), SlotY),
                           Covering(Slot.Hue),
                           Textured ? SymbolSubject::Bristle : SymbolSubject::ExpandFrame,
                           Textured ? "Textured strokes" : "No strokes yet", TexturingMeta, Textured);

    Sweep += Scaled.PanePad * 0.5f;

    Surface->TextRunCapitalised(Extent.MinimumX, Sweep, Tinted.Faint,
                                Imported ? "Imported base" : "Imported base \xE2\x80\x94 optional",
                                Scaled.RunFiner, 0.06f, false);

    Sweep += Scaled.RunFiner * 1.6f;

    Sweep += RecordSlotRow(Spanning(Extent.MinimumX, Sweep, Extent.Width(), SlotY),
                           Covering(Slot.Hue), SymbolSubject::ExpandFrame,
                           Imported ? "albedo_2k.png" : "Import base texture",
                           Imported ? "2048 \xC3\x97 2048 \xC2\xB7 RGBA 8" : "", Imported);

    return Sweep - Extent.MinimumY;
}

float TexturingPanel::RecordGeneratorBody(const PlaneExtent& Extent, TexturingContext& Applied,
                                             std::uint32_t Channel)
{
    // 🧩 Generator: the picker, then — once one stands — its note and its own
    //    parameter rows, each a shared MagnitudeRow over the parameter's span.
    const std::uint32_t Layer = Applied.LayerTaken;
    const float RowY = Appearance->ControlMeasure.FieldHeight + Scaled.PanePad * 0.5f;

    float Sweep = Extent.MinimumY;

    static const char* Options[TextureGeneratorLimit + 1u] = {};
    Options[0] = "Choose generator";
    for (std::uint32_t Each = 0u; Each < TextureGeneratorLimit; ++Each)
        Options[Each + 1u] = TextureGeneratorAt(Each).Label;

    SelectionDeclaration Picker;
    Picker.Caption       = "Generator";
    Picker.Options       = Options;
    Picker.OptionCount   = TextureGeneratorLimit + 1u;

    const std::uint32_t Standing = Applied.ChannelGenerator[Layer][Channel];
    std::uint32_t Taken = (Standing == TexturingContext::AbsentGenerator)
                        ? 0u : (Standing + 1u);

    if (SharedControls.SelectionField(ChannelGenerators[Channel],
                                      Spanning(Extent.MinimumX, Sweep, Extent.Width(), RowY),
                                      Picker, Taken).ReadingAltered)
    {
        if (Taken == 0u)
        {
            Applied.ChannelGenerator[Layer][Channel] = TexturingContext::AbsentGenerator;
        }
        else
        {
            const std::uint32_t Chosen = Taken - 1u;
            Applied.ChannelGenerator[Layer][Channel] = Chosen;

            // 📐 A freshly assigned generator reads its own defaults, not zero.
            const TextureGeneratorEntry& Entry = TextureGeneratorAt(Chosen);
            for (std::uint32_t Each = 0u; Each < Entry.ParamCount; ++Each)
                Applied.ChannelGeneratorParam[Layer][Channel][Each] = Entry.Parameters[Each].Default;
        }
    }

    Sweep += RowY;

    if (Applied.ChannelGenerator[Layer][Channel] == TexturingContext::AbsentGenerator)
        return Sweep - Extent.MinimumY;

    const TextureGeneratorEntry& Entry =
        TextureGeneratorAt(Applied.ChannelGenerator[Layer][Channel]);

    // 📐 `.gen-params` header: a hairline, the note, and the two icon buttons the
    //    reference gives it.
    // 🔴 Neither action was ported. A generator could be chosen but never reset
    //    and never removed — once assigned, the only escape was to change source
    //    mode, which abandoned the parameters rather than clearing them.
    Surface->Ground(Spanning(Extent.MinimumX, Sweep, Extent.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    Sweep += Scaled.PanePad * 0.5f;

    const float Icon = 20.0f;
    const float HeadY = Icon;

    const PlaneExtent Drop = Spanning(Extent.MaximumX - Icon,
                                      Sweep, Icon, Icon);
    const PlaneExtent Wipe = Spanning(Drop.MinimumX - Icon - 4.0f, Sweep, Icon, Icon);

    if (RecordIconAction(ChannelGenReset[Channel], Wipe, SymbolSubject::GearCog, false))
    {
        for (std::uint32_t Each = 0u; Each < Entry.ParamCount; ++Each)
            Applied.ChannelGeneratorParam[Layer][Channel][Each] = Entry.Parameters[Each].Default;
    }

    if (RecordIconAction(ChannelGenDrop[Channel], Drop, SymbolSubject::TrashBin, true))
    {
        Applied.ChannelGenerator[Layer][Channel] = TexturingContext::AbsentGenerator;

        for (std::uint32_t Each = 0u; Each < TextureGeneratorParamMax; ++Each)
            Applied.ChannelGeneratorParam[Layer][Channel][Each] = 0.0;
    }

    Surface->TextRunTruncated(Extent.MinimumX, Sweep + (Icon - Scaled.RunFiner) * 0.5f,
                              Wipe.MinimumX - Scaled.PanePad, Tinted.Faint,
                              Entry.Note, Scaled.RunFiner, false);

    Sweep += HeadY + Scaled.PanePad * 0.5f;

    for (std::uint32_t Each = 0u; Each < Entry.ParamCount; ++Each)
    {
        const TextureGeneratorParameter& Knob = Entry.Parameters[Each];

        MagnitudeDeclaration Declared;
        Declared.Caption   = Knob.Label;
        Declared.UnitGlyph = "";
        Declared.Minimum   = Knob.Minimum;
        Declared.Maximum   = Knob.Maximum;
        // 📐 Every generator parameter in the catalogue spans 0..1, so every one
        //    of them was unreadable at whole-number precision.
        Declared.Decimals  = 2u;

        double Reading = Applied.ChannelGeneratorParam[Layer][Channel][Each];

        if (SharedControls.MagnitudeRow(ChannelParams[Channel][Each],
                                        Spanning(Extent.MinimumX, Sweep, Extent.Width(), RowY),
                                        Declared, Reading, false).ReadingAltered)
        {
            Applied.ChannelGeneratorParam[Layer][Channel][Each] = Reading;
        }

        Sweep += RowY;
    }

    return Sweep - Extent.MinimumY;
}

float TexturingPanel::RecordChannelBody(const PlaneExtent& Extent, TexturingContext& Applied,
                                           std::uint32_t Channel)
{
    // 🔴 This used to draw a Source dropdown and, for a scalar, one Amount row —
    //    the same body whichever mode stood. Texture and Generator offered
    //    nothing at all, so two of the three modes were captions over an empty
    //    card. Each mode now records its own body.
    const std::uint32_t Layer = Applied.LayerTaken;
    const TextureChannelSlot& Slot = TextureChannelAt(Channel);
    const float RowY = Appearance->ControlMeasure.FieldHeight + Scaled.PanePad * 0.5f;

    float Sweep = Extent.MinimumY;

    // ① A derived channel has nothing to author, and says so.
    if (Slot.Edit == TextureChannelEdit::Derived)
    {
        Surface->TextRun(Extent.MinimumX, Sweep + Scaled.PanePad, Tinted.Faint,
                         "Derived from the textured height. No value to author.", Scaled.RunFine);

        Sweep += Scaled.RunFine * 1.6f + Scaled.PanePad;

        Sweep += RecordChannelPreview(Spanning(Extent.MinimumX, Sweep, Extent.Width(),
                                               Extent.MaximumY - Sweep),
                                      Applied, Channel);

        return Sweep - Extent.MinimumY;
    }

    // ② Source — a SEGMENTED control, as the reference's `.segrow` is, not a
    //    dropdown. Three modes, one visible choice.
    static const char* const Sources[3] = { "Value", "Texture", "Generator" };

    SegmentDeclaration Modes;
    Modes.Captions     = Sources;
    Modes.CaptionCount = 3u;

    std::uint32_t Mode = Applied.ChannelMode[Layer][Channel] % 3u;

    const PlaneExtent Segment = Spanning(Extent.MinimumX, Sweep, Extent.Width(),
                                         RowY - Scaled.PanePad * 0.5f);

    if (Controls.SegmentedSelection(ChannelBlends[Channel], Segment, Modes, Mode).ReadingAltered)
        Applied.ChannelMode[Layer][Channel] = Mode;

    Sweep += RowY;

    const PlaneExtent Rest = Spanning(Extent.MinimumX, Sweep, Extent.Width(),
                                      Extent.MaximumY - Sweep);

    if (Mode == 1u)      Sweep += RecordTextureBody(Rest, Applied, Channel);
    else if (Mode == 2u) Sweep += RecordGeneratorBody(Rest, Applied, Channel);
    else                 Sweep += RecordValueBody(Rest, Applied, Channel);

    // ③ The preview closes every body, derived or authored — it is the reference's
    //    last element in ConstructChannelBody and the only place the atlas lane
    //    is stated.
    Sweep += RecordChannelPreview(Spanning(Extent.MinimumX, Sweep, Extent.Width(),
                                           Extent.MaximumY - Sweep),
                                  Applied, Channel);

    return Sweep - Extent.MinimumY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE MASK PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordMaskCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                       const TextureLayerRow& Current)
{
    // 🔴 The old card was four flat rows — Density, Source, Invert, Applies-to —
    //    with the density slider hand-rolled from two Grounds and raw pointer
    //    arithmetic: no readout pill, no unit cell, no hover growth, no keyboard
    //    path. Against References/TPPanel.html renderMaskProperties() it was also
    //    missing the mask BLEND, the whole procedural generator section, and the
    //    empty state. It now spends the shared controls and follows that record.
    const std::uint32_t Layer = Applied.LayerTaken;
    const float Pad  = Scaled.PanePad;
    const float RowY = Appearance->ControlMeasure.FieldHeight + Pad * 0.5f;

    const PlaneExtent Body = Spanning(Extent.MinimumX + Pad, Extent.MinimumY + Pad,
                                      Extent.Width() - Pad * 2.0f,
                                      Extent.Height() - Pad * 2.0f);

    // ① No mask? The reference does not draw an empty inspector; it draws an offer.
    if (!Current.MaskDeclared && !Applied.MaskAttached[Layer])
    {
        const float Run = Scaled.RunSecondary;
        const float Span = Surface->MeasureRun("No mask attached to this layer.", Run, 0.0f);

        Surface->TextRun(Body.MinimumX + (Body.Width() - Span) * 0.5f,
                         Body.MinimumY + 40.0f, Tinted.Muted,
                         "No mask attached to this layer.", Run);

        const float MakeX = 110.0f, MakeY = 28.0f;
        const PlaneExtent Make = Spanning(Body.MinimumX + (Body.Width() - MakeX) * 0.5f,
                                          Body.MinimumY + 40.0f + Run * 2.4f, MakeX, MakeY);

        if (RecordIconAction(MaskRows[0], Make, SymbolSubject::HalfMask, false))
            Applied.MaskAttached[Layer] = true;

        const float MakeRun = Surface->MeasureRun("Create Mask", Scaled.RunFine, 0.0f);
        Surface->TextRun(Make.MinimumX + (MakeX - MakeRun) * 0.5f + 8.0f,
                         Make.MinimumY + (MakeY - Scaled.RunFine) * 0.5f,
                         Tinted.Primary, "Create Mask", Scaled.RunFine);
        return;
    }

    Surface->Confine(Body);

    float Sweep = Body.MinimumY;

    // ② MASK CONFIGURATION — density, blend, and the invert/visible actions.
    {
        const float Inner = RowY * 2.0f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep,
                                                            Body.Width(), 0.0f),
                                                   "Mask configuration", Inner);

        // 📐 The header's two actions sit on the card's own heading line.
        const float Icon = 20.0f;
        const PlaneExtent Eye = Spanning(Body.MaximumX - Pad * 1.5f - Icon,
                                         Sweep + (Scaled.ComponentY - Icon) * 0.5f, Icon, Icon);
        const PlaneExtent Flip = Spanning(Eye.MinimumX - Icon - 4.0f, Eye.MinimumY, Icon, Icon);

        if (RecordIconAction(MaskRows[1], Eye,
                             Applied.MaskVisible[Layer] ? SymbolSubject::EyeOpen
                                                        : SymbolSubject::EyeClosed, false))
        {
            Applied.MaskVisible[Layer] = !Applied.MaskVisible[Layer];
        }

        if (RecordIconAction(MaskRows[2], Flip, SymbolSubject::AlphaMask,
                             Applied.MaskInverted[Layer]))
        {
            Applied.MaskInverted[Layer] = !Applied.MaskInverted[Layer];
        }

        float Inside = Card.MinimumY;

        // 📐 Density through the SHARED magnitude row, so it carries the same
        //    readout pill and unit cell every other slider in the editor has.
        MagnitudeDeclaration Density;
        Density.Caption   = "Density";
        Density.UnitGlyph = "%";
        Density.Minimum   = 0.0;
        Density.Maximum   = 100.0;

        double Reading = static_cast<double>(Applied.MaskDensity[Layer]);

        if (SharedControls.MagnitudeRow(MaskRows[3],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Density, Reading, false).ReadingAltered)
        {
            Applied.MaskDensity[Layer] = static_cast<std::uint32_t>(Reading + 0.5);
        }

        Inside += RowY;

        // 📐 The mask's own blend — the reference offers four.
        static const char* const MaskBlends[4] =
            { "Multiply", "Replace", "Linear Dodge (Add)", "Subtract" };

        SelectionDeclaration Blending;
        Blending.Caption     = "Blend mode";
        Blending.Options     = MaskBlends;
        Blending.OptionCount = 4u;

        std::uint32_t Taken = Applied.MaskBlendTaken[Layer] % 4u;

        if (SharedControls.SelectionField(MaskRows[4],
                                          Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                          Blending, Taken).ReadingAltered)
        {
            Applied.MaskBlendTaken[Layer] = Taken;
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ③ SOURCE — a segmented control over the roster, not a dropdown.
    {
        const float Inner = RowY;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep,
                                                            Body.Width(), 0.0f),
                                                   "Source", Inner);

        SegmentDeclaration Sources;
        Sources.Captions     = TextureMaskSourceNames;
        Sources.CaptionCount = 5u;

        std::uint32_t Taken = Applied.MaskSourceTaken[Layer] % 5u;

        if (Controls.SegmentedSelection(MaskRows[5],
                                     Spanning(Card.MinimumX, Card.MinimumY, Card.Width(),
                                              RowY - Pad * 0.5f),
                                     Sources, Taken).ReadingAltered)
        {
            Applied.MaskSourceTaken[Layer] = Taken;
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ④ PROCEDURAL GENERATOR — only when the source is one.
    if ((Applied.MaskSourceTaken[Layer] % 5u) == 4u)
    {
        const std::uint32_t Standing = Applied.MaskGenerator[Layer];
        const bool Chosen = Standing < TextureGeneratorLimit;
        const std::uint32_t Knobs = Chosen ? TextureGeneratorAt(Standing).ParamCount : 0u;

        const float Inner = RowY + (Chosen ? (20.0f + Pad * 0.5f + RowY * float(Knobs)) : 0.0f);
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep,
                                                            Body.Width(), 0.0f),
                                                   "Procedural generator", Inner);

        static const char* Options[TextureGeneratorLimit + 1u] = {};
        Options[0] = "Choose generator";
        for (std::uint32_t Each = 0u; Each < TextureGeneratorLimit; ++Each)
            Options[Each + 1u] = TextureGeneratorAt(Each).Label;

        SelectionDeclaration Preset;
        Preset.Caption     = "Preset";
        Preset.Options     = Options;
        Preset.OptionCount = TextureGeneratorLimit + 1u;

        std::uint32_t Taken = Chosen ? (Standing + 1u) : 0u;

        float Inside = Card.MinimumY;

        if (SharedControls.SelectionField(MaskRows[6],
                                          Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                          Preset, Taken).ReadingAltered)
        {
            if (Taken == 0u)
            {
                Applied.MaskGenerator[Layer] = TexturingContext::AbsentGenerator;
            }
            else
            {
                const std::uint32_t Picked = Taken - 1u;
                Applied.MaskGenerator[Layer] = Picked;

                const TextureGeneratorEntry& Fresh = TextureGeneratorAt(Picked);
                for (std::uint32_t Each = 0u; Each < Fresh.ParamCount; ++Each)
                    Applied.MaskGeneratorParam[Layer][Each] = Fresh.Parameters[Each].Default;
            }
        }

        Inside += RowY;

        if (Chosen)
        {
            const TextureGeneratorEntry& Entry = TextureGeneratorAt(Standing);

            const float Icon = 20.0f;
            const PlaneExtent Drop = Spanning(Card.MaximumX - Icon, Inside, Icon, Icon);
            const PlaneExtent Wipe = Spanning(Drop.MinimumX - Icon - 4.0f, Inside, Icon, Icon);

            if (RecordIconAction(MaskRows[7], Wipe, SymbolSubject::GearCog, false))
            {
                for (std::uint32_t Each = 0u; Each < Entry.ParamCount; ++Each)
                    Applied.MaskGeneratorParam[Layer][Each] = Entry.Parameters[Each].Default;
            }

            if (RecordIconAction(MaskRows[8], Drop, SymbolSubject::TrashBin, true))
                Applied.MaskGenerator[Layer] = TexturingContext::AbsentGenerator;

            Surface->TextRunCapitalised(Card.MinimumX, Inside + (Icon - Scaled.RunFiner) * 0.5f,
                                        Tinted.Faint, Entry.Note, Scaled.RunFiner, 0.06f, false);

            Inside += Icon + Pad * 0.5f;

            for (std::uint32_t Each = 0u; Each < Entry.ParamCount; ++Each)
            {
                const TextureGeneratorParameter& Knob = Entry.Parameters[Each];

                MagnitudeDeclaration Declared;
                Declared.Caption   = Knob.Label;
                Declared.UnitGlyph = "";
                Declared.Minimum   = Knob.Minimum;
                Declared.Maximum   = Knob.Maximum;
                Declared.Decimals  = 2u;

                double Amount = Applied.MaskGeneratorParam[Layer][Each];

                if (SharedControls.MagnitudeRow(MaskParams[Each],
                                                Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                                Declared, Amount, false).ReadingAltered)
                {
                    Applied.MaskGeneratorParam[Layer][Each] = Amount;
                }

                Inside += RowY;
            }
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ⑤ APPLIES TO CHANNELS — the same facet card the channel page spends, bound
    //    to the mask's own target set rather than to ChannelOn.
    {
        const FacetDeclaration Targets =
        {
            "Applies to channels", ChannelFacetOptions(), ChannelFacetColours(),
            TextureChannelLimit, 0xFFFFFFFFu
        };

        bool* const Marks = Applied.MaskChannel[Layer];
        const float FacetY = MaskFacets.MeasureHeight(Body.Width(), Targets, Marks);

        Discard(MaskFacets.Record(Spanning(Body.MinimumX, Sweep, Body.Width(), FacetY),
                                  Targets, Marks));
    }

    Surface->Release();

    MaskFacets.RecordDeferred();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE SETTINGS PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordSettingsCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                           const TextureLayerRow& Current)
{
    const float Pad = Scaled.PanePad;
    const float RowY = Scaled.RowHeight * 0.82f;
    float Sweep = Extent.MinimumY + Pad;

    Surface->Ground(Spanning(Extent.MinimumX + Pad, Sweep, Extent.Width() - Pad * 2.0f,
                             Scaled.ComponentY),
                    Tinted.Desk, Scaled.CardRadius, CornerAll);
    Surface->Edge(Spanning(Extent.MinimumX + Pad, Sweep, Extent.Width() - Pad * 2.0f,
                           Scaled.ComponentY),
                  Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

    char Caption[48] = {};
    std::snprintf(Caption, sizeof(Caption), "%s settings", TextureLayerText(Current.Classified));

    Surface->TextRunCapitalised(Extent.MinimumX + Pad * 2.0f, Sweep + 7.0f,
                                Tinted.Primary, Caption, Scaled.RunSmall, 0.0f, true);

    Sweep += Scaled.ComponentY + Pad;

    // 📐 The settings rows — a small dummy set per kind: the reference's decal placement, pattern
    //    tiling, generator intensity, or the layer's own blend/opacity. The values are live
    //    scratch in the context; the labels are the kind's own.
    const char* const Labels[4] =
    {
        (Current.Classified == TextureLayerClassification::Decal)   ? "Scale"    :
        (Current.Classified == TextureLayerClassification::Pattern) ? "Tiling U" :
        (Current.Classified == TextureLayerClassification::Generator) ? "Intensity" : "Opacity",
        (Current.Classified == TextureLayerClassification::Decal)   ? "Fade"     :
        (Current.Classified == TextureLayerClassification::Pattern) ? "Tiling V" :
        (Current.Classified == TextureLayerClassification::Generator) ? "Balance" : "Blend",
        (Current.Classified == TextureLayerClassification::Decal)   ? "Height"   :
        (Current.Classified == TextureLayerClassification::Pattern) ? "Jitter"   :
        (Current.Classified == TextureLayerClassification::Generator) ? "Contrast" : "Seed",
        "Resolution"
    };

    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const PlaneExtent Row = Spanning(Extent.MinimumX + Pad, Sweep,
                                         Extent.Width() - Pad * 2.0f, RowY);

        // 🔴 A FIFTH HAND-ROLLED SLIDER. Two Grounds for the track, a hand-written hit
        //    test against a bare 120 px span, and a raw run for the value — no readout
        //    pill, no unit cell, no hover growth, no keyboard path, and a hit test
        //    that ignored the pointer's ordinate entirely, so a press anywhere on the
        //    row's horizontal band seized the slider. The shared row, in the same
        //    Measured arrangement the sun rows and the footer opacity now spend.
        MagnitudeDeclaration Declared;
        Declared.Caption   = Labels[Index];
        Declared.UnitGlyph = "";
        Declared.Minimum   = 0.0;
        Declared.Maximum   = 100.0;
        Declared.Layout    = MagnitudeDeclaration::Arrange::Measured;

        double Reading = static_cast<double>(Applied.SettingAmount[Applied.LayerTaken][Index]);

        if (SharedControls.MagnitudeRow(SettingRows[Index], Row, Declared, Reading,
                                        false).ReadingAltered)
        {
            Applied.SettingAmount[Applied.LayerTaken][Index] =
                static_cast<std::uint32_t>(Reading + 0.5);
        }

        Sweep += RowY + Scaled.LayerRowGap;
    }

    // 📐 The small note carrying the row's detail.
    const float NoteRun = Scaled.RunFine;
    const PlaneExtent Note = Spanning(Extent.MinimumX + Pad, Sweep,
                                      Extent.Width() - Pad * 2.0f, RowY * 1.4f);

    Surface->Ground(Note, Tinted.MenuLower, Scaled.FieldRadius, CornerAll);

    char NoteText[96] = {};
    std::snprintf(NoteText, sizeof(NoteText), "%s \u00B7 %s \u00B7 %u%%",
                  TextureLayerText(Current.Classified),
                  Current.Detail[0] != '\0' ? Current.Detail : Current.Blend,
                  Applied.LayerOpacity[Applied.LayerTaken]);

    Surface->TextRunTruncated(Note.MinimumX + Scaled.PanePad * 1.5f,
                              Note.MinimumY + (Note.Height() - NoteRun) * 0.5f,
                              Note.MaximumX - Scaled.PanePad * 1.5f,
                              Tinted.Faint, NoteText, NoteRun);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE FOLDER (COMBINED) PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECAL PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordDecalCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                        const TextureLayerRow& Current)
{
    // 🔴 This card did not exist. `grep -c RecordDecalCard` returned zero: a decal
    //    row fell through to the CHANNELS page, so selecting one opened a channel
    //    editor for a stencil that has no channels to author, and the transform
    //    and projection a decal does have could not be reached from anywhere.
    const std::uint32_t Layer = Applied.LayerTaken;
    const float Pad  = Scaled.PanePad;
    const float RowY = Appearance->ControlMeasure.FieldHeight + Pad * 0.5f;

    // 📐 A decal seeds to the identity placement — centred, unrotated, unit scale —
    //    rather than to zero, which would collapse it to nothing on first sight.
    if (!Applied.DecalSeeded)
    {
        for (std::uint32_t Each = 0u; Each < TextureLayerLimit; ++Each)
        {
            Applied.DecalPosition[Each][0]  = 0.5;
            Applied.DecalPosition[Each][1]  = 0.5;
            Applied.DecalScale[Each][0]     = 1.0;
            Applied.DecalScale[Each][1]     = 1.0;
            Applied.DecalFadeAngle[Each]    = 65.0;
            Applied.DecalDepthRange[Each]   = 0.25;
            Applied.DecalBackfaceCull[Each] = true;
            Applied.DecalUniformScale[Each] = true;
        }

        Applied.DecalSeeded = true;
    }

    const PlaneExtent Body = Spanning(Extent.MinimumX + Pad, Extent.MinimumY + Pad,
                                      Extent.Width() - Pad * 2.0f, Extent.Height() - Pad * 2.0f);

    Surface->Confine(Body);

    float Sweep = Body.MinimumY;

    // ① PROJECTION — how the stencil is cast onto the surface.
    {
        const float Inner = RowY * 3.0f + Pad * 0.5f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Projection", Inner);

        static const char* const Casts[3] = { "Planar", "Box", "Normal" };

        SegmentDeclaration Cast;
        Cast.Captions     = Casts;
        Cast.CaptionCount = 3u;

        std::uint32_t Taken = Applied.DecalProjection[Layer] % 3u;
        float Inside = Card.MinimumY;

        if (Controls.SegmentedSelection(DecalRows[0],
                                     Spanning(Card.MinimumX, Inside, Card.Width(), RowY - Pad * 0.5f),
                                     Cast, Taken).ReadingAltered)
        {
            Applied.DecalProjection[Layer] = Taken;
        }

        Inside += RowY;

        MagnitudeDeclaration Fade;
        Fade.Caption   = "Fade angle";
        Fade.UnitGlyph = "\xC2\xB0";
        Fade.Minimum   = 0.0;
        Fade.Maximum   = 90.0;

        double Angle = Applied.DecalFadeAngle[Layer];

        if (SharedControls.MagnitudeRow(DecalRows[1],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Fade, Angle, false).ReadingAltered)
        {
            Applied.DecalFadeAngle[Layer] = Angle;
        }

        Inside += RowY;

        MagnitudeDeclaration Depth;
        Depth.Caption   = "Depth range";
        Depth.UnitGlyph = "";
        Depth.Minimum   = 0.0;
        Depth.Maximum   = 1.0;
        Depth.Decimals  = 2u;

        double Range = Applied.DecalDepthRange[Layer];

        if (SharedControls.MagnitudeRow(DecalRows[2],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Depth, Range, false).ReadingAltered)
        {
            Applied.DecalDepthRange[Layer] = Range;
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ② TRANSFORM — where the stencil sits in UV.
    {
        const float Inner = RowY * 3.0f + Pad * 0.5f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Transform", Inner);

        float Inside = Card.MinimumY;

        // 📐 VectorRow renders three axes and a decal is planar, so the third axis
        //    is held at zero and captioned blank rather than adding an AxisCount
        //    field to a shared control for one caller. Stated plainly because it
        //    IS a compromise: the row shows an inert third cell.
        VectorDeclaration Placement;
        Placement.Caption     = "Position";
        Placement.AxisRuns[0] = "U";
        Placement.AxisRuns[1] = "V";
        Placement.AxisRuns[2] = "";
        Placement.Minimum     = 0.0;
        Placement.Maximum     = 1.0;
        Placement.Decimals    = 2u;

        double Place[3] = { Applied.DecalPosition[Layer][0], Applied.DecalPosition[Layer][1], 0.0 };

        if (SharedControls.VectorRow(DecalRows[3],
                                     Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                     Placement, Place).ReadingAltered)
        {
            Applied.DecalPosition[Layer][0] = Place[0];
            Applied.DecalPosition[Layer][1] = Place[1];
        }

        Inside += RowY;

        MagnitudeDeclaration Turn;
        Turn.Caption   = "Rotation";
        Turn.UnitGlyph = "\xC2\xB0";
        Turn.Minimum   = 0.0;
        Turn.Maximum   = 360.0;

        double Spun = Applied.DecalRotation[Layer];

        if (SharedControls.MagnitudeRow(DecalRows[4],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Turn, Spun, false).ReadingAltered)
        {
            Applied.DecalRotation[Layer] = Spun;
        }

        Inside += RowY;

        // 📐 The uniform-scale chain: a small action ahead of the row, so locking
        //    is visible rather than implied.
        const float Chain = 18.0f;
        const PlaneExtent Link = Spanning(Card.MinimumX, Inside + (RowY - Chain) * 0.5f,
                                          Chain, Chain);

        if (RecordIconAction(DecalRows[5], Link,
                             Applied.DecalUniformScale[Layer] ? SymbolSubject::LockClosed
                                                              : SymbolSubject::LockOpen,
                             false))
        {
            Applied.DecalUniformScale[Layer] = !Applied.DecalUniformScale[Layer];
        }

        VectorDeclaration Sizing;
        Sizing.Caption     = "Scale";
        Sizing.AxisRuns[0] = "U";
        Sizing.AxisRuns[1] = "V";
        Sizing.AxisRuns[2] = "";
        Sizing.Minimum     = 0.0;
        Sizing.Maximum     = 8.0;
        Sizing.Decimals    = 2u;

        double Sized[3] = { Applied.DecalScale[Layer][0], Applied.DecalScale[Layer][1], 0.0 };

        if (SharedControls.VectorRow(DecalRows[6],
                                     Spanning(Card.MinimumX + Chain + 4.0f, Inside,
                                              Card.Width() - Chain - 4.0f, RowY),
                                     Sizing, Sized).ReadingAltered)
        {
            // 📐 While the chain is closed the axis that MOVED drives the other,
            //    so a uniform scale stays uniform whichever cell was dragged.
            if (Applied.DecalUniformScale[Layer])
            {
                const bool AlongMoved = Sized[0] != Applied.DecalScale[Layer][0];
                const double Driven = AlongMoved ? Sized[0] : Sized[1];

                Applied.DecalScale[Layer][0] = Driven;
                Applied.DecalScale[Layer][1] = Driven;
            }
            else
            {
                Applied.DecalScale[Layer][0] = Sized[0];
                Applied.DecalScale[Layer][1] = Sized[1];
            }
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ③ SURFACE — the backface rule and the stencil the decal projects.
    {
        const float SlotY = Scaled.LayerHeadHeight * 0.9f;
        const float Inner = RowY + SlotY + Pad * 0.5f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Surface", Inner);

        float Inside = Card.MinimumY;

        SwitchDeclaration Cull;
        Cull.Caption = "Backface cull";

        bool Culling = Applied.DecalBackfaceCull[Layer];

        if (Controls.SwitchToggle(DecalRows[7],
                                  Spanning(Card.MinimumX, Inside, Card.Width(), RowY - Pad * 0.5f),
                                  Cull, Culling).ReadingAltered)
        {
            Applied.DecalBackfaceCull[Layer] = Culling;
        }

        Inside += RowY;

        const char* Naming = (Current.Detail != nullptr && Current.Detail[0] != '\0')
                           ? Current.Detail : "No stencil imported";

        RecordSlotRow(Spanning(Card.MinimumX, Inside, Card.Width(), SlotY),
                      Covering(Current.TexturingHue), SymbolSubject::StencilDecal,
                      Current.Naming, Naming, true);

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ④ COMPOSITE — the decal's own blend and opacity, folded in here rather than
    //    parked behind a Settings tab. 🔴 A decal used to offer two tabs, so its
    //    placement and its compositing were two clicks apart for no reason; with one
    //    panel per selection they belong on the same card.
    {
        const float Inner = RowY * 2.0f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Composite", Inner);

        float Inside = Card.MinimumY;

        SelectionDeclaration Blending;
        Blending.Caption     = "Blend";
        Blending.Options     = TextureBlendNames;
        Blending.OptionCount = TextureBlendCount;

        std::uint32_t Taken = Applied.LayerBlendTaken[Layer] % TextureBlendCount;

        if (SharedControls.SelectionField(DecalRows[8],
                                          Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                          Blending, Taken).ReadingAltered)
        {
            Applied.LayerBlendTaken[Layer] = Taken;
        }

        Inside += RowY;

        MagnitudeDeclaration Opacity;
        Opacity.Caption   = "Opacity";
        Opacity.UnitGlyph = "%";
        Opacity.Minimum   = 0.0;
        Opacity.Maximum   = 100.0;
        Opacity.Layout    = MagnitudeDeclaration::Arrange::Measured;

        double Reading = static_cast<double>(Applied.LayerOpacity[Layer]);

        if (SharedControls.MagnitudeRow(DecalRows[9],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Opacity, Reading, false).ReadingAltered)
        {
            Applied.LayerOpacity[Layer] = static_cast<std::uint32_t>(Reading + 0.5);
        }

        Sweep += Scaled.ComponentY + Inner + Pad;
    }

    Surface->Release();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE FOLDER PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

void TexturingPanel::RecordFolderCard(const PlaneExtent& Extent, TexturingContext& Applied,
                                         const TextureLayerRow* Rows, std::uint32_t RowCount)
{
    // 🔴 This was six flat label/value rows — Stack, Masks, Channel union, Blend,
    //    Opacity, Channels — which is a summary of the folder, not a view of what
    //    its contents jointly write. A folder is the one selection whose whole
    //    point is the combination, and the card said nothing about it.
    //
    // 📐 COVERAGE IS TEXEL FRACTION. Of the atlas the folder's contents could
    //    write on a channel, what share do they actually deposit on? It is a
    //    statement about AREA and deliberately not about visibility: a layer at
    //    5% opacity covering the whole surface reads 100% here, because it has
    //    covered everything. The alternative — weighting each layer by its
    //    opacity — answers "how much of this channel do I SEE from this folder",
    //    which is a different question and would want a second bar rather than a
    //    changed one. The heading says which is drawn so the figure is never
    //    ambiguous on screen.
    const std::uint32_t Layer = Applied.LayerTaken;
    const float Pad  = Scaled.PanePad;
    const float RowY = Appearance->ControlMeasure.FieldHeight + Pad * 0.5f;

    const TextureLayerRow& Folder = Rows[Layer];

    // ① Walk the subtree once, gathering per-channel coverage and contributors.
    float         Coverage[TextureChannelLimit] = {};
    std::uint32_t Contributors[TextureChannelLimit] = {};
    std::uint32_t Layers = 0u;
    std::uint32_t Masks  = 0u;

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        if (Index == Layer)
            continue;

        bool Inside = Rows[Index].Enclosing == Layer;

        if (!Inside)
        {
            std::uint32_t Walking = Rows[Index].Enclosing;

            while (Walking < RowCount)
            {
                if (Walking == Layer) { Inside = true; break; }
                if (Rows[Walking].Depth <= Folder.Depth) break;
                Walking = Rows[Walking].Enclosing;
            }
        }

        if (!Inside)
            continue;

        ++Layers;

        if (Applied.MaskAttached[Index])
            ++Masks;

        for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
        {
            if (!Applied.ChannelOn[Index][Channel])
                continue;

            ++Contributors[Channel];

            // 📐 One layer's own deposit on this channel. A mask scales the area
            //    it reaches — that is a statement about extent, not opacity, so
            //    it belongs in a texel measure.
            float Share = 1.0f;

            if (Applied.MaskAttached[Index])
                Share *= static_cast<float>(Applied.MaskDensity[Index]) / 100.0f;

            // 📐 Coverage composites as union, not as a sum: two layers each
            //    covering 60% of a channel cover 84% of it together, never 120%.
            Coverage[Channel] = Coverage[Channel] + Share - Coverage[Channel] * Share;
        }
    }

    const PlaneExtent Body = Spanning(Extent.MinimumX + Pad, Extent.MinimumY + Pad,
                                      Extent.Width() - Pad * 2.0f, Extent.Height() - Pad * 2.0f);

    Surface->Confine(Body);

    float Sweep = Body.MinimumY;

    // ② COMBINED CHANNEL COVERAGE — one bar per channel any content writes.
    {
        std::uint32_t Present = 0u;
        for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
            if (Contributors[Channel] > 0u) ++Present;

        const float BarRowY = 20.0f;
        const float Inner = (Present > 0u)
                          ? (float(Present) * BarRowY + Scaled.RunFiner * 1.8f)
                          : Scaled.RunSecondary * 2.0f;

        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Combined channel coverage", Inner);

        float Inside = Card.MinimumY;

        if (Present == 0u)
        {
            Surface->TextRun(Card.MinimumX, Inside, Tinted.Faint,
                             "This folder's contents write no channels.", Scaled.RunSecondary);
        }

        // 📐 The label column and the tally column are fixed, so every bar starts
        //    and ends on the same abscissa and the set reads as one chart.
        const float LabelX = 108.0f;
        const float TallyX = 74.0f;
        bool Overlapped = false;

        for (std::uint32_t Channel = 0u; Channel < TextureChannelLimit; ++Channel)
        {
            if (Contributors[Channel] == 0u)
                continue;

            const TextureChannelSlot& Slot = TextureChannelAt(Channel);
            const bool Derived = Slot.Edit == TextureChannelEdit::Derived;

            Surface->Medallion(Card.MinimumX + 4.0f, Inside + BarRowY * 0.5f, 3.5f,
                               Covering(Slot.Hue));

            Surface->TextRunTruncated(Card.MinimumX + 14.0f,
                                      Inside + (BarRowY - Scaled.RunFiner) * 0.5f,
                                      Card.MinimumX + LabelX, Tinted.Muted,
                                      Slot.Label, Scaled.RunFiner, false);

            const float TrackX = Card.MinimumX + LabelX;
            const float TrackW = Card.Width() - LabelX - TallyX;

            if (Derived)
            {
                // 📐 A derived channel has no storage, so it has no coverage to
                //    state — saying 0% would be a lie about a channel that is
                //    always present. It says what it is instead.
                Surface->TextRun(TrackX, Inside + (BarRowY - Scaled.RunFiner) * 0.5f,
                                 Tinted.Faint, "derived from height", Scaled.RunFiner);
            }
            else
            {
                const float BarY = 6.0f;
                const PlaneExtent Track = Spanning(TrackX, Inside + (BarRowY - BarY) * 0.5f,
                                                   TrackW, BarY);

                Surface->Ground(Track, Tinted.Tile, BarY * 0.5f, CornerAll);

                const float Taken = TrackW * Coverage[Channel];

                if (Taken > 1.0f)
                {
                    Surface->Ground(Spanning(Track.MinimumX, Track.MinimumY, Taken, BarY),
                                    Covering(Slot.Hue), BarY * 0.5f, CornerAll);
                }

                char Reading[16] = {};
                std::snprintf(Reading, sizeof(Reading), "%u%%",
                              static_cast<unsigned>(Coverage[Channel] * 100.0f + 0.5f));

                Surface->TextRun(Card.MaximumX - TallyX + 4.0f,
                                 Inside + (BarRowY - Scaled.RunFiner) * 0.5f,
                                 Tinted.Primary, Reading, Scaled.RunFiner);
            }

            char Tally[16] = {};
            std::snprintf(Tally, sizeof(Tally), "%u", static_cast<unsigned>(Contributors[Channel]));

            const float TallySpan = Surface->MeasureRun(Tally, Scaled.RunFiner, 0.0f);

            Surface->TextRun(Card.MaximumX - TallySpan - (Contributors[Channel] > 1u ? 12.0f : 0.0f),
                             Inside + (BarRowY - Scaled.RunFiner) * 0.5f,
                             Tinted.Muted, Tally, Scaled.RunFiner);

            // 📐 More than one contributor means the layers overlap on this
            //    channel, which is the thing worth knowing about a folder.
            if (Contributors[Channel] > 1u)
            {
                Overlapped = true;
                Surface->Stroke(SymbolSubject::LayerMerge,
                                Spanning(Card.MaximumX - 10.0f, Inside + (BarRowY - 9.0f) * 0.5f,
                                         9.0f, 9.0f),
                                Tinted.Faint);
            }

            Inside += BarRowY;
        }

        if (Overlapped)
        {
            Surface->TextRun(Card.MinimumX, Inside + 3.0f, Tinted.Faint,
                             "Stacked figures mark channels more than one layer writes.",
                             Scaled.RunFiner);
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ③ CONTENTS — what is actually inside, with its channel presence.
    {
        const float EntryY = 30.0f;
        const float Inner = (Layers > 0u) ? float(Layers) * EntryY : Scaled.RunSecondary * 2.0f;

        char Heading[40] = {};
        std::snprintf(Heading, sizeof(Heading), "Contents \xC2\xB7 %u", static_cast<unsigned>(Layers));

        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   Heading, Inner);

        float Inside = Card.MinimumY;

        if (Layers == 0u)
        {
            Surface->TextRun(Card.MinimumX, Inside, Tinted.Faint, "This folder is empty.",
                             Scaled.RunSecondary);
        }

        for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        {
            if (Index == Layer)
                continue;

            bool Inclusion = Rows[Index].Enclosing == Layer;

            if (!Inclusion)
            {
                std::uint32_t Walking = Rows[Index].Enclosing;
                while (Walking < RowCount)
                {
                    if (Walking == Layer) { Inclusion = true; break; }
                    if (Rows[Walking].Depth <= Folder.Depth) break;
                    Walking = Rows[Walking].Enclosing;
                }
            }

            if (!Inclusion)
                continue;

            Surface->Ground(Spanning(Card.MinimumX, Inside, Card.Width(), EntryY - 3.0f),
                            Tinted.Tile, 5.0f, CornerAll);

            Surface->Ground(Spanning(Card.MinimumX, Inside + 4.0f, 3.0f, EntryY - 11.0f),
                            Covering(Rows[Index].TagHue), 1.5f, CornerAll);

            Surface->TextRunTruncated(Card.MinimumX + 12.0f, Inside + 4.0f,
                                      Card.MinimumX + 150.0f, Tinted.Primary,
                                      Rows[Index].Naming, Scaled.RunFiner, false);

            char Detail[48] = {};
            std::snprintf(Detail, sizeof(Detail), "%s \xC2\xB7 %u%%",
                          TextureBlendNames[Applied.LayerBlendTaken[Index] % TextureBlendCount],
                          static_cast<unsigned>(Applied.LayerOpacity[Index]));

            Surface->TextRun(Card.MinimumX + 12.0f, Inside + 4.0f + Scaled.RunFiner * 1.35f,
                             Tinted.Faint, Detail, Scaled.RunFiner);

            // 📐 One dot per channel: filled where this row writes it. Reading the
            //    column down tells the artist which layers stack on what.
            float DotX = Card.MaximumX - 8.0f;

            for (std::uint32_t Channel = TextureChannelLimit; Channel-- > 0u; )
            {
                if (Contributors[Channel] == 0u)
                    continue;

                const bool Writes = Applied.ChannelOn[Index][Channel];

                Surface->Medallion(DotX, Inside + (EntryY - 3.0f) * 0.5f, 3.0f,
                                   Writes ? Covering(TextureChannelAt(Channel).Hue)
                                          : Tinted.HairlineFirm);

                DotX -= 9.0f;
            }

            Inside += EntryY;
        }

        Sweep += Scaled.ComponentY + Inner + Pad + Scaled.LayerRowGap;
    }

    // ④ FOLDER — the folder's own composite, which is what it contributes upward.
    {
        const float Inner = RowY * 3.0f + Pad * 0.5f;
        const PlaneExtent Card = RecordSectionCard(Spanning(Body.MinimumX, Sweep, Body.Width(), 0.0f),
                                                   "Folder", Inner);

        float Inside = Card.MinimumY;

        SelectionDeclaration Blending;
        Blending.Caption     = "Blend";
        Blending.Options     = TextureBlendNames;
        Blending.OptionCount = TextureBlendCount;

        std::uint32_t Taken = Applied.LayerBlendTaken[Layer] % TextureBlendCount;

        if (SharedControls.SelectionField(FolderRows[0],
                                          Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                          Blending, Taken).ReadingAltered)
        {
            Applied.LayerBlendTaken[Layer] = Taken;
        }

        Inside += RowY;

        MagnitudeDeclaration Opacity;
        Opacity.Caption   = "Opacity";
        Opacity.UnitGlyph = "%";
        Opacity.Minimum   = 0.0;
        Opacity.Maximum   = 100.0;

        double Reading = static_cast<double>(Applied.LayerOpacity[Layer]);

        if (SharedControls.MagnitudeRow(FolderRows[1],
                                        Spanning(Card.MinimumX, Inside, Card.Width(), RowY),
                                        Opacity, Reading, false).ReadingAltered)
        {
            Applied.LayerOpacity[Layer] = static_cast<std::uint32_t>(Reading + 0.5);
        }

        Inside += RowY;

        SwitchDeclaration Isolate;
        Isolate.Caption = "Isolate";

        bool Alone = Applied.FolderIsolate[Layer];

        if (Controls.SwitchToggle(FolderRows[2],
                                  Spanning(Card.MinimumX, Inside, Card.Width(), RowY - Pad * 0.5f),
                                  Isolate, Alone).ReadingAltered)
        {
            Applied.FolderIsolate[Layer] = Alone;
        }

        Sweep += Scaled.ComponentY + Inner + Pad;
    }

    // 📐 The mask tally is stated where it belongs — beside the contents, not as
    //    a row of its own.
    if (Masks > 0u)
    {
        char Note[48] = {};
        std::snprintf(Note, sizeof(Note), "%u of %u layers carry a mask.",
                      static_cast<unsigned>(Masks), static_cast<unsigned>(Layers));

        Surface->TextRun(Body.MinimumX, Sweep + 2.0f, Tinted.Faint, Note, Scaled.RunFiner);
    }

    Surface->Release();
}

} // namespace Slate
