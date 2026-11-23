/*============================================================================================================================================
                                                       VECTORAUTHORINGSELECTIONEDITING.CPP
============================================================================================================================================*/
// 🧩 Rebuild only source references, preserving independent selection edits and GPU evaluation without CPU vector rasterization.

#include "VectorAuthoringSelectionEditing.h"
#include <functional>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{
namespace
{
std::unordered_set<uint32_t> AuthenticateSelection(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    if (Selection.empty() || Selection.size() > 64) throw std::runtime_error("Select visible, unlocked vector.");
    std::unordered_set<uint32_t> Tokens;
    for (auto Token : Selection)
    {
        const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; });
        if (Position == Document.Layers.end() || QueryVectorAuthoringProtection(Document, *Position) || !QueryVectorAuthoringVisibility(Document, *Position))
            throw std::runtime_error("VectorAuthoring selection is missing, hidden or protected.");
        Tokens.insert(Token);
    }
    return Tokens;
}

void ReclaimSelectionCurveStorage(VectorAuthoringDocument &Document)
{
    std::vector<bool> Retained(Document.Segments.size());
    const auto Mark = [&](const auto &Source)
    {
        if (Source.Topology[0] != 6) return;
        const size_t Start = Source.Topology[1], Count = Source.Topology[2];
        if (!Count || uint64_t(Start) + Count > Retained.size()) throw std::runtime_error("Invalid copied cubic segment range.");
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
    const auto Relocate = [&](auto &Source)
    {
        if (Source.Topology[0] == 6) Source.Topology[1] = Relocation[Source.Topology[1]];
    };
    for (auto &Source : Document.Instructions) Relocate(Source);
    for (auto &Layer : Document.Layers) if (Layer.Mask) Relocate(*Layer.Mask);
    Document.Segments = std::move(Segments);
}

void TransformSource(VectorCoverageInstruction &Source, const VectorAuthoringSourcePatch &Patch, const std::array<float, 4> &Bounds)
{
    if (Patch.TranslationX != 0 || Patch.TranslationY != 0 || Patch.ScaleX != 1 || Patch.ScaleY != 1 || Patch.Rotation != 0)
    {
        const float Relative = Source.Transform[0] - Patch.ScaleBasis;
        if (Patch.ScaleX != Patch.ScaleY && std::abs(std::sin(2 * Relative)) > 1e-5f)
            throw std::runtime_error("Nonuniform scaling would shear the retained vector source.");
        const float Δx = Source.Bounds[0] - Bounds[0];
        const float Δy = Source.Bounds[1] - Bounds[1];
        const float LocalX = (Δx * std::cos(Patch.ScaleBasis) + Δy * std::sin(Patch.ScaleBasis)) * Patch.ScaleX;
        const float LocalY = (-Δx * std::sin(Patch.ScaleBasis) + Δy * std::cos(Patch.ScaleBasis)) * Patch.ScaleY;
        const float θ = Patch.ScaleBasis + Patch.Rotation;
        Source.Bounds[0] = Bounds[0] + Patch.TranslationX + LocalX * std::cos(θ) - LocalY * std::sin(θ);
        Source.Bounds[1] = Bounds[1] + Patch.TranslationY + LocalX * std::sin(θ) + LocalY * std::cos(θ);
        const bool Orthogonal = std::abs(std::sin(Relative)) > .5f;
        Source.Bounds[2] *= Orthogonal ? Patch.ScaleY : Patch.ScaleX;
        Source.Bounds[3] *= Orthogonal ? Patch.ScaleX : Patch.ScaleY;
        Source.Transform[0] += Patch.Rotation;
    }
    if (Patch.Colour) Source.Colour = *Patch.Colour;
    if (Patch.ColourEnd) Source.ColourEnd = *Patch.ColourEnd;
    if (Patch.StrokeColour)
    {
        Source.StrokeColour = *Patch.StrokeColour;
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
    }
    if (Patch.Opacity) Source.Transform[2] = *Patch.Opacity;
    if (Patch.StrokeWidth) Source.Transform[1] = *Patch.StrokeWidth;
    if (Patch.PaintCategory) Source.Transform[3] = float(*Patch.PaintCategory);
    if (Patch.FillEnabled) Source.Paint[3] = *Patch.FillEnabled ? 1.f : 0.f;
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCE BOUNDS AND RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 4> QueryVectorAuthoringSelectionBounds(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const std::unordered_set<uint32_t> Tokens(Selection.begin(), Selection.end());
    float MinimumX = std::numeric_limits<float>::max(), MinimumY = MinimumX;
    float MaximumX = -MinimumX, MaximumY = MaximumX;
    bool Found = false;
    std::unordered_set<uint32_t> Visited;
    std::function<void(uint32_t)> Accumulate = [&](uint32_t Root)
    {
        if (!Visited.insert(Root).second) return;
        const auto &Source = Document.Instructions.at(Root);
        if (Source.Topology[0] > 6)
        {
            Accumulate(Source.Topology[1]);
            if (Source.Topology[0] != 17 && Source.Topology[0] != 24 && Source.Topology[0] != 25) Accumulate(Source.Topology[2]);
            return;
        }
        Found = true;
        const float θ = Source.Transform[0];
        if (Source.Topology[0] == 6)
        {
            // 📝 Converted library paths normalize their segments to the tile frame, so the stored bounds are
            // tile-sized even for small motifs. Trace the true contour through the live mapping instead, so the
            // selection box hugs the shape rather than the canvas.
            const size_t Start = Source.Topology[1], Count = Source.Topology[2];
            if (Count && Count <= Document.Segments.size() && Start <= Document.Segments.size() - Count &&
                Source.Bounds[2] > 0 && Source.Bounds[3] > 0)
            {
                const float Cosine = std::cos(θ), Sine = std::sin(θ);
                bool Traced = false;
                for (size_t Index = 0; Index < Count; ++Index)
                {
                    const auto &Segment = Document.Segments[Start + Index];
                    for (const auto *Quad : {&Segment.StartControl, &Segment.ControlEnd})
                        for (int Point = 0; Point < 2; ++Point)
                        {
                            const float X = Source.Bounds[0] + (*Quad)[Point * 2] * Source.Bounds[2] * Cosine -
                                (*Quad)[Point * 2 + 1] * Source.Bounds[3] * Sine;
                            const float Y = Source.Bounds[1] + (*Quad)[Point * 2] * Source.Bounds[2] * Sine +
                                (*Quad)[Point * 2 + 1] * Source.Bounds[3] * Cosine;
                            if (!std::isfinite(X) || !std::isfinite(Y)) continue;
                            MinimumX = std::min(MinimumX, X);
                            MaximumX = std::max(MaximumX, X);
                            MinimumY = std::min(MinimumY, Y);
                            MaximumY = std::max(MaximumY, Y);
                            Traced = true;
                        }
                }
                if (Traced) return;
            }
        }
        const float Horizontal = (std::abs(std::cos(θ)) * Source.Bounds[2] + std::abs(std::sin(θ)) * Source.Bounds[3]) * .5f;
        const float Vertical = (std::abs(std::sin(θ)) * Source.Bounds[2] + std::abs(std::cos(θ)) * Source.Bounds[3]) * .5f;
        MinimumX = std::min(MinimumX, Source.Bounds[0] - Horizontal);
        MaximumX = std::max(MaximumX, Source.Bounds[0] + Horizontal);
        MinimumY = std::min(MinimumY, Source.Bounds[1] - Vertical);
        MaximumY = std::max(MaximumY, Source.Bounds[1] + Vertical);
    };
    for (const auto &Layer : Document.Layers) if (Tokens.contains(Layer.Token)) Accumulate(Layer.Root);
    if (!Found) throw std::runtime_error("VectorAuthoring bounds need an authenticated selection.");
    // 📝 Tile-wrapped library layers repeat their motifs off-canvas; the gesture frame hugs the visible artwork
    // so handles stay on the board. Fully off-canvas selections keep their true union so they stay editable.
    const float VisibleMinimumX = std::max(MinimumX, 0.f), VisibleMaximumX = std::min(MaximumX, 512.f);
    const float VisibleMinimumY = std::max(MinimumY, 0.f), VisibleMaximumY = std::min(MaximumY, 512.f);
    if (VisibleMinimumX <= VisibleMaximumX && VisibleMinimumY <= VisibleMaximumY)
        return {(VisibleMinimumX + VisibleMaximumX) * .5f, (VisibleMinimumY + VisibleMaximumY) * .5f,
            VisibleMaximumX - VisibleMinimumX, VisibleMaximumY - VisibleMinimumY};
    return {(MinimumX + MaximumX) * .5f, (MinimumY + MaximumY) * .5f, MaximumX - MinimumX, MaximumY - MinimumY};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION GESTURE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

VectorSelectionProgram ConstructVectorSelectionProgram(const VectorAuthoringDocument &Document)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    VectorSelectionProgram Selection{{Document.Instructions, Document.Segments, 0}, {}};
    for (const auto &Layer : Document.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Document, Layer) || QueryVectorAuthoringProtection(Document, Layer)) continue;
        uint32_t Root = Layer.Root;
        if (Layer.Mask && Layer.MaskEnabled)
        {
            const uint32_t Mask = uint32_t(Selection.Coverage.Instructions.size());
            Selection.Coverage.Instructions.push_back(*Layer.Mask);
            VectorCoverageInstruction Operation;
            Operation.Topology = {Layer.MaskLuminance ? 25u : 24u, Root, Mask, Layer.MaskInverted ? 1u : 0u};
            Root = uint32_t(Selection.Coverage.Instructions.size());
            Selection.Coverage.Instructions.push_back(Operation);
        }
        Selection.Candidates.push_back({Root, Layer.Token});
    }
    if (Selection.Coverage.Instructions.empty())
    {
        VectorCoverageInstruction Transparent;
        Transparent.Transform[2] = 0;
        Selection.Coverage.Instructions.push_back(Transparent);
    }
    ValidateVectorSelection(Selection);
    return Selection;
}

