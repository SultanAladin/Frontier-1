//============================================================================================================================================
//                                                         SESSIONSEQUENCE.CPP
//============================================================================================================================================

#include "SlateRuntime/Session/SessionSequence/Api/SessionSequence.h"

#include "SlateUI/Interface/ThemeInterchange/Api/ThemeInterchange.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Slate
{

namespace
{

// 📝 ⏱️ How long an idle tick sleeps in the window system before looking again. Twenty of these a
//    second is imperceptible and costs nothing measurable, while still bounding the damage if the
//    wake rule ever misses a source of change.
constexpr double WakeIntervalSeconds = 0.05;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTENT ROOT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Lifted verbatim from `Application/Api/SharedViewportHostBridge.h`, which three hosts included for this
//    one function. It is placed here rather than left there because `Bridge` is a banned word and the
//    header is deleted at step 11 — and because where the shipped content lives is a session-wide fact,
//    settled once at bring-up, not a thing each host should resolve for itself.
std::filesystem::path ResolveContentRoot(const std::filesystem::path& ExecutablePath)
{
    const auto Standing = [](const std::filesystem::path& Candidate)
    {
        return std::filesystem::exists(Candidate / "WhiteTeaService.codex") ||
               std::filesystem::exists(Candidate / "FontArchives");
    };

    const std::filesystem::path Starts[3] =
    {
        std::filesystem::current_path() / "EngineContent",
        ExecutablePath.parent_path() / "EngineContent",
        ExecutablePath.parent_path().parent_path() / "EngineContent"
    };

    for (const std::filesystem::path& Candidate : Starts)
        if (Standing(Candidate))
            return Candidate.lexically_normal();

    std::filesystem::path Walk = std::filesystem::current_path();

    for (std::uint32_t Step = 0u; Step < 8u; ++Step)
    {
        const std::filesystem::path Candidate = Walk / "EngineContent";

        if (Standing(Candidate))
            return Candidate.lexically_normal();

        if (!Walk.has_parent_path() || Walk.parent_path() == Walk)
            break;

        Walk = Walk.parent_path();
    }

    return (std::filesystem::current_path() / "EngineContent").lexically_normal();
}

// 📝 The device handles copied across the layer edge. `DeviceOffering` and `InterfaceAttachment` are the
//    same eight handles declared one layer apart deliberately — a component in SlateVulkan that included
//    the interface's spelling would invert the partition. All four hosts wrote this identical copy.

// 📝 Copies static text into a fixed extent, always terminated. The two paths this holds are resolved once
//    at bring-up and read for the life of the session, so they are owned here rather than pointed at —
//    a `std::filesystem::path` temporary's `c_str()` dangles at the semicolon.
void Inscribe(char* Destination, std::size_t Extent, const std::string& Source)
{
    if (Extent == 0u)
        return;

    const std::size_t Copied = (Source.size() < Extent - 1u) ? Source.size() : Extent - 1u;

    std::memcpy(Destination, Source.c_str(), Copied);
    Destination[Copied] = '\0';
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        BRING-UP
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SessionSequence::ConstructSession(const SessionDeclaration& Declared)
{
    Naming        = (Declared.Naming != nullptr)        ? Declared.Naming        : "Host";
    InvokedAs     = (Declared.InvokedAs != nullptr)     ? Declared.InvokedAs     : "";
    NorthDeclared = Declared.North;
    SouthDeclared = Declared.South;

    for (std::uint32_t Channel = 0u; Channel < 4u; ++Channel)
        Ground[Channel] = Declared.WorkspaceGround[Channel];

    // ① The five lifetimes — window, instance, surface, diagnostic, device, chain, slots, recordings.
    HostDeclaration DeviceDeclared;
    DeviceDeclared.Naming        = Naming;
    DeviceDeclared.WindowCaption = Declared.WindowCaption;
    DeviceDeclared.InitialWidth  = Declared.InitialWidth;
    DeviceDeclared.InitialHeight = Declared.InitialHeight;
    DeviceDeclared.Pacing        = Declared.Pacing;

#ifdef SLATE_DEBUG
    DeviceDeclared.DiagnosticRequested = true;
#endif

    if (!Lifetime.ConstructHost(DeviceDeclared).Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the device lifetimes were rejected" });
    }

    // ② The viewport sequence — springs, drawers, and the assembled recording.
    if (!Viewport.ConstructViewportSequence(Lifetime.Offering(), NorthDeclared, SouthDeclared).Resolved)
    {
        Lifetime.Reclaim();

        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the viewport sequence was rejected" });
    }

    // ③ Where the shipped content is. Resolved once, from argv[0], and read for the life of the session.
    const std::filesystem::path ExecutablePath = (InvokedAs[0] != '\0')
                                               ? std::filesystem::absolute(InvokedAs)
                                               : std::filesystem::current_path();
    const std::filesystem::path ContentRootPath = ResolveContentRoot(ExecutablePath);

    Inscribe(ContentPath, sizeof(ContentPath), ContentRootPath.string());
    Inscribe(FontPath,    sizeof(FontPath),    (ContentRootPath / "FontArchives").string());

    // ④ The appearance recorded beside the executable. A first run has no file yet, which is the ordinary
    //    case and not a fault — the build's own appearance stands, and the first colour the artist changes
    //    writes the file.
    {
        ThemeSelection Recorded;

        if (ThemeInterchange::AdoptBeside(InvokedAs, Recorded).Resolved)
            InscribedSelection = Recorded;
    }

    // 🔴 Applied BEFORE any panel is constructed. Panels copy their inks out of the appearance the viewport
    //    hands them at Construct, so a selection applied afterwards leaves the first frames drawn in the
    //    build's own theme and corrects itself only on the artist's first colour change.
    Viewport.Retint(InscribedSelection);

    // ⑤ The font atlas, before the first tick records. Faces added during recording land in an atlas the
    //    renderer has already uploaded, and the preview tiles then draw from stale texture data.
    Viewport.Surface().ApplyFontLoader(FontFaces);
    Discard(FontFaces.Discover(FontPath));
    Discard(FontFaces.PreparePreviews(1.0f));
    Discard(FontFaces.Load(FontPath, Viewport.Appearance().Fonts, 1.0f));

    // ⑥ The sheet's tab figures applied into the vendor's style, including the four `Patches/` adds. They
    //    default to 0.0f, at which a patched build draws stock rectangular tabs — so this call is what
    //    turns the trapezoid on.
    if (!Viewport.Seam().ApplyWorkspaceStyle(Viewport.Appearance().WorkspaceMeasure,
                                             Viewport.Appearance().Workspace).Resolved)
    {
        std::printf("%s \u2014 the workspace style was not applied\n", Naming);
    }

    Standing = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE TICK
//------------------------------------------------------------------------------------------------------------------------

SessionPass SessionSequence::Await()
{
    SessionPass Opened;

    Advanced = false;

    if (!Standing)
    {
        Opened.Current = SessionCondition::Closed;
        return Opened;
    }

    // ③·ii 🔴 ⏱️ THE WAKE QUESTION IS ASKED BEFORE THE TICK IS OPENED. Under FIFO pacing this host
    //       rebuilt the interface and presented a fresh image sixty times a second whether or not a
    //       pixel differed, measured at 8 to 9% of a core with the artist's hands off the input.
    //
    //       🔴 It must be asked HERE, not after `Lifetime.Await`. That call acquires a display image
    //       and opens a recording; surrendering one unrecorded through `Complete` presents the CLEAR
    //       GROUND, because the scope `Complete` opens carries `LOAD_OP_CLEAR`. Asked afterwards, every
    //       idle tick wiped the window to the clear ink and left it there — the editor flashed once and
    //       went black with its title bar still live.
    //
    //       ⚠️ Waits with a BOUND rather than blocking outright. If the wake rule ever misses a source
    //       of change, a bounded wait costs a late frame; an unbounded one costs a window that never
    //       redraws until the artist moves the pointer, which is reported as a hang.
    //
    //       ⚠️ `Standing` is tested above, so the interface exists. `Waking` reports true until the
    //       drawers are constructed, which covers the first ticks when there is no correct image yet.
    if (!Viewport.Waking(Lifetime.Stirred()))
    {
        Lifetime.Doze(WakeIntervalSeconds);

        Opened.Current = SessionCondition::Idle;
        return Opened;
    }

    const TickPass Pass = Lifetime.Await(Ground);

    Opened.Recording           = Pass.Recording;
    Opened.ElapsedMilliseconds = Pass.ElapsedMilliseconds;
    Opened.Width               = Pass.Width;
    Opened.Height              = Pass.Height;

    // 📝 Pending faces are flushed every tick, whatever the standing. A family the artist chose on a tick
    //    that turned out to be idle would otherwise never reach the atlas.
    Discard(FontFaces.FlushPending());

    if (Pass.Current == TickCondition::Closed)
    {
        Opened.Current = SessionCondition::Closed;
        return Opened;
    }

    // 🔴 Phase one of a device rebuild. The device STILL STANDS here, so this is the one moment the
    //    interface can release its descriptor pool, font atlas and pipelines against a live handle.
    //    Reclaiming after the rebuild idles a device the vendor has already destroyed, which the loader
    //    reports as VUID-vkDeviceWaitIdle-device-parameter.
    if (Pass.DeviceRetiring)
    {
        Viewport.Reclaim();

        Opened.Current = SessionCondition::DeviceRetiring;
        return Opened;
    }

    // 🔴 The DEVICE was rebuilt, so every device handle the interface holds names an object the vendor has
    //    returned — its font atlas, its descriptor sets, its pipelines. Renegotiating the image counts
    //    would restate figures against a device that no longer exists, so the interface is reconstructed
    //    against the handles the rebuilt device offers. Tested before DisplayRecovered because a device
    //    rebuild raises both.
    if (Lifetime.DeviceRecovered())
    {
        // 📝 Not reclaimed here: the retiring tick above already did it, while the device lived.
        if (!Viewport.ConstructViewportSequence(Lifetime.Offering(), NorthDeclared, SouthDeclared).Resolved)
        {
            std::printf("%s \u2014 the interface could not be rebuilt on the recovered device\n", Naming);

            Opened.Current = SessionCondition::Closed;
            return Opened;
        }

        // 🔴 The atlas died with the device. The loader is reattached to the rebuilt surface and the faces
        //    are re-rasterised, or every run of text after a rebuild draws from a returned image.
        Viewport.Surface().ApplyFontLoader(FontFaces);
        Discard(FontFaces.Discover(FontPath));
        Discard(FontFaces.PreparePreviews(1.0f));
        Discard(FontFaces.Load(FontPath, Viewport.Appearance().Fonts, 1.0f));

        Viewport.Retint(InscribedSelection);
        Discard(Viewport.Seam().ApplyWorkspaceStyle(Viewport.Appearance().WorkspaceMeasure,
                                                    Viewport.Appearance().Workspace));

        // 📝 The display recovery this rebuild also raised is consumed here. The reconstruction above
        //    already took the counts the new chain holds, and renegotiating them again would restate what
        //    was just constructed.
        static_cast<void>(Lifetime.DisplayRecovered());

        Opened.DeviceRebuilt        = true;
        Opened.DisplayReestablished = true;
    }

    // ③ The chain was re-established. The interface is told the counts it now holds, exactly once.
    else if (Lifetime.DisplayRecovered())
    {
        const DeviceOffering Offered = Lifetime.Offering();

        // 🔴 Read, not discarded. An interface still holding the previous image counts records against a
        //    chain depth that no longer exists, and the vendor reports that as a descriptor mismatch
        //    several ticks later rather than as the resize that caused it.
        if (!Viewport.Renegotiate(Offered.MinimumDisplayImageCount, Offered.DisplayImageCount))
            std::printf("%s \u2014 the interface rejected the restated image counts\n", Naming);

        Opened.DisplayReestablished = true;
    }

    if (Pass.Current != TickCondition::Recording)
    {
        Opened.Current = SessionCondition::Idle;
        return Opened;
    }

    // ④ Build the interface tick. A refusal here abandons the tick's content and the recording is still
    //    surrendered — an empty rendering scope presents the cleared ground, which is correct and is what
    //    the artist sees for one tick.
    if (!Viewport.Advance(Pass.ElapsedMilliseconds).Resolved)
    {
        Opened.Current = SessionCondition::Idle;
        return Opened;
    }

    Advanced       = true;
    Opened.Current = SessionCondition::Recording;

    return Opened;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SEAL AND SUBMIT
//------------------------------------------------------------------------------------------------------------------------

bool SessionSequence::Seal(const SessionPass& Pass)
{
    static_cast<void>(Pass);

    if (!Advanced)
        return false;

    if (!Viewport.SealPanels().Resolved)
    {
        Discard(Viewport.Abandon());
        return false;
    }

    Discard(Lifetime.BeginDisplay());

    // 🔴 Record the background and docked windows (including the Viewport window ground)
    //    so the 3D viewport scene and overlay passes render on top of the window ground.
    if (!Viewport.RecordBeneath(Pass.Recording))
        std::printf("%s \u2014 the interface background was not recorded\n", Naming);

    return true;
}

bool SessionSequence::RecordInterface(const SessionPass& Pass)
{
    // 🔴 Record the foreground layer (drawers, floating tool options, popups) ON TOP
    //    of the 3D viewport passes, ensuring full opacity without bleed-through.
    if (!Viewport.RecordAbove(Pass.Recording))
        std::printf("%s \u2014 the interface foreground was not recorded\n", Naming);

    return true;
}

bool SessionSequence::Complete()
{
    Advanced = false;

    // ⑤ Close the scope, submit, present, advance. A rejected present re-establishes the chain rather than
    //    ending the loop.
    return Lifetime.Complete().Resolved;
}

bool SessionSequence::Active() const
{
    return Standing && Lifetime.Active();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

bool SessionSequence::RestateAppearance(const ThemeSelection& Chosen, const std::uint32_t Scaling)
{
    bool Restated = false;

    // 📝 The artist's per-role weights are declared every tick so the workspace's panels read the current
    //    choice; the viewport re-states them after each resolve.
    if (Viewport.ApplyInterfaceScale(Scaling))
    {
        Discard(Viewport.Seam().ApplyWorkspaceStyle(Viewport.Appearance().WorkspaceMeasure,
                                                    Viewport.Appearance().Workspace));
        Restated = true;
    }

    // 📝 Compared rather than watched. The Control Centre writes the artist's choice straight into its
    //    configuration, so the change is visible here as a difference and needs no callback to report it.
    // 🔴 Only the FAMILY re-runs the font pipeline. The other members are colours and reach every panel
    //    through the appearance; reloading faces for them would re-rasterise the whole atlas on every
    //    theme or colour edit.
    const bool FamilyAltered = std::strcmp(Chosen.FontFamily, InscribedSelection.FontFamily) != 0;
    const bool Altered       = Chosen.Current     != InscribedSelection.Current
                            || Chosen.Primary     != InscribedSelection.Primary
                            || Chosen.Secondary   != InscribedSelection.Secondary
                            || Chosen.Information != InscribedSelection.Information
                            || Chosen.Warning     != InscribedSelection.Warning
                            || Chosen.Alert       != InscribedSelection.Alert
                            || FamilyAltered;

    if (!Altered)
        return Restated;

    // 🔴 The record is advanced whether the write was delivered or rejected. A read-only folder would
    //    otherwise have every later tick retry the same rejected write for the life of the process.
    Discard(ThemeInterchange::RecordBeside(InvokedAs, Chosen));
    InscribedSelection = Chosen;

    // 🔴 Declared to the viewport, which re-anchors the whole appearance on the next tick. The product
    //    reapplies its own panels on the strength of the delivery below — this component knows none.
    Viewport.Retint(Chosen);
    Discard(Viewport.Seam().ApplyWorkspaceStyle(Viewport.Appearance().WorkspaceMeasure,
                                                Viewport.Appearance().Workspace));

    if (FamilyAltered)
        FontFaces.RequestLoad(FontPath, Viewport.Appearance().Fonts, 1.0f);

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ACCESS
//------------------------------------------------------------------------------------------------------------------------

FontLoader& SessionSequence::Fonts()
{
    return FontFaces;
}

ViewportSequence& SessionSequence::Interface()
{
    return Viewport;
}

const ViewportSequence& SessionSequence::Interface() const
{
    return Viewport;
}

HostLifecycle& SessionSequence::Device()
{
    return Lifetime;
}

const HostLifecycle& SessionSequence::Device() const
{
    return Lifetime;
}

const char* SessionSequence::ContentRoot() const
{
    return ContentPath;
}

const ThemeSelection& SessionSequence::Inscribed() const
{
    return InscribedSelection;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t SessionSequence::Reclaim()
{
    if (!Standing)
        return 0u;

    // 🔴 Read before Reclaim. The register is Device lifetime, and a reclaimed device has emptied it.
    const std::uint32_t Serious = Lifetime.StateDiagnostics();

    // 📝 The viewport is retired before the lifetimes it was constructed over. HostLifecycle idles the
    //    device inside Reclaim, so nothing here needs to.
    Viewport.Reclaim();
    Lifetime.Reclaim();

    Standing = false;

    std::printf("%s \u2014 exited cleanly\n", Naming);

    return Serious;
}

}   // namespace Slate
