/*====================================================================================================================================
                                                          SOLIDARCMATCAP.CPP
====================================================================================================================================*/
// 🧩 Matcaps: analytic studio-rig composition of the built-in library, user-picture fitting, layer lookup

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::Workspaces::Sketcher
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

struct Colour
{
    double Red   = 0.0;                                                                      // [0-1] - Display-referred
    double Green = 0.0;                                                                      // [0-1] - Display-referred
    double Blue  = 0.0;                                                                      // [0-1] - Display-referred
};

struct Direction
{
    double X = 0.0;                                                                          // [-] - View right
    double Y = 0.0;                                                                          // [-] - View up
    double Z = 1.0;                                                                          // [-] - Toward the viewer
};

enum class Finish : uint8_t
{
    Metal   = 0,                                                                             // Tinted reflection only
    Brushed = 1,                                                                             // Metal, reflection blurred along screen x
    Plastic = 2,                                                                             // Diffuse base + white Fresnel reflection
    Paint   = 3,                                                                             // Plastic base + sharp clear coat
};

// 📝 Roughness widens every softbox edge (and lowers its peak in proportion to the spread area); Specular scales the
//    dielectric reflection (rubber and clay keep little of it).
struct Recipe
{
    const char* Name      = "";                                                              // [-]   - Library name
    const char* Caption   = "";                                                              // [-]   - Display caption
    const char* Category  = "";                                                              // [-]   - Metal / Plastic / Other
    Finish      Surface   = Finish::Metal;                                                   // [-]   - Optical model
    Colour      Tint      = {};                                                              // [0-1] - Metal F₀ or dielectric albedo
    double      Roughness = 0.0;                                                             // [0-1] - Reflection blur
    double      Specular  = 1.0;                                                             // [0-1] - Dielectric reflection weight
};