VectorSelectionExtent QueryVectorSelectionExtent(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    VectorSelectionExtent Extent{QueryVectorAuthoringSelectionBounds(Document, Selection), 0};
    if (Selection.size() == 1)
    {
        const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
        if (Position != Document.Layers.end())
        {
            const auto &Source = Document.Instructions.at(Position->Root);
            if (Source.Topology[0] < 6) Extent = {Source.Bounds, Source.Transform[0]};
        }
    }
    return Extent;
}

std::vector<uint32_t> QueryVectorSelectionRegion(
    const VectorAuthoringDocument &Document,
    std::array<float, 2>           Start,
    std::array<float, 2>           Finish,
    std::span<const uint32_t>      Existing)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    for (float Coordinate : {Start[0], Start[1], Finish[0], Finish[1]})
        if (!std::isfinite(Coordinate)) throw std::runtime_error("Nonfinite vector selection region.");
    std::vector<uint32_t> Selection;
    for (const auto &Layer : Document.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Document, Layer) || QueryVectorAuthoringProtection(Document, Layer)) continue;
        const std::array<uint32_t, 1> Token{Layer.Token};
        const auto Bounds = QueryVectorAuthoringSelectionBounds(Document, Token);
        const bool Intersects = Bounds[0] + Bounds[2] * .5f >= std::min(Start[0], Finish[0]) &&
            Bounds[0] - Bounds[2] * .5f <= std::max(Start[0], Finish[0]) &&
            Bounds[1] + Bounds[3] * .5f >= std::min(Start[1], Finish[1]) &&
            Bounds[1] - Bounds[3] * .5f <= std::max(Start[1], Finish[1]);
        if (Intersects || std::find(Existing.begin(), Existing.end(), Layer.Token) != Existing.end()) Selection.push_back(Layer.Token);
    }
    return Selection;
}

