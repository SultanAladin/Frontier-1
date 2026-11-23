/*============================================================================================================================================
                                                           CUBICPATHEDITING.CPP
============================================================================================================================================*/
// 🧩 Preserve GPU source topology, independent aliases and bounded cubic storage while authoring points and tangent controls.

#include "CubicPathEditing.h"
#include <limits>
#include <numbers>

namespace Frontier
{
namespace
{
using Coordinate = std::array<float, 2>;

const VectorAuthoringLayer &ResolveLayer(const VectorAuthoringDocument &Document, uint32_t Token)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Document.Layers.end()) throw std::runtime_error("Unknown cubic path source token.");
    return *Position;
}

Coordinate ProjectCoordinate(const VectorCoverageInstruction &Source, Coordinate Point, bool ToWorld)
{
    const float θ = Source.Transform[0];
    if (ToWorld)
    {
        const float X = Point[0] * Source.Bounds[2], Y = Point[1] * Source.Bounds[3];
        return {Source.Bounds[0] + X * std::cos(θ) - Y * std::sin(θ), Source.Bounds[1] + X * std::sin(θ) + Y * std::cos(θ)};
    }
    const float X = Point[0] - Source.Bounds[0], Y = Point[1] - Source.Bounds[1];
    return {(X * std::cos(θ) + Y * std::sin(θ)) / Source.Bounds[2], (-X * std::sin(θ) + Y * std::cos(θ)) / Source.Bounds[3]};
}

bool Coincident(Coordinate First, Coordinate Second)
{
    return std::abs(First[0] - Second[0]) < .000001f && std::abs(First[1] - Second[1]) < .000001f;
}

void EnforceEditable(const VectorAuthoringDocument &Document, const VectorAuthoringLayer &Layer)
{
    if (!QueryVectorAuthoringVisibility(Document, Layer) || QueryVectorAuthoringProtection(Document, Layer))
        throw std::runtime_error("Cubic path source is hidden or protected.");
    if (Document.Instructions[Layer.Root].Topology[0] > 6)
        throw std::runtime_error("Edit a retained source path, not an aggregate Boolean expression.");
}

void EncodePath(VectorAuthoringDocument &Document, VectorCoverageInstruction &Source, const CubicPath &Path)
{
    ValidateCubicPath(Path);
    // 📝 Keep the existing rotation/style; normalize controls in its local axes after recomputing finite extents.
    const float θ = Source.Transform[0];
    Coordinate Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()}, Maximum{-Minimum[0], -Minimum[1]};
    for (const auto &Anchor : Path.Anchors) for (const auto &Point : {Anchor.Position, Anchor.Incoming, Anchor.Outgoing})
    {
        const Coordinate Local{Point[0] * std::cos(θ) + Point[1] * std::sin(θ), -Point[0] * std::sin(θ) + Point[1] * std::cos(θ)};
        for (size_t Axis = 0; Axis < 2; ++Axis)
        {
            Minimum[Axis] = std::min(Minimum[Axis], Local[Axis]);
            Maximum[Axis] = std::max(Maximum[Axis], Local[Axis]);
        }
    }
    const float X = (Minimum[0] + Maximum[0]) * .5f, Y = (Minimum[1] + Maximum[1]) * .5f;
    Source.Bounds = {X * std::cos(θ) - Y * std::sin(θ), X * std::sin(θ) + Y * std::cos(θ),
        std::max(.01f, Maximum[0] - Minimum[0]), std::max(.01f, Maximum[1] - Minimum[1])};
    const auto Offset = uint32_t(Document.Segments.size());
    const size_t Count = Path.Closed ? Path.Anchors.size() : Path.Anchors.size() - 1;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto &First = Path.Anchors[Index], &Second = Path.Anchors[(Index + 1) % Path.Anchors.size()];
        const auto Start = ProjectCoordinate(Source, First.Position, false), Outgoing = ProjectCoordinate(Source, First.Outgoing, false);
        const auto Incoming = ProjectCoordinate(Source, Second.Incoming, false), Finish = ProjectCoordinate(Source, Second.Position, false);
        Document.Segments.push_back({{Start[0], Start[1], Outgoing[0], Outgoing[1]}, {Incoming[0], Incoming[1], Finish[0], Finish[1]}});
    }
    const uint32_t StrokeColourFlag = Source.Topology[3] & VectorCoverageStrokeColourExplicit;
    Source.Topology = {6, Offset, uint32_t(Count), Path.Winding | StrokeColourFlag};
    if (!Path.Closed) Source.Paint[3] = 0;
}

