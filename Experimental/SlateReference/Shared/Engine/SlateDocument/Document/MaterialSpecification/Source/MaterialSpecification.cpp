//============================================================================================================================================
//                                                       MATERIALSPECIFICATION.CPP
//============================================================================================================================================
// 🧩 The channel inventory per reflectance, declaration validation, and the partition resolution.

#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CHANNEL INVENTORY
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One bit per channel, per reflectance selection — `18` §3's table as data. Written as a mask rather than as a
//    switch of comparisons, so a selection's inventory is one word and the predicate is one test.
constexpr std::uint32_t ChannelBit(ChannelSubject Subject)
{
    return 1u << static_cast<std::uint32_t>(Subject);
}

constexpr std::uint32_t StandardChannels = ChannelBit(ChannelSubject::AlbedoColour)
                                         | ChannelBit(ChannelSubject::Metallic)
                                         | ChannelBit(ChannelSubject::Roughness)
                                         | ChannelBit(ChannelSubject::NormalIncidenceReflectance)
                                         | ChannelBit(ChannelSubject::SurfaceOrientation)
                                         | ChannelBit(ChannelSubject::AmbientOcclusion)
                                         | ChannelBit(ChannelSubject::Emission)
                                         | ChannelBit(ChannelSubject::Opacity);

constexpr std::uint32_t ConsumedChannels[static_cast<std::size_t>(ReflectanceSelection::ReflectanceCount)] =
{
    StandardChannels,

    StandardChannels | ChannelBit(ChannelSubject::Anisotropy)
                     | ChannelBit(ChannelSubject::AnisotropyDirection),

    StandardChannels | ChannelBit(ChannelSubject::ClearCoat)
                     | ChannelBit(ChannelSubject::ClearCoatRoughness)
                     | ChannelBit(ChannelSubject::ClearCoatOrientation),

    ChannelBit(ChannelSubject::AlbedoColour)       | ChannelBit(ChannelSubject::Roughness)
  | ChannelBit(ChannelSubject::SurfaceOrientation) | ChannelBit(ChannelSubject::AmbientOcclusion)
  | ChannelBit(ChannelSubject::Opacity)            | ChannelBit(ChannelSubject::SheenColour)
  | ChannelBit(ChannelSubject::SheenRoughness),

    StandardChannels | ChannelBit(ChannelSubject::SubsurfaceColour)
                     | ChannelBit(ChannelSubject::SubsurfaceThickness),

    StandardChannels | ChannelBit(ChannelSubject::Transmission)
                     | ChannelBit(ChannelSubject::RefractionRatio),

    ChannelBit(ChannelSubject::Emission) | ChannelBit(ChannelSubject::Opacity),

    ChannelBit(ChannelSubject::AlbedoColour) | ChannelBit(ChannelSubject::Opacity)
};

}   // namespace

bool ChannelConsumed(ReflectanceSelection Selected, ChannelSubject Channel)
{
    const std::size_t Index = static_cast<std::size_t>(Selected);

    if (Index >= static_cast<std::size_t>(ReflectanceSelection::ReflectanceCount))
        return false;

    if (Channel == ChannelSubject::ChannelCount)
        return false;

    return (ConsumedChannels[Index] & ChannelBit(Channel)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MATERIAL
//------------------------------------------------------------------------------------------------------------------------

void MaterialSpecification::DeclareReflectance(ReflectanceSelection Selecting)
{
    // 📝 Nothing is cleared. `42` §5: a channel declared for an unselected reflectance is retained, so switching
    //    a material's selection and switching it back returns the artist's work rather than a default.
    Selected = Selecting;
}

Deliver<bool> MaterialSpecification::DeclareChannel(ChannelSubject Channel, const ChannelSpecification& Declaring)
{
    const std::size_t Index = static_cast<std::size_t>(Channel);

    if (Index >= ChannelSpan)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such channel" });

    if (MeasureCarriesColour(Declaring.Measured))
    {
        // 🔴 `36` §1: a colour without its space is rejected rather than assumed to be in the working space. An
        //    assumed space is the defect `36` exists to prevent, placed where no report can name it.
        if (Declaring.Source == ChannelSource::Constant && !Declaring.ConstantColour.ColourDeclared())
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "a constant colour declares no space" });
        }

        if (!Declaring.DefaultColour.ColourDeclared())
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "a colour channel's default declares no space" });
        }
    }
    else if (Declaring.Measured == ChannelMeasure::Scalar)
    {
        if (std::isnan(Declaring.DefaultScalar))
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the default is not a number" });

        if (Declaring.DefaultScalar < Declaring.LowerMagnitude
         || Declaring.DefaultScalar > Declaring.UpperMagnitude)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the declared default lies outside its own interval" });
        }
    }

    Declarations[Index]                 = Declaring;
    Declarations[Index].ChannelDeclared = true;

    return Deliver<bool>::Result(true);
}

void MaterialSpecification::DeclareCutoutThreshold(double Threshold)
{
    CoverageThreshold = Threshold < 0.0 ? 0.0 : (Threshold > 1.0 ? 1.0 : Threshold);
}

