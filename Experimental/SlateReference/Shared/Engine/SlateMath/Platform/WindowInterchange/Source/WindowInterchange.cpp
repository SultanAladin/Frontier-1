//============================================================================================================================================
//                                                          WINDOWINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 Windowing over GLFW, linked dynamically through glfw3dll.lib against glfw3.dll.

#include "SlateMath/Platform/WindowInterchange/Api/WindowInterchange.h"

// 📝 🔴 Linking glfw3.lib — the static library — while glfw3.dll is present produces a build that links and
//    then misbehaves at runtime. The build script links glfw3dll.lib and GLFW_DLL is defined with it.
#include <GLFW/glfw3.h>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                WINDOW SYSTEM LIFETIME
//------------------------------------------------------------------------------------------------------------------------

// 📝 GLFW's own initialisation is process-wide and reference-counted here rather than by the caller, so
//    that a second window does not tear down the first one's window system on close.
namespace
{
    std::uint32_t OpenWindowCount = 0u;   // [-] - windows currently holding the window system open

    bool AcquireWindowSystem()
    {
        if (OpenWindowCount == 0u && glfwInit() != GLFW_TRUE)
            return false;

        ++OpenWindowCount;
        return true;
    }

    void ReleaseWindowSystem()
    {
        if (OpenWindowCount == 0u)
            return;

        --OpenWindowCount;

        if (OpenWindowCount == 0u)
            glfwTerminate();
    }

    // 📝 ⏱️ The wheel is the one input GLFW reports ONLY through a callback — there is no
    //    `glfwGetScroll`. The flag is raised here and consumed by the next Drain.
    // ⚠️ Deliberately NOT the window user pointer: the interface backend is entitled to that, and two
    //    owners of one slot is a defect that shows up as whichever ran second silently winning.
    // 🔴 Installed BEFORE the interface attaches, so the backend chains to this rather than replacing
    //    it. GLFW keeps one callback per window; the backend preserves the one it found.
    constexpr std::uint32_t StirCapacity = 8u;

    struct WheelWatch
    {
        GLFWwindow* Window  = nullptr;
        bool        Stirred = false;
    };

    WheelWatch WheelWatches[StirCapacity] = {};

    WheelWatch* WatchFor(GLFWwindow* Window)
    {
        for (std::uint32_t Index = 0u; Index < StirCapacity; ++Index)
        {
            if (WheelWatches[Index].Window == Window)
                return &WheelWatches[Index];
        }

        return nullptr;
    }

    void WheelTurned(GLFWwindow* Window, double, double)
    {
        if (WheelWatch* const Watch = WatchFor(Window))
            Watch->Stirred = true;
    }

    void WatchWheel(GLFWwindow* Window)
    {
        for (std::uint32_t Index = 0u; Index < StirCapacity; ++Index)
        {
            if (WheelWatches[Index].Window != nullptr)
                continue;

            WheelWatches[Index].Window  = Window;
            WheelWatches[Index].Stirred = false;
            glfwSetScrollCallback(Window, WheelTurned);
            return;
        }
    }

