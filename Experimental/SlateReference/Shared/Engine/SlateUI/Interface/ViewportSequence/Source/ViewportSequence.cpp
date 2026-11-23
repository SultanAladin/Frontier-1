//============================================================================================================================================
//                                                        VIEWPORTSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The shared tick driver — springs, drawers, and the assembled recording.

#include "SlateUI/Interface/ViewportSequence/Api/ViewportSequence.h"

#include <new>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ViewportSequence::ConstructViewportSequence(const InterfaceAttachment& Incoming,
                                          const DrawerDeclaration&   North,
                                          const DrawerDeclaration&   South)
{
    const Deliver<bool> InterfaceBuilt = Interface.AttachInterface(Incoming);
    if (!InterfaceBuilt.Resolved)
        return InterfaceBuilt;

    NorthDeclared = North;
    SouthDeclared = South;
    Resolved      = ResolveTinted(1.0, InterfaceScale, 0.0f, Chosen);
    RestateTypography(Resolved);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TICK
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ViewportSequence::Advance(double ElapsedMilliseconds)
{
    // ① Open the ImGui frame.
    const Deliver<bool> TickOpened = Interface.Advance();
    if (!TickOpened.Resolved)
        return TickOpened;

    // ② Adopt the surface — reads pointer and display from ImGui IO.
    const Deliver<bool> SurfaceAdopted = SurfaceOwned.Adopt();
    if (!SurfaceAdopted.Resolved)
    {
        // 📝 Retired although the adopt rejected and there is nothing to retire. `Retire` is idempotent, and
        //    a reader following this path should not have to prove it safe before moving on.
        SurfaceOwned.Retire();

        Discard(Interface.Abandon());
        return SurfaceAdopted;
    }

    // ③ Resolve the appearance against the arrived display scale.
    const DisplayCondition& Display = SurfaceOwned.Display();
    Resolved = ResolveTinted(static_cast<double>(Display.DisplayScale), InterfaceScale, 0.0f, Chosen);
    RestateTypography(Resolved);

    // ④ Construct the drawers on the first tick, when the display extent is known.
    if (!DrawersConstructed)
    {
        const Deliver<bool> DrawersBuilt =
            DrawersOwned.ConstructDrawerSpace(Motion, Resolved, NorthDeclared, SouthDeclared, Display);

        if (!DrawersBuilt.Resolved)
        {
            // 🔴 The surface is retired before the internal abandon, for the same reason `SealPanels`
            //    retires it: an adopt that SUCCEEDED against a tick this component is now abandoning must
            //    not leave the surface recordable into content nothing will assemble. The host's own
            //    `Abandon` also retires, but the invariant is this component's to hold rather than the
            //    host's to remember — `00` prevents by declaration, not by discipline.
            SurfaceOwned.Retire();

            Discard(Interface.Abandon());
            return DrawersBuilt;
        }

        DrawersConstructed = true;
    }
    else
    {
        // 📝 🔴 Rearrange now returns immediately unless the extent moved. Calling it every tick — which is
        //    what this line used to do — re-applied every spring onto its pose coordinate and released the live
        //    grab, so a drag was erased one tick after it began and no drawer could ever be dragged.
        // 🔴 ⏱️ A MOVED EXTENT MARKS EVERY PANEL. `Rearrange` returns immediately unless the extent
        //    actually moved, so the mark is raised on the same question rather than every tick — a
        //    mark every tick is a rule that never sleeps, which is the idle cost this exists to remove.
        if (Display.Width        != MarkedWidth
         || Display.Height       != MarkedHeight
         || Display.DisplayScale != MarkedScale)
        {
            MarkedWidth  = Display.Width;
            MarkedHeight = Display.Height;
            MarkedScale  = Display.DisplayScale;
            MarksOwned.MarkEvery(RedrawMark::Rearrange);
        }

        DrawersOwned.Rearrange(Display);
    }

    // ⑤ Drive the drawer springs from the pointer.
    const PointerCondition& Pointer = SurfaceOwned.Pointer();

    // 🔴 The drawers are asked FIRST, and their answer outranks the interface's capture flag. Gating them
    //    on `!PointerCaptured()` disabled them wherever a window sat beneath — and a docked workspace
    //    fills the body, so a drawer raised over one was rejected every contact. The artist could see the
    //    handle and could not press it; the click selected the workspace behind it instead.
    // 📝 `Reservations` is the same arbitration `Contacted` already performed, asked before the gate rather than
    //    after it. `14` §4.2's rule is unchanged — exactly one consumer holds the pointer — but the
    //    ordering that decides which one now puts the drawers above the windows they are drawn above.
    DrawerBearing Covered   = DrawerBearing::North;
    const bool    DrawerHeld = DrawersOwned.Covers(Pointer.PositionX, Pointer.PositionY, Covered);

    // 🔴 Withheld BEFORE the advance as well as after. A resize grip seizes the contact on the very frame
    //    the press lands, and `Reservations` answers for where the pointer is NOW — so once a drag carries the
    //    pointer off the drawer, `DrawerHeld` goes false and the withholding stopped while the vendor's
    //    grip kept the identity it had already taken. Withholding on the seizing frame is what prevents
    //    the grip from ever taking it.
    if (DrawerHeld)
        Interface.WithholdPointer();

    DrawersOwned.Advance(Pointer, Display.Elapsed, DrawerHeld || !Interface.PointerCaptured());

    // 🔴 The interface is told the drawers took it, so the window beneath does not act on the same
    //    contact. Without this both consumers answer one click: the drawer drags and the workspace
    //    selects, which is the defect wearing its other face.
    // 🔴 And again after, because `Moving` only reports a live grab once `Advance` has grabbed it. A drag
    //    that has carried the pointer clear of the drawer is held by `GrabbedBy` alone, and it is that
    //    state — not where the pointer happens to be — that must keep the vendor out for the whole drag.
    if (DrawerHeld || DrawersOwned.Moving())
        Interface.WithholdPointer();

    // ⑥ Advance the motion integrator.
    Motion.Advance(ElapsedMilliseconds > 0.0 ? ElapsedMilliseconds : Display.Elapsed);

    PanelsOpen = false;
    return Deliver<bool>::Result(true);
}

InterfaceExchange& ViewportSequence::Seam()
{
    return Interface;
}

void ViewportSequence::RecordDrawers()
{
    if (!DrawersConstructed)
        return;

    // 🔴 The drawers are laid ABOVE every window. `DockWorkspace.html` overlays the control centre and the
    //    asset browser on the whole shell, and a workspace docked full-width would otherwise bury both —
    //    every ImGui window records between the background and foreground lists.
    // 🔴 RELAYERED, never re-adopted. `Adopt` re-samples the pointer, clears the confine depth and stamps
    //    a fresh tick ordinal — doing that twice a tick merely to raise the drawers sampled the pointer
    //    mid-tick, so a drag reported one travel to the panels and another to the drawers.
    Discard(SurfaceOwned.SwitchLayer(RecordingSurface::ShellLayer::Above));

    DrawersOwned.Record(SurfaceOwned);
}

void ViewportSequence::DrawerPanels()
{
    PanelsOpen = true;
}

Deliver<bool> ViewportSequence::SealPanels()
{
    PanelsOpen = false;

    Discard(SurfaceOwned.SwitchLayer(RecordingSurface::ShellLayer::Beneath));
    SurfaceOwned.Retire();

    return Interface.Seal();
}

Deliver<bool> ViewportSequence::Abandon()
{
    PanelsOpen = false;

    // 📝 An abandoned tick retires its surface for the same reason a sealed one does: nothing may record
    //    into content that will never be assembled.
    SurfaceOwned.Retire();

    return Interface.Abandon();
}

Deliver<bool> ViewportSequence::Renegotiate(std::uint32_t MinimumImageCount, std::uint32_t ImageCount)
{
    return Interface.Renegotiate(MinimumImageCount, ImageCount);
}

Deliver<bool> ViewportSequence::Record(VkCommandBuffer CommandRecording)
{
    return Interface.Record(CommandRecording);
}

Deliver<bool> ViewportSequence::RecordBeneath(VkCommandBuffer CommandRecording)
{
    return Interface.RecordBeneath(CommandRecording);
}

Deliver<bool> ViewportSequence::RecordAbove(VkCommandBuffer CommandRecording)
{
    return Interface.RecordAbove(CommandRecording);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

DrawerSpace& ViewportSequence::Drawers()
{
    return DrawersOwned;
}

const DrawerSpace& ViewportSequence::Drawers() const
{
    return DrawersOwned;
}

RecordingSurface& ViewportSequence::Surface()
{
    return SurfaceOwned;
}

const RecordingSurface& ViewportSequence::Surface() const
{
    return SurfaceOwned;
}

const ThemeProfile& ViewportSequence::Appearance() const
{
    return Resolved;
}

void ViewportSequence::Retint(const ThemeSelection& Selected)
{
    Chosen = Selected;

    // 🔴 Re-resolved here and not left for the next tick. A host reseats its panels from Appearance() on the
    //    line after it retints, and those panels COPY the colours — so an appearance that still held the old
    //    theme would be copied into them and then never copied again, leaving the browser and the stack one
    //    theme behind for as long as the artist does not change colour twice. The retained interface scale
    //    is reapplied unchanged, so a colour edit cannot move a length.
    Resolved = ResolveTinted(Resolved.Measure.DisplayScale, InterfaceScale, 0.0f, Chosen);
    RestateTypography(Resolved);

    // 🔴 ⏱️ MARKED, OR THE NEW THEME IS NEVER DRAWN. The wake rule sleeps on an idle editor, and a
    //    colour change arrives through this call rather than through the pointer — so without a mark
    //    the artist picks a theme and the window keeps showing the old one until they happen to move
    //    the mouse. Every panel: the appearance moved under all of them.
    MarksOwned.MarkEvery(RedrawMark::Recolour);
}

bool ViewportSequence::ApplyInterfaceScale(std::uint32_t Percentage)
{
    if (Percentage < 75u)  Percentage = 75u;
    if (Percentage > 200u) Percentage = 200u;

    const double Requested = static_cast<double>(Percentage) * 0.01;
    const double Apart = Requested - InterfaceScale;
    if (Apart < 0.0001 && Apart > -0.0001)
        return false;

    InterfaceScale = Requested;
    Resolved = ResolveTinted(Resolved.Measure.DisplayScale, InterfaceScale, 0.0f, Chosen);
    RestateTypography(Resolved);
    return true;
}

void ViewportSequence::ApplyTypographyWeights(const std::uint32_t Weights[8])
{
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
        RoleWeights[Index] = Weights[Index];

    // 📝 Re-stated immediately rather than on the next tick. The strips write the weights every frame the
    //    Fonts page is open, and a panel that read the previous tick's figures would lag the choice by one
    //    frame for as long as the artist drags the strip.
    RestateTypography(Resolved);
}

void ViewportSequence::ApplyTypographyRoles(const std::uint32_t Sizes[8],
                                            const std::uint32_t Weights[8])
{
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
    {
        RoleSizes[Index] = Sizes[Index];
        RoleWeights[Index] = Weights[Index];
    }
    RestateTypography(Resolved);
}

void ViewportSequence::RestateTypography(ThemeProfile& Profile)
{
    ApplyFontWeights(Profile, RoleWeights);
    SurfaceOwned.ApplyTypographyRoles(RoleSizes, RoleWeights);
}

MotionIntegrator& ViewportSequence::MotionSource()
{
    return Motion;
}

RedrawScheduler& ViewportSequence::Marks()
{
    return MarksOwned;
}

const RedrawScheduler& ViewportSequence::Marks() const
{
    return MarksOwned;
}

bool ViewportSequence::PointerCaptured() const
{
    return Interface.PointerCaptured();
}

bool ViewportSequence::KeyboardCaptured() const
{
    return Interface.KeyboardCaptured();
}

bool ViewportSequence::Moving() const
{
    return DrawersOwned.Moving() || Motion.Moving();
}

bool ViewportSequence::Waking(bool ArtistStirred) const
{
    // ⚠️ Before the first tick nothing has ever been recorded, so there is no correct image to leave
    //    on screen. Blocking here shows the artist an empty window until they happen to move the
    //    pointer, which reads as a host that failed to open.
    if (!DrawersConstructed)
        return true;

    // 🔴 THE ARRIVAL COMES FROM THE WINDOW SYSTEM, NOT FROM THE INTERFACE. See the header: the
    //    interface's own pointer and text records are filled by `ImGui::NewFrame`, which runs after
    //    this question inside `Advance`. Reading them here asks about the last frame that recorded,
    //    which stops changing the moment the host sleeps — and the host then never wakes.
    return MarksOwned.Waking(Moving(), ArtistStirred);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECLAIM
//------------------------------------------------------------------------------------------------------------------------

void ViewportSequence::Reclaim()
{
    Discard(Interface.Abandon());
    Interface.Reclaim();

    // 📝 🔴 The drawers are reset before the integrator, because their spring ordinals index into it. The
    //    previous reclamation placement-constructed over five live objects without destroying any of them,
    //    which left the drawers holding a pointer into storage that had just been overwritten.
    DrawersOwned.Reset();
    SurfaceOwned.Reset();

    Motion.~MotionIntegrator();
    ::new (&Motion) MotionIntegrator{};

    MarksOwned.~RedrawScheduler();
    ::new (&MarksOwned) RedrawScheduler{};

    Resolved       = ThemeProfile{};
    InterfaceScale = 1.0;

    DrawersConstructed = false;
    PanelsOpen         = false;
}

}   // namespace Slate
