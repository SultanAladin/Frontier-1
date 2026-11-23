//============================================================================================================================================
//                                                       SCENEDIRECTORYPANEL.CPP
//============================================================================================================================================
// 🧩 The editor's scene directory — leaf content for the editor's workspace panels.
//    See SceneDirectoryPanel.h for the boundary this panel observes: it records
//    ONLY inside the leaf extents the editor's panel chrome hands over, and it
//    never draws the validation shell's rail, top bar or layer stack.

#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectoryPanel.h"
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
constexpr float  RunLeading = 1.30f;  // [-]  - leading-tight, for a two-run header

/// 🧩 Holds an coordinate between two bounds.
constexpr float Held(float Coordinate, float Minimum, float Maximum)
{
    return (Coordinate < Minimum) ? Minimum : (Coordinate > Maximum) ? Maximum : Coordinate;
}

/// 🧩 One coordinate of the way from a departed figure to an incoming one.
constexpr float Between(float Previous, float Incoming, float Fraction)
{
    return Previous + (Incoming - Previous) * Fraction;
}

/// 🧩 One tone travelling toward another, for a hover that grows rather than switching.
constexpr std::uint8_t BlendChannel(std::uint8_t Previous, std::uint8_t Incoming, float Fraction)
{
    return static_cast<std::uint8_t>(static_cast<float>(Previous) +
                                     (static_cast<float>(Incoming) -
                                      static_cast<float>(Previous)) * Fraction + 0.5f);
}

void AddViewportOrientationGizmo(OverlayGeometry& Overlay, const PlaneExtent& Extent)
{
    const auto Packed = [](std::uint32_t Red, std::uint32_t Green, std::uint32_t Blue, std::uint32_t Alpha)
    {
        return PackOverlayColour(Red, Green, Blue, Alpha);
    };
    const float Right = Extent.MaximumX - 24.0f;
    const float Top = Extent.MinimumY + 28.0f;
    const float Size = 46.0f;
    const float X = Right - Size;
    const float Y = Top;
    const std::uint32_t FaceTop = Packed(0xF8u, 0xFAu, 0xFCu, 116u);
    const std::uint32_t FaceFront = Packed(0x5Bu, 0x8Cu, 0xFFu, 142u);
    const std::uint32_t FaceSide = Packed(0xFCu, 0x5Au, 0x5Au, 142u);
    const std::uint32_t Edge = Packed(0xFFu, 0xFFu, 0xFFu, 210u);
    const float A[2] = { X, Y + 15.0f };
    const float B[2] = { X + Size * 0.55f, Y };
    const float C[2] = { X + Size, Y + 13.0f };
    const float D[2] = { X + Size * 0.46f, Y + 28.0f };
    const float E[2] = { X + Size * 0.46f, Y + Size };
    const float F[2] = { X + Size, Y + Size * 0.70f };
    const float G[2] = { X, Y + Size * 0.72f };
    Overlay.AddTriangle(A[0], A[1], B[0], B[1], C[0], C[1], FaceTop);
    Overlay.AddTriangle(A[0], A[1], C[0], C[1], D[0], D[1], FaceTop);
    Overlay.AddTriangle(A[0], A[1], D[0], D[1], E[0], E[1], FaceFront);
    Overlay.AddTriangle(A[0], A[1], E[0], E[1], G[0], G[1], FaceFront);
    Overlay.AddTriangle(D[0], D[1], C[0], C[1], F[0], F[1], FaceSide);
    Overlay.AddTriangle(D[0], D[1], F[0], F[1], E[0], E[1], FaceSide);
    Overlay.AddLine(A[0], A[1], B[0], B[1], Edge, 1.2f);
    Overlay.AddLine(B[0], B[1], C[0], C[1], Edge, 1.2f);
    Overlay.AddLine(C[0], C[1], F[0], F[1], Edge, 1.2f);
    Overlay.AddLine(F[0], F[1], E[0], E[1], Edge, 1.2f);
    Overlay.AddLine(E[0], E[1], G[0], G[1], Edge, 1.2f);
    Overlay.AddLine(G[0], G[1], A[0], A[1], Edge, 1.2f);
    Overlay.AddLine(A[0], A[1], D[0], D[1], Edge, 1.2f);
    Overlay.AddLine(D[0], D[1], C[0], C[1], Edge, 1.2f);
    Overlay.AddLine(D[0], D[1], E[0], E[1], Edge, 1.2f);
}

constexpr ThemeToken Blend(ThemeToken Previous, ThemeToken Incoming, float Fraction)
{
    const float Bounded = (Fraction < 0.0f) ? 0.0f : (Fraction > 1.0f) ? 1.0f : Fraction;

    return ThemeToken{ BlendChannel(Previous.Red,     Incoming.Red,     Bounded),
                       BlendChannel(Previous.Green,   Incoming.Green,   Bounded),
                       BlendChannel(Previous.Blue,    Incoming.Blue,    Bounded),
                       BlendChannel(Previous.Opacity, Incoming.Opacity, Bounded) };
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

// 📐 The editor's filter categories, and the subject each entity maps to. The category ordinals are
//    the FacetPanel's option ordinals; `EditorFacetOf` is what makes a facet a filter rather than a
//    label — a row is shown only when its category's facet is enabled.
constexpr std::uint32_t EditorFacetCount = 9u;   // [-] - mirrors SceneDirectoryContext::FacetCount

const char* const EditorFacetOptions[EditorFacetCount] =
{
    "Objects", "Lights", "Cameras", "Folders", "Audio",
    "Particles", "Triggers", "Environment", "Layers"
};

const ThemeToken EditorFacetColours[EditorFacetCount] =
{
    Covering(0x3B82F6u),   // [-] - Objects, blue
    Covering(0xF59E0Bu),   // [-] - Lights, amber
    Covering(0xEC4899u),   // [-] - Cameras, pink
    Covering(0x8A8A8Au),   // [-] - Folders, grey
    Covering(0x8B5CF6u),   // [-] - Audio, violet
    Covering(0x10B981u),   // [-] - Particles, green
    Covering(0xEF4444u),   // [-] - Triggers, red
    Covering(0x38BDF8u),   // [-] - Environment, cyan
    Covering(0x14B8A6u)    // [-] - Layers, teal
};

std::uint32_t EditorFacetOf(EntitySubject Subject)
{
    switch (Subject)
    {
        case EntitySubject::Level:
        case EntitySubject::Grouping: return 3u;   // [-] - Folders
        case EntitySubject::Actor:
        case EntitySubject::Script:   return 0u;   // [-] - Objects
        case EntitySubject::Camera:   return 2u;   // [-] - Cameras
        case EntitySubject::Illuminant:
        case EntitySubject::Sun:      return 1u;   // [-] - Lights
        case EntitySubject::Audio:    return 4u;   // [-] - Audio
        case EntitySubject::Particle: return 5u;   // [-] - Particles
        case EntitySubject::Trigger:  return 6u;   // [-] - Triggers
        case EntitySubject::Sky:      return 7u;   // [-] - Environment
        default:                      return 0u;
    }
}

/// 🧩 Whether the search and the facets jointly retain one row.
bool RowRetained(const SceneDirectoryContext& Applied, const EntityRow& Row)
{
    const bool Searching = Applied.EntityRetention[0] != '\0';

    if (Searching)
    {
        if (!RunHolds(Row.Naming, Applied.EntityRetention) &&
            !RunHolds(Row.Tagged, Applied.EntityRetention))
            return false;
    }

    for (std::uint32_t Facet = 0u; Facet < SceneDirectoryContext::FacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return Applied.FacetEnabled[EditorFacetOf(Row.Subject)];
    }

    return true;
}

/// 🧩 Whether the search or any facet is active at all.
bool RetentionActive(const SceneDirectoryContext& Applied)
{
    if (Applied.EntityRetention[0] != '\0')
        return true;

    for (std::uint32_t Facet = 0u; Facet < SceneDirectoryContext::FacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return true;
    }

    return false;
}

}   // namespace

