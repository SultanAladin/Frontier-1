//============================================================================================================================================
//                                                          VIEWPORTSEQUENCE.H
//============================================================================================================================================
// 🧩 One tick of the interface — springs, drawers, and the assembled recording, shared by every host.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/DrawerSpace/Api/DrawerSpace.h"
#include "SlateUI/Interface/InterfaceExchange/Api/InterfaceExchange.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/RedrawScheduler/Api/RedrawScheduler.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One tick of the interface — the springs, the two drawers, and the assembled recording that a host
///    submits into its own command buffer.
/// note  🔴 The host owns the Vulkan bring-up and the command buffer submission. This component owns the
///       interface context, the spring physics, the drawer arrangement, and the redraw marks. The split
///       is what keeps hosts free of ImGui spelling and keeps this component free of Vulkan submission.
/// note  ⚠️ Panels record their content between the drawer bodies and the seal. The host calls
///       `DrawerPanels` to enter that window, then records panel content through `Surface`, then calls
///       `SealPanels` to close it. The sequence records the drawer chrome itself; the panels record
///       inside.
/// tag   owning
class ViewportSequence
{
public:

    ViewportSequence()                                  = default;
    ViewportSequence(const ViewportSequence&)            = delete;
    ViewportSequence& operator=(const ViewportSequence&) = delete;
    ~ViewportSequence()                                 = default;

    /// 🧩 Constructs the interface context and both drawers over the supplied device handles.
    /// in    Incoming [-]  the device handles and the window the interface reads from
    /// in    North    [-]  what the upper drawer's tongue carries
    /// in    South    [-]  what the lower drawer's tongue carries
    /// out   Result  [-]  refuses with CapabilityAbsent when no interface context is current, and with
    ///                     ExtentExhausted when the integrator declines a drawer spring
    /// post  both drawers stand Closed and settled; nothing moves until a pointer arrives
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructViewportSequence(const InterfaceAttachment& Incoming,
                            const DrawerDeclaration&   North,
                            const DrawerDeclaration&   South);

    /// 🧩 Opens one interface tick, resolves the appearance, and drives the spring physics.
    /// in    ElapsedMilliseconds  [-]  what `TickSequence::Span` measured between this tick and the last
    /// out   Result              [-]  refuses when no context is constructed, or when a tick is already open
    /// post  the drawers have advanced; the surface is ready to record into
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Advance(double ElapsedMilliseconds);

    /// 🧩 Records the drawer chrome — bodies, edges, grips and tongues.
    /// note  Panels must not record before this call. The drawer bodies define the clipping extents
    ///       the panels record inside.
    /// cost  🚩
    /// tag   api, nonthrowing
    void RecordDrawers();

    /// 🧩 Opens the window for panels to record their content inside the drawer interiors.
    /// note  🔴 Called after RecordDrawers and before Seal. Panels call Surface() to draw into.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DrawerPanels();

    /// 🧩 Closes the panel recording window and seals the interface tick.
    /// out   Result  [-]  refuses when no tick is open
    /// post  the assembled content is ready for Record
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> SealPanels();

    /// 🧩 Closes an open tick without assembling it — the escape from any refusal after Advance.
    /// out   Result  [-]  delivers true when no tick was open
    /// note  🔴 A host that returns to the top of its loop after Advance rejected must call this. A tick
    ///       left open refuses every subsequent Advance for the life of the process.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Abandon();

    /// 🧩 Restates the minimum and actual image counts after a presentation chain was re-established.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Renegotiate(std::uint32_t MinimumImageCount, std::uint32_t ImageCount);

    /// 🧩 Records the assembled content into a command recording of the current cycle slot.
    /// in    CommandRecording [-]  a recording already inside a dynamic rendering scope
    /// out   Result          [-]  refuses when nothing has been sealed since the last Advance
    /// pre   SealPanels delivered
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Record(VkCommandBuffer CommandRecording);

    /// 🧩 Records the background and docked windows.
    Deliver<bool> RecordBeneath(VkCommandBuffer CommandRecording);

    /// 🧩 Records the foreground layer (drawers, tool options, popups).
    Deliver<bool> RecordAbove(VkCommandBuffer CommandRecording);

    /// 🧩 The two drawers, for the host to query pose and extent.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    DrawerSpace&             Drawers();
    const DrawerSpace&       Drawers() const;

    /// 🧩 The recording surface, for panels to draw into between DrawerPanels and SealPanels.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RecordingSurface&        Surface();
    const RecordingSurface&  Surface() const;

    /// 🧩 The resolved appearance, for panels to read colours and metrics.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const ThemeProfile& Appearance() const;

    /// 🧩 Declares the theme and accents every later tick resolves the appearance against.
    /// in    Selected  [-]  the artist's choice, as the Control Centre reports it
    /// note  📐 Held rather than applied at once. The appearance is resolved fresh each tick against the
    ///        arrived display scale, so the selection has to outlive the call that declares it; the very
    ///        next tick draws in the new theme.
    /// post  Appearance() reports colours re-anchored onto the chosen theme's ladder
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Retint(const ThemeSelection& Selected);

    /// 🧩 Applies the artist's interface-scale preference to every later appearance resolution.
    /// in    Percentage  [%]  clamped to the same 75–200 range as AppearanceSpecification
    /// out   Altered     [-]  true only when the effective scale changed
    /// note  The display scale remains independent: high-DPI scaling and the artist preference multiply once.
    /// post  Appearance() immediately reports the newly scaled metrics
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ApplyInterfaceScale(std::uint32_t Percentage);

