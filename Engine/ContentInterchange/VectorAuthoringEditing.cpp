/*============================================================================================================================================
                                                             VECTORAUTHORINGEDITING.CPP
============================================================================================================================================*/
// 🧩 Atomic editable array construction, cycle-bounded enclosures and changed-field channel propagation without CPU rasterization.

#include "VectorAuthoringEditing.h"
#include <functional>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     BOUNDED LOOKUPS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Stamp image components reuse the HTML/MaterialAuthoring "data:image/png;base64,..." shape and
// the same structural bounds (300 kB ceiling, valid PNG signature/IHDR, 1..256 px edges) without
// pulling in MaterialAuthoring.h's wider dependency surface for a single predicate.
bool EmbeddedStampImageValid(std::string_view DataUrl)
{
    constexpr std::string_view Prefix = "data:image/png;base64,";
    if (!DataUrl.starts_with(Prefix) || DataUrl.size() > 300000) return false;
    const std::string_view Encoded = DataUrl.substr(Prefix.size());
    constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<uint8_t, 36> Header{};
    size_t Produced = 0;
    uint32_t Accumulator = 0;
    int Bits = 0;
    for (const char Letter : Encoded.substr(0, 48))
    {
        if (Letter == '=') break;
        const char *Position = std::strchr(Alphabet, Letter);
        if (!Position) return false;
        Accumulator = (Accumulator << 6) | uint32_t(Position - Alphabet);
        Bits += 6;
        if (Bits >= 8)
        {
            Bits -= 8;
            if (Produced < Header.size()) Header[Produced++] = uint8_t((Accumulator >> Bits) & 0xff);
        }
    }
    if (Produced < 24) return false;
    constexpr std::array<uint8_t, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(Signature.begin(), Signature.end(), Header.begin())) return false;
    if (std::string_view(reinterpret_cast<const char *>(Header.data()) + 12, 4) != "IHDR") return false;
    const auto ReadExtent = [&Header](size_t Offset) {
        return (uint32_t(Header[Offset]) << 24) | (uint32_t(Header[Offset + 1]) << 16) |
               (uint32_t(Header[Offset + 2]) << 8) | uint32_t(Header[Offset + 3]);
    };
    const uint32_t Width = ReadExtent(16), Height = ReadExtent(20);
    return Width >= 1 && Width <= 256 && Height >= 1 && Height <= 256;
}

const VectorAuthoringEnclosure &ResolveEnclosure(const VectorAuthoringDocument &Document, uint32_t Token)
{
    const auto Position = std::find_if(Document.Enclosures.begin(), Document.Enclosures.end(),
        [Token](const auto &Entry) { return Entry.Token == Token; });
    if (Position == Document.Enclosures.end()) throw std::runtime_error("Unknown vector enclosure token.");
    return *Position;
}

std::vector<const VectorAuthoringEnclosure *> EnumerateEnclosures(const VectorAuthoringDocument &Document, uint32_t Token)
{
    std::vector<const VectorAuthoringEnclosure *> Sequence;
    while (Token)
    {
        if (Sequence.size() == 12) throw std::runtime_error("VectorAuthoring enclosures contain a cycle or exceed 12 nesting tiers.");
        const auto &Entry = ResolveEnclosure(Document, Token);
        Sequence.push_back(&Entry);
        Token = Entry.EnclosureToken;
    }
    return Sequence;
}

std::vector<size_t> ResolveSelection(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection)
{
    if (Selection.empty() || Selection.size() > 64) throw std::runtime_error("Select bounded vector tokens.");
    std::vector<size_t> Indices;
    for (const auto Token : Selection)
    {
        const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(),
            [Token](const auto &Layer) { return Layer.Token == Token; });
        if (Position == Document.Layers.end()) throw std::runtime_error("Unknown selected vector token.");
        const auto Index = size_t(Position - Document.Layers.begin());
        if (std::find(Indices.begin(), Indices.end(), Index) == Indices.end()) Indices.push_back(Index);
    }
    return Indices;
}

uint32_t IssueVectorAuthoringToken(VectorAuthoringDocument &Document)
{
    if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
    return ++Document.TokenClock;
}

std::string LinearizeCaption(std::string Caption)
{
    // 📝 Locale-independent ASCII folding; retained UTF-8 bytes are never truncated mid-codepoint here.
    for (auto &Letter : Caption)
        if (Letter >= 'A' && Letter <= 'Z') Letter = char(Letter + ('a' - 'A'));
    return Caption;
}