ShellMetric ScaleShellLengths(float Factor)
{
    const float Applied = (Factor > 0.0f) ? Factor : 1.0f;
    ShellMetric Scaled;

    // 📝 Every member is a length, so the whole record is scaled uniformly. The two run figures that are
    //    durations live outside it precisely so that this stays true and no member has to be excepted.
    float* const Lengths = &Scaled.TopBarHeight;
    const std::uint32_t Count = static_cast<std::uint32_t>(sizeof(ShellMetric) / sizeof(float));

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        Lengths[Index] *= Applied;

    return Scaled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTITY CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

SymbolSubject EntityGlyph(EntitySubject Subject)
{
    switch (Subject)
    {
        case EntitySubject::Level:      return SymbolSubject::CubeSolid;
        case EntitySubject::Grouping:   return SymbolSubject::FolderClosed;
        case EntitySubject::Actor:      return SymbolSubject::CubeSolid;
        case EntitySubject::Camera:     return SymbolSubject::CameraAperture;
        case EntitySubject::Illuminant: return SymbolSubject::BulbFilament;
        case EntitySubject::Audio:      return SymbolSubject::SpeakerCone;
        case EntitySubject::Particle:   return SymbolSubject::ParticleEmit;
        case EntitySubject::Trigger:    return SymbolSubject::CrosshairCentre;
        case EntitySubject::Script:     return SymbolSubject::CodeBrackets;
        case EntitySubject::Sun:        return SymbolSubject::SunDirectional;
        case EntitySubject::Sky:        return SymbolSubject::SunDirectional;
        default:                        return SymbolSubject::CubeSolid;
    }
}

ThemeToken EntityHue(EntitySubject Subject)
{
    // 📐 The reference's `COLORS` record, transcribed verbatim from `components/GameOutliner.tsx`.
    switch (Subject)
    {
        case EntitySubject::Level:      return Covering(0xEAB308u);
        case EntitySubject::Grouping:   return Covering(0x8A8A8Au);
        case EntitySubject::Actor:      return Covering(0x3B82F6u);
        case EntitySubject::Camera:     return Covering(0xEC4899u);
        case EntitySubject::Illuminant: return Covering(0xF59E0Bu);
        case EntitySubject::Audio:      return Covering(0x8B5CF6u);
        case EntitySubject::Particle:   return Covering(0x10B981u);
        case EntitySubject::Trigger:    return Covering(0xEF4444u);
        case EntitySubject::Script:     return Covering(0x06B6D4u);
        case EntitySubject::Sun:        return Covering(0xF59E0Bu);
        case EntitySubject::Sky:        return Covering(0x38BDF8u);
        default:                        return Covering(0x8A8A8Au);
    }
}

const char* EntityText(EntitySubject Subject)
{
    switch (Subject)
    {
        case EntitySubject::Level:      return "Level";
        case EntitySubject::Grouping:   return "Folder";
        case EntitySubject::Actor:      return "Actor";
        case EntitySubject::Camera:     return "Camera";
        case EntitySubject::Illuminant: return "Light";
        case EntitySubject::Audio:      return "Audio";
        case EntitySubject::Particle:   return "Particle";
        case EntitySubject::Trigger:    return "Trigger";
        case EntitySubject::Script:     return "Script";
        case EntitySubject::Sun:        return "Sun";
        case EntitySubject::Sky:        return "Sky";
        default:                        return "Entity";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SceneDirectoryPanel::ConstructSceneDirectoryPanel(ControlIndex& IncomingInteraction,
                                             MotionIntegrator& Integrator,
                                             RecordingSurface& IncomingSurface,
                                             const ThemeProfile& Resolved)
{
    if (Interaction != nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the scene directory panel is already constructed" });
    }

    Interaction     = &IncomingInteraction;
    Motion     = &Integrator;
    this->Surface = &IncomingSurface;
    Appearance = &Resolved;

    if (!Controls.ConstructControlPanel(IncomingInteraction, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the shared inspector controls were rejected" });
    }

    if (!EnvironmentControls.ConstructComponents(IncomingInteraction, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the environment slider controls were rejected" });
    }

    // 🔴 Every identity is claimed here and none inside a tick. A control registered mid-tick receives a fresh
    //    fade and reads as though the pointer had just arrived over it, once per tick, forever.
    if (!Facets.ConstructFacetPanel(Integrator, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the scene directory filter was rejected" });
    }

    ControlIdentity* const Every[] =
    {
        &InspectorStrip,
        &OutlineStrip,
        &InspectCall,
        &DirectoryCall,
        &TransferBack,
        &TransferCalls[0], &TransferCalls[1], &TransferExecute,
        &TransferArrows[0], &TransferArrows[1],
        &TransferFormatOptions[0], &TransferFormatOptions[1], &TransferFormatOptions[2], &TransferFormatOptions[3],
        &TransferFormatOptions[4], &TransferFormatOptions[5], &TransferFormatOptions[6], &TransferFormatOptions[7],
        &TransferFormatOptions[8], &TransferFormatOptions[9], &TransferFormatOptions[10],
        &TransferFields[0], &TransferFields[1], &TransferFields[2], &TransferFields[3],
        &TransferOptions[0], &TransferOptions[1], &TransferOptions[2], &TransferOptions[3],
        &TransferOptions[4], &TransferOptions[5], &TransferOptions[6], &TransferOptions[7],
        &TransferOptions[8], &TransferOptions[9], &TransferOptions[10], &TransferOptions[11],
        &TransferOptions[12], &TransferOptions[13], &TransferOptions[14],
        &TransferOptions[15], &TransferOptions[16], &TransferOptions[17],
        &TransferCardFolds[0], &TransferCardFolds[1], &TransferCardFolds[2],
        &TransferCardFolds[3], &TransferCardFolds[4], &TransferCardFolds[5],
        &TransferCardFields[0], &TransferCardFields[1], &TransferCardFields[2],
        &TransferCardFields[3], &TransferCardFields[4],
        &BookmarkSave,
        &BookmarkRecall,
        &BookmarkRetire,
        &SearchField,
        &EnvironmentQuality,
        &CardFolds[0], &CardFolds[1], &CardFolds[2], &CardFolds[3],
        // 🔴 A control that is never registered resolves to nothing, so its row
        //    would draw but refuse every contact — the axis would look editable
        //    and silently ignore the drag.
        &CardFields[0][0], &CardFields[0][1], &CardFields[0][2], &CardFields[0][3],
        &CardFields[1][0], &CardFields[1][1], &CardFields[1][2], &CardFields[1][3],
        &CardFields[2][0], &CardFields[2][1], &CardFields[2][2], &CardFields[2][3],
        &CardFields[3][0], &CardFields[3][1], &CardFields[3][2], &CardFields[3][3]
    };

    for (ControlIdentity* Identity : Every)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        *Identity = Registered.Resolve();
    }

    for (auto& Card : EnvironmentSliders)
    {
        for (ControlIdentity& Identity : Card)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
            if (!Registered.Resolved)
                return Deliver<bool>::Refuse(Registered.Error);
            Identity = Registered.Resolve();
        }
    }

    for (ControlIdentity& Identity : BookmarkNames)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        Identity = Registered.Resolve();
    }

    // 📐 The leaf's page travel. Registered here, never mid-tick.
    if (const Deliver<bool> Pages = OutlinePages.ConstructSlidingPages(Integrator, 0u);
        !Pages.Resolved)
        return Pages;

    {
        const Deliver<std::uint32_t> Eased = Integrator.RegisterEased(1.0);
        if (!Eased.Resolved)
            return Deliver<bool>::Refuse(Eased.Error);
        TransferMotion = Eased.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        const Deliver<std::uint32_t> Eased = Integrator.RegisterEased(1.0);

        if (!Eased.Resolved)
            return Deliver<bool>::Refuse(Eased.Error);

        InspectorMotion[Index] = Eased.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < SceneDirectoryContext::EntityLimit; ++Index)
    {
        ControlIdentity* const Rows[] =
        {
            &RowContacts[Index], &RowDisclosures[Index], &RowPresences[Index],
            &DetailOptions[Index][0], &DetailOptions[Index][1], &DetailOptions[Index][2]
        };

        for (ControlIdentity* Identity : Rows)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
            if (!Registered.Resolved)
                return Deliver<bool>::Refuse(Registered.Error);

            *Identity = Registered.Resolve();
        }
    }

    Reapply(Resolved);

    return Deliver<bool>::Result(true);
}

void SceneDirectoryPanel::Advance(const PointerCondition& Contact, double Elapsed,
                                   SceneDirectoryContext& Applied, bool TabPressed,
                                   const ModifierCondition& Modifiers)
{
    Sampled = Contact;
    Modified = Modifiers;
    Controls.Advance(Contact, Elapsed);
    // 📝 Sampled, never advanced: the tick owner advances the shared index exactly once, and a
    //    second advance would retire the release before the panel reads it.
    EnvironmentControls.Sample(Contact);
    Facets.Advance(Contact, Elapsed);

    // Tab alternates the directory and inspector. the removed revision feed is no longer an intermediate destination;
    // camera bookmarks remain reachable through the camera inspector's explicit tab.
    const std::uint32_t PriorPage = Applied.OutlinePage;
    if (TabPressed)
    {
        Applied.OutlinePage = Applied.OutlinePage == 0u ? 1u : 0u;
        Applied.OutlineInspectorTab = 0u;
    }
    if (Applied.OutlinePage != PriorPage)
    {
        Interaction->Withdraw();
        Interaction->Abandon();
    }

    // 📝 The search field's taken state, reported to the host so it feeds the seam's typed run only
    //    while the field actually holds the contact — the validation shell's filter captured every
    //    keystroke unconditionally, which is the "search box not working" a gate fixes.
    Applied.SearchTaken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);
}

void SceneDirectoryPanel::Reset()
{
    Controls.Reset();
    Facets.Reset();
    OutlinePages.Reset();
    TransferOverflow.Reset();

    Interaction     = nullptr;
    Motion     = nullptr;
    Surface    = nullptr;
    Appearance = nullptr;
    Sampled    = {};
    Tinted     = {};
    Scaled     = {};

    for (std::uint32_t Card = 0u; Card < SceneDirectoryContext::CardLimit; ++Card)
        for (std::uint32_t Field = 0u; Field < EnvironmentFieldLimit; ++Field)
        {
            EnvironmentArmed[Card][Field] = false;
            EnvironmentFrom[Card][Field] = 0.0;
        }
}

void SceneDirectoryPanel::Reapply(const ThemeProfile& Resolved)
{
    Appearance = &Resolved;

    // 🔴 The colours are taken from the appearance rather than left at their compiled-in declarations, which is
    //    what carries a theme into the panel. `Reapply` is already called at construction and again on every
    //    display change, so the one line below is also the whole of the panel's theme response.
    Tinted = Resolved.Shell;

    // 📝 The scene directory is authored at engine density, exactly as the shell's own is, so it takes the
    //    display and artist factors rather than the control sheet's authored reduction.
    const float Applied = static_cast<float>(Resolved.Measure.DisplayScale)
                        * Resolved.ControlMeasure.ArtistFactor;

    Scaled = ScaleShellLengths(Applied);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE VIEWPORT SKY
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::RecordViewportSky(const PlaneExtent& Extent, const SceneDirectoryContext& Applied)
{
    if (Applied.SkyTextureIdentity == 0u && Applied.GeometryTextureIdentity == 0u)
        return;

    // 📐 The dome is direction-indexed, and the viewport reads it through a PERSPECTIVE geometry rather
    //    than a single cropped quad: a quad maps azimuth and elevation linearly onto the leaf, which
    //    stretches the sun into an ellipse the moment the leaf's aspect differs from the camera's and
    //    compresses the horizon where perspective should widen it. The geometry samples the dome per
    //    screen vertex along the true pinhole ray, so the sun stays round and the horizon reads at any
    //    leaf aspect — the same projection the grid below uses, which is what keeps the two aligned.
    constexpr std::uint32_t GeometryColumns = 64u;
    constexpr std::uint32_t GeometryRows    = 36u;
    constexpr std::uint32_t QuadCount   = GeometryColumns * GeometryRows;
    constexpr std::uint32_t VertexCount = QuadCount * 4u;
    constexpr std::uint32_t IndexCount  = QuadCount * 6u;

    const float CentreX = Extent.MinimumX + Extent.Width()  * 0.5f;
    const float CentreY = Extent.MinimumY + Extent.Height() * 0.5f;

    const double HalfV = Applied.ViewportSkyCamera.FieldOfViewDegrees * 0.5 * 3.14159265358979323846 / 180.0;
    const double Aspect = static_cast<double>(Extent.Width()) / static_cast<double>(Extent.Height());
    const double TanHalfV = std::tan(HalfV);
    const double TanHalfH = TanHalfV * Aspect;

    const double Yaw   = Applied.ViewportSkyCamera.AzimuthDegrees   * 3.14159265358979323846 / 180.0;
    const double Pitch = Applied.ViewportSkyCamera.ElevationDegrees * 3.14159265358979323846 / 180.0;
    const double CosP = std::cos(Pitch);
    const double SinP = std::sin(Pitch);
    const double SinY = std::sin(Yaw);
    const double CosY = std::cos(Yaw);

    // 📐 The camera basis, the same convention CameraComponent integrates: forward along the view, right
    //    across it, up the cross product.
    const double Forward[3] = { CosP * SinY, SinP, CosP * CosY };
    const double Right[3]   = { CosY, 0.0, -SinY };
    const double Up[3]      = { -SinP * SinY, CosP, -SinP * CosY };

    float Positions[VertexCount * 2u];
    float UVs[VertexCount * 2u];
    std::uint32_t Indices[IndexCount];

    std::uint32_t VertexOffset = 0u;
    std::uint32_t IndexOffset  = 0u;

    for (std::uint32_t Row = 0u; Row < GeometryRows; ++Row)
    {
        const float RowY0 = Extent.MinimumY + static_cast<float>(Row)       / static_cast<float>(GeometryRows) * Extent.Height();
        const float RowY1 = Extent.MinimumY + static_cast<float>(Row + 1u)  / static_cast<float>(GeometryRows) * Extent.Height();

        for (std::uint32_t Column = 0u; Column < GeometryColumns; ++Column)
        {
            const float ColX0 = Extent.MinimumX + static_cast<float>(Column)      / static_cast<float>(GeometryColumns) * Extent.Width();
            const float ColX1 = Extent.MinimumX + static_cast<float>(Column + 1u) / static_cast<float>(GeometryColumns) * Extent.Width();

            const float CornerScreenX[4] = { ColX0, ColX1, ColX0, ColX1 };
            const float CornerScreenY[4] = { RowY0, RowY0, RowY1, RowY1 };

            float CornerU[4];
            float CornerV[4];

            for (std::uint32_t Corner = 0u; Corner < 4u; ++Corner)
            {
                const double NdcX = (static_cast<double>(CornerScreenX[Corner]) - CentreX) / (Extent.Width()  * 0.5);
                const double NdcY = (static_cast<double>(CentreY) - CornerScreenY[Corner]) / (Extent.Height() * 0.5);

                double Ray[3] = { NdcX * TanHalfH, NdcY * TanHalfV, 1.0 };
                const double Length = std::sqrt(Ray[0] * Ray[0] + Ray[1] * Ray[1] + Ray[2] * Ray[2]);
                Ray[0] /= Length;
                Ray[1] /= Length;
                Ray[2] /= Length;

                const double DirectionX = Right[0] * Ray[0] + Up[0] * Ray[1] + Forward[0] * Ray[2];
                const double DirectionY = Right[1] * Ray[0] + Up[1] * Ray[1] + Forward[1] * Ray[2];
                const double DirectionZ = Right[2] * Ray[0] + Up[2] * Ray[1] + Forward[2] * Ray[2];

                const double Azimuth   = std::atan2(DirectionX, DirectionZ);
                const double Elevation = std::asin(std::clamp(DirectionY, -1.0, 1.0));

                CornerU[Corner] = static_cast<float>(Azimuth / (2.0 * 3.14159265358979323846) + 0.5);
                CornerV[Corner] = static_cast<float>(std::clamp(0.5 - Elevation / 3.14159265358979323846, 0.0, 1.0));
            }

            // 📐 Locally unwrap U coordinates for this quad relative to Corner 0 so that no quad
            //    interpolates across the ±180° azimuth seam backwards. Being per-quad local, this
            //    completely prevents branch-cut line artifacts near the zenith / pole.
            for (std::uint32_t Corner = 1u; Corner < 4u; ++Corner)
            {
                while (CornerU[Corner] - CornerU[0] > 0.5f)
                    CornerU[Corner] -= 1.0f;
                while (CornerU[Corner] - CornerU[0] < -0.5f)
                    CornerU[Corner] += 1.0f;
            }

            const std::uint32_t Base = VertexOffset;

            for (std::uint32_t Corner = 0u; Corner < 4u; ++Corner)
            {
                Positions[(VertexOffset + Corner) * 2u]      = CornerScreenX[Corner];
                Positions[(VertexOffset + Corner) * 2u + 1u]  = CornerScreenY[Corner];
                UVs[(VertexOffset + Corner) * 2u]            = CornerU[Corner];
                UVs[(VertexOffset + Corner) * 2u + 1u]        = CornerV[Corner];
            }
            VertexOffset += 4u;

            Indices[IndexOffset++] = Base + 0u;
            Indices[IndexOffset++] = Base + 2u;
            Indices[IndexOffset++] = Base + 1u;
            Indices[IndexOffset++] = Base + 1u;
            Indices[IndexOffset++] = Base + 2u;
            Indices[IndexOffset++] = Base + 3u;
        }
    }

    if (Applied.SkyTextureIdentity != 0u)
        Surface->ImageGeometry(Applied.SkyTextureIdentity, Positions, UVs, VertexCount, Indices, IndexCount);

    // Geometry has already passed through the camera projection during hardware visibility rasterisation.
    // Its resolve is transparent where no surface won, so this ordinary image overlays the direction-indexed
    // atmosphere without replacing it and remains clipped to the viewport leaf by the recording surface.
    if (Applied.GeometryTextureIdentity != 0u)
        Surface->Image(Extent, Applied.GeometryTextureIdentity);
}


// 🔴 RecordGroundGrid is withdrawn. The ground lattice is solved per pixel
//    by WorkspaceOverlayFragment.slang mode 3 — see the note in WorkspaceOverlayFragment.slang.
//    The host pushes the camera to the overlay pass through OverlayGroundPose
//    instead of handing it screen-space line segments.

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE OUTLINER
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
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
    const bool HasSecondary = Secondary != nullptr && Secondary[0] != '\0';
    const float PairHeight = HasSecondary ? (Run * RunLeading + SecondaryRun * RunLeading) : Run;
    const float PairLead   = Extent.MinimumY + (Extent.Height() - PairHeight) * 0.5f;
    const float RunLead    = Crest.MaximumX + Pad;

    Surface->TextRunTruncated(RunLead, PairLead, Extent.MaximumX - RunLead - Pad,
                              Tinted.Primary, Titled, Run, true);
    if (HasSecondary)
        Surface->TextRunTruncated(RunLead, PairLead + Run * RunLeading,
                                  Extent.MaximumX - RunLead - Pad, Hue, Secondary, SecondaryRun, false);
}

void SceneDirectoryPanel::RecordTransfer(const PlaneExtent& Extent, SceneDirectoryContext& Applied)
{
    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);
    const float Pad = Scaled.PanePad;
    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, SymbolSubject::FolderClosed, Tinted.EntityAccent,
                     Applied.TransferMode == 0u ? "Import Scene" : "Export Scene",
                     "Scene transfer setup");

    const PlaneExtent Back = Spanning(Extent.MinimumX + Pad, Header.MaximumY + Pad, 82.0f, 28.0f);
    const bool OnBack = Back.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (Sampled.ContactPressed && OnBack)
    {
        Interaction->Withdraw();
        Applied.OutlinePage = 0u;
    }
    Interaction->DeclareHovered(TransferBack, OnBack, HoverOver);
    Surface->Ground(Back, OnBack ? Tinted.TileHovered : Tinted.Tile, 14.0f, CornerAll);
    Surface->Edge(Back, Tinted.HairlineFirm, 1.0f, 14.0f, CornerAll);
    Surface->TextRun(Back.MinimumX + 18.0f, Back.MinimumY + 7.0f, Tinted.Primary, "Back", Scaled.RunSecondary);

    const PlaneExtent ScrollViewport = { Extent.MinimumX, Back.MaximumY + 8.0f,
                                         Extent.MaximumX, Extent.MaximumY };
    const float PageScroll = TransferOverflow.Advance(Sampled, ScrollViewport, 880.0f);
    Surface->Confine(ScrollViewport);

    float Y = Back.MaximumY + 28.0f - PageScroll;
    Surface->TextRun(Extent.MinimumX + Pad, Y, Tinted.Primary,
                     Applied.TransferMode == 0u ? "Import format" : "Export format", Scaled.RunPrimary);
    Y += 28.0f;

    static const char* const ImportFormats[] =
        { "Codex", "FBX", "glTF", "GLB", "OBJ", "USD", "USDZ", "DAE", "STL", "PLY", "ABC" };
    static const char* const ExportFormats[] =
        { "Codex", "FBX", "glTF", "GLB", "OBJ", "USD", "USDZ", "DAE", "STL", "ABC" };
    const char* const* Formats = Applied.TransferMode == 0u ? ImportFormats : ExportFormats;
    const std::uint32_t FormatCount = Applied.TransferMode == 0u ? 11u : 10u;
    if (Applied.TransferFormat >= FormatCount)
        Applied.TransferFormat = FormatCount - 1u;

    const PlaneExtent Left = Spanning(Extent.MinimumX + Pad, Y + 20.0f, 42.0f, 42.0f);
    const PlaneExtent Right = Spanning(Extent.MaximumX - Pad - 42.0f, Y + 20.0f, 42.0f, 42.0f);
    const PlaneExtent Rail = { Left.MaximumX + 12.0f, Y, Right.MinimumX - 12.0f, Y + 84.0f };
    const double Fraction = Motion->Eased(TransferMotion).Current();
    const double Scroll = TransferFrom + (TransferTarget - TransferFrom) * Fraction;

    const auto Arrow = [&](std::uint32_t Index, const PlaneExtent& Cell, const char* Mark)
    {
        const bool On = Cell.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && On)
        {
            Interaction->Withdraw();
            if (Index == 0u && Applied.TransferFormat > 0u) --Applied.TransferFormat;
            if (Index == 1u && Applied.TransferFormat + 1u < FormatCount) ++Applied.TransferFormat;
            TransferFrom = Scroll;
            TransferTarget = static_cast<double>(Applied.TransferFormat) * 144.0;
            Motion->Eased(TransferMotion).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
        }
        Interaction->DeclareHovered(TransferArrows[Index], On, HoverOver);
        Surface->Ground(Cell, On ? Tinted.TileHovered : Tinted.Tile, 21.0f, CornerAll);
        Surface->Edge(Cell, Tinted.HairlineFirm, 1.0f, 21.0f, CornerAll);
        Surface->TextRun(Cell.MinimumX + 16.0f, Cell.MinimumY + 11.0f, Tinted.Primary, Mark, Scaled.RunPrimary);
    };
    Arrow(0u, Left, "<");
    Arrow(1u, Right, ">");

    Surface->Confine(Rail);
    for (std::uint32_t Index = 0u; Index < FormatCount; ++Index)
    {
        const PlaneExtent OptionTile = Spanning(Rail.MinimumX + 4.0f + Index * 144.0f - static_cast<float>(Scroll),
                                            Rail.MinimumY, 132.0f, 80.0f);
        const bool On = Rail.Encloses(Sampled.PositionX, Sampled.PositionY) &&
                        OptionTile.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && On)
        {
            Interaction->Withdraw();
            Applied.TransferFormat = Index;
            TransferFrom = Scroll;
            TransferTarget = static_cast<double>(Index) * 144.0;
            Motion->Eased(TransferMotion).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
        }
        Interaction->DeclareHovered(TransferFormatOptions[Index], On, HoverOver);
        const bool Taken = Applied.TransferFormat == Index;
        Surface->Ground(OptionTile, Taken ? Tinted.RowTaken : (On ? Tinted.TileHovered : Tinted.Tile), 12.0f, CornerAll);
        Surface->Edge(OptionTile, Taken ? Tinted.EntityAccent : Tinted.Hairline, 1.0f, 12.0f, CornerAll);
        Surface->TextRun(OptionTile.MinimumX + 16.0f, OptionTile.MinimumY + 16.0f, Tinted.Primary, Formats[Index], Scaled.RunPrimary);
        Surface->TextRun(OptionTile.MinimumX + 16.0f, OptionTile.MinimumY + 48.0f, Tinted.Muted,
                         Applied.TransferMode == 0u ? "Scene input" : "Scene output", Scaled.RunFine);
    }
    Surface->Release();

    Y += 112.0f;
    Surface->TextRun(Extent.MinimumX + Pad, Y, Tinted.Muted,
                     Applied.TransferMode == 0u ? "Choose a scene format to import." : "Choose a scene format to export.",
                     Scaled.RunSecondary);
    Surface->TextRun(Extent.MinimumX + Pad, Y + 24.0f, Tinted.Faint,
                     "Enter the source path below, then confirm the transfer at the end of this panel.", Scaled.RunFine);
    Y += 56.0f;

    const auto Field = [&](std::uint32_t Index, const PlaneExtent& Row, const char* Label,
                           const char* Placeholder, char* Run, std::uint32_t Limit)
    {
        Surface->TextRun(Row.MinimumX, Row.MinimumY + 9.0f, Tinted.Muted, Label, Scaled.RunSecondary);
        EditableTextDeclaration Declared;
        Declared.Placeholder = Placeholder;
        EnvironmentControls.EditableText(TransferFields[Index],
                                         Spanning(Row.MinimumX + 84.0f, Row.MinimumY,
                                                  Row.Width() - 84.0f, 34.0f),
                                         Declared, Run, Limit);
    };

    const float Half = (Extent.Width() - Pad * 3.0f) * 0.5f;
    Field(0u, Spanning(Extent.MinimumX + Pad, Y, Half, 34.0f), "Name", "Scene name",
          Applied.TransferName, 64u);
    Field(1u, Spanning(Extent.MinimumX + Pad * 2.0f + Half, Y, Half, 34.0f), "Tags", "tag, tag",
          Applied.TransferTags, 96u);
    Y += 42.0f;
    Field(2u, Spanning(Extent.MinimumX + Pad, Y, Extent.Width() - Pad * 2.0f, 34.0f),
          Applied.TransferMode == 0u ? "Source" : "Destination",
          Applied.TransferMode == 0u ? "path/to/model.obj" : "path/to/scene.codex",
          Applied.TransferLocation, 96u);
    Y += 46.0f;

    // Scale uses the same reusable editable form as Name and Location, with the established
    // calculator grammar (`10 exp 5`, `10^5`, `3.5*320`) enabled.
    {
        const PlaneExtent Row = Spanning(Extent.MinimumX + Pad, Y,
                                         Extent.Width() - Pad * 2.0f, 34.0f);
        Surface->TextRun(Row.MinimumX, Row.MinimumY + 9.0f, Tinted.Muted,
                         "Scale", Scaled.RunSecondary);
        const EditableTextVerdict Verdict = EnvironmentControls.EditableText(
            TransferFields[3u],
            Spanning(Row.MinimumX + 84.0f, Row.MinimumY, Row.Width() - 84.0f, 34.0f),
            EditableTextDeclaration{ "1.0", false, true },
            Applied.TransferScaleRun, sizeof(Applied.TransferScaleRun));
        if (Verdict.Accepted)
            Applied.TransferScale = std::strtod(Applied.TransferScaleRun, nullptr);
        Y += 42.0f;
    }

    static const char* const Forward[] = { "-Z", "+Z", "+X", "-X" };
    static const char* const Up[] = { "+Y", "+Z" };
    static const char* const Normals[] = { "Custom", "Calculate", "Face" };

    // 📐 Every axis field spends the same compact width. The two columns are anchored to opposite
    // sides of the page, so a wide transfer leaf does not stretch a three-word roster across 1200 px.
    const float ColumnGap = Pad * 2.0f;
    const float CompactX = std::min(420.0f, (Extent.Width() - Pad * 2.0f - ColumnGap) * 0.5f);
    const float LeftX = Extent.MinimumX + Pad;
    const float RightX = Extent.MaximumX - Pad - CompactX;
    const PlaneExtent ForwardRow = Spanning(LeftX, Y, CompactX, 34.0f);
    const PlaneExtent UpRow = Spanning(RightX, Y, CompactX, 34.0f);
    EnvironmentControls.SelectionField(TransferOptions[0u], ForwardRow,
        SelectionDeclaration{ "Forward", Forward, 4u }, Applied.TransferForwardAxis);
    EnvironmentControls.SelectionField(TransferOptions[1u], UpRow,
        SelectionDeclaration{ "Up Axis", Up, 2u }, Applied.TransferUpAxis);
    Y += 42.0f;

    const PlaneExtent NormalRow = Spanning(LeftX, Y, CompactX, 34.0f);
    EnvironmentControls.SelectionField(TransferOptions[2u], NormalRow,
        SelectionDeclaration{ "Normals", Normals, 3u }, Applied.TransferNormalMode);

    const auto ToggleLine = [&](ControlIdentity Target, const PlaneExtent& Row,
                                const char* Caption, bool& Reading)
    {
        const bool On = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && On && !Interaction->AnyDisclosed())
            Interaction->Grab(Target, ControlPart::Body);
        if (On && Interaction->Released(Target))
            Reading = !Reading;
        Interaction->DeclareHovered(Target, On, HoverOver);
        if (On)
            Surface->Ground(Row, Tinted.TileHovered, Scaled.FieldRadius, CornerAll);
        Surface->TextRun(Row.MinimumX + 10.0f,
                         Row.MinimumY + (Row.Height() - Scaled.RunSecondary) * 0.5f,
                         On ? Tinted.Primary : Tinted.Muted, Caption, Scaled.RunSecondary);
        const PlaneExtent Switch = Spanning(Row.MaximumX - 58.0f,
                                            Row.MinimumY + (Row.Height() - 32.0f) * 0.5f,
                                            50.0f, 32.0f);
        EnvironmentControls.SwitchTrack(Target, Switch, Reading,
                                        Tinted.EntityAccent, Tinted.Hairline, Covering(0xFFFFFFu));
    };

    ToggleLine(TransferOptions[3u], Spanning(RightX, Y, CompactX, 34.0f),
               "Triangulate", Applied.TransferTriangulate);
    Y += 44.0f;

    static const char* const TransformModes[] = { "Bake transform", "Preserve hierarchy", "Geometry only" };
    static const char* const MaterialModes[] = { "Create materials", "Reuse matching", "Link source" };
    static const char* const TexturePaths[] = { "Relative paths", "Copy textures", "Embed textures" };
    static const char* const VertexModes[] = { "Replace", "Multiply", "Ignore" };
    static const char* const ColourSpaces[] = { "Source", "sRGB", "Linear" };
    static const char* const PropertyModes[] = { "All properties", "Supported only", "None" };
    static const char* const AnimationModes[] = { "All animation", "Active action", "Current take" };
    static const char* const PrimaryAxes[] = { "+Y", "+X", "+Z" };
    static const char* const SecondaryAxes[] = { "+X", "+Z", "+Y" };

    float CardY = Y;
    const float CardGap = 8.0f;
    const float CardX = Extent.Width() - Pad * 2.0f;

    // Transfer options are one reading column. A masonry grid hid the vertical order and
    // made wheel overflow impossible to understand when cards opened at different heights.
    const auto Card = [&](std::uint32_t Index, const char* Caption,
                          float BodyHeight, const auto& RecordBody)
    {
        const float X = Extent.MinimumX + Pad;
        const float Top = CardY;
        const PlaneExtent Head = Spanning(X, Top, CardX, 34.0f);
        const bool OnHead = Head.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && OnHead && !Interaction->AnyDisclosed())
            Interaction->Grab(TransferCardFolds[Index], ControlPart::Chevron);
        if (OnHead && Interaction->Released(TransferCardFolds[Index]))
        {
            const bool Opening = !Applied.TransferCardExpanded[Index];
            for (bool& Expanded : Applied.TransferCardExpanded)
                Expanded = false;
            Applied.TransferCardExpanded[Index] = Opening;
        }
        Interaction->DeclareHovered(TransferCardFolds[Index], OnHead, HoverOver);

        const float Opening = Controls.OutlineExpansion(TransferCardFolds[Index],
                                                        Applied.TransferCardExpanded[Index], true);
        const float OpenBody = BodyHeight * Opening;
        const PlaneExtent Whole = Spanning(X, Top, CardX, Head.Height() + OpenBody);
        Surface->Ground(Whole, Tinted.Tile, Scaled.CardRadius, CornerAll);
        Surface->Edge(Whole, OnHead ? Tinted.HairlineFirm : Tinted.Hairline,
                      1.0f, Scaled.CardRadius, CornerAll);
        Surface->TextRun(Head.MinimumX + 12.0f,
                         Head.MinimumY + (Head.Height() - Scaled.RunSecondary) * 0.5f,
                         OnHead ? Tinted.Primary : Tinted.Muted, Caption, Scaled.RunSecondary, 0.0f, true);
        Surface->Stroke(Applied.TransferCardExpanded[Index]
                        ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                        Spanning(Head.MaximumX - 24.0f, Head.MinimumY + 9.0f, 14.0f, 14.0f),
                        OnHead ? Tinted.Primary : Tinted.Faint);

        if (OpenBody > 0.5f)
        {
            const PlaneExtent Clip = Spanning(X, Head.MaximumY, CardX, OpenBody);
            Surface->Confine(Clip);
            RecordBody(Spanning(X + 10.0f, Head.MaximumY + 6.0f,
                                CardX - 20.0f, BodyHeight - 10.0f));
            Surface->Release();
        }
        CardY += Head.Height() + OpenBody + CardGap;
    };

    const auto BodyRow = [](const PlaneExtent& Body, std::uint32_t Index) -> PlaneExtent
    {
        return Spanning(Body.MinimumX, Body.MinimumY + static_cast<float>(Index) * 34.0f,
                        Body.Width(), 32.0f);
    };
    const auto CardSelection = [&](ControlIdentity Target, const PlaneExtent& Row,
                                   const char* Caption, const char* const* Options,
                                   std::uint32_t OptionCount, std::uint32_t& Taken)
    {
        const float Width = std::min(420.0f, Row.Width());
        EnvironmentControls.SelectionField(Target,
            Spanning(Row.MinimumX, Row.MinimumY, Width, Row.Height()),
            SelectionDeclaration{ Caption, Options, OptionCount }, Taken);
    };

    Card(0u, Applied.TransferMode == 0u ? "Transform" : "Transform output", 112.0f,
         [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u),
                   Applied.TransferMode == 0u ? "Apply transform" : "Apply modifiers",
                   Applied.TransferApplyTransform);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Mode", TransformModes, 3u, Applied.TransferTransformMode);
        const float HalfRow = (Body.Width() - 6.0f) * 0.5f;
        ToggleLine(TransferCardFields[2], Spanning(Body.MinimumX, BodyRow(Body, 2u).MinimumY,
                                                  HalfRow, 32.0f),
                   "Apply units", Applied.TransferApplyUnits);
        ToggleLine(TransferCardFields[3], Spanning(Body.MaximumX - HalfRow, BodyRow(Body, 2u).MinimumY,
                                                  HalfRow, 32.0f),
                   "Keep pivots", Applied.TransferPreservePivots);
    });

    Card(1u, "Materials", 112.0f, [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u), "Include materials", Applied.TransferMaterials);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Material mode", MaterialModes, 3u, Applied.TransferMaterialMode);
        CardSelection(TransferCardFields[2], BodyRow(Body, 2u),
                      "Textures", TexturePaths, 3u, Applied.TransferTexturePathMode);
    });

    Card(2u, "Vertex colours", 112.0f, [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u), "Include colours", Applied.TransferVertexColours);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Mode", VertexModes, 3u, Applied.TransferVertexColourMode);
        CardSelection(TransferCardFields[2], BodyRow(Body, 2u),
                      "Colour space", ColourSpaces, 3u, Applied.TransferVertexColourSpace);
    });

    Card(3u, "Custom properties", 112.0f, [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u),
                   "Include properties", Applied.TransferCustomProperties);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Properties", PropertyModes, 3u, Applied.TransferCustomPropertyMode);
        ToggleLine(TransferCardFields[2], BodyRow(Body, 2u),
                   "Keep namespaces", Applied.TransferPreserveNamespaces);
    });

    Card(4u, "Armature", 180.0f, [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u), "Include armature", Applied.TransferArmatures);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Primary axis", PrimaryAxes, 3u, Applied.TransferPrimaryBoneAxis);
        CardSelection(TransferCardFields[2], BodyRow(Body, 2u),
                      "Secondary axis", SecondaryAxes, 3u, Applied.TransferSecondaryBoneAxis);
        ToggleLine(TransferCardFields[3], BodyRow(Body, 3u), "Include leaf bones", Applied.TransferLeafBones);
        ToggleLine(TransferCardFields[4], BodyRow(Body, 4u), "Deform bones only", Applied.TransferDeformBonesOnly);
    });

    Card(5u, Applied.TransferMode == 0u ? "Animation" : "Bake animation", 112.0f,
         [&](const PlaneExtent& Body)
    {
        ToggleLine(TransferCardFields[0], BodyRow(Body, 0u),
                   Applied.TransferMode == 0u ? "Include animation" : "Bake animation",
                   Applied.TransferAnimation);
        CardSelection(TransferCardFields[1], BodyRow(Body, 1u),
                      "Range", AnimationModes, 3u, Applied.TransferAnimationMode);
        ToggleLine(TransferCardFields[2], BodyRow(Body, 2u),
                   "Resample curves", Applied.TransferResampleAnimation);
    });

    const PlaneExtent Execute = Spanning(Extent.MinimumX + Pad, CardY + 4.0f,
                                         std::min(220.0f, Extent.Width() - Pad * 2.0f), 34.0f);
    const bool ExecuteHovered = Execute.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (Sampled.ContactPressed && ExecuteHovered && !Interaction->AnyDisclosed())
        Interaction->Grab(TransferExecute, ControlPart::Body);
    if (ExecuteHovered && Interaction->Released(TransferExecute))
        Applied.TransferDemand = Applied.TransferMode == 0u ? SceneTransferDemand::Import : SceneTransferDemand::Save;
    Interaction->DeclareHovered(TransferExecute, ExecuteHovered, HoverOver);
    Surface->Ground(Execute, ExecuteHovered ? Tinted.EntityAccent : Tinted.Tile, 10.0f, CornerAll);
    Surface->Edge(Execute, Tinted.HairlineFirm, 1.0f, 10.0f, CornerAll);
    const char* const ExecuteCaption = Applied.TransferMode == 0u ? "Import selected source" : "Save Codex document";
    const float ExecuteWidth = Surface->MeasureRun(ExecuteCaption, Scaled.RunSecondary, 0.0f);
    Surface->TextRun(Execute.MinimumX + (Execute.Width() - ExecuteWidth) * 0.5f,
                     Execute.MinimumY + (Execute.Height() - Scaled.RunSecondary) * 0.5f,
                     Tinted.Primary, ExecuteCaption, Scaled.RunSecondary, 0.0f, true);

    Surface->Release();
    const PlaneExtent Thumb = TransferOverflow.Thumb(ScrollViewport, 880.0f);
    if (Thumb.Height() > 0.0f)
        Surface->Ground(Thumb, Tinted.HairlineFirm, 1.5f, CornerAll);
    EnvironmentControls.RecordDeferred();
}

