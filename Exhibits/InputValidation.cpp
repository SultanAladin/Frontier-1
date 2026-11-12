/*============================================================================================================================================
                                                             INPUTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Bi-directional InputExchange & InputExtension validation suite with CPU telemetry visual snapshot output

#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace
{

using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::LoadWorkspaceDmSansFont;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Input::ButtonTransitionCategory;
using Frontier::Input::CursorConfinementMode;
using Frontier::Input::HapticFeedbackDirective;
using Frontier::Input::InputExchange;
using Frontier::Input::InputExtension;
using Frontier::Input::InputSignalCategory;
using Frontier::Input::InputSignalPacket;
using Frontier::Windowing::HeadlessPlatformWindow;
using Frontier::Windowing::WindowConfigurationDescriptor;

struct CountingSignalSink
{
    uint32_t ReceivedPackets = 0;
    uint32_t LastKeyCode     = 0;

    void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept
    {
        ++ReceivedPackets;
        if (Packet.Category == InputSignalCategory::KeyboardKey)
        {
            LastKeyCode = Packet.ControlCode;
        }
    }
};

bool VerifyCondition(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[InputValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

bool GenerateInputVisualSnapshot(
    uint64_t DrainedCount,
    float    CursorX,
    float    CursorY,
    float    ScrollSteps,
    uint32_t HapticCount)
{
    ImGuiContext* SnapshotCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(SnapshotCtx);
    ImGuiIO& IoInstance      = ImGui::GetIO();
    IoInstance.IniFilename   = nullptr;
    IoInstance.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IoInstance.DisplaySize   = ImVec2(1280.0f, 680.0f);
    (void)LoadWorkspaceDmSansFont(IoInstance, 13.0f);
    Frontier::Components::ConfigureImGuiWorkspaceStyle(Frontier::Components::ConstructWorkspaceTabMetrics(1.0f));

    ImGui::NewFrame();
    ImDrawList* CanvasBuffer = ImGui::GetForegroundDrawList();
    CanvasBuffer->AddText(
        ImVec2(36.0f, 24.0f),
        IM_COL32(238, 238, 244, 255),
        "InputExchange & InputExtension Bi-Directional Verification  —  Inbound Hardware Ring & Outbound OS Directives");

    // Left panel: Inbound OS -> Engine Signal Ring
    CanvasBuffer->AddRectFilled(ImVec2(36.0f, 68.0f), ImVec2(624.0f, 640.0f), IM_COL32(20, 20, 24, 255), 6.0f);
    CanvasBuffer->AddRect(ImVec2(36.0f, 68.0f), ImVec2(624.0f, 640.0f), IM_COL32(46, 46, 54, 255), 6.0f);
    CanvasBuffer->AddText(ImVec2(56.0f, 86.0f), IM_COL32(34, 197, 94, 255), "OS -> Engine Inbound Signal Intake (InputExchange)");

    char SummaryLine[160]{};
    std::snprintf(
        SummaryLine,
        sizeof(SummaryLine),
        "Drained Signals: %llu   |   Cursor: (%.1f, %.1f) px   |   Scroll: +%.1f",
        static_cast<unsigned long long>(DrainedCount),
        CursorX,
        CursorY,
        ScrollSteps);
    CanvasBuffer->AddText(ImVec2(56.0f, 118.0f), IM_COL32(214, 214, 222, 255), SummaryLine);

    const char* Channels[5] = {
        "KeyboardKey  [Scancode 17 'W' Pressed -> Held -> Released]",
        "PointerMotion [Absolute (420.5, 310.25) + Delta (+14.0, -6.5)]",
        "PointerButton [Button 0 Pressed -> Active]",
        "ScrollWheel   [Vertical +3.0 Detents]",
        "TouchContact  [Slot 0 Down @ (640.0, 360.0) — Android Ready]",
    };
    for (int Idx = 0; Idx < 5; ++Idx)
    {
        const float RowY = 166.0f + static_cast<float>(Idx) * 86.0f;
        CanvasBuffer->AddRectFilled(ImVec2(56.0f, RowY), ImVec2(604.0f, RowY + 68.0f), IM_COL32(26, 28, 34, 255), 5.0f);
        CanvasBuffer->AddRect(ImVec2(56.0f, RowY), ImVec2(604.0f, RowY + 68.0f), IM_COL32(34, 197, 94, 120), 5.0f);
        CanvasBuffer->AddText(ImVec2(74.0f, RowY + 24.0f), IM_COL32(230, 232, 240, 255), Channels[Idx]);
    }

    // Right panel: Outbound Engine -> OS Directives
    CanvasBuffer->AddRectFilled(ImVec2(656.0f, 68.0f), ImVec2(1244.0f, 640.0f), IM_COL32(20, 20, 24, 255), 6.0f);
    CanvasBuffer->AddRect(ImVec2(656.0f, 68.0f), ImVec2(1244.0f, 640.0f), IM_COL32(46, 46, 54, 255), 6.0f);
    CanvasBuffer->AddText(ImVec2(676.0f, 86.0f), IM_COL32(245, 158, 11, 255), "Engine -> OS Outbound Directives (InputFeedbackTarget)");

    CanvasBuffer->AddRectFilled(ImVec2(676.0f, 140.0f), ImVec2(1224.0f, 340.0f), IM_COL32(26, 28, 34, 255), 5.0f);
    CanvasBuffer->AddText(ImVec2(696.0f, 158.0f), IM_COL32(238, 238, 244, 255), "CursorConfinementMode :: LockedRelativeHidden");
    CanvasBuffer->AddText(ImVec2(696.0f, 190.0f), IM_COL32(156, 160, 172, 255), "Applied atomically during PlatformWindow::PollOperatingEvents");
    CanvasBuffer->AddRectFilled(ImVec2(696.0f, 232.0f), ImVec2(1204.0f, 312.0f), IM_COL32(16, 24, 20, 255), 4.0f);
    CanvasBuffer->AddText(ImVec2(716.0f, 262.0f), IM_COL32(34, 197, 94, 255), "VERIFIED: ActiveCursorMode == LockedRelativeHidden");

    CanvasBuffer->AddRectFilled(ImVec2(676.0f, 368.0f), ImVec2(1224.0f, 608.0f), IM_COL32(26, 28, 34, 255), 5.0f);
    char HapticTitle[128]{};
    std::snprintf(HapticTitle, sizeof(HapticTitle), "HapticFeedbackDirective  (Delivered Pulses: %u)", HapticCount);
    CanvasBuffer->AddText(ImVec2(696.0f, 388.0f), IM_COL32(238, 238, 244, 255), HapticTitle);

    // Motor amplitude bars
    CanvasBuffer->AddText(ImVec2(696.0f, 434.0f), IM_COL32(170, 174, 184, 255), "Low-Frequency Motor  (80%)");
    CanvasBuffer->AddRectFilled(ImVec2(696.0f, 460.0f), ImVec2(1204.0f, 482.0f), IM_COL32(14, 14, 18, 255), 4.0f);
    CanvasBuffer->AddRectFilled(ImVec2(696.0f, 460.0f), ImVec2(696.0f + 508.0f * 0.80f, 482.0f), IM_COL32(245, 158, 11, 255), 4.0f);

    CanvasBuffer->AddText(ImVec2(696.0f, 506.0f), IM_COL32(170, 174, 184, 255), "High-Frequency Motor (35%)");
    CanvasBuffer->AddRectFilled(ImVec2(696.0f, 532.0f), ImVec2(1204.0f, 554.0f), IM_COL32(14, 14, 18, 255), 4.0f);
    CanvasBuffer->AddRectFilled(ImVec2(696.0f, 532.0f), ImVec2(696.0f + 508.0f * 0.35f, 554.0f), IM_COL32(59, 130, 246, 255), 4.0f);

    ImGui::Render();
    SoftwareRasterCanvas OutputCanvas(1280, 680, RgbaColor8{ 12, 12, 14, 255 });
    OutputCanvas.RasterizeImGuiBatches(ImGui::GetDrawData(), 0, 0);
    ImGui::DestroyContext(SnapshotCtx);

    const auto OutputPath = ResolveRepositoryRootPath() / "Exhibits/Images/Input/InputExchangeTelemetry.png";
    return VerifyCondition(OutputCanvas.EncodePngToFile(OutputPath), "Write InputExchangeTelemetry.png");
}

} // namespace

int main()
{
    InputExtension         Coordinator{};
    HeadlessPlatformWindow Window{};

    Coordinator.Initialize();
    if (!VerifyCondition(
            Window.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Validation", 1920, 1080)),
            "Window initialization"))
    {
        return 1;
    }

    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 100'000ULL;
        Packet.PrimaryAxis          = 1.0f;
        Packet.ControlCode          = 17;
        Packet.Category             = InputSignalCategory::KeyboardKey;
        Packet.Transition           = ButtonTransitionCategory::Pressed;
        (void)Window.QueueSyntheticSignal(Packet);
    }
    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 110'000ULL;
        Packet.PrimaryAxis          = 420.5f;
        Packet.SecondaryAxis        = 310.25f;
        Packet.DeltaPrimary         = 14.0f;
        Packet.DeltaSecondary       = -6.5f;
        Packet.Category             = InputSignalCategory::PointerMotion;
        (void)Window.QueueSyntheticSignal(Packet);
    }
    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 120'000ULL;
        Packet.PrimaryAxis          = 1.0f;
        Packet.ControlCode          = 0;
        Packet.Category             = InputSignalCategory::PointerButton;
        Packet.Transition           = ButtonTransitionCategory::Pressed;
        (void)Window.QueueSyntheticSignal(Packet);
    }
    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 125'000ULL;
        Packet.SecondaryAxis        = 3.0f;
        Packet.Category             = InputSignalCategory::ScrollWheel;
        (void)Window.QueueSyntheticSignal(Packet);
    }
    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 130'000ULL;
        Packet.PrimaryAxis          = 640.0f;
        Packet.SecondaryAxis        = 360.0f;
        Packet.DeltaPrimary         = 0.85f;
        Packet.DeviceIdentifier     = 1;
        Packet.ControlCode          = 0;
        Packet.Category             = InputSignalCategory::TouchContact;
        Packet.TouchPhase           = Frontier::Input::TouchContactCategory::Began;
        (void)Window.QueueSyntheticSignal(Packet);
    }

    Coordinator.RequestCursorConfinement(CursorConfinementMode::LockedRelativeHidden);
    (void)Coordinator.RequestHapticFeedback(HapticFeedbackDirective{
        .DeviceIdentifier       = 0,
        .LowFrequencyAmplitude  = 0.80f,
        .HighFrequencyAmplitude = 0.35f,
        .DurationMilliseconds   = 16u,
    });

    Window.PollOperatingEvents(Coordinator);

    CountingSignalSink Downstream{};
    const std::size_t  Drained = Coordinator.TickExchangeWithSink(Downstream);

    const auto& Pointer = Coordinator.InspectPointerSnapshot();
    if (!VerifyCondition(Drained == 5, "Drained all 5 hardware packets") ||
        !VerifyCondition(Downstream.ReceivedPackets == 5 && Downstream.LastKeyCode == 17, "Downstream sink received packets") ||
        !VerifyCondition(Coordinator.QueryScanCodeHeld(17), "Key 17 consolidated down") ||
        !VerifyCondition((Pointer.HeldButtonsMask & 1u) != 0u, "Pointer button 0 consolidated down") ||
        !VerifyCondition(std::fabs(Pointer.PositionX - 420.5f) < 1e-4f, "Pointer X position") ||
        !VerifyCondition(std::fabs(Pointer.TickDeltaX - 14.0f) < 1e-4f, "Pointer delta X") ||
        !VerifyCondition(std::fabs(Pointer.ScrollDeltaY - 3.0f) < 1e-4f, "Scroll vertical delta") ||
        !VerifyCondition(Coordinator.ActiveTouchContactCount() == 1u, "Multi-touch contact slot 0 active") ||
        !VerifyCondition(Window.AppliedCursorMode() == CursorConfinementMode::LockedRelativeHidden, "Outbound cursor confinement") ||
        !VerifyCondition(Window.AppliedHapticPulseCount() == 1, "Outbound haptic pulse delivered"))
    {
        return 1;
    }

    const uint64_t TotalDrained = Coordinator.InspectExchange().ExchangedSignalTotal();
    const float    FinalCursorX = Pointer.PositionX;
    const float    FinalCursorY = Pointer.PositionY;
    const float    FinalScroll  = Pointer.ScrollDeltaY;
    const uint32_t FinalHaptics = Window.AppliedHapticPulseCount();

    Coordinator.BeginTick();
    if (!VerifyCondition(std::fabs(Coordinator.InspectPointerSnapshot().TickDeltaX) < 1e-6f, "Per-tick deltas reset cleanly"))
    {
        return 1;
    }

    InputExchange RingStress{};
    RingStress.Initialize();
    for (uint32_t Index = 0; Index < Frontier::Input::DefaultSignalQueueCapacity + 12; ++Index)
    {
        InputSignalPacket OverflowPacket{};
        OverflowPacket.TimestampNanoseconds = Index;
        OverflowPacket.ControlCode          = Index;
        OverflowPacket.Category             = InputSignalCategory::KeyboardKey;
        OverflowPacket.Transition           = ButtonTransitionCategory::Pressed;
        (void)RingStress.IntakeSignal(OverflowPacket);
    }
    if (!VerifyCondition(RingStress.DroppedSignalCount() == 12, "Overflow saturation drops excess without memory growth"))
    {
        return 1;
    }
    RingStress.Finalize();

    Window.Finalize();
    Coordinator.Finalize();

    if (!GenerateInputVisualSnapshot(TotalDrained, FinalCursorX, FinalCursorY, FinalScroll, FinalHaptics))
    {
        return 1;
    }

    std::printf("[InputValidation] All bi-directional InputExchange checks + visual snapshot PNG passed.\n");
    return 0;
}