    /// 🧩 Declares the per-role typeface weights every later resolution folds into the appearance.
    /// in    Weights  [-]  the eight `ControlCentreConfiguration::TypographyWeight` figures — Title, Header,
    ///                     Subheader, Body, Label, Caption, Warning, Alert, as `FontWeight` values
    /// note  📐 Applied after every `ResolveTinted` (construction, retint and each tick), so the artist's
    ///        strip choice reaches every panel that reads `Appearance().Fonts` on the tick it was made.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void ApplyTypographyWeights(const std::uint32_t Weights[8]);
    /// Applies both semantic role sizes and weights to every TextRun and matching measurement.
    void ApplyTypographyRoles(const std::uint32_t Sizes[8], const std::uint32_t Weights[8]);

    /// 🧩 The shared motion integrator, for panels whose interaction contributes to viewport wakefulness.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    MotionIntegrator& MotionSource();

    /// 🧩 The interface seam, for a host applying vendor style or recording a vendor tab bar.
    /// note  🔴 Handed out as the SEAM and never as ImGui. `00` §2.2 keeps every ImGui spelling inside
    ///        `SlateUI`, and this returns the component that owns them — a host still names none.
    /// note  📝 Named `Seam` and not for the member it returns: `Interface` is already the member's own
    ///        spelling, and an accessor sharing it cannot be declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    InterfaceExchange& Seam();

    /// 🧩 The redraw marks, for the host to decide whether to present.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    RedrawScheduler&         Marks();
    const RedrawScheduler&   Marks() const;

    /// 🧩 Whether the interface has taken the pointer, so the host must not treat it as a canvas stroke.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool PointerCaptured() const;

    /// 🧩 Whether the interface has taken text entry, so no shortcut consumes the same key.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool KeyboardCaptured() const;

    /// 🧩 Whether either drawer is being dragged or is still travelling under its spring.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Moving() const;

    /// 🧩 ⏱️ Whether this tick must be recorded and presented at all, or the host may block in the
    ///    window system until the artist does something.
    /// out   Waking  [-]  false means present nothing; the image already on screen is still correct
    /// note  🔴 ⏱️ THE EDITOR NEVER STOPPED PRESENTING. Under FIFO pacing the host rebuilt the whole
    ///        interface and presented a fresh image sixty times a second forever, whether or not one
    ///        pixel differed — measured at 8 to 9% of a core with the artist's hands off the input.
    ///        `RedrawScheduler` was written for exactly this, carries the wake rule, is owned by this
    ///        unit and was read by nobody; the marks were raised every tick and never asked about.
    /// note  🔴 The sketch geometry was NOT the cost and is not what this addresses. Projecting and
    ///        tessellating a 240-curve sketch measures 162 microseconds a frame, under 1% of a core;
    ///        the expense is rebuilding and presenting an unchanged interface, not computing shapes.
    ///        Measure before optimising, or the fast thing gets optimised and the idle stays.
    /// in    ArtistStirred [-]  live from the window system: pointer, button, wheel, key or focus
    /// note  ⚠️ Asked BEFORE `Advance`, because a tick that is not waking must not open an ImGui
    ///        frame it will never seal. The operands this unit CAN see — the drawer springs, the
    ///        eases and the panel marks — are read here; the host cannot see those.
    /// note  🔴 ⏱️ INPUT IS PASSED IN, NOT READ FROM THE INTERFACE. This first read the recording
    ///        surface's own pointer record, which `ImGui::NewFrame` fills — and `NewFrame` runs
    ///        inside `Advance`, AFTER this question is asked. So the rule was reading the input of
    ///        the last frame that recorded: once the eases settled the record froze, the rule
    ///        answered "nothing happened" forever, and the host slept through every subsequent
    ///        click and keystroke. It presented a cleared image each tick, so the window went to
    ///        the clear ground and stayed there with the chrome still live. The window system is
    ///        drained every tick whether or not one is recorded, so its view is the only one that
    ///        keeps reporting while asleep.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Waking(bool ArtistStirred) const;

    /// 🧩 Destroys every owned component and forgets the device handles.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

private:

    /// 🧩 Applies the declared role weights to a freshly resolved appearance.
    void RestateTypography(ThemeProfile& Profile);

    InterfaceExchange        Interface         = {};   // [-] - the interface context and ImGui
    MotionIntegrator         Motion            = {};   // [-] - spring physics
    ThemeProfile  Resolved          = {};   // [-] - colours and metrics at the display and artist scales
    ThemeSelection           Chosen            = {};   // [-] - the theme every resolve is anchored onto
    double                   InterfaceScale    = 1.0;  // [-] - artist preference, independent of display DPI
    std::uint32_t RoleSizes[8] = {24u, 20u, 16u, 14u, 12u, 10u, 14u, 14u};
    std::uint32_t RoleWeights[8] = {600u, 600u, 500u, 400u, 500u, 400u, 500u, 600u};   // [-] - the FontProfile defaults
    DrawerSpace              DrawersOwned      = {};   // [-] - the two drawers
    RedrawScheduler          MarksOwned        = {};   // [-] - per-panel redraw marks
    
    // 📝 ⏱️ What the display looked like when the panels were last marked for it. A resize must mark
    //    every panel, but only ONCE per move — marking every tick is a wake rule that never sleeps.
    float           MarkedWidth  = 0.0f;   // [px] - the extent the marks were raised against
    float           MarkedHeight = 0.0f;   // [px]
    double          MarkedScale  = 0.0;    // [-]  - zero until the first tick, so the first move marks
    RecordingSurface         SurfaceOwned      = {};   // [-] - the drawing surface
    DrawerDeclaration        NorthDeclared     = {};   // [-] - remembered until the first tick
    DrawerDeclaration        SouthDeclared     = {};   // [-] - remembered until the first tick
    bool                     DrawersConstructed = false;// [-] - deferred to the first Advance
    bool                     PanelsOpen        = false;// [-] - between DrawerPanels and SealPanels
};

}   // namespace Slate