namespace
{

bool ParentEntityRows(EntityRow* Rows, std::uint32_t RowCount, SceneDirectoryContext& Applied,
                      std::uint32_t Source, std::uint32_t Destination)
{
    if (Rows == nullptr || Source >= RowCount || Destination >= RowCount || Source == Destination)
        return false;

    const std::uint32_t SourceDepth = Rows[Source].Depth;
    std::uint32_t SourcePast = Source + 1u;
    while (SourcePast < RowCount && Rows[SourcePast].Depth > SourceDepth)
        ++SourcePast;

    if (Destination >= Source && Destination < SourcePast)
        return false;

    EntityRow OldRows[SceneDirectoryContext::EntityLimit] = {};
    bool OldExpanded[SceneDirectoryContext::EntityLimit] = {};
    bool OldPresent[SceneDirectoryContext::EntityLimit] = {};
    bool OldSelected[SceneDirectoryContext::EntityLimit] = {};
    const std::uint32_t OldAnchor = Applied.EntitySelectionAnchor;
    std::uint32_t OldDetailBits[SceneDirectoryContext::EntityLimit] = {};
    double OldPosition[SceneDirectoryContext::EntityLimit][3] = {};
    double OldRotation[SceneDirectoryContext::EntityLimit][3] = {};
    double OldScale[SceneDirectoryContext::EntityLimit][3] = {};

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        OldRows[Index] = Rows[Index];
        OldExpanded[Index] = Applied.EntityExpanded[Index];
        OldPresent[Index] = Applied.EntityPresent[Index];
        OldSelected[Index] = Applied.EntitySelected[Index];
        OldDetailBits[Index] = Applied.DetailBits[Index];
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            OldPosition[Index][Axis] = Applied.EntityPosition[Index][Axis];
            OldRotation[Index][Axis] = Applied.EntityRotation[Index][Axis];
            OldScale[Index][Axis] = Applied.EntityScale[Index][Axis];
        }
    }

    std::uint32_t Order[SceneDirectoryContext::EntityLimit] = {};
    std::uint32_t Remaining = 0u;
    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        if (Index < Source || Index >= SourcePast)
            Order[Remaining++] = Index;

    std::uint32_t DestinationAt = 0u;
    while (DestinationAt < Remaining && Order[DestinationAt] != Destination)
        ++DestinationAt;
    if (DestinationAt >= Remaining)
        return false;

    const std::uint32_t DestinationDepth = OldRows[Destination].Depth;
    std::uint32_t Home = DestinationAt + 1u;
    while (Home < Remaining && OldRows[Order[Home]].Depth > DestinationDepth)
        ++Home;

    const std::uint32_t Span = SourcePast - Source;
    for (std::uint32_t Index = Remaining; Index-- > Home;)
        Order[Index + Span] = Order[Index];
    for (std::uint32_t Index = 0u; Index < Span; ++Index)
        Order[Home + Index] = Source + Index;

    const std::int32_t DepthDelta = static_cast<std::int32_t>(DestinationDepth + 1u)
                                  - static_cast<std::int32_t>(SourceDepth);
    const std::int32_t Deepest = static_cast<std::int32_t>(OldRows[SourcePast - 1u].Depth) + DepthDelta;
    if (Deepest < 0 || Deepest >= static_cast<std::int32_t>(SceneDirectoryContext::EntityLimit))
        return false;

    std::uint32_t NewTaken = Applied.EntityTaken;
    std::uint32_t NewAnchor = Applied.EntitySelectionAnchor;

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        const std::uint32_t Old = Order[Index];
        Rows[Index] = OldRows[Old];
        if (Old >= Source && Old < SourcePast)
            Rows[Index].Depth = static_cast<std::uint32_t>(static_cast<std::int32_t>(Rows[Index].Depth) + DepthDelta);

        Applied.EntityExpanded[Index] = OldExpanded[Old];
        Applied.EntityPresent[Index] = OldPresent[Old];
        Applied.EntitySelected[Index] = OldSelected[Old];
        Applied.DetailBits[Index] = OldDetailBits[Old];
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Applied.EntityPosition[Index][Axis] = OldPosition[Old][Axis];
            Applied.EntityRotation[Index][Axis] = OldRotation[Old][Axis];
            Applied.EntityScale[Index][Axis] = OldScale[Old][Axis];
        }
        if (Old == Applied.EntityTaken)
            NewTaken = Index;
        if (Old == OldAnchor)
            NewAnchor = Index;
    }

    std::uint32_t Ancestors[SceneDirectoryContext::EntityLimit] = {};
    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        Rows[Index].Enclosing = Rows[Index].Depth == 0u ? 0xFFFFFFFFu : Ancestors[Rows[Index].Depth - 1u];
        Ancestors[Rows[Index].Depth] = Index;
        Rows[Index].EnclosedCount = 0u;
    }
    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        if (Rows[Index].Enclosing < RowCount)
            ++Rows[Rows[Index].Enclosing].EnclosedCount;

    Applied.EntityTaken = NewTaken;
    Applied.EntitySelectionAnchor = NewAnchor;
    return true;
}

} // namespace

