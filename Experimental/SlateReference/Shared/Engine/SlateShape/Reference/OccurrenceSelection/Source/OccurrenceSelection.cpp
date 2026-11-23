//============================================================================================================================================
//                                                   OCCURRENCESELECTION.CPP
//============================================================================================================================================

#include "SlateShape/Reference/OccurrenceSelection/Api/OccurrenceSelection.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    SpatialPoint Rotate(const RotationQuaternion& Rotation,
                        const SpatialPoint& Position)
    {
        const double X = Rotation.ImaginaryX;
        const double Y = Rotation.ImaginaryY;
        const double Z = Rotation.ImaginaryZ;
        const double W = Rotation.Real;

        const double DotTwo = 2.0 * (X * Position.Left + Y * Position.Up + Z * Position.Forward);
        const double CrossX = 2.0 * (Y * Position.Forward - Z * Position.Up);
        const double CrossY = 2.0 * (Z * Position.Left - X * Position.Forward);
        const double CrossZ = 2.0 * (X * Position.Up - Y * Position.Left);

        return {
            Position.Left + W * CrossX + (Y * CrossZ - Z * CrossY) + DotTwo * X,
            Position.Up + W * CrossY + (Z * CrossX - X * CrossZ) + DotTwo * Y,
            Position.Forward + W * CrossZ + (X * CrossY - Y * CrossX) + DotTwo * Z
        };
    }

    SpatialPoint ProjectPoint(const DecomposedTransform& Placement,
                              const SpatialPoint& Local)
    {
        const SpatialPoint Scaled = {
            Local.Left * Placement.ScaleX,
            Local.Up * Placement.ScaleY,
            Local.Forward * Placement.ScaleZ
        };
        const SpatialPoint Rotated = Rotate(Placement.Rotation, Scaled);
        return {
            Rotated.Left + Placement.Translation.PositionX,
            Rotated.Up + Placement.Translation.PositionY,
            Rotated.Forward + Placement.Translation.PositionZ
        };
    }

    RotationQuaternion Inverse(const RotationQuaternion& Rotation)
    {
        return { -Rotation.ImaginaryX, -Rotation.ImaginaryY, -Rotation.ImaginaryZ, Rotation.Real };
    }

    SpatialPoint UnprojectPoint(const DecomposedTransform& Placement,
                                const SpatialPoint& World)
    {
        const SpatialPoint Shifted = {
            World.Left - Placement.Translation.PositionX,
            World.Up - Placement.Translation.PositionY,
            World.Forward - Placement.Translation.PositionZ
        };
        const SpatialPoint Rotated = Rotate(Inverse(Placement.Rotation), Shifted);
        return {
            Placement.ScaleX != 0.0 ? Rotated.Left / Placement.ScaleX : Rotated.Left,
            Placement.ScaleY != 0.0 ? Rotated.Up / Placement.ScaleY : Rotated.Up,
            Placement.ScaleZ != 0.0 ? Rotated.Forward / Placement.ScaleZ : Rotated.Forward
        };
    }

    const SolidStructure* ResolveSolid(const std::vector<const SolidStructure*>& Solids,
                                       SolidNameInFeature Solid)
    {
        if (!Solid.Assigned() || Solid.IssuedIndex == 0u || Solid.IssuedIndex > Solids.size())
            return nullptr;
        return Solids[Solid.IssuedIndex - 1u];
    }
}

bool ResolveNearestOccurrenceVertex(const OccurrenceStructure& Occurrences,
                                    const std::vector<const SolidStructure*>& Solids,
                                    const SpatialPoint& Probe,
                                    double MaximumDistance,
                                    OccurrenceVertexPlacement& Resolved,
                                    double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    for (std::uint32_t OccurrenceIndex = 1u; OccurrenceIndex <= Occurrences.DeclaredCount(); ++OccurrenceIndex)
    {
        const DeclaredOccurrence* Occurrence = Occurrences.Resolve({ OccurrenceIndex });
        if (Occurrence == nullptr || Occurrence->Subject != OccurrenceSubject::Solid)
            continue;
        const SolidStructure* Solid = ResolveSolid(Solids, Occurrence->Solid);
        if (Solid == nullptr)
            continue;

        VertexName Local = {};
        double LocalDistance = MaximumDistance;
        if (!ResolveNearestVertex(*Solid, UnprojectPoint(Occurrence->Placement, Probe), MaximumDistance, Local, LocalDistance))
            continue;

        const SolidView View = Solid->Resolve();
        if (View.Vertices == nullptr || !Local.Assigned() || Local.IssuedIndex > View.Vertices->size())
            continue;
        const SpatialPoint World = ProjectPoint(Occurrence->Placement, (*View.Vertices)[Local.IssuedIndex - 1u].Position);
        const double WorldDistance = std::sqrt((World.Left - Probe.Left) * (World.Left - Probe.Left)
                                             + (World.Up - Probe.Up) * (World.Up - Probe.Up)
                                             + (World.Forward - Probe.Forward) * (World.Forward - Probe.Forward));
        if (WorldDistance <= Distance)
        {
            Distance = WorldDistance;
            Resolved = { { OccurrenceIndex }, Local };
            ResolvedAny = true;
        }
    }

    return ResolvedAny;
}