std::string ExtractCaptionStem(const std::string &Caption, size_t MaximumBytes)
{
    auto Boundary = std::min(Caption.size(), MaximumBytes);
    while (Boundary < Caption.size() && (static_cast<unsigned char>(Caption[Boundary]) & 0xc0) == 0x80) --Boundary;
    return Caption.substr(0, Boundary);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY PLACEMENT AND SCALING
//------------------------------------------------------------------------------------------------------------------------

void TransformPrimitive(
    VectorCoverageInstruction   &Instruction,
    const VectorAuthoringArrayPlacement &Location,
    float                        OriginX,
    float                        OriginY,
    float                        κ,
    bool                         RotationEnabled)
{
    // 📝 Preserve untouched sources bit-for-bit; subtracting and re-adding a distant pivot loses small coordinates.
    if (κ == 1 && Location.X == OriginX && Location.Y == OriginY && (!RotationEnabled || Location.Angle == 0)) return;
    const float θ  = RotationEnabled ? Location.Angle * std::numbers::pi_v<float> / 180 : 0;
    const float Δx = (Instruction.Bounds[0] - OriginX) * κ;
    const float Δy = (Instruction.Bounds[1] - OriginY) * κ;
    Instruction.Bounds[0] = Location.X + Δx * std::cos(θ) - Δy * std::sin(θ);
    Instruction.Bounds[1] = Location.Y + Δx * std::sin(θ) + Δy * std::cos(θ);
    Instruction.Bounds[2] *= κ;
    Instruction.Bounds[3] *= κ;
    Instruction.Transform[0] += θ;
    Instruction.Transform[1] *= κ;
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ORGANIZATION AND SELECTION
//------------------------------------------------------------------------------------------------------------------------

void ValidateVectorAuthoringOrganization(const VectorAuthoringDocument &Document)
{
    if (Document.Enclosures.size() > 64) throw std::runtime_error("VectorAuthoring exceeds 64 enclosures.");
    std::unordered_set<uint32_t> Tokens;
    for (const auto &Entry : Document.Enclosures)
    {
        if (!Entry.Token || Entry.Token > Document.TokenClock || Entry.Name.size() > 80 || !Tokens.insert(Entry.Token).second || (Entry.FolderEnabled && Entry.GroupWideEditingEnabled))
            throw std::runtime_error("Invalid vector enclosure identity or category.");
        (void)EnumerateEnclosures(Document, Entry.Token);
    }
    for (const auto &Layer : Document.Layers)
    {
        if (!Tokens.insert(Layer.Token).second) throw std::runtime_error("VectorAuthoring identities must be unique across layers and enclosures.");
        (void)EnumerateEnclosures(Document, Layer.EnclosureToken);
        ValidateVectorAuthoringSurfaceChannels(Layer.SurfaceChannels);
        if (!std::isfinite(Layer.SurfaceRelief) || Layer.SurfaceRelief < -1.f || Layer.SurfaceRelief > 1.f)
            throw std::runtime_error("Vector surface relief exceeds its finite signed interval.");
        if (uint32_t(Layer.SurfaceStructure) > uint32_t(VectorAuthoringSurfaceStructure::Weave) ||
            uint32_t(Layer.TextileGeometry) > uint32_t(VectorAuthoringTextileGeometry::WeaveYarn))
            throw std::runtime_error("Vector textile geometry classification is outside its authored domain.");
        const bool StitchGeometry = Layer.TextileGeometry == VectorAuthoringTextileGeometry::NeedleEntry ||
            Layer.TextileGeometry == VectorAuthoringTextileGeometry::StitchThread ||
            Layer.TextileGeometry == VectorAuthoringTextileGeometry::UpperThreadPass;
        const bool WeaveGeometry = Layer.TextileGeometry == VectorAuthoringTextileGeometry::WeaveYarn;
        if ((StitchGeometry && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Stitch) ||
            (WeaveGeometry && Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Weave))
            throw std::runtime_error("Native textile geometry does not match its stitch/weave surface structure.");
        if (Layer.SurfaceStructure != VectorAuthoringSurfaceStructure::Freeform &&
            (!Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Enabled || Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength <= 0 ||
             !Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled || Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength <= 0))
            throw std::runtime_error("Stitch and weave surfaces require enabled, non-zero Height and Normal contributions.");
        if (uint32_t(Layer.ComponentKind) > uint32_t(VectorAuthoringComponentKind::ImportedImage))
            throw std::runtime_error("Vector component kind is outside its authored domain.");
        if (Layer.ComponentKind != VectorAuthoringComponentKind::Freeform && !Document.Stamp)
            throw std::runtime_error("Text, SVG and image components are a Stamp-only workspace contract.");
        if (Layer.SourceMarkup.size() > VectorAuthoringSourceMarkupLimit)
            throw std::runtime_error("Vector component source markup exceeds its authored bound.");
        if (Layer.ComponentKind == VectorAuthoringComponentKind::ImportedImage && !EmbeddedStampImageValid(Layer.EmbeddedImage))
            throw std::runtime_error("An image component requires a valid bounded embedded PNG.");
        if (Layer.ComponentKind != VectorAuthoringComponentKind::ImportedImage && !Layer.EmbeddedImage.empty())
            throw std::runtime_error("Only image components may carry embedded image bytes.");
    }
}

bool QueryVectorAuthoringEnclosureContains(const VectorAuthoringDocument &Document, uint32_t BoundaryToken, uint32_t EnclosureToken)
{
    if (BoundaryToken == 0) return true;
    const auto Sequence = EnumerateEnclosures(Document, EnclosureToken);
    return std::any_of(Sequence.begin(), Sequence.end(), [BoundaryToken](const auto *Entry) { return Entry->Token == BoundaryToken; });
}

bool QueryVectorAuthoringVisibility(const VectorAuthoringDocument &Document, const VectorAuthoringLayer &Layer)
{
    if (!Layer.Visible) return false;
    const auto Sequence = EnumerateEnclosures(Document, Layer.EnclosureToken);
    return std::all_of(Sequence.begin(), Sequence.end(), [](const auto *Entry) { return Entry->VisibilityEnabled; });
}

bool QueryVectorAuthoringProtection(const VectorAuthoringDocument &Document, const VectorAuthoringLayer &Layer)
{
    if (Layer.Locked) return true;
    const auto Sequence = EnumerateEnclosures(Document, Layer.EnclosureToken);
    return std::any_of(Sequence.begin(), Sequence.end(), [](const auto *Entry) { return Entry->LockEnabled; });
}

void RelocateVectorAuthoringEnclosure(VectorAuthoringDocument &Document, uint32_t EnclosureToken, uint32_t DestinationToken)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    if (EnclosureToken == DestinationToken || QueryVectorAuthoringEnclosureContains(Document, EnclosureToken, DestinationToken))
        throw std::runtime_error("VectorAuthoring enclosure relocation would form a cycle.");
    for (const auto *Entry : EnumerateEnclosures(Document, EnclosureToken))
        if (Entry->LockEnabled) throw std::runtime_error("Protected vector enclosure cannot be relocated.");
    for (const auto *Entry : EnumerateEnclosures(Document, DestinationToken))
        if (Entry->LockEnabled) throw std::runtime_error("Protected vector enclosure cannot receive a relocation.");
    auto Replacement = Document;
    const auto Position = std::find_if(Replacement.Enclosures.begin(), Replacement.Enclosures.end(),
        [EnclosureToken](const auto &Entry) { return Entry.Token == EnclosureToken; });
    Position->EnclosureToken = DestinationToken;
    (void)ConstructVectorAuthoringCoverage(Replacement);
    Document = std::move(Replacement);
}

void NormalizeVectorAuthoringCaptions(VectorAuthoringDocument &Document)
{
    auto Replacement = Document;
    std::unordered_map<std::string, uint32_t> Populations;
    std::unordered_set<std::string> Reserved;
    for (auto &Layer : Replacement.Layers)
    {
        const auto First = Layer.Name.find_first_not_of(" \t\r\n");
        const auto Last  = Layer.Name.find_last_not_of(" \t\r\n");
        Layer.Name = First == std::string::npos ? "VectorAuthoring" : Layer.Name.substr(First, Last - First + 1);
        if (Layer.Name.size() > 80) throw std::runtime_error("VectorAuthoring caption exceeds 80 UTF-8 bytes.");
        ++Populations[LinearizeCaption(Layer.Name)];
    }
    for (const auto &Layer : Replacement.Layers)
        if (Populations.at(LinearizeCaption(Layer.Name)) == 1) Reserved.insert(LinearizeCaption(Layer.Name));
    for (auto &Layer : Replacement.Layers)
    {
        if (Populations.at(LinearizeCaption(Layer.Name)) == 1) continue;
        const auto Stem = ExtractCaptionStem(Layer.Name, 52);
        uint32_t Ordinal = 0;
        std::string Caption;
        do
        {
            ++Ordinal;
            Caption = Stem + " " + (Ordinal < 10 ? "0" : "") + std::to_string(Ordinal);
        }
        while (!Reserved.insert(LinearizeCaption(Caption)).second);
        Layer.Name = std::move(Caption);
    }
    Document = std::move(Replacement);
}

bool ApplyVectorAuthoringChannelPatch(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringChannelPatch &Patch)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Indices = ResolveSelection(Document, Selection);
    ValidateVectorAuthoringChannelPatch(Patch);
    auto Replacement = Document;
    std::unordered_set<uint32_t> Scope;
    for (const auto Index : Indices)
    {
        const auto &Layer = Document.Layers[Index];
        if (QueryVectorAuthoringProtection(Document, Layer)) continue;
        Scope.insert(Layer.Token);
        for (const auto *Entry : EnumerateEnclosures(Document, Layer.EnclosureToken))
        {
            if (!Entry->GroupWideEditingEnabled || Entry->FolderEnabled) continue;
            for (const auto &Candidate : Document.Layers)
                if (QueryVectorAuthoringEnclosureContains(Document, Entry->Token, Candidate.EnclosureToken)) Scope.insert(Candidate.Token);
        }
    }
    for (auto &Layer : Replacement.Layers)
    {
        if (!Scope.contains(Layer.Token) || QueryVectorAuthoringProtection(Document, Layer)) continue;
        (void)ApplyVectorAuthoringSurfacePatch(Layer.SurfaceChannels, Patch);
    }
    (void)ConstructVectorAuthoringCoverage(Replacement);
    if (Replacement == Document) return false;
    Document = std::move(Replacement);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCE-ONLY ARRAY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

VectorAuthoringArrayResult GenerateVectorAuthoringArray(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringArraySpecification &Specification)
{
    (void)ConstructVectorAuthoringCoverage(Document);
    const auto Indices = ResolveSelection(Document, Selection);
    std::unordered_set<size_t> Selected(Indices.begin(), Indices.end());
    std::unordered_set<uint32_t> EnclosureTokens;
    float MinimumX = std::numeric_limits<float>::max(), MinimumY = MinimumX;
    float MaximumX = -MinimumX, MaximumY = MaximumX;
    std::unordered_set<uint32_t> Visited;
    std::function<void(uint32_t)> AccumulateBounds = [&](uint32_t Root)
    {
        if (!Visited.insert(Root).second) return;
        const auto &Instruction = Document.Instructions[Root];
        if (Instruction.Topology[0] > 6)
        {
            AccumulateBounds(Instruction.Topology[1]);
            AccumulateBounds(Instruction.Topology[2]);
            return;
        }
        const float θ = Instruction.Transform[0];
        const float Horizontal = (std::abs(std::cos(θ)) * Instruction.Bounds[2] + std::abs(std::sin(θ)) * Instruction.Bounds[3]) * .5f;
        const float Vertical   = (std::abs(std::sin(θ)) * Instruction.Bounds[2] + std::abs(std::cos(θ)) * Instruction.Bounds[3]) * .5f;
        MinimumX = std::min(MinimumX, Instruction.Bounds[0] - Horizontal);
        MaximumX = std::max(MaximumX, Instruction.Bounds[0] + Horizontal);
        MinimumY = std::min(MinimumY, Instruction.Bounds[1] - Vertical);
        MaximumY = std::max(MaximumY, Instruction.Bounds[1] + Vertical);
    };
    for (const auto Index : Indices)
    {
        const auto &Layer = Document.Layers[Index];
        if (!QueryVectorAuthoringVisibility(Document, Layer) || QueryVectorAuthoringProtection(Document, Layer))
            throw std::runtime_error("Arrays require visible, unlocked vector.");
        AccumulateBounds(Layer.Root);
        for (const auto *Entry : EnumerateEnclosures(Document, Layer.EnclosureToken)) EnclosureTokens.insert(Entry->Token);
    }
    const float OriginX = (MinimumX + MaximumX) * .5f, OriginY = (MinimumY + MaximumY) * .5f;
    const auto Locations = ConstructVectorAuthoringArrayPlacements(Specification, OriginX, OriginY);
    if (Document.Layers.size() - Indices.size() + Indices.size() * Locations.size() > (Document.Stamp ? 24u : 64u) || Document.Enclosures.size() + EnclosureTokens.size() * Locations.size() > 64)
        throw std::runtime_error("Array copies exceed document layer or enclosure budgets.");
    VectorAuthoringArrayResult Result{Document, {}};
    auto &Replacement = Result.Document;
    Replacement.Layers.clear();
    Replacement.Instructions.clear();
    std::unordered_map<uint32_t, uint32_t> RetainedSources;
    // 📝 Clone expression references, never evaluated pixels. Cubic coordinates remain normalized shared source.
    const auto Clone = [&](
        auto                       &&Self,
        uint32_t                     Root,
        auto                        &Remapping,
        const VectorAuthoringArrayPlacement &Location,
        float                        κ,
        bool                         RotationEnabled) -> uint32_t
    {
        if (const auto Position = Remapping.find(Root); Position != Remapping.end()) return Position->second;
        auto Instruction = Document.Instructions[Root];
        if (Instruction.Topology[0] > 6)
        {
            Instruction.Topology[1] = Self(Self, Instruction.Topology[1], Remapping, Location, κ, RotationEnabled);
            Instruction.Topology[2] = Self(Self, Instruction.Topology[2], Remapping, Location, κ, RotationEnabled);
        }
        else TransformPrimitive(Instruction, Location, OriginX, OriginY, κ, RotationEnabled);
        if (Replacement.Instructions.size() == 128) throw std::runtime_error("Array exceeds the 128-instruction GPU coverage budget.");
        const auto Token = uint32_t(Replacement.Instructions.size());
        Replacement.Instructions.push_back(Instruction);
        Remapping.emplace(Root, Token);
        return Token;
    };
    const VectorAuthoringArrayPlacement Identity{OriginX, OriginY, 0, 0};
    for (size_t Index = 0; Index < Document.Layers.size(); ++Index)
    {
        if (Selected.contains(Index)) continue;
        auto Layer = Document.Layers[Index];
        Layer.Root = Clone(Clone, Layer.Root, RetainedSources, Identity, 1, false);
        Replacement.Layers.push_back(std::move(Layer));
    }
    const bool Circular = Specification.Category == VectorAuthoringArrayCategory::Circular || Specification.Category == VectorAuthoringArrayCategory::Radial;
    for (size_t CopyIndex = 0; CopyIndex < Locations.size(); ++CopyIndex)
    {
        const auto &Location = Locations[CopyIndex];
        const float κ = Location.Scale;
        std::unordered_map<uint32_t, uint32_t> SourceTokens, CopiedEnclosures;
        for (const auto &Entry : Document.Enclosures)
            if (EnclosureTokens.contains(Entry.Token)) CopiedEnclosures.emplace(Entry.Token, IssueVectorAuthoringToken(Replacement));
        for (const auto &Entry : Document.Enclosures)
        {
            if (!CopiedEnclosures.contains(Entry.Token)) continue;
            auto Copy = Entry;
            Copy.Token = CopiedEnclosures.at(Entry.Token);
            Copy.EnclosureToken = Entry.EnclosureToken ? CopiedEnclosures.at(Entry.EnclosureToken) : 0;
            Replacement.Enclosures.push_back(std::move(Copy));
        }
        for (const auto Index : Indices)
        {
            auto Layer = Document.Layers[Index];
            auto CaptionBoundary = std::min(Layer.Name.size(), size_t(48));
            while (CaptionBoundary < Layer.Name.size() && (static_cast<unsigned char>(Layer.Name[CaptionBoundary]) & 0xc0) == 0x80) --CaptionBoundary;
            Layer.Name = Layer.Name.substr(0, CaptionBoundary) + " · " + (CopyIndex < 9 ? "0" : "") + std::to_string(CopyIndex + 1);
            Layer.Token = IssueVectorAuthoringToken(Replacement);
            Layer.Root  = Clone(Clone, Layer.Root, SourceTokens, Location, κ, Circular && Specification.RotateCopies);
            if (Layer.Mask) TransformPrimitive(*Layer.Mask, Location, OriginX, OriginY, κ, Circular && Specification.RotateCopies);
            if (Layer.EnclosureToken) Layer.EnclosureToken = CopiedEnclosures.at(Layer.EnclosureToken);
            Result.Selection.push_back(Layer.Token);
            Replacement.Layers.push_back(std::move(Layer));
        }
    }
    NormalizeVectorAuthoringCaptions(Replacement);
    (void)ConstructVectorAuthoringCoverage(Replacement);
    return Result;
}
}