void SceneDirectoryPanel::RecordOutliner(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                                         EntityRow* Rows, std::uint32_t RowCount)
{
    if (Rows == nullptr)
        RowCount = 0u;

    if (RowCount > SceneDirectoryContext::EntityLimit)
        RowCount = SceneDirectoryContext::EntityLimit;

    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);

    const float Pad = Scaled.PanePad;

    // 📐 One three-page carousel: Directory + Details leads, the Properties / Bookmarks inspector trails.
    //    Both pages are always positioned from the same carried coordinate, so departure and arrival
    //    remain visible throughout travel in either direction.
    OutlinePages.Navigate(Applied.OutlinePage);

    const PlaneExtent DirectoryExtent = OutlinePages.Page(Extent, 0u);
    const PlaneExtent InspectorExtent = OutlinePages.Page(Extent, 1u);
    const PlaneExtent TransferExtent  = OutlinePages.Page(Extent, 2u);
    const PointerCondition LivePointer = Sampled;
    struct PointerRestore
    {
        PointerCondition& Slot;
        PointerCondition  Saved;
        ~PointerRestore() { Slot = Saved; }
    } RestorePointer{ Sampled, LivePointer };
    const auto SeatPagePointer = [&](std::uint32_t Page)
    {
        Sampled = LivePointer;
        if (OutlinePages.CurrentPage() != Page)
        {
            Sampled.PositionX = -1000000.0f;
            Sampled.PositionY = -1000000.0f;
            Sampled.ContactHeld = Sampled.ContactPressed = Sampled.ContactReleased = false;
            Sampled.ContactDoublePressed = false;
            Sampled.WheelY = 0.0f;
        }
    };

    if (!Surface->Excluded(TransferExtent))
    {
        SeatPagePointer(2u);
        Surface->Confine(Extent);
        RecordTransfer(TransferExtent, Applied);
        Surface->Release();
    }

    if (!Surface->Excluded(InspectorExtent))
    {
        SeatPagePointer(1u);
        Surface->Confine(Extent);
        RecordProperties(InspectorExtent, Applied, Rows, RowCount,
                         Applied.OutlineInspectorTab, true);
        Surface->Release();
    }

    if (Surface->Excluded(DirectoryExtent))
    {
        Sampled = LivePointer;
        return;
    }

    SeatPagePointer(0u);
    Surface->Confine(Extent);

    // 📐 The directory and its immediate details use the validation parametric split, constrained to 60%
    //    on narrow leaves so both panes remain readable.
    const float OutlinerX = (Scaled.OutlinerX < DirectoryExtent.Width() * 0.6f)
                          ? Scaled.OutlinerX : DirectoryExtent.Width() * 0.6f;

    const PlaneExtent Outlining = Spanning(DirectoryExtent.MinimumX, DirectoryExtent.MinimumY,
                                           OutlinerX, DirectoryExtent.Height());

    const PlaneExtent Header = Spanning(Outlining.MinimumX, Outlining.MinimumY,
                                        Outlining.Width(), Scaled.HeaderHeight);

    RecordLeafHeader(Header, SymbolSubject::GearCog, Tinted.EntityAccent, "Document Directory", "");

    // 📐 One footer belongs to the whole Directory destination, not only to its outliner column. The
    //    details pane now terminates above the same band, so the page has a complete baseline before
    //    it slides to Properties / Bookmarks.
    const PlaneExtent Footer = Spanning(DirectoryExtent.MinimumX,
                                        DirectoryExtent.MaximumY - Scaled.FooterHeight,
                                        DirectoryExtent.Width(), Scaled.FooterHeight);

    // 🔴 THE DIRECTORY | PROPERTIES | HISTORY STRIP IS WITHDRAWN, as asked. It was a
    //    third route to a page that Tab already cycles and that the header's Inspect
    //    call already jumps to, and it spent a whole band restating navigation the
    //    leaf has twice over. The inspector's own Properties / Bookmarks strip stays —
    //    that one chooses between two pages nothing else reaches.
    const PlaneExtent Strip = Spanning(Outlining.MinimumX, Footer.MinimumY,
                                       Outlining.Width(), 0.0f);

    // 📝 The search field, between the header and the rows — the scene directory's own filter box,
    //    drawn as a PILL: the radius is half the field's height, so both ends are fully rounded. The
    //    host feeds the typed run through the seam's `AcceptTyped` while `SearchTaken` stands
    //    (reported by `Advance`), and Backspace/Escape clear it.
    const PlaneExtent Search = Spanning(Outlining.MinimumX + Pad, Header.MaximumY + Pad,
                                        Outlining.Width() - Pad * 2.0f, Scaled.SearchHeight);

    {
        const bool Hovered = Search.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Hovered && Sampled.ContactPressed && !Interaction->AnyDisclosed())
            Interaction->Grab(SearchField, ControlPart::Body);

        const bool Taken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);

        // 🔴 A pill: `Search.Height() * 0.5f` corners, never the card radius — the reported render
        //    showed the search box with the field's small radius, reading as a squashed input.
        const float PillRadius = Search.Height() * 0.5f;

        Surface->Ground(Search, Tinted.MenuLower, PillRadius, CornerAll);
        Surface->Edge(Search, Taken ? Faded(Tinted.Primary, 0.22f) : Tinted.Hairline,
                      1.0f, PillRadius, CornerAll);

        const float GlyphExtent = 14.0f;
        const float GlyphLead   = Search.MinimumX + 10.0f;
        const float GlyphTop    = Search.MinimumY + (Search.Height() - GlyphExtent) * 0.5f;

        Surface->Stroke(SymbolSubject::MagnifierLens,
                        Spanning(GlyphLead, GlyphTop, GlyphExtent, GlyphExtent), Tinted.Faint);

        const float RunLead = GlyphLead + GlyphExtent + 8.0f;
        const float FieldRun = Scaled.RunSecondary * (12.0f / 11.5f);
        const float RunTop   = Search.MinimumY + (Search.Height() - FieldRun) * 0.5f;

        const bool Empty = Applied.EntityRetention[0] == '\0';

        Surface->TextRunTruncated(RunLead, RunTop, Search.MaximumX - RunLead - 8.0f,
                                  Empty ? Tinted.Faint : Tinted.Primary,
                                  Empty ? "Filter records\u2026" : Applied.EntityRetention, FieldRun);
    }

    // 📝 The filter card — the validation UI's generic facet filter, ported to the editor: wrapped
    //    category chips, individual removal, clear-all, and the shared dropdown. It sits after the
    //    search box, and both together decide which rows the directory presents.
    const FacetDeclaration EditorFacets =
    {
        "Filters",
        EditorFacetOptions,
        EditorFacetColours,
        EditorFacetCount,
        0xFFFFFFFFu   // [-] - no locked facet
    };

    const float FacetY = Facets.MeasureHeight(Outlining.Width() - Pad * 2.0f, EditorFacets,
                                              Applied.FacetEnabled);

    const PlaneExtent FacetCard = Spanning(Outlining.MinimumX + Pad, Search.MaximumY + Pad,
                                           Outlining.Width() - Pad * 2.0f, FacetY);

    Discard(Facets.Record(FacetCard, EditorFacets, Applied.FacetEnabled));

    const PlaneExtent Body = Spanning(Outlining.MinimumX + Pad, FacetCard.MaximumY + Pad,
                                      Outlining.Width() - Pad * 2.0f,
                                      Strip.MinimumY - FacetCard.MaximumY - Pad);

    if (Body.MaximumY <= Body.MinimumY)
    {
        Surface->Release();
        return;
    }

    Surface->Confine(Body);

    float Sweep = Body.MinimumY;

    // 📝 The search and the facets decide every row's presence: a row is presented when it matches
    //    (name or tags, within an enabled category) or when any row it holds matches, and while the
    //    filter stands every branch is forced open — the shell's own rule.
    const bool Filtering = RetentionActive(Applied);
    const std::uint32_t PriorDragDestination = Applied.DragDestination;
    const bool Carrying = Applied.DragSource < RowCount;
    const bool Dragging = Carrying && Sampled.ContactHeld &&
                          std::abs(Sampled.PositionY - Applied.DragOriginY) >= 5.0f;
    Applied.DragDestination = SceneDirectoryContext::EntityLimit;

    // 📝 Selection ranges follow the presented tree, not storage ordinals: folded descendants and
    // filtered-out records do not become selected merely because they stand between the anchor and target.
    bool Presented[SceneDirectoryContext::EntityLimit] = {};
    bool Retained[SceneDirectoryContext::EntityLimit] = {};
    float PresentedFraction[SceneDirectoryContext::EntityLimit] = {};
    float Expansion[SceneDirectoryContext::EntityLimit] = {};
    std::uint32_t Parents[SceneDirectoryContext::EntityLimit] = {};
    std::uint32_t Depths[SceneDirectoryContext::EntityLimit] = {};
    for (std::uint32_t Candidate = 0u; Candidate < RowCount; ++Candidate)
    {
        Parents[Candidate] = Rows[Candidate].Enclosing;
        Depths[Candidate] = Rows[Candidate].Depth;
        Retained[Candidate] = RowRetained(Applied, Rows[Candidate]);
        Expansion[Candidate] = Rows[Candidate].EnclosedCount > 0u
                             ? Controls.OutlineExpansion(RowDisclosures[Candidate],
                                                         Applied.EntityExpanded[Candidate], true)
                             : 1.0f;
    }
    VisibleTree::Resolve(Parents, Expansion, Retained, RowCount, Filtering, true,
                         Presented, PresentedFraction);

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        if (!Presented[Index])
            continue;

        const EntityRow&  EntryRow = Rows[Index];
        const float       Folded   = PresentedFraction[Index];
        const PlaneExtent Row      = Spanning(Body.MinimumX, Sweep, Body.Width(), Scaled.RowHeight);
        const PlaneExtent RowClip  = Spanning(Body.MinimumX, Sweep, Body.Width(),
                                              Scaled.RowHeight * Folded);

        if (Sweep > Body.MaximumY)
            break;
        Sweep += RowClip.Height();

        Surface->Confine(RowClip);

        const bool Taken   = Applied.EntitySelected[Index];
        const bool Hovered = RowClip.Encloses(Sampled.PositionX, Sampled.PositionY);
        const bool Absent  = !Applied.EntityPresent[Index];

        if (Dragging && Hovered &&
            SceneTreePolicy::AllowsParent(Applied.DragSource, Index, Depths, RowCount))
            Applied.DragDestination = Index;
        const bool Branch  = EntryRow.EnclosedCount > 0u;

        const float LeadX = Row.MinimumX + Scaled.RowLeadX
                          + static_cast<float>(EntryRow.Depth) * Scaled.RowStepX;

        // ① The disclosure cell, which takes the contact before the row does.
        const PlaneExtent Chevron = Spanning(LeadX,
                                             Row.MinimumY + (Row.Height() - Scaled.ChevronExtent) * 0.5f,
                                             Scaled.ChevronExtent, Scaled.ChevronExtent);

        const bool OnChevron = Branch && Chevron.Encloses(Sampled.PositionX, Sampled.PositionY);

        // ② The presence action at the trailing edge, outranking the row.
        const float PresenceExtent = Scaled.GlyphExtent * (20.0f / 18.0f);
        const PlaneExtent Presence = Spanning(Row.MaximumX - PresenceExtent - Scaled.PanePad * 0.5f,
                                              Row.MinimumY + (Row.Height() - PresenceExtent) * 0.5f,
                                              PresenceExtent, PresenceExtent);

        const bool OnPresence = Presence.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Sampled.ContactPressed && !Interaction->AnyDisclosed())
        {
            if (OnChevron)
                Interaction->Grab(RowDisclosures[Index], ControlPart::Chevron);
            else if (OnPresence)
                Interaction->Grab(RowPresences[Index], ControlPart::Body);
            else if (Hovered)
            {
                Interaction->Grab(RowContacts[Index], ControlPart::Body);
                Applied.DragSource = Index;
                Applied.DragDestination = SceneDirectoryContext::EntityLimit;
                Applied.DragOriginY = Sampled.PositionY;
            }
        }

        if (OnChevron && Interaction->Released(RowDisclosures[Index]))
            Applied.EntityExpanded[Index] = !Applied.EntityExpanded[Index];

        if (OnPresence && Interaction->Released(RowPresences[Index]))
        {
            const bool Incoming = !Applied.EntityPresent[Index];

            Applied.EntityPresent[Index] = Incoming;

            for (std::uint32_t Inward = Index + 1u; Inward < RowCount; ++Inward)
            {
                if (Rows[Inward].Depth <= EntryRow.Depth)
                    break;

                Applied.EntityPresent[Inward] = Incoming;
            }
        }

        if (Hovered && !OnChevron && !OnPresence && Interaction->Released(RowContacts[Index]))
        {
            SelectionSet::Apply(Applied.EntitySelected, RowCount, Applied.EntitySelectionAnchor,
                                Index, Presented,
                                SelectionGesture{ Modified.Shifted, Modified.Commanded });

            // Details always follows a member of the persistent set, including after a toggle removes
            // the clicked endpoint. An empty set is not representable while a details pane stands.
            Applied.EntityTaken = SelectionSet::Primary(Applied.EntitySelected, RowCount, Index);
        }

        Interaction->DeclareHovered(RowContacts[Index], Hovered, HoverOver);

        // ③ The row ground, then its rail. A withheld row draws at half coverage.
        const float Coverage = Absent ? 0.5f : 1.0f;

        if (Taken)
            Surface->Ground(Row, Tinted.EntityTaken, Scaled.FieldRadius, CornerAll);
        else if (Hovered)
            Surface->Ground(Row, Tinted.RowHovered, Scaled.FieldRadius, CornerAll);

        if (Taken)
        {
            const PlaneExtent Rail = Spanning(Row.MinimumX - Scaled.RailOffsetX,
                                              Row.MinimumY + (Row.Height() - Scaled.RailY) * 0.5f,
                                              Scaled.RailX, Scaled.RailY);

            Surface->Ground(Rail, Tinted.EntityAccent, 2.0f,
                            CornerTrailingUpper | CornerTrailingLower);
        }

        if (Applied.DragDestination == Index)
            Surface->Edge(Row, Tinted.EntityAccent, 2.0f, Scaled.FieldRadius, CornerAll);

        if (Branch)
            Surface->Stroke((Applied.EntityExpanded[Index] || Filtering)
                            ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                            Chevron, Faded(Tinted.Faint, Coverage));

        const float GlyphLead = LeadX + Scaled.ChevronExtent + Scaled.PanePad;
        const PlaneExtent Glyph = Spanning(GlyphLead,
                                           Row.MinimumY + (Row.Height() - Scaled.GlyphExtent) * 0.5f,
                                           Scaled.GlyphExtent, Scaled.GlyphExtent);

        Surface->Stroke(EntityGlyph(EntryRow.Subject), Glyph,
                        Faded(EntityHue(EntryRow.Subject), Coverage));

        const float NamingRun  = Scaled.RunPrimary;
        const float NamingLead = Glyph.MaximumX + Scaled.PanePad;
        const float NamingTop  = Row.MinimumY + (Row.Height() - NamingRun) * 0.5f;

        float NamingLimit = Presence.MinimumX - Scaled.PanePad;

        if (Branch)
        {
            char Counted[12] = {};
            std::snprintf(Counted, sizeof(Counted), "%u",
                          static_cast<unsigned>(EntryRow.EnclosedCount));

            const float CountRun  = Scaled.RunFine;
            const float CountLead = NamingLimit - Surface->MeasureRun(Counted, CountRun, 0.0f);

            Surface->TextRun(CountLead, Row.MinimumY + (Row.Height() - CountRun) * 0.5f,
                             Faded(Tinted.Faint, Coverage), Counted, CountRun);

            NamingLimit = CountLead - Scaled.PanePad;
        }

        Surface->TextRunTruncated(NamingLead, NamingTop, NamingLimit,
                                  Faded(Taken ? Tinted.Primary : (Hovered ? Tinted.Primary : Tinted.Muted),
                                        Coverage),
                                  EntryRow.Naming, NamingRun);

        if (Hovered || Absent)
        {
            if (OnPresence)
                Surface->Ground(Presence, Tinted.TileHovered, 3.0f, CornerAll);

            const float EyeExtent = PresenceExtent * (14.0f / 20.0f);
            const PlaneExtent Eye = Spanning(Presence.MinimumX + (PresenceExtent - EyeExtent) * 0.5f,
                                             Presence.MinimumY + (PresenceExtent - EyeExtent) * 0.5f,
                                             EyeExtent, EyeExtent);

            Surface->Stroke(Absent ? SymbolSubject::EyeClosed : SymbolSubject::EyeOpen, Eye,
                            OnPresence ? Tinted.Primary : Tinted.Faint);
        }

        Surface->Release();
    }

    if (Carrying && !Sampled.ContactHeld)
    {
        if (PriorDragDestination < RowCount)
            ParentEntityRows(Rows, RowCount, Applied, Applied.DragSource, PriorDragDestination);

        Applied.DragSource = SceneDirectoryContext::EntityLimit;
        Applied.DragDestination = SceneDirectoryContext::EntityLimit;
    }

    // 📝 The empty state: the filter stands but nothing matched.
    if (Filtering && Sweep <= Body.MinimumY + 0.5f)
    {
        const float Run = Scaled.RunSecondary;
        const char* Prose = "No records match the search or filters.";

        Surface->TextRun(Body.MinimumX + (Body.Width()
                                          - Surface->MeasureRun(Prose, Run, 0.0f)) * 0.5f,
                         Body.MinimumY + Scaled.PanePad * 2.0f, Tinted.Faint, Prose, Run);
    }

    Surface->Release();

    // ④ The footer, `{count} entities`.
    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    char Counted[16] = {};
    std::snprintf(Counted, sizeof(Counted), "%u", static_cast<unsigned>(RowCount));

    const float FooterRun = Scaled.RunFine;
    const float FooterTop = Footer.MinimumY + (Footer.Height() - FooterRun) * 0.5f;
    const float FooterLead = Footer.MinimumX + Scaled.HeaderPadX;

    Surface->TextRun(FooterLead, FooterTop, Tinted.Primary, Counted, FooterRun, 0.0f, true);
    Surface->TextRun(FooterLead + Surface->MeasureRun(Counted, FooterRun, 0.0f) + 4.0f, FooterTop,
                     Tinted.Muted, " records", FooterRun);

    // 🧩 Scene interchange always opens the dedicated transfer page; the footer never starts a hidden import.
    const auto TransferCall = [&](std::uint32_t Index, const char* Caption, float Width)
    {
        const PlaneExtent Call = Spanning(Footer.MaximumX - Scaled.HeaderPadX - Width - Index * (Width + 6.0f),
                                          Footer.MinimumY + 3.0f, Width, Footer.Height() - 6.0f);
        const bool Hovered = Call.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Hovered)
        {
            Interaction->Withdraw();
            Applied.TransferMode = Index == 0u ? 1u : 0u;
            Applied.OutlinePage = 2u;
        }
        Interaction->DeclareHovered(TransferCalls[Index], Hovered, HoverOver);
        Surface->Ground(Call, Hovered ? Tinted.TileHovered : Tinted.Tile, 9.0f, CornerAll);
        Surface->Edge(Call, Tinted.HairlineFirm, 1.0f, 9.0f, CornerAll);
        const float CaptionWidth = Surface->MeasureRun(Caption, FooterRun, 0.0f);
        Surface->TextRun(Call.MinimumX + (Call.Width() - CaptionWidth) * 0.5f, FooterTop,
                         Tinted.Primary, Caption, FooterRun);
    };
    TransferCall(0u, "Save", 52.0f);
    TransferCall(1u, "Import", 58.0f);

    // ⑤ The details pane — the small metadata and options card for the taken row.
    const PlaneExtent Detailing = Spanning(Outlining.MaximumX, DirectoryExtent.MinimumY,
                                           DirectoryExtent.MaximumX - Outlining.MaximumX,
                                           Footer.MinimumY - DirectoryExtent.MinimumY);

    if (RowCount == 0u || Applied.EntityTaken >= RowCount)
    {
        RecordLeafHeader(Spanning(Detailing.MinimumX, Detailing.MinimumY,
                                  Detailing.Width(), Scaled.HeaderHeight),
                         SymbolSubject::CrosshairCentre, Tinted.Faint,
                         "Details", "Nothing selected");

        const float Run = Scaled.RunSecondary;
        const char* Prose = "Select a record in the directory to view its details.";

        Surface->TextRun(Detailing.MinimumX + (Detailing.Width()
                                                  - Surface->MeasureRun(Prose, Run, 0.0f)) * 0.5f,
                         Detailing.MinimumY + Scaled.HeaderHeight + Scaled.PanePad * 3.0f,
                         Tinted.Faint, Prose, Run);
        Surface->Release();
        return;
    }

    const std::uint32_t Index = Applied.EntityTaken;
    const EntityRow&    Current = Rows[Index];
    const ThemeToken    Hue     = EntityHue(Current.Subject);

    const PlaneExtent DetailsHeader = Spanning(Detailing.MinimumX, Detailing.MinimumY,
                                               Detailing.Width(), Scaled.HeaderHeight);

    // The complete selected-component header is the Inspect action: icon, name and type travel together.
    const bool OnDetailsHeader = DetailsHeader.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (Sampled.ContactPressed && OnDetailsHeader && !Interaction->AnyDisclosed())
        Interaction->Grab(InspectCall, ControlPart::Body);
    if (OnDetailsHeader && Interaction->Released(InspectCall))
        Applied.OutlinePage = 1u;
    Interaction->DeclareHovered(InspectCall, OnDetailsHeader, HoverOver);

    RecordLeafHeader(DetailsHeader, EntityGlyph(Current.Subject), Hue,
                     Current.Naming, EntityText(Current.Subject));
    if (OnDetailsHeader)
        Surface->Ground(DetailsHeader, Tinted.TileHovered, 0.0f, CornerNone);
    Surface->Stroke(SymbolSubject::ChevronRight,
                    Spanning(DetailsHeader.MaximumX - Scaled.HeaderPadX - 12.0f,
                             DetailsHeader.MinimumY + (DetailsHeader.Height() - 12.0f) * 0.5f,
                             12.0f, 12.0f),
                    OnDetailsHeader ? Tinted.Primary : Tinted.Faint);

    const float DetailPad = Scaled.PanePad * 1.5f;
    const PlaneExtent DetailBody = Spanning(Detailing.MinimumX + DetailPad,
                                            DetailsHeader.MaximumY + DetailPad,
                                            Detailing.Width() - DetailPad * 2.0f,
                                            Detailing.MaximumY - DetailsHeader.MaximumY - DetailPad);

    Surface->Confine(DetailBody);

    float DetailSweep = DetailBody.MinimumY;

    // 📐 The hero tile — the crest, the token and the classification in the subject's own hue.
    {
        const float HeroHeight = Scaled.HeroCrest + Scaled.HeroPad * 2.0f;
        const PlaneExtent Hero = Spanning(DetailBody.MinimumX, DetailSweep,
                                          DetailBody.Width(), HeroHeight);

        Surface->Ground(Hero, Tinted.Tile, Scaled.CardRadius, CornerAll);
        Surface->Edge(Hero, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

        const PlaneExtent Crest = Spanning(Hero.MinimumX + Scaled.HeroPad,
                                           Hero.MinimumY + Scaled.HeroPad,
                                           Scaled.HeroCrest, Scaled.HeroCrest);

        Surface->Ground(Crest, Hue, 8.0f, CornerAll);

        const float Figure = Scaled.HeroCrest * 0.55f;

        Surface->Stroke(EntityGlyph(Current.Subject),
                        Spanning(Crest.MinimumX + (Scaled.HeroCrest - Figure) * 0.5f,
                                 Crest.MinimumY + (Scaled.HeroCrest - Figure) * 0.5f,
                                 Figure, Figure), Covering(0xFFFFFFu));

        char Token[12] = {};
        std::snprintf(Token, sizeof(Token), "g_%02u", static_cast<unsigned>(Index + 1u));

        const float NameRun = Scaled.RunPrimary;
        const float PairRun = Scaled.RunFine;

        Surface->TextRunTruncated(Crest.MaximumX + Scaled.HeroPad,
                                  Hero.MinimumY + Scaled.HeroPad * 0.9f,
                                  Hero.MaximumX - Scaled.HeroPad,
                                  Tinted.Primary, Current.Naming, NameRun, true);
        Surface->TextRun(Crest.MaximumX + Scaled.HeroPad,
                         Hero.MinimumY + Scaled.HeroPad * 0.9f + NameRun * RunLeading,
                         Hue, Token, PairRun);

        DetailSweep += HeroHeight + Scaled.PanePad;
    }

    RecordDetailOptions(Spanning(DetailBody.MinimumX, DetailSweep,
                                 DetailBody.Width(), DetailBody.MaximumY - DetailSweep),
                        Applied, Index, Current);

    Surface->Release();
    Surface->Release();

    // 🔴 The filter card's dropdown is a deferred popup, exactly like the editor panel's menus: it
    //    must record AFTER the rows and the details pane so it composites above them, never under.
    Facets.RecordDeferred();
}

