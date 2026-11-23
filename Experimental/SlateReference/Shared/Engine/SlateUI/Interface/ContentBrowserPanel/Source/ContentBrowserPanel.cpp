//============================================================================================================================================
//                                                       CONTENTBROWSERPANEL.CPP
//============================================================================================================================================
// 🧩 The content browser's recording and its arbitration, transcribed from `AsstbrowsrBasic.html` element by element.

#include "SlateUI/Interface/ContentBrowserPanel/Api/ContentBrowserPanel.h"

#include <cstdio>
#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEATED CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr double HoverOver   = 0.120;   // [s] - the reference's `transition-colors` duration
constexpr float  NotchHeight =  48.0f;  // [px] - one wheel notch
constexpr double OctetsPerMegaOctet = 1048576.0;   // [B] - the reference's own divisor
constexpr double OctetsPerKiloOctet = 1024.0;      // [B] - inspector fallback for small engine content

/// 🔴 The lattice's column count is not a media query here. The reference steps 2→6 columns across five
///    Tailwind breakpoints against the VIEWPORT; a panel inside a page has its own extent, so the count is
///    resolved from the extent actually handed to the lattice rather than from the display.
std::uint32_t ColumnsWithin(float Width, float CardGap, float CardPad)
{
    constexpr float CardIdeal = 168.0f;   // [px] - what a lattice card wants to occupy

    const float Usable = Width - CardPad * 2.0f;

    if (Usable <= CardIdeal)
        return 1u;

    const auto Resolved = static_cast<std::uint32_t>((Usable + CardGap) / (CardIdeal + CardGap));

    return (Resolved < 1u) ? 1u : ((Resolved > 6u) ? 6u : Resolved);
}

/// 🔴 A lowercase fold that reaches no locale. The reference's `toLowerCase` is applied to ASCII record
///    namings only, and a locale-aware fold would disagree with it on the very octets it is asked about.
constexpr char Folded(char Sampled)
{
    return (Sampled >= 'A' && Sampled <= 'Z') ? static_cast<char>(Sampled - 'A' + 'a') : Sampled;
}

/// 🧩 Whether Sought appears anywhere within Subject, folded, as `String.includes` decides it.
bool Within(const char* Subject, const char* Sought)
{
    if (Subject == nullptr || Sought == nullptr || Sought[0] == '\0')
        return true;

    for (std::uint32_t Origin = 0u; Subject[Origin] != '\0'; ++Origin)
    {
        std::uint32_t Index = 0u;

        while (Sought[Index] != '\0' &&
               Folded(Subject[Origin + Index]) == Folded(Sought[Index]))
        {
            ++Index;
        }

        if (Sought[Index] == '\0')
            return true;
    }

    return false;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ARCHIVE ROSTER
//------------------------------------------------------------------------------------------------------------------------

const char* ArchiveNaming(ContentArchive Archive)
{
    switch (Archive)
    {
        case ContentArchive::Topology:    return "Meshes";
        case ContentArchive::Parametric:  return "CAD";
        case ContentArchive::Arrangement: return "Scenes";
        case ContentArchive::Material:    return "Materials";
        case ContentArchive::Generator:   return "Generators";
        case ContentArchive::Typeface:    return "Fonts";
        case ContentArchive::Vector:      return "SVG / Vectors";
        default:                          return "Unclassed";
    }
}

SymbolSubject ArchiveCrest(ContentArchive Archive)
{
    // 📝 The reference names lucide `box`, `box-select`, `layout`, `palette`, `type` and `pen-tool`. Four
    //    of the six are unresolved in the symbol roster, so they crest as the placeholder the roster
    //    declares for exactly this — a dummy crest, which is what was asked for at this stage.
    switch (Archive)
    {
        case ContentArchive::Topology:    return SymbolSubject::CubeSolid;
        case ContentArchive::Parametric:  return SymbolSubject::SketchPlane;
        case ContentArchive::Arrangement: return SymbolSubject::PanelSplit;
        case ContentArchive::Material:    return SymbolSubject::MaterialSphere;
        case ContentArchive::Generator:   return SymbolSubject::PlaceholderMark;
        case ContentArchive::Typeface:    return SymbolSubject::CodeBrackets;
        case ContentArchive::Vector:      return SymbolSubject::UnwrapSeam;
        default:                          return SymbolSubject::PlaceholderMark;
    }
}

void FormatOctets(char* Written, std::uint32_t Limit, double Octets)
{
    if (Written == nullptr || Limit == 0u)
        return;
    if (Octets < OctetsPerMegaOctet * 0.1)
        std::snprintf(Written, Limit, "%.1f KB", Octets / OctetsPerKiloOctet);
    else
        std::snprintf(Written, Limit, "%.1f MB", Octets / OctetsPerMegaOctet);
}

void ApplyReferenceContent(ContentLibrary& Applying)
{
    Applying = ContentLibrary{};

    // 📐 `ASSETS` in its declared order. The reference states each size as a megaoctet count times
    //    1048576, so the product is stated here rather than the rounded figure the inspector prints.
    const auto Apply = [&](const char* Naming, const char* Extension, double MegaOctets,
                          const char* FirstTag, const char* SecondTag,
                          ContentArchive Archive, const char* Subheading)
    {
        if (Applying.RecordCount >= ContentLibrary::RecordLimit)
            return;

        ContentRecord& Written = Applying.Records[Applying.RecordCount];

        Written.Naming     = Naming;
        Written.Extension  = Extension;
        Written.Subheading = Subheading;
        Written.Octets     = MegaOctets * OctetsPerMegaOctet;
        Written.Archive    = Archive;
        Written.Tags[0]    = FirstTag;
        Written.TagCount   = 1u;

        if (SecondTag != nullptr)
        {
            Written.Tags[1] = SecondTag;
            Written.TagCount = 2u;
        }

        ++Applying.RecordCount;
    };

    // 🧩 Complete simplified reference catalogue, preserved in declared HTML order.
    #include "ContentBrowserReferenceCatalog.inc"

    // Slate's runnable default workspace is appended to the reference catalogue rather than replacing it.
    Apply("WhiteTeaService", ".codex", 6064.0 / OctetsPerMegaOctet,
          "scene", "tea service", ContentArchive::Arrangement, "Engine Content");

}

//------------------------------------------------------------------------------------------------------------------------
//                                                      BRING-UP AND SAMPLING
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::Reapply(const ThemeProfile& Resolved)
{
    // 📝 Only the colours are restated. The browser's lengths are its own reference's, not the shared metric
    //    run, and a theme is a colour choice — it must not move a single length.
    Colour = Resolved.ContentBrowser;
}

Deliver<bool> ContentBrowserPanel::ConstructContentBrowserPanel(ControlIndex& IncomingInteraction, RecordingSurface& Recording,
                                                               const ThemeProfile& Appearance)
{
    if (Interaction != nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the content browser panel is already constructed" });
    }

    const Deliver<bool> SharedDelivery = SharedControls.ConstructComponents(IncomingInteraction, Recording, Appearance);
    if (!SharedDelivery.Resolved)
        return SharedDelivery;

    Interaction = &IncomingInteraction;
    Surface     = &Recording;

    // 🔴 Every identity claimed here and none inside a tick. A refusal partway through retires the whole
    //    construction rather than leaving half a panel registered against a index it cannot fill.
    const auto Reserve = [&](ControlIdentity* Written, std::uint32_t Count) -> Deliver<bool>
    {
        for (std::uint32_t Index = 0u; Index < Count; ++Index)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();

            if (!Registered.Resolved)
            {
                Reset();
                return Deliver<bool>::Refuse(Registered.Error);
            }

            Written[Index] = Registered.Resolve();
        }

        return Deliver<bool>::Result(true);
    };

    if (const auto Verdict = Reserve(SourceRows, SourceLimit); !Verdict.Resolved)
        return Verdict;

    if (const auto Verdict = Reserve(LatticeCards, LatticeLimit); !Verdict.Resolved)
        return Verdict;

    if (const auto Verdict = Reserve(ChromeCells, ChromeLimit); !Verdict.Resolved)
        return Verdict;

    return Deliver<bool>::Result(true);
}

