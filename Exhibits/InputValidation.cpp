/*============================================================================================================================================
                                                             INPUTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Dual InputExchange, ImGuiInputAdapter & .InputSpace (FrontierStudio / BlenderNavigation / UnrealNavigation / Custom) validation suite

#include "Engine/Codecs/InputCodec.h"
#include "Engine/Input/ImGuiInputAdapter.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using Frontier::Codecs::InputCodec;
using Frontier::Codecs::InputPresetOrigin;
using Frontier::Codecs::InputSpaceDocumentModel;
using Frontier::Codecs::SpaceContainerDecoder;
using Frontier::Codecs::SpaceExtensionCategory;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::LoadWorkspaceDmSansFont;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Input::ButtonTransitionCategory;
using Frontier::Input::CursorConfinementMode;
using Frontier::Input::HapticFeedbackDirective;
using Frontier::Input::ImGuiInputAdapter;
using Frontier::Input::InputExchange;
using Frontier::Input::InputExtension;
using Frontier::Input::InputLogicalKey;
using Frontier::Input::InputModifierBits::Control;
using Frontier::Input::InputModifierBits::Shift;
using Frontier::Input::InputRoutingTarget;
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

bool ValidateAndGenerateInputSpacePresets(const std::filesystem::path& RepositoryRoot)
{
    const std::filesystem::path InputDir = RepositoryRoot / "EngineContent/Input";
    std::error_code             Ec{};
    std::filesystem::create_directories(InputDir, Ec);

    SpaceExtensionCategory ResolvedCat{};
    std::string            CanonicalExt{};
    std::string            Diag{};
    if (!VerifyCondition(
            SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".inputspace", ResolvedCat, CanonicalExt, Diag) &&
                ResolvedCat == SpaceExtensionCategory::InputSpace &&
                CanonicalExt == ".InputSpace",
            "Case-insensitive .InputSpace container extension resolution"))
    {
        return false;
    }

    struct PresetExportTarget
    {
        InputPresetOrigin Origin;
        const char*       FileName;
    };
    constexpr PresetExportTarget Targets[3] = {
        { InputPresetOrigin::FrontierStudio,    "FrontierStudio.InputSpace"    },
        { InputPresetOrigin::BlenderNavigation, "BlenderNavigation.InputSpace" },
        { InputPresetOrigin::UnrealNavigation,  "UnrealNavigation.InputSpace"  },
    };

    for (const PresetExportTarget& Target : Targets)
    {
        const InputSpaceDocumentModel SourceModel = InputSpaceDocumentModel::ConstructPreset(Target.Origin);
        const std::filesystem::path   FilePath    = InputDir / Target.FileName;
        if (!VerifyCondition(InputCodec::Encoder::EncodeToFile(FilePath, SourceModel, Diag), Target.FileName))
        {
            std::fprintf(stderr, "[InputValidation] Encode diagnostic: %s\n", Diag.c_str());
            return false;
        }

        InputSpaceDocumentModel DecodedModel{};
        if (!VerifyCondition(InputCodec::Decoder::DecodeFromFile(FilePath, DecodedModel, Diag), "Decode .InputSpace preset") ||
            !VerifyCondition(DecodedModel.PresetOrigin == Target.Origin, "PresetOrigin round-trip match") ||
            !VerifyCondition(DecodedModel.NavigationProfile == SourceModel.NavigationProfile, "NavigationProfile round-trip match") ||
            !VerifyCondition(DecodedModel.AxisCalibration == SourceModel.AxisCalibration, "AxisCalibration round-trip match") ||
            !VerifyCondition(DecodedModel.ChordEntries == SourceModel.ChordEntries, "ChordEntries + Tier 2 binary verification"))
        {
            std::fprintf(stderr, "[InputValidation] Decode diagnostic: %s\n", Diag.c_str());
            return false;
        }
    }

    // Verify Custom .InputSpace in-memory round-trip
    InputSpaceDocumentModel CustomModel = InputSpaceDocumentModel::ConstructPreset(InputPresetOrigin::Custom);
    CustomModel.ProfileTitle                               = "StudioCustomRig";
    CustomModel.AxisCalibration.PointerSensitivityPercent  = 72.5f;
    CustomModel.AxisCalibration.InvertPitchAxis            = true;
    CustomModel.ChordEntries.push_back({ "ToggleCameraLag", InputLogicalKey::L, 0xFFu, static_cast<uint16_t>(Control | Shift) });

    std::vector<std::byte> EncodedBytes{};
    InputSpaceDocumentModel DecodedCustom{};
    if (!VerifyCondition(InputCodec::Encoder::EncodeToBytes(CustomModel, EncodedBytes, Diag), "Encode Custom .InputSpace") ||
        !VerifyCondition(InputCodec::Decoder::DecodeFromBytes(EncodedBytes, DecodedCustom, Diag), "Decode Custom .InputSpace") ||
        !VerifyCondition(DecodedCustom.PresetOrigin == InputPresetOrigin::Custom &&
                             DecodedCustom.AxisCalibration.InvertPitchAxis &&
                             DecodedCustom.ChordEntries.size() == CustomModel.ChordEntries.size(),
                         "Custom .InputSpace fidelity"))
    {
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
        "Dual InputExchange (Runtime & Editor) + .InputSpace Presets  -  Bi-Directional Hardware & OS Verification");

    // Left panel: Inbound OS -> Engine Signal Ring
    CanvasBuffer->AddRectFilled(ImVec2(36.0f, 68.0f), ImVec2(624.0f, 640.0f), IM_COL32(20, 20, 24, 255), 6.0f);
    CanvasBuffer->AddRect(ImVec2(36.0f, 68.0f), ImVec2(624.0f, 640.0f), IM_COL32(46, 46, 54, 255), 6.0f);
    CanvasBuffer->AddText(ImVec2(56.0f, 86.0f), IM_COL32(34, 197, 94, 255), "OS -> InputExtension -> RuntimeExchange & EditorExchange");

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
        "KeyboardKey  [Scancode 17 'W' -> RuntimeExchange & EditorSnapshot]",
        "PointerMotion [Absolute (420.5, 310.25) + Delta (+14.0, -6.5)]",
        "PointerButton [Button 0 Pressed + Double-Click Edge Detection]",
        "ScrollWheel   [Vertical +3.0 Detents -> One-Way ImGuiInputAdapter]",
        ".InputSpace   [FrontierStudio | BlenderNavigation | UnrealNavigation]",
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

    char HapticTitle[96]{};
    std::snprintf(HapticTitle, sizeof(HapticTitle), "HapticFeedbackDirective Pulses Delivered: %u", HapticCount);
    CanvasBuffer->AddText(ImVec2(696.0f, 388.0f), IM_COL32(238, 238, 244, 255), HapticTitle);

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
    const auto RepositoryRoot = ResolveRepositoryRootPath();
    if (!ValidateAndGenerateInputSpacePresets(RepositoryRoot))
    {
        return 1;
    }

    InputExtension         Coordinator{};
    HeadlessPlatformWindow Window{};

    Coordinator.Initialize();
    Coordinator.ConfigureEditorChannelEnabled(true);
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
        Packet.LogicalKey           = InputLogicalKey::W;
        Packet.ModifierBitmask      = Control;
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
    {
        InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = 135'000ULL;
        Packet.ControlCode          = static_cast<uint32_t>('U');
        Packet.Category             = InputSignalCategory::TextCodepoint;
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

    const auto& Pointer        = Coordinator.InspectPointerSnapshot();
    const auto& EditorSnapshot = Coordinator.InspectEditorSnapshot();
    if (!VerifyCondition(Drained == 6, "Drained all 6 hardware packets") ||
        !VerifyCondition(Downstream.ReceivedPackets == 6 && Downstream.LastKeyCode == 17, "Downstream runtime sink received packets") ||
        !VerifyCondition(Coordinator.InspectEditorExchange().PendingSignalCount() == 6, "EditorExchange queued all 6 packets independently") ||
        !VerifyCondition(Coordinator.QueryScanCodeHeld(17), "Key 17 consolidated down in Runtime") ||
        !VerifyCondition(EditorSnapshot.QueryKeyPressed(InputLogicalKey::W, false) && EditorSnapshot.ControlHeld, "EditorSnapshot key + Ctrl") ||
        !VerifyCondition(EditorSnapshot.QueryTypedUtf8() == "U", "EditorSnapshot UTF-8 codepoint consolidated") ||
        !VerifyCondition((Pointer.HeldButtonsMask & 1u) != 0u, "Pointer button 0 consolidated down") ||
        !VerifyCondition(EditorSnapshot.QueryButtonPressed(0u), "EditorSnapshot button 0 pressed edge") ||
        !VerifyCondition(std::fabs(Pointer.PositionX - 420.5f) < 1e-4f, "Pointer X position") ||
        !VerifyCondition(std::fabs(Pointer.TickDeltaX - 14.0f) < 1e-4f, "Pointer delta X") ||
        !VerifyCondition(std::fabs(Pointer.ScrollDeltaY - 3.0f) < 1e-4f, "Scroll vertical delta") ||
        !VerifyCondition(Coordinator.ActiveTouchContactCount() == 1u, "Multi-touch contact slot 0 active") ||
        !VerifyCondition(Window.AppliedCursorMode() == CursorConfinementMode::LockedRelativeHidden, "Outbound cursor confinement") ||
        !VerifyCondition(Window.AppliedHapticPulseCount() == 1, "Outbound haptic pulse delivered"))
    {
        return 1;
    }

    // Verify one-way ImGuiInputAdapter draining from EditorExchange
    {
        ImGuiContext* AdapterCtx = ImGui::CreateContext();
        ImGui::SetCurrentContext(AdapterCtx);
        ImGuiIO& AdapterIo = ImGui::GetIO();
        AdapterIo.IniFilename = nullptr;
        const std::size_t ForwardedCount = ImGuiInputAdapter::ForwardEditorExchangeToImGui(Coordinator, AdapterIo);
        ImGui::DestroyContext(AdapterCtx);
        if (!VerifyCondition(ForwardedCount == 6 && Coordinator.InspectEditorExchange().PendingSignalCount() == 0,
                             "ImGuiInputAdapter drained EditorExchange cleanly"))
        {
            return 1;
        }
    }

    // Verify InputRoutingTarget::RuntimeExchangeOnly isolates EditorExchange
    Coordinator.BeginTick();
    Coordinator.ConfigureRoutingTarget(InputRoutingTarget::RuntimeExchangeOnly);
    {
        InputSignalPacket ExclusivePacket{};
        ExclusivePacket.TimestampNanoseconds = 200'000ULL;
        ExclusivePacket.ControlCode          = 30;
        ExclusivePacket.LogicalKey           = InputLogicalKey::A;
        ExclusivePacket.Category             = InputSignalCategory::KeyboardKey;
        ExclusivePacket.Transition           = ButtonTransitionCategory::Pressed;
        (void)Coordinator.IntakeHardwareSignal(ExclusivePacket);
    }
    (void)Coordinator.TickExchange();
    if (!VerifyCondition(Coordinator.QueryScanCodeHeld(30) &&
                             !Coordinator.InspectEditorSnapshot().QueryKeyPressed(InputLogicalKey::A, false) &&
                             Coordinator.InspectEditorExchange().PendingSignalCount() == 0,
                         "RuntimeExchangeOnly isolates EditorExchange"))
    {
        return 1;
    }

    const uint64_t TotalDrained = Coordinator.InspectRuntimeExchange().ExchangedSignalTotal();
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

    std::printf("[InputValidation] All dual InputExchange, ImGuiInputAdapter & .InputSpace checks passed.\n");
    return 0;
}