VectorAuthoringSourcePatch EvaluateVectorSelectionResize(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<int, 2>             Direction,
    std::array<float, 2>           Pointer,
    bool                          Aspect,
    bool                          Centred)
{
    const auto Tokens = AuthenticateSelection(Document, Selection);
    if ((Direction[0] == 0 && Direction[1] == 0) || Direction[0] < -1 || Direction[0] > 1 || Direction[1] < -1 || Direction[1] > 1)
        throw std::runtime_error("Invalid vector resize direction.");
    if (!std::isfinite(Pointer[0]) || !std::isfinite(Pointer[1])) throw std::runtime_error("Nonfinite vector resize pointer.");
    const auto Extent = QueryVectorSelectionExtent(Document, Selection);
    const auto &Bounds = Extent.Bounds;
    const float θ = Extent.Rotation;
    const float Δx = Pointer[0] - Bounds[0], Δy = Pointer[1] - Bounds[1];
    const std::array<float, 2> Local{Δx * std::cos(θ) + Δy * std::sin(θ), -Δx * std::sin(θ) + Δy * std::cos(θ)};
    std::array<float, 2> Fixed{}, Length{Bounds[2], Bounds[3]};
    for (size_t Axis = 0; Axis < 2; ++Axis)
    {
        Fixed[Axis] = Centred ? 0.f : -float(Direction[Axis]) * Bounds[Axis + 2] * .5f;
        if (Direction[Axis]) Length[Axis] = std::max(1.f, (Local[Axis] - Fixed[Axis]) * float(Direction[Axis]) * (Centred ? 2.f : 1.f));
    }
    bool Uniform = Aspect;
    float MaximumX = std::numeric_limits<float>::max(), MaximumY = MaximumX;
    std::unordered_set<uint32_t> Visited;
    const auto Inspect = [&](const VectorCoverageInstruction &Source)
    {
        const float Relative = Source.Transform[0] - θ;
        if (std::abs(std::sin(2 * Relative)) > 1e-5f) Uniform = true;
        const bool Orthogonal = std::abs(std::sin(Relative)) > .5f;
        MaximumX = std::min(MaximumX, 1024.f / std::max(1.f, Source.Bounds[Orthogonal ? 3 : 2]));
        MaximumY = std::min(MaximumY, 1024.f / std::max(1.f, Source.Bounds[Orthogonal ? 2 : 3]));
    };
    std::function<void(uint32_t)> InspectSource = [&](uint32_t Root)
    {
        if (!Visited.insert(Root).second) return;
        const auto &Source = Document.Instructions.at(Root);
        if (Source.Topology[0] <= 6) Inspect(Source);
        else { InspectSource(Source.Topology[1]); InspectSource(Source.Topology[2]); }
    };
    for (const auto &Layer : Document.Layers) if (Tokens.contains(Layer.Token))
    {
        InspectSource(Layer.Root);
        if (Layer.Mask) Inspect(*Layer.Mask);
    }
    float ScaleX = Length[0] / Bounds[2];
    float ScaleY = Length[1] / Bounds[3];
    if (Uniform)
    {
        const float Ratio = Direction[0] && Direction[1] ? std::max(ScaleX, ScaleY) : Direction[0] ? ScaleX : ScaleY;
        const float Minimum = std::max(1.f / Bounds[2], 1.f / Bounds[3]);
        const float Maximum = std::min(MaximumX, MaximumY);
        if (Minimum > Maximum) throw std::runtime_error("Selection aspect cannot fit the finite resize interval.");
        ScaleX = ScaleY = std::clamp(Ratio, Minimum, Maximum);
    }
    else { ScaleX = std::min(ScaleX, MaximumX); ScaleY = std::min(ScaleY, MaximumY); }
    if (std::abs(ScaleX - 1) < 1e-6f) ScaleX = 1;
    if (std::abs(ScaleY - 1) < 1e-6f) ScaleY = 1;
    const float OffsetX = Centred || !Direction[0] ? 0.f : Fixed[0] + float(Direction[0]) * Bounds[2] * ScaleX * .5f;
    const float OffsetY = Centred || !Direction[1] ? 0.f : Fixed[1] + float(Direction[1]) * Bounds[3] * ScaleY * .5f;
    VectorAuthoringSourcePatch Patch;
    Patch.ScaleX = ScaleX; Patch.ScaleY = ScaleY; Patch.ScaleBasis = θ;
    Patch.TranslationX = OffsetX * std::cos(θ) - OffsetY * std::sin(θ);
    Patch.TranslationY = OffsetX * std::sin(θ) + OffsetY * std::cos(θ);
    return Patch;
}

