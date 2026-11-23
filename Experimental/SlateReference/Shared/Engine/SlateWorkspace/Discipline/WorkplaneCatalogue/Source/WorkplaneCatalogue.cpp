//============================================================================================================================================
//                                                      WORKPLANECATALOGUE.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"

namespace Slate
{

namespace
{

/// 📝 The names the three standing planes carry. They are what the artist reads in the directory, so they
///    are spelled the way the artist thinks of them rather than after the axes they happen to lie on.
const char* StandingNaming(StandingWorkplane Subject)
{
    switch (Subject)
    {
        case StandingWorkplane::Ground: return "Ground Plane";
        case StandingWorkplane::Front:  return "Front Plane";
        case StandingWorkplane::Side:   return "Side Plane";
        default:                        return "Plane";
    }
}

}   // namespace

WorkplaneCatalogue::WorkplaneCatalogue()
{
    // 🔴 The three standing planes exist before the artist does anything, and Ground is active, so the
    //    promise that a sketch draws without ceremony is kept by construction rather than by a fallback
    //    somewhere downstream.
    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(StandingWorkplane::SubjectCount); ++Index)
    {
        const StandingWorkplane Subject = static_cast<StandingWorkplane>(Index);

        CataloguedWorkplane Entry;
        Entry.Named.IssuedIndex = ++IssuedCount;
        Entry.Surface           = ResolveStandingWorkplane(Subject);
        Entry.Naming            = StandingNaming(Subject);
        HeldWorkplanes.push_back(Entry);
    }

    ActiveWorkplane = HeldWorkplanes.front().Named;
}

WorkplaneName WorkplaneCatalogue::Declare(const Workplane& Surface, const std::string& Naming, bool Activate)
{
    // ⚠️ A plane that cannot describe a surface must never enter the catalogue, because everything
    //    downstream assumes an entry can be projected onto. Refusing here means `Active()` can be a plain
    //    reference rather than something every caller has to test.
    if (!Surface.Declared())
        return {};

    CataloguedWorkplane Entry;
    Entry.Named.IssuedIndex = ++IssuedCount;
    Entry.Surface           = Surface;
    Entry.Naming            = Naming;
    HeldWorkplanes.push_back(Entry);

    if (Activate)
        ActiveWorkplane = Entry.Named;

    return Entry.Named;
}

Deliver<WorkplaneName> WorkplaneCatalogue::Activate(WorkplaneName Named)
{
    if (!Named.Assigned())
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "no workplane was named" });

    if (Resolve(Named) == nullptr)
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "that workplane is not in this workspace" });

    ActiveWorkplane = Named;
    return Deliver<WorkplaneName>::Result(Named);
}

Deliver<WorkplaneName> WorkplaneCatalogue::Remove(WorkplaneName Named, std::uint32_t CurvesStandingOnIt)
{
    const CataloguedWorkplane* Held = Resolve(Named);
    if (Held == nullptr)
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "that workplane is not in this workspace" });

    // ⚠️ The three standing planes are what the world IS. Removing one would leave a workspace that could
    //    not answer "the front plane", and nothing else in the tree is written to expect that.
    if (!Held->Removable())
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "the standing planes cannot be removed" });

    // 🔴 Geometry outlives the plane it was drawn on unless something stops it. A curve keeps its world
    //    coordinates, so removing its plane does not move it — but it does leave it belonging to nothing,
    //    unmeasurable and unable to be re-drawn on. Refusing is the honest answer; the caller can move the
    //    geometry first.
    if (CurvesStandingOnIt != 0u)
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "geometry still stands on that workplane" });

    for (std::size_t Index = 0u; Index < HeldWorkplanes.size(); ++Index)
    {
        if (HeldWorkplanes[Index].Named != Named)
            continue;

        HeldWorkplanes.erase(HeldWorkplanes.begin() + static_cast<std::ptrdiff_t>(Index));
        break;
    }

    // 📝 Removing the plane being drawn on falls back to Ground rather than leaving nothing active. There
    //    is always a surface to draw on; that is the whole promise.
    if (ActiveWorkplane == Named)
        ActiveWorkplane = HeldWorkplanes.front().Named;

    return Deliver<WorkplaneName>::Result(Named);
}

const Workplane& WorkplaneCatalogue::Active() const
{
    const CataloguedWorkplane* Held = Resolve(ActiveWorkplane);

    // ⚠️ Cannot be null: the constructor seats three planes, `Remove` refuses the last of them and
    //    re-activates Ground, and `Activate` refuses an unknown name. The fallback is here so the
    //    reference is unconditionally safe rather than safe by argument.
    return Held != nullptr ? Held->Surface : HeldWorkplanes.front().Surface;
}