    void ForgetWheel(GLFWwindow* Window)
    {
        if (WheelWatch* const Watch = WatchFor(Window))
            *Watch = WheelWatch{};
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPEN AND CLOSE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> WindowInterchange::Open(DisplayExtent RequestedExtent, const char* WindowTitle)
{
    if (WindowSlot != nullptr)
        return Deliver<bool>::Result(true);

    if (!AcquireWindowSystem())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the window system failed to start" });

    // 📝 No client API is created. The drawable is surrendered to `06`, which owns everything device-side.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* OpenedWindow = glfwCreateWindow(static_cast<int>(RequestedExtent.Width),
                                                static_cast<int>(RequestedExtent.Height),
                                                WindowTitle,
                                                nullptr,
                                                nullptr);

    if (OpenedWindow == nullptr)
    {
        ReleaseWindowSystem();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the window system rejected the window" });
    }

    WindowSlot = OpenedWindow;
    WatchWheel(OpenedWindow);
    Drain();
    AdoptExtent();

    return Deliver<bool>::Result(true);
}

WindowInterchange::~WindowInterchange()
{
    if (WindowSlot == nullptr)
        return;

    ForgetWheel(static_cast<GLFWwindow*>(WindowSlot));
    glfwDestroyWindow(static_cast<GLFWwindow*>(WindowSlot));
    WindowSlot = nullptr;
    ReleaseWindowSystem();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DRAIN AND REPORT
//------------------------------------------------------------------------------------------------------------------------

void WindowInterchange::Drain()
{
    if (WindowSlot == nullptr)
        return;

    glfwPollEvents();

    GLFWwindow* OpenedWindow = static_cast<GLFWwindow*>(WindowSlot);

    int DrawWidth  = 0;
    int DrawHeight = 0;
    glfwGetFramebufferSize(OpenedWindow, &DrawWidth, &DrawHeight);

    DrawExtent.Width  = static_cast<std::uint32_t>(DrawWidth  < 0 ? 0 : DrawWidth);
    DrawExtent.Height = static_cast<std::uint32_t>(DrawHeight < 0 ? 0 : DrawHeight);
    ClosurePosed      = glfwWindowShouldClose(OpenedWindow) == GLFW_TRUE;

    // 📝 The previous level is carried before the current one is read, so `KeyDescended` reports the edge
    //    between two Drains rather than the level at one of them.
    // ⚠️ F11 is not among them: the window manager takes it for fullscreen before the process is asked.
    static constexpr int DiagnosticKeyCodes[4] = { GLFW_KEY_F6, GLFW_KEY_F7, GLFW_KEY_F8, GLFW_KEY_F9 };

    for (std::uint32_t KeyIndex = 0u; KeyIndex < 4u; ++KeyIndex)
    {
        KeyWas[KeyIndex]  = KeyDown[KeyIndex];
        KeyDown[KeyIndex] = glfwGetKey(OpenedWindow, DiagnosticKeyCodes[KeyIndex]) == GLFW_PRESS;
    }

    // ⏱️ The wake rule's evidence, sampled straight from the window system so that it keeps reporting
    //    while the host is asleep and recording nothing.
    PointerWasX = PointerX;
    PointerWasY = PointerY;
    glfwGetCursorPos(OpenedWindow, &PointerX, &PointerY);

    // ⚠️ The first sample has no predecessor, so it would read as a jump from the origin and wake a
    //    frame for nothing. Seeded once, then compared honestly.
    if (!PointerFresh)
    {
        PointerWasX  = PointerX;
        PointerWasY  = PointerY;
        PointerFresh = true;
    }

    ContactWas  = ContactDown;
    ContactDown = glfwGetMouseButton(OpenedWindow, GLFW_MOUSE_BUTTON_LEFT)   == GLFW_PRESS
               || glfwGetMouseButton(OpenedWindow, GLFW_MOUSE_BUTTON_RIGHT)  == GLFW_PRESS
               || glfwGetMouseButton(OpenedWindow, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

    // 📝 A span rather than every key: this asks whether the artist is typing at all, and the interface
    //    decides what the keystroke MEANS. Printable keys, the modifiers, the arrows, Enter and the
    //    function row all fall inside the one contiguous GLFW range.
    TypingWas  = TypingDown;
    TypingDown = false;
    for (int KeyCode = GLFW_KEY_SPACE; KeyCode <= GLFW_KEY_LAST; ++KeyCode)
    {
        if (glfwGetKey(OpenedWindow, KeyCode) == GLFW_PRESS)
        {
            TypingDown = true;
            break;
        }
    }

    FocusWas  = FocusHeld;
    FocusHeld = glfwGetWindowAttrib(OpenedWindow, GLFW_FOCUSED) == GLFW_TRUE;

    // 📝 Consumed here: the callback raised it at some point since the previous Drain, and this Drain is
    //    the tick that gets to act on it.
    if (const WheelWatch* const Watch = WatchFor(OpenedWindow))
    {
        WheelStirred = Watch->Stirred;
        WatchFor(OpenedWindow)->Stirred = false;
    }
}

bool WindowInterchange::Stirred() const
{
    if (WindowSlot == nullptr)
        return false;

    // 🔴 Motion, contact, typing, the wheel and focus — level OR edge. A held button with the pointer
    //    still is a drag the artist expects to see answered, so the level counts as much as the edge.
    // ⚠️ CLOSURE AND RESIZE ARE STIRRING TOO, and omitting them is a hang rather than a missed frame.
    //    A host that dozes on this rule and is never told the window closed sleeps through the artist
    //    clicking the X; one never told the extent moved sleeps with a stale chain. Both self-clear —
    //    closure ends the loop, and the adopted extent is restated when the display is re-established.
    return PointerX    != PointerWasX
        || PointerY    != PointerWasY
        || ContactDown || ContactWas
        || TypingDown  || TypingWas
        || WheelStirred
        || FocusHeld   != FocusWas
        || ClosurePosed
        || ExtentAltered();
}

bool WindowInterchange::KeyDescended(DiagnosticKey Declared) const
{
    const std::uint32_t KeyIndex = static_cast<std::uint32_t>(Declared);

    if (KeyIndex >= static_cast<std::uint32_t>(DiagnosticKey::KeyCount))
        return false;

    return KeyDown[KeyIndex] && !KeyWas[KeyIndex];
}

void* WindowInterchange::NativeHandle() const
{
    return WindowSlot;
}

DisplayExtent WindowInterchange::CurrentExtent() const
{
    return DrawExtent;
}

bool WindowInterchange::ClosureRequested() const
{
    return ClosurePosed;
}

void WindowInterchange::Await()
{
    if (WindowSlot == nullptr)
        return;

    glfwWaitEvents();
}

void WindowInterchange::AwaitFor(double Seconds)
{
    if (WindowSlot == nullptr)
        return;

    // ⚠️ A non-positive interval would spin, which is the behaviour this exists to remove. An
    //    interval longer than a blink is not a wait, it is a stall the artist can feel, so the
    //    ceiling is a tenth of a second: ten wakes a second cost nothing measurable and no source
    //    of change outside the window system is ever seen later than that.
    constexpr double Shortest = 0.001;
    constexpr double Longest  = 0.1;

    const double Bounded = Seconds < Shortest ? Shortest : (Seconds > Longest ? Longest : Seconds);
    glfwWaitEventsTimeout(Bounded);
}

bool WindowInterchange::ExtentAltered() const
{
    return DrawExtent.Width  != AdoptedExtent.Width
        || DrawExtent.Height != AdoptedExtent.Height;
}

void WindowInterchange::AdoptExtent()
{
    AdoptedExtent = DrawExtent;
}

}   // namespace Slate