VectorAuthoringSourcePatch EvaluateVectorSelectionRotation(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<float, 2>           Start,
    std::array<float, 2>           Pointer,
    bool                          Constrained)
{
    (void)AuthenticateSelection(Document, Selection);
    for (float Coordinate : {Start[0], Start[1], Pointer[0], Pointer[1]})
        if (!std::isfinite(Coordinate)) throw std::runtime_error("Nonfinite vector rotation pointer.");
    const auto Extent = QueryVectorSelectionExtent(Document, Selection);
    const auto &Bounds = Extent.Bounds;
    constexpr float π = std::numbers::pi_v<float>;
    float Δθ = std::atan2(Pointer[1] - Bounds[1], Pointer[0] - Bounds[0]) - std::atan2(Start[1] - Bounds[1], Start[0] - Bounds[0]);
    Δθ = std::remainder(Δθ, 2 * π);
    const float Step = (Constrained ? 15.f : 1.f) * π / 180;
    VectorAuthoringSourcePatch Patch;
    Patch.Rotation = std::round((Extent.Rotation + Δθ) / Step) * Step - Extent.Rotation;
    return Patch;
}

void ReclaimVectorAuthoringSources(VectorAuthoringDocument &Document)
{
    auto Replacement = Document;
    Replacement.Instructions.clear();
    std::unordered_map<uint32_t, uint32_t> Remapping;
    std::function<uint32_t(uint32_t)> Retain = [&](uint32_t Root)
    {
        if (const auto Position = Remapping.find(Root); Position != Remapping.end()) return Position->second;
        if (Root >= Document.Instructions.size()) throw std::runtime_error("VectorAuthoring source reference is invalid.");
        auto Source = Document.Instructions[Root];
        if (Source.Topology[0] > 6)
        {
            if (Source.Topology[1] >= Root || Source.Topology[2] >= Root) throw std::runtime_error("VectorAuthoring expression is cyclic.");
            Source.Topology[1] = Retain(Source.Topology[1]);
            Source.Topology[2] = Retain(Source.Topology[2]);
        }
        const auto Token = uint32_t(Replacement.Instructions.size());
        Remapping.emplace(Root, Token);
        Replacement.Instructions.push_back(Source);
        return Token;
    };
    for (auto &Layer : Replacement.Layers) Layer.Root = Retain(Layer.Root);
    (void)ConstructVectorAuthoringCoverage(Replacement);
    Document = std::move(Replacement);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ATOMIC SELECTION EDITS
//------------------------------------------------------------------------------------------------------------------------

void TransformVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringSourcePatch &Patch)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Tokens = AuthenticateSelection(Document, Selection);
    for (float Quantity : {Patch.TranslationX, Patch.TranslationY, Patch.ScaleX, Patch.ScaleY, Patch.Rotation, Patch.ScaleBasis})
        if (!std::isfinite(Quantity)) throw std::runtime_error("Nonfinite vector transform.");
    if (Patch.ScaleX <= 0 || Patch.ScaleY <= 0) throw std::runtime_error("VectorAuthoring scale must be positive.");
    if (Patch.TranslationX == 0 && Patch.TranslationY == 0 && Patch.ScaleX == 1 && Patch.ScaleY == 1 && Patch.Rotation == 0 &&
        !Patch.Colour && !Patch.ColourEnd && !Patch.StrokeColour && !Patch.Opacity && !Patch.StrokeWidth && !Patch.PaintCategory && !Patch.FillEnabled) return;
    const auto Bounds = QueryVectorAuthoringSelectionBounds(Document, Selection);
    auto Replacement = Document;
    VectorAuthoringSourcePatch Geometry = Patch;
    Geometry.Colour.reset(); Geometry.ColourEnd.reset(); Geometry.StrokeColour.reset(); Geometry.Opacity.reset();
    Geometry.StrokeWidth.reset(); Geometry.PaintCategory.reset(); Geometry.FillEnabled.reset();
    std::unordered_map<uint64_t, uint32_t> Remapping;
    std::function<uint32_t(uint32_t, bool)> Clone = [&](uint32_t Root, bool MaskOnly)
    {
        const auto Correlation = uint64_t(Root) * 2 + uint64_t(MaskOnly);
        if (const auto Position = Remapping.find(Correlation); Position != Remapping.end()) return Position->second;
        auto Source = Document.Instructions[Root];
        if (Source.Topology[0] > 6)
        {
            Source.Topology[1] = Clone(Source.Topology[1], MaskOnly);
            Source.Topology[2] = Clone(Source.Topology[2], MaskOnly || Source.Topology[0] == 24 || Source.Topology[0] == 25);
        }
        else TransformSource(Source, MaskOnly ? Geometry : Patch, Bounds);
        const auto Token = uint32_t(Replacement.Instructions.size());
        Replacement.Instructions.push_back(Source);
        Remapping.emplace(Correlation, Token);
        return Token;
    };
    for (auto &Layer : Replacement.Layers)
    {
        if (!Tokens.contains(Layer.Token)) continue;
        if (Patch.Opacity && Document.Instructions[Layer.Root].Topology[0] > 6)
            throw std::runtime_error("Expression-wide opacity requires an authored layer-opacity port.");
        Layer.Root = Clone(Layer.Root, false);
        if (Layer.Mask) TransformSource(*Layer.Mask, Geometry, Bounds);
    }
    ReclaimVectorAuthoringSources(Replacement);
    Document = std::move(Replacement);
}