void ReclaimSegments(VectorAuthoringDocument &Document)
{
    std::vector<bool> Retained(Document.Segments.size());
    const auto Mark = [&](const VectorCoverageInstruction &Source)
    {
        if (Source.Topology[0] != 6) return;
        const size_t Start = Source.Topology[1], Count = Source.Topology[2];
        if (!Count || Start + Count > Retained.size()) throw std::runtime_error("Invalid retained cubic segment range.");
        for (size_t Index = Start; Index < Start + Count; ++Index) Retained[Index] = true;
    };
    for (const auto &Source : Document.Instructions) Mark(Source);
    for (const auto &Layer : Document.Layers) if (Layer.Mask) Mark(*Layer.Mask);
    std::vector<uint32_t> Relocation(Retained.size());
    std::vector<VectorCubicSegment> Segments;
    for (size_t Index = 0; Index < Retained.size(); ++Index) if (Retained[Index])
    {
        Relocation[Index] = uint32_t(Segments.size()); Segments.push_back(Document.Segments[Index]);
    }
    const auto Relocate = [&](VectorCoverageInstruction &Source)
    {
        if (Source.Topology[0] == 6) Source.Topology[1] = Relocation[Source.Topology[1]];
    };
    for (auto &Source : Document.Instructions) Relocate(Source);
    for (auto &Layer : Document.Layers) if (Layer.Mask) Relocate(*Layer.Mask);
    Document.Segments = std::move(Segments);
}

void ReclaimPathStorage(VectorAuthoringDocument &Document)
{
    std::vector<bool> Reachable(Document.Instructions.size());
    const auto Mark = [&](auto &&Self, uint32_t Root) -> void
    {
        if (Reachable.at(Root)) return;
        Reachable[Root] = true;
        const auto &Instruction = Document.Instructions[Root];
        if (Instruction.Topology[0] > 6)
        {
            Self(Self, Instruction.Topology[1]);
            Self(Self, Instruction.Topology[2]);
        }
    };
    for (const auto &Entry : Document.Layers) Mark(Mark, Entry.Root);
    // Unreachable cubic references must not retain their old segment ranges.
    for (size_t Index = 0; Index < Reachable.size(); ++Index) if (!Reachable[Index]) Document.Instructions[Index] = VectorCoverageInstruction{};
    ReclaimSegments(Document);
    ReclaimVectorAuthoringSources(Document);
}

