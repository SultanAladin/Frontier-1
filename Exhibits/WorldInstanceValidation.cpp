/*============================================================================================================================================
                                                      WORLDINSTANCEVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation exhibit for lean InstanceToken/InstanceRecordEntry, dual-profile RAII instances, tight loops & .Instance codec

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InstanceCodec.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/InstanceToken.h"
#include "Engine/World/WorldInstanceRegister.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Frontier;

namespace
{

bool ExpectCondition(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        return false;
    }
    return true;
}

bool ValidateCoreMemoryLayout()
{
    bool Passed = true;
    Passed &= ExpectCondition(sizeof(World::InstanceToken) == 8u, "InstanceToken must be 8 bytes");
    Passed &= ExpectCondition(sizeof(World::InstanceRecordEntry) == 48u, "InstanceRecordEntry must be 48 bytes");
    Passed &= ExpectCondition(sizeof(World::CategoryInstanceHandle) == 16u, "CategoryInstanceHandle must be 16 bytes");
    return Passed;
}

bool ValidateSlotRecyclingContainment()
{
    World::WorldInstanceRegister WorldRegister{};
    WorldRegister.ReserveInstanceCapacity(10240u);

    constexpr uint32_t BatchCount = 10000u;
    std::vector<World::InstanceToken> IssuedTokens{};
    IssuedTokens.reserve(BatchCount);

    const auto StartClock = std::chrono::steady_clock::now();
    for (uint32_t i = 0u; i < BatchCount; ++i)
    {
        const World::InstanceToken Token = WorldRegister.IssueInstanceSlot(
            World::InstanceCategory::SurfaceGeometry,
            static_cast<uint32_t>(World::InstanceDirectiveMask::EnableVisibility));
        WorldRegister.AssignWorldTransform(Token, static_cast<float>(i), 0.0f, 1.0f);
        IssuedTokens.push_back(Token);
    }

    bool Passed = true;
    Passed &= ExpectCondition(
        WorldRegister.QueryActiveInstanceCount() == BatchCount,
        "10,000 slots should be active after batch issue");

    // Reclaim half and verify stale tokens are rejected while recycled slots advance generation
    for (uint32_t i = 0u; i < BatchCount; i += 2u)
    {
        Passed &= ExpectCondition(
            WorldRegister.ReclaimInstanceSlot(IssuedTokens[i]),
            "Even-indexed slot reclamation must succeed");
    }

    Passed &= ExpectCondition(
        WorldRegister.QueryActiveInstanceCount() == (BatchCount / 2u),
        "Active count must drop to 5,000 after reclaiming half");
    Passed &= ExpectCondition(
        !WorldRegister.AuthenticateToken(IssuedTokens[0]),
        "Stale reclaimed token must fail authentication");

    const World::InstanceToken ReusedToken = WorldRegister.IssueInstanceSlot(
        World::InstanceCategory::SurfaceGeometry,
        static_cast<uint32_t>(World::InstanceDirectiveMask::EnableVisibility));
    Passed &= ExpectCondition(
        (ReusedToken.SlotIndex < BatchCount) && (ReusedToken.Generation == 2u),
        "Recycled slot must reuse existing index with incremented generation");
    Passed &= ExpectCondition(
        !WorldRegister.AuthenticateToken(IssuedTokens[ReusedToken.SlotIndex]),
        "Previous generation token on recycled slot must remain rejected");

    // Non-kinship containment linking (EnclosingIndex / FirstNestedIndex / NextLateralIndex)
    const World::InstanceToken EnclosingFolder = WorldRegister.IssueInstanceSlot(
        World::InstanceCategory::ContainmentFolder,
        static_cast<uint32_t>(World::InstanceDirectiveMask::EnableVisibility));
    const World::InstanceToken NestedA = IssuedTokens[1];
    const World::InstanceToken NestedB = IssuedTokens[3];

    Passed &= ExpectCondition(
        WorldRegister.AttachNestedSlot(EnclosingFolder, NestedA),
        "AttachNestedSlot(Folder, A) must succeed");
    Passed &= ExpectCondition(
        WorldRegister.AttachNestedSlot(EnclosingFolder, NestedB),
        "AttachNestedSlot(Folder, B) must succeed");
    Passed &= ExpectCondition(
        !WorldRegister.AttachNestedSlot(NestedA, EnclosingFolder),
        "Circular containment must be rejected");

    const World::InstanceRecordEntry* FolderEntry = WorldRegister.FindRecordEntry(EnclosingFolder);
    const World::InstanceRecordEntry* EntryA      = WorldRegister.FindRecordEntry(NestedA);
    const World::InstanceRecordEntry* EntryB      = WorldRegister.FindRecordEntry(NestedB);
    Passed &= ExpectCondition(
        (FolderEntry != nullptr) && (FolderEntry->FirstNestedIndex == NestedB.SlotIndex),
        "Enclosing slot must point FirstNestedIndex to most recently attached nested slot");
    Passed &= ExpectCondition(
        (EntryB != nullptr) && (EntryB->EnclosingIndex == EnclosingFolder.SlotIndex) &&
            (EntryB->NextLateralIndex == NestedA.SlotIndex),
        "NestedB must link laterally to NestedA");
    Passed &= ExpectCondition(
        (EntryA != nullptr) && (EntryA->EnclosingIndex == EnclosingFolder.SlotIndex) &&
            (EntryA->NextLateralIndex == World::InvalidInstanceSlot),
        "NestedA must terminate the lateral chain");

    const auto EndClock   = std::chrono::steady_clock::now();
    const double ElapsedMs = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
    std::cout << "[PASS] 10,000-slot issue/reclaim/containment completed in " << ElapsedMs << " ms\n";
    return Passed;
}

bool ValidateDualProfileTightLoops()
{
    World::WorldInstanceRegister WorldRegister{};
    bool Passed = true;

#if FRONTIER_PROFILE_EDITOR
    struct MockOutlinerRow
    {
        std::string DisplayTitle;
        std::string CategoryTag;
        bool        VisibilityEnabled = true;
    };
    std::unordered_map<uint32_t, MockOutlinerRow> OutlinerRows{};

    World::EditorSurfaceAdapter MockAdapter{};
    MockAdapter.RegisterEditorEntry = [&OutlinerRows](const World::EditorInstanceRegistrationDescriptor& Descriptor)
    {
        OutlinerRows[Descriptor.Token.SlotIndex] = MockOutlinerRow{
            std::string(Descriptor.DisplayTitle),
            std::string(Descriptor.CategoryTag),
            Descriptor.VisibilityEnabled
        };
    };
    MockAdapter.UnregisterEditorEntry = [&OutlinerRows](World::InstanceToken Token)
    {
        OutlinerRows.erase(Token.SlotIndex);
    };
    MockAdapter.AssignEditorVisibility = [&OutlinerRows](World::InstanceToken Token, bool Visible)
    {
        if (auto It = OutlinerRows.find(Token.SlotIndex); It != OutlinerRows.end())
        {
            It->second.VisibilityEnabled = Visible;
        }
    };
    WorldRegister.BindEditorSurfaceAdapter(std::move(MockAdapter));
#endif

    // Production instance emits ZERO Editor callbacks even inside an Editor build
    World::LunarInstanceParameters MoonParams{};
    MoonParams.MoonSlotOrdinal        = 0u;
    MoonParams.RightAscensionDegrees  = 180.0f;
    MoonParams.OrbitalRateDegreesPerS = 10.0f;
    World::LunarInstance ProductionMoon(WorldRegister, MoonParams);

#if FRONTIER_PROFILE_EDITOR
    Passed &= ExpectCondition(
        OutlinerRows.empty(),
        "Production LunarInstance constructor must not emit Editor Outliner rows");

    {
        World::LunarInstance EditorMoon(WorldRegister, "HeroMoon", MoonParams);
        Passed &= ExpectCondition(
            OutlinerRows.size() == 1u &&
                OutlinerRows[EditorMoon.QueryToken().SlotIndex].CategoryTag == "env:moon",
            "Editor LunarInstance constructor must register env:moon Outliner row");

        WorldRegister.AssignVisibility(EditorMoon.QueryToken(), false);
        Passed &= ExpectCondition(
            !OutlinerRows[EditorMoon.QueryToken().SlotIndex].VisibilityEnabled,
            "AssignVisibility must propagate to Editor Outliner row");
    }

    Passed &= ExpectCondition(
        OutlinerRows.empty(),
        "Editor LunarInstance destructor must automatically unregister Outliner row");
#endif

    // Build small active scene to verify tight loops and ExecutionEnabled gate
    World::SolarInstanceParameters SunParams{};
    SunParams.AzimuthDegrees        = 120.0f;
    SunParams.DayCycleSpeedDegreesS = 5.0f;
    SunParams.CycleAnimationEnabled = true;
    World::SolarInstance Sun(WorldRegister, SunParams);

    World::CloudInstanceParameters CloudParams{};
    CloudParams.WindSpeedMetersPerS = 12.0f;
    World::CloudInstance Clouds(WorldRegister, CloudParams);

    World::SurfaceInstance BoxA(WorldRegister, World::SurfaceInstanceParameters{});
    World::TriggerInstance Sensor(WorldRegister, World::TriggerInstanceParameters{ 3.0f, 3.0f, 3.0f, true });
    WorldRegister.AssignWorldTransform(Sensor.QueryToken(), 0.0f, 0.0f, 0.0f);
    WorldRegister.AssignWorldTransform(BoxA.QueryToken(), 1.0f, 0.5f, 0.0f);

    // Step 1: Authoring mode (ExecutionEnabled = false) clears initial RequireRebuild without advancing time
    const World::WorldLoopStepMetrics InitialMetrics =
        World::ExecuteWorldSimulationSequence(WorldRegister, 1.0f, false);
    Passed &= ExpectCondition(
        InitialMetrics.UploadedTransformCount >= 4u,
        "Initial transform loop must upload rebuilt transforms");
    Passed &= ExpectCondition(
        std::fabs(WorldRegister.InspectSolarRegister().AzimuthDegrees[0] - 120.0f) < 1e-4f,
        "ExecutionEnabled=false must not advance solar ephemeris time");

    // Step 2: Idle authoring frame (RequireRebuild == false) must early-out with zero work
    const World::WorldLoopStepMetrics IdleMetrics =
        World::ExecuteWorldSimulationSequence(WorldRegister, 1.0f, false);
    Passed &= ExpectCondition(
        (IdleMetrics.UploadedTransformCount == 0u) &&
            (IdleMetrics.EvaluatedSolarCount == 0u) &&
            (IdleMetrics.EvaluatedLunarCount == 0u),
        "Idle authoring frame with RequireRebuild=false must perform zero work");

    // Step 3: Active play frame (ExecutionEnabled = true, Δt = 2.0s)
    const World::WorldLoopStepMetrics PlayMetrics =
        World::ExecuteWorldSimulationSequence(WorldRegister, 2.0f, true);
    Passed &= ExpectCondition(
        std::fabs(WorldRegister.InspectSolarRegister().AzimuthDegrees[0] - 130.0f) < 1e-4f,
        "ExecuteSolarLoop must advance azimuth by 5 deg/s * 2s = +10 deg");
    Passed &= ExpectCondition(
        std::fabs(WorldRegister.InspectLunarRegister().RightAscensionDegrees[0] - 200.0f) < 1e-4f,
        "ExecuteLunarLoop must advance moon orbit by 10 deg/s * 2s = +20 deg");
    Passed &= ExpectCondition(
        std::fabs(WorldRegister.InspectCloudRegister().AdvectionOffsetMeters[0] - 24.0f) < 1e-4f,
        "ExecuteCloudLoop must advect clouds by 12 m/s * 2s = 24 m");
    Passed &= ExpectCondition(
        (PlayMetrics.EvaluatedTriggerCount == 1u) &&
            (WorldRegister.InspectTriggerRegister().OverlapCountReading[0] >= 1u),
        "ExecuteTriggerLoop must detect overlapping participants inside trigger bounds");

    std::cout << "[PASS] Dual-profile lifecycle and tight SoA main loops verified\n";
    return Passed;
}

bool ValidateInstanceCodecShippingCook()
{
    Codecs::InstanceDocumentModel SourceModel{};
    SourceModel.DocumentTitle   = "CampfireLantern";
    SourceModel.Category        = World::InstanceCategory::SurfaceGeometry;
    SourceModel.SchemaVersion   = 1u;
    SourceModel.PositionMeters  = { 4.5f, -2.0f, 0.85f };
    SourceModel.RotationDegrees = { 0.0f, 0.0f, 35.0f };
    SourceModel.ScaleScalar     = { 1.0f, 1.0f, 1.0f };
    SourceModel.DirectiveMask   = static_cast<uint32_t>(
        World::InstanceDirectiveMask::EnableVisibility |
        World::InstanceDirectiveMask::EnableSimulationTick |
        World::InstanceDirectiveMask::EnableShadowProjection |
        World::InstanceDirectiveMask::EnableDistanceFieldContribution);

    SourceModel.EditorDirectives.OutlinerTitle      = "Campfire Lantern";
    SourceModel.EditorDirectives.CategoryLabel      = "Composite Instance";
    SourceModel.EditorDirectives.Subtitle           = "Multi-format geometry, flame particles & thermal draft";
    SourceModel.EditorDirectives.IconPath           = "EngineContent/Icons/Editor/slate-lantern.svg";
    SourceModel.EditorDirectives.RemovalProtected   = false;
    SourceModel.EditorDirectives.DuplicateProtected = false;

    SourceModel.SpaceSlots = {
        { "PrimarySurface",  ".GeometrySpace", "Content/Props/Lantern.GeometrySpace",   0ull },
        { "SurfaceMaterial", ".MaterialSpace", "Content/Materials/Brass.MaterialSpace", 0ull },
        { "FlameParticles",  ".ParticleSpace", "Content/Effects/Flame.ParticleSpace",   0ull },
        { "ThermalUpdraft",  ".ForceField",    "Content/Forces/Updraft.ForceField",     0ull },
    };

    SourceModel.BinaryDefaultPayload = {
        std::byte{ 0x46 }, std::byte{ 0x52 }, std::byte{ 0x4E }, std::byte{ 0x54 },
        std::byte{ 0x01 }, std::byte{ 0x02 }, std::byte{ 0x03 }, std::byte{ 0x04 }
    };

    bool Passed = true;

    // 1. Editor round-trip (retains [Editor] directives + 4 multi-format SpaceSlots + binary defaults)
    std::vector<std::byte> EditorBytes{};
    std::string Diagnostic{};
    Passed &= ExpectCondition(
        Codecs::InstanceEncoder::EncodeToBytes(SourceModel, EditorBytes, Diagnostic, false),
        "Editor .Instance encode must succeed");

    Codecs::InstanceDocumentModel DecodedEditor{};
    Passed &= ExpectCondition(
        Codecs::InstanceDecoder::DecodeFromBytes(EditorBytes, DecodedEditor, Diagnostic),
        "Editor .Instance decode must succeed");
    Passed &= ExpectCondition(
        (DecodedEditor.DocumentTitle == "CampfireLantern") &&
            (DecodedEditor.Category == World::InstanceCategory::SurfaceGeometry) &&
            (DecodedEditor.SpaceSlots.size() == 4u) &&
            (DecodedEditor.SpaceSlots[2].FormatExtension == ".ParticleSpace") &&
            (DecodedEditor.SpaceSlots[3].FormatExtension == ".ForceField") &&
            (DecodedEditor.EditorDirectives.OutlinerTitle == "Campfire Lantern") &&
            (DecodedEditor.BinaryDefaultPayload == SourceModel.BinaryDefaultPayload),
        "Editor .Instance round-trip must preserve multi-format slots, Editor directives and binary defaults");

    // 2. Shipping cook round-trip (strips [Editor] completely while preserving runtime SpaceSlots & binary defaults)
    std::vector<std::byte> ShippingBytes{};
    Passed &= ExpectCondition(
        Codecs::InstanceEncoder::EncodeToBytes(SourceModel, ShippingBytes, Diagnostic, true),
        "Shipping .Instance cook encode must succeed");
    Passed &= ExpectCondition(
        ShippingBytes.size() < EditorBytes.size(),
        "Shipping cooked .Instance must be smaller than Editor .Instance");

    Codecs::InstanceDocumentModel DecodedShipping{};
    Passed &= ExpectCondition(
        Codecs::InstanceDecoder::DecodeFromBytes(ShippingBytes, DecodedShipping, Diagnostic),
        "Shipping cooked .Instance decode must succeed");
    Passed &= ExpectCondition(
        DecodedShipping.EditorDirectives.OutlinerTitle.empty() &&
            DecodedShipping.EditorDirectives.IconPath.empty() &&
            (DecodedShipping.SpaceSlots.size() == 4u) &&
            (DecodedShipping.BinaryDefaultPayload == SourceModel.BinaryDefaultPayload),
        "Shipping cooked .Instance must strip Editor directives while preserving runtime slots & binary defaults");

    std::cout << "[PASS] .Instance multi-format codec & Shipping cook verified ("
              << EditorBytes.size() << " B Editor -> " << ShippingBytes.size() << " B Shipping)\n";
    return Passed;
}

} // namespace

int main()
{
    bool AllPassed = true;
    AllPassed &= ValidateCoreMemoryLayout();
    AllPassed &= ValidateSlotRecyclingContainment();
    AllPassed &= ValidateDualProfileTightLoops();
    AllPassed &= ValidateInstanceCodecShippingCook();

    if (!AllPassed)
    {
        std::cerr << "[ERROR] WorldInstanceValidation failed.\n";
        return 1;
    }

    std::cout << "[OK] All WorldInstanceValidation checks passed.\n";
    return 0;
}