void ContentBrowserPanel::Advance(const PointerCondition& Incoming, double Elapsed)
{
    static_cast<void>(Elapsed);
    Sampled = Incoming;
    SharedControls.Sample(Incoming);
}

void ContentBrowserPanel::Reset()
{
    SharedControls.Reset();
    Interaction = nullptr;
    Surface     = nullptr;

    for (auto& Written : SourceRows)   Written = ControlIdentity{};
    for (auto& Written : LatticeCards) Written = ControlIdentity{};
    for (auto& Written : ChromeCells)  Written = ControlIdentity{};

    ExclusionCount = 0u;
}

bool ContentBrowserPanel::Hovered(const PlaneExtent& Extent) const
{
    if (Surface == nullptr || Surface->Excluded(Extent))
        return false;

    return Extent.Encloses(Sampled.PositionX, Sampled.PositionY);
}

void ContentBrowserPanel::RetainExclusion(const PlaneExtent& Extent)
{
    if (ExclusionCount < RegistrationDemand)
        Exclusions[ExclusionCount++] = Extent;
}

void ContentBrowserPanel::Exclude(DrawerSpace& Drawers, DrawerBearing Bearing) const
{
    for (std::uint32_t Index = 0u; Index < ExclusionCount; ++Index)
        Drawers.Exclude(Bearing, Exclusions[Index]);
}

bool ContentBrowserPanel::Pressed(ControlIdentity Target, const PlaneExtent& Extent,
                                  ContentBrowserConfiguration& Applied, const char* Tooltip)
{
    if (Interaction == nullptr)
        return false;

    RetainExclusion(Extent);

    const bool Over = Hovered(Extent);

    static_cast<void>(Applied);

    if (Tooltip != nullptr)
        SharedControls.TooltipHint(Target, Extent, TooltipDeclaration{ Tooltip, "" });

    if (Over && Sampled.ContactPressed && !Interaction->AnyDisclosed())
        Interaction->Grab(Target, ControlPart::Body);

    Interaction->DeclareHovered(Target, Over, HoverOver);

    return Over && Interaction->Released(Target);
}

bool ContentBrowserPanel::AcceptTyped(char Arrived, ContentBrowserConfiguration& Applied)
{
    if (!Applied.SeekHolding || Arrived < 0x20)
        return false;

    std::uint32_t Occupied = 0u;

    while (Occupied + 1u < ContentBrowserConfiguration::SeekLimit && Applied.Seek[Occupied] != '\0')
        ++Occupied;

    if (Occupied + 1u >= ContentBrowserConfiguration::SeekLimit)
        return false;

    Applied.Seek[Occupied]      = Arrived;
    Applied.Seek[Occupied + 1u] = '\0';

    return true;
}

bool ContentBrowserPanel::RetractTyped(ContentBrowserConfiguration& Applied)
{
    if (!Applied.SeekHolding)
        return false;

    std::uint32_t Occupied = 0u;

    while (Occupied + 1u < ContentBrowserConfiguration::SeekLimit && Applied.Seek[Occupied] != '\0')
        ++Occupied;

    if (Occupied == 0u)
        return false;

    Applied.Seek[Occupied - 1u] = '\0';
    return true;
}