Coordinate Interpolate(Coordinate First, Coordinate Second, float Parameter)
{
    return {First[0] + (Second[0] - First[0]) * Parameter, First[1] + (Second[1] - First[1]) * Parameter};
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETAINED PATH DECODING
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 2> ConstrainCubicDirection(std::array<float, 2> Origin, std::array<float, 2> Destination)
{
    for (const auto &Coordinate : {Origin, Destination})
        for (float Position : Coordinate) if (!std::isfinite(Position) || std::abs(Position) > 16384)
            throw std::runtime_error("Constrained cubic coordinates exceed the finite authoring domain.");
    const float ΔX = Destination[0] - Origin[0], ΔY = Destination[1] - Origin[1];
    const float Radius = std::hypot(ΔX, ΔY);
    if (Radius == 0) return Origin;
    constexpr float Step = std::numbers::pi_v<float> / 4;
    const float θ = std::round(std::atan2(ΔY, ΔX) / Step) * Step;
    return {Origin[0] + Radius * std::cos(θ), Origin[1] + Radius * std::sin(θ)};
}

void ValidateCubicPath(const CubicPath &Path)
{
    if (Path.Anchors.size() < (Path.Closed ? 3u : 2u) || Path.Anchors.size() > 400 || Path.Winding > 1)
        throw std::runtime_error("A path needs 2 open or 3 closed anchors, at most 400, and a supported winding rule.");
    for (const auto &Anchor : Path.Anchors) for (const auto &Point : {Anchor.Position, Anchor.Incoming, Anchor.Outgoing})
        for (float Position : Point) if (!std::isfinite(Position) || std::abs(Position) > 16384)
            throw std::runtime_error("Cubic coordinates exceed the finite authoring domain.");
}

void ReverseCubicPath(CubicPath &Path)
{
    ValidateCubicPath(Path);
    std::reverse(Path.Anchors.begin(), Path.Anchors.end());
    for (auto &Anchor : Path.Anchors) std::swap(Anchor.Incoming, Anchor.Outgoing);
}

CubicPath DecodeCubicPath(const VectorAuthoringDocument &Document, uint32_t Token)
{
    const auto &Layer = ResolveLayer(Document, Token);
    const auto &Source = Document.Instructions[Layer.Root];
    if (Source.Topology[0] != 6) throw std::runtime_error("Select or convert one editable cubic path.");
    CubicPath Path;
    Path.Winding = Source.Topology[3] & ~VectorCoverageStrokeColourExplicit;
    const auto Start = size_t(Source.Topology[1]), Count = size_t(Source.Topology[2]);
    const auto &First = Document.Segments[Start], &Last = Document.Segments[Start + Count - 1];
    Path.Closed = Coincident({First.StartControl[0], First.StartControl[1]}, {Last.ControlEnd[2], Last.ControlEnd[3]});
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto &Segment = Document.Segments[Start + Index];
        const auto Anchor = ProjectCoordinate(Source, {Segment.StartControl[0], Segment.StartControl[1]}, true);
        if (Index && !Coincident({Document.Segments[Start + Index - 1].ControlEnd[2], Document.Segments[Start + Index - 1].ControlEnd[3]}, {Segment.StartControl[0], Segment.StartControl[1]}))
            throw std::runtime_error("Multiple cubic subpaths require the compound-path editor; source is retained unchanged.");
        Coordinate Incoming = Anchor;
        if (Index || Path.Closed)
        {
            const auto &Previous = Document.Segments[Start + (Index ? Index - 1 : Count - 1)];
            Incoming = ProjectCoordinate(Source, {Previous.ControlEnd[0], Previous.ControlEnd[1]}, true);
        }
        Path.Anchors.push_back({Anchor, Incoming, ProjectCoordinate(Source, {Segment.StartControl[2], Segment.StartControl[3]}, true)});
    }
    if (!Path.Closed)
    {
        const auto End = ProjectCoordinate(Source, {Last.ControlEnd[2], Last.ControlEnd[3]}, true);
        Path.Anchors.push_back({End, ProjectCoordinate(Source, {Last.ControlEnd[0], Last.ControlEnd[1]}, true), End});
    }
    ValidateCubicPath(Path);
    return Path;
}

CubicPath DerivePrimitiveCubicPath(const VectorAuthoringDocument &Document, uint32_t Token)
{
    const auto &Layer = ResolveLayer(Document, Token);
    const auto &Source = Document.Instructions[Layer.Root];
    if (Source.Topology[0] == 6) return DecodeCubicPath(Document, Token);
    if (Source.Topology[0] > 5) throw std::runtime_error("Convert an independent primitive, not a Boolean expression.");
    CubicPath Path; Path.Closed = true;
    if (Source.Topology[0] == 1)
    {
        constexpr float κ = .2761423749f;
        Path.Anchors = {{{0, -.5f}, {-κ, -.5f}, {κ, -.5f}}, {{.5f, 0}, {.5f, -κ}, {.5f, κ}},
            {{0, .5f}, {κ, .5f}, {-κ, .5f}}, {{-.5f, 0}, {-.5f, κ}, {-.5f, -κ}}};
    }
    else if (Source.Topology[0] == 0)
    {
        for (Coordinate Point : {Coordinate{-.5f, -.5f}, Coordinate{.5f, -.5f}, Coordinate{.5f, .5f}, Coordinate{-.5f, .5f}})
            Path.Anchors.push_back({Point, Point, Point});
    }
    else
    {
        uint32_t Count = Source.Topology[0] == 2 ? 4 : Source.Topology[0] == 3 ? 3 : uint32_t(Source.Paint[2]);
        if (Source.Topology[0] == 5) Count *= 2;
        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            const float θ = -std::numbers::pi_v<float> * .5f + 2 * std::numbers::pi_v<float> * float(Index) / float(Count);
            const float Radius = Source.Topology[0] == 5 && Index % 2 ? Source.Paint[1] : 1;
            const Coordinate Point{std::cos(θ) * .5f * Radius, std::sin(θ) * .5f * Radius};
            Path.Anchors.push_back({Point, Point, Point});
        }
    }
    for (auto &Anchor : Path.Anchors)
    {
        Anchor.Position = ProjectCoordinate(Source, Anchor.Position, true);
        Anchor.Incoming = ProjectCoordinate(Source, Anchor.Incoming, true);
        Anchor.Outgoing = ProjectCoordinate(Source, Anchor.Outgoing, true);
    }
    return Path;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ATOMIC SOURCE REPLACEMENT
