//============================================================================================================================================
//                                                          VECTORINTERCHANGE.H
//============================================================================================================================================
// 🧩 Vector outlines and typeface outlines as one thing — the accepted subset, and every refusal positioned.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/CurveSolver/Api/CurveSolver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE FILL RULE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which rule decides a closed path's interior.
/// note  Both are accepted — `52` §2. The rule is a declared property of each closed path and is read by
///        `ResolveContainment`, which takes it as the one bit the classification depends on.
/// tag   guarantee
enum class FillRule : std::uint32_t
{
    NonZero  = 0u,   // [-] - interior where the winding number is not zero
    EvenOdd  = 1u    // [-] - interior where the crossing count is odd
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE CLOSED PATH
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One path of an outline — its origin, its segment run, and whether it closes.
/// note  🔴 An open path is retained as open rather than closed on arrival. `52` §1's codec translates and does
///        nothing else, and closing a path silently changes which side of it the interior is on.
/// tag   owning
struct OutlinePath
{
    PlanarPosition            Origin       = {};                // [-] - where the run begins
    std::vector<PathSegment>  Segments     = {};                // [-] - in traversal order
    FillRule                  Rule         = FillRule::NonZero; // [-] - declared per path
    bool                      ClosedRun    = false;             // [-] - closes back on its origin
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE OUTLINE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A whole vector source — its paths, its declared colour, and where it came from.
/// note  🔴 `52` §1: a file source and a supplied-text source produce the **identical** specification, and nothing
///        downstream can tell which was used. What differs is only which of the two retentions below is occupied.
/// note  🔴 A supplied-text source retains its text, because there is no file to re-read. A source whose only copy
///        was a clipboard is unrecoverable after a reopen, and the artist reads that as the document having lost
///        their work — `52` §1.
/// tag   owning
struct OutlineSpecification
{
    std::vector<OutlinePath>  Paths          = {};   // [-] - in declaration order
    ColourSpecification       DeclaredColour = {};   // [-] - carries its space, per `36` §1
    std::string               OriginPath     = {};   // [-] - occupied by the file route only
    std::string               SourceText     = {};   // [-] - occupied by the supplied-text route only
    bool                      ColourDeclared = false; // [-] - the source declared one at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      REFUSALS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One rejected construct, named and positioned in the source.
/// note  🔴 `52` §2: a refusal names the construct **and the position in the source**. "Unsupported" with no
///        position sends the artist to search a file they did not write.
/// note  ⚠️ `00` §5.2's rejected set: effect operations, clipping and masking, script and animation, and embedded
///        raster content. Each is rejected rather than approximated, because a vector source that silently loses
///        content is worse than one that refuses it — the artist attributes the loss to their own file.
/// tag   owning
struct RejectedConstruct
{
    std::string    Construct     = {};   // [-] - what the source declared
    std::uint32_t  SourceIndex = 0u;   // [-] - character position, or path ordinal for a file route
    Refusal        Declining     = {};   // [-] - the reason, in static text
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CODEC
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Holds one decoded outline, its refusals, and the flattening both `70` and `82` resolve through.
/// note  🔴 `52` §4: `PlanarClassifier` is Exact and parity-proven, because the same outline is classified on the
///        host for `82`'s preview and on the device for `70`'s resolution. A winding test that disagreed between
///        the two gives a preview with a different silhouette from the result, and the artist attributes the
///        difference to the preview being approximate rather than to the classification being wrong.
/// tag   owning
class VectorInterchange
{
public:

    /// 🧩 Declares a decoded outline incoming from a file.
    /// in    Incoming    [-]  the decoded specification
    /// in    OriginPath  [-]  where it was read from
    /// out   Result     [-]  refuses with ContentUnsupported when no path was declared
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareFromFile(const OutlineSpecification& Incoming, const std::string& OriginPath);

    /// 🧩 Declares a decoded outline incoming as supplied source text.
    /// in    Incoming    [-]  the decoded specification
    /// in    SourceText  [-]  retained, because there is no file to re-read
    /// out   Result     [-]  refuses with ContentUnsupported when no path was declared
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareFromText(const OutlineSpecification& Incoming, const std::string& SourceText);

    /// 🧩 Records one rejected construct, to be reported through `86`.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Refuse(const std::string& Construct, std::uint32_t SourceIndex, const Refusal& Declining);

    /// 🧩 Flattens every path at a tolerance the caller supplies, into one run per path.
    /// in    Tolerance  [-]  greatest permitted deviation, in the outline's own space
    /// out   Flattened  [-]  one polyline per path, in declaration order
    /// note  🔴 Tolerance is **relative to the reduction level being resolved** — `52` §5's gate. `70` resolves an
    ///        outline at whatever level a tile was promoted to, so a fixed tolerance is either wasteful at coarse
    ///        levels or visibly polygonal at fine ones.
    /// note  🔴 The flattening is produced **once** and both the host and the device classify that one polyline.
    ///        `CurveSolver` is Bounded, so flattening twice would give two polylines agreeing to a tolerance and
    ///        differing in their last bit — and an Exact predicate over two different inputs is exact about the
    ///        wrong thing.
    /// cost  🔴
    /// tag   api, nonthrowing
    std::vector<std::vector<PlanarPosition>> Flatten(double Tolerance) const;

