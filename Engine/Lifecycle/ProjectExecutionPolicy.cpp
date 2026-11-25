/*============================================================================================================================================
                                                       PROJECTEXECUTIONPOLICY.CPP
============================================================================================================================================*/
// 🧩 Implementation of declarative ProjectExecutionPolicy boot mapping and DocumentContainerCodec serialization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/ProjectExecutionPolicy.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                POLICY CODEC IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void ProjectExecutionPolicyCodec::ClampToSafeBounds(ProjectExecutionPolicy& Policy) noexcept
{
    if (!std::isfinite(Policy.FixedStepSeconds) || Policy.FixedStepSeconds < 0.001)
    {
        Policy.FixedStepSeconds = 1.0 / 60.0;
    }
    Policy.FixedStepSeconds = std::clamp(Policy.FixedStepSeconds, 1.0 / 480.0, 0.100);

    if (!std::isfinite(Policy.MaximumStepSeconds) || Policy.MaximumStepSeconds < Policy.FixedStepSeconds)
    {
        Policy.MaximumStepSeconds = std::max(0.100, Policy.FixedStepSeconds * 4.0);
    }
    Policy.MaximumStepSeconds = std::clamp(Policy.MaximumStepSeconds, Policy.FixedStepSeconds, 0.250);

    Policy.MaximumSubstepsPerTick = std::clamp(Policy.MaximumSubstepsPerTick, 1u, 16u);
    Policy.WorkerChannelCount     = std::clamp(Policy.WorkerChannelCount, 0u, 32u);
    Policy.WindowWidthPixels      = std::clamp(Policy.WindowWidthPixels, 160u, 7680u);
    Policy.WindowHeightPixels     = std::clamp(Policy.WindowHeightPixels, 90u, 4320u);

    if (!std::isfinite(Policy.TargetStepBudgetMs) || Policy.TargetStepBudgetMs <= 0.0)
    {
        Policy.TargetStepBudgetMs = Policy.FixedStepSeconds * 1000.0;
    }
    Policy.TargetStepBudgetMs = std::clamp(Policy.TargetStepBudgetMs, 1.0, 250.0);

    if (Policy.ProjectTitle.empty())
    {
        Policy.ProjectTitle = "Untitled";
    }
}

ProjectExecutionPolicy ProjectExecutionPolicyCodec::ConstructFromBootConfiguration(
    const Serialization::ResolvedBootConfiguration& BootConfig) noexcept
{
    ProjectExecutionPolicy Policy{};
    Policy.ProjectTitle = !BootConfig.ProjectIndex.ProjectName.empty()
                              ? BootConfig.ProjectIndex.ProjectName
                              : BootConfig.RuntimeConfiguration.ApplicationTitle;

    const uint32_t TargetHz = BootConfig.RuntimeConfiguration.TargetFramesPerSecond > 0u
                                  ? BootConfig.RuntimeConfiguration.TargetFramesPerSecond
                                  : 60u;
    Policy.FixedStepSeconds   = 1.0 / static_cast<double>(std::clamp(TargetHz, 15u, 480u));
    Policy.TargetStepBudgetMs = Policy.FixedStepSeconds * 1000.0;
    Policy.WindowWidthPixels  = BootConfig.RuntimeConfiguration.WindowWidthPixels;
    Policy.WindowHeightPixels = BootConfig.RuntimeConfiguration.WindowHeightPixels;
    Policy.EnableVerticalSync = BootConfig.RuntimeConfiguration.EnableVerticalSync;
    Policy.WorkerChannelCount = BootConfig.PlatformSettings.WorkerThreadCap > 0u
                                    ? BootConfig.PlatformSettings.WorkerThreadCap
                                    : 2u;
    Policy.EnabledDomains = RuntimeDomainMask::WorkerChannels | RuntimeDomainMask::AssetStreaming;

    ClampToSafeBounds(Policy);
    return Policy;
}