void MaterialSpecification::DeclareCutoutRegistration(bool CutoutEnabled)
{
    CutoutDeclared = CutoutEnabled;
}

ReflectanceSelection MaterialSpecification::Reflectance() const     { return Selected;          }
double               MaterialSpecification::CutoutThreshold() const { return CoverageThreshold; }
bool                 MaterialSpecification::CutoutRegistered() const  { return CutoutDeclared;    }

const ChannelSpecification& MaterialSpecification::Channel(ChannelSubject Subject) const
{
    const std::size_t Index = static_cast<std::size_t>(Subject);

    return Declarations[Index < ChannelSpan ? Index : 0u];
}

bool MaterialSpecification::ChannelSampled(ChannelSubject Subject) const
{
    const std::size_t Index = static_cast<std::size_t>(Subject);

    if (Index >= ChannelSpan)
        return false;

    if (!ChannelConsumed(Selected, Subject))
        return false;

    // 📝 An undeclared or Absent channel resolves to its declared default and is not sampled. That is the whole
    //    point of `42` §2's Absent source: the value is read directly rather than fetched from anywhere.
    return Declarations[Index].ChannelDeclared && Declarations[Index].Source != ChannelSource::Absent;
}

bool MaterialSpecification::ChannelConverted(ChannelSubject Subject) const
{
    const std::size_t Index = static_cast<std::size_t>(Subject);

    return Index < ChannelSpan
        && Declarations[Index].ChannelDeclared
        && MeasureCarriesColour(Declarations[Index].Measured);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE MATERIALS
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> MaterialIndex::Declare(const std::string& Named)
{
    if (Declared.size() >= MaterialLimit)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the document reached its material ceiling" });
    }

    const std::uint32_t MaterialIndex = static_cast<std::uint32_t>(Declared.size());

    Declared.push_back(MaterialSpecification{});
    DeclaredNames.push_back(Named);

    return Deliver<std::uint32_t>::Result(MaterialIndex);
}

Deliver<const MaterialSpecification*> MaterialIndex::Resolve(std::uint32_t MaterialIndex) const
{
    if (MaterialIndex >= Declared.size())
    {
        return Deliver<const MaterialSpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "no such material" });
    }

    return Deliver<const MaterialSpecification*>::Result(&Declared[MaterialIndex]);
}

Deliver<MaterialSpecification*> MaterialIndex::Amend(std::uint32_t MaterialIndex)
{
    if (MaterialIndex >= Declared.size())
        return Deliver<MaterialSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such material" });

    return Deliver<MaterialSpecification*>::Result(&Declared[MaterialIndex]);
}

const std::string& MaterialIndex::DeclaredName(std::uint32_t MaterialIndex) const
{
    return MaterialIndex < DeclaredNames.size() ? DeclaredNames[MaterialIndex] : AbsentName;
}

std::uint32_t MaterialIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Declared.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                PARTITION RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

void PartitionResolutionIndex::Reclaim()
{
    Resolutions.clear();

    // 🔴 The revision advances on every rebuild, so a partition identity taken before it resolves stale rather
    //    than resolving to whatever partition later took the ordinal. `64` §4 depends on the opposite property
    //    for its history — it tests the **owner**, not the partition, so a re-partition does not discard it.
    ++DerivedRevision;
}

Deliver<PartitionIdentity> PartitionResolutionIndex::Declare(const ResolvedPartition& Resolving)
{
    if (!Resolving.Owner.IdentityDeclared())
    {
        return Deliver<PartitionIdentity>::Refuse(
            { RefusalReason::IdentityStale, "a partition resolves to no owner" });
    }

    if (Resolutions.size() >= PartitionLimit)
    {
        return Deliver<PartitionIdentity>::Refuse(
            { RefusalReason::ExtentExhausted, "the partition ceiling was reached" });
    }

    PartitionIdentity Registered;
    Registered.SlotIndex    = static_cast<std::uint32_t>(Resolutions.size());
    Registered.SlotGeneration = static_cast<std::uint32_t>(DerivedRevision);

    Resolutions.push_back(Resolving);

    return Deliver<PartitionIdentity>::Result(Registered);
}

Deliver<ResolvedPartition> PartitionResolutionIndex::Resolve(PartitionIdentity Subject) const
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= Resolutions.size())
        return Deliver<ResolvedPartition>::Refuse({ RefusalReason::IdentityStale, "no such partition" });

    if (Subject.SlotGeneration != static_cast<std::uint32_t>(DerivedRevision))
    {
        return Deliver<ResolvedPartition>::Refuse(
            { RefusalReason::IdentityStale, "the partitioning was derived again since the identity was taken" });
    }

    return Deliver<ResolvedPartition>::Result(Resolutions[Subject.SlotIndex]);
}

std::uint64_t PartitionResolutionIndex::Revision() const      { return DerivedRevision; }
std::uint32_t PartitionResolutionIndex::DeclaredCount() const { return static_cast<std::uint32_t>(Resolutions.size()); }

}   // namespace Slate
