//============================================================================================================================================
//                                                PHYSICALSURFACESPECIFICATION.CPP
//============================================================================================================================================

#include "SlateDocument/Document/MaterialSpecification/Api/PhysicalSurfaceSpecification.h"

#include <cmath>

namespace Slate
{
namespace
{
constexpr std::uint32_t Bit(ChannelSubject Subject)
{
    return 1u << static_cast<std::uint32_t>(Subject);
}

double Scalar(const MaterialSpecification& Material, ChannelSubject Subject, double Fallback)
{
    const ChannelSpecification& Channel = Material.Channel(Subject);
    if (!Channel.ChannelDeclared || Channel.Measured != ChannelMeasure::Scalar) return Fallback;
    return Channel.Source == ChannelSource::Constant ? Channel.ConstantScalar : Channel.DefaultScalar;
}

ColourSpecification Colour(const MaterialSpecification& Material, ChannelSubject Subject, ColourSpecification Fallback)
{
    const ChannelSpecification& Channel = Material.Channel(Subject);
    if (!Channel.ChannelDeclared || Channel.Measured != ChannelMeasure::Reflectance) return Fallback;
    return Channel.Source == ChannelSource::Constant ? Channel.ConstantColour : Channel.DefaultColour;
}
}

Deliver<CompiledPhysicalSurface> PhysicalSurfaceExchange::Compile(const MaterialSpecification& Material,
                                                                   const PhysicalSurfaceDeclaration& Declaration) const
{
    if (Declaration.Closure > SurfaceClosureSelection::Matcap ||
        Declaration.Coverage > CoverageSelection::Blended || Declaration.Wall > WallSelection::ThinWalled ||
        Declaration.Interface > InterfaceParameterization::DirectF0 ||
        (Declaration.Features & ~((1u << 6u) - 1u)) != 0u)
    {
        return Deliver<CompiledPhysicalSurface>::Refuse(
            { RefusalReason::ContentUnsupported, "the physical surface declaration contains an unsupported selection" });
    }

    CompiledPhysicalSurface Produced;
    Produced.Declaration = Declaration;
    Produced.Albedo = Colour(Material, ChannelSubject::AlbedoColour, Produced.Albedo);
    Produced.Metallic = Scalar(Material, ChannelSubject::Metallic, 0.0);
    Produced.Roughness = Scalar(Material, ChannelSubject::Roughness, 0.5);
    Produced.Opacity = Scalar(Material, ChannelSubject::Opacity, 1.0);
    Produced.Transmission = Scalar(Material, ChannelSubject::Transmission, 0.0);
    Produced.RefractionRatio = Scalar(Material, ChannelSubject::RefractionRatio, 1.5);

    if (Produced.Metallic < 0.0 || Produced.Metallic > 1.0 || Produced.Roughness < 0.0 ||
        Produced.Roughness > 1.0 || Produced.Opacity < 0.0 || Produced.Opacity > 1.0 ||
        Produced.Transmission < 0.0 || Produced.Transmission > 1.0 || Produced.RefractionRatio <= 1.0)
    {
        return Deliver<CompiledPhysicalSurface>::Refuse(
            { RefusalReason::ContentUnsupported, "a compiled physical material constant is outside its declared range" });
    }

    if (Declaration.Interface == InterfaceParameterization::DirectF0)
        Produced.NormalIncidenceReflectance = Colour(Material, ChannelSubject::NormalIncidenceReflectance,
                                                      Produced.NormalIncidenceReflectance);
    else
    {
        const double F0 = std::pow((Produced.RefractionRatio - 1.0) / (Produced.RefractionRatio + 1.0), 2.0);
        Produced.NormalIncidenceReflectance = { F0, F0, F0, WorkingSpaceIdentity };
    }

    const ChannelSubject Base[] = { ChannelSubject::AlbedoColour, ChannelSubject::Metallic,
        ChannelSubject::Roughness, ChannelSubject::Opacity, ChannelSubject::Transmission,
        ChannelSubject::RefractionRatio };
    for (ChannelSubject Subject : Base) if (Material.Channel(Subject).ChannelDeclared) Produced.ActiveChannelMask |= Bit(Subject);
    if ((Declaration.Features & static_cast<std::uint32_t>(PhysicalFeature::Anisotropy)) != 0u)
        Produced.ActiveChannelMask |= Bit(ChannelSubject::Anisotropy) | Bit(ChannelSubject::AnisotropyDirection);
    if ((Declaration.Features & static_cast<std::uint32_t>(PhysicalFeature::Coat)) != 0u)
        Produced.ActiveChannelMask |= Bit(ChannelSubject::ClearCoat) | Bit(ChannelSubject::ClearCoatRoughness);

    return Deliver<CompiledPhysicalSurface>::Result(Produced);
}

} // namespace Slate