uint32_t ConstructVectorAuthoringPrimitive(VectorAuthoringDocument &Document, VectorCoverageInstruction Source, std::string Caption)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    if (Source.Topology[0] > 6 || Source.Topology[0] == 6) throw std::runtime_error("Choose a supported parametric primitive.");
    auto Replacement = Document;
    ReclaimVectorAuthoringSources(Replacement);
    if (Replacement.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
    VectorAuthoringLayer Layer;
    Layer.Token = ++Replacement.TokenClock;
    Layer.Root = uint32_t(Replacement.Instructions.size());
    Layer.Name = std::move(Caption);
    Replacement.Instructions.push_back(Source);
    Replacement.Layers.push_back(Layer);
    NormalizeVectorAuthoringCaptions(Replacement);
    (void)ConstructVectorAuthoringCoverage(Replacement);
    Document = std::move(Replacement);
    return Layer.Token;
}

void PartitionVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Tokens = AuthenticateSelection(Document, Selection);
    auto Replacement = Document;
    std::erase_if(Replacement.Layers, [&](const auto &Layer) { return Tokens.contains(Layer.Token); });
    ReclaimVectorAuthoringSources(Replacement);
    Document = std::move(Replacement);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION TRANSFER AND STACK ORDER
//------------------------------------------------------------------------------------------------------------------------