    /// 🧩 Classifies one position against a flattened outline.
    /// in    Flattened  [-]  as `Flatten` produced it
    /// in    PointX     [-]  the position being classified
    /// in    PointY     [-]
    /// out   Containment [-] +1 inside, 0 exactly on a boundary, −1 outside
    /// note  🔴 A boundary position resolves to zero and not to inside — `70` resolves coverage from this, and a
    ///        boundary reported as interior gives every outline a one-texel bias outward at its own edge.
    /// note  📝 Where a source declares several paths with differing fill rules, each path is classified under its
    ///        own rule and the results combine by the outermost containment found. A single rule imposed across
    ///        the source would fill the holes of one path or empty the interior of another.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::int32_t Classify(const std::vector<std::vector<PlanarPosition>>& Flattened,
                          double                                         PointX,
                          double                                         PointY) const;

    const OutlineSpecification&           Declared() const;
    const std::vector<RejectedConstruct>&  Refusals() const;
    bool                                  TextRetained() const;

private:

    OutlineSpecification           DeclaredOutline;            // [-] - as decoded, never repaired
    std::vector<RejectedConstruct>  RejectedConstructs;          // [-] - awaiting `86`
    bool                           TextSourceDeclared = false; // [-] - the supplied-text route was taken
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     TYPEFACES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One glyph — its outline, and the metrics that position it.
/// note  🔴 `52` §3: text is resolved to a **glyph sequence** at intake and the glyph sequence is what is stored.
///        The character string is stored beside it so the text stays editable. Storing only characters means every
///        resolution re-runs substitution, and substitution depends on the typeface — so replacing a typeface
///        silently changes the shaping of text the artist already positioned.
/// tag   owning
struct GlyphSpecification
{
    std::uint32_t             GlyphIdentity = 0u;   // [-] - the typeface's own glyph ordinal, not a character
    std::vector<OutlinePath>  Paths         = {};   // [-] - structurally identical to a vector outline's
    double                    Advance       = 0.0;  // [-] - in the typeface's own units
    double                    BearingX  = 0.0;  // [-] - in the typeface's own units
    double                    BearingY = 0.0;  // [-] - in the typeface's own units
};

/// 🧩 One typeface's glyphs, and the resolved text that reads them.
/// note  ⚠️ Whether a typeface is embedded on save or referenced is open in `00` §12 and is not decided here. Both
///        are compatible with this: what is stored is the glyph sequence and the typeface identity either way.
/// tag   owning
class TypefaceInterchange
{
public:

    /// 🧩 Declares the typeface's identity and unit scale.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void DeclareTypeface(std::uint32_t TypefaceIdentity, double UnitsPerEm);

    /// 🧩 Declares one glyph.
    /// out   Result  [-]  refuses with ContentUnsupported when a glyph of that identity is already declared
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareGlyph(const GlyphSpecification& Declaring);

    /// 🧩 Declares one pair adjustment between two adjacent glyphs.
    /// cost  🚩
    /// tag   api, nonthrowing
    void DeclareAdjustment(std::uint32_t EarlierGlyph, std::uint32_t LaterGlyph, double Adjustment);

    /// 🧩 Resolves one glyph, by its identity rather than by a character.
    /// out   Result  [-]  refuses with ContentUnsupported when the typeface declares no such glyph
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<const GlyphSpecification*> ResolveGlyph(std::uint32_t GlyphIdentity) const;

    /// 🧩 The pair adjustment between two adjacent glyphs; zero where none is declared.
    /// cost  🚩
    /// tag   api, nonthrowing
    double Adjustment(std::uint32_t EarlierGlyph, std::uint32_t LaterGlyph) const;

    std::uint32_t TypefaceIdentity() const;
    double        UnitsPerEm() const;
    std::uint32_t GlyphCount() const;

private:

    struct PairAdjustment
    {
        std::uint32_t  EarlierGlyph = 0u;    // [-] - the preceding glyph
        std::uint32_t  LaterGlyph   = 0u;    // [-] - the following one
        double         Adjustment   = 0.0;   // [-] - in the typeface's own units
    };

    std::vector<GlyphSpecification>  DeclaredGlyphs;             // [-] - in declaration order
    std::vector<PairAdjustment>      DeclaredAdjustments;        // [-] - in declaration order
    std::uint32_t                    DeclaredIdentity = 0u;      // [-] - the typeface's identity
    double                           DeclaredUnits    = 1000.0;  // [-] - units per em
};

/// 🧩 Text as it is stored — the glyph sequence, and the characters beside it.
/// note  🔴 Both are stored. The glyph sequence is what resolves; the characters are what keeps the text editable.
///        Storing either alone loses one of the two — `52` §3.
/// tag   owning
struct ResolvedText
{
    std::vector<std::uint32_t>  GlyphSequence    = {};   // [-] - what `70` resolves as outlines
    std::string                 Characters       = {};   // [-] - what the artist edits
    std::uint32_t               TypefaceIdentity = 0u;   // [-] - which typeface shaped it
};

}   // namespace Slate