// 📝 One softbox of the studio rig: a rectangle seen from the sphere centre along Axis, half extents in the gnomonic plane.
struct Softbox
{
    Direction Axis      = {};                                                                // [-] - Unit direction to the panel centre
    double    HalfWidth = 0.0;                                                               // [-] - Gnomonic half width (along Across)
    double    HalfTall  = 0.0;                                                               // [-] - Gnomonic half height (along Upward)
    double    Power     = 1.0;                                                               // [-] - Peak radiance
    Colour    Light     = {};                                                                // [-] - Panel colour
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr std::array<Recipe, 13> Recipes{ {
    { "Chrome",            "Chrome",              "Metal",   Finish::Metal,   { 0.96, 0.96, 0.97 }, 0.00, 1.0 },
    { "BrushedSteel",      "Brushed steel",       "Metal",   Finish::Brushed, { 0.74, 0.75, 0.77 }, 0.10, 1.0 },
    { "SatinAluminium",    "Satin aluminium",     "Metal",   Finish::Metal,   { 0.88, 0.89, 0.92 }, 0.55, 1.0 },
    { "Gunmetal",          "Gunmetal",            "Metal",   Finish::Metal,   { 0.40, 0.42, 0.46 }, 0.22, 1.0 },
    { "Brass",             "Brass",               "Metal",   Finish::Metal,   { 0.93, 0.76, 0.42 }, 0.16, 1.0 },
    { "Copper",            "Copper",              "Metal",   Finish::Metal,   { 0.95, 0.63, 0.50 }, 0.20, 1.0 },
    { "SatinPlastic",      "Satin plastic",       "Plastic", Finish::Plastic, { 0.60, 0.63, 0.68 }, 0.42, 0.9 },
    { "GlossBlackPlastic", "Gloss black plastic", "Plastic", Finish::Plastic, { 0.035, 0.036, 0.040 }, 0.10, 1.0 },
    { "RedPlastic",        "Red plastic",         "Plastic", Finish::Plastic, { 0.72, 0.07, 0.05 }, 0.18, 1.0 },
    { "Rubber",            "Rubber",              "Other",   Finish::Plastic, { 0.080, 0.080, 0.085 }, 0.80, 0.60 },
    { "Clay",              "Clay",                "Other",   Finish::Plastic, { 0.74, 0.63, 0.54 }, 1.00, 0.06 },
    { "Ceramic",           "Ceramic",             "Other",   Finish::Plastic, { 0.93, 0.93, 0.91 }, 0.04, 1.0 },
    { "BluePaint",         "Blue paint",          "Other",   Finish::Paint,   { 0.04, 0.13, 0.42 }, 0.35, 1.0 },
} };

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Direction Normalise(double X, double Y, double Z) noexcept
{
    const double Size = std::sqrt(X * X + Y * Y + Z * Z);
    return { X / Size, Y / Size, Z / Size };
}

[[nodiscard]] double Dot(const Direction& First, const Direction& Second) noexcept
{
    return First.X * Second.X + First.Y * Second.Y + First.Z * Second.Z;
}

[[nodiscard]] double Smooth(double Low, double High, double Value) noexcept
{
    const double Fraction = std::clamp((Value - Low) / (High - Low), 0.0, 1.0);
    return Fraction * Fraction * (3.0 - 2.0 * Fraction);
}

[[nodiscard]] Colour Mix(const Colour& First, const Colour& Second, double Fraction) noexcept
{
    return { First.Red + (Second.Red - First.Red) * Fraction, First.Green + (Second.Green - First.Green) * Fraction,
             First.Blue + (Second.Blue - First.Blue) * Fraction };
}

[[nodiscard]] Colour Scale(const Colour& Source, double Factor) noexcept
{
    return { Source.Red * Factor, Source.Green * Factor, Source.Blue * Factor };
}

[[nodiscard]] Colour Add(const Colour& First, const Colour& Second) noexcept
{
    return { First.Red + Second.Red, First.Green + Second.Green, First.Blue + Second.Blue };
}

[[nodiscard]] Colour Multiply(const Colour& First, const Colour& Second) noexcept
{
    return { First.Red * Second.Red, First.Green * Second.Green, First.Blue * Second.Blue };
}

// 📝 The studio rig (view space). Key softbox upper left, tall strip right, broad top panel, a rim panel behind (seen in
//    grazing reflections at the silhouette), a low kicker left and a floor bounce card below the camera. Neutral-cool so metals read as engineering stock.
[[nodiscard]] const std::array<Softbox, 6>& StudioRig()
{
    static const std::array<Softbox, 6> Rig{ {
        { Normalise(-0.55, 0.62, 0.56), 0.48, 0.30, 2.10, { 1.00, 0.98, 0.95 } },
        { Normalise(0.86, 0.10, 0.36), 0.10, 0.95, 1.70, { 0.92, 0.96, 1.00 } },
        { Normalise(0.05, 1.00, 0.10), 0.70, 0.55, 1.05, { 1.00, 1.00, 1.00 } },
        { Normalise(0.25, 0.32, -0.92), 0.85, 0.22, 1.10, { 0.95, 0.97, 1.00 } },
        { Normalise(-0.92, -0.08, 0.38), 0.12, 0.45, 0.65, { 1.00, 0.97, 0.92 } },
        { Normalise(0.00, -0.80, 0.60), 0.95, 0.30, 0.38, { 0.95, 0.95, 0.96 } },
    } };
    return Rig;
}

// 📝 Softbox radiance relative to the room seen at normal incidence by a dielectric (F₀ = 0.04): studio lights are far brighter
//    than the walls, so a plastic's 4 % reflection of a softbox still reads as a white highlight over its albedo.
constexpr double DielectricLightGain = 60.0;                                                 // [-]   - HDR softbox / room ratio

// 📝 Radiance seen along the reflected direction: sky / horizon / floor gradient plus every softbox (scaled by Gain), edges
//    softened by Roughness. A panel's peak drops by its area ratio so a rough metal is dimmer, not brighter, than chrome.
[[nodiscard]] Colour Environment(const Direction& Reflected, double Roughness, double Gain = 1.0)
{
    const Colour Zenith        = { 0.50, 0.53, 0.58 };
    const Colour SkyHorizon    = { 0.78, 0.80, 0.83 };
    const Colour GroundHorizon = { 0.42, 0.41, 0.40 };
    const Colour Floor         = { 0.30, 0.30, 0.31 };
    const double Band          = 0.03 + 0.45 * Roughness;
    const Colour Sky           = Mix(SkyHorizon, Zenith, Smooth(0.0, 0.9, Reflected.Y));
    const Colour Ground        = Mix(GroundHorizon, Floor, Smooth(0.0, 0.55, -Reflected.Y));
    Colour       Radiance      = Mix(Ground, Sky, Smooth(-Band, Band, Reflected.Y));
    const double Soft          = 0.015 + 0.75 * Roughness;
    for (const Softbox& Panel : StudioRig())
    {
        const double Facing = Dot(Reflected, Panel.Axis);
        if (Facing <= 0.05) continue;
        // Gnomonic coordinates in the panel plane: Across ⟂ Axis in the horizontal, Upward = Axis × Across.
        const Direction Across = std::abs(Panel.Axis.Y) > 0.95 ? Direction{ 1.0, 0.0, 0.0 } : Normalise(Panel.Axis.Z, 0.0, -Panel.Axis.X);
        const Direction Upward = { Panel.Axis.Y * Across.Z - Panel.Axis.Z * Across.Y, Panel.Axis.Z * Across.X - Panel.Axis.X * Across.Z,
                                   Panel.Axis.X * Across.Y - Panel.Axis.Y * Across.X };
        const double U    = Dot(Reflected, Across) / Facing;
        const double V    = Dot(Reflected, Upward) / Facing;
        const double Mask = Smooth(Panel.HalfWidth + Soft, Panel.HalfWidth - Soft, std::abs(U)) * Smooth(Panel.HalfTall + Soft, Panel.HalfTall - Soft, std::abs(V));
        if (Mask <= 0.0) continue;
        const double Spread = (Panel.HalfWidth * Panel.HalfTall) / ((Panel.HalfWidth + Soft) * (Panel.HalfTall + Soft));
        // Only real lights are HDR; the dim floor bounce card (power < 0.5) stays at room level for every finish.
        const double Boost = Panel.Power >= 0.5 ? Gain : 1.0;
        Radiance = Add(Radiance, Scale(Panel.Light, Mask * Panel.Power * Spread * Boost));
    }
    return Radiance;
}

// 📝 Irradiance on the normal for the diffuse base: hemisphere ambient plus wrapped key / strip / top light.
[[nodiscard]] double Irradiance(const Direction& Normal)
{
    const std::array<Softbox, 6>& Rig = StudioRig();
    const double Ambient = 0.20 + 0.16 * (0.5 + 0.5 * Normal.Y);
    const double Key     = std::max(0.0, (Dot(Normal, Rig[0].Axis) + 0.15) / 1.15) * 0.78;
    const double Strip   = std::max(0.0, Dot(Normal, Rig[1].Axis)) * 0.22;
    const double Top     = std::max(0.0, Dot(Normal, Rig[2].Axis)) * 0.14;
    return Ambient + Key + Strip + Top;
}

[[nodiscard]] Direction Reflect(const Direction& Normal) noexcept
{
    // View vector toward the viewer V = (0, 0, 1): R = 2 (N·V) N − V.
    return { 2.0 * Normal.Z * Normal.X, 2.0 * Normal.Z * Normal.Y, 2.0 * Normal.Z * Normal.Z - 1.0 };
}

[[nodiscard]] double Schlick(double Cosine) noexcept
{
    const double Grazing = 1.0 - std::clamp(Cosine, 0.0, 1.0);
    return Grazing * Grazing * Grazing * Grazing * Grazing;
}

// 📝 Linear up to 0.8, then a smooth shoulder that approaches 1 (highlights keep their shape instead of clipping flat).
[[nodiscard]] double Shoulder(double Value) noexcept
{
    if (Value <= 0.8) return std::max(0.0, Value);
    return 0.8 + 0.2 * (1.0 - std::exp(-(Value - 0.8) / 0.2));
}

[[nodiscard]] Colour Shade(const Recipe& Look, const Direction& Normal, double StreakRow)
{
    const double Fresnel = Schlick(Normal.Z);
    switch (Look.Surface)
    {
    case Finish::Metal:
    {
        const Colour Reflectance = Add(Look.Tint, Scale(Add(Colour{ 1.0, 1.0, 1.0 }, Scale(Look.Tint, -1.0)), Fresnel));
        return Multiply(Environment(Reflect(Normal), Look.Roughness), Reflectance);
    }
    case Finish::Brushed:
    {
        // Anisotropic: average the reflection over normals tilted along screen x (±0.28), then fine streaks per row.
        Colour Sum;
        constexpr int Reach = 14;
        for (int Offset = -Reach; Offset <= Reach; ++Offset)
        {
            const double    Tilt   = 0.28 * double(Offset) / double(Reach);
            const Direction Tilted = Normalise(Normal.X + Tilt, Normal.Y, Normal.Z);
            Sum = Add(Sum, Environment(Reflect(Tilted), Look.Roughness));
        }
        const Colour Reflectance = Add(Look.Tint, Scale(Add(Colour{ 1.0, 1.0, 1.0 }, Scale(Look.Tint, -1.0)), Fresnel));
        return Scale(Multiply(Sum, Reflectance), (1.0 + 0.07 * StreakRow) / double(2 * Reach + 1));
    }
    case Finish::Plastic:
    case Finish::Paint:
    {
        // Reflection = F₀ · HDR softboxes + (F − F₀) · room: the normal-incidence part sees the lights at full strength,
        // the grazing Fresnel part sees the plain environment (no blown-out silhouette).
        const double    Damping   = Look.Specular * (1.0 - 0.45 * Look.Roughness);
        const double    Weight    = (0.04 + 0.96 * Fresnel) * Damping;
        const Direction Reflected = Reflect(Normal);
        const Colour    Gloss     = Add(Scale(Environment(Reflected, Look.Roughness, DielectricLightGain), 0.04 * Damping),
                                        Scale(Environment(Reflected, Look.Roughness), 0.96 * Fresnel * Damping));
        Colour          Result    = Add(Scale(Look.Tint, Irradiance(Normal) * (1.0 - Weight)), Gloss);
        if (Look.Surface == Finish::Paint)
        {
            const double Coat = 0.04 + 0.96 * Fresnel;
            Result = Add(Add(Scale(Result, 1.0 - Coat), Scale(Environment(Reflected, 0.02, DielectricLightGain), 0.04)),
                         Scale(Environment(Reflected, 0.02), 0.96 * Fresnel));
        }
        return Result;
    }
    }
    return {};
}

// 📝 Deterministic row noise for brushed streaks: hashed per row, smoothed over three rows, zero mean in [−1, 1].
[[nodiscard]] std::vector<double> StreakRows(uint32_t Extent)
{
    std::vector<double> Raw(Extent + 2), Rows(Extent);
    uint32_t State = 0x9E3779B9u;
    for (double& Value : Raw)
    {
        State ^= State << 13;
        State ^= State >> 17;
        State ^= State << 5;
        Value = double(State & 0xFFFFu) / 32767.5 - 1.0;
    }
    for (uint32_t Row = 0; Row < Extent; ++Row) Rows[Row] = 0.25 * Raw[Row] + 0.5 * Raw[Row + 1] + 0.25 * Raw[Row + 2];
    return Rows;
}

// 📝 Hue-preserving tone map: the brightest channel goes through the shoulder and the others scale with it (a brass
//    highlight stays brass), then colours far above 1 bleach toward white like an over-exposed light source.
[[nodiscard]] Colour Tone(const Colour& Value) noexcept
{
    const double Peak = std::max({ Value.Red, Value.Green, Value.Blue });
    if (Peak <= 0.8) return Value;
    const double Ratio  = Shoulder(Peak) / Peak;
    const Colour Kept   = Scale(Value, Ratio);
    const double Bleach = Smooth(2.0, 6.0, Peak);
    return Mix(Kept, Colour{ Shoulder(Peak), Shoulder(Peak), Shoulder(Peak) }, Bleach);
}

[[nodiscard]] uint8_t Quantise(double Value) noexcept
{
    return uint8_t(std::lround(std::clamp(Value, 0.0, 1.0) * 255.0));
}

[[nodiscard]] const Recipe* FindRecipe(const std::string& Name) noexcept
{
    for (const Recipe& Look : Recipes)
        if (Name == Look.Name) return &Look;
    return nullptr;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const std::vector<SketcherMatcapDescription>& SketcherBuiltInMatcaps()
{
    static const std::vector<SketcherMatcapDescription> Library = []
    {
        std::vector<SketcherMatcapDescription> Built;
        for (const Recipe& Look : Recipes) Built.push_back({ Look.Name, Look.Caption, Look.Category });
        return Built;
    }();
    return Library;
}

SketcherPicture ComposeSketcherMatcap(const std::string& Name, uint32_t Extent)
{
    SketcherPicture Picture;
    Picture.Path = "builtin:" + Name;
    const Recipe* Look = FindRecipe(Name);
    if (!Look)
    {
        Picture.Refusal = "no built-in matcap named " + Name;
        return Picture;
    }
    if (Extent < 8 || Extent > 2048)
    {
        Picture.Refusal = "matcap extent must lie in 8 … 2048 px";
        return Picture;
    }
    Picture.Width  = Extent;
    Picture.Height = Extent;
    Picture.Pixels.resize(size_t(Extent) * Extent * 4);
    const std::vector<double> Streaks = StreakRows(Extent);
    for (uint32_t Row = 0; Row < Extent; ++Row)
    {
        for (uint32_t Column = 0; Column < Extent; ++Column)
        {
            double X = 2.0 * (double(Column) + 0.5) / double(Extent) - 1.0;
            double Y = 1.0 - 2.0 * (double(Row) + 0.5) / double(Extent);
            // Outside the disc: the rim colour (normal pulled back just inside the silhouette).
            const double Radius = std::hypot(X, Y);
            constexpr double Rim = 0.999;
            if (Radius > Rim)
            {
                X *= Rim / Radius;
                Y *= Rim / Radius;
            }
            const Direction Normal = { X, Y, std::sqrt(std::max(0.0, 1.0 - X * X - Y * Y)) };
            const Colour    Value  = Tone(Shade(*Look, Normal, Streaks[Row]));
            uint8_t* Pixel = &Picture.Pixels[(size_t(Row) * Extent + Column) * 4];
            Pixel[0] = Quantise(Value.Red);
            Pixel[1] = Quantise(Value.Green);
            Pixel[2] = Quantise(Value.Blue);
            Pixel[3] = 255;
        }
    }
    return Picture;
}

SketcherPicture ComposeSketcherStudioMatcap(uint32_t Extent)
{
    SketcherPicture Picture;
    Picture.Path   = std::string("builtin:") + SketcherStudioMatcap;
    Picture.Width  = Extent;
    Picture.Height = Extent;
    Picture.Pixels.resize(size_t(Extent) * Extent * 4);
    const Direction KeyAxis  = Normalise(-0.45, 0.55, 0.70);
    const Direction FillAxis = Normalise(0.60, -0.10, 0.80);
    for (uint32_t Row = 0; Row < Extent; ++Row)
    {
        for (uint32_t Column = 0; Column < Extent; ++Column)
        {
            double X = 2.0 * (double(Column) + 0.5) / double(Extent) - 1.0;
            double Y = 1.0 - 2.0 * (double(Row) + 0.5) / double(Extent);
            const double Radius = std::hypot(X, Y);
            if (Radius > 0.999)
            {
                X *= 0.999 / Radius;
                Y *= 0.999 / Radius;
            }
            // Mirrors SketcherSurfaceFragment, layer 0, with the view-space normal View = n.
            const Direction View = { X, Y, std::sqrt(std::max(0.0, 1.0 - X * X - Y * Y)) };
            const double Key   = std::clamp(Dot(View, KeyAxis), 0.0, 1.0);
            const double Fill  = std::clamp(Dot(View, FillAxis), 0.0, 1.0);
            const double Sky   = 0.5 + 0.5 * View.Y;
            const double Rim   = std::pow(1.0 - std::clamp(View.Z, 0.0, 1.0), 3.0);
            const Direction Reflected = { 2.0 * View.Z * View.X, 2.0 * View.Z * View.Y, 2.0 * View.Z * View.Z - 1.0 };
            const double Gloss = std::pow(std::clamp(Dot(Reflected, KeyAxis), 0.0, 1.0), 24.0) * 0.18;
            const Colour Albedo = { 0.70, 0.72, 0.76 };
            const Colour Value  = Add(Add(Scale(Albedo, 0.22 + 0.18 * Sky + Key * 0.62), Scale(Colour{ 0.62, 0.70, 0.82 }, Fill * 0.16)),
                                      Colour{ Rim * 0.10 + Gloss, Rim * 0.10 + Gloss, Rim * 0.10 + Gloss });
            uint8_t* Pixel = &Picture.Pixels[(size_t(Row) * Extent + Column) * 4];
            Pixel[0] = uint8_t(std::lround(std::clamp(Value.Red, 0.0, 1.0) * 255.0));
            Pixel[1] = uint8_t(std::lround(std::clamp(Value.Green, 0.0, 1.0) * 255.0));
            Pixel[2] = uint8_t(std::lround(std::clamp(Value.Blue, 0.0, 1.0) * 255.0));
            Pixel[3] = 255;
        }
    }
    return Picture;
}

SketcherPicture FitSketcherMatcapPicture(const SketcherPicture& Source, uint32_t Extent)
{
    SketcherPicture Fitted;
    Fitted.Path = Source.Path;
    if (!Source.Refusal.empty())
    {
        Fitted.Refusal = Source.Refusal;
        return Fitted;
    }
    if (Source.Width == 0 || Source.Height == 0 || Source.Pixels.size() != size_t(Source.Width) * Source.Height * 4)
    {
        Fitted.Refusal = "matcap picture has no pixels";
        return Fitted;
    }
    // Centre square crop, then bilinear sampling at texel centres.
    const double Side    = double(std::min(Source.Width, Source.Height));
    const double OffsetX = 0.5 * (double(Source.Width) - Side);
    const double OffsetY = 0.5 * (double(Source.Height) - Side);
    Fitted.Width  = Extent;
    Fitted.Height = Extent;
    Fitted.Pixels.resize(size_t(Extent) * Extent * 4);
    const auto Texel = [&](int Column, int Row, int Channel)
    {
        Column = std::clamp(Column, 0, int(Source.Width) - 1);
        Row    = std::clamp(Row, 0, int(Source.Height) - 1);
        return double(Source.Pixels[(size_t(Row) * Source.Width + size_t(Column)) * 4 + size_t(Channel)]);
    };
    for (uint32_t Row = 0; Row < Extent; ++Row)
    {
        for (uint32_t Column = 0; Column < Extent; ++Column)
        {
            const double SampleX = OffsetX + (double(Column) + 0.5) * Side / double(Extent) - 0.5;
            const double SampleY = OffsetY + (double(Row) + 0.5) * Side / double(Extent) - 0.5;
            const int    Left    = int(std::floor(SampleX));
            const int    Top     = int(std::floor(SampleY));
            const double Across  = SampleX - double(Left);
            const double Down    = SampleY - double(Top);
            uint8_t* Pixel = &Fitted.Pixels[(size_t(Row) * Extent + Column) * 4];
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                const double Upper = Texel(Left, Top, Channel) * (1.0 - Across) + Texel(Left + 1, Top, Channel) * Across;
                const double Lower = Texel(Left, Top + 1, Channel) * (1.0 - Across) + Texel(Left + 1, Top + 1, Channel) * Across;
                Pixel[Channel] = uint8_t(std::lround(std::clamp(Upper * (1.0 - Down) + Lower * Down, 0.0, 255.0)));
            }
            Pixel[3] = 255;
        }
    }
    return Fitted;
}

std::string SketcherMatcapKey(const SketcherAppearance& Appearance)
{
    std::string Key;
    for (const SketcherCustomMatcap& Custom : Appearance.CustomMatcaps) Key += Custom.Name + "\t" + Custom.Path + "\n";
    return Key;
}

SketcherMatcapLibrary AssembleSketcherMatcaps(const SketcherAppearance& Appearance)
{
    SketcherMatcapLibrary Library;
    Library.Key = SketcherMatcapKey(Appearance);
    Library.Names.push_back(SketcherStudioMatcap);
    Library.Captions.push_back("Studio");
    Library.Refusals.push_back({});
    // 📝 Built-ins are fixed: composed once per process and copied.
    static const std::vector<SketcherPicture> BuiltIn = []
    {
        std::vector<SketcherPicture> Composed;
        for (const Recipe& Look : Recipes) Composed.push_back(ComposeSketcherMatcap(Look.Name));
        return Composed;
    }();
    for (size_t Index = 0; Index < Recipes.size(); ++Index)
    {
        Library.Names.push_back(Recipes[Index].Name);
        Library.Captions.push_back(Recipes[Index].Caption);
        Library.Pictures.push_back(BuiltIn[Index]);
        Library.Refusals.push_back({});
    }
    for (const SketcherCustomMatcap& Custom : Appearance.CustomMatcaps)
    {
        if (Library.Names.size() > SketcherMatcapLayerLimit) break;
        SketcherPicture Fitted = FitSketcherMatcapPicture(DecodeSketcherPicture(Custom.Path));
        Library.Names.push_back(Custom.Name);
        Library.Captions.push_back(Custom.Name);
        Library.Refusals.push_back(Fitted.Refusal);
        if (!Fitted.Refusal.empty())
        {
            // A refused picture keeps its layer (mid grey) so later layers stay aligned with their names.
            Fitted.Width  = SketcherMatcapExtent;
            Fitted.Height = SketcherMatcapExtent;
            Fitted.Pixels.assign(size_t(SketcherMatcapExtent) * SketcherMatcapExtent * 4, 128);
            for (size_t Index = 3; Index < Fitted.Pixels.size(); Index += 4) Fitted.Pixels[Index] = 255;
        }
        Library.Pictures.push_back(std::move(Fitted));
    }
    return Library;
}

uint32_t SketcherMatcapLayer(const SketcherMatcapLibrary& Library, const std::string& Name) noexcept
{
    for (size_t Index = 0; Index < Library.Names.size(); ++Index)
        if (Library.Names[Index] == Name) return uint32_t(std::min<size_t>(Index, SketcherMatcapLayerLimit));
    return 0;
}

const std::string& SketcherBodyMatcap(const SketcherAppearance& Appearance, const std::string& BodyKey)
{
    const auto Found = Appearance.BodyMatcaps.find(BodyKey);
    return Found != Appearance.BodyMatcaps.end() ? Found->second : Appearance.BodyMatcap;
}

const std::string& SketcherFaceMatcap(const SketcherAppearance& Appearance, const std::string& BodyKey, const std::string& FaceName)
{
    const auto Found = Appearance.FaceMatcaps.find(FaceName);
    return Found != Appearance.FaceMatcaps.end() ? Found->second : SketcherBodyMatcap(Appearance, BodyKey);
}

bool SketcherMatcapKnownCondition(const SketcherAppearance& Appearance, const std::string& Name)
{
    if (Name == SketcherStudioMatcap || FindRecipe(Name)) return true;
    return std::any_of(Appearance.CustomMatcaps.begin(), Appearance.CustomMatcaps.end(), [&](const SketcherCustomMatcap& Custom) { return Custom.Name == Name; });
}

} // namespace Frontier::Workspaces::Sketcher