//------------------------------------------------------------------------------------------------------------------------

uint32_t ConstructCubicPath(
    VectorAuthoringDocument   &Document,
    const CubicPath          &Path,
    VectorCoverageInstruction Style,
    std::string               Caption)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    auto Replacement = Document;
    if (Replacement.TokenClock == UINT32_MAX) throw std::runtime_error("Cubic source token clock exhausted.");
    EncodePath(Replacement, Style, Path);
    VectorAuthoringLayer Layer; Layer.Token = ++Replacement.TokenClock; Layer.Name = std::move(Caption);
    Layer.Root = uint32_t(Replacement.Instructions.size());
    Replacement.Instructions.push_back(Style); Replacement.Layers.push_back(Layer);
    ReclaimPathStorage(Replacement);
    NormalizeVectorAuthoringCaptions(Replacement);
    Document = std::move(Replacement);
    return Layer.Token;
}

void ReplaceCubicPath(VectorAuthoringDocument &Document, uint32_t Token, const CubicPath &Path)
{
    const auto &Layer = ResolveLayer(Document, Token);
    EnforceEditable(Document, Layer);
    if (Document.Instructions[Layer.Root].Topology[0] == 6 && DecodeCubicPath(Document, Token) == Path) return;
    auto Replacement = Document;
    auto Source = Document.Instructions[Layer.Root];
    EncodePath(Replacement, Source, Path);
    // 📝 A replacement root avoids mutating any other layer or mask that aliases the original instruction.
    auto Position = std::find_if(Replacement.Layers.begin(), Replacement.Layers.end(), [Token](const auto &Entry) { return Entry.Token == Token; });
    Position->Root = uint32_t(Replacement.Instructions.size());
    Replacement.Instructions.push_back(Source);
    ReclaimPathStorage(Replacement);
    Document = std::move(Replacement);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ANCHOR AND TANGENT GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

uint32_t IntegrateCubicPaths(
    VectorAuthoringDocument &Document,
    uint32_t                 RetainedToken,
    uint32_t                 ConsumedToken,
    bool                     RetainedStart,
    bool                     ConsumedEnd)
{
    if (RetainedToken == ConsumedToken) throw std::runtime_error("Join two distinct retained paths.");
    const auto &FirstLayer = ResolveLayer(Document, RetainedToken);
    const auto &SecondLayer = ResolveLayer(Document, ConsumedToken);
    EnforceEditable(Document, FirstLayer); EnforceEditable(Document, SecondLayer);
    const auto FirstIndex = size_t(&FirstLayer - Document.Layers.data()), SecondIndex = size_t(&SecondLayer - Document.Layers.data());
    if (FirstIndex + 1 != SecondIndex && SecondIndex + 1 != FirstIndex)
        throw std::runtime_error("Join adjacent layers only; intervening stacking must not change silently.");
    if (FirstLayer.Mask || SecondLayer.Mask) throw std::runtime_error("Joining independently masked paths is unavailable; masks remain unchanged.");
    if (FirstLayer.EnclosureToken != SecondLayer.EnclosureToken || FirstLayer.SurfaceChannels != SecondLayer.SurfaceChannels)
        throw std::runtime_error("Joined paths must have the same enclosing group and surface-channel contributions.");
    const auto &FirstSource = Document.Instructions[FirstLayer.Root], &SecondSource = Document.Instructions[SecondLayer.Root];
    if (FirstSource.Colour != SecondSource.Colour || FirstSource.ColourEnd != SecondSource.ColourEnd || FirstSource.StrokeColour != SecondSource.StrokeColour
        || FirstSource.Paint != SecondSource.Paint || FirstSource.Transform[1] != SecondSource.Transform[1])
        throw std::runtime_error("Joined paths must use matching paint and stroke parameters.");
    if (FirstSource.Transform[3] != 0 || SecondSource.Transform[3] != 0 || FirstSource.Transform[2] != 1
        || SecondSource.Transform[2] != 1 || FirstSource.Colour[3] != 1 || SecondSource.Colour[3] != 1)
        throw std::runtime_error("Joining currently requires opaque solid paint; gradients and transparency remain unchanged.");
    auto First = DecodeCubicPath(Document, RetainedToken), Second = DecodeCubicPath(Document, ConsumedToken);
    if (First.Closed || Second.Closed) throw std::runtime_error("Join two open paths, not closed contours.");
    if (First.Winding != Second.Winding) throw std::runtime_error("Joined paths must share the same winding rule.");
    if (RetainedStart) ReverseCubicPath(First);
    if (ConsumedEnd) ReverseCubicPath(Second);
    if (Coincident(First.Anchors.back().Position, Second.Anchors.front().Position))
    {
        // 📝 Merge one shared endpoint; keep the incoming and outgoing controls from their respective original curves.
        First.Anchors.back().Outgoing = Second.Anchors.front().Outgoing;
        First.Anchors.insert(First.Anchors.end(), Second.Anchors.begin() + 1, Second.Anchors.end());
    }
    else
    {
        // 📝 Unused open-end controls become a straight bridge; used controls remain unchanged.
        First.Anchors.back().Outgoing = First.Anchors.back().Position;
        Second.Anchors.front().Incoming = Second.Anchors.front().Position;
        First.Anchors.insert(First.Anchors.end(), Second.Anchors.begin(), Second.Anchors.end());
    }
    if (Coincident(First.Anchors.front().Position, First.Anchors.back().Position))
    {
        First.Anchors.front().Incoming = First.Anchors.back().Incoming;
        First.Anchors.pop_back(); First.Closed = true;
    }
    ValidateCubicPath(First);
    if (RetainedStart) ReverseCubicPath(First);
    auto Replacement = Document;
    std::erase_if(Replacement.Layers, [ConsumedToken](const auto &Layer) { return Layer.Token == ConsumedToken; });
    ReplaceCubicPath(Replacement, RetainedToken, First);
    ReclaimPathStorage(Replacement);
    Document = std::move(Replacement);
    return RetainedToken;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONTROL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

void TranslateCubicControl(
    CubicPath             &Path,
    size_t                 AnchorIndex,
    CubicControl           Control,
    std::array<float, 2>   Destination,
    bool                   Independent)
{
    ValidateCubicPath(Path);
    if (uint32_t(Control) > uint32_t(CubicControl::Outgoing)) throw std::runtime_error("Invalid cubic control category.");
    auto Replacement = Path;
    auto &Anchor = Replacement.Anchors.at(AnchorIndex);
    for (float Position : Destination) if (!std::isfinite(Position)) throw std::runtime_error("Nonfinite cubic control destination.");
    if (Control == CubicControl::Anchor)
    {
        const Coordinate Δ{Destination[0] - Anchor.Position[0], Destination[1] - Anchor.Position[1]};
        for (auto *Point : {&Anchor.Incoming, &Anchor.Outgoing}) for (size_t Axis = 0; Axis < 2; ++Axis) (*Point)[Axis] += Δ[Axis];
        Anchor.Position = Destination;
    }
    else
    {
        auto &Moved = Control == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
        auto &Opposite = Control == CubicControl::Incoming ? Anchor.Outgoing : Anchor.Incoming;
        Moved = Destination;
        if (!Independent) for (size_t Axis = 0; Axis < 2; ++Axis) Opposite[Axis] = 2 * Anchor.Position[Axis] - Destination[Axis];
    }
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

CubicSegmentProjection ProjectCubicSegment(const CubicPath &Path, Coordinate Position)
{
    ValidateCubicPath(Path);
    for (float Coordinate : Position) if (!std::isfinite(Coordinate)) throw std::runtime_error("Nonfinite curve projection.");
    CubicSegmentProjection Projection{0, 0, std::numeric_limits<float>::max()};
    const size_t Count = Path.Closed ? Path.Anchors.size() : Path.Anchors.size() - 1;
    for (size_t Segment = 0; Segment < Count; ++Segment)
    {
        const auto &First = Path.Anchors[Segment], &Second = Path.Anchors[(Segment + 1) % Path.Anchors.size()];
        const auto Distance = [&](float Parameter)
        {
            const auto A = Interpolate(First.Position, First.Outgoing, Parameter);
            const auto B = Interpolate(First.Outgoing, Second.Incoming, Parameter);
            const auto C = Interpolate(Second.Incoming, Second.Position, Parameter);
            const auto Point = Interpolate(Interpolate(A, B, Parameter), Interpolate(B, C, Parameter), Parameter);
            return std::hypot(Point[0] - Position[0], Point[1] - Position[1]);
        };
        // 📝 Bounded approximate closest-point search; refine each sampled local minimum, including endpoints.
        float Previous = Distance(0);
        if (Previous < Projection.Distance) Projection = {Segment, 0, Previous};
        for (int Sample = 1; Sample <= 128; ++Sample)
        {
            const float Parameter = float(Sample) / 128;
            const float Current = Distance(Parameter);
            if (Current < Projection.Distance) Projection = {Segment, Parameter, Current};
            if (Sample < 128 && Current <= Previous && Current <= Distance(Parameter + 1.f / 128))
            {
                float Minimum = Parameter - 1.f / 128, Maximum = Parameter + 1.f / 128;
                for (int Iteration = 0; Iteration < 24; ++Iteration)
                {
                    const float Left = Minimum + (Maximum - Minimum) / 3, Right = Maximum - (Maximum - Minimum) / 3;
                    if (Distance(Left) < Distance(Right)) Maximum = Right; else Minimum = Left;
                }
                const float Refined = (Minimum + Maximum) * .5f, Separation = Distance(Refined);
                if (Separation < Projection.Distance) Projection = {Segment, Refined, Separation};
            }
            Previous = Current;
        }
    }
    return Projection;
}

void BendCubicSegment(CubicPath &Path, size_t SegmentIndex, float Parameter, Coordinate Displacement)
{
    ValidateCubicPath(Path);
    const size_t Count = Path.Closed ? Path.Anchors.size() : Path.Anchors.size() - 1;
    if (SegmentIndex >= Count || !std::isfinite(Parameter) || Parameter < .02f || Parameter > .98f)
        throw std::runtime_error("Bend an interior cubic segment, not an endpoint.");
    auto Replacement = Path;
    auto &First = Replacement.Anchors[SegmentIndex], &Second = Replacement.Anchors[(SegmentIndex + 1) % Replacement.Anchors.size()];
    const float IncomingWeight = 3 * (1 - Parameter) * Parameter * Parameter;
    const float OutgoingWeight = 3 * (1 - Parameter) * (1 - Parameter) * Parameter;
    const float Denominator = IncomingWeight * IncomingWeight + OutgoingWeight * OutgoingWeight;
    // 📝 Minimum squared control displacement moves this parameter exactly while keeping both anchors fixed.
    for (size_t Axis = 0; Axis < 2; ++Axis)
    {
        First.Outgoing[Axis] += Displacement[Axis] * OutgoingWeight / Denominator;
        Second.Incoming[Axis] += Displacement[Axis] * IncomingWeight / Denominator;
    }
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

namespace
{
void ValidateAnchorIndices(const CubicPath &Path, const std::vector<size_t> &Indices)
{
    ValidateCubicPath(Path);
    if (Indices.empty()) throw std::runtime_error("Select at least one cubic anchor.");
    std::vector<bool> Included(Path.Anchors.size());
    for (size_t Index : Indices)
    {
        if (Index >= Included.size() || Included[Index]) throw std::runtime_error("Invalid or repeated cubic anchor index.");
        Included[Index] = true;
    }
}
}

void TranslateCubicAnchors(CubicPath &Path, const std::vector<size_t> &Indices, Coordinate Displacement)
{
    ValidateAnchorIndices(Path, Indices);
    auto Replacement = Path;
    for (size_t Index : Indices)
        for (auto *Point : {&Replacement.Anchors[Index].Position, &Replacement.Anchors[Index].Incoming, &Replacement.Anchors[Index].Outgoing})
            for (size_t Axis = 0; Axis < 2; ++Axis) (*Point)[Axis] += Displacement[Axis];
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

void PartitionCubicAnchors(CubicPath &Path, const std::vector<size_t> &Indices)
{
    ValidateAnchorIndices(Path, Indices);
    auto Replacement = Path;
    auto Descending = Indices;
    std::sort(Descending.begin(), Descending.end(), std::greater<size_t>());
    for (size_t Index : Descending) Replacement.Anchors.erase(Replacement.Anchors.begin() + ptrdiff_t(Index));
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

void ConfigureAutomaticCubicTangents(CubicPath &Path, const std::vector<size_t> &Indices)
{
    ValidateAnchorIndices(Path, Indices);
    auto Replacement = Path;
    const size_t Count = Path.Anchors.size();
    for (size_t Index : Indices)
    {
        const auto Position = Path.Anchors[Index].Position;
        const auto Previous = Path.Anchors[Index == 0 ? (Path.Closed ? Count - 1 : 0) : Index - 1].Position;
        const auto Next = Path.Anchors[Index + 1 == Count ? (Path.Closed ? 0 : Index) : Index + 1].Position;
        const float IncomingLength = std::hypot(Position[0] - Previous[0], Position[1] - Previous[1]) / 3;
        const float OutgoingLength = std::hypot(Next[0] - Position[0], Next[1] - Position[1]) / 3;
        const float ChordLength = std::hypot(Next[0] - Previous[0], Next[1] - Previous[1]);
        for (size_t Axis = 0; Axis < 2; ++Axis)
        {
            const float Direction = ChordLength > .000001f ? (Next[Axis] - Previous[Axis]) / ChordLength : 0;
            Replacement.Anchors[Index].Incoming[Axis] = Position[Axis] - Direction * IncomingLength;
            Replacement.Anchors[Index].Outgoing[Axis] = Position[Axis] + Direction * OutgoingLength;
        }
    }
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

void SubdivideCubicSegment(CubicPath &Path, size_t SegmentIndex, float Parameter)
{
    ValidateCubicPath(Path);
    const size_t Count = Path.Closed ? Path.Anchors.size() : Path.Anchors.size() - 1;
    if (SegmentIndex >= Count || !std::isfinite(Parameter) || Parameter <= 0 || Parameter >= 1 || Path.Anchors.size() >= 400)
        throw std::runtime_error("Invalid cubic subdivision or anchor budget.");
    auto Replacement = Path;
    auto &First = Replacement.Anchors[SegmentIndex], &Second = Replacement.Anchors[(SegmentIndex + 1) % Replacement.Anchors.size()];
    const auto A = Interpolate(First.Position, First.Outgoing, Parameter), B = Interpolate(First.Outgoing, Second.Incoming, Parameter), C = Interpolate(Second.Incoming, Second.Position, Parameter);
    const auto D = Interpolate(A, B, Parameter), E = Interpolate(B, C, Parameter), F = Interpolate(D, E, Parameter);
    First.Outgoing = A; Second.Incoming = C;
    Replacement.Anchors.insert(Replacement.Anchors.begin() + ptrdiff_t(SegmentIndex + 1), CubicAnchor{F, D, E});
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

void PartitionCubicAnchor(CubicPath &Path, size_t AnchorIndex)
{
    ValidateCubicPath(Path);
    if (AnchorIndex >= Path.Anchors.size() || Path.Anchors.size() <= (Path.Closed ? 3u : 2u))
        throw std::runtime_error("Removing that anchor would leave an invalid path.");
    auto Replacement = Path;
    Replacement.Anchors.erase(Replacement.Anchors.begin() + ptrdiff_t(AnchorIndex));
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}

void ConfigureCubicTangent(CubicPath &Path, size_t AnchorIndex, bool Smooth)
{
    ValidateCubicPath(Path);
    auto Replacement = Path;
    auto &Anchor = Replacement.Anchors.at(AnchorIndex);
    if (!Smooth) Anchor.Incoming = Anchor.Outgoing = Anchor.Position;
    else
    {
        const auto Previous = Path.Anchors[AnchorIndex ? AnchorIndex - 1 : Path.Closed ? Path.Anchors.size() - 1 : 0].Position;
        const auto Following = Path.Anchors[AnchorIndex + 1 < Path.Anchors.size() ? AnchorIndex + 1 : Path.Closed ? 0 : AnchorIndex].Position;
        for (size_t Axis = 0; Axis < 2; ++Axis)
        {
            const float Δ = (Following[Axis] - Previous[Axis]) / 6;
            Anchor.Incoming[Axis] = Anchor.Position[Axis] - Δ; Anchor.Outgoing[Axis] = Anchor.Position[Axis] + Δ;
        }
    }
    ValidateCubicPath(Replacement); Path = std::move(Replacement);
}
}
