//============================================================================================================================================
//                                                         ILLUMINANTPOPULATION.CPP
//============================================================================================================================================
// 🧩 Size validation, the single atmospheric source, incidence projection, and the reach index.

#include "SlateDocument/Document/IlluminantPopulation/Api/IlluminantPopulation.h"

#include "Shared/IntersectionClassifier.slang.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    IDENTITY ORDER
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Ordered by slot and then by generation, so the ordering is total over identities and stable across a slot
//    being reused. `44` §6's determinism requirement is satisfied by the storage rather than by a sort at read.
bool PrecedesInIdentity(OwnerIdentity Earlier, OwnerIdentity Later)
{
    if (Earlier.SlotIndex != Later.SlotIndex)
        return Earlier.SlotIndex < Later.SlotIndex;

    return Earlier.SlotGeneration < Later.SlotGeneration;
}

void EmissionDirection(RotationQuaternion Rotation, double& OutX, double& OutY, double& OutZ)
{
    // 📐 An illuminant emits along its own negative third axis, matching `46`'s camera convention. One convention
    //    across both means a spot illuminant parented to a camera points where the camera looks.
    const double SpanX = 0.0;
    const double SpanY = 0.0;
    const double SpanZ = -1.0;

    const double CrossX = Rotation.ImaginaryY * SpanZ - Rotation.ImaginaryZ * SpanY;
    const double CrossY = Rotation.ImaginaryZ * SpanX - Rotation.ImaginaryX * SpanZ;
    const double CrossZ = Rotation.ImaginaryX * SpanY - Rotation.ImaginaryY * SpanX;

    const double SecondX = Rotation.ImaginaryY * CrossZ - Rotation.ImaginaryZ * CrossY;
    const double SecondY = Rotation.ImaginaryZ * CrossX - Rotation.ImaginaryX * CrossZ;
    const double SecondZ = Rotation.ImaginaryX * CrossY - Rotation.ImaginaryY * CrossX;

    OutX = SpanX + 2.0 * (Rotation.Real * CrossX + SecondX);
    OutY = SpanY + 2.0 * (Rotation.Real * CrossY + SecondY);
    OutZ = SpanZ + 2.0 * (Rotation.Real * CrossZ + SecondZ);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     VALIDATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> IlluminantPopulation::Validate(const IlluminantSpecification& Declaring,
                                             OwnerIdentity               Subject) const
{
    if (Declaring.ExtentReach <= 0.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the declared extent reaches nothing" });
    }

    // 🔴 `44` §3: every emission shape has a non-zero size. A zero-extent source is not a smaller source, it is a
    //    source `18` §4's distribution cannot integrate — the highlight collapses to a single aliased pixel or
    //    disappears entirely depending on the roughness, and neither reads as a lighting decision.
    switch (Declaring.Emission)
    {
        case EmissionShape::Point:
        case EmissionShape::Spot:
        {
            if (Declaring.EmissionRadius <= 0.0)
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the emission radius is zero" });

            if (Declaring.Emission == EmissionShape::Spot
             && (Declaring.ConeAngle <= 0.0 || Declaring.ConeAngle >= 180.0))
            {
                return Deliver<bool>::Refuse(
                    { RefusalReason::ContentUnsupported, "the cone angle encloses no direction" });
            }

            break;
        }

        case EmissionShape::Directional:
        {
            if (Declaring.AngularSize <= 0.0)
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the angular size is zero" });

            break;
        }

        case EmissionShape::Extended:
        {
            if (Declaring.ExtendedWidth <= 0.0 || Declaring.ExtendedHeight <= 0.0)
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the extended shape has no area" });

            break;
        }

        case EmissionShape::ShapeCount:
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such emission shape" });
    }

    // 🔴 `36` §1: a colour without its space is rejected rather than assumed to be in the working space. Where a
    //    temperature is declared instead there is no coordinate yet, so nothing is compared.
    if (!Declaring.TemperatureDeclared && !Declaring.DeclaredColour.ColourDeclared())
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the illuminant declares neither a colour space nor a temperature" });
    }

    if (!Declaring.AtmosphericSource)
        return Deliver<bool>::Result(true);

    for (std::size_t Index = 0u; Index < Declarations.size(); ++Index)
    {
        if (!Declarations[Index].AtmosphericSource)
            continue;

        if (RegisteredOrder[Index] == Subject)
            continue;

        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the document already registers an atmospheric source" });
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECLARATION
//------------------------------------------------------------------------------------------------------------------------

std::size_t IlluminantPopulation::Located(OwnerIdentity Subject) const
{
    std::size_t Lower = 0u;
    std::size_t Upper = RegisteredOrder.size();

    while (Lower < Upper)
    {
        const std::size_t Middle = Lower + (Upper - Lower) / 2u;

        if (PrecedesInIdentity(RegisteredOrder[Middle], Subject))
            Lower = Middle + 1u;
        else
            Upper = Middle;
    }

    return Lower;
}

Deliver<bool> IlluminantPopulation::Declare(OwnerIdentity Subject, const IlluminantSpecification& Declaring)
{
    if (!Subject.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity lights nothing" });

    const Deliver<bool> Validated = Validate(Declaring, Subject);

    if (!Validated.Resolved)
        return Validated;

    const std::size_t Located_ = Located(Subject);

    if (Located_ < RegisteredOrder.size() && RegisteredOrder[Located_] == Subject)
    {
        Declarations[Located_] = Declaring;
    }
    else
    {
        RegisteredOrder.insert(RegisteredOrder.begin() + static_cast<std::ptrdiff_t>(Located_), Subject);
        Declarations.insert(Declarations.begin() + static_cast<std::ptrdiff_t>(Located_), Declaring);
    }

    ++DeclaredRevision;

    return Deliver<bool>::Result(true);
}

Deliver<bool> IlluminantPopulation::Amend(OwnerIdentity Subject, const IlluminantSpecification& Amending)
{
    const std::size_t Located_ = Located(Subject);

    if (Located_ >= RegisteredOrder.size() || !(RegisteredOrder[Located_] == Subject))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the owner declares no illuminant" });

    const Deliver<bool> Validated = Validate(Amending, Subject);

    if (!Validated.Resolved)
        return Validated;

    Declarations[Located_] = Amending;
    ++DeclaredRevision;

    return Deliver<bool>::Result(true);
}

Deliver<bool> IlluminantPopulation::Withdraw(OwnerIdentity Subject)
{
    const std::size_t Located_ = Located(Subject);

    if (Located_ >= RegisteredOrder.size() || !(RegisteredOrder[Located_] == Subject))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the owner declares no illuminant" });

    RegisteredOrder.erase(RegisteredOrder.begin() + static_cast<std::ptrdiff_t>(Located_));
    Declarations.erase(Declarations.begin() + static_cast<std::ptrdiff_t>(Located_));

    ++DeclaredRevision;

    return Deliver<bool>::Result(true);
}

Deliver<IlluminantSpecification> IlluminantPopulation::Resolve(OwnerIdentity Subject) const
{
    const std::size_t Located_ = Located(Subject);

    if (Located_ >= RegisteredOrder.size() || !(RegisteredOrder[Located_] == Subject))
    {
        return Deliver<IlluminantSpecification>::Refuse(
            { RefusalReason::IdentityStale, "the owner declares no illuminant" });
    }

    return Deliver<IlluminantSpecification>::Result(Declarations[Located_]);
}

Deliver<ColourSpecification> IlluminantPopulation::ResolveColour(OwnerIdentity                Subject,
                                                                 const ColourSpaceSpecification& Working) const
{
    const Deliver<IlluminantSpecification> Declared = Resolve(Subject);

    if (!Declared.Resolved)
        return Deliver<ColourSpecification>::Refuse(Declared.Error);

    const IlluminantSpecification& Held = Declared.Resolve();

    if (Held.TemperatureDeclared)
        return ProjectTemperature(Held.Temperature, Working);

    if (Held.DeclaredColour.SpaceIdentity == Working.SpaceIdentity)
        return Deliver<ColourSpecification>::Result(Held.DeclaredColour);

    // 📝 A colour declared in another space is projected rather than rejected. `36` §1 requires the space to
    //    travel with the coordinate, and the whole point of it travelling is that this conversion can happen.
    ColourSpaceSpecification Incoming = Working;
    Incoming.SpaceIdentity            = Held.DeclaredColour.SpaceIdentity;

    return Project(Held.DeclaredColour, Incoming, Working);
}

Deliver<OwnerIdentity> IlluminantPopulation::AtmosphericSource() const
{
    for (std::size_t Index = 0u; Index < Declarations.size(); ++Index)
    {
        if (Declarations[Index].AtmosphericSource)
            return Deliver<OwnerIdentity>::Result(RegisteredOrder[Index]);
    }

    return Deliver<OwnerIdentity>::Refuse(
        { RefusalReason::ExtentExhausted, "no illuminant is registered as the atmospheric source" });
}

const std::vector<OwnerIdentity>& IlluminantPopulation::Registered() const { return RegisteredOrder; }
std::uint64_t                        IlluminantPopulation::Revision() const { return DeclaredRevision; }

std::uint32_t IlluminantPopulation::RegisteredCount() const
{
    return static_cast<std::uint32_t>(RegisteredOrder.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INCIDENCE
//------------------------------------------------------------------------------------------------------------------------

Deliver<IncidenceProjection> ProjectIncidence(const IlluminantSpecification& Declared, DocumentPosition Shaded)
{
    IncidenceProjection Projected;

    if (Declared.Emission == EmissionShape::Directional)
    {
        double DirectionX = 0.0;
        double DirectionY = 0.0;
        double DirectionZ = 0.0;
        EmissionDirection(Declared.Placement.Rotation, DirectionX, DirectionY, DirectionZ);

        // 📝 The incidence direction points **toward** the illuminant, so a directional shape's emission
        //    direction is reversed here once rather than at each of `18`'s and `60`'s call sites.
        Projected.DirectionX = -DirectionX;
        Projected.DirectionY = -DirectionY;
        Projected.DirectionZ = -DirectionZ;
        Projected.Distance   = 0.0;

        // 📐 The solid angle of a disc of angular radius θ is 2π(1 − cos θ). Distance does not enter it, which is
        //    what makes a directional shape directional.
        const double HalfAngle = Declared.AngularSize * 0.5 * Pi / 180.0;
        Projected.SolidExtent  = 2.0 * Pi * (1.0 - std::cos(HalfAngle));
        Projected.Attenuation  = 1.0;

        return Deliver<IncidenceProjection>::Result(Projected);
    }

    const double SpanX = Declared.Placement.Translation.PositionX - Shaded.PositionX;
    const double SpanY = Declared.Placement.Translation.PositionY - Shaded.PositionY;
    const double SpanZ = Declared.Placement.Translation.PositionZ - Shaded.PositionZ;

    const double Distance = std::sqrt(SpanX * SpanX + SpanY * SpanY + SpanZ * SpanZ);

    if (Distance <= 0.0)
    {
        return Deliver<IncidenceProjection>::Refuse(
            { RefusalReason::ContentUnsupported, "the shaded position coincides with the illuminant" });
    }

    const double Reciprocal = 1.0 / Distance;

    Projected.DirectionX = SpanX * Reciprocal;
    Projected.DirectionY = SpanY * Reciprocal;
    Projected.DirectionZ = SpanZ * Reciprocal;
    Projected.Distance   = Distance;

    // 📐 The solid angle of a sphere of radius r at distance d is 2π(1 − √(1 − r²/d²)), which degrades gracefully
    //    as the shaded position approaches the source rather than diverging the way an inverse square does.
    double EffectiveRadius = Declared.EmissionRadius;

    if (Declared.Emission == EmissionShape::Extended)
    {
        const double HalfWidth  = Declared.ExtendedWidth  * 0.5;
        const double HalfHeight = Declared.ExtendedHeight * 0.5;

        EffectiveRadius = std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight);
    }

    const double Ratio      = EffectiveRadius / Distance;
    const double Complement = 1.0 - Ratio * Ratio;

    Projected.SolidExtent = 2.0 * Pi * (1.0 - std::sqrt(Complement > 0.0 ? Complement : 0.0));

    // 📝 The declared extent is a hard cutoff and the falloff within it is physical. Beyond it the attenuation is
    //    zero rather than merely small, which is what makes `44` §5's reach index exact rather than approximate.
    if (Distance >= Declared.ExtentReach)
        return Deliver<IncidenceProjection>::Result(Projected);

    double Attenuation = 1.0 / (Distance * Distance);

    if (Declared.Emission == EmissionShape::Spot)
    {
        double AxisX = 0.0;
        double AxisY = 0.0;
        double AxisZ = 0.0;
        EmissionDirection(Declared.Placement.Rotation, AxisX, AxisY, AxisZ);

        const double Alignment = -(AxisX * Projected.DirectionX
                                 + AxisY * Projected.DirectionY
                                 + AxisZ * Projected.DirectionZ);

        const double CosineInner = std::cos(Declared.ConeAngle * 0.5 * Pi / 180.0);
        const double CosineOuter = std::cos((Declared.ConeAngle * 0.5 + Declared.ConeSoftness) * Pi / 180.0);

        if (Alignment <= CosineOuter)
            Attenuation = 0.0;
        else if (Alignment < CosineInner && CosineInner > CosineOuter)
        {
            const double Fraction = (Alignment - CosineOuter) / (CosineInner - CosineOuter);
            Attenuation *= Fraction * Fraction;
        }
    }

    Projected.Attenuation = Attenuation * Declared.RadiantIntensity;

    return Deliver<IncidenceProjection>::Result(Projected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE REACH INDEX
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 A directional illuminant reaches every partition, because it declares no position for an extent to be
//    measured from. Every other shape is bounded by a cube of its declared reach about its position.
bool ReachesExtent(const IlluminantSpecification& Declared, PartitionExtent Extent)
{
    if (Declared.Emission == EmissionShape::Directional)
        return true;

    const DocumentPosition& Position = Declared.Placement.Translation;
    const double            Reach    = Declared.ExtentReach;

    return ClassifyVolumeOverlap(Position.PositionX - Reach, Position.PositionY - Reach, Position.PositionZ - Reach,
                                 Position.PositionX + Reach, Position.PositionY + Reach, Position.PositionZ + Reach,
                                 Extent.Minimum.PositionX,     Extent.Minimum.PositionY,     Extent.Minimum.PositionZ,
                                 Extent.Maximum.PositionX,  Extent.Maximum.PositionY,  Extent.Maximum.PositionZ) >= 0;
}

}   // namespace

Deliver<bool> IlluminantIndex::Derive(const IlluminantPopulation&          Illuminants,
                                      const std::vector<PartitionExtent>&  Extents)
{
    ReachingSets.assign(Extents.size(), {});
    TruncatedCounts.assign(Extents.size(), 0u);
    TruncatedAccumulated = 0u;

    for (std::uint32_t PartitionIndex = 0u; PartitionIndex < Extents.size(); ++PartitionIndex)
    {
        const Deliver<bool> Derived = DerivePartition(Illuminants, PartitionIndex, Extents[PartitionIndex]);

        if (!Derived.Resolved)
            return Derived;
    }

    DescribedIndex = Illuminants.Revision();

    return Deliver<bool>::Result(true);
}

Deliver<bool> IlluminantIndex::DerivePartition(const IlluminantPopulation& Illuminants,
                                               std::uint32_t               PartitionIndex,
                                               PartitionExtent             Extent)
{
    if (PartitionIndex >= ReachingSets.size())
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no such partition" });

    std::vector<OwnerIdentity>& Reaching_ = ReachingSets[PartitionIndex];

    if (TruncatedAccumulated >= TruncatedCounts[PartitionIndex])
        TruncatedAccumulated -= TruncatedCounts[PartitionIndex];

    Reaching_.clear();
    TruncatedCounts[PartitionIndex] = 0u;

    // 📝 Walked in the population's own identity order, so the reaching set is in identity order without a sort.
    //    `44` §6 depends on it and `18` integrates in exactly this order.
    for (const OwnerIdentity& Subject : Illuminants.Registered())
    {
        const Deliver<IlluminantSpecification> Declared = Illuminants.Resolve(Subject);

        if (!Declared.Resolved || !ReachesExtent(Declared.Resolve(), Extent))
            continue;

        if (Reaching_.size() >= IlluminantReachCapacity)
        {
            // 🔴 Counted rather than dropped silently. `86`'s truncation row presents it, and `60` §3.1 truncates
            //    again at the narrower packed capacity — two capacities, two reports, neither inferred.
            ++TruncatedCounts[PartitionIndex];
            continue;
        }

        Reaching_.push_back(Subject);
    }

    TruncatedAccumulated += TruncatedCounts[PartitionIndex];

    return Deliver<bool>::Result(true);
}

std::uint32_t IlluminantIndex::ReachingCount(std::uint32_t PartitionIndex) const
{
    if (PartitionIndex >= ReachingSets.size())
        return 0u;

    return static_cast<std::uint32_t>(ReachingSets[PartitionIndex].size());
}

Deliver<OwnerIdentity> IlluminantIndex::Reaching(std::uint32_t PartitionIndex, std::uint32_t ReachIndex) const
{
    if (PartitionIndex >= ReachingSets.size() || ReachIndex >= ReachingSets[PartitionIndex].size())
        return Deliver<OwnerIdentity>::Refuse({ RefusalReason::ExtentExhausted, "no such reaching illuminant" });

    return Deliver<OwnerIdentity>::Result(ReachingSets[PartitionIndex][ReachIndex]);
}

std::uint32_t IlluminantIndex::TruncatedCount(std::uint32_t PartitionIndex) const
{
    return PartitionIndex < TruncatedCounts.size() ? TruncatedCounts[PartitionIndex] : 0u;
}

std::uint32_t IlluminantIndex::TruncatedTotal() const  { return TruncatedAccumulated; }
std::uint64_t IlluminantIndex::DescribedRevision() const { return DescribedIndex;   }

std::uint32_t IlluminantIndex::SpannedCount() const
{
    return static_cast<std::uint32_t>(ReachingSets.size());
}

}   // namespace Slate
