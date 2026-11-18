/*============================================================================================================================================
                                                       COMPONENTINSTANCESTORAGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of contiguous ComponentClass instance arrays, unique naming, cycle-free tree links & category lookup

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceStorage.h"

#include <algorithm>
#include <cctype>

namespace Frontier::ComponentClass
{

namespace
{

[[nodiscard]] std::string TrimWhitespace(std::string_view View)
{
    while (!View.empty() && std::isspace(static_cast<unsigned char>(View.front())))
    {
        View.remove_prefix(1u);
    }
    while (!View.empty() && std::isspace(static_cast<unsigned char>(View.back())))
    {
        View.remove_suffix(1u);
    }
    return std::string(View);
}

[[nodiscard]] std::string ToLowerAscii(std::string_view View)
{
    std::string Out(View);
    for (char& Ch : Out)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Out;
}

} // namespace

ComponentInstanceStorage::ComponentInstanceStorage()
{
    ResetWithSpatialOrigin("Origin");
}

void ComponentInstanceStorage::ResetWithSpatialOrigin(std::string_view OriginTitle)
{
    Headers.clear();
    FreeSlots.clear();
    Geometries.clear();
    Constraints.clear();
    Particles.clear();
    Fluids.clear();
    Lights.clear();
    Cameras.clear();
    Collisions.clear();
    Arrows.clear();
    Vehicles.clear();
    Rotations.clear();

    SlotRecord OriginSlot {};
    OriginSlot.Occupied                 = true;
    OriginSlot.Gen                      = 1u;
    OriginSlot.Header.Token             = { 1u, 1u };
    OriginSlot.Header.Category          = ComponentCategory::SpatialOrigin;
    OriginSlot.Header.Title             = OriginTitle.empty() ? std::string("Origin") : std::string(OriginTitle);
    OriginSlot.Header.EnclosingIndex    = InvalidHierarchyIndex;
    OriginSlot.Header.FirstNestedIndex  = InvalidHierarchyIndex;
    OriginSlot.Header.NextPeerIndex     = InvalidHierarchyIndex;
    OriginSlot.Header.DepthLevel        = 0u;
    OriginSlot.Header.VisibilityEnabled = true;

    Headers.push_back(std::move(OriginSlot));
    OriginToken = { 1u, 1u };
}

bool ComponentInstanceStorage::AuthenticateToken(ComponentInstanceToken Token) const noexcept
{
    if (!Token.Valid())
    {
        return false;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    if (ZeroIdx >= Headers.size())
    {
        return false;
    }
    const SlotRecord& Rec = Headers[ZeroIdx];
    return Rec.Occupied && Rec.Gen == Token.Generation;
}

uint32_t ComponentInstanceStorage::AllocateTypedSlot(ComponentCategory Category)
{
    switch (Category)
    {
        case ComponentCategory::SpatialOrigin:
        case ComponentCategory::TransformPivot:
            return 0u;
        case ComponentCategory::GeometryInstance:
            Geometries.emplace_back();
            return static_cast<uint32_t>(Geometries.size() - 1u);
        case ComponentCategory::ConstraintInstance:
            Constraints.emplace_back();
            return static_cast<uint32_t>(Constraints.size() - 1u);
        case ComponentCategory::ParticleInstance:
            Particles.emplace_back();
            return static_cast<uint32_t>(Particles.size() - 1u);
        case ComponentCategory::FluidInstance:
            Fluids.emplace_back();
            return static_cast<uint32_t>(Fluids.size() - 1u);
        case ComponentCategory::LightInstance:
            Lights.emplace_back();
            return static_cast<uint32_t>(Lights.size() - 1u);
        case ComponentCategory::CameraInstance:
            Cameras.emplace_back();
            return static_cast<uint32_t>(Cameras.size() - 1u);
        case ComponentCategory::CollisionVolume:
            Collisions.emplace_back();
            return static_cast<uint32_t>(Collisions.size() - 1u);
        case ComponentCategory::DirectionArrow:
            Arrows.emplace_back();
            return static_cast<uint32_t>(Arrows.size() - 1u);
        case ComponentCategory::VehicleDynamics:
            Vehicles.emplace_back();
            return static_cast<uint32_t>(Vehicles.size() - 1u);
        case ComponentCategory::RotationalMotion:
            Rotations.emplace_back();
            return static_cast<uint32_t>(Rotations.size() - 1u);
    }
    return 0u;
}

std::string ComponentInstanceStorage::ConstructUniqueTitle(std::string_view Desired) const
{
    std::string BaseTitle = TrimWhitespace(Desired);
    if (BaseTitle.empty())
    {
        BaseTitle = "Component";
    }

    auto Exists = [this](std::string_view Candidate) -> bool
    {
        for (const SlotRecord& Rec : Headers)
        {
            if (Rec.Occupied && Rec.Header.Title == Candidate)
            {
                return true;
            }
        }
        return false;
    };

    if (!Exists(BaseTitle))
    {
        return BaseTitle;
    }

    for (uint32_t Suffix = 2u; Suffix < 10000u; ++Suffix)
    {
        std::string Candidate = BaseTitle + std::to_string(Suffix);
        if (!Exists(Candidate))
        {
            return Candidate;
        }
    }
    return BaseTitle + "_Extra";
}

void ComponentInstanceStorage::UnlinkFromEnclosing(uint32_t TargetZeroIdx) noexcept
{
    if (TargetZeroIdx >= Headers.size() || !Headers[TargetZeroIdx].Occupied)
    {
        return;
    }
    const uint32_t EnclosingIdx = Headers[TargetZeroIdx].Header.EnclosingIndex;
    if (EnclosingIdx == InvalidHierarchyIndex || EnclosingIdx >= Headers.size())
    {
        Headers[TargetZeroIdx].Header.EnclosingIndex = InvalidHierarchyIndex;
        Headers[TargetZeroIdx].Header.NextPeerIndex  = InvalidHierarchyIndex;
        return;
    }

    uint32_t Cursor = Headers[EnclosingIdx].Header.FirstNestedIndex;
    uint32_t Prev   = InvalidHierarchyIndex;
    while (Cursor != InvalidHierarchyIndex && Cursor < Headers.size())
    {
        if (Cursor == TargetZeroIdx)
        {
            const uint32_t Next = Headers[Cursor].Header.NextPeerIndex;
            if (Prev == InvalidHierarchyIndex)
            {
                Headers[EnclosingIdx].Header.FirstNestedIndex = Next;
            }
            else
            {
                Headers[Prev].Header.NextPeerIndex = Next;
            }
            break;
        }
        Prev   = Cursor;
        Cursor = Headers[Cursor].Header.NextPeerIndex;
    }

    Headers[TargetZeroIdx].Header.EnclosingIndex = InvalidHierarchyIndex;
    Headers[TargetZeroIdx].Header.NextPeerIndex  = InvalidHierarchyIndex;
}

void ComponentInstanceStorage::LinkUnderEnclosing(uint32_t TargetZeroIdx, uint32_t EnclosingZeroIdx) noexcept
{
    if (TargetZeroIdx >= Headers.size() || EnclosingZeroIdx >= Headers.size())
    {
        return;
    }
    Headers[TargetZeroIdx].Header.EnclosingIndex = EnclosingZeroIdx;
    Headers[TargetZeroIdx].Header.NextPeerIndex  = InvalidHierarchyIndex;

    uint32_t First = Headers[EnclosingZeroIdx].Header.FirstNestedIndex;
    if (First == InvalidHierarchyIndex)
    {
        Headers[EnclosingZeroIdx].Header.FirstNestedIndex = TargetZeroIdx;
    }
    else
    {
        uint32_t Tail = First;
        while (Headers[Tail].Header.NextPeerIndex != InvalidHierarchyIndex)
        {
            Tail = Headers[Tail].Header.NextPeerIndex;
        }
        Headers[Tail].Header.NextPeerIndex = TargetZeroIdx;
    }

    RefreshDepthLevels(TargetZeroIdx, static_cast<uint16_t>(Headers[EnclosingZeroIdx].Header.DepthLevel + 1u));
}

void ComponentInstanceStorage::RefreshDepthLevels(uint32_t RootZeroIdx, uint16_t Depth) noexcept
{
    if (RootZeroIdx >= Headers.size() || !Headers[RootZeroIdx].Occupied)
    {
        return;
    }
    Headers[RootZeroIdx].Header.DepthLevel = Depth;
    uint32_t Nested = Headers[RootZeroIdx].Header.FirstNestedIndex;
    while (Nested != InvalidHierarchyIndex && Nested < Headers.size())
    {
        RefreshDepthLevels(Nested, static_cast<uint16_t>(Depth + 1u));
        Nested = Headers[Nested].Header.NextPeerIndex;
    }
}

bool ComponentInstanceStorage::QueryAncestorMatch(uint32_t CandidateZeroIdx, uint32_t ProbeZeroIdx) const noexcept
{
    uint32_t Cursor = CandidateZeroIdx;
    uint32_t Guard  = 0u;
    while (Cursor != InvalidHierarchyIndex && Cursor < Headers.size() && Guard++ < Headers.size())
    {
        if (Cursor == ProbeZeroIdx)
        {
            return true;
        }
        Cursor = Headers[Cursor].Header.EnclosingIndex;
    }
    return false;
}

ComponentInstanceToken ComponentInstanceStorage::AppendInstance(
    ComponentCategory      Category,
    std::string_view       RequestedTitle,
    ComponentInstanceToken EnclosingToken,
    ComponentTransform     InitialTransform)
{
    if (Category == ComponentCategory::SpatialOrigin)
    {
        return OriginToken;
    }

    const bool HasTransform = QueryCategoryHasTransform(Category);
    uint32_t EnclosingZeroIdx = InvalidHierarchyIndex;
    if (HasTransform)
    {
        if (AuthenticateToken(EnclosingToken))
        {
            const uint32_t CandidateIdx = EnclosingToken.SlotIndex - 1u;
            if (QueryCategoryHasTransform(Headers[CandidateIdx].Header.Category))
            {
                EnclosingZeroIdx = CandidateIdx;
            }
        }
        if (EnclosingZeroIdx == InvalidHierarchyIndex)
        {
            EnclosingZeroIdx = OriginToken.SlotIndex - 1u;
        }
    }

    uint32_t SlotZeroIdx = 0u;
    if (!FreeSlots.empty())
    {
        SlotZeroIdx = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        SlotZeroIdx = static_cast<uint32_t>(Headers.size());
        Headers.emplace_back();
    }

    SlotRecord& Rec = Headers[SlotZeroIdx];
    Rec.Occupied                   = true;
    Rec.Gen                        = (Rec.Gen == 0u) ? 1u : Rec.Gen;
    Rec.Header                     = ComponentInstanceHeader{};
    Rec.Header.Token               = { SlotZeroIdx + 1u, Rec.Gen };
    Rec.Header.Category            = Category;
    Rec.Header.Title               = ConstructUniqueTitle(RequestedTitle.empty() ? QueryCategoryLabel(Category) : RequestedTitle);
    Rec.Header.CategorySlotIndex   = AllocateTypedSlot(Category);
    Rec.Header.LocalTransform      = HasTransform ? InitialTransform : ComponentTransform{};
    Rec.Header.VisibilityEnabled   = true;
    Rec.Header.EnclosingIndex      = InvalidHierarchyIndex;
    Rec.Header.FirstNestedIndex    = InvalidHierarchyIndex;
    Rec.Header.NextPeerIndex       = InvalidHierarchyIndex;

    if (HasTransform)
    {
        LinkUnderEnclosing(SlotZeroIdx, EnclosingZeroIdx);
    }

    return Rec.Header.Token;
}

bool ComponentInstanceStorage::AttachInstance(ComponentInstanceToken TargetToken, ComponentInstanceToken NewEnclosingToken) noexcept
{
    if (!AuthenticateToken(TargetToken) || !AuthenticateToken(NewEnclosingToken))
    {
        return false;
    }
    if (TargetToken == OriginToken || TargetToken == NewEnclosingToken)
    {
        return false;
    }

    const uint32_t TargetIdx    = TargetToken.SlotIndex - 1u;
    const uint32_t EnclosingIdx = NewEnclosingToken.SlotIndex - 1u;
    if (!QueryCategoryHasTransform(Headers[TargetIdx].Header.Category) ||
        !QueryCategoryHasTransform(Headers[EnclosingIdx].Header.Category))
    {
        return false;
    }

    if (QueryAncestorMatch(EnclosingIdx, TargetIdx))
    {
        return false;
    }

    UnlinkFromEnclosing(TargetIdx);
    LinkUnderEnclosing(TargetIdx, EnclosingIdx);
    return true;
}

bool ComponentInstanceStorage::RemoveInstance(ComponentInstanceToken TargetToken) noexcept
{
    if (!AuthenticateToken(TargetToken) || TargetToken == OriginToken)
    {
        return false;
    }

    const uint32_t TargetIdx    = TargetToken.SlotIndex - 1u;
    const uint32_t EnclosingIdx = Headers[TargetIdx].Header.EnclosingIndex;

    // Lift nested spatial instances to TargetIdx's enclosing parent (matching ComponentViewer behavior)
    if (QueryCategoryHasTransform(Headers[TargetIdx].Header.Category))
    {
        const uint32_t FallbackParent = (EnclosingIdx != InvalidHierarchyIndex) ? EnclosingIdx : (OriginToken.SlotIndex - 1u);
        std::vector<uint32_t> NestedIndices {};
        uint32_t Cursor = Headers[TargetIdx].Header.FirstNestedIndex;
        while (Cursor != InvalidHierarchyIndex && Cursor < Headers.size())
        {
            NestedIndices.push_back(Cursor);
            Cursor = Headers[Cursor].Header.NextPeerIndex;
        }
        Headers[TargetIdx].Header.FirstNestedIndex = InvalidHierarchyIndex;
        for (const uint32_t NestedIdx : NestedIndices)
        {
            Headers[NestedIdx].Header.EnclosingIndex = InvalidHierarchyIndex;
            Headers[NestedIdx].Header.NextPeerIndex  = InvalidHierarchyIndex;
            LinkUnderEnclosing(NestedIdx, FallbackParent);
        }
        UnlinkFromEnclosing(TargetIdx);
    }

    Headers[TargetIdx].Occupied = false;
    ++Headers[TargetIdx].Gen;
    FreeSlots.push_back(TargetIdx);
    return true;
}

ComponentInstanceToken ComponentInstanceStorage::DuplicateInstance(ComponentInstanceToken TargetToken)
{
    if (!AuthenticateToken(TargetToken) || TargetToken == OriginToken)
    {
        return {};
    }

    const uint32_t TargetIdx = TargetToken.SlotIndex - 1u;
    const ComponentInstanceHeader SourceHeader = Headers[TargetIdx].Header;
    const ComponentInstanceToken EnclosingToken = (SourceHeader.EnclosingIndex != InvalidHierarchyIndex)
        ? Headers[SourceHeader.EnclosingIndex].Header.Token
        : OriginToken;

    ComponentTransform OffsetTransform = SourceHeader.LocalTransform;
    if (QueryCategoryHasTransform(SourceHeader.Category))
    {
        OffsetTransform.LocationMeters.X += 0.25f;
    }

    const ComponentInstanceToken CopyToken = AppendInstance(
        SourceHeader.Category,
        SourceHeader.Title + "Copy",
        EnclosingToken,
        OffsetTransform);

    ComponentInstanceHeader* CopyHeader = ResolveHeader(CopyToken);
    if (CopyHeader == nullptr)
    {
        return {};
    }
    CopyHeader->VisibilityEnabled = SourceHeader.VisibilityEnabled;
    CopyHeader->HiddenInPlay      = SourceHeader.HiddenInPlay;
    CopyHeader->TagCsv            = SourceHeader.TagCsv;

    switch (SourceHeader.Category)
    {
        case ComponentCategory::GeometryInstance:
            Geometries[CopyHeader->CategorySlotIndex] = Geometries[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::ConstraintInstance:
            Constraints[CopyHeader->CategorySlotIndex] = Constraints[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::ParticleInstance:
            Particles[CopyHeader->CategorySlotIndex] = Particles[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::FluidInstance:
            Fluids[CopyHeader->CategorySlotIndex] = Fluids[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::LightInstance:
            Lights[CopyHeader->CategorySlotIndex] = Lights[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::CameraInstance:
            Cameras[CopyHeader->CategorySlotIndex] = Cameras[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::CollisionVolume:
            Collisions[CopyHeader->CategorySlotIndex] = Collisions[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::DirectionArrow:
            Arrows[CopyHeader->CategorySlotIndex] = Arrows[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::VehicleDynamics:
            Vehicles[CopyHeader->CategorySlotIndex] = Vehicles[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::RotationalMotion:
            Rotations[CopyHeader->CategorySlotIndex] = Rotations[SourceHeader.CategorySlotIndex];
            break;
        case ComponentCategory::SpatialOrigin:
        case ComponentCategory::TransformPivot:
            break;
    }

    return CopyToken;
}

bool ComponentInstanceStorage::RenameInstance(ComponentInstanceToken TargetToken, std::string_view RequestedTitle)
{
    if (!AuthenticateToken(TargetToken))
    {
        return false;
    }
    const std::string Cleaned = TrimWhitespace(RequestedTitle);
    if (Cleaned.empty())
    {
        return false;
    }
    const uint32_t TargetIdx = TargetToken.SlotIndex - 1u;
    for (uint32_t Idx = 0u; Idx < Headers.size(); ++Idx)
    {
        if (Idx != TargetIdx && Headers[Idx].Occupied && Headers[Idx].Header.Title == Cleaned)
        {
            return false;
        }
    }
    Headers[TargetIdx].Header.Title = Cleaned;
    return true;
}

const ComponentInstanceHeader* ComponentInstanceStorage::InspectHeader(ComponentInstanceToken Token) const noexcept
{
    return AuthenticateToken(Token) ? &Headers[Token.SlotIndex - 1u].Header : nullptr;
}

ComponentInstanceHeader* ComponentInstanceStorage::ResolveHeader(ComponentInstanceToken Token) noexcept
{
    return AuthenticateToken(Token) ? &Headers[Token.SlotIndex - 1u].Header : nullptr;
}

const ComponentInstanceHeader* ComponentInstanceStorage::InspectHeaderByIndex(uint32_t SlotZeroIndex) const noexcept
{
    if (SlotZeroIndex >= Headers.size() || !Headers[SlotZeroIndex].Occupied)
    {
        return nullptr;
    }
    return &Headers[SlotZeroIndex].Header;
}

ComponentInstanceToken ComponentInstanceStorage::FindTokenByTitle(std::string_view Title) const noexcept
{
    for (const SlotRecord& Rec : Headers)
    {
        if (Rec.Occupied && Rec.Header.Title == Title)
        {
            return Rec.Header.Token;
        }
    }
    return {};
}

#define FRONTIER_DEFINE_CATEGORY_ACCESSORS(FnSuffix, CatEnum, VecMember, StructType)              \
    StructType* ComponentInstanceStorage::Resolve##FnSuffix(ComponentInstanceToken Token) noexcept \
    {                                                                                             \
        const ComponentInstanceHeader* H = InspectHeader(Token);                                  \
        if (H == nullptr || H->Category != ComponentCategory::CatEnum)                            \
        {                                                                                         \
            return nullptr;                                                                       \
        }                                                                                         \
        return &VecMember[H->CategorySlotIndex];                                                  \
    }                                                                                             \
    const StructType* ComponentInstanceStorage::Inspect##FnSuffix(ComponentInstanceToken Token) const noexcept \
    {                                                                                             \
        const ComponentInstanceHeader* H = InspectHeader(Token);                                  \
        if (H == nullptr || H->Category != ComponentCategory::CatEnum)                            \
        {                                                                                         \
            return nullptr;                                                                       \
        }                                                                                         \
        return &VecMember[H->CategorySlotIndex];                                                  \
    }

FRONTIER_DEFINE_CATEGORY_ACCESSORS(Geometry, GeometryInstance, Geometries, GeometryComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Constraint, ConstraintInstance, Constraints, ConstraintComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Particle, ParticleInstance, Particles, ParticleComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Fluid, FluidInstance, Fluids, FluidComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Light, LightInstance, Lights, LightComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Camera, CameraInstance, Cameras, CameraComponentSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Collision, CollisionVolume, Collisions, CollisionVolumeSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Arrow, DirectionArrow, Arrows, DirectionArrowSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Vehicle, VehicleDynamics, Vehicles, VehicleDynamicsSettings)
FRONTIER_DEFINE_CATEGORY_ACCESSORS(Rotation, RotationalMotion, Rotations, RotationalMotionSettings)

#undef FRONTIER_DEFINE_CATEGORY_ACCESSORS

void ComponentInstanceStorage::WalkSpatialHierarchy(const std::function<void(const ComponentInstanceHeader&)>& Visitor) const
{
    if (!AuthenticateToken(OriginToken))
    {
        return;
    }

    auto VisitRecursive = [&](auto& Self, uint32_t ZeroIdx) -> void
    {
        if (ZeroIdx >= Headers.size() || !Headers[ZeroIdx].Occupied)
        {
            return;
        }
        Visitor(Headers[ZeroIdx].Header);
        uint32_t Nested = Headers[ZeroIdx].Header.FirstNestedIndex;
        while (Nested != InvalidHierarchyIndex && Nested < Headers.size())
        {
            Self(Self, Nested);
            Nested = Headers[Nested].Header.NextPeerIndex;
        }
    };

    VisitRecursive(VisitRecursive, OriginToken.SlotIndex - 1u);
}

void ComponentInstanceStorage::CollectBehaviourTokens(std::vector<ComponentInstanceToken>& OutTokens) const
{
    OutTokens.clear();
    for (const SlotRecord& Rec : Headers)
    {
        if (Rec.Occupied && !QueryCategoryHasTransform(Rec.Header.Category))
        {
            OutTokens.push_back(Rec.Header.Token);
        }
    }
}

void ComponentInstanceStorage::CollectNestedTokens(ComponentInstanceToken EnclosingToken, std::vector<ComponentInstanceToken>& OutTokens) const
{
    OutTokens.clear();
    if (!AuthenticateToken(EnclosingToken))
    {
        return;
    }
    uint32_t Cursor = Headers[EnclosingToken.SlotIndex - 1u].Header.FirstNestedIndex;
    while (Cursor != InvalidHierarchyIndex && Cursor < Headers.size())
    {
        if (Headers[Cursor].Occupied)
        {
            OutTokens.push_back(Headers[Cursor].Header.Token);
        }
        Cursor = Headers[Cursor].Header.NextPeerIndex;
    }
}

ClassSummaryMetrics ComponentInstanceStorage::ComputeSummaryMetrics() const noexcept
{
    ClassSummaryMetrics Metrics {};
    for (const SlotRecord& Rec : Headers)
    {
        if (!Rec.Occupied)
        {
            continue;
        }
        ++Metrics.TotalComponents;
        switch (Rec.Header.Category)
        {
            case ComponentCategory::GeometryInstance:   ++Metrics.GeometryCount;   break;
            case ComponentCategory::ConstraintInstance: ++Metrics.ConstraintCount; break;
            case ComponentCategory::LightInstance:      ++Metrics.LightCount;      break;
            case ComponentCategory::CollisionVolume:    ++Metrics.CollisionCount;  break;
            case ComponentCategory::ParticleInstance:   ++Metrics.ParticleCount;   break;
            case ComponentCategory::FluidInstance:      ++Metrics.FluidCount;      break;
            case ComponentCategory::VehicleDynamics:
            case ComponentCategory::RotationalMotion:   ++Metrics.BehaviourCount;  break;
            default: break;
        }
    }
    return Metrics;
}

bool ComponentInstanceStorage::QuerySlotLive(uint32_t SlotZeroIndex) const noexcept
{
    return SlotZeroIndex < Headers.size() && Headers[SlotZeroIndex].Occupied;
}

bool ComponentInstanceStorage::QueryDockTagAccepted(std::string_view DockKey, std::string_view TagCsv) noexcept
{
    const std::string CleanKey = ToLowerAscii(TrimWhitespace(DockKey));
    if (CleanKey.empty())
    {
        return true;
    }

    std::size_t Start = 0u;
    while (Start <= TagCsv.size())
    {
        const std::size_t Comma = TagCsv.find(',', Start);
        const std::size_t End   = (Comma == std::string_view::npos) ? TagCsv.size() : Comma;
        const std::string Tag   = ToLowerAscii(TrimWhitespace(TagCsv.substr(Start, End - Start)));
        if (!Tag.empty() && Tag == CleanKey)
        {
            return true;
        }
        if (Comma == std::string_view::npos)
        {
            break;
        }
        Start = Comma + 1u;
    }
    return false;
}

} // namespace Frontier::ComponentClass