bool ResolveNearestOccurrenceEdgeSpan(const OccurrenceStructure& Occurrences,
                                      const std::vector<const SolidStructure*>& Solids,
                                      const SpatialPoint& Probe,
                                      double MaximumDistance,
                                      OccurrenceEdgeSpanPlacement& Resolved,
                                      double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    for (std::uint32_t OccurrenceIndex = 1u; OccurrenceIndex <= Occurrences.DeclaredCount(); ++OccurrenceIndex)
    {
        const DeclaredOccurrence* Occurrence = Occurrences.Resolve({ OccurrenceIndex });
        if (Occurrence == nullptr || Occurrence->Subject != OccurrenceSubject::Solid)
            continue;
        const SolidStructure* Solid = ResolveSolid(Solids, Occurrence->Solid);
        if (Solid == nullptr)
            continue;

        EdgeSpanPlacement Local = {};
        if (!ResolveNearestEdgeSpan(*Solid, UnprojectPoint(Occurrence->Placement, Probe), MaximumDistance, Local, Distance))
            continue;

        Resolved = { { OccurrenceIndex }, Local };
        ResolvedAny = true;
    }

    return ResolvedAny;
}

bool ResolveNearestOccurrenceFace(const OccurrenceStructure& Occurrences,
                                  const std::vector<const SolidStructure*>& Solids,
                                  const SpatialPoint& Probe,
                                  double MaximumDistance,
                                  OccurrenceFacePlacement& Resolved,
                                  double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    for (std::uint32_t OccurrenceIndex = 1u; OccurrenceIndex <= Occurrences.DeclaredCount(); ++OccurrenceIndex)
    {
        const DeclaredOccurrence* Occurrence = Occurrences.Resolve({ OccurrenceIndex });
        if (Occurrence == nullptr || Occurrence->Subject != OccurrenceSubject::Solid)
            continue;
        const SolidStructure* Solid = ResolveSolid(Solids, Occurrence->Solid);
        if (Solid == nullptr)
            continue;

        FaceName Local = {};
        double LocalDistance = MaximumDistance;
        if (!ResolveNearestFace(*Solid, UnprojectPoint(Occurrence->Placement, Probe), MaximumDistance, Local, LocalDistance))
            continue;

        if (LocalDistance <= Distance)
        {
            Distance = LocalDistance;
            Resolved = { { OccurrenceIndex }, Local };
            ResolvedAny = true;
        }
    }

    return ResolvedAny;
}

bool ResolveNearestOccurrenceSolid(const OccurrenceStructure& Occurrences,
                                   const std::vector<const SolidStructure*>& Solids,
                                   const SpatialPoint& Probe,
                                   double MaximumDistance,
                                   OccurrenceNameInFeature& Resolved,
                                   double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    for (std::uint32_t OccurrenceIndex = 1u; OccurrenceIndex <= Occurrences.DeclaredCount(); ++OccurrenceIndex)
    {
        const DeclaredOccurrence* Occurrence = Occurrences.Resolve({ OccurrenceIndex });
        if (Occurrence == nullptr || Occurrence->Subject != OccurrenceSubject::Solid)
            continue;
        const SolidStructure* Solid = ResolveSolid(Solids, Occurrence->Solid);
        if (Solid == nullptr)
            continue;

        SolidName Local = {};
        double LocalDistance = MaximumDistance;
        if (!ResolveNearestSolid(*Solid, UnprojectPoint(Occurrence->Placement, Probe), MaximumDistance, Local, LocalDistance))
            continue;

        if (LocalDistance <= Distance)
        {
            Distance = LocalDistance;
            Resolved = { OccurrenceIndex };
            ResolvedAny = true;
        }
    }

    return ResolvedAny;
}

} // namespace Slate