bool ProjectExecutionPolicyCodec::EncodeToDocument(
    const ProjectExecutionPolicy&     Policy,
    Serialization::DocumentContainer& OutDocument)
{
    ProjectExecutionPolicy Clamped = Policy;
    ClampToSafeBounds(Clamped);

    OutDocument                    = {};
    OutDocument.SuffixCategory     = Serialization::FormatSuffixCategory::RuntimeConfiguration;
    OutDocument.DocumentTitle      = Clamped.ProjectTitle + "ExecutionPolicy";
    OutDocument.AuthoringWorkspace = "Frontier.Lifecycle";
    OutDocument.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000301";
    OutDocument.SchemaVersion      = Clamped.SchemaVersion;

    toml::table Section{};
    Section.insert("SchemaVersion",          static_cast<int64_t>(Clamped.SchemaVersion));
    Section.insert("ProjectTitle",           Clamped.ProjectTitle);
    Section.insert("FixedStepSeconds",       Clamped.FixedStepSeconds);
    Section.insert("MaximumStepSeconds",     Clamped.MaximumStepSeconds);
    Section.insert("MaximumSubstepsPerTick", static_cast<int64_t>(Clamped.MaximumSubstepsPerTick));
    Section.insert("TargetStepBudgetMs",     Clamped.TargetStepBudgetMs);
    Section.insert("WorkerChannelCount",     static_cast<int64_t>(Clamped.WorkerChannelCount));
    Section.insert("WindowWidthPixels",      static_cast<int64_t>(Clamped.WindowWidthPixels));
    Section.insert("WindowHeightPixels",     static_cast<int64_t>(Clamped.WindowHeightPixels));
    Section.insert("PreferHeadlessWindow",   Clamped.PreferHeadlessWindow);
    Section.insert("SuspendWhenMinimized",   Clamped.SuspendWhenMinimized);
    Section.insert("EnableVerticalSync",     Clamped.EnableVerticalSync);
    Section.insert("EnabledDomainsMask",     static_cast<int64_t>(static_cast<uint32_t>(Clamped.EnabledDomains)));

    OutDocument.TomlHeader.insert("ExecutionPolicy", std::move(Section));
    return true;
}

bool ProjectExecutionPolicyCodec::DecodeFromDocument(
    const Serialization::DocumentContainer& Document,
    ProjectExecutionPolicy&                 OutPolicy,
    std::string&                            OutDiagnostic)
{
    OutPolicy = {};
    const auto* Section = Document.TomlHeader["ExecutionPolicy"].as_table();
    if (Section == nullptr)
    {
        OutDiagnostic = "Missing [ExecutionPolicy] section in ProjectExecutionPolicy document.";
        return false;
    }

    OutPolicy.SchemaVersion          = static_cast<uint32_t>((*Section)["SchemaVersion"].value_or<int64_t>(1));
    OutPolicy.ProjectTitle           = (*Section)["ProjectTitle"].value_or(std::string("Untitled"));
    OutPolicy.FixedStepSeconds       = (*Section)["FixedStepSeconds"].value_or<double>(1.0 / 60.0);
    OutPolicy.MaximumStepSeconds     = (*Section)["MaximumStepSeconds"].value_or<double>(0.100);
    OutPolicy.MaximumSubstepsPerTick = static_cast<uint32_t>((*Section)["MaximumSubstepsPerTick"].value_or<int64_t>(4));
    OutPolicy.TargetStepBudgetMs     = (*Section)["TargetStepBudgetMs"].value_or<double>(16.6667);
    OutPolicy.WorkerChannelCount     = static_cast<uint32_t>((*Section)["WorkerChannelCount"].value_or<int64_t>(2));
    OutPolicy.WindowWidthPixels      = static_cast<uint32_t>((*Section)["WindowWidthPixels"].value_or<int64_t>(1280));
    OutPolicy.WindowHeightPixels     = static_cast<uint32_t>((*Section)["WindowHeightPixels"].value_or<int64_t>(720));
    OutPolicy.PreferHeadlessWindow   = (*Section)["PreferHeadlessWindow"].value_or<bool>(true);
    OutPolicy.SuspendWhenMinimized   = (*Section)["SuspendWhenMinimized"].value_or<bool>(true);
    OutPolicy.EnableVerticalSync     = (*Section)["EnableVerticalSync"].value_or<bool>(true);
    OutPolicy.EnabledDomains         = static_cast<RuntimeDomainMask>(
        static_cast<uint32_t>((*Section)["EnabledDomainsMask"].value_or<int64_t>(3)));

    ClampToSafeBounds(OutPolicy);
    OutDiagnostic.clear();
    return true;
}

bool ProjectExecutionPolicyCodec::SaveToFile(
    const std::filesystem::path&  TargetPath,
    const ProjectExecutionPolicy& Policy,
    std::string&                  OutDiagnostic)
{
    Serialization::DocumentContainer Doc{};
    if (!EncodeToDocument(Policy, Doc))
    {
        OutDiagnostic = "Failed to encode ProjectExecutionPolicy.";
        return false;
    }
    return Serialization::DocumentContainerEncoder::EncodeToFile(TargetPath, Doc, OutDiagnostic);
}

bool ProjectExecutionPolicyCodec::LoadFromFile(
    const std::filesystem::path& SourcePath,
    ProjectExecutionPolicy&      OutPolicy,
    std::string&                 OutDiagnostic)
{
    Serialization::DocumentContainer Doc{};
    if (!Serialization::DocumentContainerDecoder::DecodeFromFile(SourcePath, Doc, OutDiagnostic))
    {
        return false;
    }
    return DecodeFromDocument(Doc, OutPolicy, OutDiagnostic);
}

} // namespace Frontier::Lifecycle