void SceneDirectoryPanel::RecordDetailOptions(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                                              std::uint32_t Index, const EntityRow& Current)
{
    // 📐 The camera row's options are the camera's own settings: the lag and the pitch direction,
    //    beside the visibility every row carries. Every other row keeps the reference's generic
    //    options. The bits are the same slots — bit 1 is lag on the camera, Locked elsewhere.
    const bool Camera = Current.Camera == CameraRole::Editor;

    const char* const CameraCaptions[3]    = { "Visible", "Position Lag", "Invert Pitch" };
    const char* const GenericCaptions[3]   = { "Visible", "Locked", "Cast Shadows" };
    const char* const* const Captions = Camera ? CameraCaptions : GenericCaptions;
    const float RowY = Scaled.RowHeight;

    float Sweep = Extent.MinimumY;

    Surface->Ground(Spanning(Extent.MinimumX, Sweep, Extent.Width(), RowY),
                    Tinted.Tile, Scaled.FieldRadius, CornerAll);
    Surface->TextRun(Extent.MinimumX + Scaled.PanePad * 2.0f,
                     Sweep + (RowY - Scaled.RunPrimary) * 0.5f,
                     Tinted.Muted, "Options", Scaled.RunPrimary);

    Sweep += RowY + Scaled.PanePad * 0.5f;

    for (std::uint32_t Option = 0u; Option < 3u; ++Option)
    {
        const PlaneExtent Row = Spanning(Extent.MinimumX, Sweep, Extent.Width(), RowY);

        if (Sweep + RowY > Extent.MaximumY)
            break;

        const bool OnRow = Row.Encloses(Sampled.PositionX, Sampled.PositionY);

        const bool State = (Option == 0u) ? Applied.EntityPresent[Index]
                          : ((Applied.DetailBits[Index] & (1u << Option)) != 0u);

        if (Sampled.ContactPressed && OnRow && !Interaction->AnyDisclosed())
            Interaction->Grab(DetailOptions[Index][Option], ControlPart::Body);

        if (OnRow && Interaction->Released(DetailOptions[Index][Option]))
        {
            if (Option == 0u)
                Applied.EntityPresent[Index] = !Applied.EntityPresent[Index];
            else
                Applied.DetailBits[Index] ^= (1u << Option);
        }

        Interaction->DeclareHovered(DetailOptions[Index][Option], OnRow, HoverOver);

        if (OnRow)
            Surface->Ground(Row, Tinted.TileHovered, Scaled.FieldRadius, CornerAll);

        // 📐 Match the shared component switch's reference dimensions rather than a reduced mini-pill.
        const float ToggleY = 32.0f;
        const float ToggleX = 50.0f;
        const PlaneExtent Switch = Spanning(Row.MaximumX - ToggleX - Scaled.PanePad * 1.5f,
                                            Row.MinimumY + (Row.Height() - ToggleY) * 0.5f,
                                            ToggleX, ToggleY);

        // 🔴 This drew its own pill: the nub was placed by a ternary, so it
        //    jumped between the two ends instead of travelling, and its radius
        //    was Toggle*0.5-2 rather than the shared proportion. The same switch
        //    animated in the validation host and snapped here.
        Controls.SwitchTrack(DetailOptions[Index][Option], Switch, State,
                             Tinted.EntityAccent, Tinted.Hairline, Covering(0xFFFFFFu));

        Surface->TextRun(Row.MinimumX + Scaled.PanePad * 2.0f,
                         Row.MinimumY + (Row.Height() - Scaled.RunPrimary) * 0.5f,
                         OnRow ? Tinted.Primary : Tinted.Muted, Captions[Option], Scaled.RunPrimary);

        Sweep += RowY + Scaled.PanePad * 0.5f;
    }

    // 📐 The camera's small metadata: the pose the rig reports and the speed the artist set, stated
    //    as plain stat rows beneath the options.
    if (Camera)
    {
        Sweep += Scaled.PanePad * 0.5f;

        const float StatY = Scaled.StatY;
        const PlaneExtent Stats = Spanning(Extent.MinimumX, Sweep, Extent.Width(),
                                           StatY * 3.0f + Scaled.PanePad);

        if (Stats.MaximumY <= Extent.MaximumY)
        {
            Surface->Ground(Stats, Tinted.Tile, Scaled.FieldRadius, CornerAll);

            const auto StateRow = [&](const char* Caption, const char* Value)
            {
                const PlaneExtent Row = Spanning(Stats.MinimumX, Sweep, Stats.Width(), StatY);

                Surface->TextRun(Row.MinimumX + Scaled.PanePad * 2.0f,
                                 Row.MinimumY + (Row.Height() - Scaled.RunFine) * 0.5f,
                                 Tinted.Muted, Caption, Scaled.RunFine);

                Surface->TextRun(Row.MaximumX - Scaled.PanePad * 2.0f
                                 - Surface->MeasureRun(Value, Scaled.RunFine, 0.0f),
                                 Row.MinimumY + (Row.Height() - Scaled.RunFine) * 0.5f,
                                 Tinted.Primary, Value, Scaled.RunFine);

                Sweep += StatY;
            };

            char Positioned[64] = {};
            std::snprintf(Positioned, sizeof(Positioned), "%.0f, %.1f, %.0f m",
                          Applied.CameraPosition[0], Applied.CameraPosition[1], Applied.CameraPosition[2]);
            StateRow("Position", Positioned);

            char Turned[64] = {};
            std::snprintf(Turned, sizeof(Turned), "yaw %.0f\u00B0  pitch %.0f\u00B0",
                          Applied.CameraRotation[0], Applied.CameraRotation[1]);
            StateRow("Rotation", Turned);

            char Stepped[64] = {};
            std::snprintf(Stepped, sizeof(Stepped), "%.0f m/s", Applied.CameraSpeed);
            StateRow("Speed", Stepped);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PROPERTIES | HISTORY
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::RecordProperties(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                                           const EntityRow* Rows, std::uint32_t RowCount,
                                           std::uint32_t& InspectorTab, bool OutlinePresentation)
{
    if (Rows == nullptr)
        RowCount = 0u;

    if (RowCount > SceneDirectoryContext::EntityLimit)
        RowCount = SceneDirectoryContext::EntityLimit;

    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);

    const bool Selected = Applied.EntityTaken < RowCount;

    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);

    if (!Selected)
    {
        RecordLeafHeader(Header, SymbolSubject::CubeSolid, Tinted.Faint,
                         "Nothing selected", "No entity");
    }
    else
    {
        const EntityRow&  Current = Rows[Applied.EntityTaken];
        const ThemeToken Hue     = EntityHue(Current.Subject);

        RecordLeafHeader(Header, EntityGlyph(Current.Subject), Hue,
                         Current.Naming, EntityText(Current.Subject));
    }

    if (OutlinePresentation)
    {
        const char* Caption = "Directory";
        const float Run = Scaled.RunSecondary;
        const float PadX = 10.0f;
        const float CallSpan = PadX * 2.0f + Surface->MeasureRun(Caption, Run, 0.0f) + 12.0f;
        const PlaneExtent Call = Spanning(Header.MaximumX - Scaled.HeaderPadX - CallSpan,
                                          Header.MinimumY + (Header.Height() - 24.0f) * 0.5f,
                                          CallSpan, 24.0f);
        const bool OnCall = Call.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Sampled.ContactPressed && OnCall && !Interaction->AnyDisclosed())
            Interaction->Grab(DirectoryCall, ControlPart::Body);

        if (OnCall && Interaction->Released(DirectoryCall))
            Applied.OutlinePage = 0u;

        Interaction->DeclareHovered(DirectoryCall, OnCall, HoverOver);
        const float Lit = Interaction->HoveredFraction(DirectoryCall);

        Surface->Ground(Call, Blend(Tinted.Tile, Tinted.TileHovered, Lit),
                        Call.Height() * 0.5f, CornerAll);
        Surface->Edge(Call, Blend(Tinted.Hairline, Tinted.HairlineFirm, Lit), 1.0f,
                      Call.Height() * 0.5f, CornerAll);
        Surface->TextRun(Call.MinimumX + PadX * 0.7f,
                         Call.MinimumY + (Call.Height() - Run) * 0.5f,
                         OnCall ? Tinted.Primary : Tinted.Faint, "<", Run, 0.0f, true);
        Surface->TextRun(Call.MinimumX + PadX + 12.0f,
                         Call.MinimumY + (Call.Height() - Run) * 0.5f,
                         OnCall ? Tinted.Primary : Tinted.Muted, Caption, Run);
    }

    // The revision feed was removed from Scene Directory. Ordinary entities now have one direct Properties page;
    // only the Editor Camera has a second page because bookmarks are operational camera data.
    const bool CameraSelected = Selected && Rows[Applied.EntityTaken].Camera == CameraRole::Editor;
    if (!CameraSelected)
        InspectorTab = 0u;

    PlaneExtent Pages = Spanning(Extent.MinimumX, Header.MaximumY, Extent.Width(),
                                 Extent.MaximumY - Header.MaximumY - Scaled.FooterHeight);

    if (CameraSelected)
    {
        const char* const Captions[2] = { "Properties", "Bookmarks" };
        const PlaneExtent Strip = Spanning(Extent.MinimumX, Header.MaximumY,
                                           Extent.Width(), Scaled.ComponentY);
        const TabDeclaration Declared{ Captions, 2u };
        static_cast<void>(Controls.TabStrip(InspectorStrip, Strip, Declared, InspectorTab));
        Pages = Spanning(Extent.MinimumX, Strip.MaximumY, Extent.Width(),
                         Extent.MaximumY - Strip.MaximumY - Scaled.FooterHeight);
    }

    const std::uint32_t MotionIndex = OutlinePresentation ? 0u : 1u;
    if (InspectorTab != InspectorArriving[MotionIndex])
    {
        InspectorDeparted[MotionIndex] = InspectorArriving[MotionIndex];
        InspectorArriving[MotionIndex] = InspectorTab;
        Motion->Eased(InspectorMotion[MotionIndex]).Depart(0.0, 1.0, 240.0, 0.0,
                                                           EaseCurve::Carousel);
    }

    const float Travelled = static_cast<float>(Motion->Eased(InspectorMotion[MotionIndex]).Current());
    const float DepartedAt = InspectorDeparted[MotionIndex] == 1u ? -Pages.Width() : 0.0f;
    const float ArrivingAt = InspectorArriving[MotionIndex] == 1u ? -Pages.Width() : 0.0f;
    const float Carried = CameraSelected ? DepartedAt + (ArrivingAt - DepartedAt) * Travelled : 0.0f;

    Surface->Confine(Pages);
    const PlaneExtent Leading = Spanning(Pages.MinimumX + Carried, Pages.MinimumY,
                                         Pages.Width(), Pages.Height());
    if (!Surface->Excluded(Leading))
        RecordPropertyCards(Leading, Applied, Rows, RowCount);

    if (CameraSelected)
    {
        const PlaneExtent Trailing = Spanning(Leading.MaximumX, Pages.MinimumY,
                                              Pages.Width(), Pages.Height());
        if (!Surface->Excluded(Trailing))
            RecordCameraBookmarks(Trailing, Applied);
    }
    Surface->Release();


    // ② The footer, `{n} fields` — the strip's selection stated in the entity's own hue.
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    if (Selected)
    {
        const ThemeToken Hue       = EntityHue(Rows[Applied.EntityTaken].Subject);
        const float       FooterRun = Scaled.RunFine;
        const float       FooterTop = Footer.MinimumY + (Footer.Height() - FooterRun) * 0.5f;
        const float       ChipY     = Footer.MinimumY
                                    + (Footer.Height() - Scaled.ChipExtent) * 0.5f;

        Surface->Ground(Spanning(Footer.MinimumX + Scaled.HeaderPadX, ChipY,
                                 Scaled.ChipExtent, Scaled.ChipExtent), Hue, 2.0f, CornerAll);

        Surface->TextRun(Footer.MinimumX + Scaled.HeaderPadX + Scaled.ChipExtent
                         + Scaled.PanePad, FooterTop, Tinted.Muted,
                         (InspectorTab == 0u) ? "Properties" : "Bookmarks", FooterRun);
    }

    EnvironmentControls.RecordDeferred();
}