VectorAuthoringDocument ExtractVectorAuthoringSelection(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    if (Selection.empty() || Selection.size() > 64) throw std::runtime_error("Copy needs 1–64 selected vectors.");
    const std::unordered_set<uint32_t> Tokens(Selection.begin(), Selection.end());
    auto Fragment = Document;
    std::erase_if(Fragment.Layers, [&](const auto &Layer) { return !Tokens.contains(Layer.Token); });
    if (Fragment.Layers.size() != Tokens.size()) throw std::runtime_error("Copied selection contains missing vector tokens.");
    std::unordered_set<uint32_t> Enclosures;
    for (const auto &Layer : Fragment.Layers)
        for (uint32_t Token = Layer.EnclosureToken; Token;)
        {
            Enclosures.insert(Token);
            const auto Position = std::find_if(Document.Enclosures.begin(), Document.Enclosures.end(),
                [Token](const auto &Entry) { return Entry.Token == Token; });
            Token = Position->EnclosureToken;
        }
    std::erase_if(Fragment.Enclosures, [&](const auto &Entry) { return !Enclosures.contains(Entry.Token); });
    ReclaimVectorAuthoringSources(Fragment);
    ReclaimSelectionCurveStorage(Fragment);
    (void)ConstructVectorAuthoringCoverage(Fragment);
    return Fragment;
}

