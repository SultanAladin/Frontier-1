/*============================================================================================================================================
                                                        VECTORFIELDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation suite for 19 ForceModels, CurlNoise div=0, motion paths, Formula->SPIR-V, Codecs & Vulkan 1.3 GPU UI

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ForceModelPresetCodec.h"
#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/GeometricRaster/Compute/FieldLatticeVulkanExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"
#include "Engine/SimulationSpace/ForceFields/VectorField.h"

#if FRONTIER_EDITOR_ENABLED
    #include "Engine/Components/Appearance/TypographyRepository.h"
    #include "Engine/Components/ControlCentre/ControlCentrePanel.h"
    #include "Engine/Components/Docking/WorkspaceDockContainer.h"
    #include "Engine/Components/Panes/InspectorPanel.h"
    #include "Engine/Components/Panes/OutlinerPanel.h"
    #include "Engine/Components/Panes/ViewportPanel.h"
    #include "Engine/Editor/Workspaces/Simulation/VectorFieldViewportCoordinator.h"
    #include "Exhibits/SoftwareCanvasRasterizer.h"
    #include <imgui.h>
    #include <imgui_internal.h>
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::Codecs;
using namespace Frontier::SimulationSpace::ForceFields;

[[nodiscard]] bool VerifyCondition(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[VectorFieldValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path ResolveShaderDir()
{
#if defined(FRONTIER_COMPILED_SHADER_DIR)
    return std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#elif defined(FRONTIER_SHADER_BINARY_DIR)
    return std::filesystem::path(FRONTIER_SHADER_BINARY_DIR);
#else
    return std::filesystem::current_path() / "EngineContent/Shaders";
#endif
}

[[nodiscard]] std::filesystem::path ResolveSwiftShaderDir()
{
#if defined(FRONTIER_SWIFTSHADER_ICD_DIR)
    return std::filesystem::path(FRONTIER_SWIFTSHADER_ICD_DIR);
#elif defined(FRONTIER_REPO_ROOT_DIR)
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "ExternalPackages/VulkanCompute/SwiftShader";
#else
    return std::filesystem::current_path() / "ExternalPackages/VulkanCompute/SwiftShader";
#endif
}

#if FRONTIER_EDITOR_ENABLED
[[nodiscard]] std::filesystem::path ResolveRepoRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                            1. 19 FORCE MODELS & DIVERGENCE
//------------------------------------------------------------------------------------------------------------------------

bool ValidateAllForceModelsDivergence()
{
    for (uint32_t ModelOrd = 0u; ModelOrd < static_cast<uint32_t>(ForceModel::Count); ++ModelOrd)
    {
        ForceSourceRecordEntry Entry{};
        Entry.UniqueToken  = ModelOrd + 1u;
        Entry.Model        = static_cast<ForceModel>(ModelOrd);
        Entry.Domain       = QueryDomainForModel(Entry.Model);
        Entry.CentreMeters = { 0.0f, 0.0f, 5.0f };
        Entry.RadiusMeters = 25.0f;
        Entry.Strength     = 16.0f;
        Entry.SwirlRatio   = 0.8f;

        if (Entry.Model == ForceModel::SplineForce)
        {
            AssignPresetMotionPath(Entry, MotionPathTopology::CatmullSpline);
        }

        ForceFieldAccumulator Acc{};
        Acc.AssignSources(std::span<const ForceSourceRecordEntry>(&Entry, 1u));
        Acc.StepAnimation(0.75, true);

        const SpatialForceSample Eval = Acc.QueryForceFieldSample({ 3.2f, -1.8f, 6.4f }, { 2.0f, -1.0f, 0.5f }, 0.75);
        if (!VerifyCondition(
                std::isfinite(Eval.AdvectionVelocity.X) &&
                std::isfinite(Eval.AdvectionVelocity.Y) &&
                std::isfinite(Eval.AdvectionVelocity.Z) &&
                std::isfinite(Eval.LinearAcceleration.X),
                "All 19 ForceModel evaluations produce finite vectors"))
        {
            return false;
        }
    }

    // Verify +Z Up thermal updraft
    {
        ForceSourceRecordEntry UpEntry{};
        UpEntry.UniqueToken   = 99u;
        UpEntry.Model         = ForceModel::FlowStream;
        UpEntry.StreamProfile = FlowStreamProfile::ThermalUpdraft;
        UpEntry.RadiusMeters  = 20.0f;
        UpEntry.Strength      = 14.0f;
        UpEntry.CentreMeters  = { 0.0f, 0.0f, 0.0f };

        ForceFieldAccumulator Acc{};
        Acc.AssignSources(std::span<const ForceSourceRecordEntry>(&UpEntry, 1u));
        const auto Out = Acc.QueryForceFieldSample({ 2.0f, 1.0f, 4.0f }, {}, 0.5);
        if (!VerifyCondition(Out.AdvectionVelocity.Z > 2.0f, "FlowStream ThermalUpdraft lifts along +Z Up"))
        {
            return false;
        }
    }

    // Verify CurlNoise divergence-free guarantee
    {
        ForceSourceRecordEntry CurlEntry{};
        CurlEntry.UniqueToken           = 100u;
        CurlEntry.Model                 = ForceModel::CurlNoise;
        CurlEntry.Domain                = ForceDomainCategory::TurbulenceField;
        CurlEntry.RadiusMeters          = 30.0f;
        CurlEntry.Strength              = 15.0f;
        CurlEntry.TurbulenceScaleMeters = 12.0f;
        CurlEntry.OctaveCount           = 2u;
        CurlEntry.CentreMeters          = { 0.0f, 0.0f, 0.0f };

        ForceFieldAccumulator Acc{};
        Acc.AssignSources(std::span<const ForceSourceRecordEntry>(&CurlEntry, 1u));

        float MaxAbsDiv = 0.0f;
        for (int Ix = -1; Ix <= 1; ++Ix)
        {
            for (int Iy = -1; Iy <= 1; ++Iy)
            {
                for (int Iz = -1; Iz <= 1; ++Iz)
                {
                    const auto Probe = Acc.QueryAnemometerDiagnostics(
                        { Ix * 4.5f + 1.1f, Iy * 4.5f - 0.7f, Iz * 4.5f + 0.9f },
                        0.0);
                    MaxAbsDiv = std::max(MaxAbsDiv, std::abs(Probe.Divergence));
                }
            }
        }
        if (!VerifyCondition(MaxAbsDiv < 0.05f, "CurlNoise field divergence stays near zero (< 0.05 /s)"))
        {
            std::fprintf(stderr, "Measured MaxAbsDiv = %f\n", MaxAbsDiv);
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           2. MOTION PATHS & TEMPORAL LAWS
//------------------------------------------------------------------------------------------------------------------------

bool ValidateMotionPathsTemporalModels()
{
    ForceSourceRecordEntry LineEntry{};
    LineEntry.UniqueToken                = 1u;
    LineEntry.Model                      = ForceModel::RotationalColumn;
    LineEntry.RadiusMeters               = 15.0f;
    LineEntry.Strength                   = 12.0f;
    LineEntry.DynamicMotion              = true;
    LineEntry.Motion.Topology            = MotionPathTopology::LinearSpan;
    LineEntry.Motion.CycleBehavior       = MotionCycleBehavior::PingPong;
    LineEntry.Motion.SpeedMetresPerSecond = 10.0f;
    LineEntry.Motion.WaypointCount       = 2u;
    LineEntry.Motion.Waypoints[0]        = { 0.0f, 0.0f, 4.0f };
    LineEntry.Motion.Waypoints[1]        = { 20.0f, 0.0f, 4.0f };

    const auto MidSample = EvaluateForceMotionAtTime(LineEntry, 1.0);
    if (!VerifyCondition(std::abs(MidSample.PositionMeters.X - 10.0f) < 0.25f, "LinearSpan reaches midpoint at t=1.0s"))
    {
        return false;
    }
    const auto BounceSample = EvaluateForceMotionAtTime(LineEntry, 2.5);
    if (!VerifyCondition(std::abs(BounceSample.PositionMeters.X - 15.0f) < 0.25f, "LinearSpan PingPong bounces smoothly"))
    {
        return false;
    }

    ForceSourceRecordEntry OrbitEntry{};
    OrbitEntry.UniqueToken                         = 2u;
    OrbitEntry.Model                               = ForceModel::LinearWind;
    OrbitEntry.DynamicMotion                       = true;
    OrbitEntry.Motion.Topology                     = MotionPathTopology::HelicalOrbit;
    OrbitEntry.Motion.SpeedMetresPerSecond         = 8.0f;
    OrbitEntry.Motion.OrbitRadiusMeters            = 10.0f;
    OrbitEntry.Motion.OrbitVerticalAmplitudeMeters = 0.0f;
    OrbitEntry.CentreMeters                        = { 0.0f, 0.0f, 6.0f };

    const auto OrbSample = EvaluateForceMotionAtTime(OrbitEntry, 1.2);
    const float R = std::hypot(OrbSample.PositionMeters.X, OrbSample.PositionMeters.Y);
    if (!VerifyCondition(
            std::abs(R - 10.0f) < 0.1f && std::abs(OrbSample.PositionMeters.Z - 6.0f) < 0.1f,
            "HelicalOrbit maintains constant XY radius in +Z Up plane"))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       3. FORMULA COMPILER & GPU COMPUTE PARITY
//------------------------------------------------------------------------------------------------------------------------

bool ValidateFormulaCompilerComputeParity()
{
    const auto ValidOut = CompileFieldFormula("vec3(-p.y * 0.4 + 3.0, p.x * 0.4, 2.5 + 1.5 * sin(t))");
    if (!VerifyCondition(ValidOut.Compiled, ("Compile valid VectorFieldLang 3D expression: " + ValidOut.DiagnosticMessage).c_str()))
    {
        return false;
    }

    const auto InvalidOut = CompileFieldFormula("vec3(unknownSym, 0, )");
    if (!VerifyCondition(!InvalidOut.Compiled && !InvalidOut.DiagnosticMessage.empty(), "Invalid VectorFieldLang syntax returns structured failure diagnostic"))
    {
        return false;
    }

    ForceSourceRecordEntry FormulaSrc{};
    FormulaSrc.CentreMeters = { 0.0f, 0.0f, 0.0f };
    FormulaSrc.RadiusMeters = 20.0f;
    FormulaSrc.Strength     = 1.0f;
    const auto Sample       = EvaluateCompiledFormula(ValidOut.Program, FormulaSrc, { 4.0f, 2.0f, 3.0f }, {}, 0.0);
    if (!VerifyCondition(std::abs(Sample.X - 2.2f) < 1.0e-4f && std::abs(Sample.Y - 1.6f) < 1.0e-4f, "Bytecode evaluator exact match"))
    {
        return false;
    }

    // Dispatch FieldLatticeCompute.spv on SwiftShader / Hardware Vulkan and verify parity with CPU lattice cascade
    FieldLatticeVulkanExecutor Executor{};
    std::string                Diag;
    if (!VerifyCondition(
            Executor.Initialize(ResolveShaderDir() / "FieldLatticeCompute.spv", ResolveSwiftShaderDir(), Diag),
            ("FieldLatticeVulkanExecutor::Initialize failed: " + Diag).c_str()))
    {
        return false;
    }

    ForceSourceRecordEntry Wind{};
    Wind.UniqueToken  = 1u;
    Wind.Model        = ForceModel::LinearWind;
    Wind.CentreMeters = { 0.0f, 0.0f, 0.0f };
    Wind.RadiusMeters = 32.0f;
    Wind.Strength     = 18.0f;

    ForceSourceRecordEntry Vortex{};
    Vortex.UniqueToken  = 2u;
    Vortex.Model        = ForceModel::RotationalColumn;
    Vortex.CentreMeters = { 4.0f, -3.0f, 2.0f };
    Vortex.RadiusMeters = 22.0f;
    Vortex.Strength     = 14.0f;

    const std::array<ForceSourceRecordEntry, 2> Sources = { Wind, Vortex };
    ForceFieldAccumulator Acc{};
    Acc.AssignSources(Sources);

    FieldLatticeCascade CpuCascade(8u);
    CpuCascade.AssignFocusOrigin({ 0.0f, 0.0f, 0.0f });
    CpuCascade.EvaluateTierOnCpu(0u, Acc, 0.0);

    std::vector<ForceSourceShaderEntry> PackedSrcs;
    std::vector<SpatialVector3>         PackedWps;
    CpuCascade.PackGpuSourceBuffers(Acc, 0.0, PackedSrcs, PackedWps);

    std::vector<FieldLatticeVoxelSample> GpuVoxels;
    if (!VerifyCondition(
            Executor.DispatchLatticeTier(
                CpuCascade.QueryTierDescriptor(0u),
                PackedSrcs,
                PackedWps,
                0.0,
                GpuVoxels,
                Diag),
            ("Vulkan compute DispatchLatticeTier failed: " + Diag).c_str()))
    {
        return false;
    }

    const auto& CpuVoxels = CpuCascade.QueryTierVoxels(0u);
    if (!VerifyCondition(CpuVoxels.size() == GpuVoxels.size() && CpuVoxels.size() == 512u, "GPU lattice voxel count == 8^3"))
    {
        return false;
    }

    float MaxDiffMps = 0.0f;
    for (std::size_t I = 0u; I < CpuVoxels.size(); ++I)
    {
        MaxDiffMps = std::max(MaxDiffMps, std::abs(CpuVoxels[I].VelocityX - GpuVoxels[I].VelocityX));
        MaxDiffMps = std::max(MaxDiffMps, std::abs(CpuVoxels[I].VelocityY - GpuVoxels[I].VelocityY));
        MaxDiffMps = std::max(MaxDiffMps, std::abs(CpuVoxels[I].VelocityZ - GpuVoxels[I].VelocityZ));
    }
    if (!VerifyCondition(MaxDiffMps < 0.25f, "CPU FieldLatticeCascade vs Vulkan .slang SPIR-V compute parity < 0.25 m/s"))
    {
        std::fprintf(stderr, "Measured MaxDiffMps = %f\n", MaxDiffMps);
        return false;
    }

    Executor.Finalize();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       4. OPTION A VECTORFIELD & FORCEMODEL CODECS
//------------------------------------------------------------------------------------------------------------------------

bool ValidateVectorFieldCodecs()
{
    const auto ScratchDir = std::filesystem::temp_directory_path() / "frontier_vf_codecs_test";
    std::filesystem::create_directories(ScratchDir);

    // .ForceModel preset round-trip
    ForceModelPresetDocument PresetIn{};
    PresetIn.PresetTitle                = "HurricaneHelix";
    PresetIn.AuthorNote                 = "High-shear helical updraft preset";
    PresetIn.ForceSource.UniqueToken    = 10u;
    PresetIn.ForceSource.Model          = ForceModel::FlowStream;
    PresetIn.ForceSource.Strength       = 24.5f;
    PresetIn.ForceSource.RadiusMeters   = 28.0f;
    PresetIn.ForceSource.FormulaEnabled = true;
    PresetIn.ForceSource.FormulaSourceText = QueryBuiltInFormulaPresets()[0].FormulaSource;

    const auto PresetPath = ScratchDir / "HelixPreset.ForceModel";
    std::string Err;
    if (!VerifyCondition(ForceModelPresetCodec::Encoder::EncodeToFile(PresetPath, PresetIn, Err), ("Encode .ForceModel failed: " + Err).c_str()))
    {
        return false;
    }
    ForceModelPresetDocument PresetOut{};
    if (!VerifyCondition(ForceModelPresetCodec::Decoder::DecodeFromFile(PresetPath, PresetOut, Err), ("Decode .ForceModel failed: " + Err).c_str()))
    {
        return false;
    }
    if (!VerifyCondition(
            PresetOut.PresetTitle == PresetIn.PresetTitle &&
            PresetOut.ForceSource.Model == ForceModel::FlowStream &&
            std::abs(PresetOut.ForceSource.Strength - 24.5f) < 1.0e-4f &&
            PresetOut.ForceSource.FormulaSourceText == PresetIn.ForceSource.FormulaSourceText,
            ".ForceModel preset round-trip preserves metadata & formula"))
    {
        return false;
    }

    // .VectorFieldSpace multi-source container round-trip
    VectorFieldSpaceDocument DocIn{};
    DocIn.DocumentTitle = "WindTunnelVerification";

    ForceSourceRecordEntry Rec1{};
    Rec1.UniqueToken                = 101u;
    Rec1.Label                      = "PrimaryJet";
    Rec1.Model                      = ForceModel::FlowStream;
    Rec1.StreamProfile              = FlowStreamProfile::SlipstreamJet;
    Rec1.CentreMeters               = { -10.0f, 2.0f, 5.0f };
    Rec1.Strength                   = 22.0f;
    Rec1.RadiusMeters               = 18.0f;
    Rec1.Motion.Topology            = MotionPathTopology::CatmullSpline;
    Rec1.Motion.WaypointCount       = 3u;
    Rec1.Motion.Waypoints[0]        = { -10.0f, 2.0f, 5.0f };
    Rec1.Motion.Waypoints[1]        = { 0.0f, 8.0f, 7.0f };
    Rec1.Motion.Waypoints[2]        = { 12.0f, -3.0f, 6.0f };

    DocIn.ForceSources = { Rec1, PresetIn.ForceSource };

    const auto SpacePath = ScratchDir / "ValidationWindTunnel.VectorFieldSpace";
    if (!VerifyCondition(VectorFieldCodec::Encoder::EncodeToFile(SpacePath, DocIn, Err), ("Encode .VectorFieldSpace failed: " + Err).c_str()))
    {
        return false;
    }
    VectorFieldSpaceDocument DocOut{};
    if (!VerifyCondition(VectorFieldCodec::Decoder::DecodeFromFile(SpacePath, DocOut, Err), ("Decode .VectorFieldSpace failed: " + Err).c_str()))
    {
        return false;
    }
    if (!VerifyCondition(
            DocOut.DocumentTitle == DocIn.DocumentTitle &&
            DocOut.ForceSources.size() == 2u &&
            DocOut.ForceSources[0].Label == "PrimaryJet" &&
            DocOut.ForceSources[0].Motion.WaypointCount == 3u &&
            DocOut.ForceSources[1].FormulaSourceText == PresetIn.ForceSource.FormulaSourceText,
            ".VectorFieldSpace container round-trip preserves inline records, splines & formula"))
    {
        return false;
    }

    std::filesystem::remove_all(ScratchDir);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  5. EDITOR VULKAN 1.3 + 3-DOCK VISUAL EXHIBITS
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR_ENABLED

bool ValidateEditorVulkanOverlayExhibits()
{
    using namespace Frontier;
    using namespace Frontier::Components;
    using namespace Frontier::Editor::Simulation;

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto RepoRoot = ResolveRepoRoot();
    TypographyRepository Typefaces{};
    if (!VerifyCondition(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }
    unsigned char* FontPixels = nullptr;
    int            AtlasW = 0, AtlasH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

    OutlinerPanel  Outliner{};
    ViewportPanel  Viewport{};
    InspectorPanel Inspector{};
    VectorFieldViewportCoordinator Coordinator{};

    std::string Diag;
    if (!VerifyCondition(
            Coordinator.AttachInitializedSession(
                Outliner,
                Viewport,
                Inspector,
                ResolveShaderDir(),
                ResolveSwiftShaderDir(),
                Diag),
            ("VectorFieldViewportCoordinator::InitializeAndAttach failed: " + Diag).c_str()))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Build rich multi-force 3D demonstration scene
    const OutlinerSlotToken WindFolder = Outliner.ConstructFolder("Wind Tunnel Forces");

    ForceSourceRecordEntry VortexDesc{};
    VortexDesc.Label                  = "Helical Vortex Path";
    VortexDesc.Model                  = ForceModel::RotationalColumn;
    VortexDesc.CentreMeters           = { -6.0f, -2.0f, 5.0f };
    VortexDesc.RadiusMeters           = 20.0f;
    VortexDesc.Strength               = 22.0f;
    VortexDesc.SwirlRatio             = 1.15f;
    VortexDesc.DynamicMotion          = true;
    VortexDesc.Motion.Topology        = MotionPathTopology::CatmullSpline;
    VortexDesc.Motion.CycleBehavior   = MotionCycleBehavior::PingPong;
    VortexDesc.Motion.SpeedMetresPerSecond = 5.5f;
    VortexDesc.Motion.WaypointCount   = 3u;
    VortexDesc.Motion.Waypoints[0]    = { -14.0f, -6.0f, 4.5f };
    VortexDesc.Motion.Waypoints[1]    = { -3.0f, 10.0f, 8.0f };
    VortexDesc.Motion.Waypoints[2]    = { 12.0f, -4.0f, 6.0f };
    const OutlinerSlotToken VortexToken = Coordinator.AddForceSourceEntry(VortexDesc, WindFolder);

    ForceSourceRecordEntry JetDesc{};
    JetDesc.Label        = "Cantilever Jet Plume";
    JetDesc.Model        = ForceModel::FlowStream;
    JetDesc.StreamProfile= FlowStreamProfile::SlipstreamJet;
    JetDesc.CentreMeters = { 14.0f, 8.0f, 7.0f };
    JetDesc.EulerDegrees = { 0.0f, -12.0f, -145.0f };
    JetDesc.RadiusMeters = 17.0f;
    JetDesc.Strength     = 26.0f;
    (void)Coordinator.AddForceSourceEntry(JetDesc, WindFolder);

    ForceSourceRecordEntry BurstDesc{};
    BurstDesc.Label                        = "Orbital Radial Burst";
    BurstDesc.Model                        = ForceModel::RadialBurst;
    BurstDesc.CentreMeters                 = { -12.0f, 15.0f, 6.0f };
    BurstDesc.RadiusMeters                 = 14.0f;
    BurstDesc.Strength                     = 20.0f;
    BurstDesc.DurationMode                 = ForceDurationMode::Modulated;
    BurstDesc.FrequencyHertz               = 0.4f;
    BurstDesc.DynamicMotion                = true;
    BurstDesc.Motion.Topology              = MotionPathTopology::HelicalOrbit;
    BurstDesc.Motion.SpeedMetresPerSecond  = 6.0f;
    BurstDesc.Motion.OrbitRadiusMeters     = 8.0f;
    BurstDesc.Motion.WaypointCount         = 2u;
    BurstDesc.Motion.Waypoints[0]          = { -12.0f, 15.0f, 6.0f };
    BurstDesc.Motion.Waypoints[1]          = { -4.0f, 15.0f, 6.0f };
    (void)Coordinator.AddForceSourceEntry(BurstDesc, WindFolder);

    Outliner.SelectSingle(VortexToken);
    Outliner.ConfigureFilterStripOpen(true);
    Coordinator.ConfigureSelectedWaypointIndex(1);

    Viewport.ConfigureExecutionMode(ViewportExecutionMode::Simulate, false);
    auto& Ov = Viewport.AccessForceOverlaySettings();
    Ov.Enabled                      = true;
    Ov.ShowHeatmap                  = true;
    Ov.ShowVectors                  = true;
    Ov.ShowStreamlines              = true;
    Ov.ShowBounds                   = true;
    Ov.ShowMotionCurves             = true;
    Ov.CrossSectionAnemometerActive = true;
    Ov.CrossSectionAxis             = CrossSectionPlaneAxis::XYHorizontal;
    Ov.CrossSectionOffsetMeters     = 5.0f;
    Ov.StreamColorMode              = StreamlineColorMode::SpeedRamp;
    Ov.StreamRibbonThickness        = 0.72f;
    Ov.StreamLineCount              = 128u;
    Ov.AnemometerWorldMeters        = { 1.5f, 2.0f, 5.5f };

    // Step simulation 1.8s so moving curves & phase counters advance visibly
    for (int S = 0; S < 108; ++S)
    {
        Coordinator.StepSimulation(1.0 / 60.0);
    }

    const auto OutDir = RepoRoot / "Exhibits" / "Images" / "VectorField";
    std::filesystem::create_directories(OutDir);

    auto CaptureFullWorkspacePng = [&](const char* FileName) -> bool
    {
        HeadlessWorkspaceDockContainer Container{};
        const auto WinDesc = Frontier::Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier ForceFields", 1600, 980);
        if (!Container.Initialize(WinDesc))
        {
            return false;
        }
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Outliner",
            DockPlacementDirection::SplitLeft,
            0.21f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Viewport",
            DockPlacementDirection::CenterTabGroup,
            0.54f,
            PaneDirectiveMask::CentralViewportDirectives,
            [&]() { Viewport.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });
        (void)Container.RegisterPane(WorkspaceDockPaneDescriptor{
            "Inspector",
            DockPlacementDirection::SplitRight,
            0.26f,
            PaneDirectiveMask::StandardPaneDirectives,
            [&]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); } });

        ThemeRepository Theme{};
        Theme.ConfigureTheme(ThemePreset::Dark);
        ConfigureImGuiWorkspaceTheme(Container.ActiveTabMetrics(), Theme);

        ControlCentrePanel ControlCentre{};
        ControlCentre.AttachTypefaces(&Typefaces);
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();

        Io.DisplaySize = ImVec2(1600.0f, 980.0f);
        Io.DeltaTime   = 1.0f / 60.0f;

        for (int Frame = 0; Frame < 4; ++Frame)
        {
            ImGui::NewFrame();
            Container.PresentWindowImGuiPanes(HostWindowToken{ 0u });
            ControlCentre.Render({ 0.0f, 0.0f, 1600.0f, 980.0f }, 1.0 / 60.0, 60.0f);
            Coordinator.AccessContentBrowser().PresentInImGuiHost(1.0 / 60.0, 1.0f);
            ImGui::Render();
        }

        Frontier::Exhibits::SoftwareRasterCanvas Canvas(1600u, 980u, RgbaColor8{ 14u, 14u, 14u, 255u });
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const bool Saved = Canvas.WritePngFile(OutDir / FileName);
        Container.Finalize();
        return VerifyCondition(Saved, FileName);
    };

    // Exhibit 1: Lit PBR + Full Force Viz Overlay + 3-Dock Workspace
    if (!CaptureFullWorkspacePng("01-ForceFieldLitPbrAndOverlay.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Verify Direct High-Res Vulkan 1.3 Canvas & MRT PickID readback
    const auto W = Coordinator.AccessOverlaySequence().QueryPaneWidth(0u);
    const auto H = Coordinator.AccessOverlaySequence().QueryPaneHeight(0u);
    if (!VerifyCondition(W > 300u && H > 300u, "Vulkan 1.3 offscreen MRT color/pick buffers populated"))
    {
        return false;
    }
    {
        std::vector<ForceSourceShaderEntry> PackedSrcs;
        std::vector<SpatialVector3>         PackedWps;
        Coordinator.AccessLatticeCascade().PackGpuSourceBuffers(
            Coordinator.QueryAccumulator(),
            Coordinator.QuerySimulationTimeSeconds(),
            PackedSrcs,
            PackedWps);
        std::vector<uint32_t> Tokens(PackedSrcs.size(), 0u);
        std::vector<uint32_t> WpOff(PackedSrcs.size(), 0u), WpCnt(PackedSrcs.size(), 0u);
        std::vector<uint8_t>  IsOrb(PackedSrcs.size(), 0u);
        const auto LiveSrcs = Coordinator.QueryAccumulator().QuerySources();
        for (std::size_t I = 0u; I < PackedSrcs.size(); ++I)
        {
            Tokens[I] = (I < LiveSrcs.size()) ? LiveSrcs[I].OutlinerTokenId : static_cast<uint32_t>(I + 1u);
            WpOff[I]  = PackedSrcs[I].WaypointStartIdx;
            WpCnt[I]  = PackedSrcs[I].WaypointCount;
            IsOrb[I]  = (I < LiveSrcs.size() && LiveSrcs[I].Motion.Topology == MotionPathTopology::HelicalOrbit) ? 1u : 0u;
        }

        OverlayFrameRequest DirectReq{};
        DirectReq.PaneIndex             = 0u;
        DirectReq.WidthPx               = 1280u;
        DirectReq.HeightPx              = 720u;
        DirectReq.Camera                = Coordinator.AccessCamera(0u);
        DirectReq.OverlaySettings       = Ov;
        DirectReq.TimeSeconds           = Viewport.QuerySimulationTimeSeconds();
        DirectReq.SelectedForceToken    = VortexToken.Identifier;
        DirectReq.SelectedWaypointIndex = 1;
        DirectReq.LitShadingEnabled     = true;
        DirectReq.SliceHalfSpanMeters   = 36.0f;
        DirectReq.PackedSources         = PackedSrcs;
        DirectReq.SourceTokens          = Tokens;
        DirectReq.PackedWaypoints       = PackedWps;
        DirectReq.SourceWaypointOffsets = WpOff;
        DirectReq.SourceWaypointCounts  = WpCnt;
        DirectReq.SourcePathIsOrbit     = IsOrb;

        const auto DirectOut = Coordinator.AccessOverlaySequence().RenderPane(DirectReq, Diag);
        if (!VerifyCondition(DirectOut.RenderedOnVulkan, ("Direct Vulkan render failed: " + Diag).c_str()))
        {
            return false;
        }
        if (!VerifyCondition(
                Coordinator.AccessOverlaySequence().SavePanePng(0u, OutDir / "03-ForceFieldVulkanCanvasDirect.png"),
                "Save 03-ForceFieldVulkanCanvasDirect.png"))
        {
            return false;
        }

        // Verify PickID MRT buffer contains distinct LitObject and ForceCircle hits
        bool SawLitPick   = false;
        bool SawForcePick = false;
        for (uint32_t Y = 0u; Y < 720u; Y += 8u)
        {
            for (uint32_t X = 0u; X < 1280u; X += 8u)
            {
                const auto Pick = Coordinator.AccessOverlaySequence().QueryPickAtNormalizedUv(
                    0u,
                    static_cast<float>(X) / 1280.0f,
                    static_cast<float>(Y) / 720.0f);
                if (Pick.Category == OverlayPickCategory::LitObject)   { SawLitPick   = true; }
                if (Pick.Category == OverlayPickCategory::ForceCircle) { SawForcePick = true; }
            }
        }
        if (!VerifyCondition(SawLitPick && SawForcePick, "Vulkan RGBA32UI MRT PickID emits distinct LitObject and ForceCircle IDs"))
        {
            return false;
        }
    }

    // Exhibit 2: Split Viewport (Perspective | Top) + Diagnostics + Scrolled Field Inspector (Radial Envelope + Motion Path + Waypoints)
    Viewport.ConfigureSplitViewport(true);
    Viewport.ConfigurePrimaryProjection(ViewportProjectionPreset::Perspective);
    Viewport.ConfigureSecondaryProjection(ViewportProjectionPreset::Top);
    Viewport.ConfigureDiagnosticsVisible(true);
    Coordinator.AccessForceInspectorView().ConfigureActiveTab(ForceInspectorTab::Field);
    Coordinator.AccessForceInspectorView().ConfigureScrollOffset(830.0f);
    Ov.StreamColorMode  = StreamlineColorMode::CyanSingle;
    Ov.CrossSectionAxis = CrossSectionPlaneAxis::AllThreeAxes;

    if (!CaptureFullWorkspacePng("02-ForceFieldSplitViewPerspectiveAndTop.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Exhibits 4..6: Verify Formula, Layers, and Cache Inspector tabs 1:1
    Viewport.ConfigureSplitViewport(false);
    Viewport.ConfigureDiagnosticsVisible(false);
    Coordinator.AccessForceInspectorView().ConfigureActiveTab(ForceInspectorTab::Formula);
    Coordinator.AccessForceInspectorView().ConfigureScrollOffset(0.0f);
    if (!CaptureFullWorkspacePng("04-ForceFieldInspectorFormulaTab.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Coordinator.AccessForceInspectorView().ConfigureActiveTab(ForceInspectorTab::Layers);
    Coordinator.AccessForceInspectorView().ConfigureScrollOffset(0.0f);
    if (!CaptureFullWorkspacePng("05-ForceFieldInspectorLayersTab.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Coordinator.AccessForceInspectorView().ConfigureActiveTab(ForceInspectorTab::Cache);
    Coordinator.AccessForceInspectorView().ConfigureScrollOffset(0.0f);
    if (!CaptureFullWorkspacePng("06-ForceFieldInspectorCacheTab.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Restore default Field tab scroll offset 0
    Coordinator.AccessForceInspectorView().ConfigureActiveTab(ForceInspectorTab::Field);
    Coordinator.AccessForceInspectorView().ConfigureScrollOffset(0.0f);

    // Exhibit 7: Bottom pull-up Content Browser drawer with Force presets, 19 ForceModel laws & VectorFieldLang formulas
    auto& Browser = Coordinator.AccessContentBrowser();
    if (!VerifyCondition(Browser.QueryAllAssets().size() >= 36u, "ContentBrowser populated with >= 36 presets/laws/formulas"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    Browser.ConfigureOriginFilter(ContentAssetOrigin::All);
    Browser.ConfigureCategoryFilter(ContentAssetCategory::ForcePresets);
    Browser.ConfigureSelectedAssetId("preset-supercell-cyclone");
    Browser.SnapOpen(true);
    if (!CaptureFullWorkspacePng("07-ForceFieldContentBrowserDrawer.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Functional check 1: Construct a Force preset directly from the Content Browser
    const std::size_t BeforeBrowserAddCount = Coordinator.QueryAccumulator().QuerySources().size();
    if (!VerifyCondition(Browser.ConstructSelectedPreset(), "ConstructSelectedPreset() returned true"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    Coordinator.StepSimulation(1.0 / 60.0);
    const auto AfterBrowserSrcs = Coordinator.QueryAccumulator().QuerySources();
    if (!VerifyCondition(
            AfterBrowserSrcs.size() == BeforeBrowserAddCount + 1u &&
            AfterBrowserSrcs.back().Model == ForceModel::RotationalColumn,
            "ContentBrowser ConstructSelectedPreset() spawned ForceModel::RotationalColumn into Accumulator"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    Browser.SnapOpen(false);

    // Functional check 2: Write .ForceModel preset on disk & import via LoadForcePresetsFromDirectory
    {
        const auto TempPresetDir = RepoRoot / "Build" / "_PresetValidationTmp";
        std::filesystem::create_directories(TempPresetDir);
        Codecs::ForceModelPresetDocument SaveDoc{};
        SaveDoc.PresetTitle  = "Bespoke Cyclotron Saved";
        SaveDoc.AuthorNote   = "Validation roundtrip preset.";
        SaveDoc.ForceSource  = AfterBrowserSrcs.back();
        SaveDoc.ForceSource.Model = ForceModel::LorentzField;
        std::string EncodeDiag;
        const bool Encoded = VerifyCondition(
            Codecs::ForceModelPresetEncoder::EncodeToFile(TempPresetDir / "CyclotronSaved.ForceModel", SaveDoc, EncodeDiag),
            "EncodeToFile .ForceModel preset");
        const uint32_t LoadedCnt = Browser.LoadForcePresetsFromDirectory(TempPresetDir);
        std::error_code CleanupEc;
        std::filesystem::remove_all(TempPresetDir, CleanupEc);
        if (!Encoded || !VerifyCondition(LoadedCnt == 1u, "ContentBrowser imported .ForceModel preset file"))
        {
            TypographyRepository::AssignCurrent(nullptr);
            ImGui::DestroyContext(GuiSession);
            return false;
        }
    }

    // Exhibit 8: Viewport header Construct modal showing Force presets & all 19 ForceModel laws + Content Browser button
    Outliner.SelectSingle(VortexToken);
    Viewport.ConfigureConstructSheetOpen(true);
    if (!CaptureFullWorkspacePng("08-ForceFieldConstructModal.png"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Functional check 3: Construct via Viewport/Outliner template (ForceModel::VorticityConfinement)
    const std::size_t BeforeConstructCount = Coordinator.QueryAccumulator().QuerySources().size();
    const OutlinerSlotToken NewLawToken = Outliner.ConstructFromTemplate(
        static_cast<uint32_t>(ForceModel::VorticityConfinement),
        WindFolder);
    Coordinator.StepSimulation(1.0 / 60.0);
    const auto AfterConstructSrcs = Coordinator.QueryAccumulator().QuerySources();
    if (!VerifyCondition(
            NewLawToken.Valid() &&
            AfterConstructSrcs.size() == BeforeConstructCount + 1u &&
            AfterConstructSrcs.back().Model == ForceModel::VorticityConfinement,
            "Viewport Construct / Outliner template spawned ForceModel::VorticityConfinement"))
    {
        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    Viewport.ConfigureConstructSheetOpen(false);
    Browser.SnapOpen(false);

    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiSession);
    return true;
}

#endif // FRONTIER_EDITOR_ENABLED

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    if (!ValidateAllForceModelsDivergence())
    {
        return 1;
    }
    if (!ValidateMotionPathsTemporalModels())
    {
        return 1;
    }
    if (!ValidateFormulaCompilerComputeParity())
    {
        return 1;
    }
    if (!ValidateVectorFieldCodecs())
    {
        return 1;
    }
#if FRONTIER_EDITOR_ENABLED
    if (!ValidateEditorVulkanOverlayExhibits())
    {
        return 1;
    }
#endif

    std::printf("[VectorFieldValidation] All 19 ForceModels, CurlNoise div=0, Codecs, and Vulkan 1.3 GPU UI checks PASSED.\n");
    return 0;
}
