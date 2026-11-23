//============================================================================================================================================
//                                                          SESSIONSEQUENCE.H
//============================================================================================================================================
// 🧩 The span from the window opening to the window closing — one bring-up, one tick prologue, one teardown.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/DrawerSpace/Api/DrawerSpace.h"
#include "SlateUI/Interface/TextComponent/Api/FontLoader.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"
#include "SlateUI/Interface/ViewportSequence/Api/ViewportSequence.h"
#include "SlateVulkan/Device/HostLifecycle/Api/HostLifecycle.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A PRODUCT DECLARES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything a product states once, before its window exists.
/// note  📝 The two drawer declarations are here because all three hosts declared the same two — a north
///        Control Centre and a south Content Browser — and a product that wants different ones states them
///        rather than reimplementing the bring-up around them.
/// note  ⚠️ `InvokedAs` is `argv[0]`. It is what locates the appearance file and the shipped content beside
///        the executable, so a product that does not pass it gets the build's own appearance every run.
/// tag   guarantee, nonallocating, nonthrowing
struct SessionDeclaration
{
    const char*        Naming        = "Host";     // [-]  - static text; names this product in every report
    const char*        WindowCaption = "Slate";    // [-]  - static text
    const char*        InvokedAs     = "";         // [-]  - argv[0]; locates the appearance file and content
    std::uint32_t      InitialWidth  = 1280u;      // [px]
    std::uint32_t      InitialHeight =  720u;      // [px]
    LatencyIntent      Pacing        = LatencyIntent::SteadyPacing;   // [-]
    DrawerDeclaration  North         = {};         // [-]  - what the upper drawer's tongue carries
    DrawerDeclaration  South         = {};         // [-]  - what the lower drawer's tongue carries
    float              WorkspaceGround[4] = { 0.06f, 0.06f, 0.08f, 1.0f };   // [-] - the cleared ground
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT ONE TICK CARRIES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What `Await` decided about this tick, and what a product must do about it.
/// note  🔴 `Recording` is the ONLY standing on which a product records. The other three are answered by
///        this component before the product is handed the tick, so a product that records on `Recording`
///        alone cannot leave a recording open — which is the defect three hosts each fixed separately.
/// tag   guarantee
enum class SessionCondition : std::uint32_t
{
    Recording      = 0u,   // [-] - an image is acquired and the interface tick is open; record now
    Idle           = 1u,   // [-] - nothing was acquired; record nothing and ask again
    DeviceRetiring = 2u,   // [-] - retire device resources THIS tick, while the device still stands
    Closed         = 3u,   // [-] - the window closed, or the device was lost beyond recovery
    ConditionCount = 4u    // [-] - the closed count, never a standing
};

/// 🧩 One tick's arrangement, valid only until `Complete` or the next `Await`.
/// note  ⚠️ `Recording` is a vendor handle and is deliberately the only one that crosses. Nothing here
///        hands out the device, the chain or the cyclic slots.
/// note  🔴 `DeviceRebuilt` is raised on the tick a product must reconstruct every device resource it owns
///        — its pipelines, its images, its registered textures. A product that does not read it records
///        into handles the vendor has already returned.
/// tag   guarantee, nonallocating, nonthrowing
struct SessionPass
{
    SessionCondition  Current             = SessionCondition::Idle;   // [-]
    VkCommandBuffer   Recording           = VK_NULL_HANDLE;           // [-]  - open, outside any render scope
    double            ElapsedMilliseconds = 0.0;                      // [ms] - since the previous tick
    std::uint32_t     Width               = 0u;                       // [px] - the drawable extent this tick
    std::uint32_t     Height              = 0u;                       // [px]
    bool              DeviceRebuilt       = false;                    // [-]  - rebuild device resources now
    bool              DisplayReestablished = false;                   // [-]  - the chain's counts were restated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Drives `HostLifecycle` and `ViewportSequence` in the one order every windowed product shares.
/// note  🔴 This component owns no content. It owns the ORDER — bring-up, tick prologue, seal, submit,
///        teardown — and hands the product a tick to record into. `HostLifecycle` owns the five device
///        lifetimes; `ViewportSequence` owns one interface tick; what was copied three times and now lives
///        here is the sequence the two are driven in.
/// note  📝 Measured before it was written: three hosts shared **26 identical seam calls**, and the
///        arrangement below is those calls in the order all three already used. Nothing here is new
///        behaviour — the tick prologue is `EditorHost`'s, which was the only copy that answered every
///        recovery correctly.
/// note  ⚠️ The order this component enforces is not a convenience. Every refusal is resolved BEFORE the
///        display image is acquired, so no path returns with a recording open. the texturing host opened its
///        recording before building the interface tick and had five escape paths that returned with a
///        command buffer still recording; that class of defect is unreachable through this surface.
/// note  🔴 A product must call `Complete` for every `Await` that reported `Recording`, and must not call
///        it otherwise. `Sealed` reports whether the interface content was assembled, which is the one
///        decision a product makes between the two.
/// tag   owning
class SessionSequence
{
public:

    SessionSequence()                                  = default;
    SessionSequence(const SessionSequence&)            = delete;
    SessionSequence& operator=(const SessionSequence&) = delete;
    ~SessionSequence()                                 = default;

    /// 🧩 Opens the window, the device, the chain, the recordings and the interface, in that order.
    /// in    Declared  [-]  what the product states once
    /// out   Result    [-]  refuses at the first stage that declines, naming it, having reclaimed whatever
    ///                      earlier stages had already constructed
    /// note  🔴 The appearance recorded beside the executable is adopted and applied BEFORE any panel is
    ///        constructed. Panels copy their inks out of the appearance handed to them at Construct, so an
    ///        appearance adopted afterwards leaves the first frames in the build's own theme and corrects
    ///        itself only on the artist's first colour change.
    /// note  📝 Font discovery and the preview faces happen here too, before the first tick records. Faces
    ///        added during recording land in an atlas the renderer has already uploaded, and the preview
    ///        tiles then draw from stale texture data.
    /// post  on delivery, `Interface()` and `Device()` both stand and a product may construct its panels
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructSession(const SessionDeclaration& Declared);

    /// 🧩 Opens one tick — drains input, answers every recovery, and reports what the product must do.
    /// out   Pass  [-]  `Recording` when the product must record; every other standing is already answered
    /// note  🔴 The recoveries are answered HERE and not by the product. A display recovery restates the
    ///        chain's image counts into the interface exactly once; a device recovery reconstructs the
    ///        interface against the rebuilt device and consumes the display recovery it also raised.
    ///        Three hosts each wrote this branch and only one of them consumed both.
    /// note  ⚠️ On `DeviceRetiring` the device STILL STANDS. That is the one moment a product can release
    ///        its own device resources against a live handle; releasing them after the rebuild idles a
    ///        device the vendor has already destroyed.
    /// cost  🚩
    /// tag   api, nonthrowing
    SessionPass Await();

    /// 🧩 Seals the interface content, opens the display scope, and prepares the recording pass.
    /// out   Sealed  [-]  true when the interface content was assembled and display opened
    /// cost  🚩
    /// tag   api, nonthrowing
    bool Seal(const SessionPass& Pass);

    /// 🧩 Records the interface on top of the open display scope.
    /// out   Recorded [-] true when the interface commands were recorded into the command buffer
    /// cost  🚩
    /// tag   api, nonthrowing
    bool RecordInterface(const SessionPass& Pass);

    /// 🧩 Closes the rendering scope, submits, presents, and advances the cycle.
    /// out   Continuing  [-]  false when the loop must end
    /// note  🔴 A rejected present re-establishes the chain rather than ending the loop. It is the ordinary
    ///        answer to a resize that arrived between the acquire and the present.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool Complete();

    /// 🧩 Whether the product should keep ticking.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Active() const;