std::vector<uint32_t> IntegrateVectorAuthoringSelection(
    VectorAuthoringDocument       &Document,
    const VectorAuthoringDocument &Fragment,
    std::array<float, 2>           Displacement)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    (void)ConstructVectorAuthoringCoverage(Fragment);
    if (Fragment.Layers.empty()) throw std::runtime_error("The vector clipboard is empty.");
    for (float Coordinate : Displacement) if (!std::isfinite(Coordinate)) throw std::runtime_error("Nonfinite paste displacement.");
    auto Replacement = Document;
    ReclaimVectorAuthoringSources(Replacement);
    ReclaimSelectionCurveStorage(Replacement);
    if (Replacement.Layers.size() + Fragment.Layers.size() > (Document.Stamp ? 24u : 64u)
        || Replacement.Enclosures.size() + Fragment.Enclosures.size() > 64
        || Replacement.Instructions.size() + Fragment.Instructions.size() > 128
        || Replacement.Segments.size() + Fragment.Segments.size() > 32768)
        throw std::runtime_error("Paste exceeds vector layer, group, instruction or curve storage limits.");
    const auto IssueToken = [&]()
    {
        if (Replacement.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
        return ++Replacement.TokenClock;
    };
    std::unordered_map<uint32_t, uint32_t> Enclosures;
    for (const auto &Entry : Fragment.Enclosures) Enclosures.emplace(Entry.Token, IssueToken());
    for (auto Entry : Fragment.Enclosures)
    {
        Entry.Token = Enclosures.at(Entry.Token);
        Entry.LockEnabled = false;
        if (Entry.EnclosureToken) Entry.EnclosureToken = Enclosures.at(Entry.EnclosureToken);
        Replacement.Enclosures.push_back(std::move(Entry));
    }
    const auto InstructionOffset = uint32_t(Replacement.Instructions.size());
    const auto SegmentOffset = uint32_t(Replacement.Segments.size());
    const auto TranslatePrimitive = [&](auto &Source)
    {
        Source.Bounds[0] += Displacement[0]; Source.Bounds[1] += Displacement[1];
        if (Source.Topology[0] == 6) Source.Topology[1] += SegmentOffset;
    };
    Replacement.Segments.insert(Replacement.Segments.end(), Fragment.Segments.begin(), Fragment.Segments.end());
    for (auto Source : Fragment.Instructions)
    {
        if (Source.Topology[0] > 6)
        {
            Source.Topology[1] += InstructionOffset; Source.Topology[2] += InstructionOffset;
        }
        else TranslatePrimitive(Source);
        Replacement.Instructions.push_back(Source);
    }
    const auto FoldCaption = [](std::string Caption)
    {
        for (auto &Letter : Caption) if (Letter >= 'A' && Letter <= 'Z') Letter = char(Letter + 32);
        return Caption;
    };
    std::unordered_set<std::string> Captions;
    for (const auto &Layer : Replacement.Layers) Captions.insert(FoldCaption(Layer.Name));
    std::vector<uint32_t> Tokens;
    for (auto Layer : Fragment.Layers)
    {
        Layer.Token = IssueToken(); Layer.Root += InstructionOffset;
        Layer.Locked = false;
        auto Boundary = std::min(Layer.Name.size(), size_t(52));
        while (Boundary < Layer.Name.size() && (static_cast<unsigned char>(Layer.Name[Boundary]) & 0xc0) == 0x80) --Boundary;
        const auto Stem = Layer.Name.substr(0, Boundary);
        Layer.Name = Stem + " copy";
        for (uint32_t Ordinal = 2; !Captions.insert(FoldCaption(Layer.Name)).second; ++Ordinal)
            Layer.Name = Stem + " copy " + std::to_string(Ordinal);
        if (Layer.EnclosureToken) Layer.EnclosureToken = Enclosures.at(Layer.EnclosureToken);
        if (Layer.Mask) TranslatePrimitive(*Layer.Mask);
        Tokens.push_back(Layer.Token);
        Replacement.Layers.push_back(std::move(Layer));
    }
    (void)ConstructVectorAuthoringCoverage(Replacement);
    Document = std::move(Replacement);
    return Tokens;
}

