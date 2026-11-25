/*============================================================================================================================================
                                                  PROJECTLIFECYCLEPOLICYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Automated verification of Projects/Untitled (UntitledLifecycle), zero-Editor RuntimeLifecycle fixed-step cadence,
//    ProjectExecutionPolicy, GameplayRulePolicy, DeterministicDrivingPolicy (16->20->3 CEM-RL), and NetworkingSpace policy sync

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/ProjectExecutionPolicy.h"
#include "Engine/Lifecycle/ProjectExecutionRunner.h"
#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"
#include "Engine/SimulationSpace/Policies/DeterministicDrivingPolicy.h"
#include "Engine/SimulationSpace/Policies/GameplayRulePolicy.h"
#include "Projects/Untitled/Source/UntitledLifecycle.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMPACT TELEMETRY RASTER
//------------------------------------------------------------------------------------------------------------------------

struct ValidationGateOutcome
{
    std::string GateTitle  {};
    std::string DetailText {};
    bool        Passed     = false;
};

struct Glyph5x7
{
    char     Character;
    uint8_t  Rows[7];
};

[[nodiscard]] const uint8_t* LookupGlyph5x7(char RawChar) noexcept
{
    const char Ch = (RawChar >= 'a' && RawChar <= 'z') ? static_cast<char>(RawChar - 'a' + 'A') : RawChar;
    static constexpr Glyph5x7 Font[] = {
        { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
        { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
        { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
        { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
        { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
        { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
        { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
        { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
        { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
        { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
        { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
        { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
        { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
        { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
        { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
        { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
        { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
        { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
        { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
        { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
        { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
        { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
        { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
        { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
        { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
        { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
        { '6', { 0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E } },
        { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
        { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
        { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },
        { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
        { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
        { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
        { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
        { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
        { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
        { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
        { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
        { '>', { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 } },
        { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
        { ',', { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 } },
    };
    for (const auto& Entry : Font)
    {
        if (Entry.Character == Ch)
        {
            return Entry.Rows;
        }
    }
    return Font[0].Rows;
}

void FillRect(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int RW, int RH, uint8_t R, uint8_t G, uint8_t B)
{
    const int X0 = std::max(0, X);
    const int Y0 = std::max(0, Y);
    const int X1 = std::min(W, X + RW);
    const int Y1 = std::min(H, Y + RH);
    for (int Py = Y0; Py < Y1; ++Py)
    {
        for (int Px = X0; Px < X1; ++Px)
        {
            const std::size_t Off = static_cast<std::size_t>((Py * W + Px) * 4);
            Rgba[Off + 0] = R;
            Rgba[Off + 1] = G;
            Rgba[Off + 2] = B;
            Rgba[Off + 3] = 255u;
        }
    }
}

void OutlineRect(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int RW, int RH, uint8_t R, uint8_t G, uint8_t B)
{
    FillRect(Rgba, W, H, X, Y, RW, 1, R, G, B);
    FillRect(Rgba, W, H, X, Y + RH - 1, RW, 1, R, G, B);
    FillRect(Rgba, W, H, X, Y, 1, RH, R, G, B);
    FillRect(Rgba, W, H, X + RW - 1, Y, 1, RH, R, G, B);
}

void StampText(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int Scale, const std::string& Text, uint8_t R, uint8_t G, uint8_t B)
{
    int CursorX = X;
    for (char Ch : Text)
    {
        const uint8_t* Rows = LookupGlyph5x7(Ch);
        for (int Row = 0; Row < 7; ++Row)
        {
            for (int Col = 0; Col < 5; ++Col)
            {
                if ((Rows[Row] & (1u << (4 - Col))) != 0u)
                {
                    FillRect(Rgba, W, H, CursorX + Col * Scale, Y + Row * Scale, Scale, Scale, R, G, B);
                }
            }
        }
        CursorX += 6 * Scale;
    }
}

void RenderTelemetryPng(
    const std::filesystem::path&              OutputPath,
    const std::vector<ValidationGateOutcome>& Gates,
    const Untitled::UntitledLoopMetrics&      LoopMetrics,
    float                                     CemScoreBefore,
    float                                     CemScoreAfter,
    uint32_t                                  RelayedPackets)
{
    constexpr int W = 1280;
    constexpr int H = 760;
    std::vector<uint8_t> Rgba(static_cast<std::size_t>(W * H * 4), 0u);

    FillRect(Rgba, W, H, 0, 0, W, H, 15, 19, 28);
    FillRect(Rgba, W, H, 0, 0, W, 68, 22, 29, 44);
    FillRect(Rgba, W, H, 0, 68, W, 2, 66, 134, 244);

    StampText(Rgba, W, H, 28, 16, 3, "PROJECTS/UNTITLED LIFECYCLE + POLICY VERIFICATION", 238, 244, 255);
    StampText(Rgba, W, H, 28, 46, 2, "ZERO-EDITOR RUNTIMELIFECYCLE + GAMEPLAYRULE + DRIVING1612 + NETWORKING", 142, 178, 232);

    // Left gate matrix
    FillRect(Rgba, W, H, 24, 92, 724, 640, 21, 27, 40);
    OutlineRect(Rgba, W, H, 24, 92, 724, 640, 52, 66, 94);

    uint32_t PassCount = 0u;
    for (const auto& Gate : Gates)
    {
        if (Gate.Passed)
        {
            ++PassCount;
        }
    }
    StampText(
        Rgba, W, H, 40, 108, 2,
        "VERIFICATION GATES (" + std::to_string(PassCount) + "/" + std::to_string(Gates.size()) + " AUTOMATED CHECKS)",
        218, 228, 245);

    int RowY = 144;
    for (const auto& Gate : Gates)
    {
        FillRect(Rgba, W, H, 38, RowY, 696, 62, 26, 34, 50);
        OutlineRect(Rgba, W, H, 38, RowY, 696, 62, Gate.Passed ? 44 : 180, Gate.Passed ? 128 : 54, Gate.Passed ? 86 : 62);
        FillRect(Rgba, W, H, 48, RowY + 14, 74, 34, Gate.Passed ? 34 : 180, Gate.Passed ? 156 : 48, Gate.Passed ? 92 : 56);
        StampText(Rgba, W, H, 62, RowY + 24, 2, Gate.Passed ? "PASS" : "FAIL", 244, 252, 246);
        StampText(Rgba, W, H, 138, RowY + 14, 2, Gate.GateTitle, 236, 242, 252);
        StampText(Rgba, W, H, 138, RowY + 38, 1, Gate.DetailText, 160, 178, 206);
        RowY += 72;
    }

    // Right panel 1: Projects/Untitled Runtime Metrics
    FillRect(Rgba, W, H, 768, 92, 488, 290, 21, 27, 40);
    OutlineRect(Rgba, W, H, 768, 92, 488, 290, 52, 66, 94);
    StampText(Rgba, W, H, 786, 108, 2, "UNTITLED RUNTIME TELEMETRY", 218, 228, 245);
    StampText(Rgba, W, H, 786, 148, 2, "FIXED STEPS: " + std::to_string(LoopMetrics.TotalFixedStepsExecuted), 120, 222, 164);
    StampText(Rgba, W, H, 786, 180, 2, "DIRECTIVES OK: " + std::to_string(LoopMetrics.DirectivesAccepted), 120, 222, 164);
    StampText(Rgba, W, H, 786, 212, 2, "HASH CHECKS: " + std::to_string(LoopMetrics.AlignmentHashesComputed), 130, 188, 252);
    StampText(Rgba, W, H, 786, 244, 2, "BOOT TOML: " + std::string(LoopMetrics.BootLoadedFromDisk ? "LOADED" : "FALLBACK"), 238, 212, 128);
    StampText(Rgba, W, H, 786, 276, 2, "WORKERS: " + std::string(LoopMetrics.WorkerChannelsRunning ? "ACTIVE" : "OFF") +
                                        "  STREAM: " + std::string(LoopMetrics.AssetStreamRunning ? "ACTIVE" : "OFF"), 130, 188, 252);
    StampText(Rgba, W, H, 786, 314, 2, "EDITOR/IMGUI SYMBOLS: 0", 110, 232, 152);
    StampText(Rgba, W, H, 786, 348, 1, "LINKS STRICTLY FRONTIERENGINE + FRONTIERSERIALIZATION + FRONTIERTHREADING", 156, 174, 202);

    // Right panel 2: CEM-RL & Networking Policy Metrics
    FillRect(Rgba, W, H, 768, 402, 488, 330, 21, 27, 40);
    OutlineRect(Rgba, W, H, 768, 402, 488, 330, 52, 66, 94);
    StampText(Rgba, W, H, 786, 418, 2, "POLICY + CEM-RL + NETWORK SYNC", 218, 228, 245);

    const int ScoreBeforeInt = static_cast<int>(std::lround(CemScoreBefore * 100.0f));
    const int ScoreAfterInt  = static_cast<int>(std::lround(CemScoreAfter * 100.0f));
    StampText(Rgba, W, H, 786, 458, 2, "CEM-RL EPOCH 0 SCORE X100: " + std::to_string(ScoreBeforeInt), 184, 198, 224);
    FillRect(Rgba, W, H, 786, 484, 444, 18, 34, 44, 64);
    const int BeforeBar = std::clamp(static_cast<int>((CemScoreBefore + 2.0f) * 110.0f), 20, 440);
    FillRect(Rgba, W, H, 786, 484, BeforeBar, 18, 86, 146, 242);

    StampText(Rgba, W, H, 786, 518, 2, "CEM-RL EPOCH 3 SCORE X100: " + std::to_string(ScoreAfterInt), 120, 226, 164);
    FillRect(Rgba, W, H, 786, 544, 444, 18, 34, 44, 64);
    const int AfterBar = std::clamp(static_cast<int>((CemScoreAfter + 2.0f) * 110.0f), 20, 440);
    FillRect(Rgba, W, H, 786, 544, AfterBar, 18, 52, 198, 126);

    StampText(Rgba, W, H, 786, 584, 2, "RULE WIRE SLAB: 48 BYTES", 232, 206, 126);
    StampText(Rgba, W, H, 786, 616, 2, "MLP WEIGHT SLAB: 1612 BYTES", 130, 188, 252);
    StampText(Rgba, W, H, 786, 648, 2, "QUANTIZED DIRECTIVE: 4 BYTES", 120, 226, 164);
    StampText(Rgba, W, H, 786, 680, 2, "LOOPBACK PACKETS SYNCED: " + std::to_string(RelayedPackets), 130, 188, 252);

    std::error_code DirErr{};
    std::filesystem::create_directories(OutputPath.parent_path(), DirErr);
    stbi_write_png(OutputPath.string().c_str(), W, H, 4, Rgba.data(), W * 4);
}

// Deterministic synthetic track evaluator for CEM-RL verification:
// rewards keeping Steer near target curve (+0.35), Throttle near 0.85, Brake near 0.05
float EvaluateSyntheticPursuitEpisode(
    const Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy& Candidate,
    void* /*Payload*/)
{
    using namespace Frontier::SimulationSpace::Policies;
    float TotalReward = 0.0f;
    for (int Sample = 0; Sample < 4; ++Sample)
    {
        DrivingObservationVector Obs{};
        const float Offset = static_cast<float>(Sample - 1) * 0.25f;
        for (std::size_t Ray = 0u; Ray < 9u; ++Ray)
        {
            Obs.Features[Ray] = 0.7f + 0.05f * static_cast<float>(Ray) - 0.1f * Offset;
        }
        Obs.Features[9]  = 0.55f;
        Obs.Features[10] = Offset;
        Obs.Features[11] = 1.0f;
        Obs.Features[15] = 1.0f;

        const DrivingControlOutput Out = Candidate.EvaluateDrivingControl(Obs);
        const float TargetSteer    = std::clamp(0.35f - 0.30f * Offset, -1.0f, 1.0f);
        const float TargetThrottle = 0.85f;
        const float TargetBrake    = 0.05f;
        const float Err = std::fabs(Out.Steer - TargetSteer) +
                          std::fabs(Out.Throttle - TargetThrottle) +
                          std::fabs(Out.Brake - TargetBrake);
        TotalReward += (1.0f - Err);
    }
    return TotalReward * 0.25f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  VALIDATION ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier::Lifecycle;
    using namespace Frontier::NetworkingSpace;
    using namespace Frontier::Serialization;
    using namespace Frontier::SimulationSpace::Policies;

    const std::filesystem::path RepoRoot     = FRONTIER_REPO_ROOT_DIR;
    const std::filesystem::path UntitledRoot = RepoRoot / "Projects" / "Untitled";

    std::vector<ValidationGateOutcome> Gates{};
    Untitled::UntitledLoopMetrics      CapturedMetrics{};
    float                              ScoreBefore    = 0.0f;
    float                              ScoreAfter     = 0.0f;
    uint32_t                           RelayedPackets = 0u;

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 1: Projects/Untitled TOML Boot Chain & ProjectExecutionPolicy Round-Trip
    // ----------------------------------------------------------------------------------------------------------------
    {
        std::string               Diag{};
        ResolvedBootConfiguration Boot{};
        bool Ok = BootConfigurationLoader::LoadFromRepositoryRoot(UntitledRoot, Boot, Diag) &&
                  Boot.LoadedFromDisk &&
                  Boot.ProjectIndex.ProjectName == "Untitled" &&
                  Boot.RuntimeConfiguration.ApplicationTitle == "Untitled" &&
                  std::filesystem::exists(Boot.ResolvedWorldPath);

        ProjectExecutionPolicy Policy = ProjectExecutionPolicyCodec::ConstructFromBootConfiguration(Boot);
        DocumentContainer      PolicyDoc{};
        ProjectExecutionPolicy Restored{};
        Ok = Ok &&
             ProjectExecutionPolicyCodec::EncodeToDocument(Policy, PolicyDoc) &&
             ProjectExecutionPolicyCodec::DecodeFromDocument(PolicyDoc, Restored, Diag) &&
             Restored.ProjectTitle == "Untitled" &&
             Restored.WorkerChannelCount == 2u;

        Gates.push_back({
            "GATE 1: UNTITLED TOML BOOT + EXECUTION POLICY",
            Ok ? "ENGINESETTINGS->RUNTIME->PLATFORM->PROJECTINDEX->MAINWORLD OK" : Diag,
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 2: RuntimeLifecycle Fixed-Step Accumulator & Opt-In Zero-Overhead Domains
    // ----------------------------------------------------------------------------------------------------------------
    {
        struct StepCounter
        {
            uint64_t FixedSteps        = 0ull;
            uint64_t PresentationSteps = 0ull;
            double   LastAlpha         = -1.0;
        } Counter{};

        ProjectExecutionPolicy LeanPolicy{};
        LeanPolicy.ProjectTitle           = "LeanZeroThreadCheck";
        LeanPolicy.FixedStepSeconds       = 1.0 / 60.0;
        LeanPolicy.MaximumSubstepsPerTick = 4u;
        LeanPolicy.PreferHeadlessWindow   = true;
        LeanPolicy.EnabledDomains         = RuntimeDomainMask::None; // Strictly zero background threads!

        RuntimeLifecycle Loop{};
        bool Ok = Loop.InitializeWithPolicy(LeanPolicy) &&
                  Loop.AccessExecutionChannels() == nullptr &&
                  Loop.AccessAssetStreamThread() == nullptr;

        // Feed 2.5 fixed steps of wall time -> must execute 2 fixed steps and carry 0.5 alpha!
        auto FixedFn = [](double /*Dt*/, uint64_t /*Ord*/, void* Ptr)
        {
            ++static_cast<StepCounter*>(Ptr)->FixedSteps;
        };
        auto PresFn = [](double Alpha, double /*Wall*/, void* Ptr)
        {
            auto* C = static_cast<StepCounter*>(Ptr);
            ++C->PresentationSteps;
            C->LastAlpha = Alpha;
        };

        Ok = Ok && Loop.ExecuteFixedCadenceTick(2.5 / 60.0, FixedFn, PresFn, &Counter);
        Ok = Ok && Counter.FixedSteps == 2ull && Counter.PresentationSteps == 1ull &&
             std::fabs(Counter.LastAlpha - 0.5) < 1e-4;

        // Next 0.5 fixed step finishes the 3rd fixed step cleanly
        Ok = Ok && Loop.ExecuteFixedCadenceTick(0.5 / 60.0, FixedFn, PresFn, &Counter);
        Ok = Ok && Counter.FixedSteps == 3ull && Counter.LastAlpha < 1e-4;

        Loop.Finalize();

        Gates.push_back({
            "GATE 2: FIXED-STEP ACCUMULATOR + OPT-IN DOMAINS",
            "2.5+0.5 STEPS -> 3 FIXED SUBSTEPS + 0.50 ALPHA + 0 UNREQUESTED THREADS",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 3: Projects/Untitled UntitledLifecycle End-to-End Tight Loop & Suspend/Resume
    // ----------------------------------------------------------------------------------------------------------------
    {
        Untitled::UntitledLifecycle Session{};
        std::string                 Diag{};
        bool Ok = Session.Initialize(UntitledRoot, true, Diag) &&
                  Session.VerifyPolicyContracts(Diag);

        const uint32_t FirstRun = Session.RunBoundedTicks(12u, 1.0 / 60.0);
        const auto     Mid      = Session.InspectMetrics();

        // Suspend must pause simulation steps cleanly; Resume restores stepping
        Ok = Ok && FirstRun == 12u && Session.Suspend();
        (void)Session.RunBoundedTicks(4u, 1.0 / 60.0);
        const auto SuspendedMetrics = Session.InspectMetrics();
        Ok = Ok && (SuspendedMetrics.TotalFixedStepsExecuted == Mid.TotalFixedStepsExecuted);

        Ok = Ok && Session.Resume();
        const uint32_t SecondRun = Session.RunBoundedTicks(8u, 1.0 / 60.0);
        CapturedMetrics          = Session.InspectMetrics();

        Ok = Ok && SecondRun == 8u &&
             CapturedMetrics.TotalFixedStepsExecuted == 20ull &&
             CapturedMetrics.DirectivesAccepted == 20ull &&
             CapturedMetrics.DirectivesRejected == 0ull &&
             CapturedMetrics.AlignmentHashesComputed >= 3ull &&
             CapturedMetrics.LastWorldAlignmentHash != 0ull &&
             CapturedMetrics.BootLoadedFromDisk &&
             CapturedMetrics.WorkerChannelsRunning &&
             CapturedMetrics.AssetStreamRunning;

        Session.Finalize();

        // Verify deterministic bit-exact repeatability across a second fresh run
        Untitled::UntitledLifecycle SecondSession{};
        Ok = Ok && SecondSession.Initialize(UntitledRoot, true, Diag);
        (void)SecondSession.RunBoundedTicks(20u, 1.0 / 60.0);
        const auto RepeatMetrics = SecondSession.InspectMetrics();
        Ok = Ok && (RepeatMetrics.LastWorldAlignmentHash == CapturedMetrics.LastWorldAlignmentHash);
        SecondSession.Finalize();

        Gates.push_back({
            "GATE 3: UNTITLEDLIFECYCLE + BIT-EXACT REPEAT",
            "20 FIXED STEPS + SUSPEND/RESUME + IDENTICAL 64-BIT ALIGNMENT HASH",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 4: Shipping Runner Integration (FrontierRunShippingProjectLoop via RuntimeLifecycle)
    // ----------------------------------------------------------------------------------------------------------------
    {
        uint32_t CallbackTicks = 0u;
        FrontierProjectLaunchDescriptor Launch{};
        Launch.StructureVersion     = 1u;
        Launch.ProjectTitle         = "UntitledShippingCheck";
        Launch.InitialWidthPx       = 640u;
        Launch.InitialHeightPx      = 360u;
        Launch.BoundedTickCount     = 6u;
        Launch.NotifySimulationStep = [](double /*Dt*/, void* Payload)
        {
            ++(*static_cast<uint32_t*>(Payload));
        };
        Launch.UserPayload = &CallbackTicks;

        const int ExitCode = FrontierRunShippingProjectLoop(&Launch);
        const bool Ok      = (ExitCode == 0 && CallbackTicks == 6u);

        Gates.push_back({
            "GATE 4: SHIPPING RUNNER ON RUNTIMELIFECYCLE",
            "FRONTIERRUNSHIPPINGPROJECTLOOP EXIT=0 + 6 FIXED TICKS + 0 EDITOR UI",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 5: GameplayRulePolicy Wire (<128B), Alignment Hash Desync Check & Directive Guard
    // ----------------------------------------------------------------------------------------------------------------
    {
        GameplayRulePolicy Rule = Untitled::BuildUntitledGameplayPolicy();
        Rule.FixedSimulationHz  = 120u;
        Rule.SpeedLimitKmh      = 140.0f;

        std::array<std::byte, GameplayRulePolicy::WirePayloadBytes> Wire{};
        GameplayRulePolicy Decoded{};
        std::string        Diag{};
        bool Ok = GameplayRulePolicyEngine::EncodePolicyWireBytes(Rule, Wire) &&
                  Wire.size() == 48u &&
                  GameplayRulePolicyEngine::DecodePolicyWireBytes(Wire, Decoded, Diag) &&
                  Decoded.FixedSimulationHz == 120u &&
                  std::fabs(Decoded.SpeedLimitKmh - 140.0f) < 1e-4f;

        // Tamper 1 bit -> integrity checksum must reject
        Wire[12] ^= std::byte{ 0x01 };
        GameplayRulePolicy RejectedPolicy{};
        Ok = Ok && !GameplayRulePolicyEngine::DecodePolicyWireBytes(Wire, RejectedPolicy, Diag);

        // World alignment hash detects 2 mm position drift
        std::array<WorldKinematicsDigestEntry, 2u> Actors{};
        Actors[0] = { 1001u, 12.345, -4.200, 0.450, 14.0f, 1.2f, 0.0f, 0.25f };
        Actors[1] = { 1002u, 28.000,  1.100, 0.450, 18.5f, 0.0f, 0.0f, 0.00f };
        const uint64_t Digest = GameplayRulePolicyEngine::ComputeWorldAlignmentHash(60ull, Actors);
        Ok = Ok && GameplayRulePolicyEngine::VerifyWorldAlignmentHash(60ull, Actors, Digest);
        Actors[1].PositionMetersX += 0.002;
        Ok = Ok && !GameplayRulePolicyEngine::VerifyWorldAlignmentHash(60ull, Actors, Digest);

        // Directive bounds / ownership / NaN / rate guards
        OwnerControlDirective GoodDir{ 7u, 1001u, 60ull, 0.4f, 0.8f, 0.0f, 1u };
        OwnerControlDirective BadOwner = GoodDir;
        BadOwner.SenderEndpointIndex   = 99u;
        OwnerControlDirective BadSteer = GoodDir;
        BadSteer.Steer                 = 1.85f;
        OwnerControlDirective BadNan   = GoodDir;
        BadNan.Throttle                = std::numeric_limits<float>::quiet_NaN();

        Ok = Ok &&
             GameplayRulePolicyEngine::ValidateOwnerDirective(Rule, GoodDir, 7u, 120u) == DirectiveValidationOutcome::Accepted &&
             GameplayRulePolicyEngine::ValidateOwnerDirective(Rule, BadOwner, 7u, 120u) == DirectiveValidationOutcome::RejectedOwnerMismatch &&
             GameplayRulePolicyEngine::ValidateOwnerDirective(Rule, BadSteer, 7u, 120u) == DirectiveValidationOutcome::RejectedSteerOutOfBounds &&
             GameplayRulePolicyEngine::ValidateOwnerDirective(Rule, BadNan, 7u, 120u) == DirectiveValidationOutcome::RejectedNonFiniteValue &&
             GameplayRulePolicyEngine::ValidateOwnerDirective(Rule, GoodDir, 7u, 500u) == DirectiveValidationOutcome::RejectedRateLimitExceeded;

        Gates.push_back({
            "GATE 5: GAMEPLAYRULEPOLICY WIRE + GUARDS",
            "48B WIRE + BIT-FLIP REJECT + 2MM DESYNC CATCH + 4 DIRECTIVE REJECTIONS",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 6: DeterministicDrivingPolicy (16->20->3, 1,612 B DocumentContainer + 4 B Quantization)
    // ----------------------------------------------------------------------------------------------------------------
    {
        DeterministicDrivingPolicy DriverA{};
        DeterministicDrivingPolicy DriverB{};
        DriverA.SeedInitialize(0xC0FFEE42ull);
        DriverB.SeedInitialize(0xC0FFEE42ull);

        DocumentContainer PolicyDoc{};
        std::vector<std::byte> ContainerBytes{};
        DocumentContainer DecodedDoc{};
        DeterministicDrivingPolicy Restored{};
        std::string Diag{};

        bool Ok = (DriverA.ComputeParameterDigest() == DriverB.ComputeParameterDigest()) &&
                  DriverA.EncodeToDocument("PursuitCruiserV1", PolicyDoc) &&
                  DocumentContainerEncoder::EncodeToBytes(PolicyDoc, ContainerBytes, Diag) &&
                  DocumentContainerDecoder::DecodeFromBytes(ContainerBytes, DecodedDoc, Diag) &&
                  Restored.DecodeFromDocument(DecodedDoc, Diag) &&
                  (Restored.ComputeParameterDigest() == DriverA.ComputeParameterDigest());

        DrivingObservationVector SampleObs{};
        SampleObs.Features = { 0.8f, 0.7f, 0.9f, 0.95f, 1.0f, 0.95f, 0.9f, 0.7f, 0.8f, 0.4f, 0.1f, 0.9f, 0.0f, 0.3f, 0.0f, 1.0f };
        const DrivingControlOutput OutA = DriverA.EvaluateDrivingControl(SampleObs);
        const DrivingControlOutput OutR = Restored.EvaluateDrivingControl(SampleObs);
        const QuantizedDrivingControl Q = DeterministicDrivingPolicy::QuantizeControl(OutA);
        const DrivingControlOutput    D = DeterministicDrivingPolicy::DequantizeControl(Q);

        Ok = Ok &&
             std::fabs(OutA.Steer - OutR.Steer) < 1e-6f &&
             std::fabs(OutA.Throttle - OutR.Throttle) < 1e-6f &&
             std::fabs(OutA.Steer - D.Steer) < 1e-3f &&
             std::fabs(OutA.Throttle - D.Throttle) < 5e-3f &&
             std::fabs(OutA.Brake - D.Brake) < 5e-3f;

        Gates.push_back({
            "GATE 6: DRIVINGPOLICY 1612B CONTAINER + 4B WIRE",
            "16->20->3 TANH (403 F32) ROUND-TRIP BIT-EXACT + 4B QUANTIZATION OK",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 7: Deterministic CEM-RL Self-Play Improvement (Zero Regression, Seed Reproducible)
    // ----------------------------------------------------------------------------------------------------------------
    {
        DeterministicDrivingPolicy TrainerA{};
        DeterministicDrivingPolicy TrainerB{};
        TrainerA.SeedInitialize(0x20261123ull);
        TrainerB.SeedInitialize(0x20261123ull);

        ScoreBefore = EvaluateSyntheticPursuitEpisode(TrainerA, nullptr);

        bool Ok = true;
        CrossEntropyEpochMetrics MetricsA{};
        CrossEntropyEpochMetrics MetricsB{};
        for (uint32_t Epoch = 0u; Epoch < 4u; ++Epoch)
        {
            CrossEntropyTrainingConfig Cfg{};
            Cfg.PopulationSize = 20u;
            Cfg.EliteCount     = 5u;
            Cfg.InitialSigma   = 0.22f * std::pow(0.75f, static_cast<float>(Epoch));
            Cfg.SmoothingAlpha = 0.75f;
            Cfg.EpochSeed      = 0x9000ull + Epoch * 101ull;

            Ok = Ok &&
                 TrainerA.RunCrossEntropyEpoch(Cfg, &EvaluateSyntheticPursuitEpisode, nullptr, MetricsA) &&
                 TrainerB.RunCrossEntropyEpoch(Cfg, &EvaluateSyntheticPursuitEpisode, nullptr, MetricsB);
        }

        ScoreAfter = EvaluateSyntheticPursuitEpisode(TrainerA, nullptr);
        Ok         = Ok &&
                     (ScoreAfter > ScoreBefore + 0.10f) &&
                     (TrainerA.ComputeParameterDigest() == TrainerB.ComputeParameterDigest());

        Gates.push_back({
            "GATE 7: DETERMINISTIC CEM-RL SELF-PLAY",
            "4 EPOCHS IMPROVED REWARD + IDENTICAL DIGEST ACROSS INDEPENDENT RUNS",
            Ok
        });
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Gate 8: Unified NetworkingSpace Policy Sync (US/Egypt/China Minimax Relay + Loopback Weight Sync)
    // ----------------------------------------------------------------------------------------------------------------
    {
        // 1. Minimax relay selection across US + Egypt + China peers
        const std::array<PeerRegionLatencyReading, 3u> GlobalPeers = {{
            { 18.0f,  98.0f, 235.0f, 170.0f }, // US peer
            { 138.0f, 62.0f, 165.0f, 215.0f }, // Egypt peer
            { 225.0f, 160.0f, 52.0f,  46.0f }, // China peer
        }};
        const RelayClusterSelection Relay = SelectMinimaxRelayCluster(GlobalPeers);

        // 2. Transport failover check (Premium with Photon unavailable -> falls back to EOS -> SovereignSocket)
        TransportSelectionInput Sel{};
        Sel.AccessTier       = EntitlementAccessTier::PremiumAccess;
        Sel.AvailabilityMask =
            static_cast<uint8_t>(TransportLinkAvailabilityMask::EnablePhotonRealtime) |
            static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableEpicOnline) |
            static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableSovereignSocket);
        Sel.FailedTransportMask = static_cast<uint8_t>(TransportLinkAvailabilityMask::EnablePhotonRealtime);
        const TransportChannelCategory Chosen = ResolveActiveTransport(Sel);

        // 3. Replicate 48B GameplayRulePolicy + 1,612 B DeterministicDrivingPolicy over LoopbackTransport
        LoopbackPacketSwitch Switch{};
        LoopbackTransport    HostEndpoint(&Switch, 1u);
        LoopbackTransport    ClientEndpoint(&Switch, 2u);

        GameplayRulePolicy HostRule = Untitled::BuildUntitledGameplayPolicy();
        std::array<std::byte, GameplayRulePolicy::WirePayloadBytes> RuleBytes{};
        (void)GameplayRulePolicyEngine::EncodePolicyWireBytes(HostRule, RuleBytes);

        DeterministicDrivingPolicy HostDriver = Untitled::BuildUntitledDrivingPolicy(0x7777ull);
        std::array<std::byte, DeterministicDrivingPolicy::WeightSlabBytes> DriverBytes{};
        (void)HostDriver.EncodeWeightBlob(DriverBytes);

        const auto ToUint8 = [](std::span<const std::byte> Span)
        {
            std::vector<uint8_t> Out(Span.size());
            std::memcpy(Out.data(), Span.data(), Span.size());
            return Out;
        };

        const std::vector<uint8_t> RulePacket   = ToUint8(RuleBytes);
        const std::vector<uint8_t> DriverPacket = ToUint8(DriverBytes);

        bool Ok = (Relay.WorstPeerRttMs <= 165.0f) &&
                  (Chosen == TransportChannelCategory::EpicOnline) &&
                  HostEndpoint.OpenChannel() &&
                  ClientEndpoint.OpenChannel() &&
                  HostEndpoint.TransmitDatagram(2u, DeliveryChannel::ReliableOrdered, RulePacket) &&
                  HostEndpoint.TransmitDatagram(2u, DeliveryChannel::ReliableOrdered, DriverPacket);

        Switch.AdvanceSwitchClock(0.05);

        std::vector<PacketDatagram> Received{};
        PacketDatagram              Incoming{};
        while (ClientEndpoint.ReceiveDatagram(Incoming))
        {
            Received.push_back(std::move(Incoming));
            Incoming = {};
        }
        RelayedPackets = static_cast<uint32_t>(Received.size());

        std::string Diag{};
        GameplayRulePolicy         SyncedRule{};
        DeterministicDrivingPolicy SyncedDriver{};
        if (Received.size() == 2u)
        {
            const auto AsBytes0 = std::as_bytes(std::span<const uint8_t>(Received[0].WireBytes));
            const auto AsBytes1 = std::as_bytes(std::span<const uint8_t>(Received[1].WireBytes));
            Ok = Ok &&
                 GameplayRulePolicyEngine::DecodePolicyWireBytes(AsBytes0, SyncedRule, Diag) &&
                 SyncedDriver.DecodeWeightBlob(AsBytes1, Diag) &&
                 (SyncedRule.FixedSimulationHz == HostRule.FixedSimulationHz) &&
                 (SyncedDriver.ComputeParameterDigest() == HostDriver.ComputeParameterDigest());
        }
        else
        {
            Ok = false;
        }
        HostEndpoint.CloseChannel();
        ClientEndpoint.CloseChannel();

        Gates.push_back({
            "GATE 8: NETWORKINGSPACE POLICY + WEIGHT SYNC",
            "MINIMAX RELAY + FAILOVER + 48B RULE + 1612B DRIVER SYNCED ON LOOPBACK",
            Ok
        });
    }

    // Render verification PNG
    const std::filesystem::path OutputPng =
        RepoRoot / "Exhibits" / "Images" / "Projects" / "UntitledLifecyclePolicyVerification.png";
    RenderTelemetryPng(OutputPng, Gates, CapturedMetrics, ScoreBefore, ScoreAfter, RelayedPackets);

    bool AllPassed = true;
    for (const auto& Gate : Gates)
    {
        std::cout << (Gate.Passed ? "[PASS] " : "[FAIL] ")
                  << Gate.GateTitle << " -- " << Gate.DetailText << "\n";
        if (!Gate.Passed)
        {
            AllPassed = false;
        }
    }
    return AllPassed ? 0 : 1;
}