const CataloguedWorkplane* WorkplaneCatalogue::Resolve(WorkplaneName Named) const
{
    if (!Named.Assigned())
        return nullptr;

    for (const CataloguedWorkplane& Held : HeldWorkplanes)
        if (Held.Named == Named)
            return &Held;

    return nullptr;
}

WorkplaneName WorkplaneCatalogue::StandingName(StandingWorkplane Subject) const
{
    const std::uint32_t Index = static_cast<std::uint32_t>(Subject);
    if (Index >= static_cast<std::uint32_t>(StandingWorkplane::SubjectCount) ||
        Index >= static_cast<std::uint32_t>(HeldWorkplanes.size()))
        return {};

    // 📝 The three standing planes are seated first and are never removable, so their positions in the
    //    vector match the enum for the life of the catalogue.
    return HeldWorkplanes[Index].Named;
}

bool ActivateViewedWorkplane(WorkplaneCatalogue& Catalogue,
                             ViewportOrientation Orientation,
                             bool Perspective)
{
    // 🔴 A perspective view looks squarely at nothing. Choosing a plane from one would mean the
    //    active surface changed as the artist merely orbited.
    if (Perspective)
        return false;

    StandingWorkplane Viewed = StandingWorkplane::Ground;
    if (!ResolveViewedWorkplane(Orientation, Viewed))
        return false;

    // ⚠️ A plane the artist placed themselves outranks the view. They asked for that surface; turning
    //    to look along an axis is not a request to abandon it.
    const CataloguedWorkplane* const Standing = Catalogue.Resolve(Catalogue.ActiveName());
    if (Standing != nullptr && Standing->Surface.Source != WorkplaneOrigin::Standing)
        return false;

    const WorkplaneName Wanted = Catalogue.StandingName(Viewed);
    if (Wanted == Catalogue.ActiveName())
        return false;

    return Catalogue.Activate(Wanted).Resolved;
}

std::string ResolveWorkplaneNaming(const WorkplaneCatalogue& Catalogue, WorkplaneOrigin Source)
{
    // 📝 Counted rather than taken from the issued total, so removing a plane and making another does not
    //    leave a gap in what the artist reads.
    std::uint32_t Made = 0u;
    for (const CataloguedWorkplane& Held : Catalogue.Declared())
        if (Held.Surface.Source == Source)
            ++Made;

    const char* Stem = Source == WorkplaneOrigin::Offset ? "Offset Plane " : "Workplane ";
    return std::string(Stem) + std::to_string(Made + 1u);
}

Deliver<WorkplaneName> ResolvePointedWorkplane(const WorkplaneCatalogue& Catalogue,
                                               const SpatialPoint& RayOrigin,
                                               const SpatialDirection& RayDirection,
                                               double Reach)
{
    WorkplaneName Nearest      = {};
    double        NearestAlong = 0.0;

    for (const CataloguedWorkplane& Held : Catalogue.Declared())
    {
        const double Denominator = Dot(RayDirection, Held.Surface.Normal);

        // ⚠️ A plane seen edge-on is not being pointed at, however close the ray passes. Tested against
        //    1e-6 rather than zero for the same reason the viewport intersection is: a ray a hundredth of
        //    a degree off parallel meets the plane millions of units away.
        if (Denominator < 1.0e-6 && Denominator > -1.0e-6)
            continue;

        const SpatialDirection ToPlane = Difference(RayOrigin, Held.Surface.Origin);
        const double           Along   = Dot(ToPlane, Held.Surface.Normal) / Denominator;

        // 📝 Behind the viewer is not in front of them.
        if (Along <= 0.0)
            continue;

        const SpatialPoint Met = Added(RayOrigin, Scaled(RayDirection, Along));

        // 🔴 Bounded, not infinite. Only a hit within `Reach` of the plane's own origin counts as having
        //    been aimed at it; otherwise pointing at empty sky would select whichever plane the ray
        //    eventually crossed.
        if (LengthSquared(Difference(Held.Surface.Origin, Met)) > Reach * Reach)
            continue;

        if (!Nearest.Assigned() || Along < NearestAlong)
        {
            Nearest      = Held.Named;
            NearestAlong = Along;
        }
    }

    if (!Nearest.Assigned())
        return Deliver<WorkplaneName>::Refuse({ RefusalReason::ContentUnsupported,
                                                "no workplane lies under the pointer" });

    return Deliver<WorkplaneName>::Result(Nearest);
}

}   // namespace Slate