    /// 🧩 Reconciles the artist's Control Centre choices with the appearance, the panels and the font atlas.
    /// in    Chosen    [-]  the selection the Control Centre has written this tick
    /// in    Scaling   [-]  the interface scale percentage the Control Centre carries
    /// out   Restated  [-]  true when the appearance changed and every panel must be reseated
    /// use   A product calls this once per tick with what its Control Centre holds, then reapplies its own
    ///       panels when the delivery is true.
    /// note  🔴 The appearance file is written only when the selection actually differs from what was last
    ///        inscribed. A write per tick would rewrite the whole appearance sixty times a second for as
    ///        long as the Control Centre is open, which is a disk cost no artist asked for.
    /// note  📝 Only a change of FAMILY re-runs the font pipeline. The other members are colours and reach
    ///        every panel through the appearance; reloading faces for them would re-rasterise the whole
    ///        atlas on every colour edit.
    /// note  ⚠️ The product still reapplies its OWN panels. This component knows no panel, so it restates
    ///        the appearance and the workspace style and reports that the product must do the same.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool RestateAppearance(const ThemeSelection& Chosen, std::uint32_t Scaling);

    /// 🧩 The font loader, so a product can seat its Control Centre's family carousel.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    FontLoader& Fonts();

    /// 🧩 The interface tick — the surface, the appearance, the drawers, the motion source and the seam.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ViewportSequence&       Interface();
    const ViewportSequence& Interface() const;

    /// 🧩 The device lifetimes, for a product that constructs its own passes beside them.
    /// note  ⚠️ A product borrowing `DeviceExchange` or `DiagnosticsExtension` must reconstruct whatever it
    ///        built from them on any tick that reports `DeviceRebuilt`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    HostLifecycle&       Device();
    const HostLifecycle& Device() const;

    /// 🧩 Where the shipped content was found — fonts, materials, the codex archives.
    /// note  📝 Resolved once at construction by walking outward from the executable. A product reads this
    ///        rather than resolving its own, which is what stops three hosts from disagreeing about where
    ///        the content root is.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const char* ContentRoot() const;

    /// 🧩 The appearance the artist last inscribed, so a product can seat its Control Centre from it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const ThemeSelection& Inscribed() const;

    /// 🧩 Retires the interface and every lifetime beneath it, in the exact reverse of construction.
    /// out   Serious  [-]  how many retained diagnostic entries `86` §5 marks as a problem; zero is clean
    /// use   A product returns `Reclaim() == 0 ? 0 : 1` from `main`, so a validation run ends in an exit
    ///       code rather than in a console nobody reads.
    /// note  🔴 The diagnostic register is Device lifetime and is read BEFORE anything is reclaimed. A
    ///        reclaimed device has emptied it, so reading it afterwards reports a clean run every time.
    /// note  ⚠️ A product reclaims its OWN device resources before calling this. The interface is retired
    ///        here, and the device beneath it — a product surface left standing waits on a dead device in
    ///        its destructor, which the loader reports as "vkWaitForFences: Invalid device" at shutdown.
    /// cost  🔴
    /// tag   api, nonthrowing
    std::uint32_t Reclaim();

private:

    HostLifecycle     Lifetime      = {};      // [-] - the five device lifetimes
    ViewportSequence  Viewport      = {};      // [-] - one interface tick
    FontLoader        FontFaces     = {};      // [-] - the atlas the surface draws text from

    ThemeSelection    InscribedSelection = {}; // [-] - what was last written to the appearance file

    DrawerDeclaration NorthDeclared = {};      // [-] - retained; a device rebuild reconstructs the drawers
    DrawerDeclaration SouthDeclared = {};      // [-] - retained for the same reason

    const char*       Naming        = "Host";  // [-] - static text, from the declaration
    const char*       InvokedAs     = "";      // [-] - static text, from the declaration; argv[0]
    char              ContentPath[512] = {};   // [-] - the resolved content root
    char              FontPath[512]    = {};   // [-] - ContentPath / FontArchives, resolved once
    float             Ground[4]     = { 0.06f, 0.06f, 0.08f, 1.0f };   // [-] - the cleared ground

    bool              Standing      = false;   // [-] - ConstructSession delivered
    bool              Advanced      = false;   // [-] - the interface tick opened this tick
};

}   // namespace Slate
