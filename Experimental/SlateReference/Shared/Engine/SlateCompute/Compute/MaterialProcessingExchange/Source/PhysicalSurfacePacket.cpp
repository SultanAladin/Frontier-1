#include "SlateCompute/Compute/MaterialProcessingExchange/Api/PhysicalSurfacePacket.h"

#include <limits>

namespace Slate
{
namespace
{
bool Finite(double Value)
{
    return Value > -std::numeric_limits<double>::infinity() && Value < std::numeric_limits<double>::infinity();
}

void CopyColour(float Destination[4], const ColourSpecification& Source)
{
    Destination[0] = static_cast<float>(Source.RedCoordinate);
    Destination[1] = static_cast<float>(Source.GreenCoordinate);
    Destination[2] = static_cast<float>(Source.BlueCoordinate);
    Destination[3] = 1.0f;
}
}

Deliver<PhysicalSurfacePacket> BindPhysicalSurface(const CompiledPhysicalSurface& Compiled)
{
    if (!Finite(Compiled.Metallic) || !Finite(Compiled.Roughness) || !Finite(Compiled.Opacity) ||
        !Finite(Compiled.Transmission) || !Finite(Compiled.RefractionRatio) ||
        !Compiled.Albedo.ColourDeclared() || !Compiled.NormalIncidenceReflectance.ColourDeclared())
    {
        return Deliver<PhysicalSurfacePacket>::Refuse(
            { RefusalReason::ContentUnsupported, "the compiled physical surface cannot be represented by finite GPU constants" });
    }

    PhysicalSurfacePacket Bound;
    CopyColour(Bound.Albedo, Compiled.Albedo);
    CopyColour(Bound.NormalIncidenceReflectance, Compiled.NormalIncidenceReflectance);
    Bound.Scalars[0] = static_cast<float>(Compiled.Metallic);
    Bound.Scalars[1] = static_cast<float>(Compiled.Roughness);
    Bound.Scalars[2] = static_cast<float>(Compiled.Opacity);
    Bound.Scalars[3] = static_cast<float>(Compiled.Transmission);
    Bound.RefractionRatio = static_cast<float>(Compiled.RefractionRatio);
    Bound.ActiveChannelMask = Compiled.ActiveChannelMask;
    Bound.Closure = static_cast<std::uint32_t>(Compiled.Declaration.Closure);
    Bound.Features = Compiled.Declaration.Features;
    Bound.Coverage = static_cast<std::uint32_t>(Compiled.Declaration.Coverage);
    Bound.Wall = static_cast<std::uint32_t>(Compiled.Declaration.Wall);
    Bound.Interface = static_cast<std::uint32_t>(Compiled.Declaration.Interface);
    Bound.TwoSided = Compiled.Declaration.TwoSided ? 1u : 0u;
    return Deliver<PhysicalSurfacePacket>::Result(Bound);
}

} // namespace Slate