// 🧩 The properties column's wheel scroll, eased. Answers where it stands.
float SceneDirectoryPanel::AdvanceOutlineScroll(SceneDirectoryContext& Applied,
                                                const PlaneExtent& Viewport)
{
    // 📐 The content's height is not known until the cards have been laid out, so the
    //    ceiling is taken from the PREVIOUS tick's sweep. A column whose height
    //    changes settles in one tick, and a wheel notch never travels past content
    //    that is no longer there.
    const float Travel = (PropertyContent > Viewport.Height())
                       ? (PropertyContent - Viewport.Height()) : 0.0f;

    if (Travel <= 0.0f)
    {
        PropertyWanted = 0.0f;
        PropertyShown  = 0.0f;
        return 0.0f;
    }

    if (Viewport.Encloses(Sampled.PositionX, Sampled.PositionY) && Sampled.WheelY != 0.0f &&
        !Interaction->AnyDisclosed())
    {
        PropertyWanted -= Sampled.WheelY * 56.0f;
    }

    if (PropertyWanted < 0.0f)     PropertyWanted = 0.0f;
    if (PropertyWanted > Travel)   PropertyWanted = Travel;

    const float Remaining = PropertyWanted - PropertyShown;

    if (Remaining > 0.35f || Remaining < -0.35f)
        PropertyShown += Remaining * 0.26f;
    else
        PropertyShown = PropertyWanted;

    static_cast<void>(Applied);
    return PropertyShown;
}

