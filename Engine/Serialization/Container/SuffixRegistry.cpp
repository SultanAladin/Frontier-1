/*============================================================================================================================================
                                                           SUFFIXREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of the Frontier format suffix registry, *Space stratification check, and case-insensitive alias resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/SuffixRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace Frontier::Serialization
{

namespace
{

struct SuffixAliasEntry
{
    std::string_view     LowercaseAlias; // [-] - Legacy or draft lowercase suffix alias
    FormatSuffixCategory TargetCategory; // [-] - Canonical Frontier format ordinal
};

constexpr std::array<FormatSuffixDescriptor, 63u> RegisteredDescriptors = {{
    // Tier A — Foundational Top-Level Pillars (*Space ONLY)
    { FormatSuffixCategory::WorldSpace,           FormatPillarGroup::World,         ".WorldSpace",           1u, true,  false, true  },
    { FormatSuffixCategory::GeometrySpace,        FormatPillarGroup::Visual,        ".GeometrySpace",        1u, true,  false, true  },
    { FormatSuffixCategory::MaterialSpace,        FormatPillarGroup::Optics,        ".MaterialSpace",        1u, true,  false, true  },
    { FormatSuffixCategory::TextureSpace,         FormatPillarGroup::Visual,        ".TextureSpace",         1u, true,  false, true  },
    { FormatSuffixCategory::ImageSpace,           FormatPillarGroup::Visual,        ".ImageSpace",           1u, true,  false, true  },
    { FormatSuffixCategory::PixelSpace,           FormatPillarGroup::Visual,        ".PixelSpace",           1u, true,  false, true  },
    { FormatSuffixCategory::VectorSpace,          FormatPillarGroup::Visual,        ".VectorSpace",          1u, true,  false, true  },
    { FormatSuffixCategory::ColourSpace,          FormatPillarGroup::Visual,        ".ColourSpace",          1u, true,  false, true  },
    { FormatSuffixCategory::FontSpace,            FormatPillarGroup::Visual,        ".FontSpace",            1u, true,  false, true  },
    { FormatSuffixCategory::ParticleSpace,        FormatPillarGroup::Simulation,    ".ParticleSpace",        1u, true,  false, true  },
    { FormatSuffixCategory::PhysicsSpace,         FormatPillarGroup::Simulation,    ".PhysicsSpace",         1u, true,  false, true  },
    { FormatSuffixCategory::VolumetricSpace,      FormatPillarGroup::Simulation,    ".VolumetricSpace",      1u, true,  false, true  },
    { FormatSuffixCategory::AudioSpace,           FormatPillarGroup::Simulation,    ".AudioSpace",           1u, true,  false, true  },
    { FormatSuffixCategory::MotionSpace,          FormatPillarGroup::Logic,         ".MotionSpace",          1u, true,  false, true  },
    { FormatSuffixCategory::BehaviorSpace,        FormatPillarGroup::Logic,         ".BehaviorSpace",        1u, true,  false, true  },
    { FormatSuffixCategory::InterfaceSpace,       FormatPillarGroup::Interface,     ".InterfaceSpace",       1u, true,  false, true  },
    { FormatSuffixCategory::NetworkSpace,         FormatPillarGroup::Interface,     ".NetworkSpace",         1u, true,  false, false },
    { FormatSuffixCategory::InputSpace,           FormatPillarGroup::Interface,     ".InputSpace",           1u, true,  false, false },

    // Tier B — Specialized Sub-Formats (Never end in *Space)
    { FormatSuffixCategory::EngineSettings,       FormatPillarGroup::Configuration, ".EngineSettings",       1u, false, false, false },
    { FormatSuffixCategory::RuntimeConfiguration, FormatPillarGroup::Configuration, ".RuntimeConfiguration", 1u, false, false, false },
    { FormatSuffixCategory::PlatformSettings,     FormatPillarGroup::Configuration, ".PlatformSettings",     1u, false, false, false },
    { FormatSuffixCategory::ProjectIndex,         FormatPillarGroup::Configuration, ".ProjectIndex",         1u, false, false, false },
    { FormatSuffixCategory::GraphicsSettings,     FormatPillarGroup::Configuration, ".GraphicsSettings",     1u, false, false, false },
    { FormatSuffixCategory::UserProfile,          FormatPillarGroup::Configuration, ".UserProfile",          1u, false, false, false },
    { FormatSuffixCategory::WorkspaceLayout,      FormatPillarGroup::Interface,     ".WorkspaceLayout",      1u, false, true,  false },

    { FormatSuffixCategory::Instance,             FormatPillarGroup::World,         ".Instance",             1u, false, false, true  },
    { FormatSuffixCategory::Component,            FormatPillarGroup::World,         ".Component",            1u, false, false, true  },
    { FormatSuffixCategory::Terrain,              FormatPillarGroup::World,         ".Terrain",              1u, false, false, true  },

    { FormatSuffixCategory::SurfaceParameter,     FormatPillarGroup::Visual,        ".SurfaceParameter",     1u, false, false, true  },
    { FormatSuffixCategory::Sketch,               FormatPillarGroup::Visual,        ".Sketch",               1u, false, false, true  },
    { FormatSuffixCategory::ConvexHull,           FormatPillarGroup::Visual,        ".ConvexHull",           1u, false, false, true  },
    { FormatSuffixCategory::Pigment,              FormatPillarGroup::Visual,        ".Pigment",              1u, false, false, true  },

    { FormatSuffixCategory::ShaderNetwork,        FormatPillarGroup::Optics,        ".ShaderNetwork",        1u, false, false, true  },
    { FormatSuffixCategory::ComputeProgram,       FormatPillarGroup::Optics,        ".ComputeProgram",       1u, false, false, true  },
    { FormatSuffixCategory::SpectralCurve,        FormatPillarGroup::Optics,        ".SpectralCurve",        1u, false, false, true  },
    { FormatSuffixCategory::NoiseNetwork,         FormatPillarGroup::Optics,        ".NoiseNetwork",         1u, false, false, true  },
    { FormatSuffixCategory::Atmosphere,           FormatPillarGroup::Optics,        ".Atmosphere",           1u, false, false, true  },
    { FormatSuffixCategory::CameraOptics,         FormatPillarGroup::Optics,        ".CameraOptics",         1u, false, false, true  },
    { FormatSuffixCategory::PostStack,            FormatPillarGroup::Optics,        ".PostStack",            1u, false, false, true  },
    { FormatSuffixCategory::RenderSequence,       FormatPillarGroup::Optics,        ".RenderSequence",       1u, false, false, true  },
    { FormatSuffixCategory::IesProfile,           FormatPillarGroup::Optics,        ".IesProfile",           1u, false, false, true  },
    { FormatSuffixCategory::Photometry,           FormatPillarGroup::Optics,        ".Photometry",           1u, false, false, true  },
    { FormatSuffixCategory::Irradiance,           FormatPillarGroup::Optics,        ".Irradiance",           1u, false, false, true  },
    { FormatSuffixCategory::RadianceClipmap,      FormatPillarGroup::Optics,        ".RadianceClipmap",      1u, false, false, true  },
    { FormatSuffixCategory::RayStructure,         FormatPillarGroup::Optics,        ".RayStructure",         1u, false, false, true  },
    { FormatSuffixCategory::DistanceField,        FormatPillarGroup::Optics,        ".DistanceField",        1u, false, false, true  },

    { FormatSuffixCategory::FractureNetwork,      FormatPillarGroup::Simulation,    ".FractureNetwork",      1u, false, false, true  },
    { FormatSuffixCategory::ShardCluster,         FormatPillarGroup::Simulation,    ".ShardCluster",         1u, false, false, true  },
    { FormatSuffixCategory::ConstraintSet,        FormatPillarGroup::Simulation,    ".ConstraintSet",        1u, false, false, true  },
    { FormatSuffixCategory::ImpactProfile,        FormatPillarGroup::Simulation,    ".ImpactProfile",        1u, false, false, true  },
    { FormatSuffixCategory::ForceField,           FormatPillarGroup::Simulation,    ".ForceField",           1u, false, false, true  },
    { FormatSuffixCategory::FluidGrid,            FormatPillarGroup::Simulation,    ".FluidGrid",            1u, false, false, true  },
    { FormatSuffixCategory::SpawnSource,          FormatPillarGroup::Simulation,    ".SpawnSource",          1u, false, false, true  },
    { FormatSuffixCategory::RibbonTrail,          FormatPillarGroup::Simulation,    ".RibbonTrail",          1u, false, false, true  },
    { FormatSuffixCategory::SwarmBehavior,        FormatPillarGroup::Simulation,    ".SwarmBehavior",        1u, false, false, true  },
    { FormatSuffixCategory::Soundscape,           FormatPillarGroup::Simulation,    ".Soundscape",           1u, false, false, true  },

    { FormatSuffixCategory::TriggerZone,          FormatPillarGroup::Logic,         ".TriggerZone",          1u, false, false, true  },
    { FormatSuffixCategory::EventBus,             FormatPillarGroup::Logic,         ".EventBus",             1u, false, false, true  },
    { FormatSuffixCategory::VariableRegister,     FormatPillarGroup::Logic,         ".VariableRegister",     1u, false, false, true  },
    { FormatSuffixCategory::TimerProfile,         FormatPillarGroup::Logic,         ".TimerProfile",         1u, false, false, true  },
    { FormatSuffixCategory::Curve,                FormatPillarGroup::Logic,         ".Curve",                1u, false, false, true  },
    { FormatSuffixCategory::Timeline,             FormatPillarGroup::Logic,         ".Timeline",             1u, false, false, true  },
    { FormatSuffixCategory::Tooltip,              FormatPillarGroup::Interface,     ".Tooltip",              1u, false, false, true  },
}};

// 📝 Backward-compatible case-insensitive migration aliases for legacy Content/ files and older drafts
constexpr std::array<SuffixAliasEntry, 21u> LegacyAliasDirectory = {{
    { ".projectspace",          FormatSuffixCategory::ProjectIndex         },
    { ".componentspace",        FormatSuffixCategory::Component            },
    { ".renderspace",           FormatSuffixCategory::RenderSequence       },
    { ".render",                FormatSuffixCategory::RenderSequence       },
    { ".photometryspace",       FormatSuffixCategory::Photometry           },
    { ".surfaceparameterspace", FormatSuffixCategory::SurfaceParameter     },
    { ".uvspace",               FormatSuffixCategory::SurfaceParameter     },
    { ".instancespace",         FormatSuffixCategory::Instance             },
    { ".simulationspace",       FormatSuffixCategory::PhysicsSpace         },
    { ".motion",                FormatSuffixCategory::MotionSpace          },
    { ".acoustic",              FormatSuffixCategory::Soundscape           },
    { ".acousticspace",         FormatSuffixCategory::AudioSpace           },
    { ".interface",             FormatSuffixCategory::InterfaceSpace       },
    { ".editorlayout",          FormatSuffixCategory::WorkspaceLayout      },
    { ".skyatmosphere",         FormatSuffixCategory::Atmosphere           },
    { ".lensaperture",          FormatSuffixCategory::CameraOptics         },
    { ".vectorfield",           FormatSuffixCategory::ForceField           },
    { ".engineconfiguration",   FormatSuffixCategory::EngineSettings       },
    { ".pixelsheet",            FormatSuffixCategory::PixelSpace           },
    { ".logicspace",            FormatSuffixCategory::BehaviorSpace        },
    { ".directivespace",        FormatSuffixCategory::BehaviorSpace        },
}};

[[nodiscard]] std::string NormalizeLowercaseSuffix(std::string_view RawSuffix)
{
    std::string Lowered{};
    Lowered.reserve(RawSuffix.size() + 1u);
    if (!RawSuffix.empty() && RawSuffix.front() != '.')
    {
        Lowered.push_back('.');
    }
    for (const unsigned char Character : RawSuffix)
    {
        if (!std::isspace(Character))
        {
            Lowered.push_back(static_cast<char>(std::tolower(Character)));
        }
    }
    return Lowered;
}

[[nodiscard]] bool QueryEndsWithSpace(std::string_view Suffix) noexcept
{
    constexpr std::string_view SpaceTag = "Space";
    if (Suffix.size() < SpaceTag.size())
    {
        return false;
    }
    return Suffix.substr(Suffix.size() - SpaceTag.size()) == SpaceTag;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              REGISTRY QUERY FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::span<const FormatSuffixDescriptor> EnumerateFormatSuffixDescriptors() noexcept
{
    return RegisteredDescriptors;
}

const FormatSuffixDescriptor& InspectFormatSuffixDescriptor(FormatSuffixCategory Category) noexcept
{
    const std::size_t Ordinal = static_cast<std::size_t>(Category);
    if (Ordinal < RegisteredDescriptors.size())
    {
        return RegisteredDescriptors[Ordinal];
    }
    return RegisteredDescriptors[0u];
}

std::string_view QueryCanonicalSuffix(FormatSuffixCategory Category) noexcept
{
    return InspectFormatSuffixDescriptor(Category).CanonicalSuffix;
}

bool QueryFoundationalPillar(FormatSuffixCategory Category) noexcept
{
    return InspectFormatSuffixDescriptor(Category).FoundationalPillar;
}

bool QueryEditorOnlyFormat(FormatSuffixCategory Category) noexcept
{
    return InspectFormatSuffixDescriptor(Category).EditorOnly;
}

bool ResolveSuffixCaseInsensitive(
    std::string_view      RawSuffix,
    FormatSuffixCategory& OutCategory,
    std::string&          OutCanonicalSuffix,
    std::string&          OutDiagnostic)
{
    const std::string Lowered = NormalizeLowercaseSuffix(RawSuffix);
    if (Lowered.size() < 2u)
    {
        OutDiagnostic = "Empty or malformed format suffix.";
        return false;
    }

    // ① Exact canonical case-insensitive match first
    for (const FormatSuffixDescriptor& Descriptor : RegisteredDescriptors)
    {
        if (NormalizeLowercaseSuffix(Descriptor.CanonicalSuffix) == Lowered)
        {
            OutCategory        = Descriptor.Category;
            OutCanonicalSuffix = std::string(Descriptor.CanonicalSuffix);
            OutDiagnostic.clear();
            return true;
        }
    }

    // ② Explicit backward-compatible legacy alias resolution
    for (const SuffixAliasEntry& Alias : LegacyAliasDirectory)
    {
        if (Alias.LowercaseAlias == Lowered)
        {
            const FormatSuffixDescriptor& Target = InspectFormatSuffixDescriptor(Alias.TargetCategory);
            OutCategory        = Target.Category;
            OutCanonicalSuffix = std::string(Target.CanonicalSuffix);
            OutDiagnostic.clear();
            return true;
        }
    }

    OutDiagnostic = "Unregistered Frontier format suffix: " + std::string(RawSuffix);
    return false;
}

bool VerifySpaceStratificationRule(std::string& OutDiagnostic)
{
    for (std::size_t Ordinal = 0u; Ordinal < RegisteredDescriptors.size(); ++Ordinal)
    {
        const FormatSuffixDescriptor& Entry = RegisteredDescriptors[Ordinal];
        if (static_cast<std::size_t>(Entry.Category) != Ordinal)
        {
            OutDiagnostic = "Suffix ordinal mismatch at index " + std::to_string(Ordinal);
            return false;
        }
        const bool HasSpaceSuffix = QueryEndsWithSpace(Entry.CanonicalSuffix);
        if (Entry.FoundationalPillar && !HasSpaceSuffix)
        {
            OutDiagnostic = "Foundational pillar missing *Space suffix: " + std::string(Entry.CanonicalSuffix);
            return false;
        }
        if (!Entry.FoundationalPillar && HasSpaceSuffix)
        {
            OutDiagnostic = "Specialized sub-format illegally ends with *Space: " + std::string(Entry.CanonicalSuffix);
            return false;
        }
    }
    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::Serialization