bool ContentBrowserPanel::Retained(const ContentRecord& Record, const ContentLibrary& Library,
                                   const ContentBrowserConfiguration& Applied) const
{
    // 📐 `renderGrid` narrows by archive, then by subheading, then by the seek run — in that order, and
    //    each against the run the previous one left rather than against the whole library.
    if (Library.TraversedArchive != ContentLibrary::AbsentIndex &&
        static_cast<std::uint32_t>(Record.Archive) != Library.TraversedArchive)
    {
        return false;
    }

    if (Library.TraversedSubheading != nullptr &&
        !(Record.Subheading != nullptr && Within(Record.Subheading, Library.TraversedSubheading) &&
          Within(Library.TraversedSubheading, Record.Subheading)))
    {
        return false;
    }

    if (Applied.Seek[0] == '\0')
        return true;

    // 📐 The reference seeks the naming, the extension and the archive spelling joined by spaces, so a
    //    run spanning the join matches there and must match here.
    return Within(Record.Naming, Applied.Seek)
        || Within(Record.Extension, Applied.Seek)
        || Within(ArchiveNaming(Record.Archive), Applied.Seek);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHARED FRAGMENTS
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordHatch(const PlaneExtent& Extent)
{
    // 📐 `repeating-linear-gradient(45deg, rgba(255,255,255,.02) 0 1px, transparent 1px 7px)` — a 1px rule
    //    every 7px along the 45° diagonal. Each rule is one four-corner tongue confined to the plate, not a
    //    shader: the whole figure is two coverage steps, and a shader would be a second pipeline for it.
    // 🔴 One tongue per RULE and not one ground per pixel. Stepping the diagonal a pixel at a time recorded
    //    an upright 1×1 quad for every covered pixel, which on a lattice of plates put a quarter of a million
    //    vertices into a single command list and tripped ImGui's 16-bit index ceiling outright. A rule is
    //    convex, so it costs four corners however long it is.
    constexpr float Period = 7.0f;

    Surface->Confine(Extent);

    // 📐 The rule runs at 45°, so a plate Height tall shifts it a whole Height along, end to end.
    //    Origins therefore run from one full drop BEFORE the leading edge to one full drop PAST the
    //    trailing one — a rule applied at the trailing edge up top has walked a drop to the left by the
    //    time it reaches the bottom, so the origins beyond the edge are what covers the lower trailing
    //    corner. Ending at the trailing edge left that corner bare in a widening wedge.
    const float Drop  = Extent.Height();
    const float First = -Drop;
    const float Last  = Extent.Width() + Drop + Period;

    for (float Origin = First; Origin < Last; Origin += Period)
    {
        const float Upper = Extent.MinimumX + Origin;
        const float Lower = Upper - Drop;

        const float Corners[8] =
        {
            Upper,        Extent.MinimumY,
            Upper + 1.0f, Extent.MinimumY,
            Lower + 1.0f, Extent.MaximumY,
            Lower,        Extent.MaximumY
        };

        Surface->Tongue(Corners, 4u, Colour.Hatch);
    }

    Surface->Release();
}

void ContentBrowserPanel::RecordScrollbar(const PlaneExtent& Extent, ControlIdentity Target,
                                          float Span, float& Offset)
{
    // 📐 `::-webkit-scrollbar` — a 6px trough with a #333 thumb at radius 4, presented only when the run
    //    is longer than the extent that holds it.
    const float Visible = Extent.Height();

    if (Span <= Visible || Visible <= 0.0f)
    {
        Offset = 0.0f;
        return;
    }

    const float Limit     = Span - Visible;
    const float ThumbHeight = (Visible * Visible / Span < 28.0f) ? 28.0f : (Visible * Visible / Span);
    const float Travel      = Visible - ThumbHeight;

    // 🔴 The whole scrolling extent is withheld, not just the trough. The wheel reaches the run from
    //    anywhere over it, so a drag begun over a card in a drawer must not slide the drawer instead.
    RetainExclusion(Extent);

    const bool Holding = Interaction->Holding(Target);

    // 📐 The wheel reaches the run whenever the pointer is over the extent, whether or not the bar itself
    //    is hovered — the reference scrolls the container and not its scrollbar.
    if (Hovered(Extent) && !Interaction->AnyDisclosed() && Sampled.WheelY != 0.0f)
        Offset -= Sampled.WheelY * NotchHeight;

    const PlaneExtent Trough = Spanning(Extent.MaximumX - 6.0f, Extent.MinimumY, 6.0f, Visible);

    if (Hovered(Trough) && Sampled.ContactPressed && !Interaction->AnyDisclosed())
    {
        Interaction->Grab(Target, ControlPart::Thumb);
        Interaction->RecordInitial(Target, Offset);
    }

    if (Holding && Travel > 0.0f)
    {
        const Deliver<float> Previous = Interaction->InitialReading(Target);

        if (Previous.Resolved)
        {
            const float Moved = Sampled.PositionY - Interaction->OriginY();
            Offset = Previous.Resolve() + Moved * (Limit / Travel);
        }
    }

    // 🔴 Clamped last and always. Every reach above may carry the offset past either end, and a run
    //    recorded from a past-the-end offset presents an empty extent that reads as a panel that failed.
    if (Offset < 0.0f)       Offset = 0.0f;
    if (Offset > Limit)    Offset = Limit;

    const float ThumbY = Extent.MinimumY + (Limit > 0.0f ? (Offset / Limit) * Travel : 0.0f);

    Surface->Ground(Spanning(Extent.MaximumX - 6.0f + 3.0f, ThumbY, 3.0f, ThumbHeight),
                    Holding ? Colour.EdgeHovered : Colour.GripQuiet, 2.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SOURCES ASIDE
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordSources(const PlaneExtent& Extent, ContentLibrary& Library,
                                        ContentBrowserConfiguration& Applied)
{
    Surface->Ground(Extent, Colour.Aside);
    Surface->Ground(Spanning(Extent.MaximumX - 1.0f, Extent.MinimumY, 1.0f, Extent.Height()),
                    Colour.Stroke);

    // 📐 `p-4 pb-2` over a `text-[10px] tracking-widest uppercase` caption.
    Surface->TextRunCapitalised(Extent.MinimumX + 16.0f, Extent.MinimumY + 16.0f,
                                Colour.Faint, "Sources", Measure.RunCaption, 1.6f, true);

    // 📐 The library cache foot is pinned to the lower edge, so the traversable run is what remains.
    constexpr float FootY = 69.0f;   // [px] - border-t p-4 over two runs and a 4px meter

    const PlaneExtent Traversable = Spanning(Extent.MinimumX, Extent.MinimumY + 40.0f,
                                             Extent.Width(),
                                             Extent.Height() - 40.0f - FootY);

    Surface->Confine(Traversable);

    float Cursor = Traversable.MinimumY - Applied.AsideOffset + 8.0f;
    std::uint32_t Target = 0u;

    const auto SourceRow = [&](const char* Naming, std::uint32_t Count, float Step,
                               bool Current, SymbolSubject Crest, bool Crested)
    {
        if (Target >= SourceLimit)
            return false;

        const PlaneExtent Row = Spanning(Extent.MinimumX + 8.0f + Step, Cursor,
                                         Extent.Width() - 16.0f - Step, Measure.SourceRowHeight);

        const bool Over  = Hovered(Row);
        const bool Taken = Pressed(SourceRows[Target], Row, Applied);

        if (Current)
            Surface->Ground(Row, Colour.Taken, Measure.RadiusSoft);
        else if (Over)
            Surface->Ground(Row, Colour.Hovered, Measure.RadiusSoft);

        const ThemeToken Run = (Current || Over) ? Colour.Primary : Colour.Secondary;

        float X = Row.MinimumX + 8.0f;

        // 📐 A crested row strokes its chevron or its folder; an uncrested one holds the same 14px of
        //    space empty, exactly as the reference's `<span class="w-3.5 h-3.5">` does.
        if (Crested)
        {
            Surface->Stroke(Crest, Spanning(X, Row.MinimumY + 8.0f, 14.0f, 14.0f), Run);
        }

        X += 22.0f;

        char Tally[16] = {};
        std::snprintf(Tally, sizeof(Tally), "%u", Count);

        const float TallyX = Surface->MeasureRun(Tally, Measure.RunCaption);

        Surface->TextRunTruncated(X, Row.MinimumY + 9.0f,
                                  Row.MaximumX - X - TallyX - 12.0f, Run,
                                  Naming, Measure.RunBody);

        Surface->TextRun(Row.MaximumX - TallyX - 8.0f, Row.MinimumY + 10.0f,
                         Run, Tally, Measure.RunCaption);

        Cursor += Measure.SourceRowHeight + 2.0f;
        ++Target;

        return Taken;
    };

    // 📐 `Project` — the section caption above the library row.
    Surface->TextRunCapitalised(Extent.MinimumX + 16.0f, Cursor + 4.0f,
                                Colour.Faint, "Project", Measure.RunCaption, 1.6f, true);
    Cursor += Measure.CaptionHeight;

    // 📐 `Project Library` — standing whenever no archive is traversed, as `!state.cat` decides it.
    if (SourceRow("Project Library", Library.RecordCount, 0.0f,
                  Library.TraversedArchive == ContentLibrary::AbsentIndex,
                  SymbolSubject::FolderClosed, true))
    {
        Library.TraversedArchive    = ContentLibrary::AbsentIndex;
        Library.TraversedSubheading = nullptr;
        Library.Taken               = ContentLibrary::AbsentIndex;
    }

    // 📐 The archives, in the order the library first presents them, exactly as `Array.from(new Set(...))`
    //    yields them rather than in the enum's own order.
    constexpr std::uint32_t ArchiveLimit = static_cast<std::uint32_t>(ContentArchive::ArchiveCount);

    bool Current[ArchiveLimit] = {};

    for (std::uint32_t Index = 0u; Index < Library.RecordCount; ++Index)
    {
        const auto Archive = static_cast<std::uint32_t>(Library.Records[Index].Archive);

        if (Archive >= static_cast<std::uint32_t>(ContentArchive::ArchiveCount) || Current[Archive])
            continue;

        Current[Archive] = true;

        std::uint32_t Beneath = 0u;

        for (std::uint32_t Scan = 0u; Scan < Library.RecordCount; ++Scan)
        {
            if (static_cast<std::uint32_t>(Library.Records[Scan].Archive) == Archive)
                ++Beneath;
        }

        // 📐 A chevron only where subheadings stand beneath, which is what `subcats.length > 0` states.
        bool Subheaded = false;

        for (std::uint32_t Scan = 0u; Scan < Library.RecordCount; ++Scan)
        {
            if (static_cast<std::uint32_t>(Library.Records[Scan].Archive) == Archive &&
                Library.Records[Scan].Subheading != nullptr)
            {
                Subheaded = true;
            }
        }

        const bool ArchiveCurrent = Library.TraversedArchive == Archive &&
                              Library.TraversedSubheading == nullptr;

        if (SourceRow(ArchiveNaming(static_cast<ContentArchive>(Archive)), Beneath,
                      Measure.SourceStepX, ArchiveCurrent, SymbolSubject::ChevronDown, Subheaded))
        {
            Library.TraversedArchive    = Archive;
            Library.TraversedSubheading = nullptr;
            Library.Taken               = ContentLibrary::AbsentIndex;
        }

        if (!Subheaded)
            continue;

        // 📐 The subheadings beneath, each in first-presented order and stepped one further nesting in.
        for (std::uint32_t Scan = 0u; Scan < Library.RecordCount; ++Scan)
        {
            const ContentRecord& Record = Library.Records[Scan];

            if (static_cast<std::uint32_t>(Record.Archive) != Archive || Record.Subheading == nullptr)
                continue;

            bool Repeated = false;

            for (std::uint32_t Prior = 0u; Prior < Scan; ++Prior)
            {
                if (static_cast<std::uint32_t>(Library.Records[Prior].Archive) == Archive &&
                    Library.Records[Prior].Subheading != nullptr &&
                    Within(Library.Records[Prior].Subheading, Record.Subheading) &&
                    Within(Record.Subheading, Library.Records[Prior].Subheading))
                {
                    Repeated = true;
                }
            }

            if (Repeated)
                continue;

            std::uint32_t Tallied = 0u;

            for (std::uint32_t Count = 0u; Count < Library.RecordCount; ++Count)
            {
                const ContentRecord& Weighed = Library.Records[Count];

                if (static_cast<std::uint32_t>(Weighed.Archive) == Archive &&
                    Weighed.Subheading != nullptr &&
                    Within(Weighed.Subheading, Record.Subheading) &&
                    Within(Record.Subheading, Weighed.Subheading))
                {
                    ++Tallied;
                }
            }

            const bool SubCurrent = Library.TraversedArchive == Archive &&
                                     Library.TraversedSubheading != nullptr &&
                                     Within(Library.TraversedSubheading, Record.Subheading) &&
                                     Within(Record.Subheading, Library.TraversedSubheading);

            if (SourceRow(Record.Subheading, Tallied, Measure.SourceStepX * 2.0f,
                          SubCurrent, SymbolSubject::PlaceholderMark, false))
            {
                Library.TraversedArchive    = Archive;
                Library.TraversedSubheading = Record.Subheading;
                Library.Taken               = ContentLibrary::AbsentIndex;
            }
        }
    }

    Applied.AsideSpan = (Cursor + Applied.AsideOffset) - Traversable.MinimumY;

    Surface->Release();

    RecordScrollbar(Traversable, ChromeCells[8], Applied.AsideSpan, Applied.AsideOffset);

    // 📐 The library cache foot — a caption pair over a three-part meter at 38 / 24 / 12 percent.
    const PlaneExtent Foot = Spanning(Extent.MinimumX, Extent.MaximumY - FootY,
                                      Extent.Width(), FootY);

    Surface->Ground(Spanning(Foot.MinimumX, Foot.MinimumY, Foot.Width(), 1.0f), Colour.Stroke);

    Surface->TextRun(Foot.MinimumX + 16.0f, Foot.MinimumY + 16.0f,
                     Colour.Faint, "Library cache", Measure.RunCaption);

    const float RetainedX = Surface->MeasureRun("12.4 GB", Measure.RunCaption);

    Surface->TextRun(Foot.MaximumX - 16.0f - RetainedX, Foot.MinimumY + 16.0f,
                     Colour.Primary, "12.4 GB", Measure.RunCaption);

    const PlaneExtent Meter = Spanning(Foot.MinimumX + 16.0f, Foot.MinimumY + 38.0f,
                                       Foot.Width() - 32.0f, 4.0f);

    Surface->Ground(Meter, Colour.Taken, 2.0f);

    const float MeterSpan = Meter.Width();

    Surface->Ground(Spanning(Meter.MinimumX, Meter.MinimumY, MeterSpan * 0.38f, 4.0f),
                    Colour.Primary, 2.0f);
    Surface->Ground(Spanning(Meter.MinimumX + MeterSpan * 0.38f, Meter.MinimumY,
                             MeterSpan * 0.24f, 4.0f), Colour.Faint);
    Surface->Ground(Spanning(Meter.MinimumX + MeterSpan * 0.62f, Meter.MinimumY,
                             MeterSpan * 0.12f, 4.0f), Colour.MeterQuiet);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SEEK RAIL
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordSeekRail(const PlaneExtent& Extent, ContentBrowserConfiguration& Applied)
{
    Surface->Ground(Extent, Colour.Aside);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MaximumY - 1.0f, Extent.Width(), 1.0f),
                    Colour.Stroke);

    // 📐 `flex-1 max-w-md h-8 ... rounded-full` — the seek field, pinned to the leading edge.
    const float FieldX = (Extent.Width() - 32.0f - 200.0f < Measure.SeekX)
                           ? (Extent.Width() - 32.0f - 200.0f) : Measure.SeekX;

    const PlaneExtent SeekField = Spanning(Extent.MinimumX + 16.0f,
                                           Extent.MinimumY + 12.0f,
                                           (FieldX < 120.0f) ? 120.0f : FieldX,
                                           Measure.SeekHeight);

    const bool SeekPressed = Pressed(ChromeCells[0], SeekField, Applied);

    // 📐 `focus-within:border-white/40` — the field holds the keyboard until a contact lands off it.
    if (SeekPressed)
        Applied.SeekHolding = true;
    else if (Sampled.ContactPressed && !Hovered(SeekField))
        Applied.SeekHolding = false;

    Surface->Ground(SeekField, Colour.Field, Measure.SeekHeight * 0.5f);
    Surface->Edge(SeekField, Applied.SeekHolding ? Colour.EdgeHolding : Colour.Stroke,
                  1.0f, Measure.SeekHeight * 0.5f);

    Surface->Stroke(SymbolSubject::MagnifierLens,
                    Spanning(SeekField.MinimumX + 12.0f, SeekField.MinimumY + 9.0f, 14.0f, 14.0f),
                    Colour.Faint);

    const bool Sought = Applied.Seek[0] != '\0';

    Surface->TextRunTruncated(SeekField.MinimumX + 34.0f, SeekField.MinimumY + 10.0f,
                              SeekField.Width() - 70.0f,
                              Sought ? Colour.Primary : Colour.Faintest,
                              Sought ? Applied.Seek : "Search assets, formats, tags...",
                              Measure.RunBody);

    // 📐 The caret sits at the run's own trailing edge while the field holds the keyboard.
    if (Applied.SeekHolding)
    {
        const float Caret = SeekField.MinimumX + 34.0f +
                            Surface->MeasureRun(Applied.Seek, Measure.RunBody);

        Surface->Ground(Spanning(Caret, SeekField.MinimumY + 8.0f, 1.0f, 16.0f), Colour.Primary);
    }

    // 📐 The `/` chip — the reference's own keyboard hint, at the field's trailing edge.
    const PlaneExtent Hint = Spanning(SeekField.MaximumX - 26.0f, SeekField.MinimumY + 8.0f,
                                      18.0f, 16.0f);

    Surface->Ground(Hint, Colour.Hovered, 4.0f);
    Surface->Edge(Hint, Colour.Stroke, 1.0f, 4.0f);
    Surface->TextRun(Hint.MinimumX + 6.0f, Hint.MinimumY + 2.0f, Colour.Faint, "/", Measure.RunCaption);

    // 📐 `Create` and `Import`, pinned to the trailing edge with `ml-auto`. Import is the filled action.
    const float ImportX = 84.0f;
    const float CreateX = 82.0f;

    const PlaneExtent Import = Spanning(Extent.MaximumX - 16.0f - ImportX,
                                        Extent.MinimumY + 12.0f, ImportX, Measure.SeekHeight);

    const PlaneExtent Create = Spanning(Import.MinimumX - 8.0f - CreateX,
                                        Extent.MinimumY + 12.0f, CreateX, Measure.SeekHeight);
    const PlaneExtent Arrangement = Spanning(Create.MinimumX - 36.0f,
                                             Extent.MinimumY + 12.0f, 28.0f, Measure.SeekHeight);

    if (Pressed(ChromeCells[11], Arrangement, Applied, "Switch grid/list arrangement"))
        Applied.GridArrangement = !Applied.GridArrangement;
    Surface->Ground(Arrangement, Hovered(Arrangement) ? Colour.Taken : Colour.Hovered, Measure.RadiusSoft);
    Surface->TextRun(Arrangement.MinimumX + 8.0f, Arrangement.MinimumY + 9.0f,
                     Colour.Primary, Applied.GridArrangement ? "▦" : "☷", Measure.RunBody);

    static_cast<void>(Pressed(ChromeCells[1], Create, Applied, "Create a new record"));
    if (Pressed(ChromeCells[2], Import, Applied, "Import into the library"))
        Applied.Page = ContentBrowserPage::Import;

    const bool CreateOver = Hovered(Create);

    Surface->Ground(Create, CreateOver ? Colour.Taken : Colour.Hovered,
                    Measure.SeekHeight * 0.5f);
    Surface->Edge(Create, Colour.Stroke, 1.0f, Measure.SeekHeight * 0.5f);
    Surface->Stroke(SymbolSubject::PlusCross,
                    Spanning(Create.MinimumX + 12.0f, Create.MinimumY + 9.0f, 14.0f, 14.0f),
                    CreateOver ? Colour.Primary : Colour.Secondary);
    Surface->TextRun(Create.MinimumX + 32.0f, Create.MinimumY + 10.0f,
                     CreateOver ? Colour.Primary : Colour.Secondary, "Create", Measure.RunBody);

    const bool ImportOver = Hovered(Import);

    Surface->Ground(Import, ImportOver ? Colour.EmphaticHovered : Colour.Emphatic,
                    Measure.SeekHeight * 0.5f);
    Surface->Stroke(SymbolSubject::PersistDisc,
                    Spanning(Import.MinimumX + 12.0f, Import.MinimumY + 9.0f, 14.0f, 14.0f),
                    Colour.EmphaticRun);
    Surface->TextRun(Import.MinimumX + 32.0f, Import.MinimumY + 10.0f,
                     Colour.EmphaticRun, "Import", Measure.RunBody, 0.0f, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LATTICE
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordLattice(const PlaneExtent& Extent, ContentLibrary& Library,
                                        ContentBrowserConfiguration& Applied)
{
    Surface->Ground(Extent, Colour.Ground);

    // 📐 The breadcrumb rail — `sticky top-0`, so it is recorded before the run and never scrolls with it.
    const PlaneExtent Rail = Spanning(Extent.MinimumX, Extent.MinimumY,
                                      Extent.Width(), Measure.BreadcrumbHeight);

    Surface->Ground(Rail, Colour.Rail);
    Surface->Ground(Spanning(Rail.MinimumX, Rail.MaximumY - 1.0f, Rail.Width(), 1.0f), Colour.Stroke);

    float Crumb = Rail.MinimumX + 16.0f;

    Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Faint, "Harbor", Measure.RunBody);
    Crumb += Surface->MeasureRun("Harbor", Measure.RunBody) + 8.0f;
    Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Faint, "/", Measure.RunBody);
    Crumb += Surface->MeasureRun("/", Measure.RunBody) + 8.0f;

    if (Library.TraversedArchive == ContentLibrary::AbsentIndex)
    {
        Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Primary, "Project Library",
                         Measure.RunBody, 0.0f, true);
    }
    else
    {
        const char* Naming = ArchiveNaming(static_cast<ContentArchive>(Library.TraversedArchive));

        if (Library.TraversedSubheading == nullptr)
        {
            Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Primary, Naming,
                             Measure.RunBody, 0.0f, true);
        }
        else
        {
            Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Faint, Naming, Measure.RunBody);
            Crumb += Surface->MeasureRun(Naming, Measure.RunBody) + 8.0f;
            Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Faint, "/", Measure.RunBody);
            Crumb += Surface->MeasureRun("/", Measure.RunBody) + 8.0f;
            Surface->TextRun(Crumb, Rail.MinimumY + 14.0f, Colour.Primary,
                             Library.TraversedSubheading, Measure.RunBody, 0.0f, true);
        }
    }

    // 📐 The run itself, beneath the rail and clipped to what remains.
    const PlaneExtent Run = Spanning(Extent.MinimumX, Rail.MaximumY,
                                     Extent.Width(), Extent.Height() - Measure.BreadcrumbHeight);

    Surface->Confine(Run);

    const std::uint32_t Columns = Applied.GridArrangement
        ? ColumnsWithin(Run.Width(), Measure.CardGap, Measure.CardPad) : 1u;

    const float CardX = (Run.Width() - Measure.CardPad * 2.0f -
                             Measure.CardGap * static_cast<float>(Columns - 1u)) /
                            static_cast<float>(Columns);

    // 🧩 The rail switches the same retained records between reference cards and one compact list row.
    const float PlateY = Applied.GridArrangement ? CardX * 0.75f : 0.0f;
    const float CardHeight = Applied.GridArrangement ? PlateY + Measure.CardCaptionHeight : 38.0f;

    std::uint32_t Count    = 0u;
    std::uint32_t Target = 0u;

    for (std::uint32_t Index = 0u; Index < Library.RecordCount && Target < LatticeLimit; ++Index)
    {
        const ContentRecord& Record = Library.Records[Index];

        if (!Retained(Record, Library, Applied))
            continue;

        const std::uint32_t Column = Count % Columns;
        const std::uint32_t Course = Count / Columns;

        const float X  = Run.MinimumX + Measure.CardPad +
                             static_cast<float>(Column) * (CardX + Measure.CardGap);
        const float Y = Run.MinimumY + Measure.CardPad +
                             static_cast<float>(Course) * (CardHeight + Measure.CardGap) -
                             Applied.LatticeOffset;

        const PlaneExtent Card = Spanning(X, Y, CardX, CardHeight);

        const bool Current = Library.Taken == Index;
        const bool Over     = Hovered(Card);

        if (Pressed(LatticeCards[Target], Card, Applied) ||
            (Over && Sampled.ContactPressed && !Interaction->AnyDisclosed()))
        {
            Library.Taken = Index;
            if (Library.Records[Index].Archive == ContentArchive::Arrangement &&
                (std::strcmp(Library.Records[Index].Extension, ".codex") == 0 ||
                 std::strcmp(Library.Records[Index].Extension, "codex") == 0))
                Applied.ActivationRequested = Index;
        }

        // 📐 `hover:-translate-y-0.5` — the hovered card lifts two pixels, which is the whole of the
        //    reference's hover motion apart from its shadow.
        const PlaneExtent Lifted = Over
            ? Spanning(Card.MinimumX, Card.MinimumY - 2.0f, CardX, CardHeight)
            : Card;

        const bool MaterialCard = Record.Archive == ContentArchive::Material;
        const PlaneExtent Plate = Spanning(Lifted.MinimumX, Lifted.MinimumY, CardX, PlateY);

        // 🧩 Material catalogue entries are the reference's isolated round preview and caption: no card,
        // no square backing, and no hatch. Other authored asset kinds keep their document-card treatment.
        if (!MaterialCard)
        {
            Surface->Scrim(Lifted, Colour.CardUpper, Colour.CardLower);
            Surface->MaskCorners(Lifted, Colour.Ground, Measure.RadiusCard);
            Surface->Ground(Plate, Colour.Plate, Measure.RadiusCard,
                            CornerLeadingUpper | CornerTrailingUpper);
            RecordHatch(Plate);
            Surface->Ground(Spanning(Plate.MinimumX, Plate.MaximumY - 1.0f, CardX, 1.0f), Colour.Stroke);
        }

        const float CrestSpan = Over ? 34.0f : 32.0f;

        // 🧩 Material cards carry the reference browser's circular preview seat. Image/atlas content is
        // supplied later; the circular boundary is already stable so adding a tile never changes layout.
        if (Record.Archive == ContentArchive::Material)
        {
            const float Radius = (Over ? 30.0f : 28.0f);
            Surface->Medallion(Plate.MinimumX + CardX * 0.5f, Plate.MinimumY + PlateY * 0.5f,
                               Radius, Colour.Medallion);
            Surface->Edge(Spanning(Plate.MinimumX + CardX * 0.5f - Radius,
                                   Plate.MinimumY + PlateY * 0.5f - Radius,
                                   Radius * 2.0f, Radius * 2.0f),
                          Over ? Colour.EdgeHovered : Colour.Stroke, 1.0f, Radius);
        }
        else
        {
            Surface->Stroke(ArchiveCrest(Record.Archive),
                            Spanning(Plate.MinimumX + (CardX - CrestSpan) * 0.5f,
                                     Plate.MinimumY + (PlateY - CrestSpan) * 0.5f,
                                     CrestSpan, CrestSpan),
                            Over ? Colour.Secondary : Colour.Faintest);
        }

        // 📐 The extension chip — `absolute left-2 bottom-2`, a medallion and the run beside it.
        char Extension[16] = {};
        std::snprintf(Extension, sizeof(Extension), "%s", Record.Extension);

        for (std::uint32_t Step = 0u; Extension[Step] != '\0'; ++Step)
        {
            if (Extension[Step] >= 'a' && Extension[Step] <= 'z')
                Extension[Step] = static_cast<char>(Extension[Step] - 'a' + 'A');
        }

        if (!MaterialCard)
        {
            const float ChipX = Surface->MeasureRun(Extension, 9.0f) + 24.0f;
            const PlaneExtent Chip = Spanning(Plate.MinimumX + 8.0f,
                                              Plate.MaximumY - 8.0f - Measure.ChipHeight,
                                              ChipX, Measure.ChipHeight);
            Surface->Ground(Chip, Colour.ChipGround, Measure.RadiusSoft);
            Surface->Edge(Chip, Colour.Stroke, 1.0f, Measure.RadiusSoft);
            Surface->Medallion(Chip.MinimumX + 9.0f, Chip.MinimumY + Measure.ChipHeight * 0.5f,
                               3.0f, Colour.Secondary);
            Surface->TextRun(Chip.MinimumX + 16.0f, Chip.MinimumY + 5.0f,
                             Colour.Secondary, Extension, 9.0f, 0.6f);
        }

        // 📐 The caption pair — `name.ext` truncated, and the size beneath it.
        char Titled[96] = {};
        std::snprintf(Titled, sizeof(Titled), "%s.%s", Record.Naming, Record.Extension);

        Surface->TextRunTruncated(Lifted.MinimumX + 10.0f, Plate.MaximumY + 8.0f,
                                  CardX - 20.0f, Colour.Primary, Titled, Measure.RunBody, true);

        char Sized[32] = {};
        FormatOctets(Sized, sizeof(Sized), Record.Octets);

        Surface->TextRun(Lifted.MinimumX + 10.0f, Plate.MaximumY + 26.0f,
                         Colour.Faint, Sized, Measure.RunCaption);

        // 📐 `border ... overflow-hidden` — the border box clips its children, so the edge is in FRONT of
        //    everything the card holds. Recorded last for that reason.
        // 🔴 Recorded before the plate it reads over, the taken card's white/60 edge was overdrawn along
        //    its whole upper run by the plate's own ground, and a taken card was then indistinguishable
        //    from an untaken one down the three sides the caption did not cover.
        if (!MaterialCard)
        {
            Surface->Edge(Lifted, Current ? Colour.EdgeTaken : (Over ? Colour.EdgeHovered : Colour.Stroke),
                          1.0f, Measure.RadiusCard);
        }

        ++Count;
        ++Target;
    }

    // 📐 What the run occupied, so the next tick's scroll is held against a measured span.
    const std::uint32_t Courses = (Count + Columns - 1u) / Columns;

    Applied.LatticeSpan = Measure.CardPad * 2.0f +
                         static_cast<float>(Courses) * (CardHeight + Measure.CardGap);

    // 📐 An empty run states why rather than presenting nothing, which reads as a panel that failed.
    if (Count == 0u)
    {
        Surface->TextRun(Run.MinimumX + Measure.CardPad, Run.MinimumY + Measure.CardPad + 8.0f,
                         Colour.Faint, "No record answers this search.", Measure.RunBody);
    }

    Surface->Release();

    RecordScrollbar(Run, ChromeCells[9], Applied.LatticeSpan, Applied.LatticeOffset);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordInspector(const PlaneExtent& Extent, ContentLibrary& Library,
                                          ContentBrowserConfiguration& Applied)
{
    Surface->Ground(Extent, Colour.Aside);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MinimumY, 1.0f, Extent.Height()), Colour.Stroke);

    // 📐 The tongue pair — `Details` and `Create`, `p-2` over an `h-8` each.
    const PlaneExtent Tongues = Spanning(Extent.MinimumX, Extent.MinimumY,
                                         Extent.Width(), Measure.TongueY);

    Surface->Ground(Tongues, Colour.Rail);
    Surface->Ground(Spanning(Tongues.MinimumX, Tongues.MaximumY - 1.0f, Tongues.Width(), 1.0f),
                    Colour.Stroke);

    const float TongueX = (Tongues.Width() - 12.0f) * 0.5f;

    const char*         TongueNaming[2] = { "Details", "Create" };
    const SymbolSubject TongueCrest[2]  = { SymbolSubject::BulbFilament, SymbolSubject::PlusCross };

    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        const PlaneExtent Tongue = Spanning(Tongues.MinimumX + 8.0f +
                                            static_cast<float>(Index) * (TongueX + 4.0f),
                                            Tongues.MinimumY + 8.0f, TongueX, 32.0f);

        if (Pressed(ChromeCells[3u + Index], Tongue, Applied))
            Applied.InspectorTongue = Index;

        const bool Current = Applied.InspectorTongue == Index;

        if (Current)
            Surface->Ground(Tongue, Colour.Taken, Measure.RadiusSoft);

        const ThemeToken Run = Current ? Colour.Primary
                                         : (Hovered(Tongue) ? Colour.Secondary : Colour.Faint);

        const float Titled = Surface->MeasureRun(TongueNaming[Index], Measure.RunBody);
        const float Origin = Tongue.MinimumX + (TongueX - Titled - 20.0f) * 0.5f;

        Surface->Stroke(TongueCrest[Index],
                        Spanning(Origin, Tongue.MinimumY + 9.0f, 14.0f, 14.0f), Run);
        Surface->TextRun(Origin + 20.0f, Tongue.MinimumY + 10.0f, Run,
                         TongueNaming[Index], Measure.RunBody);
    }

    // 📐 The preview — `h-48`, always presented, whether or not a record stands taken.
    const PlaneExtent Preview = Spanning(Extent.MinimumX, Tongues.MaximumY,
                                         Extent.Width(), Measure.PreviewHeight);

    Surface->Ground(Preview, Colour.Ground);
    Surface->Ground(Spanning(Preview.MinimumX, Preview.MaximumY - 1.0f, Preview.Width(), 1.0f),
                    Colour.Stroke);

    const bool Taken = Library.Taken != ContentLibrary::AbsentIndex &&
                       Library.Taken < Library.RecordCount;

    if (!Taken)
    {
        const float Titled = Surface->MeasureRun("No preview available", Measure.RunBody);

        Surface->TextRun(Preview.MinimumX + (Preview.Width() - Titled) * 0.5f,
                         Preview.MinimumY + Measure.PreviewHeight * 0.5f - 8.0f,
                         Colour.Faintest, "No preview available", Measure.RunBody);

        const float Nothing = Surface->MeasureRun("Nothing selected", Measure.RunBody);

        Surface->TextRun(Extent.MinimumX + (Extent.Width() - Nothing) * 0.5f,
                         Preview.MaximumY + 32.0f, Colour.Primary, "Nothing selected",
                         Measure.RunBody, 0.0f, true);

        Surface->TextRun(Extent.MinimumX + 24.0f, Preview.MaximumY + 54.0f, Colour.Faint,
                         "Select an asset to inspect its", Measure.RunBody);
        Surface->TextRun(Extent.MinimumX + 24.0f, Preview.MaximumY + 72.0f, Colour.Faint,
                         "metadata and import options.", Measure.RunBody);
        return;
    }

    const ContentRecord& Record = Library.Records[Library.Taken];

    // 🔴 The 3D preview is deliberately absent. The reference reaches Three.js for a rotating solid, and
    //    a viewport is a separate mechanism from a panel recording — it was excluded from this pass by
    //    instruction, so the preview states what it would present rather than pretending to present it.
    RecordHatch(Preview);

    Surface->Stroke(ArchiveCrest(Record.Archive),
                    Spanning(Preview.MinimumX + Preview.Width() * 0.5f - 28.0f,
                             Preview.MinimumY + Measure.PreviewHeight * 0.5f - 34.0f, 56.0f, 56.0f),
                    Colour.Faintest);

    Surface->TextRunCapitalised(Preview.MinimumX + 8.0f, Preview.MinimumY + 8.0f,
                                Colour.Faint, "2d preview", 9.0f, 1.2f);

    const float Extension = Surface->MeasureRun(Record.Extension, 9.0f);

    Surface->TextRunCapitalised(Preview.MaximumX - 8.0f - Extension, Preview.MinimumY + 8.0f,
                                Colour.Faint, Record.Extension, 9.0f, 1.2f);

    const float Awaiting = Surface->MeasureRun("dimensional preview withheld", Measure.RunCaption);

    Surface->TextRun(Preview.MinimumX + (Preview.Width() - Awaiting) * 0.5f,
                     Preview.MaximumY - 24.0f, Colour.Faintest,
                     "dimensional preview withheld", Measure.RunCaption);

    // 📐 The crest row — a 40px medallion, the naming beside it and its tags beneath.
    const PlaneExtent Crest = Spanning(Extent.MinimumX, Preview.MaximumY, Extent.Width(), 68.0f);

    Surface->Ground(Spanning(Crest.MinimumX, Crest.MaximumY - 1.0f, Crest.Width(), 1.0f),
                    Colour.Stroke);

    const PlaneExtent Plate = Spanning(Crest.MinimumX + 12.0f, Crest.MinimumY + 12.0f,
                                       Measure.CrestX, Measure.CrestX);

    Surface->Ground(Plate, Colour.Medallion, Measure.RadiusPlate);
    Surface->Edge(Plate, Colour.Stroke, 1.0f, Measure.RadiusPlate);
    Surface->Stroke(ArchiveCrest(Record.Archive),
                    Spanning(Plate.MinimumX + 10.0f, Plate.MinimumY + 10.0f, 20.0f, 20.0f),
                    Colour.Secondary);

    char Titled[96] = {};
    std::snprintf(Titled, sizeof(Titled), "%s.%s", Record.Naming, Record.Extension);

    Surface->TextRunTruncated(Plate.MaximumX + 12.0f, Crest.MinimumY + 12.0f,
                              Crest.MaximumX - Plate.MaximumX - 24.0f, Colour.Primary,
                              Titled, Measure.RunCrest, true);

    float TagX = Plate.MaximumX + 12.0f;

    for (std::uint32_t Index = 0u; Index < Record.TagCount; ++Index)
    {
        const float Span = Surface->MeasureRun(Record.Tags[Index], Measure.RunCaption) + 12.0f;

        const PlaneExtent Tag = Spanning(TagX, Crest.MinimumY + 34.0f, Span, Measure.ChipHeight);

        Surface->Ground(Tag, Colour.Hatch, 4.0f);
        Surface->Edge(Tag, Colour.Stroke, 1.0f, 4.0f);
        Surface->TextRun(Tag.MinimumX + 6.0f, Tag.MinimumY + 5.0f, Colour.Secondary,
                         Record.Tags[Index], Measure.RunCaption);

        TagX += Span + 4.0f;
    }

    // 📐 The properties section — a caption over a run of name/reading pairs.
    const PlaneExtent Properties = Spanning(Extent.MinimumX, Crest.MaximumY,
                                            Extent.Width(), 118.0f);

    Surface->Ground(Spanning(Properties.MinimumX, Properties.MaximumY - 1.0f,
                             Properties.Width(), 1.0f), Colour.Stroke);

    Surface->TextRunCapitalised(Properties.MinimumX + 12.0f, Properties.MinimumY + 12.0f,
                                Colour.Faint, "Properties", Measure.RunCaption, 1.6f, true);

    float Pair = Properties.MinimumY + 36.0f;

    const auto RecordPair = [&](const char* Naming, const char* Reading)
    {
        Surface->TextRun(Properties.MinimumX + 12.0f, Pair, Colour.Faint, Naming, Measure.RunBody);

        const float Span = Surface->MeasureRun(Reading, Measure.RunBody);

        Surface->TextRun(Properties.MaximumX - 12.0f - Span, Pair, Colour.Primary,
                         Reading, Measure.RunBody);

        Pair += 20.0f;
    };

    char Extended[16] = {};
    std::snprintf(Extended, sizeof(Extended), "%s", Record.Extension);

    for (std::uint32_t Step = 0u; Extended[Step] != '\0'; ++Step)
    {
        if (Extended[Step] >= 'a' && Extended[Step] <= 'z')
            Extended[Step] = static_cast<char>(Extended[Step] - 'a' + 'A');
    }

    RecordPair("Format",   Extended);
    RecordPair("Category", ArchiveNaming(Record.Archive));

    if (Record.Subheading != nullptr)
        RecordPair("Subcategory", Record.Subheading);

    char Sized[32] = {};
    FormatOctets(Sized, sizeof(Sized), Record.Octets);

    RecordPair("Size", Sized);

    // 📐 `mt-auto` — the import action is pinned to the inspector's own lower edge.
    const PlaneExtent Import = Spanning(Extent.MinimumX + 12.0f,
                                        Extent.MaximumY - 12.0f - Measure.ImportY,
                                        Extent.Width() - 24.0f, Measure.ImportY);

    const bool ImportOver = Hovered(Import);
    const bool ImportPressed = ImportOver && (Sampled.ContactPressed || Sampled.ContactHeld);
    if (Pressed(ChromeCells[5], Import, Applied, "Import this record") ||
        (ImportOver && (Sampled.ContactPressed || Sampled.ContactReleased) && !Interaction->AnyDisclosed()))
    {
        if (Record.Archive == ContentArchive::Arrangement &&
            (std::strcmp(Record.Extension, ".codex") == 0 || std::strcmp(Record.Extension, "codex") == 0))
            Applied.ActivationRequested = Library.Taken;
        else
            Applied.Page = ContentBrowserPage::Import;
    }


    Surface->Ground(Import, ImportPressed ? Colour.EdgeTaken : (ImportOver ? Colour.EmphaticHovered : Colour.Emphatic), Measure.RadiusSoft);
    if (ImportPressed)
        Surface->Edge(Import, Colour.EmphaticRun, 1.6f, Measure.RadiusSoft);

    const float Span = Surface->MeasureRun("Import", Measure.RunBody);

    Surface->Stroke(SymbolSubject::PersistDisc,
                    Spanning(Import.MinimumX + (Import.Width() - Span - 20.0f) * 0.5f,
                             Import.MinimumY + 11.0f, 14.0f, 14.0f),
                    Colour.EmphaticRun);

    Surface->TextRun(Import.MinimumX + (Import.Width() - Span - 20.0f) * 0.5f + 20.0f,
                     Import.MinimumY + 12.0f, Colour.EmphaticRun, "Import",
                     Measure.RunBody, 0.0f, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE IMPORT CAROUSEL PAGE
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordImport(const PlaneExtent& Extent, ContentBrowserConfiguration& Applied)
{
    Surface->Ground(Extent, Colour.Ground);
    const PlaneExtent Top = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 48.0f);
    Surface->Ground(Top, Colour.Aside);
    Surface->Ground(Spanning(Top.MinimumX, Top.MaximumY - 1.0f, Top.Width(), 1.0f), Colour.Stroke);

    const PlaneExtent Library = Spanning(Extent.MinimumX + 16.0f, Extent.MinimumY + 10.0f, 106.0f, 28.0f);
    const PlaneExtent Import = Spanning(Library.MaximumX + 8.0f, Extent.MinimumY + 10.0f, 82.0f, 28.0f);
    if (Pressed(ChromeCells[10], Library, Applied, "Return to Asset Browser"))
        Applied.Page = ContentBrowserPage::Library;
    Surface->Ground(Library, Hovered(Library) ? Colour.Hovered : Colour.Plate, Measure.RadiusSoft);
    Surface->TextRun(Library.MinimumX + 12.0f, Library.MinimumY + 8.0f, Colour.Primary, "Asset Browser", Measure.RunBody);
    Surface->Ground(Import, Colour.Taken, Measure.RadiusSoft);
    Surface->TextRun(Import.MinimumX + 17.0f, Import.MinimumY + 8.0f, Colour.Primary, "Import", Measure.RunBody, 0.0f, true);

    const PlaneExtent Sidebar = Spanning(Extent.MinimumX, Top.MaximumY, Measure.AsideX, Extent.Height() - Top.Height());
    const PlaneExtent Preview = Spanning(Extent.MaximumX - Measure.InspectorX, Top.MaximumY,
                                        Measure.InspectorX, Extent.Height() - Top.Height());
    const PlaneExtent Main = Spanning(Sidebar.MaximumX, Top.MaximumY,
                                     Preview.MinimumX - Sidebar.MaximumX, Extent.Height() - Top.Height());
    Surface->Ground(Sidebar, Colour.Aside);
    Surface->Ground(Spanning(Sidebar.MaximumX - 1.0f, Sidebar.MinimumY, 1.0f, Sidebar.Height()), Colour.Stroke);
    Surface->TextRunCapitalised(Sidebar.MinimumX + 16.0f, Sidebar.MinimumY + 18.0f, Colour.Faint, "Quick Access", Measure.RunCaption, 1.6f, true);
    const char* Places[] = { "Home", "Downloads", "Projects" };
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent Place = Spanning(Sidebar.MinimumX + 8.0f, Sidebar.MinimumY + 40.0f + Index * 32.0f,
                                           Sidebar.Width() - 16.0f, 28.0f);
        if (Pressed(ChromeCells[6u + Index], Place, Applied, Places[Index]))
        {
            const char* Location[] = { "Home", "EngineContent", "." };
            std::snprintf(Applied.ImportLocation, sizeof(Applied.ImportLocation), "%s", Location[Index]);
            Applied.ImportBrowseRequested = true;
        }
        if (Hovered(Place)) Surface->Ground(Place, Colour.Hovered, Measure.RadiusSoft);
        Surface->TextRun(Place.MinimumX + 10.0f, Place.MinimumY + 8.0f, Colour.Secondary, Places[Index], Measure.RunBody);
    }

    Surface->TextRun(Main.MinimumX + 18.0f, Main.MinimumY + 18.0f, Colour.Faint, Applied.ImportLocation, Measure.RunBody);
    const PlaneExtent Directory = Spanning(Main.MinimumX + 12.0f, Main.MinimumY + 42.0f, Main.Width() - 24.0f, Main.Height() - 54.0f);
    Surface->Confine(Directory);
    for (std::uint32_t Index = 0u; Index < Applied.ImportEntryCount && Index < 128u; ++Index)
    {
        const ContentImportEntry& Entry = Applied.ImportEntries[Index];
        const PlaneExtent Card = Spanning(Directory.MinimumX + 4.0f, Directory.MinimumY + Index * 38.0f,
                                          Directory.Width() - 12.0f, 34.0f);
        if (Pressed(LatticeCards[Index], Card, Applied, Entry.Naming))
        {
            Applied.ImportTaken = Index;
            if (Entry.Directory) Applied.ImportBrowseRequested = true;
            else if (Entry.Supported) Applied.ImportConfirmed = true;
        }
        const bool Taken = Applied.ImportTaken == Index;
        if (Taken || Hovered(Card)) Surface->Ground(Card, Taken ? Colour.Taken : Colour.Hovered, Measure.RadiusSoft);
        Surface->TextRun(Card.MinimumX + 12.0f, Card.MinimumY + 9.0f, Colour.Primary, Entry.Naming, Measure.RunBody);
        Surface->TextRun(Card.MaximumX - 78.0f, Card.MinimumY + 9.0f, Colour.Faint,
                         Entry.Directory ? "Folder" : Entry.Extension, Measure.RunCaption);
    }
    Surface->Release();

    // 🧩 Import has its own inspector, matching the library page so selection never loses context.
    Surface->Ground(Preview, Colour.Aside);
    Surface->Ground(Spanning(Preview.MinimumX, Preview.MinimumY, 1.0f, Preview.Height()), Colour.Stroke);
    Surface->TextRunCapitalised(Preview.MinimumX + 14.0f, Preview.MinimumY + 16.0f,
                                Colour.Faint, "Selection", Measure.RunCaption, 1.4f, true);
    const bool Taken = Applied.ImportTaken < Applied.ImportEntryCount;
    if (Taken)
    {
        const ContentImportEntry& Entry = Applied.ImportEntries[Applied.ImportTaken];
        const PlaneExtent Circle = Spanning(Preview.MinimumX + Preview.Width() * 0.5f - 54.0f,
                                            Preview.MinimumY + 54.0f, 108.0f, 108.0f);
        Surface->Medallion(Circle.MinimumX + Circle.Width() * 0.5f, Circle.MinimumY + Circle.Height() * 0.5f,
                           54.0f, Colour.Medallion);
        Surface->TextRunTruncated(Preview.MinimumX + 14.0f, Circle.MaximumY + 20.0f,
                                  Preview.Width() - 28.0f, Colour.Primary, Entry.Naming, Measure.RunBody, true);
        Surface->TextRun(Preview.MinimumX + 14.0f, Circle.MaximumY + 42.0f, Colour.Faint,
                         Entry.Directory ? "Folder" : Entry.Extension, Measure.RunCaption);
    }
    else
        Surface->TextRun(Preview.MinimumX + 14.0f, Preview.MinimumY + 56.0f,
                         Colour.Faint, "Select a source to preview.", Measure.RunBody);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WHOLE BROWSER
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::RecordBrowser(const PlaneExtent& Extent, ContentLibrary& Library,
                                        ContentBrowserConfiguration& Applied)
{
    if (Interaction == nullptr || Surface == nullptr)
        return;

    // ⚠️ Cleared here and nowhere else. The set is per tick, and a host that reads it after RecordBrowser
    //    reads exactly what this tick arbitrated.
    ExclusionCount = 0u;

    if (Applied.Page == ContentBrowserPage::Import)
    {
        RecordImport(Extent, Applied);
        return;
    }

    Surface->Ground(Extent, Colour.Ground);

    // 📐 `h-screen w-full flex` — the sources aside, then the main column, then the inspector aside. Both
    //    asides are `flex-none`, so the lattice takes whatever the two of them leave.
    const PlaneExtent Aside = Spanning(Extent.MinimumX, Extent.MinimumY,
                                       Measure.AsideX, Extent.Height());

    const float MainX = Extent.Width() - Measure.AsideX - Measure.InspectorX;

    const PlaneExtent Rail = Spanning(Aside.MaximumX, Extent.MinimumY,
                                      MainX, Measure.TopRailHeight);

    const PlaneExtent Lattice = Spanning(Aside.MaximumX, Rail.MaximumY,
                                         MainX, Extent.Height() - Measure.TopRailHeight);

    const PlaneExtent Inspector = Spanning(Extent.MaximumX - Measure.InspectorX, Extent.MinimumY,
                                           Measure.InspectorX, Extent.Height());

    RecordSources(Aside, Library, Applied);
    RecordSeekRail(Rail, Applied);
    RecordLattice(Lattice, Library, Applied);
    RecordInspector(Inspector, Library, Applied);
}

void ContentBrowserPanel::RecordDeferred()
{
    SharedControls.RecordDeferred();
}

}   // namespace Slate