void SceneDirectoryPanel::RecordPropertyCards(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                                              const EntityRow* Rows, std::uint32_t RowCount)
{
    if (Extent.Width() <= 0.0f || Extent.Height() <= 0.0f)
        return;

    const float Pad = Scaled.PanePad;
    // 🔴 THE PROPERTIES PAGE COULD NOT BE SCROLLED. With the sun, sky, grid, sun-disc
    //    and atmosphere cards all unfolded the column stands far past the leaf, and
    //    there was no wheel path at all — the tail was unreachable. The page scrolls
    //    on the same eased chase the layer stack's lists use.
    const float Wheeled = AdvanceOutlineScroll(Applied, Extent);

    float       Sweep = Extent.MinimumY + Pad - Wheeled;
    std::uint32_t CardIndex = 0u;

    // 📝 The property card — a folding card, from the reference's generic component cards.
    // 🔴 Its rows used to be inert labels: the row said "Position" and drew no reading at all. A row
    //    may now carry three ordinates and a unit, recorded through the shared VectorRow so a
    //    transform reads as [X|Y|Z|unit] in the same pill grammar the scalar rows use.
    const auto RecordCard = [&](const char* Caption, const char* const* Fields, std::uint32_t FieldCount,
                                double (*Vectors)[3] = nullptr, const char* const* Units = nullptr)
    {
        if (CardIndex >= SceneDirectoryContext::CardLimit)
            return;

        const std::uint32_t Target = CardIndex++;
        const bool  Folded   = Applied.CardFolded[Target];
        const float Current  = Controls.OutlineExpansion(CardFolds[Target], !Folded, true);

        const float BodyHeight = (static_cast<float>(FieldCount) * Scaled.RowHeight + Pad * 2.0f) * Current;
        const PlaneExtent Card = Spanning(Extent.MinimumX + Pad, Sweep,
                                          Extent.Width() - Pad * 2.0f,
                                          Scaled.ComponentY + BodyHeight);

        // 🔴 The card ground was the literal 0x0A0A0B, which is exactly the value
        //    behind Tinted.Desk. Spelling it as a hex pinned this one surface to
        //    the dark palette while every neighbour — the header below, the
        //    hairline around it — followed the theme, so on a light theme the card
        //    stayed a black slab. Same value, taken from the record that owns it.
        Surface->Ground(Card, Tinted.Desk, Scaled.CardRadius, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

        const PlaneExtent CardHeader = Spanning(Card.MinimumX, Card.MinimumY,
                                                Card.Width(), Scaled.ComponentY);

        Surface->Ground(CardHeader, Tinted.MenuLower, Scaled.CardRadius,
                        CornerLeadingUpper | CornerTrailingUpper);

        const bool OnHeader = CardHeader.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Sampled.ContactPressed && OnHeader && !Interaction->AnyDisclosed())
            Interaction->Grab(CardFolds[Target], ControlPart::Chevron);

        if (OnHeader && Interaction->Released(CardFolds[Target]))
            Applied.CardFolded[Target] = !Applied.CardFolded[Target];

        if (Current > 0.0f)
            Surface->Ground(Spanning(CardHeader.MinimumX, CardHeader.MaximumY - 1.0f,
                                     CardHeader.Width(), 1.0f),
                            Faded(Tinted.Hairline, Current), 0.0f, CornerNone);

        const float Mark = Scaled.ActionGlyph;

        Surface->Stroke(Folded ? SymbolSubject::ChevronRight : SymbolSubject::ChevronDown,
                        Spanning(CardHeader.MinimumX + Scaled.HeaderPadX * 0.6f,
                                 CardHeader.MinimumY + (CardHeader.Height() - Mark) * 0.5f,
                                 Mark, Mark), Tinted.Faint);

        const float CaptionRun = Scaled.RunSmall;

        Surface->TextRunCapitalised(CardHeader.MinimumX + Scaled.HeaderPadX * 0.6f + Mark + Pad,
                                    CardHeader.MinimumY + (CardHeader.Height() - CaptionRun) * 0.5f,
                                    OnHeader ? Tinted.Primary : Tinted.Muted, Caption, CaptionRun,
                                    0.025f, true);

        char Tallied[8] = {};
        std::snprintf(Tallied, sizeof(Tallied), "%u", static_cast<unsigned>(FieldCount));

        const float TallyRun = Scaled.RunFiner;

        Surface->TextRun(CardHeader.MaximumX - Scaled.HeaderPadX
                         - Surface->MeasureRun(Tallied, TallyRun, 0.0f),
                         CardHeader.MinimumY + (CardHeader.Height() - TallyRun) * 0.5f,
                         Tinted.Faint, Tallied, TallyRun);

        if (Current > 0.0f)
        {
            const PlaneExtent Opened = Spanning(Card.MinimumX, CardHeader.MaximumY,
                                                Card.Width(), BodyHeight);

            Surface->Confine(Opened);

            float RowCursor = CardHeader.MaximumY + Pad;

            for (std::uint32_t FieldIndex = 0u; FieldIndex < FieldCount; ++FieldIndex)
            {
                const PlaneExtent Row = Spanning(Card.MinimumX + Pad * 1.5f, RowCursor,
                                                 Card.Width() - Pad * 3.0f, Scaled.RowHeight);

                if (Vectors != nullptr)
                {
                    VectorDeclaration Axes;
                    Axes.Caption   = Fields[FieldIndex];
                    Axes.UnitGlyph = (Units != nullptr && Units[FieldIndex] != nullptr)
                                   ? Units[FieldIndex] : "";

                    static_cast<void>(EnvironmentControls.VectorRow(
                        CardFields[Target][FieldIndex], Row, Axes, Vectors[FieldIndex]));
                }
                else
                {
                    Surface->TextRun(Row.MinimumX + 2.0f,
                                     Row.MinimumY + (Row.Height() - Scaled.RunPrimary) * 0.5f,
                                     Tinted.Muted, Fields[FieldIndex], Scaled.RunPrimary);
                }

                RowCursor += Scaled.RowHeight;
            }

            Surface->Release();
        }

        Sweep = Card.MaximumY + Pad * 0.85f;
    };

    if (RowCount == 0u || Applied.EntityTaken >= RowCount)
        return;

    const std::uint32_t Taken = Applied.EntityTaken;
    const EntityRow&    Current = Rows[Taken];

    const bool Transforms = Current.Subject != EntitySubject::Level
                         && Current.Subject != EntitySubject::Grouping
                         && Current.Subject != EntitySubject::Script;

    if (Transforms)
    {
        // 📐 One unit per row: metres, degrees, and a bare multiplier for scale.
        const char* const TransformRows[3]  = { "Position", "Rotation", "Scale" };
        const char* const TransformUnits[3] = { "m", "\xC2\xB0", "x" };

        // 📐 Three rows, three ordinate triples, contiguous so one pointer serves
        //    the loop. Scale is seeded to 1 once; a zero-scale default would read
        //    as a collapsed object on a fresh scene.
        if (!Applied.TransformSeeded)
        {
            for (std::uint32_t Each = 0u; Each < SceneDirectoryContext::EntityLimit; ++Each)
                for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
                    Applied.EntityScale[Each][Axis] = 1.0;

            Applied.TransformSeeded = true;
        }

        double Ordinates[3][3] = {};
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Ordinates[0][Axis] = Applied.EntityPosition[Taken][Axis];
            Ordinates[1][Axis] = Applied.EntityRotation[Taken][Axis];
            Ordinates[2][Axis] = Applied.EntityScale[Taken][Axis];
        }

        RecordCard("Transform", TransformRows, 3u, Ordinates, TransformUnits);

        // the rows edit in place, so carry any drag back to the record
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Applied.EntityPosition[Taken][Axis] = Ordinates[0][Axis];
            Applied.EntityRotation[Taken][Axis] = Ordinates[1][Axis];
            Applied.EntityScale[Taken][Axis]    = Ordinates[2][Axis];
        }
    }

    char ComponentCaption[48] = {};
    std::snprintf(ComponentCaption, sizeof(ComponentCaption), "%s Component",
                  EntityText(Current.Subject));

    const auto RecordEnvironmentQuality = [&]()
    {
        static const char* const Options[4] = { "Preview", "Balanced", "High", "Ultra" };
        SelectionDeclaration Declaration;
        Declaration.Caption = "Quality";
        Declaration.Options = Options;
        Declaration.OptionCount = 4u;
        Declaration.Indicator = SelectionIndicator::Marked;
        Applied.Environment.AtmosphereQuality = std::min(Applied.Environment.AtmosphereQuality, 3u);
        const PlaneExtent Card = Spanning(Extent.MinimumX + Scaled.PanePad, Sweep,
                                          Extent.Width() - Scaled.PanePad * 2.0f,
                                          Scaled.RowHeight + Scaled.PanePad * 2.0f);
        Surface->Ground(Card, Tinted.Desk, Scaled.CardRadius, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);
        EnvironmentControls.SelectionField(
            EnvironmentQuality,
            Spanning(Card.MinimumX + Scaled.PanePad, Card.MinimumY + Scaled.PanePad,
                     Card.Width() - Scaled.PanePad * 2.0f, Scaled.RowHeight),
            Declaration, Applied.Environment.AtmosphereQuality);
        Sweep = Card.MaximumY + Scaled.PanePad * 0.85f;
    };

    // 📐 The per-subject field sets, and the environment cards where the subject is the sun or the sky
    //    while the environment is presented.
    switch (Current.Subject)
    {
        case EntitySubject::Illuminant:
        {
            const char* const Fields[3] = { "Intensity", "Cast Shadows", "Light Color" };
            RecordCard(ComponentCaption, Fields, 3u);
            break;
        }
        case EntitySubject::Camera:
        {
            if (Current.Camera != CameraRole::Editor)
            {
                const char* const Fields[3] = { "Projection", "Field of View", "Clipping" };
                RecordCard(ComponentCaption, Fields, 3u);
                break;
            }

            // 📝 This scene currently exposes the Editor Camera as its camera entity. Its operational
            //    properties are real magnitude controls rather than a card of inert labels: wheel-adjusted
            //    fly speed, perspective field of view, and both clipping distances.
            const char* const CameraCaptions[4] =
                { "Fly Speed", "Field of View", "Near Clip", "Far Clip" };
            const char* const CameraUnits[4] = { "m/s", "deg", "m", "m" };
            const double CameraMinimums[4] = { 1.0, 20.0, 0.01, 10.0 };
            const double CameraMaximums[4] = { 5000.0, 150.0, 10.0, 100000.0 };
            const std::uint32_t CameraDecimals[4] = { 0u, 0u, 2u, 0u };
            double CameraValues[4] =
            {
                Applied.CameraSpeed,
                Applied.CameraFieldOfView,
                Applied.CameraNearClip,
                Applied.CameraFarClip
            };

            RecordEnvironmentCard(Applied, Extent, Sweep, CardIndex,
                                  "Editor Camera", CameraCaptions, CameraUnits, CameraMinimums,
                                  CameraMaximums, CameraValues, 4u, CameraDecimals);

            Applied.CameraSpeed       = CameraValues[0];
            Applied.CameraFieldOfView = CameraValues[1];
            Applied.CameraNearClip    = CameraValues[2];
            Applied.CameraFarClip     = CameraValues[3];

            if (Applied.CameraFarClip <= Applied.CameraNearClip)
                Applied.CameraFarClip = Applied.CameraNearClip + 0.01;
            break;
        }
        case EntitySubject::Audio:
        {
            const char* const Fields[3] = { "Volume", "Looping", "Spatial 3D" };
            RecordCard(ComponentCaption, Fields, 3u);
            break;
        }
        case EntitySubject::Particle:
        {
            const char* const Fields[3] = { "Emit Rate", "Life Time", "Looping" };
            RecordCard(ComponentCaption, Fields, 3u);
            break;
        }
        case EntitySubject::Trigger:
        {
            const char* const Fields[2] = { "Event Tag", "Radius" };
            RecordCard(ComponentCaption, Fields, 2u);
            break;
        }
        case EntitySubject::Script:
        {
            const char* const Fields[2] = { "State", "Difficulty" };
            RecordCard(ComponentCaption, Fields, 2u);
            break;
        }
        case EntitySubject::Actor:
        {
            const char* const Fields[3] = { "Static Geometry", "Simulate Physics", "Generate Overlaps" };
            RecordCard(ComponentCaption, Fields, 3u);
            break;
        }
        case EntitySubject::Level:
        {
            const char* const Fields[2] = { "Level Name", "World Partition" };
            RecordCard(ComponentCaption, Fields, 2u);
            break;
        }
        case EntitySubject::Sun:
        {
            // 📝 The sun card — the four ordinates the sky renderer reads, each a live slider. The card is
            //    drawn only while the environment is presented; a host that never presents it renders the
            //    reference's generic illuminant card instead.
            if (Applied.EnvironmentPresented)
            {
                const char* const SunCaptions[6] =
                    { "Elevation", "Azimuth", "Intensity", "Temperature", "Day Cycle", "Shadow Strength" };
                const char* const SunUnits[6] = { "\u00B0", "\u00B0", "lx", "K", "\u00B0/s", "" };
                const double SunMinimums[6] = { -90.0, 0.0, 0.0, 1000.0, -30.0, 0.0 };
                const double SunMaximums[6] = { 90.0, 360.0, 10.0, 12000.0, 30.0, 1.0 };
                double SunValues[6] = { Applied.Environment.SunElevation,
                                        Applied.Environment.SunAzimuth,
                                        Applied.Environment.SunIntensity,
                                        Applied.Environment.SunTemperature,
                                        Applied.Environment.DayCycleDegreesPerSecond,
                                        Applied.Environment.SunShadowStrength };

                RecordEnvironmentCard(Applied, Extent, Sweep, CardIndex,
                                      "Sun", SunCaptions, SunUnits, SunMinimums, SunMaximums,
                                      SunValues, 6u);

                Applied.Environment.SunElevation = SunValues[0];
                Applied.Environment.SunAzimuth = SunValues[1];
                Applied.Environment.SunIntensity = SunValues[2];
                Applied.Environment.SunTemperature = SunValues[3];
                Applied.Environment.DayCycleDegreesPerSecond = SunValues[4];
                Applied.Environment.SunShadowStrength = SunValues[5];

                // 📐 The sun disc is the icon drawn over the atmosphere: its radius multiplier and
                //    direct-term intensity. Same component as the other environment cards.
                const char* const DiscCaptions[2] = { "Disc Radius", "Disc Intensity" };
                const char* const DiscUnits[2]    = { "x", "" };
                const double DiscMinimums[2]      = { 1.0, 0.0 };
                const double DiscMaximums[2]      = { 32.0, 4.0 };
                double DiscValues[2]              = { Applied.Environment.SunDiscRadius,
                                                     Applied.Environment.SunDiscIntensity };

                RecordEnvironmentCard(Applied, Extent, Sweep, CardIndex,
                                      "Sun Disc", DiscCaptions, DiscUnits, DiscMinimums, DiscMaximums,
                                      DiscValues, 2u);

                Applied.Environment.SunDiscRadius    = DiscValues[0];
                Applied.Environment.SunDiscIntensity = DiscValues[1];
                RecordEnvironmentQuality();
            }
            else
            {
                const char* const Fields[3] = { "Intensity", "Cast Shadows", "Light Color" };
                RecordCard(ComponentCaption, Fields, 3u);
            }
            break;
        }
        case EntitySubject::Sky:
        {
            if (Applied.EnvironmentPresented)
            {
                const char* const SkyCaptions[4] =
                    { "Sky Intensity", "Turbidity", "Exposure", "Ground Albedo" };
                const char* const SkyUnits[4] = { "", "", "EV", "" };
                const double SkyMinimums[4] = { 0.0, 1.0, -8.0, 0.0 };
                const double SkyMaximums[4] = { 3.0, 10.0, 8.0, 1.0 };
                double SkyValues[4] = { Applied.Environment.SkyIntensity,
                                        Applied.Environment.SkyTurbidity,
                                        Applied.Environment.ExposureCompensation,
                                        Applied.Environment.GroundAlbedo };

                RecordEnvironmentCard(Applied, Extent, Sweep, CardIndex,
                                      "Sky", SkyCaptions, SkyUnits, SkyMinimums, SkyMaximums,
                                      SkyValues, 4u);

                Applied.Environment.SkyIntensity = SkyValues[0];
                Applied.Environment.SkyTurbidity = SkyValues[1];
                Applied.Environment.ExposureCompensation = SkyValues[2];
                Applied.Environment.GroundAlbedo = SkyValues[3];

                const char* const AtmoCaptions[6] =
                    { "Rayleigh Density", "Rayleigh Height", "Mie Density", "Mie Height",
                      "Mie Asymmetry", "Ozone Density" };
                const char* const AtmoUnits[6] = { "", "x", "x", "km", "", "x" };
                const double AtmoMinimums[6] = { 0.0, 0.2, 0.0, 0.1, -0.95, 0.0 };
                const double AtmoMaximums[6] = { 3.0, 3.0, 4.0, 8.0, 0.95, 3.0 };
                double AtmoValues[6] = { Applied.Environment.AtmosphereDensity,
                                         Applied.Environment.AtmosphereScaleHeight,
                                         Applied.Environment.MieDensity,
                                         Applied.Environment.MieScaleHeightKilometres,
                                         Applied.Environment.MieAsymmetry,
                                         Applied.Environment.OzoneDensity };

                RecordEnvironmentCard(Applied, Extent, Sweep, CardIndex,
                                      "Atmosphere", AtmoCaptions, AtmoUnits, AtmoMinimums,
                                      AtmoMaximums, AtmoValues, 6u);

                Applied.Environment.AtmosphereDensity = AtmoValues[0];
                Applied.Environment.AtmosphereScaleHeight = AtmoValues[1];
                Applied.Environment.MieDensity = AtmoValues[2];
                Applied.Environment.MieScaleHeightKilometres = AtmoValues[3];
                Applied.Environment.MieAsymmetry = AtmoValues[4];
                Applied.Environment.OzoneDensity = AtmoValues[5];
                RecordEnvironmentQuality();
            }
            else
            {
                const char* const Fields[3] = { "Intensity", "Cast Shadows", "Light Color" };
                RecordCard(ComponentCaption, Fields, 3u);
            }
            break;
        }
        default:
        {
            const char* const Fields[2] = { "Folder Name", "Is Editor Only" };
            RecordCard(ComponentCaption, Fields, 2u);
            break;
        }
    }

    // 🔴 Every card ordinal is spent whether the subject presented it or not. Skipped, a card that
    //    appears for one subject and not the next inherits the fold of whichever card held that ordinal
    //    before it, and the artist watches an unrelated card close.
    while (CardIndex < SceneDirectoryContext::CardLimit)
        static_cast<void>(Controls.OutlineExpansion(CardFolds[CardIndex++], true, true));

    // 📐 What the column actually came to, for next tick's scroll ceiling. Measured
    //    from the sweep rather than predicted, so a card folding or a subject
    //    changing the card set is accounted for without a second layout pass.
    PropertyContent = (Sweep + Wheeled) - Extent.MinimumY + Pad;

    // 📐 The thumb, only while there is somewhere to travel.
    if (PropertyContent > Extent.Height() && Extent.Height() > 0.0f)
    {
        const float Fraction = Extent.Height() / PropertyContent;
        const float ThumbY   = Extent.Height() * Fraction;
        const float Reach    = PropertyContent - Extent.Height();
        const float Along    = (Reach > 0.0f) ? (Wheeled / Reach) : 0.0f;

        Surface->Ground(Spanning(Extent.MaximumX - 5.0f,
                                 Extent.MinimumY + (Extent.Height() - ThumbY) * Along,
                                 3.0f, ThumbY),
                        Tinted.Muted, 1.5f, CornerAll);
    }
}