void ReorderVectorAuthoringSelection(
    VectorAuthoringDocument   &Document,
    std::span<const uint32_t>  Selection,
    VectorSelectionOrder      Order)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Tokens = AuthenticateSelection(Document, Selection);
    if (uint32_t(Order) > uint32_t(VectorSelectionOrder::Front)) throw std::runtime_error("Invalid vector stacking direction.");
    auto Replacement = Document;
    const auto Selected = [&](const auto &Layer) { return Tokens.contains(Layer.Token); };
    if (Order == VectorSelectionOrder::Back || Order == VectorSelectionOrder::Front)
    {
        const bool SelectedFirst = Order == VectorSelectionOrder::Back;
        std::vector<VectorAuthoringLayer> ReorderedLayers;
        ReorderedLayers.reserve(Replacement.Layers.size());
        for (size_t Pass = 0; Pass < 2; ++Pass)
        {
            const bool PartitionSelected = Pass == 0 ? SelectedFirst : !SelectedFirst;
            for (const auto &Layer : Replacement.Layers)
                if (Selected(Layer) == PartitionSelected) ReorderedLayers.push_back(Layer);
        }
        Replacement.Layers = std::move(ReorderedLayers);
    }
    else if (Order == VectorSelectionOrder::Backward)
    {
        for (size_t Index = 1; Index < Replacement.Layers.size(); ++Index)
            if (Selected(Replacement.Layers[Index]) && !Selected(Replacement.Layers[Index - 1]))
                std::swap(Replacement.Layers[Index], Replacement.Layers[Index - 1]);
    }
    else
    {
        for (size_t Index = Replacement.Layers.size() - 1; Index > 0; --Index)
            if (Selected(Replacement.Layers[Index - 1]) && !Selected(Replacement.Layers[Index]))
                std::swap(Replacement.Layers[Index - 1], Replacement.Layers[Index]);
    }
    Document = std::move(Replacement);
}

uint32_t IntegrateVectorAuthoringBoolean(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, VectorCoverageCategory Category)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Tokens = AuthenticateSelection(Document, Selection);
    if (Tokens.size() < 2 || Tokens.size() > 16 || uint32_t(Category) < 16 || uint32_t(Category) > 19)
        throw std::runtime_error("Boolean operations require 2–16 editable vectors.");
    auto Replacement = Document;
    std::optional<size_t> Destination;
    for (size_t Index = 0; Index < Replacement.Layers.size(); ++Index)
    {
        if (!Tokens.contains(Replacement.Layers[Index].Token)) continue;
        if (Replacement.Layers[Index].Mask) throw std::runtime_error("Retain masks separately before combining native expressions.");
        if (!Destination) { Destination = Index; continue; }
        VectorCoverageInstruction Operation;
        Operation.Topology = {uint32_t(Category), Replacement.Layers[*Destination].Root, Replacement.Layers[Index].Root, 0};
        Replacement.Layers[*Destination].Root = uint32_t(Replacement.Instructions.size());
        Replacement.Instructions.push_back(Operation);
    }
    auto &DestinationLayer = Replacement.Layers[*Destination];
    const auto Token = DestinationLayer.Token;
    DestinationLayer.Name = "Boolean motif";
    std::erase_if(Replacement.Layers, [&](const auto &Layer) { return Layer.Token != Token && Tokens.contains(Layer.Token); });
    ReclaimVectorAuthoringSources(Replacement);
    Document = std::move(Replacement);
    return Token;
}
}
