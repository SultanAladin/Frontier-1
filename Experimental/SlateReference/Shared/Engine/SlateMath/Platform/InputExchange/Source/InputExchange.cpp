//============================================================================================================================================
//                                                            INPUTEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Bounded cyclic arrival ordering over pointer samples, filled from the host's own pointer surface.

#include "SlateMath/Platform/InputExchange/Api/InputExchange.h"

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform`, and this is the
//    second of the two files carrying one — `04` §7 gates that and nothing above may add another.
#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif

    // 📝 🔴 The pointer surface is declared behind this version gate in the platform headers. The engine already
    //    requires a Vulkan-capable host, which is a later system than the one this names, so raising the gate
    //    removes no host — it only stops the declarations from being compiled out into an unresolved external.
    #if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
        #undef  _WIN32_WINNT
        #define _WIN32_WINNT 0x0602
    #endif

    #include <windows.h>
    #include <windowsx.h>

    // 📝 🔴 The handle this exchange is surrendered is `WindowInterchange::NativeHandle`, which is GLFW's own
    //    window record and not the operating system's window. The native accessor below is the one call that
    //    converts between them, and `04` §7 puts it here because this is already a platform file — asking
    //    `WindowInterchange` to surrender an `HWND` instead would put an operating-system spelling in a header
    //    every layer above includes.
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3.h>
    #include <GLFW/glfw3native.h>

    // 📝 The pointer surface lives in user32. Declared here rather than in the build script so that the
    //    dependency travels with the one file that reaches for it, in the archive that carries it.
    #if defined(_MSC_VER)
        #pragma comment(lib, "user32.lib")
    #endif
#endif

namespace Slate
{

#if defined(_WIN32)

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ATTACHED WINDOWS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The window procedure is a C entry point taken by the operating system and carries no argument of ours, so
//    the exchange it reports into is recovered from the window it was given. The extent is fixed and small
//    because `32` §2 brings up one window and `14` §4.2 arbitrates one pointer across it.
namespace
{
    constexpr std::uint32_t AttachmentCapacity = 4u;   // [-] - windows one process may read pointers from

    struct WindowAttachment
    {
        HWND            WindowSlot        = nullptr;   // [-] - the attached window
        InputExchange*  ReportingInto     = nullptr;   // [-] - where its samples are recorded
        WNDPROC         PrecedingReceiver = nullptr;   // [-] - what held the procedure before the attachment
    };

    WindowAttachment  Attached[AttachmentCapacity] = {};   // [-] - fixed extent; never allocated

    WindowAttachment* ResolveAttachment(HWND WindowSlot)
    {
        for (std::uint32_t Index = 0u; Index < AttachmentCapacity; ++Index)
        {
            if (Attached[Index].WindowSlot == WindowSlot)
                return &Attached[Index];
        }

        return nullptr;
    }

    // 📐 Windows reports pen pressure over a fixed integral range and tilt in whole degrees. Dividing by the
    //    declared range rather than by the largest reading observed keeps two devices with different maxima
    //    reporting the same normalised pressure for the same physical force.
    constexpr double ReportedPressureRange = 1024.0;   // [-] - the platform's declared pressure range

    void ProjectPenAxes(const POINTER_PEN_INFO& Reported, PointerSample& Filling)
    {
        // 🔴 `04` §3: the mask is what makes an absent axis distinguishable from a zero-valued one. A tablet
        //    reporting no tilt and a stylus held perfectly upright both arrive with tiltX and tiltY at zero,
        //    and only the mask separates them. Reading the axis without consulting the mask first collapses
        //    the two into the second, and `22` then applies a tilt dynamic to a device that has no tilt.
        if ((Reported.penMask & PEN_MASK_PRESSURE) != 0u)
        {
            Filling.Supplied.PressureReported = true;
            Filling.Pressure                  = static_cast<double>(Reported.pressure) / ReportedPressureRange;
        }

        // 📝 Both angles or neither. `58` §4 reads the tilt as one departure from the perpendicular, taken as
        //    the magnitude of the two together, and a magnitude formed from one reported angle and one absent
        //    one is a departure in a direction the device never reported.
        if ((Reported.penMask & PEN_MASK_TILT_X) != 0u && (Reported.penMask & PEN_MASK_TILT_Y) != 0u)
        {
            Filling.Supplied.TiltReported = true;
            Filling.TiltX             = static_cast<double>(Reported.tiltX);
            Filling.TiltY            = static_cast<double>(Reported.tiltY);
        }

        if ((Reported.penMask & PEN_MASK_ROTATION) != 0u)
        {
            Filling.Supplied.RotationReported = true;
            Filling.Rotation                  = static_cast<double>(Reported.rotation);
        }
    }

    // 📝 One bit per contact the device currently holds down, in the order `14` §4.2 arbitrates them. The pen's
    //    barrel and eraser are contacts of their own rather than modifiers on the tip, because a stroke drawn
    //    with the eraser is a different stroke and not the same stroke with an attribute.
    std::uint32_t ProjectPenContact(const POINTER_PEN_INFO& Reported)
    {
        std::uint32_t Contact = 0u;

        if ((Reported.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0u)
            Contact |= 1u;

        if ((Reported.penFlags & PEN_FLAG_BARREL) != 0u)
            Contact |= 2u;

        if ((Reported.penFlags & PEN_FLAG_ERASER) != 0u)
            Contact |= 4u;

        return Contact;
    }

    std::uint32_t ProjectMouseContact(WPARAM HeldButtons)
    {
        std::uint32_t Contact = 0u;

        if ((HeldButtons & MK_LBUTTON) != 0u)
            Contact |= 1u;

        if ((HeldButtons & MK_RBUTTON) != 0u)
            Contact |= 2u;

        if ((HeldButtons & MK_MBUTTON) != 0u)
            Contact |= 4u;

        return Contact;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE POINTER SURFACE
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 The mouse is deliberately left on the ordinary message path rather than routed through the pointer
//    surface. Routing it would take the window system's own mouse stream away from `14`'s interface, which
//    reads the same device through the accumulated window condition — and it would gain nothing, because a
//    mouse reports none of the three optional axes and arrives here with all three absent either way.
LRESULT CALLBACK ReceivePointerMessage(HWND WindowSlot, UINT Message, WPARAM Incoming, LPARAM Detail)
{
    WindowAttachment* Attachment = ResolveAttachment(WindowSlot);

    if (Attachment == nullptr)
        return DefWindowProcW(WindowSlot, Message, Incoming, Detail);

    InputExchange* Reporting = Attachment->ReportingInto;

    switch (Message)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    {
        const std::uint32_t PointerIndex = GET_POINTERID_WPARAM(Incoming);

        POINTER_INPUT_TYPE ReportedDevice = PT_POINTER;

        if (GetPointerType(PointerIndex, &ReportedDevice) && ReportedDevice == PT_PEN)
        {
            POINTER_PEN_INFO Reported = {};

            if (GetPointerPenInfo(PointerIndex, &Reported))
            {
                PointerSample Filling;

                // 🔴 `04` §3: stamped at arrival and never at consumption. The device's own reading is
                //    projected onto the timeline rather than the timeline being read here, because the
                //    reading names when the device generated the sample and this procedure names when the
                //    process got around to draining the message that carried it. Restamping here would put
                //    the drain rate back into a stroke that arrival stamping exists to keep it out of.
                Filling.Arrival = Reporting->ArrivalStamp(
                    static_cast<std::uint64_t>(Reported.pointerInfo.PerformanceCount));

                // 📝 The raw location rather than the predicted one. The platform's prediction is a display
                //    smoothing decision made without the document in view, and `22` reconstructs the path the
                //    artist drew — from what the device reported, not from where it was expected to go next.
                POINT Located = Reported.pointerInfo.ptPixelLocationRaw;
                ScreenToClient(WindowSlot, &Located);

                Filling.PositionX   = static_cast<double>(Located.x);
                Filling.PositionY   = static_cast<double>(Located.y);
                Filling.ContactMask = ProjectPenContact(Reported);

                ProjectPenAxes(Reported, Filling);

                Reporting->Record(Filling);
            }
        }

        break;
    }

    case WM_MOUSEMOVE:
    {
        PointerSample Filling;

        // 📝 The ordinary mouse path carries no device stamp of its own, so the arrival is taken here — which
        //    is still arrival and not consumption: the sample is recorded as the message is received and the
        //    consumer drains it on a later tick.
        Filling.Arrival     = Reporting->ArrivalStamp();
        Filling.PositionX   = static_cast<double>(GET_X_LPARAM(Detail));
        Filling.PositionY   = static_cast<double>(GET_Y_LPARAM(Detail));
        Filling.ContactMask = ProjectMouseContact(Incoming);

        // 🔴 Every optional axis stays absent. A mouse supplies none of the three, and `AxisPresence` default
        //    constructs to exactly that, so nothing is written here — the absence is the report.
        Reporting->Record(Filling);
        break;
    }

    default:
        break;
    }

    return CallWindowProcW(Attachment->PrecedingReceiver, WindowSlot, Message, Incoming, Detail);
}

#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                     ATTACHMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> InputExchange::Attach(void* NativeWindowSlot, const TickSequence& HostTimeline)
{
    if (AttachedWindowSlot != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "this exchange is already attached" });

    if (NativeWindowSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no window was surrendered to attach to" });

#if defined(_WIN32)

    // 📝 GLFW surrenders its own window record rather than the operating system's, so the native window is
    //    taken from it here. This is the one place the two spellings meet, and it is inside the platform layer.
    HWND WindowSlot = glfwGetWin32Window(static_cast<GLFWwindow*>(NativeWindowSlot));

    if (IsWindow(WindowSlot) == FALSE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the surrendered handle names no window" });

    WindowAttachment* Vacant = nullptr;

    for (std::uint32_t Index = 0u; Index < AttachmentCapacity; ++Index)
    {
        if (Attached[Index].WindowSlot == WindowSlot)
            return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the window is already read from" });

        if (Vacant == nullptr && Attached[Index].WindowSlot == nullptr)
            Vacant = &Attached[Index];
    }

    if (Vacant == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no attachment slot is unoccupied" });

    // 🔴 The attachment is recorded **before** the procedure is exchanged. The exchange itself delivers
    //    messages, and a message incoming between the two would find no attachment and be handed to the
    //    default procedure — which is the window system's input silently going somewhere else for one message
    //    at bring-up, and a defect that reproduces on a loaded machine and nowhere else.
    Vacant->WindowSlot    = WindowSlot;
    Vacant->ReportingInto = this;

    // 📝 Chained rather than replaced. GLFW installed its own procedure when it opened the window and `14`'s
    //    interface reads that stream; taking it away would leave the interface deaf without saying so.
    const LONG_PTR Preceding = SetWindowLongPtrW(WindowSlot,
                                                 GWLP_WNDPROC,
                                                 reinterpret_cast<LONG_PTR>(&ReceivePointerMessage));

    if (Preceding == 0)
    {
        Vacant->WindowSlot    = nullptr;
        Vacant->ReportingInto = nullptr;

        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the window rejected the pointer attachment" });
    }

    Vacant->PrecedingReceiver = reinterpret_cast<WNDPROC>(Preceding);

    // 🔴 The **converted** handle is retained and never the one surrendered. `Detach` resolves its attachment by
    //    this record, and holding GLFW's pointer here would look up a window that was never attached — the
    //    procedure would then be left installed on a window whose owner has gone.
    AttachedWindowSlot = WindowSlot;
    PrecedingReceiver  = reinterpret_cast<void*>(Preceding);
    Timeline           = &HostTimeline;

    return Deliver<bool>::Result(true);

#else

    // 🚧 `04` §8 leaves which operating systems ship first open. The pointer surfaces of the other two are
    //    unbuilt, and this refuses rather than attaching a surface that would report a position and silently
    //    no axes at all — which is indistinguishable from a device that has none.
    static_cast<void>(HostTimeline);

    return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                   "the host's pointer surface is not yet translated" });

#endif
}

void InputExchange::Detach()
{
    if (AttachedWindowSlot == nullptr)
        return;

#if defined(_WIN32)

    HWND              WindowSlot = static_cast<HWND>(AttachedWindowSlot);
    WindowAttachment* Attachment = ResolveAttachment(WindowSlot);

    // 📝 The procedure is returned only while this attachment is still the one installed. A later attachment
    //    chained onto ours holds a pointer to it, and restoring underneath that one would drop the later
    //    attachment's own procedure out of the chain rather than removing ours from it.
    if (IsWindow(WindowSlot) != FALSE)
    {
        const LONG_PTR Installed = GetWindowLongPtrW(WindowSlot, GWLP_WNDPROC);

        if (Installed == reinterpret_cast<LONG_PTR>(&ReceivePointerMessage))
        {
            SetWindowLongPtrW(WindowSlot, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PrecedingReceiver));
        }
    }

    if (Attachment != nullptr)
    {
        Attachment->WindowSlot        = nullptr;
        Attachment->ReportingInto     = nullptr;
        Attachment->PrecedingReceiver = nullptr;
    }

#endif

    AttachedWindowSlot = nullptr;
    PrecedingReceiver  = nullptr;
    Timeline           = nullptr;
}

InputExchange::~InputExchange()
{
    Detach();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARRIVAL STAMPS
//------------------------------------------------------------------------------------------------------------------------

TickPoint InputExchange::ArrivalStamp() const
{
    if (Timeline == nullptr)
        return TickPoint{};

    return Timeline->Advance();
}

TickPoint InputExchange::ArrivalStamp(std::uint64_t HostCount) const
{
    if (Timeline == nullptr)
        return TickPoint{};

    // 📝 A device that reported no counter reading of its own falls back to the timeline. Projecting a zero
    //    reading would stamp every such sample at the process origin, and `22` would read the whole stroke as
    //    having been drawn in an instant at bring-up.
    if (HostCount == 0ull)
        return Timeline->Advance();

    return Timeline->Project(HostCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ARRIVAL
//------------------------------------------------------------------------------------------------------------------------

void InputExchange::Record(const PointerSample& Incoming)
{
    const std::uint32_t WriteIndex = (OldestIndex + OccupiedCount) % ArrivalCapacity;
    ArrivalOrder[WriteIndex]       = Incoming;

    if (OccupiedCount == ArrivalCapacity)
    {
        // 📝 The extent is full, so the write above overwrote the oldest sample. Advancing the oldest
        //    ordinal is what makes that overwrite a discard rather than a corruption of the ordering.
        OldestIndex = (OldestIndex + 1u) % ArrivalCapacity;
    }
    else
    {
        ++OccupiedCount;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DRAIN
//------------------------------------------------------------------------------------------------------------------------

const PointerSample& InputExchange::Sample(std::uint32_t ArrivalIndex) const
{
    return ArrivalOrder[(OldestIndex + ArrivalIndex) % ArrivalCapacity];
}

std::uint32_t InputExchange::HeldCount() const
{
    return OccupiedCount;
}

void InputExchange::Reclaim()
{
    OldestIndex = 0u;
    OccupiedCount = 0u;
}

}   // namespace Slate