void SceneDirectoryPanel::RecordEnvironmentCard(SceneDirectoryContext& Applied,
                                                const PlaneExtent& Extent, float& Sweep,
                                                std::uint32_t& CardIndex,
                                                const char* Caption,
                                                const char* const* SliderCaptions,
                                                const char* const* UnitGlyphs,
                                                const double* Minimums, const double* Maximums,
                                                double* Values, std::uint32_t SliderCount,
                                                const std::uint32_t* DecimalPlaces)
{
    if (CardIndex >= SceneDirectoryContext::CardLimit)
        return;

    const float Pad = Scaled.PanePad;

    const std::uint32_t Target = CardIndex++;
    const bool  Folded   = Applied.CardFolded[Target];
    const float Current  = Controls.OutlineExpansion(CardFolds[Target], !Folded, true);

    const float BodyHeight = (static_cast<float>(SliderCount) * Scaled.RowHeight + Pad * 2.0f) * Current;
    const PlaneExtent Card = Spanning(Extent.MinimumX + Pad, Sweep,
                                      Extent.Width() - Pad * 2.0f,
                                      Scaled.ComponentY + BodyHeight);

    // 🔴 The second card ground; same literal, same defect as above.
    Surface->Ground(Card, Tinted.Desk, Scaled.CardRadius, CornerAll);
    Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

    const PlaneExtent CardHeader = Spanning(Card.MinimumX, Card.MinimumY,
                                            Card.Width(), Scaled.ComponentY);

    Surface->Ground(CardHeader, Tinted.MenuLower, Scaled.CardRadius,
                    CornerLeadingUpper | CornerTrailingUpper);

    const bool OnHeader = CardHeader.Encloses(Sampled.PositionX, Sampled.PositionY);

    if (Sampled.ContactPressed && OnHeader && !Interaction->AnyDisclosed())
        Interaction->Grab(CardFolds[Target], ControlPart::Chevron);

    if (OnHeader && Interaction->Released(CardFolds[Target]))
        Applied.CardFolded[Target] = !Applied.CardFolded[Target];

    if (Current > 0.0f)
        Surface->Ground(Spanning(CardHeader.MinimumX, CardHeader.MaximumY - 1.0f,
                                 CardHeader.Width(), 1.0f),
                        Faded(Tinted.Hairline, Current), 0.0f, CornerNone);

    const float Mark = Scaled.ActionGlyph;

    Surface->Stroke(Folded ? SymbolSubject::ChevronRight : SymbolSubject::ChevronDown,
                    Spanning(CardHeader.MinimumX + Scaled.HeaderPadX * 0.6f,
                             CardHeader.MinimumY + (CardHeader.Height() - Mark) * 0.5f,
                             Mark, Mark), Tinted.Faint);

    const float CaptionRun = Scaled.RunSmall;

    Surface->TextRunCapitalised(CardHeader.MinimumX + Scaled.HeaderPadX * 0.6f + Mark + Pad,
                                CardHeader.MinimumY + (CardHeader.Height() - CaptionRun) * 0.5f,
                                OnHeader ? Tinted.Primary : Tinted.Muted, Caption, CaptionRun,
                                0.025f, true);

    char Tallied[8] = {};
    std::snprintf(Tallied, sizeof(Tallied), "%u", static_cast<unsigned>(SliderCount));

    const float TallyRun = Scaled.RunFiner;

    Surface->TextRun(CardHeader.MaximumX - Scaled.HeaderPadX
                     - Surface->MeasureRun(Tallied, TallyRun, 0.0f),
                     CardHeader.MinimumY + (CardHeader.Height() - TallyRun) * 0.5f,
                     Tinted.Faint, Tallied, TallyRun);

    if (Current > 0.0f)
    {
        const PlaneExtent Opened = Spanning(Card.MinimumX, CardHeader.MaximumY,
                                            Card.Width(), BodyHeight);

        Surface->Confine(Opened);

        float RowCursor = CardHeader.MaximumY + Pad;

        for (std::uint32_t SliderIndex = 0u; SliderIndex < SliderCount; ++SliderIndex)
        {
            const PlaneExtent Row = Spanning(Card.MinimumX + Pad * 1.5f, RowCursor,
                                             Card.Width() - Pad * 3.0f, Scaled.RowHeight);

            MagnitudeDeclaration Declared;
            Declared.Caption     = SliderCaptions[SliderIndex];
            Declared.UnitGlyph   = UnitGlyphs[SliderIndex];
            Declared.Minimum     = Minimums[SliderIndex];
            Declared.Maximum     = Maximums[SliderIndex];
            Declared.Decimals    = DecimalPlaces != nullptr ? DecimalPlaces[SliderIndex] : 1u;
            // 🔴 The same defect as the shell's copy of this card: the rows were
            //    laid out with no label at all. Label · track · readout.
            Declared.Layout      = MagnitudeDeclaration::Arrange::Measured;

            double& Coordinate   = Values[SliderIndex];

            static_cast<void>(EnvironmentControls.MagnitudeRow(EnvironmentSliders[Target][SliderIndex],
                                                               Row, Declared, Coordinate, false));

            // 🔴 The drag arm: latched the first tick the slider holds the contact, with the value at
            //    that moment — the "start" the history entry describes. Released with a changed value,
            //    one demand is raised; neither fires on the intermediate ticks.
            if (Interaction->Holding(EnvironmentSliders[Target][SliderIndex]) && !EnvironmentArmed[Target][SliderIndex])
            {
                EnvironmentArmed[Target][SliderIndex] = true;
                EnvironmentFrom[Target][SliderIndex]  = Coordinate;
            }

            if (Interaction->Released(EnvironmentSliders[Target][SliderIndex]))
            {
                if (EnvironmentArmed[Target][SliderIndex])
                {
                    // No revision feed is produced. The arm remains only to close the gesture
                    // cleanly; undo/redo will belong to a future command system rather than this panel.
                    EnvironmentArmed[Target][SliderIndex] = false;
                }
            }

            RowCursor += Scaled.RowHeight;
        }

        Surface->Release();
    }

    Sweep = Card.MaximumY + Pad * 0.85f;
}

void SceneDirectoryPanel::RecordCameraBookmarks(const PlaneExtent& Extent,
                                                    SceneDirectoryContext& Applied)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);

    const float Pad = Scaled.PanePad * 1.5f;
    const float ButtonY = 26.0f;
    const float Top = Extent.MinimumY + Pad;
    const float ButtonGap = 8.0f;
    const float ButtonWidth = 108.0f;

    const PlaneExtent Save = Spanning(Extent.MaximumX - Pad - ButtonWidth, Top,
                                      ButtonWidth, ButtonY);
    const PlaneExtent Recall = Spanning(Save.MinimumX - ButtonGap - ButtonWidth, Top,
                                        ButtonWidth, ButtonY);
    const PlaneExtent Retire = Spanning(Recall.MinimumX - ButtonGap - ButtonWidth, Top,
                                        ButtonWidth, ButtonY);

    const auto Pill = [&](ControlIdentity Target, const PlaneExtent& Bounds,
                          const char* Caption, bool Enabled) -> bool
    {
        const bool Over = Enabled && Bounds.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Over && !Interaction->AnyDisclosed())
            Interaction->Grab(Target, ControlPart::Body);

        Surface->Ground(Bounds, Over ? Tinted.TileHovered : Tinted.Tile,
                        Bounds.Height() * 0.5f, CornerAll);
        Surface->Edge(Bounds, Enabled ? Tinted.HairlineFirm : Tinted.Hairline, 1.0f,
                      Bounds.Height() * 0.5f, CornerAll);
        const float Run = Scaled.RunFine;
        Surface->TextRun(Bounds.MinimumX + (Bounds.Width() - Surface->MeasureRun(Caption, Run, 0.0f)) * 0.5f,
                         Bounds.MinimumY + (Bounds.Height() - Run) * 0.5f,
                         Enabled ? Tinted.Primary : Tinted.Faint, Caption, Run);
        return Over && Interaction->Released(Target);
    };

    if (Pill(BookmarkSave, Save, "Save Current", true) &&
        Applied.CameraBookmarkCount < SceneDirectoryContext::CameraBookmarkLimit)
    {
        const std::uint32_t Bookmark = Applied.CameraBookmarkCount++;
        Applied.CameraBookmarkTaken = Bookmark;
        std::snprintf(Applied.CameraBookmarkNames[Bookmark], 32u, "Bookmark %u",
                      static_cast<unsigned>(Bookmark + 1u));
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Applied.CameraBookmarkPosition[Bookmark][Axis] = Applied.CameraPosition[Axis];
            Applied.CameraBookmarkRotation[Bookmark][Axis] = Applied.CameraRotation[Axis];
        }
    }

    if (Pill(BookmarkRecall, Recall, "Go To", Applied.CameraBookmarkCount > 0u))
        Applied.CameraBookmarkRecallRequested = true;

    if (Pill(BookmarkRetire, Retire, "Delete", Applied.CameraBookmarkCount > 0u))
    {
        const std::uint32_t Retiring = Applied.CameraBookmarkTaken;
        for (std::uint32_t Bookmark = Retiring; Bookmark + 1u < Applied.CameraBookmarkCount; ++Bookmark)
        {
            std::memcpy(Applied.CameraBookmarkNames[Bookmark],
                        Applied.CameraBookmarkNames[Bookmark + 1u], 32u);
            for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            {
                Applied.CameraBookmarkPosition[Bookmark][Axis] =
                    Applied.CameraBookmarkPosition[Bookmark + 1u][Axis];
                Applied.CameraBookmarkRotation[Bookmark][Axis] =
                    Applied.CameraBookmarkRotation[Bookmark + 1u][Axis];
            }
        }
        --Applied.CameraBookmarkCount;
        if (Applied.CameraBookmarkCount == 0u)
            Applied.CameraBookmarkTaken = 0u;
        else if (Applied.CameraBookmarkTaken >= Applied.CameraBookmarkCount)
            Applied.CameraBookmarkTaken = Applied.CameraBookmarkCount - 1u;
    }

    Surface->TextRun(Extent.MinimumX + Pad, Top + (ButtonY - Scaled.RunSecondary) * 0.5f,
                     Tinted.Primary, "Camera Bookmarks", Scaled.RunSecondary, 0.0f, true);

    float Sweep = Top + ButtonY + Pad;
    if (Applied.CameraBookmarkCount == 0u)
    {
        const char* Empty = "Move the Editor Camera, then save the current viewpoint.";
        Surface->TextRun(Extent.MinimumX + Pad, Sweep + Pad, Tinted.Faint,
                         Empty, Scaled.RunFine);
        return;
    }

    for (std::uint32_t Bookmark = 0u; Bookmark < Applied.CameraBookmarkCount; ++Bookmark)
    {
        const PlaneExtent Card = Spanning(Extent.MinimumX + Pad, Sweep,
                                          Extent.Width() - Pad * 2.0f, 58.0f);
        const bool Selected = Applied.CameraBookmarkTaken == Bookmark;
        const bool Over = Card.Encloses(Sampled.PositionX, Sampled.PositionY);

        Surface->Ground(Card, Selected ? Tinted.EntityTaken : Tinted.Tile,
                        Scaled.CardRadius, CornerAll);
        Surface->Edge(Card, Selected ? Tinted.HairlineFirm : Tinted.Hairline, 1.0f,
                      Scaled.CardRadius, CornerAll);

        if (Sampled.ContactPressed && Over)
            Applied.CameraBookmarkTaken = Bookmark;

        const PlaneExtent Name = Spanning(Card.MinimumX + 8.0f, Card.MinimumY + 5.0f,
                                          Card.Width() - 16.0f, 25.0f);
        EnvironmentControls.EditableText(
            BookmarkNames[Bookmark], Name,
            EditableTextDeclaration{ "Bookmark name", false, false },
            Applied.CameraBookmarkNames[Bookmark], 32u);

        char Pose[96] = {};
        std::snprintf(Pose, sizeof Pose, "%.1f, %.1f, %.1f m   ·   yaw %.1f°   pitch %.1f°",
                      Applied.CameraBookmarkPosition[Bookmark][0],
                      Applied.CameraBookmarkPosition[Bookmark][1],
                      Applied.CameraBookmarkPosition[Bookmark][2],
                      Applied.CameraBookmarkRotation[Bookmark][0],
                      Applied.CameraBookmarkRotation[Bookmark][1]);
        Surface->TextRun(Card.MinimumX + 10.0f, Card.MinimumY + 36.0f,
                         Tinted.Faint, Pose, Scaled.RunFiner);

        Sweep += Card.Height() + 8.0f;
        if (Sweep >= Extent.MaximumY)
            break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE GIZMO
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::RecordGizmo(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                                      OverlayGeometry& Overlay)
{
    // 📐 The ground grid and all 3 world axes (Red X, Green Y, Blue Z) are rendered 100%
    //    on the GPU by the overlay pass fragment shader (WorkspaceOverlayFragment.slang). The host records
    //    the shared screen-space orientation gizmo so every workspace shares one gizmo.
    static_cast<void>(Applied);
    static_cast<void>(Overlay);
    static_cast<void>(Extent);
}

}   // namespace Slate
