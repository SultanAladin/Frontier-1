/*============================================================================================================================================
                                                       WORLDINSTANCEREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of O(1) slot recycler, SoA transform & category registers, containment linking & Editor hooks

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/WorldInstanceRegister.h"

#include <utility>

namespace Frontier::World
{

namespace
{

template<typename ElementType>
void SwapEraseElement(std::vector<ElementType>& Storage, uint32_t Index) noexcept
{
    const size_t LastIndex = Storage.size() - 1u;
    if (Index != LastIndex)
    {
        Storage[Index] = std::move(Storage[LastIndex]);
    }
    Storage.pop_back();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               WORLD TRANSFORM STORAGE
//------------------------------------------------------------------------------------------------------------------------

void WorldTransformStorage::ReserveCapacity(uint32_t Capacity)
{
    OwnerTokens.reserve(Capacity);
    PositionMetersX.reserve(Capacity);
    PositionMetersY.reserve(Capacity);
    PositionMetersZ.reserve(Capacity);
    RotationDegreesX.reserve(Capacity);
    RotationDegreesY.reserve(Capacity);
    RotationDegreesZ.reserve(Capacity);
    ScaleScalarX.reserve(Capacity);
    ScaleScalarY.reserve(Capacity);
    ScaleScalarZ.reserve(Capacity);
    TransformRequireRebuild.reserve(Capacity);
    RebuildTransformSlots.reserve(Capacity);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               WORLD INSTANCE REGISTER
//------------------------------------------------------------------------------------------------------------------------

void WorldInstanceRegister::ReserveInstanceCapacity(uint32_t Capacity)
{
    RecordEntries.reserve(Capacity);
    SlotGenerations.reserve(Capacity);
    RecycledSlotIndices.reserve(Capacity / 4u);
    Transforms.ReserveCapacity(Capacity);
}

void WorldInstanceRegister::ClearAll() noexcept
{
#if FRONTIER_PROFILE_EDITOR
    if (EditorAdapter.UnregisterEditorEntry)
    {
        for (const InstanceRecordEntry& Entry : RecordEntries)
        {
            if (Entry.Token.QueryValid() &&
                QueryDirectiveEnabled(Entry.DirectiveMask, InstanceDirectiveMask::EnableEditorRegistration))
            {
                EditorAdapter.UnregisterEditorEntry(Entry.Token);
            }
        }
    }
#endif

    RecordEntries.clear();
    SlotGenerations.clear();
    RecycledSlotIndices.clear();
    ActiveInstanceCount = 0u;

    Transforms = WorldTransformStorage{};
    SolarRegister = SolarInstanceRegister{};
    LunarRegister = LunarInstanceRegister{};
    AtmosphereRegister = AtmosphereInstanceRegister{};
    CloudRegister = CloudInstanceRegister{};
    LuminaireRegister = LuminaireInstanceRegister{};
    SurfaceRegister = SurfaceInstanceRegister{};
    ParticleRegister = ParticleInstanceRegister{};
    ForceFieldRegister = ForceFieldInstanceRegister{};
    TriggerRegister = TriggerInstanceRegister{};
    AcousticRegister = AcousticInstanceRegister{};
    CameraRegister = CameraInstanceRegister{};
}

InstanceToken WorldInstanceRegister::IssueInstanceSlot(
    InstanceCategory Category,
    uint32_t         DirectiveMask,
    uint64_t         DocumentHash64)
{
    uint32_t AssignedSlot = InvalidInstanceSlot;
    uint16_t Generation   = 1u;

    if (!RecycledSlotIndices.empty())
    {
        AssignedSlot = RecycledSlotIndices.back();
        RecycledSlotIndices.pop_back();
        Generation = SlotGenerations[AssignedSlot];
    }
    else
    {
        AssignedSlot = static_cast<uint32_t>(RecordEntries.size());
        RecordEntries.emplace_back();
        SlotGenerations.push_back(Generation);
    }

    const InstanceToken IssuedToken{
        AssignedSlot,
        Generation,
        static_cast<uint16_t>(Category)
    };

    // Allocate contiguous SoA transform slot
    const uint32_t TransformSlot = static_cast<uint32_t>(Transforms.OwnerTokens.size());
    Transforms.OwnerTokens.push_back(IssuedToken);
    Transforms.PositionMetersX.push_back(0.0f);
    Transforms.PositionMetersY.push_back(0.0f);
    Transforms.PositionMetersZ.push_back(0.0f);
    Transforms.RotationDegreesX.push_back(0.0f);
    Transforms.RotationDegreesY.push_back(0.0f);
    Transforms.RotationDegreesZ.push_back(0.0f);
    Transforms.ScaleScalarX.push_back(1.0f);
    Transforms.ScaleScalarY.push_back(1.0f);
    Transforms.ScaleScalarZ.push_back(1.0f);
    Transforms.TransformRequireRebuild.push_back(1u);
    Transforms.RebuildTransformSlots.push_back(TransformSlot);
    Transforms.RequireRebuild = true;

    InstanceRecordEntry& Entry = RecordEntries[AssignedSlot];
    Entry                      = InstanceRecordEntry{};
    Entry.Token                = IssuedToken;
    Entry.CategorySlot         = InvalidInstanceSlot;
    Entry.TransformSlot        = TransformSlot;
    Entry.EnclosingIndex       = InvalidInstanceSlot;
    Entry.FirstNestedIndex     = InvalidInstanceSlot;
    Entry.NextLateralIndex     = InvalidInstanceSlot;
    Entry.DirectiveMask        = DirectiveMask | static_cast<uint32_t>(InstanceDirectiveMask::EnableTransformRebuild);
    Entry.DocumentHash64       = DocumentHash64;
    Entry.SpatialCellId        = 0ull;

    ++ActiveInstanceCount;
    return IssuedToken;
}

bool WorldInstanceRegister::ReclaimInstanceSlot(InstanceToken Token) noexcept
{
    if (!AuthenticateToken(Token))
    {
        return false;
    }

    InstanceRecordEntry& Entry = RecordEntries[Token.SlotIndex];

    // Detach from any enclosing slot and unbind any nested slots to root
    if (Entry.EnclosingIndex != InvalidInstanceSlot)
    {
        DetachNestedSlot(Token);
    }

    uint32_t NestedCursor = Entry.FirstNestedIndex;
    while (NestedCursor != InvalidInstanceSlot)
    {
        const uint32_t NextCursor = RecordEntries[NestedCursor].NextLateralIndex;
        RecordEntries[NestedCursor].EnclosingIndex   = InvalidInstanceSlot;
        RecordEntries[NestedCursor].NextLateralIndex = InvalidInstanceSlot;
        NestedCursor = NextCursor;
    }
    Entry.FirstNestedIndex = InvalidInstanceSlot;

#if FRONTIER_PROFILE_EDITOR
    if (QueryDirectiveEnabled(Entry.DirectiveMask, InstanceDirectiveMask::EnableEditorRegistration))
    {
        NotifyEditorInstanceUnregistered(Token);
    }
#endif

    // O(1) swap-remove from category SoA register and transform storage
    if (Entry.CategorySlot != InvalidInstanceSlot)
    {
        ReclaimCategorySlot(Token.QueryCategory(), Entry.CategorySlot);
        Entry.CategorySlot = InvalidInstanceSlot;
    }

    if (Entry.TransformSlot != InvalidInstanceSlot)
    {
        ReclaimTransformSlot(Entry.TransformSlot);
        Entry.TransformSlot = InvalidInstanceSlot;
    }

    // Advance slot generation (skip 0 so Generation == 0 remains invalid sentinel)
    uint16_t NextGeneration = static_cast<uint16_t>(SlotGenerations[Token.SlotIndex] + 1u);
    if (NextGeneration == 0u)
    {
        NextGeneration = 1u;
    }
    SlotGenerations[Token.SlotIndex] = NextGeneration;

    Entry = InstanceRecordEntry{};
    RecycledSlotIndices.push_back(Token.SlotIndex);
    if (ActiveInstanceCount > 0u)
    {
        --ActiveInstanceCount;
    }
    return true;
}

bool WorldInstanceRegister::AuthenticateToken(InstanceToken Token) const noexcept
{
    if (!Token.QueryValid() || Token.SlotIndex >= SlotGenerations.size())
    {
        return false;
    }

    return (SlotGenerations[Token.SlotIndex] == Token.Generation) &&
           (RecordEntries[Token.SlotIndex].Token == Token);
}

const InstanceRecordEntry* WorldInstanceRegister::FindRecordEntry(InstanceToken Token) const noexcept
{
    if (!AuthenticateToken(Token))
    {
        return nullptr;
    }

    return &RecordEntries[Token.SlotIndex];
}

InstanceRecordEntry* WorldInstanceRegister::AccessRecordEntry(InstanceToken Token) noexcept
{
    if (!AuthenticateToken(Token))
    {
        return nullptr;
    }

    return &RecordEntries[Token.SlotIndex];
}

bool WorldInstanceRegister::AttachNestedSlot(InstanceToken EnclosingToken, InstanceToken NestedToken) noexcept
{
    if (!AuthenticateToken(EnclosingToken) || !AuthenticateToken(NestedToken))
    {
        return false;
    }

    if (EnclosingToken.SlotIndex == NestedToken.SlotIndex)
    {
        return false;
    }

    // Prevent circular containment by walking enclosing chain
    uint32_t Cursor = EnclosingToken.SlotIndex;
    while (Cursor != InvalidInstanceSlot)
    {
        if (Cursor == NestedToken.SlotIndex)
        {
            return false;
        }
        Cursor = RecordEntries[Cursor].EnclosingIndex;
    }

    DetachNestedSlot(NestedToken);

    InstanceRecordEntry& EnclosingEntry = RecordEntries[EnclosingToken.SlotIndex];
    InstanceRecordEntry& NestedEntry    = RecordEntries[NestedToken.SlotIndex];

    NestedEntry.EnclosingIndex    = EnclosingToken.SlotIndex;
    NestedEntry.NextLateralIndex  = EnclosingEntry.FirstNestedIndex;
    EnclosingEntry.FirstNestedIndex = NestedToken.SlotIndex;
    return true;
}

bool WorldInstanceRegister::DetachNestedSlot(InstanceToken NestedToken) noexcept
{
    if (!AuthenticateToken(NestedToken))
    {
        return false;
    }

    InstanceRecordEntry& NestedEntry = RecordEntries[NestedToken.SlotIndex];
    if (NestedEntry.EnclosingIndex == InvalidInstanceSlot)
    {
        return true;
    }

    InstanceRecordEntry& EnclosingEntry = RecordEntries[NestedEntry.EnclosingIndex];
    if (EnclosingEntry.FirstNestedIndex == NestedToken.SlotIndex)
    {
        EnclosingEntry.FirstNestedIndex = NestedEntry.NextLateralIndex;
    }
    else
    {
        uint32_t LateralCursor = EnclosingEntry.FirstNestedIndex;
        while (LateralCursor != InvalidInstanceSlot)
        {
            InstanceRecordEntry& Candidate = RecordEntries[LateralCursor];
            if (Candidate.NextLateralIndex == NestedToken.SlotIndex)
            {
                Candidate.NextLateralIndex = NestedEntry.NextLateralIndex;
                break;
            }
            LateralCursor = Candidate.NextLateralIndex;
        }
    }

    NestedEntry.EnclosingIndex   = InvalidInstanceSlot;
    NestedEntry.NextLateralIndex = InvalidInstanceSlot;
    return true;
}

bool WorldInstanceRegister::AssignWorldTransform(
    InstanceToken Token,
    float         PositionX,
    float         PositionY,
    float         PositionZ,
    float         RotationX,
    float         RotationY,
    float         RotationZ,
    float         ScaleX,
    float         ScaleY,
    float         ScaleZ) noexcept
{
    InstanceRecordEntry* Entry = AccessRecordEntry(Token);
    if ((Entry == nullptr) || (Entry->TransformSlot == InvalidInstanceSlot))
    {
        return false;
    }

    const uint32_t Slot = Entry->TransformSlot;
    Transforms.PositionMetersX[Slot]  = PositionX;
    Transforms.PositionMetersY[Slot]  = PositionY;
    Transforms.PositionMetersZ[Slot]  = PositionZ;
    Transforms.RotationDegreesX[Slot] = RotationX;
    Transforms.RotationDegreesY[Slot] = RotationY;
    Transforms.RotationDegreesZ[Slot] = RotationZ;
    Transforms.ScaleScalarX[Slot]     = ScaleX;
    Transforms.ScaleScalarY[Slot]     = ScaleY;
    Transforms.ScaleScalarZ[Slot]     = ScaleZ;

    if (Transforms.TransformRequireRebuild[Slot] == 0u)
    {
        Transforms.TransformRequireRebuild[Slot] = 1u;
        Transforms.RebuildTransformSlots.push_back(Slot);
    }

    Transforms.RequireRebuild = true;
    Entry->DirectiveMask |= static_cast<uint32_t>(InstanceDirectiveMask::EnableTransformRebuild);
    return true;
}

bool WorldInstanceRegister::AssignVisibility(InstanceToken Token, bool VisibilityEnabled) noexcept
{
    InstanceRecordEntry* Entry = AccessRecordEntry(Token);
    if (Entry == nullptr)
    {
        return false;
    }

    const uint32_t VisibilityBit = static_cast<uint32_t>(InstanceDirectiveMask::EnableVisibility);
    if (VisibilityEnabled)
    {
        Entry->DirectiveMask |= VisibilityBit;
    }
    else
    {
        Entry->DirectiveMask &= ~VisibilityBit;
    }

    const uint8_t ByteValue = VisibilityEnabled ? 1u : 0u;
    if (Entry->CategorySlot != InvalidInstanceSlot)
    {
        const uint32_t CategorySlot = Entry->CategorySlot;
        switch (Token.QueryCategory())
        {
            case InstanceCategory::SolarBody:
                SolarRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                SolarRegister.RequireRebuild = true;
                break;
            case InstanceCategory::LunarBody:
                LunarRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                LunarRegister.RequireRebuild = true;
                break;
            case InstanceCategory::AtmosphereMedium:
                AtmosphereRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                AtmosphereRegister.RequireRebuild = true;
                break;
            case InstanceCategory::CloudDeck:
                CloudRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                CloudRegister.RequireRebuild = true;
                break;
            case InstanceCategory::Luminaire:
                LuminaireRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                LuminaireRegister.RequireRebuild = true;
                break;
            case InstanceCategory::SurfaceGeometry:
                SurfaceRegister.VisibilityEnabled[CategorySlot] = ByteValue;
                SurfaceRegister.RequireRebuild = true;
                break;
            case InstanceCategory::ParticleCloud:
                ParticleRegister.SimulationActive[CategorySlot] = ByteValue;
                ParticleRegister.RequireRebuild = true;
                break;
            case InstanceCategory::ForceFieldVolume:
                ForceFieldRegister.FieldActive[CategorySlot] = ByteValue;
                ForceFieldRegister.RequireRebuild = true;
                break;
            case InstanceCategory::TriggerZone:
                TriggerRegister.SensorActive[CategorySlot] = ByteValue;
                TriggerRegister.RequireRebuild = true;
                break;
            case InstanceCategory::AcousticSource:
                AcousticRegister.PlaybackActive[CategorySlot] = ByteValue;
                AcousticRegister.RequireRebuild = true;
                break;
            case InstanceCategory::CameraOptics:
                CameraRegister.PrimaryViewActive[CategorySlot] = ByteValue;
                CameraRegister.RequireRebuild = true;
                break;
            default:
                break;
        }
    }

#if FRONTIER_PROFILE_EDITOR
    if (EditorAdapter.AssignEditorVisibility &&
        QueryDirectiveEnabled(Entry->DirectiveMask, InstanceDirectiveMask::EnableEditorRegistration))
    {
        EditorAdapter.AssignEditorVisibility(Token, VisibilityEnabled);
    }
#endif

    return true;
}

void WorldInstanceRegister::BindCategorySlot(InstanceToken Token, uint32_t CategorySlot) noexcept
{
    if (InstanceRecordEntry* Entry = AccessRecordEntry(Token))
    {
        Entry->CategorySlot = CategorySlot;
    }
}

#if FRONTIER_PROFILE_EDITOR
void WorldInstanceRegister::BindEditorSurfaceAdapter(EditorSurfaceAdapter Adapter) noexcept
{
    EditorAdapter = std::move(Adapter);
}

void WorldInstanceRegister::ClearEditorSurfaceAdapter() noexcept
{
    EditorAdapter = EditorSurfaceAdapter{};
}

void WorldInstanceRegister::NotifyEditorInstanceRegistered(
    const EditorInstanceRegistrationDescriptor& Descriptor) const
{
    if (EditorAdapter.RegisterEditorEntry)
    {
        EditorAdapter.RegisterEditorEntry(Descriptor);
    }
}

void WorldInstanceRegister::NotifyEditorInstanceUnregistered(InstanceToken Token) const noexcept
{
    if (EditorAdapter.UnregisterEditorEntry)
    {
        EditorAdapter.UnregisterEditorEntry(Token);
    }
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                           O(1) SWAP-REMOVE SLOT RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void WorldInstanceRegister::ReclaimTransformSlot(uint32_t TransformSlot) noexcept
{
    const uint32_t LastIndex = static_cast<uint32_t>(Transforms.OwnerTokens.size() - 1u);
    if (TransformSlot != LastIndex)
    {
        const InstanceToken RelocatedOwner = Transforms.OwnerTokens[LastIndex];
        if (InstanceRecordEntry* RelocatedEntry = AccessRecordEntry(RelocatedOwner))
        {
            RelocatedEntry->TransformSlot = TransformSlot;
        }
    }

    SwapEraseElement(Transforms.OwnerTokens, TransformSlot);
    SwapEraseElement(Transforms.PositionMetersX, TransformSlot);
    SwapEraseElement(Transforms.PositionMetersY, TransformSlot);
    SwapEraseElement(Transforms.PositionMetersZ, TransformSlot);
    SwapEraseElement(Transforms.RotationDegreesX, TransformSlot);
    SwapEraseElement(Transforms.RotationDegreesY, TransformSlot);
    SwapEraseElement(Transforms.RotationDegreesZ, TransformSlot);
    SwapEraseElement(Transforms.ScaleScalarX, TransformSlot);
    SwapEraseElement(Transforms.ScaleScalarY, TransformSlot);
    SwapEraseElement(Transforms.ScaleScalarZ, TransformSlot);
    SwapEraseElement(Transforms.TransformRequireRebuild, TransformSlot);

    Transforms.RequireRebuild = true;
}

void WorldInstanceRegister::ReclaimCategorySlot(InstanceCategory Category, uint32_t CategorySlot) noexcept
{
    auto RelocateLastOwner = [this](const std::vector<InstanceToken>& Owners, uint32_t ErasedSlot) noexcept
    {
        const uint32_t LastIndex = static_cast<uint32_t>(Owners.size() - 1u);
        if (ErasedSlot != LastIndex)
        {
            const InstanceToken RelocatedOwner = Owners[LastIndex];
            if (InstanceRecordEntry* RelocatedEntry = AccessRecordEntry(RelocatedOwner))
            {
                RelocatedEntry->CategorySlot = ErasedSlot;
            }
        }
    };

    switch (Category)
    {
        case InstanceCategory::SolarBody:
            RelocateLastOwner(SolarRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(SolarRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(SolarRegister.AzimuthDegrees, CategorySlot);
            SwapEraseElement(SolarRegister.ElevationDegrees, CategorySlot);
            SwapEraseElement(SolarRegister.AngularDiameterDegrees, CategorySlot);
            SwapEraseElement(SolarRegister.IntensityMultiplier, CategorySlot);
            SwapEraseElement(SolarRegister.DayCycleSpeedDegreesS, CategorySlot);
            SwapEraseElement(SolarRegister.CycleAnimationEnabled, CategorySlot);
            SwapEraseElement(SolarRegister.VisibilityEnabled, CategorySlot);
            SolarRegister.RequireRebuild = true;
            break;

        case InstanceCategory::LunarBody:
            RelocateLastOwner(LunarRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(LunarRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(LunarRegister.MoonSlotOrdinal, CategorySlot);
            SwapEraseElement(LunarRegister.RightAscensionDegrees, CategorySlot);
            SwapEraseElement(LunarRegister.DeclinationDegrees, CategorySlot);
            SwapEraseElement(LunarRegister.AngularDiameterDegrees, CategorySlot);
            SwapEraseElement(LunarRegister.PhaseAngleDegrees, CategorySlot);
            SwapEraseElement(LunarRegister.OrbitalRateDegreesPerS, CategorySlot);
            SwapEraseElement(LunarRegister.AlbedoReflectance, CategorySlot);
            SwapEraseElement(LunarRegister.VisibilityEnabled, CategorySlot);
            LunarRegister.RequireRebuild = true;
            break;

        case InstanceCategory::AtmosphereMedium:
            RelocateLastOwner(AtmosphereRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(AtmosphereRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(AtmosphereRegister.RayleighScaleMeters, CategorySlot);
            SwapEraseElement(AtmosphereRegister.MieScaleMeters, CategorySlot);
            SwapEraseElement(AtmosphereRegister.OzoneDensityMultiplier, CategorySlot);
            SwapEraseElement(AtmosphereRegister.ObserverAltitudeMeters, CategorySlot);
            SwapEraseElement(AtmosphereRegister.VisibilityEnabled, CategorySlot);
            AtmosphereRegister.RequireRebuild = true;
            break;

        case InstanceCategory::CloudDeck:
            RelocateLastOwner(CloudRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(CloudRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(CloudRegister.BaseAltitudeMeters, CategorySlot);
            SwapEraseElement(CloudRegister.ThicknessMeters, CategorySlot);
            SwapEraseElement(CloudRegister.DensityFraction, CategorySlot);
            SwapEraseElement(CloudRegister.WindSpeedMetersPerS, CategorySlot);
            SwapEraseElement(CloudRegister.AdvectionOffsetMeters, CategorySlot);
            SwapEraseElement(CloudRegister.VisibilityEnabled, CategorySlot);
            CloudRegister.RequireRebuild = true;
            break;

        case InstanceCategory::Luminaire:
            RelocateLastOwner(LuminaireRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(LuminaireRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(LuminaireRegister.LuminaireFormOrdinal, CategorySlot);
            SwapEraseElement(LuminaireRegister.PeakLuminanceR, CategorySlot);
            SwapEraseElement(LuminaireRegister.PeakLuminanceG, CategorySlot);
            SwapEraseElement(LuminaireRegister.PeakLuminanceB, CategorySlot);
            SwapEraseElement(LuminaireRegister.RangeMeters, CategorySlot);
            SwapEraseElement(LuminaireRegister.OuterConeDegrees, CategorySlot);
            SwapEraseElement(LuminaireRegister.VisibilityEnabled, CategorySlot);
            LuminaireRegister.RequireRebuild = true;
            break;

        case InstanceCategory::SurfaceGeometry:
            RelocateLastOwner(SurfaceRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(SurfaceRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(SurfaceRegister.GeometryDocumentHash64, CategorySlot);
            SwapEraseElement(SurfaceRegister.MaterialDocumentHash64, CategorySlot);
            SwapEraseElement(SurfaceRegister.PrimitiveShapeOrdinal, CategorySlot);
            SwapEraseElement(SurfaceRegister.CastShadows, CategorySlot);
            SwapEraseElement(SurfaceRegister.VisibilityEnabled, CategorySlot);
            SurfaceRegister.RequireRebuild = true;
            break;

        case InstanceCategory::ParticleCloud:
            RelocateLastOwner(ParticleRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(ParticleRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(ParticleRegister.ParticleDocumentHash64, CategorySlot);
            SwapEraseElement(ParticleRegister.MaximumParticleCount, CategorySlot);
            SwapEraseElement(ParticleRegister.ReleaseRatePerSecond, CategorySlot);
            SwapEraseElement(ParticleRegister.AccumulatedClockSeconds, CategorySlot);
            SwapEraseElement(ParticleRegister.SimulationActive, CategorySlot);
            ParticleRegister.RequireRebuild = true;
            break;

        case InstanceCategory::ForceFieldVolume:
            RelocateLastOwner(ForceFieldRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(ForceFieldRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(ForceFieldRegister.ForceModelOrdinal, CategorySlot);
            SwapEraseElement(ForceFieldRegister.StrengthMetersPerS2, CategorySlot);
            SwapEraseElement(ForceFieldRegister.InfluenceRadiusMeters, CategorySlot);
            SwapEraseElement(ForceFieldRegister.DirectionAxisX, CategorySlot);
            SwapEraseElement(ForceFieldRegister.DirectionAxisY, CategorySlot);
            SwapEraseElement(ForceFieldRegister.DirectionAxisZ, CategorySlot);
            SwapEraseElement(ForceFieldRegister.FieldActive, CategorySlot);
            ForceFieldRegister.RequireRebuild = true;
            break;

        case InstanceCategory::TriggerZone:
            RelocateLastOwner(TriggerRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(TriggerRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(TriggerRegister.HalfExtentMetersX, CategorySlot);
            SwapEraseElement(TriggerRegister.HalfExtentMetersY, CategorySlot);
            SwapEraseElement(TriggerRegister.HalfExtentMetersZ, CategorySlot);
            SwapEraseElement(TriggerRegister.OverlapCountReading, CategorySlot);
            SwapEraseElement(TriggerRegister.SensorActive, CategorySlot);
            TriggerRegister.RequireRebuild = true;
            break;

        case InstanceCategory::AcousticSource:
            RelocateLastOwner(AcousticRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(AcousticRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(AcousticRegister.AcousticDocumentHash64, CategorySlot);
            SwapEraseElement(AcousticRegister.GainScalar, CategorySlot);
            SwapEraseElement(AcousticRegister.MaxDistanceMeters, CategorySlot);
            SwapEraseElement(AcousticRegister.PlaybackActive, CategorySlot);
            AcousticRegister.RequireRebuild = true;
            break;

        case InstanceCategory::CameraOptics:
            RelocateLastOwner(CameraRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(CameraRegister.OwnerTokens, CategorySlot);
            SwapEraseElement(CameraRegister.FocalLengthMillimeters, CategorySlot);
            SwapEraseElement(CameraRegister.SensorWidthMillimeters, CategorySlot);
            SwapEraseElement(CameraRegister.NearPlaneMeters, CategorySlot);
            SwapEraseElement(CameraRegister.FarPlaneMeters, CategorySlot);
            SwapEraseElement(CameraRegister.PrimaryViewActive, CategorySlot);
            CameraRegister.RequireRebuild = true;
            break;

        default:
            break;
    }
}

} // namespace Frontier::World
